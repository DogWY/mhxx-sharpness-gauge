#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include "../src/snapshot.h"

static SharpnessSnapshot shared;
static atomic_uint readers_seen;
static atomic_bool writer_paused;

static Sharpness sample(unsigned n) {
    Sharpness s = {.current=n, .maximum=n+100, .color=n%7};
    for (unsigned i = 0; i < 7; ++i) s.end[i] = n + i;
    return s;
}

static unsigned read_sample(void) {
    Sharpness s;
    assert(snapshot_read(&shared, &s));
    Sharpness expected = sample(s.current);
    assert(s.maximum == expected.maximum && s.color == expected.color);
    for (unsigned i = 0; i < 7; ++i) assert(s.end[i] == expected.end[i]);
    return s.current;
}

static void *writer(void *unused) {
    (void)unused;
    // Stop midway through preparing the inactive slot. The currently
    // published sample must remain readable before the writer resumes.
    uint64_t next = atomic_load(&shared.generation) + 1;
    atomic_store(&shared.slots[next & 1].current, 999);
    atomic_store(&writer_paused, true);
    while (atomic_load(&readers_seen) < 2) sched_yield();
    for (unsigned n = 1; n <= 10000; ++n) {
        Sharpness s = sample(n);
        snapshot_publish(&shared, &s);
    }
    return 0;
}

static void *reader(void *unused) {
    (void)unused;
    while (!atomic_load(&writer_paused)) sched_yield();
    assert(read_sample() == 0);
    atomic_fetch_add(&readers_seen, 1);
    unsigned previous = 0;
    do {
        unsigned current = read_sample();
        assert(current >= previous && current <= 10000);
        previous = current;
    } while (previous != 10000);
    return 0;
}

int main(void) {
    Sharpness s = sample(0);
    assert(!snapshot_read(&shared, &s));
    snapshot_publish(&shared, &s);
    pthread_t producer, first, second;
    assert(!pthread_create(&producer, 0, writer, 0));
    assert(!pthread_create(&first, 0, reader, 0));
    assert(!pthread_create(&second, 0, reader, 0));
    assert(!pthread_join(producer, 0));
    assert(!pthread_join(first, 0));
    assert(!pthread_join(second, 0));
    assert(snapshot_read(&shared, &s) && s.current == 10000);
    snapshot_publish(&shared, 0);
    assert(!snapshot_read(&shared, &s));
    snapshot_publish(&shared, &s);
    assert(read_sample() == 10000);
    return 0;
}
