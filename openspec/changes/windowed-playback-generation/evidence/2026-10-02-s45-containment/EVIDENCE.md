# Slice 4.5 — second-miss fault containment and the approved last resort

**Change:** `windowed-playback-generation` · **Task:** 4.5 (**the containment half landed; the task stays open**) ·
**Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Slice 4.5
**Contract:** DEC-108 §13(e) (second miss latches a named deadline-contract fault; containment is not a successful
fallback), §13(f) (the approved last resort: targeted note closure plus affected-slot silence, never a global transport
stop and never an unbounded rebuild); the spec's *A second miss latches a fault, and continuity is preserved*.

**What the task asks of this half:** latch a **named** deadline-contract fault on the second consecutive miss; keep
serving the last coherent revision wherever valid coverage remains through an admitted bounded path; keep preparation
progressing; recover at an eligible boundary; never reset the lag silently, publish partial content or block the clock;
and, only when **no valid coverage remains at all**, apply the approved last resort.

---

## What landed

| File | Change |
|---|---|
| `include/RuntimePlaybackView.h` | `ContinuityContainment { Nominal, ServeLastCoherentRevision, LastResortTargetedSilence }` and its **pure** resolution `resolveContinuityContainment(faultLatched, validCoverageRemains)` — total, with no waiting and no rebuilding outcome, so "block the clock" and "unbounded rebuild" are not representable rather than merely avoided. |
| `include/TrackPlaybackRuntime.h` | `deadlineContractFaultLatched` on the per-slot runtime: the **latched** fault, set by the second consecutive miss. |
| `src/Track/TrackPlaybackWindowBuild.cpp` | The latch and its one clear in `Track::advancePlaybackLagTraversal` (3.6b's breach *report* keeps its once-per-episode shape; the latch is what containment reads): the fault is set on `BreachSecondMiss` and cleared **only** where the boundary may serve the current revision. Plus `Track::containmentForSlot(slot, phase)` (a **coverage** question — the served window must still be readable and its interval must still cover the traversal), `Track::deadlineContractFaultLatchedForSlot`, and `Track::applyLastResortForSlot(slot, phase)` with a **named** `LastResortOutcome { NotNeeded, SlotSilenced }`. |
| `include/Track.h` | The declarations above. |

**The design points that carry the requirement:**

- **Containment is not a successful fallback.** The fault is latched *in addition to* the one-per-episode breach report,
  and it is cleared at exactly one place — the eligible boundary — so the condition can never be mistaken for meeting the
  one-pass-late guarantee, and a further edit never forgives it (AC-5's rule, already pinned by 3.6b).
- **The decision never waits.** `ContinuityContainment` has no waiting, deferring or rebuilding value; the clock is never
  blocked by a missed deadline, and no partial content is published. The served window stays byte-for-byte untouched
  through the whole episode.
- **"Valid coverage remains" is a coverage question, not a content question.** The served window must be readable and
  cover the traversal's phase. An event-empty window still covers its interval — which is the structural half of the
  spec's "an event-empty window means *no new events*, never silence for a sustained note".
- **The last resort is targeted by construction.** It uses the existing **slot-scoped** owner
  (`Track::silenceSlotMidiOutput`) for the closures and clears **that slot's** ledger; it touches no transport state and
  rebuilds nothing. Both halves being slot-scoped is the guarantee that it can never become a global stop, and the
  absence of any rebuild is the guarantee that it can never become an unbounded rebuild.
- **Silence is never opportunistic:** with the fault latched but coverage intact, applying the last resort answers
  `NotNeeded` and emits **nothing at all**.

## The oracle, case by case

| Case | Observed |
|---|---|
| the first miss | `PreviousOnePass`, **no** fault latched — the allowance is not a fault |
| the second consecutive miss | `BreachSecondMiss`, the fault **latched**, reported once per episode |
| a further edit | still `BreachSecondMiss`, still exactly **one** report, the fault still latched (AC-5) |
| continuity | the served window identity is field-for-field unchanged; `containmentForSlot` → `ServeLastCoherentRevision` |
| preparation keeps progressing | the committed lane still **admits** a replacement while the fault is latched |
| recovery | after the replacement is adopted, the next traversal is `Current` and the fault clears — at that boundary and nowhere else |
| the last resort, coverage intact | `NotNeeded`, and `MidiSendCapture` records **zero** events |
| the last resort, no coverage | `LastResortTargetedSilence` → `SlotSilenced`; exactly **one** NoteOff goes out, for the affected slot's open key; that slot's ledger is cleared and a **sibling slot's is untouched** |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **62/62** (was 61; +1 fixture) |
| `pio test -e native` (full) | **2693 cases: 7 skipped, 2686 succeeded, 0 failed** (baseline 2692 → +1) |
| `pio run -e teensy41-capture-serial` (clean rebuild) | **SUCCESS** — `variables 96,192` **unchanged** / `code 392,328 + padding 888` / locals 34,880, floor 840, **48 B headroom**. The line is **byte-identical to the previous slice's**: the new functions follow this TU's `TRACK_COLD_MEM` (FLASH) placement, so containment costs **no ITCM**, and the new `bool` fitted existing padding — **no size pin moved this time** (still `sizeof(LoopPlaybackRuntime) == 6,624` target / 6,544 host) |

## Red-proofs

Six mutations, each restored **byte-identically** (`shasum -a 256 -c` against the pre-mutation hash of all five files):

| # | Mutation | Observed failure |
|---|---|---|
| M27 | the second miss does not latch the fault | *"a second miss latches the deadline-contract fault"* |
| M28 | containment ignores coverage | *"no valid coverage remains at all"* — the last resort became unreachable |
| M29 | the last resort closes nothing (track-wide all-notes-off instead of the slot-scoped closure) | `Expected TRUE Was FALSE` on the captured NoteOff |
| M29b | the last resort sweeps ledgers globally instead | *"a sibling slot is untouched -- the last resort is targeted, not global"* |
| M30 | an eligible boundary does not clear the fault | *"an eligible boundary clears the latched fault"* |
| M31 | the last resort is applied opportunistically | `Expected 0 Was 1` — an event emitted while coverage remained |

**Two corrections the red-proofs forced, both recorded rather than smoothed over.** (1) **M29 first did *not*
discriminate**: the fixture observed only the *ledger*, and the track-wide owner (`Track::silenceTrackMidiOutput`) emits
CC 123 and touches **no** ledger, so the two owners were indistinguishable from a ledger-state fixture. The fixture now
asserts the **MIDI output** through the existing native send capture (`MidiSendCapture`), which is the closure half
itself ("asserting MIDI output"), and M29 then failed as it should. (2) That investigation also showed an assertion I had
written — `g_nativeSilenceTrackCalls == 0` — is **vacuous in this suite**, because that counter is only incremented by a
different harness's stub (`off_path_deps.h`); it was **removed** rather than left in place as fake evidence, and M29b
(a genuinely global sweep) was added so the sibling-slot assertion is shown to have teeth.

## Not claimed

- **The cross-generation closure half (§13g) is not implemented here.** Its clauses — preserve unchanged sounding notes
  without retriggering, close removed/invalidated notes at handoff, suppress obsolete closures, emit note-offs **before**
  replacement note-ons, obligations outliving view retirement — are about **emission at a version handoff**, which is the
  consumption path (3.7's consumer switch and 3.14's undo/redo handoff). What exists today is cited rather than
  restated: retention keeps closure obligations alive across view retirement (3.6a's owned bodies and its one release
  predicate), an event-empty window is "no new events" (3.5's readiness, and containment's coverage test above), and the
  last resort's NoteOffs are the only emission ordering this slice asserts. The task's own verification list — a closure
  whose note-off moves **across the handoff**, a window that is empty **while a note is sustained**, with displayed
  provenance and eventual recovery — stays owed to the consumption path.
- **No production caller applies containment.** Nothing serves windows in production yet (3.7's gate), so
  `containmentForSlot`/`applyLastResortForSlot` are implemented and exercised by the fixture; the serving-time caller
  arrives with the consumer switch. This is the same stage boundary the change has used for every lane so far.
- No deadline/timing claim (F-2 stays open), no device evidence, and the RAM1 headroom remains thin (48 B above the
  floor) as named in the previous slice.
- **Task 4.5 stays open** on the §13g half above.
