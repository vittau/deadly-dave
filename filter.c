/*
 * CRT-style output filters: scanlines and the Blargg NTSC filter.
 *
 * Both work on the finished 200 pixel tall RGBA8888 game framebuffer at the
 * game's own resolution, and both are CPU passes: no renderer features are
 * involved, so they behave the same in windowed and full screen mode.
 */

#include <stdlib.h>
#include <string.h>

#include "filter.h"
#include "ntsc.h"

/*
 * Scanline strength: the odd rows keep 1/2 of their value at most. The dimming
 * is weighted by luminance, as in CannonBall: a bright beam blooms into the gap
 * next to it and is left almost untouched, while a dark pixel is cut the most.
 */
#define SCANLINE_SHIFT 1

static int g_mode = FILTER_OFF;
static ntsc_filter_t *g_ntsc = NULL;
static int g_ntsc_phase = 0;

/* RGB555 scratch for the NTSC blitter, grown on demand. */
static uint16_t *g_input = NULL;
static size_t g_input_pixels = 0;

void filter_set_mode(int mode) {
    if (mode < FILTER_OFF || mode >= FILTER_MODE_COUNT) {
        mode = FILTER_OFF;
    }
    g_mode = mode;
}

int filter_mode(void) {
    return g_mode;
}

int filter_ntsc_enabled(void) {
    return g_mode == FILTER_NTSC || g_mode == FILTER_BOTH;
}

int filter_output_width(int src_width) {
    if (filter_ntsc_enabled()) {
        return ntsc_output_width(src_width);
    }
    return src_width;
}

/* Quantises an RGBA8888 pixel to the 15-bit index the NTSC table expects. */
static uint16_t rgb555_of(uint32_t pixel) {
    uint16_t r = (uint16_t)((pixel >> 27) & 0x1F);
    uint16_t g = (uint16_t)((pixel >> 19) & 0x1F);
    uint16_t b = (uint16_t)((pixel >> 11) & 0x1F);
    return (uint16_t)((r << 10) | (g << 5) | b);
}

static void apply_scanlines(uint32_t *pixels, int pitch, int width, int height) {
    for (int y = 1; y < height; y += 2) {
        uint32_t *row = pixels + (size_t)y * (size_t)pitch;

        for (int x = 0; x < width; x++) {
            uint32_t p = row[x];
            uint32_t r = (p >> 24) & 0xFF;
            uint32_t g = (p >> 16) & 0xFF;
            uint32_t b = (p >> 8) & 0xFF;
            uint32_t a = p & 0xFF;

            uint32_t lum = (77 * r + 150 * g + 29 * b) >> 8;

            uint32_t rd = r >> SCANLINE_SHIFT;
            uint32_t gd = g >> SCANLINE_SHIFT;
            uint32_t bd = b >> SCANLINE_SHIFT;

            uint32_t nr = (rd * (255 - lum) + r * lum) >> 8;
            uint32_t ng = (gd * (255 - lum) + g * lum) >> 8;
            uint32_t nb = (bd * (255 - lum) + b * lum) >> 8;

            row[x] = (nr << 24) | (ng << 16) | (nb << 8) | a;
        }
    }
}

static void copy_image(const uint32_t *src, int src_pitch, int src_width,
    uint32_t *dst, int dst_pitch, int height) {
    for (int y = 0; y < height; y++) {
        memcpy(dst + (size_t)y * (size_t)dst_pitch,
            src + (size_t)y * (size_t)src_pitch,
            (size_t)src_width * sizeof(uint32_t));
    }
}

static int ensure_ntsc(void) {
    if (g_ntsc == NULL) {
        g_ntsc = ntsc_create(NULL);
    }
    return g_ntsc != NULL;
}

static int ensure_input(size_t pixels) {
    if (g_input_pixels >= pixels) {
        return 1;
    }
    g_input = (uint16_t *)realloc(g_input, pixels * sizeof(uint16_t));
    if (g_input == NULL) {
        g_input_pixels = 0;
        return 0;
    }
    g_input_pixels = pixels;
    return 1;
}

void filter_render(const uint32_t *src, int src_pitch, int src_width,
    uint32_t *dst, int dst_pitch, int height) {
    int out_width;

    if (g_mode == FILTER_OFF) {
        copy_image(src, src_pitch, src_width, dst, dst_pitch, height);
        return;
    }

    if (filter_ntsc_enabled() && ensure_ntsc()) {
        size_t pixels = (size_t)src_width * (size_t)height;

        out_width = ntsc_output_width(src_width);

        if (!ensure_input(pixels)) {
            /* Out of memory: fall back to a plain copy rather than a bad frame. */
            copy_image(src, src_pitch, src_width, dst, dst_pitch, height);
            return;
        }

        for (int y = 0; y < height; y++) {
            const uint32_t *row = src + (size_t)y * (size_t)src_pitch;
            uint16_t *in_row = g_input + (size_t)y * (size_t)src_width;
            for (int x = 0; x < src_width; x++) {
                in_row[x] = rgb555_of(row[x]);
            }
        }

        ntsc_blit(g_ntsc, g_input, src_width, g_ntsc_phase, src_width, height,
            dst, dst_pitch);
        g_ntsc_phase = (g_ntsc_phase + 1) % 3;
    } else {
        out_width = src_width;
        copy_image(src, src_pitch, src_width, dst, dst_pitch, height);
    }

    if (g_mode == FILTER_SCANLINES || g_mode == FILTER_BOTH) {
        apply_scanlines(dst, dst_pitch, out_width, height);
    }
}

void filter_quit(void) {
    ntsc_destroy(g_ntsc);
    g_ntsc = NULL;
    free(g_input);
    g_input = NULL;
    g_input_pixels = 0;
    g_ntsc_phase = 0;
}
