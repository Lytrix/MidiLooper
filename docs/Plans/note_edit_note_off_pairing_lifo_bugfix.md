# NOTE_EDIT note-off pairing LIFO — bugfix

**Status:** native shipped; multi-overlap device PASS in [`213920`](../../captures/session_20260813_213920.log)
**Owner:** `findNoteOffForOnIndex` (`src/EditManager/EditApply.cpp`)
**Sibling:** [`note_edit_replay_row_payload_bugfix.md`](note_edit_replay_row_payload_bugfix.md) (device PASS in [`211832`](../../captures/session_20260813_211832.log)), [`note_edit_length_replay_loop_boundary_bugfix.md`](note_edit_length_replay_loop_boundary_bugfix.md)

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | **NO** — pairing stays in `findNoteOffForOnIndex`; apply helpers stay in `applyNoteEditPass` |
| State transition change? | **NO** — no session/capture transition touched |

Minimal patch. Reuses the LIFO rule already owned by `findLinearOffForNoteOnLifo`, `findCorrespondingNoteOff`, and `pairedNoteOnTickForOffAtIndex`.

## Symptom

`findNoteOffForOnIndex` broke its forward scan on the first later same-pitch note-on. For a nested same-pitch pair — `On(A)`, `On(B)`, `Off(B)`, `Off(A)` — the outer note A returned `-1` even though its note-off was in the vector.

Every caller then misbehaved silently:

| Caller | Outer-note `-1` effect |
|---|---|
| `applyDeleteNoteById` | Erases only the note-on; orphaned note-off remains |
| `applyMoveNoteById` | Moves the note-on; end stays at the old tick |
| `applyChangePitchById` | Retunes the note-on only; pair splits across two pitches |
| `shortenNoteEndById` | No-op; shorten is lost |
| `applyChangeLengthById` | Reads `refEnd = refStart` (zero-length); a shorten routes into the lengthen branch and the seal pushes a second note-off |

The live session path already pairs correctly via `findLinearOffForNoteOnLifo`. Replay / `materializeToEventVector` used the broken finder, so a nested pair that survived live edit could fail on rematerialize.

## Root cause

The finder treated the first later same-pitch note-on as the end of the search window — a leftover of sequential (non-nested) pairing. Same-pitch pairing is LIFO everywhere else:

- `findLinearOffForNoteOnLifo` (`NoteEditFocusLinearSpan.cpp`)
- `findCorrespondingNoteOff` (`NoteEditPairResolve.cpp`)
- `pairedNoteOnTickForOffAtIndex` (`NoteUtils.cpp`)

`applyChangeLengthById` already calls `NoteUtils::orderSamePitchNoteOffsForLifo` after its edits, so `EditApply` depended on the LIFO invariant while its own finder contradicted it.

[`realtime_incremental_work_overdub_note_off_pitch_query_refinement.md`](realtime_incremental_work_overdub_note_off_pitch_query_refinement.md) already asserted `findNoteOffForOnIndex` is LIFO. That claim was false; this change makes it true.

The loop-boundary guard `if (offIndex < 0 && newEnd == loopLength) return;` stays. With LIFO pairing, `-1` means only "genuinely unpaired", which is what that guard's comment already claimed.

## Fix

Walk the vector from the start, push same-channel+pitch note-ons, pop LIFO on each matching note-off, and return the off that closes the requested on-index. The stack must start at the beginning of the vector: an earlier unclosed same-pitch note-on changes which off closes ours.

Replay is a cold path (`materializeToEventVector` is barred from hot paths).

## Tests

`test/test_edit_apply` — `test_nested_same_pitch_outer_apply_helpers_use_lifo_off`:

Fixture `On(A)@100 / On(B)@150 / Off(B)@180 / Off(A)@300` on pitch 60. Outer note A:

- delete removes both events (no orphaned note-off at 300)
- move to `200–400` lands both endpoints
- pitch change retunes both events
- `Length → 250` shortens instead of lengthening, and adds no second note-off

Confirmed to fail against the pre-fix source on the first assertion (`Expected 0 Was 1` — orphaned off at 300).

## Device gate

[`213920`](../../captures/session_20260813_213920.log) @43.594: five same-pitch interactions (`Shorten` 1 `0–95`, `Hide` 2/3/4/115, mover 77 `96–788` pitch 60). @45.971 deselect seals `canonical=7 pre_commit=7` — Delete 4/3/2/115, Length 1 `0–95`, NoteRange+Pitch 77. Cache drops 22→18 notes. That is the multi-overlap seal, not the nested LIFO apply-helper path (native-only).
