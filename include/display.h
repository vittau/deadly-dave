#ifndef _DISPLAY_H_
#define _DISPLAY_H_

#include <stdint.h>

#include <SDL3/SDL.h>

/*
 * The game renders into a low resolution framebuffer that is 200 pixels tall,
 * exactly like the original 320x200 DOS screen. The height is fixed because the
 * whole layout depends on it and the levels are only 11 tiles tall:
 *
 *     0                    DISPLAY_TOP_BAR (16)  score, level and lives
 *     ^                    ^
 *     | top bar            | DISPLAY_SCENE_TOP (16)
 *     v                    v
 *     | scene              | the part the player looks at
 *     ^                    ^
 *     | bottom bar         | DISPLAY_SCENE_BOTTOM (166)
 *     v                    v
 *     200                  bottom of the bottom bar
 *
 * The width, on the other hand, follows the aspect-ratio of the display, so a
 * wide screen shows more of the level instead of a stretched picture, and there
 * is no fixed limit: the viewport is as wide as the screen asks for, up to the
 * width of the widest level the tile map can hold (100 columns). A level
 * narrower than the viewport is centered, with black on both sides.
 */
#define DISPLAY_HEIGHT       200
#define DISPLAY_BASE_WIDTH   320
/* The tile map is 100 columns wide, that is all the level there is to show. */
#define DISPLAY_MAX_WIDTH    1600
#define DISPLAY_TILE_SIZE    16

/*
 * The scene sits between two HUD bars, and the bottom one is taller than the
 * top one. The picture is shifted down by half of that difference so that the
 * scene, not the whole framebuffer, is what ends up centered on the screen.
 */
#define DISPLAY_TOP_BAR      16
#define DISPLAY_BOTTOM_BAR   34
#define DISPLAY_SCENE_TOP    DISPLAY_TOP_BAR
#define DISPLAY_SCENE_BOTTOM (DISPLAY_HEIGHT - DISPLAY_BOTTOM_BAR)

/* Integer scale factor, crisp pixels, black bars around the image. */
#define DISPLAY_SCALE_PIXEL_PERFECT 0
/* Fractional scale factor, fills the screen, pixels may be unevenly sized. */
#define DISPLAY_SCALE_FIT           1
/*
 * A fixed factor, whatever the window size: one, two or three screen pixels per
 * game pixel, the picture stays 200, 400 or 600 pixels tall with black around
 * it, and a wide window shows up to the whole 100 column level at once. A
 * window too small for the factor gets the largest one that fits, and FIT
 * below 320x200. The factor is the mode's distance from 1X, plus one.
 */
#define DISPLAY_SCALE_1X            2
#define DISPLAY_SCALE_2X            3
#define DISPLAY_SCALE_3X            4
#define DISPLAY_SCALE_COUNT         5

typedef struct display_geometry_struct {
    int width;      /* framebuffer width, in game pixels                    */
    int height;     /* framebuffer height, always DISPLAY_HEIGHT            */
    int scale;      /* integer scale factor in use, 0 when it is fractional */
    SDL_Rect dst;   /* where the framebuffer lands inside the window        */
} display_geometry_t;

/*
 * Pure function: given the size of the drawable area and a scaling mode,
 * decide how wide the framebuffer should be and where to blit it.
 * Exposed so it can be unit tested without a window.
 */
display_geometry_t display_compute_geometry(int out_w, int out_h, int scale_mode);

int  display_init(SDL_Renderer *renderer, int scale_mode);
void display_quit(void);

/*
 * Re-reads the current window size and rebuilds the framebuffer and the texture
 * to match, so a resize or a filter change is picked up before anything is
 * drawn into the frame.
 */
void display_sync(void);
/*
 * The SCALING row's setting. display_init() takes the mode it starts on and
 * this changes it afterwards; the new mode is picked up by the next
 * display_sync(), which runs at the top of the frame.
 */
void display_set_scale_mode(int mode);
int  display_scale_mode(void);
/* Turns vertical sync on the renderer on or off, for the pause menu's V-SYNC option. */
void display_set_vsync(int enabled);

int  display_width(void);
/* Tile columns touched by the viewport, including a partially visible one. */
int  display_columns(void);
/* Horizontal shift for elements that used to be centered on a 320 wide screen. */
int  display_center_offset(void);
/* Horizontal shift for elements that used to be glued to the right edge. */
int  display_right_offset(void);

/*
 * How long one frame of the display the window sits on lasts, in nanoseconds,
 * which is how often it is worth putting a picture on the screen. Falls back to
 * 60 Hz whenever the refresh rate is unknown or not believable.
 */
uint64_t display_frame_period_ns(void);

uint32_t *display_lock(int *pitch_in_pixels);
void display_unlock(void);
void display_present(void);

#endif
