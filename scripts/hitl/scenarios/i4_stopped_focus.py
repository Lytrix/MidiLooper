"""Stopped NOTE_EDIT A/B/A qualification; only proven filled scratch slots."""
from __future__ import annotations
import argparse
import json
import re
import uuid
import time
import hashlib
from datetime import datetime
from pathlib import Path

from hitl.actions.buttons import press_edit, press_edit_long
from hitl.actions.slots import press_slot_record
from hitl.actions.observation import await_markers
from hitl.baseline_loop_inventory import latest_base_report, record_seed_qualification_issues
from hitl.boot_handoff import wait_for_boot_handoff
from hitl.context import ActionContext
from hitl.serial.protocol import parse_cap_micros
from hitl.scenarios.i4_checkpoint import verify_checkpoint
from hitl.scenarios.i4_overlay_load import checkpoint_identity, valid_human_proof

SOURCES = {4: ('captures/i4-stopped-focus-seed-a-01', 60),
           5: ('captures/i4-stopped-focus-seed-b-01', 84)}
CHECKPOINT = 'captures/i4-stopped-focus-checkpoint-01/i4-checkpoint.json'
IMAGE_SHA256 = '8050aabcf64dc7ecd634371c824dad7ee0709057ee8fa60c91c6a54e979942f7'


def source_bundle(directory, report_path, *, checkpoint=False):
    """Bind one managed run's report/metadata/shutdown and completion chronology."""
    directory = Path(directory).resolve()
    report_path = Path(report_path).resolve()
    if report_path.parent != directory:
        raise RuntimeError('source_report_directory_mismatch')
    report = json.loads(report_path.read_text())
    metadata_path = directory/'metadata.json'
    shutdown_path = directory/'shutdown-result.json'
    metadata = json.loads(metadata_path.read_text())
    shutdown = json.loads(shutdown_path.read_text())
    session = Path(metadata['session_log']).resolve()
    linked_session = (report.get('session_log') if checkpoint else
                      (report.get('config') or {}).get('follow_serial_log'))
    if (not linked_session or Path(linked_session).resolve() != session
            or session.parent != directory or not session.is_file()
            or metadata.get('firmware_hex_sha256') != IMAGE_SHA256):
        raise RuntimeError('source_session_or_image_mismatch')
    if shutdown.get('status') != 'complete' or shutdown.get('ok') is not True or shutdown.get('drains') != 2:
        raise RuntimeError('source_shutdown_unqualified')
    if not checkpoint:
        embedded = ((report.get('assertions') or {}).get('record_evidence_drain') or {}).get('persistence_shutdown')
        if embedded != shutdown:
            raise RuntimeError('source_shutdown_report_mismatch')
    started = datetime.fromisoformat(metadata['timestamp'])
    if started.tzinfo is None:
        raise RuntimeError('source_timestamp_timezone_missing')
    completed = max(report_path.stat().st_mtime, shutdown_path.stat().st_mtime, session.stat().st_mtime)
    if completed < started.timestamp():
        raise RuntimeError('source_completion_before_start')
    provenance = dict(session_log=str(session), report_path=str(report_path),
                      started_at=started.timestamp(), completed_at=completed,
                      report_sha256=hashlib.sha256(report_path.read_bytes()).hexdigest(),
                      metadata_sha256=hashlib.sha256(metadata_path.read_bytes()).hexdigest(),
                      shutdown_sha256=hashlib.sha256(shutdown_path.read_bytes()).hexdigest())
    return report, provenance


def validate_source_order(checkpoint, seed_a, seed_b):
    if not checkpoint['completed_at'] < seed_a['started_at'] or not seed_a['completed_at'] < seed_b['started_at']:
        raise RuntimeError('checkpoint_before_seeds_order_missing')
    if len({row['session_log'] for row in (checkpoint, seed_a, seed_b)}) != 3:
        raise RuntimeError('source_sessions_not_distinct')
WINDOW_US = 360_000_000


def qualified_seed(report, slot, pitch):
    issues = record_seed_qualification_issues(report)
    config = (report or {}).get('config') or {}
    expected = dict(track_number=8, first_track_index=7, track_count=1, loop_slot=slot,
                    midi_channel=8, record_only=True, record_bars=2,
                    edit_record_fixture=False, record_low_note=pitch, record_high_note=pitch)
    if issues or any(config.get(key) != value for key, value in expected.items()):
        raise RuntimeError('unqualified_filled_seed_' + str(slot) + ':' + str(issues))
    return dict(slot=slot, pitch=pitch, visual_notes=32, loop_length=1536)


def timed_rows(lines, floor):
    found = []
    for raw in lines:
        line = raw.strip()
        stamp = parse_cap_micros(line)
        if stamp is None:
            match = re.match(r'^\[([0-9]+)\.([0-9]{3})\]', line)
            if match:
                stamp = int(match[1]) * 1_000_000 + int(match[2]) * 1000
        if stamp is not None:
            delta = (stamp - floor) & 0xffffffff
            if 0 < delta <= WINDOW_US:
                found.append((delta, line))
    return [line for _, line in sorted(found)]


def mode_after(lines, floor):
    mode = None
    for line in timed_rows(lines, floor):
        if 'Edit session: NOTE_EDIT' in line or 'NOTE_EDIT session reopened for focus change' in line:
            mode = 'NOTE_EDIT'
        elif ('Edit session: LOOP_EDIT' in line or 'Long press - exited edit mode' in line
              or 'LOOP_EDIT session refreshed for focus change' in line):
            mode = 'LOOP_EDIT'
    return mode


def focus_witness(lines, floor, seed):
    rows = timed_rows(lines, floor)
    forbidden = (',RECA,', ',RING,critical_rejected,', ',RING,overflow,',
                 ',RING,tier_a_evicted,', ',RING,wiped,', ',DIAG,boot_reset,')
    if any(any(tag in line for tag in forbidden) for line in rows):
        raise RuntimeError('focus_capture_or_integrity_failure')
    display = [line.split(',') for line in rows if ',DISP,' in line]
    notes = [line.split(',') for line in rows if ',DNTE,' in line]
    if mode_after(lines, floor) != 'NOTE_EDIT' or not display or not notes:
        return None
    d, n = display[-1], notes[-1]
    if (len(d) < 11 or len(n) != 8 or d[3] != str(seed['slot'] - 1)
            or d[4] != 'STOPPED' or d[5] != str(seed['loop_length'])
            or d[7] != str(seed['visual_notes']) or d[10] != '1'
            or n[3] != str(seed['pitch'])):
        return None
    return dict(display=','.join(d), note=','.join(n), mode='NOTE_EDIT')


def stopped(capture, *, slot=None):
    status = capture.selected_track_status(timeout_s=2)
    if (not status or status['track'] != 7 or status['slot'] not in (3, 4)
            or status['state'] != 'STOPPED' or status['transport'] != 0
            or (slot is not None and status['slot'] != slot - 1)):
        raise RuntimeError('fresh_filled_stopped_target_missing')
    overlay = capture.overlay_status(timeout_s=2)
    if (not overlay or not 0 < ((overlay['us']-status['us']) & 0xffffffff) <= WINDOW_US
            or overlay['open'] != 0 or overlay['phase'] != 0):
        raise RuntimeError('fresh_closed_idle_overlay_missing')
    return status


def await_focus(action, capture, floor, seed, *, wait=await_markers):
    found = {}
    def observe():
        status = stopped(capture)
        if status['slot'] != seed['slot'] - 1:
            return []
        witness = focus_witness(action.collector.snapshot(), floor, seed)
        if witness:
            found.update(witness, status=status)
            return ['matched']
        return []
    if not wait(observe, ('matched',), timeout_s=8, on_poll=capture.drain_capture_ring):
        raise RuntimeError('fresh_note_edit_focus_missing_' + str(seed['slot']))
    if not found:
        raise RuntimeError('focus_wait_without_witness')
    return found


def await_panel(request, reply_path, capture, *, wait=await_markers):
    result = {}
    def replies():
        if not reply_path.is_file(): return []
        try: reply = json.loads(reply_path.read_text())
        except (OSError, ValueError): return []
        if isinstance(reply, dict): result.update(reply)
        return ['approved' if isinstance(reply, dict) and valid_human_proof(reply, request) else 'refused']
    if not wait(replies, ('approved', 'refused'), timeout_s=90, on_poll=capture.drain_capture_ring):
        raise RuntimeError('owner_focus_panel_timeout')
    if not valid_human_proof(result, request):
        raise RuntimeError('owner_focus_panel_refused_or_stale')
    return dict(result)


def hold_focus(action, capture, floor, seed, *, wait=await_markers):
    started = time.monotonic()
    def observe():
        stopped(capture, slot=seed['slot'])
        if focus_witness(action.collector.snapshot(), floor, seed) is None:
            raise RuntimeError('focus_changed_during_timed_observation')
        return []
    # Intentional bounded dwell, not a marker acceptance or a physical PASS.
    wait(observe, ('dwell-never-emitted',), timeout_s=20,
         on_poll=capture.drain_capture_ring)
    return dict(held_s=time.monotonic()-started, status=stopped(capture, slot=seed['slot']))


def run_flow(action, capture, seeds, out, *, panel_closed=False, observe_only=False, wait=await_markers):
    if action.config.track_number != 8 or action.config.loop_slot != 4 or not panel_closed:
        raise RuntimeError('authorized_target_and_owner_panel_ready_required')
    if capture is None or action.collector is None or set(seeds) != {4, 5}:
        raise RuntimeError('managed_owner_and_two_filled_seeds_required')
    if getattr(action.config, 'press_ms', None) != 120:
        raise RuntimeError('focus_requires_safe_short_press_120ms')
    initial = stopped(capture)
    floor = initial['us']
    press_edit_long(action)
    if not wait(lambda: [mode_after(action.collector.snapshot(), floor) or 'unknown'],
                ('LOOP_EDIT',), timeout_s=8, on_poll=capture.drain_capture_ring):
        raise RuntimeError('fresh_loop_edit_normalization_missing')
    status = stopped(capture)
    if status['slot'] != 3:
        press_slot_record(action, 4)
        # Different filled slot: the production stopped edit branch selects only.
        if not wait(lambda: [str(stopped(capture)['slot'])], ('3',), timeout_s=8,
                    on_poll=capture.drain_capture_ring):
            raise RuntimeError('initial_slot4_selection_missing')
    entry = stopped(capture, slot=4)
    entry_floor = entry['us']
    press_edit(action)
    epochs = []
    for index, slot in enumerate((4, 5, 4)):
        if index:
            status = stopped(capture, slot=epochs[-1]['slot'])
            if mode_after(action.collector.snapshot(), entry_floor) != 'NOTE_EDIT':
                raise RuntimeError('note_edit_changed_before_slot_press')
            floor = status['us']
            press_slot_record(action, slot)
        else:
            floor = entry_floor
        witness = await_focus(action, capture, floor, seeds[slot], wait=wait)
        request = dict(nonce=uuid.uuid4().hex, session_log=str(capture.session_log),
                       label=f'track8/slot{slot}/pitch{seeds[slot]["pitch"]}/stopped-NOTE_EDIT')
        (out/f'human-focus-request-{index}.json').write_text(json.dumps(request, indent=2)+'\n')
        if observe_only:
            print('[i4-focus] OBSERVE FOR 20 SECONDS:', request['label'], flush=True)
            hold = hold_focus(action, capture, floor, seeds[slot], wait=wait)
            proof = None
        else:
            print('[i4-focus] OWNER PANEL REQUIRED:', request['label'], flush=True)
            proof = await_panel(request, out/f'human-focus-reply-{index}.json', capture, wait=wait)
            hold = None
        stopped(capture, slot=slot)
        if focus_witness(action.collector.snapshot(), floor, seeds[slot]) is None:
            raise RuntimeError('focus_changed_during_owner_observation')
        epochs.append(dict(slot=slot, floor=floor, pitch=seeds[slot]['pitch'],
                           witness=witness, owner_panel_proof=proof, timed_observation=hold))
    return dict(ok=not observe_only, machine_ok=True, issues=[], epochs=epochs, entry_floor=entry_floor,
                verdict='MACHINE_PASS_OLED_PENDING' if observe_only else 'FOCUS_QUALIFIED',
                scope='stopped NOTE_EDIT filled-slot focus A/B/A only')


def run_i4_stopped_focus(args):
    from hitl.layered_cli import config_from_runner_args
    from hitl.managed_capture import active_managed_capture
    from hitl.serial_follow import ExternalSerialFollowCollector
    from hitl.session import open_midi_session
    config = config_from_runner_args(args, list(getattr(args, 'legacy_args', [])))
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--panel-overlay-closed', action='store_true')
    parser.add_argument('--observe-only', action='store_true')
    ns, _ = parser.parse_known_args(getattr(args, 'legacy_args', []))
    capture = active_managed_capture()
    if capture is None or config.track_number != 8 or config.loop_slot != 4:
        return 2
    if ns.observe_only:
        from hitl.stimuli import run_stimulus
        from hitl.stimuli.registry import get_stimulus
        from hitl.stimuli.spec import KIND_TEMPORARY
        if run_stimulus(get_stimulus('STIM-I4-STOPPED-FOCUS-01')).kind != KIND_TEMPORARY:
            print('[i4-focus] timed observation requires an explicit TEMP variant', flush=True)
            return 2
    checkpoint, checkpoint_run = source_bundle(Path(CHECKPOINT).parent, Path(CHECKPOINT), checkpoint=True)
    protected = checkpoint_identity(checkpoint)
    replay = verify_checkpoint(checkpoint['receipts'], **checkpoint['floors'])
    if not replay['ok'] or replay['checkpoint'] != protected:
        raise RuntimeError('preserving_checkpoint_replay_missing')
    seeds = {}; seed_runs = {}
    for slot, (directory, pitch) in SOURCES.items():
        paths = list(Path(directory).glob('host_midi_automation_baseline_*.json'))
        if len(paths) != 1:
            raise RuntimeError('one_seed_report_per_managed_run_required')
        report, provenance = source_bundle(directory, paths[0])
        seeds[slot] = qualified_seed(report, slot, pitch)
        seed_runs[slot] = provenance
    validate_source_order(checkpoint_run, seed_runs[4], seed_runs[5])
    out = config.out_dir; report_path = out/'i4-stopped-focus.json'
    if report_path.exists() or any(out.glob('human-focus-reply-*.json')):
        raise RuntimeError('fresh_run_directory_required')
    collector = ExternalSerialFollowCollector(capture.session_log, out_dir=out)
    session = None; result = dict(ok=False, issues=[], seeds=seeds, protected_checkpoint=protected,
                                 source_provenance=dict(checkpoint=checkpoint_run, seeds=seed_runs))
    try:
        collector.start()
        if wait_for_boot_handoff(collector, timeout_s=30) == 'timeout':
            raise RuntimeError('boot_handoff_unproven')
        session = open_midi_session(midi_out_name=config.midi_out_name,
                                   midi_in_name=config.midi_in_name, serial_port=None)
        session.collector = collector
        result.update(run_flow(ActionContext(session, config), capture, seeds, out,
                               panel_closed=ns.panel_overlay_closed, observe_only=ns.observe_only))
    except Exception as exc:
        result['issues'] = [f'{type(exc).__name__}: {exc}']
    finally:
        if session is not None: session.close()
        else: collector.stop()
        report_path.write_text(json.dumps(result, indent=2)+'\n')
    return 0 if result['ok'] or result.get('machine_ok') is True else 2
