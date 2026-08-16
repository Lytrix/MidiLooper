# Overdub overlap hold — same-start collection (RC1)

**Status:** Active — native PASS; device gate open  
**Date:** 2026-08-16  
**Kind:** bugfix  
**Evidence:** [`233323`](../../captures/session_20260816_233323.log)  
**Parent:** [`overdub_playback_observation_overlap_refinement.md`](overdub_playback_observation_overlap_refinement.md)  
**Does not start:** wrap `beginCapture` re-establish delay; LCR `resolveState` on MIDI; Gate 3 empty-set fallback scan

---

## Symptom

Overdub on an occupied lane does not Shorten/Hide the older note. NOTE_EDIT can move the new note; after deselect the older note is still there.

[`233323`](../../captures/session_20260816_233323.log):

| Stop | note_offs | empty_sets | looked_up | max_ids | add | shorten | hide |
|------|----------:|-----------:|----------:|--------:|----:|--------:|-----:|
| 52.212 s | 17 | 17 | 0 | 0 | 17 | 0 | 0 |
| 62.029 s | 23 | 23 | 0 | 0 | 23 | 0 | 0 |
| 94.248 s | 2 | 2 | 0 | 0 | 2 | 0 | 0 |
| 102.884 s | 19 | 19 | 0 | 0 | 19 | 0 | 0 |

1-bar slot after 347.4 s: same pattern twice (storage 0 / 64 / 296 / 528 / 704). Cache **1 → 3 → 6**. NOTE_EDIT `visual_notes=6`, mover **60@528**, focus `overlapParticipants count=0`. Deselect leaves the unmoved 60@528.

Gate 3: empty `PendingNote.overlapNoteIds` is Add only.

---

## Root cause

`Track::snapshotOverlapHoldCandidates` keeps a source note only when `linearStart < holdStart`. A note that **starts at S** is excluded.

`Track::collectOverlapHoldPlaybackNoteOn` can insert that id only after `pendingNotes` already holds the incoming note. On a grid-aligned overdub the committed note-on and the incoming note-on share the tick, so collect runs while `pendingNotes` is empty.

Together: every same-start hold is an empty set. Already-sounding notes (`start < S`) still collect — [`162856`](../../captures/session_20260813_162856.log) / [`163422`](../../captures/session_20260813_163422.log) had `looked_up > 0`.

Consume geometry (`existingNoteOverlapsIncomingHold`) already treats same-start as overlap. The miss is collection only.

---

## Delay (same path, not this RC)

`snapshotOverlapHoldCandidates` is `TRACK_COLD_MEM` and walks all `overdubSourceViewNotes_` on every overdub note-on. 64-bar [`233323`](../../captures/session_20260816_233323.log) copied the clean cache at `begin_capture` (1691 notes). That walk already runs and returns nothing when the inequality misses.

After this fix, same-start note-off will `appendNotesForIds` (Gate 4, one pass). Historical 68-bar lookup was 0.4–1.1 ms ([`163422`](../../captures/session_20260813_163422.log)). Do not add an empty-set full-view fallback (Gate 3).

Wrap `commitOverdubWrapAtSessionStart` → `beginCapture` → `establishOverdubSourceView` after `invalidateCaches` is a **later slice**. Not this RC.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. `Track::snapshotOverlapHoldCandidates` still fills `PendingNote.overlapNoteIds`. |
| **State transition change?** | NO. Note-on / note-off / wrap commit unchanged. |

Reuse: extend the existing snapshot inequality. No new name.

---

## Fix

Snapshot occupancy at S is half-open `[start, end)`:

```text
linearStart <= holdStart && holdStart < linearEnd
```

(and the one-loop shifted form). Playback collect stays for notes that start after S.

---

## Tests

- `test_overlap_hold_candidates` — note starting at S is in the snapshot set; note ending at S is not.
- Existing `test_pending_note_change` consume fixtures unchanged.

Device: 1-bar grid re-overdub of an occupied lane. `overlap_hold` `looked_up > 0`, `shorten` or `hide` > 0. NOTE_EDIT deselect does not leave a stacked unmoved row on that lane.

---

## Pre-implementation review

### Ready

- Owner and call sites traced: `Track::noteOn` → `snapshotOverlapHoldCandidates`; `Track::sendMidiEvent` → `collectOverlapHoldPlaybackNoteOn`; note-off → `accumulatePendingNoteChangesForIncomingNote`.
- Consume already overlaps same-start once ids are present.
- Native helper exists; firmware must not include `OverlapHoldCandidates.h` (ITCM).

### Resolved

| Topic | Decision |
|-------|----------|
| Same-start occupancy | `<=` on start, `<` on end |
| Empty-set fallback scan | No (Gate 3) |
| Wrap re-establish | Later slice |
| LCR on MIDI | No (DEC-037) |

### Open before coding

None.

### Proceed?

YES.
