# Overdub pending paint frame drop bugfix

**Status:** investigation plan; implementation not started  
**Date:** 2026-09-07  
**Kind:** bugfix  
**Evidence:** [`session_20260907_204112.log`](../../captures/session_20260907_204112.log)  
**Parent:** [`overdub_overlay_identity_bugfix.md`](overdub_overlay_identity_bugfix.md)

## Scope and boundary

The draw-source compose already contains the closed current-preview row,
but the returned painted frame removes it after the first post-NoteOff
frame. This slice investigates and fixes only the pending paint and
playhead-tail stages:

```text
sealed/current overlay + recover
    → compose (`liveDisplayNotes`)                         ← trusted
    → pending Hide/Shorten + Stage 3 sibling drop           ← current slice
    → playhead tails                                        ← current slice if needed
    → painted frame
```

Do not reopen overlay admission, RC-A recover placement, Stage 1/2
pairing, playback ledger construction, or unmatched NoteOff admission.

## Evidence

In [`204112`](../../captures/session_20260907_204112.log), all 17
overdub preview misses have `compose_miss=0`. The representative rows
are:

```text
preview_miss id=107 p=69 s=48 e=96 compose,1,107,96 frame,0,0,0 sealed,98,96
preview_miss id=116 p=69 s=48 e=96 compose,1,116,96 frame,0,0,0 sealed,107,96
preview_miss id=126 p=69 s=48 e=96 compose,1,126,96 frame,0,0,0 sealed,116,96
```

The first wrap (`sealed=9`) remains clean. On later wraps, the open hold
at `hs=48` is present; the first sampled frame after NoteOff@96 has no
`69@48` row at all. `hidden=0` describes the visual-cache differential,
not preservation of the current preview. Occupy at the incoming On
selects the ledger span `69@48–96`.

## Architecture checkpoint

| Question | Answer |
|---|---|
| Owner module | `Loop::applyPendingHideAndShortenToNotes`, `dropLongerSamePitchStartSiblingsMarkedBySourceTransform`, and the existing display tail owner as evidence requires |
| Primary invariant | A closed current-preview row present in `liveDisplayNotes` remains in the painted frame unless its own `NoteId` is explicitly transformed |
| Ownership change? | NO |
| State-transition change? | NO |
| Unbounded MIDI-interval owner? | NO; this is bounded display-vector composition |
| Reuse decision | YES — extend the existing pending/tail owner; no new Manager or provenance field |

## Investigation and implementation order

1. Add a native regression that builds the `204112` shape:
   - a sealed prior-pass `69@48–96` row;
   - a current closed preview `NoteId` with `69@48–96`;
   - an occupy Hide/Shorten targeting the selected prior-pass identity;
   - compose before pending;
   - apply pending transforms and existing tails;
   - assert the current closed preview remains with end `96`.
2. Assert the identity matrix:
   - target identity is removed or shortened according to its transform;
   - distinct current-preview identity is not removed by target `NoteId`;
   - equal-end valid siblings remain distinct;
   - open holds retain their existing tail behavior.
3. Trace the vector after each existing stage in the native fixture:
   - after `applyDrawSourceOverlaysForTest`;
   - after pending Hide/Shorten and Stage 3;
   - after `applyOverdubCapturePlayheadTailsForTest`.
4. Implement the smallest owner-local fix. Preserve:
   - explicit target-`NoteId` transforms;
   - `123236` / `000402` remnant-drop behavior;
   - Stage 3 closed-preview exemption;
   - currentTick and wrap-tail rules.
5. Extend capture diagnostics only if the native fixture proves the
   remaining stage is not already observable. Keep any new runtime
   logging behind `SESSION_CAPTURE_DRAW_SRC`.

## Native acceptance

- `69@48–96` current closed preview survives pending paint.
- The prior selected identity follows its explicit Hide/Shorten.
- No same-start valid identity is removed solely because another valid
  identity is transformed.
- Existing remnant-drop, open-hold, wrap-tail, and equal-geometry tests
  remain unchanged and pass.

## Verification gates

1. Focused pending-note and display-window native suites.
2. `pio run -e teensy41-capture-serial`; RAM1 locals must remain at least
   4000.
3. Full `pio test -e native`.
4. Flash only after user confirmation, then recapture the same
   multi-overdub sequence.

HITL acceptance:

- `compose_miss=0`;
- `missed_preview=0` for the repeated `69@48–96` rows;
- the current preview `NoteId` and any retained prior-pass sibling have
  the expected frame membership and endpoint;
- no new currentTick tail replacement or `norowid` regression.

## Out of scope

- `upsertDisplayNoteAtPitchStartFrom` overlay admission
- RC-A recovered Shorten prefix placement
- Stage 1/2 per-pass pairing and merge
- playback ledger ownership
- unmatched capture NoteOff admission
- `DisplayNote` provenance fields
