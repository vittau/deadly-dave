#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"

static int failures = 0;

static void check(int condition, const char *what, int out_w, int out_h, int mode) {
    if (!condition) {
        printf("  FAIL [%dx%d, mode %d]: %s \n", out_w, out_h, mode, what);
        failures++;
    }
}

/*
 * Whatever the window size is, the picture must keep the aspect-ratio of the
 * framebuffer, fit inside the window and be centered in it.
 */
static void check_invariants(int out_w, int out_h, int mode) {
    display_geometry_t g;
    double scale_x;
    double scale_y;
    double drift;

    g = display_compute_geometry(out_w, out_h, mode);
    scale_x = (double)g.dst.w / (double)g.width;
    scale_y = (double)g.dst.h / (double)g.height;
    drift = (scale_x > scale_y) ? (scale_x - scale_y) : (scale_y - scale_x);

    check(g.height == DISPLAY_HEIGHT, "framebuffer height is not 200", out_w, out_h, mode);
    check(g.width >= DISPLAY_BASE_WIDTH && g.width <= DISPLAY_MAX_WIDTH,
        "framebuffer width out of range", out_w, out_h, mode);
    check((g.width % 8) == 0, "framebuffer width is not on the 8 pixel grid", out_w, out_h, mode);
    check(g.dst.w <= out_w && g.dst.h <= out_h, "picture does not fit in the window", out_w, out_h, mode);
    check(g.dst.x == (out_w - g.dst.w) / 2 && g.dst.y == (out_h - g.dst.h) / 2,
        "picture is not centered", out_w, out_h, mode);
    check(drift < 0.02, "pixels are not square, the picture is stretched", out_w, out_h, mode);

    if (mode == DISPLAY_SCALE_PIXEL_PERFECT && out_h >= DISPLAY_HEIGHT) {
        check(g.scale >= 1, "no integer scale factor was chosen", out_w, out_h, mode);
        check(g.dst.w == g.width * g.scale && g.dst.h == g.height * g.scale,
            "integer scaling was not applied exactly", out_w, out_h, mode);
    }
}

static void check_exact(int out_w, int out_h, int mode, int width, int dst_w, int dst_h) {
    display_geometry_t g;

    g = display_compute_geometry(out_w, out_h, mode);

    if (g.width != width || g.dst.w != dst_w || g.dst.h != dst_h) {
        printf("  FAIL [%dx%d, mode %d]: expected fb %d and picture %dx%d, got fb %d and picture %dx%d \n",
            out_w, out_h, mode, width, dst_w, dst_h, g.width, g.dst.w, g.dst.h);
        failures++;
    }
}

int main(void) {
    int resolutions[][2] = {
        {1280, 800},   /* 16:10, Steam Deck     */
        {1920, 1200},  /* 16:10                 */
        {1680, 1050},  /* 16:10                 */
        {2560, 1600},  /* 16:10                 */
        {960, 600},    /* 16:10, default window */
        {1920, 1080},  /* 16:9                  */
        {2560, 1440},  /* 16:9                  */
        {3840, 2160},  /* 16:9                  */
        {1366, 768},   /* 16:9                  */
        {1280, 720},   /* 16:9                  */
        {1024, 768},   /* 4:3                   */
        {3440, 1440},  /* 21:9                  */
        {320, 200},    /* smallest supported    */
        {640, 480},
        {200, 120}     /* smaller than the framebuffer */
    };
    int count = (int)(sizeof(resolutions) / sizeof(resolutions[0]));
    display_geometry_t degenerate;
    int i;

    printf("display geometry \n");

    for (i = 0; i < count; i++) {
        check_invariants(resolutions[i][0], resolutions[i][1], DISPLAY_SCALE_PIXEL_PERFECT);
        check_invariants(resolutions[i][0], resolutions[i][1], DISPLAY_SCALE_FIT);
    }

    /* 16:10 is the native shape of the game: the picture must fill the screen. */
    check_exact(1280, 800, DISPLAY_SCALE_PIXEL_PERFECT, 320, 1280, 800);
    check_exact(1280, 800, DISPLAY_SCALE_FIT, 320, 1280, 800);
    check_exact(1920, 1200, DISPLAY_SCALE_PIXEL_PERFECT, 320, 1920, 1200);
    check_exact(2560, 1600, DISPLAY_SCALE_PIXEL_PERFECT, 320, 2560, 1600);
    check_exact(960, 600, DISPLAY_SCALE_PIXEL_PERFECT, 320, 960, 600);
    check_exact(1680, 1050, DISPLAY_SCALE_FIT, 320, 1680, 1050);

    /* 16:9 gets a wider framebuffer instead of a stretched picture. */
    check_exact(1920, 1080, DISPLAY_SCALE_PIXEL_PERFECT, 384, 1920, 1000);
    check_exact(3840, 2160, DISPLAY_SCALE_PIXEL_PERFECT, 384, 3840, 2000);
    check_exact(1920, 1080, DISPLAY_SCALE_FIT, 352, 1901, 1080);

    /* Ultra wide screens stop at the widest framebuffer the levels can fill. */
    check_exact(3440, 1440, DISPLAY_SCALE_PIXEL_PERFECT, 384, 2688, 1400);

    /* A window with no size at all must not produce a broken framebuffer. */
    degenerate = display_compute_geometry(0, 0, DISPLAY_SCALE_PIXEL_PERFECT);
    if (degenerate.width != DISPLAY_BASE_WIDTH || degenerate.height != DISPLAY_HEIGHT) {
        printf("  FAIL [0x0]: expected the base framebuffer, got %dx%d \n",
            degenerate.width, degenerate.height);
        failures++;
    }

    if (failures == 0) {
        printf("  all checks passed \n");
        return 0;
    }

    printf("  %d check(s) failed \n", failures);
    return 1;
}
