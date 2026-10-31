#!/usr/bin/env python3
"""DEC-230 acceptance probe: full view preparation while the external clock/transport runs.

Fresh boot, select the authorized scratch track/slot, start the host clock pump so the restored loop
plays, then issue the scratch slot's CLEAR long-press while the pump is still running. The clear is
the preparation trigger; if it is refused (post-load holdoff) the loop keeps playing, and if it is
accepted the transport/clock still runs. The script waits for the device's own
`view_prewarm_step,...,ready=1` row and correlates the preparation with `midi_gap`, `extclk_arrival`
and `late_route` from the same capture window.

No firmware change; no allocator work. The scratch slot is the only slot touched.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
import time

import mido

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from hitl.actions.slots import DEFAULT_LONG_PRESS_MS  # noqa: E402
from hitl.control_constants import (  # noqa: E402
    CONTROL_CHANNEL_1BASED,
    LOOP_SELECT_NOTE_BASE,
    TRACK_SELECT_NOTE_BASE,
)
from hitl.midi_clock import MidiClockMaster  # noqa: E402
from hitl.midi_io import _find_midi_port, _send_long_press, _send_short_press  # noqa: E402
from hitl_midifile_chain import _default_capture_log, _read_log_since  # noqa: E402


def _press(out_port, note: int, press_ms: int = 80) -> None:
    _send_short_press(out_port, note=note, channel_1based=CONTROL_CHANNEL_1BASED,
                      press_ms=press_ms, _caller="hitl_view_prewarm_active")


def _max_field(rows: list[str], tag: str) -> tuple[int, str] | None:
    best = None
    for row in rows:
        if f",DIAG,{tag}," not in row:
            continue
        fields = row.split(",")
        try:
            value = int(fields[4])
        except (IndexError, ValueError):
            continue
        if best is None or value > best[0]:
            best = (value, row.strip())
    return best


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--track", type=int, default=7, help="0-based scratch track (default 7)")
    parser.add_argument("--slot", type=int, default=4, help="1-based scratch slot (default 4)")
    parser.add_argument("--bpm", type=float, default=120.0)
    parser.add_argument("--settle-s", type=float, default=2.0,
                        help="playback time before the clear long-press")
    parser.add_argument("--timeout-s", type=float, default=20.0,
                        help="wait for the preparation's ready row")
    parser.add_argument("--port-substring", default="Teensy")
    args = parser.parse_args()

    capture_log = _default_capture_log()
    if capture_log is None or not capture_log.exists():
        print("no active capture log (.current_session); start capture_session.py first", flush=True)
        return 2
    offset = capture_log.stat().st_size
    print(f"capture log: {capture_log}", flush=True)

    in_name = _find_midi_port(args.port_substring, is_input=True, timeout_s=30)
    out_name = _find_midi_port(args.port_substring, is_input=False, timeout_s=30)
    print(f"midi out: {out_name}", flush=True)
    with mido.open_output(out_name) as out_port:
        master = MidiClockMaster(out_port)
        try:
            # Select the authorized scratch track/slot before starting playback.
            _press(out_port, TRACK_SELECT_NOTE_BASE + args.track)
            time.sleep(0.25)
            _press(out_port, LOOP_SELECT_NOTE_BASE + (args.slot - 1))
            time.sleep(0.25)
            # External clock + transport: the restored scratch loop plays.
            master.start(bpm=args.bpm)
            print(f"transport started; playing track {args.track} slot {args.slot}", flush=True)
            time.sleep(max(args.settle_s, 0.2))
            # The clear is the preparation trigger. Keep the clock pump running through it.
            _send_long_press(out_port, note=LOOP_SELECT_NOTE_BASE + (args.slot - 1),
                             channel_1based=CONTROL_CHANNEL_1BASED,
                             press_ms=DEFAULT_LONG_PRESS_MS)
            print("clear long-press sent with the clock pump running", flush=True)

            deadline = time.monotonic() + max(args.timeout_s, 1.0)
            text = ""
            ready = False
            while time.monotonic() < deadline:
                chunk, offset = _read_log_since(capture_log, offset)
                text += chunk
                if re.search(r"DIAG,view_prewarm_step,.*ready=1", text):
                    ready = True
                    break
                time.sleep(0.1)
            # Give the 5 s telemetry family one window to publish the maxima around the prep.
            time.sleep(5.5)
            chunk, offset = _read_log_since(capture_log, offset)
            text += chunk
        finally:
            master.stop()
    rows = [line for line in text.splitlines() if line.startswith("#CAP,")]
    steps = [line for line in rows if "DIAG,view_prewarm_step," in line]
    print(f"preparation ready: {ready}; step rows: {len(steps)}", flush=True)
    for line in steps:
        print("  " + line.strip(), flush=True)
    for tag in ("midi_gap", "late_on", "extclk_arrival"):
        best = _max_field(rows, tag)
        print(f"{tag} max: {best[0] if best else 'n/a'} us", flush=True)
        if best:
            print("  " + best[1], flush=True)
    late_routes = [line for line in rows if ",DIAG,late_route," in line]
    best_route = None
    for line in late_routes:
        fields = line.split(",")
        values = [int(value) for value in fields[4:] if value.isdigit()]
        value = max(values) if values else 0
        if best_route is None or value > best_route[0]:
            best_route = (value, line)
    print(f"late_route max: {best_route[0] if best_route else 'n/a'} us", flush=True)
    if best_route:
        print("  " + best_route[1].strip(), flush=True)
    # Playback state evidence at/around the window.
    paint = [line for line in text.splitlines() if line.startswith("#PAINT_BEAT")]
    if paint:
        print("last paint rows:", flush=True)
        for line in paint[-3:]:
            print("  " + line.strip(), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
