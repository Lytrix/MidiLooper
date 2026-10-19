"""Managed MIDI-only preserving checkpoint. Never records, clears, loads or cleans up."""
from __future__ import annotations

import argparse
import json
import re

from hitl.actions.buttons import press_edit, press_edit_double, press_overlay_up
from hitl.actions.observation import await_markers
from hitl.boot_handoff import wait_for_boot_handoff
from hitl.context import ActionContext
from hitl.serial.protocol import parse_cap_micros
from hitl.stimuli.registry import STIM_I4_CHECKPOINT_01


def fresh_rows(lines, floor):
    return [line for line in lines if (stamp := parse_cap_micros(line)) is not None and stamp > floor]


def latest_mode(lines):
    events = [(int(m[1]), int(m[2])) for line in lines
              if (m := re.fullmatch(r'#CAP,(\d+),LDSV,([01])', line.strip()))]
    return max(events)[1] if events else None


def verify_checkpoint(lines, *, enter_floor, row_floor, save_floor):
    issues = []
    opened = fresh_rows(lines, enter_floor)
    selected = [line for line in fresh_rows(lines, row_floor)
                if (stamp := parse_cap_micros(line)) <= save_floor and ',OVLY,sel,' in line]
    saved = fresh_rows(lines, save_floor)
    if not any(re.fullmatch(r'#CAP,\d+,LDSV,1', line) for line in opened):
        issues.append('missing_fresh_overlay_enter')
    if not selected or max(selected, key=parse_cap_micros).split(',')[2:] != ['OVLY', 'sel', '0', '0']:
        issues.append('missing_or_changed_save_row')
    required = {
        'confirm': r'OVLY,confirm,0,0',
        'request': r'PERS,rev_request,0,0,0,queued',
        'exit': r'LDSV,0',
        'queued_exit': r'PERS,rev_overlay_save,0,0,0,queued_exit',
        'dispatch': r'PERS,rev_dispatch,0,0,0,run',
    }
    stamps = {}
    for name, pattern in required.items():
        matches = [line for line in saved if re.fullmatch(r'#CAP,\d+,' + pattern, line)]
        if len(matches) != 1:
            issues.append('missing_or_ambiguous_' + name)
        else:
            stamps[name] = parse_cap_micros(matches[0])
    commits = [line for line in saved if ',PERS,rev_complete,' in line]
    identity = None
    if len(commits) != 1:
        issues.append('missing_or_ambiguous_checkpoint')
    else:
        match = re.fullmatch(r'#CAP,(\d+),PERS,rev_complete,0,(\d+),(\d+),S(\d{4,5})_v(\d{4,5})', commits[0])
        if match and int(match[2]) == int(match[4]) and 0 < int(match[2]) <= 65535 and int(match[3]) == int(match[5]) and 0 < int(match[3]) <= 65535:
            identity = dict(set_id=int(match[2]), revision_id=int(match[3]),
                            label=f'S{int(match[2]):04d}_v{int(match[3]):04d}')
            stamps['complete'] = int(match[1])
        else:
            issues.append('checkpoint_identity_mismatch')
    for before, after in (('confirm', 'request'), ('request', 'dispatch'), ('dispatch', 'complete'),
                          ('confirm', 'exit'), ('exit', 'queued_exit')):
        if before in stamps and after in stamps and stamps[before] > stamps[after]:
            issues.append('receipt_order_' + before + '_' + after)
    if latest_mode(opened) != 0:
        issues.append('overlay_not_closed')
    return dict(ok=not issues, issues=issues, checkpoint=identity,
                floors=dict(enter_floor=enter_floor, row_floor=row_floor, save_floor=save_floor),
                receipts=opened)


def fresh_floor(capture):
    status = capture.selected_track_status(timeout_s=2.0)
    if status is None or status['transport'] != 0 or status['state'] not in ('EMPTY', 'STOPPED'):
        raise RuntimeError('fresh_stopped_status_missing')
    return status['us']


def checkpoint(action, capture, *, panel_overlay_closed=False, wait=await_markers):
    scope = STIM_I4_CHECKPOINT_01.authorization
    if not scope.allows(track=action.config.track_number, slots=(action.config.loop_slot,)):
        raise RuntimeError('unauthorized_I4_target')
    if capture is None or action.collector is None:
        raise RuntimeError('managed_owner_and_follower_required')
    enter_floor = fresh_floor(capture)
    mode = latest_mode(action.collector.snapshot())
    if mode == 1 or (mode is None and not panel_overlay_closed):
        raise RuntimeError('closed_overlay_precondition_unproven')

    def observe(floor, marker, timeout):
        if not wait(lambda: fresh_rows(action.collector.snapshot(), floor), (marker,),
                    timeout_s=timeout, on_poll=capture.drain_capture_ring):
            raise RuntimeError('missing_fresh_' + marker)

    print('[i4] Enter overlay; no scratch content touched', flush=True)
    press_edit_double(action)
    observe(enter_floor, ',LDSV,1', 8.0)
    row_floor = fresh_floor(capture)
    press_overlay_up(action)
    observe(row_floor, ',OVLY,sel,0,0', 8.0)
    save_floor = fresh_floor(capture)
    # Refuse a changed row BEFORE sending the mutation, not only in the final oracle.
    rows = [line for line in fresh_rows(action.collector.snapshot(), row_floor) if ',OVLY,sel,' in line]
    if not rows or max(rows, key=parse_cap_micros).split(',')[2:] != ['OVLY', 'sel', '0', '0']:
        raise RuntimeError('save_row_changed_before_confirm')
    print('[i4] Confirm Save; creating preserving Current checkpoint', flush=True)
    press_edit(action)
    observe(save_floor, ',PERS,rev_complete,', 90.0)
    result = verify_checkpoint(action.collector.snapshot(), enter_floor=enter_floor,
                               row_floor=row_floor, save_floor=save_floor)
    result['panel_overlay_closed'] = panel_overlay_closed
    return result


def run_i4_checkpoint(args):
    from hitl.layered_cli import config_from_runner_args
    from hitl.managed_capture import active_managed_capture
    from hitl.serial_follow import ExternalSerialFollowCollector
    from hitl.session import open_midi_session

    config = config_from_runner_args(args, list(getattr(args, 'legacy_args', [])))
    scope = STIM_I4_CHECKPOINT_01.authorization
    if not scope.allows(track=config.track_number, slots=(config.loop_slot,)):
        print('[i4] unauthorized_I4_target', flush=True)
        return 2
    capture = active_managed_capture()
    if capture is None or capture.session_log is None:
        print('[i4] managed_owner_required', flush=True)
        return 2
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--panel-overlay-closed', action='store_true')
    ns, _ = parser.parse_known_args(getattr(args, 'legacy_args', []))
    report_path = config.out_dir / 'i4-checkpoint.json'
    if report_path.exists():
        print('[i4] existing_checkpoint_report_refused; use a new run directory', flush=True)
        return 2
    collector = ExternalSerialFollowCollector(capture.session_log, out_dir=config.out_dir)
    session = None
    report = dict(ok=False, verdict='SETUP_INCOMPLETE', checkpoint=None,
                  stimulus=STIM_I4_CHECKPOINT_01.fingerprint_key(),
                  authorization=scope.as_dict(), session_log=str(capture.session_log),
                  oled_observation='UNMEASURED')
    try:
        collector.start()
        if wait_for_boot_handoff(collector, timeout_s=30.0) == 'timeout':
            raise RuntimeError('boot_handoff_unproven')
        session = open_midi_session(midi_out_name=config.midi_out_name,
                                    midi_in_name=config.midi_in_name, serial_port=None)
        session.collector = collector
        report.update(checkpoint(ActionContext(session, config), capture,
                                 panel_overlay_closed=ns.panel_overlay_closed))
        report['verdict'] = 'CHECKPOINT_CREATED' if report['ok'] else 'SETUP_INCOMPLETE'
    except Exception as exc:
        report['issues'] = [f'{type(exc).__name__}: {exc}']
    finally:
        # Managed owner remains open for its canonical DeviceSession Stop/drain teardown.
        if session is not None:
            session.close()
        else:
            collector.stop()
        report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('[i4] ' + report['verdict'] + ': ' + str(report.get('checkpoint')), flush=True)
    return 0 if report['ok'] else 2
