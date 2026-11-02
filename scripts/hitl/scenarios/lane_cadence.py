"""Temporary window-lane cadence legs under the card's own content (DEC-246 amendment 25).

Recipe STIM-LANE-CADENCE-TEMP-01: the managed serial owner, the existing MidiClockMaster and nothing else -- **no
content gestures** (no record, edit, clear, save, load or slot change; the owner chose the set already on the card).
Each leg drives the device from the host clock at one tempo for long enough to cross at least 100 window seams of the
two-bar span, then stops. The firmware rows (`DIAG,win_gap` schema 4, `DIAG,lane_fair`, and on the measurement image
`DIAG,succ`) are the measurement; this driver only paces the clock and records what it sent. Ordinary automatic
persistence may occur. Firmware identity/upload and the canonical outer shutdown belong to the runner.
"""
from __future__ import annotations

import argparse
import json
import time

#: (bpm, seconds): 100 seams of a two-bar span is 800 beats, i.e. 267 s at 180 BPM and 200 s at 240 BPM.
LEGS = ((180, 280), (240, 210))
SETTLE_S = 5
#: The owner's "most intense" leg (2026-11-02): every track with content playing, plus a dense take recorded on the
#: authorized HITL slot (track 8, slot 4) while they play, at 300 BPM. A two-bar seam is 1.6 s, so 200 s of playing
#: after the take crosses about 125 seams.
INTENSE_BPM = 300
INTENSE_MEASURE_S = 200
INTENSE_RECORD_BARS = 32   # 25.6 s of Record at 300 BPM (about 16 seams; a longer take makes a huge loop)
INTENSE_OVERDUB_BARS = 200  # 160 s of Overdub (about 100 seams)
HITL_TRACK, HITL_SLOT = 8, 4
STEP_16TH_CLOCKS = 6

import re  # noqa: E402

_STATE_RE = re.compile(r"DIAG,disp_window,t=(\d),s=\d+,st=([A-Z_]+)")
_CONTENT_RE = re.compile(r"DIAG,(?:route_reject|disp_window),t=(\d),[^\n]*?len=(\d+)")


def track_states(lines):
    """Per 0-based track: (has content, latest display state) from the device's own rows."""
    content, state = {}, {}
    for line in lines:
        m = _CONTENT_RE.search(line)
        if m and int(m.group(2)) > 0:
            content[int(m.group(1))] = True
        m = _STATE_RE.search(line)
        if m:
            state[int(m.group(1))] = m.group(2)
    return {t: (content.get(t, False), state.get(t)) for t in range(8)}


_CAP_US_RE = re.compile(r"^#CAP,(\d+),")


def _mark(lines):
    """A phase boundary: the log position and the latest device micros written so far."""
    us = 0
    for line in reversed(lines[-400:]):
        m = _CAP_US_RE.match(line)
        if m:
            us = int(m.group(1))
            break
    return dict(line=len(lines), device_us=us)


def _wait_line(log_lines, start, patterns, timeout_s, refuse=()):
    """Wait for a new device line matching any of `patterns` after line `start`; None on a refusal or timeout."""
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        lines = log_lines()
        for line in lines[start:]:
            if any(r in line for r in refuse):
                return None
            if any(p in line for p in patterns):
                return line.strip()
        time.sleep(0.2)
    return None


_RRQ_RE = re.compile(r"^#CAP,\d+,RRQ,(\d+),(\d+),\d+,\d+,([a-z_]+),")


def _wait_restores_settled(log_lines, timeout_s=90.0, quiet_s=3.0):
    """Wait until no slot restore is queued or running (`RRQ` rows), then `quiet_s` more. The device buffers MIDI while
    it restores, so a gesture sent earlier arrives late and out of order (a long-press clear lands as a short press)."""
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        state = {}
        for line in log_lines():
            m = _RRQ_RE.match(line.strip())
            if m:
                state[(m.group(1), m.group(2))] = m.group(3)
        if state and all(v not in ("queued", "running") for v in state.values()):
            time.sleep(quiet_s)
            return True
        time.sleep(1.0)
    return False


def run_intense(action, session, capture, master, report, save, scale):
    """Three states at 300 BPM with every track that has content playing: Record, Overdub, Play.

    The take and the overdub are on the authorized HITL slot (track 8, slot 4). Every transition is confirmed from
    the device's own lines before the next gesture; a refused or unconfirmed step fails the leg by name.
    """
    from hitl.actions.slots import _slot_note, press_slot_clear, press_slot_overdub, press_slot_record, press_track
    from hitl.control_constants import CONTROL_CHANNEL_1BASED
    from hitl.transport_clock import stream_pattern_for_bars

    def log_lines():
        return capture.session_log.read_text(errors="replace").splitlines()

    bar_s = 4 * 60.0 / INTENSE_BPM
    leg = dict(bpm=INTENSE_BPM, mode="intense", phases={})
    report["legs"].append(leg)

    # 1. Stopped and settled: clear the HITL slot (a clear drains the owed save, which a playing set keeps blocked).
    leg["restores_settled"] = _wait_restores_settled(log_lines)
    press_track(action, HITL_TRACK)
    time.sleep(0.6)
    cleared = None
    for attempt in range(6):
        start = len(log_lines())
        press_slot_clear(action, HITL_SLOT)
        refused = _wait_line(log_lines, start, ("Clear refused", "Clear aborted"), 2.5)
        if refused is None:
            cleared = attempt + 1
            break
        time.sleep(2.0)
    leg["clear_attempts"] = cleared
    if cleared is None:
        raise RuntimeError("hitl_slot_clear_refused")
    save()

    # 2. Arm the take while stopped: a record start needs its slot's record storage prewarmed, and that stepped
    #    owner runs only while the transport is stopped (DEC-231 / DEC-243), so an arm under a running 300 BPM set
    #    would never start. The same rule the midifile chain uses: arm, wait for the device's own "armed", then Start.
    lines = log_lines()
    press_slot_record(action, HITL_SLOT)
    armed = _wait_line(log_lines, len(lines), ("waiting for clock to start recording",), 20.0)
    leg["armed"] = armed is not None
    if armed is None:
        raise RuntimeError("record_arm_not_confirmed")
    save()

    # 3. Start the host clock at 300 BPM: the take starts at tick 0 and every track with content plays with it.
    lines = log_lines()
    leg["phases"]["record"] = dict(begin=_mark(lines))
    master.start(bpm=INTENSE_BPM)
    # Tier-A state rows are retained in the ring while the transport runs, so the live confirmation is the logger's:
    # an armed take starts on the Start it was waiting for.
    rec = _wait_line(log_lines, len(lines), ("Transport started",), 5.0)
    if rec is None:
        raise RuntimeError("record_not_confirmed")
    notes, _cc, clocks, timing = stream_pattern_for_bars(
        session.out_port, session.in_port, midi_channel_1based=1, low_note=36, high_note=71,
        step_clocks=STEP_16TH_CLOCKS, gate_clocks=3, target_bars=INTENSE_RECORD_BARS, cc_number=74, cc_step=9,
        pitch_cycle_bars=2, phase_start_delay_bars=0, phase_start_delay_beats=0, max_seconds_guard=120.0,
        stop_press_advance_clocks=12, stop_press_note=_slot_note(HITL_SLOT),
        stop_press_channel_1based=CONTROL_CHANNEL_1BASED, emit_immediate_first_step=True, master=master,
        grid_origin_pulse=master.grid_origin_pulse())  # count the take's bars from its own tick 0
    stopped = _wait_line(log_lines, len(lines), ("Stop Recording",), 4 * bar_s + 3)
    leg["phases"]["record"].update(notes=notes, clocks=clocks, end=_mark(log_lines()),
                                   stop_in_stream=bool(timing.get("stop_press_sent_during_stream")),
                                   stop_confirmed=stopped is not None)
    if stopped is None:
        raise RuntimeError("record_stop_not_confirmed")
    save()
    time.sleep(4 * bar_s)  # let the take commit and play once
    leg["tracks"] = {t: list(v) for t, v in track_states(log_lines()).items()}

    # 4. Overdub the take for about 100 seams.
    lines = log_lines()
    leg["phases"]["overdub"] = dict(begin=_mark(lines))
    press_slot_overdub(action, HITL_SLOT)
    od = _wait_line(log_lines, len(lines), ("Overdub session opened",), 4 * bar_s + 3)
    if od is None:
        raise RuntimeError("overdub_not_confirmed")
    od_bars = max(8, int(INTENSE_OVERDUB_BARS * scale))
    notes, _cc, clocks, timing = stream_pattern_for_bars(
        session.out_port, session.in_port, midi_channel_1based=1, low_note=72, high_note=95,
        step_clocks=STEP_16TH_CLOCKS, gate_clocks=3, target_bars=od_bars, cc_number=74, cc_step=9,
        pitch_cycle_bars=2, phase_start_delay_bars=0, phase_start_delay_beats=0, max_seconds_guard=od_bars * bar_s * 2 + 30,
        stop_press_advance_clocks=12, stop_press_note=_slot_note(HITL_SLOT),
        stop_press_channel_1based=CONTROL_CHANNEL_1BASED, emit_immediate_first_step=True, master=master)
    stopped = _wait_line(log_lines, len(lines), ("Stop Overdub", "Overdub stopped", "capture hot stop"),
                         4 * bar_s + 3)
    leg["phases"]["overdub"].update(notes=notes, clocks=clocks, bars=od_bars, end=_mark(log_lines()),
                                    stop_confirmed=stopped is not None)
    if stopped is None:
        report["issues"].append("overdub_stop_not_confirmed_live")  # named, and the Play phase still runs
    save()

    # 5. Play: everything sounding, the take with its overdub included.
    measure = max(10.0, INTENSE_MEASURE_S * scale)
    print(f"[lane_cadence] intense: record + overdub done; playing {measure:.0f}s at {INTENSE_BPM} BPM", flush=True)
    leg["phases"]["play"] = dict(begin=_mark(log_lines()))
    deadline = time.monotonic() + measure
    while time.monotonic() < deadline:
        time.sleep(min(1.0, deadline - time.monotonic()))
        if master.pump_error() is not None:
            raise RuntimeError("clock_pump_failed: " + str(master.pump_error()))
    leg["phases"]["play"]["end"] = _mark(log_lines())
    master.stop()
    leg["pulses_sent"] = master.pulses_sent()
    save()


#: DEC-246 amendment 26: the device successor-lead sweep with deterministic arbitration.
SWEEP_DENY_LEVELS = tuple(range(8))
SWEEP_SPAN_TICKS = 1536       # one successor span (two bars)
SWEEP_RESOLUTION = 48         # a sixteenth
SWEEP_SEARCH_SEAMS = 20
SWEEP_VERIFY_SEAMS = 50
_SUCC_RE = re.compile(r"^#CAP,(\d+),DIAG,succ,(\d+),(.*)$")


def _succ_latest(lines, start, track0):
    """The latest `DIAG,succ` row for `track0` after line `start`, as a dict (or None)."""
    found = None
    for line in lines[start:]:
        m = _SUCC_RE.match(line.strip())
        if not m or int(m.group(2)) != track0:
            continue
        f = [int(x) for x in m.group(3).split(",")]
        if len(f) < 11:
            continue
        found = dict(requested=f[0], handed=f[1], missed=f[2], cancelled=f[3], refused=f[4], max_ready=f[5],
                     max_admit=f[6], min_margin=f[7], max_grains=f[8], min_lead=f[9], denied=f[10])
    return found


def run_sweep(action, session, capture, master, report, save, scale, deny_levels=SWEEP_DENY_LEVELS):
    """Amendment 26: for each denial level (0-7 withheld grants after every grain served to track 8), the smallest
    successor lead at which every seam hands off cleanly -- 20-seam bisection points, then 50/50 at the answer.

    Load: the HITL take on track 8 slot 4 (32 dense bars, the only lane-served slot on this card), the rest of the set
    playing, normal display and save activity, 300 BPM. No overdub (it is a known heap-pressure defect).
    """
    import uuid
    from hitl.actions.slots import _slot_note, press_slot_clear, press_slot_record, press_track
    from hitl.control_constants import CONTROL_CHANNEL_1BASED
    from hitl.transport_clock import stream_pattern_for_bars

    def log_lines():
        return capture.session_log.read_text(errors="replace").splitlines()

    bar_s = 4 * 60.0 / INTENSE_BPM
    seam_s = SWEEP_SPAN_TICKS / (INTENSE_BPM * 192 / 60.0)
    track0 = HITL_TRACK - 1
    leg = dict(bpm=INTENSE_BPM, mode="sweep", points=[], table={})
    report["legs"].append(leg)

    # The take: clear while stopped, arm while stopped, Start, 32 dense bars, stop on the grid (as the intense mode).
    leg["restores_settled"] = _wait_restores_settled(log_lines)
    press_track(action, HITL_TRACK)
    time.sleep(0.6)
    for attempt in range(6):
        start = len(log_lines())
        press_slot_clear(action, HITL_SLOT)
        if _wait_line(log_lines, start, ("Clear refused", "Clear aborted"), 2.5) is None:
            break
        time.sleep(2.0)
    else:
        raise RuntimeError("hitl_slot_clear_refused")
    lines = log_lines()
    press_slot_record(action, HITL_SLOT)
    if _wait_line(log_lines, len(lines), ("waiting for clock to start recording",), 20.0) is None:
        raise RuntimeError("record_arm_not_confirmed")
    lines = log_lines()
    master.start(bpm=INTENSE_BPM)
    if _wait_line(log_lines, len(lines), ("Transport started",), 5.0) is None:
        raise RuntimeError("record_not_confirmed")
    stream_pattern_for_bars(
        session.out_port, session.in_port, midi_channel_1based=1, low_note=36, high_note=71,
        step_clocks=STEP_16TH_CLOCKS, gate_clocks=3, target_bars=INTENSE_RECORD_BARS, cc_number=74, cc_step=9,
        pitch_cycle_bars=2, phase_start_delay_bars=0, phase_start_delay_beats=0, max_seconds_guard=120.0,
        stop_press_advance_clocks=12, stop_press_note=_slot_note(HITL_SLOT),
        stop_press_channel_1based=CONTROL_CHANNEL_1BASED, emit_immediate_first_step=True, master=master,
        grid_origin_pulse=master.grid_origin_pulse())
    if _wait_line(log_lines, len(lines), ("Stop Recording",), 4 * bar_s + 3) is None:
        raise RuntimeError("record_stop_not_confirmed")
    save()

    def probe(lead, deny, seams):
        """Configure, then wait for `seams` closed successors on track 8 (or the first miss/discard)."""
        nonce, end = uuid.uuid4().hex, uuid.uuid4().hex
        start = len(log_lines())
        accepted, detail = capture.send_command(f"!SUCC_PROBE {lead} {track0} {deny}", nonce=nonce, end_nonce=end)
        if not accepted:
            raise RuntimeError("succ_probe_command_refused: " + str(detail))
        if _wait_line(log_lines, start, ("SUCC_PROBE,ok",), 5.0) is None:
            raise RuntimeError("succ_probe_not_acknowledged")
        t0 = time.monotonic()
        # A miss repositions the lane at the full priced lookahead (about 30 s at 300 BPM), so allow for one.
        deadline = t0 + seams * seam_s * 3 + 45
        row = None
        while time.monotonic() < deadline:
            time.sleep(1.0)
            if master.pump_error() is not None:
                raise RuntimeError("clock_pump_failed: " + str(master.pump_error()))
            row = _succ_latest(log_lines(), start, track0)
            if row and (row["missed"] or row["cancelled"] or row["handed"] >= seams):
                break
        ok = bool(row) and row["missed"] == 0 and row["cancelled"] == 0 and row["handed"] >= seams
        point = dict(lead=lead, deny=deny, seams=seams, ok=ok, row=row, seconds=round(time.monotonic() - t0, 1))
        leg["points"].append(point)
        save()
        print(f"[lane_cadence] sweep deny={deny} lead={lead}: {'ok' if ok else 'FAIL'} {row}", flush=True)
        return ok

    search = max(5, int(SWEEP_SEARCH_SEAMS * scale))
    verify = max(10, int(SWEEP_VERIFY_SEAMS * scale))
    for deny in deny_levels:
        if not probe(SWEEP_SPAN_TICKS, deny, search):
            leg["table"][deny] = dict(lmin=None, note="even a request at N's start (one span) misses")
            save()
            continue
        lo, hi = 0, SWEEP_SPAN_TICKS
        while hi - lo > SWEEP_RESOLUTION:
            mid = (lo + hi) // 2
            if probe(mid, deny, search):
                hi = mid
            else:
                lo = mid
        while hi <= SWEEP_SPAN_TICKS and not probe(hi, deny, verify):
            hi += SWEEP_RESOLUTION
        leg["table"][deny] = dict(lmin=hi if hi <= SWEEP_SPAN_TICKS else None, failing_below=lo, verify_seams=verify)
        save()
    master.stop()
    leg["pulses_sent"] = master.pulses_sent()
    save()


def run_lane_cadence(args):
    from hitl.layered_cli import config_from_runner_args
    from hitl.managed_capture import active_managed_capture
    from hitl.midi_clock import MidiClockMaster
    from hitl.serial_follow import ExternalSerialFollowCollector
    from hitl.session import open_midi_session
    from hitl.stimuli.registry import STIM_LANE_CADENCE_TEMP_01

    legacy = list(getattr(args, "legacy_args", []))
    config = config_from_runner_args(args, legacy)
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--lane-cadence-scale", type=float, default=1.0, help="leg duration multiplier (smoke runs)")
    parser.add_argument("--lane-cadence-mode", choices=("legs", "intense", "sweep"), default="intense")
    parser.add_argument("--lane-cadence-deny", default="0,1,2,3,4,5,6,7", help="sweep: denial levels (comma list)")
    ns, _ = parser.parse_known_args(legacy)
    config.out_dir.mkdir(parents=True, exist_ok=True)
    path = config.out_dir / "lane-cadence.json"
    report = dict(ok=False, verdict="FAIL", stimulus=STIM_LANE_CADENCE_TEMP_01.fingerprint_key(), legs=[], issues=[],
                  scope="no content gestures; the card's own set; host clock pacing only")
    session = collector = master = None

    def save():
        path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    try:
        save()
        capture = active_managed_capture()
        if capture is None or capture.session_log is None or config.serial_port is not None:
            raise RuntimeError("managed_outer_serial_owner_required")
        collector = ExternalSerialFollowCollector(capture.session_log, out_dir=config.out_dir)
        collector.start()
        session = open_midi_session(midi_out_name=config.midi_out_name, midi_in_name=config.midi_in_name, serial_port=None)
        session.collector = collector
        master = MidiClockMaster(session.out_port)
        if ns.lane_cadence_mode in ("intense", "sweep"):
            from hitl.context import ActionContext
            ctx = ActionContext(session=session, config=config)
            if ns.lane_cadence_mode == "intense":
                run_intense(ctx, session, capture, master, report, save, ns.lane_cadence_scale)
            else:
                run_sweep(ctx, session, capture, master, report, save, ns.lane_cadence_scale,
                          tuple(int(x) for x in ns.lane_cadence_deny.split(",") if x.strip()))
        for bpm, seconds in (LEGS if ns.lane_cadence_mode == "legs" else ()):
            seconds = max(5.0, seconds * ns.lane_cadence_scale)
            leg = dict(bpm=bpm, planned_s=seconds, start_monotonic=time.monotonic(), start_us_hint=None)
            report["legs"].append(leg)
            save()
            print(f"[lane_cadence] leg {bpm} BPM for {seconds:.0f}s", flush=True)
            master.start(bpm=bpm)
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                time.sleep(min(1.0, deadline - time.monotonic()))
                if master.pump_error() is not None:
                    raise RuntimeError("clock_pump_failed: " + str(master.pump_error()))
            master.stop()
            leg["pulses_sent"] = master.pulses_sent()
            leg["end_monotonic"] = time.monotonic()
            save()
            time.sleep(SETTLE_S)
        report["ok"], report["verdict"] = True, "OBSERVED"
    except Exception as exc:
        report["issues"].append(f"{type(exc).__name__}: {exc}")
        report["ok"], report["verdict"] = False, "FAIL"
    finally:
        try:
            if master is not None:
                master.stop()
        except Exception as exc:
            report["issues"].append(f"clock_stop: {exc}")
        try:
            if session is not None:
                session.close()
            elif collector is not None:
                collector.stop()
        except Exception as exc:
            report["issues"].append(f"follower_or_MIDI_close: {exc}")
        save()
    print("[lane_cadence] " + report["verdict"] + ": " + str(path), flush=True)
    return 0 if report["ok"] else 2
