# Slice 3.12c — the MIDI-edge wiring of the admission limit

**Change:** `windowed-playback-generation` · **Task:** 3.12 (**third increment; the task stays open**) ·
**Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Slice 3.12
**Contract:** DEC-108 §14 (item 12(c) overflow-unreachable-at-admission, OPEN-2); the spec's *Accepted content is never
truncated to fit a window* (fixture 2.11) and *Legacy dense content is served through bounded pages* (fixture 5.5).

**What the task asks of this clause:** count the capture interval **at acceptance** in effective events, with the
note-off and generated-closure reserve **inside `L`**, using **preallocated bounded accounting that includes
outstanding reservations** and **never invokes the resolver on the MIDI edge**; refuse capacity pressure **before**
acceptance, **by name**; never refuse a stop/switch/undo request (DEC-078). Without this wiring "capacity pressure is
refused before acceptance" is a property of the unit rather than of the running system.

---

## What landed

| File | Change |
|---|---|
| `include/RuntimePlaybackView.h` | `WindowAdmissionAccounting` gains the edge's vocabulary: `kUnitsForNoteOn = 2` / `kUnitsForPlainEvent = 1`, `admitsNewEvent(isNoteOn)` — the **one** decision, as a pure function of the value so a refusal is nameable and testable without reaching a log — and `closeReservation()`, the closure clause as arithmetic (a unit **already inside `L`** moves from owed to accepted, so `committed()` is unchanged). |
| `include/Track.h`, `src/Track/TrackCaptureInput.cpp` | `Track::playbackWindowServesSlot(slot)`, `Track::playbackWindowEventCountForSlot(slot)` (both O(1), the same presence/validity test `publishedPlaybackWindowForSlot` applies) and `Track::captureIntervalAdmissionForSlot(slot)` — the accounting as a **derived value**: the serving window's content **+** this interval's accepted rows **+** the closures they owe (`pendingNotes`, which *is* the owed set) **+** the reserve. |
| `src/Track/TrackCaptureInput.cpp` | The gate in `Track::recordMidiEvents` — the MIDI edge `MidiHandler` calls for CC/PC/AT/PB and which `noteOn`/`noteOff` call for notes — consulted **before** the canonical append, with the same staged cleanup as the append-deny path. |
| `include/CaptureAppendResult.h`, `src/CaptureAppendResult.cpp` | `CaptureAppendDenyReason::WindowAdmission` (`"window_admission"`), appended last so no existing reason's value moves: the refusal is named in the same vocabulary as every other capture denial. |

**The design points that carry the requirement:**

- **Derived, not mirrored.** The accounting is a value computed from the capture it describes, at the moment it is
  asked — `window content + capture.store.size() + pendingNotes.size() + reserve`. Nothing is stored, so no counter can
  drift from the capture; the requirement's "preallocated, bounded accounting" is met as *arithmetic with no
  allocation, no scan and no resolver*, which is the property that matters at the edge. Consequence worth naming:
  **no new per-slot state, no `sizeof` pin move, no PSRAM charge.**
- **A closure is the one thing this limit may never refuse.** An admitted note-on's closing row is already counted
  inside `L`, so appending it is net-zero for `committed()`. Refusing it would truncate a guaranteed event — the spec's
  "an accepted note's closure remains guaranteed even when further input is refused".
- **The refusal leaves nothing behind.** A refused note-on gives its staging reservation back (the overdub path takes
  that reservation before the append) and never joins the owed set; no row is appended; no partial state exists.
  Deliberately **not** `MemoryMonitor::notifyCaptureAppendFailed`: over-capacity is a supported compatibility case and
  is not a memory fault.
- **Interaction acceptance is structurally outside.** The counters hold MIDI content only; a stop/switch/undo request
  has no representation to be refused by (DEC-078), and the fixture drives a stop through the same over-capacity state.

## The scope the first full-suite run decided (a measured correction, not a preference)

The gate's first wiring bounded **every** capture interval at `L`. The full native suite then failed exactly one case —
`test_shared_capture_finalization::test_record_normalization_preserves_take_larger_than_overdub_staging`
(`Expected Not-Equal`): a whole-take record take larger than the overdub staging could no longer be captured. That is
**over-reach**, and the spec's own wording says why: the invariant is that **for the configured span `W`, every
`W`-wide interval fits `L`** — "a window SHALL always be able to hold the content of the intervals it serves". A fresh
record take is not served by the 2048-event window (its playback storage is the whole-take view), so it is not
window-bounded; what `L` bounds is *what a window must hold*.

The gate is therefore consulted **exactly where a window serves the slot** (`playbackWindowServesSlot`), which is also
why the presence question has its own accessor: an empty interval is a legitimately Ready window, so "no window" and
"an empty window" must not be conflated. The sibling suite is the oracle for that boundary and is green again.

## The oracle, case by case

| Case | Observed |
|---|---|
| a window serves the slot | the window's content, the interval's accepted rows, the owed closures and the reserve are **one total** against `L`; `committed() == L - 1` leaves room for exactly one plain event and none for a note-on |
| the boundary | the plain event that fits is accepted (`committed() == L`), the next is refused; the refused note-on appends no row and leaves the owed set unchanged |
| the closure | with the interval at `L`, the closure of an accepted note **is** accepted and `committed()` is unchanged — the move, not a growth |
| the refusal's name | `captureAppendDenyReasonLabel(CaptureAppendDenyReason::WindowAdmission) == "window_admission"` |
| the refusal's cleanup (overdub path) | `staging->matchingOpenReservation(...)` is `nullptr` after a refused note-on; no row, nothing owed |
| no window serves the slot | no window term at all, and the take accepts again — the whole-take case the sibling suite pins |
| interaction | a stop request is still admitted out of `RECORDING` while the interval sits at the limit |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **59/59** (was 57; +2 fixtures) |
| `pio test -e native -f test_shared_capture_finalization` | **60/60** — the over-reach oracle, green after the scope correction |
| `pio test -e native` (full) | **2690 cases: 7 skipped, 2683 succeeded, 0 failed** (baseline 2688 → +2) |
| `pio run -e teensy41-capture-serial` (clean rebuild) | **SUCCESS** — `variables 96,192` **unchanged** / `code 392,072 + padding 1,144` / locals 34,880, floor 840, **304 B headroom**, **no re-base**. +144 B ITCM, no `.data`, and **no new state** (the accounting is derived), so `sizeof(Track)` and `sizeof(LoopPlaybackRuntime)` are untouched |

## Red-proofs

Five mutations, each restored **byte-identically** (`shasum -a 256 -c` against the pre-mutation hash of all four files):

| # | Mutation | Observed failure |
|---|---|---|
| M17 | the limit bounds note-ons only (`type == midi::NoteOn` in the gate) | both fixtures: *"the next plain event is refused by the limit"* |
| M18 | closures gated too (the `NoteOff` exemption dropped) | `Expected 0 Was 1` — the owed unit did not clear and `committed()` grew |
| M19 | `playbackWindowServesSlot` always `false` | *"a completed, published window now serves the slot"* |
| M20 | the refusal keeps its staging reservation | *"a refused note-on gives its staging reservation back"* |
| M21 | the live accepted rows are not counted (`+= 0`) | `Expected 1 Was 0` on the accounting, and the boundary assert |

**A process note the restore oracle earned.** The mutation harness restores by inverse substitution; the first pass used
a non-unique anchor, which (a) aborted one restore by assertion and (b) silently rewrote two unrelated `if` lines. The
`shasum -c` check is what caught it, and the repair was verified byte-identical before the proofs were re-run on the
final code. Anchors are now context-qualified and both directions assert their occurrence count.

## Not claimed

- **No production window lane yet.** Nothing in production publishes a window (3.5's finding; the requester is task
  3.7's), so in today's running system the gate is **inert**: `playbackWindowServesSlot` is false and the interval is
  not window-bounded. The edge **is** wired and the refusal is exercised — the fixture supplies the window and the
  overdub path's live staging — but "capacity pressure is refused before acceptance" becomes a property of the running
  system only when the window lane serves a slot, which is 3.7's own gate. Stated plainly rather than implied.
- **The live-rows-only bound is deliberately not applied** (see the scope section): a record take is not capped at `L`,
  and the sibling suite pins that boundary.
- **The plain-event reservation release on refusal is executed but not asserted.** The overdub fixture asserts the
  *note-on* reservation give-back; the `releaseIndependentEvent()` branch for a refused CC runs, but its effect has no
  accessor to observe it, so a mutation targeting that line alone would not fail. Named as a gap, not smoothed over.
- **Fixture 5.5's timing envelope and the over-capacity same-tick, wrap and cross-page-closure cases remain**, and so do
  fixture 2.12 (preview, 3.13) and the paged path's production consumer. **Task 3.12 stays open.**
