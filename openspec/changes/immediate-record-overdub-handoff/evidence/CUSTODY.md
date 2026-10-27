# P1 — custody ABI, reserves and parametric ledger

Task1.4 delivers measured host/shipping-ARM layout and real native allocator transitions. This is **not a supported-capacity or firmware-readiness proof**. No project firmware source, installed dependency, allocator policy, work quantum, device or SD content changed. Baseline `faa089b8365720972f8535decd747180a963723f` plus prior dirty source.

## Reproducible probes

Preserved [size symbols](probes/custody-size.cpp), [native allocator assertions](probes/custody-allocator.cpp), [runner](probes/run-custody-probes.py) and [shipping flag snapshot](probes/shipping-flags.json). Run from the project:

```sh
python3 openspec/changes/immediate-record-overdub-handoff/evidence/probes/run-custody-probes.py \
  --out-dir "$PI_SCRATCH_DIR/custody-repro"
```

Runner uses installed Arm GNU11.3.Rel1/shipping Cortex-M7 hard-float defines/includes, not native target emulation. `-fno-access-control` is observation-only for private sizeof. Host uses actual native definitions; target does not define PIO_UNIT_TEST_NATIVE. Objects are artificial sizeof arrays, not firmware growth. Output is scratch-only. The flag snapshot came from `pio run -e teensy41-capture-serial -t envdump`,600MHz; ENV was redacted, not preserved/shared. Refresh snapshot deliberately if source/build configuration changes. Runtime probes execute the production storage/allocator implementation, not copied domain logic; target allocator execution/fragmentation is not measured.

Original probe exact expanded argv/version/source SHA manifest and full report are in session scratch: `custody-{arm,host}-command.txt`, `custody-{arm,host}-version.txt`, `custody-source-hashes.json`, `CUSTODY-ABI-RESERVE-REPORT.md`. Preserved copies were separately recompiled host+ARM successfully; native allocator assertions reran successfully. They are readable evidence tools, not added production resources.

## Actual inline layouts

| Type | Native bytes | Shipping ARM bytes |
|---|---:|---:|
| MidiEvent |32|20|
| EventChunk |8280|5208|
| LoopEventStore |88|64|
| CaptureRange |112|80|
| ChunkReservation |1040|1040|
| CaptureStopPresentation |160|120|
| Track CaptureStopJob |1032|720|
| CaptureStopJobs |42016|29704|
| Interaction latch |32|32|
| Track |328264|384|
| Loop |1600|1008|
| LoopPlaybackRuntime |9896|7768|
| RuntimePlaybackViewSet |39056|33248|
| CaptureAdmissionStaging |4192|2612|
| Record preparation role |36408|25736|
| Record preparation workspace |32768|24576|
| Record preparation cursor |3600|1120|
| Source staging cursor |112|64|
| UndoEntry |200|132|
| TrackUndoPublication |984|596|
| PendingNote |1072|1056|
| Candidate custody box |168|128|

Native Track contains its test MIDI sink and is NOT a target price. Candidate = actual presentation plus two32-bit obligation/state words, not a finished protocol. Persistence QueueState was separately measured at4616bytes host/ARM; it is global once, not per interval. The old header comment calling a presentation80bytes is stale:80 is CaptureRange, current presentation120. No unrelated comment/source cleanup done.

## Directory, tail and credit accounting

- Each armed capture producer/range also prewarms at least512 uint16 IDs: **1,024 requested heap bytes**, beyond inline sizeof. Actual InternalHeapFirstAllocator can fall back external then abort, so bool reserve is not blanket failure-safety. Target headers/alignment/fragmentation are not priced by sizeof. Native malloc usable size measured1024.
- Each free armed range owns one real empty pool chunk: **5,208 logical target bytes**. Producer owns its live tail. Seal swaps existing directories/bodies: the destination spare becomes successor producer tail. Do NOT charge an extra spare tail to an already sealed interval.
- ChunkReservation includes its1024-byte ID array inline in1040bytes. Available credits own real chunks; consumed output chunks and remaining credits are disjoint.
- Pool backs64chunks per block:333,312target bytes/block, all8blocks2,666,496bytes. Opened blocks remain backed after release. Budget uses physical backed high-water, not body bytes plus the same blocks again. Global used/lifecycle/ref arrays2048bytes are once, not per interval.
- CaptureRange release invalidates logical reads/generation on the first positive release grain, not at final physical release. A pinned chunk ref does not keep that logical range readable. No range release begins before all logical consumers detach.

### Asserted storage trace

Producer+3armed destinations occupy4chunks. Seal A257rows then B1/C1 leaves **5chunks**:A2+B1+C1+live1; directory pointers exchange without body copy. First A release rejects old generation; complete release retains directory; rearm adds a tail. Eight canonical credits are8real occupied chunks. Extra immutable physical reference keeps a body after raw-owner release. Failed reservation growth preserves508existing credits and rolls back new acquisitions. After pressure release, all8opened blocks remain backed. These are storage primitive assertions, NOT Track early-Stop or private-source proof.

## Candidate price and whole-setup ledger

For each active Track: `N` distinct sealed intervals still requiring canonical work; `R` **additional distinct** post-job presented/retiring sources; `K` cold-admitted stable boxes; `F=K-N-R` free armed boxes; one live producer and one serialized canonical worker. The same source with job+reader obligations is charged once, not in both N and R.

Candidate requested metadata/directories = `K*(128+M+1024) +1024 producer directory +1024 canonical-output directory`, plus producer inline64 if not already included, slab/allocator overhead and unknown exact receipt fields `M`. Existing canonical Capture80 and ChunkReservation1040 are already inside runtime7768. Existing ring29704 includes jobs/presentations/worker; never sum its children again. If boxes are additive, charge them additively; do not assume old presentation storage can be subtracted until replacement indexes/lifetimes are designed.

Necessary global chunk condition:

```text
unique committed/retained chunks
+ sum_tracks(distinct N/R raw chunks + live chunks + F spare tails
             + canonical output chunks + unconsumed canonical credits)
+ other pool owners + recovery reserve <=512
```

Raw→canonical is a genuinely different body while both survive: charge both. Undo/persistence refs to the same physical body do not add another body. Full output plus remaining credits is not always8: long Record grows output while replenishment tops up credits toward8. Possible synthetic closures add work/body capacity; do not assume zero or filtering savings.

A+B+C is N3, not a proved cap3. K3/F0 can demonstrate three seals but does not prepay another Stop. A further distinct retiring reader consumes another box/body; a reader of A while A's job is live consumes no second A. Pool512 is global, not per Track. Eight-track symbolic K3 adds24*128=3072inline box bytes plus24576directory requested bytes, BEFORE tails/bodies/receipts/overlap. This is not support for simultaneous Record on8tracks or arbitrary density.

## Baseline reserves beyond a box

One2048-row event storage allocates arrays `(sizeof(MidiEvent)+9)*2048` = **59,392target bytes**. Three windows+two Capture staging stores:296,960target dynamic bytes, plus33,248view-set shell. Another existing-style stage costs **62,004target bytes**, not80 or128. A sealed box does not magically provide independent B/C closure/order staging.

Optional whole-take bridge/base arrays add487,424target requested bytes; actual native prewarm/upgrade probe confirms634,880host. This is state-dependent existing baseline, not a proposed custody increment. Record preparation union scratch25,736target is already in ring29,704 and stays serialized; do not multiply by N. Presentation retirement constructs a720-byte target job on stack; call-frame/stack high-water remains unmeasured.

Current4job undo-ID list reserves add2048requested bytes/ring. UndoEntry132 and root refs are only wrappers; nested maps/vectors and retained graph pages/slabs remain distinct. Snapshot slab quantum32KiB does not price graph occupancy.

## Unresolved terms and recommendation

Exact per-interval closure/pending-transform/preview/source receipts; independent frozen staging strategy; root/current/build/retained graph overlap; causal successor undo order; visual reconstruction buffers/retirement; checkpoint/SD buffers; optional per-store bar indexes; allocator overhead/fragmentation; RAM1 stack high-water; recovery reserve; retirement/rearm/service gaps. These are unknown terms, **not zero**. Candidate masks are not a proved reference protocol; generalized generation/ABA behavior must be explicit beyond current32-bit presentation generations.

Preferred existing-owner direction: externally allocated stable Track custody slab, job/presentation/source obligations detached by generation independently, one canonical worker, release only after the final logical consumer, bounded retirement then rearm. Confidence in direction high; final layout/cap/response envelope unproved. Task1.4 measurement can close; resource readiness and production packet generation remain BLOCKED until the unresolved terms are concretely contracted/qualified.
