#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#include "display.h"
#include "filter.h"

static SDL_Renderer *g_display_renderer = NULL;
static SDL_Texture *g_display_texture = NULL;
/* Size of the texture currently allocated, which is the filtered size. */
static int g_texture_width = 0;
/*
 * The filtered height, which the scanline filter grows to the destination's own
 * vertical resolution and the other modes keep at DISPLAY_HEIGHT.
 */
static int g_texture_height = 0;
/*
 * The game draws into this offscreen buffer instead of straight into the
 * texture. display_present() runs the output filter while copying it into the
 * texture, so a mode change never needs the game to draw differently.
 */
static uint32_t *g_frame_pixels = NULL;
static int g_frame_width = 0;
/* Safe default so that the game can query the size before the window exists. */
static display_geometry_t g_geometry = {
    DISPLAY_BASE_WIDTH, DISPLAY_HEIGHT, 1, { 0, 0, DISPLAY_BASE_WIDTH, DISPLAY_HEIGHT }
};
static int g_scale_mode = DISPLAY_SCALE_PIXEL_PERFECT;

/*
 * Widths are kept on the 8 pixel font grid and never leave the range the game
 * artwork and the level data can cope with.
 */
static int clamp_width(int width) {
    if (width < DISPLAY_BASE_WIDTH) {
        width = DISPLAY_BASE_WIDTH;
    }
    if (width > DISPLAY_MAX_WIDTH) {
        width = DISPLAY_MAX_WIDTH;
    }
    return width & ~7;
}

display_geometry_t display_compute_geometry(int out_w, int out_h, int scale_mode) {
    display_geometry_t geometry;
    int scale;
    int shift;

    if (out_w < 1 || out_h < 1) {
        out_w = DISPLAY_BASE_WIDTH;
        out_h = DISPLAY_HEIGHT;
    }

    geometry.height = DISPLAY_HEIGHT;

    /*
     * Largest integer factor at which the original 320x200 screen still fits in
     * the window. On a screen narrower than 16:10 the width is what limits it.
     */
    scale = out_h / DISPLAY_HEIGHT;
    if ((out_w / DISPLAY_BASE_WIDTH) < scale) {
        scale = out_w / DISPLAY_BASE_WIDTH;
    }

    if (scale_mode == DISPLAY_SCALE_PIXEL_PERFECT && scale >= 1) {
        /*
         * The scale factor comes from the height, the leftover width is given
         * back to the game as extra framebuffer columns. On a 16:10 screen this
         * lands on 320 and the picture fills the display (1280x800 -> 4x).
         */
        geometry.width = clamp_width(out_w / scale);
        geometry.scale = scale;
        geometry.dst.w = geometry.width * scale;
        geometry.dst.h = DISPLAY_HEIGHT * scale;
    } else {
        /*
         * The framebuffer takes the aspect-ratio of the screen and is then
         * fitted into it. All of it is integer arithmetic on purpose, so that
         * the result is identical on every platform and needs no libm.
         */
        geometry.width = clamp_width(((out_w * DISPLAY_HEIGHT) + (out_h / 2)) / out_h);
        geometry.scale = 0;

        if ((out_w * DISPLAY_HEIGHT) <= (out_h * geometry.width)) {
            /* The width of the window is what limits the picture. */
            geometry.dst.w = out_w;
            geometry.dst.h = ((out_w * DISPLAY_HEIGHT) + (geometry.width / 2)) / geometry.width;
        } else {
            /* The height of the window is what limits the picture. */
            geometry.dst.h = out_h;
            geometry.dst.w = ((out_h * geometry.width) + (DISPLAY_HEIGHT / 2)) / DISPLAY_HEIGHT;
        }
    }

    if (geometry.dst.w > out_w) {
        geometry.dst.w = out_w;
    }
    if (geometry.dst.h > out_h) {
        geometry.dst.h = out_h;
    }
    if (geometry.dst.w < 1) {
        geometry.dst.w = 1;
    }
    if (geometry.dst.h < 1) {
        geometry.dst.h = 1;
    }

    geometry.dst.x = (out_w - geometry.dst.w) / 2;
    geometry.dst.y = (out_h - geometry.dst.h) / 2;

    /*
     * What the player looks at is the scene between the two HUD bars, and the
     * bottom bar is taller than the top one, so a picture centered as a whole
     * still shows the scene a little high. Move it down by half the difference
     * of the bars, but only as far as the window allows: on a screen the picture
     * exactly fills (1280x800 on a Steam Deck, where the scale is 4) there is no
     * room at all, and spending the shift there pushed the last rows, and the
     * trophy banner sitting on them, off the bottom edge.
     */
    shift = (DISPLAY_BOTTOM_BAR - DISPLAY_TOP_BAR) / 2;
    if (geometry.scale > 0) {
        shift = shift * geometry.scale;
    } else {
        shift = (shift * geometry.dst.h) / DISPLAY_HEIGHT;
    }
    geometry.dst.y = geometry.dst.y + shift;
    if (geometry.dst.y > (out_h - geometry.dst.h)) {
        geometry.dst.y = out_h - geometry.dst.h;
    }

    return geometry;
}

static int display_build_texture(int width, int height) {
    if (g_display_texture != NULL && g_texture_width == width &&
        g_texture_height == height) {
        return 0;
    }
    if (g_display_texture != NULL) {
        SDL_DestroyTexture(g_display_texture);
        g_display_texture = NULL;
    }

    g_display_texture = SDL_CreateTexture(g_display_renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING, width, height);

    if (g_display_texture == NULL) {
        printf("Failed to create the framebuffer texture. Error: (%s) \n", SDL_GetError());
        g_texture_width = 0;
        g_texture_height = 0;
        return -1;
    }

    /* SDL3 filters textures linearly by default, which would blur the pixel art. */
    SDL_SetTextureScaleMode(g_display_texture, SDL_SCALEMODE_NEAREST);

    g_texture_width = width;
    g_texture_height = height;
    return 0;
}

/*
 * Makes the offscreen buffer the size of the current framebuffer. It is filled
 * with opaque black so a frame that does not cover every pixel (the intro, a
 * level shorter than the viewport) cannot show stale data.
 */
static int display_build_frame(void) {
    uint32_t *pixels;

    if (g_frame_pixels != NULL && g_frame_width == g_geometry.width) {
        return 0;
    }

    pixels = (uint32_t *)realloc(g_frame_pixels,
        (size_t)g_geometry.width * DISPLAY_HEIGHT * sizeof(uint32_t));
    if (pixels == NULL) {
        printf("Failed to allocate the game framebuffer. Error: (%s) \n", SDL_GetError());
        return -1;
    }

    g_frame_pixels = pixels;
    g_frame_width = g_geometry.width;
    SDL_memset4(g_frame_pixels, 0x000000FF, (size_t)g_geometry.width * DISPLAY_HEIGHT);
    return 0;
}

int display_init(SDL_Renderer *renderer, int scale_mode) {
    int out_w = 0;
    int out_h = 0;

    g_display_renderer = renderer;
    g_scale_mode = scale_mode;

    /*
     * Present on the vertical blank, so a frame is shown whole instead of torn.
     * The game speed does not hang on it any more: the loop paces its logic on
     * its own clock (see the frame pacer in game.c), the display only decides
     * how often the picture is put on the screen. A backend that cannot do it
     * is not an error either, the loop then paces the frames by itself off
     * display_frame_period_ns().
     */
    if (!SDL_SetRenderVSync(g_display_renderer, 1)) {
        printf("Could not enable vsync, frames are paced by the loop alone. Error: (%s) \n",
            SDL_GetError());
    }

    SDL_GetCurrentRenderOutputSize(g_display_renderer, &out_w, &out_h);
    g_geometry = display_compute_geometry(out_w, out_h, g_scale_mode);

    if (display_build_frame() != 0) {
        return -1;
    }
    return display_build_texture(filter_output_width(g_geometry.width),
        filter_output_height(DISPLAY_HEIGHT, g_geometry.dst.h));
}

void display_quit(void) {
    if (g_display_texture != NULL) {
        SDL_DestroyTexture(g_display_texture);
        g_display_texture = NULL;
    }
    g_texture_width = 0;
    g_texture_height = 0;
    free(g_frame_pixels);
    g_frame_pixels = NULL;
    g_frame_width = 0;
    filter_quit();
    g_display_renderer = NULL;
}

void display_sync(void) {
    display_geometry_t next;
    int out_w = 0;
    int out_h = 0;

    if (g_display_renderer == NULL) {
        return;
    }

    SDL_GetCurrentRenderOutputSize(g_display_renderer, &out_w, &out_h);
    next = display_compute_geometry(out_w, out_h, g_scale_mode);

    g_geometry = next;
    display_build_frame();
    /*
     * Rebuilt here as well as in display_present(), so a filter mode change
     * (which changes the filtered size) is picked up even when the window did
     * not move.
     */
    display_build_texture(filter_output_width(g_geometry.width),
        filter_output_height(DISPLAY_HEIGHT, g_geometry.dst.h));
}

/*
 * Only flips the flag. The new mode is picked up by the next display_sync(),
 * which runs at the top of the frame: rebuilding the framebuffer here would
 * pull it from under the frame that is currently being drawn.
 */
void display_toggle_scale_mode(void) {
    g_scale_mode = (g_scale_mode == DISPLAY_SCALE_PIXEL_PERFECT) ?
        DISPLAY_SCALE_FIT : DISPLAY_SCALE_PIXEL_PERFECT;
}

void display_set_vsync(int enabled) {
    if (g_display_renderer == NULL) {
        return;
    }
    if (!SDL_SetRenderVSync(g_display_renderer, enabled ? 1 : 0)) {
        printf("Could not change vsync. Error: (%s) \n", SDL_GetError());
    }
}

int display_width(void) {
    return g_geometry.width;
}

int display_columns(void) {
    return (g_geometry.width + (DISPLAY_TILE_SIZE - 1)) / DISPLAY_TILE_SIZE;
}

int display_center_offset(void) {
    return (g_geometry.width - DISPLAY_BASE_WIDTH) / 2;
}

int display_right_offset(void) {
    return g_geometry.width - DISPLAY_BASE_WIDTH;
}

/*
 * The rate the game falls back to when the display will not say what it runs
 * at, and the range outside which the answer is not believed: a mode SDL
 * reports as 5 Hz or 10000 Hz is a broken or virtual display, not something to
 * pace the game with.
 */
#define DISPLAY_FALLBACK_PERIOD_NS (SDL_NS_PER_SECOND / 60)
#define DISPLAY_MIN_PERIOD_NS      (SDL_NS_PER_SECOND / 360)
#define DISPLAY_MAX_PERIOD_NS      (SDL_NS_PER_SECOND / 24)

uint64_t display_frame_period_ns(void) {
    const SDL_DisplayMode *mode;
    SDL_Window *window;
    SDL_DisplayID display;
    uint64_t period;

    if (g_display_renderer == NULL) {
        return DISPLAY_FALLBACK_PERIOD_NS;
    }

    /*
     * Asked for again on every frame instead of cached: the window can be
     * dragged to a second screen that runs at another rate, and the mode of the
     * one it is on can change under it. All of this reads SDL's own display
     * list, so it costs about as little as the size query display_sync() makes.
     */
    window = SDL_GetRenderWindow(g_display_renderer);
    if (window == NULL) {
        return DISPLAY_FALLBACK_PERIOD_NS;
    }

    display = SDL_GetDisplayForWindow(window);
    if (display == 0) {
        return DISPLAY_FALLBACK_PERIOD_NS;
    }

    mode = SDL_GetCurrentDisplayMode(display);
    if (mode == NULL) {
        return DISPLAY_FALLBACK_PERIOD_NS;
    }

    if (mode->refresh_rate_numerator > 0 && mode->refresh_rate_denominator > 0) {
        /* Exact: 60000/1001 stays 60000/1001 instead of going through a float. */
        period = (SDL_NS_PER_SECOND * (uint64_t)mode->refresh_rate_denominator) /
            (uint64_t)mode->refresh_rate_numerator;
    } else if (mode->refresh_rate > 0.0f) {
        period = (uint64_t)((double)SDL_NS_PER_SECOND / (double)mode->refresh_rate);
    } else {
        /* 0 means "unknown", which some backends and virtual displays do report. */
        return DISPLAY_FALLBACK_PERIOD_NS;
    }

    if (period < DISPLAY_MIN_PERIOD_NS || period > DISPLAY_MAX_PERIOD_NS) {
        return DISPLAY_FALLBACK_PERIOD_NS;
    }

    return period;
}

uint32_t *display_lock(int *pitch_in_pixels) {
    if (g_frame_pixels == NULL && display_build_frame() != 0) {
        return NULL;
    }

    if (pitch_in_pixels != NULL) {
        *pitch_in_pixels = g_frame_width;
    }

    return g_frame_pixels;
}

/*
 * Nothing to unlock any more: the game now draws into the offscreen buffer and
 * display_present() is the one that locks the texture, to filter into it.
 */
void display_unlock(void) {
}

void display_present(void) {
    SDL_FRect dst;
    void *texture_pixels = NULL;
    int texture_pitch = 0;
    int out_width;
    int out_height;

    if (g_display_renderer == NULL || g_frame_pixels == NULL) {
        return;
    }

    out_width = filter_output_width(g_geometry.width);
    out_height = filter_output_height(DISPLAY_HEIGHT, g_geometry.dst.h);
    if (display_build_texture(out_width, out_height) != 0) {
        return;
    }

    if (!SDL_LockTexture(g_display_texture, NULL, &texture_pixels, &texture_pitch)) {
        printf("Failed to lock the framebuffer texture. Error: (%s) \n", SDL_GetError());
        return;
    }

    filter_render(g_frame_pixels, g_frame_width, g_geometry.width,
        (uint32_t *)texture_pixels, texture_pitch / (int)sizeof(uint32_t),
        DISPLAY_HEIGHT, out_height);

    SDL_UnlockTexture(g_display_texture);

    /* The geometry is computed in whole pixels, SDL3 draws with floats. */
    dst.x = (float)g_geometry.dst.x;
    dst.y = (float)g_geometry.dst.y;
    dst.w = (float)g_geometry.dst.w;
    dst.h = (float)g_geometry.dst.h;

    SDL_RenderTexture(g_display_renderer, g_display_texture, NULL, &dst);
    SDL_RenderPresent(g_display_renderer);
}
