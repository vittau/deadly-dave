# AGENTS.md

Reimplementation of *Dangerous Dave* in C99 + SDL3: one executable that reads
`res/` at runtime. No package manager, no test framework, no generated sources
except the icons.

## Build and run

- `make` builds `ddave` against the system SDL3 (`pkg-config sdl3`). Fast, use it
  while iterating. It compiles with `-std=c99 -Wall` and is warning-free; there
  is no linter, formatter or typecheck besides that. On macOS it also builds
  `Deadly Dave.app`; a bare binary is opened by Terminal when double-clicked.
  `-Wextra` only adds the unused parameters that the state, route and callback
  functions share a signature for (`dave_state_*`, the monster states,
  `game_popup_routine()`, the soundfx `stop`/`resume` callbacks); that is on
  purpose.
- The same sources also have to build with MSVC (the Windows CI job, Ninja plus
  `cl`), so anything POSIX only has to be guarded. See the Gotchas.
- CMake (`cmake -S . -B build -G Ninja && cmake --build build`) fetches and
  statically links SDL 3.4.16 and writes the binary to the repo root as
  `deadly-dave`. This is what CI and the releases use; configuring takes ~40s
  because it clones SDL.
- The game `chdir`s to `SDL_GetBasePath()` and reads `res/` from there. Run it
  from the repo root (or from `Deadly Dave.app`); from anywhere else it prints
  "Could not find the game assets" and exits `-6`.
- `make app` (macOS) builds the bundle. It copies the binary, so re-run it after
  a rebuild to test the bundle. `make icon` regenerates `assets/icon.*`.

## Tests

- `cd tests && make`. `./tests/test_display` checks `display_compute_geometry`
  (framebuffer width, scaling, centring) and is pure: run it after touching
  `display.c`, and update its expected widths if the geometry changes. It links
  `filter.c` and `ntsc.c` as well, because `display.c` does; `tests/Makefile`
  adds `-lm` for the NTSC filter's `sin`/`cos`/`pow`/`exp`.
- `./tests/test_filter` checks `filter_output_height` and the scanline pattern
  (`filter_render` at twice and three times the source height, where a row that
  straddles the dark half must come out blended). It is pure and links only
  `filter.c` and `ntsc.c`.
- `test_display`, `test_filter` and `test_invfreq` (which writes `out.raw` into
  `tests/`) run headless. `test_monster` opens a window and needs a real display,
  so CI runs none of them.
- `test_monster` does not link `game.c` or `display.c`: it carries its own copies
  of the drawing, input and asset loading code. It exercises `tile.c`,
  `plasma.c` and `monster.c` for real, but a change in `game.c` will not show up
  there.
- The third argument of `invfreq_decode_soundfx()` is samples *per symbol*, not a
  buffer size; every tune has its own value in `soundfx.c` (the jumping sound is
  345). A too large value writes far past the buffer and the process dies with
  SIGBUS, which is what `test_invfreq` used to do.
- `./tests/test_config` checks the settings file parser: a full file, the lines
  it has to tolerate (comments, blank lines, CRLF, an unknown key, a key with
  spaces around it, a non numeric value, a last line with no newline), the range
  guard on `fps_limit`/`filter` and the `config_format()` → `config_parse()`
  round trip. It is pure and links `config.c` for the parser alone.

## Settings

The pause menu rows are kept between runs. `config.c` / `include/config.h` owns
them: `config_t` carries V-SYNC, FPS LIMIT, FILTERS, MODE and SCALING, with the
defaults in the `g_config` initializer in `game.c` (a first run, or a deleted
file, gets vsync on, the frame paced to the display's refresh, no filter, full
screen and pixel perfect scaling). `config_load()` runs right after `SDL_Init()`
in `game_main()`, before the window is created, so MODE decides that window, and
it runs before `display_init()`, which is handed SCALING and reads the filter
mode, so both are already in place when the texture is sized; FPS LIMIT is only
read by the pacer. Every pause menu change calls `config_save()`, as do
`toggle_fullscreen()` and `toggle_scale_mode()`, because `Cmd`/`Alt`+`Enter` and
`F5` go through those two as well. `-w` overrides MODE for that one run and is
deliberately not written back.

`FPS_LIMIT_REFRESH_INDEX`/`_UNLIMITED_`/`_COUNT` live in `config.h` rather than
`game.c`, because they are the range of a persisted field and `config_parse()`
has to guard it; the labels and the pacing stay in `game.c`.

The file is `<SDL_GetPrefPath("vittau", "deadly-dave")>/config.ini`, a plain
`key=value` list: `~/Library/Application Support/vittau/deadly-dave/` on macOS,
`%APPDATA%\vittau\deadly-dave\` on Windows, `~/.local/share/vittau/deadly-dave/`
on Linux. The two strings are the directory name SDL builds, so they follow
SDL's rules and match the bundle identifier. **Never** write next to
`SDL_GetBasePath()`: on macOS that is inside the `.app`, which is read-only once
installed and whose code signature writing to it would break. `config_parse()`
and `config_format()` are pure and tested; a key this version does not know, a
malformed line and a number out of range are skipped one at a time, so a
hand-edited file, or one written by a newer version, still loads what it can.
Keep a new setting by adding it to `config_t`, to both branches of
`config_parse()`, to `config_format()` and to the `g_config` initializer.

## CRT filters

`docs/CRT.md` explains the theory and the port in full. In short:

- `ntsc.c` / `include/ntsc.h` is a scalar C99 port of Shay Green's `snes_ntsc`
  0.2.2, stripped of the SIMD, hi-res and field-merge paths. It takes RGB555
  palette indices and writes RGBA8888. The palette table is 32768 entries × 128
  words (~16 MB) and is built once by `ntsc_create()`; that is a visible pause,
  so the game builds it lazily the first time NTSC is enabled and only frees it
  in `filter_quit()`.
  It uses the library's full 8-bit internal range (`NTSC_RGB_BITS 8`). Do not
  switch back to half range (`rgb_bits 7`): it leaves headroom for doubled
  hi-res pixels and needs a brightness boost elsewhere to compensate, so with no
  shader to do that it just renders the game at half brightness (midtones
  crushed, only saturated colours reaching full scale).
- `filter.c` / `include/filter.h` owns the `FILTERS` mode
  (OFF/SCANLINES/NTSC/BOTH), quantises the framebuffer to RGB555, runs the
  blitter, and applies the scanlines (luminance-weighted `>> 1`, alpha preserved,
  except the weight saturates at `SCANLINE_MAX_LUM` so white keeps a faint trace
  instead of escaping the effect entirely).
  The bands are **half a game row** tall, as if 320x200 were
  shown on a 640x400 screen, so the scanlines need more rows than the source:
  `filter_output_height(src, dst)` returns `2*src` when `dst` is a multiple of
  it, `dst` itself otherwise (a row per physical row, no row dropped or doubled
  by the scaler), and `src` when the picture is too short for half rows. A row
  that straddles the edge of the dark half is mixed between dimmed and original.
  `display.c` must therefore size the texture with **both** `filter_output_width`
  and `filter_output_height`, and rebuild it when either changes; `filter_render`
  takes `src_height` and `out_height`.
  NTSC widens the image: `filter_output_width(w)` is `((w-1)/3 + 1) * 7`, so a
  320 pixel framebuffer becomes 749.
- Both are `-lm` users (`sin`/`cos`/`pow`/`exp`), hence the extra link line in
  the Makefile, CMakeLists.txt and tests/Makefile.
- The pause menu's `FILTERS` row cycles the mode. Like V-SYNC/FPS/MODE it is
  kept between runs (see Settings); the saved mode is applied before
  `display_init()`, so the texture is built at the filtered size from the start.

## Levels and the original game data

`original/` is gitignored and holds the original game: `Dave.EXE` (LZEXE v0.91),
`UNPACKED_DAVE.EXE` (it runs as is in DOSBox) and `EGADAVE.DAV`. Everything below
is decoded from `UNPACKED_DAVE.EXE`; the format is on the ModdingWiki
(`https://moddingwiki.shikadi.net/wiki/Dangerous_Dave_Level_format`).

- Levels start at `0x26e0a`: ten 1280-byte chunks of 256 path bytes + 1000 tile
  bytes (100x10, row major, one byte per tile = the `res/tiles/tileN.bmp` index)
  + 24 unused. Start state: motion flags at `0x257e8`, startX at `0x257f2` and
  startY at `0x25806` (both `u16[10]`). Monster table at `0x25b66` (80 bytes a
  level: enabled/x/y/offset/calmness, four of each). Warp map at `0x2583a`
  (level -> chunk); the warp view starts at the column in `0x25862` and Dave at
  `0x25862+20` (x, relative to that column); the warp startY is 16.
- A chunk can hold two levels: the main one on the left (where Dave starts) and
  a warp/bonus on the right. level1+level10 warp (chunk1), level2+level5 warp
  (chunk2), level6+level8 warp (chunk6), level7+level9 warp (chunk7). Those
  warps are the `levelN_secret.ddt` files, loaded when Dave walks off the level
  edge (`game_level_has_secret()`); the rest of a chunk is plain level. A secret
  file is the same full 100-column chunk with `D` on the warp start column, so
  the two definitions of a chunk are duplicates on purpose. A warp's own door
  hands Dave back to a fixed level with no intermission banner (`game_warp_exit_level()`:
  5->8, 8->9, 9->10, 10->3).
- A `.ddt` is the chunk transposed: one line per column, a comma separated tag
  per row, `;` at the end. A line carries 11 tags and the first is the row that
  hides behind the top HUD bar, so tag `T` in line `L` is column `L`, row `T-1`.
  Tags are 3 chars and go through `tile_create()`; `D` spawns Dave at
  `startX/16, startY/16`. A monster tag replaces the tile under it, so put it on
  a cell the original leaves empty.
- Tag to tile byte is not one to one. `TR1`/`TR2` (34/35) are the same drawing,
  but the tree corners are not: byte 43 is top-left (`TR4`), 44 top-right
  (`TR3`), 45 bottom-left (`TR6`), 46 bottom-right (`TR5`), because
  `tile_create_tree()` maps `TR5` to 46 and `TR6` to 45. `PPK`/`PPF` (30/31) look
  identical too, PPF being the one Dave falls through. Fire `FR1..4`, water
  `WT1..5` and vines `VI1..4` are animation phases of a single tile, and moss
  `  M`/`D+M` both draw the moss tile (`D+M` also spawns Dave).
- Decode a level to the full 100-column chunk, never cut it short: the game
  follows the window aspect ratio, so a wide window draws the whole level,
  including a warp half that cannot be reached from the main level. Cutting one
  only leaves the extra columns black.
- To check a decode, render every `ddt` cell back to its tile byte and compare it
  with the chunk bytes (must match everywhere). An independent cross-check is the
  vgmaps map, `https://vgmaps.de/files/pc/maps/dangerous-dave-in-the-deserted-pirates-hideout-level-NN-pc-map.webp`
  (`NN` 01..10; needs a browser User-Agent), which confirms the tile layer but
  draws the monsters by hand and is 150px tall, so it cuts the bottom row.
- The font is 8x6 glyph tiles: `res/font/<name>.bmp` is `res/tiles/tile(500+index).bmp`
  (white glyph on transparent) and `res/font/black/<name>.bmp` is
  `tile(600+index).bmp` (black glyph on white), `index` being the glyph's place
  in `font_chars[]` (`A-Z`, `0-9`, space, `, . ( ) ! ? - ' :`). A new glyph needs
  both BMPs, `res/font/<name>.bmp` and `res/font/black/<name>.bmp` (byte for byte
  the two tiles), and the character appended to the **end** of `font_chars[]`:
  the place in that string is the offset from tile 500, so inserting one in the
  middle would hand every glyph after it its neighbour's tile.

## Gotchas

- SDL3, not SDL2: `SDL_Init` returns true on success, event types are
  `SDL_EVENT_*`, and the `SDL_Free*` names are now `SDL_DestroySurface` /
  `SDL_DestroyTexture`.
- Never `SDL_free()` the pointer from `SDL_GetBasePath()`. SDL caches it and
  frees it on `SDL_Quit`, so freeing it is a double free at exit (this really
  crashed before).
- Tiles are BMPs converted to RGBA8888; a pixel is transparent when
  `(pixel & 0x000000FF) == 0` (the alpha byte), not by a colour key. See
  `render_tile_idx`. Many tiles ship as 24 bit BMPs with no alpha at all, so the
  entity sprites are keyed out at load (`key_out_black_background`), which clears
  only the black reachable from the tile border and leaves enclosed black alone.
  Do not apply that to level or HUD tiles: they are meant to be opaque.
- The framebuffer is always 200px tall but its width follows the screen aspect.
  Do not hardcode 320. Use `display_width()`, `display_columns()`,
  `display_center_offset()` / `display_right_offset()`. `display_sync()` runs
  once per frame before `display_lock()`. A level narrower than the viewport is
  centred by `game_view_x()` with black on the sides, and projectile range is
  kept at the original 320px on purpose, so a wider window changes nothing but
  what you can see.
- `display_compute_geometry()` pushes the picture down by half the difference
  between the two HUD bars, so the scene between them is what looks centered, but
  only as far as the window allows: a screen the picture exactly fills (1280x800
  on a Steam Deck) gets no nudge, otherwise the bottom rows, and the trophy
  banner on them, end up off the screen. `tests/test_display.c` checks both the
  "never past the bottom edge" and the "scene centered while there is room"
  invariants.
- The game draws into an offscreen `RGBA8888` buffer that `display_lock()`
  hands out, not straight into the texture. `display_present()` locks the
  texture itself and runs `filter_render()` while copying, which is what lets
  the `FILTERS` mode change the texture width and height mid-run. `display_unlock()` is a
  no-op; do not move drawing back onto the texture or the filter loses its
  source. The filter is a frame operation, applied once per presented frame,
  so it goes on the `display_present()` side of the two clocks, never inside
  the state machine.
- Monsters, plasma and the bullet are drawn blended: `render_tile_idx` XORs
  their colours over what is behind them. The XOR must keep the sprite's alpha
  byte, or it produces alpha 0 pixels that render black and eat the level
  wherever the sprite touches a tile.
- Jump is edge triggered in `dave.c` (`jump_pressed`, `key_up_prev`): it starts
  on a fresh press while grounded and a press in the air is dropped, never
  buffered for the landing.
- Up is split on purpose. The keyboard "up" both jumps and climbs (faithful to
  the original); a pad's up feeds `keys_state.climb_up`, which only climbs and
  flies, and only the pad's `A` jumps. Keep the two apart.
- Pad input is split between `gamepad_update()` (held buttons and axes, once per
  frame) and `gamepad_event()` (the one shot buttons). The intro starts on
  Enter/Space or any face button, and `Start` there raises `enter` as well as
  `escape` on purpose: on the title screen it means "go", in game it is the quit
  popup. That popup is answered with the pad's `A` (yes, `key_y`) and `B` (no,
  `key_n`); `X` shoots and `B` also toggles the jetpack, so do not reuse those
  buttons without checking what reads the flag. `keys_state.key_y`, `.key_n`,
  `.enter` and `.quit` are only read by the popups and the intro.
- `game_shutdown()` is the only exit path. Route new exits through it so the
  process really terminates instead of leaving a window-less process behind.
- Portability, since the Windows CI job is MSVC: `access()`/`chdir()` are POSIX,
  and `game.c` reaches them through `dd_access`/`dd_chdir` (`<io.h>`/`<direct.h>`
  plus the underscore names on Windows). Nothing else may include `unistd.h`.
- The Windows icon is compiled by `rc.exe` from a generated `.rc`, and it reads
  `\a` inside a string as an escape: the path CMake writes there needs its
  backslashes doubled, otherwise the build fails with `RC2135: file not found`
  on the `D:\a\...` runner workspace. See `CMakeLists.txt`.
- `SDL_MAIN_HANDLED` belongs only in `game.c`, the one file that includes
  `SDL3/SDL_main.h`; SDL3 does not pull that header in through `SDL.h`, so the
  define is inert anywhere else.
- SDL picks the renderer backend (`SDL_CreateRenderer(window, NULL)`), which puts
  the frame scaling on the GPU and keeps the software renderer as a fallback. Do
  not pin it back to `SDL_SOFTWARE_RENDERER`. The framebuffer is a streaming
  RGBA8888 texture: lock it once per frame and write rows with `g_pixels_pitch`
  as the stride, never with `display_width()`. `display_init()` turns vsync on
  (`SDL_SetRenderVSync`); a backend that refuses it only prints a line, the loop
  paces itself anyway.
- The logic and the frames are two different clocks, and mixing them up is the
  easiest way to change the game speed by accident. Both loops (`start_intro()`
  and `gameloop()`) ask `pacer_begin_frame()` how many 14 ms steps have come
  due and run the state machine that many times, while `display_sync()`,
  `display_lock()` / `display_unlock()` and `display_present()` stay one set per
  frame. So a state function draws twice in a frame that owed two steps, and
  work that belongs to the frame rather than to the tick does not go inside one.
  The 14 ms step is the game speed (every movement, animation, timer and monster
  tick is one step), so it is not a knob: changing it changes the whole game.
  Keep the single `get_keys()` per step too, and do not hoist it out to once per
  frame - the one shot flags (`jetpack`, `key_y`, `key_n`) are rebuilt by every
  call and the presses behind them come out of the event queue, so one J reaches
  exactly one step; polled once for two steps, one press would toggle twice.
- The frame budget comes from the display, not from a constant:
  `display_frame_period_ns()` reads the refresh rate of the screen the window is
  on, every frame, because the window can be dragged to another one. It aims a
  1/64 margin *under* one refresh on purpose. Aim over and the loop drifts past
  each blank a little more every frame until it drops one, a hitch every few
  seconds; aim under and it never sleeps at all and vsync alone decides when the
  frame goes out. A rate of 0 (some backends and virtual displays) or anything
  outside 24-360 Hz falls back to 60 Hz. A frame that owes no step is dropped
  whole, which is why `display_sync()` runs *after* the step count and not
  before: above ~71 fps the game presents only when the logic actually moved.
  `LOGIC_MAX_STEPS` caps the catch-up, so a dragged window or a suspend skips
  the time it missed instead of replaying it.
- `render_tile_idx` is the hot path (the visible level is ~1000 tiles of 64
  pixels each frame): it resolves the source and destination rows once per line,
  skips whole rows out of view and reads `surface->pitch`. Keep any change to
  that loop row based, and leave per-pixel work out of it.
- Which sprites are XOR blended is the `blended_sprites[]` list in `game.c`,
  turned into `g_blended[]` while the assets load. Add new blended sprites there
  or they will be painted opaque.
- The state structs and their tiles are allocated with `calloc()`: the drawing
  and collision code assumes every field starts at zero. Keep it that way
  instead of trusting `malloc`.
- The warp corridor between levels is authored for the original 320 pixel
  screen, so the warp state lays the view out against that width: it sets
  `view_columns` (which `game_view_x()` uses instead of the level's own
  `level_columns`) to 20 and paints the framebuffer outside the centred 320 pixel
  picture black. The walk itself ends at `DISPLAY_BASE_WIDTH - 20` in level
  coordinates, not at the viewport edge. Both used to follow the viewport, which
  made the corridor show its undrawn length and the intermission last as long as
  the corridor on a wide window.
- `get_keys()` writes `keys_state.enter` and `.quit` and never clears them, and
  only the intro reads `enter`. Do not use them as edge triggered inside the
  game loop.
- F10 is a development shortcut that jumps to the ending screen from any game
  state, so the last screen can be looked at without playing the ten levels. It
  is a one shot flag on `keys_state_t.congrats`, set by `get_keys()` and read at
  the top of `game_state_step()`; the ending itself then starts a fresh run on
  level 5, as it does after the last level. Not F11: macOS takes that key for
  "Show Desktop", so it never reaches the window.
- The intro is authored as a 320 pixel wide picture: `draw_tile_centered()` puts
  the tiles in the middle of the framebuffer (the maze is 80..240 wide on a 320
  pixel one) and the three text lines go through `draw_text_line_centered()`,
  which assumes 8 pixel wide font tiles. Add text through that helper rather
  than hand tuning an x offset; the title screen has no F1 help screen, that line
  was removed. `draw_char()` finds a glyph by looking it up in `font_chars[]` (A-Z,
  0-9, then `space , . ( ) ! ? - ' :`) and adds the offset to the 500 or 600 tile
  block, so that order has to keep matching the font tiles in `res/font`.
- The ending screen is a box of 16 pixel grail tiles sized around its text and
  centered on the framebuffer, with black around it, not a frame spanning the
  window: the widest line and the block of lines, each with 4 pixels of air,
  rounded up to whole tiles and framed by one more on every side. For the text the
  original ships that comes to 20x10 tiles, 320x160, which is why it lands exactly
  on the width of the original screen and keeps that size on a wider one. The
  lines are therefore centered against the box by hand, not with
  `draw_text_line_centered()`. `draw_grail_frame()` walks the box clockwise (top,
  right, bottom, left) and `draw_grail()` draws each grail one animation frame
  further along than the one before it, so the five frames travel around the frame
  as a wave instead of every grail glowing in step; the four corners are drawn
  once, by the top and the bottom row. The box is centered on the **scene**, not
  on the framebuffer: `display_compute_geometry()` shifts the picture down by half
  the difference between the two HUD bars so the scene looks centered on the
  screen, and this is the one screen with no HUD on it, so a box centered on the
  framebuffer sits 9 pixels low.

## Conventions

- A new `.c`/`.h` must be added to both `Makefile` and `CMakeLists.txt` (and to
  `tests/Makefile` if a test uses it). The two builds are separate on purpose.
- Keep both builds warning-free. `make` uses `-std=c99 -Wall`; the CMake build
  compiles the same sources with MSVC. `-Wmissing-prototypes` and
  `-Wstrict-prototypes` are clean, and every function that no header declares is
  `static`, so helpers stay file local.
- The tile categories and sprite indices are a single `enum` in `tile.h`, not
  `static const int`: a header full of those gives every translation unit its own
  copy of all 143.
- `assets/icon.{icns,ico,png}` are generated from `res/tiles/tile55.bmp`; do not
  hand-edit them, change the tile and rerun `make icon`.
- CI (`.github/workflows/release.yml`) only runs on `v*` tags and manual
  dispatch, not on pushes to `master`. A tag builds the three packages and
  publishes them as a release; the macOS bundle version comes from the tag name,
  so a new build is a new tag, not a moved one. The actions are on Node 24
  runtimes (`checkout` v7, `upload-artifact` v7, `download-artifact` v8,
  `action-gh-release` v3).
- The Windows job sets up the MSVC environment with its own `pwsh` step instead
  of `ilammy/msvc-dev-cmd`, a Node 20 action with no Node 24 release that warned
  on every run. The step enters the developer shell of the Visual Studio that
  ships on the runner and appends the variables it changes to `$GITHUB_ENV`, so
  `cmake`, `ninja` and `cl` find each other in the later steps. Keep it that way
  rather than bringing back an action on an old runtime.
- Build outputs are gitignored: `ddave`, `deadly-dave`, `Deadly Dave.app/`,
  `build/`, `dist/`, the test binaries, `*.dSYM/`, `.DS_Store`.
