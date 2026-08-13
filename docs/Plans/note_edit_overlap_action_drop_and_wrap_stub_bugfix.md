# NOTE_EDIT overlap action drop and wrap-stub commit

**Status:** Active — RC1 withdrawn; RC2 native shipped (device FAIL); RC3 native shipped; device gate open  
**Date:** 2026-08-13  
**Kind:** bugfix  
**Parent:** [`note_edit_visual_cache_display_unification_refinement.md`](note_edit_visual_cache_display_unification_refinement.md) (Stages 8–9 shipped; do not reopen evaluate/select rematerialize)  
**Evidence:** [`session_20260813_192755.log`](../../captures/session_20260813_192755.log)  
**Overlap consume:** out of scope — [`overdub_playback_observation_overlap_refinement.md`](overdub_playback_observation_overlap_refinement.md)  
**Sibling (193838 mover jump):** [`note_edit_mover_wrap_length_jump_bugfix.md`](note_edit_mover_wrap_length_jump_bugfix.md) — do not fold 47→2351 into RC2.  
**Sibling (replay writer):** [`note_edit_length_replay_loop_boundary_bugfix.md`](note_edit_length_replay_loop_boundary_bugfix.md) — persisted `2256–2304` Length row is destructive on rematerialize; keep RC2/RC3 emit filter.

After overdub undo (cache **60 → 34**), moving noteId **111** on pitch 30: the resolve at 67.824 emits only `MoveNote` for an already-hidden neighbor. That step is min-length stay-hidden, not a dropped Shorten. Select-away commits noteId **14** as `ChangeLength` `2256–2304` (loop end).

---

## Debugging boundary

```
Loop::passes / rematerializeEditView     ← session-store apply at open; keep
        ↓
visualCache + current-state overlay      ← Stage 8/9; do not reopen
        ↓
analyze → resolveConstrainedGeometry → buildEditSessionActions   ← RC1 withdrawn
        ↓
buildPreCommitBaselineLiveDiffOverlapPasses                      ← RC2
        ↓
overdubSourceViewNotes_ / overlapNoteIds ← do not read
```

Do not fold paint 59 vs 60 or undo-warm into this plan. Do not emit Shorten for a below-`noteMinLengthTicks` stub.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. `buildPreCommitBaselineLiveDiffOverlapPasses` keeps overlap commit rows. RC1 does not change `appendOverlapTargetActions`. |
| **State-transition change?** | NO. NOTE_EDIT open / rematerialize-at-open / commit unchanged. |
| **Reuse** | YES — extend `buildPreCommitBaselineLiveDiffOverlapPasses` (RC2). Keep inclusive `linearSpansOverlapForAnalysis`. Keep `test_builder_omits_hide_when_live_already_absent`. |

---

## Device facts ([`192755`](../../captures/session_20260813_192755.log))

Undo then NOTE_EDIT on a 3-bar loop (`2304` ticks), cache **34**.

Selected mover: noteId **111**, pitch **30**, first `DNTE,30,528,528,47`. Coarse moves keep length **47**.

### RC1 — Hide/Shorten dropped for one step

Same-pitch neighbor **16** (`855–1046`):

| Time | Resolver | Actions |
|------|----------|---------|
| 67.251 | `storeNoteOns=34` `interactions=1 constrained=1` | `HideNote` 16 `855–1046` + `MoveNote` 111 `816–863` |
| 67.824 | `storeNoteOns=33` `interactions=1 constrained=1` | **`MoveNote` 111 `864–911` only** |
| 67.854 | `storeNoteOns=33` `interactions=1 constrained=1` | `ShortenNote` 16 `855–911` + `MoveNote` 111 `912–959` |

`appendOverlapTargetActions`: when `!constrained.visible` and `!livePresent`, the function `continue`s without a target action. That is the existing contract in `test_builder_omits_hide_when_live_already_absent`.

**RC1 withdrawn — proven path, not a dropped Shorten.** At 67.824 the causing span is `864–911` against baseline `855–1046`:

- `classifyEditSessionInteraction` returns `OverlapNoteOff` (`855 < 864` and `1046 >= 864`).
- `computeShortenedEndTick` is `863`.
- Stub length `855–863` is **8** ticks. `noteMinLengthTicks` is **12** (`Config::DEFAULT_NOTE_MIN_LENGTH_TICKS`). `resolveConstrainedGeometry` sets `visible = false`.
- 16 is already absent (`storeNoteOns=33`). Silent continue keeps Hide. Emitting `ShortenNote` would reinsert an 8-tick pair.

67.854 is the first step whose stub is long enough (`855–911` = 56 ticks): `ShortenNote` 16 `855–911`. Same omit repeats at 70.725. Neighbor **96** Hide → Shorten → Restore is the same owner with stubs ≥ 12 ticks (`576–623` = 47).

### RC2 — Wrap stub committed to loop end

Focus rebuild of 111 (65.743) and later 22 (86.412) lists `overlapParticipantNoteId=14`.

Select-away at 81.670:

```
pre-commit row: source=overlap_baseline_diff targetNoteId=14 action=Update property=Length start=2256 end=2304
commitEditAction incoming ChangeLength targetNoteId=14 start=2256 moverBaselineEnd=575 newEnd=2304
```

`buildPreCommitBaselineLiveDiffOverlapPasses` emits a Length row when an overlap participant’s live end ≠ `baselineMap` end. Stage 6 skipped inserting a Visible row from a zero-length display note (`endTick == startTick`, 174635 noteId **14**). After undo, 14 is still an overlap participant and this path writes `2256–2304`.

Selected `DNTE` lengths stay 47 / 95. The loop-end bar is note **14**, not the mover.

---

## RC1 — Withdrawn: 67.824 is min-length stay-hidden

**Owner:** `resolveConstrainedGeometry` + `appendOverlapTargetActions` — no firmware change.

**Why withdrawn:** 67.824 is `OverlapNoteOff` whose remaining stub is 8 ticks, below `noteMinLengthTicks` (12). `constrained.visible` is false and 16 is already absent. `test_builder_omits_hide_when_live_already_absent` already requires no target action.

**Do not:** emit `ShortenNote` from that invisible constrained row (reinserts a below-min-length pair). Do not reopen Stage 8/9.

If a later capture shows 16 **painted at full `855–1046`** during the 864 step, that is projection, not this builder path — open a sibling then. The 192755 action log does not prove a paint-at-full-length frame for 16 at 67.824.

---

## RC2 — Wrap stub 14 must not commit Length to loop end ✅ shipped (native)

**Owner:** `buildCommitOverlapRowsFromCurrentState` (device commit when current-state is non-empty) and `buildPreCommitBaselineLiveDiffOverlapPasses` (parity / undo) in [`NoteEditFocusPreCommit.cpp`](../../src/EditManager/NoteEditFocusPreCommit.cpp). The 81.670 log label `source=overlap_baseline_diff` is any non-mover row.

**Invariant:** an overlap participant whose painted / visual-cache span is zero-length (`endTick == startTick`) or that is not in `visualCache.notes` must not emit `ChangeLength` / NoteRange to `loopLength` (`2256–2304` on this loop).

**Change:** both builders skip Length/NoteRange when `live.endTick == loopLength` and `committedDisplayNotes` has no painted non-zero span for that `NoteId`. `commitAllPendingNoteEditActions` passes `visualCacheNotesForSelectedSlot`. Null `committedDisplayNotes` keeps prior emit (existing tests). Do not change `rowIncludedInSelectableInventory`. Do not call overdub consume.

**Test:** `test_commit_skips_unpainted_loop_end_length_192755` — 14 live `2256–2304` with zero-length cache row emits no loop-end Length; painted 16 `855–911` still emits Length.

**Not this RC:** RC1 action emit, open-time rematerialize, undo-warm.

---

## Device gate (after RC2) — FAIL

[`193838`](../../captures/session_20260813_193838.log) and [`201948`](../../captures/session_20260813_201948.log) still write `ChangeLength` 14 `2256–2304`. RC2 only skips when the cache has no non-zero span for 14. Device still emits, so 14 is in `visualCache.notes` with `endTick != startTick`. The RC2 fixture’s “painted 14 still emits” path is the device path.

`commitEditAction incoming` logs Length rows only. 201948 pre-commit still lists mover **100** NoteRange; that is not a dropped mover row in this owner.

---

## RC3 — Skip loop-end Length unless cache paints loop end ✅ shipped (native)

**Owner:** `shouldSkipOverlapDiffToLoopEnd` in [`NoteEditFocusPreCommit.cpp`](../../src/EditManager/NoteEditFocusPreCommit.cpp).

**Invariant:** an overlap participant with live end == `loopLength` must not emit `ChangeLength` / NoteRange to `loopLength` unless `visualCache.notes` paints that `NoteId` with `endTick == loopLength`. Null display list still emits.

**Change:** replace “any non-zero painted span” with “painted end == `loopLength`”. Zero-length / missing / wrap / short painted ends skip. Painted `2256–2304` still emits.

**Test:** `test_commit_skips_unpainted_loop_end_length_192755` — painted `2256–2280` no longer emits loop-end Length; painted `2256–2304` still does. `test_commit_skips_painted_wrap_stub_loop_end_length_201948` — wrap paint `2256–96` + live `2256–2304` emits no loop-end Length.

**Not this RC:** note 5 cache pairing, mover pitch 11 after select-away, undo-warm.

---

## Device gate (after RC3)

Same 34-note loop as 201948 / 193838:

1. Select away after a move: no `ChangeLength targetNoteId=14 … end=2304`.
2. 16 still Hide at 816, omit at 864 (stub &lt; 12), Shorten at 912. 96 / 76 Hide → Restore still run.

Env: `teensy41-capture-serial`. Ask before upload.

---

## Pre-implementation review

### Ready

- Production overlap commit owner is `buildCommitOverlapRowsFromCurrentState`; parity/undo uses `buildPreCommitBaselineLiveDiffOverlapPasses`.
- Stage 6 already skips zero-length display upsert; RC2 must not re-insert 14 as Visible.
- RC1 is withdrawn; `test_builder_omits_hide_when_live_already_absent` stays.

### Resolved

| Topic | Decision |
|-------|----------|
| Plan | Sibling bugfix, not a Stage 8/9 reopen |
| RC1 67.824 | Withdrawn — min-length stay-hidden (8 &lt; 12) |
| Inclusive overlap classify | Keep `linearSpansOverlapForAnalysis` |

### Open before coding RC2

1. Skip predicate pinned: skip Length/NoteRange when live end == `loopLength` and `committedDisplayNotes` has no painted non-zero span. Null list does not skip.

### Proceed?

RC3 native shipped. Device gate open.
