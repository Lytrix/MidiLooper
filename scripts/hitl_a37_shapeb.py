#!/usr/bin/env python3
"""A3.7 shape (b): CLEAR while a save is genuinely owed **and a restore is in flight**.

The previous shape-(b) attempt pressed CLEAR after the deferred save had already completed, so the
discriminating condition was never exercised. This script constructs it deliberately on the named scratch
range (track 5 slot 1 is the approved scratch slot; the reload target defaults to track 1 slot 2, the other
committed slot on the current device library):

  1. fresh boot, so the library is restored and the payload mirror is current;
  2. clear the reload target, which **supersedes its restore request** (A2.1) so the slot is owed a fresh
     restore and the payload probe decides whether it is reloadable;
  3. record a long take on the scratch slot and stop, which leaves a deferred workspace save genuinely
     owed;
  4. re-select the reload target -- if its payload is still on the card this begins a restore, i.e. an
     active `LoadLoopJob`;
  5. long-press CLEAR on the reload target with another track audibly playing.

Verdict is read from the serial log:

  * `Clear waiting for deferred save completion` -> the drain path ran;
  * `State saved successfully`                    -> the drain advanced and the save completed;
  * `Clear refused: ... blocked by <name>`        -> the named wait (an expected outcome while
                                                     `resumable-midi-boundary-playback-maintenance`
                                                     task 3.3 is open);
  * `[StorageManager] LoadLoopJob superseded before publication` or an `RRQ` restore line -> a restore was
    in flight at the CLEAR;
  * `Clear aborted` with no named reason / `Persistence drain stuck` / `drain blocked` -> failure.

Usage (device already flashed, or let the script re-upload):

  .venv/bin/python scripts/hitl_a37_shapeb.py --out-dir captures/a37-shapeb-01
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
        self.path = path
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


def send_note(out, note: int, channel_1based: int, hold_ms: int) -> None:
    out.send(mido.Message("note_on", channel=channel_1based - 1, note=note, velocity=127))
    time.sleep(max(hold_ms, 1) / 1000.0)
    out.send(mido.Message("note_off", channel=channel_1based - 1, note=note, velocity=0))


def stream_clock(out, *, bars: float, bpm: float, channel_1based: int, low: int, high: int,
                 start: bool) -> None:
    total = int(round(bars * 96))
    period = 60.0 / (bpm * 24.0)
    if start:
        out.send(mido.Message("start"))
    for i in range(total):
        out.send(mido.Message.from_bytes([MIDI_CLOCK]))
        if i % 6 == 0:
            note = low + ((i // 6) % max(1, (high - low + 1)))
            send_note(out, note, channel_1based, 40)
        time.sleep(period)


def select_track_slot(out, track_1based: int, slot_1based: int) -> None:
    send_note(out, TRACK_SELECT_NOTE_BASE + (track_1based - 1), CONTROL_CHANNEL_1BASED, 120)
    time.sleep(0.4)
    send_note(out, LOOP_SELECT_NOTE_BASE + (slot_1based - 1), CONTROL_CHANNEL_1BASED, 120)
    time.sleep(0.5)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--port", default="/dev/cu.usbmodem154944801")
    ap.add_argument("--scratch-track", type=int, default=5)
    ap.add_argument("--scratch-slot", type=int, default=1)
    ap.add_argument("--reload-track", type=int, default=6)
    ap.add_argument("--reload-slot", type=int, default=1)
    ap.add_argument("--midi-channel", type=int, default=5)
    ap.add_argument("--bpm", type=float, default=120.0)
    ap.add_argument("--save-bars", type=float, default=16.0)
    ap.add_argument("--play-bars", type=float, default=2.0)
    ap.add_argument("--prepare-bars", type=float, default=2.0)
    ap.add_argument("--pre-clear-settle-s", type=float, default=1.5)
    ap.add_argument("--restore-lead-ms", type=int, default=150)
    ap.add_argument("--clear-press-ms", type=int, default=1400)
    ap.add_argument("--settle-ms", type=int, default=6000)
    ap.add_argument("--boot-wait-s", type=float, default=12.0)
    ap.add_argument("--no-upload", action="store_true")
    args = ap.parse_args()

    out_dir: Path = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=False)

    if not args.no_upload:
        print("[a37b] re-uploading for a fresh boot")
        subprocess.run(["pio", "run", "-e", "teensy41-capture-serial", "-t", "upload"],
                       capture_output=True, timeout=240, check=False, cwd=str(REPO_ROOT))
        port = wait_for_port()
        if port is None:
            print("[a37b] no port after upload")
            return 1
        args.port = port

    log = SerialLog(args.port, out_dir / "session.log")
    print(f"[a37b] serial log -> {out_dir / 'session.log'}")
    out = mido.open_output("Teensy MIDI")

    def mark() -> int:
        return len(log.snapshot())

    try:
        print(f"[a37b] boot settle {args.boot_wait_s:.0f}s")
        time.sleep(args.boot_wait_s)
        print(f"[a37b] boot banner: {any('boot_reset' in l for l in log.snapshot())}")

        # 1. Give the reload target a payload of its own (a short pass), then CLEAR it: the clear
        # **supersedes** its restore request (A2.1), so the slot is owed a fresh restore and the payload
        # probe decides whether it is still reloadable.
        select_track_slot(out, args.reload_track, args.reload_slot)
        print(f"[a37b] prepare reload target {args.reload_track}/{args.reload_slot} with a short pass")
        stream_clock(out, bars=args.prepare_bars, bpm=args.bpm, channel_1based=args.midi_channel,
                     low=48, high=79, start=True)
        time.sleep(0.2)
        send_note(out, RECORD_BUTTON_NOTE, CONTROL_CHANNEL_1BASED, 120)
        time.sleep(0.8)
        print("[a37b] pre-CLEAR the reload target (supersede its request)")
        send_note(out, RECORD_BUTTON_NOTE, CONTROL_CHANNEL_1BASED, args.clear_press_ms)
        time.sleep(args.pre_clear_settle_s)

        # 2. A long take on the scratch slot: leave a deferred workspace save genuinely owed, and leave
        # the scratch track audibly playing (the "another track playing" half of shape b).
        select_track_slot(out, args.scratch_track, args.scratch_slot)
        print(f"[a37b] record {args.save_bars} bars on scratch {args.scratch_track}/{args.scratch_slot} "
              "(save owed)")
        stream_clock(out, bars=args.save_bars, bpm=args.bpm, channel_1based=args.midi_channel,
                     low=48, high=79, start=True)
        time.sleep(0.2)
        send_note(out, RECORD_BUTTON_NOTE, CONTROL_CHANNEL_1BASED, 120)

        # 3. Re-select the reload target: if its payload is still on the card this begins a restore.
        select_track_slot(out, args.reload_track, args.reload_slot)
        time.sleep(args.restore_lead_ms / 1000.0)

        # 4. CLEAR on the reload target while the restore is in flight and the save is owed.
        print("[a37b] CLEAR long press on the reload target")
        clear_mark = mark()
        send_note(out, RECORD_BUTTON_NOTE, CONTROL_CHANNEL_1BASED, args.clear_press_ms)

        time.sleep(args.settle_ms / 1000.0)
        out.send(mido.Message.from_bytes([MIDI_STOP]))
        time.sleep(1.0)

        lines = log.snapshot()
        interesting = [l for l in lines[clear_mark:]
                       if re.search(r"Clear|drain|State saved|LoadLoopJob|RRQ|restore|superseded",
                                    l, re.IGNORECASE)]
        print("[a37b] --- relevant serial since CLEAR ---")
        for line in interesting[-60:]:
            print("   ", line[:180])
        restore_before = sum(1 for l in lines[:clear_mark] if "RRQ," in l)
        restore_after = sum(1 for l in lines[clear_mark:] if "RRQ," in l)
        print(f"[a37b] RRQ lines before CLEAR={restore_before} after={restore_after}")
    finally:
        out.close()
        log.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
