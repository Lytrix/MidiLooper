# Overdub overlap hold — same-start collection (RC1)

**Status:** RC1–RC12 shipped — occupied-lane consume + display FROZEN [`140355`](../../captures/session_20260817_140355.log); successor [`overdub_loop_length_during_overdub_enhancement.md`](overdub_loop_length_during_overdub_enhancement.md)  
**Date:** 2026-08-17  
**Kind:** bugfix  
**Evidence:** [`233323`](../../captures/session_20260816_233323.log) (RC1); [`235407`](../../captures/session_20260816_235407.log) (RC2); [`000417`](../../captures/session_20260817_000417.log) (RC3); [`001517`](../../captures/session_20260817_001517.log) (RC4); [`003204`](../../captures/session_20260817_003204.log) (RC5)  
**Parent:** [`overdub_playback_observation_overlap_refinement.md`](overdub_playback_observation_overlap_refinement.md)  
**Does not start:** LCR `resolveState` on MIDI; Gate 3 empty-set fallback scan

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

Wrap `beginCapture` no longer re-establishes (RC2). That also avoids a dirty-cache reconstruct on wrap.

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

Device RC1: 1-bar grid re-overdub of an occupied lane. `overlap_hold` `looked_up > 0`.

---

## RC2 — wrap source view keeps this-session notes

**Evidence:** [`235407`](../../captures/session_20260816_235407.log)

RC1 collection works: stop `empty_sets=0`, `looked_up=2`, `max_ids=1`. Same-start exact against the session-start note (60@528) Hides. Inner and same-start-longer do not.

Before overdub, undo left `slice_clean notes=1`. Five wrap commits (1152 / 1920 / 2688 / 3456 / 4224). Stop DNTE still has **60@64 length 176 and 224** plus **60@224 length 16**. Consume already Shortens an inner hold and Hides a same-start-longer hold when the prior id is in `overdubSourceViewNotes_` (`test_pending_shorten_long_source_on_overlap`, `test_pending_hide_same_start_longer`).

`commitOverdubWrapAtSessionStart` called `beginCapture` → `establishOverdubSourceView` after `invalidateCaches`. Dirty cache is not authoritative, so the view was rebuilt without this-session Adds. Playback could still insert those ids; `appendNotesForIds` found nothing.

### Architecture checkpoint (RC2)

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. `Loop` still owns `overdubSourceViewNotes_` and pending changes. |
| **State transition change?** | NO. Wrap still seals and `beginCapture`s. Source view stays established for the session. |

### Fix

- `beginCapture(Overdub)` calls `establishOverdubSourceView` only when the view is not established.
- `applyPendingNoteChangesToOverdubSourceView` merges Add / Shorten / Hide into the view before seal.
- `commitOverdubWrapAtSessionStart` applies pending, then seals.

### Tests

- `test_pending_hide_same_start_longer` — incoming 64–288 Hides source 64–240.
- `test_wrap_keeps_source_view_inner_shortens_prior_add` — wrap-2 inner 224–240 Shortens wrap-1 Add 64–240 to 223.
- `test_wrap_keeps_source_view_same_start_longer_hides_prior_add` — wrap-2 64–288 Hides wrap-1 Add 64–240.

Device: 1-bar inner (start after existing start, end before existing end) Shortens the outer. Same-start longer Hides the shorter. NOTE_EDIT does not show stacked 60@64 rows.

---

## RC3 — wrap commit must not clear the session source view

**Evidence:** [`000417`](../../captures/session_20260817_000417.log)

RC2 `beginCapture` skip and `applyPendingNoteChangesToOverdubSourceView` never ran on device.

`commitOverdubWrapAtSessionStart` order:

1. `commitCapturePass` → `commitPendingCapturePass` → `clearOverdubSourceView()`
2. `applyPendingNoteChangesToOverdubSourceView` — no-op (`!overdubSourceViewEstablished_`)
3. `beginCapture(Overdub)` — view is gone, so it re-establishes after `invalidateCaches` and resets `overlapHoldTotals_`

[`000417`](../../captures/session_20260817_000417.log): `lcr,6c` at begin_capture **and** at every wrap (notes 1 → 5 → 7). Stop `overlap_hold` all zeros. Stop DNTE still stacked **60@64 length 176 and 224** plus **60@296 length 96 and 120**. Flatten has five 60 Ons at 64 and offs at 240 / 288 — reconstruct pairs the shorter Add with a later off, so the earlier note looks lengthened and still duplicated.

Native RC2 tests called apply + `beginCapture` without `commitPendingCapturePass` in between, so they passed.

### Architecture checkpoint (RC3)

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. `Loop` still owns the source view. |
| **State transition change?** | NO. Overdub session already spans wraps. View leave-point is session close / discard, which is the RC2 contract. |

### Fix

- `commitPendingCapturePass` clears the source view only when `hasOverdubSession()` is false (record commit / no session).
- `closeOverdubSession` calls `clearOverdubSourceView`.
- `discardCapture` still clears.

### Tests

- `test_wrap_commit_keeps_source_view_same_start_longer_hides_prior_add` — production wrap order: accumulate → seal → `commitPendingCapturePass` → apply → `beginCapture` → same-start-longer Hide.
- `test_discard_and_commit_clear_source_view` — commit without session still clears; commit with session keeps the view; `closeOverdubSession` clears.

Device: same 1-bar inner / same-start-longer as RC2. Stop `overlap_hold` `note_offs > 0` and no stacked 60@64 176+224.

Not this RC: `finalizePendingNotes` still does not accumulate. Last-held 60@296–392 vs 296–416 (96 and 120) stays a later slice if it remains after wrap commit keeps the view.

---

## RC4 — apply Hide on each capture pass before merge

**Evidence:** [`001517`](../../captures/session_20260817_001517.log)

RC3 ran: one `begin_capture`, `overlap_hold` `note_offs=19` `looked_up=12` `max_examined=8`. The source view already holds this-session Adds. Stop DNTE still stacked **60@64 length 176 and 224**.

Not a missing materialize of “temporary passes.” Overlap already sees wrap notes via `applyPendingNoteChangesToOverdubSourceView`. Hide of wrap-1 id 10 seals. `LoopPasses::materializeToEventVector` then applied that Delete on the **merged** flatten. `applyDeleteNoteById` LIFO-pairs the earlier On with the later Off@288, leaves On@64 + Off@240 (length 176), and `appendOverdubPassDisplayNotes` adds the surviving pass’s true 64–288. Native `test_wrap_commit_hide_drops_shorter_same_start_from_display` was **Expected 1 Was 2** before this fix.

### Architecture checkpoint (RC4)

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. `LoopPasses::materializeToEventVector` still owns flatten + edit apply. |
| **State transition change?** | NO. Same seal, same display rebuild. |

Reuse: apply note edits on each record/overdub layer, then merge — same per-pass apply `appendOverdubPassDisplayNotes` already uses. No new pass type.

### Tests

- `test_wrap_commit_hide_drops_shorter_same_start_from_display` — wrap-1 64–240 Hide + wrap-2 64–288; gather + reconstruct + append is one 60@64 ending 288.
- `makeOverdubPassWithNote` uses `NoteId` 2 so a Delete of record `NoteId` 1 cannot hit the overdub layer (production allocates unique ids).

Device: stop DNTE one 60@64 (the longest). No stacked 176+224.

---

## RC5 — LCR resolveWindow apply Hide per capture pass before merge

**Evidence:** [`003204`](../../captures/session_20260817_003204.log)

RC4 is on the device (`materializeToEventVector` per-pass + `LOOP_COLD_MEM`). Overlap ran: `note_offs=24` `looked_up=13` `max_examined=7` `hide=2`. Stop idle cache used prepared LCR (`lcr,6a ev=31 notes=10` then `VCACHE,slice_clean notes=10`), not `gatherCommittedEvents`. Select: `1/3` then `2/3` notes at tick 64 — `DNTE` 60@64 length **224** (`noteId=105`) and **176** (`noteId=124`).

Idle SEVT / first DNTE dump is `mergeActiveCapturePasses` (raw chunks, no edits). Not the display gate.

`LoopContentResolution::resolveWindow` merged all passes, then `applyNoteEditPassSequence`. Delete LIFO-paired the shorter On with Off@288. Native `test_resolve_window_hide_drops_shorter_same_start` is wrap-1 64–240 Hide + wrap-2 64–288 through `resolveWindow`.

Not a new pass type. No LCR on MIDI. DEC-037: this is the already-wired 6A idle consumer, not a new display wire.

### Architecture checkpoint (RC5)

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. `LoopContentResolution::resolveWindow` already owns prepared-window flatten for idle display. |
| **State transition change?** | NO. Same seal, same idle slice. |

Reuse: apply note edits on each capture pass, then merge — same as `LoopPasses::materializeToEventVector`.

### Tests

- `test_resolve_window_hide_drops_shorter_same_start` — index `resolveWindow` matches per-pass materialize; one 60@64 ending 288.

Device: stop select DNTE one 60@64 (the longest). No stacked 176+224.

---

## RC6 — rebuild source view after each committed wrap

**Status:** Native in this commit; device gate open.  
**Evidence:** [`005745`](../../captures/session_20260817_005745.log) 1-wrap PASS (keep consume); [`004947`](../../captures/session_20260817_004947.log) multi-wrap stale S snapshot.  
**Sibling:** [`overdub_overlap_hold_wrap_source_view_rebuild_bugfix.md`](overdub_overlap_hold_wrap_source_view_rebuild_bugfix.md)

`overdubSourceViewNotes_` stayed the session-start snapshot. Wrap 1 consume worked. After `publishPreparedOverdubPass`, wrap 2 had no wrap-1 span geometry.

### Architecture checkpoint (RC6)

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. `Loop` still owns the source view. |
| **State transition change?** | NO. Wrap still seals, publishes, `beginCapture(Overdub)`. |

### Fix

After seal + publish, `Loop::rebuildOverdubSourceView` consumes `tryResolvePreparedWindow` (else windowed `resolveWindow(passes)`) and `reconstructDisplayNotes`. Not visual cache. Not `establishOverdubSourceView`. Wrap path no longer calls `applyPendingNoteChangesToOverdubSourceView`. Consume unchanged.

CAP: `DIAG,lcr,vch` (idle visual cache; was `6a`); `DIAG,lcr,src,why=open` (establish; was `6c`); `DIAG,lcr,src,why=wrap,from=prep|win` (rebuild).

### Tests

- `test_wrap_commit_keeps_source_view_same_start_longer_hides_prior_add` — rebuild, no applyPending; wrap-2 Hides wrap-1 Add.
- `test_rebuild_overdub_source_view_after_publish_includes_wrap_add` — prepared index after publish.

Device: 1-bar same-start-longer two wraps; select at 64 one 60 ending 288. Wrap `src,why=wrap`; wrap `beginCapture` must not emit `why=open`. 1-wrap [`005745`](../../captures/session_20260817_005745.log) stays green.

---

## RC7 — enter uses the same rebuild as wrap

**Status:** Native in this commit; device gate open.  
**Evidence:** [`113236`](../../captures/session_20260817_113236.log) — overdub undo left `VCACHE slice_clean notes=1`; enter copied that cache; first wrap was Gate 3 empty-set Adds until wrap rebuild.  
**Sibling:** [`overdub_overlap_hold_enter_source_view_rebuild_bugfix.md`](overdub_overlap_hold_enter_source_view_rebuild_bugfix.md)

### Architecture checkpoint (RC7)

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. `Loop` still owns the source view. |
| **State transition change?** | NO. Enter is still `beginCapture(Overdub)` → `establishOverdubSourceView`. |

### Fix

`establishOverdubSourceView` calls `rebuildOverdubSourceView(..., "open")`. Not visual cache. Not `copyEffectiveCommittedEventsInRange`. Display idle `vch` unchanged. Consume unchanged.

Device: enter emits `src,why=open,from=prep|win`; first occupied-lane notes `looked_up > 0`.

---

## RC8 — hold-window just-in-time fill (not full loop)

**Status:** Native in this commit; device gate open.  
**Evidence:** [`120324`](../../captures/session_20260817_120324.log) / [`115622`](../../captures/session_20260817_115622.log) — enter window missed 60@224; empty hold IDs Add-only.  
**Sibling:** [`overdub_overlap_hold_hold_window_jit_bugfix.md`](overdub_overlap_hold_hold_window_jit_bugfix.md)

### Architecture checkpoint (RC8)

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. `Loop::ensureOverdubSourceNotesForHold` still owns D2 fill. |
| **State transition change?** | NO. Consume unchanged. |

### Fix

16-bar `resolveWindow(passes)` for **this pitch** at the hold tick. Not prepared. Not the full loop. Note-on snapshot merges sounding notes only. Note-off unions newly merged overlapping pitch notes when hold IDs are empty. Gate 3 stays for notes already in the enter view.

Device: `src,why=hold,from=win` with `merged>0` on occupied-lane hold that starts before the missed note; Hide or Shorten, not Add-only stacked `SEVT`.

---

## RC9 — transport stop finalizes pending before All Notes Off

**Status:** Native in this commit; device gate open.  
**Evidence:** [`122152`](../../captures/session_20260817_122152.log) — extra 60@64 length 104 from wrap_synth Off@168 after `sendAllNotesOff` cleared pending.  
**Sibling:** [`overdub_overlap_hold_transport_stop_pending_bugfix.md`](overdub_overlap_hold_transport_stop_pending_bugfix.md)

### Architecture checkpoint (RC9)

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. |
| **State transition change?** | NO. |

### Fix

Same order as record: `stopOverdubbingToStopped` / `finalizePendingNotes` then `sendAllNotesOff`. Stop-close also accumulates overlap, same as MIDI note-off.

---

## RC10 — live paint applies pending Hide/Shorten

**Status:** Native in this commit; device gate open.  
**Evidence:** [`122152`](../../captures/session_20260817_122152.log) — Hide at note-off, `DISP` unchanged until stop cache rebuild.  
**Sibling:** [`overdub_overlap_hold_live_pending_display_bugfix.md`](overdub_overlap_hold_live_pending_display_bugfix.md)

### Architecture checkpoint (RC10)

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. Paint overlay only. |
| **State transition change?** | NO. |

### Fix

`resolveDisplayNotesLiveCapture` paints a copy with pending Hide/Shorten. Does not mutate visual cache or the live-capture compose indices.

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
