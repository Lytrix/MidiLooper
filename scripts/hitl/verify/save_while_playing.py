"""Serial-only Record→PLAYING save-before-Stop evidence. Pixels need owner witness."""
from __future__ import annotations

import re


def verify_save_while_playing(lines, *, after_us, track_index=7, slot_index=3,
                              expected_length=1536):
    issues = []
    witnesses = {}
    if not isinstance(after_us, int) or after_us <= 0:
        return dict(ok=False, issues=['fresh_session_floor_required'], witnesses={})
    if track_index != 7 or slot_index not in (3, 4):
        return dict(ok=False, issues=['unauthorized_validation_target'], witnesses={})
    rows = []
    for line in lines:
        parts = line.strip().split(',')
        if len(parts) >= 3 and parts[0] == '#CAP' and parts[1].isdigit():
            stamp = int(parts[1])
            if after_us < stamp <= 0xffffffff:
                rows.append((stamp, parts))
    rows.sort(key=lambda row: row[0])

    def stamps(predicate, low=after_us, high=0xffffffff):
        return [stamp for stamp, parts in rows if low < stamp < high and predicate(parts)]

    starts = stamps(lambda p: len(p) >= 4 and p[2] == 'RECA' and p[3] == str(slot_index))
    if not starts:
        return dict(ok=False, issues=['fresh_target_record_start_missing'], witnesses={})
    start = starts[0]
    witnesses['record_start_us'] = start
    selections = [(stamp, p) for stamp, p in rows if stamp < start and len(p) == 8
                  and p[2:6] == ['MI', 'U', '144', '16'] and p[6].isdigit()
                  and 60 <= int(p[6]) <= 67 and p[7].isdigit() and int(p[7]) > 0]
    if not selections or int(selections[-1][1][6]) != 60 + track_index:
        issues.append('fresh_selected_track_proof_missing')
    realtime = [(stamp, p[4]) for stamp, p in rows if stamp < start and len(p) == 8
                and p[2:4] == ['MI', 'U'] and p[4] in ('250', '251', '252')]
    transport_start_seen = bool(realtime and realtime[-1][1] in ('250', '251'))
    stops = stamps(lambda p: len(p) == 10 and p[2:5] == ['RECS', 'stop', str(slot_index)]
                   and p[8] == str(expected_length), start)
    if not stops:
        return dict(ok=False, issues=issues + ['fresh_expected_record_stop_missing'], witnesses=witnesses)
    record_stop = stops[0]
    witnesses['record_stop_us'] = record_stop
    transport_stops = stamps(lambda p: len(p) == 8 and p[2:5] == ['MI', 'U', '252'], record_stop)
    if not transport_stops:
        return dict(ok=False, issues=issues + ['end_transport_stop_missing'], witnesses=witnesses)
    transport_stop = transport_stops[0]
    witnesses['transport_stop_us'] = transport_stop
    done_pattern = re.compile(rf'^LoopPersist,slot:{track_index}:{slot_index},done,ok(?:,|$)')
    done = stamps(lambda p: len(p) >= 11 and p[2:4] == ['PERS', 'work']
                  and done_pattern.match(','.join(p[7:])) is not None,
                  record_stop, transport_stop)
    if not done:
        issues.append('target_persistence_done_before_stop_missing')
    result_low = done[0] if done else record_stop
    results = stamps(lambda p: len(p) == 8 and p[2:4] == ['PERS', 'result'] and p[7] == 'ok',
                     result_low, transport_stop)
    if not results:
        issues.append('successful_result_before_stop_missing')
    result = results[0] if results else result_low
    bars = [(stamp, int(p[3])) for stamp, p in rows if record_stop < stamp < result
            and len(p) == 5 and p[2] == 'BAR' and p[3].isdigit() and p[4].isdigit()]
    advancing_bars = any(b[1] > a[1] for a, b in zip(bars, bars[1:]))
    if not transport_start_seen and not advancing_bars:
        issues.append('fresh_transport_running_proof_missing')
    witnesses['transport_running_proof'] = 'midi_start' if transport_start_seen else (
        'advancing_bar_ticks' if advancing_bars else 'missing')
    frames = [(stamp, p) for stamp, p in rows if record_stop < stamp < result
              and len(p) >= 5 and p[2:4] == ['DISP', str(slot_index)]]
    if not frames or frames[-1][1][4] != 'PLAYING':
        issues.append('target_playing_before_result_missing')
    completed = stamps(lambda p: len(p) == 5 and p[2:4] == ['SAVE', 'completed'],
                       result, transport_stop)
    if not completed:
        issues.append('save_completed_before_stop_missing')
    if completed:
        completion_frames = [(stamp, p) for stamp, p in rows
                             if record_stop < stamp < completed[0] and len(p) >= 5
                             and p[2:4] == ['DISP', str(slot_index)]]
        if not completion_frames or completion_frames[-1][1][4] != 'PLAYING':
            issues.append('target_not_playing_through_completed_flash')
        initial_playing = [stamp for stamp, p in frames if p[4] == 'PLAYING']
        if initial_playing and any(stamp > initial_playing[0] and p[4] != 'PLAYING'
                                   for stamp, p in completion_frames):
            issues.append('target_playback_interrupted_before_completion')
    if done: witnesses['target_done_us'] = max(stamp for stamp in done if stamp <= result)
    if results: witnesses['result_us'] = result
    if frames: witnesses['playing_us'] = frames[-1][0]
    if completed: witnesses['save_completed_us'] = completed[0]
    # A different selected track or transport interruption invalidates this bounded leg.
    for stamp, p in rows:
        if start < stamp < transport_stop and len(p) == 8 and p[2:6] == ['MI', 'U', '144', '16']:
            if p[6].isdigit() and 60 <= int(p[6]) <= 67 and p[7].isdigit() and int(p[7]) > 0:
                if int(p[6]) != 60 + track_index:
                    issues.append('track_changed_during_record_save_leg')
        if start < stamp <= record_stop and len(p) == 8 and p[2:5] == ['MI', 'U', '252']:
            issues.append('transport_interrupted_during_record')
    return dict(ok=not issues, issues=issues, witnesses=witnesses,
                target=dict(track_index=track_index, slot_index=slot_index),
                oled_observation='UNMEASURED')
