# G2 execution packet — 3.7: the handoff's closures are **emitted**, not dropped

Slice of tasks **3.7a** (last named increment before 3.7b) with tasks **4.5** (its cross-generation closure half) and
**3.14** (its "against the real handoff" clause). Written before any edit, per the
[G2 execution packet](../../../../Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice)
requirement; the parent packet's ladder is
[`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § *Slice 3.7b* ("then complete tasks 3.14 and 4.5 against the
real production handoff, with their interleaving, closure and recovery proofs").

## 1. Why this slice, and why now

3.7a I5 made the paged lane serve on the clock path and **named** what it unblocked: *"3.14's and 4.5's remaining clauses
(written against the real handoff) are the next slice"* ([`tasks.md` § 3.7a, increment I5](../../tasks.md)). Both tasks
name the **same** owed item in the same words — the emitted closure at the real handoff:

* 4.5 § OWED: *"production containment/recovery, actual emitted ordering, a moving closure at its correct tick,
  sustained-note behavior through an empty window, displayed provenance and eventual recovery through the real
  consumer"*;
* 4.5b § *"Task 4.5 stays OPEN: the **emitted order at the real handoff** (the clock-path caller that emits the
  replacement's note-ons) ... are the serving path's (3.7)"*;
* the spec requirement *"Cross-generation note closure is reconciled off-clock"* — *"notes removed or invalidated by the
  replacement SHALL be closed at handoff ... required note-offs SHALL be emitted **before** the replacement's note-ons"*
  ([`specs/…/spec.md` § Requirement](../../specs/windowed-playback-generation/spec.md)).

## 2. Scope

**The handoff in scope** is the one the production serving path already performs: `reconcilePlaybackLedgerAfterFullLoopRebuild`
(`src/Track/TrackPlaybackWindowBuild.cpp`, FLASH-placed), reached by every merged-window (re)build — the clock path's own
`buildMergedPlaybackFallbackCold` from `Track::playCommittedLoopMidi`, the launch/occupancy preparation
(`Track::ensurePlaybackMergedEventsForSlot`), the prepared-pair service and `Track::rebuildPlaybackOrder`.

**The defect, in the owner's own vocabulary:** that handoff already **computes** the invalidated open notes — tagged
ledger entries whose committed NoteOn is absent from the incoming full-loop stream — and then **drops the obligation
silently** (`ActiveNoteLedger::eraseOpenNotesMissingFromCommittedNoteOns` → `eraseAt`). Nothing reaches the wire, so a
note whose content the new revision removed stays **sounding** (the ledger no longer knows it) until a stop or a clear.
The replacement's own note-ons are not the repair: their identities are the *new* ones, and the ledger's identity-exact
NoteOff resolution refuses an unknown identity as an orphan, so the old sounding can never be closed by the new stream.

**The repair, at the same owner:**

1. the invalidation predicate is stated **once**, as a bounded allocation-free **plan** on the ledger
   (`ActiveNoteLedger::MissingCommittedNoteOnPlan` + `planOpenNotesMissingFromCommittedNoteOns`), and the existing
   `eraseOpenNotesMissingFromCommittedNoteOns` is re-expressed through it, so the owner that drops an obligation and the
   owner that closes it cannot disagree about which ones those are;
2. the handoff **emits** each planned closure on the MIDI wire (the same immediate-send owner the two existing closure
   paths use — `silenceSlotMidiOutput`, `applyClosureSettlementForSlot`), at **this handoff's tick**, and discharges each
   obligation through the ledger's **own event path** (`applyPlaybackEvent` with the plan entry's identity, i.e. the
   identity-exact resolution — a LIFO pop would discharge a *different* entry of the same pitch);
3. the interpretive rule the reconciliation already owns is applied to the **incoming stream's own emptiness**:
   `handoffAbsenceMeansRemoval(wholeLoop, streamIsEmpty)` — so an **event-empty** whole-loop window closes nothing and
   **drops** nothing (its obligations stay alive, the spec's "closure obligations SHALL stay alive").

**Ordering is structural, not a promise:** the emissions happen inside the handoff reconcile, which every caller runs
before the walk that emits the incoming revision's note-ons; the fixture asserts the wire order on one turn.

**Out of scope (named, not implied):** the pitch-addressed *re-issue* case (4.5b's `suppressedRetriggers`, where the same
pitch returns under a *new* identity — this slice closes the invalidated identity and lets the replacement sound at its
own onset); the *moving* closure whose note-off slid behind the re-anchored cursor; displayed provenance; the
between-grains undo/redo matrix; retained-source release; preview-session undo/redo; the device leg; and 3.7b's fallback
removal.

## 3. Reuse, and what is deliberately not added

* The **ledger** stays the owner of "which obligations are open" and of the identity predicate; the **Track** stays the
  owner of the wire. No new state: the plan is a value on the stack (≤128 entries × 4 B), and no size pin moves.
* `midiHandler.sendMidiEvent` — the owner `silenceSlotMidiOutput` and `applyClosureSettlementForSlot` already use — is
  the emission edge; `Track::sendMidiEvent` (with its playback-output gate, remap and sink) is **not** used, because the
  ledger's own channel is already the remapped one and the two existing closure paths emit unconditionally.
* **Not** `planClosureSettlementForSlot`: its key is `(channel, pitch)` read from the incoming events, and its applier
  discharges by a LIFO pop. The handoff's rule is *identity* addressed (the ledger's entries), and the guide records why
  the identity predicate deliberately does not consult the stream's channel/pitch
  ([`OVERDUB_LEDGER_NOTE_EVALUATIONS.md` § 8](../../../../Guides/OVERDUB_LEDGER_NOTE_EVALUATIONS.md)). The interpretive
  rule itself **is** reused (`handoffAbsenceMeansRemoval`).
* No new marker or Tier-A row: the ledger's existing `DIAG,ledger,erase` stays where a drop still happens, and the
  observable this slice needs is the MIDI wire (`MidiSendCapture`), which is the claim itself.

## 4. Transition oracle (pinned before the edits)

Driven through the production clock entry (`Track::playMidiEvents` → `Track::playCommittedLoopMidi` →
`buildMergedPlaybackFallbackCold` → the reconcile), on a one-bar slot with a **current published root** whose events
carry their identity (`value.noteId`, as the production publish path stamps them):

| # | Transition | Oracle |
|---|---|---|
| O1 | A full-loop handoff whose stream **lacks** an open tagged identity | exactly **one** NoteOff on the wire, for the entry's own pitch, in the handoff turn — and the ledger entry is gone |
| O2 | The same handoff whose stream **carries** another open tagged identity | **no** emission for it (no early NoteOff, no retrigger), and the entry stays open |
| O3 | An untagged open entry (`kInvalidNoteId`) across the same handoff | kept, with no emission (the existing rule: absence is not evidence) |
| O4 | The incoming stream's **own** replacement note-on, ahead of the handoff | the closure precedes it on the wire in the same turn |
| O5 | An **event-empty** whole-loop incoming stream | nothing emitted, and the tagged entries **stay** (not dropped) |
| O6 | After the handoff, the preserved note's own NoteOff arrives at its tick | emitted once, and it discharges that obligation (no second closure, no orphan) |

## 5. Fixtures and red-proofs

* **Fixture (added):** `test_handoff_emits_the_closures_it_used_to_drop` in
  `test/test_runtime_working_set_ownership/` — a two-root drive (R1 in, ledger opened by real emission + one
  pre-existing invalidated obligation + one untagged entry; R2 published as the replacement version), then the production
  clock path. Asserts O1–O6 on the wire and in the ledger.
* **Red-proofs (planned, each restored byte-identically and sha256-checked):**
  * **RP-A** the silent drop restored (the emission removed) → O1 fails (no NoteOff, entry dropped);
  * **RP-B** the closure emitted but the obligation **not** discharged → O1's ledger half fails;
  * **RP-C** the plan taken even when the incoming stream is empty (the interpretive rule dropped) → O5 fails.
  A mutation whose anchor is not unique, or whose restore is not byte-identical, is re-run — not recorded as evidence.

## 6. Exit criteria (what this slice must show before it is called landed)

1. focused `test_runtime_working_set_ownership` green, with the new fixture;
2. full `pio test -e native` green, no suite ERRORED/FAILED;
3. `pio run -e teensy41-capture-serial` **SUCCESS** through the RAM1 guard, with the measured line reported (the
   reconcile is already FLASH-placed, so **no ITCM growth is expected**: only the four call sites may carry one more
   argument);
4. the three red-proofs measured red with byte-identical restores;
5. the ledger guide's handoff section updated to the behaviour that now ships, and `tasks.md` recorded as an increment of
   3.7a/4.5/3.14 with everything still owed named.

## 7. Risks, and how they are bounded

* **Wire cut on the same track.** A closure for one slot's pitch can cut another *enabled* slot's voice on the same
  track channel. This is the **existing** property of the slot-scoped closure owner (`silenceSlotMidiOutput`, used by
  mute and by the last resort) and is a consequence of one MIDI channel per track; it is named in the evidence rather
  than re-designed here.
* **Cost.** The plan reuses the predicate the shipped code already runs on this path (the same nested scan), so no
  unbounded or new work is added; the emissions it adds are bounded by the open-note count (≤128).
* **Behavior change on the empty window.** Entries that the old code dropped are now kept. That is the spec's
  "an event-empty window means *no new events*" applied to the obligation set, and it is observable in the fixture.
