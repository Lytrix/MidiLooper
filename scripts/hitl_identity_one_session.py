#!/usr/bin/env python3
"""Read one take's identity and optional stored-payload parity in one session.

Identity alone never authorizes a reboot. With --decoder, two identical SD reads
must match the stopped RAM events first. An unanswered command or an outstanding
ring row has no inferred cause. The native decoder adapts Teensy event storage in
legacy and LSV1 files; pointer-bearing SysEx is rejected.
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
    parser.add_argument("--boot-settle-s", type=float, default=0, help="Keep reader open during boot settling.")
    parser.add_argument("--decoder", type=Path, help="Production native slot-body decoder executable.")
    parser.add_argument("--stop-before-baseline", action="store_true",
                        help="Send MIDI Stop before reading the stopped-only baseline; never toggle.")
    parser.add_argument("--record-bars", type=int, default=2)
    parser.add_argument("--bpm", type=float, default=120.0)
    parser.add_argument("--settle-ms", type=int, default=1200)
    parser.add_argument("--press-ms", type=int, default=90)
    parser.add_argument("--stats-timeout-s", type=float, default=4.0)
    parser.add_argument("--stopped-timeout-s", type=float, default=6.0)
    return parser


def growth_slot_obstacle(observation, track, slot):
    """Require a fully observed, settled empty target; existing empty payloads are allowed.

    consistent_view=false means multi-turn collection in the firmware; invalidated rows and
    pending requests are the relevant exclusions, not that non-atomic-window flag alone.
    """
    if not observation.get("complete") or len(observation.get("slots", [])) != 64:
        return "library_observation_incomplete"
    rows = observation["slots"]
    if len({(r["track"], r["slot"]) for r in rows}) != 64:
        return "library_observation_duplicate_slots"
    if not observation.get("window", {}).get("admission_complete"):
        return "restore_admission_pending"
    if any(r.get("invalidated") or r.get("request") in ("queued", "running", "waiting") for r in rows):
        return "restore_or_observation_unsettled"
    target = next((r for r in rows if (r["track"], r["slot"]) == (track, slot)), None)
    if target is None or not target.get("content_representation_observed"):
        return "target_content_unobservable"
    if target["payload"] not in ("absent", "present_verified"):
        return "target_payload_unverified"
    if (target["has_committed_content"] or target["loop_length_ticks"]
            or target["committed_event_count"] or target["committed_pass_count"]):
        return "target_already_populated"
    return None


def run_fixture(args, session, press, stream, *, stop_transport=None, start_transport=None, pulse_clock=None,
                sleep=time.sleep, monotonic=time.monotonic):
    """Production orchestration, with injected I/O for device-free transition tests."""
    from hitl.control_constants import (
        TRACK_SELECT_NOTE_BASE, LOOP_SELECT_NOTE_BASE, RECORD_BUTTON_NOTE,
        GLOBAL_TRANSPORT_NOTE,
    )
    result = {"pinned_track_index": args.track - 1, "pinned_slot_index": args.loop_slot - 1,
              "reboot_allowed": False, "stored_payload_verified": False, "verdict": "inconclusive"}
    controls_started = False
    def fail(reason):
        if controls_started and stop_transport is not None:
            stop_transport()
            result["failure_stop_sent"] = True
            result["failure_identity_stats"] = stats()
        result["reason"] = reason
        return result
    def stats():
        lines, ok = session.request_line("!CAP_IDENTITY_STATS", marker=IDENTITY_STATS_MARKER,
                                         timeout_s=args.stats_timeout_s)
        return next((parse_identity_stats(l) for l in lines if IDENTITY_STATS_MARKER in l), {}) if ok else {}
    def wait(predicate, start, timeout, clock=False):
        deadline = monotonic() + timeout
        next_clock = monotonic()
        while monotonic() < deadline:
            if clock and pulse_clock is not None and monotonic() >= next_clock:
                pulse_clock()
                next_clock = monotonic() + 60.0 / (args.bpm * 24)
            for line in session.snapshot()[start:]:
                value = predicate(line)
                if value:
                    return value
            sleep(0.01)
        return None
    if getattr(args, "stop_before_baseline", False):
        if stop_transport is None:
            return fail("explicit_stop_unavailable")
        stop_transport()
        sleep(args.settle_ms / 1000)
    base = stats()
    result["baseline_stats"] = base
    required = ("entered", "enq", "rej", "tx", "evict", "take")
    if not all(isinstance(base.get(k), int) for k in required):
        return fail("baseline_identity_unavailable")
    from hitl.library_observation import parse_observation_lines
    lines, complete = session.request_line("!LIB_OBSERVE", marker="LIBOBS,E,", timeout_s=30)
    observation = parse_observation_lines(lines)
    result["preflight_observation"] = observation
    reason = growth_slot_obstacle(observation, args.track - 1, args.loop_slot - 1)
    if not complete or reason:
        return fail(reason or "library_observation_incomplete")
    controls_started = True
    press(TRACK_SELECT_NOTE_BASE + args.track - 1)
    sleep(args.settle_ms / 1000)
    edge = len(session.snapshot())
    press(LOOP_SELECT_NOTE_BASE + args.loop_slot - 1)
    sleep(args.settle_ms / 1000)
    def fresh_start(line):
        if "RID,start," not in line:
            return None
        fields = parse_identity_stats(line.replace("RID,start,", "RID,stats,", 1))
        return fields if isinstance(fields.get("take"), int) and fields["take"] == (base["take"] + 1) & 0xffffffff else None
    def recording_start(line):
        if "#MEM_DENIED," in line and "op=record_start," in line:
            return {"record_start_refusal": line}
        identity = fresh_start(line)
        if identity:
            return identity
        if "Recording started @ tick" in line:
            # Provisional only. The stopped read-back must later attest the exact next take.
            return {"take": (base["take"] + 1) & 0xffffffff,
                    "t": args.track - 1, "ss": args.loop_slot - 1, "provisional": True}
        return None
    from hitl.serial.protocol import parse_track_transition
    def arm_or_start(line):
        start = recording_start(line)
        if start:
            return start
        transition = parse_track_transition(line)
        return "armed" if transition and transition[1] == "ARMED" else None
    # Empty-slot selection can arm or start recording itself. Do not cancel that arm.
    observed = [arm_or_start(l) for l in session.snapshot()[edge:]]
    start = next((value for value in reversed(observed) if value), None)
    if start is None and any("Overdubbing started" in l for l in session.snapshot()[edge:]):
        return fail("selection_started_overdub")
    if start is None:
        edge = len(session.snapshot())
        press(RECORD_BUTTON_NOTE)
        start = wait(arm_or_start, edge, args.stopped_timeout_s)
    if start == "armed":
        edge = len(session.snapshot())
        if start_transport is not None:
            start_transport()
        else:
            press(GLOBAL_TRANSPORT_NOTE)
        start = wait(recording_start, edge, args.stopped_timeout_s, clock=True)
    if start is None:
        return fail("fresh_record_start_unobserved")
    if "record_start_refusal" in start:
        result["record_start_refusal"] = start["record_start_refusal"]
        return fail("record_start_refused")
    result["start_identity"] = start
    if (start.get("t"), start.get("ss")) != (args.track - 1, args.loop_slot - 1):
        return fail("record_start_wrong_slot")
    result["note_stream_started"] = True
    stream()
    result["note_stream_completed"] = True
    edge = len(session.snapshot())
    press(RECORD_BUTTON_NOTE)
    if not wait(lambda l: "Recording stopped @ tick" in l, edge, args.stopped_timeout_s, clock=True):
        return fail("record_stop_unobserved")
    edge = len(session.snapshot())
    if stop_transport is not None:
        stop_transport()
    else:
        press(GLOBAL_TRANSPORT_NOTE)
    # A Track STOPPED transition is not global transport confirmation.
    result["transport_stop_log_observed"] = bool(wait(
        lambda l: "Transport stopped" in l, edge, args.stopped_timeout_s))
    after = stats()  # Firmware admits this read-back only while global transport is stopped.
    result["after_stats"] = after
    if not all(isinstance(after.get(k), int) for k in required):
        return fail("stopped_after_identity_unavailable")
    result["transport_stopped_confirmed"] = True
    result["counter_delta"] = {k: after[k] - base[k] for k in required if k != "take"}
    expected = {"take": start["take"], "t": args.track - 1, "ss": args.loop_slot - 1,
                "sp": args.loop_slot - 1, "cs": args.loop_slot - 1,
                "pt": args.track - 1, "ps": args.loop_slot - 1, "ev": "a"}
    if any(after.get(k) != v for k, v in expected.items()):
        return fail("take_admission_identity_mismatch_or_missing")
    if result["counter_delta"]["entered"] < 2 or any(v < 0 for v in result["counter_delta"].values()):
        return fail("counter_episode_inconsistent")
    result["untransmitted_or_evicted_total"] = after["enq"] - after["tx"]
    lines, complete = session.request_line("!CAPTURE_CONTENT", marker="CAPTURE_CONTENT,end", timeout_s=15)
    result["ram_content_dump_complete"] = complete
    result["ram_content_dump"] = [l for l in lines if "CAPTURE_CONTENT," in l]
    # This dump reads RAM passes, not the SD payload. It cannot authorize a reboot.
    result["verdict"] = "identity_pass"
    return result


def stream_take(args, out, message, *, sleep=time.sleep, monotonic=time.monotonic):
    from hitl.control_constants import MIDI_CLOCKS_PER_BAR
    period = 60.0 / (args.bpm * 24)
    deadline = monotonic()
    total = args.record_bars * MIDI_CLOCKS_PER_BAR
    step = getattr(args, 'note_step_clocks', 6)
    gate = getattr(args, 'note_gate_clocks', 2)
    low = getattr(args, 'note_low', 48)
    count = getattr(args, 'note_count', 12)
    if not (0 < gate <= step and 0 <= low < 128 and 0 < count <= 128-low):
        raise ValueError('invalid_note_workload')
    for i in range(total):
        out.send(message("clock"))
        if i >= gate and (i-gate) % step == 0:
            note = low + ((i-gate) // step) % count
            out.send(message("note_off", channel=args.track - 1, note=note, velocity=0))
        if i % step == 0 and i + gate < total:
            note = low + (i // step) % count
            out.send(message("note_on", channel=args.track - 1, note=note, velocity=100))
        deadline += period
        sleep(max(0, deadline - monotonic()))


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if not (1 <= args.track <= 8 and 1 <= args.loop_slot <= 8 and args.record_bars > 0
            and 0 < args.bpm <= 300 and args.settle_ms >= 0 and args.press_ms > 0
            and args.stats_timeout_s > 0 and args.stopped_timeout_s > 0 and 0 <= args.boot_settle_s <= 60):
        parser.error("invalid track, slot or timing bounds")
    import mido
    from hitl.control_constants import CONTROL_CHANNEL_1BASED
    from hitl.midi_io import _send_short_press
    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    result = {"verdict": "inconclusive", "reboot_allowed": False}
    session = Session(args.serial_port, out_dir / "session.log")
    try:
        time.sleep(args.boot_settle_s)
        with mido.open_output("Teensy MIDI") as out:
            def press(note):
                _send_short_press(out, note=note, channel_1based=CONTROL_CHANNEL_1BASED,
                                  press_ms=args.press_ms)
            try:
                result = run_fixture(args, session, press, lambda: stream_take(args, out, mido.Message),
                                     stop_transport=lambda: out.send(mido.Message("stop")),
                                     start_transport=lambda: out.send(mido.Message("start")),
                                     pulse_clock=lambda: out.send(mido.Message("clock")))
                if result["verdict"] == "identity_pass" and args.decoder:
                    from hitl.slot_payload_readback import verify_saved_slot, ReadbackError
                    try:
                        verification = verify_saved_slot(session, args.track-1, args.loop_slot-1,
                                                         result["ram_content_dump"], args.decoder, out_dir)
                        result["stored_payload"] = verification
                        result["stored_payload_verified"] = True
                        result["reboot_allowed"] = True
                    except ReadbackError as exc:
                        result["verdict"] = "inconclusive"
                        result["reason"] = str(exc)
            except BaseException as exc:
                # Never leave a take running because host I/O or interruption escaped orchestration.
                out.send(mido.Message("stop"))
                result["reason"] = "host_exception"
                result["error"] = str(exc)
                result["failure_stop_sent"] = True
                raise
    finally:
        result["session_line_count"] = len(session.snapshot())
        session.close()
        (out_dir / "one-session-result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(json.dumps(result, indent=2))
    return 0 if result["verdict"] == "identity_pass" else 2


if __name__ == "__main__":
    raise SystemExit(main())
