#include "render.h"

typedef struct { int x, y, z, width, height, depth; } Region;
typedef void (*ClearTexture)(void *, void *, const void *, const Region *,
                             const float rgba[4], int channels);
typedef struct {
    void *commands, *texture;
    ClearTexture clear;
    float scale_x, scale_y;
} Frame;

// The retail game crops the acquired window texture to renderer->width/height.
// Scale from the original plugin's 1280x720 design grid into that crop.
static int begin_frame(uintptr_t base, void *commands, Frame *out) {
    if (!base || !commands) return 0;
    uintptr_t renderer = *(uintptr_t *)(base + 0x2126998);
    if (!renderer) return 0;
    uintptr_t window = renderer + 213248;
    if (*(void **)(window + 376) != commands) return 0;
    unsigned acquired = *(unsigned *)(window + 4660 * 4);
    uintptr_t game_texture = *(uintptr_t *)(window + 4659 * 4);
    if (acquired >= 3 || !game_texture ||
        game_texture != *(uintptr_t *)(window + (4656 + acquired) * 4)) return 0;
    void *texture = *(void **)(game_texture + 60);
    ClearTexture clear = *(ClearTexture *)(base + 0x191fd90);
    int width = *(int *)(renderer + 176), height = *(int *)(renderer + 180);
    if (!texture || !clear || width < 1 || width > 8192 || height < 1 || height > 8192)
        return 0;
    *out = (Frame){commands, texture, clear, width / 1280.f, height / 720.f};
    return 1;
}

// Coordinates are local to the sword's top-left anchor at (110, 40).
static void rect(const Frame *f, float left, float top, float right, float bottom,
                 const float rgba[4]) {
    int x0 = (int)((110 + left) * f->scale_x + .5f);
    int y0 = (int)((40 + top) * f->scale_y + .5f);
    int x1 = (int)((110 + right) * f->scale_x + .5f);
    int y1 = (int)((40 + bottom) * f->scale_y + .5f);
    if (x1 <= x0 || y1 <= y0) return;
    Region r = {x0, y0, 0, x1 - x0, y1 - y0, 1};
    f->clear(f->commands, f->texture, 0, &r, rgba, 15);
}

#define RGB(r, g, b) {(r) / 255.f, (g) / 255.f, (b) / 255.f, 1}
static const float palette[][4] = {
    RGB(201,44,44), RGB(201,102,44), RGB(201,209,44), RGB(112,217,44),
    RGB(44,134,217), RGB(255,255,255), RGB(155,89,208)
};
static const float trough[] = RGB(22,24,29);

// In the reference plugin, each present tier gets at least two design pixels.
// Any extra width this costs is taken proportionally from the wider tiers.
static void fill_bar(const Frame *f, const Sharpness *state) {
    float widths[7], wide_sum = 0, fixed_sum = 0;
    unsigned lower = 0;
    for (unsigned i = 0; i < 7; ++i) {
        unsigned upper = state->end[i];
        widths[i] = 0;
        if (upper <= lower) continue;
        widths[i] = (upper - lower) * 65.f / state->maximum;
        if (widths[i] < 2) fixed_sum += 2;
        else wide_sum += widths[i];
        lower = upper;
    }
    if (!lower) {
        rect(f, 17, 3, 17 + 65.f * state->current / state->maximum, 14,
             palette[state->color]);
        return;
    }
    float scale = wide_sum + fixed_sum > 65 && wide_sum > 0 ?
                  (65 - fixed_sum) / wide_sum : 1;
    float x = 17;
    lower = 0;
    for (unsigned i = 0; i < 7; ++i) {
        unsigned upper = state->end[i];
        if (upper <= lower) continue;
        if (state->current <= lower) break;
        float width = widths[i] < 2 ? 2 : widths[i] * scale;
        float fill = width;
        if (state->current < upper)
            fill *= (float)(state->current - lower) / (upper - lower);
        rect(f, x, 3, x + fill, 14, palette[i]);
        x += width;
        lower = upper;
    }
}

#ifdef MHGU_SHARPNESS_DIAGNOSTIC
// Draw the remaining value, capacity and current tier below the gauge.
static int diagnostic_number(const Frame *frame, int x, unsigned value) {
    static const unsigned char digits[10][5] = {
        {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7},
        {5,5,7,1,1}, {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1},
        {7,5,7,5,7}, {7,5,7,1,7}
    };
    char reversed[10];
    unsigned count = 0;
    do {
        reversed[count++] = (char)(value % 10);
        value /= 10;
    } while (value);
    while (count) {
        const unsigned char *rows = digits[(unsigned)reversed[--count]];
        for (int y = 0; y < 5; ++y)
            for (int col = 0; col < 3; ++col)
                if (rows[y] & (4u >> col))
                    rect(frame, x+col, 20+y, x+col+1, 21+y, palette[5]);
        x += 4;
    }
    return x;
}

static void diagnostic_readout(const Frame *frame, const Sharpness *state) {
    rect(frame, 12, 19, 70, 26, trough);
    int x = diagnostic_number(frame, 13, state->current);
    for (int y = 0; y < 5; ++y) {
        int dx = (4 - y) / 2;
        rect(frame, x+dx, 20+y, x+dx+1, 21+y, palette[5]);
    }
    x = diagnostic_number(frame, x + 4, state->maximum);
    diagnostic_number(frame, x + 3, state->color);
}
#endif

static void draw_gauge(const Frame *frame, const Sharpness *state) {
    const float *accent = palette[state->color];
    // Original blade quadrilateral (13,0), (95,0), (85,17), (13,17).
    for (int y = 0; y < 17; ++y)
        rect(frame, 13, y, 95 - (y + .5f) * (10.f / 17), y + 1, accent);
    rect(frame, 16, 2, 83, 15, trough);
    fill_bar(frame, state);
    rect(frame, 6, 6.375f, 14, 10.625f, accent);
    // Pommel triangle (0,2), (9,8.5), (0,15).
    for (int y = 2; y < 15; ++y) {
        float distance = y < 8 ? y - 1.5f : 14.5f - y;
        rect(frame, 0, y, distance * (9.f / 6.5f), y + 1, accent);
    }
}

void sharpness_draw(uintptr_t main_base, void *commands, const Sharpness *state) {
    Frame frame;
    if (!state || !state->maximum || state->current > state->maximum ||
        state->color >= 7 || !begin_frame(main_base, commands, &frame)) return;
    draw_gauge(&frame, state);
#ifdef MHGU_SHARPNESS_DIAGNOSTIC
    diagnostic_readout(&frame, state);
#endif
}

#ifdef MHGU_SHARPNESS_PROBE
void sharpness_draw_probe(uintptr_t main_base, void *commands) {
    Frame frame;
    if (begin_frame(main_base, commands, &frame))
        rect(&frame, 0, 0, 10, 10, palette[3]);
}
#endif
