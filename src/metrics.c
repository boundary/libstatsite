#include <stdlib.h>
#include <string.h>
#include "metrics.h"
#include "set.h"

static int counter_delete_cb(void *data, const char *key, void *value);
static int timer_delete_cb(void *data, const char *key, void *value);
static int set_delete_cb(void *data, const char *key, void *value);
static int gauge_delete_cb(void *data, const char *key, void *value);
static int iter_cb(void *data, const char *key, void *value);

struct cb_info {
	enum metric_type type;
	void *data;
	metric_callback cb;
};

/**
 * Initializes the metrics struct.
 * @arg eps The maximum error for the quantiles
 * @arg quantiles A sorted array of double quantile values, must be on (0, 1)
 * @arg num_quants The number of entries in the quantiles array
 * @arg histograms A radix tree with histogram settings. This is not owned
 * by the metrics object. It is assumed to exist for the life of the metrics.
 * @arg set_precision The precision to use for sets
 * @return 0 on success.
 */
int init_metrics(double timer_eps, double *quantiles, uint32_t num_quants,
		struct radix_tree * histograms, unsigned char set_precision,
		uint64_t set_max_exact, struct metrics * m)
{
	// Copy the inputs
	m->timer_eps = timer_eps;
	m->num_quants = num_quants;
	m->quantiles = malloc(num_quants * sizeof(double));
	memcpy(m->quantiles, quantiles, num_quants * sizeof(double));
	m->histograms = histograms;
	m->set_precision = set_precision;
	m->set_max_exact = set_max_exact;

	// Allocate the hashmaps
	int res = hashmap_init(0, &m->counters);
	if (res)
		return res;
	res = hashmap_init(0, &m->timers);
	if (res)
		return res;
	res = hashmap_init(0, &m->sets);
	if (res)
		return res;
	res = hashmap_init(0, &m->gauges);
	if (res)
		return res;

	// Set the head of our linked list to null
	m->kv_vals = NULL;
	return 0;
}

/**
 * Initializes the metrics struct, with preset configurations.
 * This defaults to a timer epsilon of 0.01 (1% error), and quantiles at
 * 0.5, 0.95, and 0.99. Histograms are disabed, and the set
 * precision is 12 (2% variance).
 * @return 0 on success.
 */
int init_metrics_defaults(struct metrics * m)
{
	double quants[] = { 0.5, 0.95, 0.99 };
	return init_metrics(0.01, (double *)&quants, 3, NULL, 12, 0, m);
}

/**
 * Destroys the metrics
 * @return 0 on success.
 */
int destroy_metrics(struct metrics * m)
{
	// Clear the copied quantiles array
	free(m->quantiles);

	// Nuke all the k/v pairs
	struct key_val *current = m->kv_vals;
	struct key_val *prev = NULL;
	while (current) {
		free(current->name);
		prev = current;
		current = current->next;
		free(prev);
	}

	// Nuke the counters
	hashmap_iter(m->counters, counter_delete_cb, NULL);
	hashmap_destroy(m->counters);

	// Nuke the timers
	hashmap_iter(m->timers, timer_delete_cb, NULL);
	hashmap_destroy(m->timers);

	// Nuke the sets
	hashmap_iter(m->sets, set_delete_cb, NULL);
	hashmap_destroy(m->sets);

	// Nuke the gauges
	hashmap_iter(m->gauges, gauge_delete_cb, NULL);
	hashmap_destroy(m->gauges);

	return 0;
}

int metrics_clear_hash(struct metrics * m, enum metric_type metric_type)
{
	int rc = 0;

	switch (metric_type) {
		case metric_type_GAUGE:
		case metric_type_GAUGE_DELTA:
			hashmap_clear(m->gauges);
			break;
		case metric_type_COUNTER:
			hashmap_clear(m->counters);
			break;
		case metric_type_TIMER:
			hashmap_clear(m->timers);
			break;
		case metric_type_SET:
			hashmap_clear(m->sets);
			break;
		default:
			rc = -1;
			break;
	}
	return rc;
}

/**
 * Increments the counter with the given name
 * by a value.
 * @arg name The name of the counter
 * @arg val The value to add
 * @return 0 on success
 */
static int metrics_increment_counter(struct metrics * m, char *name, double val, double sample_rate)
{
	struct counter *c = hashmap_get_value(m->counters, name);

	// New counter
	if (!c) {
		c = malloc(sizeof(struct counter));
		init_counter(c);
		hashmap_put(m->counters, name, c);
	}
	// Add the sample value
	return counter_add_sample(c, val, sample_rate);
}

/**
 * Adds a new timer sample for the timer with a
 * given name.
 * @arg name The name of the timer
 * @arg val The sample to add
 * @return 0 on success.
 */
static int metrics_add_timer_sample(struct metrics * m, char *name, double val, double sample_rate)
{
	histogram_config *conf;
	struct timer_hist *t = hashmap_get_value(m->timers, name);

	// New timer
	if (!t) {
		t = malloc(sizeof(struct timer_hist));
		init_timer(m->timer_eps, m->quantiles, m->num_quants, &t->tm);
		hashmap_put(m->timers, name, t);

		// Check if we have any histograms configured
		if (m->histograms && (conf = radix_longest_prefix_value(m->histograms, name))) {
			t->conf = conf;
			t->counts = calloc(conf->num_bins, sizeof(unsigned int));
		} else {
			t->conf = NULL;
			t->counts = NULL;
		}
	}
	// Add the histogram value
	if (t->conf) {
		conf = t->conf;
		if (val < conf->min_val)
			t->counts[0]++;
		else if (val >= conf->max_val)
			t->counts[conf->num_bins - 1]++;
		else {
			int idx = ((val - conf->min_val) / conf->bin_width) + 1;
			if (idx >= conf->num_bins)
				idx = conf->num_bins - 1;
			t->counts[idx]++;
		}
	}
	// Add the sample value
	return timer_add_sample(&t->tm, val, sample_rate);
}

/**
 * Adds a new K/V pair
 * @arg name The key name
 * @arg val The value associated
 * @return 0 on success.
 */
static int metrics_add_kv(struct metrics * m, char *name, double val)
{
	struct key_val *kv = malloc(sizeof(struct key_val));
	kv->name = strdup(name);
	kv->val = val;
	kv->next = m->kv_vals;
	m->kv_vals = kv;
	return 0;
}

/**
 * Sets a gauge value
 * @arg name The name of the gauge
 * @arg val The value to set
 * @arg delta Is this a delta update
 * @arg timestamp_ms User-specified timestamp in milliseconds
 * @return 0 on success
 */
int metrics_set_gauge_ts(struct metrics * m, char *name, double val, bool delta, uint64_t user, uint64_t timestamp_ms)
{
	struct gauge *g = hashmap_get_value(m->gauges, name);

	// New gauge
	if (!g) {
		g = malloc(sizeof(struct gauge));
		g->value = 0;
		hashmap_put(m->gauges, name, g);
	}

	g->user = user;
	g->timestamp_ms = timestamp_ms;
	g->prev_value = g->value;
	if (delta) {
		g->value += val;
	} else {
		g->value = val;
	}

	g->user_flags = 0;

	return 0;
}

/**
 * Sets a guage value
 * @arg name The name of the gauge
 * @arg val The value to set
 * @arg delta Is this a delta update
 * @return 0 on success
 */
int metrics_set_gauge(struct metrics * m, char *name, double val, bool delta, uint64_t user)
{
	return metrics_set_gauge_ts(m, name, val, delta, user, 0);
}

/**
 * Adds a new sampled value
 * arg type The type of the metrics
 * @arg name The name of the metric
 * @arg val The sample to add
 * @return 0 on success.
 */
int metrics_add_sample(struct metrics * m, enum metric_type type, char *name, double val, double sample_rate)
{
	switch (type) {
	case metric_type_KEY_VAL:
		return metrics_add_kv(m, name, val);

	case metric_type_GAUGE:
		return metrics_set_gauge(m, name, val, false, 0);

	case metric_type_GAUGE_DELTA:
		return metrics_set_gauge(m, name, val, true, 0);

	case metric_type_COUNTER:
		return metrics_increment_counter(m, name, val, sample_rate);

	case metric_type_TIMER:
		return metrics_add_timer_sample(m, name, val, sample_rate);

	default:
		return -1;
	}
}

/**
 * Adds a value to a named set.
 * @arg name The name of the set
 * @arg value The value to add
 * @return 0 on success
 */
int metrics_set_update(struct metrics * m, char *name, char *value)
{
	set_t *s = hashmap_get_value(m->sets, name);

	// New set
	if (!s) {
		s = malloc(sizeof(set_t));
		set_init(m->set_precision, s, m->set_max_exact);
		hashmap_put(m->sets, name, s);
	}
	// Add the sample value
	set_add(s, value);
	return 0;
}

/**
 * Iterates through all the metrics
 * @arg m The metrics to iterate through
 * @arg data Opaque handle passed to the callback
 * @arg cb A callback function to invoke. Called with a type, name
 * and value. If the type is KEY_VAL, it is a pointer to a double,
 * for a counter, it is a pointer to a counter, and for a timer it is
 * a pointer to a timer. Return non-zero to stop iteration.
 * @return 0 on success, or the return of the callback
 */
int metrics_iter(struct metrics * m, void *data, metric_callback cb)
{
	// Handle the K/V pairs first
	struct key_val *current = m->kv_vals;
	int should_break = 0;
	while (current && !should_break) {
		should_break = cb(data, metric_type_KEY_VAL, current->name, &current->val);
		current = current->next;
	}
	if (should_break)
		return should_break;

	// Store our data in a small struct
	struct cb_info info = { metric_type_COUNTER, data, cb };

	// Send the counters
	should_break = hashmap_iter(m->counters, iter_cb, &info);
	if (should_break)
		return should_break;

	// Send the timers
	info.type = metric_type_TIMER;
	should_break = hashmap_iter(m->timers, iter_cb, &info);
	if (should_break)
		return should_break;

	// Send the gauges
	info.type = metric_type_GAUGE;
	should_break = hashmap_iter(m->gauges, iter_cb, &info);
	if (should_break)
		return should_break;

	// Send the sets
	info.type = metric_type_SET;
	should_break = hashmap_iter(m->sets, iter_cb, &info);

	return should_break;
}

// Counter map cleanup
static int counter_delete_cb(void *data, const char *key, void *value)
{
	free(value);
	return 0;
}

// Timer map cleanup
static int timer_delete_cb(void *data, const char *key, void *value)
{
	struct timer_hist *t = value;
	destroy_timer(&t->tm);
	if (t->counts)
		free(t->counts);
	free(t);
	return 0;
}

// Set map cleanup
static int set_delete_cb(void *data, const char *key, void *value)
{
	set_t *s = value;
	set_destroy(s);
	free(s);
	return 0;
}

// Gauge map cleanup
static int gauge_delete_cb(void *data, const char *key, void *value)
{
	struct gauge *g = value;
	free(g);
	return 0;
}

// Callback to invoke the user code
static int iter_cb(void *data, const char *key, void *value)
{
	struct cb_info *info = data;
	return info->cb(info->data, info->type, (char *)key, value);
}
