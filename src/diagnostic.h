#ifndef MHGU_DIAGNOSTIC_H
#define MHGU_DIAGNOSTIC_H

#include "sharpness.h"

#ifdef MHGU_SHARPNESS_DIAGNOSTIC
// All calls run on the gameplay sampling thread.
void sharpness_log_line(const char *address, unsigned length);
typedef struct {
    const char *reason;
    uint32_t detail, current, maximum, tier;
} DiagnosticEvent;

static char *append_text(char *to, const char *from) {
    while (*from) *to++ = *from++;
    return to;
}

static char *append_number(char *to, uint32_t n, unsigned radix) {
    char reversed[10];
    unsigned count = 0;
    do {
        reversed[count++] = "0123456789abcdef"[radix == 16 ? n & 15 : n % 10];
        n = radix == 16 ? n >> 4 : n / 10;
    } while (n);
    while (count) *to++ = reversed[--count];
    return to;
}

static void log_state(const char *reason, uint32_t detail, const Sharpness *state) {
    static DiagnosticEvent last;
    DiagnosticEvent next = {reason, detail, 0, 0, 0};
    if (state) {
        next.current = state->current;
        next.maximum = state->maximum;
        next.tier = state->color;
    }
    if (last.reason == reason && last.detail == detail && last.current == next.current &&
        last.maximum == next.maximum && last.tier == next.tier) return;
    last = next;
    char line[160];
    char *p = append_text(line, "MHGU-Sharpness-Gauge ");
    p = append_text(p, reason);
    p = append_text(p, " detail=0x");
    p = append_number(p, detail, 16);
    if (state) {
        p = append_text(p, " current=");
        p = append_number(p, next.current, 10);
        p = append_text(p, " max=");
        p = append_number(p, next.maximum, 10);
        p = append_text(p, " tier=");
        p = append_number(p, next.tier, 10);
    }
    *p++ = '\n';
    sharpness_log_line(line, (unsigned)(p - line));
}
#else
#define log_state(reason, detail, state) ((void)0)
#endif

#define HIDE(reason, detail, state) do { log_state(reason, detail, state); return false; } while (0)

#endif
