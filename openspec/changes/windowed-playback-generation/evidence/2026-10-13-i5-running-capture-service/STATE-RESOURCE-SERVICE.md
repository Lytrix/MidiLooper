# I5 — candidate state pricing, mutation coverage and service feasibility

**Result: candidate ABI pricing measured; mutation/lifetime audit mapped with concrete implementation gaps; current service guarantee falsified on host. NOT a passing readiness contract.** User requested continuing exact pricing, coverage and composed bound until HITL is required. Hardware is not the first missing gate: the current admitted opportunity bound cannot satisfy the existing28tick adoption lead. No firmware or size-pin change, new arbitration policy, upload/device/SD action, commit or task checkoff.

Branch `feature/unified-rolling-playback-path` @ `26125ceb` plus preserved dirty tree. References: [source-progress proposal](SOURCE-PROGRESS.md), [request/service draft](CONTRACT.md), [production fixture evidence](EXECUTION.md). This measurement/audit narrows those proposals but does not approve them for Apply.

## 1. Exact candidate layout and ABI measurements

The measured scratch layout is explicit below. It is not an implementation of incremental fill/order, and no production header was modified.

```cpp
struct CapturePrefixWorkPrice {
  uint64_t stabilityGeneration;
  uint64_t sourcePoolLifetime;
  uint64_t captureInterval;
  uint64_t incorporatedContentToken;
  size_t frozenSourceExtent;
  uint16_t orderedPrefixEvents;
  uint16_t deltaEndEvents;
  uint16_t deltaRangeStart;
  uint16_t mergePrefixCursor;
  uint16_t mergeDeltaCursor;
  uint32_t sourceChunkIndex;
  uint16_t sourceRowIndex;
  uint16_t sourceChunkEnd;
  uint16_t mergeOutputCursor;
  uint16_t copyBackCursor;
  uint8_t stage;
  bool extentPending;
};
struct LoopAppendPrice { Loop baseline; uint64_t stabilityGeneration; };
struct RuntimeAppendPrice {
  LoopPlaybackRuntime baseline;
  CapturePrefixWorkPrice extension;
};
```

Existing source/destination pointers, fill source ordinal/copy count, operation/lease identity, coverage/projection and request deadline are reused, not duplicated in this record. Source extent uses size_t; event-index fields are16bit because the existing window cap is2048, not a16bit Capture length limit. The forward cursor is an ordinal coordinate, not a pointer into a reallocatable directory. Proposed bounded source-reading APIs and generation validation do not exist yet.

**Target compiler measurements (ARM Cortex-M7 hard-float ABI):** Loop baseline992; runtime baseline7632; generic build baseline184; record64/alignment8; Loop append-to-owner1000; runtime append-to-owner7696; generic build append-to-owner248. **Host compiler:** Loop1584; runtime9768; build304; record72/alignment8; Loop1592; runtime9840; build376.

These are exact sizes of the listed C++ aggregates compiled against current headers, not actual future embedded production layouts. Member insertion or use of existing cursor storage may change alignment/reuse; any revised layout must be measured again. Production assertions remain Loop992/runtime7632/TrackPlaybackRuntime32 target and runtime9768 native. No pin was rebased and no linked RAM1/code placement claim is made.

The first price excluded the physical cursor: record56 on both ABIs, target runtime7688. The mutation audit found unpriced global-ordinal directory walks, so that price was explicitly superseded by the final64/72B record. Final evidence must not reuse the earlier56B figure.

### Placement/cardinality

Direct per-slot runtime placement is preferred for this proposal. LoopPlaybackRuntime embeds both viewBuild and bridgeWindowBuild; putting the full extension into the generic builder type would duplicate it in these owners (and affect other instances). A direct64B target runtime extension avoids that accidental multiplication; this does not prove that all API implementation will need no additional state.

LoopPool allocates8 Loop shells per allocated Track: source-counter delta8*8=64B per array,512B across8 arrays. Eight allocated target runtimes add8*64=512B; together **1024B payload**, excluding allocator overhead and linked code. At64 allocated runtimes the payload would be512+4096=4608B; this is a cardinality illustration, not approval for permanent runtime residency in all slots. Only active-role runtime scaling is allowed. Existing three-view arrays add no new payload in this layout.

LoopPool uses extmem first with internal fallback. External-first does not imply zero RAM1 effects: actual fallback, constructor/code growth, alignment and FlexRAM bank pressure require a real implementation build later. No full firmware build or RAM1 relink was needed/performed for this header-only price.

### Reproduction/evidence

Scratch `i5-state-price.cpp` defines the aggregates and extern-C char arrays sized by sizeof; ARM `nm -S --size-sort --radix=d` reports their sizes. Native executable prints sizeof/alignof directly. Target compiler: `/Users/eelkejager/.platformio/packages/toolchain-gccarmnoneeabi-teensy/bin/arm-none-eabi-g++`; flags C++17, cortex-m7/thumb, hard float/fpv5-d16, current Teensy41/core/USB_MIDI_SERIAL/capture defines and framework library includes from `pio run -e teensy41-capture-serial -t envdump`. This compiled only a scratch object; symbols are measurement scaffolding, not real resident allocations.

`compile_commands.json` contained native flags and was not used as target evidence. Initial compile attempts lacked MIDI.h, then incorrectly selected the local mbed-style MIDI package; both failed before an object was produced. Final target include resolution uses the Teensy framework MIDI library, not that package. No failed attempt is counted as a size result.

Final logs: `i5-state-price-arm-command.txt`, `i5-state-price-arm-final.log`, `i5-state-price-arm-final-sizes.txt`, `i5-state-price-native-final-sizes.txt`. Source/layout and commands are in session scratch. The former56B result remains historical in `i5-state-price-*-sizes.txt`, not the final price.

## 2. Mutation/lifetime coverage map

Audit searched production src and relevant include headers for Capture producer aliases, direct mutations, sorting consumers, lifecycle callers and existing tests. This is a mapped implementation obligation, **not completed runtime coverage**. Loop-level stability remains a feasible direction for located production paths, but public producer access is not an enforced boundary.

- **Ordinary accepted append:** `Loop::appendCaptureEventWithResult` writes only the new local row, assigns its ID before append and advances content after success. Preserve stability; refuse/duplicate leaves the receipts unchanged. Existing content-generation test covers currency, not prefix reuse.
- **Sort, including read-triggered:** `ensureCaptureEventsSorted` invalidates content before helper clear/reload. Add stability before write. `copyCaptureIntervalEvents` and committed-with-Capture materialization can invoke sorting while Record is active; ingress-only hooks are insufficient. Existing prewarm tests cover same-count sort during fill/after fill and completion.
- **Missing IDs:** `assignMissingNoteIdsInStore` and window variant advance content in the allocation callback before assignment when target aliases capture.store. Add stability there, preserving the no-op case. Snapshot publisher can reach the window variant during live Capture.
- **Begin/discard:** `beginCapture`, `discardCapture`, `clearCaptureOnNewPass` must invalidate the interval before extent reset/clear, even when reserved storage is retained. Existing content hooks and tests establish eventual currency rejection.
- **Copied-source retirement:** `retireCopiedCaptureSourceGrain` must invalidate before chunk removal and terminal phase closure, including empty terminal work; current content hook is conditional on nonempty source.
- **Seal/cut:** `sealCaptureRangeForJob` currently transfers ownership first and advances Loop content afterward. Proposed stability must invalidate before transfer while saving pre-transfer content provenance; use the existing seal preflight to preserve refusal semantics. Existing tests prove final receipt/cut, not pre-transfer ordering.
- **Phase close:** `closeProducerCapturePhase` changes phase without content-generation advance. Live source callback later returns zero; add explicit pre-close termination for reusable work.
- **Record stop projection:** `projectCaptureAtRecordStop` already advances content before tick rewrite/drop; stability must do likewise. Deferred stop uses frozen/private output instead and requires a distinct owner receipt.
- **Generated closures:** `finalizeCaptureWrapWindowAtStop` can append via the store helper directly and advances content only after the helper. This bypasses ordinary ingress bookkeeping; require a pre-write stop/closure boundary and independent projection/closure equivalence, not automatic append certification.
- **Seal minimum-length filtering:** invalidate in the existing pre-compaction callback. Earlier sort/ID/closure effects are separately accounted.
- **Prepared transfer/commit:** `stagePreparedCapturePassWithChunkStorage` already invalidates content before a live-source detach; preserve its alias check so unrelated private canonical output does not invalidate the new producer. `completePreparedCapturePassAtomic` needs explicit interval termination even after an empty detach.
- **Remove-open-note helper:** sorting then clear/reload; invalidate before compaction. No production caller located for ordinary Record, but the API can mutate the public producer and cannot certify reuse if invoked.
- **Overdub extraction/undo/redo:** pre-clear/reload hooks exist; these are phase-gated Overdub/session paths, not ordinary Record ingress. Do not transfer their proven phase guards to all Capture APIs.

### Cancellation and replacement

Record start prewarms before reset; raw first-bar preparation occurs before Capture interval start/raw-origin assignment. Initial preparation uses an empty emission anchor and live-current owner requires epoch0: stopped nonzero-epoch and running-transport initial anchoring remain absent.

Deferred Record stop qualifies capacity, stamps final geometry, cuts raw ownership then moves held notes to the job. Existing next-step token/count rejection is not an incremental borrow handoff certificate. New pre-transfer stability boundary and source/projection/closure equivalence are required.

Clear supersedes a matching finalizer, then resets/discards producer. Cache invalidation is not builder cancellation. Current source checks reject before later reads while Loop stays alive; no UAF is claimed. Clear-undo restores through discard-before-pass-restore, with playback reset later; proposed source invalidation must still precede destruction.

Load admission calls noLiveOwnerForSlot, but its current terms name admitted coverage/source/launch/gather/paging work—not every generic live builder borrow. The proposed retained live request/borrow must become an explicit liveness term or be demonstrably included in an existing one. This is a concrete missing gate, not a proven current load corruption.

Runtime release checks lease generation before reset. Pool reset frees pool and advances lifetime without changing Loop content; live source reads currently lack the frozen range's pool-qualified ready/read checks. A reusable live cursor must validate pool lifetime before access. `LoopEventStore::at` walks the directory from chunk zero for each global ordinal: delta row count alone does not bound source lookup. The final priced cursor addresses storage coordinates, but a bounded read API plus tests remains missing.

No production direct capture-store assignment, shiftAllTicks, adoptAll or resumable rewrite caller was found in the searched scope. Temporary committed staging and private canonical outputs were distinguished from live Record. Public `Loop::capture` still permits bypass; enforce owner methods or guarded adapters rather than assume future callers comply.

**Remaining coverage gaps:** production-path fixtures for pre-write receipt timing, append reuse, mutate-then-grow, read-triggered sorting, empty close, seal transfer, live pool reset, clear/load/undo/slot reuse and stop equivalence. Full outer load-commit admission composition and interrupt/asynchronous coherence were not established. This is not an exhaustive proof of every possible external misuse.

## 3. Host service counterexample

Acceptance requires existing late-input successor adopted at768 after Off740, lead28. Traversal immutability forbids substituting the later On1468 tick as a mid-traversal deadline. No assertion/geometry/deadline was changed.

A standalone native witness invokes **the production `selectPlaybackHolderRoundRobin`**, with8 eligible/progress-capable tracks and cursor.next=1 after track0's preceding service. At each lane opportunity tick advances64—the permitted gap. Selections are tracks1..7 then0; ticks804,868,932,996,1060,1124,1188,**1252**. Every track receives its round-robin turn, yet track0 cannot complete even one post-append grain before768. A single-track opportunity delayed64 also arrives at804, after768.

Independent arithmetic, source cost/margin/sibling work optimistically zero:

`G_remaining * 8 * 64 <= 28`

For1..5 remaining grains the allowed worst-case window budgets are512/1024/1536/2048/2560ticks; none passes. Maximum lane gap needed would be3.5/1.75/1.166667/0.875/0.7ticks respectively, before any other required work. Those are necessary-condition calculations, **not approved replacement budgets or device measurements**.

The witness asserts production selection order and failure arithmetic and exits0 with `COUNTEREXAMPLE PASS`. That means the falsifying witness is verified—not playback readiness passes. The trace is permitted by the current service contract, not a measured physical run. No actual512tick gap or physical MIDI loss is attributed to hardware.

Source-prefix reuse reduces redundant work but cannot waive all work caused by a newly accepted Off. Proposed delta sort/merge/copy-back, token revalidation and publication still owe opportunities. A previously complete prefix cannot already contain an Off not yet accepted. Even optimistic one-grain fusion lacks a passing28tick guarantee under the existing opportunity allowance. Do not silently reduce sounding roles, grant mode-wide priority, set margin zero or do work on the clock/input edge.

Outer deferred-owner rotation already contributes to the defined lane gap: do not multiply the7-owner cycle again. Prerequisite source/finalizer costs only make this counterexample worse; they are not needed to establish infeasibility. Exact composed service remains unproved for any stronger proposed arbitration/interval mechanism.

Witness source/log: session scratch `i5-service-witness.cpp`, `i5-service-witness.log`; compile `clang++ -std=c++17 -Iinclude -include initializer_list <source> -o <scratch executable>`. No duplicated round-robin implementation or firmware source mutation.

## 4. Verification and disposition

`pio test -e native -f test_midi_service_drain -f test_runtime_working_set_ownership -f test_playback_prewarm -f test_loop_take_survival`: **591/591 succeeded**, exit0, four PASSED suites. Log `i5-pricing-feasibility-focused.log`. No fresh full-native or firmware build/device result is claimed; unchanged predecessor full-native evidence remains separately recorded.

Candidate state price is measured. Mutation/lifetime coverage is mapped, with specific missing hooks/read APIs/liveness fixtures; it is not closed. Current numerical service is **falsified**, not qualified. Therefore G1 — architecture closure and G2 — executable contract remain BLOCKED on concrete dependency changes, before HITL.

Preferred next decision: an explicit existing-owner service/adoption design that can satisfy the unchanged legal boundary under the admitted workload, with production-path controlled fixtures and a justified enforced gap. Confidence is high that merely completing prefix/delta APIs or deleting the old margin cannot meet this contract. Choosing a stronger arbitration or changing interval/adoption semantics is a design/authority decision, not an incidental Apply edit. This packet does not approve such a change or assert it is achievable with current bounds.

Closeout records pricing/audit/feasibility evidence in source-progress/contract/task/runtime handoff without any parent checkoff. No DEC append: no new architecture decision or policy was adopted; the proposed mechanism remains a proposal. No HITL required yet because a genuine host-visible contract blocker prevents readiness—not because hardware gates are waived.

Final document checks: strict OpenSpec validation valid; local links resolve; documented candidate record exactly matches compiled scratch field order; task checkboxes unchanged29/60; diff hygiene clean. Runtime state/scope, task references and latest iterative handoff updated. No passing composed service bound or complete mutation enforcement is claimed.
