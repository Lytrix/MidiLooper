"""STIM-INGRESS-COST-TEMP-01: stopped Clock-only conditional observations.

The outer managed DeviceSession retains serial through its two Stops/drains,
postflight and safety latch. This scenario never opens serial or sends Start.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import time
import uuid

from hitl.scenarios import pit_motion
from hitl.scenarios.pit_clock import preflight

FIELDS = ('id', 'route', 'lane', 'cpuHz', 'guardBits', 'accepted', 'rejected',
          'minCycles', 'maxCycles', 'sumCycles', 'saturated', 'contractBits')
BITS = (32, 2, 2, 32, 32, 32, 32, 32, 32, 64, 1, 32)
MARKER = ',ingress_cost,'
LIMITATIONS = {
    'scope': 'Conditional instrumented observations only: not uninstrumented time, WCET, full receiver/IRQ cost or target readiness',
    'hook': 'Includes enabled inner probes and their first publication; excludes wrapper prologue/epilogue and final publication',
    'publication': 'Publication self-accounting tail excluded/unmeasured; additional overhead remains',
    'mask': 'Conservative bracket includes endpoint/sampling/preemption costs, not exclusive IRQ-off cycles',
    'continuity': 'Continuous Run-mode/DWT and gross rate bound are an ASSUMPTION; matching Ready/era endpoints cannot establish unseen continuity',
    'host': 'Absolute-deadline helper records host send calls only; no wire-arrival/pacing guarantee, calibrated ppm or independent calibration',
    'statistics': 'Lifetime min/max only, never subtracted; only counts/sums are per-leg deltas. Packet count is not Clock count',
    'routes': 'DIN and USB-host NOT RUN: no physical fixture, not qualified',
    'persistence': 'Automatic persistence possible; no content/Start/slot/save/load/delete gestures; no SD byte-immutability claim',
    'shutdown': 'Outer managed DeviceSession owns serial, two Stops/drains, postflight and safety latch; scenario verdict is conditional on outer success',
    'delivery': 'Exact fences plus available request tail checked; future delivery beyond bounded observation window is unproven',
}


def parse_snapshot(rows, request_id):
    if type(request_id) is not int or not 0 <= request_id < 1 << 32:
        raise RuntimeError('request_id_range')
    markers = [r.rstrip('\r\n') for r in rows if MARKER in r]
    if len(markers) != 12:
        raise RuntimeError('missing_or_duplicate_ingress_cost_rows')
    result = []
    seen = set()
    for marker in markers:
        p = marker.split(',')
        if len(p) != 16 or p[0] != '#CAP' or p[2:4] != ['DIAG', 'ingress_cost']:
            raise RuntimeError('invalid_ingress_cost_contract')
        if any(re.fullmatch(r'[0-9]+', v) is None for v in [p[1], *p[4:]]):
            raise RuntimeError('invalid_unsigned_integer')
        values = list(map(int, p[4:]))
        if int(p[1]) >= 1 << 32 or any(v >= 1 << b for v, b in zip(values, BITS)):
            raise RuntimeError('integer_range')
        s = dict(zip(FIELDS, values))
        if s['id'] != request_id:
            raise RuntimeError('request_id_mismatch')
        key = s['route'], s['lane']
        if s['route'] > 2 or key in seen:
            raise RuntimeError('duplicate_or_invalid_route_lane')
        seen.add(key)
        if (s['cpuHz'], s['guardBits'], s['contractBits']) != (600_000_000, 15, 7):
            raise RuntimeError('CPU_guard_or_conditional_contract_refused')
        n, lo, hi, total = (s[k] for k in ('accepted', 'minCycles', 'maxCycles', 'sumCycles'))
        if n == 0:
            if (lo, hi, total) != ((1 << 32) - 1, 0, 0):
                raise RuntimeError('invalid_empty_statistics')
        elif lo > hi or not n * lo <= total <= n * hi:
            raise RuntimeError('inconsistent_statistics')
        if s['saturated']:
            raise RuntimeError('saturated_statistics')
        result.append(s)
    return sorted(result, key=lambda s: (s['route'], s['lane']))


def delayed_snapshot_rows(lines, nonce, end_nonce):
    from hitl.serial.protocol import fence_token
    starts = [i for i, r in enumerate(lines) if fence_token(r) == nonce]
    ends = [i for i, r in enumerate(lines) if fence_token(r) == end_nonce]
    if len(starts) != 1 or len(ends) != 1 or ends[0] <= starts[0]:
        return None
    return [r for r in lines[starts[0] + 1:] if MARKER in r]


def observe(capture, request_id, *, timeout_s=2.0):
    if type(request_id) is not int or not 0 <= request_id < 1 << 32:
        raise RuntimeError('request_id_range')
    if not 0 < timeout_s <= 2 or not math.isfinite(timeout_s):
        raise RuntimeError('sample_timeout_range')
    nonce, end_nonce = uuid.uuid4().hex, uuid.uuid4().hex
    sample = dict(id=request_id, nonce=nonce, end_nonce=end_nonce)
    log = capture.session_log
    # Only bytes appended during THIS request, with a hard memory/I/O cap.
    offset = log.stat().st_size if log is not None and log.is_file() else 0
    deadline = time.monotonic() + timeout_s
    try:
        accepted, detail = capture.send_command(f'!INGRESS_COST {request_id}', nonce=nonce, end_nonce=end_nonce)
        if not accepted:
            raise RuntimeError('managed_command_refused: ' + detail)
        rows = capture.fenced_response(nonce, end_nonce, timeout_s=max(.001, deadline - time.monotonic()), poll_s=.01)
        if rows is None:
            raise RuntimeError('ingress_cost_end_fence_missing')
        while True:
            if log is not None and log.is_file():
                with log.open('rb') as stream:
                    stream.seek(offset)
                    data = stream.read(262145)
                if len(data) > 262144:
                    raise RuntimeError('request_tail_exceeds_bound')
                # capture_session appends raw serial chunks. A trailing fragment
                # is not a complete row and must wait for its newline/deadline.
                complete = data[:data.rfind(b'\n') + 1]
                tail = delayed_snapshot_rows(complete.decode('utf-8', errors='replace').splitlines(), nonce, end_nonce)
                # Require exact unique fences in the available tail even when
                # fenced_response already returned all immediate rows.
                rows = tail
            sample['response_rows'] = rows
            if time.monotonic() > deadline:
                raise RuntimeError('sample_deadline_exceeded')
            if rows is not None and len(rows) >= 12:
                sample['rows'] = parse_snapshot(rows, request_id)
                return sample
            if time.monotonic() >= deadline:
                raise RuntimeError('incomplete_rows_or_nonunique_fences')
            time.sleep(.01)
    except Exception as exc:
        sample['error'] = f'{type(exc).__name__}: {exc}'
        return sample
    finally:
        capture.complete_command(nonce)


def count_sum_deltas(before, after):
    """Validate lifetime monotonicity; never subtract extrema."""
    if len(before) != 12 or len(after) != 12:
        raise RuntimeError('snapshot_size')
    result = []
    for a, b in zip(before, after):
        if (a['route'], a['lane']) != (b['route'], b['lane']):
            raise RuntimeError('snapshot_identity_changed')
        d = dict(route=a['route'], lane=a['lane'])
        for key in ('accepted', 'rejected', 'sumCycles'):
            if b[key] < a[key]:
                raise RuntimeError('decreasing_' + key)
            d[key] = b[key] - a[key]
        if a['accepted'] and (b['minCycles'] > a['minCycles'] or b['maxCycles'] < a['maxCycles']):
            raise RuntimeError('lifetime_extrema_regressed')
        if not d['accepted'] and (b['minCycles'], b['maxCycles']) != (a['minCycles'], a['maxCycles']):
            raise RuntimeError('extrema_changed_without_samples')
        if not d['accepted'] * b['minCycles'] <= d['sumCycles'] <= d['accepted'] * b['maxCycles']:
            raise RuntimeError('delta_sum_inconsistent')
        d.update(lifetime_minCycles=b['minCycles'], lifetime_maxCycles=b['maxCycles'])
        result.append(d)
    return result


def evaluate_leg(before, after):
    deltas = count_sum_deltas(before, after)
    if any(d['rejected'] for d in deltas):
        raise RuntimeError('new_rejected_samples')
    counts = [d['accepted'] for d in deltas if d['route'] == 0]
    if counts[0] <= 0 or counts != [counts[0], counts[0], 3 * counts[0], counts[0]]:
        raise RuntimeError('quiescent_macro_ratios_refused')
    return dict(ok=True, verdict='CONDITIONAL_INSTRUMENTED_OBSERVATIONS', count_sum_deltas=deltas,
                other_routes={'1': 'NOT RUN: no DIN fixture; not qualified', '2': 'NOT RUN: no USB-host fixture; not qualified'})


def add_arguments(parser):
    parser.add_argument('--ingress-cost-mode', choices=('smoke', 'full'), default='full', help='2s or 5s Clock-only per 120/300 BPM leg')
    parser.add_argument('--ingress-cost-settle-s', type=float, default=.2, help='Quiescent delay, 0.1..2 seconds')
    parser.add_argument('--ingress-cost-stable-budget-s', type=float, default=6, help='Stable snapshot polling budget, 3..15 seconds')
    parser.add_argument('--ingress-cost-max-run-s', type=float, default=60, help='Scenario bound, 30..120 seconds (outer managed bound <=360s)')


def run_ingress_cost(args):
    from hitl.layered_cli import config_from_runner_args
    from hitl.managed_capture import active_managed_capture
    from hitl.midi_clock import MidiClockMaster
    from hitl.session import open_midi_session
    from hitl.stimuli.registry import STIM_INGRESS_COST_TEMP_01

    legacy = list(getattr(args, 'legacy_args', []))
    config = config_from_runner_args(args, legacy)
    config.out_dir.mkdir(parents=True, exist_ok=True)
    path = config.out_dir / 'ingress-cost.json'
    if path.exists():
        return 2  # Never overwrite evidence.
    report = dict(ok=False, verdict='FAIL', limitations=LIMITATIONS, legs=[], issues=[],
                  observations=[], guards=[], preflights=[], stimulus=STIM_INGRESS_COST_TEMP_01.fingerprint_key(),
                  other_routes={'1': 'NOT RUN: no DIN fixture; not qualified', '2': 'NOT RUN: no USB-host fixture; not qualified'})
    session = master = None
    request_id = 0
    previous_guard = None
    last_cost_request = None
    last_snapshot = None

    def save():
        path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    def remaining():
        left = deadline - time.monotonic()
        if left <= 0:
            raise RuntimeError('scenario_deadline_exceeded')
        return left

    def wait(seconds):
        if seconds > remaining():
            raise RuntimeError('scenario_deadline_exceeded')
        time.sleep(seconds)

    def guard():
        nonlocal request_id, previous_guard
        report['preflights'].append(preflight(capture))
        s = pit_motion.observe(capture, request_id, timeout_s=min(2, remaining()))
        request_id += 1
        report['guards'].append(s)
        save()
        pit_motion.validate_sample(s, previous_guard, stopped=True)
        previous_guard = s
        return s

    def query(until):
        nonlocal request_id, last_cost_request, last_snapshot
        if last_cost_request is not None:
            pause = max(0, 1 - (time.monotonic() - last_cost_request))
            if time.monotonic() + pause >= until:
                raise RuntimeError('quiescent_snapshot_timeout')
            wait(pause)
        if time.monotonic() >= until:
            raise RuntimeError('quiescent_snapshot_timeout')
        last_cost_request = time.monotonic()
        s = observe(capture, request_id, timeout_s=min(2, remaining(), until - time.monotonic()))
        request_id += 1
        report['observations'].append(s)
        save()
        if s.get('error'):
            raise RuntimeError(s['error'])
        if last_snapshot is not None:
            count_sum_deltas(last_snapshot, s['rows'])
        last_snapshot = s['rows']
        return s['rows']

    def stable():
        until = min(deadline, time.monotonic() + ns.ingress_cost_stable_budget_s)
        previous = query(until)
        while time.monotonic() < until:
            if until - time.monotonic() < 1:
                break
            current = query(until)
            deltas = count_sum_deltas(previous, current)
            if all(not d['accepted'] and not d['rejected'] and not d['sumCycles'] for d in deltas):
                return current
            previous = current
        raise RuntimeError('quiescent_snapshot_timeout')

    try:
        save()
        parser = argparse.ArgumentParser(add_help=False)
        add_arguments(parser)
        # argparse errors must still produce the failure artifact.
        try:
            ns, _ = parser.parse_known_args(legacy)
        except SystemExit as exc:
            raise RuntimeError('invalid_scenario_options') from exc
        for value, low, high in ((ns.ingress_cost_settle_s, .1, 2), (ns.ingress_cost_stable_budget_s, 3, 15), (ns.ingress_cost_max_run_s, 30, 120)):
            if not math.isfinite(value) or not low <= value <= high:
                raise RuntimeError('scenario_option_out_of_range')
        deadline = time.monotonic() + ns.ingress_cost_max_run_s
        report.update(options=vars(ns), duration_s=2 if ns.ingress_cost_mode == 'smoke' else 5)
        capture = active_managed_capture()
        if capture is None or capture.session_log is None or config.serial_port is not None:
            raise RuntimeError('managed_outer_serial_owner_required')
        session = open_midi_session(midi_out_name=config.midi_out_name, midi_in_name=config.midi_in_name, serial_port=None)
        for bpm in (120, 300):
            leg = dict(bpm=bpm)
            report['legs'].append(leg)
            leg['before_guard'] = guard()
            leg['before'] = stable()
            # Fresh guard immediately before Clock-only input, not inferred from a prior leg.
            leg['pump_guard'] = guard()
            probe = pit_motion.ClockSendProbe(session.out_port)
            master = MidiClockMaster(probe)
            try:
                master.start_pump(bpm=bpm)
                wait(report['duration_s'])
            finally:
                master.stop_pump()  # No Start/Stop/content gesture; outer shutdown sends Stops.
                leg['host_pacing'] = probe.report(bpm)
                leg['host_pulses_sent'] = master.pulses_sent()
                error = master.pump_error()
                master = None
                save()
            if error is not None:
                raise RuntimeError('clock_pump_failed: ' + str(error))
            wait(ns.ingress_cost_settle_s)
            leg['after'] = stable()  # Never apply ratios to an in-flight callback snapshot.
            leg['after_guard'] = guard()
            leg.update(evaluate_leg(leg['before'], leg['after']))
            save()
        report.update(ok=True, verdict='CONDITIONAL_INSTRUMENTED_OBSERVATIONS')
    except Exception as exc:
        report['issues'].append(f'{type(exc).__name__}: {exc}')
    finally:
        try:
            if master is not None:
                master.stop_pump()
        except Exception as exc:
            report.update(ok=False, verdict='FAIL')
            report['issues'].append('pump_cleanup: ' + str(exc))
        try:
            if session is not None:
                session.close()  # MIDI only; outer serial remains alive through canonical shutdown.
        except Exception as exc:
            report.update(ok=False, verdict='FAIL')
            report['issues'].append('MIDI_close: ' + str(exc))
        save()
    print('[ingress_cost] ' + report['verdict'] + ': ' + str(path), flush=True)
    return 0 if report['ok'] else 2
