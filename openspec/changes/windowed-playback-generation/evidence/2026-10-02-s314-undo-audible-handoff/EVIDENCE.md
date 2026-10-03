# Slice 3.14 — the undo/redo audible handoff (blanket silence replaced)

**Change:** `windowed-playback-generation` · **Task:** 3.14 (**the audible half landed; the task stays open**) ·
**Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Slice 3.14
**Contract:** DEC-108 §13(g); the design's *Runtime continuity dependency and undo/redo integration* ("Task 3.14 —
undo/redo audible handoff explicitly replaces that blanket playback-refresh behavior with a request through the existing
immutable publication owner and the cross-generation reconciliation above. The valid serving source and closure
obligations survive until the prepared boundary; unchanged notes are not retriggered or closed.").

**What the task asks of this half:** stop `refreshPlaybackAfterCapturePassStateChange` (and its capture/overdub undo/redo
callers) from blanket-silencing the slot — "not `silenceSlotMidiOutput` as blanket cache invalidation" — and settle the
closures selectively instead, so **unchanged sustained notes receive no early NoteOff and no retrigger** while the notes
the change actually removed are closed. Keep checkpoint/undo semantics and ownership unchanged. Red-prove the fixture
with the blanket silence restored.

---

## What landed

| File | Change |
|---|---|
| `include/RuntimePlaybackView.h` | `NotePresenceSet` — a bounded `(channel, note)` presence set (16×4 words, 256 B) so an off-clock identity reconciliation batches its input without allocating. |
| `include/Track.h`, `src/Track/TrackPlaybackWindowBuild.cpp` | `Track::reconcileAfterEditPassStateChange(slotIndex, passIds, identitiesAreRemoved)` — the audit handoff for a committed-identity change: the affected edit passes' **own added events** give the identities, and one ledger walk (O(open notes)) classifies every sounding note as **closed** (its identity was removed), **suppressed** (its identity was re-established, so it must not be retriggered), or **preserved** (untouched). Off-clock, allocation-free, and it applies the closures itself. |
| `src/TrackUndo.cpp` | `refreshPlaybackAfterCapturePassStateChange` no longer calls `track.silenceSlotMidiOutput(slotIndex)`; the four **edit-batch** call sites (two undo → identities removed, two redo → re-established) go through a new `refreshPlaybackAfterEditPassStateChange(track, slot, passIds, identitiesAreRemoved)` that reconciles first. Both refreshes now invalidate with the ledger **preserved**. |
| `src/Track/TrackPlaybackRuntime.cpp` | The per-slot reset's two questions are separated: the revision/generation caches always clear (they describe the merged representation every caller is invalidating) while `preserveLedger` now governs **only** the ledger — the set of notes sounding on the wire. |

**The design points that carry the requirement:**

- **The blanket invalidation had two halves, and both are gone.** The silence emitted a NoteOff for *every* sounding
  note; the refresh on the same path also **cleared the ledger**. Removing only the silence would still have killed
  untouched soundings, which is what my own fixture caught (below). Both are now selective: closures come from the
  reconciliation, and the ledger survives the cache invalidation because it *is* the closure-obligation set.
- **Selective means identity-directed.** For an edit batch, the affected identities are exactly its own added events
  (an `EditPass` carries no chunk bodies). A sounding note the batch never named is preserved — so an undo of a batch
  that touched none of the soundings emits **nothing at all**, which is the defect this slice removes.
- **The re-establishing direction does not retrigger.** A note whose identity the change re-establishes while it is
  still sounding is reported as *suppressed* rather than closed: it never stopped sounding, so neither an early NoteOff
  nor a replacement note-on is owed.
- **Undo semantics and ownership are untouched.** The pass-state change, the revision bump and the stack discipline are
  exactly as they were; this slice changes only what happens to the *soundings* afterwards. Nothing allocates on the
  path, and the reconciliation runs where the undo already mutates the pass timeline (off-clock, never on the clock
  edge).

## The oracle, case by case

| Case | Observed |
|---|---|
| undo of an edit batch, one sounding the batch owns (60) and one it never named (64) | the batch is disabled by the production path; **exactly one** NoteOff goes out (60); 60 leaves the ledger; **64 stays sounding and is not silenced** |
| the same, on the wire | `MidiSendCapture` holds one event — the blanket silence this replaces emitted **three** (60, 64, and every other sounding note) |
| the re-establishing direction | `closureCount == 0`, `suppressedRetriggerCount == 1` (60), `preservedCount == 1` (64), and **no** MIDI at all |
| the generic refresh (the overdub-session sites' path) | emits **nothing** by itself and leaves the soundings it did not reconcile alone |
| a slot with no window lane | nothing is settled and **nothing is silenced** — the blanket emitter was the only thing that acted there, and acting was the defect |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **64/64** (was 63; +1 fixture) |
| `pio test -e native` (full) | **2695 cases: 7 skipped, 2688 succeeded, 0 failed** (baseline 2694 → +1) |
| `pio run -e teensy41-capture-serial` (clean rebuild) | **SUCCESS** — `variables 96,192` **unchanged** / `code 392,328 + padding 888` / locals 34,880, floor 840, **48 B headroom**. The line is **byte-identical to the three previous slices**: the new work is FLASH-placed in cold TUs, no ITCM growth, and no size pin moved (`LoopPlaybackRuntime` still 6,624 target / 6,544 host — this slice adds no per-slot state) |

## Red-proofs

Six mutations, each restored **byte-identically** (`shasum -a 256 -c` against the pre-mutation hash of all six files):

| # | Mutation | Observed failure |
|---|---|---|
| M36a | **the blanket silence restored on the edit-batch path** (the task's own red-proof) | `Expected 1 Was 3` — a NoteOff for *every* sounding note, which is the defect |
| M36b | the blanket silence restored on the generic refresh path | `Expected 0 Was 2` — emitted where nothing is owed |
| M37 | the refresh clears the ledger again (`invalidatePlaybackMergedMidiEvents(false)`) | *"a sounding note the batch never named is untouched"* |
| M38 | the reconciliation preserves everything (closes nothing) | `Expected 1 Was 0` on the closure count |
| M39 | the re-issuing direction closes instead of suppressing | `Expected 0 Was 1` — *"a re-establishing change closes nothing"* |

**Two corrections recorded rather than smoothed over.** (1) **M36's first attempt did not discriminate**: I mutated the
*generic* refresh while the fixture drives the *edit-batch* one, so the suite stayed green — a mutation that cannot
trigger is not evidence. The fixture then gained a **direct** assertion on the generic path (calling the production
function and watching the wire) and both blanket proofs discriminate. (2) That failure is also what surfaced the
**ledger-clearing half** of the blanket invalidation: my fixture's "a sounding note the batch never named is untouched"
assertion failed *before* I split the reset's two questions — the finding came from the fixture, not from a guess, and
it is why `LoopPlaybackRuntime::reset` now clears the caches unconditionally while `preserveLedger` governs only the
ledger.

## Finding recorded while wiring (not fixed here)

`UndoEntryKind::RecordPassAdded`'s undo case (disabling a **capture** pass) does **not** call the refresh at all, so it
is not a site of this defect; whether it *should* go through the new handoff is a separate question, recorded here
rather than silently changed.

## Not claimed

- **The two overdub-session sites are only half covered.** They no longer silence and they preserve the ledger, but they
  do **not** close the session's removed soundings: their affected identities live in the session's *committed* passes,
  not in an edit batch's added events, so they need a committed-pass identity source (`PassId` list → the passes' own
  rows). **Risk stated plainly:** until that exists, undoing an overdub session can leave a note sounding whose content
  the undo removed. That is strictly better than the blanket silence (which killed *every* sounding note) but it is not
  the requirement yet.
- **The refresh is not yet a request through the immutable publication owner.** Today's refresh invalidates the cached
  representation; the *request* half of the task needs a bounded content source for the affected interval, and the
  existing request path takes a caller-owned enumerated scratch — so the choice between a cold content-sized allocation
  and the owner's own companion-pass source is a design decision the packet must state before the code takes it.
  Named as **3.14b**.
- Also owed: "pause between real preparation grains for repeated undo/redo, track/slot switching and stale completion"
  (needs 3.7's serving path), retained-source lifetime and eventual release (3.6a's release predicate — cited, not
  restated), and preview-session undo/redo (3.13's preview discipline / fixture 2.12 — cited).
- No timing claim, no device evidence, and the RAM1 headroom remains 48 B above the floor.
- **Task 3.14 stays open** on the items above.
