#!/usr/bin/env python3
"""Extracts the EGA and CGA artwork of the original game.

The original draws in VGA, EGA or CGA. Its VGA tiles are res/tiles; this script
writes the other two as res/ega-tiles and res/cga-tiles. Every tile is saved as
tileN.bmp under the number of its VGA twin, with the same size and the same BMP
header, so the game can load any of the sets without anything else changing:
the drawing, the collision boxes and the HUD layout all go by those numbers
and sizes.

Both sources are a UINT32LE tile count, a UINT32LE offset per tile, then the
tiles. The first 53 are 16x16 and headerless; the rest start with UINT16LE
width-1 and height-1.

- EGA is original/EGADAVE.DAV. The pixels are 16 colour EGA, row by row, and
  every row holds its four bit planes one after the other (intensity, red,
  green, blue), each padded to a whole byte.
- CGA is inside original/UNPACKED_DAVE.EXE, at CGA_OFFSET, compressed the way
  the VGA set next to it is: a UINT32LE unpacked length, then runs where a
  byte with the top bit set copies (byte & 0x7F) + 1 bytes and any other
  repeats the next byte byte + 3 times. The pixels are 2 bits each, four to a
  byte, on the bright cyan, magenta and white palette. Some sprites carry
  bytes past their last row, which are not drawing and are skipped.

Both sets have more tiles than the VGA one because the sprites are pre-shifted
so the code could draw them on a byte boundary: four copies in EGA (0, 2, 4, 6
pixels), two in CGA. Only the unshifted copy is kept. The copies are a little
larger than the VGA sprites (the padding the shifts need, and sometimes a row
more), so each animation is placed at the one offset where it best covers its
VGA twin and cut to the VGA size; the script reports every tile it moved and
any drawn pixel that falls off.

The VGA sprites that carry an alpha channel get theirs rebuilt the same way it
was built for VGA: Dave's frames from the matching mask tile (black in the
mask is the sprite), the bullet from its non-black pixels. Tiles the original
never had (the font, the popup box pieces and a few port additions) are not
written: the game falls back to res/tiles for those.

usage: extract-tiles.py [original directory] [res directory]
"""

import os
import struct
import sys

EGA_PALETTE = [
    (0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170),
    (170, 0, 0), (170, 0, 170), (170, 85, 0), (170, 170, 170),
    (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255),
    (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255),
]

# CGA palette 1, high intensity: black, cyan, magenta, white.
CGA_PALETTE = [(0, 0, 0), (85, 255, 255), (255, 85, 255), (255, 255, 255)]
CGA_OFFSET = 0xC620
CGA_TILES = 239

VGA_TILES = 158
SHIFTED_FIRST = 53   # first VGA sprite that EGA and CGA store pre-shifted
SHIFTED_LAST = 132   # last one
JETPACK_UNIT = 142   # the jetpack bar unit, the one later tile shifted too

# Dave's frames and the masks their alpha comes from, as VGA numbers.
MASKS = {}
for frames, masks in ((range(53, 60), range(60, 67)), (range(67, 69), range(69, 71)),
                      (range(71, 74), range(74, 77)), (range(77, 83), range(83, 89))):
    MASKS.update(zip(frames, masks))
BULLETS = (127, 128)
# The level tiles that are a pickup drawn on black (the jetpack, the trophy's
# frames, the gun, the gems, the crown, the ring, the scepter). Only these are
# moved inside their 16x16 cell to sit where the VGA ones do; the rest of the
# level tiles fill the cell and stay put.
PICKUPS = ([4], [10, 11, 12, 13, 14], [20], [47], [48], [49], [50], [51], [52])
# How far a sprite may move. The HUD words need the most: "LEVEL" sits 14
# pixels into its VGA tile and at the very left of its EGA and CGA ones.
SEARCH_X = 16
SEARCH_Y = 4


def source_index(vga, shifts):
    """The unshifted tile of a set with `shifts` copies per sprite that draws a VGA tile."""
    if vga < SHIFTED_FIRST:
        return vga
    if vga <= SHIFTED_LAST:
        return SHIFTED_FIRST + (vga - SHIFTED_FIRST) * shifts
    # After the shifted sprites the sets line up again, except for the
    # jetpack bar unit, which is also kept in every shift.
    index = SHIFTED_FIRST + (SHIFTED_LAST - SHIFTED_FIRST + 1) * shifts + (vga - SHIFTED_LAST - 1)
    if vga > JETPACK_UNIT:
        index += shifts - 1
    return index


def split_tiles(d):
    """Yields (index, width, height, pixel bytes) for every tile of a set."""
    count = struct.unpack_from("<I", d, 0)[0]
    offsets = list(struct.unpack_from("<%dI" % count, d, 4)) + [len(d)]
    for i in range(count):
        start, end = offsets[i], offsets[i + 1]
        if i < SHIFTED_FIRST:
            yield i, 16, 16, d[start:end]
        else:
            w, h = struct.unpack_from("<HH", d, start)
            yield i, w + 1, h + 1, d[start + 4:end]


def load_ega(path):
    with open(path, "rb") as f:
        d = f.read()
    tiles = []
    for i, w, h, data in split_tiles(d):
        per_plane = (w + 7) // 8
        if len(data) != per_plane * 4 * h:
            raise SystemExit("EGA tile %d: %d bytes for %dx%d" % (i, len(data), w, h))
        rows = []
        for y in range(h):
            row = []
            for x in range(w):
                colour = 0
                for plane in range(4):
                    byte = data[(y * 4 + plane) * per_plane + x // 8]
                    if byte & (0x80 >> (x % 8)):
                        colour |= 8 >> plane
                row.append(EGA_PALETTE[colour])
            rows.append(row)
        tiles.append(rows)
    return tiles


def unpack_rle(d, pos):
    length = struct.unpack_from("<I", d, pos)[0]
    out = bytearray()
    pos += 4
    while len(out) < length:
        run = d[pos]
        pos += 1
        if run & 0x80:
            out += d[pos:pos + (run & 0x7F) + 1]
            pos += (run & 0x7F) + 1
        else:
            out += bytes([d[pos]]) * (run + 3)
            pos += 1
    return bytes(out[:length])


def load_cga(path):
    with open(path, "rb") as f:
        d = unpack_rle(f.read(), CGA_OFFSET)
    if struct.unpack_from("<I", d, 0)[0] != CGA_TILES:
        raise SystemExit("no CGA tiles at 0x%X in %s" % (CGA_OFFSET, path))
    tiles = []
    for i, w, h, data in split_tiles(d):
        per_row = (w + 3) // 4
        if len(data) < per_row * h:
            raise SystemExit("CGA tile %d: %d bytes for %dx%d" % (i, len(data), w, h))
        rows = []
        for y in range(h):
            rows.append([CGA_PALETTE[(data[y * per_row + x // 4] >> (6 - 2 * (x % 4))) & 3]
                         for x in range(w)])
        tiles.append(rows)
    return tiles


def load_bmp(path):
    """Returns (header bytes, width, height, bytes per pixel, rows of RGBA)."""
    with open(path, "rb") as f:
        d = f.read()
    data_off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0] // 8
    if bpp not in (3, 4) or h <= 0:
        raise SystemExit("%s: unexpected BMP layout" % path)
    stride = (w * bpp + 3) & ~3
    rows = []
    for y in range(h):
        base = data_off + (h - 1 - y) * stride
        row = []
        for x in range(w):
            b, g, r = d[base + x * bpp:base + x * bpp + 3]
            a = d[base + x * bpp + 3] if bpp == 4 else 255
            row.append((r, g, b, a))
        rows.append(row)
    return d[:data_off], w, h, bpp, rows


def save_bmp(path, header, w, h, bpp, rows):
    stride = (w * bpp + 3) & ~3
    out = bytearray(header)
    for y in range(h - 1, -1, -1):
        line = bytearray()
        for r, g, b, a in rows[y]:
            line += bytes((b, g, r, a)) if bpp == 4 else bytes((b, g, r))
        out += line + bytes(stride - len(line))
    with open(path, "wb") as f:
        f.write(out)


def drawn(pixel):
    return pixel[:3] != (0, 0, 0) and (len(pixel) < 4 or pixel[3] != 0)


def strip_grab_line(tile):
    """
    Some sprites (the HUD words, the door banner, one monster) end in a row of
    a single colour across their whole width: the edge of the sheet they were
    grabbed from, not part of the drawing. It is dropped.
    """
    last = tile[-1]
    if last[0] != (0, 0, 0) and len(set(last)) == 1:
        return tile[:-1]
    return tile


def best_offset(pairs, w, h):
    """
    The one (dx, dy) that places every tile of a group over its VGA twin
    best: losing no drawn pixel past the top or left edge first, then growing
    the tile past the right or bottom one as little as possible (see place()),
    then covering the most, then moving the least. A whole animation shares
    it, so the frames do not jump about.
    """
    masks = [{(x, y) for y in range(h) for x in range(w) if drawn(vga[y][x])} for _, vga in pairs]
    pixels = [[(x, y) for y, row in enumerate(ega) for x, pixel in enumerate(row) if drawn(pixel)]
              for ega, _ in pairs]
    best = None
    # The two sets were drawn for the same positions, so the answer is near 0,0.
    for dy in range(-SEARCH_Y, SEARCH_Y + 1):
        for dx in range(-SEARCH_X, SEARCH_X + 1):
            inside = lost = grown = 0
            for mask, points in zip(masks, pixels):
                for x, y in points:
                    vx, vy = x + dx, y + dy
                    if vx < 0 or vy < 0:
                        lost += 1
                    elif vx >= w or vy >= h:
                        grown += 1
                    else:
                        inside += (vx, vy) in mask
            key = (-lost, -grown, inside, -(abs(dx) + abs(dy)))
            if best is None or key > best[0]:
                best = (key, dx, dy)
    return best[1], best[2]


def place(ega, dx, dy, w, h):
    """
    Puts the tile at dx, dy on a black canvas the size of the VGA tile.
    Drawing that still runs past the right or bottom edge grows the canvas
    rather than being cut, since that keeps the top left corner, which is all
    the game positions a sprite by. Returns the rows and the pixels lost.
    """
    points = [(x + dx, y + dy) for y, row in enumerate(ega) for x, pixel in enumerate(row) if drawn(pixel)]
    w = max([w] + [x + 1 for x, _ in points])
    h = max([h] + [y + 1 for _, y in points])
    rows = [[(0, 0, 0)] * w for _ in range(h)]
    lost = 0
    for y, row in enumerate(ega):
        for x, pixel in enumerate(row):
            if 0 <= x + dx < w and 0 <= y + dy < h:
                rows[y + dy][x + dx] = pixel
            elif drawn(pixel):
                lost += 1
    return rows, lost


def resize_header(header, w, h, bpp):
    header = bytearray(header)
    stride = (w * bpp + 3) & ~3
    struct.pack_into("<I", header, 2, len(header) + stride * h)
    struct.pack_into("<ii", header, 18, w, h)
    struct.pack_into("<I", header, 34, stride * h)
    return bytes(header)


def groups():
    """
    VGA tiles that move as one: every frame of an animation, which the VGA set
    keeps next to each other at one size. Dave's masks are left out, they go
    wherever their frames go.
    """
    sizes = {}
    for vga in range(VGA_TILES):
        with open(os.path.join(RES, "tiles", "tile%d.bmp" % vga), "rb") as f:
            sizes[vga] = struct.unpack_from("<ii", f.read(26), 18)
    result = []
    for vga in range(VGA_TILES):
        if vga in MASKS.values():
            continue
        last = result[-1] if result else None
        if (last and vga >= SHIFTED_FIRST and last[-1] >= SHIFTED_FIRST and
                sizes[last[-1]] == sizes[vga] and last[-1] == vga - 1):
            last.append(vga)
        else:
            result.append([vga])
    return result


def extract(name, tiles, shifts, vga):
    out_dir = os.path.join(RES, name + "-tiles")
    os.makedirs(out_dir, exist_ok=True)
    # Level tiles are whole 16x16 cells; a plain last row (water) is drawing there.
    tiles = [tile if i < SHIFTED_FIRST else strip_grab_line(tile) for i, tile in enumerate(tiles)]

    offsets = dict.fromkeys(range(SHIFTED_FIRST), (0, 0))
    for group in [g for g in groups() if g[0] >= SHIFTED_FIRST] + list(PICKUPS):
        _, w, h, _, _ = vga[group[0]]
        pairs = [(tiles[source_index(i, shifts)], vga[i][4]) for i in group]
        offsets.update(dict.fromkeys(group, best_offset(pairs, w, h)))
    for frame, mask in MASKS.items():
        offsets[mask] = offsets[frame]

    placed = {}
    for i in range(VGA_TILES):
        header, w, h, bpp, _ = vga[i]
        tile = tiles[source_index(i, shifts)]
        dx, dy = offsets[i]
        rows, lost = place(tile, dx, dy, w, h)
        if i in MASKS.values():
            # A mask only ever becomes its frame's alpha, which is the VGA size.
            rows, lost = [row[:w] for row in rows[:h]], 0
        new_w, new_h = len(rows[0]), len(rows)
        if (dx, dy) != (0, 0) or (new_w, new_h) != (w, h) or lost:
            print("%s tile%d: %dx%d at %+d,%+d, %dx%d%s%s" % (
                name, i, len(tile[0]), len(tile), dx, dy, new_w, new_h,
                " (VGA is %dx%d)" % (w, h) if (new_w, new_h) != (w, h) else "",
                ", %d drawn pixels lost" % lost if lost else ""))
        placed[i] = (resize_header(header, new_w, new_h, bpp), new_w, new_h, bpp, rows)

    for i, (header, w, h, bpp, rows) in placed.items():
        if bpp == 4:
            if i in MASKS:
                mask = placed[MASKS[i]][4]
                alpha = [[255 if mask[y][x] == (0, 0, 0) else 0 for x in range(w)] for y in range(h)]
            elif i in BULLETS:
                alpha = [[255 if rows[y][x] != (0, 0, 0) else 0 for x in range(w)] for y in range(h)]
            else:
                alpha = [[255] * w for _ in range(h)]
            rgba = [[rows[y][x] + (alpha[y][x],) for x in range(w)] for y in range(h)]
        else:
            rgba = [[pixel + (255,) for pixel in row] for row in rows]
        save_bmp(os.path.join(out_dir, "tile%d.bmp" % i), header, w, h, bpp, rgba)

    print("wrote %d tiles to %s" % (len(placed), out_dir))


def main():
    global RES
    here = os.path.dirname(os.path.abspath(__file__))
    original = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "original")
    RES = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, "..", "res")

    vga = {i: load_bmp(os.path.join(RES, "tiles", "tile%d.bmp" % i)) for i in range(VGA_TILES)}
    extract("ega", load_ega(os.path.join(original, "EGADAVE.DAV")), 4, vga)
    extract("cga", load_cga(os.path.join(original, "UNPACKED_DAVE.EXE")), 2, vga)


if __name__ == "__main__":
    main()
