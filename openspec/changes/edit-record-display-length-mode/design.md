# Design — live record display, edit refresh, length-mode lifecycle

**Change:** `edit-record-display-length-mode`  
**Status:** Proposal agreed pending spike  
**Bug spec:** [BUG.md](./BUG.md)  
**Related:** `note-move-pitch-overlap-flaky` (overlap store — separate), `m8-edit` (NoteEditSession)

---

## Context

Three failure modes reported during edit-baseline HITL:

| ID | Symptom | Primary files |
|----|---------|---------------|
| **D1** | Notes invisible on piano roll during RECORD until stop | `DisplayManager::resolveDisplayNotes`, `Loop::capturePreview`, `Track` capture insert |
| **D2** | LEN / piano roll stale after edit mutations | `Track::invalidateCaches`, `DisplayManager::drawNoteInfo`, `SC_DNTE`, fader settle timers |
| **D3** | P0 stretched to loop end after fader 1 reselect post-overlap | `NoteEditManager::lengthEditingMode`, `sendCoarseFaderPosition`, coarse fader handler |

Overlap **store** corruption (P0 LIFO delete) was fixed in `NoteMovementUtils`; user still sees **display / length-mode** issues on hardware.

---

## Goals

- **D1:** Live capture notes visible during RECORD for the recording-focus slot before STOP.
- **D2:** After any edit mutation that calls `invalidateCaches()`, sidebar LEN and piano roll match store on next frame (≤33 ms @ 30 Hz display) without requiring fader 1 reselect.
- **D3:** When `lengthEditingMode == false`, no code path may apply `LENGTH EDIT` to a note other than an explicit user coarse/fine move in length mode; fader 1 select + scheduled fader 2 feedback must not mutate note ends.

## Non-Goals

- Change HITL to play transport during edit (document only unless product decides otherwise).
- Full `MidiFaderProcessor` rewrite.

---

## Hypothesis D1 — live record display

**Current path:** `isLiveRecordingDisplay` → `loop.buildLiveEventView` → `capturePreview.notes` + open-note tails (`DisplayManager.cpp` ~279–346).

**Likely gaps (spike to confirm):**

1. `captureDisplayRevision` not bumped on every capture insert → live cache stale until stop rebuilds visual cache.
2. `resolveDisplayLoopLength` returns growing length but `liveDisplayNotes` empty because `capturePreview.notes` empty until `finalizeLoopAtStop` / take publish.
3. RECORD with **transport stopped** but clock advancing: special case at `resolvePlayheadInLoop` (~217) — playhead OK but notes not merged.

**Spike:** Enable `#CAP DISP` during RECORD; compare `frameNotes.size()` vs `REVT` count mid-record.

**Fix direction:**

- Bump `captureDisplayRevision` (and/or `capturePreview.revision`) on each capture preview update in `Track` / `Loop` hot path.
- Ensure `resolveDisplayNotes` rebuild path runs when revision changes even if `liveEventCount` unchanged.
- Add native or host test: inject capture events → preview notes non-empty before stop.

---

## Hypothesis D2 — edit display refresh lag

**Current path:** NOTE_EDIT uses `track.getCachedNotesForSlot` (`DisplayManager.cpp` ~241). Mutations call `track.invalidateCaches()` but **DNTE** may only emit on select/bracket update (`DisplayManager::drawNoteInfo` ~988).

**Likely gaps:**

1. HITL waits (`COARSE_EDIT_READY_MS` 1200 ms, `DISPLAY_SETTLE_MS` 800 ms) mask firmware latency — user sees lag on device between waits.
2. After move/length, `finalReconstructAndSelect` updates selection index but **does not** force immediate DNTE / sidebar refresh until next `scheduleOtherFaderUpdates`.
3. `getCachedNotes()` stale for one frame if invalidation does not rebuild before `DisplayManager::update()`.

**Fix direction:**

- After edit mutations in `NoteEditManager` / `NoteMovementUtils::finalReconstructAndSelect`, call a lightweight **`requestNoteInfoRefresh(Track&)`** that rebuilds cached notes if dirty and emits `SC_DNTE` once (guard re-entrancy).
- Optionally reduce fader feedback ignore windows only for display capture builds (TBD — do not break DROID feedback prevention).

---

## Hypothesis D3 — length-mode leak (P0 → loop end)

**Observed user flow:** Length mode ON → extend M0 → toggle OFF (script waits for log) → overlap moves → pitch → **fader 1** reselect on M0 → P0 stretches.

**Code paths:**

```text
toggleLengthEditingMode(false)
  → lengthEditingMode = false
  → commitAllPendingNoteEditActions(track)
  → scheduleOtherFaderUpdates(FADER_SELECT)
    → enableStartEditing → sendFaderUpdate(FADER_COARSE)
      → sendCoarseFaderPosition  // uses lengthEditingMode flag

handleCoarseFaderMovement (on user OR feedback pitchbend)
  → if lengthEditingMode: LENGTH EDIT on selected note END
  → else: POSITION EDIT on START
```

**Likely bugs:**

1. **`lengthEditingMode` still true** when `sendCoarseFaderPosition` runs (toggle debounce dropped OFF press; or double-toggle race with HITL note 35).
2. **Feedback pitchbend** after select maps to END of **wrong** reconstructed note (selectedIdx vs movingNote identity) — applies LENGTH EDIT to P0 when coarse fader position reflects M0 end step 14.
3. **`commitAllPendingNoteEditActions`** extends edit span but does not refresh `movingNote` vs selected note — fader 2 targets END tick that **coincides** with P0 off tick (680) from prior length extend.

**Fix direction (preferred — minimal):**

1. On length mode OFF: set `lengthEditingMode = false` **before** any fader send; assert in debug build if `sendCoarseFaderPosition` sees true after OFF.
2. On fader 1 select: if `!lengthEditingMode`, **never** call LENGTH EDIT in coarse handler; ignore coarse movement until `startEditingEnabled` and mode confirmed POSITION.
3. When scheduling fader 2 after select, pass explicit **position-edit** routing (do not rely on global flag alone) — e.g. `sendCoarseFaderPositionForStart(track)`.
4. After select change, if selected note ≠ `movingNote` identity, do not apply pending coarse delta from previous note's END pitchbend.

**Regression:** HITL gate after fader 1 reselect post-overlap: `require_p0_record_gate` + serial must not contain `LENGTH EDIT` with P0 start tick.

---

## HITL / transport policy

Edit baseline **stops transport** before NOTE_EDIT (`--start-transport` only ensures clock for **record**). Document in script header and BUG.md:

- **Record phase:** transport started for MIDI clock + fixture note-ons.
- **Edit phase:** STOPPED by design for deterministic state — OLED playhead frozen; not a D1 defect.

Optional flag `--play-during-edit` (TBD in tasks) for manual visual testing only.

---

## Verification matrix

| Check | Method |
|-------|--------|
| D1 mid-record notes | Manual + `#CAP DISP` frameNotes > 0 during RECORD |
| D2 post-move LEN | `#CAP DNTE` length matches reconstruction within 1 frame |
| D3 P0 after reselect | HITL `require_p0_record_gate` after fader 1 on M0 post-overlap |
| No regression overlap | Existing P0 contained-delete + restore gates (033940 run) |
| Native | `pio test -e native` unchanged or new display-less unit tests for revision bump |

---

## Sequencing

1. **Spike D1** with serial DISP/REVT (1 session) — confirm root cause before code.
2. **Fix D3** first (data corruption visible to user) — length-mode + fader feedback.
3. **Fix D2** — DNTE refresh hook.
4. **Fix D1** — capture preview revision.
5. HITL gate additions + docs.

Do **not** merge with `note-move-pitch-overlap-flaky` PR — separate review stack.
