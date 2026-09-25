#include <stdio.h>
#include <string.h>

#include "highscore.h"

static int failures = 0;

static void expect_int(const char *what, long got, long want) {
    if (got != want) {
        printf("  FAIL: %s: expected %ld, got %ld \n", what, want, got);
        failures++;
    }
}

static void expect_str(const char *what, const char *got, const char *want) {
    if (strcmp(got, want) != 0) {
        printf("  FAIL: %s: expected '%s', got '%s' \n", what, want, got);
        failures++;
    }
}

/* The original's table, five JON rows of 100 points on level 1. */
static void check_defaults(void) {
    highscore_table_t table;

    highscore_defaults(&table);
    for (int i = 0; i < HIGHSCORE_COUNT; i++) {
        expect_int("default score", table.entries[i].score, 100);
        expect_int("default level", table.entries[i].level, 1);
        expect_int("default assisted", table.entries[i].assisted, 0);
        expect_str("default name", table.entries[i].name, "JON");
    }
}

/*
 * A score takes the first row it beats outright, pushing the rest down and
 * the last one out; a tie goes below the older row, and a score that beats no
 * row changes nothing.
 */
static void check_insert(void) {
    highscore_table_t table;

    highscore_defaults(&table);
    expect_int("a score that beats nothing", highscore_insert(&table, 100, 3, 0), -1);
    expect_int("nothing moved", table.entries[0].level, 1);

    expect_int("first high score", highscore_insert(&table, 500, 4, 0), 0);
    expect_int("its level", table.entries[0].level, 4);
    expect_str("its name starts empty", table.entries[0].name, "");
    strcpy(table.entries[0].name, "AAA");

    expect_int("a better one", highscore_insert(&table, 900, HIGHSCORE_LEVEL_WON, 1), 0);
    expect_int("assisted", table.entries[0].assisted, 1);
    expect_int("the old one moved down", table.entries[1].score, 500);
    expect_str("with its name", table.entries[1].name, "AAA");

    expect_int("a tie goes below", highscore_insert(&table, 500, 2, 0), 2);
    expect_int("the older tie stays above", table.entries[1].level, 4);

    highscore_insert(&table, 300, 2, 0);
    highscore_insert(&table, 200, 2, 0);
    expect_int("the last default fell out", table.entries[4].score, 200);
    expect_int("rank of a score equal to the last row", highscore_rank(&table, 200), -1);
    expect_int("rank of one just above", highscore_rank(&table, 201), 4);
}

static void check_file(void) {
    highscore_table_t table;

    highscore_parse(&table,
        "# Deadly Dave high scores: score,level,assisted,name. Level 11 is a finished game.\n"
        "55100,11,0,I2 \r\n"
        "\n"
        "50550,10,1,q\n"
        "not a row\n"
        "42950,12,0,BAD\n"
        "42900,9,0,TOOLONG\n"
        "42800,9,0,A,B\n"
        "41900,9,0,DWT");

    expect_int("first score", table.entries[0].score, 55100);
    expect_int("won", table.entries[0].level, HIGHSCORE_LEVEL_WON);
    /* Only the CR of a CRLF line is cut: a blank is a character of the name. */
    expect_str("a name with a space in it", table.entries[0].name, "I2 ");
    expect_int("second score", table.entries[1].score, 50550);
    expect_int("assisted row", table.entries[1].assisted, 1);
    expect_str("lower case is read upper", table.entries[1].name, "Q");
    /* The level out of range, the long name and the comma are skipped. */
    expect_int("third score", table.entries[2].score, 41900);
    expect_str("last line with no newline", table.entries[2].name, "DWT");
    expect_int("the rest stay the defaults", table.entries[3].score, 100);
    expect_str("default name", table.entries[4].name, "JON");

    /* A hand-edited file out of order is sorted, best first. */
    highscore_parse(&table, "100,1,0,LOW\n900,2,0,TOP\n");
    expect_str("sorted", table.entries[0].name, "TOP");
    expect_str("sorted second", table.entries[1].name, "LOW");

    /* No file at all is the original table. */
    highscore_parse(&table, "");
    expect_str("empty file", table.entries[0].name, "JON");
}

static void check_round_trip(void) {
    highscore_table_t table;
    highscore_table_t reread;
    char text[512];

    highscore_defaults(&table);
    highscore_insert(&table, 12345, 7, 1);
    strcpy(table.entries[0].name, "A B");
    highscore_insert(&table, 99999, HIGHSCORE_LEVEL_WON, 0);
    table.entries[0].name[0] = '\0';

    expect_int("format fits", highscore_format(text, (int)sizeof(text), &table) > 0, 1);
    highscore_parse(&reread, text);
    for (int i = 0; i < HIGHSCORE_COUNT; i++) {
        expect_int("round trip score", reread.entries[i].score, table.entries[i].score);
        expect_int("round trip level", reread.entries[i].level, table.entries[i].level);
        expect_int("round trip assisted", reread.entries[i].assisted, table.entries[i].assisted);
        expect_str("round trip name", reread.entries[i].name, table.entries[i].name);
    }

    expect_int("too small a buffer", highscore_format(text, 16, &table), -1);
}

int main(void) {
    printf("high scores \n");

    check_defaults();
    check_insert();
    check_file();
    check_round_trip();

    if (failures == 0) {
        printf("  all checks passed \n");
        return 0;
    }

    printf("  %d check(s) failed \n", failures);
    return 1;
}
