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
  `display.c`, and update its expected widths if the geometry changes.
- `test_display` and `test_invfreq` (which writes `out.raw` into `tests/`) run
  headless. `test_monster` opens a window and needs a real display, so CI runs
  none of them.
- `test_monster` does not link `game.c` or `display.c`: it carries its own copies
  of the drawing, input and asset loading code. It exercises `tile.c`,
  `plasma.c` and `monster.c` for real, but a change in `game.c` will not show up
  there.
- The third argument of `invfreq_decode_soundfx()` is samples *per symbol*, not a
  buffer size; every tune has its own value in `soundfx.c` (the jumping sound is
  345). A too large value writes far past the buffer and the process dies with
  SIGBUS, which is what `test_invfreq` used to do.

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
  as the stride, never with `display_width()`.
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
- `get_keys()` writes `keys_state.enter` and `.quit` and never clears them, and
  only the intro reads `enter`. Do not use them as edge triggered inside the
  game loop.
- The intro is authored as a 320 pixel wide picture: `draw_tile_centered()` puts
  the tiles in the middle of the framebuffer (the maze is 80..240 wide on a 320
  pixel one) and the three text lines go through `draw_text_line_centered()`,
  which assumes 8 pixel wide font tiles. Add text through that helper rather
  than hand tuning an x offset; the title screen has no F1 help screen, that line
  was removed. `draw_char()` finds a glyph by looking it up in `font_chars[]` (A-Z,
  0-9, then `space , . ( ) ! ?`) and adds the offset to the 500 or 600 tile
  block, so that order has to keep matching the font tiles in `res/font`.

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
- The Windows job uses `ilammy/msvc-dev-cmd@v1`, which still targets Node 20:
  every run carries a deprecation warning because the action has no Node 24
  release (as of September 2026). Not something to fix on our side.
- Build outputs are gitignored: `ddave`, `deadly-dave`, `Deadly Dave.app/`,
  `build/`, `dist/`, the test binaries, `*.dSYM/`, `.DS_Store`.
