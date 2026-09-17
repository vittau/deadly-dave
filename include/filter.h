#ifndef _FILTER_H_
#define _FILTER_H_

#include <stdint.h>

/*
 * The CRT-style output filters, applied to the finished game framebuffer just
 * before it is uploaded to the display texture:
 *
 *   FILTER_SCANLINES  darkens every other framebuffer row, like the gaps
 *                     between the picture lines of a CRT.
 *   FILTER_NTSC       runs the Blargg NTSC/composite filter (see ntsc.h),
 *                     which adds colour bleeding and rainbow fringing.
 *   FILTER_BOTH       NTSC first, then scanlines over its output.
 *
 * The mode is a runtime setting (the pause menu's FILTERS row); the NTSC
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

/* True when the selected mode runs the NTSC filter. */
int  filter_ntsc_enabled(void);

/*
 * Width of the filtered image for a given source width. NTSC widens the image
 * (7 output columns per 3 input columns); the other modes leave it alone.
 */
int  filter_output_width(int src_width);

/*
 * Runs the selected effect from an RGBA8888 source image to an RGBA8888
 * destination. `src_pitch` and `dst_pitch` are in pixels. `dst` must be at
 * least filter_output_width(src_width) wide and `height` tall. The NTSC burst
 * phase advances once per call, so call this exactly once per presented frame.
 */
void filter_render(const uint32_t *src, int src_pitch, int src_width,
    uint32_t *dst, int dst_pitch, int height);

void filter_quit(void);

#endif
