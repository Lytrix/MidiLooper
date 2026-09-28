# S4f.18 — bounded private canonical tick ordering

Status: verified stable-ordering implementation; Record integration remains open. Baseline 7c163335. Unrelated working changes preserved.
Authority: shared capture finalization contract; DEC-093 — shared capture finalization;
DEC-043 — execution bounds. Existing owner: LoopEventStore.

## G0 and G1

VERIFIED: capture's legacy tick sort is stable; minimum-length filtering later uses
Off/On/other ordering and remains a separate stage. Raw CaptureRange hides its
store and permits only immutable reads. appendReserved produces dense chunks.
Ephemeral seal already supports private derived/preparation output without queue
admission. PersistenceQueue exposes NotScheduled/Queued/Writing/Persisted; runtime
chunk reference counts expose sharing. The current canonical-copy path still
serves overdub staging and is not switched here.

Extend LoopEventStore with a borrowed stable tick-sort cursor over private dense
prepared chunks. No whole-take event/index array, extra pass, directory allocation
or raw-range mutation. Bottom-up runs use divide-and-rotate stable merge: binary
cuts partition a merge, three reversals rotate the middle ranges, and a fixed
stack resumes the two smaller merges. Lower/upper-bound asymmetry preserves equal
ticks. Each step consumes Q units, each a constant-work state transition, binary
comparison, swap, or chunk metadata visit. This yields O(N log² N) moves/comparisons
in the worst case, not insertion-sort quadratic work. Fixed stack capacity is twice
size_t's bit width; it bounds divide recursion independently of take length.

Admission is O(1); a bounded preflight visits every chunk before the first mutation.
Each must be exclusively referenced and NotScheduled, and all except the final
chunk must be full. Captured owner generation, pool lifetime, row count and directory
size must remain unchanged. Every accessed chunk is checked again before reading,
swapping or refreshing metadata. External alias/persistence admission denies the
next access; no rollback or publication of partial order is promised. Caller owns
the exclusive preparation role and must not expose it until Complete. Discard is
constant-work cursor retirement, not recovery of the original row order.

No new manager, scheduler, allocation, store field or publication route. Cursor
belongs to existing external preparation role; measure ARM size before integration.
No source sorting occurs on stop/launch. API accepts only tick ordering; minimum
length tie policy and canonical compaction remain separate gates.

## G2 fixed oracle and review

G0 PASS; G1 existing-owner extension; G2 PASS WITH TIGHTENING: this API is only for
private unpublished preparation, never a published source or live capture producer.
Caller must invalidate/retire the cursor before replacing its role and preserve
raw input until source borrowers release it. No source fallback on denial.

- Feed private dense chunks in reverse, alternating, equal-tick and deterministic
  random order; exact sequence must equal independent std::stable_sort, including
  channel/pitch/velocity/NoteId. Exercise empty, singleton, 255/256/257 and >2,048 rows.
- Each step reports <=Q units; Q=0 denies without mutation. No allocations, chunk
  reference changes, directory moves or capacity changes during sorting.
- Raw frozen source remains readable byte-for-byte while separate prepared output
  is sorted. Pool reset/clear/append and newly queued persistence deny stale work.
- Normal queued and SD-loaded persisted chunks deny during preflight before any
  row changes. Shared refs deny. Denied/partial results are not Complete.
- Fixed watchdog scales conservatively with N and squared log2(N); compare actual
  unit counts and exact contents rather than elapsed host timing.
- Mutate equal-tick cut direction and generation checks; tests must discriminate.

No timing/deadline/HITL claim. Full Record worker integration, first-bar/onward
playback and resource admission remain open. Required checks: focused owner tests,
full native, capture-serial build/RAM1 floor >=840, ARM layout, strict OpenSpec and
diff checks. Existing integration stimulus remains STIM-BASE-2-ONE-OD-01@v1.

## Focused implementation evidence

Six owner fixtures pass (`/tmp/order-focused.log`). Inputs cover reverse,
alternating, equal and deterministic random ticks at 0/1/2/255/256/257/2,056 rows,
with exact stable NoteId/channel/pitch/velocity sequence. Q=1 and Q=7 enforce work
accounting; directory capacity, IDs, references and pool usage remain unchanged.
Real queue states Queued/Writing/Persisted are denied. A narrow native-only
resource-boundary hook retains a chunk reference to simulate an immutable borrower;
the sorter and asserted reference check are production code. `cloneShared` was
verified to deep-copy rather than retain refs, so it cannot supply this fixture.

Empty input originally required pool initialization despite having no resources;
binding now accepts the empty directory without allocation. Same-shape owner and
pool replacement fixtures discriminate the two identities independently. Frozen
raw input survives sorting separate prepared output byte-for-byte in event fields.
Four mutations fail assertions: equal-tick lower-bound direction, owner generation,
pool lifetime and persistence guard. Source restored before full checks. Logs:
`/tmp/order-mutation-{equal_cut,generation,pool,persistence}.log`.

ARM measurement: cursor **864 bytes**, store **56 bytes** (no store fields added).
No cursor is resident in a production role yet. Integration must budget/prewarm
that role, keep ephemeral-seal scope inside each append turn and explicitly admit
canonical publication/persistence after ordering; already-ephemeral sealed chunks
are not automatically re-admitted by the sorter. No admission/deadline/device claim.
No additional decision/exclusion; this implements the existing owner contract.

## Dependency-closure correction and final focused check

The first firmware build exposed a missed transitive cost: the previously unused
`PersistenceQueue::chunkState` accessor added 64 ITCM bytes (padding 776) and called
the lazy queue allocator. The fixed no-allocation oracle was retained. The accessor
now reads the same process-lifetime owner pointer without creating state, and is
FLASH placed. Allocation still belongs to existing queue prewarm/write operations;
an absent queue still reports NotScheduled. No queue ownership/lifetime changes.
ARM disassembly is loads/comparisons/returns only, with no allocator or other call.
The sorter and both chunks of every swap remain protected by this read-only query.

The first full native run passed **2,549 / 7 skipped** before this correction.
After it, **15/15** ordering + persistence fixtures pass, including strengthened
mixed NoteOn/NoteOff/CC equal-tick order and every chunk's refreshed first/last tick.
`/tmp/order-refined-focused.log`. Capture-serial rebuild passes with RAM1 padding
restored to **840** (`/tmp/order-final-build-2.log`); no budget floor was weakened.
Final full native is rerun after the accessor change. Initial failed build evidence:
`/tmp/order-final-build.log`. No firmware upload or device claim.

## Final verification and closeout

- Final full native: **2,549 passed / 7 skipped**, 2,556 total, after the query
  correction and strengthened mixed-event/metadata assertions. `pio test -e native`,
  `/tmp/order-final-native-2.log`.
- Capture-serial build PASS: FLASH code **1,047,456**; RAM1 variables **96,160** /
  code **392,376** / padding **840** / locals **34,912**; RAM2 **155,232**, EXTRAM
  **9,312**. `/tmp/order-final-build-2.log`. No upload.
- ARM cursor **864 bytes**, store **56 bytes**. No resident field or resource cap
  changes. Strict OpenSpec validation and whitespace checks pass.
- Runtime state/current work, execution bounds, storage guide and task evidence
  updated. Pure implementation of existing owners/DEC-043/DEC-093; no new decision
  or shipped deliverable. Parent remains open, **50/176** complete. Filter compaction,
  prepared-output persistence/publication and shared Record routing remain required.
