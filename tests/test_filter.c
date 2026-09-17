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
}

int main(void) {
    printf("filter geometry \n");

    check_output_height();
    check_scanline_pattern();

    if (failures == 0) {
        printf("  all checks passed \n");
        return 0;
    }

    printf("  %d check(s) failed \n", failures);
    return 1;
}
