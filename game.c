#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

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
#include "soundfx.h"


SDL_Window *g_window;
SDL_Renderer *g_renderer;
uint32_t *g_pixels;
int g_pixels_pitch = DISPLAY_BASE_WIDTH;
assets_t *g_assets;
soundfx_t *g_soundfx;
SDL_Gamepad *g_gamepad;

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
    SPRITE_IDX_MONSTER_UFO3, SPRITE_IDX_MONSTER_UFO4
};
static uint8_t g_blended[1000];

static void render_tile_idx(int tile_idx, int x, int y) {
    SDL_Surface *surface = g_assets->imgdata[tile_idx];

    if (surface == NULL) {
        return;
    }

    int blend = g_blended[tile_idx];
    int screen_width = display_width();

    for (int line_idx = 0; line_idx < surface->h; line_idx++) {
        int dst_y = y + line_idx;

        if (dst_y < 0 || dst_y >= DISPLAY_HEIGHT) {
            continue;
        }

        const uint32_t *src = (const uint32_t *)((const uint8_t *)surface->pixels +
            (size_t)line_idx * (size_t)surface->pitch);
        uint32_t *dst = g_pixels + (size_t)dst_y * (size_t)g_pixels_pitch;

        for (int column_idx = 0; column_idx < surface->w; column_idx++) {
            int dst_x = x + column_idx;
            uint32_t pixel;

            if (dst_x < 0 || dst_x >= screen_width) {
                continue;
            }
            pixel = src[column_idx];
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
    int screen_width = display_width();

    for (int line_idx = 0; line_idx < DISPLAY_HEIGHT; line_idx++) {
        SDL_memset4(g_pixels + line_idx * g_pixels_pitch, 0x000000FF, (size_t)screen_width);
    }
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

    for (int line_idx = y; line_idx < (y + height); line_idx++) {
        SDL_memset4(g_pixels + line_idx * g_pixels_pitch, 0x000000FF, (size_t)screen_width);
    }
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
    int level_width = (int)game->level_columns * TILE_SIZE;

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
static const char font_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ,.()!?";

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
    if (dave->tile->get_sprite(dave->tile) != 0) {
        draw_tile_offset(dave->tile, view_x);
    }
}

static void draw_monsters_offset(monster_t *monsters[MAX_MONSTERS], int view_x) {
    for (int i = 0; i < MAX_MONSTERS; i++) {
        if (monsters[i] == NULL) {
            continue;
        }
        if  (monsters[i]->tile->get_sprite(monsters[i]->tile) != 0) {
                render_tile_idx(monsters[i]->tile->get_sprite(monsters[i]->tile),
                    monsters[i]->tile->x - view_x, monsters[i]->tile->y);
        }
        if (monsters[i]->plasma != NULL) {
            int sprite = monsters[i]->plasma->get_sprite(monsters[i]->plasma);
            if (sprite != 0) {
                render_tile_idx(monsters[i]->plasma->get_sprite(monsters[i]->plasma),
                    monsters[i]->plasma->tile->x - view_x, monsters[i]->plasma->tile->y);
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

static void draw_x_levels_to_go(int x) {
    char good_work[128];
    snprintf(good_work, sizeof(good_work), "GOOD WORK! ONLY %d MORE TO GO!", x);
    draw_text_line(good_work, 50 + display_center_offset(), 58);
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
    render_tile_idx(148, 176 + offset, 0);
    render_tile_idx(148 + level, 184 + offset, 0);
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

static void draw_quit_popup(tile_t *flashing_cursor) {
    int offset = display_center_offset();

    draw_popup_box(88 + offset, 80, 5, 21);
    draw_text_line_black("QUIT? (Y OR N):", 104 + offset, 98);
    flashing_cursor->x = 224 + offset;
    draw_tile(flashing_cursor);
    flashing_cursor->tick(flashing_cursor);
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
    game->tick = 0;
    game->lives = 4;
    game->score = 0;

    game->scroll_offset = 0;
    game->scroll_remaining = 0;

    game->bullet = NULL;
    game->in_warp = WARP_NONE;
    game->level = 1;
    game->level_columns = TILEMAP_WIDTH;
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
        if (event->gbutton.button == SDL_GAMEPAD_BUTTON_EAST) {
            state->jetpack = 1;
        } else if (event->gbutton.button == SDL_GAMEPAD_BUTTON_START) {
            state->escape = 1;
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

    state->jetpack = 0;
    state->climb_up = 0;
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
 * Shows the intro until the player starts the game. Returns 0 when the player
 * quit or closed the window, so the caller can shut the game down.
 */
static int start_intro(void) {
    int32_t intro_should_finish = 0;
    uint64_t timer_begin;
    uint64_t timer_end;
    uint64_t delay;

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
    tile_create_block(&block[1], SPRITE_IDX_DIRT, 88, 64, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[2], SPRITE_IDX_DIRT, 120, 64, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[3], SPRITE_IDX_DIRT, 136, 64, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[4], SPRITE_IDX_DIRT, 152, 64, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[5], SPRITE_IDX_DIRT, 168, 64, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[6], SPRITE_IDX_DIRT, 200, 64, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[7], SPRITE_IDX_DIRT, 232, 64, TILE_SIZE, TILE_SIZE);

    tile_create_block(&block[8], SPRITE_IDX_DIRT, 88, 80, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[9], SPRITE_IDX_DIRT, 120, 80, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[10], SPRITE_IDX_CROWN, 136, 80, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[11], SPRITE_IDX_DIRT, 232, 80, TILE_SIZE, TILE_SIZE);

    tile_create_block(&block[12], SPRITE_IDX_DIRT, 88, 96, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[13], SPRITE_IDX_DIRT, 120, 96, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[14], SPRITE_IDX_DIRT, 168, 96, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[15], SPRITE_IDX_DIRT, 184, 96, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[16], SPRITE_IDX_DIRT, 200, 96, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[17], SPRITE_IDX_DIRT, 232, 96, TILE_SIZE, TILE_SIZE);

    tile_create_block(&block[18], SPRITE_IDX_DIRT, 88, 112, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[19], SPRITE_IDX_DIRT, 120, 112, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[20], SPRITE_IDX_CROWN, 216, 112, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[21], SPRITE_IDX_DIRT, 232, 112, TILE_SIZE, TILE_SIZE);

    tile_create_block(&block[22], SPRITE_IDX_DIRT, 88, 128, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[23], SPRITE_IDX_DIRT, 120, 128, TILE_SIZE, TILE_SIZE);
    tile_create_intro_fire(&block[24], 136, 128);
    tile_create_block(&block[25], SPRITE_IDX_DIRT, 152, 128, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[26], SPRITE_IDX_DIRT, 168, 128, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[27], SPRITE_IDX_DIRT, 184, 128, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[28], SPRITE_IDX_DIRT, 232, 128, TILE_SIZE, TILE_SIZE);

    tile_create_block(&block[29], SPRITE_IDX_DIRT, 88, 144, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[30], SPRITE_IDX_DIRT, 232, 144, TILE_SIZE, TILE_SIZE);

    tile_create_block(&block[31], SPRITE_IDX_DIRT, 88, 160, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[32], SPRITE_IDX_DIRT, 104, 160, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[33], SPRITE_IDX_DIRT, 120, 160, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[34], SPRITE_IDX_DIRT, 136, 160, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[35], SPRITE_IDX_DIRT, 152, 160, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[36], SPRITE_IDX_DIRT, 168, 160, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[37], SPRITE_IDX_DIRT, 184, 160, TILE_SIZE, TILE_SIZE);
    tile_create_block(&block[38], SPRITE_IDX_DIRT, 200, 160, TILE_SIZE, TILE_SIZE);
    tile_create_intro_fire(&block[39], 216, 160);
    tile_create_block(&block[40], SPRITE_IDX_DIRT, 232, 160, TILE_SIZE, TILE_SIZE);

    while (!intro_should_finish) {
        timer_begin = SDL_GetTicks();

        get_keys(&key_state);

        /* Quit, or the window was closed: leave so the game can shut down. */
        if (key_state.escape || key_state.quit) {
            return 0;
        }

        if (key_state.enter || key_state.space) {
            intro_should_finish = 1;
        }

        SDL_SetRenderDrawColor(g_renderer, 0x00, 0x00, 0x00, 0xFF);
        SDL_RenderClear(g_renderer);
        display_sync();
        g_pixels = display_lock(&g_pixels_pitch);
        if (g_pixels == NULL) {
            return 0;
        }

        clear_screen();

        // The intro screen is a fixed 320 pixel wide picture, so it is centered
        // instead of being spread over the whole framebuffer.
        int offset = display_center_offset();

        // Draw all tiles
        for (int idx = 0; idx < 41; idx++) {
            draw_tile_centered(&block[idx]);
            block[idx].tick(&block[idx]);
        }

        draw_text_line("BY JOHN ROMERO", 110 + offset, 50);
        draw_text_line("(C) 1990 SOFTDISK, INC.", 79 + offset, 57);

        display_unlock();
        display_present();

        timer_end = SDL_GetTicks();
        delay = 14 - (timer_end-timer_begin);
        delay = delay > 14 ? 0 : delay;
        SDL_Delay((uint32_t)delay);
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
    if (keys->quit || keys->key_y) {
        return G_STATE_QUIT_NOW;
    }

    if (keys->key_n) {
        g_soundfx->resume(g_soundfx);
        return G_STATE_WARP;
    }

    draw_quit_popup(&game->flashing_cursor);
    return G_STATE_WARP_POPUP;
}

static int game_popup_routine(game_context_t *game, tile_t *map, keys_state_t *keys) {
    if (keys->quit || keys->key_y) {
        return G_STATE_QUIT_NOW;
    }

    if (keys->key_n) {
        g_soundfx->resume(g_soundfx);
        return G_STATE_LEVEL;
    }

    draw_quit_popup(&game->flashing_cursor);
    return G_STATE_LEVEL_POPUP;
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
            if ((game->scroll_remaining % 1) == 0) {
                game->scroll_offset++;
            }
        } else if (game->scroll_remaining < 0) {
            game->scroll_remaining++;
            if ((game->scroll_remaining % 1) == 0) {
                game->scroll_offset--;
            }
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

    if (keys->escape) {
        g_soundfx->stop(g_soundfx);
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


static int game_level_has_secret(int level) {
    if (level == 5) {
        return 1;
    }
    return 0;
}

static int game_level(game_context_t *game, tile_t *map, keys_state_t *keys) {
    dave_t *dave = game->dave;
    if (keys->quit) {
        return G_STATE_QUIT_NOW;
    }

    if (keys->escape) {
        g_soundfx->stop(g_soundfx);
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

    if (keys->escape) {
        return G_STATE_WARP_POPUP;
    }

    game->dave->mute = 1;
    if (game->dave->tile->y > 200) {
        game->level_secret_state = SECRET_LEVEL_ENTER;
        game->dave->mute = 0;
        return G_STATE_NONE;
    }

    /*
     * The warp corridor ends when Dave leaves the view, or at the level's right
     * edge when the viewport is wider than the corridor and shows all of it.
     */
    if ((game->dave->tile->x - game_view_x(game)) > (display_width() - 20) ||
            game->dave->tile->x > ((int)game->level_columns * TILE_SIZE) - 20) {
        if (game->level_secret_state == SECRET_LEVEL_ENTER) {
            game->level_secret_state = SECRET_LEVEL_VISITED;
        } else {
            game->level++;
            game->level_secret_state = SECRET_LEVEL_NOT_VISITED;
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

    if (game->in_warp == WARP_DOWN) {
        tile_t warp_label;
        tile_t zone_label;

        tile_create_label_warp(&warp_label, 32 + display_center_offset(), 92);
        tile_create_label_zone(&zone_label, 200 + display_center_offset(), 92);
        draw_tile(&warp_label);
        draw_tile(&zone_label);
    } else {
        draw_x_levels_to_go(9 - game->level);
    }

    return G_STATE_WARP;
}

static int game_level_load(game_context_t *game, tile_t *map, char *file) {
    int i = 0;
    long fsize;
    char *buf;
    int collected_count = 0;
    int pos = 0, cur_col = 0;
    int in_comment = 0;
    char tag[4] = {0, 0, 0, 0};
    char *map_str = NULL;

    int monsters_count = 0;
    FILE* f = fopen(file, "rb");
    if (f == NULL) {
        printf("Error loading level file: %s \n", file);
        exit(0);
    }
    fseek(f, 0, SEEK_END);
    fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    buf = malloc(fsize + 1);
    fread(buf, 1, fsize, f);
    fclose(f);

    buf[fsize] = 0;
    map_str = buf;

    while (map_str[i] != 0) {
        if (in_comment) {
            if (map_str[i] == '\n' || map_str[i] == '\r') {
                in_comment = 0;
            }
        } else {
            if (map_str[i] == '#') {
                in_comment = 1;
            } else if (map_str[i] == ',') {
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
                    }

                    tile_create(&map[cur_col*12 + pos], tag, cur_col * 16, pos*16);
                    pos++;
                } else {
                    free(buf);
                    return -1;
                }
            } else if (map_str[i] == ';') {
                if (collected_count == 3) {
                    collected_count = 0;
                    tile_create(&map[cur_col*12 + pos], tag, cur_col*16, pos*16);
                    cur_col++;
                    pos = 0;
                } else {
                    free(buf);
                    return -2;
                }
            } else if (map_str[i] == '\n' || map_str[i] == '\r') {
                //just ignore
            } else {
                if (collected_count >= 3) {
                    free(buf);
                    return -3;
                }
                tag[collected_count] = map_str[i];
                collected_count++;
            }
        }
        i++;
    }

    free(buf);

    game->level_columns = (cur_col > 0 && cur_col <= TILEMAP_WIDTH) ? (uint64_t)cur_col : TILEMAP_WIDTH;

    return 0;
}

static void clear_gameloop(game_context_t *game) {
    clear_monsters(game);
    dave_destroy(game->dave);
    bullet_destroy(game->bullet);
}

static int gameloop(int starting_level) {
    game_context_t* game;
    tile_t map[TILEMAP_WIDTH * TILEMAP_HEIGHT];
    keys_state_t key_state = {0};
    char level_path[4096];

    int state = G_STATE_NONE;
    int next_state;

    uint64_t timer_begin;
    uint64_t timer_end;
    uint64_t delay;
    uint64_t tick_interval = 14;

    game = calloc(1, sizeof(game_context_t));
    init_game(game);
    game->level = starting_level;

    while (1) {
        timer_begin = SDL_GetTicks();
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

        get_keys(&key_state);

        if (state == G_STATE_NONE) {
            clear_map(map);
            clear_monsters(game);

            if (game->level_secret_state == SECRET_LEVEL_ENTER) {
                snprintf(level_path, 4096, "res/levels/level%ld_secret.ddt", (long)game->level);
            } else {
                snprintf(level_path, 4096, "res/levels/level%ld.ddt", (long)game->level);
            }

            game_level_load(game, map, level_path);
            next_state = G_STATE_LEVEL_START;

        } else if (state == G_STATE_LEVEL_START) {
            game->dave->tile->x = game->dave->default_x;
            game->dave->tile->y = game->dave->default_y;
            game->scroll_offset = 0;
            game->blinking_timer = 0;
            game_set_scroll_to_dave(game);
            next_state = G_STATE_LEVEL_BLINKING;

        } else if (state == G_STATE_LEVEL_BLINKING) {
            next_state = game_level_blinking(game, map, &key_state);

        } else if (state == G_STATE_LEVEL) {
            next_state = game_level(game, map, &key_state);

        } else if (state == G_STATE_LEVEL_POPUP) {
            next_state = game_popup_routine(game, map, &key_state);

        } else if (state == G_STATE_WARP_START) {
            clear_map(map);
            clear_monsters(game);

            game->scroll_offset = 0;

            if (game->in_warp == WARP_RIGHT) {
                game_level_load(game, map, "res/levels/warp_right.ddt");
            } else {
                game_level_load(game, map, "res/levels/warp_down.ddt");
                game->dave->face_direction = DAVE_DIRECTION_FRONT;
            }
            next_state = G_STATE_WARP;

        } else if (state == G_STATE_WARP) {
            next_state = game_warp(game, map, &key_state);

        } else if (state == G_STATE_WARP_POPUP) {
            next_state = game_warp_popup(game, map, &key_state);

        } else if (state == G_STATE_GAMEOVER) {
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

        state = next_state;

        // Render screen
        display_unlock();
        display_present();

        // Wait for the next tick
        timer_end = SDL_GetTicks();
        delay = tick_interval - (timer_end-timer_begin);
        delay = delay > tick_interval ? 0 : delay;
        SDL_Delay((uint32_t)delay);
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

    // This might start audio for some Intel Display Audio Drivers in Windows
    // SDL_setenv_unsafe("SDL_AUDIODRIVER", "directsound", 1);

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

