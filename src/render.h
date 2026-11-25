#ifndef MHGU_RENDER_H
#define MHGU_RENDER_H

#include <stdint.h>
#include "sharpness.h"

void sharpness_draw(uintptr_t main_base, void *commands, const Sharpness *state);

#ifdef MHGU_SHARPNESS_PROBE
void sharpness_draw_probe(uintptr_t main_base, void *commands);
#endif

#endif
