#!/usr/bin/env python3
"""A3.7 shape (b), second construction: restore **in flight** + save owed at the CLEAR edge.

The first construction left the reload only *queued* (the scratch track was still overdubbing, so restore
preparation was denied by capture), and a queued request is not a restore in flight. This one sequences it:

  1. CLEAR a populated scratch slot, which (with the reload-after-external-reset route enabled) queues a
     reload of it;
  2. record a long take on the scratch slot and stop, leaving a deferred save genuinely owed and the
     scratch track audibly playing -- another track;
  3. re-select the cleared slot and CLEAR it while the save is still owed, so restore preparation has
     resumed and a `LoadLoopJob` is in flight at the CLEAR edge.

Run against the investigation build (route temporarily enabled); the normal build leaves the route off.

Usage:
  .venv/bin/python scripts/hitl_a37_shapeb_v2.py --out-dir captures/a37b2-01
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

import mido
import serial

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from hitl.control_constants import (  # noqa: E402
    CONTROL_CHANNEL_1BASED,
    LOOP_SELECT_NOTE_BASE,
    RECORD_BUTTON_NOTE,
    TRACK_SELECT_NOTE_BASE,
)

MIDI_CLOCK = 0xF8
MIDI_STOP = 0xFC


class SerialLog:
    def __init__(self, port: str, path: Path) -> None:
        self.lines: list[str] = []
        self._stop = False
        self._handle = path.open("w", encoding="utf-8")
        self._ser = serial.Serial(port, 115200, timeout=0.2)
        self._thread = threading.Thread(target=self._read, daemon=True)
        self._thread.start()

    def _read(self) -> None:
        while not self._stop:
            try:
                raw = self._ser.readline()
            except Exception:
                break
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").rstrip()
            self.lines.append(line)
            self._handle.write(line + "\n")
            self._handle.flush()

    def snapshot(self) -> list[str]:
        return list(self.lines)

    def close(self) -> None:
        self._stop = True
        time.sleep(0.3)
        try:
            self._ser.close()
        except Exception:
            pass
        self._handle.close()


def wait_for_port(timeout_s: float = 30.0) -> str | None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        found = sorted(Path("/dev").glob("cu.usbmodem*"))
        if found:
            return str(found[0])
        time.sleep(0.2)
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--port", default="/dev/cu.usbmodem154944801")
    ap.add_argument("--reload-track", type=int, default=6)
    ap.add_argument("--reload-slot", type=int, default=8)
    ap.add_argument("--scratch-track", type=int, default=5)
    ap.add_argument("--scratch-slot", type=int, default=1)
    ap.add_argument("--midi-channel", type=int, default=5)
    ap.add_argument("--bpm", type=float, default=120.0)
    ap.add_argument("--save-bars", type=float, default=8.0)
    ap.add_argument("--between-ms", type=int, default=150)
    ap.add_argument("--boot-wait-s", type=float, default=14.0)
    ap.add_argument("--clear-press-ms", type=int, default=1400)
    ap.add_argument("--settle-ms", type=int, default=6000)
    ap.add_argument("--no-upload", action="store_true")
    args = ap.parse_args()

    out_dir: Path = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=False)

    if not args.no_upload:
        print("[a37b2] re-uploading the investigation build")
        subprocess.run(["pio", "run", "-e", "teensy41-capture-serial", "-t", "upload"],
                       capture_output=True, timeout=240, check=False, cwd=str(REPO_ROOT))
        port = wait_for_port()
        if port is None:
            print("[a37b2] no port after upload")
            return 1
        args.port = port

    log = SerialLog(args.port, out_dir / "session.log")
    out = mido.open_output("Teensy MIDI")

    def send_note(note: int, hold_ms: int) -> None:
        out.send(mido.Message("note_on", channel=CONTROL_CHANNEL_1BASED - 1, note=note, velocity=127))
        time.sleep(max(hold_ms, 1) / 1000.0)
        out.send(mido.Message("note_off", channel=CONTROL_CHANNEL_1BASED - 1, note=note, velocity=0))

    def select(track: int, slot: int) -> None:
        send_note(TRACK_SELECT_NOTE_BASE + (track - 1), 120)
        time.sleep(0.4)
        send_note(LOOP_SELECT_NOTE_BASE + (slot - 1), 120)
        time.sleep(0.5)

    try:
        print(f"[a37b2] boot settle {args.boot_wait_s:.0f}s")
        time.sleep(args.boot_wait_s)
        print(f"[a37b2] boot banner: {any('boot_reset' in l for l in log.snapshot())}")

        # 1. Queue a reload of the reload target.
        select(args.reload_track, args.reload_slot)
        print(f"[a37b2] CLEAR reload target {args.reload_track}/{args.reload_slot} (queues its reload)")
        send_note(RECORD_BUTTON_NOTE, args.clear_press_ms)
        time.sleep(1.0)

        # 2. Owe a save and leave the scratch track audibly playing.
        select(args.scratch_track, args.scratch_slot)
        total = int(round(args.save_bars * 96))
        period = 60.0 / (args.bpm * 24.0)
        out.send(mido.Message("start"))
        for i in range(total):
            out.send(mido.Message.from_bytes([MIDI_CLOCK]))
            if i % 6 == 0:
                out.send(mido.Message("note_on", channel=args.midi_channel - 1,
                                      note=48 + ((i // 6) % 32), velocity=110))
                time.sleep(0.04)
                out.send(mido.Message("note_off", channel=args.midi_channel - 1,
                                      note=48 + ((i // 6) % 32), velocity=0))
            time.sleep(period)
        time.sleep(0.2)
        print("[a37b2] stop record (save owed)")
        send_note(RECORD_BUTTON_NOTE, 120)
        time.sleep(args.between_ms / 1000.0)

        # 3. Re-select the cleared slot so its restore is requested, then CLEAR with the load in flight.
        select(args.reload_track, args.reload_slot)
        time.sleep(args.between_ms / 1000.0)
        clear_mark = len(log.snapshot())
        print("[a37b2] CLEAR at the save-owed edge")
        send_note(RECORD_BUTTON_NOTE, args.clear_press_ms)
        time.sleep(args.settle_ms / 1000.0)
        out.send(mido.Message.from_bytes([MIDI_STOP]))
        time.sleep(1.0)

        window = log.snapshot()[clear_mark:]
        patterns = re.compile(
            r"Clear waiting|Clear refused|Clear aborted|Clear selected|Clear ignored|State saved|"
            r"drain|Draining|RRQ,|LoadLoopJob|out-of-range|FATAL|MEM_EXT_REFUSED")
        print("[a37b2] --- relevant serial since CLEAR ---")
        for line in [l for l in window if patterns.search(l)][:40]:
            print("   ", line[:180])
        print("[a37b2] --- context before CLEAR (tail) ---")
        for line in [l for l in log.snapshot()[max(0, clear_mark - 25):clear_mark]
                     if patterns.search(l)]:
            print("   ", line[:180])
    finally:
        out.close()
        log.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
