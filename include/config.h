#ifndef _CONFIG_H_
#define _CONFIG_H_

/*
 * The settings the pause menu offers are kept between runs, in a small
 * key=value file inside SDL's per-user writable directory, so both builds can
 * find it the same way: on macOS ~/Library/Application Support/<org>/<app>/,
 * on Windows %APPDATA%\<org>\<app>\ and on Linux ~/.local/share/<org>/<app>/.
 *
 * Nothing is ever written next to the executable. On macOS that is inside the
 * .app bundle, which is read-only on an installed copy and whose code
 * signature writing to it would break.
 *
 * Only the settings that make sense to carry over are here; the level, the
 * score and the warp cursor are per run and stay that way.
 *
 * The FPS limit is the one row whose choices are not in the module that owns
 * it (the labels are in game.c, the pacing is in the pacer), so its range is
 * declared here, next to the field it bounds.
 */
#define FPS_LIMIT_REFRESH_INDEX   3
#define FPS_LIMIT_UNLIMITED_INDEX 4
#define FPS_LIMIT_COUNT           5

typedef struct config_struct {
    int vsync;          /* 1 on, 0 off, as the pause menu's V-SYNC row           */
    int fps_limit;      /* 0..FPS_LIMIT_COUNT-1, the pause menu's FPS LIMIT row  */
    int filter;         /* FILTER_OFF .. FILTER_BOTH (see filter.h)              */
    int fullscreen;     /* 1 full screen, 0 windowed, the pause menu's MODE      */
    int scaling;        /* DISPLAY_SCALE_PIXEL_PERFECT or _FIT (see display.h)   */
} config_t;

/*
 * Pure: reads a "key=number" text blob into *config. Anything it does not
 * understand - an unknown key, a malformed line, a number out of range - is
 * left alone on purpose, so a field keeps whatever the caller started it with
 * (the defaults) and a hand-edited file still loads what it can. Exposed so it
 * can be unit tested without a filesystem or a window.
 */
void config_parse(config_t *config, const char *text);

/*
 * Pure: writes *config in the format config_parse() reads back. Returns the
 * number of characters written (the NUL not counted), or -1 when it does not
 * fit in out_size. Exposed for the same reason as config_parse().
 */
int config_format(char *out, int out_size, const config_t *config);

/* Reads the settings file over *config. Missing or unreadable: it is left as is. */
void config_load(config_t *config);
/* Writes *config to the settings file, creating the directory if needed. */
void config_save(const config_t *config);

#endif
