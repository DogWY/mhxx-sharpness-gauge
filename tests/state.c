/* Exercise the Switch's 32-bit object layouts with a native 32-bit or
 * wasm32 C compiler. Native method stubs use the actual object layout and 32-bit calling convention. */
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
static uint32_t equipment[320], resources[64], resource[28];
static uint32_t weapon[1], second_weapon[1], weapon_methods[82];
static uint32_t level[3], second_level[3], level_methods[34];
static uint16_t rows[20] = {
    0, 0, 30, 30, 95, 95, 150, 150, 150, 0,
    0, 0, 10, 25, 50, 60, 75, 90, 150, 0
};
static unsigned calls, row_reads, fallback_calls;
static bool no_weapon;
static uintptr_t find_weapon(uintptr_t registry, uintptr_t item) {
    if (registry != (uintptr_t)resources || item != (uintptr_t)equipment + 24)
        __builtin_trap();
    ++calls;
    if (no_weapon) return 0;
    return *(uint16_t *)(item + 6) == 7 ? (uintptr_t)second_weapon : (uintptr_t)weapon;
}
static uintptr_t get_level(uintptr_t who, unsigned selector) {
    if (who != (uintptr_t)weapon && who != (uintptr_t)second_weapon) __builtin_trap();
    if (selector > 1) return 0;
    return who == (uintptr_t)second_weapon || selector ? (uintptr_t)second_level : (uintptr_t)level;
}
static unsigned row_index(uintptr_t who) {
    if (who != (uintptr_t)level && who != (uintptr_t)second_level) __builtin_trap();
    ++row_reads;
    return ((const uint32_t *)who)[2];
}
static int fixed_color(uintptr_t who) {
    if (who != (uintptr_t)weapon) __builtin_trap();
    return 5;
}
static int fallback_tier(uintptr_t who, unsigned points) {
    if (who != (uintptr_t)level || !points || points > 150) __builtin_trap();
    ++fallback_calls;
    return points <= 70 ? 1 : 5;
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

    weapon[0] = (uintptr_t)weapon_methods;
    level[0] = (uintptr_t)level_methods;
    second_weapon[0] = (uintptr_t)weapon_methods;
    second_level[0] = (uintptr_t)level_methods;
    second_level[2] = 1;
    weapon_methods[216/4] = (uintptr_t)get_level;
    weapon_methods[324/4] = (uintptr_t)fixed_color;
    level_methods[32/4] = (uintptr_t)row_index;
    level_methods[128/4] = base + 0xf4724;
    level_methods[132/4] = base + 0xf4794;
    resources[252/4] = (uintptr_t)resource;
    resource[100/4] = 2;
    resource[108/4] = (uintptr_t)rows;

    Sharpness state;
    CHECK(read_with_weapon(base, find_weapon, &state));
    CHECK(calls == 1 && row_reads == 1 && fallback_calls == 0);
    CHECK(log_count == 1 && logged("MHGU-Sharpness-Gauge visible detail=0x1750d20 current=103 max=150 tier=4\n"));
    CHECK(read_with_weapon(base, find_weapon, &state) && log_count == 1);
    CHECK(state.current == 103 && state.maximum == 150 && state.color == 4);
    CHECK(state.end[0] == 30 && state.end[1] == 30 && state.end[2] == 95);
    CHECK(state.end[3] == 95 && state.end[4] == 150 && state.end[6] == 150);

    /* Same live equipment allocation, maximum, current and color. */
    equipment[28/4] = 7u << 16; /* replace equipped weapon ID */
    CHECK(read_with_weapon(base, find_weapon, &state) && state.end[0] == 10);
    equipment[28/4] = 1u << 5; /* first weapon, different level */
    CHECK(read_with_weapon(base, find_weapon, &state) && state.end[3] == 60);
    equipment[28/4] = 0;
    CHECK(read_with_weapon(base, find_weapon, &state) && state.end[3] == 95);

    hunter[0x2774 / 4] = 95;
    hunter[0x13f0 / 4] = 2;
    CHECK(read_with_weapon(base, find_weapon, &state) && state.current == 95 && state.color == 2);
    CHECK(log_count == 2 && logged("MHGU-Sharpness-Gauge visible detail=0x1750d20 current=95 max=150 tier=2\n"));
    hunter[0x2774 / 4] = 150;
    hunter[0x13f0 / 4] = 4;
    CHECK(read_with_weapon(base, find_weapon, &state) && state.current == 150);
    calls = 0;
    load[6] = 1;
    CHECK(!read_with_weapon(base, find_weapon, &state));
    CHECK(logged("MHGU-Sharpness-Gauge hud-overlay-active detail=0x2\n"));
    unsigned hidden_count = log_count;
    CHECK(!read_with_weapon(base, find_weapon, &state) && log_count == hidden_count);
    load[6] = 0;
    node[6] = 0;
    CHECK(!read_with_weapon(base, find_weapon, &state));
    node[6] = 1;
    session[12] = 0;
    CHECK(!read_with_weapon(base, find_weapon, &state));
    session[12] = 4;
    CHECK(!read_with_weapon(base, find_weapon, &state));
    session[12] = 2;
    hunter[0] = base + 0x17515ec;
    CHECK(!read_with_weapon(base, find_weapon, &state));
    hunter[0] = base + 0x174bdc4;
    CHECK(!read_with_weapon(base, find_weapon, &state));
    hunter[0] = base + 0x1750d20;
    hunter[5060 / 4] = 0;
    CHECK(!read_with_weapon(base, find_weapon, &state));
    CHECK(logged("MHGU-Sharpness-Gauge equipment-missing detail=0x1750d20 current=150 max=150 tier=4\n"));
    CHECK(calls == 0);

    /* Restore all addresses, but change the active weapon's row and the
     * row itself during the hidden interval. No previous ends survive. */
    hunter[5060/4] = (uintptr_t)equipment;
    level[2] = 1;
    CHECK(read_with_weapon(base, find_weapon, &state) && state.end[0] == 10);
    rows[12] = 8;
    CHECK(read_with_weapon(base, find_weapon, &state) && state.end[0] == 8);
    equipment[28/4] = 2u << 5; /* valid equipment, missing level */
    CHECK(read_with_weapon(base, find_weapon, &state) && state.end[0] == 150);
    equipment[28/4] = 0;
    CHECK(read_with_weapon(base, find_weapon, &state) && state.end[0] == 8);
    hunter[5068/4] = 8; /* fixed-tier weapon ignores points and row */
    CHECK(read_with_weapon(base, find_weapon, &state));
    CHECK(state.end[4] == 0 && state.end[5] == 150);
    hunter[5068/4] = 0;
    equipment[1236/4] = 10;
    CHECK(read_with_weapon(base, find_weapon, &state) && state.end[0] == 150);
    equipment[1236/4] = 0;
    no_weapon = true;
    CHECK(read_with_weapon(base, find_weapon, &state) && state.end[0] == 150);
    no_weapon = false;
    level[2] = 2;
    CHECK(!read_with_weapon(base, find_weapon, &state));
    level[2] = 1;
    resource[108/4] = 0;
    CHECK(!read_with_weapon(base, find_weapon, &state));
    level_methods[132/4] = (uintptr_t)fallback_tier;
    CHECK(read_with_weapon(base, find_weapon, &state) && fallback_calls > 0);
    CHECK(state.end[0] == 0 && state.end[1] == 70 && state.end[5] == 150);
    return 0;
}
