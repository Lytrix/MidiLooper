## Why

Long overdub after loop wrap (`session_20260811_183525`) shows a sustained `Capture append failed (duplicate)` storm (191×, 0× `pool_alloc`) that starves the CAP ring and freezes the display. Capture today uses `isDuplicateCaptureEvent` with a reverse-tick early-out that assumes append order is tick-monotonic — false after wrap.

That path is not a complete overlap model. Overdub must resolve newly inserted notes against a stable **`overdubSourceView`** using the same note-overlap rules as NOTE_EDIT, and commit an **`overdubPass` that is a complete delta** (Add plus Shorten/Remove of source notes) — closer to the existing `editPass` model than to a deduplication cache.

## What Changes

- Establish **`overdubSourceView`** at overdub start (`Loop` provides; `Track` lifecycle triggers): materialize-aware, semantically stable for the session (not a “loop freeze”).
- **Evaluate on insert** across wraps against that same view; session still one `commitCapturePass` / one undo.
- **`overdubPass` complete delta:** Add + Shorten + Remove/Hide of source notes; source material immutable.
- Reuse constrained-geometry / `EditSessionAction` semantics (not a capture-only overlap policy).
- Wrap-safe **candidate lookup** into the source view (183525 performance framing); physical backing not prescribed.
- Phase 1 = source view + native tests only; later phases wire resolve/encode/early-out; deny-log throttle separate if needed.

## Non-goals

- Per-wrap undo / FrozenPass / FrozenGeometry domain nouns
- Persistence overlap resolution, Phase 5 recovery, overlay, admit API
- Changing DEC-020 mid-pass sealed-chunk writer
- New global note index mandated before chunk/window audit
- `lastSeenTick` as semantic authority
- Critical reclaim / `pool_alloc` policy
- Bundling all 183525 fixes into Phase 1

## Capabilities

### New Capabilities

- `overdub-pass-overlap-resolution`: `overdubSourceView` contract; evaluate-on-insert; complete overdubPass delta; candidate-lookup constraints; note-edit geometry parity; persistence boundary

### Modified Capabilities

- `timeline-passes`: wrap does not create pass/undo; session remains one `overdubPass` / undo while evaluation may run many times
- `edit-session-action-geometry`: overdub overlap decisions SHALL follow the same constrained-geometry / action semantics for equivalent note geometry (separate apply target for pending overdub delta)

## Impact

- Firmware: `Loop` (source view + later delta encode), `Track` overdub lifecycle, capture input; geometry helpers; native tests
- Related: parked `capture-pass-boundary-materialization` Q16 min-length globals
- Evidence: [`captures/session_20260811_183525.log`](../../../captures/session_20260811_183525.log); [`long_overdub_wrap_duplicate_display_freeze_bugfix.md`](../../../docs/Plans/long_overdub_wrap_duplicate_display_freeze_bugfix.md)
- Guides: [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../../../docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md), [`NAMING.md`](../../../docs/Authority/NAMING.md)
- Verification: Phase 1 native source-view tests; later device wrap+bar41; `pio test -e native`

## Primary architectural invariant

> Each overdub session has a stable, materialize-aware `overdubSourceView` established at start. Newly inserted notes are resolved against that view (including across wraps). The source is never destructively modified. The resulting `overdubPass` records the complete delta (additions plus source shorten/remove). The session remains one overdub pass/undo under the current undo model.

## Per-phase review

[ARCHITECTURE-REVIEW.md](./ARCHITECTURE-REVIEW.md)
