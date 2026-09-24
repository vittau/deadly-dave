#ifndef _COMPOSITE_H_
#define _COMPOSITE_H_

#include <stdint.h>

/*
 * The IBM CGA's composite output, for the FILTERS row's NTSC modes while VIDEO
 * MODE is CGA. The Blargg filter (ntsc.h) encodes RGB the way an SNES does,
 * with three pixels to every two colour cycles and the phase turning on every
 * line; the CGA did neither, and its "artifact colours" come from exactly
 * that.
 *
 * It is reenigne's algorithm (see composite.c): every pixel is turned back into
 * its 4 bit IRGB colour, into two dots of the card's 14.318 MHz clock (four to
 * a colour cycle) and into the signal his measurements of a real card give
 * for each dot, its neighbour and its phase; that signal is then decoded the
 * way a plain television does. Every line starts on the same phase, so a
 * pattern of pixels mixes into the same colour on every line and every frame.
 * Nothing is tuned by eye: it is the old IBM CGA in 320x200 mode at neutral
 * monitor settings.
 *
 * The output is two pixels per source pixel, one per dot.
 */

/* Width of the composite image for a given source width. */
int  composite_output_width(int src_width);

/*
 * Renders `height` rows of an RGBA8888 image into an RGBA8888 destination that
 * is composite_output_width(src_width) wide. Pitches are in pixels.
 */
void composite_render(const uint32_t *src, int src_pitch, int src_width,
    uint32_t *dst, int dst_pitch, int height);

void composite_quit(void);

#endif
