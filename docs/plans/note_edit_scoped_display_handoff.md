# Note Edit Scoped Display Handoff

## Purpose

Execute the scoped NOTE_EDIT display projection plan from:

- `/Users/eelkejager/.cursor/plans/note_edit_scoped_display_8b87deb3.plan.md`

Do not start by redesigning. The accepted direction is to stop reconstructing the full mutable
`NoteEditSession.store` for active NOTE_EDIT display. Stable, unrelated notes should come from the
committed display source or committed display window, and only participant `NoteId` geometry should
be overlaid from the active session store.

## Evidence

Use `captures/session_20260805_153954.log` as the immediate regression source.

The relevant facts from the capture:

- Commit rows target only `noteId=35` and `noteId=31`.
- The post-commit `DISP` snapshot reports `visualCache=40` and `frameNotes=41`.
- The extra rendered note is introduced by active NOTE_EDIT projection/session-store rendering, not
  by a direct edit row for that note.
- No serial evidence showed invariant/orphan repair (`LinearNote`, `check=`, `orphan`,
  `non-canonical`) in that capture.

## Architecture Decisions Already Validated

- `applyEditSessionActions()` remains the only live-store writer for geometry.
- `projectNoteEditDisplayNotes()` owns temporary display composition only. It must not repair or
  establish canonical note geometry.
- `EditManager::projectedNoteEditDisplayNotes` remains the NOTE_EDIT projection/cache owner.
- `DisplayWindowUtils` remains the window owner.
- `NoteId` is stable object identity for projection overlay. Do not use `NoteRef` or display index
  as identity.
- Projection and commit are parallel consumers of apply-owned edit state:
  - Projection must not derive state from pending `editPass` rows.
  - Commit must not depend on display projection.
- "Participant NoteIds" is design vocabulary only. Do not add a new C++ class, manager, or top-level
  module for participants.

## Projection Invariant

For the projected NOTE_EDIT display list:

- Non-participant notes are copied unchanged from the committed display source/window.
- Participant notes are projected from `NoteEditSession.store`.
- Hidden participants remove the committed display note.
- Newly created participants are inserted.
- No unrelated `NoteId` may change pitch, start tick, end tick, or visibility.
- Every participant `NoteId` appears at most once.
- Ordering preserves the committed/windowed base except where participant geometry naturally changes
  ordering through edited start tick or pitch.

## Expected Implementation Shape

Use existing owners and nouns:

- `EditManager::projectedNoteEditDisplayNotes`
- `NoteEditFocus::projectNoteEditDisplayNotes`
- `DisplayWindowUtils`
- `NoteEditSession.store`
- `changedOverlapNoteIds`
- `evaluationScope`
- `EditPass` / apply-owned rows

Recommended implementation path:

1. Add native regression from `session_20260805_153954.log`.
   - Reproduce apply rows for `noteId=35` length and `noteId=31` move.
   - Assert projected display count does not grow from 40 to 41.
   - Assert non-participants keep identical `NoteId`, pitch, start tick, and end tick.
   - Assert no non-participant reaches loop start/end.
2. Add participant discovery as a small helper or local function using existing state.
   - Include `focus.movingNoteId`.
   - Include `focus.changedOverlapNoteIds`.
   - Include current geometry interaction targets from the existing evaluation scope when available.
   - Return a `NoteIdList`; projection consumes only this list.
3. Change active NOTE_EDIT projection to overlay participants onto a committed/windowed base.
   - For short loops, base can come from `loop.visualCache.notes`.
   - For long loops, base should be filtered by `DisplayWindowUtils`.
   - Participant spans come from `NoteEditSession.store` via stable `NoteId` lookup and existing
     linear span helpers.
   - Do not call full `NoteUtils::reconstructDisplayNotes(sessionEvents, ...)` for unrelated notes.
4. Preserve existing consumers.
   - `selectableDisplayNotesAtEditSelect`
   - `drawNoteInfo`
   - NOTE_EDIT display path in `DisplayManager::resolveDisplayNotes`
5. Verify.
   - Focused native suites: `test_edit_apply`, `test_note_edit_focus`, display window tests.
   - Full native: `pio test -e native`.
   - Firmware build: `pio run -e teensy41-capture-serial`.
   - Ask before upload.

## To-Dos From Plan

Use the existing to-dos if the next chat has them. Do not recreate duplicates unless they are absent.

- `session-153954-regression` — Add native regression for post-commit display count and non-participant spans.
- `participant-scope` — Build projection participant set.
- `scoped-projection` — Overlay participant spans onto committed/windowed base notes.
- `window-bounds` — Use `DisplayWindowUtils` windowing for long-loop NOTE_EDIT projection.
- `verify` — Run focused tests, full native suite, and capture-serial build.

Start by marking `session-153954-regression` in progress.

## Pre-Implementation Review

Ready:

- The active OpenSpec `edit-session-action-geometry` already defines `applyEditSessionActions()` as
  the only live-store geometry writer.
- Phase A already names `projectNoteEditDisplayNotes` / `EditManager::projectedNoteEditDisplayNotes`
  as the active NOTE_EDIT display projection owner.
- Stable identity is `NoteId`, not `NoteRef`.

Open before coding:

1. Pin the smallest native fixture that reproduces `session_20260805_153954.log` without needing the
   full hardware capture.
2. Decide whether the participant discovery helper lives in `EditManager` or `NoteEditFocus`.
   Prefer the smaller change that keeps projection ownership in the existing Phase A owner.

Proceed:

- YES, after pinning the regression fixture.
- Stop only if the implementation requires a new top-level module, a new domain noun, or changing
  session/commit state transitions.
