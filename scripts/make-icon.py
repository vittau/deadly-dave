#!/usr/bin/env python3
"""Builds the app icons for every platform from a game tile.

It writes assets/icon.icns (macOS bundle), assets/icon.ico (embedded in the
Windows executable) and assets/icon.png (Linux window icon).

The sprite keeps its transparency and is only ever sampled with nearest
neighbour, so it stays crisp. Enlarging uses whole factors, which turns every
source pixel into a solid square block, and the one target smaller than the
sprite is sampled down without any smoothing. The result sits centered in a
square canvas, since the sprite is not square.

usage: make-icon.py [tile.bmp] [output-directory]
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

TRANSPARENT = (0, 0, 0, 0)

# Sizes macOS wants in an .iconset, and the smaller ones Windows likes in an .ico.
ICNS_SIZES = {
    "icon_16x16.png": 16, "icon_16x16@2x.png": 32,
    "icon_32x32.png": 32, "icon_32x32@2x.png": 64,
    "icon_128x128.png": 128, "icon_128x128@2x.png": 256,
    "icon_256x256.png": 256, "icon_256x256@2x.png": 512,
    "icon_512x512.png": 512, "icon_512x512@2x.png": 1024,
}
ICO_SIZES = (16, 32, 48, 64, 128, 256)
PNG_SIZE = 256


def load_bmp(path):
    """Reads a 32 bit BGRA (bit fields) BMP into rows of RGBA tuples."""
    with open(path, "rb") as f:
        d = f.read()

    data_off = struct.unpack_from("<I", d, 10)[0]
    header_size = struct.unpack_from("<I", d, 14)[0]
    width, height = struct.unpack_from("<ii", d, 18)
    bits = struct.unpack_from("<H", d, 28)[0]
    compression = struct.unpack_from("<I", d, 30)[0]

    if bits != 32:
        raise SystemExit("expected a 32 bit BMP, got %d" % bits)
    if compression == 3 and header_size >= 108:
        masks = struct.unpack_from("<IIII", d, 54)
        if masks != (0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000):
            raise SystemExit("unexpected channel masks: %r" % (masks,))

    stride = ((width * bits + 31) // 32) * 4
    rows = []
    for y in range(abs(height)):
        row = (height - 1 - y) if height > 0 else y
        base = data_off + row * stride
        rows.append([tuple(d[base + x * 4:base + x * 4 + 4][i] for i in (2, 1, 0, 3))
                     for x in range(width)])
    return width, abs(height), rows


def upscaled_block(src, factor):
    img = []
    for line in src:
        wide = [pixel for pixel in line for _ in range(factor)]
        img.extend([wide] * factor)
    return img


def downscaled(src_w, src_h, src, dst_w, dst_h):
    img = []
    for y in range(dst_h):
        sy = min(src_h - 1, y * src_h // dst_h)
        img.append([src[sy][min(src_w - 1, x * src_w // dst_w)] for x in range(dst_w)])
    return img


def render(src_w, src_h, src, size):
    """The sprite centered in a transparent size x size canvas."""
    if size >= src_w:
        factor = size // src_w
        while factor > 1 and src_h * factor > size:
            factor -= 1
        body = upscaled_block(src, factor)
    else:
        body = downscaled(src_w, src_h, src, size, max(1, round(src_h * size / src_w)))

    canvas = [[TRANSPARENT] * size for _ in range(size)]
    body_w, body_h = len(body[0]), len(body)
    off_x, off_y = (size - body_w) // 2, (size - body_h) // 2
    for y in range(body_h):
        canvas[off_y + y][off_x:off_x + body_w] = body[y]
    return canvas


def png_bytes(img):
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    raw = bytearray()
    for line in img:
        raw.append(0)
        for pixel in line:
            raw += bytes(pixel)

    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", len(img[0]), len(img), 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
            chunk(b"IEND", b""))


def write_icns(path, pngs):
    iconset = tempfile.mkdtemp(suffix=".iconset")
    try:
        for name, data in pngs.items():
            with open(os.path.join(iconset, name), "wb") as f:
                f.write(data)
        subprocess.run(["iconutil", "-c", "icns", iconset, "-o", path], check=True)
    finally:
        shutil.rmtree(iconset, ignore_errors=True)


def write_ico(path, entries):
    """entries is a list of (size, png data); PNG inside ICO is fine since Vista."""
    out = struct.pack("<HHH", 0, 1, len(entries))
    offset = 6 + 16 * len(entries)
    for size, data in entries:
        dim = 0 if size >= 256 else size
        out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in entries:
        out += data
    with open(path, "wb") as f:
        f.write(out)


def main():
    tile = sys.argv[1] if len(sys.argv) > 1 else "res/tiles/tile55.bmp"
    outdir = sys.argv[2] if len(sys.argv) > 2 else "assets"

    src_w, src_h, src = load_bmp(tile)
    os.makedirs(outdir, exist_ok=True)

    cache = {}
    for size in sorted(set(ICNS_SIZES.values()) | set(ICO_SIZES) | {PNG_SIZE}):
        cache[size] = png_bytes(render(src_w, src_h, src, size))

    with open(os.path.join(outdir, "icon.png"), "wb") as f:
        f.write(cache[PNG_SIZE])

    write_ico(os.path.join(outdir, "icon.ico"), [(s, cache[s]) for s in ICO_SIZES])

    icns = os.path.join(outdir, "icon.icns")
    if sys.platform == "darwin" and shutil.which("iconutil"):
        write_icns(icns, {name: cache[size] for name, size in ICNS_SIZES.items()})
    else:
        print("skipping icon.icns, needs macOS iconutil")

    print("wrote icons from %s (%dx%d) into %s" % (tile, src_w, src_h, outdir))


if __name__ == "__main__":
    main()
