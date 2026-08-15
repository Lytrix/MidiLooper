# LoopContentResolution — overdub state evaluation (no note map)

**Status:** Active — **038.1 landed**; issue 1 wrap-tail reconstruct **shipped** (`finishOpenNotes=false` on committed wrap/display). Not 038.2.  
**Date:** 2026-08-15  
**Kind:** refinement (investigation)  
**Decision:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype); [DEC-038](../DECISION_LOG.md#dec-038-overdub-wrap-commit-and-session-undo) wrap commit + session undo  
**Parent:** [`loop_content_resolution_incremental_commit_maintenance_refinement.md`](loop_content_resolution_incremental_commit_maintenance_refinement.md)  
**Architecture:** [`loop_event_sourced_resolution_architecture.md`](loop_event_sourced_resolution_architecture.md)  
**Does not authorize:** Track firmware until 038.1 approval; deleting 3b; path B; `handleMidiInput` resolve; SD on wrap; one **U:** per wrap; reuse of `NoteEditSessionUndoStack`

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

**6.0 at wrap:** bounded 6D.4 publish (delta sort + restamp + `O(Δ)` pair/span — **6E.4 PASS**). No 6C reconstruct, no full `resolveWindow`, no `VCACHE,full`, no SD.

**Open note at S:** must not seal an incomplete Add into wrap N. **6E.5:** owner is `Track::finalizePendingNotes` (STOP only). Wrap must not call it. `LoopStopFinalize::finalizeWrapWindowOnStore` does not close a mid-loop held ON. Add stays NoteOff-owned (`accumulatePendingNoteChangesForIncomingNote`).

**Empty wrap:** no pass, no session-stack push, no revision bump, no LCR publish.

**Formal triggers:** commit while still OVERDUBBING; wrap publishes a pass; one-session-one-pass withdrawn; undo routing. **DEC-038 recorded.** Firmware waits for 038.1 approval.

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
- Mid-session wrap undo sets that pass `Disabled`. `findRawWindow` already skips non-Active. **6E.4:** `setPreparedCapturePassState` hides it from `tryResolvePreparedState` without restamp. GUS stamp+1 without publish → miss (3b).
- Session gate blocks undo of an older overdub/record until this session stops (NOTE_EDIT rule; change vs today’s discard-then-**U:**).
- New wrap commit after session undo drops the session redo tail (same as NOTE_EDIT `pushEntry`).

### Rejected undo shapes

- **N U: entries (one per wrap)** — N gestures after stop; GUS grows with wraps.
- **Reuse `NoteEditSessionUndoStack`** — wrong payload and owner.
- **Path B LCR rewind** — already rejected.

---

## What is missing today

- `keepPreparedIndex` → `dropWorkingBuffers()` now keeps `spans` + `spanBoundaries` + sparse `soundingAt`. Rebuild events/notes are dropped. `tryResolvePreparedState` consumes that keep-set. Not wired to Track.
- `resolveState(LoopPasses, …)` rematerializes the whole loop. Do not call it on the MIDI path.
- `SoundingNote` has `onTick` but no `endTick`. Geometry needs `appendNoteEvents(noteId)` or a kept span.
- 6D.4 publish now pairs the new pass and appends its spans / boundaries (**6E.4**). Not wired to wrap-commit or session undo.

6.0 consume reading:

> Overdub start/note-on may consume already-prepared `resolveState` / `appendNoteEvents`. It must not construct, sort, checkpoint, or resolve a window in order to open a note map.

---

## Next

**6E.1–6E.5 PASS.** Native overdub-evaluation slices are complete.

**Hide vs Shorten (loop length):** source fills the loop. Incoming ON@4000 OFF@200.

- **loop = 4000** (same as note): ON@4000 phases to 0. Consume `[0, 200)` → OverlapNoteOn → **Hide**. Native PASS.
- **loop = 4100** (a bit more): ON@4000 stays 4000. Consume `[4000, 4100)` → OverlapNoteOff → **Shorten 0–3999**. Native PASS.

Same on LCR candidates and the Loop note-map path. Do not change geometry.

**Next when asked:** DEC-038 **038.2** GUS `passIds`. HITL wrap-over-wrap after upload. Not midi_gap / 6.3.

---

## 6E.1b — session start tick as wrap origin (PASS)

`test_stage6e1b_session_start_is_wrap_origin`. S = 777.

```text
sessionPhase(t) = (t - S + loopLength) % loopLength
if sessionPhase(end) < sessionPhase(start):
  consume = [start, S) when start < S, else [start, loopLength)
else:
  consume = [start, end)
```

S = 0 is the 6E.1 clip. Geometry still uses the consume window (production).

1. **Rotated.** Every 6E.1 row whose linear spans stay linear after `t' = (t + S) % loopLength`. Same candidate ids and Shorten/Hide kinds.
2. **Absolute.** Source stays 60@0–5000. Incoming `(4000+S, 200+S)` crosses S. Consume clips to S. Shorten to incomingStart−1.

Loop-filling 4000/4100 rows are not rotated: `3999+S` wraps and reconstruct does not pair that as a tail wrap. They stay S = 0 in 6E.1.

Not firmware. Not 6E.5. Not wrap-commit.

---

## 6E.2 — consume tracks checkpoint replay, not history (PASS)

`test_stage6e2_consume_tracks_checkpoint_replay_not_history`. Canonical 64-bar fixture. Hold 200 ticks at `loopLength - 240`.

```text
resolveState(checkpoints, consumeStart)
  replayStart > 0
  consumeStart - replayStart < TICKS_PER_BAR
  eventsReplayed < eventsInHistory          (spans, not MIDI events)
  passChunkListsWalked = 0

resolveWindow(index, hold 200) visits and window events
  < resolveWindow(index, 16 bars)           FAIL path (6C consume)
  < resolveWindow(LoopPasses, whole loop)   FAIL path (full rematerialize)
eventsReplayed < rematerialize window events
```

32 extra early-bar overdubs grow `eventsInHistory`. Same consume tick: `eventsReplayed` stays within +2 of the baseline. Replay does not track `history_events`.

Not firmware. Not 6C consume-path edits.

---

## 6E.3 — keep spans after drop-rebuild-buffers (PASS)

`test_stage6e3_keep_spans_after_drop_rebuild_buffers`.

`dropWorkingBuffers` no longer assigns `checkpoints = {}`. It keeps `spans`, `spanBoundaries`, and thins `soundingAt` to the device 8-bar stride. `rebuildEvents` / `rebuildNotes` still drop.

`tryResolvePreparedState(tick, revision)` consumes that keep-set. Stamp miss returns false (3b / 6C unchanged). Not wired to Track.

Keep-all per-bar `soundingAt` is the FAIL path: 64 snapshots / more sounding copies than the 8-bar keep-set. After complete, `checkpointCount` is `kCanonicalBars / 8`. `resolveState` still matches the rematerialize oracle.

---

## 6E.4 — wrap-1 publish is wrap-2 source (PASS)

`test_stage6e4_publish_is_next_wrap_source`. 8-bar loop. Record 60@0–48. Wrap-1 72@200–400.

`publishPreparedOverdubPass` pairs the new pass, reconstructs that pass only, appends spans, merges `spanBoundaries`, and patches sparse `soundingAt`. Does not write `tickEvents`.

- After publish, `tryResolvePreparedState(300)` matches the rematerialize oracle and includes wrap-1. Span count grows by 1.
- `setPreparedCapturePassState(Disabled)` hides wrap-1. Stamp stays. Prepared remains true.
- Stamp+1 without publish → `tryResolvePreparedState` false (3b).

Not Track session-undo. Not wrap-commit DEC.

---

## 6E.5 — held note across S does not seal an Add (PASS)

`test_stage6e5_held_note_across_session_start_does_not_seal_add`. S = 777. Completed 72@200–400. Held 60 ON@500, no OFF.

| Path | Result |
|------|--------|
| Reconstruct unpaired ON | Invents open tail `500–loopLength−1` (`finishCanonicalSpansOpenNotes`). Do not publish. |
| Synthetic OFF at S (`finalizePendingNotes` shape) | Seals `500–777`. Incomplete Add. STOP only. |
| `finalizeWrapWindowOnStore` at S | 0 synthetic offs — held ON is not in the tail window. |
| Publish completed pair only | `resolveState(300)` has 72. `resolveState(600)` has no held id. |

Owner: `Track::finalizePendingNotes` then `sealCapture` / `LoopStopFinalize::finalizeWrapWindowOnStore`. Wrap re-entry must not call that pipeline. Add is created on NoteOff only.

---

## Native 6E (before firmware)

Native tests in [`test/test_loop_content_resolution/test_loop_content_resolution.cpp`](../../test/test_loop_content_resolution/test_loop_content_resolution.cpp). 6E.3 also keeps prepared checkpoints in `LoopContentResolution`. Do not wire Track.

| Slice | Prove |
|-------|--------|
| **6E.1** | **PASS** — LCR candidates + existing geometry. 60@0–5000 / 4000–4200 → Shorten. Loop-filling source + incoming 4000–200: loop 4000 → **Hide**; loop 4100 → **Shorten 0–3999**. |
| **6E.1b** | **PASS** — S = 777 is wrap origin. Rotated linear 6E.1 rows match. Absolute 0–5000 + incoming across S Shortens. Loop-filling 4000/4100 not rotated. |
| **6E.2** | **PASS** — checkpoint replay < interval and < history spans. Hold window < 16-bar `resolveWindow` and < full rematerialize. Early-bar history growth does not grow replay. |
| **6E.3** | **PASS** — after `deviceGateComplete`, `tryResolvePreparedState` matches the oracle. `checkpointCount` is the 8-bar stride. Keep-all per-bar `soundingAt` copies fail the size gate. |
| **6E.4** | **PASS** — publish pairs + appends spans. `resolveState(300)` sees wrap-1. Disable hides it without restamp. Stamp+1 → miss. |
| **6E.5** | **PASS** — held ON@500 across S=777 is not published. Reconstruct-to-loop-end and OFF-at-S are the FAIL seals. Owner: `finalizePendingNotes` (STOP). |

---

## 038.1 landed (2026-08-15)

`Track::commitOverdubWrapAtSessionStart` seals completed pairs at S, publishes, `beginCapture`, stays OVERDUBBING. Held ONs stay on live capture. `handleUndo` / `handleRedo` session-gate while OVERDUBBING. No GUS `passIds`. After stop, existing single-`passId` `OverdubPassAdded` covers the last wrap only.

Native: `test_overdub_source_view` wrap/extract/session + `didPlayheadCrossPhase`. Full native 1222. Firmware RAM1 free 6528 (`TrackUndo` + wrap TUs in FLASH).

## Production architecture gate (DEC-038; firmware after 038.1 approval)

- Keep `spans` + `spanBoundaries` at `deviceGateComplete` (not per-bar `soundingAt`).
- `tryResolvePreparedState(tick)` — miss → 3b / 6C unchanged.
- `establishOverdubSourceView` is a wrap-boundary flag; no note-list reconstruct on hit.
- Start-tick re-entry: existing commit-site publish, then `beginCapture(Overdub)`, stay OVERDUBBING.
- `handleUndo` while OVERDUBBING is session-gated. Stop pushes one `OverdubPassAdded` with all wrap `passIds`.
- Keep 3b on miss.
- **S** is the first-session `playheadPhaseTick` from `startOverdubbing`. Session-scoped; not cleared by wrap `beginCapture` / `discardCapture`.

### Architecture reassessment (formal triggers)

| Field | Content |
|-------|---------|
| **Reason triggered** | State transition (seal while OVERDUBBING); undo routing (session-gate, one U: `passIds`); 038.2 GUS wire |
| **Current architecture** | One `OverdubPass` at stop. Undo while OVERDUBBING is `discardCapture` via `loopHasLiveOverdubCapture`. `playheadPhaseTick` is computed then discarded |
| **Proposed evolution** | Extend `Track` / `Loop` / `handleUndo` / `OverdubPassAdded`. No new owner |
| **Migration impact** | 038.1 RAM only. 038.2 GUS STK2 → next token. No loop-file change |
| **Recommendation** | 038.1 first. 038.2 after session undo is proven |
| **Approval required** | YES before Track firmware |

---

## HITL [`231206`](../../captures/session_20260815_231206.log) (2026-08-15)

Overdub start 44.967 s tick 1752 (S). Wraps 52.995 / 61.053 / 69.077 (ticks 4824 / 7896 / 10968). Transport stop 72.6 s. `#CAP` ring overflow at stop — trust INFO timestamps for order.

**E: count-up** is session undo depth (`overdubSessionUndoDepth` = sealed wraps + live store). Expected for 038.1.

| # | Symptom | Evidence | Not in this log |
|---|---------|----------|-----------------|
| 1 | Notes lengthen to loop end at each wrap; correct at stop | Before wrap 1: DISP 83/51. Wrap 1: `wrap_synth=0`, `lcr,6c ev=279 notes=151`, `lcr,6a ev=279 notes=151`, DISP 152/151. Wrap 2: 172. Wrap 3: 194. Stop: `VCACHE,full,ev,267,notes,100`, DISP 100/100 | |
| 3 | MIDI stream stalls at wrap and after | `clockrate` stays 47. Wrap 1 MO gap 129 ms (52908915→53038453) during 6c/6a. Wrap 2 MO gap 515 ms (60660908→61175703). Then **4.218 s** no MO (65028901→69247836) until wrap 3 rebuild | |
| 2 | Undo does not remove notes | | No `MIDI: Overdub session undo` / `Overdub session undone` / post-overdub `MIDI: Undo` after 44.967 s |

Issue 1 path: `commitOverdubWrapAtSessionStart` → `publishPreparedOverdubPass` + `beginCapture` → `establishOverdubSourceView` / `rebuildVisualCacheIdleSlice` both `tryResolvePreparedWindow` + `reconstructDisplayNotes`. `finishCanonicalSpansOpenNotes` sets unpaired ON end to `loopLength`. 4-bar loop ⇒ 6c window is the full loop (`kMaxDetailedWindowBars=16`). Stop uses `rebuildVisualCacheFromPasses` (`gatherCommittedEvents`, 267→100).

Issue 2 firmware gap (if undo was during OVERDUBBING): `Loop::undoOverdubSession` / `TrackUndo::undoOverdubSession` never call `LoopContentResolution::setPreparedCapturePassState`. DEC-038 pin required that. After stop, 038.1 GUS still has only the last wrap.

Issue 3 path: `maybeCommitOverdubWrap` runs inside `playCommittedLoopMidi` before emit. Wrap 2/3 LCR `tot` 82 ms / 106 ms on that tick. `invalidateCaches` forces merge rebuild.

## HITL [`232914`](../../captures/session_20260815_232914.log) — undo during + after stop

Baseline STOPPED DISP **110**. Overdub 24.488 s tick 552, `events=280`. Wraps 32.711 / 40.826 / 48.812. Session undos 41.090 / 43.713 / 50.671 (`MIDI: Overdub session undo`). Stop 52.117. GUS undo 56.082 (`MIDI: Undo (entries=10)`, `kind=1` `OverdubPassAdded`).

| Wall | Event | DISP committed |
|------|--------|----------------|
| before | STOPPED | 110 |
| wrap 1 | `6c ev=204 notes=112` | 112 |
| wrap 2 | `6c ev=237 notes=125` | 125 |
| 41.090 + 43.713 | session undo ×2 | **125** (no drop) |
| wrap 3 | `6c ev=248 notes=137` | 137 |
| 50.671 | session undo | **137** committed; live 145→139 |
| stop | `VCACHE,full,ev,280,notes,103` | **103** |
| 56.082 | GUS `Overdub undone` | **103** (`slice_clean` 103, `DFRAME` 103) |

Session undo **fires** while OVERDUBBING. Sealed-wrap committed count does not fall (LCR still serves the pass — no `setPreparedCapturePassState`). Live undo only clears `capture.store` (145→139). Stop rematerialize uses `LoopPasses` (103). After-stop one `passId` + LCR slice leave 103. Issue 1 still paints wrap reconstruct tails, so OLED cannot show the session-undo disable.

## Issue 1 shipped — committed reconstruct omits open tails

`NoteUtils::reconstructDisplayNotes(..., finishOpenNotes=false)` on:

- `rebuildVisualCacheIdleSlice` / `rebuildVisualCacheFromPasses`
- `establishOverdubSourceView` LCR hit
- `publishPreparedOverdubPass`

Default remains `true` (live overlay / NOTE_EDIT). Unpaired NoteOns no longer become `loopLength` tails on wrap display. Native: `test_reconstruct_display_omits_open_tails_when_finish_open_notes_false`, `test_source_view_prepared_window_omits_unpaired_open_tails`.

Does not fix wrap-on-clock stall (issue 3). After-stop multi-wrap GUS is 038.2.

## Issue 2 shipped — session undo hides the wrap in prepared LCR

`Loop::setCapturePassState` calls `setPreparedCapturePassState` then `restampPreparedPlaybackRevision` after the revision bump. Prepared stays ready; `findRawWindow` / `eraseDisabledSounding` skip the Disabled pass. Native: `test_overdub_session_undo_hides_wrap_from_prepared_lcr`.

After-stop one `OverdubPassAdded` `passId` is still 038.2.

## Issue 2 playback — silence + merge rebuild on overdub undo

HITL [`234535`](../../captures/session_20260815_234535.log): after issue 1+2 flash, wrap tails gone and committed DISP drops on GUS undo (123→107→90→89→71). Sounding notes from the disabled wrap stay on because `TrackUndo::undoOverdubSession` / `OverdubPassAdded` never called `silenceSlotMidiOutput`. `playCommittedLoopMidi` `reset(true)` keeps the slot ledger; OFFs left with the Disabled pass are never sent.

Fix: `refreshPlaybackAfterCapturePassStateChange` — `silenceSlotMidiOutput` then `invalidatePlaybackMergedMidiEvents(false)`. Session undo/redo and GUS `OverdubPassAdded` undo/redo. Does not use `sendAllNotesOff` (CC 123). Native gather after session undo: `test_overdub_session_undo_disables_sealed_wrap`. Device HITL next.

## HITL [`235536`](../../captures/session_20260815_235536.log) — live undo must not slot-silence

Overdub 11.112 tick 544. Wraps 19.096 / 27.094 / 35.094. Eight `Overdub session undo` while OVERDUBBING. Committed DISP stayed **42** after wrap 1 and **43** after wrap 2 — session undo only cleared live (61→42, 46→43). MI,U notes between undos kept `capture.store` non-empty, so `Loop::undoOverdubSession` never reached wrap disable.

`b24a8c4` still ran `silenceSlotMidiOutput` + merge reset on those live undos. That NoteOffs the whole slot ledger (record + earlier wraps). After transport stop 38.676, GUS `Undo (entries=2)` `kind=1` dropped STOPPED DISP 43→24 (older overdub, not the session wraps).

Gate: session silence/merge rebuild only when `playbackRevision` changes (`setCapturePassState`). Live store clear does not bump revision. GUS `OverdubPassAdded` unchanged.

## HITL [`000149`](../../captures/session_20260816_000149.log) — stopped record must not layer slot 0

Clear Loop 6 at 184.200. `restorePlaybackAfterSlotClear` started slot 0 at 184.413 (first enabled bit in boot `enabledMask=0x33`). Record Loop 6 from STOPPED at 248.604. `replaceSingleEnabledSlotWithTarget` returned early because enabledCount > 1, so record-stop PLAYING still layered slot 0.

Pin: queues are playing-only and discarded on stop. Hold-while-playing still builds a multi-slot enabled set. Select / record / play from STOPPED replaces the enabled set with that slot only.

## HITL [`002545`](../../captures/session_20260816_002545.log) — queued switch must stop the previous slot

Boot `enabledMask=0x33`. Play Loop 6, short-press Loop 5 at 15.433 (`Queued playback switch`), LoopEnd commit `5->4` at 26.204. `queuePlayingSlotSwitch` left `pendingEnabledSetReplacement` false because `enabledCount > 1`, so additional enabled slots kept `playMidiEventsForSlot`. Same at 39.994 Loop 3 → commit `5->2` at 45.983.

Pin: short-press queue is a replace. Previous slots stop when the queued slot starts. Hold-to-add at the same queue point is later.

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
