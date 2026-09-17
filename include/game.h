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

#define SECRET_LEVEL_NOT_VISITED 0
#define SECRET_LEVEL_ENTER 1
#define SECRET_LEVEL_VISITED 2

#define WARP_NONE  0
#define WARP_RIGHT 1
#define WARP_DOWN  2

typedef struct keys_state_struct {
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
} keys_state_t;

typedef struct game_context_struct {
    uint8_t tick;
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
} game_context_t;


typedef struct game_assets {
    SDL_Surface *imgdata[1000];
} assets_t;

int game_main(int is_windowed, int starting_level);
#endif
