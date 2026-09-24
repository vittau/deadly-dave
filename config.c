/*
 * The pause menu settings, kept between runs in SDL's per-user writable
 * directory (SDL_GetPrefPath). The file is a plain key=value list, because
 * that is a config.ini on Windows, a plain text file on Linux and nothing
 * unusual on macOS either - the Mac-native alternative, NSUserDefaults, would
 * mean a second implementation behind an Objective-C file for the one build,
 * and this one file works for all three.
 *
 * The parsing and the writing are pure functions, only the load and the save
 * touch the filesystem.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "config.h"
#include "display.h"
#include "filter.h"

/*
 * Both strings become a directory name, so they follow SDL's rules (letters,
 * numbers and spaces) and match the bundle identifier the macOS package uses,
 * so the settings of the .app and of a plain binary land in the same place.
 */
#define CONFIG_ORG  "vittau"
#define CONFIG_APP  "deadly-dave"
#define CONFIG_NAME "config.ini"

#define CONFIG_HEADER \
    "# Deadly Dave settings. Delete this file to go back to the defaults.\n"

/* The header and the seven short lines fit in this with room to spare. */
#define CONFIG_BUFFER 256

/*
 * Cuts the trailing blanks off a line in place, the carriage return a file
 * edited on Windows carries included.
 */
static void config_trim_end(char *text) {
    size_t length = strlen(text);

    while (length > 0) {
        char last = text[length - 1];
        if (last != ' ' && last != '\t' && last != '\r' && last != '\n') {
            break;
        }
        text[--length] = '\0';
    }
}

void config_parse(config_t *config, const char *text) {
    char *copy;
    char *line;
    size_t length;

    if (config == NULL || text == NULL) {
        return;
    }

    /* A mutable copy: the lines are cut apart in place. */
    length = strlen(text);
    copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, text, length + 1);

    line = copy;
    /* line is NULL once a last line without a newline of its own was read. */
    while (line != NULL && *line != '\0') {
        char *value;
        char *end;
        char *next;
        long number;

        next = strchr(line, '\n');
        if (next != NULL) {
            *next = '\0';
            next++;
        }

        while (*line == ' ' || *line == '\t') {
            line++;
        }
        config_trim_end(line);

        /* Blank lines and comments, the two things a hand-edited file grows. */
        if (*line != '\0' && *line != '#' && *line != ';') {
            value = strchr(line, '=');
            if (value != NULL) {
                *value = '\0';
                value++;
                config_trim_end(line);

                number = strtol(value, &end, 10);
                if (end != value) {
                    if (strcmp(line, "vsync") == 0) {
                        config->vsync = (number != 0);
                    } else if (strcmp(line, "fullscreen") == 0) {
                        config->fullscreen = (number != 0);
                    } else if (strcmp(line, "scaling") == 0 &&
                            number >= DISPLAY_SCALE_PIXEL_PERFECT &&
                            number < DISPLAY_SCALE_COUNT) {
                        config->scaling = (int)number;
                    } else if (strcmp(line, "fps_limit") == 0 &&
                            number >= 0 && number < FPS_LIMIT_COUNT) {
                        config->fps_limit = (int)number;
                    } else if (strcmp(line, "filter") == 0 &&
                            number >= FILTER_OFF && number < FILTER_MODE_COUNT) {
                        config->filter = (int)number;
                    } else if (strcmp(line, "video_mode") == 0 &&
                            number >= 0 && number < VIDEO_MODE_COUNT) {
                        config->video_mode = (int)number;
                    } else if (strcmp(line, "scrolling") == 0 &&
                            number >= 0 && number < SCROLLING_COUNT) {
                        config->scrolling = (int)number;
                    }
                }
            }
        }

        line = next;
    }

    free(copy);
}

int config_format(char *out, int out_size, const config_t *config) {
    int length;

    if (out == NULL || out_size <= 0 || config == NULL) {
        return -1;
    }

    length = snprintf(out, (size_t)out_size,
        CONFIG_HEADER
        "vsync=%d\n"
        "fps_limit=%d\n"
        "filter=%d\n"
        "fullscreen=%d\n"
        "scaling=%d\n"
        "video_mode=%d\n"
        "scrolling=%d\n",
        config->vsync ? 1 : 0, config->fps_limit, config->filter,
        config->fullscreen ? 1 : 0, config->scaling, config->video_mode,
        config->scrolling);

    if (length < 0 || length >= out_size) {
        return -1;
    }
    return length;
}

/*
 * Absolute path of the settings file, or NULL when SDL cannot work out where
 * to put it. The caller has to SDL_free() it, unlike SDL_GetBasePath().
 */
static char *config_path(void) {
    char *directory = SDL_GetPrefPath(CONFIG_ORG, CONFIG_APP);
    char *path;
    size_t length;

    if (directory == NULL) {
        printf("Could not find a directory for the settings. Error: (%s) \n",
            SDL_GetError());
        return NULL;
    }

    /* SDL_GetPrefPath ends its path with a separator, so no joining is needed. */
    length = strlen(directory) + strlen(CONFIG_NAME) + 1;
    path = (char *)SDL_malloc(length);
    if (path != NULL) {
        snprintf(path, length, "%s%s", directory, CONFIG_NAME);
    }
    SDL_free(directory);
    return path;
}

void config_load(config_t *config) {
    char *text;
    char *path;

    if (config == NULL) {
        return;
    }

    path = config_path();
    if (path == NULL) {
        return;
    }

    text = (char *)SDL_LoadFile(path, NULL);
    if (text != NULL) {
        config_parse(config, text);
        SDL_free(text);
    }
    /* No file at all, that is the first run: the caller's defaults stand. */
    SDL_free(path);
}

void config_save(const config_t *config) {
    char text[CONFIG_BUFFER];
    char *path;
    int length;

    if (config == NULL) {
        return;
    }

    path = config_path();
    if (path == NULL) {
        return;
    }

    length = config_format(text, (int)sizeof(text), config);
    if (length < 0) {
        /* Cannot happen while the fields keep their documented ranges. */
        SDL_free(path);
        return;
    }

    if (!SDL_SaveFile(path, text, (size_t)length)) {
        printf("Could not save the settings to '%s'. Error: (%s) \n", path, SDL_GetError());
    }
    SDL_free(path);
}
