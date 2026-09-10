# Overdub live wrap — open loop-end playhead tail display

**Status:** Plan **APPROVED** (2026-09-10) — pin **(c) both**; Stage 0 next  
**Separate from:** OpenSpec 4.6 Stage 4 (`742852ec`)  
**Observed:** HITL / manual second overdub — note held across loop end loses the
growing tail (`On → endTick = currentTick`); OLED shows only the wrapped head
fragment. Display restores correctly at **STOPPED**.  
**Not this:** DisplayNoteWindowGather stash (`preservedOverdubStopHandoff` only
when `!captureActive`); LOOP_EDIT length-fader mid-capture gate (`766d1b33`);
DEC-051 seal; Occupy/Shorten/Hide; 4.6 identity stamps.

## Debugging boundary

```text
… → capturePreview open indices / wrapHeld state     ← trust unless logs disagree
 → allowWrapContinuation during OVERDUBBING          ← current investigation
 → applyCapturePlayheadTails linear vs wrap split
 → STOPPED reconstruct / seal                        ← out (must stay equivalent)
```

## Symptom (product)

During **OVERDUBBING**, an open capture-preview note near loop end should paint
as a live playhead tail, then as the documented live wrap split after phase-0.
Instead the pre-wrap tail disappears and only the post-wrap head remains until
stop.

## Code fact (current tree)

Both draw-source and live-capture paths force:

```text
allowWrapContinuation =
  !isOverdubbing() && !(isRecording() && !isPlaying())
```

So **while OVERDUBBING, wrap continuation is off** (linear close only —
`DisplayNoteWindowGather` / `DisplayNoteResolveLiveCapture`, comment `012925`).
Guide still documents that policy in
[`NOTE_WRAPPING_LOGIC.md`](../Guides/NOTE_WRAPPING_LOGIC.md) § Display — this
plan changes **open capture-preview paint policy for OVERDUBBING**, not seal.

## Stash check (closed)

`stash@{0}` is **not** the fix: handoff requires `!captureActive()`; no
`applyCapturePlayheadTails` edits.

## Desired invariant — pinned **(c) both**

Prove **A** and **B** separately so a post-wrap pass cannot hide a pre-wrap
regression.

### A — Before phase-0 wrap

```text
On near loop end
currentTick > On
currentTick < loopEnd

expected paint:
  NoteOn → currentTick
```

### B — After phase-0 wrap (same still-open note)

```text
same open capture-preview note
phase crosses 0

expected paint:
  NoteOn → loopEnd
  loopStart → currentTick
```

### C — STOPPED

```text
stop after the same hold

expected:
  existing sealed/reconstructed result, geometry-equivalent to pre-fix
```

Diagnostic:

| Result | Meaning |
|---|---|
| **A fails** | live-tail application itself is wrong |
| **A passes, B fails** | wrap-continuation policy/path only |
| **A and B pass, C changes** | crossed display-only boundary — **stop** |

## Policy truth table (pinned)

Define `shouldAllowCapturePlayheadWrapContinuation(recording, playing, overdubbing)`
from **capture state** — not a generic “enable wrap” switch.
**OVERDUBBING is a specific exception for open capture-preview paint.**

| State | Live playhead tail | Wrap continuation |
|---|---:|---:|
| RECORDING, not playing | yes | **NO** |
| RECORDING + PLAYING | yes | **NO** |
| OVERDUBBING + PLAYING | yes | **YES** |
| stopped | n/a | existing stopped path |
| sealed/closed capture | no live tail | existing behavior |

`applyCapturePlayheadTails` remains the owner; `allowWrapContinuation` is only
the policy input to that owner.

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | **NO** — extend `applyCapturePlayheadTails` policy only |
| State-transition change? | **NO** — no seal / wrap-commit / DEC-051 |
| Unbounded MIDI-interval owner? | **NO** |

## Stages

### Stage 0 — Capture proof (A / B / C)

1. HITL or manual OD2 hold across loop end.
2. Dump `#CAP` / `DNTE` (or equivalent) for the open pitch at **A**, **B**, and
   **C**; confirm current `allowWrapContinuation == false` on OVERDUBBING frames.
3. Do not start Stage 1 until A/B/C expectations are logged against today’s
   wrong paint.

### Stage 1 — Policy predicate (behavior unchanged)

Add `shouldAllowCapturePlayheadWrapContinuation` next to existing display-window
helpers with the truth table above. Wire call sites to the predicate; **keep
OVERDUBBING → NO** until Stage 2 so Stage 1 is behavior-preserving.

Native: full truth-table tests.

### Stage 2 — Enable OVERDUBBING wrap continuation for open preview paint

Flip OVERDUBBING + PLAYING → **YES** under the predicate.
`applyCapturePlayheadTails` remains owner; paint-only
(`capturePlayheadTailsArePaintOnly`).

**Identity-neutral acceptance:** the open capture-preview / open-note
representation used for live painting is the **same** before and after wrap;
the change only alters how `applyCapturePlayheadTails` projects it into the
display window. Do not invent a second note/head identity to “fix” paint.

Native fixture from Stage 0. Device: dedicated HITL or base+manual hold.
**C must remain geometry-equivalent to pre-fix.**

Update [`NOTE_WRAPPING_LOGIC.md`](../Guides/NOTE_WRAPPING_LOGIC.md) overdub live
paint sentence when Stage 2 ships.

## Explicit non-goals

- Popping DisplayNoteWindowGather stash  
- Changing wrap seal at phase 0 (DEC-051)  
- Occupy resolve / Shorten-Hide  
- OpenSpec 4.6 / `legacyMidiEventsFromCommitted`  
- Globally enabling wrap continuation outside open capture-preview OVERDUBBING  

## Approval

**APPROVED** with (c) pinned and Stage 0 split into pre-wrap / post-wrap /
STOPPED proofs. Isolated capture-preview → display projection policy bug.
