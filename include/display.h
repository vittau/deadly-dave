#ifndef _DISPLAY_H_
#define _DISPLAY_H_

#include <stdint.h>

#include <SDL3/SDL.h>

/*
 * The game renders into a low resolution framebuffer that is 200 pixels tall,
 * exactly like the original 320x200 DOS screen. The height is fixed because the
 * whole layout depends on it (16 pixels of top bar, 150 of scene, 34 of bottom
 * bar) and the levels are only 11 tiles tall.
 *
 * The width, on the other hand, follows the aspect-ratio of the display so that
 * a wide screen shows more of the level instead of being stretched:
 *
 *    4:3   -> 320 (or a little more)    16:10 -> 320   16:9 -> up to 384
 *
 * DISPLAY_MAX_WIDTH is 24 tile columns. It is a content limit, not a technical
 * one: the narrowest level (warp_down, 23 columns) and the warp corridor
 * (27 columns) still look right at that width.
 */
#define DISPLAY_HEIGHT       200
#define DISPLAY_BASE_WIDTH   320
#define DISPLAY_MAX_WIDTH    384
#define DISPLAY_TILE_SIZE    16

/* Integer scale factor, crisp pixels, black bars around the image. */
#define DISPLAY_SCALE_PIXEL_PERFECT 0
/* Fractional scale factor, fills the screen, pixels may be unevenly sized. */
#define DISPLAY_SCALE_FIT           1

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
 * Re-reads the current window size and rebuilds the framebuffer when needed.
 * Returns 1 when the framebuffer width changed.
 */
int  display_sync(void);
void display_toggle_scale_mode(void);
int  display_scale_mode(void);

int  display_width(void);
int  display_height(void);
/* Tile columns touched by the viewport, including a partially visible one. */
int  display_columns(void);
/* Horizontal shift for elements that used to be centered on a 320 wide screen. */
int  display_center_offset(void);
/* Horizontal shift for elements that used to be glued to the right edge. */
int  display_right_offset(void);

uint32_t *display_lock(int *pitch_in_pixels);
void display_unlock(void);
void display_present(void);

#endif
