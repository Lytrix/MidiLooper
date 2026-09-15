"""Restart check for the focused 32-base/16-overdub/held-83 fixture.

Pitch 48 occurs once per base loop and brackets complete observed cycles.
This is fixture-specific musical-content evidence, not a timing/parity gate.
"""
from collections import Counter


def verify_restart_survival(rows, actions, *, channel=4):
    starts = [a['host_ns'] for a in actions if a.get('event') == 'start']
    stops = [a['host_ns'] for a in actions if a.get('event') == 'stop']
    if len(starts) != 1 or len(stops) != 1 or starts[0] >= stops[0]:
        return {'result': 'INCONCLUSIVE_CAPTURE', 'reason': 'missing_restart_bounds'}
    messages = [r for r in rows if starts[0] < r['host_ns'] < stops[0]
                and r.get('message', {}).get('channel') == channel]
    def is_on(row):
        m = row['message']
        return m['type'] == 'note_on' and m.get('velocity',0) > 0
    anchors = [i for i,r in enumerate(messages) if is_on(r) and r['message']['note'] == 48]
    if len(anchors) < 3:
        return {'result':'INCONCLUSIVE_CAPTURE','reason':'fewer_than_two_complete_cycles'}
    expected = set(range(48,80)) | set(range(24,40)) | {83}
    cycles = []
    for left,right in zip(anchors,anchors[1:]):
        notes = messages[left:right]
        ons = Counter(r['message']['note'] for r in notes if is_on(r))
        offs = Counter(r['message']['note'] for r in notes
                       if r['message']['type'] == 'note_off'
                       or (r['message']['type'] == 'note_on' and not is_on(r)))
        cycles.append({'start_host_ns':messages[left]['host_ns'],
                       'end_host_ns':messages[right]['host_ns'],
                       'missing':sorted(expected-set(ons)),
                       'unexpected':sorted(set(ons)-expected),
                       'duplicate_ons':sorted(n for n,count in ons.items() if count != 1),
                       'unpaired':sorted(n for n in ons if offs[n] != ons[n]),
                       'note_on_counts':dict(ons)})
    passed = all(not any(c[k] for k in ('missing','unexpected','duplicate_ons','unpaired')) for c in cycles)
    return {'result':'PASS' if passed else 'FAIL', 'scope':'focused fixture restart content',
            'complete_cycles':len(cycles),'cycles':cycles, 'deadline_safety_proven':False}
