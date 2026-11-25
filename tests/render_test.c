// Compile with clang -std=c11 -Wall -Wextra -Werror tests/render_test.c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/render.c"

enum { WIDTH = 3840, HEIGHT = 2160 };
static unsigned char pixels[HEIGHT][WIDTH][3];

static void paint(void *commands, void *texture, const void *view, const Region *r,
                  const float color[4], int mask) {
    (void)commands; (void)texture; (void)view;
    assert(mask == 15 && r->depth == 1);
    assert(r->x >= 0 && r->x + r->width <= WIDTH);
    assert(r->y >= 0 && r->y + r->height <= HEIGHT);
    for (int y = r->y; y < r->y + r->height; ++y)
        for (int x = r->x; x < r->x + r->width; ++x)
            for (int k = 0; k < 3; ++k)
                pixels[y][x][k] = (unsigned char)(color[k] * 255 + .5f);
}

static void expect(int x, int y, int red, int green, int blue) {
    assert(pixels[y][x][0] == red && pixels[y][x][1] == green &&
           pixels[y][x][2] == blue);
}

int main(int argc, char **argv) {
    Sharpness s = {.current = 75, .maximum = 100, .color = 5,
                   .end = {50,50,50,50,50,100,100}};
    Frame f = {.clear = paint, .scale_x = 1, .scale_y = 1};
    draw_gauge(&f, &s);
    expect(127, 43, 201, 44, 44);
    expect(160, 43, 255, 255, 255);
    expect(175, 43, 255, 255, 255);
    expect(176, 43, 22, 24, 29);
    expect(123, 40, 255, 255, 255);

    s.current = 50;
    s.color = 0;
    draw_gauge(&f, &s);
    expect(159, 43, 201, 44, 44);
    expect(160, 43, 22, 24, 29);
    expect(123, 40, 201, 44, 44);

    memset(pixels, 0, sizeof pixels);
    f.scale_x = f.scale_y = 3;
    s = (Sharpness){.current=100,.maximum=100,.color=6,.end={1,20,35,55,70,90,100}};
    draw_gauge(&f, &s);
    expect(369, 119, 0, 0, 0);
    expect(369, 120, 155, 89, 208);
    expect(381, 129, 201, 44, 44);
    expect(386, 129, 201, 44, 44);
    expect(387, 129, 201, 102, 44);
    expect(575, 161, 155, 89, 208);
    expect(576, 161, 22, 24, 29);
    if (argc == 2) {
        FILE *out = fopen(argv[1], "wb");
        assert(out);
        // Cropped 4K preview at native pixel scale.
        fprintf(out, "P6\n320 75\n255\n");
        for (int y = 110; y < 185; ++y)
            assert(fwrite(pixels[y] + 320, 3, 320, out) == 320);
        assert(!fclose(out));
    }

    s.current = 0;
    draw_gauge(&f, &s);
    expect(381, 129, 22, 24, 29);
    return 0;
}
