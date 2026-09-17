/*
 * CRT-style output filters: scanlines and the Blargg NTSC filter.
 *
 * Both read the finished 200 pixel tall RGBA8888 game framebuffer. NTSC writes
 * it back at the game's own resolution, scanlines write it at the destination's
 * vertical resolution so their half-height bands land on whole pixels. Both are
 * CPU passes: no renderer features are involved, so they behave the same in
 * windowed and full screen mode.
 */

#include <stdlib.h>
#include <string.h>

#include "filter.h"
#include "ntsc.h"

/*
 * Scanline strength: the dark band keeps 1/2 of its value at most. The dimming
 * is weighted by luminance, as in CannonBall: a bright beam blooms into the gap
 * next to it and is dimmed the least, while a dark pixel is cut the most.
 */
#define SCANLINE_SHIFT 1

/*
 * The luminance weight saturates here instead of at full brightness, so even
 * white keeps a faint trace of the scanline. Without it a bright pixel is left
 * completely untouched and the effect disappears over the lighter artwork.
 */
#define SCANLINE_MAX_LUM 216

/*
 * Scanlines are half a game row tall, the way a 320x200 picture would look on a
 * 640x400 monitor: two bands fit in every source row. Drawing them therefore
 * takes at least twice the source rows. When the display scales by a whole
 * multiple of two source rows the texture keeps that doubled height and the GPU
 * scales it whole; everywhere else (an odd integer scale, or a fractional fit)
 * the texture is built at the height of the destination, one row per physical
 * row, and each output row carries the part of the dark half that lands on it.
 * That keeps the lines at half a game row on every device instead of assuming
 * the picture scales by a whole number of source rows.
 */
#define SCANLINE_VSCALE 2

/* Fixed point 1.0 for the per-row mix between the dark band and the original. */
#define SCANLINE_UNIT 65536u

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

int filter_output_height(int src_height, int dst_height) {
    int doubled;

    if (g_mode != FILTER_SCANLINES && g_mode != FILTER_BOTH) {
        return src_height;
    }

    doubled = src_height * SCANLINE_VSCALE;
    if (dst_height < doubled) {
        return src_height;
    }
    if ((dst_height % doubled) == 0) {
        return doubled;
    }
    return dst_height;
}

/* Quantises an RGBA8888 pixel to the 15-bit index the NTSC table expects. */
static uint16_t rgb555_of(uint32_t pixel) {
    uint16_t r = (uint16_t)((pixel >> 27) & 0x1F);
    uint16_t g = (uint16_t)((pixel >> 19) & 0x1F);
    uint16_t b = (uint16_t)((pixel >> 11) & 0x1F);
    return (uint16_t)((r << 10) | (g << 5) | b);
}

/*
 * The luminance-weighted dimming of one pixel, mixed toward the original by `w`
 * (SCANLINE_UNIT = the dark band, 0 = untouched). The mix is what lets an output
 * row that straddles the edge between the bright and the dark half carry both.
 */
static uint32_t scanline_pixel(uint32_t p, uint32_t w) {
    uint32_t r = (p >> 24) & 0xFF;
    uint32_t g = (p >> 16) & 0xFF;
    uint32_t b = (p >> 8) & 0xFF;
    uint32_t a = p & 0xFF;
    uint32_t lum = (77 * r + 150 * g + 29 * b) >> 8;

    uint32_t nr = r >> SCANLINE_SHIFT;
    uint32_t ng = g >> SCANLINE_SHIFT;
    uint32_t nb = b >> SCANLINE_SHIFT;

    if (lum > SCANLINE_MAX_LUM) {
        lum = SCANLINE_MAX_LUM;
    }

    nr = (nr * (255 - lum) + r * lum) >> 8;
    ng = (ng * (255 - lum) + g * lum) >> 8;
    nb = (nb * (255 - lum) + b * lum) >> 8;

    if (w < SCANLINE_UNIT) {
        nr = (nr * w + r * (SCANLINE_UNIT - w)) >> 16;
        ng = (ng * w + g * (SCANLINE_UNIT - w)) >> 16;
        nb = (nb * w + b * (SCANLINE_UNIT - w)) >> 16;
    }

    return (nr << 24) | (ng << 16) | (nb << 8) | a;
}

/*
 * How much of output row `t` falls in the dark half of its source row, in
 * SCANLINE_UNIT. A source row is out_height rows tall and its dark half is the
 * lower half, so the output row spans [start, start + src_height) and the dark
 * band is [out_height/2, out_height).
 */
static uint32_t scanline_weight(int t, int src_height, int out_height) {
    int start;
    int end;
    int dark_start;
    int a;
    int b;
    int overlap;

    if (out_height <= src_height) {
        /* Too short for half rows: keep the plain every-other-row band. */
        return (t & 1) ? SCANLINE_UNIT : 0u;
    }

    start = (int)(((long long)t * src_height) % out_height);
    end = start + src_height;
    dark_start = out_height / 2;

    a = start > dark_start ? start : dark_start;
    b = end < out_height ? end : out_height;
    overlap = b > a ? b - a : 0;

    return (uint32_t)(((long long)overlap * SCANLINE_UNIT) / src_height);
}

/*
 * Writes out_height rows from a src_height row image, darkening the lower half
 * of every source row. When src and dst are the same buffer this walks upwards,
 * so no source row is overwritten before it has been read; that is what lets
 * the NTSC result be scanlined in place.
 */
static void render_scanlines(const uint32_t *src, int src_pitch, int width,
    uint32_t *dst, int dst_pitch, int src_height, int out_height) {
    for (int t = out_height - 1; t >= 0; t--) {
        int y = (int)(((long long)t * src_height) / out_height);
        const uint32_t *srow = src + (size_t)y * (size_t)src_pitch;
        uint32_t *drow = dst + (size_t)t * (size_t)dst_pitch;
        uint32_t w = scanline_weight(t, src_height, out_height);

        if (w == 0) {
            memcpy(drow, srow, (size_t)width * sizeof(uint32_t));
        } else {
            for (int x = 0; x < width; x++) {
                drow[x] = scanline_pixel(srow[x], w);
            }
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
    uint32_t *dst, int dst_pitch, int src_height, int out_height) {
    int scanlines = (g_mode == FILTER_SCANLINES || g_mode == FILTER_BOTH);
    int out_width;

    if (g_mode == FILTER_OFF) {
        copy_image(src, src_pitch, src_width, dst, dst_pitch, src_height);
        return;
    }

    if (filter_ntsc_enabled() && ensure_ntsc()) {
        size_t pixels = (size_t)src_width * (size_t)src_height;

        out_width = ntsc_output_width(src_width);

        if (!ensure_input(pixels)) {
            /* Out of memory: fall back to a plain copy rather than a bad frame. */
            copy_image(src, src_pitch, src_width, dst, dst_pitch, src_height);
            return;
        }

        for (int y = 0; y < src_height; y++) {
            const uint32_t *row = src + (size_t)y * (size_t)src_pitch;
            uint16_t *in_row = g_input + (size_t)y * (size_t)src_width;
            for (int x = 0; x < src_width; x++) {
                in_row[x] = rgb555_of(row[x]);
            }
        }

        ntsc_blit(g_ntsc, g_input, src_width, g_ntsc_phase, src_width, src_height,
            dst, dst_pitch);

        if (scanlines) {
            /* In place: the blit already left the picture in dst. */
            render_scanlines(dst, dst_pitch, out_width, dst, dst_pitch,
                src_height, out_height);
        }

        g_ntsc_phase = (g_ntsc_phase + 1) % 3;
    } else if (scanlines) {
        render_scanlines(src, src_pitch, src_width, dst, dst_pitch,
            src_height, out_height);
    } else {
        copy_image(src, src_pitch, src_width, dst, dst_pitch, src_height);
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
