# AGENTS.md

Reimplementation of *Dangerous Dave* in C99 + SDL3: one executable that reads
`res/` at runtime. No package manager, no test framework, no generated sources
except the icons.

## Build and run

- `make` builds `ddave` against the system SDL3 (`pkg-config sdl3`). Fast, use it
  while iterating.
- CMake (`cmake -S . -B build -G Ninja && cmake --build build`) fetches and
  statically links SDL 3.4.16 and writes the binary to the repo root as
  `deadly-dave`. This is what CI and the releases use; configuring takes ~40s
  because it clones SDL.
- The game `chdir`s to `SDL_GetBasePath()` and reads `res/` from there. Run it
  from the repo root (or from `Deadly Dave.app`); from anywhere else it prints
  "Could not find the game assets" and exits `-6`.
- `make app` (macOS) builds the bundle, `make icon` regenerates `assets/icon.*`.

## Tests

- `cd tests && make`, then `./tests/test_display`. It is pure (no window, no
  SDL init) and always runnable.
- `test_monster` opens a window and `test_invfreq` writes `out.raw`; both need a
  real display and are not run in CI.

## Gotchas

- SDL3, not SDL2: `SDL_Init` returns true on success, event types are
  `SDL_EVENT_*`, and the `SDL_Free*` names are now `SDL_DestroySurface` /
  `SDL_DestroyTexture`.
- Never `SDL_free()` the pointer from `SDL_GetBasePath()`. SDL caches it and
  frees it on `SDL_Quit`, so freeing it is a double free at exit (this really
  crashed before).
- Tiles are 32 bit BMPs converted to RGBA8888; a pixel is transparent when
  `(pixel & 0x000000FF) == 0` (the alpha byte). See `render_tile_idx`.
- The framebuffer is always 200px tall but its width follows the screen aspect.
  Do not hardcode 320. Use `display_width()`, `display_columns()`,
  `display_center_offset()` / `display_right_offset()`. `display_sync()` runs
  once per frame before `display_lock()`.
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
