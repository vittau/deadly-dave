#!/usr/bin/env python3
"""Extracts the original's font into the port's font tiles.

The original keeps three copies of its font in UNPACKED_DAVE.EXE, one per video
mode, 128 ASCII cells of 8x8 pixels each (the lower case ones repeat the upper
case). The glyph is dark on a light cell: it prints black text on the white
popups as is, and white text on black by XORing every pixel (the colour mask at
0x1392 is 0xFFFF), which in VGA also turns the grey anti-aliasing of the glyph
into the orange and yellow its XORed palette entries hold.

- VGA at VGA_FONT, 64 bytes a cell, one byte a pixel, on the game's own palette
  at VGA_PALETTE (256 entries of 6 bit RGB; it is not the BIOS default).
- EGA at EGA_FONT, stored as the four bit planes the game copies to video
  memory (EGA_PLANE bytes apart), 8 bytes a cell in each, one bit a pixel.
- CGA at CGA_FONT, 16 bytes a cell, two bits a pixel on the bright cyan,
  magenta and white palette.

Every glyph sits in rows 1 to 6 of its cell, which are the port's 8x6 font
tiles. For each character of the game's font_chars[] (game.c) this writes the
white tile, tile(500+index).bmp, with the XORed cell's black made transparent,
and the black tile, tile(600+index).bmp, opaque like the original's cell: the
VGA pair into res/tiles, with their res/font and res/font/black copies, and the
EGA and CGA pairs into res/ega-tiles and res/cga-tiles, which the game loads
for those modes before it falls back to res/tiles.

usage: extract-font.py [original directory] [res directory]
"""

import os
import struct
import sys

VGA_FONT = 0x20FC0
VGA_PALETTE = 0x26B0A
EGA_FONT = 0x1EB40
EGA_PLANE = 0x8E0
CGA_FONT = 0x1D880

EGA_PALETTE = [
    (0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170),
    (170, 0, 0), (170, 0, 170), (170, 85, 0), (170, 170, 170),
    (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255),
    (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255),
]
CGA_PALETTE = [(0, 0, 0), (85, 255, 255), (255, 85, 255), (255, 255, 255)]

# game.c's font_chars[], in order, with the names of their res/font files.
FONT = [(c, c.lower()) for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"] + [
    (" ", "space"), (",", "comma"), (".", "dot"), ("(", "pthesis_l"),
    (")", "pthesis_r"), ("!", "exclamation"), ("?", "question"), ("-", "dash"),
    ("'", "apostrophe"), (":", "colon"), ("*", "asterisk"),
]

FIRST_ROW = 1
ROWS = 6
WIDTH = 8


def bmp(header, rows):
    """An 8x6 tile with the BMP header of the tiles already there (32 bit, alpha last)."""
    pixels = bytearray()
    for row in reversed(rows):
        for r, g, b, a in row:
            pixels += bytes((b, g, r, a))
    return header + bytes(pixels)


def cells(d, mode):
    """Returns cell(character code, white) -> rows of RGB for one video mode."""
    if mode == "vga":
        palette = [tuple(v * 255 // 63 for v in d[VGA_PALETTE + i * 3:VGA_PALETTE + i * 3 + 3])
                   for i in range(256)]

        def cell(code, white):
            base = VGA_FONT + code * 64
            return [[palette[d[base + y * 8 + x] ^ (0xFF if white else 0)] for x in range(WIDTH)]
                    for y in range(8)]
    elif mode == "ega":
        def cell(code, white):
            rows = []
            for y in range(8):
                row = []
                for x in range(WIDTH):
                    colour = 0
                    for plane in range(4):
                        bit = (d[EGA_FONT + plane * EGA_PLANE + code * 8 + y] >> (7 - x)) & 1
                        colour |= bit << plane
                    row.append(EGA_PALETTE[colour ^ (0xF if white else 0)])
                rows.append(row)
            return rows
    else:
        def cell(code, white):
            rows = []
            for y in range(8):
                word = (d[CGA_FONT + code * 16 + y * 2] << 8) | d[CGA_FONT + code * 16 + y * 2 + 1]
                rows.append([CGA_PALETTE[((word >> (14 - 2 * x)) & 3) ^ (3 if white else 0)]
                             for x in range(WIDTH)])
            return rows
    return cell


def tile(cell, code, white):
    rows = cell(code, white)[FIRST_ROW:FIRST_ROW + ROWS]
    # White text is drawn on black screens: the XORed cell's black is left out.
    return [[pixel + (0 if white and pixel == (0, 0, 0) else 255,) for pixel in row] for row in rows]


def write(path, data):
    with open(path, "wb") as f:
        f.write(data)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    original = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "original")
    res = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, "..", "res")
    with open(os.path.join(original, "UNPACKED_DAVE.EXE"), "rb") as f:
        d = f.read()
    # Every font tile shares the header of the first, which the port has always had.
    with open(os.path.join(res, "tiles", "tile500.bmp"), "rb") as f:
        first = f.read()
    header = first[:struct.unpack_from("<I", first, 10)[0]]
    if struct.unpack_from("<iiHH", first, 18) != (WIDTH, ROWS, 1, 32):
        raise SystemExit("res/tiles/tile500.bmp: expected an 8x6 32 bit BMP")

    for mode, folder in (("vga", "tiles"), ("ega", "ega-tiles"), ("cga", "cga-tiles")):
        cell = cells(d, mode)
        for index, (char, name) in enumerate(FONT):
            white = bmp(header, tile(cell, ord(char), True))
            black = bmp(header, tile(cell, ord(char), False))
            write(os.path.join(res, folder, "tile%d.bmp" % (500 + index)), white)
            write(os.path.join(res, folder, "tile%d.bmp" % (600 + index)), black)
            if mode == "vga":
                write(os.path.join(res, "font", name + ".bmp"), white)
                write(os.path.join(res, "font", "black", name + ".bmp"), black)
        print("wrote %d glyphs to %s" % (len(FONT), os.path.join(res, folder)))


if __name__ == "__main__":
    main()
