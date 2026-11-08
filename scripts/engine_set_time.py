#!/usr/bin/env python3
"""Set the engine's RTC to this computer's local time (sets browser slice 1).

    .venv/bin/python scripts/engine_set_time.py [--port /dev/cu.usbmodemXXXX] [--utc]

The Teensy loader sets the RTC to the computer's local time at every upload, so the engine reads the RTC as local time
with a shown offset of 0. This script does the same between uploads: `!ENGINE time <local seconds>` and `!ENGINE tz 0`.
`--utc` instead sets UTC with the computer's offset (only useful if uploads no longer set the clock).
"""
import argparse
import glob
import sys
import time

import serial


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--port', help='serial port (default: the first /dev/cu.usbmodem*)')
    ap.add_argument('--utc', action='store_true', help='set UTC and the computer\'s offset instead of local time')
    args = ap.parse_args()
    port = args.port or next(iter(sorted(glob.glob('/dev/cu.usbmodem*'))), None)
    if port is None:
        print('no /dev/cu.usbmodem* port found', file=sys.stderr)
        return 1
    gmtoff = time.localtime().tm_gmtoff
    clock = int(time.time()) if args.utc else int(time.time()) + gmtoff
    offset = gmtoff // 60 if args.utc else 0
    with serial.Serial(port, 115200, timeout=0.2) as s:
        s.reset_input_buffer()
        s.write(f'!ENGINE time {clock}\n'.encode())
        s.write(f'!ENGINE tz {offset}\n'.encode())
        s.flush()
        deadline = time.time() + 2.0
        buf = b''
        while time.time() < deadline:
            buf += s.read(4096)
    replies = [l for l in buf.decode(errors='replace').splitlines() if l.startswith('#ENGINE,time')]
    if not replies:
        print('no #ENGINE,time reply', file=sys.stderr)
        return 1
    print(replies[-1])
    return 0


if __name__ == '__main__':
    sys.exit(main())
