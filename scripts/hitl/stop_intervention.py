"""One-shot transport Stop tied to an actual overdub-stop button release.

Wrap the concrete output port only for the overdub pass. This also covers the
streamer's scheduled button sends and backends overriding BaseOutput.send.
Host timestamps show what was sent; only device evidence proves stop overlap.
"""
from __future__ import annotations

import threading
import time
from typing import Callable

import mido


class StopInterventionPort:
    def __init__(self, output, emit: Callable[[dict], None], *, delay_s: float = .305,
                 channel: int = 4, control_channel: int = 15, button: int = 36,
                 schedule=None):
        self.output, self.emit = output, emit
        self.delay_s, self.channel = delay_s, channel
        self.control_channel, self.button = control_channel, button
        self.schedule = schedule or self._schedule
        self.stream_seen = self.press_seen = self.armed = self.sent = False
        self.timer = None
        self.error = None

    def __getattr__(self, name):
        return getattr(self.output, name)

    @staticmethod
    def _schedule(delay, callback):
        timer = threading.Timer(delay, callback)
        timer.start()
        return timer

    def _event(self, event, **fields):
        self.emit({"host_ns": time.monotonic_ns(), "event": event, **fields})

    def send(self, message):
        control = (message.type in {"note_on", "note_off"}
                   and message.channel == self.control_channel and message.note == self.button)
        on = message.type == "note_on" and message.velocity > 0
        if message.type == "note_on" and message.channel == self.channel and on:
            self.stream_seen = True
        if control and on and self.stream_seen and not self.armed:
            self.output.send(mido.Message("note_on", channel=self.channel, note=83, velocity=100))
            self.press_seen = True
            self._event("held_capture_on", note=83)
        self.output.send(message)
        if control and not on and self.press_seen and not self.armed:
            self.armed = True
            self._event("stop_button_release_sent", delay_s=self.delay_s)
            self.timer = self.schedule(self.delay_s, self._interrupt)

    def _interrupt(self):
        try:
            self.output.send(mido.Message("stop"))
            self.sent = True
            self._event("midi_stop_sent")
            self.output.send(mido.Message("note_off", channel=self.channel, note=83, velocity=0))
            self._event("held_input_released", note=83)
        except Exception as exc:
            self.error = str(exc)
            self._event("intervention_error", detail=self.error)

    def finish(self):
        if self.timer is not None:
            self.timer.join()
        self._event("intervention_finished", stream_seen=self.stream_seen,
                    armed=self.armed, sent=self.sent, error=self.error)


def pending_job_stop_matches(lines, *, track=4, slot=0):
    """Prove Stop crossed a still-owned job, even after visible PLAYING."""
    from hitl.serial.protocol import parse_cap_micros
    from hitl.verify.midi_boundary_timing import extract_stop_timeline
    timeline = extract_stop_timeline(lines)
    if timeline['drops'] or timeline['malformed']:
        return []
    entries = sorted({r['at_us'] for r in timeline['states'] if r['valid']
                      and (r['track'], r['slot'], r['from_state'], r['to_state'])
                      == (track, slot, 6, 7)})
    stops = [parse_cap_micros(line) for line in lines if ',MI,U,252,' in line]
    grains = [r for r in timeline['grains'] if r['valid']
              and (r['track'], r['slot']) == (track, slot)]
    matches = []
    for index, entry in enumerate(entries):
        limit = entries[index + 1] if index + 1 < len(entries) else float('inf')
        for stop in stops:
            if stop is None or not entry < stop < limit:
                continue
            before = [r for r in grains if entry <= r['start_us'] < stop]
            if any(r['phase'] == 10 and r['finished_phase'] for r in before):
                continue
            for end in grains:
                if not (stop < end['start_us'] <= end['end_us'] < limit
                        and end['phase'] == 10 and end['finished_phase']
                        and end['destination'] == 1):
                    continue
                if any(r['generation'] == end['generation'] and r['destination'] == 0
                       and r['epoch'] < end['epoch'] for r in before):
                    matches.append((entry, stop, end['end_us']))
    return matches


def verify_interruption(lines, actions, midi_rows, *, channel=4, slot=0, observe_us=8_000_000):
    """Score only stop precedence/emission; excludes restart survival and layering."""
    from hitl.serial.protocol import parse_cap_micros, parse_track_transition

    issues = []
    sent = [a for a in actions if a.get('event') == 'midi_stop_sent']
    if len(sent) != 1:
        issues.append('expected_one_intervention_send')
    if any(',RING,overflow' in x or '#CAPTURE_RECONNECT' in x for x in lines):
        issues.append('incomplete_capture')
    entries, stops, destinations = [], [], []
    for line in lines:
        ts = parse_cap_micros(line)
        if ts is None:
            continue
        transition = parse_track_transition(line)
        if transition == ('OVERDUBBING', 'OVERDUB_STOPPING'):
            entries.append(ts)
        if transition == ('OVERDUB_STOPPING', 'STOPPED'):
            destinations.append(ts)
        if ',MI,U,252,' in line:
            stops.append(ts)
    matches = pending_job_stop_matches(lines, track=channel, slot=slot)
    # Legacy firmware retains the visible stopping state until completion.
    if not any(',DIAG,stop_grain,' in line for line in lines):
        matches = [(a, b, c) for a in entries for b in stops for c in destinations if a < b < c]
    if not matches:
        issues.append('no_device_stop_inside_stopping')
        return {'result': 'INCONCLUSIVE_CAPTURE', 'issues': issues}
    # Duplicate ST notifications represent the same event; retain earliest entry/destination.
    start, stop, end = min(matches)
    cap_times = [parse_cap_micros(x) for x in lines]
    final = max(t for t in cap_times if t is not None)
    if final - end < observe_us:
        issues.append('short_device_observation')
    note_on_after = []
    all_notes_off = []
    for line in lines:
        ts = parse_cap_micros(line)
        if ts is None or ts < stop:
            continue
        fields = line.split(',')
        if len(fields) >= 7 and fields[2] == 'MO':
            if fields[3] == '144' and int(fields[4]) == channel + 1 and int(fields[6]) > 0:
                note_on_after.append(ts)
            if fields[3:7] == ['176', str(channel + 1), '123', '0']:
                all_notes_off.append(ts)
        transition = parse_track_transition(line)
        if transition is not None and transition[1] == 'PLAYING':
            issues.append('returned_to_playing_after_stop')
    if note_on_after:
        issues.append('device_note_on_after_stop')
    if not all_notes_off:
        issues.append('all_notes_off_missing')
    closed = [r['host_ns'] for r in midi_rows if r.get('event') == 'monitor_closed']
    opened = [r['host_ns'] for r in midi_rows if r.get('event') == 'monitor_opened']
    if sent:
        stamp = sent[0]['host_ns']
        if not opened or not closed or min(opened) > stamp or max(closed) - stamp < observe_us * 1000:
            issues.append('usb_observation_not_bounded')
        if any(r.get('host_ns',0) >= stamp and r.get('message',{}).get('type') == 'note_on'
               and r['message'].get('channel') == channel and r['message'].get('velocity',0) > 0
               for r in midi_rows):
            issues.append('usb_note_on_after_stop')
    failed = any(i in issues for i in ('returned_to_playing_after_stop','device_note_on_after_stop','usb_note_on_after_stop'))
    return {'result': 'FAIL' if failed else 'INCONCLUSIVE_CAPTURE' if issues else 'PASS',
            'scope': 'single-track stop precedence and silence only', 'issues': issues,
            'stopping_start_us': start, 'stop_received_us': stop, 'stopped_us': end,
            'intervention_offset_us': stop-start, 'recovery_us': end-stop,
            'all_notes_off_delay_us': min(all_notes_off)-stop if all_notes_off else None,
            'observation_after_stopped_us': final-end,
            'restart_survival_verified': False, 'layered_playback_verified': False}


def strict_stop_transition_options(options, *, track=4, slot=0):
    """The focused fixture must send its stop request, not accept stale PLAYING.

    The existing send callback already suppresses a press sent by the streamer.
    No automatic retries: missing actual destination evidence is inconclusive.
    """
    from hitl.serial.protocol import parse_track_transition
    result = dict(options)
    if result.get('requested') == 'PLAYING_AFTER_OVERDUB_STOP':
        result['already_satisfied'] = lambda lines, baseline: False
        result['max_retries'] = 0
        result['confirmed'] = lambda lines, baseline: any(
            parse_track_transition(line) == ('OVERDUB_STOPPING', 'STOPPED')
            for line in lines[baseline:]) or bool(
                pending_job_stop_matches(lines[baseline:], track=track, slot=slot))
    return result


def strict_normal_stop_transition_options(options):
    """Require a fresh deferred-stop completion for the normal-stop checkpoint."""
    from hitl.serial.protocol import parse_track_transition
    result = dict(options)
    if result.get('requested') == 'PLAYING_AFTER_OVERDUB_STOP':
        result['already_satisfied'] = lambda lines, baseline: False
        result['max_retries'] = 0
        result['confirmed'] = lambda lines, baseline: any(
            parse_track_transition(line) == ('OVERDUB_STOPPING', 'PLAYING')
            for line in lines[baseline:])
    return result
