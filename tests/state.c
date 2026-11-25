/* Exercise the Switch's 32-bit object layouts with a native 32-bit or
 * wasm32 C compiler. Only the retail tier lookup is replaced. */
#define MHGU_SHARPNESS_DIAGNOSTIC
#include "../src/sharpness.c"

static unsigned log_count;
static char last_line[160];
void sharpness_log_line(const char *line, unsigned size) {
    ++log_count;
    if (size >= sizeof last_line) __builtin_trap();
    for (unsigned i = 0; i < size; ++i) last_line[i] = line[i];
    last_line[size] = 0;
}

static bool logged(const char *expected) {
    const char *p = last_line;
    while (*expected && *p == *expected) { ++p; ++expected; }
    return !*p && !*expected;
}

_Static_assert(sizeof(uintptr_t) == sizeof(uint32_t), "32-bit fixture required");
static uint32_t image[0x18a0000 / 4];
static uint32_t hud[88], group0[14], group2[14], overlays[8], nodes[8];
static uint32_t node[8], load[8], session[16], manager[16], hunter[0x2800/4];
static uint32_t equipment[2], resources[2];
static unsigned calls;
static int tier(uintptr_t who, int points) {
    if (who != (uintptr_t)hunter || points < 1 || points > 150) return -1;
    ++calls;
    return points <= 30 ? 0 : points <= 95 ? 2 : 4;
}

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

int main(void) {
    /* NRO .rodata 0x4a58. ID 5 is absent from the game vtable list. */
    static const uintptr_t class_table[16] = {
        0x1749a88, 0x174a354, 0x174ac20, 0x174b4f0, 0x174bdc4, 0,
        0x174c694, 0x174cf64, 0x174d838, 0x174e110, 0x174e9e0,
        0x174f2b4, 0x174fb80, 0x1750450, 0x1750d20, 0x17515ec
    };
    for (unsigned i = 0; i < 16; ++i)
        CHECK(melee_hunter(class_table[i]) == (i != 5 && !(0x8450u & (1u << i))));

    uintptr_t base = (uintptr_t)image;
    image[0x1899458 / 4] = (uintptr_t)hud;
    hud[0x148 / 4] = (uintptr_t)group0;
    hud[0x150 / 4] = (uintptr_t)group2;
    group0[0x34 / 4] = (uintptr_t)overlays;
    group2[0x34 / 4] = (uintptr_t)nodes;
    overlays[2] = (uintptr_t)load;
    nodes[5] = (uintptr_t)node;
    node[0x18 / 4] = 1;
    image[0x1885b58 / 4] = (uintptr_t)manager;
    image[0x188ca48 / 4] = (uintptr_t)session;
    session[0x30 / 4] = 2;
    manager[0x1c / 4 + 2] = (uintptr_t)hunter;
    hunter[0] = base + 0x1750d20;
    hunter[0x2774 / 4] = 103;
    hunter[0x2778 / 4] = 150;
    hunter[0x13f0 / 4] = 4;
    hunter[5060 / 4] = (uintptr_t)equipment;
    image[0x1856b18 / 4] = (uintptr_t)resources;

    Sharpness state;
    CHECK(read_with_tier(base, tier, &state));
    CHECK(calls > 0 && calls < 60);
    CHECK(log_count == 1 && logged("MHGU-Sharpness-Gauge visible detail=0x1750d20 current=103 max=150 tier=4\n"));
    CHECK(read_with_tier(base, tier, &state) && log_count == 1);
    CHECK(state.current == 103 && state.maximum == 150 && state.color == 4);
    CHECK(state.end[0] == 30 && state.end[1] == 30 && state.end[2] == 95);
    CHECK(state.end[3] == 95 && state.end[4] == 150 && state.end[6] == 150);

    hunter[0x2774 / 4] = 95;
    hunter[0x13f0 / 4] = 2;
    CHECK(read_with_tier(base, tier, &state) && state.current == 95 && state.color == 2);
    CHECK(log_count == 2 && logged("MHGU-Sharpness-Gauge visible detail=0x1750d20 current=95 max=150 tier=2\n"));
    hunter[0x2774 / 4] = 150;
    hunter[0x13f0 / 4] = 4;
    CHECK(read_with_tier(base, tier, &state) && state.current == 150);
    calls = 0;
    load[6] = 1;
    CHECK(!read_with_tier(base, tier, &state));
    CHECK(logged("MHGU-Sharpness-Gauge hud-overlay-active detail=0x2\n"));
    unsigned hidden_count = log_count;
    CHECK(!read_with_tier(base, tier, &state) && log_count == hidden_count);
    load[6] = 0;
    node[6] = 0;
    CHECK(!read_with_tier(base, tier, &state));
    node[6] = 1;
    session[12] = 0;
    CHECK(!read_with_tier(base, tier, &state));
    session[12] = 4;
    CHECK(!read_with_tier(base, tier, &state));
    session[12] = 2;
    hunter[0] = base + 0x17515ec;
    CHECK(!read_with_tier(base, tier, &state));
    hunter[0] = base + 0x174bdc4;
    CHECK(!read_with_tier(base, tier, &state));
    hunter[0] = base + 0x1750d20;
    hunter[5060 / 4] = 0;
    CHECK(!read_with_tier(base, tier, &state));
    CHECK(logged("MHGU-Sharpness-Gauge equipment-missing detail=0x1750d20 current=150 max=150 tier=4\n"));
    CHECK(calls == 0);
    return 0;
}
