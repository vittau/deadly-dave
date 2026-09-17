# AGENTS.md

Reimplementation of *Dangerous Dave* in C99 + SDL3: one executable that reads
`res/` at runtime. No package manager, no test framework, no generated sources
except the icons.

## Build and run

- `make` builds `ddave` against the system SDL3 (`pkg-config sdl3`). Fast, use it
  while iterating. It compiles with `-std=c99 -Wall` and is warning-free; there
  is no linter, formatter or typecheck besides that. On macOS it also builds
  `Deadly Dave.app`; a bare binary is opened by Terminal when double-clicked.
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
- `test_display` and `test_invfreq` (which writes `out.raw`) run headless.
  `test_monster` opens a window and needs a real display, so CI runs none.

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

## Conventions

- A new `.c`/`.h` must be added to both `Makefile` and `CMakeLists.txt` (and to
  `tests/Makefile` if a test uses it). The two builds are separate on purpose.
- `assets/icon.{icns,ico,png}` are generated from `res/tiles/tile55.bmp`; do not
  hand-edit them, change the tile and rerun `make icon`.
- CI (`.github/workflows/release.yml`) only runs on `v*` tags and manual
  dispatch, not on pushes to `master`.
- Build outputs are gitignored: `ddave`, `deadly-dave`, `Deadly Dave.app/`,
  `build/`, `dist/`, the test binaries, `*.dSYM/`, `.DS_Store`.
