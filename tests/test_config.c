#include <stdio.h>
#include <string.h>

#include "config.h"
#include "display.h"
#include "filter.h"

static int failures = 0;

static void expect_int(const char *what, int got, int want) {
    if (got != want) {
        printf("  FAIL: %s: expected %d, got %d \n", what, want, got);
        failures++;
    }
}

/*
 * config_parse() only ever writes over the values it is given, so a test starts
 * from the same defaults game.c does and then checks which of them a file
 * managed to move.
 */
static config_t defaults(void) {
    config_t config;

    config.vsync = 1;
    config.fps_limit = FPS_LIMIT_REFRESH_INDEX;
    config.filter = FILTER_OFF;
    config.fullscreen = 1;
    config.scaling = DISPLAY_SCALE_PIXEL_PERFECT;
    return config;
}

static void check_file(void) {
    config_t config = defaults();

    config_parse(&config,
        "# Deadly Dave settings. Delete this file to go back to the defaults.\n"
        "vsync=0\n"
        "fps_limit=1\n"
        "filter=3\n"
        "fullscreen=0\n"
        "scaling=1\n");

    expect_int("vsync", config.vsync, 0);
    expect_int("fps_limit", config.fps_limit, 1);
    expect_int("filter", config.filter, FILTER_BOTH);
    expect_int("fullscreen", config.fullscreen, 0);
    expect_int("scaling", config.scaling, DISPLAY_SCALE_FIT);
}

/*
 * A file that was hand-edited, or written by a version with a row this one does
 * not have, has to load what it can and leave the rest alone rather than fail
 * whole. That is also what makes a missing file mean "the defaults".
 */
static void check_tolerated_lines(void) {
    config_t config = defaults();

    config_parse(&config,
        "\n"
        "   \n"
        "; a semicolon comment too\n"
        "  vsync = 0  \n"
        "unknown_row=7\n"
        "a line without an equals sign\n"
        "fps_limit=lots\n"
        "filter=99\n"
        "filter=2\r\n"
        "fullscreen=2\n");

    expect_int("indented key and value", config.vsync, 0);
    expect_int("unknown key keeps the default", config.fps_limit, FPS_LIMIT_REFRESH_INDEX);
    expect_int("a later valid line is read, CRLF or not", config.filter, FILTER_NTSC);
    expect_int("any non zero is on", config.fullscreen, 1);
}

/*
 * The FPS limit is an index into a table of labels, the filter mode and the
 * scaling mode index arrays of names, so any of them from outside its range
 * would read past the end of one. None is allowed through, and the row keeps
 * what it had.
 */
static void check_range_guard(void) {
    config_t config = defaults();

    config_parse(&config, "fps_limit=-1\nfps_limit=5\n");
    expect_int("fps_limit outside the table keeps the default",
        config.fps_limit, FPS_LIMIT_REFRESH_INDEX);

    config.filter = FILTER_BOTH;
    config_parse(&config, "filter=99\nfilter=-1\n");
    expect_int("filter outside the enum keeps what it had", config.filter, FILTER_BOTH);

    config.scaling = DISPLAY_SCALE_FIT;
    config_parse(&config, "scaling=7\nscaling=5\nscaling=-1\n");
    expect_int("scaling outside its five modes keeps what it had",
        config.scaling, DISPLAY_SCALE_FIT);
}

static void check_round_trip(void) {
    config_t config = defaults();
    config_t reread = defaults();
    char text[256];
    int length;

    config.vsync = 0;
    config.fps_limit = FPS_LIMIT_UNLIMITED_INDEX;
    config.filter = FILTER_NTSC;
    config.fullscreen = 0;
    config.scaling = DISPLAY_SCALE_FIT;

    length = config_format(text, (int)sizeof(text), &config);
    if (length <= 0) {
        printf("  FAIL: config_format() wrote nothing \n");
        failures++;
        return;
    }
    expect_int("the length is the text, without the terminator", length, (int)strlen(text));

    config_parse(&reread, text);
    expect_int("round trip vsync", reread.vsync, 0);
    expect_int("round trip fps_limit", reread.fps_limit, FPS_LIMIT_UNLIMITED_INDEX);
    expect_int("round trip filter", reread.filter, FILTER_NTSC);
    expect_int("round trip fullscreen", reread.fullscreen, 0);
    expect_int("round trip scaling", reread.scaling, DISPLAY_SCALE_FIT);

    /* Last, since it truncates the text: a buffer too small is refused whole. */
    expect_int("a buffer that cannot hold it reports -1",
        config_format(text, 4, &config), -1);

    /* A file saved without a newline on its last line still reads. */
    reread = defaults();
    config_parse(&reread, "vsync=0");
    expect_int("a last line without a newline", reread.vsync, 0);
}

int main(void) {
    printf("config parsing \n");

    check_file();
    check_tolerated_lines();
    check_range_guard();
    check_round_trip();

    if (failures == 0) {
        printf("  all checks passed \n");
        return 0;
    }

    printf("  %d check(s) failed \n", failures);
    return 1;
}
