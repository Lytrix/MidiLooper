# Overdub live wrap — open loop-end playhead tail display

**Status:** Plan only — **not implementing** until Stage 4 HITL gate closes  
**Observed:** HITL / manual second overdub — note held across loop end loses the
growing tail (`On → endTick = currentTick`); OLED shows only the wrapped head
fragment. Display restores correctly at **STOPPED**.  
**Not this:** DisplayNoteWindowGather stash (`preservedOverdubStopHandoff` only
when `!captureActive`); LOOP_EDIT length-fader mid-capture gate (`766d1b33`).

## Debugging boundary

```text
… → capturePreview open indices / wrapHeld state     ← trust unless logs disagree
 → allowWrapContinuation during OVERDUBBING          ← current investigation
 → applyCapturePlayheadTails linear vs wrap split
 → STOPPED reconstruct / seal                        ← out (already correct)
```

Do **not** reopen: length-fader capture gate; OpenSpec 4.6 Stage 4 identity
stamps; stashed gather handoff; occupy Shorten/Hide geometry.

## Symptom (product)

During **OVERDUBBING** (seen on second overdub), an open note whose On is near
loop end should keep painting as a live playhead tail: **start = NoteOn**,
**end = currentTick** (and after wrap, the documented live wrap split). Instead
the tail disappears and only the post-wrap head segment remains until stop.

## Code fact (current tree)

Both draw-source and live-capture paths force:

```text
allowWrapContinuation =
  !isOverdubbing() && !(isRecording() && !isPlaying())
```

So **while OVERDUBBING, wrap continuation is off**. Linear close only
(`DisplayNoteWindowGather` / `DisplayNoteResolveLiveCapture`, comment `012925`).

Guide note: [`NOTE_WRAPPING_LOGIC.md`](../Guides/NOTE_WRAPPING_LOGIC.md) § Display
states overdub wrap continuation is off; live RECORD growing also disables it.
This plan asks whether **open-at-loop-end live paint** during overdub should
still grow `endTick = currentTick` (and/or show head+tail after phase-0 wrap)
without reopening seal/storage.

## Stash check (closed)

`stash@{0}` DisplayNoteWindowGather WIP:

| Change | Applies during OD2? |
|---|---|
| `preservedOverdubStopHandoff` | **No** — requires `!captureActive()` |
| Draw-source complete-snapshot / sealed union helpers | Selection only; **no** `applyCapturePlayheadTails` edits |

Stash is **not** the fix for this symptom.

## Desired invariant (draft — pin before coding)

```text
While OVERDUBBING, an open capturePreview note whose On is still in the
current loop cycle paints endTick = playhead (currentTick), including when
the On is near loop end. After phase-0 wrap of that held note, live paint
follows LivePlayhead wrap split (tail + head) unless a sealed reason
forbids it — seal/storage unchanged.
```

Pin with user before firmware: is the missing piece (a) pre-wrap growing tail
only, (b) post-wrap head+tail live split, or (c) both?

## Architecture checkpoint (pre-impl)

| Question | Expected |
|---|---|
| Ownership change? | **NO** — extend `applyCapturePlayheadTails` / `allowWrapContinuation` policy |
| State-transition change? | **NO** — no seal / wrap-commit / DEC-051 change |
| Unbounded MIDI-interval owner? | **NO** |

Formal trigger only if the fix moves wrap seal ownership or enables wrap
continuation globally without a capture-scoped predicate.

## Proposed stages (after pin)

### Stage 0 — Capture proof

1. HITL or manual OD2 hold across loop end; dump `#CAP` / `DNTE` for the open
   pitch before wrap, at wrap, and after wrap vs STOPPED.
2. Confirm `allowWrapContinuation == false` on those frames (log or native
   fixture mirroring the policy).

### Stage 1 — Policy predicate

Replace the blanket `!isOverdubbing()` with an explicit predicate, e.g.
`shouldAllowCapturePlayheadWrapContinuation(recording, playing, overdubbing)`
owned next to existing display-window helpers. Default behavior unchanged until
Stage 2 flips the overdub case under test.

Native: table tests for RECORD growing / OVERDUBBING / PLAYING.

### Stage 2 — Overdub live open tail (+ optional wrap split)

Enable the pinned behavior for OVERDUBBING open preview notes only.
`applyCapturePlayheadTails` remains the owner; paint-only (`capturePlayheadTailsArePaintOnly`).

Native fixture from Stage 0 slice. Device: dedicated HITL or base+manual hold;
STOPPED frame must stay unchanged vs pre-fix.

### Explicit non-goals

- Popping DisplayNoteWindowGather stash as this fix  
- Changing wrap seal at phase 0 (DEC-051)  
- Occupy resolve / Shorten-Hide  
- OpenSpec 4.6 identity stamps  

## Relation to OpenSpec 4.6 Stage 4

Independent display bug. Gate Stage 4 with HITL `base` first; implement this
plan only after Stage 4 closeout (or explicit user override).
