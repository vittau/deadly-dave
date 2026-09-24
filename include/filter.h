#ifndef _FILTER_H_
#define _FILTER_H_

#include <stdint.h>

/*
 * The CRT-style output filters, applied to the finished game framebuffer just
 * before it is uploaded to the display texture:
 *
 *   FILTER_SCANLINES  darkens the gaps between the picture lines of a CRT.
 *                     The bands are half a game row tall, so a 320x200 picture
 *                     is filtered as if it were shown on a 640x400 screen.
 *   FILTER_NTSC       runs the Blargg NTSC/composite filter (see ntsc.h),
 *                     which adds colour bleeding and rainbow fringing, or,
 *                     while VIDEO MODE is CGA, the CGA composite model (see
 *                     composite.h), which mixes pixel patterns into the
 *                     card's artifact colours.
 *   FILTER_BOTH       NTSC first, then scanlines over its output.
 *
 * The mode is the pause menu's FILTERS row, persisted like the other rows (see
 * config.h), so it is set before display_init() builds the texture. The NTSC
 * palette table is built lazily the first time NTSC is enabled and then kept,
 * so toggling it later is instant.
 */
#define FILTER_OFF       0
#define FILTER_SCANLINES 1
#define FILTER_NTSC      2
#define FILTER_BOTH      3
#define FILTER_MODE_COUNT 4

void filter_set_mode(int mode);
int  filter_mode(void);
/* Whether the NTSC modes model a CGA card instead of running the Blargg filter. */
void filter_set_cga(int enabled);

/*
 * Width of the filtered image for a given source width. NTSC widens the image
 * (7 output columns per 3 input columns, or 2 per column for the CGA model);
 * the other modes leave it alone.
 */
int  filter_output_width(int src_width);

/*
 * Height of the filtered image, that is how tall the destination buffer and the
 * display texture have to be. Scanlines need the image at the destination's own
 * vertical resolution so their half-row bands land on whole pixels: this returns
 * twice the source height when `dst_height` is a multiple of it, `dst_height`
 * itself when it is not, and the source height when the picture is too small to
 * show half rows. The other modes always keep the source height.
 */
int  filter_output_height(int src_height, int dst_height);

/*
 * Runs the selected effect from an RGBA8888 source image to an RGBA8888
 * destination. `src_pitch` and `dst_pitch` are in pixels. `src` is `src_height`
 * tall and `dst` must be filter_output_width(src_width) wide and `out_height`
 * tall, with `out_height` from filter_output_height(). The NTSC burst phase
 * advances once per call, so call this exactly once per presented frame.
 */
void filter_render(const uint32_t *src, int src_pitch, int src_width,
    uint32_t *dst, int dst_pitch, int src_height, int out_height);

void filter_quit(void);

#endif
