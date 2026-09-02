# Overdub reconstruct — equal-tick On+Off must not finish open to loop end

**Status:** Host shipped — device recapture next  
**Date:** 2026-09-02  
**Kind:** bugfix  
**Evidence:** [`013709`](../../captures/session_20260902_013709.log) reconstruct `DNTE,86,776,776,2296` from `SEVT N+F@776 ch13 pitch 86`  
**Does not authorize:** changing `midiEventChronologicalLess` / Gate 5A Off-before-On; source-view LIFO pairing (`3024→448`); OVERDUBBING reconstruct; RC2b tails

---

## Debugging boundary

```
storage SEVT same-tick N+F (776, ch13, 86/84)
    ← trust
Gate 5A Off-before-On chronological sort
    ← stay
startTick < pairingOffTick rejects equal tick
    ← this slice
finishCanonicalSpansOpenNotes extends leftover On to L
    ← consequence, not a second owner
source-view 3024→448 / occupy at 288
    ← Symptom 2; other owner
```

## Problem

`013709` raw reconstruct (`DIAG,dnte,src,reconstruct`) emits `86@776` length **2296** (to loop end). Storage is a completed same-tick pair:

```
SEVT,N,776,13,86
SEVT,F,776,13,86
```

`appendCanonicalSpansFromMidiRange` pairs only when `startTick < pairingOffTick`. Equal tick fails. Chronological sort then processes Off first (stack empty → Off discarded) and On second (`finishCanonicalSpansOpenNotes` → exclusive end `L`). Same for On-then-Off merge order: Off sees `776 < 776` false and is discarded.

Zero-length notes are already invalid for occupy ([`overdub_playback_observation_overlap_refinement.md`](overdub_playback_observation_overlap_refinement.md)). They must not become open holds.

## Invariant

```
A same-channel same-pitch NoteOn+NoteOff at the same tick is a completed
zero-duration pair. Reconstruct and findOpenNoteOns consume both events
and do not emit a display span or an open note to loop end.

Abutting replacement (Off of A and On of B at the same tick while A is
open) still closes A first — Gate 5A / startTick < pairingOffTick.
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — `NoteUtils::appendCanonicalSpansFromMidiRange` / `findOpenNoteOns` |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** — same event walk; equal-tick peek is the remaining same-tick events |

Reuse: extend those owners. Do not change `midiEventChronologicalLess`.
