#!/usr/bin/env python3
"""Bounded clear/reload probe: find the producer of the out-of-range bar tick.

With the reload-after-external-reset route temporarily enabled in an investigation build, clearing a
slot whose payload is still on the card queues a reload of it. `LoopEventStore::append` now refuses a
tick beyond `MAX_INDEXED_TICKS` and logs `LoopEventStore refused out-of-range tick <tick> (max <max>)`
instead of sizing the bar index to it, so this probe can name **which** slot/payload produced the corrupt
tick without aborting the firmware.

It clears only the named slots (the content this session created outside the approved range, whose removal
the owner approved) and reports, per slot, the restore-request transitions and any refusal.

Usage:
  .venv/bin/python scripts/hitl_clear_reload_probe.py --out-dir captures/cr-01 \
      --track 3 --slots 2,3,4
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


def send_note(out, note: int, hold_ms: int) -> None:
    out.send(mido.Message("note_on", channel=CONTROL_CHANNEL_1BASED - 1, note=note, velocity=127))
    time.sleep(max(hold_ms, 1) / 1000.0)
    out.send(mido.Message("note_off", channel=CONTROL_CHANNEL_1BASED - 1, note=note, velocity=0))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--port", default="/dev/cu.usbmodem154944801")
    ap.add_argument("--track", type=int, default=3)
    ap.add_argument("--slots", default="2,3,4", help="1-based loop slots to clear, comma separated")
    ap.add_argument("--boot-wait-s", type=float, default=14.0)
    ap.add_argument("--after-clear-s", type=float, default=8.0)
    ap.add_argument("--clear-press-ms", type=int, default=1400)
    ap.add_argument("--no-upload", action="store_true")
    args = ap.parse_args()

    slots = [int(s) for s in args.slots.split(",") if s.strip()]
    out_dir: Path = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=False)

    if not args.no_upload:
        print("[cr] re-uploading the investigation build")
        subprocess.run(["pio", "run", "-e", "teensy41-capture-serial", "-t", "upload"],
                       capture_output=True, timeout=240, check=False, cwd=str(REPO_ROOT))
        port = wait_for_port()
        if port is None:
            print("[cr] no port after upload")
            return 1
        args.port = port

    log = SerialLog(args.port, out_dir / "session.log")
    out = mido.open_output("Teensy MIDI")
    try:
        print(f"[cr] boot settle {args.boot_wait_s:.0f}s")
        time.sleep(args.boot_wait_s)
        print(f"[cr] boot banner: {any('boot_reset' in l for l in log.snapshot())}")

        report: list[str] = []
        for slot_1based in slots:
            send_note(out, TRACK_SELECT_NOTE_BASE + (args.track - 1), 120)
            time.sleep(0.4)
            send_note(out, LOOP_SELECT_NOTE_BASE + (slot_1based - 1), 120)
            time.sleep(0.6)
            mark = len(log.snapshot())
            print(f"[cr] CLEAR track {args.track} slot {slot_1based}")
            send_note(out, RECORD_BUTTON_NOTE, args.clear_press_ms)
            time.sleep(args.after_clear_s)
            window = log.snapshot()[mark:]
            refusals = [l for l in window if "out-of-range tick" in l]
            requests = [l for l in window if "RRQ," in l]
            jobs = [l for l in window if "LoadLoopJob" in l]
            fatal = [l for l in window if "FATAL" in l or "MEM_EXT_REFUSED" in l]
            summary = (f"track={args.track} slot={slot_1based} refusals={len(refusals)} "
                       f"requests={len(requests)} jobs={len(jobs)} fatal={len(fatal)}")
            print("   ", summary)
            for l in refusals[:4]:
                print("      refusal:", l[:170])
            for l in requests[:6]:
                print("      rrq:    ", l[:170])
            for l in fatal[:3]:
                print("      FATAL:  ", l[:170])
            report.append(summary)

        (out_dir / "probe-result.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    finally:
        out.close()
        log.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
