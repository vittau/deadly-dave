#include <stdio.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include "display.h"

static SDL_Renderer *g_display_renderer = NULL;
static SDL_Texture *g_display_texture = NULL;
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
     * of the bars. When the picture already fills the window that runs a few
     * pixels past the bottom edge, over the black bottom bar, which is fine.
     */
    shift = (DISPLAY_BOTTOM_BAR - DISPLAY_TOP_BAR) / 2;
    if (geometry.scale > 0) {
        shift = shift * geometry.scale;
    } else {
        shift = (shift * geometry.dst.h) / DISPLAY_HEIGHT;
    }
    geometry.dst.y = geometry.dst.y + shift;

    return geometry;
}

static int display_build_texture(void) {
    if (g_display_texture != NULL) {
        SDL_DestroyTexture(g_display_texture);
    }

    g_display_texture = SDL_CreateTexture(g_display_renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING, g_geometry.width, g_geometry.height);

    if (g_display_texture == NULL) {
        printf("Failed to create the framebuffer texture. Error: (%s) \n", SDL_GetError());
        return -1;
    }

    /* SDL3 filters textures linearly by default, which would blur the pixel art. */
    SDL_SetTextureScaleMode(g_display_texture, SDL_SCALEMODE_NEAREST);

    return 0;
}

int display_init(SDL_Renderer *renderer, int scale_mode) {
    int out_w = 0;
    int out_h = 0;

    g_display_renderer = renderer;
    g_scale_mode = scale_mode;

    SDL_GetCurrentRenderOutputSize(g_display_renderer, &out_w, &out_h);
    g_geometry = display_compute_geometry(out_w, out_h, g_scale_mode);

    return display_build_texture();
}

void display_quit(void) {
    if (g_display_texture != NULL) {
        SDL_DestroyTexture(g_display_texture);
        g_display_texture = NULL;
    }
    g_display_renderer = NULL;
}

int display_sync(void) {
    display_geometry_t next;
    int out_w = 0;
    int out_h = 0;

    if (g_display_renderer == NULL) {
        return 0;
    }

    SDL_GetCurrentRenderOutputSize(g_display_renderer, &out_w, &out_h);
    next = display_compute_geometry(out_w, out_h, g_scale_mode);

    if (next.width != g_geometry.width) {
        g_geometry = next;
        display_build_texture();
        return 1;
    }

    g_geometry = next;
    return 0;
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

uint32_t *display_lock(int *pitch_in_pixels) {
    uint32_t *pixels = NULL;
    int pitch = 0;

    if (!SDL_LockTexture(g_display_texture, NULL, (void*)&pixels, &pitch)) {
        printf("Failed to lock the framebuffer texture. Error: (%s) \n", SDL_GetError());
        return NULL;
    }

    if (pitch_in_pixels != NULL) {
        *pitch_in_pixels = pitch / (int)sizeof(uint32_t);
    }

    return pixels;
}

void display_unlock(void) {
    SDL_UnlockTexture(g_display_texture);
}

void display_present(void) {
    SDL_FRect dst;

    /* The geometry is computed in whole pixels, SDL3 draws with floats. */
    dst.x = (float)g_geometry.dst.x;
    dst.y = (float)g_geometry.dst.y;
    dst.w = (float)g_geometry.dst.w;
    dst.h = (float)g_geometry.dst.h;

    SDL_RenderTexture(g_display_renderer, g_display_texture, NULL, &dst);
    SDL_RenderPresent(g_display_renderer);
}
