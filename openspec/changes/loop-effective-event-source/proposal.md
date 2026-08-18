## Why

Device capture [`035414`](../../../captures/session_20260814_035414.log) attributes **6.78 s** overdub-open latency to `establishOverdubSourceView` (`gatherCommittedEvents` + `reconstructDisplayNotes` over 3385 events / 68 bars). `set_state` is 7 µs. Layer A closed the `UndoStacks` stall; this is a **source-view acquisition** problem, not an overdub FSM problem.

The overdub operation is cheap; constructing a representation overdub happens to need is expensive — and that representation must not be a display reconstruction.

## What Changes

- **D1 — Incrementally maintained runtime effective event source** (`Loop` owner): canonical committed content, updated on pass commit / undo toggle / edit apply / load — **before** the user presses overdub. No full-loop merge at `beginOverdubSession()`.
- **D2 — Range-driven overdub source window**: overdub entry consumes `effectiveEvents().range(window)` only; overlap/note-off reads the established window, not a full-loop `DisplayNote` rebuild.
- **Display decoupled**: visual cache rebuild remains **idle/async**; overdub MIDI capture becomes active immediately; display may lag briefly (eventually consistent).
- **Forbidden at overdub entry**: full-loop `gatherCommittedEvents()`, full-loop `reconstructDisplayNotes()`, full visual-cache rebuild as a prerequisite.

**Non-goals (this change):** persisted checkpoint + tail (D3 / Layer C Stage 6); range-first `LoadLoopJob` publication gate (D4 / Layer D Stage 7); replace `GlobalUndoStack` (Stage 3b); post-stop `PlaybackFullMaterialize` legacy path cleanup (separate slice); interval reservation; RC-J patches.

## Capabilities

### New Capabilities

- `loop-effective-event-source`: Runtime effective committed content maintained incrementally; range query API; overdub entry without display reconstruction.

### Modified Capabilities

- `overdub-pass-overlap-resolution`: overdub source established from effective event range, not full-loop display reconstruct.
- `long-record-memory-headroom`: overdub-open latency MUST NOT scale with loop event count or historical pass count.

## Impact

- `Loop` / `LoopMaterialization` / `LoopCapture` — effective store owner, incremental maintenance, `establishOverdubSourceView` rewrite.
- `Track::beginOverdubSession` — no synchronous full-loop work; optional narrow window establish only.
- `DisplayManager` / `LoopVisualCache` — display rebuild not gated on overdub entry.
- Brownfield: [`loop_layer_d_overdub_rebuild_architecture.md`](../../../docs/Plans/loop_layer_d_overdub_rebuild_architecture.md), DEC-035 Layers C–D, [`035414`](../../../captures/session_20260814_035414.log).
- Formal trigger: new runtime derived representation owner — see `ARCHITECTURE-REVIEW.md`.
