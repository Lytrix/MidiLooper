## Context

Motivation and scope live in `proposal.md`; the decisions below are recorded as **DEC-108**, and the trigger analysis
as this directory's `ARCHITECTURE-REASSESSMENT.md`. This section only grounds the approach in today's code. Every row
was read at HEAD (`feature/runtime-working-set-ownership`).

| element | where it is today |
|---|---|
| the playback window build | `Track::ensurePlaybackMergedEventsForSlot` (`src/Track/TrackPlaybackWindowBuild.cpp:700`), reached from the `SlotProvision` turn, `src/MidiButtonActions.cpp:59`, `src/main.cpp:234` |
| **the three consumption paths** | `src/Track/TrackPlaybackHotPath.cpp:287-314`: a **prepared pair** (`resolvePreparedPairConsume` against `{playbackRevision, playbackGeneration, publishedSnapshotIdentity}`), a **fixed view/bridge**, and the **merged window** fallback (`useMergedFallback = fixedView == nullptr && livePair == nullptr`) |
| the window it builds | whole-loop (`windowStartTick = 0`, `windowLengthTicks = loopLengthTicks`) for a short loop; **two bars centred on the playhead** for a demoted loop (`kMergedMidiEventsGatherBars = 2`, `TrackPlaybackWindowBuild.cpp:260-275`) |
| the demotion predicate | `Loop::shouldAvoidFullVisualRebuild` (`src/Loop/LoopMaterialization.cpp:596`) = `loopLength > DisplayWindowUtils::kMaxDetailedWindowBars (16) * Config::TICKS_PER_BAR`; `fullTakePlaybackViewAdmissible()` (`include/Loop.h:290`) is its complement and is what RC-MEM-2 uses to drop bridges, their bases and the prepared pair |
| the gather | `Loop::tryGatherPublishedSnapshotEventsInWindow` (`include/Loop.h:967`, definition `src/Loop/LoopSnapshotPlayback.cpp:171`) and its `…WithCapture` wrapper (`src/Loop/LoopMaterialization.cpp:586`); paths `None` / `Snapshot` / `Overlay` |
| readiness as it exists | `Track::isPlaybackMergedMidiEventsReadyForSlot` (`TrackPlaybackWindowBuild.cpp:899`): committed passes, non-zero length, an allocated runtime, `builtFromRevision == loop.playbackRevision`, **and a non-empty window**; its only consumer is the switch-commit binding in `TrackManager::updateAllTracks` (`TrackManagerTransportTick.cpp:216`) |
| the currency pair | `Loop::publishedSnapshotIsCurrent()` (`include/Loop.h:871`) compares `coveredPassRevision` against `committedPassRevision()` — which **is** `playbackRevision` by contract (DEC-073) — so coverage, not the revision stamp, decides currency; `publishedSnapshotIdentity()` is non-null only when current, and the `Overlay` path therefore records a null identity |
| retention that already exists | the prepared pair: `PreparedPairConsumeDecision::held` keeps the previous pair while the replacement prepares (`TrackPlaybackRuntime.h:361`, "P0 held while P1 prepares"). **No equivalent exists for the merged window** |
| the existing declared machinery | `CoverageServiceContract` (`include/RuntimePlaybackViewBuild.h:56`, `RuntimePlaybackViewBuild.cpp:131-142`) with `TrackPlaybackRuntime::beginCoverageBuild`; exercised by `test_playback_prewarm` |
| the gather fanout | nine call sites of the two entry points across six files: `DisplayNoteResolveCommitted.cpp` ×2, `DisplayColdHelpers.cpp` ×2, `TrackPlaybackWindowBuild.cpp` ×2, `LoopVisualCache.cpp` ×2, and the intra-wrapper call in `LoopMaterialization.cpp` (`S4C45` §3 reconciles the older "five" wording) |
| the commit-side binding | `TrackManager::updateAllTracks` (`src/TrackManager/TrackManagerTransportTick.cpp:216-233`): readiness → occupancy → keep the switch **pending** with no inline build |
| the off-clock precedent | `MidiServiceDrain::DeferredIdleOwner::SlotProvision = 5`, body `TrackManager::servicePendingSlotProvision()` (`src/TrackManager/TrackManagerSlotMatrix.cpp:348-390`), dispatched before the all-stopped guard, eligibility `MidiServiceDrain::slotProvisionNeeded` |
| the measured consequence of no root | the gather returns nothing ⇒ `GatherMissHoldEmpty` (`include/Utils/RuntimeTimingTelemetry.h:37`) ⇒ RWO's S4b.7 demoted-target hold, bounded and reported as `slot_switch_hold_exceeded` (DEC-104) |
| **the size pins** | `static_assert(sizeof(LoopPlaybackRuntime) == 6368)`, `sizeof(Loop) == 984`, `sizeof(TrackPlaybackRuntime) == 32` (`src/Track/TrackPlaybackRuntime.cpp:2246-2249`) |

**Constraints.** Hot paths stay allocation-free; the RAM1 budget is enforced by `scripts/check_ram1_budget.py` with a
floor of **840 B** and invariant 36's **872 B** usable padding (a bank cross costs 32,768 B and reaches the documented
silent boot hang); new per-slot state belongs in the external `LoopPlaybackRuntime` — pinned at **6,368 B**, re-based
deliberately with the reason recorded at the assert (DEC-107 recorded the last `6,352 -> 6,360` step) — or is
`FLASHMEM`/`TRACK_COLD_MEM` constructed. The external pool's ceiling is **measured** (a 24-bar heap run refused
`bytes=8192` at `live=8527KB`). `ExecutionBounds.md` classes the present build **Unbounded** (row 2) and its rows 27–29
declare the windowed-view contract as **excluded from admission** until its caps are implemented and proven. The `P`
class this change reshapes is derived in `runtime-working-set-ownership/S2C1-PLAYBACK-WINDOW-LEASE.md` on the target ABI.

## Goals / Non-Goals

**Goals:**

- State how playback comes to be served by an interval window with an identity, a **tempo-derived** deadline and one
  named owner, while the published root keeps its checkpoint authority for undo and save.
- Make previous-version retention explicit as a **pinned source revision**, and readiness a **completed publication**
  predicate (an empty interval is valid silence).
- Keep the acceptance shape RWO handed over (`S4C1` AC-1…AC-9, `S4C45` F-1…F-8) as this change's own gates.

**Non-Goals (design-level boundaries beyond `proposal.md`):**

- Not claiming the bounds: `N`, the product limit beyond the provisional 2,048-event window capacity, and any
  supported-density claim stay **owed** (DEC-108 §11) until measured.
- Not restating `test_playback_prewarm`, which pins today's reserved-capacity formula; this change's fixtures are new
  because the **unit** changes.
- Not building the visual tier's envelope, and not re-opening the display detailed window's caps.

## Decisions

**D1 — Identity separates content currency from lifetime safety: `(slot, playbackGeneration, playbackRevision,
interval)`.** `playbackRevision` is the sole **content-currency** authority (`committedPassRevision()` is its alias);
`playbackGeneration` guards ownership changes and stale deferred completions; `interval` carries the loop geometry a
consumer needs to interpret coverage; the snapshot identity is **provenance/lifetime protection, not a second definition
of "current"**. The interval is `[start, start + span)` in loop-local ticks, prepared **ahead of** the playhead.
*Alternatives:* a fresh validity counter (rejected — a second currency authority); identity without a revision (rejected
— no way to detect staleness); identity by loop only (rejected — that is the unit being replaced); dropping
`playbackGeneration` (rejected — the pair's freshness identity already carries it and a stale deferred completion would
become unattributable).

**D2 — One window-build/publication owner: `Track::stepPlaybackViewMaintenance`**, carried by the existing post-MIDI
deferred machinery and implementing the **already-declared** `CoverageServiceContract` (fixed work quantum, persistent
build cursor, bounded denied turns, demonstrated handoff margin). `SlotProvision` continues to *request* preparation and
*read* readiness; it is not a competing steady-state publisher. *Alternatives:* keep publishing from the `SlotProvision`
turn (rejected — it contradicts the owner `ExecutionBounds.md` declares for exactly this work, and its eligibility is a
pending switch, not an affected interval); a new scheduler of this change's own (rejected — AC-7 records a second
publication scheduler as a failure mode); building inline where readiness is read (rejected — AC-6, and the S4b.6/S4b.7
binding exists to prevent it). Note that the declared rows are "excluded from admission" until the contract's caps are
implemented and proven, so the **owner lands first** and no behaviour changes behind it.

**D3 — Readiness is completed publication with matching identity and sufficient coverage — not non-empty events.** A
successfully prepared **empty** interval is valid silence, and the completion state is **not** a second revision counter
(rejected alternative: a separate readiness flag — a second authority for currency). This corrects today's
`!mergedEvents.empty()` term, whose only consumer is the switch-commit binding; both of RWO's demoted-target fixtures
currently depend on empty ⇒ not-ready, so they are re-pointed rather than inherited unchanged. RWO also measured that
removing the `SlotProvision` turn's window call left every fixture green and pinned that as a harness tripwire; the
fixtures here must **fail** when the window build is dropped, so this change closes that measurement gap.

**D4 — Previous-version retention means a retained immutable source revision, not the last window buffer.** For an
affected interval whose deadline is missed: serve **that interval's** previous revision for that traversal; never replace
its content mid-consumption; publish the replacement before the following eligible traversal; and **do not reset the lag
allowance when another edit arrives**. Later windows stay constructible from the pinned old source revision **without a
resident whole-loop playback root**, and that source-retention cost is charged to the envelope. A **second** missed
traversal is a **named contract breach**, not successful one-pass-late operation — reporting it does not make the
requirement pass. *Alternatives:* copy the last window buffer (rejected — an old two-bar buffer cannot serve later
intervals of a long loop); hold the consumer (rejected — AC-5(a)); serve the new partial window (rejected — torn).
Precedent adopted rather than invented: the pair's `PreparedPairConsumeDecision::held`.

**D5 — Reservation: extend the existing view set with window-sized publication; do not restore whole-take reservations
for demoted slots.** Reuse the view set's bounded storage, lease and publication mechanisms; budget **current, ready and
building** windows, including reader retention, staging and transition overlap; verify that the three rolling views'
lifetimes suffice **before** adding buffers. *Alternatives:* simply enabling the whole-take prepared-pair publisher on
long loops (rejected — it is the reservation RC-MEM-2 removed, and `test_playback_prewarm` pins that a windowed-only slot
reserves no whole-take representation).

**D6 — Unify the consumption contract, not every representation.** Prepared-pair and fixed-view playback stay for
admissible loops; immutable **interval** publication is introduced for demoted/windowed loops; a whole-loop view is
treated as coverage that happens to span the whole loop. The live mutable merged-window build is removed **once its
replacement is verified**, with a cold reference path retained for parity testing. *Alternatives:* replace all three
representations at once (rejected — an unnecessary rewrite of working behaviour); leave the live build in place
(rejected — it is the Unbounded row this change exists to close).

**D7 — The published root keeps its checkpoint authority; parity is judged against the current revision.** Undo parity
and save parity are one requirement on two consumers, and neither consults the window's lag — the one-pass-late rule is a
playback rule. A lagging checkpoint is named in the lag vocabulary, never silently current (today's
`storePublishedSnapshot(…, kSnapshotCoverageCurrent)` at the restore edge is the case `F-6` has to falsify).
*Alternatives:* judge parity against the window (rejected — it would make a playback rule a checkpoint rule); narrow the
root here (explicitly out of scope; RWO's S5 stays gated on this change's parity proof).

**D8 — Placement: window state stays external, and retention is charged.** New state goes in the external
`LoopPlaybackRuntime` or the `LoopPool` `Loop`; any additional counter is `DMAMEM`/`FLASHMEM`; `static_assert` size pins
are re-based **deliberately with the reason recorded at the assert** before any field is added. The RAM1 line must not
grow and the budget check is part of this change's gates. *Alternatives:* a by-value array inside `Track` (rejected — it
emits implicit ctors/dtors into ITCM and has already cost one 32 KB FlexRAM bank).

**D9 — The deadline is derived, not asserted.** It follows tempo, quantisation, phase advance and service availability.
The **16–31 ms** figure is the cited **120 BPM** example (1/32–1/16 beat). The reachable playback clock range is the one
`ClockManager::setBpmFloat` enforces — **20–300 BPM** (`src/ClockManager.cpp:107-112`); the `TEMPO_MIN_BPM` /
`TEMPO_MAX_BPM` = 60/187 pair in `include/Utils/TempoMapping.h` is the **CC102 fader mapping**, not the clock. Across
20–300 BPM that beat fraction is **6.25–187.5 ms**, and external-clock pulses advance playback directly
(`onMidiClockPulse` → `currentTick += Config::TICKS_PER_CLOCK`), so batched pulses can compress the effective deadline
further: the proof must be against **actual clock advancement**, not against a nominal tempo. Limits stay provisional: the
**2,048-event** ceiling as a per-window capacity, a **two-bar** span as the initial comparison configuration, with
smaller spans (quarter-bar, one-bar) to be evaluated. A smaller window does not solve unbounded same-tick density, and
**accepted content is never truncated to fit** (see the overflow blocker in the G2 section below).

**D10 — Receipts follow the delivery rules this repo already paid for.** A readiness receipt is emitted where it is
produced when the stream has room (the `persistenceResultImmediate` / `SC_PERSIST_RESULT` shape), keeps the ring as its
fallback so it is delayed rather than lost, and diagnostic cadence is bounded through `Utils/TelemetryCadence.h`, so a
diagnostic cannot evict the evidence it explains.

## Owners and call paths

| concern | owner today | what this change adds |
|---|---|---|
| window build as a working buffer | `Track::ensurePlaybackMergedEventsForSlot` + `Loop::tryGatherPublishedSnapshotEventsInWindow[WithCapture]` | interval-resolved coverage and a window identity stamped with the revision — the buffer remains the preparation's, not the playback authority |
| **window publication (new)** | *nothing in the steady state* | `Track::stepPlaybackViewMaintenance` under `CoverageServiceContract`, publishing immutable interval windows |
| readiness | `Track::isPlaybackMergedMidiEventsReadyForSlot` | completed-publication semantics (empty valid), matching identity, sufficient coverage, and a failure that names its holder |
| retention (new) | the prepared pair only (`…::held`) | a **pinned source revision** per affected interval, charged to the envelope |
| commit binding | `TrackManager::updateAllTracks` (`TrackManagerTransportTick.cpp`) | unchanged shape: read readiness, stay pending, never build inline |
| preparation request | `TrackManager::servicePendingSlotProvision` (`DeferredIdleOwner::SlotProvision`) | requests preparation and reads readiness; publishes nothing in the steady state |
| checkpoint consumers (read-only) | `TrackUndo.cpp`, `StorageLoopIo.cpp`, `WorkspaceSave.cpp`, `LibraryObservationSession.cpp` | parity assertions only — no ownership change |
| content verification | `.agents/skills/hitl-content-verification`, `docs/Guides/HITL_CONTENT_VERIFICATION.md` | fed the windowed/demoted cases (new stimulus profile), not replaced |

## Interfaces this change must fix

Restating RWO's four (`S4C1` §"Interfaces S2.1 needs"), now with DEC-108's answers: (1) window identity
`(slot, playbackGeneration, playbackRevision, interval)`; (2) the `P`-class accounting — today 331 KiB per holder,
derived `P_windowed(c) = Views(3)·c·35 + Stagings(2)·(c·29 + 128·20)`, with window-sized publication inside the existing
view set and **no** restored whole-take reservation for demoted slots, plus the new source-retention term; (3) the
readiness predicate (completed publication) and the lag bound (one pass, not reset by further edits); (4) the off-clock
owner `Track::stepPlaybackViewMaintenance`.

## Invariants

- A published window is immutable and carries `(slot, playbackGeneration, playbackRevision, interval)`; no consumer read
  straddles a replacement, and no interval's content changes mid-consumption.
- One content-currency authority: the loop's current `playbackRevision`. `playbackGeneration` guards ownership, not
  currency; the snapshot identity is provenance/lifetime protection.
- Readiness = completed publication + matching identity + sufficient coverage. An empty interval is valid silence.
- Lag is bounded at one pass, the allowance is not reset by a further edit, and a second missed traversal is a **named
  breach**.
- The MIDI input boundary, the transport tick and the clock path read readiness only; the commit binds to a prepared
  window and otherwise keeps the request pending.
- One owner acquires, replaces and publishes; `SlotProvision` requests and reads.
- The published root remains the checkpoint authority; no root dependency is removed.
- No new scheduler and no second publication owner.
- Accepted content is never truncated to fit a window, and no new DTCM state (invariant 36) — the RAM1 line does not grow.

## Failure behavior

- **Not-ready interval at the deadline:** serve that interval's previous revision for that traversal; no tear; the
  replacement publishes before the following eligible traversal.
- **Second consecutive miss:** a **named contract breach** (not a pass), reported with its holder.
- **Prepared empty interval:** ready, and it sounds as silence — it is not a readiness failure.
- **Demoted slot with no published root:** the hold is satisfied by "**required window and source lifetime ready**", not
  by whole-root residency; the switch is accepted, retained and bound at a valid boundary with no second gesture
  (DEC-108 §10, revising DEC-104's report-only shape for this lane).
- **Refusal at the owner:** non-destructive and named with its own operation kind (the `OperationKind::SlotSwitch` /
  `op=slot_switch` precedent) — the request stays pending.
- **Persistence in flight:** one drain budget, so a large window publish cannot starve an in-flight save.

## Risks / Trade-offs

- [A window authority that is only a rename of today's merged window proves nothing] → F-1/F-8 assert identity,
  retention and residency, not naming; F-8 is swept over loop lengths.
- [The declared owner's admissibility condition is not met, so active windowed playback stays denied] → the owner and
  its `CoverageServiceContract` caps land **first**, and the change claims no behaviour behind them until the proof
  fields exist (`ExecutionBounds.md` rows 27–29).
- [Source retention silently inflates the envelope] → the retained source revision is a charged, measured term, and the
  budget check is a gate.
- [Readiness change breaks the switch-commit binding] → the binding's single consumer is named, and both of RWO's
  demoted-target fixtures are re-pointed rather than inherited.
- [Nine gather consumers could grow a second read path] → consumers stay read-only; F-7 compares windowed output with the
  whole-loop path; F-7/F-8 extend the existing content pipeline rather than adding one.
- [New state lands in DTCM] → external/`FLASHMEM` placement, deliberate pin re-bases, and the RAM1 budget check.
- [Scope creep into RWO S2.11's other levers] → named non-goals in `proposal.md`.

## Migration Plan

No runtime migration and no data migration in this step: this change ships planning artifacts only, so nothing on device
or on the SD card changes. At Apply the shape is additive by construction — prepared-pair and fixed-view playback stay
for admissible loops while interval publication is introduced for demoted loops, and the live merged-window build is
removed only once its replacement is verified — so rollback is "the publication unit stays as it is", which needs no
schema or card-format change. Device legs are authorized per session (no upload without confirmation), and no SD
operation is implied by this change.

## Gating before Apply

**Landed 2026-10-02:** `DEC-108` (the contract) and this directory's `ARCHITECTURE-REASSESSMENT.md` (the trigger
record), which is what RWO's `IMPLEMENTATION_CONTRACT.md` §4 and `design.md:812` required of this sibling. **Still
required before a firmware edit:** [`docs/Templates/DECISION_REVIEW.md`](../../../docs/Templates/DECISION_REVIEW.md)
completed for this change, one G2 execution packet per implementation slice, and the owner
(`Track::stepPlaybackViewMaintenance` + `CoverageServiceContract`) landing before any behaviour change.

## Recorded decisions (owner-supplied, 2026-10-02 → DEC-108)

The four items this design previously left as TBD are answered: the window size (two-bar initial comparison configuration,
smaller spans to be evaluated), the product limit (the existing 2,048-event ceiling as an explicitly provisional
per-window capacity), the envelope targets (eight preserved, 16 next, 32/48/64 measured without promise), and the
demoted-target hold (required window + source lifetime ready, never whole-root residency). All four, with the owner,
reservation, retention, identity and readiness answers, are recorded in **DEC-108** — **proposed contract, not proven
behaviour**.

## Still owed — unproven bounds (no claim until measured)

- `N` beyond today's eight sounding roles, and any supported-density claim (both a memory **and** a deadline test must
  pass).
- The per-window capacity beyond the provisional 2,048-event ceiling; whether smaller spans are adopted.
- The RAM1 line on a fresh target build, and the corrected size baselines (6,368 B / 984 B).
- The source-retention cost as a charged envelope term.
- Device evidence: the windowed HITL profile, and the sweeps over loop length, density, tempo, window span and competing
  deferred work.

## Contract gaps returned to architecture (G2 review, 2026-10-02) — two passes; all choices decided on the record

A read-only G2 implementation-contract review tested this change against HEAD in **two passes**. The first pass found five
**blockers** and three **tightening** items; the second pass re-tested the first pass's resolutions and found that **two
of them name APIs that cannot deliver what they claim**, plus a task-ordering contradiction and three further tightening
items. Every claim in both passes was re-verified against the tree for this record, and each row names the code that
carries it. **Status: resolutions 1–3 are corrected below, the choices the two passes left open were decided by the owner
on 2026-10-02 (DEC-108 §12–§13), and no item below remains undecided.**

| # | Finding | Severity | Verified evidence |
|---|---|---|---|
| 1 | **Rootless source retention has no ownership protocol.** Naming a "pinned source revision" (D4/§10) does not define how its content survives undo, clear, reclamation, load and deferred completion. | BLOCKER | the gather fails without a published root (`src/Loop/LoopSnapshotPlayback.cpp:182-188`); `RuntimePlaybackViewFill::discard()` is explicitly borrowed-only; pass reclamation protects by **undo** references (`Loop::DisabledPassReclaim::referenced`, `src/Loop/LoopPassReclaim.cpp:45-50`); `Track::noLiveOwnerForSlot` lists owner kinds with no window/source-retention term (`src/Track/TrackPlaybackWindowBuild.cpp:937-945`) |
| 2 | **Bounded fill grains do not establish length-independent readiness.** Window-sized *output* plus a fixed quantum does not bound the *scan*. | BLOCKER | `beginCoverageBuild` receives the complete source (`const MidiEvent* source, size_t sourceCount`, `src/Track/TrackPlaybackRuntime.cpp:1577`); `RuntimePlaybackViewFill::step` iterates `while (remaining > 0 && cursor_ < sourceCount_)` and completes only at `cursor_ >= sourceCount_` (`src/Track/RuntimePlaybackViewFill.cpp:125-175`) |
| 3 | **Overflow behavior cannot satisfy "every accepted event is served."** Reporting capacity pressure is not audibility. | BLOCKER | `begin` denies when `sourceCount > destination.storage.capacity` (`RuntimePlaybackViewFill.cpp:62`); `step` returns `Denied` when `destination_->storage.append(event)` fails (`:156-161`) — while this change's spec requires every accepted event to be served and keeps a provisional 2,048-event capacity |
| 4 | **The declared reachable tempo range was incorrect** (this change's own error; corrected below). | BLOCKER | `ClockManager::setBpmFloat` clamps **20–300 BPM** (`src/ClockManager.cpp:107-112`) and `onMidiClockPulse` advances playback directly (`currentTick += Config::TICKS_PER_CLOCK`, `:235`) after `setBpmFloat(computedBpm)` (`:223`); `TEMPO_MIN_BPM`/`TEMPO_MAX_BPM` = 60/187 are the **CC102 fader mapping** (`include/Utils/TempoMapping.h:23-24`), not the clock range |
| 5 | **Removing the live fallback leaves NOTE_EDIT preview unspecified.** | BLOCKER | on a demoted loop NOTE_EDIT reaches the merged fallback, which switches to `sessionMidiEvents()` and `sessionPlaybackPreviewRevision()` (`src/Track/TrackPlaybackWindowBuild.cpp:202-205, 255-259`); the contract defines committed `playbackRevision` identity and later removes that path without a preview-source contract |
| A | **Deferred service fairness** — owning one turn and one drain budget does not establish bounded denial or starvation freedom. | TIGHTENING | `servicePendingSlotProvision` serves the first pending track and returns (`src/TrackManager/TrackManagerSlotMatrix.cpp:372-390`); other deferred owners compete for the same budget |
| B | **Shared-view and cancellation transitions** — three views' sufficiency and cleanup are asserted, not tabulated. | TIGHTENING | `LoopPlaybackRuntime::reset(bool preserveLedger)` cancels builders and preserves only selected record-return cases (`src/Track/TrackPlaybackRuntime.cpp:324+`); the views already serve record first-record, rolling playback and build roles |
| C | **Second-miss and cross-generation boundary behavior** — a named breach does not define what is emitted afterwards. | TIGHTENING | the contract names the second miss but not post-breach playback, lag retirement or ledger reconciliation; the boundary scenarios do not cover a note-on from the retained revision whose note-off moves in the replacement |
**Second pass (2026-10-02) — it re-tests the resolutions above:**

| # | Finding | Severity | Verified evidence |
|---|---|---|---|
| 6 | **The chosen traversal is not work-bounded.** `appendWindowBudgeted` limits **events appended**, not **chunks inspected**: non-intersecting chunks do not consume its budget, so a window that intersects nothing can walk every chunk in one call (and it builds temporaries). | BLOCKER (refines resolution 2) | `src/CommittedEventRange.cpp:178-210`: the walk `for (; ci < list->size(); ++ci)` consults `LoopEventStore::chunkTickSpan` per chunk and `continue`s on non-intersection, while only appended events consume the budget |
| 7 | **Pass IDs do not preserve an immutable revision.** The pin helper only fills an ID list; the veto is rebuilt per track **from the undo stack** inside the memory-pressure consumer, and the direct reset/replacement path releases chunks, clears passes and **reuses IDs** without consulting any pin. Raw committed-chunk enumeration also does not apply note edits or establish effective ordering, so the effective-content resolver must be named. | BLOCKER (refines resolution 1) | `pinPassesFromLoopPasses` calls only `pinCapturePass`/`pinEditPass` (`src/PassReclaim.cpp:10-24`); the consumer builds a fresh `PassReferenceSet` from `collectReferencedPasses(track.getGlobalUndoStack(), refs)` (`src/TrackManager/TrackManagerMemoryPressure.cpp:80-92`); `Loop::resetPassTimeline` calls `LoopEventStore::releaseChunkRefs`, clears the passes, sets `nextPassId_ = 1` and resets the revision (`src/Loop.cpp:32-45`) | 
| 8 | **The admission limit has no accounting interval.** "Events per interval" never defines the accounting unit (capture interval, fixed playback window, or sliding window), the aggregate across overdubs, whether **raw or effective** events are counted, reserved capacity for note-off closures, edits that move events into a full interval, or previously accepted dense content restored from SD/undo. Two individually legal captures can exceed one playback window, and a sliding window can straddle two limited partitions. | BLOCKER (refines resolution 3) | the requirement bounds acceptance without naming the accounting unit (this change's spec, "Accepted content is never truncated to fit a window"); MIDI-content admission must stay distinct from **interaction** acceptance, which DEC-078 keeps unconditional |
| 9 | **The task ordering contradicts fixture-first.** §1.6 of the task list requires fixtures 2.9–2.12 to pass **before** the owner slice, while the behaviour those fixtures pin arrives in §3.6 and later. | BLOCKER | `tasks.md` §1.6 versus §3.6 |
| 5 | **One retained revision versus repeated edits.** The rules permit one pinned revision per slot, interval-specific handoffs and further edits without resetting the lag, but they do not determine which revision stays authoritative: a concrete trace (interval A advances, interval B is late, A changes again) needs coalescing, which versions stay constructible, when each pin retires, and what a "pass" means across wrap, geometry change, MIDI Start and restart. | TIGHTENING | this change's spec §"One pass late is the only stale authority" |
| 6 | **Scheduling and breach behaviour are still decisions**, not packet detail: arbitration across tracks and competing owners, clock-assumption revalidation, and the exact output after a second miss. | TIGHTENING | the carried-items paragraph below |
| 7 | **Preview lifetime is unspecified.** The preview requirement gives versioning/retention to the edit-session lifecycle but not whether a builder survives session mutation or exit, nor whether preview keeps its existing synchronous compatibility path or moves to immutable deferred publication. | TIGHTENING | the spec's preview requirement |

**Third pass (2026-10-02) — it re-tests the resolutions again; all five items are now answered:**

| # | Finding | Severity | Verified evidence | Resolution |
|---|---|---|---|---|
| 1 | **Admission capacity and window capacity disagreed.** The admission rule sized storage for "two adjacent limited partitions" while task 5.1 kept a 2,048-event per-window ceiling — a window holding two such partitions can need 4,096 — and "two adjacent partitions" had no geometric rule. | BLOCKER | the spec's accounting clause vs `tasks.md` §5.1 | **Answered — see §"Capacity accounting, legacy compatibility and the retained-source resolver"**, which replaces the partition language with a **sliding window-span invariant** whose storage capacity is exactly the limit, so 2,048 stays valid |
| 2 | **Legacy dense content had no surviving path.** The spec promised pre-limit SD/undo content would play through "the existing committed path", but §3.7 removes the live merged-window fallback — the path demoted loops actually reach in `playMidiEventsForSlot` (`src/Track/TrackPlaybackHotPath.cpp:287-304`) — and fixed storage rejects overflow. | BLOCKER | `useMergedFallback` → `buildMergedPlaybackFallbackCold`; `RuntimePlaybackViewFill::begin` denies when `sourceCount > capacity` | **Answered — bounded segmented serving through the existing publication owner (see the same section)** |
| 3 | **The retained-source resolver/index was a placeholder.** `resolveWindow` gathers the whole snapshot before filtering (`src/LoopContentResolution.cpp:1073-1085`), the indexed overloads resolve synchronously with temporaries, and `TickIndex` stores per-pass event vectors plus index rows (`include/LoopContentResolution.h:105-135`) — materially different choices, none selected. | BLOCKER | as cited | **Answered — extend the effective-content owner, the selected `LoopSnapshot` and the query path over it (corrected 2026-10-02 per **F-1**: the earlier wording named `LoopContentResolution`, which `NAMING.md` forbids in new docs), `TickIndex` semantics reused but not its whole-history event-copy storage; `CommittedEventRange` becomes a body primitive only once its inspected-work budgeting is extended and verified (see the same section)** |
| 4 | **Superseded clauses stayed normative:** "at most one retained revision per slot" beside the serving/candidate/retiring roles; empty intervals "produce silence" beside preserving sustained notes; a release scenario narrower than the release rule; obsolete "OPEN" wording above decided items. | TIGHTENING | spec §"One pass late…", §"Readiness is completed publication…", design resolutions | **Applied** — clauses replaced in place, one release predicate everywhere, "emits no new events" for empty coverage, continuity made subject to the approved exception, stale OPEN wording removed |
| 5 | **Slice dependencies were still implicit:** fixtures ahead of the slices implementing them, fallback removal before preview and legacy compatibility, publication before capacity/ownership. | TIGHTENING | `tasks.md` §2.1–2.8, §3.7, §3.13 | **Applied** — the four-stage ladder (fixture established → implemented and verified → production path enabled → old path removed) is now the stated rule, and §3.7 names every dependency |
**Covered by the review (specification level, not runtime-proven):** whole-take reservation exclusion for demoted
slots; completed empty intervals as valid readiness; the published root as undo/save authority separate from playback
lag; external state placement with fresh RAM1 verification; `activeLoopIndex` as the sole active-slot authority; and no
allocation or build on timing-critical edges. The review also confirmed the stop path defers full validation rather than
running `validateAndCleanupMidiEvents` synchronously, and that this change does not authorize changing that.

**What the three passes require before Apply:** every choice the passes raised is now **decided** — the two earlier ones
in the resolutions below, and the third pass's three (capacity accounting, legacy compatibility, retained-source
resolver/index) in §"Capacity accounting, legacy compatibility and the retained-source resolver". What remains is
**proof**: the accounting granularity and its worst-case work, the legacy path's deadline demonstration, the index's
resident-memory bound, and every measurement — all unproven until their fixtures and legs run.

## Recorded resolutions (G2 blockers, 2026-10-02 → DEC-108 §12–§13)

The five blockers were decided by the owner on 2026-10-02 and **corrected on the second review pass** — two of the first
wording's cited mechanisms cannot deliver what it claimed (rows 6 and 7 above). Each resolution keeps the *goal*; the
mechanism is restated as a requirement on the mechanism, and two genuine choices were left open for the owner because they
carry a memory or product tradeoff — **both are decided below** (OPEN-1 and OPEN-2). None of it is proven until its
fixture runs.
1. **Retention: the retained revision must be *held*, not merely named.** Targeted at the second pass's row 7. The
   contract therefore requires: an **immutable description of the retained revision** (pass membership and order, edit
   metadata, loop geometry) plus **actual body ownership** with a **generation-qualified identity**; registration in the
   reclamation veto **as an input the consumer reads** — today `TrackManagerMemoryPressure` rebuilds its `PassReferenceSet`
   from the undo stack alone (`src/TrackManager/TrackManagerMemoryPressure.cpp:80-92`), so a playback-owned list is
   invisible unless that input is added — and **the same protection honoured on the direct reset/replacement paths**
   (`Loop::resetPassTimeline` releases chunks, clears passes and reuses IDs at `src/Loop.cpp:32-45`); an explicitly named
   **effective-content resolver** (the effective-content owner to extend is named in §14 as the query path over the
   selected `LoopSnapshot`, `Loop::tryGatherPublishedSnapshotEventsInWindow`) —
   since raw committed-chunk enumeration does not apply note edits or establish effective ordering; and **release
   conditions covering builders, outstanding future-window obligations, readers and
   cancellation**, not only windows that already exist.
   **Decided (2026-10-02):** the retained source is **pinned bodies plus reconstruction through the effective-content
   resolver** — not a frozen per-interval copy, which would reintroduce the content-sized residency this change exists to
   remove. The pin must therefore be honoured by the reclamation consumer **and** by the direct reset/replacement path,
   and the reconstruction cost is charged to the deadline.
2. **Enumeration: bound the *inspected* work, not just the appended events.** Targeted at the second pass's row 6.
   `appendWindowBudgeted` currently charges only appended events (`src/CommittedEventRange.cpp:178-210`), so an interval
   that intersects nothing can walk every chunk in one call. The contract requires a **grain budget on inspected work**
   (chunks examined, including rejected ones, plus source lookup) as well as on appended events, and a **chunk-level
   interval index on the retained source** (§14: represented over **pinned bodies** and owned by the publishing owner)
   so total lookup does not grow with unrelated loop content — a chunk quantum
   alone bounds per-turn latency, not readiness. Enumeration of the retained source SHALL go through the
   **effective-content resolver**, not raw committed chunks; where only a flat/typed source exists (NOTE_EDIT session
   store, compatibility paths) the complete scan is accounted in admission as a measured cost.
3. **Overflow: unreachable at admission — once the accounting unit is defined.** Targeted at the second pass's row 8.
   The limit is retained (the provisional 2,048-event figure) and capacity pressure is refused before acceptance rather
   than absorbed after it. The **accounting unit is decided (OPEN-2, 2026-10-02)**: the limit is enforced on the
   **capture interval at acceptance**, counted in **effective** events plus a **reserved note-off/generated-closure
   capacity**, aggregated across the slot's overdub passes; the window's storage capacity is **exactly the limit**, under
   the **sliding window-span invariant** below, so playback capacity is derived from the limit, not the reverse.
   Refusals are named at admission, and restored dense content that predates the limit is served through the **bounded
   segmented compatibility path** below and is not retroactively refused. **MIDI-content admission stays distinct from
   interaction acceptance**, which DEC-078 keeps unconditional — this limit SHALL NOT cause a stop, switch or undo
   request to be refused.
   **Answered (third pass, row 1 → DEC-108 §14):** the partition language is **withdrawn** in favour of the sliding
   window-span invariant; the closure reserve counts **inside** the limit; admission uses preallocated bounded accounting
   and **no resolver runs on the MIDI edge**. See §"Capacity accounting, legacy compatibility and the retained-source
   resolver" below.
4. **Clock basis: corrected, and revalidation decided.** The proof runs against actual clock advancement over **20–300 BPM**
   (`ClockManager::setBpmFloat`, `src/ClockManager.cpp:107-112`), including batched external pulses that advance playback
   directly — see D9. Clock-assumption **revalidation** is decided in §"Recorded lifecycle and recovery policies" item 3:
   revalidate admission before publication or handoff, and on failure **retain the request, continue the valid audible
   source, report the miss and never rebuild inline**.
5. **Preview: its own source, published through deferred immutable publication** (second-pass row 7). NOTE_EDIT preview
   keeps its own versioned/retained/invalidated preview source, is identified as preview rather than as a committed
   window at the same revision, and never counts as committed currency. **Decided (2026-10-02):** preview moves to
   **immutable deferred publication** rather than keeping its synchronous compatibility path — so the contract SHALL
   define **source freezing** (the preview revision a builder is working from), **cancellation** (what happens when the
   session mutates, re-focuses another track, undoes or exits between build grains), and **committed-content handoff**
   (how the preview source yields to committed content at session exit). No mixed or stale preview publication is
   permitted, and because preview no longer needs the live mutable fallback, the fallback's removal is **not** narrowed
   on preview's account.

## Recorded lifecycle and recovery policies (2026-10-02 → DEC-108 §13)

The carried tightening items are now **decided**. They keep the existing deferred machinery; no second scheduling
framework is introduced.

**1 — Revision handoff, and what a "pass" is.** An interval's version is fixed once its traversal begins; a new edit
updates the *desired* revision and never modifies a published window or a builder's frozen source. Superseded
**preparation requests** are coalesced — never accepted musical content — and useful preparation is not repeatedly
cancelled for newer edits, so progress toward the oldest outstanding deadline is preserved. A later edit never resets an
earlier interval's missed-deadline allowance. **Pass = one traversal of the affected interval in the playback
projection** — not a revision increment and not elapsed time: ordinary wrap advances traversal accounting; transport stop
**pauses** traversal and does not forgive an existing miss; MIDI Start, seek and geometry changes invalidate old
positional deadlines and require revalidation, without erasing pending content obligations or fault history. The worked
trace: A and B serve revision 0 → revision 1 changes both, A's replacement ready and B's not → A serves 1 while B serves 0
for its allowed late traversal → revision 2 changes A again → B still owes the revision-1 change at its next traversal,
and a revision-2 window may satisfy that obligation if it includes the change → revision 0 stays retained until no
reader, builder, future-window obligation or outstanding note closure needs it. **"One retained revision per slot" is
replaced by explicit source roles** — **serving, candidate and retiring** — sharing unchanged bodies where possible;
these are source *lifetimes*, not three extra whole-loop copies and not automatically the three view buffers. Further
preparation is bounded by **role admission**, and a still-needed source is never discarded merely to enforce a count; the
role capacity must pass the trace above before it is claimed sufficient.

**2 — Fairness, then deadline order.** Playback maintenance gets a recurring, **bounded** service opportunity; holders
rotate round-robin; within a holder the **earliest outstanding playback deadline** is served first; the selection
cursor advances **even when a holder cannot progress**, so a blocked early track cannot monopolise service; capture
finalization and persistence keep their own explicit opportunities and playback urgency must not starve them; one
declared grain per admitted turn with no hidden stacking. The proof must include **all admitted holders**, including
inbound switch preparation and overlapping finalization — not merely eight sounding tracks. No microsecond budget is
invented here: the supported envelope is **derived** from grain costs and the worst permitted gap between service
opportunities.

**3 — Clock revalidation.** Admission is revalidated before publication/handoff when tempo, clock source, projection or
observed advancement invalidates the earlier assumptions: a changed clock does not invalidate the musical content, it may
invalidate the **promise that preparation will finish in time**. On failure the accepted request is retained, the valid
audible source continues, the missed deadline is reported, and an inline rebuild is never used to compensate.

**4 — Shared views: ownership survives selection changes.** Published/current views are read-only; only the owning
builder writes its candidate; a ready view is immutable until adopted or explicitly retired; storage becomes reusable
only after every relevant owner releases it. Transitions: a **new edit** retains the frozen builder source and coalesces
the successor; a **slot switch** prepares inbound coverage before binding and retains outbound resources until their
readers and closures finish; a **selected-track change** does not retarget an existing worker (only preview lifecycle
changes); a **record/overdub stop with a successor start** preserves the predecessor's frozen ownership and the successor
cannot borrow its buffers; **transport stop** uses the existing stop semantics, cancels obsolete lookahead and preserves
accepted content and finalization; **clear/load** invalidates the old operation identity and retains its source until
cancellation and readers finish; a **late completion** is rejected for publication and releases only the resources it
owns. **Returning to a slot must not revive an earlier operation on it** — identity is compared generation-qualified,
never by slot number or pointer.

**5 — Second miss: continuity first, and the approved last resort.** The first miss is the one-traversal allowance; the
second **latches a named deadline-contract fault**. Playback continues the last coherent revision **where valid coverage
can still be supplied through an admitted bounded path**, preparation keeps progressing, and recovery happens at an
eligible boundary — never a silent lag reset, a partial publication or a blocked clock. This deliberately distinguishes
**fault containment** from meeting the one-pass-late guarantee. **Approved exception (owner, 2026-10-02):** when no valid
coverage remains at all, continuity cannot be promised, and the last resort is **targeted note closure plus affected-slot
silence** — never a global transport stop and never an unbounded rebuild.

**6 — Cross-generation note closure, prepared off-clock.** Unchanged sounding notes are preserved without retriggering;
notes removed or invalidated by the replacement are **closed at handoff**; obsolete closures are suppressed afterwards;
required note-offs are emitted **before** the replacement's note-ons; and closure obligations stay alive independently of
view-buffer retirement. An **event-empty window means "no new events"** — it must not silence notes legitimately
sustained across that interval.

**Verification split.** **Native tests force** the interleavings: A→B→A slot reuse with the first A completion arriving
last, the interval A/B revision trace, cancellation between every preparation grain, repeated edits without a deadline
reset, stop/start, undo/redo, clear/load, clock revalidation, resource refusal followed by recovery, and closures
crossing version boundaries — on the **real request→worker→publication→consumption path** with controlled ticks and
explicit expected MIDI output, no sleeps and no copied state machines. **Hardware proves** grain duration and maximum
service gaps, sustained multi-track playback with capture and persistence contention, external-clock behaviour, and
memory high-water marks with boundary content parity. Reproducing the exact A→B→A race on hardware is **not** mandatory
once native tests establish the ownership ordering — native tests cannot establish physical timing, and hardware legs are
not a substitute for the ordering proof.

## Capacity accounting, legacy compatibility and the retained-source resolver (2026-10-02 → DEC-108 §14)

The third pass's three implementation choices are **decided** by the owner. All three are statements of what must hold;
their bounds are still unproven until measured.

**Capacity accounting — a sliding window-span invariant, not a partition multiplier.** For the configured playback span
`W`, **every** `W`-wide interval of the loop — including intervals that cross the loop wrap — SHALL fit within the limit
`L` (provisionally the **2,048-event** figure). The window's storage capacity is therefore exactly `L`: the
per-partition multiplier and the "two adjacent partitions" wording are **withdrawn**, and task 5.1's 2,048-event
per-window ceiling stays valid as that same quantity. Pending accepted content and the **reserved capacity for note-off
and generated closures count inside `L`**, not in addition to it. Admission SHALL use **preallocated, bounded
accounting** that includes outstanding reservations and SHALL **not invoke the effective-content resolver on the MIDI
edge**; per-bar counts MAY be used only as a **conservative bound covering every intersected bar**, never as an exact
sliding-window count. An accepted note's closure SHALL remain guaranteed even when further input is refused. A change to
the window span, to edit geometry or to content SHALL preserve the invariant **before it becomes audible**. The exact
accounting granularity and its worst-case work SHALL be specified and tested before implementation.

**Legacy compatibility — bounded segmented serving through the existing publication owner.** An interval restored from
SD or undo whose content exceeds `L` SHALL be served as **multiple fixed-capacity immutable event pages**, consumed
through a **resumable cursor** that preserves the source revision, event ordering and **exactly-once** emission across
page boundaries. Pages SHALL be prepared **off-clock** from bounded, preallocated storage. Pagination SHALL NOT delay
events to later musical ticks, rebuild inline, restore whole-take reservations or truncate content. Capacity exceeding
one page is a **supported compatibility case and is not itself a deadline fault**; genuine preparation or playback
deadline failures still follow the approved fault-containment policy, because **legacy content is not exempt from timing
constraints** — a storage bound is not a throughput proof. Before this path is enabled, preparation, page handoff and
MIDI emission SHALL be shown to meet their deadlines within the declared legacy envelope, including over-capacity
same-tick events, loop wrap and cross-page note closures.

**Retained-source resolver and interval index — extend the effective-content owner, the selected `LoopSnapshot`.**
The **effective-content owner** is the selected **`LoopSnapshot`** and the query path over it: the published-snapshot
window gather (`Loop::tryGatherPublishedSnapshotEventsInWindow`) plus the new interval index.
`Track::stepPlaybackViewMaintenance` owns preparation and publication scheduling and invokes that resolver against a
**generation-qualified immutable retained source**. *(Corrected 2026-10-02 — **F-1** of `DECISION_REVIEW.md**: the
earlier wording named `LoopContentResolution` as this owner. `NAMING.md` outranks OpenSpec and states that
`LoopContentResolution` is a historical DEC-037 prototype name new docs must not use, that committed effective state is
the selected `LoopSnapshot`, and DEC-037 keeps it a parallel prototype whose three caller gates never passed
(`Architecture/DerivedViews.md`: retained only as a compatibility adapter). No requirement changed — only the name of
the owner; see **DEC-108 §14(c)**'s correction.)* `TickIndex`'s lookup and edit-resolution **semantics**
are reused where suitable, but its **whole-history event-copy storage is NOT inherited unchanged** — the index is
specified over **pinned bodies**, with its own representation change recorded explicitly, its ownership (the publishing
owner), its **resident-memory bound**, and its construction/update lifecycle stated. `CommittedEventRange` is used as a
**body-reading primitive only after its inspected-work budgeting is extended and verified**. Index lookup SHALL include
**edit-moved events at their effective destinations**; updates SHALL cover **both the old and the new affected ranges**
and publish **atomically with the corresponding source revision**, never modifying an index that is still serving
readers. Lookup, reconstruction, ordering, index maintenance and retirement SHALL all be bounded **and accounted** — not
merely the emitted events. Verification covers moved-in notes, retained revisions, rootless playback and sparse long
loops.

## Runtime continuity dependency and undo/redo integration (2026-10-02)

The end-to-end goal is owned once by the runtime spec's
[Continuous runtime operation has one end-to-end acceptance contract](../runtime-working-set-ownership/specs/runtime-working-set-ownership/spec.md#requirement-continuous-runtime-operation-has-one-end-to-end-acceptance-contract).
This change owns the windowed contribution, not a second workload or latency definition. DEC-109 — continuity
acceptance ownership records this scope clarification. Runtime task S7.5a — envelope/timing oracle precedes
qualification here; task 6.8 — continuity evidence handoff supplies the sibling contribution to runtime task
S7.5b — dependency closure and task S7.5 — combined continuity acceptance. Numerical limits remain unproven.

**Concrete integration, not checkpoint parity alone:** today's `refreshPlaybackAfterCapturePassStateChange`
in `src/TrackUndo.cpp` calls `Track::silenceSlotMidiOutput`, which emits NoteOff for every active ledger key.
Task 3.14 — undo/redo audible handoff explicitly replaces that blanket playback-refresh behavior with a
request through the existing immutable publication owner and the cross-generation reconciliation above.
The valid serving source and closure obligations survive until the prepared boundary; unchanged notes are
not retriggered or closed. `TrackUndo` still owns undo semantics; the checkpoint-root/pinning migration remains
outside this task. The proposal's previous read-only treatment of undo consumers does not apply to this narrow
playback-refresh integration. Preview undo/redo uses task 3.13's existing immutable preview discipline.

Before the integration slice, its execution packet must enumerate capture-pass and overdub undo/redo callers,
the request-to-handoff identity and ledger transition oracle, including repeated operations/selection changes
between grains. Production-path native fixtures must fail under the present blanket silence and assert exact
MIDI output, retention and cleanup after the replacement. Task 3.7 — mutable fallback removal depends on it.

**Timing and fault boundary:** window/index/retention/page/closure work receives an allocation within the shared
runtime service and memory envelope. Page readiness is not physical output-throughput proof; each enabled MIDI
route needs the runtime's output measurements under simultaneous capture/finalization and persistence/display
contention. One-pass-late publication is audible-action latency, distinct from emission timing of the old revision.
Admission refusal and the approved affected-slot-silence last resort are preserved safety policies; neither is
a passing outcome during normal in-envelope operation. This change does not disable the runtime's separate
StopAdmissionContract recovery, nor certify that dependency merely by forbidding global stops in the window owner.

## Open Questions (deferrable; they do not change the specs or the task order)

- Whether the interval window and the loop's whole-loop window share one build entry point or two (a code-shape question;
  both satisfy the same requirements, and the whole-loop case is now framed as coverage spanning the loop).
- How the owner's turn is wired into the existing deferred owner enumeration (a turn kind versus a service point inside
  an existing turn) — the spec constrains the property (off-clock, named, bounded, reachable), not the enum value.
