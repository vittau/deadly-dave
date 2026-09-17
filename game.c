#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* access() and chdir() are POSIX; MSVC has them in io.h/direct.h, underscored. */
#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#define dd_access _access
#define dd_chdir _chdir
#else
#include <unistd.h>
#define dd_access access
#define dd_chdir chdir
#endif

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
/* SDL3 no longer pulls this in through SDL.h; it is where SDL_SetMainReady lives. */
#include <SDL3/SDL_main.h>

#include "game.h"
#include "display.h"
#include "filter.h"
#include "soundfx.h"


SDL_Window *g_window;
SDL_Renderer *g_renderer;
uint32_t *g_pixels;
int g_pixels_pitch = DISPLAY_BASE_WIDTH;
assets_t *g_assets;
soundfx_t *g_soundfx;
SDL_Gamepad *g_gamepad;

/*
 * Pause menu settings. Not persisted: every run starts back on the same
 * defaults the game always ran with - vsync on and the frame paced to the
 * display's own refresh rate.
 */
static int g_vsync_enabled = 1;
#define FPS_LIMIT_REFRESH_INDEX   3
#define FPS_LIMIT_UNLIMITED_INDEX 4
#define FPS_LIMIT_COUNT           5
static const char *g_fps_limit_labels[FPS_LIMIT_COUNT] = {
    "30", "60", "120", "REFRESH", "UNLIMITED"
};
static int g_fps_limit_index = FPS_LIMIT_REFRESH_INDEX;

/* Pause menu FILTERS row: matches filter.h's FILTER_OFF..FILTER_BOTH order. */
static const char *g_filter_labels[FILTER_MODE_COUNT] = {
    "OFF", "SCANLINES", "NTSC", "BOTH"
};

/* Defined further down, alongside the other keyboard shortcut handling. */
static void toggle_fullscreen(void);
/* Defined further down, alongside the rest of the level HUD drawing. */
static void draw_level_frame(game_context_t *game);
/* Defined further down, alongside the level file loading it wraps. Returns 0 or a load error. */
static int game_load_current_level(game_context_t *game, tile_t *map);
/* Defined further down, alongside the rest of the secret level handling. */
static int game_level_has_secret(int level);

/* Sprites that are XORed over what is behind them instead of painted on top. */
static const int blended_sprites[] = {
    SPRITE_IDX_BULLET_RIGHT, SPRITE_IDX_BULLET_LEFT,
    SPRITE_IDX_MONSTER_SUN1, SPRITE_IDX_MONSTER_SUN2,
    SPRITE_IDX_MONSTER_SUN3, SPRITE_IDX_MONSTER_SUN4,
    SPRITE_IDX_MONSTER_SPIDER1, SPRITE_IDX_MONSTER_SPIDER2,
    SPRITE_IDX_MONSTER_SPIDER3, SPRITE_IDX_MONSTER_SPIDER4,
    SPRITE_IDX_MONSTER_SWIRL1, SPRITE_IDX_MONSTER_SWIRL2,
    SPRITE_IDX_MONSTER_SWIRL3, SPRITE_IDX_MONSTER_SWIRL4,
    SPRITE_IDX_MONSTER_BONES1, SPRITE_IDX_MONSTER_BONES2,
    SPRITE_IDX_MONSTER_BONES3, SPRITE_IDX_MONSTER_BONES4,
    SPRITE_IDX_PLASMA_RIGHT1, SPRITE_IDX_PLASMA_RIGHT2, SPRITE_IDX_PLASMA_RIGHT3,
    SPRITE_IDX_PLASMA_LEFT1, SPRITE_IDX_PLASMA_LEFT2, SPRITE_IDX_PLASMA_LEFT3,
    SPRITE_IDX_MONSTER_UFO1, SPRITE_IDX_MONSTER_UFO2,
    SPRITE_IDX_MONSTER_UFO3, SPRITE_IDX_MONSTER_UFO4,
    SPRITE_IDX_MONSTER_GREEN_DISK1, SPRITE_IDX_MONSTER_GREEN_DISK2,
    SPRITE_IDX_MONSTER_GREEN_DISK3, SPRITE_IDX_MONSTER_GREEN_DISK4,
    SPRITE_IDX_MONSTER_SILVER_DISK1, SPRITE_IDX_MONSTER_SILVER_DISK2,
    SPRITE_IDX_MONSTER_SILVER_DISK3, SPRITE_IDX_MONSTER_SILVER_DISK4
};
static uint8_t g_blended[1000];

/*
 * Paints the parts of the scene outside the centered 320 pixel wide picture
 * black. The warp corridor is such a picture, and its level data continues
 * past the right edge of it, so the rest has to be hidden. Only the scene
 * band is touched, between the two HUD bars: those are drawn the full width
 * of the window by draw_level_frame() and must stay that way, not be cut down
 * to the 320 pixel picture along with the corridor.
 */
static void clear_screen_sides(void) {
    int screen_width = display_width();
    int left = display_center_offset();

    if (left < 1) {
        return;
    }

    for (int line_idx = DISPLAY_SCENE_TOP; line_idx < DISPLAY_SCENE_BOTTOM; line_idx++) {
        uint32_t *row = g_pixels + line_idx * g_pixels_pitch;

        SDL_memset4(row, 0x000000FF, (size_t)left);
        SDL_memset4(row + left + DISPLAY_BASE_WIDTH, 0x000000FF,
            (size_t)(screen_width - left - DISPLAY_BASE_WIDTH));
    }
}

static void render_tile_idx(int tile_idx, int x, int y) {
    SDL_Surface *surface = g_assets->imgdata[tile_idx];

    if (surface == NULL) {
        return;
    }

    int blend = g_blended[tile_idx];
    int screen_width = display_width();
    int col_begin = 0;
    int col_end = surface->w;

    /*
     * Clip the horizontal span once: every row writes the same columns, so the
     * per-pixel range test the inner loop used to run is not needed any more.
     */
    if (x < 0) {
        col_begin = -x;
    }
    if (x + surface->w > screen_width) {
        col_end = screen_width - x;
    }
    if (col_begin >= col_end) {
        return;
    }

    for (int line_idx = 0; line_idx < surface->h; line_idx++) {
        int dst_y = y + line_idx;

        if (dst_y < 0 || dst_y >= DISPLAY_HEIGHT) {
            continue;
        }

        const uint32_t *src = (const uint32_t *)((const uint8_t *)surface->pixels +
            (size_t)line_idx * (size_t)surface->pitch);
        uint32_t *dst = g_pixels + (size_t)dst_y * (size_t)g_pixels_pitch;

        for (int column_idx = col_begin; column_idx < col_end; column_idx++) {
            int dst_x = x + column_idx;
            uint32_t pixel = src[column_idx];

            if ((pixel & 0x000000FF) == 0) { // a fully transparent pixel is not drawn
                continue;
            }

            if (blend) {
                uint32_t oldpixel = dst[dst_x];

                if (oldpixel != 0x000000FF) {
                    /*
                     * XOR the colours but keep the sprite's alpha. The
                     * alpha byte marks transparency, so mixing it with
                     * the background would punch holes in the level
                     * wherever a blended sprite touches it.
                     */
                    pixel = (pixel & 0x000000FF) |
                            ((pixel ^ oldpixel) & 0xFFFFFF00);
                }
            }
            dst[dst_x] = pixel;
        }
    }
}

static void clear_screen(void) {
    /* The framebuffer is one contiguous block whose stride is the screen width. */
    SDL_memset4(g_pixels, 0x000000FF, (size_t)display_width() * DISPLAY_HEIGHT);
}

/*
 * Paints a horizontal band of the screen black. Used for the strip the score,
 * the level number and the lives sit on: those sprites only add up to 320
 * pixels, so on a wider screen the scene would show through between them.
 */
static void clear_screen_band(int y, int height) {
    int screen_width = display_width();

    if (y < 0) {
        height = height + y;
        y = 0;
    }
    if ((y + height) > DISPLAY_HEIGHT) {
        height = DISPLAY_HEIGHT - y;
    }
    if (height <= 0) {
        return;
    }

    /* Rows are contiguous, so the band is a single run. */
    SDL_memset4(g_pixels + y * g_pixels_pitch, 0x000000FF, (size_t)screen_width * height);
}

/*
 * Repeats a sprite from the left to the right edge of the screen. The top and
 * bottom bars of the HUD are 320 pixels wide but their pattern repeats every 32
 * pixels, so they tile seamlessly over any framebuffer width.
 */
static void render_tile_idx_row(int tile_idx, int y) {
    SDL_Surface *surface = g_assets->imgdata[tile_idx];
    int screen_width = display_width();

    if (surface == NULL || surface->w < 1) {
        return;
    }

    for (int x = 0; x < screen_width; x += surface->w) {
        render_tile_idx(tile_idx, x, y);
    }
}

/*
 * Horizontal position of the visible window in level pixels. A level narrower
 * than the viewport is centered, leaving black on both sides, rather than being
 * pinned to the left edge; a level wider than the viewport scrolls as usual.
 */
static int game_view_x(game_context_t *game) {
    int screen_width = display_width();
    int level_width = (int)game->view_columns * TILE_SIZE;

    if (level_width < screen_width) {
        return -((screen_width - level_width) / 2);
    }
    return game->scroll_offset * TILE_SIZE;
}

static void draw_tile_offset(tile_t *tile, int view_x) {
    render_tile_idx(tile->get_sprite(tile), tile->x - view_x, tile->y);
}

static void draw_tile(tile_t *tile) {
    draw_tile_offset(tile, 0);
}

/*
 * Draws a tile whose position was authored for a 320 pixel wide screen, keeping
 * it centered when the framebuffer is wider than that.
 */
static void draw_tile_centered(tile_t *tile) {
    render_tile_idx(tile->get_sprite(tile), tile->x + display_center_offset(), tile->y);
}

/* The font tiles follow this order, 100 indices apart for the black set. */
static const char font_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ,.()!?-'";

static void draw_char(char c, int x, int y, int is_black) {
    const char *letter = memchr(font_chars, c, sizeof(font_chars) - 1);

    if (letter != NULL) {
        render_tile_idx(((is_black) ? 600 : 500) + (int)(letter - font_chars), x, y);
    }
}

static void draw_text_line(const char *line, int x, int y) {
    size_t length = strlen(line);

    for (size_t i = 0; i < length; i++) {
        draw_char(line[i], x + (i * 8), y, 0);
    }
}

static void draw_text_line_black(const char *line, int x, int y) {
    size_t length = strlen(line);

    for (size_t i = 0; i < length; i++) {
        draw_char(line[i], x + (i * 8), y, 1);
    }
}

/*
 * Draws a line centred on the framebuffer. The font tiles are 8 pixels wide, so
 * the width of the line is known without measuring anything.
 */
static void draw_text_line_centered(const char *line, int y) {
    int width = (int)strlen(line) * 8;

    draw_text_line(line, (display_width() - width) / 2, y);
}

static void draw_popup_box(int x, int y, int rows, int columns) {
    // Four corners
    render_tile_idx(SPRITE_IDX_POPUP_BOX_T1, x, y);
    render_tile_idx(SPRITE_IDX_POPUP_BOX_T3, x + ((columns-1) * 8), y);
    render_tile_idx(SPRITE_IDX_POPUP_BOX_B1, x, y + ((rows-1) * 8));
    render_tile_idx(SPRITE_IDX_POPUP_BOX_B3, x + ((columns-1) * 8), y + ((rows-1) * 8));

    // Left & Right panels
    for (int i = 1; i < rows - 1; i++) {
        render_tile_idx(SPRITE_IDX_POPUP_BOX_M1, x, y + (i * 8));
        render_tile_idx(SPRITE_IDX_POPUP_BOX_M3, x + ((columns-1) * 8), y + (i * 8));
    }

    // Top & Bottom panels
    for (int i = 1; i < columns - 1; i++) {
        render_tile_idx(SPRITE_IDX_POPUP_BOX_T2, x + (i * 8), y);
        render_tile_idx(SPRITE_IDX_POPUP_BOX_B2, x + (i * 8), y + ((rows-1) * 8));
    }

    // Center
    for (int i = 1; i < rows - 1; i++) {
        for (int j = 1; j < columns - 1; j++) {
            render_tile_idx(SPRITE_IDX_POPUP_BOX_M2, x + (j*8), y + (i*8));
        }
    }
}

static void draw_map(game_context_t *game, tile_t *map) {
    int view_x = game_view_x(game);
    int first_col = view_x / TILE_SIZE;
    int first;
    int count;

    if (first_col < 0) {
        first_col = 0;
    }
    first = first_col * TILEMAP_HEIGHT;
    /* One extra column on each side keeps tiles from popping at the edges. */
    count = (display_columns() + 2) * TILEMAP_HEIGHT;
    if ((first + count) > (TILEMAP_WIDTH * TILEMAP_HEIGHT)) {
        count = (TILEMAP_WIDTH * TILEMAP_HEIGHT) - first;
    }
    if (count < 0) {
        count = 0;
    }

    for (int i = 0; i < count; i++) {
        if (map[first + i].sprites[0] != 0) {
            draw_tile_offset(&map[first + i], view_x);
        }
    }
}


static void draw_bullet_offset(bullet_t *bullet, int view_x) {
    if (bullet == NULL) {
        return;
    }
    draw_tile_offset(bullet->tile, view_x);
}

static void draw_dave_offset(dave_t *dave, int view_x) {
    int sprite = dave->tile->get_sprite(dave->tile);

    if (sprite != 0) {
        render_tile_idx(sprite, dave->tile->x - view_x, dave->tile->y);
    }
}

static void draw_monsters_offset(monster_t *monsters[MAX_MONSTERS], int view_x) {
    for (int i = 0; i < MAX_MONSTERS; i++) {
        tile_t *tile;
        int sprite;

        if (monsters[i] == NULL) {
            continue;
        }

        tile = monsters[i]->tile;
        sprite = tile->get_sprite(tile);
        if (sprite != 0) {
            render_tile_idx(sprite, tile->x - view_x, tile->y);
        }

        if (monsters[i]->plasma != NULL) {
            tile = monsters[i]->plasma->tile;
            sprite = monsters[i]->plasma->get_sprite(monsters[i]->plasma);
            if (sprite != 0) {
                render_tile_idx(sprite, tile->x - view_x, tile->y);
            }
        }
    }
}

static void draw_scrollable_area(game_context_t *game, tile_t *map) {
    int view_x = game_view_x(game);

    draw_map(game, map);
    draw_dave_offset(game->dave, view_x);
    draw_monsters_offset(game->monsters, view_x);
    draw_bullet_offset(game->bullet, view_x);
}

/*
 * The intermission banner behind the warp corridor: the number of levels left,
 * except on the corridor that leads into the last one, which gets its own line.
 */
static void draw_intermission_text(int levels_to_go) {
    char text[128];

    if (levels_to_go <= 0) {
        snprintf(text, sizeof(text), "YES! YOU FINISHED THE GAME!");
    } else if (levels_to_go == 1) {
        snprintf(text, sizeof(text), "THIS IS THE LAST LEVEL!!!");
    } else {
        snprintf(text, sizeof(text), "GOOD WORK! ONLY %d MORE TO GO!", levels_to_go);
    }
    draw_text_line(text, 50 + display_center_offset(), 58);
}

static void draw_jetpack(int bars) {
    if (bars < 0) {
        bars = 0;
    } else if (bars > 900) {
        bars = 900;
    }

    render_tile_idx(SPRITE_IDX_JETPACK_LABEL, 0, 170);
    render_tile_idx(SPRITE_IDX_JETPACK_BAR_FRAME, 72, 170);
    for (int i = 0; i < (bars/15); i++) {
        render_tile_idx(SPRITE_IDX_JETPACK_BAR, 76 + i*2, 174);
    }
}

static void draw_level_number(int level) {
    int offset = display_center_offset();

    render_tile_idx(136, 104 + offset, 0);
    /* The HUD always shows two digits, "01" through "10". */
    render_tile_idx(148 + (level / 10), 176 + offset, 0);
    render_tile_idx(148 + (level % 10), 184 + offset, 0);
}

static void draw_lives(int lives) {
    int offset = display_right_offset();
    int start_x = 256 + offset;

    render_tile_idx(135, 192 + offset, 0);

    for (int idx = 0; (idx < (lives - 1)) && (idx < 4); idx++) {
        render_tile_idx(143, start_x + (16 * idx), 0);
    }
}

static void draw_score(int score) {
    render_tile_idx(137, 0, 0);
    for (int i = 0; i < 5; i++) {
        int mod = score % 10;
        score = score / 10;
        render_tile_idx(148 + mod, 96 - (8 * i), 0);
    }
}

#define PAUSE_OPTION_VSYNC   0
#define PAUSE_OPTION_FPS     1
#define PAUSE_OPTION_MODE    2
#define PAUSE_OPTION_FILTERS 3
#define PAUSE_OPTION_WARP    4
#define PAUSE_OPTION_QUIT    5
#define PAUSE_OPTION_COUNT   6

/* Levels on disk, res/levels/level1.ddt through level10.ddt; WARP cycles through them. */
#define TOTAL_LEVELS 10

static void pause_menu_option_text(game_context_t *game, int option, char *out, size_t out_size) {
    switch (option) {
    case PAUSE_OPTION_VSYNC:
        snprintf(out, out_size, "V-SYNC: %s", g_vsync_enabled ? "ON" : "OFF");
        break;
    case PAUSE_OPTION_FPS:
        snprintf(out, out_size, "FPS LIMIT: %s", g_fps_limit_labels[g_fps_limit_index]);
        break;
    case PAUSE_OPTION_MODE:
        snprintf(out, out_size, "MODE: %s",
            ((SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0) ? "FULLSCREEN" : "WINDOWED");
        break;
    case PAUSE_OPTION_FILTERS:
        snprintf(out, out_size, "FILTERS: %s", g_filter_labels[filter_mode()]);
        break;
    case PAUSE_OPTION_WARP:
        snprintf(out, out_size, "WARP: %lu%s", (unsigned long)game->level,
            (game->level_secret_state == SECRET_LEVEL_ENTER) ? "S" : "");
        break;
    default:
        snprintf(out, out_size, "QUIT");
        break;
    }
}

/*
 * Applies the currently selected row; QUIT is handled by the caller instead.
 * WARP jumps straight to a level and loads it right away, so the scene behind
 * the menu shows it immediately instead of only once the menu closes. Returns
 * 0, or a load error from the WARP row.
 */
static int pause_menu_apply_option(game_context_t *game, tile_t *map, int option) {
    switch (option) {
    case PAUSE_OPTION_VSYNC:
        g_vsync_enabled = !g_vsync_enabled;
        display_set_vsync(g_vsync_enabled);
        break;
    case PAUSE_OPTION_FPS:
        g_fps_limit_index = (g_fps_limit_index + 1) % FPS_LIMIT_COUNT;
        break;
    case PAUSE_OPTION_MODE:
        toggle_fullscreen();
        break;
    case PAUSE_OPTION_FILTERS:
        filter_set_mode((filter_mode() + 1) % FILTER_MODE_COUNT);
        break;
    case PAUSE_OPTION_WARP:
        /*
         * A level with a secret twin gets an extra step on it before moving
         * on, so the sequence goes ...4, 5, 5S, 6... instead of skipping it.
         */
        if (game_level_has_secret((int)game->level) && game->level_secret_state != SECRET_LEVEL_ENTER) {
            game->level_secret_state = SECRET_LEVEL_ENTER;
        } else {
            game->level_secret_state = SECRET_LEVEL_NOT_VISITED;
            game->level = (game->level % TOTAL_LEVELS) + 1;
        }
        game->score = 0;
        game->pause_level_changed = 1;
        return game_load_current_level(game, map);
    default:
        break;
    }
    return 0;
}

static void draw_pause_menu(game_context_t *game) {
    int offset = display_center_offset();
    const int columns = 24;
    const int rows = 11;
    int box_x = offset + ((DISPLAY_BASE_WIDTH - (columns * 8)) / 2);
    int box_y = DISPLAY_SCENE_TOP + (((DISPLAY_SCENE_BOTTOM - DISPLAY_SCENE_TOP) - (rows * 8)) / 2);
    char line[32];

    draw_popup_box(box_x, box_y, rows, columns);
    draw_text_line_black("PAUSE", box_x + (((columns * 8) - (5 * 8)) / 2), box_y + 8);

    for (int i = 0; i < PAUSE_OPTION_COUNT; i++) {
        pause_menu_option_text(game, i, line, sizeof(line));
        /* +18, not +16: the cursor's biggest frame fills its 8x8 tile, so it
         * would otherwise touch the text (the tile ends at box_x + 16). */
        draw_text_line_black(line, box_x + 18, box_y + 24 + (i * 10));
    }

    game->flashing_cursor.x = box_x + 8;
    game->flashing_cursor.y = box_y + 24 + (game->pause_selected * 10);
    draw_tile(&game->flashing_cursor);
    game->flashing_cursor.tick(&game->flashing_cursor);
}

/* Called by whoever opens the pause menu, to start it on a clean cursor and input state. */
static void pause_menu_open(game_context_t *game) {
    game->pause_selected = 0;
    game->pause_prev_up = 0;
    game->pause_prev_down = 0;
    game->pause_prev_confirm = 0;
    game->pause_level_changed = 0;
}

/*
 * One press-release-press cycle per Escape tap, no matter which state reads
 * it: game_level(), game_level_blinking(), game_warp() and the pause menu
 * itself all call this instead of checking keys->escape directly. Without it,
 * the very key-down that opens the menu is still 1 on the next logic step,
 * and the menu (if it read the raw flag) would see that as the press that
 * closes it again - open and close would fight over one physical press.
 */
static int consume_escape_edge(game_context_t *game, keys_state_t *keys) {
    int edge = keys->escape && !game->prev_escape;
    game->prev_escape = keys->escape;
    return edge;
}

/*
 * Drives the pause menu one step: navigates, applies the selected row and
 * closes it, on the same button layout wherever it is opened from. is_warp
 * says which frozen scene to redraw behind the box, since it is redrawn in
 * full every call rather than assumed to still be sitting in the framebuffer
 * from before the menu opened.
 */
static int game_pause_menu(game_context_t *game, tile_t *map, keys_state_t *keys, int stay_state,
        int resume_state, int is_warp) {
    int up_edge;
    int down_edge;
    int confirm_level;
    int confirm_edge;

    if (keys->quit) {
        return G_STATE_QUIT_NOW;
    }

    if (consume_escape_edge(game, keys) || keys->key_n) {
        /*
         * WARP already loaded the new level into map so the menu could show
         * it live; on close, drop into it the same way a normal level load
         * does rather than resuming the state (level or warp corridor) that
         * was behind the menu before it jumped.
         */
        if (game->pause_level_changed) {
            return G_STATE_LEVEL_BLINKING;
        }
        g_soundfx->resume(g_soundfx);
        return resume_state;
    }

    up_edge = keys->climb_up && !game->pause_prev_up;
    down_edge = keys->down && !game->pause_prev_down;
    game->pause_prev_up = keys->climb_up;
    game->pause_prev_down = keys->down;

    if (up_edge) {
        game->pause_selected = (game->pause_selected + PAUSE_OPTION_COUNT - 1) % PAUSE_OPTION_COUNT;
    } else if (down_edge) {
        game->pause_selected = (game->pause_selected + 1) % PAUSE_OPTION_COUNT;
    }

    confirm_level = keys->enter || keys->space;
    confirm_edge = confirm_level && !game->pause_prev_confirm;
    game->pause_prev_confirm = confirm_level;

    if (confirm_edge) {
        if (game->pause_selected == PAUSE_OPTION_QUIT) {
            return G_STATE_QUIT_NOW;
        }
        if (pause_menu_apply_option(game, map, game->pause_selected) != 0) {
            /* A WARP row whose level could not be read has nothing left to show. */
            return G_STATE_QUIT_NOW;
        }
    }

    /*
     * Redrawn in full every call, exactly like every other state: the
     * streaming texture behind g_pixels is not guaranteed to keep what was
     * last drawn into it between locks, so drawing only the box on top of an
     * assumed-frozen frame could show whatever an old, different frame left
     * in that particular buffer.
     */
    clear_screen();
    draw_scrollable_area(game, map);
    draw_level_frame(game);
    /* A WARP jump swaps the corridor for a regular level, which isn't clipped to 320 pixels. */
    if (is_warp && !game->pause_level_changed) {
        clear_screen_sides();
    }
    draw_pause_menu(game);
    return stay_state;
}

static void unload_assets(assets_t *assets) {
    for (int i = 0; i < 1000; i++) {
        if (assets->imgdata[i] != NULL) {
            SDL_DestroySurface(assets->imgdata[i]);
            assets->imgdata[i] = NULL;
        }
    }
}

/*
 * Makes the black background of a sprite transparent. The background always
 * reaches the border of the tile, while black inside the art (an outline, a
 * detail) is enclosed, so only the black reachable from the border is cleared.
 */
static void key_out_black_background(SDL_Surface *surface) {
    int w;
    int h;
    int pitch;
    uint32_t *pixels;
    int *stack;
    int top = 0;

    if (surface == NULL || surface->w < 1 || surface->h < 1) {
        return;
    }

    w = surface->w;
    h = surface->h;
    pixels = (uint32_t *)surface->pixels;
    pitch = surface->pitch / (int)sizeof(uint32_t);
    stack = malloc(sizeof(int) * (size_t)w * (size_t)h);
    if (stack == NULL) {
        return;
    }

/* Black is R=G=B=0 in RGBA8888, and the alpha byte is the low one. */
#define PUSH_BLACK(i) do {                                              \
        if ((pixels[(i)] >> 8) == 0 && (pixels[(i)] & 0xFF) != 0) {     \
            pixels[(i)] &= 0xFFFFFF00;                                  \
            stack[top++] = (i);                                         \
        }                                                               \
    } while (0)

    for (int x = 0; x < w; x++) {
        PUSH_BLACK(x);
        PUSH_BLACK((h - 1) * pitch + x);
    }
    for (int y = 0; y < h; y++) {
        PUSH_BLACK(y * pitch);
        PUSH_BLACK(y * pitch + (w - 1));
    }

    while (top > 0) {
        int i = stack[--top];
        int y = i / pitch;
        int x = i - y * pitch;

        if (x > 0) {
            PUSH_BLACK(i - 1);
        }
        if (x < w - 1) {
            PUSH_BLACK(i + 1);
        }
        if (y > 0) {
            PUSH_BLACK(i - pitch);
        }
        if (y < h - 1) {
            PUSH_BLACK(i + pitch);
        }
    }

#undef PUSH_BLACK
    free(stack);
}

static int load_assets(void) {
    char fname[64];
    int loaded = 0;
    g_assets = calloc(1, sizeof(struct game_assets));

    for (int i = 0; i < 1000; i++) {
        g_assets->imgdata[i] = NULL;
        memset(fname, '\0', sizeof(fname));
        snprintf(fname, sizeof(fname), "res/tiles/tile%u.bmp", i);
        if (dd_access(fname, 0) == 0) {
            SDL_Surface *surface = SDL_LoadBMP(fname);
            if (surface != NULL) {
                g_assets->imgdata[i] = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA8888);
                /*
                 * Monsters, plasma and the bullet ship as 24 bit BMPs with no
                 * alpha channel, so their black background would be drawn
                 * opaque on top of the level. Only those are keyed out, the
                 * level tiles and the HUD bars are meant to be opaque.
                 */
                if (!SDL_ISPIXELFORMAT_ALPHA(surface->format) &&
                        i >= SPRITE_IDX_MONSTER_SPIDER1 && i <= SPRITE_IDX_BULLET_LEFT) {
                    key_out_black_background(g_assets->imgdata[i]);
                }
                SDL_DestroySurface(surface);
            }
            loaded++;
        }
    }

    /*
     * Nothing loaded means the artwork is not next to the binary. Fail with a
     * message instead of drawing with no tiles at all.
     */
    for (size_t i = 0; i < sizeof(blended_sprites) / sizeof(blended_sprites[0]); i++) {
        g_blended[blended_sprites[i]] = 1;
    }

    if (loaded == 0) {
        printf("Could not find the game assets in 'res/tiles'. \n");
        printf("The 'res' directory has to sit next to the executable. \n");
        free(g_assets);
        g_assets = NULL;
        return -1;
    }

    return 0;
}

/*
 * Set game and monster properties to default values
 */
static void init_game(game_context_t *game) {
    game->lives = 4;
    game->score = 0;

    game->scroll_offset = 0;
    game->scroll_remaining = 0;

    game->bullet = NULL;
    game->in_warp = WARP_NONE;
    game->level = 1;
    game->level_columns = TILEMAP_WIDTH;
    game->view_columns = TILEMAP_WIDTH;
    game->level_secret_state = SECRET_LEVEL_NOT_VISITED;

    tile_create_flashing_cursor(&game->flashing_cursor, 224, 96);
    tile_create_bottom_separator(&game->bottom_separator, 0, DISPLAY_SCENE_BOTTOM);
    /* The 4 pixel separator plus a 1 pixel gap sit at the bottom of the top bar. */
    tile_create_top_separator(&game->top_separator, 0, DISPLAY_SCENE_TOP - 5);
    tile_create_grail_banner(&game->grail_banner, 70, 183);
    tile_create_gun_banner(&game->gun_banner, 240, 170);

    for (int i = 0; i < MAX_MONSTERS; i++) {
        game->monsters[i] = NULL;
    }
}

/* The desktop's usual full screen shortcut: Command on macOS, Alt elsewhere. */
#if defined(__APPLE__)
#define TOGGLE_FULLSCREEN_MOD SDL_KMOD_GUI
#else
#define TOGGLE_FULLSCREEN_MOD SDL_KMOD_ALT
#endif

/*
 * A window created full screen has no windowed geometry for SDL to go back to,
 * so one is picked here. The window stays resizable in both modes.
 */
static void toggle_fullscreen(void) {
    if ((SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0) {
        SDL_SetWindowFullscreen(g_window, false);
        SDL_SetWindowSize(g_window, DISPLAY_BASE_WIDTH * 3, DISPLAY_HEIGHT * 3);
        SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    } else {
        SDL_SetWindowFullscreen(g_window, true);
    }
    SDL_SyncWindow(g_window);
}

/*
 * Controllers go through SDL's gamepad API, so an Xbox pad on Windows (XInput),
 * a DualSense on Linux or a Switch Pro controller all report the same buttons:
 * A is SOUTH, B is EAST, X is WEST and Start is START.
 *
 * The left stick and the D-pad move Dave, A jumps, B toggles the jetpack, X
 * shoots and Start is the Escape key. The stick axes rest at zero and the
 * D-pad reports one button at a time, so a dead zone is only needed for the
 * stick.
 *
 * The up direction is special. On the keyboard up jumps, which is faithful to
 * the original and works, but on a pad jumping whenever the stick or the D-pad
 * went up would be unplayable, so up only climbs and flies there and A is the
 * only jump. A, like the keyboard jump key, also climbs up a vine.
 */
#define GAMEPAD_DEAD_ZONE 8000

static void gamepad_open(void) {
    SDL_JoystickID *ids;
    int count = 0;

    if (g_gamepad != NULL) {
        return;
    }

    ids = SDL_GetGamepads(&count);
    if (ids == NULL) {
        return;
    }

    if (count > 0) {
        g_gamepad = SDL_OpenGamepad(ids[0]);
    }
    SDL_free(ids);
}

static void gamepad_update(keys_state_t *state) {
    Sint16 x, y;

    if (g_gamepad == NULL) {
        return;
    }

    x = SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    y = SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_LEFTY);

    state->left     |= (x < -GAMEPAD_DEAD_ZONE) || SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    state->right    |= (x >  GAMEPAD_DEAD_ZONE) || SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    state->down     |= (y >  GAMEPAD_DEAD_ZONE) || SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    /* Up climbs and flies, only A is allowed to jump, see gamepad_update's note. */
    state->climb_up |= (y < -GAMEPAD_DEAD_ZONE) || SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
    state->jump     |= SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
    state->fire     |= SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_WEST) ||
                       SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
}

/* Handles the events that are a one shot in the game: the jetpack toggles, so
 * holding the button must not flip it every frame, and Escape opens a popup. */
static void gamepad_event(SDL_Event *event, keys_state_t *state) {
    if (event->type == SDL_EVENT_GAMEPAD_ADDED) {
        gamepad_open();

    } else if (event->type == SDL_EVENT_GAMEPAD_REMOVED) {
        if (g_gamepad != NULL && SDL_GetGamepadID(g_gamepad) == event->gdevice.which) {
            SDL_CloseGamepad(g_gamepad);
            g_gamepad = NULL;
        }
        gamepad_open();

    } else if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        if (event->gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
            /* The pause menu's A/confirm. */
            state->key_y = 1;
            state->enter = 1;
        } else if (event->gbutton.button == SDL_GAMEPAD_BUTTON_EAST) {
            state->jetpack = 1;
            /* The pause menu's B/close. */
            state->key_n = 1;
        } else if (event->gbutton.button == SDL_GAMEPAD_BUTTON_START) {
            /*
             * Start is Escape once the game is running, but it is also the
             * button everyone presses to leave the title screen, so raise both
             * and let the intro pick; see start_intro().
             */
            state->escape = 1;
            state->enter = 1;
        } else if (event->gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) {
            /* Nothing else uses the fourth face button; it starts the game too. */
            state->enter = 1;
        }
    }
}

static void get_keys(keys_state_t* state) {
    SDL_Event event;

    /*
     * WASD, with the arrow keys kept as an alternative. W is jump, S climbs
     * down and the space bar shoots, Ctrl still does too.
     */
    const bool *keystate = SDL_GetKeyboardState(NULL);
    state->right      = (keystate[SDL_SCANCODE_RIGHT] != 0 || keystate[SDL_SCANCODE_D] != 0) ? 1 : 0;
    state->left       = (keystate[SDL_SCANCODE_LEFT]  != 0 || keystate[SDL_SCANCODE_A] != 0) ? 1 : 0;
    state->jump       = (keystate[SDL_SCANCODE_UP]    != 0 || keystate[SDL_SCANCODE_W] != 0) ? 1 : 0;
    state->down       = (keystate[SDL_SCANCODE_DOWN]  != 0 || keystate[SDL_SCANCODE_S] != 0) ? 1 : 0;
    state->escape     = (keystate[SDL_SCANCODE_ESCAPE] != 0) ? 1 : 0;
    state->fire       = (keystate[SDL_SCANCODE_SPACE] != 0 || keystate[SDL_SCANCODE_LCTRL] != 0) ? 1 : 0;
    state->space      = (keystate[SDL_SCANCODE_SPACE] != 0) ? 1 : 0;
    state->key_y      = (keystate[SDL_SCANCODE_Y] != 0) ? 1 : 0;
    state->key_n      = (keystate[SDL_SCANCODE_N] != 0) ? 1 : 0;
    /*
     * Up without jumping, see keys_state_t: the pause menu points its cursor
     * with it too, so Up/W move it there without also being read as jump.
     */
    state->climb_up   = (keystate[SDL_SCANCODE_UP] != 0 || keystate[SDL_SCANCODE_W] != 0) ? 1 : 0;

    state->jetpack = 0;
    /* Enter is otherwise only ever raised by the KEY_DOWN case below or a gamepad event. */
    state->enter = 0;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_KEY_DOWN) {
            int is_repeat = event.key.repeat;
            /* Edge triggered: the jetpack is a toggle, holding the key is not meant to flip it. */
            if (event.key.scancode == SDL_SCANCODE_J && is_repeat == 0) {
                state->jetpack = 1;
            }
            if (event.key.scancode == SDL_SCANCODE_RETURN && is_repeat == 0) {
                if ((event.key.mod & TOGGLE_FULLSCREEN_MOD) != 0) {
                    toggle_fullscreen();
                } else {
                    state->enter = 1;
                }
            }
            if (event.key.scancode == SDL_SCANCODE_F5 && is_repeat == 0) {
                display_toggle_scale_mode();
            }
        } else if (event.type == SDL_EVENT_QUIT) {
            state->quit = 1;
        } else {
            gamepad_event(&event, state);
        }
    }

    gamepad_update(state);
}

/*
 * Basically this function should return 1 if any keyboard key is pressed, however for now it is only
 * counting known keys
 */
static int is_any_key_pressed(keys_state_t* key_state) {
    if (key_state->right || key_state->left || key_state->space || key_state->down || key_state->jump ||
            key_state->fire) {
        return 1;
    }
    return 0;
}

/*
 * Frame pacing.
 *
 * The game logic is a fixed 14 ms step: every movement, animation, timer and
 * monster tick advances exactly once per step, so the step length IS the game
 * speed and it must not follow the display.
 *
 * The picture runs on a clock of its own: one frame per refresh of the screen
 * the window is on, presented on the vertical blank. The rate is asked of the
 * display rather than pinned to 60, because with vsync on the two have to
 * agree. SDL_RenderPresent() returns at the blank, so a budget built from the
 * panel's own rate lines the frames up with it, while a budget that is a
 * multiple and a half of it hands out frames that are alternately one blank and
 * two blanks long: a 16.6 ms budget on a 90 Hz panel (the Steam Deck OLED)
 * still averages 60 fps, but every second frame stays up twice as long as the
 * one before it, and that unevenness is what the eye picks up in a scroll. A
 * display that will not say what it runs at gets 60 Hz out of
 * display_frame_period_ns(), and then the sleep alone is what bounds the frames.
 *
 * The two clocks are kept apart by an accumulator. Each frame adds the time it
 * really took to the logic clock and spends it in whole 14 ms steps, so a frame
 * runs however many steps have come due: a frame that came early runs none and
 * leaves the time in the accumulator, a 40 ms frame runs the 2 steps it owes
 * plus whatever was left over. The game therefore advances at the very same
 * 1000/14 steps per second it always did, whatever the frame rate is, and no
 * time is invented or lost because only whole steps are ever taken out of the
 * accumulator.
 */
#define LOGIC_STEP_NS   ((int64_t)14 * 1000000)   /* the 14 ms tick, unchanged */
#define LOGIC_MAX_STEPS 5                         /* catch-up bound, 70 ms of logic */
/*
 * The budget is aimed a shade under one refresh, about 1.6%, and the direction
 * is the whole point. Our idea of the refresh period is never exactly the
 * panel's: SDL may report a rounded 60 for a 59.94 Hz mode, and the clock this
 * loop reads drifts against the one the panel scans out on. Aim a hair long and
 * the loop asks for a frame just after each blank has gone by, drifts further
 * every frame, and once the gap grows past what is left to draw in it drops a
 * frame and starts over - a hitch every few seconds. Aim a hair short and the
 * loop is always already late for its own budget, never sleeps, and vsync alone
 * decides when the frame goes out, which is what we want it to do.
 */
#define FRAME_BUDGET_MARGIN(period) ((period) / 64)

typedef struct frame_pacer_struct {
    uint64_t frame_begin;   /* when the current frame started                  */
    uint64_t next_frame;    /* earliest moment the next frame may be drawn     */
    uint64_t frame_period;  /* the frame budget: one refresh, less the margin  */
    int64_t  accumulator;   /* nanoseconds owed to the logic clock             */
} frame_pacer_t;

/*
 * The FPS LIMIT pause menu option overrides the budget with a fixed period,
 * except REFRESH (its default) and UNLIMITED, which keep pacing off the
 * display: REFRESH is one refresh of the screen the window is on, less the
 * margin above, and UNLIMITED is no budget at all - the loop never sleeps and
 * only vsync, if it's on, still paces the frames.
 */
static uint64_t pacer_budget_ns(void) {
    uint64_t period;

    switch (g_fps_limit_index) {
    case 0: return (uint64_t)SDL_NS_PER_SECOND / 30;
    case 1: return (uint64_t)SDL_NS_PER_SECOND / 60;
    case 2: return (uint64_t)SDL_NS_PER_SECOND / 120;
    case FPS_LIMIT_UNLIMITED_INDEX: return 0;
    default:
        period = display_frame_period_ns();
        return period - FRAME_BUDGET_MARGIN(period);
    }
}

static void pacer_init(frame_pacer_t *pacer) {
    pacer->frame_begin = SDL_GetTicksNS();
    pacer->next_frame = pacer->frame_begin;
    pacer->frame_period = pacer_budget_ns();
    pacer->accumulator = 0;
}

/*
 * Opens a frame: sleeps until the display is ready for another one and returns
 * how many 14 ms logic steps that frame has to run before it draws.
 *
 * The wait is an SDL_DelayNS(), never a spin, so a frame with nothing to do
 * gives the CPU back instead of burning a battery. It is also the fallback and
 * not the main event: with the budget a shade under one refresh the loop is
 * always already past it, so this sleep does nothing and vsync inside
 * SDL_RenderPresent() is what the frames line up on. It is what paces the game
 * when vsync could not be turned on. Either way the time both of them spend is
 * measured here and handed to the logic clock, so neither changes the game
 * speed.
 *
 * The step count is clamped to LOGIC_MAX_STEPS and the rest of the accumulator
 * is dropped with it: after a dragged window, a suspended machine or a
 * breakpoint the game skips the time it could not keep up with instead of
 * spiralling into a catch-up storm it would never come out of.
 */
static int pacer_begin_frame(frame_pacer_t *pacer) {
    uint64_t now = SDL_GetTicksNS();
    int steps;

    if (now < pacer->next_frame) {
        SDL_DelayNS(pacer->next_frame - now);
        now = SDL_GetTicksNS();
    }

    /* The window may have been dragged onto a screen with another refresh rate. */
    pacer->frame_period = pacer_budget_ns();

    pacer->next_frame += pacer->frame_period;
    if (pacer->next_frame < now) {
        /* The frame overran its budget; line the ceiling back up with now. */
        pacer->next_frame = now + pacer->frame_period;
    }

    pacer->accumulator += (int64_t)(now - pacer->frame_begin);
    pacer->frame_begin = now;

    steps = (int)(pacer->accumulator / LOGIC_STEP_NS);
    if (steps > LOGIC_MAX_STEPS) {
        steps = LOGIC_MAX_STEPS;
        pacer->accumulator = 0;
    } else {
        pacer->accumulator -= (int64_t)steps * LOGIC_STEP_NS;
    }

    return steps;
}

/*
 * Shows the intro until the player starts the game. Returns 0 when the player
 * quit or closed the window, so the caller can shut the game down.
 */
static int start_intro(void) {
    int32_t intro_should_finish = 0;
    frame_pacer_t pacer;

    keys_state_t key_state = {0};
    // Clear screen
    SDL_SetRenderDrawColor(g_renderer, 0x00, 0x00, 0x00, 0xFF);
    SDL_RenderClear(g_renderer);

    // This will consume all keys waiting prior such as the enter starting the game
    get_keys(&key_state);
    get_keys(&key_state);
    get_keys(&key_state);

    /* A close requested while those ran still has to end the game. */
    if (key_state.quit) {
        return 0;
    }
    memset(&key_state, 0x00, sizeof(keys_state_t));

    tile_t block[41];

    tile_create_intro_banner(&block[0], 103, 0);
    tile_create_block(&block[1], SPRITE_IDX_DIRT, 80, 64, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[2], SPRITE_IDX_DIRT, 112, 64, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[3], SPRITE_IDX_DIRT, 128, 64, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[4], SPRITE_IDX_DIRT, 144, 64, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[5], SPRITE_IDX_DIRT, 160, 64, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[6], SPRITE_IDX_DIRT, 192, 64, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[7], SPRITE_IDX_DIRT, 224, 64, TILE_SIZE, TILE_SIZE);

    tile_create_block(&block[8], SPRITE_IDX_DIRT, 80, 80, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[9], SPRITE_IDX_DIRT, 112, 80, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[10], SPRITE_IDX_CROWN, 128, 80, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[11], SPRITE_IDX_DIRT, 224, 80, TILE_SIZE, TILE_SIZE);

    tile_create_block(&block[12], SPRITE_IDX_DIRT, 80, 96, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[13], SPRITE_IDX_DIRT, 112, 96, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[14], SPRITE_IDX_DIRT, 160, 96, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[15], SPRITE_IDX_DIRT, 176, 96, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[16], SPRITE_IDX_DIRT, 192, 96, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[17], SPRITE_IDX_DIRT, 224, 96, TILE_SIZE, TILE_SIZE);

    tile_create_block(&block[18], SPRITE_IDX_DIRT, 80, 112, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[19], SPRITE_IDX_DIRT, 112, 112, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[20], SPRITE_IDX_CROWN, 208, 112, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[21], SPRITE_IDX_DIRT, 224, 112, TILE_SIZE, TILE_SIZE);

    tile_create_block(&block[22], SPRITE_IDX_DIRT, 80, 128, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[23], SPRITE_IDX_DIRT, 112, 128, TILE_SIZE, TILE_SIZE);
    tile_create_intro_fire(&block[24], 128, 128);
    tile_create_block(&block[25], SPRITE_IDX_DIRT, 144, 128, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[26], SPRITE_IDX_DIRT, 160, 128, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[27], SPRITE_IDX_DIRT, 176, 128, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[28], SPRITE_IDX_DIRT, 224, 128, TILE_SIZE, TILE_SIZE);

    tile_create_block(&block[29], SPRITE_IDX_DIRT, 80, 144, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[30], SPRITE_IDX_DIRT, 224, 144, TILE_SIZE, TILE_SIZE);

    tile_create_block(&block[31], SPRITE_IDX_DIRT, 80, 160, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[32], SPRITE_IDX_DIRT, 96, 160, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[33], SPRITE_IDX_DIRT, 112, 160, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[34], SPRITE_IDX_DIRT, 128, 160, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[35], SPRITE_IDX_DIRT, 144, 160, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[36], SPRITE_IDX_DIRT, 160, 160, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[37], SPRITE_IDX_DIRT, 176, 160, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[38], SPRITE_IDX_DIRT, 192, 160, TILE_SIZE, TILE_SIZE);
    tile_create_intro_fire(&block[39], 208, 160);
    tile_create_block(&block[40], SPRITE_IDX_DIRT, 224, 160, TILE_SIZE, TILE_SIZE);

    pacer_init(&pacer);

    while (!intro_should_finish) {
        int steps = pacer_begin_frame(&pacer);

        if (steps == 0) {
            /*
             * The screen refreshes faster than the 14 ms step and none came
             * due, so nothing has moved and there is no new picture to put up.
             * Sending the same one again would buy a blit and a wait for the
             * blank and change nothing on the screen, so the frame is dropped
             * and the window keeps what it already shows.
             */
            continue;
        }

        SDL_SetRenderDrawColor(g_renderer, 0x00, 0x00, 0x00, 0xFF);
        SDL_RenderClear(g_renderer);
        /* Picks up window resizes and aspect-ratio changes before anything is drawn. */
        display_sync();

        g_pixels = display_lock(&g_pixels_pitch);
        if (g_pixels == NULL) {
            return 0;
        }

        /*
         * One get_keys() per logic step, exactly as before: the one shot flags
         * (jetpack, enter) come out of the event queue, so a single J or Enter
         * is only ever seen by the one step that polled its event.
         */
        for (int step = 0; step < steps; step++) {
            get_keys(&key_state);

            /*
             * Enter, space, Escape or any of the pad's face buttons starts the
             * game - Escape and Start (which raises escape too) are not "quit"
             * here, only closing the window is.
             */
            if (key_state.enter || key_state.space || key_state.jump ||
                    key_state.fire || key_state.jetpack || key_state.escape) {
                intro_should_finish = 1;

            /* The window was closed: leave so the game can shut down. */
            } else if (key_state.quit) {
                display_unlock();
                return 0;
            }

            /*
             * The picture is the state before the last tick, which is the order
             * the tiles were drawn in when the loop drew once per step; the
             * steps before it only advance the animation.
             */
            if (step == (steps - 1)) {
                clear_screen();

                // Draw all tiles
                for (int idx = 0; idx < 41; idx++) {
                    draw_tile_centered(&block[idx]);
                }

                draw_text_line_centered("BY JOHN ROMERO", 50);
                draw_text_line_centered("(C) 1990 SOFTDISK, INC.", 57);
                draw_text_line_centered("MODERNIZED BY VITOR MACHADO", 184);
            }

            for (int idx = 0; idx < 41; idx++) {
                block[idx].tick(&block[idx]);
            }
        }

        display_unlock();
        display_present();
    }

    return 1;
}

static void clear_monsters(game_context_t *game) {
    for (int i = 0; i < MAX_MONSTERS; i++) {
        monster_destroy(game->monsters[i]);
        game->monsters[i] = NULL;
    }
}

static void clear_map(tile_t *map) {
    for (int i = 0; i < TILEMAP_WIDTH * TILEMAP_HEIGHT; i++) {
        map[i].sprites[0] = 0;
        map[i].sprites[1] = 0;
        map[i].mod = 0;
        map[i].x = 0;
        map[i].y = 0;
    }
}

static int game_warp_popup(game_context_t *game, tile_t *map, keys_state_t *keys) {
    return game_pause_menu(game, map, keys, G_STATE_WARP_POPUP, G_STATE_WARP, 1);
}

static int game_popup_routine(game_context_t *game, tile_t *map, keys_state_t *keys) {
    return game_pause_menu(game, map, keys, G_STATE_LEVEL_POPUP, G_STATE_LEVEL, 0);
}

/*
 * When dave nears screen edge and there is more tiles in that edge, we will scroll the screen
 * to make new screen visible under the assumption usually the player moving to one direction.
 *
 *  offset = 0                             offset = 0 + delta
 *  +-------------------+                 +--------------------+
 *  |        1          .                 .           2        |
 *  |                   .                 .                    |
 *  |              DAVE .      ---->      . DAVE               |
 *  |                   .                 .                    |
 *  +-------------------+                 +--------------------+
 */
static int game_adjust_scroll_to_dave(game_context_t *game) {
    int screen_width = display_width();
    /*
     * Last column the viewport may start at, so that it never scrolls past the
     * end of the level. The wider the screen the more columns are on it, and
     * the window can be resized while playing, so it is recomputed every frame.
     */
    int max_offset = (int)game->level_columns - display_columns();

    if (max_offset < 0) {
        max_offset = 0;
    }
    if (game->scroll_offset > max_offset) {
        game->scroll_offset = max_offset;
    }

    /* Here we still have scrolling to do so we just scroll the screen a bit */
    if (game->scroll_remaining != 0) {
        if (game->scroll_remaining > 0) {
            game->scroll_remaining--;
            game->scroll_offset++;
        } else if (game->scroll_remaining < 0) {
            game->scroll_remaining++;
            game->scroll_offset--;
        }
        return 1;
    }
    /*
     * If we got here we have no previous scrolling in progress and we need to caluclate 
     * and decide if any scrolling is needed. 
     */
    else {
        int delta = (game->dave->tile->x - (game->scroll_offset * 16));
        if (delta > screen_width - (16 + 16 + 8) && game->scroll_offset < max_offset) {
            if ((max_offset -  game->scroll_offset) < 15) {
                game->scroll_remaining = (max_offset - game->scroll_offset);
            } else {
                game->scroll_remaining = 15;
            }
            return 1;

        } else if (delta < (16 + 14) && game->scroll_offset > 0) {
            if (game->scroll_offset < 15) {
                game->scroll_remaining = (0 - game->scroll_offset);
            } else {
                game->scroll_remaining = -15;
            }
            return 1;
        } else {
            return 0;
        }
    }
}

static void game_set_scroll_to_dave(game_context_t *game) {
    while (game_adjust_scroll_to_dave(game) != 0) {};
}

static void game_do_map(tile_t *map) {
    for (int i = 0; i < TILEMAP_WIDTH * TILEMAP_HEIGHT; i++) {
        if (map[i].sprites[0] != 0) {
            map[i].tick(&map[i]);
        }
    }
}

static void game_do_plasmas(game_context_t *game, tile_t *map) {
    for (int i = 0; i < MAX_MONSTERS; i++) {
        if (game->monsters[i] != NULL) {
            if (game->monsters[i]->plasma != NULL) {
                if (game->monsters[i]->plasma->is_dead(game->monsters[i]->plasma)) {
                    plasma_destroy(game->monsters[i]->plasma);
                    game->monsters[i]->plasma = NULL;
                } else {
                    /*
                     * The range is measured from where it was fired, like the
                     * original 320 pixel view did, so a wider viewport does not
                     * make a plasma travel further.
                     */
                    int reach = DISPLAY_BASE_WIDTH + 80;
                    plasma_t *plasma = game->monsters[i]->plasma;
                    plasma->tick(plasma, map, plasma->spawn_x - reach, plasma->spawn_x + reach);
                }
            }
        }
    }
}

static void game_do_bullets(game_context_t *game, tile_t *map, keys_state_t *keys) {
    if (game->bullet != NULL) {
        /* Same as the plasma: the bullet's range is the original one. */
        int reach = DISPLAY_BASE_WIDTH;
        game->bullet->tick(game->bullet, map,
            game->bullet->spawn_x - reach, game->bullet->spawn_x + reach);

        if (game->bullet->is_dead(game->bullet)) {
            bullet_destroy(game->bullet);
            game->bullet = NULL;
        }
    } else {
        if (keys->fire && game->dave->has_gun) {
            if (game->dave->face_direction == DAVE_DIRECTION_LEFT ||
                game->dave->face_direction == DAVE_DIRECTION_FRONTL) {
                game->bullet = bullet_create_left(game->dave->tile->x - 8, game->dave->tile->y + 8);
            } else if (game->dave->face_direction == DAVE_DIRECTION_RIGHT ||
                game->dave->face_direction == DAVE_DIRECTION_FRONTR) {
                game->bullet = bullet_create_right(game->dave->tile->x + 8, game->dave->tile->y + 8);
            }
        }
    }
}

static void draw_level_frame(game_context_t *game) {
    clear_screen_band(0, game->top_separator.y);
    render_tile_idx_row(game->bottom_separator.sprites[0], game->bottom_separator.y);
    render_tile_idx_row(game->top_separator.sprites[0], game->top_separator.y);
    if (game->dave->has_trophy) {
        game->grail_banner.x = 70 + display_center_offset();
        draw_tile(&game->grail_banner);
    }
    if (game->dave->has_gun) {
        game->gun_banner.x = 240 + display_right_offset();
        draw_tile(&game->gun_banner);
    }
    if (game->dave->jetpack_bars > 0) {
        draw_jetpack(game->dave->jetpack_bars);
    }
    draw_score(game->score);
    draw_level_number(game->level);
    draw_lives(game->lives);
}

/*
 * tile collision box calculation:
 *                 ex. collision box inside sprite                 ex. collision box bigger than sprite
 * y              +---------------------------+          y        +------------------------+
 *                |                           |                   |                        |
 * y+dy           |   +------------+          |          y+dy     |  +---------------------------+
 *                |   |            |          |                   |  |                     |     |
 *                |   |            |          |    or             |  |                     |     |
 *                |   |            |          |                   |  |                     |     |
 * (y+dy)+(h+dh)  |   +------------+          |                   |  |                     |     |
 *                |                           |                   |  |                     |     |
 * y+h            +---------------------------+          y+h      +--|---------------------+     |
 *                                                                   |                           |
 *                                                 (y+dy)+(h+dh)     +---------------------------+
 *
 *                x  x+dx    (x+dx)+(w+dw)     x+w                x  x+dx               x+w  (x+dx)+(w+dw)
 *
 */
static int collision_detect(tile_t *tile1, tile_t *tile2) {
    int box1_x = tile1->x + tile1->collision_dx;
    int box1_y = tile1->y + tile1->collision_dy;
    int box1_w = tile1->width + tile1->collision_dw;
    int box1_h = tile1->height + tile1->collision_dh;

    int box2_x = tile2->x + tile2->collision_dx;
    int box2_y = tile2->y + tile2->collision_dy;
    int box2_w = tile2->width + tile2->collision_dw;
    int box2_h = tile2->height + tile2->collision_dh;

    if (box1_x < box2_x + box2_w &&
        box1_x + box1_w > box2_x &&
        box1_y < box2_y + box2_h &&
        box1_y + box1_h > box2_y) {
        return 1;
    }

    return 0;
}

/*
 * Game loop routing while user didn't press any key after level started,
 * screen will be same as game_level, however dave will blink and monsters freeze.
 */
static int game_level_blinking(game_context_t *game, tile_t *map, keys_state_t *keys) {
    dave_t *dave = game->dave;

    // blinking_timer makes sure:
    // [1, 10] - blinking (cannot interrupt)
    // [11,20] - visible
    // [21,31] - blinking
    // [32 --> 11] -> ringed to 11
    game->blinking_timer++;
    if (game->blinking_timer >= 32) {
        game->blinking_timer = 11;
    }

    if (keys->quit) {
        return G_STATE_QUIT_NOW;
    }

    if (consume_escape_edge(game, keys)) {
        g_soundfx->stop(g_soundfx);
        pause_menu_open(game);
        return G_STATE_LEVEL_POPUP;
    }

    // Minimal time for blinking, to avoid releasing dave due to any keys still
    // pressed from intro menu or previous round
    if (game->blinking_timer >= 10) {
        if (is_any_key_pressed(keys)) {
            return G_STATE_LEVEL;
        }
    }

    game_do_map(map);

    clear_screen();
    draw_map(game, map);
    draw_monsters_offset(game->monsters, game_view_x(game));
    if (game->blinking_timer >= 11 && game->blinking_timer <= 20) {
        draw_dave_offset(dave, game_view_x(game));
    }
    draw_level_frame(game);

    return G_STATE_LEVEL_BLINKING;
}


/*
 * Levels whose chunk also stores a warp zone (a bonus area reached by going
 * off the edge of the level): level5_secret.ddt and friends.
 */
static int game_level_has_secret(int level) {
    if (level == 5 || level == 8 || level == 9 || level == 10) {
        return 1;
    }
    return 0;
}

/*
 * Where a warp zone hands Dave back when its door is reached. Taken from the
 * original: a warp drops him into the next level that has a warp zone, and
 * level 10's wraps around to level 3 (5->8, 8->9, 9->10, 10->3).
 */
static int game_warp_exit_level(int level) {
    switch (level) {
    case 5:
        return 8;
    case 8:
        return 9;
    case 9:
        return 10;
    case 10:
        return 3;
    default:
        return level + 1;
    }
}

static int game_level(game_context_t *game, tile_t *map, keys_state_t *keys) {
    dave_t *dave = game->dave;
    if (keys->quit) {
        return G_STATE_QUIT_NOW;
    }

    if (consume_escape_edge(game, keys)) {
        g_soundfx->stop(g_soundfx);
        pause_menu_open(game);
        return G_STATE_LEVEL_POPUP;
    }


    // If we need to adjust screen by scrolling, just draw scene without progressing any game objects.
    if (game_adjust_scroll_to_dave(game)) {
        clear_screen();
        draw_scrollable_area(game, map);
        draw_level_frame(game);

        return G_STATE_LEVEL;
    }

    // Tick dave, monsters, and all block tiles in map
    game->dave->tick(game->dave, map, keys->left, keys->right, keys->jump, keys->climb_up, keys->down, keys->jetpack);

    for (int i = 0; i < MAX_MONSTERS; i++) {
        if (game->monsters[i] != NULL) {
            game->monsters[i]->tick(game->monsters[i], game->dave->tile->x);
        }
    }

    game_do_map(map);
    game_do_plasmas(game, map);
    game_do_bullets(game, map, keys);

    if (dave->is_dead(game->dave)) {
        game->lives--;
        game->dave->state = DAVE_STATE_STANDING;
        game->dave->jump_state = 0;
        game->dave->step_count = 0;
        game->dave->on_fire = 0;

        if (game->lives == 0) {
            return G_STATE_GAMEOVER;
        }

        return G_STATE_LEVEL_START;
    }

    for (int idx = 0; idx < TILEMAP_WIDTH * TILEMAP_HEIGHT ; idx++) {
        if ((map[idx].sprites[0] != 0) &&
                collision_detect(game->dave->tile, &map[idx])) {

            if (map[idx].mod == LOOT) {
                game->score = game->score + map[idx].score_value;
                map[idx].sprites[0] = 0;
                map[idx].mod = 0;
                g_soundfx->play(g_soundfx, TUNE_TREASURE);

            } else if (map[idx].mod == TROPHY) {
                game->dave->has_trophy = 1;
                map[idx].mod = 0;
                map[idx].sprites[0] = 0;
                game->score = game->score + map[idx].score_value;
                g_soundfx->play(g_soundfx, TUNE_GOT_TROPHY);

            } else if (map[idx].mod == GUN) {
                game->dave->has_gun = 1;
                map[idx].mod = 0;
                map[idx].sprites[0] = 0;
                game->score = game->score + map[idx].score_value;
                g_soundfx->play(g_soundfx, TUNE_GOT_SOMETHING);

            } else if (map[idx].mod == JETPACK) {
                game->dave->jetpack_bars = 900;
                map[idx].mod = 0;
                map[idx].sprites[0] = 0;
                g_soundfx->play(g_soundfx, TUNE_GOT_SOMETHING);

            } else if (map[idx].mod == DOOR) {
                if (game->dave->has_trophy) {
                    g_soundfx->play(g_soundfx, TUNE_NEXTLEVEL);
                    game->in_warp = WARP_RIGHT;
                    return G_STATE_WARP_START;
                }

            } else if (map[idx].mod == FIRE) {
                if (game->dave->on_fire != 1) {
                    game->dave->on_fire = 1;
                    g_soundfx->stop(g_soundfx);
                    g_soundfx->play(g_soundfx, TUNE_OUCH);
                }
            } else if (map[idx].mod == CLIMB) {
                game->dave->on_tree = 1;
            }
        }
    }

    // Checking secret level (warp down) condition (left side)
    if (game->dave->tile->x < 0) {
        if (game->level_secret_state == SECRET_LEVEL_NOT_VISITED &&
            game_level_has_secret(game->level)) {
            g_soundfx->stop(g_soundfx);
            game->in_warp = WARP_DOWN;
            return G_STATE_WARP_START;
        } else {
            game->dave->tile->x = 0;
        }
    }

    // Checking secret level (warp down) condition (right side)
    if (game->dave->tile->x > (16 * 99)) {
        if (game->level_secret_state == SECRET_LEVEL_NOT_VISITED &&
            game_level_has_secret(game->level)) {
            g_soundfx->stop(g_soundfx);
            game->in_warp = WARP_DOWN;
            return G_STATE_WARP_START;
        } else {
            game->dave->tile->x = (16 * 99);
        }
    }

    // In game mechanics when dave falls under the screen he will appear on top like a loop
    if (game->dave->tile->y > 200) {
        game->dave->tile->y = -20;
    }

    for (int idx = 0; idx < 5; idx++) {
        if (game->monsters[idx] != NULL) {
            if (game->bullet != NULL) {
                if (collision_detect(game->bullet->tile, game->monsters[idx]->tile)) {
                    if (game->monsters[idx]->on_fire != 1) {
                        bullet_destroy(game->bullet);
                        game->bullet = NULL;
                        game->monsters[idx]->on_fire = 1;
                        g_soundfx->stop(g_soundfx);
                        g_soundfx->play(g_soundfx, TUNE_EXPLOSION);
                    }
                }
            }

            if (collision_detect(game->dave->tile, game->monsters[idx]->tile)) {
                if (game->monsters[idx]->is_alive(game->monsters[idx])) {
                    game->dave->on_fire = 1;
                    game->monsters[idx]->on_fire = 1;
                    g_soundfx->stop(g_soundfx);
                    g_soundfx->play(g_soundfx, TUNE_EXPLOSION);
                }
            }
        }
    }

    for (int idx = 0; idx < MAX_MONSTERS; idx++) {
        if (game->monsters[idx] != NULL) {
            if (game->monsters[idx]->plasma != NULL) {
                if (collision_detect(game->dave->tile, game->monsters[idx]->plasma->tile)) {
                    plasma_destroy(game->monsters[idx]->plasma);
                    game->monsters[idx]->plasma = NULL;
                    game->dave->on_fire = 1;
                    g_soundfx->stop(g_soundfx);
                    g_soundfx->play(g_soundfx, TUNE_EXPLOSION);
                }
            }
        }
    }

    clear_screen();
    draw_scrollable_area(game, map);
    draw_level_frame(game);

    return G_STATE_LEVEL;
}


static int game_warp(game_context_t *game, tile_t *map, keys_state_t *keys) {
    if (keys->quit) {
        return G_STATE_QUIT_NOW;
    }

    if (consume_escape_edge(game, keys)) {
        pause_menu_open(game);
        return G_STATE_WARP_POPUP;
    }

    game->dave->mute = 1;
    if (game->dave->tile->y > 200) {
        game->level_secret_state = SECRET_LEVEL_ENTER;
        game->dave->mute = 0;
        return G_STATE_NONE;
    }

    /*
     * The warp corridor is authored for the original 320 pixel wide screen and
     * its level data is wider than that, so the walk is measured against the
     * original width and not against the viewport: on a wide window Dave used to
     * walk the whole level data instead of the length of the original screen,
     * which is a long wait once the corridor fits on screen in full. The
     * corridor's own right edge still ends the walk when that comes first.
     */
    if (game->dave->tile->x > (DISPLAY_BASE_WIDTH - 20) ||
            game->dave->tile->x > ((int)game->level_columns * TILE_SIZE) - 20) {
        if (game->level_secret_state == SECRET_LEVEL_ENTER) {
            /* A warp zone's door hands Dave back to a fixed level. */
            game->level = game_warp_exit_level((int)game->level);
        } else {
            game->level++;
        }
        game->level_secret_state = SECRET_LEVEL_NOT_VISITED;
        /* The last level's door rolls the ending instead of loading level 11. */
        if (game->level > TOTAL_LEVELS) {
            game->dave->mute = 0;
            return G_STATE_CONGRATS;
        }
        game->dave->mute = 0;
        return G_STATE_NONE;
    }

    if (game->in_warp == WARP_RIGHT) {
        game->dave->tick(game->dave, map, 0, 1, 0, 0, 0, 0);
    } else {
        game->dave->tick(game->dave, map, 0, 0, 0, 0, 0, 0);
    }
    game_do_map(map);

    clear_screen();
    draw_scrollable_area(game, map);
    draw_level_frame(game);
    /* Only the original 320 pixels of corridor are the picture. */
    clear_screen_sides();

    if (game->in_warp == WARP_DOWN) {
        tile_t warp_label;
        tile_t zone_label;

        tile_create_label_warp(&warp_label, 32 + display_center_offset(), 92);
        tile_create_label_zone(&zone_label, 200 + display_center_offset(), 92);
        draw_tile(&warp_label);
        draw_tile(&zone_label);
    } else if (game->level_secret_state != SECRET_LEVEL_ENTER) {
        /*
         * Only the corridor that follows a level's door carries the countdown.
         * The one that closes a warp zone has no banner: the warp is a detour,
         * not a level of its own, and the count would be wrong anyway.
         */
        draw_intermission_text(TOTAL_LEVELS - game->level);
    }

    return G_STATE_WARP;
}

/*
 * Reads a .ddt into map, spawning Dave and the monsters it names. Returns 0 on
 * success and a negative code on failure (missing file, short read, malformed
 * tag), so the caller can shut down instead of the process exiting from here.
 */
static int game_level_load(game_context_t *game, tile_t *map, char *file) {
    int i = 0;
    long fsize;
    char *buf;
    int collected_count = 0;
    int pos = 0, cur_col = 0;
    int in_comment = 0;
    char tag[4] = {0, 0, 0, 0};

    int monsters_count = 0;
    FILE* f = fopen(file, "rb");
    if (f == NULL) {
        printf("Error loading level file: %s \n", file);
        return -4;
    }
    fseek(f, 0, SEEK_END);
    fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize < 0) {
        printf("Could not read level file: %s \n", file);
        fclose(f);
        return -5;
    }

    buf = malloc((size_t)fsize + 1);
    if (buf == NULL) {
        printf("Could not allocate the level file: %s \n", file);
        fclose(f);
        return -6;
    }
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        printf("Could not read level file: %s \n", file);
        free(buf);
        fclose(f);
        return -7;
    }
    fclose(f);

    buf[fsize] = 0;

    while (buf[i] != 0) {
        if (in_comment) {
            if (buf[i] == '\n' || buf[i] == '\r') {
                in_comment = 0;
            }
        } else {
            if (buf[i] == '#') {
                in_comment = 1;
            } else if (buf[i] == ',') {
                if (collected_count == 3) {
                    collected_count = 0;
                    if ((strcmp(tag, " D ") == 0) || (strcmp(tag, "D+M") == 0)) {
                        game->dave = dave_create(g_soundfx, cur_col * 16, pos * 16);
                    } else if (strcmp(tag, "SU1") == 0) {
                        game->monsters[monsters_count] = monster_create_sun(cur_col * 16, pos * 20);
                        monsters_count++;
                    } else if (strcmp(tag, "SU2") == 0) {
                        game->monsters[monsters_count] = monster_create_sun((cur_col * 16) - 8, (pos * 20) - 16);
                        monsters_count++;
                    } else if (strcmp(tag, "SP1") == 0) {
                        game->monsters[monsters_count] = monster_create_spider(cur_col * 16, (pos * 20) - 12);
                        monsters_count++;
                    } else if (strcmp(tag, "SW1") == 0) {
                        // Tested vs original and its +3, -14 exactly
                        game->monsters[monsters_count] = monster_create_swirl((cur_col * 16) + 3, (pos * 20) - 14);
                        monsters_count++;
                    } else if (strcmp(tag, "BZ1") == 0) {
                        game->monsters[monsters_count] = monster_create_bones(cur_col * 16, (pos * 16) - 2);
                        monsters_count++;
                    } else if (strcmp(tag, "UF1") == 0) {
                        game->monsters[monsters_count] = monster_create_ufo(cur_col * 16, pos * 16);
                        monsters_count++;
                    } else if (strcmp(tag, "GD1") == 0) {
                        game->monsters[monsters_count] = monster_create_guard(cur_col * 16, pos * 16);
                        monsters_count++;
                    } else if (strcmp(tag, "GRD") == 0) {
                        game->monsters[monsters_count] = monster_create_green_disk(cur_col * 16, pos * 16);
                        monsters_count++;
                    } else if (strcmp(tag, "SLV") == 0) {
                        game->monsters[monsters_count] = monster_create_silver_disk(cur_col * 16, pos * 16);
                        monsters_count++;
                    }

                    tile_create(&map[cur_col * TILEMAP_HEIGHT + pos], tag, cur_col * 16, pos*16);
                    pos++;
                } else {
                    free(buf);
                    return -1;
                }
            } else if (buf[i] == ';') {
                if (collected_count == 3) {
                    collected_count = 0;
                    tile_create(&map[cur_col * TILEMAP_HEIGHT + pos], tag, cur_col*16, pos*16);
                    cur_col++;
                    pos = 0;
                } else {
                    free(buf);
                    return -2;
                }
            } else if (buf[i] == '\n' || buf[i] == '\r') {
                /*
                 * A newline closes the column just like the ';' does when a
                 * whole tag is waiting. level8.ddt shipped without the ';' at
                 * the end of one line and the parse stopped right there, so
                 * everything to the right of it stayed blank and the level
                 * looked cut in half; a missing ';' must not do that again.
                 */
                if (collected_count == 3) {
                    collected_count = 0;
                    tile_create(&map[cur_col * TILEMAP_HEIGHT + pos], tag, cur_col*16, pos*16);
                    cur_col++;
                    pos = 0;
                }
            } else {
                if (collected_count >= 3) {
                    free(buf);
                    return -3;
                }
                tag[collected_count] = buf[i];
                collected_count++;
            }
        }
        i++;
    }

    free(buf);

    game->level_columns = (cur_col > 0 && cur_col <= TILEMAP_WIDTH) ? (uint64_t)cur_col : TILEMAP_WIDTH;
    game->view_columns = game->level_columns;

    return 0;
}

/*
 * Loads game->level into map and resets Dave to its start, the same load
 * G_STATE_NONE and G_STATE_LEVEL_START do together over two ticks. The pause
 * menu's WARP row calls this directly so the scene behind the box shows the
 * new level right away instead of only once the menu closes. Returns 0, or the
 * load error so the caller can quit.
 */
static int game_load_current_level(game_context_t *game, tile_t *map) {
    char level_path[4096];
    int rc;

    clear_map(map);
    clear_monsters(game);

    if (game->level_secret_state == SECRET_LEVEL_ENTER) {
        snprintf(level_path, sizeof(level_path), "res/levels/level%ld_secret.ddt", (long)game->level);
    } else {
        snprintf(level_path, sizeof(level_path), "res/levels/level%ld.ddt", (long)game->level);
    }

    rc = game_level_load(game, map, level_path);
    if (rc != 0) {
        return rc;
    }

    game->dave->tile->x = game->dave->default_x;
    game->dave->tile->y = game->dave->default_y;
    /* Only the warp corridor mutes Dave; a level jumped to straight out of it must not stay muted. */
    game->dave->mute = 0;
    game->scroll_offset = 0;
    game->blinking_timer = 0;
    game_set_scroll_to_dave(game);
    return 0;
}

/*
 * The ending screen, shown once the last level is done: the text the original
 * puts in the executable, inside a frame of grails (the trophies that open a
 * level's door). Any key or pad button starts a fresh run on level 5.
 */
static int game_congrats(game_context_t *game, keys_state_t *keys) {
    static const char *lines[] = {
        "CONGRATULATIONS!",
        "",
        "YOU MADE IT THROUGH ALL THE PERIL-",
        "OUS AREAS IN CLYDE'S HIDEOUT!",
        "",
        "VERY GOOD WORK! DID YOU FIND",
        "THE 4 WARP ZONES? THEY ARE LOCATED",
        "ON LEVELS 5,8,9 AND 10. JUST JUMP",
        "OFF THE TOP OF THE SCREEN AT THE",
        "EXTREME LEFT OR RIGHT EDGE OF THE",
        "WORLD AND VOILA! YOU'RE THERE!",
        "",
        "PRESS ANY BUTTON"
    };

    if (keys->quit) {
        return G_STATE_QUIT_NOW;
    }

    clear_screen();

    /* A frame of grails around the picture, with their glow animation. */
    {
        static const int grail[] = {
            SPRITE_IDX_TROPHY0, SPRITE_IDX_TROPHY1, SPRITE_IDX_TROPHY2,
            SPRITE_IDX_TROPHY3, SPRITE_IDX_TROPHY4
        };
        static int tick = 0;
        int frame = grail[(tick++ / 10) % 5];

        for (int x = 0; x < display_width(); x += TILE_SIZE) {
            render_tile_idx(frame, x, DISPLAY_SCENE_TOP);
            render_tile_idx(frame, x, DISPLAY_SCENE_BOTTOM - TILE_SIZE);
        }
        for (int y = DISPLAY_SCENE_TOP + TILE_SIZE; y < DISPLAY_SCENE_BOTTOM - TILE_SIZE; y += TILE_SIZE) {
            render_tile_idx(frame, 0, y);
            render_tile_idx(frame, display_width() - TILE_SIZE, y);
        }
    }

    for (int i = 0; i < (int)(sizeof(lines) / sizeof(lines[0])); i++) {
        draw_text_line_centered(lines[i], 44 + (i * 10));
    }

    if (keys->enter || keys->space || keys->jump || keys->fire || keys->jetpack ||
            keys->climb_up || keys->down || keys->left || keys->right ||
            keys->key_y || keys->key_n) {
        game->level = 5;
        game->level_secret_state = SECRET_LEVEL_NOT_VISITED;
        return G_STATE_NONE;
    }
    return G_STATE_CONGRATS;
}

static void clear_gameloop(game_context_t *game) {
    clear_monsters(game);
    dave_destroy(game->dave);
    bullet_destroy(game->bullet);
}
/*
 * One 14 ms logic step of the game: it advances the state it is given and
 * draws the result into g_pixels, which is exactly what it did when the loop
 * ran one step per frame. Only the loop around it changed, so the state
 * machine still moves forward by a single tick per call. Returns the next
 * state; the two states that end the game are left to the caller, which owns
 * the framebuffer lock and the teardown.
 */
static int game_state_step(game_context_t *game, tile_t *map, keys_state_t *key_state, int state) {
    char level_path[4096];
    int next_state = state;

    if (state == G_STATE_NONE) {
        clear_map(map);
        clear_monsters(game);

        if (game->level_secret_state == SECRET_LEVEL_ENTER) {
            snprintf(level_path, 4096, "res/levels/level%ld_secret.ddt", (long)game->level);
        } else {
            snprintf(level_path, 4096, "res/levels/level%ld.ddt", (long)game->level);
        }

        if (game_level_load(game, map, level_path) != 0) {
            return G_STATE_QUIT_NOW;
        }
        next_state = G_STATE_LEVEL_START;

    } else if (state == G_STATE_LEVEL_START) {
        game->dave->tile->x = game->dave->default_x;
        game->dave->tile->y = game->dave->default_y;
        game->scroll_offset = 0;
        game->blinking_timer = 0;
        game_set_scroll_to_dave(game);
        next_state = G_STATE_LEVEL_BLINKING;

    } else if (state == G_STATE_LEVEL_BLINKING) {
        next_state = game_level_blinking(game, map, key_state);

    } else if (state == G_STATE_LEVEL) {
        next_state = game_level(game, map, key_state);

    } else if (state == G_STATE_LEVEL_POPUP) {
        next_state = game_popup_routine(game, map, key_state);

    } else if (state == G_STATE_WARP_START) {
        int rc;
        clear_map(map);
        clear_monsters(game);

        game->scroll_offset = 0;

        if (game->in_warp == WARP_RIGHT) {
            rc = game_level_load(game, map, "res/levels/warp_right.ddt");
        } else {
            rc = game_level_load(game, map, "res/levels/warp_down.ddt");
        }
        if (rc != 0) {
            return G_STATE_QUIT_NOW;
        }
        if (game->in_warp != WARP_RIGHT) {
            game->dave->face_direction = DAVE_DIRECTION_FRONT;
        }
        /* The corridor is the original 320 pixel wide screen, whatever the window. */
        game->view_columns = DISPLAY_BASE_WIDTH / TILE_SIZE;
        next_state = G_STATE_WARP;

    } else if (state == G_STATE_WARP) {
        next_state = game_warp(game, map, key_state);

    } else if (state == G_STATE_WARP_POPUP) {
        next_state = game_warp_popup(game, map, key_state);

    } else if (state == G_STATE_CONGRATS) {
        next_state = game_congrats(game, key_state);
    }

    return next_state;
}

static int gameloop(int starting_level) {
    game_context_t* game;
    tile_t map[TILEMAP_WIDTH * TILEMAP_HEIGHT];
    keys_state_t key_state = {0};

    int state = G_STATE_NONE;
    frame_pacer_t pacer;

    game = calloc(1, sizeof(game_context_t));
    init_game(game);
    game->level = starting_level;

    /*
     * Escape (or gamepad Start) is what leaves the title screen, and the same
     * press is often still held on the very first step here: without this,
     * consume_escape_edge() would see it as a brand new press and open the
     * pause menu immediately instead of a plain level start.
     */
    get_keys(&key_state);
    game->prev_escape = key_state.escape;

    pacer_init(&pacer);

    while (1) {
        /*
         * How many 14 ms steps this frame owes, after the pacer has waited out
         * the FPS LIMIT budget for it. Everything below runs once per frame,
         * the state machine runs once per step.
         */
        int steps = pacer_begin_frame(&pacer);

        if (steps == 0) {
            /*
             * The screen refreshes faster than the 14 ms step and none came
             * due: nothing in the game has moved, so the frame is dropped
             * rather than spent redrawing and presenting the same picture. The
             * display never pulls the logic forward to fill it.
             */
            continue;
        }

        SDL_SetRenderDrawColor(g_renderer, 0x00, 0x00, 0x00, 0xFF);
        SDL_RenderClear(g_renderer);

        // Picks up window resizes and aspect-ratio changes before anything is drawn
        display_sync();

        g_pixels = display_lock(&g_pixels_pitch);
        if (g_pixels == NULL) {
            clear_gameloop(game);
            free(game);
            return 1;
        }

        for (int step = 0; step < steps; step++) {
            if (state == G_STATE_GAMEOVER) {
                display_unlock();
                clear_gameloop(game);
                free(game);
                return 2;

            } else if (state == G_STATE_QUIT_NOW) {
                display_unlock();
                clear_gameloop(game);
                free(game);
                return 1;
            }

            /*
             * One get_keys() per logic step, as before. The one shot flags
             * (jetpack, enter, key_y, key_n) are rebuilt by every call - jetpack
             * and enter are zeroed, key_y/key_n reassigned from the keyboard -
             * and the presses behind them come out of the event queue, which
             * only hands each event to the single step that polled it. A J or a
             * pad B pressed once therefore toggles the jetpack once, however
             * many steps this frame runs.
             */
            get_keys(&key_state);

            state = game_state_step(game, map, &key_state, state);
        }

        // Render screen
        display_unlock();
        display_present();
    }

    return 0;
}

/*
 * Single exit point, so however the game ends the audio device, the assets and
 * SDL are released and the process can really terminate.
 */
static int game_shutdown(void) {
    printf("bye bye \n");
    if (g_soundfx != NULL) {
        soundfx_destroy(g_soundfx);
        g_soundfx = NULL;
    }
    if (g_assets != NULL) {
        unload_assets(g_assets);
        g_assets = NULL;
    }
    if (g_gamepad != NULL) {
        SDL_CloseGamepad(g_gamepad);
        g_gamepad = NULL;
    }
    display_quit();
    SDL_Quit();
    return 0;
}

int game_main(int is_windowed, int starting_level) {
    int ret = 0;
    const int windowed_scale = 3;

    SDL_SetMainReady();
    /* SDL3 returns true on success, and dropped SDL_INIT_NOPARACHUTE. */
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("Failed to initialize SDL video. Error: (%s) \n", SDL_GetError());
        return -1;
    }

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        printf("Failed to initialize SDL audio. Error: (%s) \n", SDL_GetError());
        return -2;
    }

    /* A controller is optional, the game runs fine on the keyboard alone. */
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        printf("Failed to initialize SDL gamepad. Error: (%s) \n", SDL_GetError());
    } else {
        gamepad_open();
    }

    /*
     * SDL3 takes no position here: a windowed run is centered afterwards, and a
     * plain SDL_WINDOW_FULLSCREEN with no mode set is the old fullscreen desktop.
     */
    if (is_windowed) {
        g_window = SDL_CreateWindow("",
            DISPLAY_BASE_WIDTH * windowed_scale, DISPLAY_HEIGHT * windowed_scale,
            SDL_WINDOW_RESIZABLE);
    } else {
        g_window = SDL_CreateWindow("",
            DISPLAY_BASE_WIDTH * windowed_scale, DISPLAY_HEIGHT * windowed_scale,
            SDL_WINDOW_FULLSCREEN | SDL_WINDOW_RESIZABLE);
    }

    if (g_window == NULL) {
        printf("Failed to create the window. Error: (%s) \n", SDL_GetError());
        return -4;
    }

    if (is_windowed) {
        SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }

    /*
     * Let SDL pick the backend. The accelerated one scales the framebuffer on
     * the GPU, which matters on a large window, and every platform SDL runs on
     * still has the software renderer as a fallback.
     */
    g_renderer = SDL_CreateRenderer(g_window, NULL);

    if (g_renderer == NULL) {
        printf("Failed to create the renderer. Error: (%s) \n", SDL_GetError());
        return -5;
    }

    if (display_init(g_renderer, DISPLAY_SCALE_PIXEL_PERFECT) != 0) {
        printf("Failed to initialize the display. \n");
        return -3;
    }

    // Flush any pre-pressed keys
    SDL_FlushEvent(SDL_EVENT_KEY_DOWN);
    SDL_FlushEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);
    SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION);

    /*
     * The artwork and the levels are loaded through paths relative to the
     * binary, not to the working directory, so that the game also runs when it
     * is started from somewhere else (a file manager, for instance).
     */
    const char *base_path = SDL_GetBasePath();
    if (base_path != NULL) {
        if (dd_chdir(base_path) != 0) {
            printf("Failed to switch to the game directory '%s'. \n", base_path);
        }
        /* SDL caches this string and frees it itself on SDL_Quit. */
    }

    if (load_assets() != 0) {
        SDL_Quit();
        return -6;
    }

    /* Window and taskbar icon, the same sprite the packages use as app icon. */
    if (g_assets->imgdata[SPRITE_IDX_ICON] != NULL) {
        SDL_SetWindowIcon(g_window, g_assets->imgdata[SPRITE_IDX_ICON]);
    }

    g_soundfx = soundfx_create();
    if (g_soundfx == NULL) {
        /* The game plays its tunes through Dave and the popups, so it needs them. */
        printf("Failed to create the sound effects. \n");
        return game_shutdown();
    }

    while (1) {
        if (!start_intro()) {
            return game_shutdown();
        }

        ret = gameloop(starting_level);
        printf("game-loop finished with ret-code: %d \n", ret);

        /*
         * ret 1 is the player quitting. ret 2 is game over: fall through and
         * show the intro again for a new game.
         */
        if (ret == 1) {
            return game_shutdown();
        }
    }
}

