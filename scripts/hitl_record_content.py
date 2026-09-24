#!/usr/bin/env python3
"""Record a pass into the authorized scratch slot without the base preset.

The base preset currently fails on this device (`record stop did not reach PLAYING`, empty store —
`captures/a51-finding-01.md`), while a direct clock+note stream records and saves cleanly
(`captures/a37-control-02/` sealed 64 events). This script is the minimal content creator for the
content-verification pipeline: it streams notes against MIDI clock, stops the record, and holds the port
open through the deferred save.

Usage:
  .venv/bin/python scripts/hitl_record_content.py --out-dir captures/cc-01 \
      --track 5 --loop-slot 1 --bars 2
"""
from __future__ import annotations

import argparse
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


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--port", default="/dev/cu.usbmodem154944801")
    ap.add_argument("--track", type=int, default=5)
    ap.add_argument("--loop-slot", type=int, default=1)
    ap.add_argument("--midi-channel", type=int, default=5)
    ap.add_argument("--bars", type=float, default=2.0)
    ap.add_argument("--bpm", type=float, default=120.0)
    ap.add_argument("--boot-wait-s", type=float, default=10.0)
    ap.add_argument("--settle-ms", type=int, default=3000)
    args = ap.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=False)
    lines: list[str] = []
    stop = False
    ser = serial.Serial(args.port, 115200, timeout=0.2)
    log = (args.out_dir / "session.log").open("w", encoding="utf-8")

    def reader() -> None:
        while not stop:
            try:
                raw = ser.readline()
            except Exception:
                break
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").rstrip()
            lines.append(line)
            log.write(line + "\n")

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    def send_note(out, note: int, hold_ms: int, channel_1based: int) -> None:
        out.send(mido.Message("note_on", channel=channel_1based - 1, note=note, velocity=127))
        time.sleep(max(hold_ms, 1) / 1000.0)
        out.send(mido.Message("note_off", channel=channel_1based - 1, note=note, velocity=0))

    out = mido.open_output("Teensy MIDI")
    time.sleep(args.boot_wait_s)

    # Control notes live on the control channel; musical notes on the track's channel.
    send_note(out, TRACK_SELECT_NOTE_BASE + (args.track - 1), 120, CONTROL_CHANNEL_1BASED)
    time.sleep(0.4)
    send_note(out, LOOP_SELECT_NOTE_BASE + (args.loop_slot - 1), 120, CONTROL_CHANNEL_1BASED)
    time.sleep(0.6)

    total = int(round(args.bars * 96))
    period = 60.0 / (args.bpm * 24.0)
    out.send(mido.Message("start"))
    for i in range(total):
        out.send(mido.Message.from_bytes([MIDI_CLOCK]))
        if i % 6 == 0:
            send_note(out, 48 + ((i // 6) % 32), 40, args.midi_channel)
        time.sleep(period)
    time.sleep(0.2)
    send_note(out, RECORD_BUTTON_NOTE, 120, CONTROL_CHANNEL_1BASED)
    time.sleep(args.settle_ms / 1000.0)
    out.send(mido.Message.from_bytes([MIDI_STOP]))
    time.sleep(2.0)

    stop = True
    time.sleep(0.3)
    try:
        ser.close()
    except Exception:
        pass
    log.close()
    out.close()

    saved = sum(1 for l in lines if "saved successfully" in l)
    sealed = [l for l in lines if "RECS,stage,seal" in l]
    length = [l for l in lines if "record loop length" in l or "Recording stopped" in l]
    print(f"[record] lines={len(lines)} saved_lines={saved}")
    for l in sealed[-3:]:
        print("   seal:", l[:160])
    for l in length[-3:]:
        print("   length:", l[:160])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
