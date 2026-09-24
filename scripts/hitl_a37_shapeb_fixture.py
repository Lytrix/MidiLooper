#!/usr/bin/env python3
"""A3.7 shape (b) fixture: restore in flight + save owed at the CLEAR edge, asserted not assumed.

Owner direction (2026-09-24): reload-on-clear stays disabled, so the restore in flight must come from an
**intended** entry point — here the boot restore queue, which the deferred load frame services while the
boot admission scan is still running. The fixture records a take on the authorized scratch slot so a save
is owed and a second track is audibly playing, then presses CLEAR on the scratch slot.

The verdict is gated on four sampled preconditions; a run that cannot hold them is INCONCLUSIVE, never
counted as shape (b):

  1. the CLEAR target's content exists immediately before the press (loop-length feedback > 0);
  2. a restore has **actually started** — an `RRQ … running` with no terminal before the press, or a
     `LoadLoopJob` still active at the press (a `queued` request does NOT satisfy this);
  3. a save is owed — `Clear waiting for deferred save completion`;
  4. another track is audibly playing at the press — a track other than the target in PLAYING, which the
     CLEAR does not stop.

Usage:
  .venv/bin/python scripts/hitl_a37_shapeb_fixture.py --out-dir captures/a37fix-01
"""
from __future__ import annotations

import argparse
import json
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
UPTIME_RE = re.compile(r"^\[(\d+\.\d+)\]")
RRQ_RE = re.compile(r"RRQ,(\d+),(\d+),(\d+),(\d+),(\w+),(\w+),(\w+)")
LENGTH_RE = re.compile(r"Sent loop length CC feedback: (?:length=0|(\d+) bars)")


class SerialLog:
    """Line reader that keeps a monotonic host timestamp beside each device line."""

    def __init__(self, port: str, path: Path) -> None:
        self.lines: list[tuple[float, str]] = []
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
            self.lines.append((time.monotonic(), line))
            self._handle.write(line + "\n")
            self._handle.flush()

    def mark(self) -> int:
        return len(self.lines)

    def since(self, mark: int) -> list[str]:
        return [l for _, l in self.lines[mark:]]

    def all(self) -> list[str]:
        return [l for _, l in self.lines]

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


def analyse(lines: list[str], clear_index: int, target_track_1based: int,
            other_playing: bool) -> dict[str, object]:
    """Check the four preconditions from the captured stream around the CLEAR index."""
    before = lines[:clear_index]
    after = lines[clear_index:]

    # (1) target content exists immediately before the press: the last length feedback before CLEAR.
    last_len = None
    for line in reversed(before):
        m = LENGTH_RE.search(line)
        if m:
            last_len = int(m.group(1)) if m.group(1) else 0
            break
    content_exists = bool(last_len and last_len > 0)

    # (2) a restore actually started and had not terminated before the press. `running` or a named
    #     `waiting_*` both prove it started; a merely `queued` request does not.
    open_requests: dict[tuple[int, int], str] = {}
    for line in before:
        m = RRQ_RE.search(line)
        if not m:
            continue
        key = (int(m.group(1)), int(m.group(2)))
        state = m.group(3)
        if state == "running":
            open_requests[key] = "running"
        elif state == "waiting":
            wait = re.search(r",waiting,([a-z_]+),", line)
            open_requests[key] = wait.group(1) if wait else "waiting"
        elif state == "terminal":
            open_requests.pop(key, None)
    restore_started = len(open_requests) > 0
    job_lines_before = [l for l in before if "LoadLoopJob" in l]

    # (3) a save was owed: the CLEAR took the drain path.
    save_owed = any("Clear waiting for deferred save completion" in l for l in after)

    # (4) another track audibly playing: a `Playback started` after the other take, with no
    #     `PLAYING -> STOPPED/EMPTY` transition between it and the press. The capture's `ST,Track` line
    #     carries no track index, so the sequence is the evidence (the CLEAR's own stop is at/after the
    #     press index and is excluded).
    last_start = None
    last_stop = None
    for i, line in enumerate(before):
        if "Playback started" in line:
            last_start = i
        if re.search(r"ST,Track,PLAYING,(STOPPED|EMPTY)", line):
            last_stop = i
    sequence_playing = last_start is not None and (last_stop is None or last_stop < last_start)
    other_playing_measured = bool(other_playing) or sequence_playing

    named_wait = [l for l in after if "Clear refused" in l]
    completed = any("State saved successfully" in l for l in after)
    aborted = [l for l in after if "Clear aborted" in l]
    selected_other = any(f"Selected track {target_track_1based}" in l for l in before)

    return {
        "content_exists_before_clear": content_exists,
        "target_length_bars_before_clear": last_len,
        "restore_started_before_clear": restore_started,
        "open_restore_requests_at_clear": [f"{t+1}/{s+1}" for (t, s) in open_requests],
        "load_loop_job_lines_before_clear": len(job_lines_before),
        "save_owed": save_owed,
        "other_track_playing": other_playing_measured,
        "other_track_playing_sequence_evidence": sequence_playing,
        "target_track_selected_before_clear": selected_other,
        "named_wait": named_wait[:3],
        "save_completed": completed,
        "clear_aborted": aborted[:3],
    }


UPTIME_LINE_RE = re.compile(r"^\[(\d+\.\d+)\]")
CAP_MICROS_RE = re.compile(r"^#CAP,(\d+),")


def device_time(line: str) -> float | None:
    """Device time in seconds for a captured line, or None.

    The `#CAP` lines are flushed from a PSRAM ring, so their **file order is not time order**: a ring line
    written at 10.6 s can appear after a prompt line written at 27.1 s. Every ordering decision in the
    analysis must use device time, not the line index -- getting this wrong inverted precondition 2 on the
    first analysed run.
    """
    m = CAP_MICROS_RE.match(line)
    if m:
        return int(m.group(1)) / 1_000_000.0
    m = UPTIME_LINE_RE.match(line)
    if m:
        return float(m.group(1))
    return None


MO_RE = re.compile(r"^#CAP,(\d+),MO,(\d+),(\d+),(\d+),(\d+)")


MO_RE = re.compile(r"^#CAP,(\d+),MO,(\d+),(\d+),(\d+),(\d+)")


def analyse_log(path: Path, target_track_1based: int, num_tracks: int = 8) -> dict[str, object]:
    """Analyse one captured run read-only, in **chronological** order.

    Two ordering facts drive this:
      * `#CAP` lines are flushed from a PSRAM ring, so file order is not time order -- a ring line written
        at 10.6 s can appear after a prompt line written at 27.1 s. Request transitions and the target-length
        reading must therefore be iterated in device-time order, not merely partitioned by timestamp.
      * The direct `[StorageManager]` prints carry no device time; they fall back to file position relative
        to the drain line, whose order is reliable.

    Precondition 2 requires an **observed `running` transition for the same full request identity**
    `(track, slot, requestGeneration, setEpoch)`: a named wait alone does not prove the restore ever started,
    and attribution without the generation/epoch could match a superseded request. Playback attribution uses
    the **MIDI output channel** (`MO`), because `ST,Track` carries no track index.
    """
    raw = [l.rstrip("\n") for l in path.read_text(encoding="utf-8", errors="replace").splitlines()]
    clear_file_index = None
    for i, l in enumerate(raw):
        if "Clear waiting for deferred save completion" in l:
            clear_file_index = i
            break
    if clear_file_index is None:
        return {"error": "no CLEAR drain line found; the CLEAR did not take the drain path"}

    # Chronological event list: device time where available, stable within equal times.
    events = sorted(((t, i, l) for i, (t, l) in enumerate((device_time(x), x) for x in raw)
                     if t is not None), key=lambda e: (e[0], e[1]))
    clear_time = None
    for t, i, l in events:
        if i == clear_file_index:
            clear_time = t
            break
    if clear_time is None:  # the drain line had no device time; fall back to the nearest earlier event
        clear_time = max((t for t, i, _ in events if i < clear_file_index), default=0.0)

    before = [(t, i, l) for t, i, l in events if (t < clear_time) or (t == clear_time and i <= clear_file_index)]
    after = [(t, i, l) for t, i, l in events if (t, i) not in {(b[0], b[1]) for b in before}]

    # (1) target content immediately before the press, in chronological order.
    last_len = None
    for t, i, l in before:
        m = LENGTH_RE.search(l)
        if m:
            last_len = int(m.group(1)) if m.group(1) else 0
    content_exists = bool(last_len and last_len > 0)

    # (2) request identity and state, chronological, keyed by the full identity.
    seen_running: set[tuple[int, int, int, int]] = set()
    open_requests: dict[tuple[int, int, int, int], str] = {}
    for t, i, l in before:
        m = RRQ_RE.search(l)
        if not m:
            continue
        ident = (int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)))
        state, wait, terminal = m.group(5), m.group(6), m.group(7)
        if state == "running":
            seen_running.add(ident)
            open_requests[ident] = "running"
        elif state == "waiting":
            open_requests[ident] = wait
        elif state == "terminal":
            open_requests.pop(ident, None)
    # started AND unresolved: an observed `running` for an identity with no terminal before the press.
    unresolved = {k: v for k, v in open_requests.items() if k in seen_running}
    not_started = {k: v for k, v in open_requests.items() if k not in seen_running}

    # (4) another track playing across the edge, attributed by MIDI output channel. Only real track
    #     channels count: the control channel (16) also carries `MO` traffic (button/CC echoes) and is not
    #     a track, and the target's own channel is excluded by definition.
    target_channel = target_track_1based
    control_channel = 16
    notes_before: dict[int, int] = {}
    notes_after: dict[int, int] = {}
    after_keys = {(a[0], a[1]) for a in after}
    for t, i, l in before + after:
        m = MO_RE.match(l)
        if not m or int(m.group(2)) not in (128, 144):
            continue
        ch = int(m.group(3))
        if ch == control_channel or ch > num_tracks:
            continue
        bucket = notes_after if (t, i) in after_keys else notes_before
        bucket[ch] = bucket.get(ch, 0) + 1
    other_channels = sorted(ch for ch in notes_before
                            if ch != target_channel and notes_after.get(ch, 0) > 0)
    control_notes_before = 0  # reported implicitly by exclusion; kept out of the verdict

    def ident_str(k: tuple[int, int, int, int]) -> str:
        return f"track{k[0]+1}/slot{k[1]+1}/gen{k[2]}/epoch{k[3]}"

    after_untimed = [l for i, l in enumerate(raw) if i > clear_file_index]
    refused = [l for l in after_untimed if "Clear refused" in l]
    completed = any("State saved successfully" in l for l in after_untimed)
    # A3.7 shape (b) accepts completion or the named commit-gate / job-capacity wait. A capture-block
    # refusal (`transport_active`) means a track was recording, which is not the shape under test.
    accept_wait = any(("waiting_commit_gate" in l or "waiting_job_capacity" in l) for l in refused)
    if completed:
        outcome = "completed"
    elif accept_wait:
        outcome = "named_wait"
    elif refused:
        outcome = "refused_other"
    else:
        outcome = "unknown"
    return {
        "clear_drain_at_s": clear_time,
        "content_exists_before_clear": content_exists,
        "target_length_bars_before_clear": last_len,
        "restore_started_before_clear": len(unresolved) > 0,
        "unresolved_requests": {ident_str(k): v for k, v in unresolved.items()},
        "open_but_never_ran_requests": {ident_str(k): v for k, v in not_started.items()},
        "save_owed": True,   # reaching the drain line is the evidence: handleClearTrack only drains if owed
        "other_track_playing": len(other_channels) > 0,
        "other_track_channels_notes_before": {ch: notes_before[ch] for ch in other_channels},
        "other_track_channels_notes_after": {ch: notes_after[ch] for ch in other_channels},
        "target_channel": target_channel,
        "target_channel_notes_after_clear": notes_after.get(target_channel, 0),
        "save_completed": completed,
        "outcome": outcome,
        "named_wait": refused[:3],
        "clear_aborted": [l for l in after_untimed if "Clear aborted" in l][:3],
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--port", default="/dev/cu.usbmodem154944801")
    ap.add_argument("--scratch-track", type=int, default=5)
    ap.add_argument("--scratch-slot", type=int, default=1)
    ap.add_argument("--other-track", type=int, default=6)
    ap.add_argument("--other-slot", type=int, default=1)
    ap.add_argument("--other-bars", type=float, default=2.0)
    ap.add_argument("--other-channel", type=int, default=6)
    ap.add_argument("--midi-channel", type=int, default=5)
    ap.add_argument("--tracks", type=int, default=8, help="Track count for MO channel attribution.")
    ap.add_argument("--bpm", type=float, default=120.0)
    ap.add_argument("--record-bars", type=float, default=2.0)
    ap.add_argument("--start-s", type=float, default=3.0,
                    help="Seconds after the boot banner before the shape starts (boot restore window).")
    ap.add_argument("--clear-delay-ms", type=int, default=200)
    ap.add_argument("--clear-press-ms", type=int, default=1400)
    ap.add_argument("--settle-ms", type=int, default=8000)
    ap.add_argument("--no-upload", action="store_true")
    ap.add_argument("--analyse-log", type=Path, default=None,
                    help="Read-only: analyse an existing session.log and exit (no device access).")
    args = ap.parse_args()

    if args.analyse_log is not None:
        result = analyse_log(args.analyse_log, args.scratch_track, args.tracks)
        print(json.dumps(result, indent=2))
        pre = [result.get("content_exists_before_clear"), result.get("restore_started_before_clear"),
               result.get("save_owed"), result.get("other_track_playing")]
        if not all(pre):
            print("PRECONDITIONS NOT HELD")
        elif result.get("outcome") not in ("completed", "named_wait"):
            print(f"PRECONDITIONS HELD BUT OUTCOME={result.get('outcome')!r} -> NOT SHAPE (b)")
        else:
            print(f"SHAPE (b) TAKEN: outcome={result.get('outcome')}")
        return 0

    out_dir: Path = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=False)

    if not args.no_upload:
        print("[a37fix] re-uploading for a fresh boot")
        subprocess.run(["pio", "run", "-e", "teensy41-capture-serial", "-t", "upload"],
                       capture_output=True, timeout=240, check=False, cwd=str(REPO_ROOT))
        port = wait_for_port()
        if port is None:
            print("[a37fix] no port after upload")
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

    def record_take(track: int, slot: int, bars: float, channel_1based: int,
                    start_transport: bool = False) -> None:
        """Select the loop (a short press on a stopped track starts recording) and record `bars`.

        Stopping with a short RECORD press returns the track to PLAYING -- that is the measured way to
        start playback here (owner: "start playback is not long press, it is short press after recording").
        """
        send_note(TRACK_SELECT_NOTE_BASE + (track - 1), 120)
        time.sleep(0.4)
        send_note(LOOP_SELECT_NOTE_BASE + (slot - 1), 120)
        time.sleep(0.5)
        if start_transport:
            out.send(mido.Message("start"))
        # Press RECORD explicitly to start the take: relying on the loop-select press to begin recording
        # was dropped during the boot window (measured `records=0`, then a later RECORD started it).
        send_note(RECORD_BUTTON_NOTE, 120)
        time.sleep(0.3)
        total = int(round(bars * 96))
        period = 60.0 / (args.bpm * 24.0)
        for i in range(total):
            out.send(mido.Message.from_bytes([MIDI_CLOCK]))
            if i % 6 == 0:
                out.send(mido.Message("note_on", channel=channel_1based - 1,
                                      note=48 + ((i // 6) % 32), velocity=110))
                time.sleep(0.04)
                out.send(mido.Message("note_off", channel=channel_1based - 1,
                                      note=48 + ((i // 6) % 32), velocity=0))
            time.sleep(period)
        time.sleep(0.2)
        send_note(RECORD_BUTTON_NOTE, 120)   # stop recording -> the track returns to PLAYING
        time.sleep(0.6)

    other_playing = False
    try:
        # Wait for the boot banner, then start as early as the boot restore window allows.
        deadline = time.monotonic() + 20.0
        while time.monotonic() < deadline and not any("boot_reset" in l for l in log.all()):
            time.sleep(0.1)
        print(f"[a37fix] boot banner seen; starting the shape {args.start_s:.1f}s after it")
        time.sleep(args.start_s)

        # 1. Another track audibly playing: record a take on it and stop, which returns it to PLAYING.
        print(f"[a37fix] record+stop on track {args.other_track} slot {args.other_slot} "
              f"({args.other_bars} bars) so it plays")
        other_mark = log.mark()
        record_take(args.other_track, args.other_slot, args.other_bars, args.other_channel,
                    start_transport=True)
        other_window = log.since(other_mark)
        other_playing = any("PLAYING" in l for l in other_window) and not any(
            "RECORDING" in l for l in other_window[-6:])
        accidental = [l for l in other_window if "Start Recording" in l]
        stopped = [l for l in other_window if "Recording stopped" in l]
        print(f"[a37fix] other-track: records={len(accidental)} stops={len(stopped)} "
              f"playing_evidence={other_playing}")

        # 2. Owe a save on the authorized scratch slot (its own take; the stop leaves it PLAYING too).
        print(f"[a37fix] record {args.record_bars} bars on scratch {args.scratch_track}/{args.scratch_slot}")
        record_take(args.scratch_track, args.scratch_slot, args.record_bars, args.midi_channel)
        print("[a37fix] record stopped; save owed")
        time.sleep(args.clear_delay_ms / 1000.0)

        # 3. CLEAR on the scratch slot with the shape held.
        clear_index = len(log.all())
        print("[a37fix] CLEAR long press")
        send_note(RECORD_BUTTON_NOTE, args.clear_press_ms)
        time.sleep(args.settle_ms / 1000.0)
        out.send(mido.Message.from_bytes([MIDI_STOP]))
        time.sleep(1.0)

        result = analyse_log(out_dir / "session.log", args.scratch_track, args.tracks)
        result["track"] = args.scratch_track
        result["slot"] = args.scratch_slot
        result["other_track_playing_runtime_crosscheck"] = other_playing
        clear_index = clear_index  # retained for clarity; analysis is device-time ordered
        (out_dir / "fixture-verdict.json").write_text(json.dumps(result, indent=2), encoding="utf-8")

        preconditions = [
            result["content_exists_before_clear"],
            result["restore_started_before_clear"],
            result["save_owed"],
            result["other_track_playing"],
        ]
        print("[a37fix] --- verdict ---")
        for k, v in result.items():
            print(f"    {k}: {v}")
        acceptable = result.get("outcome") in ("completed", "named_wait")
        if not all(preconditions):
            print("[a37fix] INCONCLUSIVE: the fixture could not hold all four preconditions "
                  "(no shape-(b) claim)")
        elif not acceptable:
            print(f"[a37fix] INCONCLUSIVE: preconditions held but the outcome was "
                  f"{result.get('outcome')!r}, not completion or the commit-gate/job-capacity wait")
        else:
            print(f"[a37fix] SHAPE (b) TAKEN: preconditions held; outcome={result.get('outcome')}")
    finally:
        out.close()
        log.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
