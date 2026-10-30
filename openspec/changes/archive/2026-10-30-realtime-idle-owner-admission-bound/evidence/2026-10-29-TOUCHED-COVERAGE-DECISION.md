# Task 4.5 resolved — touched-coverage preparation (chosen architecture)

**Date:** 2026-10-29
**Change:** `realtime-idle-owner-admission-bound`
**Decision:** owner-chosen, replacing both rejected options (shrink `kOverdubSourceWindowBars` / optimise
the moving-window derive).

## The problem the moving window created

The view's domain was "what is near the playhead" (16 bars, centred). Its consequences, measured:

- the derive is **repeated**: ~83 ms per wrap on device (16-bar window, 160 notes, from `RIAO-2.P` and
  the 201 ms whole-pass device span at ≈375× host/device);
- the cost is paid **again at every wrap**, though the content did not change;
- a **coverage transition** (wrap with a displaced viewport) cannot be bounded by a multi-turn derive
  without serving stale geometry — the 4.5 blocker.

## The chosen shape

```
sourceGeneration : committed content identity the coverage is qualified by
requiredCoverage : loop-relative phase regions the session's accepted notes touch (monotonic)
preparedCoverage : regions derived and published for sourceGeneration (per region, atomic)
pendingCoverage  : regions owed to the deferred owner
```

- An accepted note adds the regions **its own span** touches. Required never shrinks in a session;
  playhead movement and wrap neither move nor invalidate prepared coverage.
- Coverage is a set of **bounded tiles**, not one contiguous extent: notes at bar 2 and bar 27 require
  two regions, not the 25 bars between them, and elapsed wraps cannot manufacture touched coverage.
- **Conservative closure**: candidate lookup widens backward by the **maximum committed source-note
  span**. This is conservative, not exact — a global maximum may over-admit, never under-admit. A
  per-pitch/per-region index is future work.
- Event acceptance **marks** demand; the bounded deferred owner prepares it, one grain per turn.

## Where coverage is actually required (the characterization that was asked for)

| point | requirement | today |
|---|---|---|
| note-on (`TrackCaptureInput.cpp:222-232`) | occupancy at the **pressed phase**; playback ledger primary, source view fallback | synchronous, one tick |
| note held (`ensureOverdubSourceNotesForHold`) | participation for the held pitch — the 94.9 ms `why=private_hold` path | synchronous |
| note-off (`TrackCaptureInput.cpp:1106-1130`) | the **complete note span** → Shorten/Hide | synchronous |
| stop finalization (`TrackCaptureInput.cpp:720-745`) | same, for notes held at the stop | synchronous |

So "preparation may catch up while the note is held" is **only true for the span behind the playhead**.
Two points need source information on the MIDI path, and both become **named prerequisites**:

- `CoveragePrerequisite::OccupancyAtPhase(phase)` — ledger first, as today; the source-view fallback is
  refused rather than served from partial coverage.
- `CoveragePrerequisite::SpanFinalization(span)` — when the span's regions are not prepared, the
  completion defers **by name** and is not computed against partial coverage.

No synchronous derivation is added to the note path.

## Why this is better than the windowed shape it replaces

| | moving window (rejected) | touched coverage (chosen) |
|---|---|---|
| cost per region | ~83 ms extrapolated, **per wrap** | a 4-bar tile roughly ~20 ms extrapolated, **once per generation** |
| wrap | re-derives, coverage transition hazard | nothing to catch up: coverage did not move |
| sparse session | derives 16 bars regardless | derives only the touched tiles |
| closure | fixed 1-bar pad (under-approximates long notes) | max committed span (never under-admits) |
| readiness | whole-loop completeness | **per-tile, atomic** |

Measured baselines this rests on: derive cost is near-linear in notes (`RIAO-2.P`: whole 310/523 µs
host, 16-bar 160/221, 4-bar 40/52) and the production path is already window-bounded (`RIAO-2.Q`: whole
311 ops, 16-bar 181, 4-bar 61 — all 311 before the windowed derive).

## What this changes in the tree

- The windowed, wrap-padded derive from tasks 4.2–4.4 is **kept as the grain**, not discarded.
- `kOverdubSourceWindowBars` and the playhead-centred coverage stop being correctness mechanisms.
- Whole-loop readiness stops being a requirement: readiness is **per tile**, atomic, and the whole-loop
  cache becomes an **optional prewarm** that no consumer may depend on.
- `(coverageStart, coverageLength)` becomes insufficient — per-tile coverage replaces it. It was a step,
  not the destination.
- The tile is the **correctness/publication** unit; the **execution** unit is a separate resumable
  event/operation cursor inside it, so neither a dense tile nor a long closure can enlarge a turn.
- Three **persistent** prerequisites (`OccupancyAtPhase`, `HeldParticipationAtPhase`,
  `SpanFinalization`) carry owed obligations until discharged or the session ends; they are coalesced by
  note identity + tile, not queued per tick.
- Parity is asserted on **convergence** (every phase touched and prepared), not on a cache being complete.

**Cost wording, corrected.** The native characterization measured **approximately linear** cost in
operations/notes, not a superlinear production derive. The "4 bars ≈ 20 ms / 2 bars ≈ 10 ms" figures are
**device-cost estimates extrapolated from host measurements**, not direct device measurements; the device
oracle must confirm them.

## Acceptance recorded (from the owner's list, now in `tasks.md` §4)

Sparse touches do not derive the middle; repeated wraps without new regions derive nothing; revisiting a
prepared region re-derives nothing; a committed note sustaining into a region is included through
closure; wrap-crossing source and overdub notes stay correct; same-pitch twins keep `NoteId`; multi-turn
preparation never exposes partial coverage as ready; a source-generation change invalidates
coverage/cache; touching the whole loop converges to whole-loop parity with the old derivation.

## Not done

Nothing of the architecture is implemented yet. Tasks 4.1a–4.15 (renumbered in `tasks.md` §4) are the
work, with 4.1a (the prerequisite contract) and 4.1b (tile envelope) first. `design.md` D3/D3a rewritten;
`specs/overdub-pass-overlap-resolution/spec.md` rewritten around the touched-coverage requirements.
