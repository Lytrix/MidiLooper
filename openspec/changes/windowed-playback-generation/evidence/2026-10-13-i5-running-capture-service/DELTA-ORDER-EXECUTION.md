# I5 — bounded private delta ordering execution

User approved firmware/native implementation toward running-Capture readiness through existing owners, consecutively where dependency-ready. Default rollingOFF and target continuation remain disabled; no upload/HITL/SD or installed dependency edits. Branch feature/unified-rolling-playback-path @26125ceb plus preserved dirty tree.

Acceptance criterion: source-qualified complete running-Capture successor by its legal adoption boundary, with exact MIDI/ledger and independent physical/input-clock health. This slice closes only its specified private delta-order prerequisite, not the28tick witness or parent tasks.

Owner/primary files: existing RuntimePlaybackOrderBuilder; header/implementation and existing test_playback_prewarm suite. Reuse [source-progress protocol](SOURCE-PROGRESS.md#bounded-ordering-with-existing-arrays), existing comparator, fixed order/phases/mergeScratch arrays. No new store, view, publisher, manager or resident queue. No Track/Loop source receipt claim.

Preconditions: completed raw FixedPlaybackEventStorage builder, unchanged prefix bytes/retention/projection/provenance and storage lifetime; caller certifies those conditions. Only appended private rows. Published RuntimePlaybackView/borrowed indexes cannot extend. Extension does not imply content currency.

Mutation: freeze suffix extent; fill/sort only suffix order entries, then merge prefix+suffix into existing scratch and copy back within the supplied entry quantum. Later append waves remain outside the frozen extent until a subsequent extension. Zero delta and entirely filtered delta do no prefix index work. Refused extension leaves completed/in-progress state intact. Shrink below frozen extent denies before reads. Legacy begin/ordering behavior stays compatible.

Oracle: independently enumerated index order for lower-tick delta, same-phase Off/On, wrapend/zero, equal-key provenance, filtered gaps and multiple append waves. Prefix phases/order sentinels prove no prefix fill/sort replay; step1 snapshots bound array writes. Explicit refusal/current-ready immutability and fixed-storage capacity checks. Missing new API is not a behavioral RED; removal control must falsify prefix/no-dropped-delta expectations.

Validation: focused playback prewarm during development; actual target/native ABI measurement and defaultcapture build/no pin rebase; one focused implementation review; fullnative at closeout plus strictspec/link/diff/hashes. No measured service budget/readiness or hardware claim. Source receipt/mutator coverage, live running request/initial anchor, source-stop closure, physical provenance and complete enforced owner costs remain required before Track live reuse/target integration.

Completion: **bounded private delta-order prerequisite implemented/native verified**. Running-Capture readiness and all parent tasks remain open. No source-currency, publication or physical-time claim.

## Implemented behavior and verification

`extendPrivateOrder` accepts only completed raw fixed-storage attempts with existing phase/order/scratch arrays and capacity<=UINT16_MAX. It preserves the prefix, freezes the suffix source extent, fills/sorts only retained suffix indexes, normalizes the suffix, then merges/copies back the whole candidate within the entry quantum. Entirely filtered suffixes charge inspected rows but avoid prefix index work; later append waves wait for another extension. Typed view and borrowed-index attempts refuse unchanged. Raw storage itself carries no publication role: caller-certified private ownership/prefix/lifetime remains mandatory; this API cannot certify a raw alias to a published view. Source shrink below frozen extent denies before reads.

`extensionWorkEntriesUpperBound` saturates suffix fill/sort plus full prefix-delta merge/copy-back; zero delta costs0. It is an operation-count bound, not a physical grain or composed service deadline. No allocation, array/vector addition, Loop/Track fields, Track/reader publication or source receipt introduced.

- Four registered cases in test_playback_prewarm: exact7/9 one-entry-turn append waves; wrap/same-phase Off-before-On + one index-write per turn; filtered suffix/refusal/shrink/view/borrow boundaries; independent order/bound matrix270 combinations (three modes × five prefixes × six deltas × three quanta), including later waves, provenance ties and saturation.
- Focused prewarm **218/218 passed**. First matrix run had one Unity zero-length array comparison failure; kept count assertion and skipped only the empty array comparison, then all cases passed. Missing new API was not a behavioralRED. No expectation/bound was weakened.
- Removal of prefix merge transition caused **3 specific failures/215 passes**, plus harnessERRORED/exit1: premature Complete,14-vs6-turn bound and independently wrong first index. Restored source/header byte-identically before final checks (`i5-delta-order-precontrol.sha256`).
- One focused implementation review found no concrete defects; no repeat review.
- Final focused prewarm/ordinaryinput/checkpoint command: **245/245 passed**. `pio test -e native`: **3138total/3131passed/7skipped/0fail**,116 executed suites,252.138s.
- Default `pio run -e teensy41-capture-serial`: **SUCCESS**,46.870s. RAM1 variables96192/code392216/padding1000/locals34880 unchanged; floor840/margin160. RAM2 variables163360/EXTRAM11680 unchanged. No upload/device action.
- Actual target ABI order builder72B/alignment4/runtime7632B; native136B/alignment8/runtime9768B. New compact frontiers occupy existing layout padding; pins unchanged, no rebase. Compiler probes include actual headers (logs `i5-delta-order-price-{arm,native}.log` in scratch).

## Acceptance disposition and genuine remaining integration decision

Closed: reusable bounded private suffix order and prefix merge/copy-back on existing arrays. **Not closed:** complete source-qualified successor by adoption768 after acceptedchange740, exact production MIDI/ledger and independent physical/input-clock health.

The next live-source integration cannot treat the existing Capture content generation as an append-stability receipt. SOURCE-PROGRESS.md still requires exhaustive mutation/transfer/read-triggered-sort invalidation, pool lifetime validation, growing request/initial anchor, distinct-coverage coalescing and stop projection/closure equivalence. This primitive deliberately does not bypass those preconditions or mutate typed views.

A genuine target timing integration decision remains: USB-device, USB-host and DIN stock receive queues retain no arrival timestamps. Installed dependency edits are prohibited in the approved scope. A supported receive observer or explicitly selected project-owned maintained transport hook/adapter/fork, with generation-qualified receipt lifetime/resource and bounded correlation error, is needed before asserting all-route physical provenance. No such integration has been accepted; handler/dequeue timestamps cannot substitute. The independent counter reservation/read semantics and complete enforced owner/return costs also remain unqualified. See [physical-reference evidence](PHYSICAL-REFERENCE-CONTRACT.md), not a new planning layer.

DEC-124 — bounded private append ordering records this existing-owner implementation extension. Runtime/task/design/delivery/latest existing handoff and playback-order evaluation note updated; no parent checkoff, defaultrollingOFF/targetcontinuation change, SD/HITL/Git or installeddependency edits. Logs in scratch: i5-delta-order-first/matrix/matrix-fixed/merge-control/final-focused/final-native/final-build and price/precontrol receipts.

Final closeout checks: native-qualified real-input continuation16/16PASS after the order change; strict OpenSpec valid; diff hygiene clean after removing two Markdown trailing-space line breaks from DEC-124 — bounded private append ordering;12 local execution/status links resolve;116/116 executed native suites PASSED; parentcount29/60 unchanged. Order/header restored-source hashes and main/ClockManager/passivehelper precontrol receipts all pass. No code edits after final native/default build; later edits are closeout prose only.
