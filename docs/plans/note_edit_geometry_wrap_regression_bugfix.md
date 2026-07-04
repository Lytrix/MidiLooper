# Note edit geometry wrap regression — bugfix handoff

**Kind:** bugfix  
**Capture:** `captures/session_20260703_132750.log`  
**OpenSpec:** `openspec/changes/note-edit-tick-coordinates-and-audition/`

## Symptom

During NOTE_EDIT pitch change on a long loop (>16 bars), when playback wraps past `endTick`, edited note geometry regresses to pre-edit values. Serial shows repeated `Resynced selection after pitch (merged note idx not found)` and spurious F4 inbound after feedback window expiry.

## Root cause

Three tick spaces were conflated on edit/motor paths:

| Space | Role |
|-------|------|
| **Storage tick** | Session store, `NoteId`, edit mutation |
| **Loop-relative** | F1 nav, F2/F3 outbound (`noteRelativeTick`) |
| **Window-relative** | Piano-roll draw only (`filterDisplayNotesToWindow`) |

`selectableDisplayNotesForEditUi` applied window remap to motor/resync inventory. Pitch resync searched window-relative ticks against storage ticks → match failed → `selectClosestNote` fallback. Stale F4 motor echo then applied pitch regression after wrap.

Playback used `mergeActiveCapturePasses` only — live session edits not auditioned until commit.

## Fix (shipped in this change)

### Tier 1 — coordinates + fader feedback

1. `filterDisplayNotesByWindowInclusion` — window filter for edit inventory without tick remap.
2. `handleNoteValueFaderInput` — `syncSelectedNoteIdxToFilteredInventory` after pitch; removed geometry search + `selectClosestNote` fallback.
3. F3 position mode — `noteRelativeTick` (Phase 10.1).
4. F4 motor — `liveEditDisplayNoteAtSelect` when `focus.active`.
5. `scheduleSelectDependentMotorSync` — skip F2–F4 during active geometry edit kinds.

### Tier 2 — edit-aware playback audition

1. `EditManager::sessionPreviewRevision_` bumped from `Track::invalidateCaches` when note edit active.
2. `ensurePlaybackWindowBuilt` — session store preview when note edit active; revision key includes preview revision.
3. **Non-edit playback** — `mergeMaterializedPassesWithCapture` (takes + active `editPasses` + live capture), not capture-only merge.
4. `EditSelectNoteState::createDefaultNote` — `track.invalidateCaches()` after session store write so add is audible before commit returns.

### Tier 3 — unified geometry-dependent fader flow (F2–F4) — **shipped (HITL pending)**

Follow-up wrap regression (`captures/session_20260703_140021.log`): F4 pitch reverts at loop BAR wrap because stale `lastSentCC` is not refreshed on pitch edit and `FEEDBACK_IGNORE_PERIOD` bypass allows motor echo.

**OpenSpec:** Tier 3 tasks in [`openspec/changes/note-edit-tick-coordinates-and-audition/tasks.md`](../../openspec/changes/note-edit-tick-coordinates-and-audition/tasks.md); delta spec [`specs/note-edit-fader-feedback/spec.md`](../../openspec/changes/note-edit-tick-coordinates-and-audition/specs/note-edit-fader-feedback/spec.md).

**Implementation plan:** `.cursor/plans/f4_pitch_wrap_simplification_368efdef.plan.md`

1. `NoteEditDependentFaderSnapshot` + unified `sendDependentFaderSnapshot` / `shouldIgnoreDependentFaderInput` / `publishDependentFaderLatch`
2. Phase 12 dead-code cleanup (deferred from `note-edit-fader-feedback-regression` archive)
3. F1 (ch16 select) stays separate — not in geometry snapshot

### Tier 4 — pitch overlap restore on wrapped notes — **shipped (HITL pending)**

**Capture:** `captures/session_20260703_224146.log`

**Symptom:** Pitching a wrapped moving note (1512–1588, loop 1536) across an overlapping wrapped note on another lane (pitch 12 at 1482–1577) extends the neighbor toward the moving note instead of hide/shorten.

**Root cause:** Overlap hide/restore stored display wrap end (`41`) in `OverlapNote.baseline`. Pitch-lane restore recreated `NoteOff@41`; later passes logged `noteOff@1615` and `LIFO pair length mismatch`.

**Fix:** `linearBaselineForOverlapRestore`; baselineMap-preferred hide; skip display wrap segments in pitch adjacent merge/overlap passes.

### Tier 5 — F1 reselect stretch + wrapped overlap shorten/restore — **shipped (HITL pending)**

**Capture:** `captures/session_20260703_230756.log`

**Symptoms:**

1. F1 reselect commits spurious `ChangeLength` without user length edit — e.g. `start=1482 baselineEnd=1535 newEnd=1615` at 17.784 s when only changing select slot.
2. Wrapped overlap shorten/restore uses display wrap end (`95`) instead of linear storage end — e.g. pitch 49 `start=49` logs `restoring to 95` and `Failed to find note-off for shortened note`.
3. Downstream: pitch 12 pair corrupted to `noteOff@1615` → `LIFO pair length mismatch` on delete.

**Root cause:** `rebuildNoteEditFocusFromStore` built `baselineMap` by overwriting per display segment (wrap tail won). `rebuildNoteEditFocusAtSelect` set `focus.last` from display live note, mixing tick spaces on F1 reselect. Overlap shorten/restore paths used display `DisplayNote` end instead of linear session span.

**Fix:**

1. `baselineMap` — one entry per `noteId` via `findLinearNoteSpanForNoteId`, not last display segment.
2. `rebuildNoteEditFocusAtSelect` — `focus.last` and `movingNoteRange` from linear session span (matches `rebuildNoteEditFocusForDisplayNote`).
3. `resolveLinearNoteSpanForOverlap` + `linearBaselineForOverlapRestore` on shorten/delete/restore paths in `NoteMovementUtils.cpp`.

### Tier 6 — duplicate pitch lane selection/resync — **shipped (HITL pending)**

**Capture:** `captures/session_20260703_231310.log`

**Symptom:** Pitching a wrapped moving note (1484–1579) onto MIDI pitch **32** collides with an existing pitch 32 @ tick 24. Selection jumps to wrong inventory row (e.g. idx 1 or idx 29 showing `32,24,...` instead of `32,1484,...`). Session store / `focus.last` stay correct at 1484–1579.

**Root cause:** `reconstructNotes` paired note-offs with stack top (pitch-only LIFO), so when two same-pitch notes coexist (e.g. pitch 32 @ 24 and mover @ 1487), offs paired with the wrong on. Display inventory corrupted → selection resync jumped to wrong row (idx 1 / pitch 13). Selection-index ranking alone was insufficient.

**Fix:** `reconstructNotes` — pair off with stack top only when `start < off`; else latest open on with `start < off`; stack-top fallback for wrap-head. Allow wrap split for beyond-loop offs when `tailStart > 0`. Pitch resync: no geometry fallback when `focus.active`; keep current index if moving note not in filtered list.

## Verification

- Native: `pio test -e native -f test_note_edit_focus` + `test_note_edit_fader_feedback` + `test_edit_apply`
- Firmware: `pio run -e teensy41-capture-serial` (RAM1 must link; `PERF_TELEMETRY=1` on) — see [capture_serial_ram1_recovery_extmem_debug_enhancement.md](capture_serial_ram1_recovery_extmem_debug_enhancement.md)
- HITL: pitch wrapped moving note across overlapping wrapped neighbor; neighbor must hide/restore cleanly, no extension toward mover start.
- HITL: move + pitch with F1 reselect — no spurious ChangeLength; wrapped overlap shorten/restore uses linear end (not display wrap tail).
- HITL: pitch moving note onto duplicate pitch lane (e.g. 32 @ 24 + mover @ 1484) — selection stays on mover; DNTE shows linear start.

## Files

- `src/Utils/DisplayWindowUtils.{h,cpp}`
- `src/NoteEditManager.cpp`
- `src/EditManager.{h,cpp}`
- `src/Track.{h,cpp}`
- `src/Utils/NoteMovementUtils.cpp`
- `src/NoteEditFocus.cpp`
- `include/Utils/NoteEditMem.h`
- `src/Utils/DebugSessionCapture.cpp`
- `test/test_note_edit_focus/test_note_edit_focus.cpp`
