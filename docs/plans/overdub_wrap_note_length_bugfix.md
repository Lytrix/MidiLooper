## Problem statement

When overdubbing, a note that should remain **96 ticks** long ends its `NoteOff` exactly at **loop wrap** (tick `0`). During live overdub display this produces a brief **1px** wrapped head segment at tick `0`. After overdub stop/commit, the visualization settles but the note is shown as **95 ticks** (one tick short) instead of preserving **96 ticks**.

This is a **display + reconstruction boundary semantics** issue, not a stop-path validation issue.

## Evidence in code (current behavior)

- **Wrapped head-off interval produces a 1-tick head segment when `headOffTick == 0`.**
  - `NoteUtils::buildCanonicalSpansFromMidi` treats `noteOffTick < note.startTick` as wrapped and calls `exclusiveEndForWrappedHeadOff(noteOffTick, loopLength)` with `splitHeadTail=true`.
  - `exclusiveEndForWrappedHeadOff()` currently returns `headOffTick + loopLength + 1`, which makes `headOffTick == 0` map to `exclusiveEnd == loopLength + 1`.
  - `IntervalProjection::renderProjectedIntervalToDisplayNotes` uses `displayInclusiveEndTick(exclusiveEnd, start, loopLength)`; for `exclusiveEnd == loopLength + 1` the wrapped remainder yields `endTickInclusive == 0`, producing a **1-tick** head segment `[0..0]`.
  - Code location: `src/Utils/NoteUtils.cpp` (`buildCanonicalSpansFromMidi`, `exclusiveEndForWrappedHeadOff`) and `src/Utils/IntervalProjection.cpp` (`displayInclusiveEndTick`, `renderProjectedIntervalToDisplayNotes`).

- **Display drawing treats `endTick` as if it were exclusive, but reconstructed `DisplayNote.endTick` is inclusive.**
  - `IntervalProjection::renderProjectedIntervalToDisplayNotes` explicitly computes `endTickInclusive`.
  - `DisplayManager::drawNoteBar` maps `eTick` directly to x-position as if it were the exclusive end. This loses 1 tick of length in the common “end at loop boundary” case, because inclusive `loopLength - 1` should be drawn as exclusive `loopLength`.
  - Code location: `src/DisplayManager.cpp` (`drawNoteBar`).

## Target behavior

- A note that conceptually ends **exactly at loop wrap** must **not** generate a 1-tick head segment at tick `0`.
- After overdub stop, the displayed length must remain **96 ticks** (no one-tick shrink).

## Implementation plan

### 1) Fix wrap boundary semantics in reconstruction (`headOffTick == 0`)

Adjust `NoteUtils::buildCanonicalSpansFromMidi` so that a wrapped head-off at tick `0` is treated as **ending at the loop boundary**, not “including tick 0”.

Concrete change:

- In the wrapped split path (`noteOffTick < note.startTick` and `splitHeadTail=true`), add a special-case:
  - If `noteOffTick == 0`, emit a **non-split** canonical span ending at the loop boundary:
    - `start = note.startTick`
    - `exclusiveEnd = static_cast<int32_t>(loopLength)` (so projected display end becomes `loopLength - 1`)
    - `splitHeadTail = false`
  - This removes the spurious `[0..0]` head segment and preserves the tail segment length.

Files:
- `src/Utils/NoteUtils.cpp`

### 2) Fix inclusive end tick rendering in `DisplayManager::drawNoteBar`

Make `drawNoteBar` treat `eTick` as **inclusive end** (which matches `IntervalProjection` output), by converting to an internal **exclusive end** for pixel mapping:

- For non-wrapped notes (`eTick >= s` and `eTick < lengthLoop`):
  - compute `exclusiveEnd = (eTick == lengthLoop - 1) ? lengthLoop : (eTick + 1)`
  - map `exclusiveEnd` for `x1`
- For wrapped notes:
  - compute `wrappedEndExclusive = ((eTick % lengthLoop) + 1) % lengthLoop`, with the special-case that an inclusive end of `lengthLoop - 1` should make the wrapped part end at `lengthLoop` (no head segment).

This aligns drawing with the existing “inclusive end tick” convention.

Files:
- `src/DisplayManager.cpp`

### 3) Add a native unit test for the wrap-at-zero boundary

Add a new test case in `test/test_noteutils_reconstruct/test_noteutils_reconstruct.cpp`:

- Given `loopLength = 1536`
- Note-on at `loopLength - 96`
- Note-off at `0`

Assert:

- The reconstructed display notes include **only the tail segment**:
  - `startTick == loopLength - 96`
  - `endTick == loopLength - 1`
- And **do not** include a head segment `[0..0]`.

This proves the new boundary semantics and prevents regressions.

### 4) Verification steps

- Run `pio test -e native` (ensures `NoteUtils` reconstruction behavior is correct).
- Optional HITL confirmation (if you want to validate visually):
  - reproduce overdub note-off-at-wrap scenario and confirm the display no longer shows the 1px blip and final length remains 96 ticks.

## Scope / constraints

- No ownership changes: changes are local to **reconstruction semantics** and **display rendering**.
- No stop-path heavy validation changes: `LoopStopFinalize::finalizeWrapWindowOnStore` remains unchanged.

