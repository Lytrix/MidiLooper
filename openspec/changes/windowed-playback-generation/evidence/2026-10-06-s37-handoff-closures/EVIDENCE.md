# 3.7 — the handoff's closures are **emitted**, not dropped (firmware + tests)

Slice of task **3.7a** (the last increment named before 3.7b) carrying the shared owed item of tasks **4.5** (its
cross-generation closure half, § 13(g)) and **3.14** (its "against the real handoff" clause). Packet:
[`EXECUTION.md`](EXECUTION.md). Commit: `3.7a handoff closures — the invalidated soundings are closed on the wire, and an
event-empty replacement drops nothing`.

## 1. What was wrong, measured in the owner's own vocabulary

`reconcilePlaybackLedgerAfterFullLoopRebuild` (FLASH-placed, `src/Track/TrackPlaybackWindowBuild.cpp`) already computed the
invalidated obligations of a version handoff — tagged open ledger entries whose committed NoteOn is **absent** from the
incoming full-loop stream — and then dropped them **without a wire event**
(`ActiveNoteLedger::eraseOpenNotesMissingFromCommittedNoteOns` → `eraseAt`). Two consequences, and the second is why the
replacement's own note-ons cannot repair the first:

* the note whose content the incoming revision removed stayed **sounding** (the ledger no longer knew it), so nothing
  closed it until a stop, a mute or a clear;
* the replacement's own NoteOff carries the **new** identity, and the ledger's identity-exact NoteOff resolution refuses
  an unknown identity as an orphan (`applyPlaybackEvent` → `findIndexByNoteId` → `return false`) — so the new stream
  could never close the old sounding either.

The spec's requirement is the opposite: *"notes removed or invalidated by the replacement SHALL be closed at handoff …
required note-offs SHALL be emitted before the replacement's note-ons"*, and *"closure obligations SHALL stay alive
independently of view-buffer retirement"*
([`spec.md` § Cross-generation note closure is reconciled off-clock](../../specs/windowed-playback-generation/spec.md)).

## 2. What landed

1. **The predicate, stated once.** `ActiveNoteLedger::MissingCommittedNoteOnPlan` — bounded by `kMaxOpenNotes`, i.e.
   **1,024 B on the stack** (128 Entries × 8 B: channel + pitch + `uint32_t` identity + padding), nothing allocating —
   with `planOpenNotesMissingFromCommittedNoteOns(events, count)` and `erasePlannedOpenNotes(plan)`; the shipped
   `eraseOpenNotesMissingFromCommittedNoteOns` is now exactly the two of them in sequence, so the owner that *drops* an
   obligation and the owner that *closes* it cannot disagree about the set. Untagged entries are never planned (their
   absence is not evidence), and channel/pitch are not consulted — the stream's own `value.noteId` decides, which is the
   rule [`OVERDUB_LEDGER_NOTE_EVALUATIONS.md` § 8](../../../../Guides/OVERDUB_LEDGER_NOTE_EVALUATIONS.md) records.
2. **The wire half.** `closeHandoffInvalidatedNotes(ledger, plan, tick)` (FLASH, file-static) sends each planned closure
   through the same immediate-send owner the two existing closure paths use (`midiHandler.sendMidiEvent`;
   `silenceSlotMidiOutput`, `applyClosureSettlementForSlot`) and **then** discharges the obligation through the ledger's
   **own** event path, with the entry's own `(channel, pitch, noteId)` — the identity-exact resolution, not a LIFO pop, so
   two entries sharing a pitch cannot be confused. **The order is the existing closure owner's** (`applyClosureSettlementForSlot`
   sends first): discharging first would throw the obligation away before the wire had its chance. The discharge is what
   removes the Entry, so a second closure for the same identity is impossible — a later plan cannot contain it, and a later
   note-off carrying that erased identity is refused as an orphan — which is the spec's *"closures that became obsolete
   SHALL be suppressed"* holding **by construction** rather than by a guard branch. The closure is stamped with **the
   handoff's own tick** (a new `currentTick` parameter, threaded from all four call sites, each of which already had it in
   scope).
3. **The interpretive rule at the handoff.** The reconciliation now asks the rule it already owns —
   `handoffAbsenceMeansRemoval(wholeLoop, incomingIsEmpty)` — before taking any plan: an **event-empty** whole-loop
   replacement closes nothing and **drops nothing**, so those obligations stay alive instead of being forgotten (they are
   discharged later, at a boundary that carries content). A **slice** (non-full-loop window) already returned early and
   still returns early.
Ordering is structural: every caller runs the reconciliation before the walk that serves the incoming revision's note-ons
(`buildMergedPlaybackFallbackCold` from `Track::playCommittedLoopMidi`, `Track::ensurePlaybackMergedEventsForSlot`, the
prepared-pair service, `Track::rebuildPlaybackOrder`).

## 3. Verification

* **Focused** `test_runtime_working_set_ownership` **162/162** (baseline 161; the new fixture is the +1).
* **Full native** `pio test -e native` **2,840 cases / 7 skipped / 2,833 succeeded / 0 failed** (baseline 2,839 / 2,832;
  no suite ERRORED or FAILED).
* **Capture build** `pio run -e teensy41-capture-serial` **SUCCESS** through the RAM1 guard:
  `variables 96,192 / ITCM 393,216 (code 392,248 + padding 968) / locals 34,880`, floor 840 → 128 B of headroom. The line
  is **byte-identical to the pre-slice baseline** (`392,248 + 968`, the S4e.9 record in `PROJECT_STATE.md`): the
  reconciliation is already FLASH-placed, so closing on the wire cost **no ITCM** and **no budget re-base**. Not flashed.
* `git diff --check` clean; `openspec validate windowed-playback-generation --strict` valid.
* **Red-proofs, each restored byte-identically and with the slice's own diff reproduced exactly** (sha256-checked copy
  restore; the harness refuses to mutate unless the anchor occurs exactly once and the file is at the recorded revision):

| id | mutation | measured |
|---|---|---|
| **RP-A** | the silent drop restored (plan + emission replaced by `eraseOpenNotesMissingFromCommittedNoteOns`) | **RED** — `and it is the handoff's first wire event -- not the replacement's content` |
| **RP-B** | the closure emitted but the obligation never discharged (the `applyPlaybackEvent` line removed from the helper) | **RED** — `the invalidated obligation is discharged, not forgotten` |
| **RP-C** | the interpretive rule dropped (empty incoming stream treated as evidence of removal) | **RED** — `Expected 0 Was 2. an event-empty incoming window silences nothing` (two tagged soundings would have been closed by an empty replacement) |

**Independent review (one pass, code-reviewer).** The slice was reviewed against its contract before landing; the one
concrete defect it found was the closure's **order** — the obligation was discharged before the NoteOff reached the
immediate-send edge, the reverse of the existing closure owner (`applyClosureSettlementForSlot` sends first) — and it is
fixed above. Its other findings are recorded rather than smoothed: the emission is the only one on this path that does not
go through `Track::sendMidiEvent`'s gates (with the delivery-confirmation limit named in § 5), the "never a second closure"
property holds by the **erase** rather than by a guard branch (so the wording above was corrected), the fixture now asserts
the closure's channel, and the plan's recorded size was corrected to its real 1,024 B.

## 4. The fixture, and exactly what it measures

`test_handoff_emits_the_closures_it_used_to_drop` drives the **production clock entry** (`Track::playMidiEvents` → the
merged-window rebuild → the handoff reconcile) over a two-root version handoff, with the roots installed through the
existing test seam (`Loop::storePublishedSnapshotForTest`, the helper stamps each event's `noteId` the way
`LoopSnapshotPublish.cpp` does, because the rule is identity addressed):

* **R1** — a sustained note at 48 (id 2000) and one at 55 (id 2001); walked with the output disabled so the *ledger* is
  populated by the walk itself, and the fixture asserts the open identities really are R1's;
* **R2** — keeps 48 under its own identity (its closing note-off moves to an earlier tick), removes 55, and adds a note at
  62 whose onset falls inside the handoff turn's own window;
* an **untagged** sounding (pitch 67) is marked alongside them;
* a final event-empty root is published while one further tagged obligation is open.

Measured: the closure goes out for **the invalidated sounding's own pitch**, on **the track's own playback channel**
 (asserted — the code's "the ledger's channel is already remapped" is not taken on faith), at **the handoff's own tick**,
 and is **the first wire event of that turn** (every closure precedes the replacement's first note-on in the driven
 sequence); the invalidated obligation is discharged; the unchanged sounding receives **no early NoteOff and no
 retrigger**, and its own note-off arrives **exactly once** at its own tick and discharges it; the untagged entry
 survives; a second closure for the same pitch never fires; and the event-empty replacement emits **nothing** while both
 obligations stay alive.

## 5. Named, not claimed

* **The pitch-addressed re-issue case is not this rule.** 4.5b's `ClosureSettlement` keys on `(channel, pitch)` and
  *suppresses* a retrigger when the replacement re-issues the pitch; this slice keys on the **identity** the ledger holds,
  which is the predicate the shipped prune already used (and which deliberately does not consult the stream's channel —
  the ledger's channel is the track's remapped one). Where a version handoff re-creates the **same pitch under a new
  identity** and that new identity's onset lies behind the re-anchored cursor, the old sounding is now closed (no stuck
  note — strictly better than the silent drop) and re-sounds at the next boundary rather than continuing seamlessly.
  Re-tagging a preserved obligation onto a replacement identity is **owed**, not done here.
* **The moving closure is only covered on the provable side.** A note whose note-off moves *earlier but still ahead* of the
  handoff is measured here (48's off moved from 700 to 300 and arrived once). A closure that slid **behind** the
  re-anchored cursor is out of scope and still owed to 4.5.
* **Off-clock preparation paths are not device-measured.** The reconcile also runs on the launch/occupancy preparation and
  the prepared-pair service; the fixture drives the clock path, and the other call sites are covered by the shared
  full-native run only.
* **The slot-scoped cut is pre-existing and named.** A closure for one slot's pitch can cut another *enabled* slot's voice
  on the same track channel (one MIDI channel per track). This is the property of the existing slot-scoped closure owner
  (`silenceSlotMidiOutput`, used by mute and the last resort) and is not re-designed here.
* **No device leg.** The behavior is observable on the wire, but no registered stimulus drives a note sounding across a
  content-removing version handoff, so no HITL run is claimed; the device leg stays owed with 6.4/6.5.
* **No timing claim**, no `ExecutionBounds.md` row change, no displayed provenance, no between-grains undo/redo matrix, no
  retained-source release, no preview-session undo/redo, no fallback removal (3.7b).
* **Task states:** 3.7a stays open (3.7b's removal conditions are unchanged); 4.5 stays open on the moving-closure case and
  displayed provenance; 3.14 stays open on the publication-owner requester, the between-grains matrix, retained-source
  lifetime, displayed provenance and preview-session undo/redo. This increment closes the *emitted-closure-at-the-real-
  handoff* item all three shared.

* **The immediate-send edge has no delivery confirmation — inherited, not introduced.** `MidiHandler::sendMidiEvent` is
  best-effort: a packet the USB/serial TX path cannot take is dropped silently, and the obligation is already discharged by
  then, so a drop during a saturated buffer still leaves that note sounding (there is no channel on this edge to ask whether
  it went out). Both existing closure owners share this exactly (`silenceSlotMidiOutput` does not even discharge;
  `applyClosureSettlementForSlot` sends then discharges), so the slice adopts the owner's order and names the limit rather
  than inventing delivery semantics on the clock edge.
* **The closure does not consult the slot's output gate.** It goes to `midiHandler` directly, so a slot whose output is
  currently suppressed (muted via `TrackManager::setSlotMuted`, which silences through `silenceSlotMidiOutput` and
  deliberately keeps its ledger Entries) can still emit an unmatched NoteOff on the track's channel — which can cut another
  voice at the same pitch (live input echo, or a sibling slot of the same track). That is the pre-existing slot-scoped
  closure model (one MIDI channel per track, the same exposure `silenceSlotMidiOutput` has on a mute toggle); a gate that
  distinguishes "this slot is being served" from "its output is enabled" needs an owner-level contract and is **owed**.
* **A transient gather miss looks like an event-empty replacement.** The merged store is also emptied when the gather refuses
  (`GatherMissHoldEmpty`), so during such a miss a handoff closes nothing and drops nothing — the deliberate side of rule 3
  (the obligations survive), recorded so it is not read as a coverage claim.
* **Inherited predicate limit:** the plan consults `noteId` only, so a version handoff that moves a note's identity to a
  **different pitch** leaves the old pitch's open Entry judged "present", closes nothing, and that sounding survives the
  handoff (the old code left it open too — this slice neither fixes nor worsens it).
* **Cost, recorded:** the plan is a **1,024 B** value on the rebuild turn's stack (Flash-placed code, so no ITCM and no RAM1
  variable cost; the guard's line is unchanged). A streaming plan would remove the frame and is not needed here.
