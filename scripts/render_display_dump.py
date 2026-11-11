#!/usr/bin/env python3
"""Render a MidiLooper OLED framebuffer dump to a PNG.

The firmware prints 64 lines `#FB,<row>,<256 hex chars>`: 256x64 pixels, 4-bit gray, two pixels per
byte, high nibble first. The default is a linear gray rendering of the last complete dump.
Use --panel for the manual's yellow-green OLED palette and six native pixels of black surround.

  scripts/render_display_dump.py captures/session_X.log -o frame.png [--scale 4] [--panel] [--ascii]

Capture the serial output of `!ENGINE fbdump` (engine firmware) or `!FB_DUMP` (legacy capture
firmware). The PNG shows what was drawn. Physical brightness depends on the panel's grayscale table.
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


def write_png(path, pixels, scale, panel=False):
    width, height = WIDTH, HEIGHT
    palette = b""
    color_type = 0  # grayscale
    levels = bytes(n * 17 for n in range(16))
    if panel:
        inset = 6
        width += 2 * inset
        height += 2 * inset
        surround = [[0] * width] * inset
        pixels = surround + [[0] * inset + row + [0] * inset for row in pixels] + surround
        # Same sixteen drive levels as groove_cycle_bars_mockup.png.
        palette = bytes(channel for v in levels for channel in (v, v * 85 // 100, v * 30 // 100))
        color_type = 3  # indexed color
        levels = bytes(range(16))
    raw = b"".join(
        b"\x00" + bytes(levels[v] for v in row for _ in range(scale))
        for row in pixels for _ in range(scale))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width * scale, height * scale, 8, color_type, 0, 0, 0)
    with open(path, "wb") as out:
        out.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header))
        if palette:
            out.write(chunk(b"PLTE", palette))
        out.write(chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("log", help="serial capture containing a #FB dump ('-' for stdin)")
    parser.add_argument("-o", "--output", default="display_dump.png")
    parser.add_argument("--scale", type=int, default=4)
    parser.add_argument("--panel", action="store_true", help="use the OLED palette and black surround")
    parser.add_argument("--ascii", action="store_true", help="also print gray levels as hex digits")
    args = parser.parse_args()

    source = sys.stdin if args.log == "-" else open(args.log, errors="replace")
    with source:
        pixels = parse_last_dump(source)
    if pixels is None:
        sys.exit("no complete #FB dump found")
    write_png(args.output, pixels, args.scale, panel=args.panel)
    if args.ascii:
        for y, row in enumerate(pixels):
            print("%02d %s" % (y, "".join(" " if v == 0 else "%x" % v for v in row)))
    print(args.output)


if __name__ == "__main__":
    main()
