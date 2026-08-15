# Preflight — DEC-038 wrap commit and session undo

**Mode:** Full — formal triggers fired.  
**Date:** 2026-08-15  
**Change:** `loop-content-resolution`  
**Status:** DEC-038 recorded. Firmware not started. Awaiting implement approval for 038.1.

Plan: [`docs/Plans/loop_content_resolution_overdub_state_evaluation_refinement.md`](../../../docs/Plans/loop_content_resolution_overdub_state_evaluation_refinement.md)

---

## Problem

Overdub cannot overlap same-session prior-wrap notes. Source view is frozen at session start. Native 6E PASS: `resolveState` + publish can make wrap-1 the wrap-2 source. Production still seals one `OverdubPass` at stop only. Undo while OVERDUBBING discards live capture (`loopHasLiveOverdubCapture`) and falls through toward GUS after that.

## Domain

Overdub lifecycle / capture commit / undo routing (DEC-037 6E, DEC-031/032).

## Similar historical decisions

### Search locations

- [x] `docs/DECISION_LOG.md` — DEC-037, DEC-036, DEC-032, DEC-031, DEC-024
- [x] `docs/Plans/loop_content_resolution_overdub_state_evaluation_refinement.md`
- [x] `openspec/changes/loop-content-resolution/`
- [x] `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md` § Routing / capture close

### Relevant findings

- DEC-037 6.0: button must not construct LCR. 6D.4 publish at commit site. 6E native PASS.
- DEC-031/032: OverdubPass + EditPass companions; Add on NoteOff.
- DEC-036 3b: keep visual-cache copy on miss.
- `handleUndo` already session-gates NOTE_EDIT with no U: fallthrough.
- `Track::finalizePendingNotes` is the STOP closer (6E.5). Wrap must not call it.
- `UndoEntry` has one `passId`; `editPassIds` is companions (STK2).

### Existing owner

`Track` overdub start/stop / `Loop::commitCapturePass` / `beginCapture`. `TrackUndo::pushOverdubPassAdded`. `MidiButtonActions::handleUndo`. LCR `publishPreparedOverdubPass`.

### Existing extension point

`commitCapturePass` + `finalizeCommitSideEffects` + `publishPreparedOverdubPass` + `beginCapture(Overdub)`. `handleUndo` session-gate pattern. `setPreparedCapturePassState`.

### Reuse possible

**YES** — extend those owners. No new Manager. No new undo kind. No `NoteEditSessionUndoStack`.

### Architecture review required

**YES** — commit while OVERDUBBING; undo routing; GUS `passIds` wire (038.2).

---

## Formal triggers

| Trigger | Why |
|---------|-----|
| State transition | Seal an `OverdubPass` while still OVERDUBBING; one-session-one-pass withdrawn |
| Undo semantics | Session-gate while OVERDUBBING; one U: `passIds` list on stop |
| Storage schema (038.2 only) | GUS STK2 → next token for `passIds` |

**Approval required:** YES before Track firmware.

---

## Ownership change

**NO** transfer. Mutable wrap list lives on the overdub lifecycle owner (`Loop` / `Track`), not `EditManager`. LCR remains publish consumer.

## State transition change

**YES** — approved by DEC-038: wrap re-entry seals and `beginCapture` again without leaving OVERDUBBING.

## Extension point (implementation)

038.1: store first-session `playheadPhaseTick` as S (session-scoped); detect playhead return to S; seal completed pairs only; publish; session-stack `PassId`; `beginCapture(Overdub)`; `handleUndo` session-gate.  
038.2: stop pushes one `OverdubPassAdded` with `passIds` + companions.

## Tests

- Native: wrap at S publishes completed pair; held ON not sealed; session disable hides wrap-1; stamp miss → 3b (6E already).
- 038.1: native wrap-reentry + session undo without GUS.
- 038.2: native GUS `passIds` + legacy STK2 single `passId`.
- HITL after firmware: wrap-over-wrap Shorten; undo while OVERDUBBING; one U: after stop.

## Does not start

midi_gap / 6.3; path B; SD on wrap; session-id on `OverdubPass`; deleting 3b; `finalizePendingNotes` at S.
