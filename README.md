# Deadly Dave

![Level 2, whole, on a wide viewport](res/screenshots/screen4.png)

An open source reimplementation of *Dangerous Dave*, a 1988 DOS game by John
Romero, that runs natively on the machines we actually use now. The same
sprites, the same levels, the same sound effects and the same feel, without an
emulator and without anything to install.

> **Disclaimer:** the improvements in this fork were made possible by AI coding
> agents, Claude Code and OpenCode (running DeepSeek models), working on top of
> the sources below. Read the diff with that in mind.

## Why this one

- **Native on Windows, macOS and Linux.** Every download is self-contained:
  SDL3 is built in, Windows needs no runtime, on macOS it is a regular app you
  double-click with no terminal in sight, and on Linux the only things it uses
  are the libraries every desktop already ships.
- **Any display, any aspect ratio.** The viewport grows with the screen instead
  of stretching the picture, so a wide screen shows more of the level rather
  than a flattened one. A level narrower than the viewport is centred, with
  black on the sides, and everything is scaled by whole numbers so the pixels
  stay square and sharp. `F5` switches to a mode that fills the whole screen,
  and the window can be resized to whatever you like. The screenshot above is
  level 2 in full, on a screen wide enough to hold it.
- **Keyboard or controller, together.** The keyboard keeps the feel of the
  original; a controller works out of the box and can be plugged in while
  playing.
- **Perfect on a Steam Deck.** On SteamOS it needs nothing installed: the
  screen is 1280x800, exactly 4x the original 320x200, so the picture is pixel
  perfect with no stretch and nothing is cut off. The built-in controls are
  picked up as a gamepad (the title screen and the quit popup included), so it
  plays the same in Gaming Mode, added as a non-Steam game, as on a desktop.
- **Starts full screen.** `Cmd`+`Enter` on macOS, `Alt`+`Enter` anywhere else,
  switches between full screen and windowed. `-w` starts windowed instead.
- **The whole game**, ten levels and the secret one, with the intro and the
  original sound effects decoded from the game's own data.

![Level 6](res/screenshots/screen3.png)

## Controls

| Action        | Keyboard                    | Controller                       |
| ------------- | --------------------------- | -------------------------------- |
| Move          | `A` / `D`, or left / right  | left stick or D-pad left / right |
| Jump          | `W`, or up                  | `A`                              |
| Climb         | `W` / `S`, or up / down     | stick or D-pad up / down         |
| Shoot         | `Space`, or left Ctrl       | `X`, or right shoulder           |
| Jetpack       | `J`                         | `B`                              |
| Quit popup    | `Escape`                    | `Start`                          |
| In the popup  | `Y` / `N`                   | `A` quits, `B` goes back         |

On the keyboard up jumps, faithful to the original. On a pad it only climbs and
flies, because jumping whenever a stick went up would be miserable, so there
`A` is the jump. On the title screen any of the pad's face buttons, or `Start`,
starts the game.

## Getting it

Download the package for your system from the
[releases page](https://github.com/vittau/deadly-dave/releases). Each one is a
folder with the game inside and nothing to install:

| System  | Package                                           |
| ------- | ------------------------------------------------- |
| Windows | `deadly-dave-windows-x86_64.zip`                  |
| macOS   | `deadly-dave-macos-universal.zip` (Intel + Apple) |
| Linux   | `deadly-dave-linux-x86_64.tar.gz`                 |

On macOS, drag `Deadly Dave.app` to Applications. On Windows, unzip and run
`deadly-dave.exe`. On Linux, unpack and run `./deadly-dave`.

## Building it

The game needs **SDL 3.4.16** or newer and a C99 compiler.

    make                 # uses pkg-config to find SDL3
    make app             # macOS: wraps the game in "Deadly Dave.app"
    cd tests && make     # unit tests

On macOS `make` also produces `Deadly Dave.app`: double-click that one, not the
`ddave` binary, since a bare Unix executable always opens a Terminal when
double-clicked.

The CMake build fetches and links SDL3 statically instead, which is what the
releases use. Pushing a tag (`git tag v1.0.0 && git push origin v1.0.0`) builds
the three packages and publishes them.

## Acknowledgments

* skoperst    - for [deadly-dave](https://github.com/skoperst/deadly-dave), the
                port this one is forked from.
* MaiZure     - for starting the 'lmdave' project this is based on.
* Malvineous  - for allowing the unpacking of original resources from dave.exe.
