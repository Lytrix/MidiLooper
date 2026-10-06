# 4.5 — the closure that slid **behind** the re-anchored cursor is closed at the handoff (firmware + tests)

Slice of task **4.5** (its last named implementation item) carried on task **3.7a**'s serving handoff, and the fixture
that 4.5's own verify clause asks for (*"a closure whose note-off moves across the handoff"*). Packet:
[`EXECUTION.md`](EXECUTION.md). Commit message: *4.5 moving closure — the closure whose own tick slid behind the
re-anchored cursor is closed at the handoff*.

## 1. What was wrong, measured in the walk's and the handoff's own vocabulary

The handoff's plan asked one question — *does the incoming stream still carry this identity's NoteOn?*
(`ActiveNoteLedger::planOpenNotesMissingFromCommittedNoteOns`) — while the walk resumes from a **floor**: at a version
handoff the merged store is rebuilt and `reanchorPlaybackIndex` points `nextEventIndex` at the first event whose phase is
`> loop.lastTickInLoop` (the prepared pair resolves the identical term as its `publicationPhase`), and
`advancePlaybackCursor` then **skips** every event at or behind it (`evTick <= prevTickInLoop` → `cursor++`, no send) for
the rest of the pass. So a sounding whose identity the replacement **kept** while moving its NoteOff behind that floor was
neither planned (its On is present) nor reachable: the sounding outlived the content that superseded it, and the wrap
re-articulated the note from its own onset.

Measured before the fix, on the new fixture's production drive: the handoff turn emitted **no closure at all** — the
fixture failed at *"and it is the handoff's first wire event -- not the replacement's content"*, with the plan empty and
the sounding still open in the ledger.

## 2. What landed

1. **One predicate, one scan.** `ActiveNoteLedger::HandoffClosurePlan` (renamed from `MissingCommittedNoteOnPlan`, its
   docs restated) and `planHandoffClosures(events, eventCount, servedThroughPhase, loopLength)`: for each tagged open
   Entry it records whether its identity has a NoteOn in the stream and whether **any** of its NoteOffs is still
   reachable, and plans the Entry when `!noteOnPresent || (closurePresent && !closureReachable)` — with an **early exit**
   once an Entry is settled as `noteOnPresent && closureReachable` (the common shape, which cannot flip afterwards), so
   the handoff turn does not pay the full open-notes x stream product for it. Costs no new state, no marker and no row;
   `erasePlannedOpenNotes` / `planOpenNotesMissingFromCommittedNoteOns` are the same predicate with
   `servedThroughPhase == UINT32_MAX` (nothing served → only the structural half can fire), which is what the drop-only
   callers that owe no wire half keep calling.
2. **The floor is the walk's own term.** `Track::playbackClosureServeFloor` (FLASH-placed) answers
   `loop.lastTickInLoop`, or `UINT32_MAX` when there is no position yet **or** the playhead wrapped backward
   (`didDisplayPlayheadWrapBackward`) — the pass starts or restarts, so the walk serves that whole pass again and every
   closure is reachable. It is threaded into `reconcilePlaybackLedgerAfterFullLoopRebuild(loop, runtime, currentTick,
   servedThroughPhase)` from all four call sites (the clock path's `buildMergedPlaybackFallbackCold`, the
   launch/occupancy preparation, the prepared-pair service and `Track::rebuildPlaybackOrder`).
3. **The wire half is unchanged.** `closeHandoffInvalidatedNotes` → `closePlannedHandoffClosures`: same immediate-send
   edge (`midiHandler.sendMidiEvent`), same order (send, **then** discharge by identity through `applyPlaybackEvent`),
   same stamp (the handoff's own tick), and the ordering stays structural (every caller reconciles ahead of the walk
   that serves the replacement's note-ons).

## 3. Verification

* **Focused** `test_runtime_working_set_ownership` **175/175** (baseline 174/174, measured with the slice stashed).
* **Full native** `pio test -e native` **2,854 cases / 7 skipped / 2,847 succeeded / 0 failed** (baseline measured with
  the slice stashed: **2,853 / 7 / 2,846 / 0**; no suite ERRORED or FAILED).
* **Capture build** `pio run -e teensy41-capture-serial` **SUCCESS**, and the RAM1 line is **byte-identical A/B**:
  `variables 96,192 / ITCM 393,216 (code 392,248 + padding 968) / locals 34,880`, floor 840 → 128 B of headroom. The
  predicate and the floor are FLASH-placed, so the increment cost **no ITCM** and needed **no re-base**; the four call
  sites' extra argument moved nothing.
* `git diff --check` clean, `openspec validate windowed-playback-generation --strict` valid, `git diff --stat` limited
  to the six files the slice touches. **Not flashed.**

## 4. The fixture, and exactly what it measures

`test_handoff_closes_a_closure_that_slid_behind_the_re_anchored_cursor` drives the **production clock entry**
(`Track::playMidiEvents` → the merged rebuild → the handoff reconcile) over a **two-bar** slot (the committed pass's own
length) with roots installed through the existing seam (`Loop::storePublishedSnapshotForTest`, identities stamped as
`LoopSnapshotPublish.cpp` stamps them):

* **R1** opens three soundings through the walk itself (the walk is driven with the output disabled, so the ledger — not
  the fixture — opens them), one of them under an identity the fixture also marks **untagged** (67);
* **Drive A** — a mid-pass handoff at tick 256, i.e. with the floor at 128: 48's closure moved to **100** (behind it),
  55's to **400** (ahead), 57's revision carries an On and **no** closure at all, and the replacement sounds at 256
  inside the handoff turn's window;
* **Drive B** — the same shape, but the handoff lands on the turn that **restarts the pass** (the wrap), where every one
  of those closures lies behind the *pre-wrap* position and must **not** be closed.

Measured: the slid closure is the handoff turn's **first wire event**, for 48's own pitch, on the track's playback
channel, **stamped at the handoff's tick (256)**, and it is the only closure in that turn for it (one, never two); the
obligation is discharged rather than left sounding; the closure the walk will still deliver (55) is **not** closed early
and arrives exactly once at its own tick; the never-ending sounding (57) is **never** silenced — not at the handoff and
not later in the pass; the untagged Entry survives; every closure precedes the replacement's note-ons; the restart-turn
handoff closes **nothing** (zero NoteOffs on the wire) and leaves its stand-in sounding open; and the restarted pass then
delivers each closure exactly once, with the handoff-closed note re-articulating from its own onset.

The fixture pins its own premise in **both** drives (`isFullLoopMergedPlaybackWindow` after each handoff turn) and reads
the never-ending shape back out of the rebuilt stream (one On, zero Offs for that identity), so neither drive can pass
vacuously if a future lane change makes the merged store a slice.

## 5. Red-proofs, each restored byte-identically and sha256-checked

| id | mutation | measured |
|---|---|---|
| **RP-A** | the closure half dropped (`undeliverable = !noteOnPresent`, `include/ActiveNoteLedger.h`) | **RED** — *"...and it is the handoff's first wire event -- not the replacement's content"* (nothing on the wire; the sounding survives the handoff) |
| **RP-B** | the restart clause dropped (`playbackClosureServeFloor` returns `loop.lastTickInLoop` unconditionally, `src/Track/TrackPlaybackWindowBuild.cpp`) | **RED** — *Expected 0 Was 1. a restarted pass leaves every closure to the walk -- the floor is not the pre-wrap position there* |
| **RP-B'** | the floor dropped (`return 0;`) | **RED** — *"...and it is the handoff's first wire event..."* (every behind closure becomes "ahead") |
| **RP-C** | the "no closure in the stream" guard dropped (`!noteOnPresent \|\| !closureReachable`) | **RED** — *Expected 0 Was 1. a sounding whose revision carries no closure is never silenced by absence* |

Restores verified by sha256 against the pre-mutation copies (`ActiveNoteLedger.h` `e9ba102d230e4798…`,
`TrackPlaybackWindowBuild.cpp` `b6b58270d33665f2…`), and the focused suite re-run green after each restore.

## 6. Recorded rather than smoothed over

* **The packet's RP-B is two mutations, not one.** It predicted *"the floor always taken (`UINT32_MAX` replaced by
  `0`)"*; measurement separated two load-bearing pieces — the **restart clause** (a pre-wrap position must not be
  mistaken for a traversed one) and the **floor's presence** (a zero floor makes every closure "ahead") — so both are
  red-proved as `RP-B` and `RP-B'`.
* **One focused independent review was run, and it moved three things.** The reviewer's concrete findings: the scan had
  **lost the structural prune's early exit** (the common shape now costing the full open-notes × stream product) — fixed
  in the predicate; **Drive B pinned no premise**, so its two assertions could go vacuous — fixed; and the plan's
  **"1,024 B" was a lower bound** (plus `count`, two live copies) — corrected. Its other findings are recorded as named
  limits above rather than fixed here: the plan answers about the **merged window** (not another lane's stream), the
  off-clock sites judge the wrap at their own turn, and the pair's fresh-origin anchor differs from
  `reanchorPlaybackIndex`'s at phase 0 (pre-existing). No finding contradicted O1/O2/O4/O5/O7 as measured.
* **The first fixture draft used the change's long-loop length and measured nothing.** With a 49,152-tick slot
  `shouldAvoidFullVisualRebuild` is true, the merged store is a **two-bar slice** and the reconcile returns early — the
  fixture's own premise assert caught it (*loopLength=49152 windowStart=0 windowLength=1536*), and the fixture now uses
  the committed two-bar length. That measurement is the reason the guide's handoff table says *"windowed gather — skip
  the handoff entirely"* is a real reach limit, not a formality: **the handoff reconcile plans over full-loop windows
  only**, so a demoted long slot's serving handoff belongs to the windowed lane (3.7a/3.14), exactly as the packet
  scoped it.
* **The snapshot helper needed one real fix.** A revision that carries an On and **no** closure cannot put its note
  record's end at `loopLengthTicks` and must not emit an Off event at that tick (`LoopSnapshotConstruction` rejects it:
  the first draft failed at `construction.step`), so the helper now ends such a note at `loopLengthTicks - 1` and
  **skips** its Off event. The fixture asserts the resulting shape from the rebuilt stream itself (one On, zero Offs for
  that identity), so the O3 case cannot be vacuous.
* **The O3 shape is constructed, and named as such.** A loop's own content normally closes its notes; the
  never-ending-sounding case exists to make the *guard* load-bearing ("the absence of a closure is not evidence"), and
  the fixture states that it is a constructed shape rather than a production route.

## 7. Named, not claimed

* **No device leg.** The behaviour is audible on the wire, but no registered stimulus drives a sounding across a
  content-moving version handoff, so no HITL run is claimed; the device leg stays owed with 6.4/6.5. **No timing
  claim**, no `ExecutionBounds.md` row change, no fallback removal (3.7b), no selection or subscription change.
* **The off-clock call sites are covered natively only, and two of their properties are named limits.** The
  launch/occupancy preparation, the prepared-pair service and `Track::rebuildPlaybackOrder` receive the same floor (the
  loop's own position), and their device behaviour is not claimed — the limit I6 recorded for the same sites. On top of
  that: (a) those turns are **not** the serving turn, so the wrap test is judged at the reconcile rather than at the pass
  the walk restarts, and a pre-wrap reconcile there can close for a pass the next turn replays from its head; and (b) at a
  **fresh origin** the pair's own anchor is the first event with phase > 0 (`publicationPhase`), while
  `reanchorPlaybackIndex` serves phase 0 — so an obligation whose incoming NoteOff sits at phase 0 is unreachable for the
  pair-served pass and this predicate does not plan it. That divergence is **pre-existing** in the pair path (it never
  adopted phase-0 rows), it needs content whose closure lands exactly at a loop boundary, and it is recorded rather than
  fixed here.
* **The windowed lane's serving handoff is untouched.** For a long/demoted slot the merged store is a slice and the
  reconcile returns early; the lane's own handoff (and its closures) remains 3.7a/3.14's item, which is where the
  long-loop version of this defect lives.
* **A stalled clock is conservative.** When the turn's phase is not ahead of the floor but the playhead did not wrap,
  the floor is still taken and a behind closure is still planned; when the turn is a wrap/fresh turn the plan is empty
  (measured by RP-B). Nothing is planned outside a rebuild turn, so a duplicate same-tick turn that both rebuilds and
  cannot advance leaves the obligation to the next handoff — named, not claimed as covered.
* **The plan is computed against the merged window.** Where a slot is served by another lane (bridge, fixed view, paged
  ring) the "reachable" answer describes the incoming full-loop content, not that lane's stream — the same exposure I6's
  invalidation rule carries, and named rather than claimed away. The predicate's **size** is a lower bound only: 128 × 8 B
  of Entries **plus** `count`, and two live copies can coexist (the ledger's local and the caller's) — so it is ~1,03x B
  per copy, and the byte-identical RAM1 line bounds code/static, not the clock-path stack (no timing claim).
* **Inherited limits, unchanged:** the immediate-send edge has no delivery confirmation (`MidiHandler::sendMidiEvent` is
  best-effort) and the closure does not consult the slot's output gate; both are properties of the existing
  slot-scoped closure owner that I6 recorded. `closePlannedHandoffClosures` also bypasses `Track::sendMidiEvent`'s
  capture-echo guard, so a closure emitted while a slot is overdubbing is not suppressed from that capture — inherited by
  `silenceSlotMidiOutput`, named here rather than newly introduced. The plan still consults `noteId` only (a handoff that
  moves an identity to a different pitch is neither fixed nor worsened).
* **4.5 stays open** on the items this slice does not touch: **displayed provenance** (the display's, per
  [`DISPLAY-CONSUMER-RECONCILIATION.md`](../../DISPLAY-CONSUMER-RECONCILIATION.md) → RWO **S4e**) and **eventual
  recovery through the real consumer** (`Track::advancePlaybackLagTraversal`, the containment latch's own entry, still
  has **no production caller** — `RECONCILIATION.md` row 4.5 records it, and 3.7a's checkoff names the closure/recovery
  gates). **3.7a stays open** (3.7b's conditions are unchanged); **3.14** is untouched by this slice.
