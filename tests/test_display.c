#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
 * framebuffer, fit horizontally and never run past the bottom edge. The scene
 * (the part between the two HUD bars) is the thing that gets centered
 * vertically, but only while the picture has room to move.
 */
static void check_invariants(int out_w, int out_h, int mode) {
    display_geometry_t g;
    double scale_x;
    double scale_y;
    double drift;
    double scene_top;
    double scene_bottom;

    g = display_compute_geometry(out_w, out_h, mode);
    scale_x = (double)g.dst.w / (double)g.width;
    scale_y = (double)g.dst.h / (double)g.height;
    drift = (scale_x > scale_y) ? (scale_x - scale_y) : (scale_y - scale_x);

    scene_top = g.dst.y + (DISPLAY_SCENE_TOP * scale_y);
    scene_bottom = g.dst.y + (DISPLAY_SCENE_BOTTOM * scale_y);

    check(g.height == DISPLAY_HEIGHT, "framebuffer height is not 200", out_w, out_h, mode);
    check(g.width >= DISPLAY_BASE_WIDTH && g.width <= DISPLAY_MAX_WIDTH,
        "framebuffer width out of range", out_w, out_h, mode);
    check((g.width % 8) == 0, "framebuffer width is not on the 8 pixel grid", out_w, out_h, mode);
    check(g.dst.w <= out_w, "picture is wider than the window", out_w, out_h, mode);
    check(g.dst.x == (out_w - g.dst.w) / 2, "picture is not centered horizontally", out_w, out_h, mode);
    check(g.dst.y >= 0, "picture starts above the window", out_w, out_h, mode);
    check(g.dst.y + g.dst.h <= out_h, "picture runs past the bottom edge", out_w, out_h, mode);
    check(scene_top >= -0.5 && scene_bottom <= out_h + 0.5, "scene does not fit in the window", out_w, out_h, mode);
    if (g.dst.y + g.dst.h < out_h) {
        /* There is room, so the scene gets to be centered. */
        check(fabs(((scene_top + scene_bottom) / 2.0) - (out_h / 2.0)) <= 1.0,
            "scene is not centered", out_w, out_h, mode);
    } else {
        /* No room at all: the picture is flush with the bottom instead. */
        check(g.dst.y == out_h - g.dst.h, "picture is not flush with the bottom", out_w, out_h, mode);
    }
    check(drift < 0.02, "pixels are not square, the picture is stretched", out_w, out_h, mode);

    if (mode == DISPLAY_SCALE_1X && out_w >= DISPLAY_BASE_WIDTH && out_h >= DISPLAY_HEIGHT) {
        check(g.scale == 1 && g.dst.w == g.width && g.dst.h == g.height,
            "1x did not keep one screen pixel per game pixel", out_w, out_h, mode);
    }
    if ((mode == DISPLAY_SCALE_2X || mode == DISPLAY_SCALE_3X) &&
            out_w >= DISPLAY_BASE_WIDTH && out_h >= DISPLAY_HEIGHT) {
        check(g.scale >= 1 && g.scale <= mode - DISPLAY_SCALE_1X + 1 &&
            g.dst.w == g.width * g.scale && g.dst.h == g.height * g.scale,
            "fixed scale went past its factor", out_w, out_h, mode);
    }
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
        check_invariants(resolutions[i][0], resolutions[i][1], DISPLAY_SCALE_1X);
        check_invariants(resolutions[i][0], resolutions[i][1], DISPLAY_SCALE_2X);
        check_invariants(resolutions[i][0], resolutions[i][1], DISPLAY_SCALE_3X);
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

    /* Ultra wide screens keep widening the framebuffer, they do not stop. */
    check_exact(3440, 1440, DISPLAY_SCALE_PIXEL_PERFECT, 488, 3416, 1400);
    check_exact(5120, 1440, DISPLAY_SCALE_PIXEL_PERFECT, 728, 5096, 1400);

    /*
     * 1x never scales: the window width becomes level up to the 100 columns
     * there are, and the picture stays 200 pixels tall with black around it.
     */
    check_exact(1280, 800, DISPLAY_SCALE_1X, 1280, 1280, 200);
    check_exact(960, 600, DISPLAY_SCALE_1X, 960, 960, 200);
    check_exact(1000, 600, DISPLAY_SCALE_1X, 1000, 1000, 200);
    check_exact(3440, 1440, DISPLAY_SCALE_1X, 1600, 1600, 200);
    check_exact(320, 200, DISPLAY_SCALE_1X, 320, 320, 200);

    /* 2x is the same at a factor of two, and drops to 1x where 2x does not fit. */
    check_exact(1920, 1080, DISPLAY_SCALE_2X, 960, 1920, 400);
    check_exact(3440, 1440, DISPLAY_SCALE_2X, 1600, 3200, 400);
    check_exact(960, 600, DISPLAY_SCALE_2X, 480, 960, 400);
    check_exact(640, 400, DISPLAY_SCALE_2X, 320, 640, 400);
    check_exact(600, 380, DISPLAY_SCALE_2X, 600, 600, 200);

    /* 3x likewise, dropping to 2x where 3x does not fit. */
    check_exact(1920, 1080, DISPLAY_SCALE_3X, 640, 1920, 600);
    check_exact(3440, 1440, DISPLAY_SCALE_3X, 1144, 3432, 600);
    check_exact(1280, 800, DISPLAY_SCALE_3X, 424, 1272, 600);
    check_exact(960, 600, DISPLAY_SCALE_3X, 320, 960, 600);
    check_exact(900, 560, DISPLAY_SCALE_3X, 448, 896, 400);

    /* A window with no size at all must not produce a broken framebuffer. */
    degenerate = display_compute_geometry(0, 0, DISPLAY_SCALE_PIXEL_PERFECT);
    if (degenerate.width != DISPLAY_BASE_WIDTH || degenerate.height != DISPLAY_HEIGHT) {
        printf("  FAIL [0x0]: expected the base framebuffer, got %dx%d \n",
            degenerate.width, degenerate.height);
        failures++;
    }

    /*
     * 1280x800 is the Steam Deck and the picture fills it exactly, so there is
     * no room for the nudge that centers the scene: anything but 0 here cuts the
     * bottom rows off the screen, trophy banner included.
     */
    {
        display_geometry_t deck = display_compute_geometry(1280, 800, DISPLAY_SCALE_PIXEL_PERFECT);
        if (deck.dst.y != 0) {
            printf("  FAIL [1280x800]: picture pushed to y %d, the bottom of the framebuffer is off screen \n",
                deck.dst.y);
            failures++;
        }
    }

    if (failures == 0) {
        printf("  all checks passed \n");
        return 0;
    }

    printf("  %d check(s) failed \n", failures);
    return 1;
}
