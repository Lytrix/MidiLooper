# G2 execution packet — 4.5: the closure that slid **behind** the re-anchored cursor

Slice of task **4.5** (its last named implementation item) carried on task **3.7a**'s serving handoff, and the
fixture the task's own verify clause asks for (*"a closure whose note-off moves across the handoff"*). Written
before any edit, per the [G2 execution packet](../../../../Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice)
requirement.

## 1. Why this slice, and why now

4.5's OWED list names *"a moving closure at its correct tick"*, and I6 — which landed the emitted closure at the
real handoff — closed the ahead side of that item while naming the other side owed in the same words: *"A closure
that slid **behind** the re-anchored cursor is out of scope and still owed to 4.5"*
([`../2026-10-06-s37-handoff-closures/EVIDENCE.md`](../2026-10-06-s37-handoff-closures/EVIDENCE.md) § 5,
[`RECONCILIATION.md`](../../RECONCILIATION.md) row *4.5 containment, last resort, closures*). Task **3.7a**'s text
requires 4.5 to be finished *"against the real handoff … including their production-path interleavings and
closure/recovery gates"* before it is checked, so this is the closure half of that gate.

## 2. The defect, in the walk's and the handoff's own vocabulary

At a version handoff the merged store is rebuilt from the incoming revision and the walk's cursor is **re-anchored
to the current playhead** (`reanchorPlaybackIndex` points `nextEventIndex` at the first event whose phase is
`> loop.lastTickInLoop`; the prepared pair resolves the identical term as its `publicationPhase`). From there the
cursor only moves forward, and `advancePlaybackCursor` **skips** an event whose phase does not cross the frame
(`evTick <= prevTickInLoop` → `cursor++`, no send). So an event behind that floor is not delivered again until the
pass restarts at the wrap.

The handoff's plan asks only one question — *does the incoming stream still carry this identity's NoteOn?* — so a
sounding whose identity survives the replacement while its **NoteOff moved behind the traversal position** is
**not** planned: nothing is closed on the wire, and the walk cannot reach that note-off for the rest of the pass.
The sounding therefore outlives the content that superseded it (up to a whole pass), and the wrap re-serves the
note from its onset — a re-articulation of a note the replacement had already ended.

**The decision this slice takes** (preferred path, confidence high): the handoff owes that closure, exactly as it
owes one whose NoteOn the replacement removed, and emits it **at the handoff's own tick** — the same treatment, the
same owner and the same ordering I6 established. The rejected alternative is to leave the obligation alive and let
the next pass deliver it at its own tick: that keeps a sounding audible past the end its own revision gives it and
re-articulates it at the wrap, i.e. it serves content the revision no longer contains, and it needs a retrigger
suppression the walk does not have. No new state, no second authority, no DEC: the change is a **refinement of the
predicate the handoff already owns** (which obligations the incoming full-loop stream cannot discharge), inside the
owner that already states it.

## 3. Scope

**In scope:** the merged-window handoff (`reconcilePlaybackLedgerAfterFullLoopRebuild`) and its **four** call sites — the
clock path's `buildMergedPlaybackFallbackCold`, the launch/occupancy preparation
(`Track::ensurePlaybackMergedEventsForSlot`), the prepared-pair service and `Track::rebuildPlaybackOrder` — so every
merged-window handoff carries the same floor. The slice's fixture **measures the clock path only**; the other three are
code-shape claims covered by the shared native run (recorded in the evidence, not claimed as measured).

**Out of scope (named, not implied):** the windowed lane's own serving handoff (3.7a/3.14's publication path, not
yet the serving path); displayed provenance (the display's, `DISPLAY-CONSUMER-RECONCILIATION.md` → RWO **S4e**);
eventual recovery through the real consumer (containment's production caller — `advancePlaybackLagTraversal` has
none yet); the device leg (6.4/6.5); 3.7b's fallback removal.

## 4. Reuse, and what is deliberately not added

* The **ledger** stays the owner of "which obligations are open" and of the identity predicate; the **Track** stays
  the owner of the wire. No new state, no new marker, no new Tier-A row: the observable is the MIDI wire.
* The plan's value type (`ActiveNoteLedger::MissingCommittedNoteOnPlan`) is **renamed** with the predicate it
  carries (`HandoffClosurePlan`, `planHandoffClosures`), because it is no longer only "missing NoteOn": one scan
  answers both halves, so the owner that plans, the owner that closes and the owner that drops cannot disagree.
  The structural half stays reachable by name for callers that owe no wire half
  (`planOpenNotesMissingFromCommittedNoteOns`, `eraseOpenNotesMissingFromCommittedNoteOns`), which are now the
  union at `servedThroughPhase == UINT32_MAX` — i.e. the same predicate with no traversal yet.
* The **floor** is the walk's own term, not a new coordinate: `reanchorPlaybackIndex`'s threshold
  (`loop.lastTickInLoop`), promoted to "the pass restarts, so nothing is behind" by the walk's own wrap test
  (`didDisplayPlayheadWrapBackward`) and its fresh-origin case (`lastTickInLoop == UINT32_MAX`). The pair path
  resolves the identical term as its `publicationPhase`, which is why one helper serves all three sites.
* `closeHandoffInvalidatedNotes` keeps the emission edge (`midiHandler.sendMidiEvent`), the order (send, then
  discharge by identity through `applyPlaybackEvent`) and the stamp (the handoff's own tick).

## 5. Transition oracle (pinned before the edits)

Driven through the production clock entry (`Track::playMidiEvents` → merged rebuild → the handoff reconcile), on a
slot with a **current published root** whose events carry their identity, walked with the output disabled so the
ledger is populated by the walk itself:

| # | Transition | Oracle |
|---|---|---|
| O1 | A sounding whose replacement keeps its identity and moves its NoteOff **behind** the traversal position | exactly **one** NoteOff on the wire for that pitch, in the handoff turn, stamped with the handoff's tick, and the entry is gone |
| O2 | Same handoff, a sounding whose NoteOff is still **ahead** of the traversal position | **no** handoff closure; its own note-off arrives once at its own tick and discharges it |
| O3 | A never-ending sounding (identity present, **no** NoteOff in the stream) | **no** closure (absence of a closure is not evidence of ending) |
| O4 | An **untagged** open entry across the same handoff | kept, with no emission |
| O5 | The replacement's own note-ons in the handoff turn | every handoff closure precedes them on the wire |
| O6 | A handoff on a turn where the **pass restarts** (fresh origin or wrap) | nothing is planned: the whole pass is served again, so every closure is reachable — **on the clock path, where the reconcile turn is the serving turn**; on the off-clock sites the wrap is judged at the reconcile's own turn, which the evidence names as a limit |
| O7 | The next pass after O1's closure | the note re-articulates from its own onset and closes exactly once at its own tick (never a second closure for the same pass) |

## 6. Fixtures and red-proofs

* **Fixture (added):** `test_handoff_closes_a_closure_that_slid_behind_the_re_anchored_cursor` in
  `test/test_runtime_working_set_ownership/` — two roots (R1 opens the soundings through the walk; R2 keeps one
  identity with its closure behind the traversal position, keeps another with its closure ahead, adds a
  replacement note) plus an untagged sounding, then the wrap turn for O6 and the following pass for O7.
* **Red-proofs (planned, each restored byte-identically and sha256-checked):**
  * **RP-A** the closure half of the predicate dropped (the plan returns the structural set only) → O1 fails
    (nothing on the wire, the sounding survives the handoff);
  * **RP-B** the floor always taken (`UINT32_MAX` replaced by `0`) → O2/O6 fail (a closure the stream will still
    deliver is closed early, and a restarted pass closes one it must not);
  * **RP-C** the "no NoteOff in the stream" guard dropped → O3 fails (a never-ending sounding is silenced).
  A mutation whose anchor is not unique, or whose restore is not byte-identical, is re-run — not recorded.

## 7. Exit criteria

1. focused `test_runtime_working_set_ownership` green, with the new fixture;
2. full `pio test -e native` green, no suite ERRORED/FAILED;
3. `pio run -e teensy41-capture-serial` **SUCCESS** through the RAM1 guard, with the measured line reported (the
   handoff is FLASH-placed, so the added work must not move ITCM);
4. the three red-proofs measured red with byte-identical restores;
5. `OVERDUB_LEDGER_NOTE_EVALUATIONS.md`'s handoff table updated to the behaviour that now ships, and `tasks.md`
   recorded as an increment of 4.5/3.7a with everything still owed named.

## 8. Risks, and how they are bounded

* **Over-closing (the dangerous direction).** The predicate fires only for an Entry the ledger holds **open** and
  whose own revision carries a **present** NoteOff behind the floor, so a slot that is not serving (no open
  Entries) plans nothing; the floor is the walk's own term, so on the clock path the handoff cannot decide reachability
  differently from `advancePlaybackCursor`; and a wrapping or fresh turn plans nothing at all. RP-B measures the bound.
  **The plan is computed against the merged window**, so where a slot is served by another lane (bridge, fixed view,
  paged ring) the reconcile's "reachable" answer is about the incoming full-loop content, not about that lane's stream —
  the same exposure I6's invalidation rule has, and named rather than claimed away.
* **Cost.** One scan per open Entry over the incoming stream with two flags — the same nested shape the shipped
  structural prune already ran on this path, and it replaces it rather than adding to it. The predicate **exits early**
  once an Entry is settled as present-with-a-reachable-closure (`noteOnPresent && closureReachable` cannot flip), which is
  the common shape, so the handoff turn does not pay the full open-notes x stream product for it; no measurement of that
  exit is claimed (it cannot change any outcome by construction).
* **The off-clock call sites.** The launch/occupancy preparation, the pair service and `Track::rebuildPlaybackOrder` are
  covered by the shared native run only, and their device behaviour is not claimed (as I6 recorded for the same sites).
  Their turn is not the serving turn, so the floor they pass is the loop's position **at the reconcile**, and their
  fresh-origin anchor differs from `reanchorPlaybackIndex`'s (the pair seeds at the first event with phase > 0); both are
  named limits rather than claimed coverage.
