# I4 — bounded save serialization optimization

Owner requested implementing the three traced reductions. Branch feature/unified-rolling-playback-path @26125ceb + preserved dirty work. No device/Save/reset/upload/load/clear/destructive SD/Git action in this slice. Installed HEX8ed5a9dc and committed test checkpoint S0001_v0001 untouched. Trace: docs/Runtime/I4_SLOW_SAVE_TRACE.md.

## G0 — implementation evidence

VERIFIED: StorageManager owns revision and lower-level writes; main only offers a Persistence turn. Snapshot/layout currently calls full measureLoopSlotFileBytes; body write gathers published LSV1 tail again. Tail grain scans one pitch/bar membership, including null buckets, maxGrains1. Write byte slice replays prefix from0. Existing payloadSubphase/emitIndex/nestedIndex/nestedRefIndex fields are unused by old byte-slice path. Existing monolithic serializer and production SD fixture provide independent exact bytes/readback/identity oracles. Private temp chunk/header and final SlotIndex are patched/serialized before footer CRC and final validate/catalog publication.

Ownership/lifetime/state: preserve existing storageSession, per-loop shared PSRAM scratch, published immutable refs, Snapshot→Write→Validate→CatalogUpdate→Complete and failure reset. No new retained root, queue, cache, runtime role or format. Workspace saves and revisions share optimized tail path; monolithic external/native serializer remains compatibility oracle. Undo/capture/published roots are read only. No source-admission relaxation.

Bound class: existing empirical persistence row14, budgets between steps. Revised structural grain cap: ≤16 membership lookups and ≤one populated membership page per call, or ≤512-byte forward serializer work/output. Cap16 reuses small fixed-unit batch scale already used by save ordering (not a time-budget relaxation). maxGrains1 and worker/main yield remain. Prefix replay removed; no loop-length-sized skip permitted in one grain. Worst per-grain hardware duration still UNKNOWN; no latency guarantee/speedup claimed before hardware measurement.

Hidden dependencies resolved: private revision chunk needs actual length only after body writes; final CRC reads bytes from file, so patch before CRC and SlotIndex is safe without altering CRC convention. Patch seeks/open failures must fail closed and never publish. Writer must handle scalar records/ID arrays split at512-byte boundaries without advancing committed cursor on I/O failure. Tests must cover reused shared fixture includes and CRC/readback.

G0 verdict PASS. Existing-owner extension, no reassessment trigger/new unbounded operation. Relevant DEC-027 — deferred scheduler north star / DEC-043 — execution-bound property searched; use existing classified grain, update ExecutionBounds same slice. No new scheduling framework.

## G2 — contract before edits

Primary files: src/StorageLoopIoBodyFormatV1.cpp, include/StorageLoopIo.h, src/StorageManager/RevisionCommit.cpp, existing test_storage_loop_io and test_storage_drain_restore; native-only measurement instrumentation may live in src/StorageLoopIo.cpp. No concurrent edits to same path.

1. Gather grain: walk at most16 note/event membership lookups, stop after processing one populated page; preserve canonical pitch/bar ordering and paged references. Empty membership consumes bounded metadata work, not a whole owner opportunity each. Test actual visit cap and total service reduction; exact monolithic bytes remain equal.
2. Forward payload: serialize root/count/notes/events/pitch+bar/ref arrays/eventbar+refs via existing scalar scratch cursor; local fixed512-byte buffer only. Prepare on local cursor copy, commit cursor+byteOffset only after full sink success. Each span/transition has bounded forward progress; no full-prefix rescans, no regather, no new resident buffer. Native-only span counter discriminates old replay from new traversal; tests field/array splits/failure and tail reset.
3. Revision sizing: layout records included identity/metadata with private placeholder bodyLength0, without calling full measurement. Write actual body once; derive length from tracked/file offsets, patch private LoopSlot chunk header non-truncating, restore append position. Final directory uses observed length, footer CRC/validate use complete patched bytes. Keep existing slot inclusion predicate and published IDs/current source guards. New request retries start clean; failed patch must not publish. Existing completed-input correctness preserved; no concurrent source contract changed.

Acceptance: byte equality against unchanged monolithic writer on mixed/dense/multibar data; native gather≤16lookups/page≤1/output≤512; forward work independent of previous emitted offset; zero revision measurement calls; valid exact chunk sizes/offsets/readback/streamed CRC/preserved Current/commit→load; failures retain logical cursor or fail closed with no catalog/provenance publication. Full native/defaultcapturebuild/RAM1padding≥840/one bounded static review/strictOpenSpec/diff. Existing registered STIM-I4-CHECKPOINT-01@v1 recipe reused later after upload approval, no new marker/stimulus. Final seconds-scale acceptance remains hardware-only/unmeasured.

## Results

Implemented all three reductions locally; **NOT UPLOADED**. Existing scratch fields reused for forward progress; local512-byte slice/header storage, no new resident state/cache. Revision lengths derive from actual body bytes, private header patch precedes final directory/footerCRC. DEC-118 — bounded forward save serialization and ExecutionBounds row14 record metadata lookup cap16/page1/maxGrains1; main cadence/time budgets unchanged.

Regression/evidence sequence:
- Parent native-only counter seams expose membership lookups/populated pages/monolithic payload calls; target build contains no counters. Before behavior edits, sparse24barfixture required3102turns (cap220 assertionRED), resumedwrite invoked monolithicpayloadonce (expected0RED);52otherspassed, harness additionallySIGINT/ERRORED. No missing-module RED mislabel.
- Independent bounded sizing slice (fixer9f40d167) modified only RevisionCommit/StorageLoopIo/native storage-drain fixture, no concurrent path edits. Real Snapshot zero-measurement assertionRED Expected0Was2; delegate focused19/19GREEN. Parent inspected/converged code and ran combined tests; no recursive delegation.
- Final sparse24barfixture: **199turns vs3102**, exact monolithic bytes; every step≤16queries/≤1populatedpage/≤512bytes. Dense256same-pitch paged references/ID-array chunks, mixed/scalar record splits, sharedscratch restart/maxGrains2 refusal, failurecursor and exactretry outputs covered. Resumed payload slices invoke no monolithic serializer.
- Real commit→load fixture requires zero fullmeasurecalls, occupied placeholders retained, actual chunk lengths/SlotIndex offsets matching monolithic body bytes, streamedfooterCRC, exactS1/v1 identity, Current payload unchanged. Concrete partialpatch failure before catalog/provenance publication preserves source/epoch and fresh retry completes.
- Final focused native storage-loop/drain/set-revision suites **148/148**. Final `pio test -e native`: **3081total /3074passed /7skipped /0failed**, exit0,247.655s. Subsequent tail-only pio suite and direct native binary55/55 verify sparse199count. Host checkpoint21/21 pass. No device test or speedup seconds claimed.
- Final `pio run -e teensy41-capture-serial`: SUCCESS. RAM1variables96,192 /ITCM393,216 =code392,056+padding1,160 (unchanged floor840;320Bmargin), locals34,880; RAM2variables163,360; EXTRAM11,680 unchanged. No floor rebase. HEXSHA256 **cdfe4b517563df6c5fa077f63aa59f7e3459a7feade8846c66f3ff5461ab489d**, candidate only. Installed8ed5a9dc and existingS0001_v0001 untouched.
- One focused independent static review: **no concrete findings**, no fix requested. Reviewer did not execute tests/build and no second review round claimed.
- Diff check initially found2Markdown hard-break trailing spaces in new decision; corrected and finalclean. Strict set-revision change valid. Unrelated dirty work preserved; no Git delivery.

Scratch logs: i4-serializer-red.txt, i4-serializer-green1.txt, i4-serializer-focused.txt, i4-serializer-native-full.txt, i4-serializer-build-final.txt, i4-serializer-native-work-count.txt, i4-serializer-host.txt. Pre/post-status captured for user-change preservation. Native work reduction is a structural result on fixtures, **not elapsed hardware improvement**. Existing monotonic epoch/source admission/immutable data/CRC convention/format/owner guards remain; general monolithic serializer and measurement compatibility API remain unchanged outside revision layout.

Closeout: runtime/guide/delivery/tasks updated, DEC-118 — bounded forward save serialization and execution inventory amended. Optional hardware gate remains: separate upload approval, canonical two-drain preflight, pinned candidate upload, reused registered checkpoint leg/newdir/current OLED witness and sameworkspace workload latency/parity. No threshold increase or release of requested structural caps; no new instrumentation, device/Save/reset/load/clear or SD operation in this implementation slice.

Final closeout checks: strictset-revisionvalidation/diffhygiene pass; registeredSTIM-I4-CHECKPOINT-01@v1 still present; candidatecdfe4b51 hashunchanged. Before/afterstatus compared: only intended optimizer source/header/tests/authority/runtime/task/doc additions, unrelateddirtyentries retained. Existing capture recipe records candidate-versus-installed distinction; no hardware qualification performed.

## Subsequent approved hardware verification

Ownerapprovedcdfe4b51 upload and registeredcheckpoint03 now qualify exactS0001_v0002,
live+drainedoracle/two-drainsPASS/no latch. Ordered41tail shapes/152bars/256129bytes
matchreference02; measuredSave14.862218svs44.656534s,3.0047×faster/66.7188%less.
Ownerconfirmedresponsivevisiblecompletion andacceptablewait forworkload. No new
source/budget/stimulus change; no genericdeadline/fullSD-MIDIbyteparity/widerI4claim.
[I4 — optimized hardware evidence](OPTIMIZED-HARDWARE.md).
