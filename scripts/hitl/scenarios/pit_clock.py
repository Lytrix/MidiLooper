"""Stopped-only PIT qualification via the canonical managed serial owner.

Recipe: STIM-PIT-CLOCK-TEMP-01, one fenced DIAG,pit_obs row per second;
no MIDI gestures, content authorization, new firmware tags or RAM1 expenditure.
Firmware tier/fit/build qualification belongs to the observation-image packet.
Production physical reads bracket rawTicks under preserved outer PRIMASK; the
separate readCycles read preserves incoming mask. These are contract assumptions,
not host proof of register semantics or a WCET/arrival-delay measurement.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import statistics
import time
import uuid

FIELDS = ('id', 'status', 'era', 'rawTicks', 'beforeUs', 'widthUs',
          'musicalTick', 'epoch', 'readCycles', 'cpuHz', 'configBits', 'transport', 'source')
BITS = (32, 32, 32, 64, 64, 32, 64, 32, 32, 32, 32, 1, 1)
ORACLE = {
    'provenance': 'owner-approved I5 running-Capture PIT hardware qualification request',
    'ready_status': 2, 'mandatory_config_mask': 0x27f,
    'forbidden_config_mask': 1 << 8, 'low_carry_ticks': 1 << 32,
    'nominal_hz': 24_000_000, 'startup_raw_ticks_exclusive': 24_000_000 * 60,
    'rate_acceptance_hz': [23_900_000, 24_100_000],
    'rate_basis': 'host monotonic_ns first/last enclosing command brackets; interval overlap',
    'rate_scope': 'gross functional check (~0.42%), not absolute calibration or fitted ppm',
    'default_duration_s': 210, 'sample_interval_s': 1, 'maximum_duration_s': 240,
    'full_duration_min_s': 210, 'latency_threshold': None,
    'latency_scope': 'observed production read duration only; no WCET claim',
    'correlation_scope': 'stopped point correlation only; no musical due, external arrival, or correlation-rate claim',
    'qualification_limits': 'Run mode only; no debugger/sleep qualification; no parent 29/60 checkoff',
    'shutdown_owner': 'managed outer DeviceSession: two Stops, drains, postflight, safety latch',
}


def parse_observation(rows, request_id):
    markers = [row.strip() for row in rows if ',DIAG,pit_obs,' in row]
    if len(markers) != 1:
        raise RuntimeError('missing_or_duplicate_pit_obs')
    parts = markers[0].split(',')
    if len(parts) != 17 or parts[0] != '#CAP' or parts[2:4] != ['DIAG', 'pit_obs']:
        raise RuntimeError('invalid_pit_obs_contract')
    if any(re.fullmatch(r'[0-9]+', value) is None for value in [parts[1], *parts[4:]]):
        raise RuntimeError('invalid_pit_obs_integer')
    values = list(map(int, parts[4:]))
    if int(parts[1]) >= 1 << 32 or any(value >= 1 << bits for value, bits in zip(values, BITS)):
        raise RuntimeError('pit_obs_integer_range')
    sample = dict(zip(FIELDS, values))
    if sample['id'] != request_id:
        raise RuntimeError('pit_obs_request_id_mismatch')
    sample.update(marker=markers[0], capMicros=int(parts[1]))
    return sample


def delayed_observation_rows(lines, nonce, end_nonce, request_id):
    """A ring-delayed row may follow its end fence; still require both exact fences.

    The scenario has only this one request in flight and IDs never repeat in a run.
    A missing fence or duplicate matched row never becomes successful evidence.
    """
    from hitl.serial.protocol import fence_token
    starts = [i for i, line in enumerate(lines) if fence_token(line) == nonce]
    ends = [i for i, line in enumerate(lines) if fence_token(line) == end_nonce]
    if len(starts) != 1 or len(ends) != 1 or ends[0] <= starts[0]:
        return None
    prefix = f',DIAG,pit_obs,{request_id},'
    matched = [line for line in lines[starts[0] + 1:] if prefix in line]
    return matched or None


def observe(capture, request_id, *, now=time.monotonic_ns):
    if not 0 <= request_id < 1 << 32:
        raise RuntimeError('request_id_range')
    nonce, end_nonce = uuid.uuid4().hex, uuid.uuid4().hex
    sample = dict(id=request_id, nonce=nonce, end_nonce=end_nonce)
    sample['s_ns'] = now()  # immediately before enqueueing with the serial owner
    deadline = time.monotonic() + 2.0
    try:
        accepted, detail = capture.send_command(f'!PIT_OBSERVE {request_id}',
                                               nonce=nonce, end_nonce=end_nonce)
        if not accepted:
            raise RuntimeError('managed_command_refused: ' + detail)
        rows = capture.fenced_response(nonce, end_nonce, timeout_s=2.0, poll_s=0.01)
        if rows is None:
            raise RuntimeError('pit_obs_end_fence_missing')
        # Ring fallback can arrive after the immediate end fence. Keep the host
        # bracket open until its ID-matched row ALSO arrives, with the same bound.
        if not any(',DIAG,pit_obs,' in row for row in rows):
            while True:
                log = capture.session_log
                if log is not None and log.is_file():
                    delayed = delayed_observation_rows(
                        log.read_text(encoding='utf-8', errors='replace').splitlines(),
                        nonce, end_nonce, request_id)
                    if delayed is not None:
                        rows = delayed
                        break
                if time.monotonic() >= deadline:
                    raise RuntimeError('pit_obs_delayed_row_missing')
                time.sleep(0.01)
        sample['r_ns'] = now() # both end fence and row observed, including delayed delivery
        sample['response_rows'] = rows
        sample.update(parse_observation(rows, request_id))
    except Exception as exc:
        sample['error'] = f'{type(exc).__name__}: {exc}'
    finally:
        # fenced_response also completes; idempotent release covers refusal/exception.
        capture.complete_command(nonce)
    return sample


def preflight(capture):
    status = capture.selected_track_status(timeout_s=2.0)
    overlay = capture.overlay_status(timeout_s=2.0)
    if not status or status.get('transport') != 0 or status.get('state') not in ('EMPTY', 'STOPPED'):
        raise RuntimeError('fresh_stopped_status_missing')
    if not overlay or overlay.get('open') != 0 or overlay.get('phase') != 0:
        raise RuntimeError('fresh_closed_idle_overlay_missing')
    return dict(selected_track_status=status, overlay_status=overlay)


def validate_sample(sample, previous=None):
    if sample.get('error'):
        raise RuntimeError(sample['error'])
    if sample['status'] != 2 or sample['era'] == 0:
        raise RuntimeError('physical_reference_not_ready')
    if sample['configBits'] & 0x27f != 0x27f or sample['configBits'] & (1 << 8):
        raise RuntimeError('invalid_pit_configuration_or_highTIF')
    if sample['readCycles'] == 0 or sample['cpuHz'] == 0:
        raise RuntimeError('DWT_or_CPU_unavailable')
    if sample['transport'] != 0:
        raise RuntimeError('transport_not_stopped')
    # Match production integer microsecond conversion, including sub-us remainder.
    if not sample['beforeUs'] <= sample['rawTicks'] // 24 <= sample['beforeUs'] + sample['widthUs']:
        raise RuntimeError('physical_read_bracket_inconsistent')
    if sample['s_ns'] > sample['r_ns']:
        raise RuntimeError('invalid_host_bracket')
    if previous is None:
        if sample['rawTicks'] >= ORACLE['startup_raw_ticks_exclusive']:
            raise RuntimeError('fresh_boot_startup_gate')
    else:
        for key in ('era', 'epoch', 'musicalTick', 'transport', 'source', 'cpuHz'):
            if sample[key] != previous[key]:
                raise RuntimeError('changed_' + key)
        if sample['rawTicks'] <= previous['rawTicks'] or sample['beforeUs'] < previous['beforeUs']:
            raise RuntimeError('nonmonotonic_physical_clock')
        if sample['s_ns'] <= previous['r_ns']:
            raise RuntimeError('overlapping_or_nonmonotonic_host_brackets')
    if sample['rawTicks'] >= 1 << 32 and not sample['configBits'] & (1 << 7):
        raise RuntimeError('lowTIF_missing_after_carry')


def rate_interval(samples):
    first, last = samples[0], samples[-1]
    ticks = last['rawTicks'] - first['rawTicks']
    wide = last['r_ns'] - first['s_ns']
    narrow = last['s_ns'] - first['r_ns']
    if ticks <= 0 or narrow <= 0 or wide < narrow:
        raise RuntimeError('nonpositive_rate_interval')
    return [ticks * 1e9 / wide, ticks * 1e9 / narrow]


def evaluate(samples, duration_s):
    result = dict(ok=False, verdict='FAIL', issues=[], startup='NOT_RUN',
                  latching='NOT_RUN', rate='NOT_RUN', latency='NOT_RUN',
                  stopped_point_correlation='NOT_RUN', samples=samples)
    valid = []
    for sample in samples:
        try:
            validate_sample(sample, valid[-1] if valid else None)
            valid.append(sample)
            result['startup'] = 'PASS'
        except (RuntimeError, KeyError) as exc:
            result['issues'].append(str(exc))
            if not valid:
                result['startup'] = 'FAIL'
            break
    if valid:
        latencies = sorted(s['readCycles'] * 1e9 / s['cpuHz'] for s in valid)
        result['observed_read_latency_ns'] = dict(min=latencies[0], median=statistics.median(latencies),
            p95=latencies[max(0, math.ceil(len(latencies) * .95) - 1)], max=latencies[-1], samples=len(latencies))
        result['latency'] = 'OBSERVED_NO_WCET'
        result['stopped_point_correlation'] = 'PASS' if not result['issues'] else 'FAIL'
    full = duration_s >= 210
    carry = any(a['rawTicks'] < 1 << 32 <= b['rawTicks'] and
                (b['rawTicks'] & 0xffffffff) < (a['rawTicks'] & 0xffffffff) and
                (b['rawTicks'] >> 32) == (a['rawTicks'] >> 32) + 1 and
                b['configBits'] & (1 << 7) for a, b in zip(valid, valid[1:]))
    result['natural_first_low_carry_observed'] = carry
    result['latching'] = ('PASS' if carry and not result['issues'] else 'FAIL') if full else 'SMOKE_ONLY'
    if full and not carry:
        result['issues'].append('natural_first_low_carry_missing')
    if len(valid) >= 2:
        try:
            interval = rate_interval(valid)
            result['relative_rate_interval_hz'] = interval
            result['rate'] = 'PASS' if interval[0] <= 24_100_000 and interval[1] >= 23_900_000 else 'FAIL'
            if result['rate'] == 'FAIL':
                result['issues'].append('gross_relative_rate_interval_outside_acceptance')
        except RuntimeError as exc:
            result['rate'] = 'FAIL'
            result['issues'].append(str(exc))
    else:
        result['issues'].append('insufficient_rate_samples')
    valid_duration = math.isfinite(duration_s) and 3 <= duration_s <= 240
    if not valid_duration or len(valid) != math.ceil(duration_s) + 1:
        result['issues'].append('sample_count_missing_or_invalid_duration')
    if full and valid and (valid[-1]['s_ns'] - valid[0]['s_ns']) < 210e9:
        result['issues'].append('full_observation_duration_missing')
    result['ok'] = not result['issues']
    result['verdict'] = ('PASS' if full else 'SMOKE_PASS') if result['ok'] else 'FAIL'
    return result


def run_pit_clock(args):
    from hitl.layered_cli import config_from_runner_args
    from hitl.managed_capture import active_managed_capture
    from hitl.serial_follow import ExternalSerialFollowCollector
    from hitl.session import open_midi_session
    from hitl.stimuli.registry import STIM_PIT_CLOCK_TEMP_01

    legacy = list(getattr(args, 'legacy_args', []))
    config = config_from_runner_args(args, legacy)
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--pit-duration-s', type=float, default=210.0)
    ns, _ = parser.parse_known_args(legacy)
    config.out_dir.mkdir(parents=True, exist_ok=True)
    path = config.out_dir / 'pit-clock.json'
    if path.exists():
        print('[pit] existing_report_refused; use a new run directory', flush=True)
        return 2
    samples = []
    report = dict(ok=False, verdict='FAIL', oracle_manifest=dict(ORACLE),
                  stimulus=STIM_PIT_CLOCK_TEMP_01.fingerprint_key(),
                  duration_s=ns.pit_duration_s, samples=samples,
                  startup='NOT_RUN', latching='NOT_RUN', rate='NOT_RUN',
                  latency='NOT_RUN', stopped_point_correlation='NOT_RUN')
    report['managed_shutdown'] = 'OUTER_PENDING: consult shutdown-result.json and safety latch'
    session = collector = None
    def save():
        path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    try:
        if not math.isfinite(ns.pit_duration_s) or not 3 <= ns.pit_duration_s <= 240:
            raise RuntimeError('pit_duration_must_be_3_to_240_seconds')
        if config.boot_reset != 'force':
            raise RuntimeError('explicit_runner_boot_reset_required')
        capture = active_managed_capture()
        if capture is None or capture.session_log is None or config.serial_port is not None:
            raise RuntimeError('managed_outer_serial_owner_required')
        report['session_log'] = str(capture.session_log)
        save()  # thresholds and limitations pinned before any device query
        collector = ExternalSerialFollowCollector(capture.session_log, out_dir=config.out_dir)
        collector.start()
        session = open_midi_session(midi_out_name=config.midi_out_name,
                                    midi_in_name=config.midi_in_name, serial_port=None)
        session.collector = collector
        report['preflight'] = preflight(capture)
        start = time.monotonic()
        for request_id in range(math.ceil(ns.pit_duration_s) + 1):
            due = start + min(request_id, ns.pit_duration_s)
            time.sleep(max(0, due - time.monotonic()))
            if time.monotonic() > start + ns.pit_duration_s + 2:
                raise RuntimeError('bounded_observation_wall_time_exceeded')
            sample = observe(capture, request_id)
            if request_id == 0:
                start = sample['s_ns'] / 1e9
            samples.append(sample)
            save()  # preserve even invalid/missing/fenced-refused responses
            validate_sample(sample, samples[-2] if len(samples) > 1 else None)
        report.update(evaluate(samples, ns.pit_duration_s))
    except Exception as exc:
        report.update(evaluate(samples, ns.pit_duration_s))
        report['ok'], report['verdict'] = False, 'FAIL'
        report['issues'].append(f'{type(exc).__name__}: {exc}')
    finally:
        # MIDI/follower only. Never close the serial capture owner or duplicate its teardown.
        try:
            if session is not None:
                session.close()
            elif collector is not None:
                collector.stop()
        except Exception as exc:
            report['ok'], report['verdict'] = False, 'FAIL'
            report.setdefault('issues', []).append(f'follower_or_MIDI_close: {exc}')
        finally:
            save()
    print('[pit] ' + report['verdict'] + ': ' + str(path), flush=True)
    return 0 if report['ok'] else 2
