#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "filter.h"

static int failures = 0;

static void expect_int(const char *what, int got, int want) {
    if (got != want) {
        printf("  FAIL: %s: expected %d, got %d \n", what, want, got);
        failures++;
    }
}

static void expect_pixel(const char *what, uint32_t got, uint32_t want) {
    if (got != want) {
        printf("  FAIL: %s: expected %08x, got %08x \n", what, want, got);
        failures++;
    }
}

/*
 * filter_output_height is what tells the display how tall the texture has to
 * be. Doubling only helps when the extra rows can land on whole pixels, so it
 * is used for a whole multiple of two source rows and the destination height
 * itself everywhere else.
 */
static void check_output_height(void) {
    filter_set_mode(FILTER_OFF);
    expect_int("off keeps the source height", filter_output_height(200, 800), 200);
    expect_int("ntsc keeps the source height",
        (filter_set_mode(FILTER_NTSC), filter_output_height(200, 800)), 200);

    filter_set_mode(FILTER_SCANLINES);
    expect_int("too small for half rows", filter_output_height(200, 200), 200);
    expect_int("below two source rows", filter_output_height(200, 300), 200);
    expect_int("exactly two source rows", filter_output_height(200, 400), 400);
    expect_int("an even multiple", filter_output_height(200, 800), 400);
    expect_int("an odd multiple", filter_output_height(200, 600), 600);
    expect_int("a fractional fit", filter_output_height(200, 1080), 1080);

    filter_set_mode(FILTER_BOTH);
    expect_int("both doubles like scanlines", filter_output_height(200, 800), 400);
}

/*
 * A source row is a mid grey, so the luminance-weighted dimming has a value
 * that is neither the original nor black. With two output rows per source row
 * the second row is the dark band; with three, the middle row straddles the
 * edge and carries half of the darkening.
 */
static void check_scanline_pattern(void) {
    uint32_t src[2] = { 0x808080FFu, 0x808080FFu };
    uint32_t white[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu };
    uint32_t dst[6];
    const uint32_t bright = 0x808080FFu;
    const uint32_t half = 0x6F6F6FFF;   /* (95 + 128) / 2 */
    const uint32_t dark = 0x5F5F5FFF;   /* (128 >> 1) weighted */
    int i;

    filter_set_mode(FILTER_SCANLINES);

    memset(dst, 0, sizeof(dst));
    filter_render(src, 1, 1, dst, 1, 2, 4);
    expect_pixel("2x row 0 is bright", dst[0], bright);
    expect_pixel("2x row 1 is dark", dst[1], dark);
    expect_pixel("2x row 2 is bright", dst[2], bright);
    expect_pixel("2x row 3 is dark", dst[3], dark);

    memset(dst, 0, sizeof(dst));
    filter_render(src, 1, 1, dst, 1, 2, 6);
    for (i = 0; i < 2; i++) {
        expect_pixel("3x first row is bright", dst[i * 3 + 0], bright);
        expect_pixel("3x second row straddles", dst[i * 3 + 1], half);
        expect_pixel("3x third row is dark", dst[i * 3 + 2], dark);
    }

    /*
     * White saturates the luminance weight, but the dark band must still be a
     * little darker so the effect does not vanish over the bright artwork.
     */
    memset(dst, 0, sizeof(dst));
    filter_render(white, 1, 1, dst, 1, 2, 4);
    expect_pixel("white stays bright", dst[0], 0xFFFFFFFFu);
    expect_pixel("white is still dimmed a little", dst[1], 0xEAEAEAFFu);
}

static int channel(uint32_t p, int shift) {
    return (int)((p >> shift) & 0xFF);
}

/*
 * One channel averaged over the colour cycle starting at dst[16], in the
 * middle of the row: the colour the eye sees under the fine structure the
 * decoder leaves.
 */
static int cycle_channel(const uint32_t *dst, int shift) {
    return (channel(dst[16], shift) + channel(dst[17], shift) +
        channel(dst[18], shift) + channel(dst[19], shift)) / 4;
}

/* Renders 16 pixels of `a` on the even columns and `b` on the odd ones. */
static void render_stripes(uint32_t a, uint32_t b, uint32_t *dst) {
    uint32_t src[16];
    int i;

    for (i = 0; i < 16; i++) {
        src[i] = (i & 1) ? b : a;
    }
    filter_render(src, 16, 16, dst, 32, 1, 1);
}

/*
 * The CGA composite model (reenigne's), through the NTSC mode while CGA is on:
 * two output pixels per source pixel, white left as it is, the artifact colours
 * of the published old CGA palette for one pixel white and black stripes
 * (orange with white on the even columns, blue with it on the odd ones), what
 * the game's own colours come out as (solid cyan a sea green, solid magenta a
 * lavender, the title's magenta and black fire red), and a pixel pattern that
 * decodes the same on every line, since the CGA's phase never moves.
 */
static void check_cga_composite(void) {
    const uint32_t black = 0x000000FFu;
    const uint32_t cyan = 0x55FFFFFFu;
    const uint32_t magenta = 0xFF55FFFFu;
    const uint32_t white = 0xFFFFFFFFu;
    uint32_t pattern[2][16];
    uint32_t dst[2 * 32];
    int i;

    filter_set_mode(FILTER_NTSC);
    filter_set_cga(1);
    expect_int("the composite image is twice as wide", filter_output_width(320), 640);

    render_stripes(white, white, dst);
    expect_pixel("solid white stays white", dst[16], 0xFFFFFFFFu);

    render_stripes(white, black, dst);
    expect_int("white and black stripes are the palette's orange",
        cycle_channel(dst, 24) > 192 && cycle_channel(dst, 16) > 64 && cycle_channel(dst, 16) < 128 &&
        cycle_channel(dst, 8) < 48, 1);
    render_stripes(black, white, dst);
    expect_int("the other way round, its medium blue",
        cycle_channel(dst, 24) < 48 && cycle_channel(dst, 16) > 128 && cycle_channel(dst, 8) > 192, 1);

    render_stripes(cyan, cyan, dst);
    expect_int("solid cyan is a sea green",
        cycle_channel(dst, 16) > cycle_channel(dst, 8) && cycle_channel(dst, 8) > cycle_channel(dst, 24), 1);

    render_stripes(magenta, magenta, dst);
    expect_int("solid magenta is a lavender",
        cycle_channel(dst, 8) > cycle_channel(dst, 24) && cycle_channel(dst, 24) > cycle_channel(dst, 16), 1);

    render_stripes(magenta, black, dst);
    expect_int("the title's fire, magenta on the even columns, is red",
        cycle_channel(dst, 24) > 128 && cycle_channel(dst, 16) < 96 && cycle_channel(dst, 8) < 96, 1);

    for (i = 0; i < 16; i++) {
        pattern[0][i] = pattern[1][i] = (i & 1) ? magenta : cyan;
    }
    filter_render(&pattern[0][0], 16, 16, dst, 32, 2, 2);
    expect_pixel("a pattern is the same on the next line", dst[32 + 16], dst[16]);

    filter_set_cga(0);
    expect_int("off again, NTSC is the Blargg width", filter_output_width(320), 749);
    filter_set_mode(FILTER_OFF);
}

int main(void) {
    printf("filter geometry \n");

    check_output_height();
    check_scanline_pattern();
    check_cga_composite();

    if (failures == 0) {
        printf("  all checks passed \n");
        return 0;
    }

    printf("  %d check(s) failed \n", failures);
    return 1;
}
