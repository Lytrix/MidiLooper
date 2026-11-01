#!/usr/bin/env python3
#  Copyright (c)  2025 Lytrix (Eelke Jager)
#  Licensed under the PolyForm Noncommercial 1.0.0
"""revision-manifest-prototype B.4: trigger `!REVSTORE_PROBE` and collect the `REVSTORE,` rows.

Requires the `teensy41-capture-serial-revstore-probe` image and a STOPPED transport (the command refuses while
running). Verifies boot first by waiting for `BOOT,load,ok` unless --no-boot-wait is given; `boot_reset,crash=0` is
reported when seen. Writes the raw rows to --out (default: stdout only).

    .venv/bin/python scripts/revstore_probe.py --port /dev/cu.usbmodemXXXX --out evidence.txt
"""
import argparse
import glob
import sys
import time

import serial


def pick_port(explicit):
    if explicit:
        return explicit
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("no /dev/cu.usbmodem* port: is the Teensy connected?")
    return ports[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port")
    parser.add_argument("--out")
    parser.add_argument("--boot-timeout", type=float, default=60.0)
    parser.add_argument("--probe-timeout", type=float, default=120.0)
    parser.add_argument("--no-boot-wait", action="store_true")
    parser.add_argument("--command", default="!REVSTORE_PROBE", help="serial command (e.g. !REVSHADOW_STATUS)")
    parser.add_argument("--prefix", default="REVSTORE,", help="row prefix to collect")
    parser.add_argument("--done", default="REVSTORE,done", help="row that ends collection (prefix match)")
    args = parser.parse_args()

    port = pick_port(args.port)
    rows = []
    with serial.Serial(port, 115200, timeout=0.2) as link:
        if not args.no_boot_wait:
            deadline = time.time() + args.boot_timeout
            booted = False
            while time.time() < deadline and not booted:
                line = link.readline().decode("utf-8", "replace").strip()
                if "boot_reset" in line and "crash=" in line:
                    print(line)
                    rows.append(line)
                if "BOOT,load,ok" in line:
                    print(line)
                    rows.append(line)
                    booted = True
            if not booted:
                print("warning: BOOT,load,ok not seen (board may have booted before the port opened)")
        link.reset_input_buffer()
        link.write((args.command + "\n").encode("ascii"))
        link.flush()
        deadline = time.time() + args.probe_timeout
        while time.time() < deadline:
            line = link.readline().decode("utf-8", "replace").strip()
            if not line.startswith(args.prefix):
                continue
            print(line)
            rows.append(line)
            if line.startswith(args.done) or line.startswith(args.prefix + "denied") or line.startswith(args.prefix + "off"):
                break
        else:
            print("warning: probe timeout before REVSTORE,done")
    if args.out:
        with open(args.out, "w", encoding="utf-8") as handle:
            handle.write("\n".join(rows) + "\n")
    return 0 if rows and rows[-1].startswith(args.done) else 1


if __name__ == "__main__":
    sys.exit(main())
