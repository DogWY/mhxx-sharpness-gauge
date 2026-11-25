#ifndef MHGU_SHARPNESS_H
#define MHGU_SHARPNESS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t current;
    uint32_t maximum;
    uint32_t color; /* Game tier, red (0) through purple (6). */
    uint32_t end[7]; /* Cumulative tier ends; equal neighbors are empty. */
} Sharpness;

bool sharpness_read(uintptr_t main_base, Sharpness *out);

#endif
