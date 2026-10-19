"""Strict host oracle for the named live rolling Record experiment.

Snapshots prove source/physical metadata; an independent MIDI input listener
proves wire inventory. Neither proves arrival age, owner WCET or a deadline.
"""
from __future__ import annotations

from collections import Counter
import hashlib
import json
import re
from pathlib import Path
from typing import Mapping

# Ordered firmware qualification schema. Kept explicit; no permissive prefix parse.
FIELDS = ('id', 'track', 'slot', 'state', 'tick', 'epoch', 'phase',
          'frameOrigin', 'framePeriod', 'boundRole', 'boundStorage', 'currentRole',
          'currentStorage', 'published', 'complete', 'sourceOrigin', 'sourceGeneration',
          'captureToken', 'liveToken', 'start', 'length', 'period', 'rows', 'cursor',
          'ledger', 'firstTick', 'firstNoteId', 'lastTick', 'lastNoteId', 'rawCount',
          'rawMax', 'recording', 'stopJob', 'valid', 'frameEpoch', 'requestEpoch',
          'requestActive', 'requestInvalidated', 'readyRole', 'buildRole',
          'readyFlags', 'buildFlags', 'workerFlags')
SIGNED = {'frameOrigin'}
WIDE = {'tick', 'boundStorage', 'currentStorage', 'captureToken', 'liveToken'}
BOOLEANS = {'valid', 'published', 'complete', 'recording', 'stopJob', 'requestActive', 'requestInvalidated'}
# `PlaybackWindowOrigin` as reported by the firmware query (RuntimePlaybackView.h).
COMMITTED_ORIGIN = 0
CAPTURE_ORIGIN = 2
# A frame the device reports as unbound (`UINT8_MAX`) carries no frame geometry.
UNBOUND_ROLE = 255

ROLLING_RECORD_REPORT_SCHEMA_VERSION = 2
ROLLING_RECORD_BUILD_ENV = 'teensy41-capture-serial-window-qualification'
ROLLING_RECORD_STIMULUS = 'STIM-ROLLING-RECORD-TEMP-01@v1'
ROLLING_RECORD_PASS_GROUPS = {
    'all': ('growth', 'empty', 'held', 'matched'),
    'growth-empty': ('growth', 'empty'),
    'growth': ('growth',),
    'empty': ('empty',),
    'held': ('held',),
    'matched': ('matched',),
}
ROLLING_RECORD_AGGREGATE_GROUPS = ('growth-empty', 'held', 'matched')


def parse_snapshot(lines, request_id):
    matches = [line.strip() for line in lines if ',DIAG,rolling_record,' in line]
    if len(matches) != 1:
        raise RuntimeError('rolling_snapshot_missing_or_duplicate')
    p = matches[0].split(',')
    if len(p) != 4 + len(FIELDS) or p[0] != '#CAP' or p[2:4] != ['DIAG', 'rolling_record']:
        raise RuntimeError('rolling_snapshot_schema')
    if not p[1].isdigit() or int(p[1]) >= 1 << 32:
        raise RuntimeError('rolling_snapshot_device_timestamp')
    result = {'device_us': int(p[1]), 'raw': matches[0]}
    for name, value in zip(FIELDS, p[4:]):
        if re.fullmatch(r'-?[0-9]+' if name in SIGNED else r'[0-9]+', value) is None:
            raise RuntimeError('rolling_snapshot_integer:' + name)
        v = int(value)
        if name in SIGNED:
            valid = -(1 << 31) <= v < 1 << 31
        else:
            valid = 0 <= v < (1 << (64 if name in WIDE else 32))
        if not valid or (name in BOOLEANS and v not in (0, 1)):
            raise RuntimeError('rolling_snapshot_range:' + name)
        result[name] = v
    if any(result[name] >= 16 for name in ('readyFlags', 'buildFlags')) or result['workerFlags'] >= 4:
        raise RuntimeError('rolling_snapshot_owner_flags')
    if any(result[name] not in (0, 1, 2, 255) for name in ('readyRole', 'buildRole')):
        raise RuntimeError('rolling_snapshot_owner_role')
    if result['id'] != request_id:
        raise RuntimeError('rolling_snapshot_request_mismatch')
    return result


def qualify(sample, *, slot, recording=True):
    if sample['track'] != 7 or sample['slot'] != slot - 1:
        raise RuntimeError('rolling_snapshot_wrong_target')
    if not sample['valid'] or not sample['epoch']:
        raise RuntimeError('rolling_snapshot_invalid')
    if recording and not sample['recording']:
        raise RuntimeError('rolling_snapshot_not_recording')
    if (sample['phase'] != 3 or sample['frameEpoch'] != sample['epoch'] or sample['boundRole'] != sample['currentRole']
            or sample['boundStorage'] != sample['currentStorage'] or not sample['currentStorage']
            or not sample['published'] or not sample['complete']
            or sample['sourceOrigin'] != CAPTURE_ORIGIN
            or not sample['captureToken']):
        raise RuntimeError('rolling_snapshot_unqualified_source')
    period = sample['period']
    if (period < 768 or period % 768 or sample['framePeriod'] != period
            or sample['length'] != 768 or sample['start'] % 768
            or sample['start'] + sample['length'] > period
            or sample['cursor'] > sample['rows']):
        raise RuntimeError('rolling_snapshot_geometry')
    phase = (sample['tick'] - sample['frameOrigin']) % period
    if not sample['start'] <= phase < sample['start'] + sample['length']:
        raise RuntimeError('rolling_snapshot_not_covering_playhead')
    if sample['rows'] and (not sample['firstNoteId'] or not sample['lastNoteId']):
        raise RuntimeError('rolling_snapshot_missing_note_identity')
    return sample


def validate_sequence(samples):
    ids = [s['id'] for s in samples]
    if len(ids) != len(set(ids)):
        raise RuntimeError('rolling_reused_request_id')
    for a, b in zip(samples, samples[1:]):
        if b['epoch'] != a['epoch'] or b['tick'] < a['tick']:
            raise RuntimeError('rolling_position_changed')
        if b['sourceGeneration'] != a['sourceGeneration']:
            raise RuntimeError('rolling_operation_changed')


def wire_inventory(messages, *, start, end, channel, pitches):
    if not start < end:
        raise RuntimeError('wire_window_invalid')
    rows = [m for m in messages if start <= m['host_mono'] <= end and m.get('channel') == channel]
    notes = [m for m in rows if m.get('type') in ('note_on', 'note_off')]
    actual = [(m['type'] if m['velocity'] else 'note_off', m['note'], m['velocity']) for m in notes]
    expected = [(kind, pitch, velocity) for pitch in pitches
                for kind, velocity in [('note_on', 98), ('note_off', 0)]]
    if actual != expected:
        raise RuntimeError('wire_inventory_mismatch:' + repr({'actual': actual, 'expected': expected}))
    return {'ok': True, 'notes': notes, 'expected_pairs': len(pitches)}


def content_inventory(lines, *, pitches, channel):
    begins = [r for r in lines if r.startswith('CAPTURE_CONTENT,begin,')]
    ends = [r for r in lines if r == 'CAPTURE_CONTENT,end']
    if len(begins) != 1 or len(ends) != 1:
        raise RuntimeError('content_dump_incomplete_or_duplicate')
    result = {'record': [], 'effective': []}
    for line in lines:
        p = line.split(',')
        if p[:2] != ['CAPTURE_CONTENT', 'event'] or len(p) != 10 or p[2] not in result:
            continue
        tick, kind, ch, note, velocity, identity = map(int, p[4:])
        result[p[2]].append(dict(tick=tick, type=kind, channel=ch, note=note,
                                  velocity=velocity, noteId=identity))
    if not result['record'] or Counter(tuple(sorted(r.items())) for r in result['record']) != Counter(tuple(sorted(r.items())) for r in result['effective']):
        raise RuntimeError('storage_effective_mismatch')
    for pitch in pitches:
        rows = [r for r in result['record'] if r['note'] == pitch]
        if (len(rows) != 2 or [r['type'] for r in rows] != [144, 128]
                or [r['velocity'] for r in rows] != [98, 0]
                or any(r['channel'] != channel or not r['noteId'] for r in rows)
                or rows[0]['noteId'] != rows[1]['noteId']):
            raise RuntimeError('content_pair_identity:' + str(pitch))
    if len(result['record']) != 2 * len(pitches):
        raise RuntimeError('content_unexpected_rows')
    return result


def wire_schedule(notes, accepted, samples, begin, *, tolerance_ticks=32, held_growth=False):
    """Functional placement in observed query brackets, not calibrated latency.

    A 32-tick observation tolerance permits USB/host readback skew. It cannot
    hide a one-bar displacement and is not an admitted service/arrival bound.
    """
    period = begin['period']
    base = begin['frameOrigin'] + ((begin['tick'] - begin['frameOrigin']) // period) * period
    ordered = sorted(accepted, key=lambda r: (r['tick'], r['type'] != 128))
    cut = (begin['tick'] - begin['frameOrigin']) % period
    if not held_growth:
        ordered = [r for r in ordered if r['tick'] > cut] + [r for r in ordered if r['tick'] <= cut]
    if len(notes) != len(ordered):
        raise RuntimeError('wire_schedule_inventory_size')
    brackets = []
    for note, source in zip(notes, ordered):
        if note['note'] != source['note'] or note['velocity'] != source['velocity']:
            raise RuntimeError('wire_schedule_order')
        lower = [s for s in samples if s['host_after'] <= note['host_mono']]
        upper = [s for s in samples if s['host_before'] >= note['host_mono']]
        if not lower or not upper:
            raise RuntimeError('wire_schedule_unbracketed')
        low, high = lower[-1]['tick'], upper[0]['tick']
        expected = base + source['tick']
        if held_growth:
            grown = [s for s in samples if s['framePeriod'] == 1536]
            if not grown:
                raise RuntimeError('held_growth_not_observed')
            expected = source['tick'] + (768 if source['type'] == 144 else grown[0]['frameOrigin'])
        elif source['tick'] <= cut:
            expected += period
        if not low - tolerance_ticks <= expected <= high + tolerance_ticks:
            raise RuntimeError('wire_schedule_displaced_interval')
        brackets.append(dict(source_tick=source['tick'], noteId=source['noteId'],
                             expected_tick=expected, observed_tick_bracket=[low, high]))
    return dict(ok=True, observation_tolerance_ticks=tolerance_ticks, rows=brackets)


def _frozen_capture_serving(sample, before, final, *, slot):
    """DEC-136 bound phase: the take's frozen receipt is served while the Record stop job is bound.

    A mismatching Stop's truncation rewind re-anchors the projection, so the frame may be reported
    unbound and the frozen content is then served by the reuse branch in the *same* physical view. A
    bound frame must instead satisfy the ordinary serving terms (identity, geometry, playhead coverage).
    """
    if sample['recording'] or not sample['stopJob']:
        return False
    if (sample['sourceOrigin'] != CAPTURE_ORIGIN or not sample['captureToken']
            or sample['captureToken'] != before['liveToken'] or sample['period'] != final
            or not sample['published'] or not sample['complete']):
        return False
    if sample['boundRole'] == UNBOUND_ROLE:
        return sample['boundStorage'] == 0 and sample['framePeriod'] == 0
    try:
        qualify(sample, slot=slot, recording=False)
    except RuntimeError:
        return False
    return True


def stop_handoff(before, observations, stop, *, slot):
    """Post-stop responsibility and handoff contract for one leg (DEC-136 aware).

    `observations` is the ordered post-stop sample sequence for the same take. A matching Stop keeps
    the physical-reader contract. A mismatching Stop must end in one of the two designed terminals:

    * `corrected_frozen_handoff` — the job is still bound and the corrected frozen content is served
      across the playhead from a different physical view (the pre-route contract, unchanged);
    * `corrected_frozen_root_return` — DEC-136: the frozen take is served while the job is bound (the
      rewind may leave the frame unbound and the reuse branch keeps the same view), then a root-backed
      committed successor serves the take's content, and only then is the job released.

    The scan is ordered, so the corrected handoff wins whenever it is observed; that terminal keeps its
    original meaning (the device still *holds* the job while the corrected frozen content serves) and so
    carries no discharge evidence by design — the recorded growth/empty legs show a corrected-role sample
    and, later, a release, and they are labelled by scan order exactly as they were before this contract.
    On the root-return path a job that is never discharged inside the observation window, or one released
    while the frozen Capture content still serves, is not a completed handoff.
    """
    samples = list(observations)
    if not samples:
        raise RuntimeError('stop_observation_missing')
    for sample in samples:
        if (sample['track'] != 7 or sample['slot'] != slot - 1 or not sample['valid']
                or not sample['epoch'] or sample['recording']):
            raise RuntimeError('stop_snapshot_invalid')
    final = stop['final_length']
    mismatch = stop['raw_length'] > final or before['period'] != final or before['rawMax'] >= final
    if not mismatch:
        # A non-truncating Stop must keep the take **continuously served**, but it need not keep the same
        # physical reader on every take. DEC-136's current-window progression prepares the served window
        # for the interval the playhead is in; that window can predate the take's last accepted appends, so
        # on a one-bar take stopped inside the bar the pre-Stop reader is **empty** while the live capture
        # generation already holds the note (measured natively in
        # `test_midi_input_clock_integration`: `rows 0 -> 2`, `captureToken 2 -> 4` == live 4, no lost row).
        # The post-Stop frozen return window is then the first view that can serve the sealed take, and
        # requiring the old `(currentRole,currentStorage)`/token was unsatisfiable for that class.
        #
        # The narrowed terminal keeps DEC-138's same-reader term **where it is satisfiable** (the pre-Stop
        # window was already current), and otherwise requires the handoff to **complete inside the observed
        # sequence**: some post-Stop sample names the sealed generation (not older than the pre-Stop live
        # token) and holds at least the take's rows. The positional frame may be cleared or still bound
        # (DEC-137).
        after = samples[0]
        if not after['stopJob']:
            raise RuntimeError('stop_responsibility_unproven')
        # DEC-141: "the Stop was processed" is measured, not the clock era. The clock `epoch` does **not**
        # change across a Record stop on hardware (measured `4 -> 4` and `8 -> 8` on two different takes),
        # so requiring `after['epoch'] != before['epoch']` — DEC-138's term — was unsatisfiable for the
        # very class it gates. What the device does produce is a take that was RECORDING and then is not
        # (every post-Stop sample is already required to be non-recording above) while the job takes
        # ownership of the interval; those are the facts this branch asserts.
        if not before['recording']:
            raise RuntimeError('stop_matching_stop_not_processed')
        served_was_current = before['captureToken'] == before['liveToken']
        # The take's row count: the pre-Stop served rows when that window held any (a slice is legitimate),
        # otherwise the raw accepted rows the take already had (the DEC-136 lag case serves none).
        take_rows = before['rawCount'] if before['rows'] == 0 else before['rows']
        if served_was_current:
            # The pre-Stop window already carried the take's final generation: the reader itself must be
            # preserved, exactly as DEC-138 required, and its first post-Stop sample must still hold the take.
            if ((after['currentRole'], after['currentStorage'])
                    != (before['currentRole'], before['currentStorage'])):
                raise RuntimeError('stop_matching_reader_not_preserved')
            if after['captureToken'] != before['captureToken']:
                raise RuntimeError('stop_frozen_geometry_or_token_mismatch')
            if not (take_rows > 0 and after['rows'] >= take_rows):
                raise RuntimeError('stop_matching_take_row_count_mismatch')
            sealed = after
        else:
            # DEC-136 lag: the pre-Stop served window can predate the take's last appends, so the first
            # post-Stop sample may still be that stale window (the earlier images) or may already be the
            # sealed take (the new qualification image). The handoff must complete inside the observed
            # sequence: some sample names the sealed generation (not older than the pre-Stop live token)
            # and holds at least the take's rows.
            sealed = None
            for sample in samples:
                if (sample['captureToken'] >= before['liveToken']
                        and sample['period'] == final
                        and sample['sourceOrigin'] == CAPTURE_ORIGIN
                        and sample['published'] and sample['complete']):
                    sealed = sample
                    break
            if sealed is None:
                raise RuntimeError('stop_matching_reader_not_preserved')
            if not (take_rows > 0 and sealed['rows'] >= take_rows):
                raise RuntimeError('stop_matching_take_row_count_mismatch')
        if (sealed['period'] != final
                or sealed['sourceOrigin'] != CAPTURE_ORIGIN
                or not sealed['published'] or not sealed['complete']):
            raise RuntimeError('stop_frozen_geometry_or_token_mismatch')
        if sealed['boundRole'] == UNBOUND_ROLE:
            if sealed['boundStorage'] or sealed['framePeriod'] != 0:
                raise RuntimeError('stop_matching_frame_inconsistent')
        else:
            qualify(sealed, slot=slot, recording=False)
        return 'matching_physical_reader_preserved'
    for sample in samples:
        if not _frozen_capture_serving(sample, before, final, slot=slot):
            continue
        if ((sample['currentRole'], sample['currentStorage'])
                != (before['currentRole'], before['currentStorage'])):
            return 'corrected_frozen_handoff'
    ok, term = _root_return_terminal(before, samples, final, slot=slot)
    if ok:
        return 'corrected_frozen_root_return'
    raise RuntimeError(term)


def _root_return_terminal(before, samples, final, *, slot):
    """DEC-136 root return: the frozen take served while bound, then the job released after the takeover.

    Returns `(True, None)` when the sequence shows it, else `(False, <first failing term>)`. It is a **fact
    about the sequence**, not a label: the device can serve the frozen content through a corrected-role view
    first and still complete the root return afterwards, so the scenario records this separately from
    `stop_handoff`'s single ordered label.

    There is deliberately no cursor/ledger-progression term: measured on the qualification image (attempt 8),
    a legitimately serving released window parks its read cursor for several samples, so that term refused a
    true root return. What the released window actually *serves* is checked by the scenario against the
    take's own accepted content.
    """
    bound = [index for index, sample in enumerate(samples)
             if _frozen_capture_serving(sample, before, final, slot=slot)]
    if not bound:
        return False, 'stop_frozen_serving_not_observed'
    released = [index for index, sample in enumerate(samples) if not sample['stopJob']]
    if not released:
        return False, 'stop_root_backed_successor_not_observed'
    if released[0] < bound[-1]:
        return False, 'stop_release_not_terminal'
    for index in released:
        sample = samples[index]
        if sample['sourceOrigin'] == CAPTURE_ORIGIN or sample['captureToken']:
            return False, 'stop_early_release_while_frozen_capture_serves'
        if (sample['sourceOrigin'] != COMMITTED_ORIGIN or not sample['published']
                or not sample['complete'] or sample['period'] != final):
            return False, 'stop_root_successor_unqualified'
    return True, None


def root_return_completed(before, observations, stop, *, slot):
    """True when this leg's post-stop sequence completed the DEC-136 root return (job released)."""
    samples = list(observations)
    if not samples:
        return False
    return _root_return_terminal(before, samples, stop['final_length'], slot=slot)[0]


def full_recipe_complete(legs):
    """Each required leg passes with its own evidence: no coverage may be borrowed from another leg.

    * `growth` / `empty` — the truncating (mismatching) Stop class, at least one of them ending in the
      corrected frozen handoff;
    * `held` — the DEC-136 discrimination leg: its Stop sequence must complete the **root return** (the
      bound frozen phase, then the job released after a root-backed successor is read). That is a fact
      about the sequence (`root_return_completed`), recorded beside the ordered label, because the device
      may serve the frozen content through a corrected-role view first and still complete the release —
      measured on the qualification image, attempt 8;
    * `matched` — a non-truncating Stop, which must preserve the same physical reader.

    The leg set is a membership requirement, not a count: extra legs are allowed (and are still required to
    pass), because a coverage gap must be closed by adding the producing leg rather than by re-labelling an
    existing one. Each requirement is attributed to the leg that can produce it, so a leg that silently
    degrades (a held leg that strands, a matched leg that truncates) cannot be covered by another leg.
    """
    by_name = {leg['name']: leg for leg in legs}
    if not {'growth', 'empty', 'held', 'matched'} <= set(by_name):
        return False
    if not all(leg['ok'] for leg in legs):
        return False
    held_stop = by_name['held'].get('stop') or {}
    if not held_stop.get('root_return_completed'):
        return False
    if by_name['matched']['stop_outcome'] != 'matching_physical_reader_preserved':
        return False
    return any(by_name[name]['stop_outcome'] == 'corrected_frozen_handoff'
               for name in ('growth', 'empty'))

def rolling_record_contract_fingerprint():
    """Fingerprint the host contract that gives a rolling-record bundle its meaning.

    Split managed sessions may be aggregated later, so commit+dirty alone is not enough to prove
    that their scenario and verifier agreed. This digest covers the small set of host owners whose
    behavior determines the reports; it is not a firmware or source-arrival guarantee.
    """
    root = Path(__file__).resolve().parents[3]
    relative_paths = (
        'scripts/hitl/verify/rolling_record.py',
        'scripts/hitl/scenarios/rolling_record.py',
        'scripts/hitl/boot_handoff.py',
        'scripts/hitl/runner.py',
        'scripts/hitl/managed_capture.py',
        'scripts/hitl/run_metadata.py',
        'scripts/hitl/stimuli/registry.py',
    )
    digest = hashlib.sha256()
    for relative in relative_paths:
        path = root / relative
        if not path.is_file():
            raise RuntimeError('rolling_record_contract_file_missing:' + relative)
        digest.update(relative.encode('utf-8'))
        digest.update(b'\0')
        digest.update(path.read_bytes())
        digest.update(b'\0')
    return digest.hexdigest()



def classify_rolling_record_pass(pass_group, legs):
    """Classify one managed pass; only `all` can produce the recipe PASS verdict."""
    if pass_group not in ROLLING_RECORD_PASS_GROUPS:
        raise ValueError('rolling_record_pass_group_unknown')
    if not isinstance(legs, (list, tuple)):
        legs = []
    valid_legs = all(isinstance(leg, Mapping) for leg in legs)
    names = [leg.get('name') if isinstance(leg, Mapping) else None for leg in legs]
    duplicate_names = len(names) != len(set(names))
    expected = ROLLING_RECORD_PASS_GROUPS[pass_group]
    if pass_group == 'all':
        shape_ok = valid_legs and not duplicate_names and set(expected) <= set(names)
    else:
        shape_ok = valid_legs and not duplicate_names and tuple(names) == expected
    functional_ok = bool(legs) and valid_legs and all(leg.get('ok') is True for leg in legs)
    try:
        full_ok = (pass_group == 'all' and shape_ok and functional_ok
                   and full_recipe_complete(legs))
    except (KeyError, TypeError):
        full_ok = False
    if full_ok:
        verdict = 'PASS'
    elif pass_group == 'all':
        verdict = 'INCOMPLETE_REQUIRED_STOP_COVERAGE' if shape_ok and functional_ok else 'FAIL'
    else:
        verdict = 'SMOKE_ONLY' if shape_ok and functional_ok else 'FAIL'
    return {
        'ok': full_ok if pass_group == 'all' else shape_ok and functional_ok,
        'functional_legs_ok': functional_ok,
        'full_recipe_complete': full_ok,
        'verdict': verdict,
        'leg_names': names,
    }


def _bundle_issue(issues, run_name, code):
    issues.append(f'{run_name}:{code}')


def aggregate_rolling_record_bundles(bundles):
    """Aggregate completed pass reports without borrowing terminals across legs.

    A single `all` report is accepted for backward compatibility. Otherwise the only accepted
    partition is one same-session `growth-empty` bundle plus separate `held` and `matched` bundles.
    This function checks host/report identity and canonical teardown before applying the unchanged
    `full_recipe_complete` predicate.
    """
    issues = []
    if not bundles:
        return dict(ok=False, verdict='INCOMPLETE_REQUIRED_STOP_COVERAGE',
                    full_recipe_complete=False, issues=['no_run_reports'])
    current_fingerprint = rolling_record_contract_fingerprint()
    prepared = []
    identity = None
    seen_run_dirs = set()
    for index, bundle in enumerate(bundles):
        if not isinstance(bundle, Mapping):
            _bundle_issue(issues, str(index), 'run_bundle_invalid')
            continue
        run_name = str(bundle.get('run_dir') or index)
        if run_name in seen_run_dirs:
            _bundle_issue(issues, run_name, 'duplicate_run_directory')
        seen_run_dirs.add(run_name)
        report = bundle.get('report')
        metadata = bundle.get('metadata')
        shutdown = bundle.get('shutdown')
        cleanup = bundle.get('midi_cleanup')
        if not all(isinstance(value, Mapping) for value in (report, metadata, shutdown, cleanup)):
            _bundle_issue(issues, run_name, 'run_bundle_artifact_missing')
            continue

        pass_group = report.get('pass_group')
        if pass_group not in ROLLING_RECORD_PASS_GROUPS:
            _bundle_issue(issues, run_name, 'pass_group_missing_or_unknown')
        if report.get('report_schema_version') != ROLLING_RECORD_REPORT_SCHEMA_VERSION:
            _bundle_issue(issues, run_name, 'report_schema_mismatch')
        if report.get('validation_contract_fingerprint') != current_fingerprint:
            _bundle_issue(issues, run_name, 'validation_contract_mismatch')
        if report.get('stimulus') != ROLLING_RECORD_STIMULUS:
            _bundle_issue(issues, run_name, 'stimulus_mismatch')
        authorization = report.get('authorization')
        slots = authorization.get('slots') if isinstance(authorization, Mapping) else None
        if (not isinstance(authorization, Mapping) or authorization.get('track') != 8
                or not isinstance(slots, (list, tuple)) or sorted(slots) != [4, 5]):
            _bundle_issue(issues, run_name, 'authorization_mismatch')

        firmware_hash = metadata.get('firmware_hex_sha256')
        if not isinstance(firmware_hash, str) or re.fullmatch(r'[0-9a-f]{64}', firmware_hash) is None:
            _bundle_issue(issues, run_name, 'firmware_identity_missing')
        if metadata.get('build_env') != ROLLING_RECORD_BUILD_ENV:
            _bundle_issue(issues, run_name, 'qualification_build_required')
        scenario_ids = metadata.get('scenario_ids')
        selected_slot = metadata.get('loop_slot')
        selected_slot_valid = (isinstance(selected_slot, int) and not isinstance(selected_slot, bool)
                               and isinstance(slots, (list, tuple)) and selected_slot in slots)
        if (not isinstance(metadata.get('git_commit'), str) or not metadata.get('git_commit')
                or metadata.get('track_number') != 8 or not selected_slot_valid
                or metadata.get('midi_channel') != 8 or not isinstance(scenario_ids, list)
                or 'rolling_record' not in scenario_ids):
            _bundle_issue(issues, run_name, 'run_metadata_mismatch')
        if shutdown.get('ok') is not True or shutdown.get('status') != 'complete' or shutdown.get('drains') != 2:
            _bundle_issue(issues, run_name, 'shutdown_incomplete')
        cleanup_total = cleanup.get('total')
        cleanup_completed = cleanup.get('completed')
        if (cleanup.get('ok') is not True or cleanup.get('status') != 'complete'
                or not isinstance(cleanup_total, int) or isinstance(cleanup_total, bool) or cleanup_total < 1
                or not isinstance(cleanup_completed, int) or isinstance(cleanup_completed, bool)
                or cleanup_completed != cleanup_total):
            _bundle_issue(issues, run_name, 'midi_cleanup_incomplete')

        legs = report.get('legs')
        if not isinstance(legs, list) or not all(isinstance(leg, Mapping) for leg in legs):
            _bundle_issue(issues, run_name, 'leg_list_missing_or_invalid')
            legs = []
        if pass_group in ROLLING_RECORD_PASS_GROUPS:
            classification = classify_rolling_record_pass(pass_group, legs)
            if not classification['ok']:
                _bundle_issue(issues, run_name, 'pass_group_not_complete')
            if (pass_group != 'all' and
                    (report.get('verdict') == 'PASS' or report.get('full_recipe_complete') is True)):
                _bundle_issue(issues, run_name, 'partial_report_claims_recipe_pass')
            if report.get('functional_legs_ok') is not classification['functional_legs_ok']:
                _bundle_issue(issues, run_name, 'functional_leg_summary_mismatch')
            if report.get('full_recipe_complete') is not classification['full_recipe_complete']:
                _bundle_issue(issues, run_name, 'full_recipe_summary_mismatch')
            if report.get('verdict') != classification['verdict']:
                _bundle_issue(issues, run_name, 'pass_verdict_mismatch')
            terminals = {leg.get('name'): leg.get('stop_outcome') for leg in legs}
            if report.get('stop_terminals_by_leg') != terminals:
                _bundle_issue(issues, run_name, 'stop_terminal_summary_mismatch')
            if report.get('ok') is not classification['ok']:
                _bundle_issue(issues, run_name, 'pass_ok_summary_mismatch')
            for leg in legs:
                name = str(leg.get('name'))
                if leg.get('ok') is not True or leg.get('issues'):
                    _bundle_issue(issues, run_name, 'leg_failed:' + name)
                drain = leg.get('record_evidence_drain')
                if not isinstance(drain, Mapping) or drain.get('complete') is not True:
                    _bundle_issue(issues, run_name, 'leg_record_drain_incomplete:' + name)
                if name != 'matched':
                    inventory = leg.get('wire_inventory')
                    schedule = leg.get('wire_schedule')
                    content = leg.get('content')
                    if (not isinstance(inventory, Mapping) or inventory.get('ok') is not True
                            or not isinstance(schedule, Mapping) or schedule.get('ok') is not True
                            or not isinstance(content, Mapping)
                            or not content.get('record') or not content.get('effective')):
                        _bundle_issue(issues, run_name, 'leg_content_or_schedule_incomplete:' + name)
                elif (not isinstance(leg.get('wire_inventory'), Mapping)
                      or not isinstance(leg.get('wire_schedule'), Mapping)
                      or leg['wire_schedule'].get('skipped') != 'matched leg: Stop class only'):
                    _bundle_issue(issues, run_name, 'matched_stop_scope_missing')

        run_identity = (
            firmware_hash,
            metadata.get('git_commit'),
            report.get('validation_contract_fingerprint'),
            report.get('stimulus'),
            json.dumps(authorization, sort_keys=True) if isinstance(authorization, Mapping) else None,
            selected_slot,
        )
        if identity is None:
            identity = run_identity
        elif run_identity != identity:
            if firmware_hash != identity[0]:
                _bundle_issue(issues, run_name, 'firmware_identity_mismatch')
            if selected_slot != identity[5]:
                _bundle_issue(issues, run_name, 'selected_slot_mismatch')
            _bundle_issue(issues, run_name, 'run_identity_mismatch')
        prepared.append((run_name, pass_group, report))

    if issues:
        return dict(ok=False, verdict='INCOMPLETE_REQUIRED_STOP_COVERAGE',
                    full_recipe_complete=False, issues=issues)

    groups = [group for _run_name, group, _report in prepared]
    if groups == ['all']:
        all_legs = list(prepared[0][2]['legs'])
    else:
        if any(group in ('growth', 'empty') for group in groups):
            return dict(ok=False, verdict='INCOMPLETE_REQUIRED_STOP_COVERAGE',
                        full_recipe_complete=False, issues=['growth_empty_same_run_required'])
        if 'all' in groups:
            return dict(ok=False, verdict='INCOMPLETE_REQUIRED_STOP_COVERAGE',
                        full_recipe_complete=False, issues=['all_pass_cannot_mix_with_partial_groups'])
        if len(groups) != len(ROLLING_RECORD_AGGREGATE_GROUPS) or set(groups) != set(ROLLING_RECORD_AGGREGATE_GROUPS):
            issues = []
            if len(groups) != len(set(groups)):
                issues.append('duplicate_pass_group')
            if set(groups) != set(ROLLING_RECORD_AGGREGATE_GROUPS):
                issues.append('required_pass_group_missing')
            return dict(ok=False, verdict='INCOMPLETE_REQUIRED_STOP_COVERAGE',
                        full_recipe_complete=False, issues=issues)
        by_group = {group: report for _run_name, group, report in prepared}
        all_legs = []
        for group in ROLLING_RECORD_AGGREGATE_GROUPS:
            all_legs.extend(by_group[group]['legs'])

    names = [leg.get('name') for leg in all_legs]
    if len(names) != len(set(names)):
        return dict(ok=False, verdict='INCOMPLETE_REQUIRED_STOP_COVERAGE',
                    full_recipe_complete=False, issues=['duplicate_leg_name'])
    complete = full_recipe_complete(all_legs)
    if not complete:
        return dict(ok=False, verdict='INCOMPLETE_REQUIRED_STOP_COVERAGE',
                    full_recipe_complete=False, issues=['full_recipe_predicate_failed'])
    return dict(
        ok=True,
        verdict='PASS',
        full_recipe_complete=True,
        issues=[],
        firmware_hex_sha256=identity[0],
        git_commit=identity[1],
        validation_contract_fingerprint=identity[2],
        stimulus=identity[3],
        pass_groups=[group for _run_name, group, _report in prepared],
        source_runs=[run_name for run_name, _group, _report in prepared],
        stop_terminals_by_leg={leg['name']: leg['stop_outcome'] for leg in all_legs},
    )
