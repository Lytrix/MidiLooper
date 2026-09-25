#!/usr/bin/env python3
"""One fail-closed S1.3 growth step; existing single-session fixture owns recording.

User-authorized verified-empty candidates; fixed stopped focus is track 6 slot 3.
Never clears or overwrites a populated slot. Reboot after a take requires SD/RAM
parity and successful storage admission after restoring focus. No firmware edits.
Exit 2 also stops a sweep on a named memory denial, even if the take later survives.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

from hitl_identity_one_session import Session, build_parser, run_fixture, stream_take
from hitl.library_observation import parse_observation_lines
from hitl.slot_payload_readback import download_slot, verify_saved_slot, ram_effective
from hitl.midi_io import _send_short_press

READY = ('admission_complete', 'interactive_ready', 'undo_hydrated',
         'transport_stopped', 'deferred_prewarm_idle')
CONTENT = ('has_committed_content', 'has_pass_content', 'has_root_content',
           'loop_length_ticks', 'committed_event_count', 'committed_pass_count')


def complete_observation(obs):
    rows = obs.get('slots', [])
    if not obs.get('complete') or len(rows) != 64 or len({(r['track'],r['slot']) for r in rows}) != 64:
        raise ValueError('incomplete_library')
    if not obs.get('window', {}).get('admission_complete'):
        raise ValueError('admission_incomplete')
    if any(r.get('invalidated') or r.get('request') in ('queued','running','waiting') for r in rows):
        raise ValueError('unsettled_library')
    return {(r['track'], r['slot']): r for r in rows}


def compare_growth(before, after, target):
    a, b = complete_observation(before), complete_observation(after)
    loaded = lambda rows: {key for key,r in rows.items() if r['has_committed_content']}
    old,new = loaded(a),loaded(b)
    if new-old != {target} or old-new:
        raise ValueError('population_not_exactly_target_plus_one')
    for key in a:
        if key != target and any(a[key].get(k) != b[key].get(k) for k in CONTENT):
            raise ValueError(f'existing_content_fields_changed:{key}')
    roles = lambda rows: {key for key,r in rows.items() if r['required_for_initial_playback']}
    if roles(a) != roles(b):
        raise ValueError('initial_playback_roles_changed')
    return dict(loaded_before=len(old), loaded_after=len(new), added=list(target), removed=[])


def checkpoint(lines):
    markers = [l for l in lines if re.search(r'#CAP,\d+,DIAG,post_restore_census,',l)]
    if len(markers) != 1:
        raise ValueError('missing_or_duplicate_post_restore_checkpoint')
    fields = {k:int(v) for k,v in re.findall(r'([a-z_]+)=(\d+)',markers[0])}
    if any(fields.get(k) != 1 for k in READY):
        raise ValueError('checkpoint_not_ready')
    owners = [l for l in lines if l.startswith('#MEM_OWNERS,')]
    if not owners:
        raise ValueError('missing_owner_census')
    footer = [l[l.index('Boot transport t='):] for l in lines if 'Boot transport t=' in l]
    if len(footer) != 8:
        raise ValueError('missing_boot_transport_footer')
    return dict(readiness=fields, owner_line=owners[-1], boot_roles=footer,
                reservation_walk=[l for l in lines if 'DIAG,capture_prewarm,' in l])


def observe(session):
    lines, ok = session.request_line('!LIB_OBSERVE',marker='LIBOBS,E,',timeout_s=30)
    obs = parse_observation_lines(lines)
    if not ok: raise ValueError('observation_timeout')
    complete_observation(obs)
    return obs


def boot(args, directory):
    directory.mkdir(parents=True, exist_ok=False)
    firmware = args.firmware.resolve()
    digest = hashlib.sha256(firmware.read_bytes()).hexdigest()
    if digest != 'b289af534577e25b57c52146b27f66c245c97f43a5028fc10a37b9a8a9bbd208':
        raise ValueError('firmware_changed_from_verified_fixture')
    # Re-upload the already-built capture-serial image through the pinned
    # PlatformIO GUI uploader. The bundled old CLI rejects this target's HEX.
    uploader=Path.home()/'.platformio/packages/tool-teensy/teensy_post_compile'
    with (directory/'upload.log').open('w') as log:
        subprocess.run([str(uploader),'-file='+firmware.stem,'-path='+str(firmware.parent),
                        '-tools='+str(uploader.parent),'-board=TEENSY41','-reboot'],
                       stdout=log,stderr=subprocess.STDOUT,timeout=45,check=True)
    (directory/'identity.json').write_text(json.dumps(dict(firmware_hex_sha256=digest,
        git_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
        dirty=bool(subprocess.check_output(['git','status','--porcelain'],text=True)),
        driver_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()),indent=2))
    deadline=time.monotonic()+30
    while not Path(args.serial_port).exists() and time.monotonic()<deadline:
        time.sleep(.1)
    session=Session(args.serial_port,directory/'session.log')
    request = session.request_line
    def logged_request(command, **kwargs):
        with (directory/'commands.log').open('a') as log:
            log.write(f'{time.monotonic():.6f} {command}\n')
        return request(command, **kwargs)
    session.request_line=logged_request
    try:
        deadline=time.monotonic()+35
        while time.monotonic()<deadline:
            if any('DIAG,post_restore_census,' in l for l in session.snapshot()): break
            time.sleep(.1)
        # Fixed extra settle makes the owner census follow the readiness marker.
        time.sleep(5)
        obs=observe(session)
        point=checkpoint(session.snapshot())
        (directory/'library.json').write_text(json.dumps(obs,indent=2))
        (directory/'checkpoint.json').write_text(json.dumps(point,indent=2))
        return session,obs,point
    except BaseException:
        session.close()
        raise


def dump(session):
    lines,ok=session.request_line('!CAPTURE_CONTENT',marker='CAPTURE_CONTENT,end',timeout_s=15)
    if not ok: raise ValueError('ram_dump_timeout')
    return [l for l in lines if 'CAPTURE_CONTENT,' in l]


def fixture_args(args):
    dense = getattr(args, 'profile', 'two-bar') == 'dense-four-bar'
    fixture = build_parser().parse_args(['--out-dir',str(args.out_dir/'take'),
        '--serial-port',args.serial_port,'--track',str(args.track),'--loop-slot',str(args.slot),
        '--record-bars','4' if dense else '2','--stop-before-baseline'])
    if dense:
        fixture.note_step_clocks=1
        fixture.note_gate_clocks=1
        fixture.note_low=0
        fixture.note_count=128
    return fixture


def original_selected_slot(point, observation, track, allow_empty=False):
    for line in point['boot_roles']:
        m=re.search(r'Boot transport t=(\d+) active=(\d+) selected=(\d+)',line)
        if m and int(m[1]) == track-1:
            selected=int(m[3])
            row=complete_observation(observation).get((track-1,selected))
            if not row or (not row['has_committed_content'] and not allow_empty):
                raise ValueError('original_selection_empty_would_arm_on_restore')
            return selected
    raise ValueError('original_selection_unknown')


def restore_focus(session, press, track, original_slot, *, empty_anchor=False, stop=None):
    # Returning the target track's local selection is required even after global
    # focus moves away; otherwise the next boot's playback-required set changes.
    if empty_anchor and stop is None: raise ValueError('empty_anchor_requires_explicit_stop')
    press(60+track-1);time.sleep(1.2)
    press(50+original_slot);time.sleep(1.2)
    if empty_anchor:
        stop();time.sleep(.5)
    length,events=ram_effective(dump(session),track-1,original_slot)
    if empty_anchor:
        if length or events: raise ValueError('empty_anchor_gained_content')
    elif not length or not events: raise ValueError('original_focus_content_unavailable')
    # Track 6 already retains slot 3 (or was restored above). Pressing that
    # selected slot again toggles playback, so only switch global track focus.
    press(65);time.sleep(1.2)
    length,events=ram_effective(dump(session),5,2)
    if length != 1536 or len(events) != 62: raise ValueError('fixed_focus_not_restored')
    download_slot(session,5,2)


def preflight_empty_anchor(session, press, stop, track, original_slot, before):
    from hitl_identity_one_session import parse_identity_stats, IDENTITY_STATS_MARKER
    def entered():
        lines,ok=session.request_line('!CAP_IDENTITY_STATS',marker=IDENTITY_STATS_MARKER,timeout_s=10)
        stats=next((parse_identity_stats(l) for l in lines if IDENTITY_STATS_MARKER in l),{})
        if not ok or not isinstance(stats.get('entered'),int): raise ValueError('arm_cancel_identity_unavailable')
        return stats['entered']
    initial=entered()
    restore_focus(session,press,track,original_slot,empty_anchor=True,stop=stop)
    if entered()!=initial: raise ValueError('arm_cancel_started_a_take')
    a,b=complete_observation(before),complete_observation(observe(session))
    if any(a[k].get(f)!=b[k].get(f) for k in a for f in CONTENT):
        raise ValueError('arm_cancel_changed_content')
    return dict(arm_cancel_verified=True,identity_entries=initial,content_fields_unchanged=True)


def compare_survival(before, after):
    if (before['length'] != after['length'] or
            Counter(map(tuple,before['events'])) != Counter(map(tuple,after['events']))):
        raise ValueError('payload_events_did_not_survive')
    # Epoch/geometry/next-id metadata may legitimately be persisted on selection.
    # Each side has already passed two identical reads, CRC and SD/RAM parity.
    return dict(events_equal=True,length=after['length'],event_count=after['event_count'],
                file_bytes_equal=before['sha256']==after['sha256'],
                before_sha256=before['sha256'],after_sha256=after['sha256'])


def memory_denials(lines):
    return [line.strip() for line in lines
            if line.startswith(('#MEM_DENIED,', '#MEM_EXT_REFUSED,'))]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out-dir',type=Path,required=True)
    ap.add_argument('--track',type=int,choices=range(1,9),default=6)
    ap.add_argument('--allow-empty-anchor',action='store_true',help='Require a no-take arm/cancel preflight for an empty original selection.')
    ap.add_argument('--profile',choices=('two-bar','dense-four-bar'),default='two-bar')
    ap.add_argument('--slot',type=int,choices=range(1,9),required=True)
    ap.add_argument('--serial-port',default='/dev/cu.usbmodem154944801')
    ap.add_argument('--decoder',type=Path,required=True)
    ap.add_argument('--firmware',type=Path,required=True,help='Verified capture-serial hex, hash pinned in this fixture.')
    args=ap.parse_args()
    fa=fixture_args(args)  # Validate reused fixture arguments before any device action.
    import mido
    root=args.out_dir;root.mkdir(parents=True,exist_ok=False)
    result=dict(pass_step=False,track=args.track,slot=args.slot,profile=args.profile,reboot_after_take_allowed=False)
    session=None
    try:
        session,before,p0=boot(args,root/'before')
        original_slot=original_selected_slot(p0,before,args.track,args.allow_empty_anchor)
        empty_anchor=not complete_observation(before)[(args.track-1,original_slot)]['has_committed_content']
        with mido.open_output('Teensy MIDI') as out:
            press=lambda note:_send_short_press(out,note=note,channel_1based=16,press_ms=120)
            stop=lambda:out.send(mido.Message('stop'))
            if empty_anchor:
                result['anchor_preflight']=preflight_empty_anchor(session,press,stop,args.track,original_slot,before)
            result['take']=run_fixture(fa,session,press,lambda:stream_take(fa,out,mido.Message),
                                      stop_transport=stop,start_transport=lambda:out.send(mido.Message('start')),
                                      pulse_clock=lambda:out.send(mido.Message('clock')))
            if result['take']['verdict'] != 'identity_pass':
                raise ValueError('take_fixture:'+result['take'].get('reason','unknown'))
            take_dir=root/'take';take_dir.mkdir()
            payload=verify_saved_slot(session,args.track-1,args.slot-1,result['take']['ram_content_dump'],args.decoder,take_dir)
            result['stored_before_reboot']=payload
            # Restore the original selection and fixed stopped focus, then wait for SD
            # admission (no save remains) before authorizing the survival reboot.
            stop();time.sleep(.5)
            restore_focus(session,press,args.track,original_slot,empty_anchor=empty_anchor,stop=lambda:out.send(mido.Message('stop')))
            result['reboot_after_take_allowed']=True
        session.close();session=None
        session,after,p1=boot(args,root/'after')
        result['growth']=compare_growth(before,after,(args.track-1,args.slot-1))
        if p0['boot_roles'] != p1['boot_roles']: raise ValueError('boot_transport_roles_changed')
        # RAM proof requires selecting the now-known populated target. All slope
        # telemetry above was frozen before this interaction.
        with mido.open_output('Teensy MIDI') as out:
            press=lambda note:_send_short_press(out,note=note,channel_1based=16,press_ms=120)
            out.send(mido.Message('stop'));time.sleep(.5);press(60+args.track-1);time.sleep(1.2)
            press(50+args.slot-1);time.sleep(1.2)
            ram=dump(session)
            stored=verify_saved_slot(session,args.track-1,args.slot-1,ram,args.decoder,root/'after')
            result['survival']=compare_survival(payload,stored)
            result['stored_after_reboot']=stored
            restore_focus(session,press,args.track,original_slot,empty_anchor=empty_anchor,stop=lambda:out.send(mido.Message('stop')))
        result['pass_step']=True
        result['checkpoints']=[p0,p1]
    except Exception as exc:
        result['error']=str(exc)
    finally:
        if session:
            # Host exceptions must not strand recording/transport running.
            try:
                with mido.open_output('Teensy MIDI') as out:
                    out.send(mido.Message('stop'));time.sleep(.5)
                    out.send(mido.Message('stop'));time.sleep(.5)
            finally:
                session.close()
        result['memory_denials']=[]
        for phase in ('before','after'):
            log=root/phase/'session.log'
            if log.exists():
                with log.open() as lines:
                    result['memory_denials'].extend(memory_denials(lines))
        (root/'result.json').write_text(json.dumps(result,indent=2))
        print(json.dumps({k:v for k,v in result.items() if k not in ('take','stored_before_reboot','stored_after_reboot','checkpoints')},indent=2))
    return 0 if result['pass_step'] and not result['memory_denials'] else 2

if __name__=='__main__': raise SystemExit(main())
