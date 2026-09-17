/*
 * Blargg-style NTSC/composite video filter for Deadly Dave.
 *
 * A scalar C99 port of the scalar path of Shay Green's snes_ntsc 0.2.2
 * <http://www.slack.net/~ant/>, following the CannonBall-SE adaptations for
 * 32-bit RGBA output. The SIMD, hi-res and field-merge paths are not ported:
 * the game renders at its native resolution and always simulates interlacing
 * by advancing the burst phase every frame.
 *
 * Copyright (C) 2006-2007 Shay Green. This module is free software; you can
 * redistribute it and/or modify it under the terms of the GNU Lesser General
 * Public License as published by the Free Software Foundation; either version
 * 2.1 of the License, or (at your option) any later version.
 */

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include "ntsc.h"

#define NTSC_ALIGNMENT_COUNT 3
#define NTSC_BURST_COUNT     3
#define NTSC_RESCALE_IN      8
#define NTSC_RESCALE_OUT     7

#define NTSC_ARTIFACTS_MID 1.0f
#define NTSC_FRINGING_MID  1.0f
#define NTSC_STD_DECODER_HUE 0
#define NTSC_EXT_DECODER_HUE (NTSC_STD_DECODER_HUE + 15)

/*
 * Full 8-bit range per channel, which is what the original library uses. The
 * CannonBall-SE variant halves it (7 bits) to leave headroom for its doubled
 * hi-res pixels, and relies on a brightness-boost shader to bring the picture
 * back; this port has no such shader, so half range just renders the game dark.
 */
#define NTSC_RGB_BITS   8
#define NTSC_GAMMA_SIZE 256

#define NTSC_ENTRY_SIZE   128
#define NTSC_PALETTE_SIZE 0x8000
#define NTSC_BURST_SIZE   (NTSC_ENTRY_SIZE / NTSC_BURST_COUNT)

#define NTSC_KERNEL_HALF 16
#define NTSC_KERNEL_SIZE (NTSC_KERNEL_HALF * 2 + 1)
#define NTSC_RGB_KERNEL_SIZE (NTSC_BURST_SIZE / NTSC_ALIGNMENT_COUNT)

#define NTSC_RGB_UNIT   (1 << NTSC_RGB_BITS)
#define NTSC_RGB_OFFSET (NTSC_RGB_UNIT * 2 + 0.5f)

#define NTSC_PI 3.14159265358979323846f

enum { ntsc_black = 0 };

const ntsc_setup_t ntsc_composite = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
const ntsc_setup_t ntsc_svideo    = { 0, 0, 0, 0, .2f, 0, .2f, -1, -1, 0 };
const ntsc_setup_t ntsc_rgb       = { 0, 0, 0, 0, .2f, 0, .7f, -1, -1, -1 };

/* Packed signed RGB used inside the kernels. */
#define NTSC_PACK_RGB(r, g, b) ((r) << 21 | (g) << 11 | (b) << 1)
#define NTSC_RGB_BUILDER ((1L << 21) | (1 << 11) | (1 << 1))
#define NTSC_RGB_BIAS    (NTSC_RGB_UNIT * 2 * NTSC_RGB_BUILDER)
#define NTSC_CLAMP_MASK  (NTSC_RGB_BUILDER * 3 / 2)
#define NTSC_CLAMP_ADD   (NTSC_RGB_BUILDER * 0x101)

typedef struct ntsc_init_struct {
    float to_rgb[NTSC_BURST_COUNT * 6];
    float to_float[NTSC_GAMMA_SIZE];
    float contrast;
    float brightness;
    float artifacts;
    float fringing;
    float kernel[NTSC_RESCALE_OUT * NTSC_KERNEL_SIZE * 2];
} ntsc_init_t;

struct ntsc_filter_struct {
    uint32_t table[NTSC_PALETTE_SIZE][NTSC_ENTRY_SIZE];
};

typedef struct pixel_info_struct {
    int offset;
    float negate;
    float kernel[4];
} pixel_info_t;

#define NTSC_PIXEL_OFFSET_(ntsc, scaled) \
    (NTSC_KERNEL_SIZE / 2 + (ntsc) + ((scaled) != 0) + \
        (NTSC_RESCALE_OUT - (scaled)) % NTSC_RESCALE_OUT + \
        (NTSC_KERNEL_SIZE * 2 * (scaled)))

#define NTSC_PIXEL_OFFSET(ntsc, scaled) \
    NTSC_PIXEL_OFFSET_(((ntsc) - (scaled) / NTSC_RESCALE_OUT * NTSC_RESCALE_IN), \
        (((scaled) + NTSC_RESCALE_OUT * 10) % NTSC_RESCALE_OUT)), \
    (1.0f - (((ntsc) + 100) & 2))

static const pixel_info_t ntsc_pixels[NTSC_ALIGNMENT_COUNT] = {
    { NTSC_PIXEL_OFFSET(-4, -9), { 1, 1, .6667f, 0 } },
    { NTSC_PIXEL_OFFSET(-2, -7), { .3333f, 1, 1, .3333f } },
    { NTSC_PIXEL_OFFSET(0, -5),  { 0, .6667f, 1, 1 } },
};

#define NTSC_ROTATE_IQ(i, q, sin_b, cos_b) { \
    float ntsc_t = (i) * (cos_b) - (q) * (sin_b); \
    (q) = (i) * (sin_b) + (q) * (cos_b); \
    (i) = ntsc_t; \
}

static void ntsc_init_filters(ntsc_init_t *impl, const ntsc_setup_t *setup) {
    /* local copies of the chroma (low half) and luma (high half) kernels */
    float kernels[NTSC_KERNEL_SIZE * 2];
    int i;

    /* generate luma (y) filter using a sinc kernel with rolloff */
    {
        float rolloff = 1 + setup->sharpness * 0.032f;
        float maxh = 32;
        float pow_a_n = powf(rolloff, maxh);
        float sum;
        float to_angle = setup->resolution + 1;
        to_angle = NTSC_PI / maxh * 0.20f * (to_angle * to_angle + 1);

        kernels[NTSC_KERNEL_SIZE * 3 / 2] = maxh;
        for (i = 0; i < NTSC_KERNEL_HALF * 2 + 1; i++) {
            int x = i - NTSC_KERNEL_HALF;
            float angle = x * to_angle;

            if (x || pow_a_n > 1.056f || pow_a_n < 0.981f) {
                float rolloff_cos_a = rolloff * cosf(angle);
                float num = 1 - rolloff_cos_a -
                    pow_a_n * cosf(maxh * angle) +
                    pow_a_n * rolloff * cosf((maxh - 1) * angle);
                float den = 1 - rolloff_cos_a - rolloff_cos_a + rolloff * rolloff;
                float dsf = num / den;
                kernels[NTSC_KERNEL_SIZE * 3 / 2 - NTSC_KERNEL_HALF + i] = dsf - 0.5f;
            }
        }

        sum = 0;
        for (i = 0; i < NTSC_KERNEL_HALF * 2 + 1; i++) {
            float x = NTSC_PI * 2 / (NTSC_KERNEL_HALF * 2) * i;
            float blackman = 0.42f - 0.5f * cosf(x) + 0.08f * cosf(x * 2);
            sum += (kernels[NTSC_KERNEL_SIZE * 3 / 2 - NTSC_KERNEL_HALF + i] *= blackman);
        }

        sum = 1.0f / sum;
        for (i = 0; i < NTSC_KERNEL_HALF * 2 + 1; i++) {
            int x = NTSC_KERNEL_SIZE * 3 / 2 - NTSC_KERNEL_HALF + i;
            kernels[x] *= sum;
            assert(kernels[x] == kernels[x]);
        }
    }

    /* generate chroma (iq) filter using a gaussian kernel */
    {
        float cutoff_factor = -0.03125f;
        float cutoff = setup->bleed;

        if (cutoff < 0) {
            cutoff *= cutoff;
            cutoff *= cutoff;
            cutoff *= cutoff;
            cutoff *= -30.0f / 0.65f;
        }
        cutoff = cutoff_factor - 0.65f * cutoff_factor * cutoff;

        for (i = -NTSC_KERNEL_HALF; i <= NTSC_KERNEL_HALF; i++) {
            kernels[NTSC_KERNEL_SIZE / 2 + i] = expf(i * i * cutoff);
        }

        for (i = 0; i < 2; i++) {
            float sum = 0;
            int x;
            for (x = i; x < NTSC_KERNEL_SIZE; x += 2) {
                sum += kernels[x];
            }
            sum = 1.0f / sum;
            for (x = i; x < NTSC_KERNEL_SIZE; x += 2) {
                kernels[x] *= sum;
                assert(kernels[x] == kernels[x]);
            }
        }
    }

    /* generate linear rescale kernels (the 8 -> 7 horizontal resampling) */
    {
        float weight = 1.0f;
        float *out = impl->kernel;
        int n = NTSC_RESCALE_OUT;
        do {
            float remain = 0;
            weight -= 1.0f / NTSC_RESCALE_IN;
            for (i = 0; i < NTSC_KERNEL_SIZE * 2; i++) {
                float cur = kernels[i];
                float m = cur * weight;
                *out++ = m + remain;
                remain = cur - m;
            }
        } while (--n);
    }
}

static const float ntsc_default_decoder[6] =
    { 0.956f, 0.621f, -0.272f, -0.647f, -1.105f, 1.702f };

static void ntsc_init_setup(ntsc_init_t *impl, const ntsc_setup_t *setup) {
    impl->brightness = setup->brightness * (0.5f * NTSC_RGB_UNIT) + NTSC_RGB_OFFSET;
    impl->contrast = setup->contrast * (0.5f * NTSC_RGB_UNIT) + NTSC_RGB_UNIT;

    impl->artifacts = setup->artifacts;
    if (impl->artifacts > 0) {
        impl->artifacts *= NTSC_ARTIFACTS_MID * 1.5f - NTSC_ARTIFACTS_MID;
    }
    impl->artifacts = impl->artifacts * NTSC_ARTIFACTS_MID + NTSC_ARTIFACTS_MID;

    impl->fringing = setup->fringing;
    if (impl->fringing > 0) {
        impl->fringing *= NTSC_FRINGING_MID * 2 - NTSC_FRINGING_MID;
    }
    impl->fringing = impl->fringing * NTSC_FRINGING_MID + NTSC_FRINGING_MID;

    ntsc_init_filters(impl, setup);

    {
        float to_float = 1.0f / (NTSC_GAMMA_SIZE - 1);
        float gamma = 1.1333f - setup->gamma * 0.5f;
        int i;
        for (i = 0; i < NTSC_GAMMA_SIZE; i++) {
            impl->to_float[i] = powf(i * to_float, gamma) * impl->contrast + impl->brightness;
        }
    }

    {
        float hue = setup->hue * NTSC_PI + NTSC_PI / 180 * NTSC_EXT_DECODER_HUE;
        float sat = setup->saturation + 1;
        float s, c;
        float *out = impl->to_rgb;
        int n;

        hue += NTSC_PI / 180 * (NTSC_STD_DECODER_HUE - NTSC_EXT_DECODER_HUE);
        s = sinf(hue) * sat;
        c = cosf(hue) * sat;

        n = NTSC_BURST_COUNT;
        do {
            const float *in = ntsc_default_decoder;
            int m = 3;
            do {
                float ii = *in++;
                float qq = *in++;
                *out++ = ii * c - qq * s;
                *out++ = ii * s + qq * c;
            } while (--m);
            if (NTSC_BURST_COUNT <= 1) {
                break;
            }
            NTSC_ROTATE_IQ(s, c, 0.866025f, -0.5f);
        } while (--n);
    }
}

#define NTSC_RGB_TO_YIQ(r, g, b, y, i) (\
    (y = (r) * 0.299f + (g) * 0.587f + (b) * 0.114f),\
    (i = (r) * 0.596f - (g) * 0.275f - (b) * 0.321f),\
    ((r) * 0.212f - (g) * 0.523f + (b) * 0.311f)\
)

#define NTSC_YIQ_TO_RGB(y, i, q, to_rgb, type, r, g) (\
    (r) = (type)((y) + to_rgb[0] * (i) + to_rgb[1] * (q)),\
    (g) = (type)((y) + to_rgb[2] * (i) + to_rgb[3] * (q)),\
    (type)((y) + to_rgb[4] * (i) + to_rgb[5] * (q))\
)

static void ntsc_gen_kernel(ntsc_init_t *impl, float y, float i, float q, uint32_t *out) {
    const float *to_rgb = impl->to_rgb;
    int burst_remain = NTSC_BURST_COUNT;

    y -= NTSC_RGB_OFFSET;
    do {
        const pixel_info_t *pixel = ntsc_pixels;
        int alignment_remain = NTSC_ALIGNMENT_COUNT;
        do {
            float yy = y * impl->fringing * pixel->negate;
            float ic0 = (i + yy) * pixel->kernel[0];
            float qc1 = (q + yy) * pixel->kernel[1];
            float ic2 = (i - yy) * pixel->kernel[2];
            float qc3 = (q - yy) * pixel->kernel[3];

            float factor = impl->artifacts * pixel->negate;
            float ii = i * factor;
            float yc0 = (y + ii) * pixel->kernel[0];
            float yc2 = (y - ii) * pixel->kernel[2];

            float qq = q * factor;
            float yc1 = (y + qq) * pixel->kernel[1];
            float yc3 = (y - qq) * pixel->kernel[3];

            const float *k = &impl->kernel[pixel->offset];
            int n;
            ++pixel;
            for (n = NTSC_RGB_KERNEL_SIZE; n; --n) {
                float ci = k[0] * ic0 + k[2] * ic2;
                float cq = k[1] * qc1 + k[3] * qc3;
                float cy = k[NTSC_KERNEL_SIZE + 0] * yc0 + k[NTSC_KERNEL_SIZE + 1] * yc1 +
                    k[NTSC_KERNEL_SIZE + 2] * yc2 + k[NTSC_KERNEL_SIZE + 3] * yc3 +
                    NTSC_RGB_OFFSET;
                int r, g, b = NTSC_YIQ_TO_RGB(cy, ci, cq, to_rgb, int, r, g);

                *out++ = NTSC_PACK_RGB(r, g, b) - NTSC_RGB_BIAS;

                if (k < &impl->kernel[NTSC_KERNEL_SIZE * 2 * (NTSC_RESCALE_OUT - 1)]) {
                    k += NTSC_KERNEL_SIZE * 2 - 1;
                } else {
                    k -= NTSC_KERNEL_SIZE * 2 * (NTSC_RESCALE_OUT - 1) + 2;
                }
            }
        } while (--alignment_remain);

        if (NTSC_BURST_COUNT <= 1) {
            break;
        }

        to_rgb += 6;
        NTSC_ROTATE_IQ(i, q, -0.866025f, -0.5f);
    } while (--burst_remain);
}

static void ntsc_correct_errors(uint32_t color, uint32_t *out) {
    int n;
    for (n = NTSC_BURST_COUNT; n; --n) {
        unsigned i;
        for (i = 0; i < (unsigned)NTSC_RGB_KERNEL_SIZE / 2; i++) {
            uint32_t error = color -
                out[i] - out[(i + 12) % 14 + 14] - out[(i + 10) % 14 + 28] -
                out[i + 7] - out[i + 5 + 14] - out[i + 3 + 28];
            uint32_t fourth = (error + 2 * NTSC_RGB_BUILDER) >> 2;
            fourth &= (NTSC_RGB_BIAS >> 1) - NTSC_RGB_BUILDER;
            fourth -= NTSC_RGB_BIAS >> 2;
            out[i + 3 + 28] += fourth;
            out[i + 5 + 14] += fourth;
            out[i + 7] += fourth;
            out[i] += error - (fourth * 3);
        }
        out += NTSC_ALIGNMENT_COUNT * NTSC_RGB_KERNEL_SIZE;
    }
}

ntsc_filter_t *ntsc_create(const ntsc_setup_t *setup) {
    ntsc_init_t impl;
    ntsc_filter_t *ntsc;
    unsigned entry;

    if (setup == NULL) {
        setup = &ntsc_composite;
    }

    ntsc = (ntsc_filter_t *)calloc(1, sizeof(ntsc_filter_t));
    if (ntsc == NULL) {
        return NULL;
    }

    ntsc_init_setup(&impl, setup);

    for (entry = 0; entry < NTSC_PALETTE_SIZE; entry++) {
        /* RGB555: 5 bits per channel, expanded to the filter's 8-bit input. */
        unsigned r5 = (entry >> 10) & 0x1F;
        unsigned g5 = (entry >> 5) & 0x1F;
        unsigned b5 = entry & 0x1F;
        unsigned ir = (r5 << 3) | (r5 >> 2);
        unsigned ig = (g5 << 3) | (g5 >> 2);
        unsigned ib = (b5 << 3) | (b5 >> 2);
        float rr = impl.to_float[ir];
        float gg = impl.to_float[ig];
        float bb = impl.to_float[ib];
        float y, i, q;
        int r, g, b;
        uint32_t rgb;

        q = NTSC_RGB_TO_YIQ(rr, gg, bb, y, i);
        b = NTSC_YIQ_TO_RGB(y, i, q, impl.to_rgb, int, r, g);
        rgb = NTSC_PACK_RGB(r, g, b);

        ntsc_gen_kernel(&impl, y, i, q, ntsc->table[entry]);
        ntsc_correct_errors(rgb, ntsc->table[entry]);
    }

    return ntsc;
}

void ntsc_destroy(ntsc_filter_t *ntsc) {
    free(ntsc);
}

int ntsc_output_width(int in_width) {
    return ((in_width - 1) / 3 + 1) * 7;
}

/* Returns the kernel block of one palette entry inside one burst. */
#define NTSC_ENTRY(ktable, n) \
    ((const uint32_t *)((const uint8_t *)(ktable) + \
        (size_t)(n) * (NTSC_ENTRY_SIZE * sizeof(uint32_t))))

/*
 * Declares the sliding window of six kernels for one output row. Expands to
 * declarations, so it must run inside a block, not as an expression.
 */
#define NTSC_BEGIN_ROW(ntsc, burst, pixel0, pixel1, pixel2) \
    const uint32_t *ktable = (ntsc)->table[0] + (burst) * NTSC_BURST_SIZE; \
    const uint32_t *kernel0 = NTSC_ENTRY(ktable, (unsigned)(pixel0)); \
    const uint32_t *kernel1 = NTSC_ENTRY(ktable, (unsigned)(pixel1)); \
    const uint32_t *kernel2 = NTSC_ENTRY(ktable, (unsigned)(pixel2)); \
    const uint32_t *kernelx0; \
    const uint32_t *kernelx1 = kernel0; \
    const uint32_t *kernelx2 = kernel0

#define NTSC_COLOR_IN(index, color) { \
    kernelx##index = kernel##index; \
    kernel##index = NTSC_ENTRY(ktable, (unsigned)(color)); \
}

#define NTSC_CLAMP_(io, shift) { \
    uint32_t ntsc_sub = (io) >> (9 - (shift)) & NTSC_CLAMP_MASK; \
    uint32_t ntsc_clamp = NTSC_CLAMP_ADD - ntsc_sub; \
    (io) |= ntsc_clamp; \
    ntsc_clamp -= ntsc_sub; \
    (io) &= ntsc_clamp; \
}

#define NTSC_RGB_OUT_(rgb_out, io, alevel) { \
    uint32_t ntsc_rgba = (((io) << 3) & 0xFF000000u) | \
                         (((io) << 5) & 0x00FF0000u) | \
                         (((io) << 7) & 0x0000FF00u) | \
                         ((uint32_t)(alevel)); \
    (rgb_out) = ntsc_rgba; \
}

#define NTSC_RGB_OUT(x, rgb_out, alevel) { \
    uint32_t ntsc_raw = \
        kernel0[x] + kernel1[(x + 12) % 7 + 14] + kernel2[(x + 10) % 7 + 28] + \
        kernelx0[(x + 7) % 14] + kernelx1[(x + 5) % 7 + 21] + kernelx2[(x + 3) % 7 + 35]; \
    NTSC_CLAMP_(ntsc_raw, 8 - NTSC_RGB_BITS); \
    NTSC_RGB_OUT_(rgb_out, ntsc_raw, alevel); \
}

void ntsc_blit(const ntsc_filter_t *ntsc, const uint16_t *input, int in_stride,
    int burst_phase, int in_width, int in_height, uint32_t *out, int out_pitch) {
    int chunk_count = (in_width - 1) / 3;
    int row;

    for (row = 0; row < in_height; row++) {
        const uint16_t *line_in = input + (size_t)row * (size_t)in_stride;
        uint32_t *line_out = out + (size_t)row * (size_t)out_pitch;
        int n;

        NTSC_BEGIN_ROW(ntsc, burst_phase, ntsc_black, ntsc_black, line_in[0]);
        line_in++;

        for (n = chunk_count; n; --n) {
            /* Order of input and output pixels must not be altered. */
            NTSC_COLOR_IN(0, line_in[0]);
            NTSC_RGB_OUT(0, line_out[0], 255);
            NTSC_RGB_OUT(1, line_out[1], 255);

            NTSC_COLOR_IN(1, line_in[1]);
            NTSC_RGB_OUT(2, line_out[2], 255);
            NTSC_RGB_OUT(3, line_out[3], 255);

            NTSC_COLOR_IN(2, line_in[2]);
            NTSC_RGB_OUT(4, line_out[4], 255);
            NTSC_RGB_OUT(5, line_out[5], 255);
            NTSC_RGB_OUT(6, line_out[6], 255);

            line_in += 3;
            line_out += 7;
        }

        /* Finish the final columns with black. */
        NTSC_COLOR_IN(0, ntsc_black);
        NTSC_RGB_OUT(0, line_out[0], 255);
        NTSC_RGB_OUT(1, line_out[1], 255);

        NTSC_COLOR_IN(1, ntsc_black);
        NTSC_RGB_OUT(2, line_out[2], 255);
        NTSC_RGB_OUT(3, line_out[3], 255);

        NTSC_COLOR_IN(2, ntsc_black);
        NTSC_RGB_OUT(4, line_out[4], 255);
        NTSC_RGB_OUT(5, line_out[5], 255);
        NTSC_RGB_OUT(6, line_out[6], 255);

        burst_phase = (burst_phase + 1) % NTSC_BURST_COUNT;
    }
}
