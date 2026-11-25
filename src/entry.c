#include <stdatomic.h>
#include "render.h"

#ifndef MHGU_SHARPNESS_PROBE
// The gameplay/presentation thread owns all pointers into the player graph.
// Only raw gauge values cross the boundary to the render worker.
static atomic_flag lock = ATOMIC_FLAG_INIT;
static bool visible;
static Sharpness latest;
#endif

void sharpness_sample(uintptr_t base) {
#ifdef MHGU_SHARPNESS_PROBE
    (void)base;
#else
    Sharpness next;
    bool valid = sharpness_read(base, &next);
    while (atomic_flag_test_and_set_explicit(&lock, memory_order_acquire))
        __asm__ volatile("yield");
    visible = valid;
    if (valid) latest = next;
    atomic_flag_clear_explicit(&lock, memory_order_release);
#endif
}

void sharpness_present(uintptr_t base, void *commands) {
#ifdef MHGU_SHARPNESS_PROBE
    sharpness_draw_probe(base, commands);
#else
    if (atomic_flag_test_and_set_explicit(&lock, memory_order_acquire)) return;
    bool draw = visible;
    Sharpness copy;
    if (draw) copy = latest;
    atomic_flag_clear_explicit(&lock, memory_order_release);
    if (draw) sharpness_draw(base, commands, &copy);
#endif
}

// Astris reports OutputDebugString in the guest log. Include the actual NSO
// base so separate launches can confirm which landing pad layout was used.
void sharpness_log_line(const char *line, unsigned size) {
    register const char *address __asm__("r0") = line;
    register unsigned length __asm__("r1") = size;
    __asm__ volatile("svc #0x27" : "+r"(address), "+r"(length) : : "memory", "cc");
}

void sharpness_log_init(uintptr_t module) {
    char line[] = "MHGU-Sharpness-Gauge initialized @0x00000000\n";
    const char *digits = "0123456789abcdef";
    for (unsigned i = 0; i < 8; ++i) {
        line[sizeof(line) - 3 - i] = digits[module & 15];
        module >>= 4;
    }
    sharpness_log_line(line, sizeof(line) - 1);
}
