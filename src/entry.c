#include "render.h"

#ifndef MHGU_SHARPNESS_PROBE
#include "snapshot.h"

// Only raw values cross the gameplay/render thread boundary.
static SharpnessSnapshot latest;
#endif

void sharpness_sample(uintptr_t base) {
#ifdef MHGU_SHARPNESS_PROBE
    (void)base;
#else
    Sharpness next;
    bool valid = sharpness_read(base, &next);
    snapshot_publish(&latest, valid ? &next : 0);
#endif
}

void sharpness_present(uintptr_t base, void *commands) {
#ifdef MHGU_SHARPNESS_PROBE
    sharpness_draw_probe(base, commands);
#else
    Sharpness copy;
    if (snapshot_read(&latest, &copy)) sharpness_draw(base, commands, &copy);
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
