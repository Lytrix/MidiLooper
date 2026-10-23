# I5 — append stability and bounded prefix/delta preparation

**Status: live-source stability/lifetime/reuse design remains unimplemented and unqualified.** Owner later approved firmware/native work toward readiness; the rawprivate delta-order prerequisite is now implemented under [DEC-124 — bounded private append ordering / execution evidence](DELTA-ORDER-EXECUTION.md). That closes neither the source receipt nor Track publication/service contract. Original documentary capture below is historical; no size-pin rebase or timing-policy change is inferred. Existing Track/Loop ownership, three views, default rolling OFF and compatibility emitters remain.

## Verified basis

- `Loop::appendCaptureEventWithResult` assigns IDs to the new local row and appends it through `LoopEventStore::appendToTailChunk`. A successful ordinary append preserves already populated raw rows and global ordinals. Nonmonotonic input marks sorting dirty; it does not sort on append.
- This is a narrow raw-prefix property. `ensureCaptureEventsSorted`, missing-ID assignment, removal, Record stop projection, retirement, clear/reload and seal/transfer can change rows, ordinals or owner. Some capture-reading APIs call `ensureCaptureEventsSorted`. Count growth alone cannot distinguish append-only growth from mutation followed by growth.
- `captureContentGeneration_` changes for both accepted append and destructive mutation. Store ownership and pool stamps do not individually certify prefix stability. Bare chunk IDs, Loop address and LoopId are not sufficient lifetime receipts.
- Fill retains source cursor and copied count but rejects any live extent/token change. Order retains fill/merge/copy-back state; after order fill, merges use the fixed `orderedEventCount`. There is no safe suffix-extension API today.
- Fresh discovery check: `pio test -e native -f test_playback_prewarm -f test_loop_take_survival`: **358/358 succeeded**, exit0. Existing same-count-sort and post-fill/completion rejection tests passed. This is mutation-safety evidence, not prefix reuse verification. Log: session scratch `i5-prefix-discovery-focused.log`.

## Proposed source receipt

**Owner:** Loop, alongside the existing Capture content generation. Add a separate 64-bit append-stability generation (provisional implementation identifier; not a new content-currency authority).

A qualified attempt retains:

- identified slot/runtime lease and Track operation generation;
- Capture interval and source owner identity;
- append-stability generation and current Capture content token;
- qualified source extent, inspected frontier and copied-event frontier;
- fixed projection/coverage and destination storage generation.

The content token remains the complete-content currency receipt. The proposed stability generation only answers whether rows already inspected/copied are still valid. It is not a source pin, snapshot, current-version substitute or permission to read recycled storage.

### Mutation rules

1. Successful ordinary accepted append changes content generation and extent but preserves append stability. Failed/refused/duplicate input does not advance either receipt merely to pretend progress.
2. Prior-row writes, ID assignment, reorder, erase, compaction, projection, clear, owner transfer, retirement and source replacement invalidate append stability **before** the operation can expose changed rows or ownership. Conservative invalidation is permitted for these destructive operations, not as an excuse to restart on every ordinary append.
3. Begin/discard/end Capture and sealed-range handoff terminate the old live receipt. New Capture cannot reuse an earlier interval's progress.
4. Zero is terminal on counter exhaustion; neither generation may wrap into a previously valid receipt. Pool reset/runtime replacement also invalidates by their existing lifetime/operation guards.
5. Geometry/coverage changes independently invalidate projection-dependent work even if raw bytes are stable. Growing Record prediction is not automatically the provisional playback geometry.
6. Cooperative worker boundaries must validate the receipt before source reads and before commit. Any real concurrent producer path requires coherent before/after sampling; no interrupt-safety claim follows from scalar fields alone.

**Implementation closure required:** enumerate every reachable live-Capture mutator, including direct store calls and sorting consumers, and place the invalidation at its actual write/transfer boundary. An uninstrumented mutation is a defect. New checks cannot weaken existing content-token safety for legacy consumers. Public mutable access or store operations that bypass Loop must be covered explicitly or excluded by an enforced ownership precondition—not assumed absent.

No new physical source pins are proposed for the cooperative live borrow. Cancellation-before-destruction and stale-generation refusal must be proven for stop, clear/load, slot reuse and undo. Frozen source work continues to use job-owned CaptureRange receipts; a live stability receipt cannot be silently transferred to that different owner.

## Proposed private preparation protocol

Track owns the existing private `build`; served `current` and completed `ready` remain immutable. No fourth view, new event vector, global scratch or second publisher.

### Frozen attempt extent

- Begin an attempt against a coherent source receipt and fixed extent N. Retain inspected/copy frontiers across ordinary append; do not call the existing resetting `begin` to extend it.
- Appends beyond N update the retained newest demand but do not change the current sort's extent. Continue useful prefix work under the stability receipt; it is private and not falsely labelled with the new content token.
- Finish the bounded fill of the frozen extent and its bounded prefix order. Appends during merge/copy-back do not change run boundaries or force a restart; they wait as an extent delta.
- Once prefix work is coherent, freeze the next extent N2 and inspect only the new rows `[N,N2)`. Matching delta rows append to the same private event storage within its existing capacity. Nonintersecting rows still consume inspected-work budget.
- Out-of-order ticks and same-tick Off/On do not permit an append-order fast path without validation. Use the same playback comparator/provenance tie-break as ordinary ordering.

This is **reuse of certified private work**, not publication of an old prefix. A failed stability/geometry/lifetime check invalidates genuinely stale work; retained content/deadline/miss obligations survive through the existing request protocol.

### Bounded ordering with existing arrays

Let P be the coherent ordered prefix event count and Q the frozen candidate count after delta fill.

1. Preserve prefix order `[0,P)` in `storage.order`.
2. Extend the existing order builder with explicit subrange boundaries to project and sort only delta indexes `[P,Q)`. Use only `[P,Q)` in `order` and `mergeScratch` during this sort; no delta operation may overwrite prefix indexes.
3. After delta order is coherent in `order`, merge the sorted prefix and delta into the existing `mergeScratch[0,Q)`, one bounded comparison/output unit at a time. Both input ranges remain in `order` until this merge ends.
4. Copy back at most the configured quantum of indexes per subsequent grain. Do not read from `order` as merge input while copy-back overwrites it.
5. Only after copy-back completes is Q the new coherent prefix frontier. Later accepted rows are processed as another frozen delta; no array alias, dynamic extent growth or half-merged output is exposed.

The event bytes never move for sorting; only the prewarmed phase/index arrays change. Capacity remains the existing per-window limit. A nonempty delta can require O(P+delta) index merge/copy-back work; **this is not constant-cost late input**. Repeated tiny deltas can repeatedly incur that work and must be included in the supported workload/service proof. An empty matching delta can avoid index work only after all new source rows are inspected and their irrelevance is certified.

`RuntimePlaybackOrderBuilder::extendPrivateOrder` now implements frozen suffix-only fill/sort and bounded prefix merge/copy-back for caller-certified rawprivate fixed storage; typed views/borrowed-index attempts refuse. `extensionWorkEntriesUpperBound` counts suffix operations and full merge/copy-back. [I5 — delta-order evidence](DELTA-ORDER-EXECUTION.md) pins fourcases/270independentmatrix combinations and unchanged72target/136host state sizes. This is not an extension API for the live Capture source, fill cursor, source receipt or published view. Current fill/build `begin` still resets destination metadata; Track must certify the unchanged prefix/lifetime/projection and current complete-content currency before using private reuse.

### Commit and supersession

Before publication, Track checks source stability, latest accepted extent/content token, source eligibility, operation/lease/storage generations, final coverage/projection and clock/adoption position. All accepted rows and closure obligations affecting the candidate must be incorporated. Prefix work retains its old receipt until that validation; do not relabel it merely because raw bytes appear stable.

On further append, coalesce newest content requirements without refreshing the oldest deadline. Finish coherent useful work and process the new delta. Ready already handed to a consumer is not mutated by this protocol. Distinct-coverage obligations and the existing pending/frozen record capacity remain a separate contract dependency; this proposal does not add an unbounded queue or settle that issue.

At matching/mismatching Record stop, use the frozen job's existing handoff protocol. Any reusable private work needs an explicit raw-to-final projection/closure equivalence proof; otherwise its old projection is genuinely invalid. Prior mismatching-stop tests do not prove that equivalence for incremental preparation.

## Resource and execution accounting

- Target pins observed in `TrackPlaybackRuntime.cpp`: **Loop992 B**, **LoopPlaybackRuntime7632 B**, **TrackPlaybackRuntime32 B**. Native runtime pin is9768 B. This proposal does not alter them.
- [I5 — exact candidate pricing/audit/service witness](STATE-RESOURCE-SERVICE.md) now compiler-measures a concrete64B target/72B host runtime record including a pool-qualified forward source cursor, plus8B Loop counter. Append-to-owner target aggregates are Loop1000/runtime7696;64 shells+8 runtimes add1024B payload. These are candidate aggregate prices, not actual production layout/RAM1-link results or pin-rebase approval. Existing generic builders occur twice per runtime; direct runtime placement avoids duplicating the record there.
- Preferred state placement is the existing Loop source owner and existing externally allocated slot builder/runtime; no Track DTCM/global shadow owner. If adding the counter enlarges other resident stores or LoopPool shells, price every affected cardinality. RAM1 impact must be linked/measured, not inferred from external-first allocation.
- Only existing prewarmed event/order/phase/scratch arrays are used. Resident playback cost remains window-sized; no extent-sized copied Capture mirror. Source scan can still grow with unrelated history: skipping already inspected rows helps append reuse but does not by itself solve initial long-source interval lookup.
- Bound each grain by inspected source units or index-operation quantum, including validation/transitions/copy-back. Declare changes to the execution-bound inventory only with enforcement/evidence. No full merge, sort or allocation on the MIDI/stop/clock edge.
- Charge remaining fill/delta order/merge/copy-back, validation, begin/extension, publication, closure/source prerequisite work, retirement and competitor turns. Do not count the original prefix work again once certified, but do not omit still-pending merge work.

**Service gate remains open:** existing Off740→adoption768 gives28ticks; On1468 is not a later legal mid-traversal adoption point. Prefix/delta reuse alone does not prove this deadline for eight sounding roles. No new Delta, priority, margin, denial limit or active envelope is approved by saving this proposal.

## Fixture-first acceptance needed before implementation

- Real Track input grows a live source between each fill/order/merge/copy-back/publication step; count source visits independently and assert old prefix work is retained, not silently rescanned.
- Same-count sort, mutate-then-grow, ID writes, removal/projection, seal/transfer, phase closure, clear/load, pool reset and stale runtime reuse invalidate before stale reads/publication. Counter exhaustion refuses safely.
- Delta includes lower ticks, equal-phase Off/On, wrap-end/zero events, duplicates and nonintersecting rows. Independent ordered MIDI/ledger oracle must match a cold full-source reference without using the candidate's own order as expectation.
- Multiple append waves during prefix merge and delta copy-back preserve frozen boundaries/frontiers; exact mutation controls detect prefix overwrite, dropped delta, refreshed deadline and premature token relabel.
- Admission refusal, ready occupancy and source retirement retain oldest content/position and recover through production main's service ordering; no stationary-clock timing claim.
- Matching/nonmatching stop, root-before-edge, pending closures, source release, sibling sounding slots and transport/epoch transitions retain existing semantics.
- Measure source0/2/66/130 and one/four/64-bar inputs, then density/repeated-delta workload with actual competing owners. Host/native grain reductions are not device service proof.
- Full native/default build/RAM1/PSRAM evidence and one focused lifecycle/oracle review follow a ready execution packet; managed registered hardware remains separately authorized. No parent checkoff from local builder completion.

## Disposition and next action

**Preferred direction (proposal):** Loop-owned append stability + frozen private extents + bounded delta-sort/merge/copy-back on existing arrays. Confidence is high in the required distinction and moderate in implementation/resource feasibility; no pass is claimed for the missing mechanism.

G1 — architecture contract remains open on exhaustive mutation coverage, exact state/resource price, source-lifetime/stop equivalence, distinct-coverage coalescing and initial anchor. G2 — execution contract additionally needs composed numerical service closure and controlled acceptance fixtures. This file narrows the mechanism but does not generate executable firmware tasks while those dependencies remain open.

Owner approved this file plus links/refinement in CONTRACT.md, design.md, tasks.md and runtime state/scope only. No firmware/test/DEC/authority/size-pin/SD/hardware/Git change. The358-case native check belongs to preceding read-only discovery; this document-only capture will use link, strict OpenSpec and diff checks without claiming another native run.

## Documentary closeout

Saved SOURCE-PROGRESS.md, refined CONTRACT.md, added design/task references and refreshed runtime state/scope within the owner-approved file list. `openspec validate windowed-playback-generation --strict`: valid; local Markdown file links resolve; active documentary scope present; `git diff --check`: clean. Task checkboxes remain29complete/31open (60total). No additional test run, code/build/device action or decision/policy approval is claimed by this capture.

**Host feasibility follow-up:** [I5 — pricing, mutation gaps and counterexample](STATE-RESOURCE-SERVICE.md) records591/591 focused passes and a production-selector witness that gives track0 its next service at1252 after acceptance740, missing adoption768 even with just one owed grain. It also maps pre-transfer/empty-close/closure receipt hooks, live pool validation, explicit liveness and bounded chunk traversal still needed. Mutation/lifetime coverage is not closed and the current composed service guarantee is falsified—not waiting solely for HITL.

**Implementation follow-up:** DEC-124 — bounded private append ordering closes only the specified existing-array ordering primitive, with actual firmware/native verification in DELTA-ORDER-EXECUTION.md. Loop stability receipt/mutator closure, bounded forward source cursor, source-stop equivalence, distinct-coverage demand and initial running anchor still require their own dependency-closed execution before Track reuse. No counter field or new live source hold is silently introduced by the order extension.
