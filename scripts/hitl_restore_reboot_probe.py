#!/usr/bin/env python3
"""S1.3 prerequisite probe: does a recorded scratch slot survive a reboot?

The runtime change's S1.3 device slope needs N to be raisable *across boots*. That was blocked
(`Track 6 loaded state: EMPTY`, `index.bin` does not reconcile). This probe measures the
prerequisite directly and cheaply:

  1. fresh boot (serial open resets the board),
  2. record a pass into the named scratch slot, wait for the deferred save to land,
  3. record the pre-reboot content/length,
  4. reboot by re-opening serial,
  5. read the boot census and query `!CAPTURE_CONTENT` again.

Verdict is read from the two logs, not from this script's exit code:

  * `SURVIVED`      - post-reboot length/content matches the pre-reboot value
  * `EMPTY`         - the slot came back empty (the recorded blocker)
  * `MISMATCH`      - came back with a different length
  * `NO_DUMP`       - no complete `CAPTURE_CONTENT` block after reboot

Usage:
  .venv/bin/python scripts/hitl_restore_reboot_probe.py --out-dir captures/s13-probe-01 \
      --track 6 --loop-slot 1 --midi-channel 6 --record-bars 4
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
    LOOP_SELECT_NOTE_BASE,
    RECORD_BUTTON_NOTE,
    TRACK_SELECT_NOTE_BASE,
)

MIDI_CLOCK = 0xF8
MIDI_STOP = 0xFC
LENGTH_RE = re.compile(r"CAPTURE_CONTENT,begin,track=(\d+),slot=(\d+),length=(\d+)")


class Session:
    """One serial session bound to a log file."""

    def __init__(self, port: str, path: Path) -> None:
        self.path = path
        self.lines: list[str] = []
        self._stop = False
        self._handle = path.open("w", encoding="utf-8")
        self._ser = serial.Serial(port, 115200, timeout=0.2)
        self._t = threading.Thread(target=self._read, daemon=True)
        self._t.start()

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

    def send(self, data: bytes) -> None:
        self._ser.write(data)

    def wait_for(self, needle: str, timeout_s: float) -> bool:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if any(needle in line for line in self.lines):
                return True
            time.sleep(0.1)
        return False

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


def stream_bars(out, *, bars: float, bpm: float, channel_1based: int,
                low: int, high: int, start: bool) -> None:
    total = int(round(bars * 96))
    period = 60.0 / (bpm * 24.0)
    if start:
        out.send(mido.Message("start"))
    for i in range(total):
        out.send(mido.Message.from_bytes([MIDI_CLOCK]))
        if i % 6 == 0:
            send_note(out, low + ((i // 6) % max(1, high - low + 1)), channel_1based, 40)
        time.sleep(period)



def force_device_reset(port: str, *, repo_root: Path) -> bool:
    """Reset the board and return True when the port re-enumerates afterwards.

    `pio run -t upload` performs the Teensy reset and re-flashes, which is the only reset this repo's
    HITL scripts rely on. We do not want a rebuild here, so `teensy_loader_cli` is not assumed either:
    the fallback is to open the port at 134 baud, which the Teensy bootloader treats as a reboot
    request.
    """
    try:
        with serial.Serial(port, 134, timeout=0.2) as dev:
            time.sleep(0.25)
        deadline = time.monotonic() + 20.0
        while time.monotonic() < deadline:
            if Path(port).exists():
                return True
            time.sleep(0.2)
        return False
    except Exception:
        return False

def capture_dump(session: Session, *, wait_s: float = 8.0) -> str | None:
    """Send !CAPTURE_CONTENT and return the begin line, if a complete block arrived."""
    before = len(session.lines)
    session.send(b"!CAPTURE_CONTENT\n")
    deadline = time.monotonic() + wait_s
    while time.monotonic() < deadline:
        if any("CAPTURE_CONTENT,end" in line for line in session.lines[before:]):
            break
        time.sleep(0.2)
    for line in session.lines[before:]:
        if "CAPTURE_CONTENT,begin" in line:
            return line
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--port", default="/dev/cu.usbmodem154944801")
    ap.add_argument("--track", type=int, default=6)
    ap.add_argument("--loop-slot", type=int, default=1)
    ap.add_argument("--midi-channel", type=int, default=6)
    ap.add_argument("--bpm", type=float, default=120.0)
    ap.add_argument("--record-bars", type=float, default=4.0)
    ap.add_argument("--settle-ms", type=int, default=9000)
    ap.add_argument("--boot-wait-s", type=float, default=12.0)
    ap.add_argument("--low-note", type=int, default=48)
    ap.add_argument("--high-note", type=int, default=79)
    args = ap.parse_args()

    out_dir: Path = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=False)

    results: dict[str, object] = {}

    # ---- Session 1: record the pass ----
    s1 = Session(args.port, out_dir / "session1-record.log")
    out = mido.open_output("Teensy MIDI")
    in_port = mido.open_input("Teensy MIDI")
    try:
        print(f"[probe] session 1 boot settle {args.boot_wait_s:.0f}s")
        time.sleep(args.boot_wait_s)
        results["boot_banner_s1"] = any("boot_reset" in l for l in s1.lines)

        send_note(out, TRACK_SELECT_NOTE_BASE + (args.track - 1), CONTROL_CHANNEL_1BASED, 120)
        time.sleep(0.4)
        send_note(out, LOOP_SELECT_NOTE_BASE + (args.loop_slot - 1), CONTROL_CHANNEL_1BASED, 120)
        time.sleep(0.6)

        print(f"[probe] record {args.record_bars} bars on track {args.track} slot {args.loop_slot}")
        stream_bars(out, bars=args.record_bars, bpm=args.bpm, channel_1based=args.midi_channel,
                    low=args.low_note, high=args.high_note, start=True)
        time.sleep(0.2)
        send_note(out, RECORD_BUTTON_NOTE, CONTROL_CHANNEL_1BASED, 120)
        print(f"[probe] record stop; settle {args.settle_ms} ms for the deferred save")
        time.sleep(args.settle_ms / 1000.0)
        out.send(mido.Message.from_bytes([MIDI_STOP]))
        time.sleep(0.8)

        results["save_completed_s1"] = sum(
            1 for l in s1.lines if "saved successfully" in l)
        pre = capture_dump(s1)
        results["pre_reboot_dump"] = pre
        print(f"[probe] pre-reboot dump: {pre}")
    finally:
        out.close()
        in_port.close()
        s1.close()

    # ---- Session 2: force a real reset, verify the banner, then query again ----
    # Opening serial resets the Teensy *unreliably*. A session that silently continues reads the
    # pre-reboot state and fakes a SURVIVED verdict, so the reset is explicit and proved by the banner
    # (and by requiring a fresh boot before the post-reboot dump is trusted).
    print("[probe] forcing a device reset (teensy reset before re-capturing)")
    reset_ok = force_device_reset(args.port, repo_root=REPO_ROOT)
    print(f"[probe] reset issued: {reset_ok}")
    time.sleep(2.0)
    s2 = Session(args.port, out_dir / "session2-reboot.log")
    try:
        print(f"[probe] session 2 (reset) settle {args.boot_wait_s:.0f}s")
        time.sleep(args.boot_wait_s)
        results["boot_banner_s2"] = any("boot_reset" in l for l in s2.lines)
        results["reconcile_warning_s2"] = sum(
            1 for l in s2.lines if "could not reconcile" in l)
        results["track_state_s2"] = [
            l for l in s2.lines if "loaded state" in l][:8]
        post = capture_dump(s2)
        results["post_reboot_dump"] = post
        print(f"[probe] post-reboot dump: {post}")
    finally:
        s2.close()

    # ---- Verdict ----
    def parse(line: str | None):
        if not line:
            return None
        m = LENGTH_RE.search(line)
        return int(m.group(3)) if m else None

    pre_len, post_len = parse(results.get("pre_reboot_dump")), parse(results.get("post_reboot_dump"))
    if not results.get("boot_banner_s2"):
        verdict = "NO_RESET"
    elif results.get("post_reboot_dump") is None:
        verdict = "NO_DUMP"
    elif pre_len is None:
        verdict = "UNKNOWN_PRE"
    elif post_len == pre_len and post_len > 0:
        verdict = "SURVIVED"
    elif post_len == 0:
        verdict = "EMPTY"
    else:
        verdict = "MISMATCH"
    results["verdict"] = verdict

    print("\n[probe] --- verdict ---")
    for key in ("boot_banner_s1", "save_completed_s1", "pre_reboot_dump", "boot_banner_s2",
                "reconcile_warning_s2", "post_reboot_dump", "verdict"):
        print(f"  {key}: {results.get(key)}")
    for line in results.get("track_state_s2") or []:
        print(f"  state: {line[:140]}")

    import json
    (out_dir / "probe-result.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    return 0 if verdict == "SURVIVED" else 1


if __name__ == "__main__":
    raise SystemExit(main())
