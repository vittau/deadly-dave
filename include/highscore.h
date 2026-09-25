#ifndef _HIGHSCORE_H_
#define _HIGHSCORE_H_

#include <stdint.h>

/*
 * The high score table, as the original keeps it in DSCORES.DAV: five rows of
 * score, name and level, the best first, and a new score only takes a row
 * from one it beats outright (a tie leaves the older row above it). The
 * original's default table, in the executable at 0x25f53, is five "JON" rows
 * of 100 points on level 1.
 *
 * The file is highscores.ini next to config.ini (see config_pref_path()), a
 * plain text file rather than the original's binary one, for the same
 * reasons as the settings.
 */
#define HIGHSCORE_COUNT       5
#define HIGHSCORE_NAME_LENGTH 3
/* The level of a finished game, which the table shows as WON. */
#define HIGHSCORE_LEVEL_WON   11

/*
 * What a name may hold: a subset of the font the table is drawn with, in the
 * order the controller's letter picker steps through it. The comma is left
 * out because it separates the fields of the file.
 */
extern const char highscore_name_chars[];

typedef struct highscore_entry_struct {
    uint32_t score;
    int level;                                /* 1..10, or HIGHSCORE_LEVEL_WON   */
    int assisted;                             /* earned with an ASSISTS mode on  */
    char name[HIGHSCORE_NAME_LENGTH + 1];
} highscore_entry_t;

typedef struct highscore_table_struct {
    highscore_entry_t entries[HIGHSCORE_COUNT];
} highscore_table_t;

/* The original's table: five JON rows of 100 points on level 1. */
void highscore_defaults(highscore_table_t *table);

/* 1 when c may be part of a name. */
int highscore_name_char_ok(char c);

/* The row a score would take, or -1 when it beats none of them. */
int highscore_rank(const highscore_table_t *table, uint32_t score);

/*
 * Puts a score in the row it earned, moving the rows below it down and the
 * last one out, with an empty name for the player to fill in. Returns that
 * row, or -1 (and changes nothing) when the score is not a high score.
 */
int highscore_insert(highscore_table_t *table, uint32_t score, int level, int assisted);

/*
 * Pure: reads the text of a highscores.ini over *table. Starts from the
 * defaults, fills the rows in the order the valid lines come in and sorts
 * them, best first; a malformed line is skipped on its own, as in
 * config_parse(). Exposed so it can be tested without a filesystem.
 */
void highscore_parse(highscore_table_t *table, const char *text);

/*
 * Pure: writes *table in the format highscore_parse() reads. Returns the
 * number of characters written, or -1 when it does not fit in out_size.
 */
int highscore_format(char *out, int out_size, const highscore_table_t *table);

/* Reads the file over *table; missing or unreadable, the table is the defaults. */
void highscore_load(highscore_table_t *table);
/* Writes *table to the file. */
void highscore_save(const highscore_table_t *table);

#endif
