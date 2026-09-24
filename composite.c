/*
 * CGA composite output. See include/composite.h for what it models and why the
 * Blargg filter does not.
 *
 * This is reenigne's CGA composite algorithm, from his DOSBox patch, as 86Box
 * carries it in src/video/vid_cga_comp.c (GPL 2 or later; Copyright 2015-2019
 * reenigne, Copyright 2015-2019 Miran Grca), cut down to what the game uses:
 * the original IBM CGA (the "old" revision), 320x200 graphics with the colour
 * burst on (BIOS mode 4, what Dangerous Dave sets), a black border and the
 * emulators' default knobs (brightness 0, contrast 100, saturation 100,
 * sharpness 0, hue 0). The waveform comes from his oscilloscope measurements of
 * a real card; nothing in here is tuned by eye.
 *
 * The card's signal is built one hdot at a time (the 14.318 MHz dot clock, 4
 * hdots to a colour cycle, 2 to a 320-mode pixel). What an hdot puts out
 * depends on its own colour, the next hdot's colour (the card's colour
 * multiplexer does not switch instantly) and where in the colour cycle it
 * falls, which is what g_table holds. A television then decodes it: chroma is
 * demodulated against the subcarrier over a cycle, luma is what is left, and
 * both go back to RGB. Every hdot is one output pixel.
 */

#include <math.h>
#include <stdlib.h>

#include "composite.h"

#define HDOTS_PER_PIXEL 2
#define TAU 6.28318531

/*
 * The measured output of the colour multiplexer, indexed by
 * (left colour & 7) << 5 | (right colour & 7) << 2 | phase, left being this
 * hdot, right the next one, phase the hdot's place in the colour cycle.
 */
static const unsigned char g_chroma_multiplexer[256] = {
      2,   2,  2,   2, 114, 174,   4,  3,   2,  1, 133, 135,   2, 113, 150,   4,
    133,   2,  1,  99, 151, 152,   2,  1,   3,  2,  96, 136, 151, 152, 151, 152,
      2,  56, 62,   4, 111, 250, 118,  4,   0, 51, 207, 137,   1, 171, 209,   5,
    140,  50, 54, 100, 133, 202,  57,  4,   2, 50, 153, 149, 128, 198, 198, 135,
     32,   1, 36,  81, 147, 158,   1, 42,  33,  1, 210, 254,  34, 109, 169,  77,
    177,   2,  0, 165, 189, 154,   3, 44,  33,  0,  91, 197, 178, 142, 144, 192,
      4,   2, 61,  67, 117, 151, 112, 83,   4,  0, 249, 255,   3, 107, 249, 117,
    147,   1, 50, 162, 143, 141,  52, 54,   3,  0, 145, 206, 124, 123, 192, 193,
     72,  78,  2,   0, 159, 208,   4,  0,  53, 58, 164, 159,  37, 159, 171,   1,
    248, 117,  4,  98, 212, 218,   5,  2,  54, 59,  93, 121, 176, 181, 134, 130,
      1,  61, 31,   0, 160, 255,  34,  1,   1, 58, 197, 166,   0, 177, 194,   2,
    162, 111, 34,  96, 205, 253,  32,  1,   1, 57, 123, 125, 119, 188, 150, 112,
     78,   4,  0,  75, 166, 180,  20, 38,  78,  1, 143, 246,  42, 113, 156,  37,
    252,   4,  1, 188, 175, 129,   1, 37, 118,  4,  88, 249, 202, 150, 145, 200,
     61,  59, 60,  60, 228, 252, 117, 77,  60, 58, 248, 251,  81, 212, 254, 107,
    198,  59, 58, 169, 250, 251,  81, 80, 100, 58, 154, 250, 251, 252, 252, 252,
};

/* The measured level the intensity bits add, indexed by left | right << 1. */
static const double g_intensity[4] = {
    77.175381, 88.654656, 166.564623, 174.228438,
};

/* The 16 RGBI colours, index bits: 1 blue, 2 green, 4 red, 8 intensity. */
static const unsigned char g_rgbi[16][3] = {
    { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0xAA }, { 0x00, 0xAA, 0x00 }, { 0x00, 0xAA, 0xAA },
    { 0xAA, 0x00, 0x00 }, { 0xAA, 0x00, 0xAA }, { 0xAA, 0x55, 0x00 }, { 0xAA, 0xAA, 0xAA },
    { 0x55, 0x55, 0x55 }, { 0x55, 0x55, 0xFF }, { 0x55, 0xFF, 0x55 }, { 0x55, 0xFF, 0xFF },
    { 0xFF, 0x55, 0x55 }, { 0xFF, 0x55, 0xFF }, { 0xFF, 0xFF, 0x55 }, { 0xFF, 0xFF, 0xFF },
};

/*
 * The signal level of one hdot, scaled to 0..256, indexed by
 * left colour << 6 | right colour << 2 | phase.
 */
static int g_table[1024];
/* The decoder's I/Q to RGB weights, with the hue and saturation folded in. */
static int g_ri, g_rq, g_gi, g_gq, g_bi, g_bq;
/* RGB555 to the nearest RGBI colour. */
static unsigned char *g_nearest = NULL;

/* One row: the signal (w + 10 hdots) and its two chroma components (w + 2). */
static int *g_signal = NULL;
static int *g_chroma_a = NULL;
static int *g_chroma_b = NULL;
static int g_row_hdots = 0;

static int build_tables(void) {
    /* 320x200 graphics: the hue offset the emulators use for every mode but 80 column text. */
    const double mode_hue = 4.0;
    const double saturation = 2.9;
    double min_v = g_chroma_multiplexer[0] + g_intensity[0];
    double max_v = g_chroma_multiplexer[255] + g_intensity[3];
    double contrast = 256.0 / (max_v - min_v);
    double brightness = -min_v * contrast;
    double i, q, a, c, s, r, adjust_i, adjust_q;

    g_nearest = (unsigned char *)malloc(32768);
    if (g_nearest == NULL) {
        return 0;
    }

    for (int x = 0; x < 1024; x++) {
        int phase = x & 3;
        int right = (x >> 2) & 15;
        int left = (x >> 6) & 15;
        double v = g_chroma_multiplexer[((left & 7) << 5) | ((right & 7) << 2) | phase] +
            g_intensity[(left >> 3) | ((right >> 2) & 2)];

        g_table[x] = (int)(v * contrast + brightness);
    }

    /* The colour burst is colour 6 on the card: its phase is the reference. */
    i = g_table[6 * 68] - g_table[6 * 68 + 2];
    q = g_table[6 * 68 + 1] - g_table[6 * 68 + 3];
    a = TAU * (33 + 90 + mode_hue) / 360.0;
    c = cos(a);
    s = sin(a);
    r = 256 * saturation / sqrt(i * i + q * q);
    adjust_i = -(i * c + q * s) * r;
    adjust_q = (q * c - i * s) * r;

    g_ri = (int)(0.9563 * adjust_i + 0.6210 * adjust_q);
    g_rq = (int)(-0.9563 * adjust_q + 0.6210 * adjust_i);
    g_gi = (int)(-0.2721 * adjust_i + -0.6474 * adjust_q);
    g_gq = (int)(0.2721 * adjust_q + -0.6474 * adjust_i);
    g_bi = (int)(-1.1069 * adjust_i + 1.7046 * adjust_q);
    g_bq = (int)(1.1069 * adjust_q + 1.7046 * adjust_i);

    for (int rgb = 0; rgb < 32768; rgb++) {
        int red = ((rgb >> 10) & 31) * 255 / 31;
        int green = ((rgb >> 5) & 31) * 255 / 31;
        int blue = (rgb & 31) * 255 / 31;
        int best = 0;
        int best_distance = -1;

        for (int k = 0; k < 16; k++) {
            int dr = red - g_rgbi[k][0];
            int dg = green - g_rgbi[k][1];
            int db = blue - g_rgbi[k][2];
            int distance = dr * dr + dg * dg + db * db;
            if (best_distance < 0 || distance < best_distance) {
                best = k;
                best_distance = distance;
            }
        }
        g_nearest[rgb] = (unsigned char)best;
    }
    return 1;
}

static int ensure_row(int hdots) {
    if (g_row_hdots >= hdots) {
        return 1;
    }
    free(g_signal);
    free(g_chroma_a);
    free(g_chroma_b);
    g_signal = (int *)calloc((size_t)hdots + 10, sizeof(int));
    g_chroma_a = (int *)calloc((size_t)hdots + 2, sizeof(int));
    g_chroma_b = (int *)calloc((size_t)hdots + 2, sizeof(int));
    if (g_signal == NULL || g_chroma_a == NULL || g_chroma_b == NULL) {
        g_row_hdots = 0;
        return 0;
    }
    g_row_hdots = hdots;
    return 1;
}

static uint32_t byte_clamp(int v) {
    if (v < 0) {
        return 0;
    }
    v >>= 13;
    return v > 255 ? 255 : (uint32_t)v;
}

int composite_output_width(int src_width) {
    return src_width * HDOTS_PER_PIXEL;
}

void composite_render(const uint32_t *src, int src_pitch, int src_width,
    uint32_t *dst, int dst_pitch, int height) {
    int w = src_width * HDOTS_PER_PIXEL;

    if ((g_nearest == NULL && !build_tables()) || !ensure_row(w)) {
        return;
    }

    for (int y = 0; y < height; y++) {
        const uint32_t *in = src + (size_t)y * (size_t)src_pitch;
        uint32_t *out = dst + (size_t)y * (size_t)dst_pitch;
        int *o = g_signal;
        int *sample;
        int *ap = g_chroma_a + 1;
        int *bp = g_chroma_b + 1;
        int prev = 0;

        /*
         * The signal: 4 hdots of black border, the step into the first pixel,
         * every hdot of the row (each with the colour of the one after it), the
         * step out into the border and 5 more of border. Hdot x of the row is at
         * phase x & 3: the picture's first column starts a colour cycle.
         */
        for (int x = 0; x < 4; x++) {
            *o++ = g_table[(x + 3) & 3];
        }
        for (int x = 0; x < src_width; x++) {
            uint32_t p = in[x];
            int rgb555 = (int)(((p >> 27) & 31) << 10 | ((p >> 19) & 31) << 5 | ((p >> 11) & 31));
            int colour = g_nearest[rgb555];

            if (x == 0) {
                *o++ = g_table[(colour << 2) | 3];
            } else {
                /* The second hdot of the previous pixel, followed by this one. */
                *o++ = g_table[(prev << 6) | (colour << 2) | ((2 * x - 1) & 3)];
            }
            /* The first hdot of this pixel is followed by its second. */
            *o++ = g_table[(colour << 6) | (colour << 2) | ((2 * x) & 3)];
            prev = colour;
        }
        *o++ = g_table[(prev << 6) | ((w - 1) & 3)];
        for (int x = 0; x < 5; x++) {
            *o++ = g_table[x & 3];
        }

        /* Demodulate the colour: two components a quarter cycle apart. */
        sample = g_signal + 4;
        for (int x = -1; x < w + 1; x++) {
            ap[x] = sample[-4] - 2 * (sample[-2] - sample[0] + sample[2]) + sample[4];
            bp[x] = 2 * (sample[-3] - sample[-1] + sample[1] - sample[3]);
            sample++;
        }

        /*
         * Luma is the signal with the colour taken out, averaged over three
         * hdots; the colour components rotate a quarter turn every hdot.
         */
        sample = g_signal + 5;
        sample[-1] = sample[-1] * 8 - ap[-1];
        sample[0] = sample[0] * 8 - ap[0];
        for (int x = 0; x < w; x++) {
            int a = ap[0];
            int b = bp[0];
            int ci, cq, luma;

            sample[1] = sample[1] * 8 - ap[1];
            luma = (2 * sample[0] + sample[-1] + sample[1]) * 256;
            switch (x & 3) {
            case 0: ci = a;  cq = b;  break;
            case 1: ci = -b; cq = a;  break;
            case 2: ci = -a; cq = -b; break;
            default: ci = b; cq = -a; break;
            }
            out[x] = (byte_clamp(luma + g_ri * ci + g_rq * cq) << 24) |
                (byte_clamp(luma + g_gi * ci + g_gq * cq) << 16) |
                (byte_clamp(luma + g_bi * ci + g_bq * cq) << 8) | 0xFF;
            sample++;
            ap++;
            bp++;
        }
    }
}

void composite_quit(void) {
    free(g_nearest);
    g_nearest = NULL;
    free(g_signal);
    free(g_chroma_a);
    free(g_chroma_b);
    g_signal = g_chroma_a = g_chroma_b = NULL;
    g_row_hdots = 0;
}
