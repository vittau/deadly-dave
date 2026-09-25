/*
 * The high score table and its file. The ranking, the parsing and the writing
 * are pure functions, only the load and the save touch the filesystem, the
 * same split as config.c.
 *
 * The file is one line a row, "score,level,assisted,name", best first. The
 * name is the last field so that it can hold a space, the one character of a
 * name a plain split would lose.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "config.h"
#include "highscore.h"

#define HIGHSCORE_FILE "highscores.ini"

#define HIGHSCORE_HEADER \
    "# Deadly Dave high scores: score,level,assisted,name. Level 11 is a finished game.\n" \
    "# Delete this file to go back to the original table.\n"

/* The header and five rows fit in this with room to spare. */
#define HIGHSCORE_BUFFER 512

const char highscore_name_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .-!?'";

void highscore_defaults(highscore_table_t *table) {
    for (int i = 0; i < HIGHSCORE_COUNT; i++) {
        table->entries[i].score = 100;
        table->entries[i].level = 1;
        table->entries[i].assisted = 0;
        strcpy(table->entries[i].name, "JON");
    }
}

int highscore_name_char_ok(char c) {
    return c != '\0' && strchr(highscore_name_chars, c) != NULL;
}

int highscore_rank(const highscore_table_t *table, uint32_t score) {
    for (int i = 0; i < HIGHSCORE_COUNT; i++) {
        /* Strictly above, as the original: a tie goes below the older row. */
        if (score > table->entries[i].score) {
            return i;
        }
    }
    return -1;
}

int highscore_insert(highscore_table_t *table, uint32_t score, int level, int assisted) {
    int row = highscore_rank(table, score);

    if (row < 0) {
        return -1;
    }

    for (int i = HIGHSCORE_COUNT - 1; i > row; i--) {
        table->entries[i] = table->entries[i - 1];
    }
    table->entries[row].score = score;
    table->entries[row].level = level;
    table->entries[row].assisted = (assisted != 0);
    table->entries[row].name[0] = '\0';
    return row;
}

/*
 * Reads one "score,level,assisted,name" line into *entry. Returns 0 when the
 * line is not a valid row, which leaves *entry in an unspecified state.
 */
static int highscore_parse_line(const char *line, highscore_entry_t *entry) {
    char *end;
    unsigned long score;
    long level;
    long assisted;
    size_t length;

    score = strtoul(line, &end, 10);
    if (end == line || *end != ',' || score > 0xFFFFFFFFUL) {
        return 0;
    }
    line = end + 1;

    level = strtol(line, &end, 10);
    if (end == line || *end != ',' || level < 1 || level > HIGHSCORE_LEVEL_WON) {
        return 0;
    }
    line = end + 1;

    assisted = strtol(line, &end, 10);
    if (end == line || *end != ',' || (assisted != 0 && assisted != 1)) {
        return 0;
    }
    line = end + 1;

    length = strlen(line);
    if (length > HIGHSCORE_NAME_LENGTH) {
        return 0;
    }
    for (size_t i = 0; i < length; i++) {
        char c = (char)toupper((unsigned char)line[i]);

        if (!highscore_name_char_ok(c)) {
            return 0;
        }
        entry->name[i] = c;
    }
    entry->name[length] = '\0';

    entry->score = (uint32_t)score;
    entry->level = (int)level;
    entry->assisted = (int)assisted;
    return 1;
}

void highscore_parse(highscore_table_t *table, const char *text) {
    char line[128];
    int rows = 0;

    if (table == NULL) {
        return;
    }
    highscore_defaults(table);
    if (text == NULL) {
        return;
    }

    while (*text != '\0' && rows < HIGHSCORE_COUNT) {
        const char *next = strchr(text, '\n');
        size_t length = (next != NULL) ? (size_t)(next - text) : strlen(text);
        highscore_entry_t entry;

        /* The carriage return a file edited on Windows carries. */
        if (length > 0 && text[length - 1] == '\r') {
            length--;
        }
        /* A line too long for a row is not one; skip it whole. */
        if (length < sizeof(line)) {
            memcpy(line, text, length);
            line[length] = '\0';
            if (line[0] != '#' && line[0] != ';' && highscore_parse_line(line, &entry)) {
                table->entries[rows++] = entry;
            }
        }

        if (next == NULL) {
            break;
        }
        text = next + 1;
    }

    /* A hand-edited file may be out of order; best first, ties keep their order. */
    for (int i = 1; i < HIGHSCORE_COUNT; i++) {
        highscore_entry_t moving = table->entries[i];
        int j = i;

        while (j > 0 && table->entries[j - 1].score < moving.score) {
            table->entries[j] = table->entries[j - 1];
            j--;
        }
        table->entries[j] = moving;
    }
}

int highscore_format(char *out, int out_size, const highscore_table_t *table) {
    int length;

    if (out == NULL || out_size <= 0 || table == NULL) {
        return -1;
    }

    length = snprintf(out, (size_t)out_size, "%s", HIGHSCORE_HEADER);
    for (int i = 0; i < HIGHSCORE_COUNT && length >= 0 && length < out_size; i++) {
        const highscore_entry_t *entry = &table->entries[i];
        int written = snprintf(out + length, (size_t)(out_size - length), "%lu,%d,%d,%s\n",
            (unsigned long)entry->score, entry->level, entry->assisted ? 1 : 0, entry->name);

        if (written < 0) {
            return -1;
        }
        length += written;
    }

    if (length < 0 || length >= out_size) {
        return -1;
    }
    return length;
}

void highscore_load(highscore_table_t *table) {
    char *text;
    char *path;

    if (table == NULL) {
        return;
    }
    highscore_defaults(table);

    path = config_pref_path(HIGHSCORE_FILE);
    if (path == NULL) {
        return;
    }

    text = (char *)SDL_LoadFile(path, NULL);
    if (text != NULL) {
        highscore_parse(table, text);
        SDL_free(text);
    }
    /* No file yet: nobody has made the table, the original's stands. */
    SDL_free(path);
}

void highscore_save(const highscore_table_t *table) {
    char text[HIGHSCORE_BUFFER];
    char *path;
    int length;

    if (table == NULL) {
        return;
    }

    path = config_pref_path(HIGHSCORE_FILE);
    if (path == NULL) {
        return;
    }

    length = highscore_format(text, (int)sizeof(text), table);
    if (length < 0) {
        /* Cannot happen while the rows keep their documented ranges. */
        SDL_free(path);
        return;
    }

    if (!SDL_SaveFile(path, text, (size_t)length)) {
        printf("Could not save the high scores to '%s'. Error: (%s) \n", path, SDL_GetError());
    }
    SDL_free(path);
}
