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

### Tier 3 — unified geometry-dependent fader flow (F2–F4) — **shipped (HITL pending)**

Follow-up wrap regression (`captures/session_20260703_140021.log`): F4 pitch reverts at loop BAR wrap because stale `lastSentCC` is not refreshed on pitch edit and `FEEDBACK_IGNORE_PERIOD` bypass allows motor echo.

**OpenSpec:** Tier 3 tasks in [`openspec/changes/note-edit-tick-coordinates-and-audition/tasks.md`](../../openspec/changes/note-edit-tick-coordinates-and-audition/tasks.md); delta spec [`specs/note-edit-fader-feedback/spec.md`](../../openspec/changes/note-edit-tick-coordinates-and-audition/specs/note-edit-fader-feedback/spec.md).

**Implementation plan:** `.cursor/plans/f4_pitch_wrap_simplification_368efdef.plan.md`

1. `NoteEditDependentFaderSnapshot` + unified `sendDependentFaderSnapshot` / `shouldIgnoreDependentFaderInput` / `publishDependentFaderLatch`
2. Phase 12 dead-code cleanup (deferred from `note-edit-fader-feedback-regression` archive)
3. F1 (ch16 select) stays separate — not in geometry snapshot

## Verification

- Native: `pio test -e native -f test_note_edit_fader_feedback`
- HITL: note edit pitch on long loop while playing; let playhead wrap; fader/edit state must not regress.

## Files

- `src/Utils/DisplayWindowUtils.{h,cpp}`
- `src/NoteEditManager.cpp`
- `src/EditManager.{h,cpp}`
- `src/Track.{h,cpp}`
- `test/test_note_edit_fader_feedback/test_note_edit_fader_feedback.cpp`
