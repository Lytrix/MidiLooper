# Slice 4.5b — the cross-generation closure reconciliation (§13g)

**Change:** `windowed-playback-generation` · **Task:** 4.5 (**second increment; the task stays open**) ·
**Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Slice 4.5
**Contract:** DEC-108 §13(g); the spec's *Cross-generation note closure is reconciled off-clock* — "version handoff SHALL
preserve sounding notes that remain unchanged, **without retriggering** them; notes removed or invalidated by the
replacement SHALL be closed at handoff; closures that became obsolete SHALL be suppressed afterwards; required note-offs
SHALL be emitted **before** the replacement's note-ons; and closure obligations SHALL stay alive independently of
view-buffer retirement… An **event-empty window means 'no new events'**".

**This increment is the dependency task 3.14 names** ("task 4.5's off-clock selective reconciliation"), so it is the
piece that unblocks the undo/redo handoff.

---

## What landed

| File | Change |
|---|---|
| `include/RuntimePlaybackView.h` | `ClosureSettlement` — the handoff's plan as a **value**: `closures` (sounding keys the replacement removes, whose note-offs are required at handoff), `suppressedRetriggers` (sounding keys the replacement re-issues, whose note-ons must not be emitted), `preservedCount` (neither), and `eventEmptyWindow`. Every list is bounded by `ActiveNoteLedger::kMaxOpenNotes`; nothing allocates. Plus the one **interpretive** rule, stated where it is decided: `handoffAbsenceMeansRemoval(incomingIsWholeLoop, incomingIsEmpty)`. |
| `include/Track.h`, `src/Track/TrackPlaybackWindowBuild.cpp` | `Track::planClosureSettlementForSlot(slot, events, count, incomingIsWholeLoop)` — the **off-clock** planner: one bounded pass over the incoming revision's note-ons into a 256-byte presence set, then a walk of the outgoing ledger (≤128 entries) with no search. `Track::applyClosureSettlementForSlot(slot, settlement)` — emits the closures **first** and only closures, discharging exactly those ledger obligations through the ledger's own event path. |

**The design points that carry the requirement:**

- **The three fates are separated by construction**, which is what makes the clauses checkable instead of implied: a
  sounding note is either closed (removed), suppressed (re-issued → not retriggered, not closed), or preserved
  (untouched). "Unchanged" therefore has a mechanical meaning: present in the incoming content → no retrigger.
- **"Removed" is only claimed when the evidence supports it.** A window is a *slice* of the loop, so absence from its
  content cannot prove a sounding note was removed — a sustained note whose onset lies outside the slice is still
  sounding. Absence is evidence of removal therefore **only** when the incoming content is authoritative for the whole
  loop, and never for an empty window. That single rule is what makes "an event-empty window means *no new events*" a
  consequence rather than a special case (stated as an interpretation, and marked as one below).
- **Ordering is a calling order, not a promise.** The applier emits closures and nothing else; the replacement's own
  note-ons are the caller's to emit when it returns. That is the spec's "required note-offs SHALL be emitted before the
  replacement's note-ons" as structure.
- **Off-clock and bounded.** The plan is computed where the handoff is decided (no content scan on the clock path, no
  allocation, a 256-byte presence set rather than a 2 KB byte table), and the clock path only ever *reads* the finished
  plan.
- **Obligations outlive view retirement** (3.6a's owned bodies and its one release predicate) — cited, not restated:
  nothing in this plan depends on the view buffer that carried a note's origin.

## The oracle, case by case

| Case | Observed |
|---|---|
| authoritative whole-loop content, three soundings | `closures = {64, 67}` in plan order, `suppressedRetriggers = {60}`, `preservedCount = 0` |
| applying it | exactly **two** NoteOffs go out — for 64 then 67 — and **only** those (real MIDI output via `MidiSendCapture`); 64 and 67 leave the ledger; **60 stays sounding** |
| a *slice* of the loop, one sounding absent from it | `closureCount == 0`, `preservedCount == 2`, **no** MIDI emitted, both soundings still in the ledger |
| an **event-empty** window, authoritative | `eventEmptyWindow`, `closureCount == 0`, **no** MIDI emitted, every sounding note preserved |
| the interpretive rule | `handoffAbsenceMeansRemoval(wholeLoop, empty) == false` — an empty window is never removal evidence |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **63/63** (was 62; +1 fixture) |
| `pio test -e native` (full) | **2694 cases: 7 skipped, 2687 succeeded, 0 failed** (baseline 2693 → +1) |
| `pio run -e teensy41-capture-serial` (clean rebuild) | **SUCCESS** — `variables 96,192` **unchanged** / `code 392,328 + padding 888` / locals 34,880, floor 840, **48 B headroom**. The line is **byte-identical to the two previous slices**: the planner and applier follow this TU's `TRACK_COLD_MEM` (FLASH) placement, so the reconciliation costs **no ITCM**, and no size pin moved (`LoopPlaybackRuntime` still 6,624 target / 6,544 host) |

## Red-proofs

Four mutations, each restored **byte-identically** (`shasum -a 256 -c` against the pre-mutation hash of all five files):

| # | Mutation | Observed failure |
|---|---|---|
| M32 | absence always means removal (the slice/empty rule dropped) | *"a slice cannot prove a sounding note was removed"* (`Expected 0 Was 2`) |
| M33 | a re-issued note is closed as well (the suppression branch falls through) | *"the two absent soundings are closed"* (`Expected 2 Was 3`) |
| M34 | the applier emits no closure | `Expected 2 Was 0` on the captured MIDI |
| M35 | the applier does not discharge the ledger | *"a closed note's obligation is discharged"* |

**A harness note, recorded because it cost a repair:** M32's inverse used a bare `return true;`, which is not unique in
that header, so the restore aborted and the mutation stayed applied — the `shasum -c` check caught it. The repair used a
context-qualified anchor and was verified byte-identical before the gates were run. (Same lesson as 3.13's: both
directions of a mutation anchor must be context-qualified.)

## Not claimed

- **The emitted order at the real handoff is not integrated here.** The plan states the order and the applier emits
  closures first, but the *clock-path* caller that emits the replacement's note-ons afterwards is the consumption path
  (3.7's consumer switch), because nothing serves windows in production yet.
- **"A closure whose note-off moves across the handoff"** — the replacement *moving* a note-off past the boundary and
  the closure firing at the correct tick — is emission-tick behaviour on that same serving path, so it stays owed.
- **Displayed provenance** for a reconciled handoff is not asserted here (the display path reads the view's
  `passOrigins`/`canonicalRanks`; no fixture drives it).
- `handoffAbsenceMeansRemoval` is an **INTERPRETATION** (as 3.8's note did for its mappings): the requirement does not
  say in so many words that whole-loop authority is the condition for treating absence as removal, but its
  "event-empty window" clause and its "removed notes" clause cannot both hold without one, and this is the pair the code
  makes checkable. A reader may dispute the mapping without the citations being wrong.
- No timing claim, no device evidence, and the RAM1 headroom remains 48 B above the floor.
- **Task 4.5 stays open** on the integration items above.
