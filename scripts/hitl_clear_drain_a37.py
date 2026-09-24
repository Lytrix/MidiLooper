#!/usr/bin/env python3
"""Packet C / A3.7 device gate: CLEAR with a deferred save owed (drain path).

Runs on an explicitly named scratch slot (default track 5 / slot 1). It:

  1. records a short pass on the scratch slot so the workspace save is owed,
  2. waits a configurable moment so the deferred save is either still owed or done,
  3. long-presses CLEAR (RECORD button long press on the control channel),
  4. records serial for the whole run.

The verdict is read from the serial log, not from this script's exit code:

  * `Clear ignored - track is empty`        -> the clear saw an already-empty slot
  * `Clear waiting for deferred save completion` -> the clear took the drain path
  * `Clear refused: ... blocked by <name>`  -> a NAMED refusal (expected outcome)
  * `Clear aborted: ...`                    -> must be 0 unless it names a real failure
  * `State saved successfully (v4)`         -> the owed save actually completed
  * `Persistence drain stuck` / `drain blocked` -> must be 0

Usage (device must already have `teensy41-capture-serial` flashed):

  .venv/bin/python scripts/hitl_clear_drain_a37.py --out-dir captures/a37-clear-01 \
      --track 5 --loop-slot 1
"""
from __future__ import annotations

import argparse
import re
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
    GLOBAL_TRANSPORT_NOTE,
    LOOP_SELECT_NOTE_BASE,
    RECORD_BUTTON_NOTE,
    TRACK_SELECT_NOTE_BASE,
)

MIDI_CLOCK = 0xF8
MIDI_START = 0xFA
MIDI_STOP = 0xFC

TICKS = re.compile(r"#CAP,(\d+),(.*)$")


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


def send_note(out, note: int, channel_1based: int, hold_ms: int) -> None:
    out.send(mido.Message("note_on", channel=channel_1based - 1, note=note, velocity=127))
    time.sleep(max(hold_ms, 1) / 1000.0)
    out.send(mido.Message("note_off", channel=channel_1based - 1, note=note, velocity=0))


def stream_clock(out, in_port, *, bars: float, bpm: float, channel_1based: int,
                 low: int, high: int, start: bool) -> None:
    """Drive the transport with MIDI clock and stream one note per 16th."""
    ticks_per_bar = 96
    total = int(round(bars * ticks_per_bar))
    period = 60.0 / (bpm * 24.0)
    if start:
        out.send(mido.Message("start"))
    for i in range(total):
        out.send(mido.Message.from_bytes([MIDI_CLOCK]))
        if i % 6 == 0:
            note = low + ((i // 6) % max(1, (high - low + 1)))
            send_note(out, note, channel_1based, hold_ms=40)
        time.sleep(period)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--port", default="/dev/cu.usbmodem154944801")
    ap.add_argument("--track", type=int, default=5)
    ap.add_argument("--loop-slot", type=int, default=1)
    ap.add_argument("--midi-channel", type=int, default=5)
    ap.add_argument("--bpm", type=float, default=120.0)
    ap.add_argument("--record-bars", type=float, default=2.0)
    ap.add_argument("--overdub-bars", type=float, default=0.0,
                    help="Arm an overdub for this many bars before the CLEAR (shape b: a track is "
                         "audibly playing / timing-critical while the clear drains).")
    ap.add_argument("--clear-after-ms", type=int, default=120,
                    help="Delay from record-stop release to the CLEAR long press. Use a small value to "
                         "land the clear while a deferred save is still owed.")
    ap.add_argument("--clear-press-ms", type=int, default=1400)
    ap.add_argument("--settle-ms", type=int, default=9000)
    ap.add_argument("--boot-wait-s", type=float, default=10.0)
    ap.add_argument("--low-note", type=int, default=48)
    ap.add_argument("--high-note", type=int, default=79)
    args = ap.parse_args()

    out_dir: Path = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=False)
    log = SerialLog(args.port, out_dir / "session.log")
    print(f"[a37] serial log -> {out_dir / 'session.log'}")

    try:
        print(f"[a37] boot settle {args.boot_wait_s:.0f}s")
        time.sleep(args.boot_wait_s)
        booted = any("boot_reset" in line for line in log.snapshot())
        print(f"[a37] boot banner seen: {booted}")

        out = mido.open_output("Teensy MIDI")
        in_port = mido.open_input("Teensy MIDI")

        # 1. Select the scratch track and slot with short presses.
        send_note(out, TRACK_SELECT_NOTE_BASE + (args.track - 1), CONTROL_CHANNEL_1BASED, 120)
        time.sleep(0.4)
        send_note(out, LOOP_SELECT_NOTE_BASE + (args.loop_slot - 1), CONTROL_CHANNEL_1BASED, 120)
        time.sleep(0.6)

        # 2. Start transport, arm record, stream the pass, stop record.
        mark = len(log.snapshot())
        print(f"[a37] record {args.record_bars} bars on track {args.track} slot {args.loop_slot}")
        stream_clock(out, in_port, bars=args.record_bars, bpm=args.bpm,
                     channel_1based=args.midi_channel,
                     low=args.low_note, high=args.high_note, start=True)
        time.sleep(0.2)
        send_note(out, RECORD_BUTTON_NOTE, CONTROL_CHANNEL_1BASED, 120)
        print(f"[a37] record stop; waiting {args.clear_after_ms} ms before CLEAR")
        time.sleep(max(args.clear_after_ms, 0) / 1000.0)

        # 2b. Optional shape (b): leave the track audibly playing and timing-critical.
        if args.overdub_bars > 0:
            print(f"[a37] overdub {args.overdub_bars} bars (shape b: track audibly playing at CLEAR)")
            send_note(out, RECORD_BUTTON_NOTE, CONTROL_CHANNEL_1BASED, 120)
            stream_clock(out, in_port, bars=args.overdub_bars, bpm=args.bpm,
                         channel_1based=args.midi_channel,
                         low=args.low_note, high=args.high_note, start=False)

        # 3. The A3.7 intervention: CLEAR (RECORD long press) on the scratch slot.
        print(f"[a37] CLEAR long press ({args.clear_press_ms} ms)")
        send_note(out, RECORD_BUTTON_NOTE, CONTROL_CHANNEL_1BASED, args.clear_press_ms)
        mark_clear = len(log.snapshot())

        time.sleep(args.settle_ms / 1000.0)
        out.send(mido.Message.from_bytes([MIDI_STOP]))
        time.sleep(1.0)

        print("[a37] --- serial since CLEAR ---")
        for line in log.snapshot()[mark_clear:]:
            print("   ", line[:160])
        print("[a37] --- record-phase serial (tail) ---")
        for line in log.snapshot()[mark:mark_clear][-25:]:
            print("   ", line[:160])
    finally:
        log.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
