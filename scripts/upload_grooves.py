#!/usr/bin/env python3
"""Copy a groove pack's MIDI grooves to /GROOVES/ on the engine's SD card over serial.

Owner 2026-11-10: "create the folder and move these to the SD card via python". Uses the engine firmware's
`!ENGINE file-open / file-data / file-close / file-list` verbs (EngineMain.cpp `fileCommand`), which write only under
/GROOVES/ and never delete or rename. Each 64-byte chunk is acknowledged before the next is sent.

  .venv/bin/python scripts/upload_grooves.py "grooves/Grooves From Mars/MIDI"            # every machine folder
  .venv/bin/python scripts/upload_grooves.py "grooves/Grooves From Mars/MIDI" --only MPC60 "SP 1200"
  .venv/bin/python scripts/upload_grooves.py --list "/GROOVES/MPC60"

`Patterns` folders (whole patterns, not timing grooves) are skipped. The pack's files are the owner's own download:
they are never committed (the folder is excluded from git).
"""
import argparse
import glob
import pathlib
import sys
import time
import zlib

import serial

CHUNK = 64


def port():
    ports = glob.glob("/dev/cu.usbmodem*")
    if not ports:
        sys.exit("no Teensy serial port")
    return serial.Serial(ports[0], 115200, timeout=0.05)


def ask(s, line, want, timeout=3.0):
    s.write((line + "\n").encode())
    s.flush()
    buf, t = b"", time.time()
    while time.time() - t < timeout:
        buf += s.read(4096)
        while b"\n" in buf:
            row, buf = buf.split(b"\n", 1)
            row = row.decode(errors="replace").strip()
            if row.startswith(want):
                return row
    return ""


def put(s, local, remote):
    data = local.read_bytes()
    if not ask(s, f"!ENGINE file-open {remote}", "#ENGINE,file,open").endswith(",ok"):
        return False, "open"
    for i in range(0, len(data), CHUNK):
        row = ask(s, "!ENGINE file-data " + data[i:i + CHUNK].hex(), "#ENGINE,file,data")
        if ",ok," not in row:
            return False, f"data at {i}"
    row = ask(s, f"!ENGINE file-close {zlib.crc32(data):08X}", "#ENGINE,file,close")
    return row.startswith("#ENGINE,file,close,ok"), row


def listing(s, path):
    s.write(f"!ENGINE file-list {path}\n".encode())
    s.flush()
    buf, t, rows = b"", time.time(), []
    while time.time() - t < 3.0:
        buf += s.read(4096)
        while b"\n" in buf:
            row, buf = buf.split(b"\n", 1)
            row = row.decode(errors="replace").strip()
            if row.startswith("#ENGINE,file,entry,"):
                rows.append(row[19:])
            elif row.startswith("#ENGINE,file,list,"):
                return rows
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", nargs="?", help="the pack's MIDI folder (one subfolder a machine)")
    ap.add_argument("--only", nargs="*", help="machine folders to copy (default: all)")
    ap.add_argument("--list", metavar="DIR", help="list a card folder and stop")
    args = ap.parse_args()
    s = port()
    time.sleep(0.5)
    if args.list:
        for row in listing(s, args.list):
            print(row)
        return
    if not args.source:
        ap.error("source folder needed")
    root = pathlib.Path(args.source)
    files = sorted(p for p in root.rglob("*.mid") if "Patterns" not in p.parts)
    if args.only:
        files = [p for p in files if p.relative_to(root).parts[0] in args.only]
    done = failed = 0
    t0 = time.time()
    for p in files:
        rel = p.relative_to(root)
        remote = "/GROOVES/" + "/".join(rel.parts)
        ok, why = put(s, p, remote)
        done += ok
        failed += not ok
        print(("ok   " if ok else "FAIL ") + remote + ("" if ok else f"  ({why})"), flush=True)
    print(f"{done} copied, {failed} failed, {time.time() - t0:.0f} s")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
