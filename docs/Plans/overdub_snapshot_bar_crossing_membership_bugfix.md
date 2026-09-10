# Overdub — published LoopSnapshot omits bar-crossing NoteRef

**Status:** NEXT after consumer Stage 6 — end-state membership RC (not started)  
**Consumer containment (do not treat as end state):**  
[`overdub_start_snapshot_omits_bar_crossing_record_note_bugfix.md`](overdub_start_snapshot_omits_bar_crossing_record_note_bugfix.md) Stages 1–6  
**Evidence:** [`194531`](../../captures/session_20260910_194531.log) id 27 p74;  
[`205031`](../../captures/session_20260910_205031.log) id 26 p73 @ 1480–1541  

## Architectural marker

Stages 1–6 are **containment/diagnostic**. They must **not** be accepted as proof
that snapshot membership is architecturally complete.

**End-state invariant:** The immutable `LoopSnapshot` contains every committed
playback/display-relevant NoteRef that belongs in its span. Display/source
consumers trust that representation. VisualCache is a temporary live/capture
display base — not a permanent repair authority for Snapshot completeness.

**Stopping rule:** After Stage 6, no further consumer exception stages unless a
new independent failure demands one. This plan is the next RC.

## Debugging boundary

```text
committed events / STOPPED reconstruct   ← trust (note exists)
 → LoopSnapshot construction / bar membership / publish  ← ROOT (incomplete)
 → draw-source / source-view consumers                   ← currently compensate
 → OLED
```

Do **not** change DEC-051 seal, Occupy Hide/Shorten owners, or wrap Stage 2b
upsert unless investigation proves a formal trigger.

## Textbook evidence

```text
committed events:     note exists
visualCache:          note exists
snapshot:             note missing
draw source:          note missing
STOPPED reconstruct:  note exists
```

## Architecture checkpoint (before firmware)

| Question | Expected |
|---|---|
| Ownership change? | **YES** — snapshot construction/membership content must include the NoteRef (design pin required) |
| State transition change? | **NO** unless proven otherwise |
| Unbounded MIDI-interval owner? | **NO** — construction/idle publish only |

## Stages (outline)

1. Trace owner: snapshot construction / bar membership / journal apply for spans crossing `TICKS_PER_BAR`.
2. Native fixture from 194531 / 205031 seeds — publish then `appendSnapshotNotesForBar*` includes the NoteId; sealed Delete still omits Hide targets.
3. Fix membership in that owner (bar refs for closed bar-crossing notes).
4. HITL `base`: snapshot gather alone keeps id 26/27 without Stage 6 heal.
5. Unwind consumer Stage 5–6 (and trim other membership-only compensations).

## Non-goals

- Another paint-time heal
- VisualCache as second completeness authority
- Wrap-held Option C unless this investigation points there
