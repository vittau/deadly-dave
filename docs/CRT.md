# CRT filters: Blargg NTSC and scanlines

This document explains how the two CRT-flavoured video effects in the game work:
the **Blargg NTSC filter** (a simulation of the analogue colour signal that a
console sent to a television) and the **scanline** effect (the dark gaps between
the picture lines of a CRT). It also explains how both are wired into Deadly
Dave, and what a faithful port has to reproduce.

The NTSC filter is a port of Shay Green's `snes_ntsc 0.2.2` library (LGPL 2.1);
the scanline pass is written for this game. This document is self-contained: it
describes the algorithms from first principles, and the source files it names
are the ones in this repository.

---

## 1. Why a CRT does not show the framebuffer

A modern display treats the game's framebuffer as a grid of square RGB pixels:
one pixel in, one pixel on the panel. A television of the era never saw the
framebuffer at all. It received an **analogue NTSC signal** in which:

- **Brightness (luma, Y)** is the amplitude of the signal.
- **Colour (chroma, I and Q)** is a ~3.58 MHz sine wave added on top, whose
  *phase* encodes hue and whose *amplitude* encodes saturation.
- The colour reference is a **colorburst** at the start of every scanline.

The console generated that signal by outputting one sample per pixel, at a rate
(~5.37 MHz on the SNES) that is only about 1.5 times the colour subcarrier. A
single pixel is therefore **shorter than one full colour cycle**, so the TV
cannot resolve its colour: the decoder sees a phase and amplitude that depend on
where the pixel falls relative to the subcarrier, and it smears the result over
several screen columns. That is where the characteristic look comes from:

- **Dot crawl / rainbow fringing** on sharp vertical edges (e.g. white text on
  black), because a lone bright pixel carries high-frequency energy in the
  chroma band.
- **Colour bleeding** sideways, because the chroma signal is band-limited.
- A **soft, slightly blurry** image, because the luma channel is also filtered.
- **Shimmer** when the screen scrolls, because a pixel keeps or changes its
  phase as it moves.

The important property, and the one the whole optimization rests on, is that
this processing is **linear**: passing pixels A and B through the decoder equals
passing A+black through it, passing black+B through it, and summing the two
outputs. Each pixel can therefore be treated in isolation and precomputed.

---

## 2. The Blargg / `snes_ntsc` algorithm

### 2.1 Signal path

For every input pixel the filter models:

1. **RGB -> YIQ** (`Y = 0.299R + 0.587G + 0.114B`, etc.).
2. **Luma and chroma are filtered separately** and re-modulated:
   - luma gets a sharpened **sinc** low-pass (the `sharpness` / `resolution`
     setup values),
   - chroma gets a **gaussian** low-pass (the `bleed` value),
   - `artifacts` and `fringing` mix a little of each component into the other to
     recreate encoder cross-talk.
3. **Horizontal rescale** from the input sampling grid to the composite grid,
   with `NTSC_RESCALE_IN 8` samples feeding `NTSC_RESCALE_OUT 7`. The blitter
   walks the input three pixels at a time and emits seven composite columns, so
   each output pixel depends on the surrounding **six** input pixels.
4. **YIQ -> RGB** through a decoder matrix, optionally hue-rotated per burst
   phase, then **clamp** to the output range.

### 2.2 The three burst phases

NTSC's colourburst is not the same on every scanline, and it does not repeat
every line:

- A proper NTSC signal shifts the burst **180 degrees per line** (two phases).
- The NES/SNES PPU shifts it **120 degrees per line**, so the pattern only
  repeats every **three** scanlines (120 * 3 = 360).
- Between frames it shifts by another 120 degrees, so a static screen actually
  alternates between two composite frames.

The filter therefore keeps three kernels per pixel, one per burst phase, and
`ntsc_blit()` takes a `burst_phase` (0, 1 or 2) that the caller advances every
frame. Averaging the three phases together would flicker less but also shimmer
less authentically, so this port never does it.

### 2.3 The precomputed kernel table

Because the pipeline is linear, `ntsc_create()` precomputes, for **every
possible input colour**, the response of a single pixel on a black background,
for each of the 3 burst phases and each of the 3 column alignments. That is
**9 kernels**; each kernel holds the **14** output pixels the source pixel
affects (7 output columns generated per 3 input columns, plus reach). The
entries are stored as **signed packed RGB** (`NTSC_PACK_RGB(r,g,b) =
(r<<21)|(g<<11)|(b<<1)`, centred on `NTSC_RGB_BIAS`), because a changed pixel
can both raise and lower the neighbouring output values.

The table covers all 32768 RGB555 colours, which makes it about 16 MB and a
noticeable but one-off init cost. The table is the whole reason the filter runs
in real time: at blit time it is only sums and a clamp.

### 2.4 Blitting

For each output pixel the blitter sums the contributions of the six input pixels
it overlaps (kernels `k0..k5` at offsets derived from the alignment), then
clamps. In pseudo-code, for the low-res path:

```
for each row:
    START_ROW(black, black, in[0])        // prime the 3-pixel sliding window
    for each chunk of 3 input pixels:
        COLOR_IN(0, in[0]); OUT(0); OUT(1);
        COLOR_IN(1, in[1]); OUT(2); OUT(3);
        COLOR_IN(2, in[2]); OUT(4); OUT(5); OUT(6);
        advance 3 in, 7 out
    flush with black to finish the last columns
```

The clamp is the classic branch-free trick, done on the packed signed value so
all three channels are clamped at once (`NTSC_CLAMP_`), after which the channel
bits are shifted into the output byte positions.

### 2.5 Presets

`setup.artifacts` / `fringing` / `bleed` / `sharpness` select the connection:

| Preset            | Look                                                         |
| ----------------- | ------------------------------------------------------------ |
| `ntsc_composite`  | Colour bleeding **and** artefacts (rainbow edges). Default NTSC. |

The port keeps only composite, the look the game draws with; snes_ntsc's
`svideo` and `rgb` presets are not carried over because nothing selects them.

Blargg's own forum posts describing all of the above are the best primary source
next to the library header.

---

## 3. Scanlines

`scanlines` are simpler and have a different physical cause. A CRT draws a
picture as a series of horizontal lines with a small vertical gap (or a darker
transition) between them, and a 240p/200p source only fills part of each field,
so the viewer sees dark bands between bright lines.

The effect is purely a **vertical** operation on the finished image: dim or
darken every other row.

The scanline pass works as follows:

- Applied **to the game image**, not to the final scaled window, so the lines
  align with the source pixels and scale up with the picture.
- Only the **odd** rows are touched; even rows are left alone.
- The dimming is **luminance-weighted**:

  ```
  lum   = (77*R + 150*G + 29*B) >> 8
  dimmed = channel >> shift                 // shift 1 = 1/2, 2 = 1/4, 3 = 1/8
  out    = (dimmed * (255 - lum) + channel * lum) >> 8
  ```

  A black pixel is dimmed the most and a maximum-brightness pixel is not dimmed
  at all, modelling a bright beam blooming into the gap next to it.
- At higher internal resolutions it computes a partial **coverage** for the
  sub-rows of each source row, and blends the dimmed value toward the original
  by that coverage; at the original resolution it is the simple every-other-row
  mask.

Deadly Dave keeps the luminance weighting, the `shift = 1` and the preserved
alpha, with two changes. The luminance weight **saturates below full
brightness** (`SCANLINE_MAX_LUM`): with the plain formula a maximum-brightness
pixel is not dimmed at all, which makes the lines disappear over the game's
lighter artwork, and a faint trace over white reads better. And the bands are
**half a game row** tall: a 320x200
picture is filtered as if it were shown on a 640x400 screen, with two bands per
source row instead of one dark row per two. That is the partial-coverage case
above, taken to the destination's own resolution:

- The dark half of a source row is its lower half. An output row that lands
  wholly inside it is dimmed, one that lands wholly outside is untouched, and
  one that straddles the edge is blended between the two in proportion, so the
  lines stay half a row even when the picture does not scale by a whole number
  of source rows.
- Because of this the filtered image needs **more rows than the source**:
  `filter_output_height()` returns twice the source height when the destination
  is a whole multiple of it, the destination height itself otherwise (one output
  row per physical row, so the scaler never drops or doubles one), and the
  source height when the destination is too short to show half rows at all.

This is deliberately not a shader: it is a CPU pass over the framebuffer, so it
composes trivially with the NTSC output (NTSC first, then scanlines) and needs
no renderer features.

---

## 4. How this maps onto Deadly Dave

Dave renders into a single 200 px tall RGBA8888 framebuffer that is uploaded to
a streaming texture and scaled to the window (see `display.c`). The filter is
inserted between "the game finished drawing" and "the texture is uploaded":

```
game state functions ──draw──> offscreen RGBA8888 game buffer
                                        │
                          filter_render  │  (NTSC blit, then scanlines)
                                        v
                               texture RGBA8888 buffer
                                        │
                              display_present()
```

Consequences:

- Drawing no longer targets the texture directly; `display_lock()` hands out an
  **offscreen** buffer of the same width and `display_present()` runs the filter
  while copying it into the texture. Everything that draws (levels, HUD, popup,
  intro) is unchanged.
- The **NTSC blit expands the image horizontally** (7 output columns per 3
  input columns). When NTSC is on, the texture is therefore
  `filter_output_width(width)` wide and is stretched to the same destination
  rectangle, so the picture is the same size on screen, just filtered.
- The **scanlines grow the texture vertically** to the destination's own height
  (`filter_output_height()`, see above), so `display_build_texture()` tracks
  both dimensions and rebuilds when either one changes.
- The **input palette is quantised to RGB555**: the table is built for 32768
  colours (`entry = R5<<10 | G5<<5 | B5`), which is the smallest quantisation
  that does not visibly band the artwork. The table is ~16 MB and is built the
  first time NTSC is enabled, then kept; enabling it again is instant.
- Scanlines are applied to the game buffer (or to the NTSC output) after NTSC
  and are resampled to the destination's vertical resolution, so they stay half
  a game row at any window size instead of getting thicker with the scale.
- The effect is a **frame** operation, not a logic-tick one: it runs once per
  presented frame in `display_present()`, on the same side of the two clocks as
  `display_sync()`.

### 4.1 Menu

The pause menu gets a `FILTERS` row that cycles:

```
FILTERS: OFF  ->  SCANLINES  ->  NTSC  ->  BOTH  ->  OFF
```

- `SCANLINES` is the half-row dimming above.
- `NTSC` is the Blargg composite preset.
- `BOTH` is NTSC followed by scanlines on the result.

Like the other pause-menu rows (V-SYNC, FPS LIMIT, MODE), the choice is a
runtime setting and is not written to disk.

---

## 5. References

- Shay Green (Blargg), `snes_ntsc 0.2.2`, http://www.slack.net/~ant/ (LGPL 2.1).
- ModdingWiki, Dangerous Dave level format (for the surrounding game, not the
  filter): https://moddingwiki.shikadi.net/wiki/Dangerous_Dave_Level_format
