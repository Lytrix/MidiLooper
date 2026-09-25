#!/usr/bin/env python3
"""S1.3 save/restore-survival — one take and its identity counters in a single serial session.

The bounded fixture the owner asked for. It deliberately does **not** use the record harness's
collector lifecycle, because that closes and reopens the port around the take and has twice left the
device's command channel stalled — which is the transition under suspicion, not just a nuisance.

One connection, one reader thread, for the whole run:

  1. open the serial port and keep one reader attached;
  2. read the baseline identity counters (`!CAP_IDENTITY_STATS`);
  3. perform the take on the pinned slot (track/loop select, arm, stream, record stop);
  4. stop the transport, confirm the stopped state, then request `!CAP_IDENTITY_STATS` again
     **before closing**;
  5. save the response and keep the reboot withheld.

If the second request fails on this same connection, the script says so and prints the reception
evidence (baseline request answered, stopped-state confirmation, telemetry continuing) so command
reception, parsing and stopped-state admission can be separated next. **A retained-counter design is
only justified if this route cannot recover the evidence**, and it would have to establish survival
across re-upload rather than a software reset.

Read-only apart from the authorized take itself: no clearing, no quarantine, no rename, and no reboot.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import threading
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

IDENTITY_STATS_MARKER = "RID,stats,"

# The device's own statements that the take is under way and finished.
RECORDING_MARKERS = ("Recording started", "Loop ", "RECA,")
STOPPED_MARKERS = ("STOPPED_RECORDING", "Transport stopped", "STOPPED,PLAYING")


class Session:
    """One serial connection with one reader thread, held open for the whole run."""

    def __init__(self, port: str, log_path: Path) -> None:
        import serial  # third-party; imported here so module import stays device-free

        self._serial = serial.Serial(port, 115200, timeout=0.1)
        self._lines: list[str] = []
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._log = log_path.open("w", encoding="utf-8")
        self._thread = threading.Thread(target=self._read, daemon=True)
        self._thread.start()

    def _read(self) -> None:
        while not self._stop.is_set():
            try:
                raw = self._serial.readline()
            except Exception:
                break
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").rstrip()
            if not line:
                continue
            with self._lock:
                self._lines.append(line)
            self._log.write(line + "\n")

    def snapshot(self) -> list[str]:
        with self._lock:
            return list(self._lines)

    def request_line(self, command: str, *, marker: str, timeout_s: float) -> tuple[list[str], bool]:
        """Send one command on the **existing** connection and wait for its response marker."""
        start = len(self.snapshot())
        self._serial.write(command.encode("ascii") + b"\n")
        self._serial.flush()
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            for line in self.snapshot()[start:]:
                if marker in line:
                    return self.snapshot()[start:], True
            time.sleep(0.05)
        return self.snapshot()[start:], False

    def close(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2.0)
        try:
            self._serial.close()
        except Exception:
            pass
        self._log.close()


def parse_identity_stats(line: str) -> dict[str, int | str]:
    """Parse the `RID,stats,...` row into a dict; unknown keys are dropped, never guessed."""
    out: dict[str, int | str] = {}
    match = re.search(r"RID,stats,(?P<body>.*)$", line)
    if not match:
        return out
    for key, value in re.findall(r"([A-Za-z_]+)=([^,]*)", match.group("body")):
        if key == "ev":
            out[key] = value
            continue
        try:
            out[key] = int(value)
        except ValueError:
            out[key] = value
    return out


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--serial-port", required=True)
    parser.add_argument("--track", type=int, required=True, help="User-facing track 1-8 (approved slot).")
    parser.add_argument("--loop-slot", type=int, required=True, help="User-facing loop slot 1-8.")
    parser.add_argument("--record-bars", type=int, default=2)
    parser.add_argument("--bpm", type=float, default=120.0)
    parser.add_argument("--settle-ms", type=int, default=1200)
    parser.add_argument("--press-ms", type=int, default=90)
    parser.add_argument("--stats-timeout-s", type=float, default=4.0)
    parser.add_argument("--stopped-timeout-s", type=float, default=6.0)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    out_dir: Path = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    import mido
    import serial  # noqa: F401  (import before sys.path shadowing matters)
    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        GLOBAL_TRANSPORT_NOTE,
        LOOP_SELECT_NOTE_BASE,
        MIDI_CLOCKS_PER_BAR,
        RECORD_BUTTON_NOTE,
        TRACK_SELECT_NOTE_BASE,
    )
    from hitl.midi_io import _send_short_press

    session = Session(args.serial_port, out_dir / "session.log")
    out = mido.open_output("Teensy MIDI")
    result: dict[str, object] = {
        "pinned_track_user": args.track,
        "pinned_slot_user": args.loop_slot,
        "pinned_track_index": args.track - 1,
        "pinned_slot_index": args.loop_slot - 1,
        "reboot_allowed": False,
    }

    def press(note: int, ms: int | None = None) -> None:
        _send_short_press(
            out, note=note, channel_1based=CONTROL_CHANNEL_1BASED, press_ms=ms or args.press_ms
        )

    def wait_for(markers: tuple[str, ...], timeout_s: float, start: int) -> bool:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            for line in session.snapshot()[start:]:
                if any(marker in line for marker in markers):
                    return True
            time.sleep(0.05)
        return False

    try:
        time.sleep(1.0)

        # (2) Baseline counters, first thing, on the connection we will keep.
        base_lines, base_ok = session.request_line(
            "!CAP_IDENTITY_STATS", marker=IDENTITY_STATS_MARKER, timeout_s=args.stats_timeout_s
        )
        base_stats = next(
            (parse_identity_stats(l) for l in base_lines if IDENTITY_STATS_MARKER in l), {}
        )
        result["baseline_stats_answered"] = base_ok
        result["baseline_stats"] = base_stats
        print(f"[one-shot] baseline counters answered={base_ok} {base_stats}")

        # (3) The take, on the pinned slot.
        press(TRACK_SELECT_NOTE_BASE + (args.track - 1))
        time.sleep(args.settle_ms / 1000.0)
        press(LOOP_SELECT_NOTE_BASE + (args.loop_slot - 1))
        time.sleep(args.settle_ms / 1000.0)
        arm_index = len(session.snapshot())
        press(RECORD_BUTTON_NOTE)
        result["take_started"] = wait_for(RECORDING_MARKERS, 3.0, arm_index)
        print(f"[one-shot] take started={result['take_started']}")

        period = 60.0 / (args.bpm * 24.0)
        clocks = int(args.record_bars * MIDI_CLOCKS_PER_BAR)
        for i in range(clocks):
            out.send(mido.Message.from_bytes([0xF8]))  # MIDI clock
            if i % 6 == 0:
                note = 48 + ((i // 6) % 12)
                out.send(mido.Message("note_on", channel=5, note=note, velocity=100))
                time.sleep(0.02)
                out.send(mido.Message("note_off", channel=5, note=note, velocity=0))
            time.sleep(period)

        press(RECORD_BUTTON_NOTE)
        result["record_stop_markers"] = wait_for(STOPPED_MARKERS, 4.0, arm_index)

        # (4) Stop the transport, confirm stopped, then read the counters again on the SAME connection.
        press(GLOBAL_TRANSPORT_NOTE)
        stopped_index = len(session.snapshot())
        result["transport_stopped_confirmed"] = wait_for(
            ("Transport stopped", "ST,Track,PLAYING,STOPPED", "STOPPED"), args.stopped_timeout_s,
            stopped_index,
        )
        print(f"[one-shot] transport stopped confirmed={result['transport_stopped_confirmed']}")

        after_lines, after_ok = session.request_line(
            "!CAP_IDENTITY_STATS", marker=IDENTITY_STATS_MARKER, timeout_s=args.stats_timeout_s
        )
        after_stats = next(
            (parse_identity_stats(l) for l in after_lines if IDENTITY_STATS_MARKER in l), {}
        )
        result["after_stats_answered"] = after_ok
        result["after_stats"] = after_stats
        print(f"[one-shot] after-take counters answered={after_ok} {after_stats}")

        if after_ok and base_stats and after_stats:
            delta = {
                key: after_stats.get(key, 0) - base_stats.get(key, 0)
                for key in ("entered", "enq", "rej", "tx", "evict")
            }
            result["counter_delta"] = delta
            print(f"[one-shot] counter delta: {delta}")
            print(
                "[one-shot] reading: "
                + (
                    "the emit branch RAN (entered > 0)"
                    if delta.get("entered", 0) > 0
                    else "the emit branch was NOT reached (entered == 0) on this take"
                )
            )
        else:
            telemetry_continues = any("[Memory] ext" in l for l in session.snapshot()[stopped_index:])
            result["reception_evidence"] = {
                "baseline_request_answered": base_ok,
                "transport_stopped_confirmed": result["transport_stopped_confirmed"],
                "telemetry_continuing_after_stop": telemetry_continues,
            }
            print(
                "[one-shot] second request FAILED on the same connection. Reception evidence: "
                f"{result['reception_evidence']}. Next: separate command reception, parsing and "
                "stopped-state admission."
            )
    finally:
        # (5) Reboot stays withheld: nothing here reboots, clears or renames.
        result["session_line_count"] = len(session.snapshot())
        (out_dir / "one-session-result.json").write_text(
            json.dumps(result, indent=2), encoding="utf-8"
        )
        print(f"[one-shot] wrote {out_dir / 'one-session-result.json'}")
        out.close()
        session.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
