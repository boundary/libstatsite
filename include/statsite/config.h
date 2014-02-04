#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

// Represents the configuration of a histogram
typedef struct histogram_config {
    char *prefix;
    double min_val;
    double max_val;
    double bin_width;
    int num_bins;
    struct histogram_config *next;
    char parts;
} histogram_config;

#endif
