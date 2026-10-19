"""STIM-ROLLING-RECORD-TEMP-01: one managed serial owner, scoped scratch Record.

Clock and fixture generation reuse existing owners. This module never opens
serial, clears a latch, changes other-slot content or treats host time as arrival.
"""
from __future__ import annotations

import argparse
import json
import threading
import time
import uuid

from hitl.verify.rolling_record import (
    ROLLING_RECORD_BUILD_ENV,
    ROLLING_RECORD_PASS_GROUPS,
    ROLLING_RECORD_REPORT_SCHEMA_VERSION,
    aggregate_rolling_record_bundles,
    classify_rolling_record_pass,
    content_inventory,
    full_recipe_complete,
    parse_snapshot,
    qualify,
    rolling_record_contract_fingerprint,
    root_return_completed,
    stop_handoff,
    validate_sequence,
    wire_inventory,
    wire_schedule,
)

# The recipe's legs, in run order. `matched` (DEC-137) is the non-truncating Stop class: its take is
# stopped inside its own loop length, so the device must preserve the same physical reader.
LEGS = ('growth', 'empty', 'held', 'matched')


class WireListener:
    """Sole reader of the device MIDI port during the measured phase."""
    def __init__(self, port):
        self.port, self.rows, self.errors = port, [], []
        self.done = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)

    def _run(self):
        try:
            while not self.done.is_set():
                message = self.port.poll()
                if message is None:
                    self.done.wait(.001)
                    continue
                if message.type in ('note_on', 'note_off'):
                    if len(self.rows) >= 20000:
                        raise RuntimeError('wire_capture_capacity')
                    self.rows.append(dict(host_mono=time.monotonic(), **message.dict()))
        except Exception as exc:
            self.errors.append(str(exc))

    def start(self):
        self.thread.start()

    def close(self):
        self.done.set()
        self.thread.join(timeout=2)
        if self.thread.is_alive():
            raise RuntimeError('wire_listener_did_not_stop')


class NoInputConsumer:
    # Host-clock fixture needs no inbound clock. Keep real input exclusively
    # with WireListener, including the fixture helper's initial queue drain.
    def poll(self):
        return None


def fenced(capture, command, timeout_s=2):
    start, end = uuid.uuid4().hex, uuid.uuid4().hex
    try:
        accepted, detail = capture.send_command(command, nonce=start, end_nonce=end)
        if not accepted:
            raise RuntimeError('managed_command_refused:' + detail)
        rows = capture.fenced_response(start, end, timeout_s=timeout_s, poll_s=.01)
        if rows is None:
            raise RuntimeError('managed_end_fence_missing')
        return rows
    finally:
        capture.complete_command(start)


class Observations:
    def __init__(self, capture, report, save):
        self.capture, self.report, self.save = capture, report, save
        self.request, self.last_query = 0, 0.0

    def read(self):
        time.sleep(max(0, .25 - (time.monotonic() - self.last_query)))
        self.last_query = time.monotonic()
        self.request += 1
        before = time.monotonic()
        rows = fenced(self.capture, '!ROLLING_RECORD_STATUS ' + str(self.request))
        sample = parse_snapshot(rows, self.request)
        sample.update(host_before=before, host_after=time.monotonic())
        self.report['observations'].append(sample)
        self.save()
        return sample

    def until(self, predicate, *, timeout_s=15):
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            sample = self.read()
            if predicate(sample):
                return sample
        raise RuntimeError('rolling_observation_condition_timeout')


def _run_leg(name, *, config, session, capture, collector, observe, report, save):
    from hitl.context import ActionContext
    from hitl.actions.buttons import press_record
    from hitl.actions.slots import press_track, press_slot_record
    from hitl.actions.observation import await_markers
    from hitl.flows.stopped_preparation import clear_slot
    from hitl.edit_mode_precondition import ensure_loop_edit_before_record
    from hitl.legacy_record_baseline import _run_record_arm_until_capturing
    from hitl.legacy_edit_baseline import FixtureNote, _stream_fixture_record
    from hitl.midi_clock import MidiClockMaster
    from hitl.serial_collector import RunAbort
    from hitl.scenarios.pit_clock import preflight

    leg = dict(name=name, ok=False, issues=[], snapshots=[], wire=[], stop_outcome='NOT_RUN')
    report['legs'].append(leg)
    save()
    action = ActionContext(session, config)
    preflight(capture)
    press_track(action, 8)
    status = capture.selected_track_status(timeout_s=2)
    if not status or status['track'] != 7 or status['transport'] or status['state'] not in ('EMPTY', 'STOPPED'):
        raise RuntimeError('scratch_track_not_stopped')
    legacy = argparse.Namespace(press_ms=120, phase_wait_ms=500, state_sync_timeout_ms=5000,
                                serial_grace_ms=500, clear_press_ms=900, clear_before_record=False,
                                loop_slot=config.loop_slot, tempo_bpm=120.0)
    abort = RunAbort(run_deadline=time.monotonic() + 85, serial_collector=collector)
    master = MidiClockMaster(session.out_port)
    listener = WireListener(session.in_port)
    stream = None
    stream_errors = []
    try:
        baseline = len(collector.snapshot())
        press_slot_record(action, config.loop_slot)
        if not await_markers(lambda: collector.snapshot()[baseline:],
                [f'Button press: Loop {config.loop_slot} (short)'], timeout_s=3,
                cancel=lambda: abort.check() is not None):
            raise RuntimeError('slot_select_ack_unproven')
        master.stop()  # Filled-slot selection may toggle transport; stop is idempotent.
        deadline = time.monotonic() + 3
        status = None
        while time.monotonic() < deadline:
            status = capture.selected_track_status(timeout_s=1)
            if (status and status['track'] == 7 and status['slot'] == config.loop_slot - 1
                    and not status['transport'] and status['state'] in ('EMPTY', 'STOPPED', 'ARMED')):
                break
        if (not status or status['track'] != 7 or status['slot'] != config.loop_slot - 1
                or status['transport'] or status['state'] not in ('EMPTY', 'STOPPED', 'ARMED')):
            raise RuntimeError('scratch_selection_unproven_before_clear')
        def empty_slot():
            rows = fenced(capture, '!CAPTURE_CONTENT_SLOT ' + str(config.loop_slot - 1))
            return (any(r.startswith('CAPTURE_CONTENT,begin,') and r.endswith('length=0') for r in rows)
                    and 'CAPTURE_CONTENT,end' in rows
                    and not any(r.startswith('CAPTURE_CONTENT,event,') for r in rows))
        outcome = clear_slot(action, capture.session_log, track_index0=7, slot0=config.loop_slot - 1,
                             lines_reader=lambda _: collector.snapshot(), emptiness_reader=empty_slot)
        leg['clear'] = {'ok': outcome.ok, 'status': str(outcome.status)}
        if not outcome.ok:
            raise RuntimeError('scoped_record_preparation_unconfirmed')
        status = capture.selected_track_status(timeout_s=2)
        if not status or status['track'] != 7 or status['slot'] != config.loop_slot - 1:
            raise RuntimeError('scratch_selection_unproven_after_clear')
        if not ensure_loop_edit_before_record(session.out_port, collector, phase_wait_ms=500, skip=False):
            raise RuntimeError('loop_edit_unproven')
        status = capture.selected_track_status(timeout_s=2)
        if (not status or status['track'] != 7 or status['slot'] != config.loop_slot - 1
                or status['transport'] or status['state'] not in ('EMPTY', 'STOPPED', 'ARMED')):
            raise RuntimeError('scratch_target_unproven_before_arm')
        leg['pre_arm_status'] = status
        # Fresh managed identity above replaces the legacy retained-DISP proof;
        # global Record is unchanged and no second slot-toggle is sent here.
        ok, _ = _run_record_arm_until_capturing(session.out_port, session.in_port, collector, legacy,
                track_baseline_len=baseline, record_loop_slot=None, abort=abort,
                select_loop_slot=False, master=master, tempo_bpm=120.0)
        if not ok:
            raise RuntimeError('record_start_unproven')
        # Helper setup is over; only listener may now read device note output.
        listener.start()
        fixture = ((FixtureNote(15, 65, 4),) if name == 'held' else
                   ((FixtureNote(4, 60, 2), FixtureNote(36, 64, 2)) if name == 'empty' else
                    ((FixtureNote(4, 62, 2),) if name == 'matched' else
                     (FixtureNote(4, 60, 2), FixtureNote(20, 62, 2), FixtureNote(36, 64, 2)))))
        pitches = [n.pitch for n in fixture]
        leg['fixture'] = [dict(step=n.step, pitch=n.pitch, gate_steps=n.gate_steps) for n in fixture]
        origin = master.grid_origin_pulse()
        if origin is None:
            raise RuntimeError('host_grid_origin_missing')

        # `matched` (DEC-138): a **one-bar** take. The loop length is 768 from the start, so the observed
        # period already *is* the final length — a growing take cannot satisfy the non-truncating class,
        # because its period only grows at a full wrap and it then runs past the final extent (measured:
        # attempt 8-04 stopped at take tick 2080 inside a 2304-tick loop while the observed period was
        # streamer **on the grid** (`stop_press_advance_clocks`), because a host-observed press lands
        # 270–590 ticks late (measured) while the grid press lands ~160 ticks late (attempt 8-03).
        #
        # The advance is set so the **first post-stop sample lands inside the same cycle**: a snapshot
        # query costs ~136 ticks (measured in `matched-fix-01`: press ~640 → first `recording == 0`
        # sample at 776, i.e. past the 768 wrap), and DEC-138's matched terminal compares the era and
        # the serving `(currentRole, currentStorage)` across the Stop. Sampling after the wrap rotates
        # the serving view legitimately and makes both terms unsatisfiable for reasons that have
        # nothing to do with the Stop. 46 clocks = 368 ticks before the phase ends ⇒ nominal press
        # ~400 ⇒ measured landing ~560, leaving ~200 ticks to the wrap for the query to fit in, while
        # still leaving the pre-stop sample comfortably inside the take.
        target_bars = {'held': 4, 'matched': 1}.get(name, 8)
        stop_press_advance_clocks = 46 if name == 'matched' else 0

        def send_fixture():
            try:
                counts = _stream_fixture_record(session.out_port, NoInputConsumer(), fixture=fixture,
                    target_bars=target_bars, midi_channel_1based=config.midi_channel,
                    step_clocks=6, master=master, grid_origin_index=origin, abort=abort,
                    stop_press_advance_clocks=stop_press_advance_clocks,
                    max_seconds_guard=25)
                leg['fixture_counts'] = list(counts)
            except Exception as exc:
                stream_errors.append(str(exc))

        stream = threading.Thread(target=send_fixture, daemon=True)
        stream.start()
        initial_index = len(report['observations'])
        if name == 'held':
            begin = observe.until(lambda s: s['recording'] and 1152 <= s['tick'] <= 1344)
            qualify(begin, slot=config.loop_slot)
            finish = observe.until(lambda s: s['recording'] and 2496 <= s['tick'] <= 2784)
            qualify(finish, slot=config.loop_slot)
        elif name == 'matched':
            # Non-truncating Stop (DEC-138): one pre-stop observation of the one-bar take. Its period is
            # the final length from the start, and the window closes before the stream's grid-timed Stop
            # (take tick 480) so the sample cannot race it.
            # The pre-stop sample only has to be a *recording* observation of the one-bar take with
            # content; the former narrow phase window (`256..440`) was a timing cliff that the grid
            # press can step over, and it added no discrimination over these terms.
            begin = finish = observe.until(
                lambda s: s['recording'] and s['framePeriod'] == 768 and s['period'] == 768
                and s['rawMax'] >= 200,
                timeout_s=20)
            qualify(begin, slot=config.loop_slot)
        else:
            begin = observe.until(lambda s: s['recording'] and s['framePeriod'] == 2304
                                  and s['start'] == 0 and s['lastTick'] + 32 <= (s['tick'] - s['frameOrigin']) % 2304 <= 736,
                                  timeout_s=20)
            qualify(begin, slot=config.loop_slot)
            cycle = (begin['tick'] - begin['frameOrigin']) // 2304
            finish = observe.until(lambda s: s['recording'] and s['framePeriod'] == 2304
                                   and s['frameOrigin'] == begin['frameOrigin'] and s['start'] == 0
                                   and (s['tick'] - s['frameOrigin']) // 2304 == cycle + 1
                                   and s['lastTick'] + 32 <= (s['tick'] - s['frameOrigin']) % 2304 <= 736,
                                   timeout_s=10)
            qualify(finish, slot=config.loop_slot)
        observe.read()  # Upper bracket for notes received while the final reply was in flight.
        all_samples = report['observations'][initial_index:]
        samples = [s for s in all_samples if s['id'] >= begin['id']]
        for sample in samples:
            qualify(sample, slot=config.loop_slot)
            # The quiet-replay currency check belongs to the legs whose claims read the served take; the
            # matched leg's single pre-stop sample is checked by `stop_handoff` itself (the frozen serving
            # must carry the take's own token), and a growing take legitimately bumps `liveToken` between
            # reads (measured: attempt 8-05 raised here with no content claim at stake).
            if (name not in ('held', 'matched')
                    or (name == 'held' and sample['framePeriod'] == 1536)) and \
                    sample['captureToken'] != sample['liveToken']:
                raise RuntimeError('quiet_replay_current_token_stale')
        validate_sequence(samples)
        if name in ('growth', 'empty'):
            starts = {s['start'] for s in samples if s['framePeriod'] == 2304}
            if starts != {0, 768, 1536}:
                raise RuntimeError('later_intervals_not_observed')
            if name == 'empty' and not any(s['start'] == 768 and s['rows'] == 0 for s in samples):
                raise RuntimeError('empty_interval_not_observed')
        if finish['ledger'] != 0:
            raise RuntimeError('replay_window_ledger_not_closed')
        if name == 'matched':
            # The matched leg stops inside the take, so its replay window cannot span a full cycle. Its
            # claim is the non-truncating Stop class; the scheduled inventory belongs to growth/empty.
            leg['wire_inventory'] = {'ok': None, 'notes': len(listener.rows),
                                     'scope': 'matched leg: Stop class only; inventory belongs to growth/empty'}
        else:
            ordered_pitches = pitches if name == 'held' else pitches[1:] + pitches[:1]
            leg['wire_inventory'] = wire_inventory(listener.rows, start=begin['host_after'],
                end=finish['host_after'], channel=config.midi_channel - 1, pitches=ordered_pitches)
        leg['snapshots'] = samples
        stop_baseline = len(collector.snapshot())
        stop_sent_by_stream = name == 'matched'
        if stop_sent_by_stream:
            # The fixture streamer pressed the Stop on the grid (see `stop_press_advance_clocks`), because
            # a host-observed press lands 270–590 ticks late — more than the slack left inside the take, so
            # it could only ever truncate. The stream's remaining steps carry no notes, so nothing is input
            # after the Stop and the replay windows stay input-free.
            before_stop = finish
        else:
            # End fixture before control gestures; no input remains in replay windows.
            stream.join(timeout=10)
            if stream.is_alive() or stream_errors or listener.errors or master.pump_error():
                raise RuntimeError('fixture_or_wire_thread_failed:' + repr(stream_errors + listener.errors))
            before_stop = observe.read()
            qualify(before_stop, slot=config.loop_slot)
        if not stop_sent_by_stream:
            # Exactly one Record stop request, never a blind retry (the matched leg's Stop is the stream's).
            press_record(action)
        after_stop = observe.until(lambda s: not s['recording'],
                                   timeout_s=12 if stop_sent_by_stream else 8)
        if stop_sent_by_stream:
            stream.join(timeout=10)
            if stream.is_alive() or stream_errors or listener.errors or master.pump_error():
                raise RuntimeError('fixture_or_wire_thread_failed:' + repr(stream_errors + listener.errors))
        leg['stop'] = {'before': before_stop, 'after': after_stop}
        stop_samples = [after_stop]
        until = time.monotonic() + 6
        while time.monotonic() < until:
            stop_samples.append(observe.read())
        leg['stop']['observations'] = stop_samples
        leg['record_evidence_drain'] = capture.drain_record_evidence(master.send_realtime_stop, timeout_s=30)
        if not leg['record_evidence_drain'].get('complete'):
            raise RuntimeError('record_evidence_drain_incomplete')
        master.stop_pump()
        content_rows = fenced(capture, '!CAPTURE_CONTENT_SLOT ' + str(config.loop_slot - 1), timeout_s=20)
        leg['content_rows'] = content_rows
        leg['content'] = content_inventory(content_rows, pitches=pitches, channel=config.midi_channel)
        from hitl.serial.protocol import extract_recs_stop_lengths
        stops = extract_recs_stop_lengths(collector.snapshot()[stop_baseline:])
        lengths = {(s['raw_length'], s['final_length']) for s in stops}
        if len(lengths) != 1:
            raise RuntimeError('stop_geometry_receipt_missing_or_ambiguous')
        stop_receipt = stops[-1]
        leg['stop']['geometry'] = stop_receipt
        # DEC-136 fact, recorded beside the ordered label: the device may serve the frozen content
        # through a corrected-role view first and still complete the root return afterwards.
        leg['stop']['root_return_completed'] = root_return_completed(
            before_stop, stop_samples, stop_receipt, slot=config.loop_slot)
        # The stop contract derives the matching/mismatching class from the receipt itself and reads the
        # whole post-stop sequence: the DEC-136 held terminal is a bound frozen phase followed by the
        # root-backed committed takeover and release, which no single sample can prove.
        try:
            leg['stop_outcome'] = stop_handoff(before_stop, stop_samples, stop_receipt,
                                              slot=config.loop_slot)
        except RuntimeError as exc:
            raise RuntimeError('stop_handoff_unproven:' + repr([str(exc)]))
        if name == 'matched':
            # Graded elsewhere: the matched leg's own claim is the preserved reader, not the interval
            # schedule (its window is a single cycle); the raw rows stay in the leg for evidence.
            leg['wire_schedule'] = {'ok': None, 'skipped': 'matched leg: Stop class only'}
        else:
            leg['wire_schedule'] = wire_schedule(leg['wire_inventory']['notes'],
                leg['content']['record'], all_samples, begin, held_growth=(name == 'held'))
        accepted = {(r['tick'], r['noteId']) for r in leg['content']['record']}
        for sample in samples:
            if sample['rows'] and ((sample['firstTick'], sample['firstNoteId']) not in accepted
                                   or (sample['lastTick'], sample['lastNoteId']) not in accepted):
                raise RuntimeError('served_endpoint_not_in_accepted_content')
        if leg['stop_outcome'] == 'corrected_frozen_root_return':
            for sample in stop_samples:
                if not sample['stopJob'] and sample['rows'] and (
                        (sample['firstTick'], sample['firstNoteId']) not in accepted
                        or (sample['lastTick'], sample['lastNoteId']) not in accepted):
                    raise RuntimeError('root_return_serves_foreign_content')
        leg['postflight'] = preflight(capture)
        leg['ok'] = True
    except Exception as exc:
        leg['issues'].append(f'{type(exc).__name__}: {exc}')
        save()
        raise
    finally:
        abort.run_deadline = time.monotonic()
        if stream is not None:
            stream.join(timeout=2)
        print('[rolling-record] cleanup: stop clock', flush=True)
        master.stop()
        print('[rolling-record] cleanup: close wire listener', flush=True)
        if listener.thread.ident is not None:
            listener.close()
        leg['wire'] = listener.rows
        leg['wire_errors'] = listener.errors
        save()
        print('[rolling-record] cleanup: leg saved', flush=True)
    return leg


def run_rolling_record(args):
    from hitl.layered_cli import config_from_runner_args
    from hitl.managed_capture import active_managed_capture
    from hitl.serial_follow import ExternalSerialFollowCollector
    from hitl.session import open_midi_session
    from hitl.boot_handoff import wait_for_boot_handoff
    from hitl.stimuli.registry import STIM_ROLLING_RECORD_TEMP_01
    from hitl.stimuli import print_stimulus_banner, write_headered_snapshot

    config = config_from_runner_args(args, list(getattr(args, 'legacy_args', [])))
    spec = STIM_ROLLING_RECORD_TEMP_01
    if not spec.authorization.allows(track=config.track_number, slots=(config.loop_slot,)) or config.midi_channel != 8:
        raise RuntimeError('rolling_record_unauthorized_target_or_channel')
    pass_group = getattr(args, 'rolling_record_pass', None)
    legacy_parser = argparse.ArgumentParser(add_help=False)
    legacy_parser.add_argument('--rolling-record-legs', choices=tuple(ROLLING_RECORD_PASS_GROUPS), default=None)
    legacy_args = list(getattr(args, 'legacy_args', []))
    legacy_ns, _ = legacy_parser.parse_known_args(legacy_args)
    if pass_group is None:
        pass_group = legacy_ns.rolling_record_legs or 'all'
    elif legacy_ns.rolling_record_legs is not None and legacy_ns.rolling_record_legs != pass_group:
        raise RuntimeError('rolling_record_pass_selector_conflict')
    if pass_group not in ROLLING_RECORD_PASS_GROUPS:
        raise RuntimeError('rolling_record_pass_group_unknown')
    if config.build_env != ROLLING_RECORD_BUILD_ENV:
        raise RuntimeError('rolling_record_requires_qualification_build_env')
    capture = active_managed_capture()
    if capture is None or capture.session_log is None or config.serial_port is not None:
        raise RuntimeError('rolling_record_managed_owner_required')
    report_path = config.out_dir / 'rolling-record.json'
    if report_path.exists():
        raise RuntimeError('rolling_record_existing_evidence')
    report = dict(
        report_schema_version=ROLLING_RECORD_REPORT_SCHEMA_VERSION,
        pass_group=pass_group,
        validation_contract_fingerprint=rolling_record_contract_fingerprint(),
        ok=False,
        verdict='FAIL',
        functional_legs_ok=False,
        full_recipe_complete=False,
        stimulus=spec.fingerprint_key(),
        authorization=spec.authorization.as_dict(),
        observations=[],
        legs=[],
        issues=[],
        session_log=str(capture.session_log),
        deadline_safety_proven=False,
        stop_terminals_by_leg={},
        limitations=['Host send/capture time is not wire arrival or device WCET',
                     'Snapshot is explicit observation, not continuous dispatch instrumentation'],
    )
    def save():
        report_path.write_text(json.dumps(report, indent=2) + '\n')
    collector = ExternalSerialFollowCollector(capture.session_log, out_dir=config.out_dir)
    session = None
    try:
        print_stimulus_banner(spec)
        collector.start()
        if wait_for_boot_handoff(collector, timeout_s=30) == 'timeout':
            raise RuntimeError('boot_handoff_unproven')
        session = open_midi_session(midi_out_name=config.midi_out_name,
                                   midi_in_name=config.midi_in_name, serial_port=None)
        session.collector = collector
        observe = Observations(capture, report, save)
        for name in ROLLING_RECORD_PASS_GROUPS[pass_group]:
            _run_leg(name, config=config, session=session, capture=capture, collector=collector,
                     observe=observe, report=report, save=save)
        outcomes = {leg['stop_outcome'] for leg in report['legs']}
        report['stop_terminals_by_leg'] = {leg['name']: leg['stop_outcome'] for leg in report['legs']}
        report['matching_stop_exercised'] = 'matching_physical_reader_preserved' in outcomes
        report['mismatching_stop_exercised'] = bool(
            {'corrected_frozen_handoff', 'corrected_frozen_root_return'} & outcomes)
        report.update(classify_rolling_record_pass(pass_group, report['legs']))
        report['ok'] = report['full_recipe_complete'] if pass_group == 'all' else report['functional_legs_ok']
    except Exception as exc:
        report['issues'].append(f'{type(exc).__name__}: {exc}')
        save()
        print('[rolling-record] failure saved: ' + repr(report['issues']), flush=True)
    finally:
        print('[rolling-record] cleanup: defer MIDI close until managed shutdown', flush=True)
        if session is not None:
            capture.defer_midi_close(session)
        else:
            collector.stop()
        print('[rolling-record] cleanup: snapshot before managed shutdown', flush=True)
        write_headered_snapshot(config.out_dir / 'rolling-record-serial.log', collector.snapshot(), spec)
        save()
    print('[rolling-record] ' + report['verdict'] + ': ' + repr(report['issues']), flush=True)
    return 0 if report['ok'] else 2
