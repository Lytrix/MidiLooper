"""MIDI-only managed load of an explicitly owner-identified test checkpoint."""
from __future__ import annotations
import argparse
import json
import re
import time
import uuid
from pathlib import Path

from hitl.actions.buttons import (press_edit, press_edit_double, press_edit_long,
                                  press_overlay_up, press_record, press_transport, press_play_stop)
from hitl.actions.observation import await_markers
from hitl.boot_handoff import wait_for_boot_handoff
from hitl.context import ActionContext
from hitl.serial.protocol import parse_cap_micros

WINDOW_US = 360_000_000


def fresh_rows(lines, floor):
    pairs = []
    for line in lines:
        stamp = parse_cap_micros(line)
        if stamp is not None and 0 <= stamp <= 0xffffffff:
            delta = (stamp - floor) & 0xffffffff
            if 0 < delta <= WINDOW_US:
                pairs.append((delta, line.strip()))
    return [line for _, line in sorted(pairs)]


def checkpoint_identity(report):
    identity = report.get('checkpoint') or {}
    sid, rev = identity.get('set_id'), identity.get('revision_id')
    if (report.get('ok') is not True or report.get('verdict') != 'CHECKPOINT_CREATED'
            or report.get('stimulus') != 'STIM-I4-CHECKPOINT-01@v1'
            or type(sid) is not int or type(rev) is not int
            or not 0 < sid <= 65535 or not 0 < rev <= 65535
            or identity.get('label') != f'S{sid:04d}_v{rev:04d}'):
        raise RuntimeError('unproven_test_checkpoint')
    return dict(identity)


def valid_human_proof(reply, request):
    return (reply.get('approved') is True and
            all(reply.get(key) == request[key] for key in ('nonce', 'session_log', 'label')))


def live_status(capture):
    status = capture.selected_track_status(timeout_s=2.0)
    if (not status or status['track'] != 7 or status['slot'] != 3
            or status['state'] not in ('STOPPED', 'PLAYING') or status['transport'] not in (0, 1)):
        raise RuntimeError('fresh_authorized_target_status_missing')
    return status


def fresh_overlay_snapshot(capture, floor, *, timeout_s=2.0):
    sample = capture.overlay_status(timeout_s=timeout_s)
    if (not isinstance(sample, dict) or
            not 0 < ((sample['us'] - floor) & 0xffffffff) <= WINDOW_US):
        return None
    return sample


def await_overlay_snapshot(capture, floor, expected, *, timeout=8, wait=await_markers):
    """Await fresh scalar state, never fabricate or replay an LDSV/OVLY receipt."""
    found = {}
    deadline = time.monotonic() + timeout
    def samples():
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return []
        sample = fresh_overlay_snapshot(capture, floor, timeout_s=min(2.0, remaining))
        if sample is not None and all(sample.get(key) == value for key, value in expected.items()):
            found.update(sample)
            return ['matched']
        return []
    if not wait(samples, ('matched',), timeout_s=timeout, poll_s=0.25) or not found:
        raise RuntimeError('missing_fresh_overlay_snapshot_' + str(expected))
    return dict(found)


def latest_selection(lines):
    matches = [p for line in lines if (p := line.split(','))[2:4] == ['OVLY', 'sel'] and len(p) == 6]
    return tuple(matches[-1][4:6]) if matches else None


def latest_mode(lines):
    matches = [p for line in lines if len(p := line.split(',')) == 4 and p[2] == 'LDSV']
    return matches[-1][3] if matches else None


def await_identity(request, reply_path, capture, wait=await_markers):
    found = {}
    def replies():
        if not reply_path.is_file(): return []
        try: reply = json.loads(reply_path.read_text())
        except (OSError, ValueError): return []
        if not isinstance(reply, dict): return ['refused']
        found.update(reply)
        return ['approved' if valid_human_proof(reply, request) else 'refused']
    if not wait(replies, ('approved', 'refused'), timeout_s=90, on_poll=capture.drain_capture_ring):
        raise RuntimeError('owner_checkpoint_identity_timeout')
    if not valid_human_proof(found, request):
        raise RuntimeError('owner_checkpoint_identity_refused_or_stale')
    return dict(found)


def verify_load(lines, identity, *, enter_floor, load_floor, choice_floor):
    sid, rev = identity['set_id'], identity['revision_id']
    label = identity['label']
    rows = fresh_rows(lines, enter_floor)
    issues = []
    stamps = {}
    required = {
        'enter': r'LDSV,1',
        'prompt': rf'PERS,rev_load_dirty_prompt,0,{sid},{rev},shown',
        'yes': rf'PERS,rev_load_dirty_yes,0,{sid},{rev},save_then_load',
        'commit_request': r'PERS,rev_request,0,0,0,queued',
        'commit_dispatch': r'PERS,rev_dispatch,0,0,0,run',
        'load_request': rf'PERS,rev_load_request,0,{sid},{rev},queued',
        'load_dispatch': rf'PERS,rev_load_dispatch,0,{sid},{rev},run',
        'load_complete': rf'PERS,rev_load_complete,0,{sid},{rev},{label}(?:,default_transport)?',
        'exit': r'LDSV,0',
    }
    for name, pattern in required.items():
        floor = enter_floor if name == 'enter' else load_floor if name == 'prompt' else choice_floor
        matches = [line for line in fresh_rows(rows, floor) if re.fullmatch(r'#CAP,\d+,' + pattern, line)]
        if len(matches) != 1: issues.append('missing_or_ambiguous_' + name)
        else: stamps[name] = parse_cap_micros(matches[0])
    commits = [line for line in fresh_rows(rows, choice_floor) if ',PERS,rev_complete,' in line]
    created = None
    if len(commits) == 1:
        p = commits[0].split(',')
        if len(p) == 8 and p[5].isdigit() and p[6].isdigit():
            cs, cr = int(p[5]), int(p[6])
            if 0 < cs <= 65535 and 0 < cr <= 65535 and p[7] == f'S{cs:04d}_v{cr:04d}' and (cs, cr) != (sid, rev):
                created = dict(set_id=cs, revision_id=cr, label=p[7]); stamps['commit_complete'] = int(p[1])
    if created is None: issues.append('preserving_commit_identity_missing_or_wrong')
    order = ['enter', 'prompt', 'yes', 'commit_request', 'commit_dispatch', 'commit_complete',
             'load_request', 'load_dispatch', 'load_complete', 'exit']
    for before, after in zip(order, order[1:]):
        if before in stamps and after in stamps:
            if ((stamps[before]-enter_floor)&0xffffffff) > ((stamps[after]-enter_floor)&0xffffffff):
                issues.append('wrong_order_' + before + '_' + after)
    if latest_mode(rows) != '0': issues.append('overlay_not_closed')
    return dict(ok=not issues, issues=issues, witnesses=stamps, preserving_checkpoint=created,
                loaded_checkpoint=dict(identity), oled_observation='UNMEASURED')


def run_flow(action, capture, identity, request_path, reply_path, *, panel_closed=False, wait=await_markers):
    if action.config.track_number != 8 or action.config.loop_slot != 4:
        raise RuntimeError('unauthorized_target')
    if capture is None or action.collector is None:
        raise RuntimeError('managed_owner_and_follower_required')
    status = live_status(capture)
    if not panel_closed: raise RuntimeError('owner_closed_overlay_precondition_required')
    initial_overlay = fresh_overlay_snapshot(capture, status['us'])
    if initial_overlay is None or initial_overlay['open'] != 0:
        raise RuntimeError('fresh_closed_overlay_precondition_missing')
    if status['transport'] == 0:
        press_transport(action); time.sleep(0.65); status = live_status(capture)
    if status['transport'] != 1: raise RuntimeError('transport_start_unconfirmed')
    if status['state'] == 'STOPPED':
        press_play_stop(action); time.sleep(0.65); status = live_status(capture)
    if status['state'] != 'PLAYING': raise RuntimeError('target_playing_unconfirmed')

    snapshots = [initial_overlay]
    def observe_overlay(floor, **expected):
        sample = await_overlay_snapshot(capture, floor, dict(open=1, **expected), wait=wait)
        snapshots.append(sample)
        return sample

    def observe_marker(floor, marker, timeout=8):
        if not wait(lambda: fresh_rows(action.collector.snapshot(), floor), (marker,),
                    timeout_s=timeout, on_poll=capture.drain_capture_ring):
            raise RuntimeError('missing_fresh_' + marker)
    entry_status = live_status(capture)
    if entry_status['state'] != 'PLAYING' or entry_status['transport'] != 1:
        raise RuntimeError('playing_entry_precondition_changed')
    enter_floor = entry_status['us']
    press_edit_double(action); observe_overlay(enter_floor, mode=0, phase=0)
    floor = live_status(capture)['us']; press_overlay_up(action)
    observe_overlay(floor, mode=0, row=0, phase=0)
    for row in (1, 2):
        floor = live_status(capture)['us']; press_record(action)
        observe_overlay(floor, mode=0, row=row, phase=0)
    floor = live_status(capture)['us']; press_edit_long(action)
    observe_overlay(floor, mode=2, row=0, set_id=identity['set_id'], phase=0)
    request = dict(nonce=uuid.uuid4().hex, session_log=str(capture.session_log), label=identity['label'])
    request_path.write_text(json.dumps(request, indent=2) + '\n')
    print('[i4-load] OWNER IDENTITY REQUIRED: highlighted Set/revision must be ' + identity['label'], flush=True)
    proof = await_identity(request, reply_path, capture, wait=wait)
    status = live_status(capture)
    if status['state'] != 'PLAYING' or status['transport'] != 1:
        raise RuntimeError('playing_load_precondition_changed')
    current = fresh_overlay_snapshot(capture, status['us'])
    if (current is None or any(current.get(key) != value for key, value in
            dict(open=1, mode=2, row=0, set_id=identity['set_id'], phase=0).items())):
        raise RuntimeError('history_focus_changed_before_load_confirm')
    snapshots.append(current)
    load_floor = current['us']
    press_edit(action)
    dirty_target = dict(mode=1, set_id=identity['set_id'], revision_id=identity['revision_id'], phase=0)
    sample = observe_overlay(load_floor, **dirty_target)
    for _ in range(3):
        floor = live_status(capture)['us']; press_overlay_up(action)
        sample = observe_overlay(floor, row=max(0, sample['row'] - 1), **dirty_target)
        if sample['row'] == 0: break
    else: raise RuntimeError('dirty_yes_focus_unproven')
    choice_floor = live_status(capture)['us']
    current = fresh_overlay_snapshot(capture, choice_floor)
    if (current is None or any(current.get(key) != value for key, value in
            dict(open=1, row=0, **dirty_target).items())):
        raise RuntimeError('dirty_yes_focus_changed_before_confirm')
    snapshots.append(current)
    choice_floor = current['us']
    print('[i4-load] Confirming dirty Yes: preserve Current, then load only ' + identity['label'], flush=True)
    press_edit(action)
    observe_marker(choice_floor, f',PERS,rev_load_complete,0,{identity["set_id"]},{identity["revision_id"]},{identity["label"]}',180)
    observe_marker(choice_floor, ',LDSV,0')
    result = verify_load(action.collector.snapshot(), identity, enter_floor=enter_floor,
                         load_floor=load_floor, choice_floor=choice_floor)
    result.update(floors=dict(enter_floor=enter_floor, load_floor=load_floor, choice_floor=choice_floor),
                  owner_identity_proof=proof, playing_before_enter=True, entry_status=entry_status,
                  live_overlay_snapshots=snapshots)
    return result


def run_i4_overlay_load(args):
    from hitl.layered_cli import config_from_runner_args
    from hitl.managed_capture import active_managed_capture
    from hitl.serial_follow import ExternalSerialFollowCollector
    from hitl.session import open_midi_session
    config = config_from_runner_args(args, list(getattr(args, 'legacy_args', [])))
    capture = active_managed_capture()
    if config.track_number != 8 or config.loop_slot != 4 or capture is None or capture.session_log is None:
        print('[i4-load] authorized managed target required',flush=True); return 2
    parser=argparse.ArgumentParser(add_help=False)
    parser.add_argument('--panel-overlay-closed',action='store_true')
    ns,_=parser.parse_known_args(getattr(args,'legacy_args',[]))
    source=Path('captures/i4-checkpoint-t8s4-03/i4-checkpoint.json')
    identity=checkpoint_identity(json.loads(source.read_text()))
    out=config.out_dir;report_path=out/'i4-overlay-load.json'
    if report_path.exists() or (out/'human-load-reply.json').exists():
        raise RuntimeError('fresh_run_directory_required')
    collector=ExternalSerialFollowCollector(capture.session_log,out_dir=out)
    session=None
    result=dict(ok=False,issues=[],checkpoint_reference=str(source),loaded_checkpoint=identity,
                session_log=str(capture.session_log),oled_observation='UNMEASURED')
    try:
        collector.start()
        if wait_for_boot_handoff(collector,timeout_s=30)=='timeout':raise RuntimeError('boot_handoff_unproven')
        session=open_midi_session(midi_out_name=config.midi_out_name,midi_in_name=config.midi_in_name,serial_port=None)
        session.collector=collector
        result.update(run_flow(ActionContext(session,config),capture,identity,out/'human-load-request.json',
                               out/'human-load-reply.json',panel_closed=ns.panel_overlay_closed))
    except Exception as exc:result['issues']=[f'{type(exc).__name__}: {exc}']
    finally:
        if session is not None:session.close()
        else:collector.stop()
        report_path.write_text(json.dumps(result,indent=2)+'\n')
    print('[i4-load] Result:',result['ok'],result['issues'],flush=True)
    return 0 if result['ok'] else 2
