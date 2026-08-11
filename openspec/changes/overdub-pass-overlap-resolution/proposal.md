## Why

Long overdub after loop wrap (`session_20260811_183525`) shows a sustained `Capture append failed (duplicate)` storm (191×, 0× `pool_alloc`) that starves the CAP ring and freezes the display. Capture today uses `isDuplicateCaptureEvent` with a reverse-tick early-out that assumes append order is tick-monotonic — false after wrap.

That path is not a complete overlap model. Overdub must resolve newly inserted notes against **one immutable source pass** using the same note-overlap rules as NOTE_EDIT (`NoteGeometryResolver` / `edit-session-action-geometry`), accumulating consequences on a **new overdubPass**, without inventing capture-only duplicate semantics or `lastSeenTick` authority.

## What Changes

- Normative **two-pass** overdub overlap model: immutable source pass → per-note overlap evaluation → ops on pending/new `overdubPass`.
- **Evaluate on insert** during one overdub session (including after every loop wrap); session still ends as **one** `commitCapturePass()` / **one** overdub undo.
- Reuse **NoteGeometryResolver** / constrained-geometry / `EditSessionAction` encoding patterns for Shorten / Hide / Add — source pass not mutated in place.
- Explicit **source-pass identity**: pre-session canonical note geometry as one immutable chunk/window view (named in design).
- Replace capture-only append-order dedup as the semantic authority; wrap-safe **source-pass candidate lookup** (183525 performance track).
- Behavior-preserving deny-log / CAP throttle so verification survives wrap (orthogonal).

## Non-goals

- Per-wrap undo / new pass-per-wrap grouping
- Persistence overlap resolution, Phase 5 recovery, overlay picker, admit API
- Changing DEC-020 mid-pass sealed-chunk writer
- New global note index mandated before chunk/window audit
- `lastSeenTick` as semantic authority
- Critical reclaim / `pool_alloc` policy (183525 falsified reclaim for these denies)

## Capabilities

### New Capabilities

- `overdub-pass-overlap-resolution`: two-pass overdub overlap contract, session vs evaluation boundaries, source-pass immutability, per-note evaluate-on-insert, encoding of consequences on `overdubPass`, candidate-lookup constraints, parity with note-edit overlap policy

### Modified Capabilities

- `timeline-passes`: clarify that loop wrap does not create a pass/undo; overdub session remains one `overdubPass` / undo unit while overlap evaluation may run many times within the session (no change to pass kinds or storage families)
- `edit-session-action-geometry`: extend applicability so overdub overlap decisions SHALL follow the same constrained-geometry / action semantics for equivalent note geometry (overdub remains a separate apply/encode owner for the pending overdubPass — not NOTE_EDIT session store)

## Impact

- Firmware (implementation phases): `Loop` / `LoopCapture`, `TrackCaptureInput`, overlap apply encoding onto pending overdub pass; candidate lookup via existing materialize / chunk-window APIs; `NoteGeometryResolver` reuse
- Related: [`capture-pass-boundary-materialization`](../capture-pass-boundary-materialization/) Q16 NoteMinLength — design must reconcile capture-tier min-length with edit geometry min-length
- Evidence: [`captures/session_20260811_183525.log`](../../../captures/session_20260811_183525.log); plan [`.cursor/plans/wrap_duplicate_display_freeze_c7075cd6.plan.md`](../../../.cursor/plans/wrap_duplicate_display_freeze_c7075cd6.plan.md)
- Guides: [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../../../docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md), [`NAMING.md`](../../../docs/Authority/NAMING.md)
- Verification: native matrix (duplicate / overlap / wrap re-eval / source immutability); device wrap+bar41 after lookup; `pio test -e native`
- Persistence tracks A/B remain parallel and non-blocking for **docs-only** propose; firmware apply must not share a session with Phase 5 / overlay impl

## Primary architectural invariant

> A newly inserted overdub note is resolved incrementally against the immutable note geometry of one source pass. Overlap consequences belong to the new overdub pass. One overdub session may perform many such evaluations across multiple loop wraps while remaining one undoable overdub operation.

## Per-phase review

[ARCHITECTURE-REVIEW.md](./ARCHITECTURE-REVIEW.md) — architecture + implementation gates each phase.
