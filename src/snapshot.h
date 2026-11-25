#ifndef MHGU_SNAPSHOT_H
#define MHGU_SNAPSHOT_H

#include <stdatomic.h>
#include "sharpness.h"

typedef struct {
    _Atomic uint32_t current, maximum, color, end[7];
} SharpnessSlot;

typedef struct {
    _Atomic uint64_t generation;
    SharpnessSlot slots[2];
} SharpnessSnapshot;

// A single gameplay thread writes the inactive slot, then publishes its
// generation. Use sequentially consistent atomics throughout: two equal
// generation reads bracket a period in which the selected slot was active
// and hence unmodified. Atomic payloads also keep discarded, overtaken
// reads race-free. A suspended writer always leaves a completed slot live.
static inline void snapshot_publish(SharpnessSnapshot *snapshot, const Sharpness *value) {
    uint64_t next = atomic_load(&snapshot->generation) + 1;
    SharpnessSlot *slot = &snapshot->slots[next & 1];
    atomic_store(&slot->maximum, value ? value->maximum : 0);
    if (value) {
        atomic_store(&slot->current, value->current);
        atomic_store(&slot->color, value->color);
        for (unsigned i = 0; i < 7; ++i)
            atomic_store(&slot->end[i], value->end[i]);
    }
    atomic_store(&snapshot->generation, next);
}

static inline bool snapshot_read(const SharpnessSnapshot *snapshot, Sharpness *out) {
    uint64_t generation;
    Sharpness copy;
    do {
        generation = atomic_load(&snapshot->generation);
        const SharpnessSlot *slot = &snapshot->slots[generation & 1];
        copy.maximum = atomic_load(&slot->maximum);
        if (copy.maximum) {
            copy.current = atomic_load(&slot->current);
            copy.color = atomic_load(&slot->color);
            for (unsigned i = 0; i < 7; ++i)
                copy.end[i] = atomic_load(&slot->end[i]);
        }
    } while (generation != atomic_load(&snapshot->generation));
    if (!copy.maximum) return false;
    *out = copy;
    return true;
}

#endif
