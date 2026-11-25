#include "sharpness.h"
#include "diagnostic.h"

static uint32_t word(uintptr_t address) {
    return *(volatile const uint32_t *)address;
}

/* GU uses 32-bit guest pointers. Singleton slots and object references can
 * be empty while switching scenes. Never retain an object between frames. */
static uintptr_t object_at(uintptr_t address) {
    uintptr_t value = word(address);
    return value >= 0x10000 && (value & 3) == 0 ? value : 0;
}

static uintptr_t hud_nodes(uintptr_t manager, unsigned group) {
    uintptr_t owner = object_at(manager + 0x148 + 4 * group);
    return owner ? object_at(owner + 0x34) : 0;
}

static bool node_visible(uintptr_t nodes, unsigned index) {
    uintptr_t node = object_at(nodes + 4 * index);
    return node && *(volatile const uint8_t *)(node + 0x18) != 0;
}

/* The reference HUD tests group 0, node 2, then the eight nodes in group
 * 2. Hide immediately when the HUD is absent so loading cannot show a
 * stale bar. These are equivalent to the retail getters 55D2B4/65E1D0. */
static bool gameplay_hud_visible(uintptr_t base) {
    uintptr_t manager = object_at(base + 0x1899458);
    if (!manager) HIDE("hud-manager-missing", 0, 0);
    uintptr_t overlays = hud_nodes(manager, 0);
    if (overlays && node_visible(overlays, 2)) HIDE("hud-overlay-active", 2, 0);
    uintptr_t indicators = hud_nodes(manager, 2);
    if (!indicators) HIDE("hud-indicators-missing", 0, 0);
    for (unsigned i = 0; i < 8; ++i) {
        if (node_visible(indicators, i)) return true;
    }
    HIDE("hud-indicators-hidden", 0, 0);
}

static bool melee_hunter(uintptr_t vtable_offset) {
    /* Reference NRO's class table (0x4a58), after its exclusion mask
     * 0x8450 removes the three gunner classes and Prowler. */
    switch (vtable_offset) {
        case 0x1749a88:
        case 0x174a354:
        case 0x174ac20:
        case 0x174b4f0:
        case 0x174cf64:
        case 0x174d838:
        case 0x174e110:
        case 0x174f2b4:
        case 0x174fb80:
        case 0x1750450:
        case 0x1750d20: /* uPlayerQuest14, Charge Blade */
            return true;
        default:
            return false;
    }
}

typedef uintptr_t (*FindWeapon)(uintptr_t registry, uintptr_t item);
typedef unsigned (*GetValue)(uintptr_t self);
typedef uintptr_t (*GetLevel)(uintptr_t weapon, unsigned level);
typedef int (*TierAt)(uintptr_t self, unsigned points);

static uint32_t method(uintptr_t self, unsigned offset) {
    uintptr_t vtable = object_at(self);
    return vtable ? word(vtable + offset) : 0;
}

static void fixed_tier(Sharpness *state, unsigned color) {
    if (color > 6) color = 6;
    for (unsigned i = 0; i < 7; ++i)
        state->end[i] = i < color ? 0 : state->maximum;
}

/* Retail sub_6029CDFC delegates to a weapon level's +132 tier method.
 * All eleven melee classes use sub_600F4794 with sub_600F4724 at +128:
 * the former compares points against cumulative thresholds; the latter
 * uses the level's +32 row index and the registry's rKireajiData.
 * Read that row once per sample, including changes made in-place.
 * For a different implementation, ask its actual tier method. */
static bool level_ends(uintptr_t base, uintptr_t registry, uintptr_t level,
                       Sharpness *state) {
    uintptr_t tier_method = method(level, 132);
    if (tier_method == base + 0xf4794 && method(level, 128) == base + 0xf4724) {
        uintptr_t resource = object_at(registry + 252);
        if (!resource) return false;
        GetValue get_index = (GetValue)method(level, 32);
        if (!get_index) return false;
        unsigned index = get_index(level);
        if (index >= word(resource + 100)) return false;
        uintptr_t rows = object_at(resource + 108);
        if (!rows) return false;
        uintptr_t ends = rows + 20 * index + 4; /* sub_6013F290 */
        for (unsigned i = 0; i < 6; ++i) {
            uint32_t end = *(volatile const uint16_t *)(ends + 2*i);
            state->end[i] = end < state->maximum ? end : state->maximum;
        }
    } else {
        if (!tier_method) return false;
        TierAt tier_at = (TierAt)tier_method;
        uint32_t start = 0;
        for (int tier = 0; tier < 6; ++tier) {
            uint32_t lo = start, hi = state->maximum;
            while (lo < hi) {
                uint32_t mid = lo + (hi - lo + 1) / 2;
                if (tier_at(level, mid) <= tier) lo = mid;
                else hi = mid - 1;
            }
            state->end[tier] = lo;
            start = lo;
        }
    }
    state->end[6] = state->maximum;
    return true;
}

static bool read_with_weapon(uintptr_t base, FindWeapon find_weapon, Sharpness *out) {
    if (!base || !out || !gameplay_hud_visible(base)) return false;

    /* Retail sub_60276278 -> sub_603F8398: use the local multiplayer
     * slot even when this hunter isn't stored in slot zero. */
    uintptr_t manager = object_at(base + 0x1885b58);
    uintptr_t session = object_at(base + 0x188ca48);
    if (!manager) HIDE("player-manager-missing", 0, 0);
    if (!session) HIDE("session-missing", 0, 0);
    unsigned slot = word(session + 0x30);
    if (slot >= 4) HIDE("local-slot-invalid", slot, 0);
    uintptr_t hunter = object_at(manager + 0x1c + 4 * slot);
    if (!hunter) HIDE("hunter-missing", slot, 0);
    uintptr_t hunter_class = word(hunter) - base;
    if (!melee_hunter(hunter_class)) HIDE("hunter-class-hidden", hunter_class, 0);

    /* sub_6029CDFC computes the tier on the game's own update path. */
    Sharpness state = {
        .current = word(hunter + 0x2774),
        .maximum = word(hunter + 0x2778),
        .color = word(hunter + 0x13f0)
    };
    if (!state.maximum || state.current > state.maximum || state.color > 6)
        HIDE("sharpness-invalid", hunter_class, &state);
    if (state.maximum > 4096) HIDE("sharpness-range", hunter_class, &state);
    uintptr_t equipment = object_at(hunter + 5060);
    if (!equipment) HIDE("equipment-missing", hunter_class, &state);
    uintptr_t registry = object_at(base + 0x1856b18);
    if (!registry) HIDE("weapon-table-missing", hunter_class, &state);

    /* sub_6027C7EC(hunter,1) uses the equipment category (distinct from
     * the player class). The native lookup returns red for these modes. */
    unsigned type = *(volatile const uint8_t *)(equipment + 1236);
    uintptr_t weapon = 0;
    if (type != 4 && type != 5 && type != 6 && type != 10)
        weapon = find_weapon(registry, equipment + 24); /* sub_600DF814 */
    if (!weapon) {
        fixed_tier(&state, 0);
    } else if (*(volatile const uint8_t *)(hunter + 5068) & 8) {
        int (*get_color)(uintptr_t) = (int (*)(uintptr_t))method(weapon, 324);
        if (!get_color) HIDE("weapon-method-missing", hunter_class, &state);
        int tier = get_color(weapon);
        fixed_tier(&state, tier < 0 ? 0u : (unsigned)tier);
    } else {
        unsigned selector = (*(volatile const uint16_t *)(equipment + 28) >> 5) & 31;
        GetLevel get_level = (GetLevel)method(weapon, 216);
        if (!get_level) HIDE("weapon-method-missing", hunter_class, &state);
        uintptr_t level = get_level(weapon, selector);
        if (!level)
            fixed_tier(&state, 0);
        else if (!level_ends(base, registry, level, &state))
            HIDE("sharpness-row-missing", hunter_class, &state);
    }
    log_state("visible", hunter_class, &state);
    *out = state;
    return true;
}

bool sharpness_read(uintptr_t base, Sharpness *out) {
    return read_with_weapon(base, (FindWeapon)(base + 0x13f03c), out);
}
