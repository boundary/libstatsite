#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include "set.h"

/*
 * The set code has been updated to mimic the behaviour in collectd/statsd.
 * Each push interval will provide the current count.  The set_reset function
 * will free the memory, but allow the last count to be accessed.   In the event
 * that no new data arrives, the old value will be pushed.   If a new set update
 * arrives, the old value is discarded and the set "restarts"
 */

// Link the external murmur hash in
extern void MurmurHash3_x64_128(const void *key, const int len, const uint32_t seed, void *out);

/**
 * Initializes a new set
 * @arg precision The precision to use when converting to an HLL
 * @arg s The set to initialize
 * @return 0 on success.
 */
int set_init(unsigned char precision, set_t * s)
{
	// Initialize as an exact set
	s->type = EXACT;
	s->store.s.precision = precision;
	s->store.s.count = 0;
	s->reset = false;
	s->store.s.hashes = (uint64_t *) malloc(sizeof(uint64_t) * SET_MAX_EXACT);
	if (!s->store.s.hashes)
		return 1;
	return 0;
}

/**
 * Destroys the set
 * @return 0 on sucess
 */
int set_destroy(set_t * s)
{
	switch (s->type) {
	case EXACT:
		free(s->store.s.hashes);
		break;

	case APPROX:
		hll_destroy(&s->store.h);
		break;
	}
	return 0;
}

/**
 * Converts a full exact set to an approximate HLL set.
 */
static void convert_exact_to_approx(set_t * s)
{
	// Store the hashes, as HLL initialization
	// will step on the pointer
	uint64_t *hashes = s->store.s.hashes;

	// Initialize the HLL
	s->type = APPROX;
	hll_init(s->store.s.precision, &s->store.h);

	// Add each hash to the HLL
	for (int i = 0; i < SET_MAX_EXACT; i++) {
		hll_add_hash(&s->store.h, hashes[i]);
	}

	// Free the array of hashes
	free(hashes);
}

/**
 * Adds a new key to the set
 * @arg s The set to add to
 * @arg key The key to add
 */
void set_add(set_t * s, char *key)
{
	uint32_t i;
	uint64_t out[2];
	if (s->reset) {
		set_init(s->store.s.precision, s);
	}
	MurmurHash3_x64_128(key, strlen(key), 0, &out);
	switch (s->type) {
	case EXACT:
		// Check if this element is already added
		for (i = 0; i < s->store.s.count; i++) {
			if (out[1] == s->store.s.hashes[i])
				return;
		}

		// Check if we can fit this in the array
		if (i < SET_MAX_EXACT) {
			s->store.s.hashes[i] = out[1];
			s->store.s.count++;
			return;
		}
		// Otherwise, force conversion to HLL
		// and purposely fall through to add the
		// element to the HLL
		convert_exact_to_approx(s);

	case APPROX:
		hll_add_hash(&s->store.h, out[1]);
		break;
	}
}

/**
 * Returns the size of the set. May be approximate.
 * @arg s The set to query
 * @return The size of the set.
 */
uint64_t set_size(set_t * s)
{

	if (s->reset) {
		return s->store.s.count;
	}

	switch (s->type) {
	case EXACT:
		return s->store.s.count;

	case APPROX:
		return ceil(hll_size(&s->store.h));

	default:
		abort();
	}
}

/**
 * Reset a set, but retain the last count.
 * @arg s The set
 */
void set_reset(set_t *s) {

	if (s->reset) {
		return;
	}

	s->store.s.count = set_size(s);
	set_destroy(s);
	s->reset = true;
}
