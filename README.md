## Deadly Dave

An open source implementation of *Dangerous Dave*, a 1988 DOS game by John Romero.


Main focus:
 1. Replicate look & feel.
 2. Run on modern systems.

### Controls

| Action        | Keys                        |
| ------------- | --------------------------- |
| Move          | `A` / `D`, or left / right  |
| Jump          | `W`, or up                  |
| Climb down    | `S`, or down                |
| Shoot         | `Space`, or left Ctrl       |
| Jetpack       | `J`                         |

### Building

The game needs **SDL 3.4.16** or newer and nothing else.

    make                 # uses pkg-config to find SDL3
    make app             # macOS: wraps the game in "Deadly Dave.app"
    make icon            # regenerates assets/icon.* (needs python3)
    cd tests && make     # unit tests

The CMake build fetches and links SDL3 statically instead. On macOS the `app`
target produces a bundle with the resources inside it, so the game can be
double-clicked and opens without a terminal.

Every build uses the same icon, the Dave sprite from `res/tiles/tile55.bmp`,
turned into an `.icns` for the macOS bundle, an `.ico` embedded in the Windows
executable and a `.png` for the Linux window.

### Releases

Pushing a tag builds portable packages for the three systems and attaches them
to a GitHub release:

    git tag v1.0.0
    git push origin v1.0.0

The workflow can also be run by hand from the Actions tab, which builds the
packages without publishing a release. Each package is a self-contained folder
(the executable plus the `res` directory it reads at runtime) and needs nothing
installed:

| System  | Package                              |
| ------- | ------------------------------------ |
| Windows | `deadly-dave-windows-x86_64.zip`     |
| macOS   | `deadly-dave-macos-universal.zip`    |
| Linux   | `deadly-dave-linux-x86_64.tar.gz`    |

SDL3 is linked statically everywhere, the C runtime is static on Windows and the
macOS build is a universal binary. On Linux the only things used from the system
are the C library and the video/audio libraries that every desktop already ships
(X11 or Wayland, ALSA or PulseAudio).

### Display and aspect-ratio

The game draws into a low resolution framebuffer that is always 200 pixels tall,
like the original 320x200 DOS screen, and is scaled up to the window keeping
square pixels. The width follows the shape of the display, so a wide screen
shows more of the level instead of a stretched picture:

| Display           | Framebuffer | Notes                                    |
| ----------------- | ----------- | ---------------------------------------- |
| 16:10 (1280x800)  | 320x200     | native shape, fills the screen (4x)      |
| 16:10 (1920x1200) | 320x200     | native shape, fills the screen (6x)      |
| 16:9  (1920x1080) | 384x200     | 4 extra tile columns, 5x, thin bars      |
| 4:3   (1024x768)  | 336x200     | close to the native shape, bars top/down |

Vertically the picture is positioned so that the scene, the part between the two
HUD bars, ends up centered on the screen. The framebuffer is not centered
blindly because the bottom bar is taller than the top one, which would leave the
scene sitting a little high.

By default the picture is scaled by a whole number, which keeps every pixel the
same size, and any leftover room becomes a black border. `F5` switches to a
scaling that fills the whole screen instead, at the cost of unevenly sized
pixels. The window can also be resized freely while playing.

The game starts full screen. `Cmd`+`Enter` (macOS) or `Alt`+`Enter` (elsewhere)
switches between full screen and windowed, and `-w` starts windowed instead.

### Acknowledgments
* MaiZure    - for starting the 'lmdave' project this is based on.
* Malvineous - for allowing the unpacking of original resources from dave.exe.



