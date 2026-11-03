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
FIRST_WINDOW_WAIT_S = 240
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


_TICK_ROUTE_RE = re.compile(r"^#CAP,(\d+),DIAG,tick_route,(.*)$")
SWEEP_TRACKS = 8              # slot-ticks per clock pulse in `tick_route` (every track counts once)
SWEEP_TICKS_PER_PULSE = 8     # external MIDI clock: 24 PPQN against 192 internal


def _routed_window_served_ticks(lines, start):
    """Window-served ticks of the routed slot since line `start`, from the live `tick_route` rows.

    `tick_route` reaches the log while the transport runs (the per-track lane rows are retained until it stops). On
    this card the routed slot is the only one whose misses are `none` (the lane's silent contained miss; every other
    track plays merged), so each row's pulses minus its `none` count are the pulses the routed slot was served from a
    window. Assumption of this card, stated: no other track produces `none`.
    """
    served = 0
    for line in lines[start:]:
        m = _TICK_ROUTE_RE.match(line.strip())
        if not m:
            continue
        fields = dict(kv.split("=", 1) for kv in m.group(2).split(",") if "=" in kv)
        try:
            counts = [int(fields[k]) for k in ("view", "bridge", "paged", "merged", "none")]
        except (KeyError, ValueError):
            continue
        pulses = sum(counts) // SWEEP_TRACKS
        served += max(0, pulses - counts[4]) * SWEEP_TICKS_PER_PULSE
    return served


def run_sweep(action, session, capture, master, report, save, scale, deny_levels=SWEEP_DENY_LEVELS, hold_s=0.0,
              points=(), overdub_bars=0):
    """Amendment 26: for each denial level (0-7 withheld grants after every grain served to track 8), the smallest
    successor lead at which every seam hands off cleanly -- 20-seam bisection points, then 50/50 at the answer.

    Load: the HITL take on track 8 slot 4 (32 dense bars, the only lane-served slot on this card), the rest of the set
    playing, normal display and save activity, 300 BPM. No overdub by default (it is a known heap-pressure defect);
    `overdub_bars` > 0 overdubs the take once before the restart, so every committed window interleaves two passes
    (C5.3b dense-rank qualification).
    """
    import uuid
    from hitl.actions.slots import _slot_note, press_slot_clear, press_slot_overdub, press_slot_record, press_track
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

    if overdub_bars > 0:
        # One overdub pass in the upper register, interleaved in time with the take (the intense mode's gesture).
        time.sleep(4 * bar_s)  # let the take commit and play once
        lines = log_lines()
        press_slot_overdub(action, HITL_SLOT)
        if _wait_line(log_lines, len(lines), ("Overdub session opened",), 4 * bar_s + 3) is None:
            raise RuntimeError("overdub_not_confirmed")
        stream_pattern_for_bars(
            session.out_port, session.in_port, midi_channel_1based=1, low_note=72, high_note=95,
            step_clocks=STEP_16TH_CLOCKS, gate_clocks=3, target_bars=overdub_bars, cc_number=74, cc_step=9,
            pitch_cycle_bars=2, phase_start_delay_bars=0, phase_start_delay_beats=0,
            max_seconds_guard=overdub_bars * bar_s * 2 + 30, stop_press_advance_clocks=12,
            stop_press_note=_slot_note(HITL_SLOT), stop_press_channel_1based=CONTROL_CHANNEL_1BASED,
            emit_immediate_first_step=True, master=master)
        stopped = _wait_line(log_lines, len(lines), ("Stop Overdub", "Overdub stopped", "capture hot stop"),
                             4 * bar_s + 3)
        leg["overdub"] = dict(bars=overdub_bars, stop_confirmed=stopped is not None)
        if stopped is None:
            raise RuntimeError("overdub_stop_not_confirmed")
        save()

    # Measurement workaround (amendments 26-27): avoid the open Record-return ownership handoff. Let the Record job
    # retire and the committed source publish, stop, record the state, then restart: the sweep starts from a clean
    # manifest-served committed lane, never from the Record-return path.
    time.sleep(45.0)
    master.stop()
    # The Record presentation retires only while stopped (playing, it waits for a committed window that it itself
    # blocks: the open ownership-handoff defect), in several deferred steps -- 4 s was not enough on the device.
    time.sleep(20.0)
    status = {}
    # `!REVSHADOW_STATUS` copies the whole loop's events twice into heap vectors for its shadow parity; on an overdubbed
    # take (about 3,000 rows) that ran the device out of heap and reset it (2026-11-03), so it is skipped then.
    for command in ("!STOP_STATUS",) + (() if overdub_bars > 0 else ("!REVSHADOW_STATUS",)):
        nonce, end = uuid.uuid4().hex, uuid.uuid4().hex
        accepted, _ = capture.send_command(command, nonce=nonce, end_nonce=end)
        rows = capture.fenced_response(nonce, end, timeout_s=5.0, poll_s=0.05) if accepted else None
        status[command] = [r.strip() for r in (rows or []) if r.strip().startswith(("#STOP_STATUS", "REVSHADOW,lane"))]
    leg["pre_restart_status"] = status
    save()
    print(f"[lane_cadence] pre-restart status: {status}", flush=True)
    lines = log_lines()
    master.start(bpm=INTENSE_BPM)
    if _wait_line(log_lines, len(lines), ("Transport started",), 5.0) is None:
        raise RuntimeError("restart_not_confirmed")
    # The first manifest window must already serve before any successor is timed: a successor is only requested while
    # a positioned window serves, so the first `succ` demand after a reset is that proof. A restart that never serves is
    # a separate recovery defect, not a successor failure.
    nonce, end = uuid.uuid4().hex, uuid.uuid4().hex
    start = len(log_lines())
    capture.send_command(f"!SUCC_PROBE {SWEEP_SPAN_TICKS} {track0} 0", nonce=nonce, end_nonce=end)
    # Collect the fenced reply: it also releases the capture's one outstanding-request slot for the sweep's probes.
    capture.fenced_response(nonce, end, timeout_s=5.0, poll_s=0.05)
    t0 = time.monotonic()
    first = None
    # The first window after a restart is placed by the generic DEC-147 price, which can put it several traversals
    # ahead (about 26 s each for the 33-bar take at 300 BPM); 240 s covers that placement, not a stuck lane.
    while time.monotonic() < t0 + FIRST_WINDOW_WAIT_S and first is None:
        time.sleep(1.0)
        current = log_lines()
        # The live signal: one whole span of the routed slot served from a window (owner ruling, amendment 27). The
        # successor probe's `succ` row is retained until the transport stops, so it can only confirm afterwards.
        served = _routed_window_served_ticks(current, start)
        row = _succ_latest(current, start, track0)
        if served >= SWEEP_SPAN_TICKS or (row and row["requested"] > 0):
            first = dict(window_served_ticks=served, succ=row)
    leg["first_window_served_s"] = None if first is None else round(time.monotonic() - t0, 1)
    leg["first_window_evidence"] = first
    save()
    if first is None:
        raise RuntimeError(f"restart_no_first_manifest_window_within_{FIRST_WINDOW_WAIT_S}s")
    print(f"[lane_cadence] first manifest window serving {leg['first_window_served_s']}s after restart", flush=True)

    def configure(lead, deny):
        """`!SUCC_PROBE` (it resets the probe's counters); returns the log line the new counters start after."""
        nonce, end = uuid.uuid4().hex, uuid.uuid4().hex
        start = len(log_lines())
        accepted, detail = capture.send_command(f"!SUCC_PROBE {lead} {track0} {deny}", nonce=nonce, end_nonce=end)
        if not accepted:
            raise RuntimeError("succ_probe_command_refused: " + str(detail))
        rows = capture.fenced_response(nonce, end, timeout_s=5.0, poll_s=0.05)  # releases the request slot
        if not any(r.strip().startswith("SUCC_PROBE,ok") for r in (rows or [])) and \
                _wait_line(log_lines, start, ("SUCC_PROBE,ok",), 1.0) is None:
            raise RuntimeError("succ_probe_not_acknowledged")
        return start

    def probe(lead, deny, seams):
        """One honest point: a failure whose smallest actual lead fell more than one resolution step short of `lead` is
        the configure-phase artefact, so it is measured once more before it counts."""
        ok, row = probe_once(lead, deny, seams)
        if not ok and row and row["min_lead"] < lead - SWEEP_RESOLUTION:
            print(f"[lane_cadence] sweep deny={deny} lead={lead}: short actual lead {row['min_lead']}, measuring again",
                  flush=True)
            ok, row = probe_once(lead, deny, seams)
        return ok

    def probe_once(lead, deny, seams):
        """Configure, then wait for `seams` closed successors on track 8 (or the first miss/discard).

        A configure lands at an arbitrary phase of the serving window N, so N's own successor is requested at once with
        whatever lead is left, not `lead` (device: actual 752 at configured 768, 1152 and 1344). That first seam is a
        method artefact: it is let resolve, then the probe is configured again -- the counters reset at a point where
        no successor of the current window is owed yet -- and only the seams after it are judged.
        """
        start = configure(lead, deny)
        warm_deadline = time.monotonic() + seam_s * 3 + 45
        while time.monotonic() < warm_deadline:
            time.sleep(0.25)
            warm = _succ_latest(log_lines(), start, track0)
            if warm and (warm["handed"] + warm["missed"] + warm["cancelled"]) >= 1:
                break
        start = configure(lead, deny)
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
        honest = bool(row) and row["min_lead"] >= lead - SWEEP_RESOLUTION
        point = dict(lead=lead, deny=deny, seams=seams, ok=ok, honest=honest, row=row,
                     seconds=round(time.monotonic() - t0, 1))
        leg["points"].append(point)
        save()
        print(f"[lane_cadence] sweep deny={deny} lead={lead}: {'ok' if ok else 'FAIL'}"
              f"{'' if honest else ' (short actual lead)'} {row}", flush=True)
        return ok, row

    if not deny_levels:
        # The takeover check (amendment 27): the take plays past the frozen Record reader's period and the committed
        # takeover; the capture's lane rows answer whether a committed window serves. No sweep.
        # Stops promptly once the first window has served; `--lane-cadence-hold` keeps playing to observe steady state.
        leg["takeover_play_s"] = hold_s
        if hold_s > 0:
            time.sleep(hold_s)
    if points:
        # Fixed points (deny, lead, seams), no search: the per-seam trace (`succ_seam`) is the measurement.
        for deny, lead, seams in points:
            probe(lead, deny, seams)
        master.stop()
        leg["pulses_sent"] = master.pulses_sent()
        save()
        return
    coarse = max(5, int(10 * scale))
    boundary = max(10, int(20 * scale))
    verify = max(10, int(SWEEP_VERIFY_SEAMS * scale))
    for deny in deny_levels:
        # Coarse: one span halved until the first failure; then bisect between the last pass and that failure.
        passing, failing = None, None
        lead = SWEEP_SPAN_TICKS
        while lead >= SWEEP_RESOLUTION:
            if probe(lead, deny, coarse):
                passing = lead
                lead //= 2
            else:
                failing = lead
                break
        if passing is None:
            leg["table"][deny] = dict(lmin=None, note="even a request at N's start (one span) misses")
            save()
            continue
        lo, hi = (failing or 0), passing
        while hi - lo > SWEEP_RESOLUTION:
            mid = (lo + hi) // 2
            if probe(mid, deny, coarse):
                hi = mid
            else:
                lo = mid
        while hi <= SWEEP_SPAN_TICKS and not probe(hi, deny, boundary):
            hi += SWEEP_RESOLUTION
        while hi <= SWEEP_SPAN_TICKS and not probe(hi, deny, verify):
            hi += SWEEP_RESOLUTION
        leg["table"][deny] = dict(lmin=hi if hi <= SWEEP_SPAN_TICKS else None, failing_below=lo, verify_seams=verify)
        save()
        print(f"[lane_cadence] deny={deny}: Lmin={leg['table'][deny]['lmin']} ticks", flush=True)
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
    parser.add_argument("--lane-cadence-deny", default="0,3,7", help="sweep: denial levels (comma list; 'none': setup only)")
    parser.add_argument("--lane-cadence-points", default="",
                        help="sweep: fixed probes deny:lead:seams,... instead of the search")
    parser.add_argument("--lane-cadence-overdub-bars", type=int, default=0,
                        help="sweep: overdub the take for this many bars before the restart (0: none)")
    parser.add_argument("--lane-cadence-hold", type=float, default=0.0,
                        help="sweep with deny none: seconds to keep playing after the first window serves")
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
                          tuple(int(x) for x in ns.lane_cadence_deny.split(",") if x.strip() and x.strip() != "none"),
                          hold_s=ns.lane_cadence_hold,
                          points=tuple(tuple(int(v) for v in p.split(":")) for p in ns.lane_cadence_points.split(",")
                                       if p.strip()),
                          overdub_bars=ns.lane_cadence_overdub_bars)
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
