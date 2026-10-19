"""Temporary moving PIT observations, not calibrated timing/arrival qualification.

Recipe STIM-PIT-MOTION-TEMP-01: managed serial owner, existing transport action
and MidiClockMaster only; no content gestures. Ordinary automatic persistence
may occur. Firmware identity/upload and canonical outer shutdown belong to runner.
"""
from __future__ import annotations

import argparse
import json
import re
import statistics
import time
import uuid

from hitl.scenarios.pit_clock import preflight

FIELDS = ('id', 'status', 'era', 'rawTicks', 'beforeUs', 'widthUs',
          'musicalTick', 'epoch', 'periodUs', 'guardBits', 'configBits', 'transport', 'source')
BITS = (32, 32, 32, 64, 64, 32, 64, 32, 32, 32, 32, 1, 1)
LIMITATIONS = {
    'independent_calibration': 'BLOCKED: no independent calibrated source; owner confirms RTC/interrupts/derived MIDI only',
    'arrival': 'BLOCKED: host USB send calls are not wire arrival; ingress receipts default OFF',
    'bounds': 'BLOCKED: complete owner/IRQ bounds and source ISR/main protocol unresolved; no WCET or arrival bounds',
    'persistence': 'Ordinary automatic persistence possible; no record/edit/clear/save/load/slot/SD gestures',
    'shutdown': 'Canonical outer DeviceSession owns two Stops, drains, postflight and safety latch',
}

# Firmware ClockSource enum: include/ClockManager.h, INTERNAL=0, EXTERNAL=1.
# guardBits: 1 no active/pending Capture; 2 all tracks STOPPED/EMPTY and no
# stop-in-flight; 4 storage idle (jobs/restores/undo/prewarm); 8 no pendingStart.


def parse_observation(rows, request_id):
    markers = [r.strip() for r in rows if ',DIAG,pit_motion,' in r]
    if len(markers) != 1:
        raise RuntimeError('missing_or_duplicate_pit_motion')
    parts = markers[0].split(',')
    if len(parts) != 17 or parts[0] != '#CAP' or parts[2:4] != ['DIAG', 'pit_motion']:
        raise RuntimeError('invalid_pit_motion_contract')
    if any(re.fullmatch(r'[0-9]+', v) is None for v in [parts[1], *parts[4:]]):
        raise RuntimeError('invalid_pit_motion_integer')
    values = list(map(int, parts[4:]))
    if int(parts[1]) >= 1 << 32 or any(v >= 1 << b for v, b in zip(values, BITS)):
        raise RuntimeError('pit_motion_integer_range')
    result = dict(zip(FIELDS, values))
    if result['id'] != request_id:
        raise RuntimeError('pit_motion_request_id_mismatch')
    return dict(result, marker=markers[0], capMicros=int(parts[1]))


def delayed_observation_rows(lines, nonce, end_nonce, request_id):
    from hitl.serial.protocol import fence_token
    starts = [i for i, r in enumerate(lines) if fence_token(r) == nonce]
    ends = [i for i, r in enumerate(lines) if fence_token(r) == end_nonce]
    if len(starts) != 1 or len(ends) != 1 or ends[0] <= starts[0]:
        return None
    # Keep all diagnostic rows, not only the requested ID: wrong/duplicate IDs fail.
    rows = [r for r in lines[starts[0] + 1:] if ',DIAG,pit_motion,' in r]
    return rows or None


def observe(capture, request_id, *, timeout_s=2.0, now=time.monotonic_ns):
    if type(request_id) is not int or not 0 <= request_id < 1 << 32:
        raise RuntimeError('request_id_range')
    nonce, end_nonce = uuid.uuid4().hex, uuid.uuid4().hex
    sample = dict(id=request_id, nonce=nonce, end_nonce=end_nonce, s_ns=now())
    deadline = time.monotonic() + timeout_s
    try:
        accepted, detail = capture.send_command(f'!PIT_MOTION {request_id}', nonce=nonce, end_nonce=end_nonce)
        if not accepted:
            raise RuntimeError('managed_command_refused: ' + detail)
        rows = capture.fenced_response(nonce, end_nonce, timeout_s=timeout_s, poll_s=0.01)
        if rows is None:
            raise RuntimeError('pit_motion_end_fence_missing')
        # fenced_response requires exact fences; a ring row may follow the end.
        if not any(',DIAG,pit_motion,' in r for r in rows):
            while True:
                log = capture.session_log
                if log is not None and log.is_file():
                    delayed = delayed_observation_rows(log.read_text(encoding='utf-8', errors='replace').splitlines(), nonce, end_nonce, request_id)
                    if delayed is not None:
                        rows = delayed
                        break
                if time.monotonic() >= deadline:
                    raise RuntimeError('pit_motion_delayed_row_missing')
                time.sleep(0.01)
        # Even an immediate row can have a duplicate already delivered after its
        # end fence. Validate the complete available request tail, not just the
        # fenced subset. Future serial delivery outside this window is unproven.
        log = capture.session_log
        if log is not None and log.is_file():
            tail = delayed_observation_rows(
                log.read_text(encoding='utf-8', errors='replace').splitlines(),
                nonce, end_nonce, request_id)
            if tail is not None:
                rows = tail
        sample.update(r_ns=now(), response_rows=rows)
        sample.update(parse_observation(rows, request_id))
    except Exception as exc:
        sample['error'] = f'{type(exc).__name__}: {exc}'
    finally:
        capture.complete_command(nonce)
    return sample


def validate_sample(sample, previous=None, *, source=None, stopped=False):
    if sample.get('error'):
        raise RuntimeError(sample['error'])
    if sample['status'] != 2 or not sample['era']:
        raise RuntimeError('physical_reference_not_ready')
    if sample['configBits'] & 0x27f != 0x27f or sample['configBits'] & 256:
        raise RuntimeError('invalid_pit_configuration')
    if not sample['beforeUs'] <= sample['rawTicks'] // 24 <= sample['beforeUs'] + sample['widthUs']:
        raise RuntimeError('physical_read_bracket_inconsistent')
    if stopped:
        if sample['transport'] != 0 or sample['guardBits'] != 15:
            raise RuntimeError('stopped_idle_guard_refused')
    else:
        if sample['transport'] != 1 or sample['guardBits'] & 9 != 9:
            raise RuntimeError('moving_guard_refused')
        if sample['source'] != source:
            raise RuntimeError('unexpected_source')
        if not sample['epoch'] or not sample['periodUs']:
            raise RuntimeError('missing_epoch_or_period')
    if previous is not None:
        for key in ('era',) if stopped else ('era', 'epoch', 'source'):
            if sample[key] != previous[key]:
                raise RuntimeError('changed_' + key)
        if sample['rawTicks'] <= previous['rawTicks']:
            raise RuntimeError('nonincreasing_physical_ticks')
        if not stopped and sample['musicalTick'] <= previous['musicalTick']:
            raise RuntimeError('nonincreasing_musical_ticks')


def distribution(values):
    return dict(count=len(values), min=min(values), median=statistics.median(values), max=max(values)) if values else dict(count=0)


def evaluate(samples, *, source, points=5):
    issues = []
    for i, sample in enumerate(samples):
        try:
            validate_sample(sample, samples[i - 1] if i else None, source=source)
        except (RuntimeError, KeyError) as exc:
            issues.append(str(exc))
    if points not in (5, 6) or len(samples) != points:
        issues.append('sample_count_must_be_5_or_6')
    residuals = []
    if source == 0 and not issues:
        for a, b in zip(samples, samples[1:]):
            if a['periodUs'] == b['periodUs']:
                residuals.append((b['rawTicks'] - a['rawTicks']) / 24 -
                                 (b['musicalTick'] - a['musicalTick']) * b['periodUs'])
    return dict(ok=not issues, issues=issues, internal_residual_us=distribution(residuals),
                residual_scope='Observed dtPIT - deltaTicks*periodUs only for stable period; no acceptance threshold')


class ClockSendProbe:
    """Bounded call metadata only, never a payload queue or wire-arrival receipt."""
    def __init__(self, port, *, limit=8000, now=time.monotonic_ns):
        if not 1 <= limit <= 8000:
            raise ValueError('clock_attempt_limit')
        self.port, self.limit, self.now = port, limit, now
        self.attempts = []

    def __getattr__(self, name):
        return getattr(self.port, name)

    def send(self, message):
        if message.type != 'clock':
            return self.port.send(message)
        if len(self.attempts) >= self.limit:
            raise RuntimeError('clock_attempt_limit_before_send')
        entry = dict(before_ns=self.now(), after_ns=None, ok=False)
        self.attempts.append(entry)
        try:
            result = self.port.send(message)
            entry['ok'] = True
            return result
        finally:
            entry['after_ns'] = self.now()

    def report(self, bpm):
        before = [a['before_ns'] for a in self.attempts]
        intervals = [(b - a) / 1e6 for a, b in zip(before, before[1:])]
        return dict(attempts=self.attempts, interval_ms=distribution(intervals),
                    nominal_interval_ms=60000 / bpm / 24,
                    lateness='Unavailable: master pump absolute deadlines not exposed',
                    scope='Host send-call intervals only, including failed attempts; NOT wire arrival')


def run_pit_motion(args):
    from hitl.actions.buttons import press_transport
    from hitl.context import ActionContext
    from hitl.layered_cli import config_from_runner_args
    from hitl.managed_capture import active_managed_capture
    from hitl.midi_clock import MidiClockMaster
    from hitl.serial_follow import ExternalSerialFollowCollector
    from hitl.session import open_midi_session
    from hitl.stimuli.registry import STIM_PIT_MOTION_TEMP_01

    legacy = list(getattr(args, 'legacy_args', []))
    config = config_from_runner_args(args, legacy)
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--pit-motion-points', type=int, default=5)
    ns, _ = parser.parse_known_args(legacy)
    config.out_dir.mkdir(parents=True, exist_ok=True)
    path = config.out_dir / 'pit-motion.json'
    if path.exists():
        return 2
    report = dict(ok=False, verdict='FAIL', limitations=LIMITATIONS, legs=[], issues=[],
                  stimulus=STIM_PIT_MOTION_TEMP_01.fingerprint_key(), points=ns.pit_motion_points,
                  max_run_s=60, idle_wait_s=20, interval_s=1)
    session = collector = master = None
    deadline = time.monotonic() + 60
    request_id = 0
    era = None

    def save():
        path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    def remaining():
        left = deadline - time.monotonic()
        if left <= 0:
            raise RuntimeError('maximum_60s_exceeded')
        return left

    def query():
        nonlocal request_id, era
        s = observe(capture, request_id, timeout_s=min(2, remaining()))
        request_id += 1
        report.setdefault('observations', []).append(s)
        save()
        if not s.get('error'):
            if era is not None and s['era'] != era:
                raise RuntimeError('changed_era_between_legs')
            era = s['era']
        return s

    def idle():
        until = min(deadline, time.monotonic() + 20)
        while True:
            remaining()
            s = query()
            # Only busy guards may wait. Protocol/physical faults abort immediately.
            validate_sample(dict(s, transport=0, guardBits=15), stopped=True)
            if s['transport'] == 0 and s['guardBits'] == 15:
                report.setdefault('preflights', []).append(preflight(capture))
                # Last query immediately before Start also proves fresh stopped guard.
                s = query()
                validate_sample(s, stopped=True)
                remaining()
                return s
            if time.monotonic() >= until:
                raise RuntimeError('stopped_storage_idle_timeout')
            time.sleep(min(.2, remaining()))

    try:
        save()
        if ns.pit_motion_points not in (5, 6):
            raise RuntimeError('points_must_be_5_or_6')
        capture = active_managed_capture()
        if capture is None or capture.session_log is None or config.serial_port is not None:
            raise RuntimeError('managed_outer_serial_owner_required')
        collector = ExternalSerialFollowCollector(capture.session_log, out_dir=config.out_dir)
        collector.start()
        session = open_midi_session(midi_out_name=config.midi_out_name, midi_in_name=config.midi_in_name, serial_port=None)
        session.collector = collector
        action = ActionContext(session=session, config=config)
        for source, bpm in ((0, None), (1, 120), (1, 300)):
            leg = dict(source=source, bpm=bpm, samples=[])
            report['legs'].append(leg)
            leg['before_start'] = idle()
            probe = None
            try:
                if source == 0:
                    press_transport(action)
                else:
                    probe = ClockSendProbe(session.out_port)
                    master = MidiClockMaster(probe)
                    master.start(bpm=bpm)
                start = time.monotonic()
                for index in range(ns.pit_motion_points):
                    time.sleep(min(max(0, start + index + 1 - time.monotonic()), remaining()))
                    remaining()
                    s = query()
                    leg['samples'].append(s)
                    save()
                    validate_sample(s, leg['samples'][-2] if index else None, source=source)
                    if master is not None and master.pump_error() is not None:
                        raise RuntimeError('clock_pump_failed: ' + str(master.pump_error()))
                leg.update(evaluate(leg['samples'], source=source, points=ns.pit_motion_points))
                if source == 0:
                    # Last moving row proves running; never toggle blindly on failure.
                    press_transport(action)
                else:
                    master.stop()  # API retires pump BEFORE sending Stop.
                    if master.pump_error() is not None:
                        raise RuntimeError('clock_pump_failed: ' + str(master.pump_error()))
                    leg['host_pulses_sent'] = master.pulses_sent()
                    master = None
                leg['after_stop'] = query()
                # Final receipt is observational; idle guard is required at the NEXT
                # Start, not inferred from Stop delivery or host pulse count.
                validate_sample(dict(leg['after_stop'], transport=0, guardBits=15), stopped=True)
                leg['stop_comparison_scope'] = 'Observation only; no accepted-loss-free proof or tick/count equality: Stop-tail/transport grouping unproven'
            finally:
                if probe is not None:
                    # Pump is stopped in outer finally on failure before ports close.
                    if master is not None:
                        master.stop_pump()
                    leg['host_pacing'] = probe.report(bpm)
                save()
        report['ok'] = all(leg.get('ok', False) for leg in report['legs'])
        report['verdict'] = 'OBSERVED' if report['ok'] else 'FAIL'
    except Exception as exc:
        report['issues'].append(f'{type(exc).__name__}: {exc}')
        report['ok'], report['verdict'] = False, 'FAIL'
    finally:
        try:
            if master is not None:
                master.stop()
        except Exception as exc:
            report['issues'].append(f'clock_stop: {exc}')
            report['ok'], report['verdict'] = False, 'FAIL'
        finally:
            try:
                if session is not None:
                    session.close()
                elif collector is not None:
                    collector.stop()
            except Exception as exc:
                report['issues'].append(f'follower_or_MIDI_close: {exc}')
                report['ok'], report['verdict'] = False, 'FAIL'
            save()
    print('[pit_motion] ' + report['verdict'] + ': ' + str(path), flush=True)
    return 0 if report['ok'] else 2
