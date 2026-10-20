**Latest hardware gate (2026-10-16): I5 — dedicated rolling HITL is partial, not acceptance.** [I5 — actual execution and continuation](evidence/2026-10-13-i5-running-capture-service/ROLLING-RECORD-HITL-EXECUTION.md). Device growth/source/wire/content/frozen-mismatch checks pass; following empty take fails and held/matching Stop are unverified. Fixed real Visual-owner private cancellation; latest stale completed idle→ready shell correction passes native3281/7skip/focused505/ASan10/host46/builds but is NOTuploaded. Added qualification-only fixed read snapshot, no resident state or budget changes; original metadata hash defect retained/corrected locally. Retry cap reached; exact pretest OFF image restored/postflightguard15/two drains/nolatch. Earlier no-device statements below describe prior local increments, not current evaluation status. Parents remain unchecked29/60.

## Current implementation increment — growing live Record coverage

DEC-134 — growing live Record coverage extends the existing Track request/three-view owner/reader to later bars, superseding DEC-133 — connected first-bar Record's initial-only implementation limit. [I5 — current contract, verification and remaining gates](evidence/2026-10-13-i5-running-capture-service/ROLLING-RECORD-COVERAGE.md): monotonic accepted-extent period grows only at a complete full-wrap handoff with explicit playback-origin reanchor; raw Capture coordinates unchanged, owed distinct next interval retained, same-geometry append work preserved, changed geometry rebuilt, and no legacy live fallback. Complete empty intervals, held NoteIds/no retrigger, nonzero origin, late-ready and matching subinterval/mismatching frozen Stop pass. Idle-role retention keeps all three views available. Fullnative3275pass7skip/focused500/ASan8/three controls/default+enabled buildsPASS/padding1160/952≥840/ABI unchanged/no fields. Total raw-source inspection remains extent-dependent and physical input-return/owner-IRQ/arrival/dense enabled-device gates remain unqualified; no hardware/upload/defaultactivation/parentcheckoff. Historical first-bar-only and earlier RED statements below are not current limitations for this functional increment.

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
| **the size pins** | `static_assert(sizeof(LoopPlaybackRuntime) == 7896)` (host ABI `10080`), `sizeof(Loop) == 1008`, `sizeof(TrackPlaybackRuntime) == 32` (`src/Track/TrackPlaybackRuntime.cpp:2943-2978`), each re-base carrying its reason at the assert; the display-window receipt key is pinned at `<= 32 B` in `DisplayNoteWindowGather.cpp` |

**Constraints.** Hot paths stay allocation-free; the RAM1 budget is enforced by `scripts/check_ram1_budget.py` with a
floor of **840 B** and invariant 36's **872 B** usable padding (a bank cross costs 32,768 B and reaches the documented
silent boot hang); new per-slot state belongs in the external `LoopPlaybackRuntime` — pinned at **7,896 B** on the
target ABI (**10,080 B** host), re-based deliberately with the reason recorded at the assert (the chain from DEC-107's
`6,352 -> 6,360` step through DEC-136 and DEC-147 is in `src/Track/TrackPlaybackRuntime.cpp:2935-2978`) — or is
`FLASHMEM`/`TRACK_COLD_MEM` constructed. Mutable capture-sampler state is `DMAMEM` (`.bss.dma`), never DTCM: the display
window receipt key is the most recent example. The external pool's ceiling is **measured** (a 24-bar heap run refused
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

**D6 — One rolling playback path per sounding slot (DEC-116 supersedes DEC-108 item 2 / the prior D6).** The earlier decision to
keep prepared-pair/fixed-view as permanent playback forms for admissible loops is withdrawn. The final playback form is
one immutable interval-window request/build/adopt/emission protocol under `Track::stepPlaybackViewMaintenance` for short
and long loops, Record return, overdub, NOTE_EDIT, layered/jam, launch, undo and restored content. Different source
freeze/pin adapters do **not** become different playback drivers. The existing prepared-pair, bridge, paged and merged
emission branches are transitional parity fallbacks, each retired at its documented verification gate; no accepted
content is lost to make them removable. DEC-108's remaining identity, service and source-retention rules still apply.
**Frozen Record interval inspection (2026-10-08 extension to D6, not another publisher).** A one-bar frozen `CaptureRange` window currently makes `RuntimePlaybackViewFill` inspect every sealed row before completion: a 24-bar/16-pair-per-bar Record stop at phase 473 has 768 rows, at least 24 × 32-row Fill grants before Order, and fewer than five 64-tick opportunities before the next bar. Preserve the Track owner and Loop-owned Capture source; give the sealed range a generation-qualified, incrementally maintained block-to-bar membership summary (including out-of-order accepted append). One bounded owner grain charges a summary inspection or a candidate row; skip only blocks *proven* irrelevant after final stop projection, never infer sortedness. A stale/unknown summary conservatively inspects the block and cannot publish partial content. The source index belongs to the accepted Capture storage, transfers with its sealed chunks, and is invalidated or maintained on every tick-changing rewrite; it is not a new derived playback view or content authority. The source-body/index cost must be measured in PSRAM/LoopPool and never added to Track's 384-byte DTCM shape. A source above the indexable envelope uses safe full inspection with an explicitly named deadline consequence; no silent acceptance or lowered lead. Frozen-source release, complete Snapshot publication, immutable current/ready/build and legacy fallback keep their existing contracts.


**Unaligned Record return / late-ready hypothesis (2026-10-09; not yet device-attributed).** The pinned indexed image committed 25 bars (`length=19200`, stop at local phase 18922, 278 ticks before wrap) and emitted no device channel-8 NoteOn until ~4.8 s later although persisted early bars contain notes. Ring integrity passed, but there is no captured view-role/adoption receipt; the `win_track_not_playing` gate names exclusion from the ordinary committed rolling requester while a Record job owns the return, *not* refusal of the separate frozen owner/reader. The frozen Track holder can prepare `ready` for bar zero near the wrap; `resolveQualificationPlaybackSourceForTurn` accepts a frozen ready only when `ready.coverage.startPhase == phase`, so a completed bar-zero ready after wrap cannot be adopted. Meanwhile `fillPlaybackHolders` can treat a complete ready covering the playhead as satisfying successor work, even though it covers the *current* bar and the next bar is owed. This is a code-grounded possible causal chain, not a proven state trace on the device. The 24-bar native oracle has a longer aligned setup and does not test that interleaving. The preferred existing-owner path is to classify a late ready by its own source/interval and missed boundary, retain the frozen job, and service the next owed interval rather than treating current-bar coverage as next-bar readiness. Any edge adoption at a crossed boundary must carry a cursor floor and preserve the active-note ledger; merely relaxing the `startPhase == phase` comparison or suppressing a stop CC123 would replay/strand notes. Freeze before-boundary and after-boundary 25-bar TrackManager-edge RED oracles before changing policy. No second scheduler, fourth view, stop-edge build, invented deadline, or whole-root gate.

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
  by whole-root residency; the retained source revision and interval index provide the window without requiring a resident whole-loop root, and the switch is accepted, retained and bound at a valid boundary with no second gesture (DEC-108 §10, revising DEC-104's report-only shape for this lane).
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

No runtime or data migration occurs merely by preparing this change. Implementation is staged so the new serving path is
integrated and proven while the existing merged-window fallback remains available as a compatibility/cold-reference path.
Task 3.7a connects `Track::requestPlaybackWindow` and the playback-window consumer to the retained-source interval index
over pinned bodies and the effective-content resolver; it also supplies the production caller integration owed by 3.12
(paged legacy stream) and 3.13 (preview source). The 3.14 and 4.5 handoff/closure proofs then run against that serving path.
Only after those dependencies, parity and timing gates pass does task 3.7b remove the fallback and reclassify
`ExecutionBounds.md` row 2. Rollback before 3.7b retains today's fallback; no SD format change is implied.

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
- The RAM1 line on a fresh target build, and the corrected size baselines (target `LoopPlaybackRuntime` **7,896 B**, `Loop` **1,008 B**; host `10,080 B`).
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

**Main delivery direction — service the audible obligation and its source, not a transport mode.** Keep one immutable `current` per sounding slot and one Track-owned `build`/`ready` publication path. Record, overdub, preview and committed content change the *source adapter*, never the clock-edge reader. A complete unchanged `current` continues over wraps without requesting a new build; after an accepted change, source qualification, interval construction, closure and adoption form **one affected-tick obligation** with an earliest deadline. The current outer `MidiServiceDrain::selectDeferredIdleOwner` rotates seven owner kinds; Record first-bar refresh is reached on VisualCache turns, Record/overdub stop grains have local precedence when their track's deferred turn runs, and Snapshot backlog has an extra opportunity. The inner playback turn chooses one track round-robin and its most urgent owing slot. None of those local rules alone guarantees that both a needed source and its playback successor get enough turns before the affected tick. Extend the **existing** deferred-owner arbitration with a bounded deadline-aware service opportunity for this obligation, including its already-owned Snapshot/frozen-source/stop prerequisites; do not create a second scheduler, always prioritize all RECORDING/OVERDUBBING work, or starve persistence, display, finalization or another sounding role. Preserve the cross-track playback rotation and within-track earliest deadline; prove an upper bound on each owner's denied turns and the total composed service gap. Changing that outer timing policy is a formal timing-model review before firmware, not authorization from this paragraph alone.
Only a source owner **actually required** for this interval is charged or promoted: the job-owned frozen Capture range can serve playback while the complete `LoopSnapshot` remains unpublished. A Snapshot turn becomes a playback prerequisite only when no already-qualified retained/Capture source can answer the owed interval. This preserves `Loop`'s atomic checkpoint authority without making whole-root publication the playback gate.

**Migration sequence and decisive oracle.** (1) Freeze production entry → owner → edge MIDI/ledger and source-lifetime traces, including a Record whose last accepted event arrives late, stop geometry differs, the whole `LoopSnapshot` is still private, and other tracks plus persistence/display owe turns; a new-reader removal must turn the trace RED. (2) Move live Capture, frozen Record, committed/retained, overdub and NOTE_EDIT onto the same request/`current`/`ready`/`build` lifecycle, preserving accepted input and the last coherent view through a source change. (3) Qualify remaining work per affected tick together with bounded source-owner and playback turns, at one-, four- and 64-bar lengths and all supported sounding roles; count a missed tick or named refusal as failure, not as a fallback success. (4) Only after parity, RAM1/PSRAM and pinned-device continuity pass for every route, retire temporary pair/bridge/first-Record/merged/paged **emission** selections under task 3.7b; keep a cold reference oracle. The earlier default-image post-stop channel-8 gap (2.121 s in the pinned status entry) is unresolved: a missing service turn is a hypothesis to measure alongside stale source, blocked holder, role readiness and edge adoption, not an attributed scheduler defect.

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

## DEC-116 — single rolling playback contract (design target; no firmware checkoff)

One **logical** driver is leased per sounding slot, not one shared mutable buffer for the whole device. The existing
`Track` playback owner has `current` (immutable reader), `ready` (complete immutable successor) and `build` (private
candidate), generation-qualified and released after every reader/source obligation ends. Its store remains window-sized
for any loop length; a short loop fitting the selected tunable span has whole-loop *coverage*, not a separate whole-take
publisher or a resident bundle on every recorded slot. A legacy dense/restored interval may use bounded pages behind
the same reader, with admission/closure capacity and no truncation. The provisional 2,048-event limit and span comparison
remain measurement inputs, not certified supported density or a mandate to widen every window to two bars.

The source adapter freezes one of: live Capture during Record; a job-owned sealed range through Record stop; selected
complete `LoopSnapshot` or retained revision with pinned bodies for committed content; live overdub delta; or
`EditManager`'s identity-qualified NOTE_EDIT delta with `origin == Preview` and a session lifecycle token. `Loop` keeps
canonical/whole-root publication, `EditManager` keeps its session, `TrackUndo` undo semantics, `StorageManager` SD and
`TrackManager` slot selection. The Track window owner selects/resolves the interval, owns the build and publishes once;
the one clock-edge reader adopts and emits through the existing cursor/ledger without a build, sort or source scan.
Record bar one is the same `current` role before and after stop, with a source/lifetime handoff rather than a separate
return-window publisher. Edits, undo, source loss, overdub wrap, transport seek/stop and slot changes preserve the
last coherent traversal and cancel/revalidate stale candidates through their generation and deadline contracts.

**Record is a source transition on the main window driver, not a first-bar playback mode.** From Record start, the existing Track window owner uses generation-qualified live Capture to prepare complete interval windows in private `build`/`ready` roles while `current` remains immutable. Accepted input can supersede an in-flight candidate; only a window whose frozen Capture generation includes the accepted rows for its covered interval may become audible. The next interval is requested and serviced ahead of use under the same role protocol as ordinary playback; neither a dedicated first-bar emitter nor a stop-only publisher supplies an alternative clock reader. The initial empty/raw first-bar reservation in today's implementation is a preparation resource, not evidence that later Capture rows are already in a complete audible window. Record input acceptance is not made contingent on a window build succeeding.

At stop, freeze the Record source and determine final length, projection and closure obligations. If the already-serving complete `current` covers the return interval and matches the frozen Capture generation **and final geometry/content**, transfer its source hold to the job-owned frozen range without copying or re-emitting its content; keep its emission cursor and ledger obligations. If it does not match (late append, length change, missing closure or refused preparation), do not relabel it as current: retain only valid old coverage for its permitted traversal, preserve the frozen range, and have the same off-clock owner prepare a corrected `ready` successor. Release the range only when the selected committed source and its needed windows really take over. The clock edge only qualifies/adopts completed views and emits; stop performs no content-proportional projection, sort or rebuild to make this handoff pass.

`Loop::stepPendingSnapshotPublish` may progress the whole `LoopSnapshot` while Record is active, but its candidate remains private until complete and atomic publication. The window owner may resolve a covered interval from the live or frozen Capture, and then from the selected complete Snapshot or a pinned retained committed revision; it must **not** read an unpublished partial Snapshot as an interval root. Source changes are not new drivers. Short-loop full coverage can be reused over wraps while unchanged; long-loop intervals are prepared ahead without scanning unrelated bars. Readiness under late input, Record stop and simultaneous holders remains a measured service/admission obligation, not a claim established by this source-handoff design; fallback output is not a passing deadline verdict.

**Minimum served `current` versus successor deadline (DEC-116 refinement).** An already complete, immutable `current` with the sounding slot's generation, source hold, revision/origin and interval coverage is *servable* now: its identity and content are tested at the read, not by reserving an extra bar of future service. If its source and revision remain valid, a short loop reuses this same view across wraps at **zero new build grains**. A changed source is different: a private `ready` successor must contain every accepted event and closure affecting its next traversal; the old `current` remains readable but cannot be relabeled as that new content. Currency and the one-pass-late fault rule remain separate from structural completeness.

For each accepted Record append, edit or source/geometry transition, compute the earliest affected playback tick from actual projection; the window owner charges the *remaining* work after that acceptance, including inspected source/index work, order, closures, publication, retained-source release and same-track sibling work. Preparation may advance incrementally while Capture grows; a new append may coalesce a superseded request but must not destroy useful progress toward an older deadline. The service contract to establish is `M + (G_remaining + K) × H × Δ ≤ D`, where `D` is the available tick distance to that affected tick, `H` includes all competing sounding tracks, `K` charges same-track work, `Δ` is a measured and enforced maximum advance between eligible service opportunities, and `M` is only a separately justified residual handoff margin. **No fixed one-bar margin applies to an already-served `current`, and 64 ticks alone is not the cost of a complete replacement.** Today's `leadTicks = Config::TICKS_PER_BAR` is a transitional admission policy, not the final product deadline or a certified Record bound. Even with `M = 0`, today's two-grain empty and three-grain two-event builds cost `2 × 8 × 64 = 1,024` and `3 × 8 × 64 = 1,536` ticks under the current worst-case owner allowance, both beyond a one-bar 768-tick return. The final protocol must prove smaller *remaining* work and/or stronger service under the supported simultaneous workload; do not merely delete the old guard. Stop-time final geometry and accepted closures are charged, not waived. If the bound cannot be met, name the refusal/miss and preserve only the prior valid traversal; neither dropping accepted input nor calling legacy fallback a readiness pass is allowed. Numeric envelope and hardware proof remain open.

**Per-sounding-slot admission, not active-slot-only admission.** `TrackManager` continues to own `activeLoopIndex` and the enabled/layered-slot decisions; those decisions identify which slots actually sound, not a second active-slot selector. For each sounding slot, including a non-active layered/jam slot, the Track owner must offer, service and publish its own generation-qualified interval obligation and the clock edge must read that slot's own completed window/cursor/ledger effects. A track-global playback generation may guard an operation but cannot by itself substitute for slot identity or cancel another still-sounding slot merely because the active slot changed. An inbound slot may prepare before its switch; after a switch, its still-valid window may be adopted only against the newly selected slot and source identity. Merely removing the current `slot != activeLoopIndex` guard without making the per-slot request, service and release terms coherent is not the contract.

**NOTE_EDIT is a source composition, not a second playback engine.** For either loop length, the window owner resolves the requested committed interval plus an identity-qualified frozen session delta. An empty *delta* leaves committed content sounding; an interval whose effective result has no events is still a complete, ready silent window, not a reason to retain an obsolete preview or reject the request. Opening, changing, undoing/redoing, re-focusing and exiting the session qualify preview source/committed-base dependencies and handoff; only soundings whose identities are removed or replaced close, while unaffected notes continue. The old whole-loop `sessionMidiEvents`/merged projection may serve during migration, but neither its materialization nor its emptiness predicate defines the final reader.

**Overdub has one scheduled-loop emission owner.** While live Capture overlays committed content, accepted overdub events and their closures enter the same ordered window cursor and ledger contract exactly once, including at loop wrap, stop and the transition to a committed pass. MIDI input passthrough/capture append remain distinct from scheduled playback; a separate clock-edge replay of the live Capture store is not retained beside the window reader, and the edge does not sort that store. If a window for the pre-seal source is already in traversal, preserve its identity and closures through adoption of a complete successor, with no double soundings, missed Offs or inline rebuild. These are cutover invariants, not a choice of overlay data structure or fixture.

**Migration order and release triggers:** pin every production route and independently expected MIDI/ledger trace
(short/long, empty, Record, overdub, layered/jam, preview mutation/undo/exit, switch, restored legacy density);
then move source adapters to one owner and one consumer behind a parity-controlled transition, keeping the old branch
only until that route's exact output, selective closures, capacity/refusal and memory-lifetime assertions pass. Complete
bounded service/tempo/clock-contention device measurement before enabling active admission or claiming continuity.
Remove all separate pair/fixed/merged/paged **emission** branches and reclassify the unbounded fallback only after the
last supported route passes; retain a cold oracle for tests. The sibling runtime change checks active-role residency,
simultaneous holders and combined timing/content acceptance; neither change gets a checkoff from a decision document.

**Pros/cons:** role-leased windows cost memory proportional to *sounding* roles and overlap but isolate each track and
keep immutable handoffs; one global physical buffer saves nominal bytes but cannot serve simultaneous immutable readers
without relocating the same bytes/cursors elsewhere and adding contention. A shared build scratch is a future measured
optimization, not the serving authority. Widening every physical window alone increases fill/storage cost and does not
fix missed adoption. See [DEC-116 — one rolling path per sounding slot](../../../docs/DECISION_LOG.md#dec-116--one-rolling-playback-path-per-sounding-slot)
and the [reassessment addendum](ARCHITECTURE-REASSESSMENT.md#2026-10-07-addendum--one-path-for-all-scheduled-loop-playback).

### I5 — running-Capture request and composed service draft (2026-10-13)

Owner-approved document capture only: [I5 — request/lifetime/service draft](evidence/2026-10-13-i5-running-capture-service/CONTRACT.md) grounds a live source recipe in existing Track pending/frozen/admitted records and three views, preserving oldest qualified position, useful work, source lifetimes and main's production service ordering. It is not firmware approval or a G1/G2 pass. The legal adoption boundary before the earliest changed event is the preparation deadline: the existing Off740 → boundary768 → On1468 oracle has **28 ticks for adoption**, not 728 ticks of freely usable preparation lead; mid-traversal replacement remains prohibited. Current full-restart measurements cannot stand in for the normative useful-progress requirement. Incremental certification, overlap/coalescing representation, initial running-epoch anchor and composed numeric service remain BLOCKED; no guard, lead, arbitration, envelope, requirement or task-checkoff change is made here.

The separately owner-approved [I5 — source-progress proposal](evidence/2026-10-13-i5-running-capture-service/SOURCE-PROGRESS.md) specifies Loop-owned append stability, frozen private extents and bounded prefix/delta sorting and merge/copy-back within the existing arrays. Ordinary append preserves raw prefix rows, but same-count sorting, ID mutation, removal/projection, transfer and reset invalidate that guarantee. Current fill/order APIs do not support the proposed extension. Exact new scalar-state cost against Loop992/runtime7632 target pins, exhaustive mutator coverage and stop/lifetime equivalence must be resolved; no size-neutrality, pin rebase, implementation approval or28tick service guarantee is conferred by saving it.

[I5 — owner/input timing reassessment](evidence/2026-10-13-i5-running-capture-service/OWNER-SERVICE-REASSESSMENT.md) records a further structural input-batch counterexample: externally aligned Off736 followed by four FIFO Clocks reaches768 inside one valid five-message batch, before main can prepare. More urgent main-only opportunities are therefore insufficient. Preferred next design work is transport-preserving resumable dispatch plus main-owned checkpoints/bounded owner arbitration; the timing model, continuation state/resource/lifetime and physical-clock health contract require explicit approval and proof before firmware changes. No smaller batch cap, clock delay, cross-track EDF replacement or inline build is approved.

User-approved [I5 — concrete timing-contract draft](evidence/2026-10-13-i5-running-capture-service/TIMING-CONTRACT.md) fixes a single MidiHandler route continuation preserving USB/DIN transport grouping and USB-host FIFO, main-owned checkpoints with mandatory normal-owner turns, candidate1080B continuation/checkpoint payload and conditional2*H*G sufficiency. Host model19,531traces/fivefocusednative599pass is not production continuation/WCET evidence. Extra turns may not hold Clocks until a virtual deadline passes: independent physical reference/input/clock budgets and enforced child costs remain gates. Next is a separately scoped fixture-first qualification measurement slice; no firmware timing/quanta/budget approval or parent checkoff from this draft.

**I5 — local building-block status (2026-10-13):** [I5 — native continuation](evidence/2026-10-13-i5-running-capture-service/CONTINUATION-EXECUTION.md) delivers native-gated retained-batch mechanics only, actual1056B/no target enablement. [I5 — checkpoint verification](evidence/2026-10-13-i5-running-capture-service/CHECKPOINT-EXECUTION.md) delivers passive caller-owned saved-normal arbitration and selected-owner cost preview/commit under DEC-123 — checkpoint arbitration and deadline admission. PhysicalClockStamp requires extended64bit microseconds plus nonzero producer era; a fullwrap regression disproved the earlier micros32 half-range guard. Logical/physical expiry, invalidity and overflow refuse without spending cursor/debt. Actual owed-owner cursor2B, deadline48B/stamp16B/cost12B/decision4B/table84B on bothABIs; draft1080B is not an implemented aggregate. Focused69/qualified16/fullnative3127pass7skip/defaultcaptureSUCCESS/RAM1unchanged; one focusedreview/two coverage omissions closed and removalcontrolsRED/restored. No main execution wiring, real physical producer or measured/enforced owner cost, source-progress implementation, target continuation activation or readiness/HITL claim. The conditional service bound remains unqualified; preserving debt on refusal does not prove useful progress or28tick adoption readiness.

**I5 — physical-reference contract draft captured (2026-10-13; proposed, not accepted/implemented):** [I5 — physical-reference draft](evidence/2026-10-13-i5-running-capture-service/PHYSICAL-REFERENCE-CONTRACT.md) records user-authorized installed framework1.159.0 timer/transport inspection. Preferred candidate is ClockManager-owned hardware-chained PIT elapsedtime with cold pair reservation, finite coherent reads/nonrecycled era; exact hardware semantics/resource/sharedPIT debugpolicy remain unqualified. RawRTC is reset by existing RtcTime corrections; micros32/DWT/SysTick do not supply unlimited elapsedtime. USB-device/host/DIN queues carry payload without arrivalstamps, so elapsedsource and ingressdeadline provenance are separate proofs. Proposal preserves existing queue/content/tick ownership and requires receipt lifetime/correlation/transport-rate invalidation plus complete owner-to-input costs. No timer reservation, transport hook/fork, metadata layout, numericbudget, reference invalidation change or targetenablement accepted; G1 — architecture stays open. Capture only/no newDEC, firmware/dependency/build/device/SD/Git/taskcheckoff; progress29/60 unchanged.

**I5 — private delta ordering implemented (2026-10-13):** Owner subsequently approved firmware/native implementation toward the named running-Capture criterion. [I5 — delta-order execution](evidence/2026-10-13-i5-running-capture-service/DELTA-ORDER-EXECUTION.md) closes the specified rawprivate order prerequisite under DEC-124 — bounded private append ordering, using frozen suffixes and existing phase/order/scratch arrays; no new view or source currency. Caller certifies unchanged prefix/projection/provenance/retention/lifetime; typedview/borrowedindex extensions refuse. Suffix work and full merge/copyback are quantum-limited and separately priced in entry operations. Fourcases/270matrix/focused245/fullnative3131pass7skip/defaultcaptureSUCCESS; builder72target/136host/runtime7632/9768/padding1000unchanged, one review no defects/removalcontrolRED-restored. Track live source receipts/request/initialanchor/stopclosure and actual physical producer/ingress/owner-return costs remain unimplemented/unqualified. Installeddependency edits remain prohibited; supportedreceivehook or project-owned maintainedtransport receipt integration/support policy must be selected before all-route physicalprovenance claim. No defaultgate/targetactivation, device/SD/Git or parent checkoff.

**I5 — ingress correspondence implementation (2026-10-13):** Subsequent owner approval selects DEC-125 — pinned receiver ingress receipts, superseding the prior unselected integration policy. Existing queues own exact-slot metadata and MidiHandler acquired/native-retained copies; insertion-only hash-pinned generated sources preserve installed files/licenses/payload order. DefaultOFF/no new payload queue or hot metadata allocation. Reset/DINoverflow/hostdisconnect/samplerreplacement invalidate currency, not accepted payload; lateFIFO/completing-byte observations are not whole-frame/sender arrival. [I5 — ingress execution](evidence/2026-10-13-i5-running-capture-service/INGRESS-EXECUTION.md) records actual-body/parser and all-dispatch correspondence fixtures/controls, one focused review/coverage correction, fullnative3154pass7skip118suites/default+ingressSUCCESS. Receipt32/cell40/State19168target19192host/enabledcontinuation5152B; ordinary128batch adds4096Bstackpayload/input5352compilerframe. FLASH-placement preserves existing floor840; numeric budgets/pins unchanged. No physical source/era lifecycle/correlation/arrival bounds, whole execution/return costs, live source receipt/request/anchor/stop/readiness, targetactivation/device/SD/Git or parentcheckoff. Progress29/60 and defaultrollingOFF/nativeonlycontinuation remain.

**I5 — raw-prefix source-stability prerequisite (2026-10-13):** DEC-126 — raw-prefix source stability implements distinct terminal LoopEventStore destructive-prefix and pool-owned static-alias tokens, caller-owned frozen extent/forward coordinates, one-row-or-chunk step and explicit suffix extension. Accepted append preserves earlier raw rows; destructive instance operations/grains and static ID/reference-release aliases invalidate independently of legacy worker generations. Caller retains physical store/chunks/excludes mutable workers/discards before destruction; raw-prefix token is not Loop interval/phase/projection, physical lease or playback currency. No Track borrower or changed live adapter/publication. Measured store+8B/targetcursor48/hostcursor64/Loop1000target/runtime7640target/stopring29064target/Track384unchanged; pins measured, numericalRAM1budgetunchanged. Source15/fullnative3169pass7skip119suites/defaultcaptureSUCCESS/padding1016≥840/controls8-3RED-restored/onefocusedreview no concrete defect under preconditions. [I5 — source-stability packet and evidence](evidence/2026-10-13-i5-running-capture-service/SOURCE-STABILITY-EXECUTION.md). Next Loop semantic identity and Track cancellation/retention before adapter/fill integration, accounting for conservative unrelated static-retirement invalidation; request/anchor/coalescing/adoption/stop and physical/input costs remain open. No740→768 readiness, targetactivation/upload/device/SD/Git/parentcheckoff; progress29/60/defaultrollingOFF unchanged.

**I5 — semantic live-source retention integration (2026-10-13):** DEC-127 — semantic live-source retention extends existing owners with terminal assignment-qualified phase identity and exact projection/raw receipts frozen at descriptor creation. Live fill advances Row/Chunk rather than ordinal replay and preserves strict content/count currency through completion. Both runtime builders name borrows until discard; release/composite guards refuse locally, maintenance cancels stale work before holder selection, and Track/runtime destructors detach before source/destination destruction. Standalone callers retain/discard explicitly; no new lease registry/view/queue/publisher. [I5 — integration evidence](evidence/2026-10-13-i5-running-capture-service/LIVE-SOURCE-INTEGRATION.md) records focused418/fullnative3178passed7skipped119suites/defaultcaptureSUCCESS/padding1720≥840/controls3-2-1RED-restored and one focused review. Target Loop1008/runtime7792/Track384/stopring29064; incremental64shells+8runtimes1728B payload plus other Capture/descriptor stack costs, no numeric budget relaxation. These are serialized/retained guarantees, NOT ISR/main synchronization: internal timer ticks can transition/reset Track. Before target activation qualify shared read/cancel/adopt protocol and complete owner/IRQ return costs. Running request/initial anchor/append coalescing/adoption/stop and physical/input bounds still gate740→768 readiness. Proposed two-channel PIT/resource/shared-debug policy needs owner decision before target producer; ingress permission is resolved, counter alone does not bound sender delay/external pacing. No parentcheckoff/upload/HITL/device/SD/Git/default activation;29/60/defaultrollingOFF/targetcontinuationdisabled.

**I5 — ClockManager PIT elapsed-reference prerequisite (2026-10-13):** Subsequent user approval resolves the resource/sharedpolicy choice under DEC-128 — ClockManager PIT elapsed reference. [I5 — physical producer execution](evidence/2026-10-13-i5-running-capture-service/PHYSICAL-PRODUCER-EXECUTION.md) pins NXP RM exact0/1pair/start/highthenlatchedlow/stickyhighflag and implements coldallfourdisabled reservation before real MIDI timerbegin, result-qualified availability, fixed masked reads, nonrecycled era/terminaloverflow/backwards/configuration refusal. StockFRZ1/Runmodeonly; debugger/sleep/unseenstop-restore unsupported, explicitinvalidationfuture. Optional existing ingress sampler timestamps observations, notsenderarrival/deadline. State16BRAM2/linked+32B/no DTCMmember/pin/numericbudget change; ordinary24/ingress28/combinedcontinuation30/fullnative3188passed7skipped119suites/default+ingressSUCCESS/padding1688-1912≥840. Controls1/1/1specificRED restoredbyteidentical; one focused review no concrete defect under contract. Countermechanics locallyverified only; device start/latch/flags/accuracy/latency/errata/correlation/delay/pacing/completecost and sourceISR/mainprotocol remain open. No checkpointdispatch/740→768readiness/parentcheckoff/rollingactivation/upload/HITL/device/SD/Git;29/60/defaultrollingOFF/targetcontinuationdisabled/receiptsdefaultOFF.

**I5 — stopped-only PIT hardware qualification (2026-10-14):** [I5 — hardware qualification](evidence/2026-10-13-i5-running-capture-service/PHYSICAL-HARDWARE-QUALIFICATION.md) delivers request-only capture-gated ClockManager observations via existing serial/capture/managed lifecycle owners, no state/queue/hotpoll. Dedicated TEMP preset210s/full01/211matchedpoints PASS through natural first lowcarry, Readyera2/configqualified/MIDIch2/highTIFclear. Owner approved DEC-129 — Run-mode physical-clock debug admission after runningboardC_DEBUGEN1/S_HALT0/S_SLEEP0; C_DEBUGEN diagnostic, actualhalt/sleeprefuse, FRZ1/unsupported unseeninterruptioncontinuity unchanged. Hostrelative23.967714–24.058948MHz grossinterval, observedDWTread11.970–34.090µs/median29.555 and stoppedtick0epoch1physicalbrackets7–22µs do not imply calibration/WCET/movingclock/deadline. Focused84/ingressnative33/host102/fullnative3196pass7skip119suites/defaultSUCCESS/padding1688≥840; one focused review fenceordering repair retested. Qualifieduploaded9b50680a…b770c/2drains/postflightcomplete/nolatch; latercachedf3d02128…d9581NOTuploaded. No content/SDcleanup/destructive commands/Git/parentcheckoff;29/60/defaultrollingOFF/targetcontinuationdisabled/receiptsdefaultOFF. Further movingcorrelation/arrivalage/pacing/wholecost/errata/concurrency/runningrequest/adoption gates remain separate.

**I5 — guarded moving observations (2026-10-14):** [I5 — motion qualification](evidence/2026-10-13-i5-running-capture-service/PHYSICAL-MOTION-QUALIFICATION.md) extends existing snapshot/serial/capture/managed owners, stack-only period field/unchanged56BABI/no new residentstate or hot probe. Separate motion schema leaves stopped `pit_obs` intact; all-track Capture/pending action and storage guard before every Start.51Readyera2rows/15movingpoints/internal+USB-device120/300BPM establish sampled point consistency only; internalperiod residual and hostsend catch-up distributions are not calibratedaccuracy/due-time/wireage/WCET. Exact retained/uploaded96e4160aHEX/c801239eELF; canonical2drains/postflightcomplete/no latch. Native3203pass7skip119suites/host105/defaultSUCCESS/padding1688unchanged; one focusedreview2findings fixed/retested. [I5 — complete owner/IRQ qualification gates](evidence/2026-10-13-i5-running-capture-service/OWNER-IRQ-QUALIFICATION.md) enumerates actual shared/multitrack/conditionalpoll/TX/SD/allocator/mask/IRQ costs; finite admission remainsNOTREADY. No independent calibrated reference/onlyhostUSB-deviceinput; no calibration/arrivalage proof or policy/default/parentcheckoff change,29/60/rollingOFF/targetcontinuationdisabled/receiptsdefaultOFF.


## Open Questions (deferrable; they do not change the specs or the task order)

- How the owner's turn is wired into the existing deferred owner enumeration (a turn kind versus a service point inside
  an existing turn) — the spec constrains the property (off-clock, named, bounded, reachable), not the enum value.

<a id="s315-investigation-handoff"></a>

**Capture note:** the report below describes the preceding read-only investigation; this appendix and
its task/runtime links were subsequently saved with explicit owner approval. Its internal slice labels
are local handoff labels, not new OpenSpec tasks or approved gate results.

## 3.15a–f — one reader per sounding slot: investigation handoff (2026-10-09)

**Status: revised investigation draft, saved with owner permission; not implementation approval.**
One independent assessment returned `REVISE` with five concrete findings; the author incorporated the
corrections below, but the revision was not reassessed. No firmware, tests, builds, device operations,
task checkoffs or default-reader enablement were performed by this investigation. Source navigation
reflects the investigated dirty worktree on `feature/unified-rolling-playback-path`; verify function
anchors against live code before applying. Existing requirements and the preceding design contract
take precedence over this handoff, especially matching-current preservation at Record stop, future
source obligations, scheduled overdub migration and affected-tick service. A proposed fixture or
transition below is not evidence that it has run or that its complete implementation contract is ready.
Capture one execution packet per eventual implementation slice; keep blocked or unclaimed routes explicit.

### Correction checklist (5 findings)

| # | Finding | Resolution |
|---|---|---|
| 1 | S2 lacked stopped-job lifetime/release transitions | **Fixed** — §S2 now carries a full state table grounded in `Track::stepDeferredRecordRetirement` (`src/Track/TrackOverdubLifecycle.cpp:1899-2050`), `LoopPlaybackRuntime::releaseFrozenRecordWindow` (`src/Track/TrackPlaybackRuntime.cpp:2142-2159`), `Track::retireJobCaptureSourceGrain` (`TrackOverdubLifecycle.cpp:426-445`), `hasBoundRecordReturnWindowForPendingJob` (`TrackPlaybackRuntime.cpp:2020-2031`), with one named release point and one acknowledgement surrogate. |
| 2 | S4 promised empty effective coverage without naming changes | **Fixed** — §S4 names the two refusal predicates to replace (`TrackPlaybackWindowBuild.cpp:5496-5499`, `:5584-5589`), the demoted-only gate (`:5574-5579`), the interval-source adapter, the edge hook, and a stale-preview-retention RED control. |
| 3 | S1a left epoch handling as a decision | **Fixed** — §S1a/S1b adopt option (a): **native epoch-0 only**; nonzero epoch is a registered expected-RED **non-claim**, and the production (device-epoch) live-Record gate is named as remaining and unclaimed. |
| 4 | S5 did not separate paged source from second edge reader | **Fixed** — §S5 cites `beginPagedPlaybackForSlot` (`:2767`), `consumePagedPlaybackWindowForSlot` (`:2996`), the independent edge branch (`TrackPlaybackHotPath.cpp:321-347`) and the one-cursor/one-ledger contract (`:73-96`), with a transition oracle and a removal control. |
| 5 | S3's “authority decision” | **Fixed** — **no policy expansion**: existing gesture semantics are the default (`MidiButtonActions::executeAction(OVERDUB_FOR_SLOT)` `src/MidiButtonActions.cpp:339-341` → `handleOverdubForSlot` `:362`, declared `include/MidiButtonActions.h:56`; the loop-row double press keeps the outgoing slot enabled). The two API-only `requestSlotSwitch`/CC123 fixtures are **out of scope, unchanged, expected-RED**; S3's executable part is the two-sounding-slot readiness gap. |

**Correction to my own prior text:** `scripts/check_ram1_budget.py` has **no CLI** (`grep __main__|argparse|sys.argv` → no match; it ends with `Import("env")` + `AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _run)`). It runs automatically because `platformio.ini:26` registers `extra_scripts = post:scripts/check_ram1_budget.py`. The only honest invocation is the build itself.

### 1. Request

Advance `windowed-playback-generation` **3.15a–f — one reader per sounding slot** by implementable, ordered slices. **Non-goals:** any firmware/test/build/flash/SD/MIDI action in this session; default rolling enablement; legacy-emitter/test removal; lowering `leadTicks`; counting fallback output as a pass; new Manager/scheduler/queue/fourth view/second active-slot authority/SD-format change; new switch/CC123 policy.

### 2. G0: ownership, deferred work, source release, cancellation

**Owners (each cited):** `Track` — per-slot `LoopPlaybackRuntime` (`playbackRuntime.slotIfAllocated`), the three-view set `current`/`ready`/`build` (`include/RuntimePlaybackView.h:28`), `rolling` frame, `pendingViewRequest_`/`frozenViewRequest`, `retainedSources`, `pagedEmission`; **`Loop`** — Capture store + `captureContentGeneration()`, `sealCaptureRangeForJob`, pass set, `publishedSnapshotIdentity()`; **`TrackManager`** — `activeLoopIndex`, enabled/layered slot set, deferred-owner rotation (`MidiServiceDrain::selectDeferredIdleOwner`); **`EditManager`** — `sessionPlaybackPreviewRevision_` (bump `src/EditManager.cpp:219`, reset `EditManager/NoteEditSelection.cpp:48`) + session projection; **`Loop::stepPendingSnapshotPublish`** — whole-root publication; **`StorageManager`** — persistence drain.

**Deferred work:** `Track::stepPlaybackViewMaintenance` (one declared grain/turn; entry `src/Track/TrackPlaybackWindowBuild.cpp:4905`), `Track::stepDeferredRecordRetirement` (bounded, stall-escaped), `Track::serviceCaptureStopDescriptorReservations`, `Track::processSnapshotOwnerTurn`, `Track::stepDeferredOverdubStop`, one background construction unit in `src/main.cpp`. Edge entries: `TrackManager::updateAllTracks` → `Track::playMidiEvents`/`playMidiEventsForSlot` (`src/Track/TrackPlaybackHotPath.cpp:552-576`) → `playCommittedLoopMidi` (`:202`).

**Source release points (exhaustive for this route):** `releaseFrozenRecordWindow` (`TrackPlaybackRuntime.cpp:2142`; refuses while launch preparation or a coverage-owned build is active/pending; clears `current`/`ready` metadata **except** `firstRecordReturnWindow`); `retireJobCaptureSourceGrain` (`TrackOverdubLifecycle.cpp:426`; one `rawRange.releaseGrain(1)` + `loop.releaseSealedCaptureRangeBorrow(&job.admission)` only while the job identity matches; `ownsRawRange=false`; `reserveTail()` re-arm, else `serviceCaptureStopDescriptorReservations` on later idle turns); `discardPagedWindow`, `discardCoverageBuild`, `retireCoverageSource`, `tryReleaseRetainedPlaybackSource`, `releaseEligiblePlaybackRuntimeSlots`.

**Cancellation:** `applyPlaybackViewTransition(ClearOrLoad)` (candidate released, prepared successor retired, operation identity invalidated — `TrackPlaybackWindowBuild.cpp:3050+`), `ViewBuild::discard(preserveDestination)`, `cancelPreviewWindowForSlot` (`SourceMutated`/`SelectedTrackChanged`/`SessionExited`, `:5637+`), `retireSupersededReady`, epoch invalidation → `DeadlineInvalidatedEpoch`, `noLiveOwnerForSlot` (named owner list, `:3123`), `cancelRollingPlaybackWindow` (`:4050`).

**Reader acknowledgement — the honest gap:** there is **no** reader-acknowledgement API. The only place a *served* window identity gates a release is inside `stepDeferredRecordRetirement` (`:1980-2006`): `currentRootWindowServed` (same committed revision **and** `snapshotIdentity == loop.publishedSnapshotIdentity()`) or `frozenCaptureServed` (origin `Capture` + exact `frozenCaptureContentGeneration`). Either returns `Pending`; otherwise `if (!currentRootWindowServed) runtime->releaseFrozenRecordWindow()`. **Consequence the plan must respect:** a *live* Capture `current` carries the pre-transfer token, so it satisfies **neither** predicate — publishing it without extending this acknowledgement would let `releaseFrozenRecordWindow` discard the frozen `ready` metadata that the stop handoff needs. That is the exact hazard S2 resolves.

### 3. Slices

#### S1a — **first executable slice** (test-only, native epoch-0)

- **Files:** `test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp` (gated by `UNIFIED_RECORD_CAPTURE_INITIAL_ORACLE`; harness `test/test_support/track_harness.inc`).
- **Extend** `test_record_capture_initial_window_reaches_unified_reader` (currently RED at `owner progress=0 / raw=1 / loopLen=0`) with: (i) after the bounded owner turns, a **complete published** `Capture`-origin `current` with `PlaybackCoverageInterval{0, TICKS_PER_BAR, TICKS_PER_BAR}` and `captureContentGeneration` equal to `loop.captureContentGeneration()`; (ii) the **raw first-bar shell retired off-clock** — `set->views[current].rawCoveragePrepared == false` and no `firstRecordReturnWindow` on `current`; (iii) one stamped edge producing exact independently written ordered MIDI + `(channel,pitch)` ledger and duplicate-edge silence; (iv) **new** `test_live_capture_current_survives_accepted_input_and_is_superseded_by_the_frozen_ready` (accepted NoteOn after publication cancels only the private candidate; a real `Track::stopRecording` then supersedes the live `current` with the frozen bar-2 `ready`, which the retirement predicate acknowledges).
- **Non-claim, explicitly registered:** `test_live_capture_current_at_nonzero_clock_epoch_is_not_claimed` — expects RED at the epoch-0 guard and is documented as *not* a defect. S1b does **not** touch `clock.epoch == 0`.
- **Controls (restored byte-identically):** (C1) withhold the Track owner grant → readiness RED; (C2) suppress the rolling/Capture selection in `resolveQualificationPlaybackSourceForTurn` (`:849-931`) → adoption RED; (C3) suppress the supersession call → the new case RED.
- **Commands:** `PLATFORMIO_BUILD_FLAGS='-DUNIFIED_RECORD_CAPTURE_INITIAL_ORACLE' pio test -e native -f test_runtime_working_set_ownership`; then `pio test -e native -f test_runtime_working_set_ownership`.
- **Done when:** (i)–(iv) GREEN, (C1)–(C3) RED-then-restored, the non-claim case documented RED, unflagged focused + full native green.

#### S1b — firmware: live Capture `current` published (epoch-0 branch only)

- **Anchor:** `Track::stepPlaybackViewMaintenance` live-Capture branch, `TrackPlaybackWindowBuild.cpp:4929-4976` (guard `slot == activeLoopIndex && isRecording() && clock.epoch == 0 && !hasPendingDeferredOverdubStop() && !hasQueuedPlaybackStart() && rolling.phase == Idle && coldRecordCaptureShell(...)`); completion terminal at `:4966-4974`; `finishPlaybackViewGrain`; the comment at `:4964-4965` (*"Do not retire the raw return shell until its frozen-job and one-reader replacements are linked"*) is the change being made coherent.
- **Change:** on Complete, promote the candidate to `current` (three-view promotion, no fourth view) with identity `{loop.playbackRevision or frozen pre-publication revision, playbackGeneration, nullptr, PlaybackWindowOrigin::Capture, captureContentGeneration}` and retire the raw shell's `current` metadata in the **same** owner turn. Do **not** touch `TrackPlaybackHotPath.cpp:263-284` (return-window branches) in this slice.
- **Predicates:** *admission* — candidate complete ∧ identity-qualified ∧ no louder obligation on `current`; *supersession* — `captureContentGeneration` moved or a frozen `ready` exists; *release* — the view is unborrowed and superseded/stopped; *cancellation* — accepted input, epoch change, transport stop, slot exit, clear/load, undo (existing `applyPlaybackViewTransition(ClearOrLoad)`).
- **Oracle:** S1a GREEN; owner-grant and selection-removal controls stay separate.
- **Honest gate:** this slice is inside the native epoch-0 branch. **Device-epoch live first-Record Capture on the one reader remains open and unclaimed**; naming it is required in the closeout, and no default/qualification image behaviour changes.

#### S2 — firmware: stop handoff and the single release point

**Current contract (2026-10-18): DEC-142 — Record presentation lifetime split.** The earlier ring-owned
retirement table is superseded: the slot-owned presentation holds the stable extent from admission; real
pre-publication Record finalization can prepare/serve frozen onward coverage. At nonempty `RecordPublished`,
`publishRecordPresentation` activates the runtime presentation role, clears the finalizer and pops the FIFO
synchronously. Reader/root-return completion is independent of the next interaction's admission/service.

| State | Owner and next transition |
|---|---|
| Real admitted Record, before publication | Matching live finalizer + slot presentation extent; production holders prepare frozen windows off-clock; reader adopts complete work only |
| Nonempty Record publication | Stable extent stays in place; runtime presentation owner activated; finalizer/FIFO released in the same transition |
| Root queued/stale or frozen reader still served | Existing Snapshot and window owners progress independently; presentation retains source; accepted interactions remain serviceable |
| Root-backed window actually adopted | Ordinary deferred maintenance services presentation retirement even after window holder has no further work |
| Stopped complete-unadopted successor | Existing bounded expiry through build discard/release; never an unbounded FIFO hold |
| Partial extent release | One chunk per turn; presentation stays active and extent-owned until release completes |
| Release complete | Drop matching Loop borrow, re-arm descriptor, clear generation-qualified runtime presentation owner and presentation identity |
| Empty take | No presentation promoted; existing RecordPublished ring dispatch finishes bounded release and pops the job |
| Dormant boot/load descriptor | Stable descriptor only, no reserved tail; cold capture admission reserves the requested slot tail |

Source qualification checks interval-derived presentation generation, slot/LoopId/owner/geometry/content identity
and matching reader owner (live finalizer or published presentation). Copied coverage under default compatibility
is Committed-origin/token-zero; rolling qualification is Capture-origin with the frozen content token.
Verification, negative controls and remaining device gates:
[`PRESENTATION-OWNER-NATIVE.md`](evidence/2026-10-18-rolling-base-2bar-rerun/PRESENTATION-OWNER-NATIVE.md).

#### S3 — firmware: two sounding slots, existing gesture semantics

- **Anchor:** `Track::resolveRollingPlaybackWindow` (`TrackPlaybackWindowBuild.cpp:4232`; reject clause `:4253-4259`), `Track::serviceRollingPlaybackWindow` (`:4033-4052`, non-active retention via `coldRollingShape`/`coldLayeredSlotFrameForEmission`/`slotMayOwnColdFullLoopWindow`), `Track::resetPlaybackStateForSlot`, `MidiButtonActions::handleOverdubForSlot` (`src/MidiButtonActions.cpp:362`).
- **Executable target:** `test_two_sounding_slots_keep_distinct_readers_through_selection` (currently RED: after 512 bounded turns **neither** slot publishes a complete new-reader `ready`). Make both publish their own `(slot, generation, revision, interval)` ready and adopt at their own phase origin, without the active-slot change evicting the still-enabled outgoing slot.
- **Default (no new policy):** loop-row double press → `OVERDUB_FOR_SLOT` keeps the outgoing slot enabled — that is current behaviour and the only in-scope gesture. The two `requestSlotSwitch`/12×CC123 fixtures remain **expected-RED, out of scope, unchanged**; do not implement conditional CC123 suppression (a prior attempt lost outgoing `Off@800` on `commitQueuedPlaybackStart`'s re-anchor — `CURRENT_WORK.md:5714`). `TrackManager` keeps `activeLoopIndex` and the enabled/layered decisions; no second authority.
- **RED control:** substitute the shared Track projection origin for the non-active slot → the outgoing `Off@800` oracle RED (already recorded as a working mutation).

#### S4 — firmware: NOTE_EDIT interval reader, including an empty effective interval

- **Anchor:** `Track::requestPreviewWindow` (`:5487-5529`), `Track::servicePreviewWindowForNoteEditSession` (`:5549-5635`), `Track::cancelPreviewWindowForSlot` (`:5637+`), edge `fixedViewEligible` (`TrackPlaybackHotPath.cpp:219-220`), existing gather-scratch pattern `Track::gatherRetainedSourceWindowForSlot`, `playbackWindowInterval`.
- **Exact predicate replacements (the finding's demand):**
 1. `requestPreviewWindow` `:5496-5499` — replace *`source == nullptr || sourceCount == 0` ⇒ `NotServable`* with: refuse only when the **identity/composition is absent**; an empty-but-identity-qualified composition is a complete interval and must be `Admitted`. Delete the whole-loop `playbackWindowInterval(loop.loopLengthTicks,false,0,coverage)` (`:5508-5511`) in favour of the requested interval + delta.
 2. `servicePreviewWindowForNoteEditSession` `:5584-5589` — the empty-`sessionMidiEvents()` refusal goes; emptiness is no longer an input.
 3. `:5574-5579` — the demoted-only gate (`!loop.shouldAvoidFullVisualRebuild(...)`) is replaced by interval qualification for both loop lengths.
- **Interval-source adapter:** a caller-owned scratch that composes committed interval content with identity-qualified changed/hidden/added/deleted rows and hands the result to `requestPlaybackViewMaintenance` under `origin == Preview`, mirroring how the committed route feeds `requestPlaybackWindow` (`:5483`).
- **Edge hook:** a selector branch for `editManager.isNoteEditActive()` on the active slot, placed beside the frozen-Capture branch (`:849-931`), adopting only `Preview`-origin identities; `TrackPlaybackHotPath.cpp:348-354` already keeps a non-`Idle` rolling frame audible, so a complete silent preview is not dropped. The merged NOTE_EDIT path stays the **named compatibility fallback** until 3.7b.
- **RED controls:** (i) suppress the edge hook → adoption RED; (ii) make the empty composition refuse again → the empty-interval case RED; (iii) **stale-preview retention**: keep the previous preview serving after the deletion — the fixture must fail (proves the new ready replaced it), and restoring the old behaviour must fail too (asymmetry recorded).
- **Commands:** `pio test -e native -f test_windowed_preview_integration` and `-f test_runtime_working_set_ownership`.

#### S5 — firmware: pages become an internal source of the same reader

- **Anchor:** `Track::beginPagedPlaybackForSlot` (`:2767-2850`: requires `runtime->frozenViewRequest.active`, `SourceKind::CommittedSnapshot`, retained source, refuses `PinnedEditRowsUnresolved` by name), `Track::stepPagedPlaybackForSlot`, `Track::consumePagedPlaybackWindowForSlot` (`:2996-3045`: `StaleRevision`, `NeedsPage`, `Exhausted` → `discardPagedWindow`), `WindowPagedStream`; edge branch `runtime.pagedEmission.bound` + `admittedForEmission()` + `servicePagedPassBoundaryForTurn` (`TrackPlaybackHotPath.cpp:321-331`), `eventCount = pagedEmission ? 1u` (`:347`), merged fallback (`:335`).
- **Transition oracle (decidable):** for one restored over-`L` interval, assert (a) the reader emits every accepted row exactly once, in order, with the same `(channel,pitch)` ledger closure as the committed oracle; (b) wrap does not duplicate or omit (`advancePlaybackCursor` with `playbackCursorAdvanceSend` `:73-79` is the **single** ledger/send owner — pages must feed that cursor, not a parallel sender); (c) `NeedsPage` during a traversal keeps the prior window readable rather than tearing; (d) `StaleRevision` names a miss, never a silent re-serve.
- **One-cursor/one-ledger rule to state in code comments:** pages are a *source* of the slot's window; the page cursor is the window cursor's backing, never a second emission cursor. **Removal control:** disable the page-as-source mapping → the restored-density oracle RED; only after it stays GREEN with the independent `pagedEmission` edge branch removed may that branch retire (3.7b), with the branch's own fixture polarity recorded.
- **Out of scope:** same-tick accumulation and closure-capacity refusal (`kWindowAdmissionLimit`, `kWindowClosureReserve`) stay at their owning boundary; the pager's `PinnedEditRowsUnresolved` refusal stays.

#### S6 — root-current next wrap: executable fixture half + measurement-gated half

- **Verified cause (no speculation):** `canCompleteWithinLead` requires `available ≥ leadTicks` **first** (`src/Track/RuntimePlaybackViewBuild.cpp:244`) with `leadTicks = Config::TICKS_PER_BAR = 768` (`TrackPlaybackWindowBuild.cpp:3346`, `include/Track.h:560`); the failing request at bar-two phase1008 of a 1,536-tick loop has 528 → refused before grains are considered. Grains would refuse anyway: `turns = grains × 8`, `2 grains ⇒ 1,792 ticks` needed (verified arithmetic over `deriveGrainsFrom` `:3286-3299`).
- **Executable now (native fixture, no policy change):** pin the role/admission contract for private root work while a Capture successor is complete, using the already-parameterized `coverageRoleAvailable(bool keepCaptureReady)` (`include/TrackPlaybackRuntime.h:823`, used `:4699`, `:5120`) and the recorded `rootQualificationWaitsForCaptureReady` / `AwaitAdmission` behaviour.
- **Genuinely blocked half:** admitting the root `ready` with < 768 ticks of lead requires a measured composed service envelope (task 3.10 / 6.3 / 6.5) per design.md's `M + (G_remaining + K) × H × Δ ≤ D`. Stop criterion: **measured** device/service evidence, or an explicit decision to keep the refusal. Do not lower `leadTicks`, credit legacy output, or re-grant deadlines.

#### S7 — 3.15f, route-by-route decidable

Per route (short committed, long committed, live Record, frozen Record return, layered outgoing, NOTE_EDIT, overdub overlay, restored dense, undo/redo, switch, internal/external clock): independent MIDI/ledger oracle + cold legacy reference + refusal/miss rows named as failures. Device leg requires: pinned lane image, `captures/.device-shutdown-incomplete.json` latch cleared as a deliberate human step (it exists, 218 B), verified two-Stop persistence drains before/after, registered `STIM-WINDOW-CONTENT-01@v1` via `windowed_content_parity`, complete accepted-content dump for row-for-row parity, ring `tierA_evict/overflow/crit_rej` counters, RAM1/PSRAM high-water. 3.7b removes each compatibility branch only after its route's oracle stays GREEN without it.

### 4. Commands (actual arguments)

- Focused native: `pio test -e native -f test_runtime_working_set_ownership`, `-f test_shared_capture_finalization`, `-f test_playback_prewarm`, `-f test_windowed_preview_integration`.
- Gated: `PLATFORMIO_BUILD_FLAGS='-DUNIFIED_RECORD_CAPTURE_INITIAL_ORACLE' pio test -e native -f test_runtime_working_set_ownership`; `' -DUNIFIED_RECORD_RETURN_READER_ORACLE'` on `-f test_shared_capture_finalization`; `'-DUNIFIED_LAYERED_READER_ORACLE'` on the ownership suite.
- Full: `pio test -e native`.
- Builds + RAM1: `pio run -e teensy41-capture-serial` and `pio run -e teensy41-capture-serial-window-qualification`; the budget check is a **post-link action** (`platformio.ini:26`), so read the `RAM1: variables:…, code:…, padding:…` line from the build output; current pin `96,192 / 390,360 / 2,856`, floor 840 (`scripts/ram1_budget.json`). No standalone invocation exists.
- Specs/diff: `openspec validate windowed-playback-generation --strict`, `git diff --check`.

### 5. Remaining unproven external evidence

69-vs-64 `win_gap_overrun` attribution (read-only; 3.10); a measured composed owner service envelope; PSRAM high-water for chunk hints; whether any defensible envelope admits a changed one-bar successor; device-epoch live first-Record on the one reader (S1b limit); device content/timing parity for every S1–S5 route; the latch state; and whether the tightened lane verifier passes against a real capture.

### 6. Risks, rollback, open questions

Highest risk is S1b's raw-shell retirement — mitigated by the S1a fixture (iv) and S2's acknowledgement surrogate; the S2 ordering hazard (a live `current` causing `releaseFrozenRecordWindow` to discard the frozen `ready`) must be RED-proved before landing. Rollback: every change stays behind `PLAYBACK_WINDOW_ROLLING_ENABLED` (default 0), so a revert restores the shipping image by construction; work stays on `feature/unified-rolling-playback-path`, never `dev`. Defaults for the open items: S4 keeps merged as compatibility fallback; S5 keeps closure-capacity refusals untouched; S6 keeps today's refusal until measured; the API-only switch fixtures stay expected-RED and out of scope.

### 7. Closeout impact (later, not now)

Newest-first entries in `docs/Runtime/PROJECT_STATE.md` and `docs/Runtime/CURRENT_WORK.md` (ID + short name), `docs/DELIVERABLE_TRACKING.md`, `openspec/changes/windowed-playback-generation/tasks.md` (3.15a–f only with their own gate evidence), a packet under `evidence/<date>-s315…/EXECUTION.md`, and `docs/Authority/Architecture/Playback.md`/`ExecutionBounds.md` only if one-reader serving is actually enabled. A `DEC-###` only if an owner/contract/representation changes. If this plan is persisted, the repo convention is `docs/Plans/<main>_<sub>_<kind>.md`. No file was written by this investigation.

## I5 — native ingress-hook/mask qualification follow-up (2026-10-14)

[I5 — native complete-hook evidence](evidence/2026-10-13-i5-running-capture-service/INGRESS-MASK-QUALIFICATION.md) supplies controllable production-path mask/sampler/receiver boundaries before target cost probes: defaultOFF/nativeonly/targetrejected seam, no target PRIMASK/state/payload/owner/pin/transform change. Actualreceiver packet/burst frequency/refusal/pressure/FIFO, real physical sampler and receipt/dispatch nesting are independently asserted on incoming0/1. Baseline13RED/restore10RED/frequency3RED restored; review2oracle gaps fixed/retested; native3207pass7skip119suites/ingress45/receiver18/receipt9/ASan18/host30/defaultSUCCESS/sameRAM1. Upstream unconditional DIN enable is observed, not repaired or used to claim whole-driver preservation. Target duration/IRQ-off/overhead, finite completeowner/IRQ bounds, calibration/arrival/sourceISR-main protocol remain open. No upload/device/SD/Git/defaultactivation/readiness or parentcheckoff;29/60. Next gated duration recipe builds on these fixtures, not on standalone sampler maxima.

### I5 — ingress-cost arithmetic prerequisite (2026-10-14)

[I5 — cost arithmetic packet](evidence/2026-10-13-i5-running-capture-service/INGRESS-COST-QUALIFICATION.md)
fixes the conditional full-wrap/refusal/saturation oracle before target instrumentation.
The arithmetic header is passive/caller-owned; subsequent gated target callers are
described below. No new owner/producer/resource. Separate hook-call, conservative
mask-bracket, publication and empty-probe lanes cannot be combined into whole-IRQ
cost or a subtractive WCET claim. Enclosing extended-time brackets and whole-interval
continuous-counter/rate-bound contract are prerequisites, not facts inferred from
matching endpoint configuration. Native arithmetic acceptance alone is not target
producer/measurement qualification. Finite owner bounds/source concurrency and
740→768 readiness remain open after the conditional observations below.
No parent checkoff;29/60/rollingOFF/targetcontinuationdisabled/receiptsdefaultOFF unchanged.

### I5 — gated target ingress-cost observations (2026-10-15)

[I5 — target execution/evidence](evidence/2026-10-13-i5-running-capture-service/TARGET-INGRESS-COST-EXECUTION.md)
implements opt-in diagnostics within the default capture environment, extending existing
MidiIngress State by gated384B aggregate payload (ARM19552B/host19576B), fixed384B snapshots
and local40B tokens; no manager/queue/hot allocation/source transform/installed edit.
Saved-mask protected publication/snapshot, strict low-rate12row read-only command and
named central Tier-A emitter leave default-OFF behavior intact. Endpoints reuse the
existing PIT sampler around DWT, supplying enclosing brackets and a conditional1.2GHz
gross rate premise for600MHz Run-mode, explicitly not calibrated or endpoint-proven.
Hook includes inner instrumentation; mask is conservative/nonexclusive; publication
self-accounting tail unmeasured; empty probe separate with no subtractive correction.
Smoke02/full01 stopped Clock-only120/300BPM PASS each8complete snapshots/6Readyera2guard15;
full USB hook deltas239/598/exact1:1:3:1/no new rejection/no saturation. Lifetime hook
21699..33039/mask5895..15983cycles are observations, not WCET/full receiverIRQ/admission.
Native3228passed7skipped121suites/host112/ASan8/ON+OFFbuilds/one repaired focused review;
unchanged floor840. Exact enabled HEX+ELF preserved/verified no-build upload, then exact
default-OFF HEX+ELF restored/verified; restored02 stopped/idle/guard15/two drains/postflight/
no latch. Boot/storage preflight refusals retained. No Start/content/slot/SD-cleanup
gestures; automatic persistence occurred. No new decision/defaultactivation/parentcheckoff/
Git;29/60 unchanged. Complete finite owner/IRQ bounds/calibration/wirearrival/sourceISR-main
and running request/anchor/coalescing/adoption/stop remain separate open gates.

### I5 — builder-call cancellation and atomic live-source reads (2026-10-15)

DEC-130 — builder-call cancellation and live-source exclusion extends DEC-127's
retained-source boundary: public builder mutation calls retain their Fill/Order/private
candidate state, refuse re-entry and resolve pending discard at the call boundary.
Runtime reset during either executing builder returns false/busy before destructive
retirement; callers still own retry. Fixed saved-mask descriptor/currency/one-row or
charged chunk-transition reads exclude source check/use gaps, never whole-grain work.
Both builders, published sibling/ledger/accepted Capture preservation, cancellation
before and during call, reset retry, preserve conflicts and incoming masks0/1 have
production-body fixtures. Baseline lost-begin cancellation and Fill SIGSEGV discriminate;
focused477 and ASan/UBSan9 plus original4case witness PASS; one focused review approved.
Compiler-measured ARM builder256/runtime7792 unchanged; host400/9976 (+8/+16).
[I5 — source concurrency packet/evidence](evidence/2026-10-13-i5-running-capture-service/SOURCE-CONCURRENCY-EXECUTION.md)
owns broad-check completion and exact remaining gates. Standalone Fill/direct candidate
writers remain serialized; all Track role/publication/adoption writers, running request/
anchor/coalescing/stop and finite physical/input/owner/IRQ timing are NOT qualified.
No target activation/upload/device/SD/Git or parentcheckoff; progress29/60 unchanged.

### I5 — exact Track completion authority (2026-10-15)

DEC-131 — exact Track completion authority extends the existing builders with physical
destination-generation receipts, so old queries/cleanup cannot claim reused storage.
Track coverage maintenance retains role/storage/operation receipt across final grain and
clock sampling; qualification before judgement and before short masked publication rejects
cancelled completions without cleaning up replacement work. Generic publication detaches
the transferred builder. Cold live completion checks exact completed destination/role.
Ready adoption consumes only ready, never promotes a private completion; the existing
main bridge worker explicitly publishes its detached same-source successor. No fourth
view/new owner/queue/transport change. Target264/7808 and host408/9992 builder/runtime
sizes measured (+16/runtime), Track/Loop target pins unchanged, numeric RAM1floor unchanged.
Six new registered cases, four baselineREDs, focused468/fullnative3243passed7skipped121suites,
ASan6 and wrong-mask2RED/liveheader unchanged; one focused review approved and coverage
gaps added/retested. DefaultcaptureSUCCESS/padding1368≥840/margin528/no upload.
[I5 — Track publication packet/evidence](evidence/2026-10-13-i5-running-capture-service/TRACK-PUBLICATION-CONCURRENCY.md)
records exact gates and continuation: acquisition-before-builder-entry/direct Record
release/start/bind/seek/projection still open; worker execution still excludes direct
external candidate writers. Running request/anchor/coalescing/stop and finite owner/IRQ/
calibration/wirearrival also open; no740→768readiness/parentcheckoff/targetactivation,
29/60/rollingOFF/targetcontinuationdisabled/receiptsdefaultOFF.

### I5 — preparation acquisition and Record exclusion (2026-10-15)

DEC-132 — preparation acquisition and Record exclusion extends the existing two builders
with non-masking scoped preparation ownership before role/source selection. Cancelled
turns cannot begin another call or certify completion. Foreign reset cancels/refuses;
reset itself reserves both lanes atomically through unmasked destruction, and slot
release carries the same exact capabilities through lease retirement. No reset queue.
Record admission acquires before geometry/timeline/pre-roll mutation and returns real
Capture acceptance; manager pending Record consumes only that success. Direct retained
reset/bind, first-bar service and deferred projection participate, while content work
stays unmasked. Required frozen release refuses without losing source/job retirement.

Qualified ready adoption is still allowed: bridge service snapshots/revalidates physical
context, storage and role metadata, rejects cleanup after cancellation, and detaches its
exact completed worker while preserving a ready/current window independently adopted.
No implicit private promotion, fourth view, new Track field, manager or hot allocation.
Flags fit existing layout after cursor/phase reorder: targetbuilder264/runtime7808,
host408/9992; targetTrack384/Loop1008 unchanged, no pin or numeric budget relaxation.

Eleven newcases/focused479/fullnative3254passed7skipped121suites/ASan12/four scratch controls
specificRED/live source hashes unchanged. One focused review found two concrete defects,
fixed/retested without a repeat review; bridge progress regression fixed by preserving
and detaching the exact completed worker during ready adoption. DefaultcaptureSUCCESS
41.065s/RAM1padding1160≥840/margin320. [I5 — final acquisition evidence](evidence/2026-10-13-i5-running-capture-service/ACQUISITION-CONCURRENCY.md)
supersedes the prior acquisition/direct-Record continuation notes above for these paths.
Required next gate: registered managed default-path Record/Stop compatibility HITL;
no upload/device/SD/Git/defaultactivation/checkoff performed. Parents29/60 unchanged.
Running request/initial anchor/append coalescing/stop integration and finite owner/IRQ/
calibration/wirearrival remain open; no740→768 readiness is claimed.

### I5 — acquisition/Record hardware compatibility closeout (2026-10-15)

The compatibility gate named above now PASS: owner-approved scratch8/4–5 only,
exact retained default-OFF HEXf48aca66…b0068/ELF7cd88398…a3563 verified no-build upload,
registered STIM-BASE-2-2-01@v1/base8/4/channel8/hostUSB300BPM livePASS/overall_oktrue/
cap_full/issuesempty/critical-loss0. Record/two overdubs/undo-redo/geometry/grid/span/
cleanup/persistence checks pass. Advisory second-overdub lateness retained; no deadline
safety proof. Fingerprinted replay02 passes header/must-prove without skips; raw-session
legacy replay limitation retained. Parallel native3254passed7skipped121suites. No firmware
edit; initial postflash storage-work refusal retained, settled/finalpostflight guard15/
STOPPED/closedoverlay/two drains/nolatch. Newdefault-OFF image remains installed, scratch
content retained; no SDcleanup/Git/defaultactivation/parentcheckoff,29/60 unchanged.
[I5 — hardware execution evidence](evidence/2026-10-13-i5-running-capture-service/ACQUISITION-CONCURRENCY.md#current-session-hardware-compatibility-execution-2026-10-15).
This supersedes only the prior pending-compatibility status, not the remaining running
request/initial anchor/append coalescing/stop/finite owner-IRQ/calibration/wirearrival gates.
