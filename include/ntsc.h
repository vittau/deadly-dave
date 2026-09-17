#ifndef _NTSC_H_
#define _NTSC_H_

#include <stdint.h>

/*
 * Blargg-style NTSC/composite video filter, a scalar C99 port of the scalar
 * path of Shay Green's snes_ntsc 0.2.2.
 *
 * The filter takes 15-bit RGB555 pixels (one palette index per pixel) and
 * writes 32-bit RGBA8888 pixels. Each input pixel is expanded into output
 * pixels at the composite-video horizontal sampling rate: 3 input columns
 * become 7 output columns, so a filtered image is wider than its source and is
 * meant to be stretched back to the same on-screen size.
 *
 * Initialisation precomputes a kernel for every one of the 32768 colours in
 * the RGB555 palette, which costs ~16 MB and a noticeable (one-off) amount of
 * time, so the caller creates the filter lazily and keeps it.
 *
 * snes_ntsc is Copyright (C) 2006-2007 Shay Green, LGPL 2.1 or later.
 */

typedef struct ntsc_setup_struct {
    /* All ranges follow snes_ntsc: 0 is neutral for every field. */
    float hue;        /* -1 = -180 degrees, +1 = +180 degrees               */
    float saturation; /* -1 = grayscale, +1 = oversaturated                 */
    float contrast;   /* -1 = dark, +1 = light                              */
    float brightness; /* -1 = dark, +1 = light                              */
    float sharpness;  /* edge contrast enhancement / blurring               */
    float gamma;      /* -1 = dark, +1 = light                              */
    float resolution; /* image resolution                                   */
    float artifacts;  /* artefacts caused by colour changes                 */
    float fringing;   /* colour artefacts caused by brightness changes       */
    float bleed;      /* colour bleed (reduced colour resolution)           */
} ntsc_setup_t;

/* The three classic connection presets; composite is the full NTSC look. */
extern const ntsc_setup_t ntsc_composite;
extern const ntsc_setup_t ntsc_svideo;
extern const ntsc_setup_t ntsc_rgb;

typedef struct ntsc_filter_struct ntsc_filter_t;

/*
 * Builds the filter and its palette table. Returns NULL when the table cannot
 * be allocated. `setup` may be NULL for the composite preset.
 */
ntsc_filter_t *ntsc_create(const ntsc_setup_t *setup);
void ntsc_destroy(ntsc_filter_t *ntsc);

/*
 * Output width produced for a given input width: 7 columns per 3 input
 * columns. Use it to size the destination buffer and the display texture.
 */
int ntsc_output_width(int in_width);

/*
 * Filters one image. Input is RGB555 indices (0..32767), out is RGBA8888.
 * `in_stride` and `out_pitch` are in pixels. `burst_phase` cycles through
 * 0,1,2 between frames; the caller advances it so a still image shimmers the
 * way a real NTSC signal does.
 */
void ntsc_blit(const ntsc_filter_t *ntsc, const uint16_t *input, int in_stride,
    int burst_phase, int in_width, int in_height, uint32_t *out, int out_pitch);

#endif
