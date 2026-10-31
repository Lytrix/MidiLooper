# Display base — staged preparation on the idle owner

**Status:** Slice 1 complete; **Slice 2 landed (work-bound producer; S = 7 sparse, 25 dense after the wrap-pass precheck)**; **Slice 3a/3b/3c1 landed** (`DisplayBaseGrain` admission, manager-owned DMAMEM candidate, flash-resident producer, idle-owner call site, and the consumer-due-tick K = 0 arbitration). **Slice 3c1's device measurement is a service-target miss:** chain-51 (`0ebccccc…`) reports `disp_base_prep` `steps=18, tiles=5, wrap=1, us=1.1–1.3 s`; the display is selected on only ~1/3 of VisualCache turns, so the effective service interval is ~63 ms/step and the 500 ms target is missed. Slice 3c2 (frame request/serve/adopt) is **blocked** until S, the display share, or the target is decided. Chain-48 measured T ≈ 56 VisualCache turns/s (median `last_us` 16.3 ms, 11.3–25.0 ms); S = 25 at K = 0 was the native estimate, but the device service interval is the binding number. Edited slots conservatively stage every pass (S = 36) — a p95 caveat. The stop-edge wedge did **not** reproduce in chain-48/49/51 (47–172 ms save→Record margins), so `order_deny` is still unnamed. Chain-53 is invalid; the deterministic stale-root reproducer is required for Slice 3c2/4 and again as final acceptance.
**Scope owner:** `DisplayManager` (base and frame) · deferred idle owner (producer) · `LoopContentResolution` (stepped derive).
**Change vehicle:** `immediate-record-overdub-handoff`, capability `realtime-visual-cache`; new contract **C6** and a new task group, not a rewrite of 5.1–5.4.
**Decision candidate:** **DEC-237 — the committed display base may be served one generation stale while a declared idle-owner producer prepares the next base, provided the four C6 invariants hold.** Append only when C6 is accepted.

---

## 1. Decision requested

On a **committed** change (`playbackRevision` / published identity moves) the display frame must not run the indivisible committed gather. It keeps serving the previous qualified base and the deferred idle owner steps the replacement with `LoopContentResolution::stepTilePreparation`; the frame adopts the replacement only when it is complete and only for the key it was built for.

This moves work between owners, so it needs:

1. a **C6** contract under the active change, with the four invariants below;
2. a new `IdleOwnerChild` in the closed admission registry (DEC-212);
3. an explicit amendment to **DEC-211 point 4** ("a miss gathers that window … on a miss"): a miss now **requests** the gather and serves the previous base while the producer runs;
4. the acceptance that a *stepped* walk is not automatically a *bounded* walk (§3).

### C6 invariants (the four load-bearing requirements)

**C6.1 — Stale-base completeness (generation-range).** Let the served base cover through generation **B** and committed state have advanced to **C**. Every committed delta in **(B, C]** must remain represented by an authoritative retained overlay until a base covering that delta is adopted. Adopting a base through **G+2** may release retained deltas ≤ G+2, but not G+3. A frame must never be served with neither the committed delta nor a base covering it.

**C6.2 — No realtime synchronous fallback.** During active transport, failure/expiry of staged preparation may never invoke the content-sized synchronous committed gather on the frame owner. Allowed outcomes are: serve the previous qualified frame, use another already-qualified bounded source, or report an honest named hold. The synchronous path remains only for an explicitly cold/non-realtime state.

**C6.3 — O(1) adoption.** Candidate adoption transfers buffer ownership (swap/move); it does not copy the staged note inventory and does not allocate on the frame path.

**C6.4 — Producer liveness.** C6 must state why `DisplayBaseGrain` eventually receives service. Either the existing coverage contract guarantees eventual `coverage Idle` (pin that bound), or coverage can remain continuously owed and C6 defines bounded arbitration (measured, not invented).

**Not in scope:** the moving-window/per-tick origin work already attributed separately (the reverted bar-stable attempt; prerequisite = next-bar tile prefetch), the C5 visual-cache whole-pass fallback, any second display source, or any change to the overlap tile cache.

---

## 2. Problem and evidence

Owner rig `captures/session_20261031_162028.log` (image `1309a167…`, 12 overdubs):

| Window | worst MIDI gap | worst frame | full / incremental | pass walks |
|---|---|---|---|---|
| 140–160 s (fixed) | 65–105 ms | 65–97 ms | 1–2 / ~55 | 1–3 |
| 130–135 s (follow) | 83–116 ms | 83–86 ms | 15–27 / 2–25 | 16–29 |

The measured slow frame is the single-shot committed derive on the display lane; the incremental path is healthy. `DisplayFullRebuild` is **not** the slow signal: 68–75 full rebuilds per 5 s window can still cost 1–2.5 ms per frame. The slow signal is `disp_passwalk` / `site=drawsrc`.

The class is the single-shot committed derive: `rebuildDisplayNotesInWindow` (base/window sites) and the draw source's direct `resolveDisplayNotesInWindow` (`gatherCommittedDrawSourceBars`). Three live call sites share the class:

- `DisplayManager::buildCommittedDisplayBase` (the overdub/live-capture base);
- `DisplayManager::resolveWindowedDisplayNotes` (the committed window path, when the visual cache does not cover);
- `DisplayManager::gatherCommittedDrawSourceBars` (the per-frame draw source's stale-root branch — this is what `disp_passwalk` counts).

### Slice 1 attribution — the base/window sites are not the measured slow owner

Two instrumented runs on the same 20-bar chain (12 overdubs, `16ths.mid` + `chords.mid`):

| Run | Instrumented sites | Device content | `disp_base` rows | `disp_passwalk` | slow frames |
|---|---|---|---|---|---|
| chain-52, image `6a84d322…` | `buildCommittedDisplayBase` (`site=live`), `resolveWindowedDisplayNotes` (`site=window`) | `cap=13`, 12 `Overdubbing started`, 10 `Overdub stop` | **0** | **53 total**; 18–29 in the slow 100–160 s windows | 86–110 ms; `stop_tail … work_budget,16` at 107–146 s |
| chain-53, image `00ce71d5…` | plus `gatherCommittedDrawSourceBars` (`site=drawsrc`) | **invalid: `cap=0`, 0 `Overdubbing started`** — host sent 12 windows, device accepted no record or overdub | 0 | 0 | not a control; empty/preview content only |
| chain-55, image `00ce71d5…` | plus `site=drawsrc` | **valid: `cap=13`, 12 `Overdubbing started`, 1 record** (gate passed) | **0** | **38 total**; 25–31 in the slow 86–136 s windows | 83–106 ms; `stop_tail … work_budget,16` at 89–129 s; `disp_src pending=1, phase=5` |
| chain-56, image `7866c63d…` | plus `disp_frame_slow` phase split | **valid: `cap=13`, 12 starts, 1 record** | **2** — `site=drawsrc`, 63,983 / 70,843 µs, 309/327 notes, 20-bar range | 27/25/20/17 in the slow windows | 56 slow frames split: **2 resolve spikes 63–71 ms** (the drawsrc rows) and **54 wait frames** (resolve 1.7–2.1 ms, draw 5.4–5.7 ms, transfer 0.05 ms, frame `us` 50–98 ms) |

- The base/window sites never fired. `disp_passwalk` is present in the slow bands, but only **2** calls exceed 5 ms and produce a `site=drawsrc` row; those are the 63–71 ms resolve spikes.
- **Two classes, two owners.** (1) The draw-source pass gather: 63–71 ms, 309–327 notes over the 20-bar range — the staged-base target. (2) The wait class: 54 of 56 slow frames have small resolve/draw/transfer and a 50–98 ms frame `us`, coinciding with `stop_tail … work_budget,16` (`disp_src pending=1, phase=5`); that is main-loop/stop-lane starvation (DEC-227), not the display gather. The staged base does not fix class 2.
- The 377–381 ms spike at 11–16 s in the valid runs is record start, not the committed display.
- **Chain-53 is invalid** (`cap=0`, no Record/Overdub acceptance) and provides **no evidence** about fresh-boot or state dependence. The ordinary owner rig does not deterministically establish the stale-root precondition; a deterministic stale-root device reproducer is required.
- A run is only usable for timing when the device content gate passes (`cap >= 13` and 12 `Overdubbing started`, or the exact accepted-pass count). The host-side `midifile-chain.json` phase report is not proof of device acceptance; the gate is now enforced by `scripts/hitl_midifile_chain.py` (`device_record_starts`, `device_overdub_starts`, `device_cap_last`).
- The paint window is **20 bars** (`disp_turn req=…,12288`, 493 notes), while `kCommittedDisplayBaseMaxBars` is 16. A single 16-bar base cannot cover the draw-source bar range; the staged producer must cover the draw-source range in the 4-bar tile unit (five tiles for 20 bars), not reuse the 16-bar base blindly.

The draw source is confirmed for class 1 (the two 63–71 ms spikes); the staged base remains the right fix there. Class 2 (54 wait frames with small resolve/draw/transfer) is the stop lane's main-loop starvation and is a separate gate (DEC-227), not this plan's acceptance. Do not claim the staged base closes the frame `us` when the wait dominates.

---

## 3. The bound gap: stepped is not yet bounded

`stepTilePreparation` is parity-guaranteed with `resolveDisplayNotesInWindow` for the same window, and it is the right *decomposition*. It is **not** a per-turn bound today:

- The event budget bounds `CommittedEventRange::appendWindowBudgeted`'s **appended events**, not **chunks inspected**. A window that intersects little can still walk every chunk of a pass in one call.
- On pass completion the cursor runs `applyActiveEdits` and `appendPassLocalDisplayNotes` over the **whole pass layer**. `appendPassLocalDisplayNotes` reconstructs notes and calls `upsertResolvedDisplayNote`, which scans the accumulated `dest` for every note — a content-sized merge. Bare `NoteId` is the linear stage's key today; it is not a safe global key for wrap-head/tail rows, so the identity semantics must be pinned by the parity oracle rather than assumed.
- Device evidence already records the consequence: `idle_turn,cov=tile` with `prep` **10–48.6 ms** and `pub` up to 22.1 ms ([CURRENT_WORK](../Runtime/CURRENT_WORK.md), DEC-217 entry).

Therefore simply calling `stepTilePreparation` from the idle owner would move the 65–95 ms block into a 10–48 ms idle turn. That is not the accepted outcome: the child max is 4 ms (`kOwnerWorkBudgetUs`), and the fastest supported clock interval leaves only 4.3 ms of residual.

**C6 requires the producer's entire `stepDisplayBasePreparation()` turn — traversal, pass completion, edit application, pairing, wrap-held staging and final window filter — to be ≤ the declared max.** Slice 2 owns that; Slice 3 may not start until its RED fixture is green.

---

## 4. C6 contract (the G1 resolution)

### 4.1 Owners

| Phase | Owner |
|---|---|
| Frame/base or draw source needs content, key is stale | `DisplayManager::resolveDisplayNotesLiveCapture` / `rebuildOverdubCommittedDisplayLayer` / `gatherCommittedDrawSourceBars` (request and adopt only) |
| Base derivation | deferred idle owner, selected display track, `VisualCache` turn, after the coverage tile worker reports no owed tile |
| Base content | `DisplayManager` (`CommittedDisplayBase` remains the single display source, DEC-211) |
| Draw source (consumer) | `DisplayManager::gatherCommittedDrawSourceBars` / `resolveDisplayNotesByDrawSource` — consumes the served base/staged tiles for the requested bar range; it may not re-walk the passes while a qualified base/tile set covers that range |
| Retained committed delta | the existing `Loop::sealedCapturePreviewNotes_` / `retiredSealedDisplayNotes_` bridge, with the release predicate extended (C6.1) |
| Stepped primitive | `LoopContentResolution::stepTilePreparation` (unchanged parity contract) |

The frame never derives on the staged path. The idle owner never publishes into the frame; it leaves a completed candidate and the frame adopts it on its own path.

### 4.2 New state

`DisplayManager`:

```text
DisplayBasePrep {
  active, ready
  trackIndex, displaySlot, loopId
  playbackRevision, loopLengthTicks, snapshotCurrent, publishedIdentity
  windowStart, windowLength          // the draw-source bar range to cover (observed 20 bars; not the 16-bar cap)
  tileCursor, tileCount, tileIndex   // 4-bar construction/publication unit; all tiles one generation
  tiles                              // qualified tile buffers/descriptors (all five for a 20-bar range)
  stagedNotes                        // optional flattened view; assembled only when every tile is ready
  lookupStart, lookupLength          // closure-padded read window for the current tile
  TilePrepCursor cursor
  DisplayNoteVec stagedNotes         // final candidate: filtered to the base window, wrap-held rows included
  startedMs, steps
}
```

- The prep object/state lives in `DMAMEM` (not DTCM): the cursor is ~80–100 B plus its vectors, and the note storage is external-first. `variables 96,192` must be unchanged.
- One prep at a time (one display). A key change resets it; there is no queue.

### 4.3 Key, cancellation and overdue

Prep key = `(trackIndex, displaySlot, loopId, playbackRevision, loopLengthTicks, snapshotCurrent, publishedIdentity, windowStart, windowLength)`.

**Semantic cancellation** (the candidate is no longer correct): revision move, slot/track switch, loop-length change, a current root/source superseding the candidate, track leaving `OVERDUBBING`, transport stop, undo/load/save invalidation, paint window leaving the served base's coverage.

**Time/step threshold** (same key, same source, still progressing): do **not** discard. Mark the prep `overdue` at `kDisplayBasePrepOverdueMs` (**1,000 ms** proposed; measured), emit a named `disp_base,overdue` row, and continue bounded preparation. The 1 s threshold is a service SLA/diagnostic, not a cancellation; restarting a valid candidate at 1 s would create a liveness loop while C6.4's cadence is still unproven.

Only a proven **no-progress watchdog** (no cursor advance across a measured number of admitted turns) may abandon a same-key candidate. Abandonment is named, leaves the frame on the qualified base/hold, and never invokes the synchronous gather (C6.2).

Cancellation is lossless and **cheap on the frame**: the frame only invalidates ownership/key state. Buffer clear/reuse and any cleanup run on the deferred owner, never as a free/rebuild on the frame path.

### 4.4 Frame handoff (serve stale → adopt)

**Draw source first (the measured owner).** In `gatherCommittedDrawSourceBars`, before
`committedDrawSourceIsStaleRootPassGather` selects the pass walk: if a served/adopted base or staged tile
set covers the requested `[firstBar, lastBar]` range, serve those committed rows for that range; if none
covers it yet, keep the previous qualified draw source for the whole requested range (C6.2). The
stale-root pass walk is allowed only in the explicitly cold/non-realtime case, never as the
active-transport escape.

**Base/committed layer.** In `rebuildOverdubCommittedDisplayLayer`, on a base miss:

1. If the previous base is valid for the same slot/loop/length **and its gather window still contains the paint window**, and the prep is startable/active → keep the committed prefix, set the capture prefix boundary as the existing hold path does, mark `liveCommittedLayerHeldForStagedBase_`, and return. No gather.
2. Otherwise, on **active transport**, never the content-sized gather (C6.2): hold the previous qualified frame **as a whole, including its published window geometry**, or use another already-qualified bounded source, and emit a named `disp_hold` / `disp_turn why=base_stale` row with the requested and published intervals. The new requested interval is diagnostic/owed only; it does not become the frame's content coordinate system until a qualified source covering it is available. The existing `buildCommittedDisplayBase` remains only for the explicitly cold/non-realtime state (boot, stopped lane, slot load with no live capture frame); a gate and a native fixture keep it off the active-transport frame owner. A genuinely cold live start with no qualified source is a named hold, not a licence to gather.

On the frame path, before capture-layer sync:

3. If `prep.ready` and its key matches the current loop/slot/length and its complete tile set covers the current paint and draw-source range → **swap** the candidate into the active committed buffer (C6.3), set `committedDisplayBase_` to the prep key/window/count, reset the capture prefix counters so the overlay re-appends, clear the prep ownership. The committed inventory is not copied; the capture overlay re-sync is the existing per-frame compose, not a new committed gather. The adoption publishes the **complete tile set atomically**; a partially prepared set is never visible. The adopted notes feed both the base compose and the draw source's committed part.

The swap is never partial and never mixes two intervals: the old base + retained delta are served whole, the new base is adopted whole.

### 4.5 Stale-base completeness (C6.1)

The existing display bridge already retains sealed/retired preview rows:

- `Loop::retainCapturePreviewAtSeal` is called by `commitCapturePass`, so a committed overdub pass's closed preview rows are retained;
- `retireSealedCapturePreviewForDisplay` moves them to `retiredSealedDisplayNotes_` on session close;
- `applyDrawSourceOverlays` unions sealed/retired rows into the frame while the cache is dirty or the rows are not represented.

C6 extends that **release predicate**, not the retention path: `sealedCapturePreviewIsCovered()` / `retiredSealedDisplayCovered()` must not release a row unless the **served base** (or a window-covering cache/base) represents it. Adoption releases by **generation range**, not by "candidate adopted": a base through G+2 may release deltas ≤ G+2, but a retained G+3 delta stays until a base through G+3 is adopted. No second overlay owner is introduced.

**Release ownership:** `Loop` owns the retained rows; `DisplayManager` owns the served-base coverage test. The release call takes a narrow `baseCoversRow` / window predicate from the display owner (or is invoked from the display owner), so `Loop` does not include `DisplayManager` and the display does not clear `Loop`-owned rows directly. The exact seam is a Slice 2 G1 item; no new manager.

The required fixture is the one the review named, extended to multiple generations:

```text
base through B
→ overdub G+1 commits
→ staged base deliberately held
→ G+2 commits before adoption
→ G+3 commits before adoption
→ every frame still contains G+1, G+2 and G+3
→ base through G+2 adopts
→ G+3 retained delta still present
→ base through G+3 adopts
→ no missing and no duplicated notes
```

This is a C6.1 acceptance case, not only a cancellation fixture, and it is a Slice 1 RED. The two-commit case ("two commits during one prep") is the same invariant at a smaller scale.

### 4.6 No realtime synchronous fallback (C6.2)

During active transport, no display frame path may execute a content-sized committed gather: the passes fallback in `rebuildDisplayNotesInWindow`, `resolveDisplayNotes(passes, …)`, or the equivalent windowed whole-pass route. Allowed sources are the served base, a qualified cache/base window, the retained delta, and a named hold. Prep expiry is not a licence to run the gather.

The synchronous path stays available for cold/non-realtime states only. A debug gate plus a native fixture drive the live path and assert the gather count is **0**; a device `disp_hold` row names any interval where no qualified source was available.

### 4.7 O(1) adoption (C6.3)

The producer owns candidate vector **A**; the frame owns active vector **B**. Adoption transfers ownership (`liveDisplayNotes.swap(prep.stagedNotes)` or equivalent move), updates the key/count/window, and resets the overlay boundary. It does not copy the committed inventory and does not allocate. The swapped-out frame buffer is cleared/reused by the deferred owner. A fixture asserts zero allocations and zero copies on the adoption call.

### 4.8 Producer liveness (C6.4)

The display grain runs only when `serviceOverdubCoverageTurn` returned `Idle`, so coverage correctness never waits behind display. That priority must not leave the producer without a liveness guarantee.

**Measured: B holds.** `test_dec237_coverage_stays_owed_under_continuous_input_then_reaches_idle` keeps the coverage owed for all 64 continuous-input turns (`idleTurns = 0`) and drains to Idle after the input stops; the fixed-set test reaches Idle directly (A shape). The display producer cannot rely on natural coverage gaps for service.

**Do not choose K before Slice 2.** Slice 2 measures two quantities:

- **S** — the worst number of `DisplayBaseGrain` turns required to produce a complete 20-bar candidate;
- **T** — the actual owner-turn cadence while the chain is active.

With one display grain after every K coverage grains, completion latency ≈ **S × (K + 1) × T**. The 500 ms service target bounds K from above; the coverage worker's own correctness deadline bounds it from below. C6.4 selects the **largest measured K** that keeps coverage correctness green while keeping the worst qualified display candidate inside its service target.

Rule shape (the veto must resolve to an existing measurable predicate, not prose):

```text
if coverage has an immediate correctness deadline: coverage wins
else if display service debt >= K:                   DisplayBaseGrain
else:                                               coverage
```

**Measured on chain-48 (2026-10-31, image `586825f8…`, `session_20261031_201107.log`).** The chain is valid (`cap=13`, 1 Record + 12 overdub starts, 0 `order_deny`, 0 `preparation_denied`). T from `disp_owner_cadence`: **~56 VisualCache turns/s** (median `last_us` 16.3 ms, range 11.3–25.0 ms; per-window `max_us` 24–250 ms, median 69 ms; the first two samples are boot/wrap artifacts). The wrap-pass precheck then reduced S from 36 to **25 dense** (22 tile grains + 3 boundary-content passes) and **7 sparse** (5 tiles + 2 passes). At **K = 0**, S = 25 costs **~408 ms at the median cadence and ~625 ms at the worst observed cadence**; K = 1 would be ~815 ms, so **C6.4 selects K = 0 as the only admissible setting**. The 500 ms p95 is plausible at the median but not proven for the worst cadence; the wired device path in Slice 4 must measure it. Edited slots conservatively stage every active pass (S = 36, ~590–645 ms); that worst edited candidate is outside the target and is a named Slice 3/4 p95 caveat, not the unedited acceptance shape. The precheck skips a pass only when no committed chunk's span intersects the loop boundary window (`closureTicks` each side); the edit guard keeps every pass when an active Note edit could move a note across the boundary.

Slice 2 is authorized; Slice 3 can start with K = 0 and the edited-slot caveat recorded.

**K = 0 is now a data-driven rule (3c1).** The due tick comes from the consumer that created the obligation: `DeferredOverdubCompletion.endTick` is the closed note's horizon. `Loop::overdubCoverageRequiredNow(currentPhase, kCoverageLeadTicks)` compares that due tick forward from the playhead, and `MidiServiceDrain::chooseCoverageOrDisplay` lets coverage win only when the due tick is within the declared lead (`kCoverageLeadTicks = 8`, one owner opportunity at the fastest supported tempo). Merely owed coverage is **not** urgent. Occupancy-only holds have no closed span yet and are not urgent until their note-off; the seal deadline remains their backstop. The continuous-input fixture drives the display candidate to `ready` while coverage is continuously owed, then lets the coverage worker drain every tile; mutations prove both sides (making all owed coverage urgent starves the display; ignoring the due tick fails the due-tick fixture).

**Device measurement (chain-51, image `0ebccccc…`): the service target is missed.** The new `DIAG,disp_base_prep` row fired twice: `steps=18, tiles=5, wrap=1, us=1,126,570` and `us=1,315,625`. The VisualCache cadence is still ~56 turns/s, but the display is selected on only ~1/3 of those turns (coverage is urgent on the rest), so the **effective display service interval is ~63 ms per step**. S = 18 therefore completes in **1.1–1.3 s**, not 500 ms. The `S × (K + 1) × T` formula's T must be the display service interval, not the VisualCache turn cadence. Slice 3c2 (frame adoption) would serve the stale base for that whole interval, so it is **blocked** until the decision: reduce S (a larger declared grain, pending a device max-per-step-work row), give the display a larger safe share (a tempo-aware one-opportunity lead or a declared share), or revise the target for continuous dense chains. The 500 ms p95 remains unproven and is currently missed on the measured continuous chain.

---

## 5. What the producer must do (Slice 2)

The producer is one bounded unit per turn:

1. **Traversal grain** — `stepTilePreparation` over the closure-padded lookup window of **one 4-bar tile** of the draw-source bar range, `eventBudget = kGrainEventsPerTurn` (64), same as the tile owner. The observed paint/draw range is 20 bars; `kCommittedDisplayBaseMaxBars = 16` is not the construction unit and cannot cover it. The candidate is composed from five 4-bar tiles (or the declared tile count for the range), and each tile is parity-checked before publication.
2. **Inspected-chunk bound (landed)** — `WindowTraversalResult` carries `chunksInspected`; `appendWindowBudgeted` takes an optional `maxChunks` cap (`0` = historical unlimited) and `stepTilePreparation` sums it across passes. The producer passes 64. A non-intersecting chunk costs the span read, not a copy; the cap makes a sparse window bounded too. Existing callers keep the unlimited behavior.
3. **Pass-completion bound (landed)** — the completion step must not run a content-sized merge:
   - accelerate the existing upsert **without changing its identity semantics**. There are **two identity scopes**, and they must not be merged: the resolver's per-pass stage keys valid rows by bare `NoteId` (replace) and invalid rows by pitch+start (keep first); cross-tile assembly keys a valid row by `NoteId`+start (a wrap head/tail pair shares the id at different starts) and invalid rows by pitch+start; the wrap-held append keys by pitch+start, replaces the first non-wrap row, and keeps the first wrap-held row. Body tiles are applied **before** wrap rows, matching `resolveDisplayNotesInWindow` + `appendOverdubPassDisplayNotes`. Do not introduce a new global uniqueness assumption;
   - if the completion still exceeds the declared device max on a dense pass, add a resumable post-processing cursor (edit application, pairing, append) instead of raising the child max;
   - stage the wrap-held append per pass; skip a pass only when no committed chunk's span intersects the loop boundary window (`closureTicks` each side). An active Note edit may move a note across the boundary, so an edited slot stages every active pass (conservative fallback). The adoption may not call `appendOverdubPassDisplayNotes` over all passes.
4. **Raw-event retention is optional** — `keepRawEvents=false` is an optimization available to Slice 2 only if measurement shows it materially helps the 4 ms or PSRAM gate. It changes a shared cursor API and adds another parity dimension; keep the existing behavior when the gates pass without it.
5. **Work bound inside the bound (landed)** — native fixtures assert deterministic work units, never host wall-clock: per step `chunksInspected ≤ 64`, `eventsAppended ≤ 64`, `passLayersProjected ≤ 1`, `wrapRowsStaged ≤ 256`, `mergeComparisons ≤ 512`; the completion step is the last tile or wrap grain, with no separate content-sized assembly. The **device** ≤ 4 ms whole-call check is Slice 4's gate, because the producer must be wired before it can be measured. The fine-grain claim is per **grain**: a `fineGrainUs = 1000` child must have every grain, including completion, within the declared fine-grain work; the current completion grain is one tile/wrap layer, not an all-pass projection.
6. **Candidate storage** — the completion turn produces `stagedNotes` in the producer's buffer; adoption is the §4.7 swap. No frame-side copy.

**Atomic tile-set rule (C6.3 extension):** all required tiles belong to one candidate generation; `ready = true` only after every required tile completes and parity-qualifies; adoption publishes the **complete** set atomically. The frame never reads tiles 1–4 from the new generation with tile 5 from the old. The candidate may be a descriptor set of tile buffers (DEC-211's "single source" does not require one contiguous vector); if it is flattened into one vector, that assembly is inside the ≤ 4 ms whole-call bound.

**Parity oracle:** for the same committed generation, slot, loop length and bar range, the staged result equals the **draw source's current stale-root single-shot** (`gatherCommittedDrawSourceBars` → `resolveDisplayNotesInWindow`) and, per tile, `resolveDisplayNotesInWindow` for that tile: same `NoteId`s, pitch/velocity, start/end, cardinality; order-insensitive equality, including wrap-split rows. The base/window composition is a secondary oracle. The oracle is the independent single-shot call, never the staged path itself.

**Declared child:** `IdleOwnerChild::DisplayBaseGrain`, `maxUs = 4000`, `fineGrainUs = 1000`, `unbounded = false`. The enumeration test pins the call site; no unregistered producer.

---

## 6. Slices, fixtures, gates

### Slice 1 — attribution and REDs (report-only; **go now**)

- Add `DIAG,disp_base,<site>,<phase>,<us>,<ops>,<notes>` (Tier-A through `DIAG,disp_`) around:
  `buildCommittedDisplayBase` phases (root copy, snapshot gather, passes resolve, wrap append, filter),
  `resolveWindowedDisplayNotes`' full-rebuild phase, and
  `gatherCommittedDrawSourceBars`' stale-root pass gather (`site=drawsrc`).
- Run the same owner rig command and image; record which site/phase carries the slow frame. Keep `disp_rebuild` / `disp_passwalk`.
- **Two-run result (recorded):** chain-52 and chain-55 show the draw-source rows in the slow bands; chain-56 splits class 1 (2 resolve spikes) from class 2 (54 wait frames). Chain-53 is invalid and adds nothing. The base/window sites emitted **no** over-budget rows.
- Add a **deterministic stale-root device reproducer** before Slice 3: force the published root to lag the committed generation (the native fixture already does this) and confirm `disp_passwalk`/`site=drawsrc` reproduces with the slow frame. The same reproducer is a **final acceptance run** after the fix: it must show positive staged request/step/adoption evidence, not only the absence of the old path.
- Add a **coverage duty-cycle** fixture/row: on a dense continuous chain, count `VisualCache` turns where `nextOwedTile` is non-empty vs idle. This resolves C6.4 A vs B before Slice 3.
- REDs / characterization (native):
  - `test_committed_change_serves_previous_base_and_never_runs_the_whole_pass_gather_on_the_frame` — fails today because the frame calls the gather; extend it to the **draw-source** range.
  - `test_stale_base_keeps_committed_deltas_across_multiple_generations` — the §4.5 B→C range; fails today if any retained delta can be released against a non-window-covering cache.
  - `test_existing_step_tile_preparation_max_completion_cost` — characterization only: measure the current `stepTilePreparation` completion worst case. No new producer API in this slice.
  - `test_draw_source_20_bar_range_is_covered_by_five_tiles` — the 20-bar vs 16-bar-cap case; fails today because the base cap cannot cover the range.
- No behavior change. Gate: focused display suite + `test_capture_ring`.

### Slice 2 — producer core (pure, no wiring)

**Landed (2026-10-31):** `include/Utils/DisplayBasePreparation.h` — the pure, unwired candidate. It stages **body tiles first, then one wrap-held layer per active pass**, assembles incrementally under two identity scopes, and exposes `ready` only after the last grain completes. The work bound is deterministic (not host wall-clock), and the device ≤ 4 ms whole-call bound remains Slice 4.

- **Sizes measured:** after the wrap-pass precheck, the sparse 20-bar record+13-overdub shape is **S = 7** (5 tiles + 2 boundary-content passes), parity 15 notes. The dense 13-pass / 20-bar shape (32 notes per pass) is **S = 25** (22 tile grains + 3 boundary-content passes), with max per step `chunks=8`, `events=64`, `passLayers=1`, `wrapRows=12`, `merge=244`. The edited-slot fallback stages all 14 active passes (**S = 36**) by design.
- **Native fixtures (green):** `test_dec237_slice2_pure_producer_20_bar_13_pass_parity`, `test_dec237_window_traversal_inspects_at_most_the_declared_chunks`, `test_dec237_slice2_producer_steps_stay_within_the_declared_work_bound`, `test_dec237_slice2_staged_base_parity_per_tile_and_whole_range`, `test_dec237_slice2_wrap_identity_survives_candidate_assembly`, `test_dec237_slice2_producer_inspects_at_most_the_declared_chunks`, `test_dec237_slice2_active_note_edit_stages_every_wrap_pass`, `test_dec237_display_note_upsert_index_keeps_the_two_identity_scopes`. Focused suite **31/31**; full native **3,778 / 8 skipped / 3,770 / 0**; target build SUCCESS, HEX `a6398480…`, uploaded and boot-verified.
- **Mutation-red proved:** disabling the chunk cap reds the sparse-chunk test (`65 > 0`) and the work bound; counting the old per-tile pass projection reds `passLayers` (`14 > 1`); ignoring the start tick in `NoteIdAndStart` reds the identity case; always replacing in the wrap merge reds the first-wrap-wins case; skipping wrap staging reds whole-range parity (`438` vs `437`) and the wrap pair (`2` vs `1`).
- **Identity scopes:** resolver per-pass = bare `NoteId` (replace) / pitch+start (keep first); cross-tile assembly = `NoteId`+start / pitch+start; wrap-held = pitch+start (replace first non-wrap, keep first wrap). Body before wrap is load-bearing.
- No frame or idle-owner call yet. Gate: `test_overdub_source_window_parity`, the focused display batch, full native, target build RAM1 unchanged.
- **Done before Slice 3:** the device owner cadence **T** and C6.4's K; S = 7/25 after the precheck, K = 0 (see §4.8).

### Slice 3 — admission, stale serve, adoption

**Slice 3a (landed native):** `IdleOwnerChild::DisplayBaseGrain` is declared (4 ms / 1 ms fine grain) and the admission test pins it; `DisplayManager` owns a DMAMEM staged candidate and exposes `serviceStagedDisplayBaseTurn` / `stagedDisplayBaseOwed` / `stagedDisplayBaseReady`; `Loop::hasOwedOverdubCoverage` gates the producer behind coverage. The manager-level fixture drives the 20-bar / 13-overdub shape to parity.

**Slice 3b (landed):** the producer's inline tile/index code is instantiated in `LoopContentResolution.cpp` — the LCR-linked flash TU — through `constructFlash` / `startFlash` / `stepFlash` / `cancelFlash`; the DMAMEM candidate is placement-constructed from flash, so no `Prep` constructor or identity-index template code enters ITCM. The idle-owner call site now runs one bounded grain per VisualCache owner turn under `boundedIdleOwnerChildMayRun`. Target build: RAM1 `96,192`, ITCM `391,928 + 1,288` (448 B headroom), RAM2 variables `170,848` (+352 B), flash +4,488 B. The 3b call site initially used the conservative A gate (`hasOwedOverdubCoverage`); 3c1 replaced it with the data-driven due-tick rule below. The frame request/serve/adopt path is 3c2. Full native **3,780 / 8 skipped / 3,772 / 0**.

**Slice 3c1 (landed native; device result is a service-target miss):** the coverage-vs-display arbitration is data-driven. `Loop::overdubCoverageRequiredNow` reads the consumer's `DeferredOverdubCompletion.endTick` (occupancy-only holds are not urgent until note-off) and compares it forward against `kCoverageLeadTicks = 8`; `MidiServiceDrain::chooseCoverageOrDisplay` is the one truth table (urgent coverage → coverage, otherwise display owed → display, otherwise normal idle). The idle-owner call site uses it and returns after the display grain, so the display child owns that owner turn. Native fixtures: the due-tick/truth-table case, the K = 0 continuous-coverage case (candidate reaches ready while coverage stays owed, then coverage drains), and mutations for both failure directions. A report-only `DIAG,disp_base_prep,site=idle,steps,tiles,wrap,us,ready=1` row is emitted on candidate completion. Target ITCM is unchanged at `391,928 + 1,288` (448 B headroom), RAM1 `96,192`, RAM2 `170,848`, flash +312 B; full native **3,782 / 8 skipped / 3,774 / 0**. **Device chain-51:** the row fired with `steps=18, tiles=5, wrap=1, us=1,126,570/1,315,625`; the effective display service interval is ~63 ms/step, so the candidate takes 1.1–1.3 s and the 500 ms target is missed. 3c2 is blocked on the decision above.

**Slice 3c2 (next):** frame request/start on a covered miss, serve stale + retained delta, swap-adopt on ready, named hold, no synchronous fallback; the §4.5 multi-generation C6.1 fixture and the cancellation/adoption cases; adoption zero-copy/allocation. The device run for the `disp_base_prep` row belongs with 3c1's measurement.

- `IdleOwnerChild::DisplayBaseGrain` + registry/enumeration test. **Landed.**
- Selected-track `VisualCache` turn: after the coverage worker reports `Idle`, step one prep grain under `boundedIdleOwnerChildMayRun`; C6.4's K = 0 rule governs fairness. **Landed (3b/3c1).**
- Frame: request/start on a covered miss, serve stale + retained delta, swap-adopt on ready, named hold, no synchronous fallback. **3c2.**
- Fixtures: the §4.5 critical multi-generation fixture (two and three commits during one prep, C6.1 acceptance); commit while overdubbing; slot switch; transport stop; root becomes current; window move inside/outside coverage; stop/undo/load/save during prep; adoption zero-copy/zero-allocation; `base_stale` hold freezes content **and** window geometry. Assert no blank bar, no mixed interval, no duplicate note across generations, no second source read (`overdubSourceViewNotes_` read count = 0). **3c2.**
- Gate: focused display + live-capture boundary + idle admission; full native; target build RAM1 unchanged.

### Slice 4 — device acceptance

Same rig command as `session_20261031_162028.log`, same content, stored image hash and boot rows (`boot_reset,crash=0`, `BOOT,load,ok`). Run the authoritative HITL entry point; do not execute a captured `command.json`.

Acceptance (fixed-window windows):

- **device content gate:** `cap >= 13` and 12 `Overdubbing started` (or the exact accepted-pass count for the take); a run that fails this gate is invalid, not a zero;
- **deterministic stale-root reproducer:** staged request > 0, `DisplayBaseGrain` steps > 0, adoption > 0, `disp_passwalk = 0`, and content parity. A run with no stale-root demand is invalid for this item — `disp_passwalk = 0` alone is not evidence the new path ran;
- commit-driven display frames: **0** whole-pass/committed gathers, **0** stale-root draw-source pass walks, and **0** synchronous committed gathers on the active-transport frame path, proven by `disp_base` plus `disp_passwalk` (not by `DisplayFullRebuild` alone);
- the **20-bar draw-source range** is served from the adopted tile set (five 4-bar tiles); the 16-bar base cap is not used as the range, and `disp_turn` names the range and any hold;
- **class-1 draw-source resolve** ≤ **5 ms** (`kDisplayResolveBudgetMicros`); class-2 frame `us`/wait is reported separately and is a stop-lane gate (DEC-227), not display acceptance;
- every `disp_base` step ≤ **4 ms** (whole call, including completion/filter); no `idle_turn` unit > **8 ms**;
- any held interval is named (`disp_turn why=base_stale`, requested vs published); the hold shows the published window geometry, not the requested interval, and no silent stale frame exists;
- multi-generation: while a delta above the served base is outstanding, it remains visible; adoption releases only the generations the new base covers;
- worst MIDI gap has no 65 ms+ display-attributable gap; if the floor is higher, record the remaining named contributor;
- committed replacement visible within **500 ms** of the commit (p95); the 1 s threshold marks `overdue` and continues, it does not discard or gather;
- content/geometry parity for all 12 windows (zero missing/extra/duplicated), and the live capture overlay still composes;
- adoption shows zero allocations/copies of the committed inventory in the capture trace.

The follow-phase windows are reported separately and are **not** acceptance for this slice (their per-tick rebuilds remain the named next prerequisite).

### Slice 5 — closeout

- Append **DEC-237**; add **C6** (with C6.1–C6.4 and the A-or-B liveness record) to `CONTRACTS.md`; update the `realtime-visual-cache` delta spec with the staged-base and retained-delta scenarios; add the `ExecutionBounds.md` row for `DisplayBaseGrain` (declared unit, cancellation, retained-frame policy).
- Update `PROJECT_STATE.md`, `CURRENT_WORK.md`, `DELIVERABLE_TRACKING.md`, and the active `tasks.md` (new group; 5.4 stays open for its own content/timing gate).
- Commit firmware + tests on `feature/unified-rolling-playback-path`; no PR unless requested.

---

## 7. Risks and open questions

| Risk | Handling |
|---|---|
| The 65–95 ms is dominated by the quadratic/identity merge, not the walk. Fixing the merge may shrink the single-shot gather enough to meet the frame budget without staging. | Slice 1 measures it; the indexed merge is a parity-preserving prerequisite either way. If the single-shot rebuild falls below 5 ms on the rig, record that and keep the staged producer as the long-loop/dense fallback rather than shipping dead complexity. |
| The measured slow owner changed: the base/window sites were fast; the draw-source stale-root gather is the slow one. | Slice 1's three-run attribution records it; the draw source is now the consumer in §4.1/§4.4, and `disp_passwalk=0` is the acceptance signal. |
| Slow frames have two owners: the draw-source resolve spike and the stop-lane wait. | `disp_frame_slow` splits them (chain-56: 2 resolve spikes vs 54 wait frames). The staged base is scoped to class 1; class 2 is a separate DEC-227 gate. |
| The 20-bar paint/draw range exceeds the 16-bar base cap. | The producer tiles the draw-source range in 4-bar units; the base cap is not the range. The five-tile fixture pins it. |
| The rerun silently accepts no record/overdub and produces meaningless fast numbers. | A timing run is gated on `cap >= 13` and 12 `Overdubbing started`; the host phase report is not proof. Chain-53 is recorded as invalid, not as a control. |
| The ordinary owner rig does not deterministically establish the stale-root precondition. | Add the deterministic reproducer. Final acceptance requires positive staged request/step/adoption evidence, not only `disp_passwalk=0`. |
| The retained delta releases against a cache that covers its rows but not the paint window. | C6.1 extends the release predicate to the served base/window; the §4.5 fixture is a Slice 1 RED. |
| Multiple generations commit while one prep is outstanding; adoption of G+2 drops the G+3 delta. | C6.1 is a **range** invariant: release only deltas ≤ the adopted base's generation. The multi-generation fixture is a C6.1 acceptance case, not just a cancellation case. |
| A same-key candidate that legitimately needs >1 s is cancelled and restarted forever. | The 1 s threshold is `overdue` + diagnostic, not cancellation; only a proven no-progress watchdog may abandon a same-key candidate (§4.3). |
| A `base_stale` hold paints old content under the new requested coordinates. | The hold freezes content **and** window geometry; the requested interval stays diagnostic/owed until a qualified source covers it (§4.4). |
| No synchronous fallback leaves a genuine cold start with no source. | A cold/non-realtime state may still build synchronously; active transport holds and names the interval. The fixture distinguishes cold from expiry. |
| Producer starvation under continuous coverage. | C6.4 A-or-B; the Slice 1 duty-cycle fixture decides. No wiring until one is proven. |
| Wrap staging adds one grain per boundary-content pass, not one per active pass. | Measured: the precheck takes S from 36 to 25 dense (7 sparse); K = 0 is ~408–448 ms, under the 500 ms target. Edited slots conservatively fall back to every pass (S = 36, ~590–645 ms); that is the recorded p95 caveat, and a finer edit-aware boundary check is a later refinement. |
| Adoption swaps the whole frame buffer and drops the capture overlay. | The swap is followed by the existing capture-layer sync on the same frame; the fixture asserts the overlay re-appends and G+1 notes occur exactly once. |
| Cancellation freeing a large candidate on the frame. | Cancel only invalidates ownership on the frame; clear/reuse runs on the deferred owner (§4.3). |
| RAM1 / PSRAM growth. | Prep state in `DMAMEM` (placement-constructed; no global constructor in ITCM); `variables 96,192` and `sizeof(Loop)`/`DisplayManager` gates. The candidate buffer and cursor are priced before wiring. |
| The producer's inline tile/index code lands in ITCM when the idle-owner call site references it (+4,656 B measured, over the 12-bank ceiling). | Resolved in 3b: `LoopContentResolution.cpp` (the LCR flash TU) instantiates the producer through `constructFlash`/`startFlash`/`stepFlash`/`cancelFlash`; the DMAMEM candidate is placement-constructed from flash. Target ITCM `391,928 + 1,288` (448 B headroom), RAM2 +352 B, flash +4,488 B. |
| The completion split changes the coverage-tile path's parity. | The coverage caller keeps its current behavior until its own measurement; the display consumer opts into the bounded post-processing. Parity suite runs for both. |
| `DisplayFullRebuild` and the base gather are different counters; the evidence may name the wrong site. | Slice 1's `disp_base` row names the site and phase before any owner change. |

## 8. Verification commands

- `pio test -e native` (full, in parallel with the target build once the slice has a device gate).
- `pio run -e teensy41-capture-serial` (build); after a successful build the upload is pre-authorized — record the HEX hash and prove boot separately over serial.
- Focused: `test_live_capture_display_boundary`, `test_overdub_source_window_parity`, `test_realtime_idle_owner_admission`, `test_capture_ring`, `test_display_window_utils`, plus the new staged-base and retained-delta suites.
- HITL: the owner rig command that produced `session_20261031_162028.log`; stimulus list first, no canonical stimulus edits.

## 9. Slice 1/2 progress (2026-10-31)

- **Checkpoint `94278d67`** (2026-10-31): Slice 1 instrumentation + the Slice 2 scaffold + the stop-edge report-only rows committed on `feature/unified-rolling-playback-path`; HEX `dac97d87…` is the board image (no upload), full native 3,771 / 8 skipped / 3,763 / 0.
- **Slice 2 landed (work-bound producer):** body tiles first, then one wrap-held layer per **boundary-content** pass; incremental assembly under two identity scopes; per-step work counters and a chunk-inspected cap. Native: 31/31 in `test_overdub_source_window_parity`, focused display batch 390 cases (2 skipped), full native **3,778 / 8 skipped / 3,770 / 0**, target build SUCCESS at RAM1 `96,192` and ITCM `391,928 + 1,288` (unchanged), HEX `a6398480…` uploaded and boot-verified. After the precheck S = 7 sparse (5 tiles + 2 passes), S = 25 dense (22 tile grains + 3 passes); the edited fallback is S = 36. Max per step on the dense shape `chunks=8`, `events=64`, `passLayers=1`, `wrapRows=12`, `merge=244`. Mutation-red proved the chunk cap, old per-tile pass projection, the `NoteIdAndStart` key, the first-wrap-wins merge, wrap staging, and the edit fallback. Chain-48/49 measured T ≈ 56 turns/s; S = 25 at K = 0 is ~408 ms at the median cadence and ~625 ms at the worst observed cadence, so **C6.4 selects K = 0 as the only admissible setting**, with the 500 ms p95 still to be proven on the wired path; edited slots remain the p95 caveat. Chain-49 repeated T (`last_us` 9.5–24.2 ms) with no wedge and a 47 ms save→Record margin.
- **Instrumentation (committed in `94278d67`):** `DisplayBaseGatherTrace` and `DIAG,disp_base` around `buildCommittedDisplayBase` (`site=live`), `resolveWindowedDisplayNotes` (`site=window`) and the stale-root `gatherCommittedDrawSourceBars` (`site=drawsrc`). Tier-A through the existing `DIAG,disp_` prefix; one row per over-budget (≥ 5 ms) gather. Report-only.
- **Images:** first instrumentation build `6a84d322…` (live/window); second build `00ce71d5…` (adds drawsrc). Both flashed; boot proven (`boot_reset,crash=0`, `BOOT,load,ok`). RAM1 `96,192`; ITCM `391,672 + 1,544` (704 B above the 840 B floor).
- **Chain-52 (first image):** `disp_base` **0**; `disp_passwalk` 18–29 in the 100–160 s windows, 0 in the 41–76 s windows; slow frames 86–110 ms where the pass walks are high, 1.4–2.5 ms where `disp_rebuild` was 75/window. The base/window sites are **not** the slow owner.
- **Chain-53 (second image): invalid as a control.** `cap=0`, 0 `Overdubbing started`; the host sent 12 windows but the device accepted no record pass or overdub. Its fast display numbers are empty/preview content. The `site=drawsrc` instrumentation has not yet been exercised by a valid take.
- **Chain-55 (second image, valid):** `cap=13`, 12 starts (gate passed); `disp_base=0`, `disp_passwalk` 38 total (25–31 in the slow windows); slow frames 83–106 ms with `stop_tail … work_budget,16` at 89–129 s and `disp_src pending=1, phase=5`. The stop lane is a co-owner, not just the draw source.
- **Chain-56 (third image, valid):** `cap=13`, 12 starts (gate passed). `disp_base` **2** — `site=drawsrc`, 63,983/70,843 µs, 309/327 notes, 20-bar range. `disp_frame_slow` **56** rows split into **2 resolve spikes (63–71 ms)** and **54 wait frames** (resolve 1.7–2.1 ms, draw 5.4–5.7 ms, transfer 0.05 ms, frame `us` 50–98 ms). The draw-source class is confirmed; the wait class is the stop-lane/main-loop owner.
- **Guarded draw-source RED:** `test_stale_root_draw_source_serves_committed_overdub_passes_across_the_sliding_window` now carries `DEC237_STAGED_BASE_RED=1`; with the guard it fails at `drawSourceCommittedPassGatherCountForTest() == 0` (1 failed / 54 succeeded), and without it the suite is green. The failure demonstrates the class-1 gap the staged base must close.
- **Guarded retained-delta RED:** `test_dec237_retained_delta_survives_a_cache_cover_while_the_base_is_stale` carries `DEC237_RETAINED_DELTA_RED=1`; with the guard it fails at the C6.1 assertion after a cache-only release (1 failed / 55 succeeded). Without the guard the display-boundary suite is **56 cases: 1 skipped, 55 succeeded**.
- **Content gate:** a timing run is usable only when the device shows `cap >= 13` and 12 `Overdubbing started` (or the exact accepted-pass count for the take). The host-side `midifile-chain.json` phase report is not proof of device acceptance; `scripts/hitl_midifile_chain.py` now enforces the gate and records `device_record_starts`, `device_overdub_starts`, `device_cap_last`.
- **Gates:** `test_capture_ring` 38/38 PASS (including the `disp_base` and `disp_frame_slow` Tier-A assertions). `test_overdub_source_window_parity` **30/30** PASS, including `test_dec237_20_bar_draw_range_is_five_tiles`, the fixed-set and continuous-input duty-cycle fixtures (**B holds: 64/64 owed**, then Idle), the original sparse parity (**S = 19**, 5 tiles + 14 wrap layers, 15 notes), the dense work-bound/parity/wrap-identity cases, and the chunk-cap primitive. `test_live_capture_display_boundary` 56 cases (1 skipped guarded RED, 55 succeeded). Target builds SUCCESS.
- **Device blocker (2026-10-31, chain-57/58):** the Record stop wedged at `normalization preparation_denied deny=order_denied cursor=13 at=1 phase=2`. `cursor=13` is `RecordPreparationCursor::Phase::Denied`; `at=1` is the **pre-denial Order phase** (not Reorder); `phase=2` is the stop's `CanonicalOrder`. Chain-58 adds `STOP_TRACE phase=2, canon=276, order=0, passes=0, len=15360` and `disp_src cpass=0, pending=1, phase=2`. The order **preflight** rejects the private output through `stepPrivateTickOrder`'s per-chunk `validChunk` (bad/unused chunk id, `poolChunkRefCount != 1`, persistence state `!= NotScheduled`, or `chunk.used != expected`); the dense-chunk-count check already passed and the credits remain, so the failing term is a per-chunk state. The take never commits and no overdub can open. This belongs to the stop-edge/canonical-order owner: name the exact `validChunk` term (a `denyDetail` code), then either clear/rebind the canonical output or fix the chunk state; the terminal-denial retirement remains the liveness backstop. The board was recovered by re-uploading the same image (no stall after reboot). A valid chain is required before T can be recorded.
- **Remaining before Slice 3:** **C6.4 is decided: K = 0** (S = 25 dense; ~408 ms at the median cadence, ~625 ms at the worst observed; K = 1 ~815 ms). The 500 ms p95 must be proven on the wired path in Slice 4. The edited-slot fallback (S = 36) is the recorded p95 caveat. The device whole-call µs remains Slice 4's gate. The stop-edge wedge is still unreproduced (`order_deny=0` in chain-48); the next attempt must target the clear→Record race. No producer/frame wiring until the editable fallback and the device preconditions are re-checked in the Slice 3 fixtures.
- **Update (2026-10-31 18:45, report-only `denyDetail` landed, not reproduced yet):** `LoopEventStore::stepPrivateTickOrder`'s
  `validChunk` now calls the FLASHMEM `reportPrivateOrderChunkDenial` on its first rejection. It emits one row per denied
  cursor: `DIAG,order_deny,reason=<r>,phase,canon,chunk,id,ref,persist,inuse,used,expected,count,chunks`, with reason
  1 index, 2 id ≥ pool, 3 id unused, 4 refcount ≠ 1, 5 persistence ≠ NotScheduled, 6 used ≠ expected. Every observed
  term is printed. It is Tier-A (pinned in `test_capture_ring`) and direct when USB has room. No result changes. ITCM
  cost: +64 B (padding 1,480, floor 840); the lambda body stays the original expression. Image `2984670c…` (owner tree
  plus the row) is on the board, retained in `captures/order-deny-detail-2984670c3d53/`. Full native 3,768 / 8 skipped /
  3,760 / 0.
  **Reproduction:** three valid rig chains on it (`midifile-chain-59/62/63`, serial `session_20261031_183306.log`), 36
  overdubs, **no denial**. **Correlation across the owner's slice-1 sessions:** in all three denied sessions (16:54,
  17:57, 18:16, ~58 s after boot) the take has **zero** `PERS,mid_pass … ok:t7:s3` rows; every passing session has
  19–109. Other-slot `LoopPersist` runs in both, so it does not discriminate. Code fact: sealed capture chunks are
  admitted to the persistence queue (`sealChunk` → `admitSealedChunk`, `Queued`), and the preflight accepts only
  `NotScheduled`. That makes reason 5 the leading suspect, **unproven**; the next denial on this image names the term.
  Do not clear or rebind canonical storage before it does.
- **Update (2026-10-31 19:20, native reproduction of the save/record race):** across the owner's sessions the denial
  happens exactly when Record arms **before the clear's workspace save has finished the recording slot**: passing runs
  finish `LoopPersist,slot:7:3` 156–370 ms before `Recording started`; the three denied runs were still at
  `SlotMeta,slot:7:1..7:3,start` (`captureActiveForScheduler` pauses the save; `shouldDeferMidPassForWorkspaceSave`
  defers mid-pass while it is pending). Native fixture `test_record_stop_commits_while_the_clear_save_is_in_flight`
  (`test_playback_prewarm.cpp`, frozen-reader suite) drives the production scheduler: the clear's own
  `markTrackSlotsMaterialDirty` / `admitTrackSlotPersistence` / `requestDeferredSaveState`, a dense 20-bar take with
  scheduler turns during capture, then the stop job interleaved with `processDeferredSaveState`. Under
  **AddressSanitizer** it is a **heap-use-after-free**: the resumed save's `LoopPersist` finalize for the recording slot
  (`stepFinalizeDeferredLoopSlotTemp` → `CurrentSetStorage::removeLoopSlotSealJournal`, `WorkspaceSave.cpp:361`)
  removes the slot's seal journal while the mid-pass writer holds it open
  (`appendMidPassJournalRecord` → `File::write`, `MidPassChunkPersist.cpp:109`). Without ASan it crashes or passes
  depending on layout (exit 139 on direct runs), so this case is a deliberate **RED** and currently errors the suite.
  On the device a failed append re-queues the chunk with no `ok` row, matching the zero mid-pass rows. **Not yet
  shown:** how that turns into the order preflight's rejected chunk; the `DIAG,order_deny` row on `2984670c…` names it.
  **Repair is an ownership decision, not started:** the slot save must not remove a seal journal that the live
  capture's mid-pass writer owns (closing it first, skipping its removal while that slot is capturing, or re-binding
  the journal), and the stall's terminal-denial retirement stays as the liveness backstop.
- **Correction (2026-10-31 19:50) — the ASan finding is not the device mechanism.** The use-after-free fired inside
  the *recording* loop (`processDeferredSaveState` during capture), through a journal handle the **previous fixture
  iteration's** take left open: `resetMidPassChunkPersistState()` runs only at boot, so the mid-pass job stays bound
  with `journalOpen` across takes, and the clear's `LoopPersist` finalize removed that file under it. That is a real
  latent defect (a later take on the same slot within one boot appends through a handle to a removed journal;
  `ensureMidPassJournalOpen` returns early on `journalOpen` + same slot), but every denied device session was the
  first take after a fresh boot, with the job unbound. Re-run from boot state (`resetMidPassChunkPersistState()` +
  `SD.reset()`), the fixture commits for every pause point (0–8 save steps before Record), with no ASan error, and
  the during-capture flush (`flushOneQueuedCaptureChunkDuringTransport`, which ignores the workspace save) drains
  every sealed chunk. So the native model does **not** reproduce the device's zero mid-pass rows. Also corrected:
  `hasPendingDeferredOverdubStop()` is `captureStopJobs_->count != 0` and covers the Record job, so the save **is**
  parked during the Record stop's queued work.
  **Open:** why the device's during-capture flush writes nothing in the denied runs. Its silent early-outs are
  `queueDepth()==0`, `sdIoActive`, `deferredSaveBlockedByActiveSlotLoadSd()`, and inside `stepMidPassChunkPersist` a
  slot-resolve failure (re-queue, no row) or `ensureMidPassJournalOpen` failure (no row). None of those emits a row.
  The generation/ownership guard for stale slot saves is still the right contract, but the evidence no longer ties it
  to this stall, so it is **not implemented**.
- **Update (2026-10-31 19:30) — report-only mid-pass outcome rows landed (image `dac97d87…`, on the board, retained in
  `captures/midpass-outcome-dac97d872577/`).** `DIAG,midpass_skip,reason,queue,writing,detail,prev_repeats,
  admit_refused,admit_last` is emitted only when the writer's outcome changes. Reasons: 0 wrote, 1 queue empty, 2 SD I/O
  active, 3 slot-load SD, 4 no queued chunk, 5 slot unresolved, 6 journal open failed, 7 journal append failed,
  8 deferred for the workspace save. Admission refusals in `admitSealedChunk` are counted in the zeroed queue state,
  not emitted (seal runs on the capture path); reasons 1 no queue, 2 not sealed, 3 already scheduled, 4 queue full.
  The row is Tier-A (pinned in `test_capture_ring`); `DIAG,order_deny` is still present. Cost: ITCM +192 B (padding
  1,288, 448 B above the floor), RAM1 variables unchanged. Focused suites 117/117.
  **Smoke run** (`midifile-chain-64`, `session_20261031_192235.log`, passing chain, 18 rows, `tierA_evict=0`): at the
  clear (51.198 s) **15 chunks were already queued** and deferred (`reason=8`, 1,308 repeats) until the clear's save
  finished slot 4 (`LoopPersist,slot:7:3,done` 54.175 s). Record started at 54.407 s (232 ms margin), and the first
  write followed at 54.49 s. `admit_refused=0` throughout. The next stalled session names what happens when Record
  wins that race, alongside the `order_deny` term.
