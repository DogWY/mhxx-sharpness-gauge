/* Model the retail 32-bit swapchain layout and call the public HUD entry. */
#include "../src/render.c"

_Static_assert(sizeof(uintptr_t) == 4, "32-bit guest layout required");
static uint32_t main_image[0x2126998 / 4 + 1];
static uint32_t renderer[213248 / 4 + 4661];
static uint32_t wrapper[3][16];
static int texture[3], commands, wrong_commands;
static void *expected_texture;
static Region first;
static unsigned clears;
static bool wrong_target;

static void capture(void *buffer, void *target, const void *view, const Region *r,
                    const float color[4], int channels) {
    (void)color;
    if (!clears) first = *r;
    ++clears;
    if (buffer != &commands || target != expected_texture || view || channels != 15)
        wrong_target = true;
}

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

int main(void) {
    uintptr_t base = (uintptr_t)main_image;
    main_image[0x2126998 / 4] = (uintptr_t)renderer;
    main_image[0x191fd90 / 4] = (uintptr_t)capture;
    uint32_t *window = renderer + 213248 / 4;
    window[376 / 4] = (uintptr_t)&commands;
    for (unsigned i = 0; i < 3; ++i) {
        wrapper[i][60 / 4] = (uintptr_t)&texture[i];
        window[4656 + i] = (uintptr_t)wrapper[i];
    }
    Sharpness state = {.current = 100, .maximum = 100, .color = 0, .end = {100}};
    /* Expected top-left pixel of the first blade scanline in each crop. */
    static const int sizes[][4] = {
        {1280, 720, 123, 40}, {2560, 1440, 246, 80}, {3840, 2160, 369, 120},
        {1920, 1080, 185, 60}, {1920, 720, 185, 40}
    };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; ++i) {
        unsigned slot = i % 3;
        window[4660] = slot;
        window[4659] = window[4656 + slot];
        expected_texture = &texture[slot];
        renderer[176 / 4] = sizes[i][0];
        renderer[180 / 4] = sizes[i][1];
        clears = 0;
        sharpness_draw(base, &commands, &state);
        CHECK(clears && !wrong_target);
        CHECK(first.x == sizes[i][2] && first.y == sizes[i][3]);
    }

    clears = 0;
    sharpness_draw(base, &wrong_commands, &state);
    CHECK(!clears);
    window[4660] = 3;
    sharpness_draw(base, &commands, &state);
    CHECK(!clears);
    window[4660] = 1;
    window[4659] = window[4656]; /* The bound texture belongs to slot 0. */
    sharpness_draw(base, &commands, &state);
    CHECK(!clears);
    window[4660] = 0;
    renderer[176 / 4] = 0;
    sharpness_draw(base, &commands, &state);
    CHECK(!clears);
    return 0;
}
