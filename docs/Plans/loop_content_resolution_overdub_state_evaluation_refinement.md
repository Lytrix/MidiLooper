# LoopContentResolution — overdub state evaluation (no note map)

**Status:** Active — native **6E.1 PASS**; 6E.2–6E.5 not started; no firmware  
**Date:** 2026-08-15  
**Kind:** refinement (investigation)  
**Decision:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype); wrap-commit + session-undo DEC not yet numbered  
**Parent:** [`loop_content_resolution_incremental_commit_maintenance_refinement.md`](loop_content_resolution_incremental_commit_maintenance_refinement.md)  
**Architecture:** [`loop_event_sourced_resolution_architecture.md`](loop_event_sourced_resolution_architecture.md)  
**Does not authorize:** firmware; deleting 3b; path B; `handleMidiInput` resolve; SD on wrap; one **U:** per wrap; reuse of `NoteEditSessionUndoStack`

---

## Chosen pins (2026-08-15)

1. **Overlap query:** consume prepared `resolveState(tick)` (pitch-filtered), not `overdubSourceViewNotes_`.
2. **Wrap persist:** when the playhead returns to the start-overdub tick, commit the current wrap as an `OverdubPass` + DEC-031 companions. Stay OVERDUBBING. Stop commits the partial wrap. Empty wrap creates no pass.
3. **Undo:** session-gated wrap undo while OVERDUBBING (same **routing** as NOTE_EDIT **E:**). One **U:** `OverdubPassAdded` on stop for all wraps of that session. Persist grain ≠ undo grain.
4. **Crash mid-session:** out of the first slice. Wrap commit is RAM + LCR only. Reboot during OVERDUBBING is the same loss as today’s uncommitted overdub. No session-id on `OverdubPass` yet.

---

## Answers

**Can overdub evaluate against LCR without a premapped note map?** Yes, if the incoming note is an argument to prepared `resolveState`. No, if live capture is inserted into LCR on each MIDI note so LCR invents note-offs (path B).

**Are same-session prior-wrap notes overlap sources today?** No. Source view is frozen at session start. Wrap-1 ids can appear in playback observation; `appendNotesForIds` looks them up in `overdubSourceViewNotes_`, which does not contain them, so they produce no Shorten.

**Example**

```text
committed
  Note 31 ON  @ 100
  Note 31 OFF @ 200
  Note 32 OFF @ 150          (ON earlier in history)

overdub incoming
  Note 31 @ 125  → Shorten source 31 to end at 125
  Note 32 @ 125  → Shorten source 32 so OFF becomes 125
```

Geometry is already DEC-031/032 + `resolveConstrainedGeometry`. The waste is candidate find via a reconstructed `DisplayNote` list. 6C device: [`205928`](../../captures/session_20260815_205928.log) **37 ms**; [`210508`](../../captures/session_20260815_210508.log) **312 ms**. 3b copy is **120 µs–2.2 ms**.

---

## Path A (accepted) vs path B (rejected)

**A — incoming note is an argument to a prepared query**

```text
prepared LCR (history + 6D.4 delta)
  → consume resolveState(125)
  → pitch-filter → source still sounding
  → resolveConstrainedGeometry → Pending Shorten/Hide
  → wrap re-entry or stop: this wrap's OverdubPass + companions
```

Live capture stays in `capture.store` for the **current wrap only**. Notes that start during the hold stay on `collectOverlapHoldPlaybackNoteOn`.

**B — live capture joins LCR and LCR invents note-offs**

Forbidden: resolve from `handleMidiInput` (OpenSpec 6.5 / DEC-037); query-time re-resolve (Stage 2); LCR as replacement for `overdubSourceView` (6.0); per-note index mutation (6D.1 FAIL).

---

## Wrap commit at start-tick re-entry

```text
start overdub at tick S
  wrap 1 live capture
  playhead returns to S
    seal wrap 1 → OverdubPass + companions + 6D.4 publish
    push overdub session-stack entry (not GUS)
    beginCapture(Overdub) again
    consume prepared resolveState
  wrap 2 overlaps wrap 1 like any later overdub
  stop
    seal partial wrap N
    one U: OverdubPassAdded (passIds = wrap 1..N, all companions)
    clear session stack
```

Still an `OverdubPass`. No new domain noun.

**6.0 at wrap:** bounded 6D.4 publish only (delta sort + restamp + `O(Δ)` pair/span if 6E.4 PASS). No 6C reconstruct, no full `resolveWindow`, no `VCACHE,full`, no SD.

**Open note at S:** must not seal an incomplete Add into wrap N. Owner named by native 6E.5 from the existing capture close pipeline.

**Empty wrap:** no pass, no session-stack push, no revision bump, no LCR publish.

**Formal triggers:** commit while still OVERDUBBING; wrap publishes a pass; one-session-one-pass withdrawn. Firmware waits for DEC + 6E PASS.

---

## Undo — chosen: session-gated wraps, one U: on stop

Converge overdub and NOTE_EDIT as the same kind of **content session**. Overdub Adds via MIDI in; NOTE_EDIT Adds/changes via MIDI/GPIO. Shared: Add/Shorten/Hide (DEC-032), session-gated undo while open, one **U:** on close.

Do **not** put wraps into `NoteEditSessionUndoStack` / `SessionUndoEntry`. Reuse **routing**, not the NOTE_EDIT payload or the **E:** name. `handleUndo` already session-gates on `isNoteEditActive()` with no **U:** fallthrough ([`MidiButtonActions::handleUndo`](../../src/MidiButtonActions.cpp); [LOOP_MIDI](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) § Routing). While OVERDUBBING, the same gate applies to an **overdub session stack** owned by the overdub lifecycle (`Track` trigger / `Loop` pass list), not `EditManager`.

The session stack **replaces** today’s “discard live capture without popping GUS” (`loopHasLiveOverdubCapture` in [`TrackUndo.cpp`](../../src/TrackUndo.cpp)): current incomplete wrap first, then sealed wraps of this session. No double-discard.

```text
OVERDUBBING
  undo → overdub session stack only (live wrap, then wrap N-1, …)
         no U: fallthrough
stop
  one OverdubPassAdded on GUS
  passIds = wrap 1..N
  editPassIds = all companions
after stop
  undo → that one U: disables every wrap pass + companions
```

NOTE_EDIT cannot be open during overdub (`openNoteEditSession` stops overdub first). The two session gates do not nest.

### Why this undo grain

- After stop, “undo that overdub” stays one gesture.
- Same session shape as NOTE_EDIT.
- GUS depth and sidebar **U:** do not grow with wrap count.
- Per-wrap passes still exist for LCR/overlap.

### Costs that stay in the DEC

- `UndoEntry` has one `passId` today. One **U:** for N wraps needs a `passIds` list on `OverdubPassAdded` (GUS wire bump). Extend that kind; do not add a new one (DEC-031).
- Mid-session wrap undo sets that pass `Disabled`. `findRawWindow` already skips non-Active. Stamp/delta policy pinned in 6E.4.
- Session gate blocks undo of an older overdub/record until this session stops (NOTE_EDIT rule; change vs today’s discard-then-**U:**).
- New wrap commit after session undo drops the session redo tail (same as NOTE_EDIT `pushEntry`).

### Rejected undo shapes

- **N U: entries (one per wrap)** — N gestures after stop; GUS grows with wraps.
- **Reuse `NoteEditSessionUndoStack`** — wrong payload and owner.
- **Path B LCR rewind** — already rejected.

---

## What is missing today

- `keepPreparedIndex` → `dropWorkingBuffers()` clears `checkpoints`. After idle complete, only `TickIndex` + 6D.4 `delta` remain. `resolveState` from span boundaries is not consumable during PLAYING/OVERDUB.
- `resolveState(LoopPasses, …)` rematerializes the whole loop. Do not call it on the MIDI path.
- `SoundingNote` has `onTick` but no `endTick`. Geometry needs `appendNoteEvents(noteId)` or a kept span.
- 6D.4 publish does not pair the new pass into `byNoteId` / `spanBoundaries`.

6.0 consume reading:

> Overdub start/note-on may consume already-prepared `resolveState` / `appendNoteEvents`. It must not construct, sort, checkpoint, or resolve a window in order to open a note map.

---

## Next

**6E.1 PASS** (`test_stage6e1_resolve_state_candidates_match_note_map_oracle`). Candidates from prepared `resolveState(S)` plus hold-window NOTE_ONs match the reconstruct note-map oracle for:

- user example (31 still sounding @125; 32 OFF @150)
- pending shorten long source
- pending hide when covered (notes that **start** in the hold — window ONs, not `resolveState` alone)
- pending add-only other pitch
- pending wrap-crossing tail shorten
- pending wrap-crossing skip head Hide

Geometry stays in `test_pending_note_change` / `test_resolve_constrained_geometry`. No firmware.

**Next when asked:** native **6E.2** (scaling) or **6E.3** (keep spans after drop-rebuild-buffers). Do not start wrap-commit DEC or midi_gap / 6.3.

---

## Native 6E (before firmware)

Extend [`test/test_loop_content_resolution/test_loop_content_resolution.cpp`](../../test/test_loop_content_resolution/test_loop_content_resolution.cpp) only.

| Slice | Prove |
|-------|--------|
| **6E.1** | **PASS** — `resolveState(S)` + hold-window NOTE_ONs match note-map oracle on the user example and the pending overlap cases. No `overdubSourceViewNotes_`. |
| **6E.2** | Consume tracks checkpoint-interval replay, not `history_events`. Full reconstruct / 16-bar `resolveWindow` = FAIL |
| **6E.3** | Keep `spans` + `spanBoundaries` after drop-rebuild-buffers. Keep-all-`soundingAt` = FAIL |
| **6E.4** | Wrap-1 publish is wrap-2 source; session-disable wrap 1 hides it; GUS stamp miss → 3b |
| **6E.5** | Held note across start-tick S does not seal an incomplete Add |

---

## Production architecture gate (after 6E PASS + DEC)

- Keep `spans` + `spanBoundaries` at `deviceGateComplete` (not per-bar `soundingAt`).
- `tryResolvePreparedState(tick)` — miss → 3b / 6C unchanged.
- `establishOverdubSourceView` is a wrap-boundary flag; no note-list reconstruct on hit.
- Start-tick re-entry: existing commit-site publish, then `beginCapture(Overdub)`, stay OVERDUBBING.
- `handleUndo` while OVERDUBBING is session-gated. Stop pushes one `OverdubPassAdded` with all wrap `passIds`.
- Keep 3b on miss.

---

## Out of scope

- midi_gap / 6.3 / 6.4
- Making all of LCR incrementally live
- Path B (live capture as a third find source)
- Deleting `materializeToEventVector` or the 3b copy
- Flatten `openOnByPitch`
- `resolveWindow` + `reconstructDisplayNotes` from `handleMidiInput`
- SD persist on wrap commit
- Commit at loop tick 0 when start-overdub tick is not 0
- Mid-session crash durability / session-id on `OverdubPass`
