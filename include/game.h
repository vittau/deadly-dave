#ifndef _GAME_H_
#define _GAME_H_

#include <SDL3/SDL.h>

#include "tile.h"
#include "dave.h"
#include "bullet.h"
#include "monster.h"
#include "plasma.h"

#define MAX_MONSTERS 10


#define G_STATE_NONE              0
#define G_STATE_LEVEL_START       2
#define G_STATE_LEVEL_BLINKING    3
#define G_STATE_LEVEL             4
#define G_STATE_LEVEL_POPUP       5
#define G_STATE_WARP_START        6
#define G_STATE_WARP              7
#define G_STATE_WARP_POPUP        8
#define G_STATE_GAMEOVER          9
#define G_STATE_QUIT_NOW          10
#define G_STATE_CONGRATS          11
/* The end of a run: the GAME OVER banner, the name of a new high score, the table. */
#define G_STATE_GAMEOVER_BANNER   12
#define G_STATE_HIGHSCORE_NAME    13
#define G_STATE_HIGHSCORE_TABLE   14

#define SECRET_LEVEL_NOT_VISITED 0
#define SECRET_LEVEL_ENTER 1

#define WARP_NONE  0
#define WARP_RIGHT 1
#define WARP_DOWN  2

typedef struct keys_state_struct {
    /*
     * Development shortcut: F10 raises it to jump to the ending screen from
     * wherever the game is, so the last screen does not have to be played to.
     * Not F11: macOS takes that one for "Show Desktop".
     */
    int32_t congrats;
    int32_t jump;
    /*
     * Up without jumping: only climbs and flies, used by the controller's up
     * and, on the keyboard, by Up/W - also the pause menu's cursor-up.
     */
    int32_t climb_up;
    int32_t left;
    int32_t right;
    int32_t down;
    int32_t fire;
    int32_t jetpack;
    int32_t quit;
    int32_t escape;
    int32_t enter;
    int32_t space;
    int32_t key_y;
    int32_t key_n;
    /*
     * One shot, for the screens at the end of a run. pressed is any key or pad
     * button going down; typed is the character a key types into a high score
     * name (0 for none); erase is Backspace, Left or the pad's B or D-pad left;
     * pick_up/pick_down are Up/Down or the D-pad, which step the controller's
     * letter picker, and pick_accept is Right or the pad's A or D-pad right,
     * which takes the letter it shows.
     */
    int32_t pressed;
    int32_t typed;
    int32_t erase;
    int32_t pick_up;
    int32_t pick_down;
    int32_t pick_accept;
} keys_state_t;

typedef struct game_context_struct {
    uint8_t blinking_timer;

    dave_t *dave;
    bullet_t *bullet;
    monster_t *monsters[MAX_MONSTERS];
    tile_t flashing_cursor;
    tile_t bottom_separator;
    tile_t top_separator;
    tile_t grail_banner;
    tile_t gun_banner;

    int64_t scroll_offset;
    int64_t scroll_remaining;
    /*
     * SCROLLING SMOOTH: left edge of the view in level pixels, which follows
     * Dave every step instead of scroll_offset's whole tiles.
     */
    int64_t view_px;

    uint64_t level;
    /* Number of tile columns the loaded level actually has. */
    uint64_t level_columns;
    /*
     * Columns the view is laid out against. It is level_columns for a level, but
     * the warp corridor is a fixed 320 pixel wide picture, so it stays at
     * DISPLAY_BASE_WIDTH / TILE_SIZE however wide the window is.
     */
    uint64_t view_columns;

    uint64_t level_secret_state;
    uint64_t lives;
    uint64_t score;

    int32_t in_warp;

    /* Escape is read as one edge per physical press everywhere it's checked
     * (see consume_escape_edge in game.c), so opening and closing the pause
     * menu never fight over the same held key. */
    int32_t prev_escape;

    /* Pause menu: which row the cursor sits on, and last frame's raw input,
     * so a button held across the frame that opened the menu isn't read
     * again as a fresh press by the menu itself. */
    int32_t pause_selected;
    int32_t pause_prev_up;
    int32_t pause_prev_down;
    int32_t pause_prev_confirm;
    /* Set by the WARP row: the level was changed, so closing the menu must reload it. */
    int32_t pause_level_changed;

    /*
     * The end of a run. run_won picks the scene behind the table (the ending
     * or the level Dave died on) and end_timer counts the steps a screen has
     * been up. highscore_row is the row being named, highscore_name_length
     * how much of the name is in, and highscore_pick the character the
     * controller's picker shows, highscore_picking once it has been used.
     */
    int32_t run_won;
    int32_t end_timer;
    int32_t highscore_row;
    int32_t highscore_name_length;
    int32_t highscore_pick;
    int32_t highscore_picking;
} game_context_t;


typedef struct game_assets {
    SDL_Surface *imgdata[1000];
} assets_t;

int game_main(int is_windowed, int starting_level);
#endif
