# Parked — do not apply

**Status:** Deferred (2026-08-05) — OpenSpec task 0.9 from **edit-session-action-geometry** prior art Q9 + Q16.

## Why parked

Capture **NoteMinLength** hot stop and capture-pass boundary materialization are a **separate tier**
from NOTE_EDIT geometry (see [`edit_session_action_geometry_prior_art_refinement.md`](../../docs/plans/edit_session_action_geometry_prior_art_refinement.md) Q8–Q16).

NOTE_EDIT minimum note edit length hide uses the same **`noteMinLengthTicks`** globals but applies
during **edit session action apply** / resolve — not on record/overdub stop.

## To revive

- `/opsx:explore` or `/opsx:propose` when capture hot-stop NoteMinLength implementation is scheduled
- Prerequisite: [`capture_pass_note_min_length_refinement.md`](../../docs/plans/capture_pass_note_min_length_refinement.md)

Artifacts in this folder are reference only until explicitly unparked.
