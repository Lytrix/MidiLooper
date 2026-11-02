#!/usr/bin/env python3
"""Render an OLED framebuffer dump (`!FB_DUMP`, capture builds) to a PNG.

The firmware prints 64 lines `#FB,<row>,<256 hex chars>`: 256x64 pixels, 4-bit gray, two pixels per
byte, high nibble first. The last complete dump in the input is rendered (linear gray, level 15 = white).

  scripts/render_display_dump.py captures/session_X.log -o frame.png [--scale 4] [--ascii]

Send the command on the serial port first, e.g. `!FB_DUMP` followed by a newline. The PNG shows what
was drawn; how bright low gray levels look on the panel depends on its grayscale table.
"""

import argparse
import struct
import sys
import zlib

WIDTH, HEIGHT = 256, 64


def parse_last_dump(lines):
    rows = {}
    last_complete = None
    for line in lines:
        start = line.find("#FB,")
        if start < 0:
            continue
        parts = line[start:].strip().split(",", 2)
        if len(parts) != 3 or len(parts[2]) != WIDTH:
            continue
        row = int(parts[1])
        if row == 0:
            rows = {}
        rows[row] = parts[2]
        if len(rows) == HEIGHT:
            last_complete = dict(rows)
    if last_complete is None:
        return None
    pixels = []
    for y in range(HEIGHT):
        hexrow = last_complete[y]
        row = []
        for i in range(0, len(hexrow), 2):
            byte = int(hexrow[i:i + 2], 16)
            row += [byte >> 4, byte & 0x0F]
        pixels.append(row)
    return pixels


def write_png(path, pixels, scale):
    raw = b"".join(
        b"\x00" + bytes(v * 17 for v in row for _ in range(scale))
        for row in pixels for _ in range(scale))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", WIDTH * scale, HEIGHT * scale, 8, 0, 0, 0, 0)
    with open(path, "wb") as out:
        out.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("log", help="serial capture containing a #FB dump ('-' for stdin)")
    parser.add_argument("-o", "--output", default="display_dump.png")
    parser.add_argument("--scale", type=int, default=4)
    parser.add_argument("--ascii", action="store_true", help="also print gray levels as hex digits")
    args = parser.parse_args()

    source = sys.stdin if args.log == "-" else open(args.log, errors="replace")
    with source:
        pixels = parse_last_dump(source)
    if pixels is None:
        sys.exit("no complete #FB dump found")
    write_png(args.output, pixels, args.scale)
    if args.ascii:
        for y, row in enumerate(pixels):
            print("%02d %s" % (y, "".join(" " if v == 0 else "%x" % v for v in row)))
    print(args.output)


if __name__ == "__main__":
    main()
