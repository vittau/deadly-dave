## Deadly Dave

An open source implementation of *Dangerous Dave*, a 1988 DOS game by John Romero.


Main focus:
 1. Replicate look & feel.
 2. Run on modern systems.

### Building

The game needs **SDL 3.4.16** or newer and nothing else.

    make                 # uses pkg-config to find SDL3
    cd tests && make     # unit tests

The CMake build fetches and links SDL3 statically instead.

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



