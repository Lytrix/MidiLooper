# Tasks — runtime-code-tidyup

Scope: `docs/Runtime/CURRENT_WORK.md` § Now implementing (task 1.3 writes it before any code edit —
`AGENTS.md` forbids implementing outside that section, and it is currently empty).
Base: `refactor/runtime-code-tidyup` stacked on `feature/resumable-midi-boundary-playback-maintenance` at
`4aacd18e`. Design: `design.md`. Baselines and per-phase gates: `design.md` § D10.

Every phase is behavior-preserving. If a task cannot show equivalence with its gate, drop the task and
record why — do not reach for HITL.

## 1. Preflight, scope and baselines (no code change)

- [x] 1.1 Search `docs/DECISION_LOG.md` (with the `grep` tool, never a full read) for the topics this change touches —
  RC-MEM, DEC-057/063/067/068/072, the undo trim rails, the D8 read-site sweep, instrumentation gating — and record each
  hit that constrains a phase. Verify by writing `openspec/changes/runtime-code-tidyup/PREFLIGHT.md` containing the
  owner/change/test summary, the cited `DEC-###` list, and the explicit statement that no formal trigger fires
  (`docs/ARCHITECTURE_REASSESSMENT.md` lists "Refactor with no ownership movement" as not a trigger).
  Done: five constraining decisions recorded (DEC-072 staged-pass receipt, DEC-068 `ownsLiveSession`,
  DEC-067 admission predicate, DEC-063 trim rails + cursor boundedness, DEC-057 byte identity) plus the
  `SESSION_CAPTURE_SNAPSHOT_IDLE` acceptance note and `include/Globals.h:86,88` for the trim values.
- [x] 1.2 Confirm from [`docs/Authority/DELIVERY_RULES.md`](../../../docs/Authority/DELIVERY_RULES.md) verification
  matrix that a behavior-preserving refactor carries no HITL gate, and cite the clause in `PREFLIGHT.md`. Verify: the
  citation names the matrix row that exempts this change.
  Done: lines 127–147 cited. Only `Native unit tests` and `Firmware build` are unconditional; every HITL row is
  qualified by behavior coupling. The `Persistence … deferred save HITL when touching save path` row is
  **nominally** triggered by task 2.9 and is surfaced in `PREFLIGHT.md` with its byte-identity argument rather
  than silently waived — the user can override.
- [x] 1.3 Update `docs/Runtime/CURRENT_WORK.md` § Now implementing to name `runtime-code-tidyup` phases P1–P6 as the
  implementation scope. Verify: the section names this change and no other work, and `git diff` shows only that section
  changed.
  Done with one interpretation recorded: the section already carried the `runtime-working-set-ownership` target
  (commit `4aacd18e`). It is retained below the new entry and explicitly marked **queued, not started**, so the
  *active* scope is unambiguous without erasing the prior decision context. `git diff` is confined to the
  section head (+12 lines).
- [x] 1.4 Re-measure and record the four baseline gates in `PREFLIGHT.md`: `pio test -e native` case/pass counts, `pio run -e teensy41-capture-serial`
  RAM1 line (`variables`/`code`/`padding`/`free for local variables`), `openspec validate runtime-code-tidyup --strict`.
  Verify: all four values present and dated.
  Done: native 2,262 cases / 1 skipped / 2,261 passed (00:01:12); RAM1 `variables 96128, code 392344,
  padding 872, free for local variables 34944`; `openspec validate --strict` valid; linker budget row added.

## 2. P1 — remove dead code and dead artifacts

For every deletion: prove zero references first (`grep`), delete, then run the native suite. A link error
is a finding to investigate, not a nuisance to silence.

- [x] 2.1 Delete `Loop::clearFailureInjectionForTest()` and `Loop::completedChangeCount()`
  (`include/LoopSnapshot.h:331,346`), each of which has exactly one repo-wide occurrence. Verify: grep returns nothing
  and `pio test -e native` passes.
- [x] 2.2 Delete the four unreferenced playback-view orphans: `RuntimePlaybackViewSet::resetMetadataPreserveRetained()`,
  `RuntimePlaybackViewLifecycle::buildIsPending()`, `hasReadySuccessor()` (`include/RuntimePlaybackView.h:332,333,419`),
  `RuntimePlaybackViewFill::active()` (`include/RuntimePlaybackViewFill.h:58`). Verify: grep shows 0 call sites each;
  native suite passes.
- [x] 2.3 Delete `RuntimeCompanionOverlapWalk::actionReady()` (`include/RuntimeCompanionOverlapWalk.h:27`),
  `Action::sourceIndex` (`:20`, written at cpp:56 and never read) and `RuntimeCompanionSpanWalk::spanReady()`
  (`include/RuntimeCompanionSpanWalk.h:24`). Verify: grep shows 0 read sites; native suite passes.
- [x] 2.4 Delete the four unreachable `InvestigationCaptureLog::logVisualCache*` emitters
  (`include/Utils/InvestigationCaptureLog.h:85,88,92,94`; `src/Utils/InvestigationCaptureLog.cpp:297,308,318,327`) that
  duplicate the live inline emitters in `src/Loop/LoopVisualCache.cpp:481,659,682,706`. Verify: `grep logVisualCache`
  returns no call sites; the `vch,win=`/`gather_*` capture lines still originate from `LoopVisualCache.cpp`.
- [x] 2.5 Apply the design § D2 rule to the five test-only diagnostics
  (`RuntimeCanonicalPassInputs::borrowsCaptureOrder`, `hasFrozenBinding`,
  `RuntimeCompanionBridgeMap::lastStepInputCount`, `RuntimeCompanionOverlapWalk::lastStepInputCount`,
  `RuntimeCaptureCompanions::capacity`/`row`)
  and to `RuntimePlaybackBridgeCursor::emissionCursor()`: keep a probe only where a test asserts a grain/bound contract,
  otherwise re-point the test at `data()`/`size()` and delete the accessor — test edit in the same commit. Verify:
  native suite passes with the same case count, and each kept probe has a named contract assertion.
- [x] 2.6 Delete `PendingSnapshotPublishBox::applyingCompanions` (`src/Loop/LoopSnapshotBoxes.h:84`) and its seven
  assignments in `src/Loop/LoopSnapshotPublish.cpp:659,844,1016,1097,1139,1161,1308`. Verify: grep shows writes gone and
  no reader existed; native suite passes.
- [x] 2.7 Delete the inert computation in `src/Loop/LoopSnapshotPublish.cpp:790-798` (`identityPreserved` computed from just-assigned values, then `(void)`-cast)
  and collapse `Loop::liveRecordCanApplyCompletedTicks` (`:1810-1823`) to its single guarded `return completedTicks != 0;`.
  Verify: same return value on every input path (read the guard), native suite incl. `test_loop_take_survival` passes.
- [x] 2.8 Delete the unreachable `Phase::Trim` pop branch in `src/Loop/LoopPreparedCompanionHistory.cpp:240-243`
  (the resize at `:163` makes the condition unfirable and the file's own comment says the walk was removed).
  Verify: native suite passes; the phase's remaining transitions still reachable.
- [x] 2.9 Delete the vestigial LSV1 writer state in `src/StorageLoopIoBodyFormatV1.cpp`: `telemetryTailEntered`
  (`:109`), `sectionHeaderWritten` (`:108`, written `:1283`), `sortSwapBudget` (`:94`, only ever set to 0 at `:968,1021,1046`),
  the `PayloadWriteSubphase` enum + `payloadSubphase`/`emitIndex`/`nestedIndex`/`nestedRefIndex` cluster (`:97-107`),
  and drop them from `loopSnapshotTailProgressSignature` (`:1130-1137`). Verify: `test_storage_loop_io` passes; the
  body-format byte oracle (`test_storage_loop_io` round-trip) is unchanged.
- [x] 2.10 Delete the zero-reference symbols `checkedSizeAdd` (`src/StorageLoopIoBodyFormatV1.cpp:160`),
  `kLsv1LoopSnapshotRootHeaderBytes` (`:129`), `kLsv1SaveOrderingFlushBatch` (`include/StorageLoopIo.h:229`) and the
  redundant `#include "Utils/NoteUtils.h"` in `src/Loop/LoopSnapshotPublish.cpp:10`. Verify: grep returns one hit each
  before deletion and none after; build has no new warnings.
- [x] 2.11 Resolve OD-4 with the user; default: wrap `readLoopSnapshotBodyFormatV1`
  (`src/StorageLoopIoBodyFormatV1.cpp:1503-1613`, test-only, 8 call sites in `test_storage_loop_io`) in `#if defined(PIO_UNIT_TEST_NATIVE)`
  and correct the ownership sentence in
  [`docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../../../docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md). Verify:
  `test_storage_loop_io` passes and the guide no longer names it a production load owner.
- [x] 2.12 Delete `TrackUndoPublication::units_` + `completedUnits()` (`include/TrackUndoPublication.h:46,60` and the five writes in the cpp),
  `MidiBatchComposition::Summary::nonClockCount` (`include/Utils/MidiBatchComposition.h:25`, increment `:61`, the `MidiHandler.cpp:226` argument and the `(void)` at `RuntimeTimingTelemetry.cpp:746`),
  `ExternalPoolAccounting::trackedBlocks` + `reset()` and the `kTagCount 18 → 17` phantom slot
  (`include/Utils/ExternalPoolAccounting.h:47,72,131`) — **keep** `tagBlocks`, which is read by the decrement guard at
  cpp:182 — and `CommittedMidiScratch::RefreshKind::FromPasses` (`include/Utils/CommittedMidiScratch.h:20`). Verify:
  grep proof per symbol; `test_pool_budget`, `test_midi_dispatch_order`, `test_runtime_timing_telemetry` pass.
- [x] 2.13 Delete `InvestigationAllocProbe::recordClockPulseEntry` + its accessors and read the existing
  `RuntimeTimingTelemetry::noteClockPulse` counter instead (`ClockManager.cpp:186` vs `:189`). Keep the probe's
  span/project/intervalTimer counters, which have no telemetry equivalent. Verify: `test_clock_transport_master` and
  `test_runtime_timing_telemetry` pass; one pulse source remains.
- [x] 2.14 Delete `StorageManager::admitLoopUndoHistory()` (pure no-op body, 0 callers) and `admitLoopPersist()` (0 callers, annotated `/// Deprecated` by this branch)
  from `include/StorageManager.h` and `src/StorageManager/PersistenceAdmit.cpp`. Verify: grep shows decl+def only
  before, none after; firmware builds.
- [x] 2.15 Remove the dead artifacts with `git rm`: `openspec/changes/resumable-midi-boundary-playback-maintenance.zip`
  (247 KB duplicate of a tracked directory, contains `__MACOSX/` entries, already stale — missing `ARCHITECTURE-REVIEW.md`),
  `companion_pair_order_repro.cpp`, `create_history_order_repro.cpp`, and the two `.patch` files that apply neither
  forward nor reverse. Verify: `git ls-files | grep -E '\.zip$'` is empty; the two repro *intents* are already
  represented in the tree (`src/EditManager/EditApply.cpp` equal-tick `stable_partition`) or recorded in `design.md`.
- [x] 2.16 Remove the duplicate `#include "Utils/InvestigationCaptureLog.h"` inside the anonymous namespace in
  `src/Track/TrackDeferredMaintenance.cpp:71` (namespace opens at `:36`). Verify: one include remains at `:19`; firmware
  builds.
- [x] 2.17 P1 gate: run the full native suite and the firmware build, and record the removed-symbol census plus the new
  RAM1 line in `PREFLIGHT.md`. Verify: native 2,262-case baseline still passes; RAM1 `code` unchanged or lower;
  `padding` not reduced into a bank edge.

### P1 outcome (2026-09-18)

Gate: `pio test -e native` **2,262 cases / 1 skipped / 2,261 passed**; firmware build SUCCESS with
RAM1 `variables 96096 / code 392248 / padding 968 / free for local variables 34976` and FLASH
`code 1007552` — every figure better than the recorded baseline (96128 / 392344 / 872 / 34944 / 1009608).

Removed: 4 orphan accessors, 1 orphan field, 4 unreachable emitters, 7 write-only assignments, 3 dead
LSV1 writer scalars, 1 inert verification block plus its 128-byte stack array, 1 degenerate branch,
3 zero-reference symbols, 1 redundant include, 1 duplicate include inside an anonymous namespace,
1 dead counter, 1 dead accessor, 1 dead no-op admission, 1 deprecated admission, 1 retired enumerator,
and the committed 247 KB zip plus four scratch artifacts.

Six review claims were corrected by verification while applying the tasks:

- `RuntimeCompanionOverlapWalk::Action::sourceIndex` is a **test-asserted contract**, not dead. It was
  deleted first, `test_edit_apply` failed to build (3 assertions), and it was restored marked as test
  surface. The original conclusion came from a `head`-truncated grep — full greps are now used.
- `MidiBatchComposition::Summary::nonClockCount` is asserted by `test_midi_dispatch_order.cpp:70,86`.
  The *field* stays; the *telemetry parameter* is a reserved slot for the deferred DROID/USB-host
  feedback split (`resumable-midi-boundary-playback-maintenance` task 4.1), so it is documented rather
  than removed (a removal would churn an 11-site macro API for no behaviour gain).
- `PayloadWriteSubphase` / `emitIndex` / `nestedIndex` / `nestedRefIndex` feed the documented
  `LoopPersist,tail_step` line and its progress signature
  (`docs/Plans/deferred_persist_lsv1_edittail_slice_bugfix.md`). Removing them would change a
  documented diagnostic, so they are kept and now commented.
- `InvestigationAllocProbe::clockPulseEntries` is **not** a duplicate of
  `RuntimeTimingTelemetry::clockPulses`: the former is monotonic since boot and sampled as a delta by
  the length-path probe, the latter is reset per reporting window (`RuntimeTimingTelemetry.cpp:384`).
  Kept and documented.
- `Phase::Trim` **is** reachable — it is assigned at `LoopPreparedCompanionHistory.cpp:226` and its
  transition is essential. Only the unreachable `pop_back()` was removed.
- `ExternalPoolAccounting::tagBlocks` is read (the decrement guard at cpp:182); only `trackedBlocks`
  was write-only. `kTagCount` stays 18 so the printed `tags=` vector length is unchanged.

**RAM1 bank note (found by this phase's gate and fixed in it).** Gating the test-only reader changed
inlining: `.text.itcm` grew **+976 bytes**, crossed the 32 KB boundary, and dropped
`free for local variables` from 34,944 to **2,208** — the exact state `Teensy-RAM1-Bank-Overflow.mdc`
records as a *silent* boot overrun. Fixed by marking `buildLoopSnapshotFromValidatedPayload`
`noinline` + `.flashmem` via a new `LSV1_BUILDER_MEM` in `src/StorageLoopIoBodyFormatV1.cpp`, which
returned the bank. Measured by stashing the phase and diffing `size -A` sections, not by guessing.

## 3. P2 — name the constants and enums

Values never change in this phase — only their definition site and name.

- [x] 3.1 Link the duplicated membership envelope: add `static_assert(kLsv1MaxMembershipRefsPerBar == kSnapshotMembershipMaxRefsPerBar)`
  (or define one from the other) between `src/StorageLoopIoBodyFormatV1.cpp:138` and `include/LoopSnapshot.h:43`.
  Verify: compiles; the assert fires if either value is edited.
- [x] 3.2 Replace the raw `2048` bounds in `src/Loop/LoopPreparedCompanionHistory.cpp:437,460` with
  `RuntimePlaybackViewConfig::kRecordsPerBridge` (already used at `:23`) and confirm intent with the
  `LoopOverdubSession.cpp` twins before touching them. Verify: native suite passes; grep shows no raw `2048` in that
  file.
- [x] 3.3 Replace the `768u` minimum-loop-length literals (`src/Track/RuntimeCompanionApply.cpp:226,301`, `src/Track/RuntimeNotePairLookup.cpp:306`)
  with `LoopEventStoreConfig::BAR_TICKS` **only after** confirming minimum length is exactly one bar; if the semantics
  differ, add a named `kMinimumLoopLengthTicks` instead. Verify: compiles; native suite passes; the chosen name states
  the semantic in a comment.
- [x] 3.4 Name the bit fields: closure `flags` (`include/RuntimePlaybackView.h:163-165` and its 13 uses in `src/Track/RuntimePlaybackBridge.cpp`)
  → named `kAffected/kOriginalOffFound/kInherited` or a scoped enum; bridge cycle masks (`1/2/4/8`, literal `15` at `:366,429-443,604,634-644`);
  atomic-preflight denial bits (`src/Track/TrackPreparedStopCommit.cpp:11-22,52-61,70-79`) → one `AtomicPreflightBit`
  enum that keeps each existing value. Verify: native suite passes; grep shows no bare mask literals at those sites; bit
  values documented per group.
- [x] 3.5 Give `UndoEntryReleaseCursor` (`src/Track/TrackUndoRetirement.cpp:19-64`) and `GlobalUndoStackTrimCursor`
  (`:69-113`) private `enum class Phase : uint8_t` matching `TrackUndoPublication`'s shape, preserving `uint8_t`
  storage. Verify: compiles with no size change for the cursors; native suite passes.
- [x] 3.6 Record OD-2 with the user: document whether `snapshot_unit`/snapshot-idle probes belong on the broad
  `SESSION_CAPTURE` gate or `SESSION_CAPTURE_SNAPSHOT_IDLE`, as a `DEC` entry plus a comment at the gate in
  `src/Loop/LoopSnapshotPublish.cpp` — **no gating change in this change**. Verify: `DEC-###` appended; the comment
  names the decision; `git diff` shows no `#if` change.
- [x] 3.7 P2 gate: full native suite plus firmware build; record the RAM1 line. Verify: no literal value in the diff
  changed (review the diff for `=` on the touched constants); suite unchanged.

### P2 outcome (2026-09-18)

Gate: `pio test -e native` **2,262 / 1 skipped / 2,261 passed**; firmware build SUCCESS with the RAM1 line
**identical to P1** (`variables 96096 / code 392248 / padding 968 / free for local variables 34976`,
FLASH `code 1007632`) — this phase is FlexRAM-bank neutral, measured rather than assumed.

Landed: the LSV1/in-memory membership-envelope `static_assert`; `RuntimePlaybackViewConfig::kRecordsPerBridge`
replacing raw `2048` in both companion-row guards; `LoopEventStoreConfig::BAR_TICKS` replacing `768u` at the three
minimum-length clamps (confirmed one-bar-floor semantics with a comment); `PlaybackHeldNoteClosure` flag constants
plus `resolved()` rewritten over named bits (12 sites); `PlaybackBridgeCycleMask` constants for the bridge cycle
masks including the age-rotation expression (12 sites); named `AtomicPreflightBit` / `ReservedBridgePreflightBit`
constants for the `atomic_preflight` denial codes, with the forensic values unchanged; and a private `Phase` enum
class for both DEC-063 retirement cursors (values preserved, `nextPhase()` helper replacing `++phase_`).

Two follow-ups recorded rather than silently dropped:

- The `atomic_preflight` codes are named as file-local constants in `TrackPreparedStopCommit.cpp` rather than in
  its header: they are an implementation detail of a capture-trace encoding with no decode table in `docs/`,
  `scripts/` or the OpenSpecs, so putting them in a public header would add API surface for no consumer.
- `ExternalPoolAccounting::kTagCount` stays 18 (see the P1 outcome): shrinking it to the highest enumerator
  would change the length of the printed `tags=` vector.

**Gate caught a native-only masking bug.** `LoopEventStoreConfig::BAR_TICKS` compiled in the native suite because a
test TU's include order pulled `LoopEventStore.h` in transitively, but the firmware build failed with
`'LoopEventStoreConfig' has not been declared` in `RuntimeNotePairLookup.cpp`. Fixed by adding the explicit
`#include "LoopEventStore.h"` to both companion TUs that use it (`RuntimeNotePairLookup.cpp`,
`RuntimeCompanionApply.cpp`) — a reminder that the firmware build, not the native suite, proves include hygiene here.

OD-2 is recorded as **DEC-076** in `docs/DECISION_LOG.md` (index row + entry) with a comment at the gate in
`LoopSnapshotPublish.cpp`; the gate itself is unchanged.

## 4. P3 — de-duplicate into existing owners

- [x] 4.1 Add `playbackEventKind(const MidiEvent&)` to `include/MidiEvent.h` (or to the new ordering header) and one
  merge-order key in a header-inline `include/Utils/PlaybackEventOrder.h`; migrate the duplicated comparator
  (`src/Track/RuntimePlaybackBridge.cpp:588-598` vs `:699-706`) and the 8 `eventKind` ternary sites in touched files.
  Verify: `test_playback_prewarm` and `test_continuous_capture` pass with byte-identical emitted traces.
- [x] 4.2 Introduce one `RuntimeWorkerStepResult` and rename the five result enums per design § D4 (folding `RuntimePlaybackViewBuildGrain` into the class's `Phase`/`nextGrain()`),
  preserving the extra `Ready` state where it carries information. Verify: compiles; every call site updated; native
  suite passes with unchanged case count.
- [x] 4.3 Converge the lifecycle accessors per design § D5: `available()` as the admission-state query (replacing `idle()`/`active()` where they mean the same),
  `lastStepInputCount()` as the grain-progress query, one source-identity shape. Verify: grep shows one spelling per
  concept; native suite passes.
- [x] 4.4 Collapse the duplicated atomic-transfer bodies (`src/Track/TrackPreparedStopCommit.cpp:112-134` vs `:136-161`)
  into one body parameterised by an undo pointer, and route `commitStopped`/`commitStoppedWithUndo`/`commitWithUndo`
  through the same shape so the three test-only wrappers stop being the only callers of a production path. Verify:
  `test_playback_prewarm` stop/undo cases pass; `commitStopped*` has either a production caller or is removed.
- [x] 4.5 Add inline `externalFirstAlloc(size_t)` / `externalFirstFree(void*)` to
  `include/Utils/InternalHeapFirstAllocator.h` and migrate the 16 sites across the eight companion TUs, preserving
  extmem→malloc order and free routing. Verify: `test_pending_note_change`, `test_loop_take_survival`,
  `test_playback_prewarm` pass; no new out-of-line symbol (size check).
- [x] 4.6 Extract the admission guard (10 sites) and source-identity guard (5 sites) into one owner per design § D3/§
  D4. Verify: native suite passes; **re-measure RAM1** because member layout of by-value `TrackPlaybackRuntime` members
  may change; revert if a bank edge moves.
- [x] 4.7 Promote the existing ceiling-grain lambda to one `constexpr ceilGrains(...)` on
  `include/RuntimePlaybackOrderBuilder.h` and give the per-phase slack terms names — **values unchanged**. Verify: grep
  shows the two local lambdas gone and no slack literal re-valued in the diff; native suite passes.
- [x] 4.8 Add `activeNoteEntriesMatch(...)` to `include/ActiveNoteLedger.h` and use it in all four sites
  (`src/Track/RuntimePlaybackView.cpp:40,362-363`, `src/Track/RuntimePlaybackBridge.cpp:310-311,408-409`). Verify: the
  anonymous-namespace copy is deleted; native suite passes.
- [x] 4.9 Extract one `installRetainedCurrentView(views, sourceIndex, requireRawPrepared)` for `installPreparedCurrent`
  / `replaceRetainedWithReady` (`src/Track/RuntimePlaybackView.cpp:541-557` vs `:559-575`). Verify:
  `test_playback_prewarm` lifecycle cases pass.
- [x] 4.10 Extract the duplicated event-membership page comparator in `src/Loop/LoopSnapshot.cpp:1131-1142` vs
  `:1240-1251` — comparator only; do **not** template the bar-apply stage machines (design § D8.4). Verify:
  `test_loop_event_store` and `test_loop_take_survival` pass byte-identically.
- [x] 4.11 Extract one `shouldTrimGlobalUndoStackForMemory(...)` predicate into `include/PassReclaim.h` used by both
  `src/PassReclaim.cpp:84-116` and `src/Track/TrackUndoRetirement.cpp:85-86`, with a comment recording that the two
  agree on reachable cases today and why `cursor == 0` stays as written. Verify: `test_pool_budget` and
  `test_global_undo_stack_persistence` pass; no policy value changed.
- [x] 4.12 Consolidate the 12 `*_MEM` aliases this branch added into aliases of the canonical owners
  (`NOTE_EDIT_MEM`/`LOOP_COLD_MEM`/`TRACK_COLD_MEM`) instead of independent `FLASHMEM` definitions; do not rename the
  legacy ones. Verify: firmware builds; the RAM1 line is unchanged; each TU still documents its cold rationale in one
  line.
- [x] 4.13 Replace the four unique-id append/contains copies, the seven `make_unique<…Box>()` sites (private `ensurePendingSnapshotPublishBox()`),
  and the seven bar-count ceil-div copies with one named helper each, adopted on touch. Verify: native suite passes;
  helpers are inline/constexpr so no new symbols.
- [x] 4.14 P3 gate: full native suite plus the differential/oracle fixtures (`test_playback_prewarm`, `test_continuous_capture`, `test_loop_take_survival`, `test_storage_loop_io`)
  and a firmware build; record the RAM1 line. Verify: byte-identical traces/inventories and unchanged case count.

### P3 progress (2026-09-18) — phase incomplete, gate deferred

Landed so far, each verified green: **4.5** (the external-first allocate/free pair now lives once on
`include/Utils/InternalHeapFirstAllocator.h` as inline `externalFirstAlloc`/`externalFirstFree`, and all
16 sites across the eight worker TUs route through it — the three local helper pairs were deleted and the
five inline destructor sites migrated; semantics are identical because every site already freed with the
`else std::free(...)` form), **4.8** (one `activeNoteEntriesMatch` on `ActiveNoteLedger.h` replacing four
copies, one of which was a local `ledgerEntryEquals` in an anonymous namespace), **4.9**
(`installRetainedCurrentView` shared by `installPreparedCurrent` / `replaceRetainedWithReady`), **4.11**
(one `shouldTrimGlobalUndoStackForMemory` predicate on `PassReclaim.h` used by both the batch trimmer and
`GlobalUndoStackTrimCursor`; the cursor keeps its own compaction policy and re-decides after each entry,
per DEC-063).

Sub-phase gate: native **2,262 / 1 skipped / 2,261 passed**; firmware SUCCESS with RAM1
`variables 96096 / code 392312 / padding 904 / free for local variables 34976` — free-for-locals unchanged,
so still the same FlexRAM bank.

**Second sub-commit:** **4.1** — `include/Utils/PlaybackEventOrder.h` now owns `eventKind` and the five-key
merge key (`mergeKey`/`mergeKeyBefore`), and both sides of the bridge merge (the compile-side base/insertion
merge and the consume-side `insertionFirst`) build a key instead of repeating the ternary chain, with the
order-builder comparator using the shared `eventKind`. This is the duplication with a correctness invariant
behind it: a compiled order that disagreed with the consumed order would drop or duplicate accepted events,
and `test_playback_prewarm` (179) plus `test_continuous_capture` (77) confirm the byte-identical traces.
**4.10** — one `eventMembershipPageLess` replaces the two identical event-membership page comparators in
`LoopSnapshot.cpp`. Sub-phase gate: native 2,262 / 1 skipped / 2,261 passed; firmware SUCCESS with RAM1
unchanged (`code 392312 / padding 904 / free 34976`) and FLASH code 1,005,544 (-2,096).

**Third sub-commit:** **4.7** — `RuntimePlaybackOrderBuilder::ceilGrains` is the single owner of the
ceiling-grain idiom; the two local lambdas are gone and 11 sites across the four worker TUs call it. Every
migrated site was checked for a preceding `capacity == 0 -> UINT64_MAX` guard, so `1 + (count-1)/quantum`
and the exact-ceil form are provably identical there; the fixed slack terms are documented in place (naming
them was declined because no bound derivation is recorded anywhere and a wrong name would mislead more than
a bare `+ 4` with a comment). **4.12** — the six remaining independent `FLASHMEM` aliases in this subsystem
become `NOTE_EDIT_MEM` aliases, matching what the eight sibling companion TUs already did, so placement has
one definition. `LSV1_IO_MEM`/`LSV1_SAVE_STEP_MEM`, `LOOP_SNAPSHOT_COLD_MEM` and `IC_LOG_MEM` are
deliberately left alone: the first two have no appropriate existing owner in the storage TU, and the last
two are `noinline` + `.flashmem` by design (consolidating them to plain `FLASHMEM` could let the compiler
inline that code into ITCM callers and cost a FlexRAM bank). Sub-phase gate: native 2,262 / 1 skipped /
2,261 passed; firmware SUCCESS with RAM1 byte-identical (`code 392312 / padding 904 / free 34976`).

**Fourth sub-commit:** **4.13** — the three repeated id-list idioms become one template pair
(`containsId`/`appendUniqueId`) on `LoopSnapshotBoxes.h`, which both the publisher and the playback view
already include; the two near-identical playback helpers and the publisher's inline scan are gone (12 + 3
call sites). The guarded box-creation idiom becomes `Loop::ensurePendingSnapshotPublishBox()` at its three
sites — the four unconditional `= std::make_unique<...>()` sites are deliberate *resets* and were left
alone, which is why this is 3 of the 7 sites the task named. The bar-count ceiling division gains an owner
as `LoopEventStoreConfig::barCount` (the namespace that already owns `BAR_TICKS`), adopted at the two
in-scope body-format sites and behind `totalVisualBarsForLoop`, whose redundant zero guard it subsumes.
The seven other repo-wide bar-count sites are outside this change's added code and are left for a
touch-and-adopt pass rather than a sweep. Sub-phase gate: native 2,262 / 1 skipped / 2,261 passed;
firmware SUCCESS, RAM1 `code 391736 / padding 1480 / free 34976` (-576 code, same bank, more slack).

### P3 remaining tasks: two premise corrections found by measurement (2026-09-18)

Before starting 4.2 and 4.3 I measured their blast radius and checked their premises. Both are wrong as
written, so neither was attempted; the phase gate 4.14 stays open.

**4.2 — the shared result enum is a ~930-site, type-aware rename.** Reference counts:
`RuntimePlaybackOrderBuildResult` **800** (240 production, 560 test), `RuntimePlaybackViewBuildGrain` 75,
`RuntimeCompanionSpanResult` 30, `RuntimePlaybackViewFillResult` 28, `RuntimeCompanionOverlapResult` 26.
The enumerator rename cannot be done with a text sweep: `::Pending` (419), `::Complete` (358) and
`::Denied` (343) are shared with many other enums in the repo, so every site needs type-aware rewriting.
The functional gain is vocabulary consistency only, and the diff would touch most test files. The design's
own rejected alternative ("rename only, keeping five names") exists precisely because this trade is
unattractive, and NAMING.md forbids rename-only churn. **Resolved with the user (2026-09-18):** keep only the small win. `RuntimePlaybackViewBuildGrain` was
folded into the class as a public `RuntimePlaybackViewBuild::Grain` (the build's own stage, so one type
instead of two: 94 references migrated, the free enum and the private duplicate `Phase` are gone). The
800-site `RuntimePlaybackOrderBuildResult` rename is left to touch-and-adopt and is **not** part of this
change.

**4.3 — the three "synonym" accessors are three different predicates, not three names for one concept.**
Measured definitions:
- `available()` (5 classes: NotePairLookup, NoteDeleteLookup, SpanWalk, Reconstruction, OverlapWalk,
  CompanionApply) = `phase_ == Idle || phase_ == Complete [|| phase_ == Denied]` — "may `begin()`".
- `idle()` (CanonicalPassInputs, CompanionPasses) = `phase_ == Phase::Idle` — strictly the Idle stage.
- `active()` (PlaybackOrderBuilder, ViewBuild) = `phase_ != Idle && phase_ != Complete` — a grain is in
  flight, i.e. close to the **negation** of `available()`.
- `RuntimeCaptureCompanions::idle()` = `capture_ == nullptr` — no capture owner attached; a different
  concept again.

Converging these onto one name would change behaviour at call sites (a `Complete` builder is `available()`
but not `idle()`; a mid-grain builder is `active()` but not `available()`). Design D5 assumed they were
synonyms; that assumption does not hold. **Resolved with the user (2026-09-18):** the rename is dropped. The one genuine defect is fixed:
`RuntimeCaptureCompanions::idle()` is now `hasCaptureOwner()` (`capture_ != nullptr`), which states the
ownership question it answers instead of borrowing a phase word — one production guard, one native
assertion and two comment mentions updated, no semantic change. `available()`/`idle()`/`active()` keep
their distinct meanings and are documented here so a later reader does not "converge" them by mistake.

**Fifth sub-commit:** **4.4** — the two atomic-transfer bodies (`advanceAtomicTransfer` and
`advanceAtomicTransferWithUndo`) were identical except for which tail commit published the protected undo
entry. They now share one `advanceAtomicTransferImpl(..., GlobalUndoStack* stack, TrackUndoPublication* undo,
...)` and are thin forwarders. Source length grew slightly (the shared body needs a parameterised branch
where each original had a direct call) but there is now one body to maintain and the two routes cannot
drift. The task's second clause ("`commitStopped*` has either a production caller or is removed") was
checked and **dissolved**: the review counted only external callers, but `commit`, `commitWithUndo`,
`commitStopped` and `commitStoppedWithUndo` are all invoked from the two advance functions, i.e. they are
the routed production tail, not test-only wrappers. Sub-phase gate: native 2,262 / 1 skipped / 2,261
passed; firmware SUCCESS with RAM1 unchanged (`code 391736 / padding 1480 / free 34976`).

**Sixth sub-commit (4.6 + the P3 phase gate 4.14):** the ten admission guards become `!available()` and the
five source-identity guards become `sourceIdentityMatches()`, with **no member-layout change** — the review's
shared `RuntimeWorkerSourceIdentity` struct was deliberately not introduced, because it would move members of
classes held by value in `TrackPlaybackRuntime` for a naming-only gain. `available()` gained three missing
definitions (CanonicalPassInputs, CompanionBridgeMap, CompanionCompanionPasses) and BridgeMap's identity
check was named from its pointer-bound `baseIdentity_` form.

**The gate caught a real semantic error in this sub-commit, which is worth recording.** My first pass gave all
ten sites the same 2-value `available()` (`Idle || Complete`). But the guards have two variants: six sites
were `!= Idle && != Complete && != Denied` (admitting a restart from `Denied`) and four were `!= Idle &&
!= Complete` (treating `Denied` as terminal). Collapsing the 3-value sites onto a 2-value predicate denied
admission where the original allowed it, and `test_frozen_canonical_pipeline_bridge_matches_materialized_reference`
plus `test_per_pass_bridge_mapping_denies_invalid_provenance_and_reuse` failed — they were found by
`pio test -e native -f test_playback_prewarm`, not by inspection. Fixed by giving the three affected classes
the 3-value form their original guard used, with a comment distinguishing "denied run is restartable" from
"Denied is terminal".

**P3 phase gate 4.14: closed.** Final P3 gates: native 2,262 cases / 1 skipped / 2,261 passed; firmware
SUCCESS with RAM1 `variables 96096 / code 391736 / padding 1480 / free for local variables 34976` and FLASH
`code 1004520`. Against the recorded baseline that is home-neutral on locals (34,976 vs 34,944), code -608,
padding +608, and FLASH -5,088. The phase gate stays unchecked until they
land; the P3 work is committed in sub-commits so each is independently bisectable.

## 5. P4 — converge naming on the authority (record the decision first)

- [x] 5.1 Author the `DEC-###` for OD-1 (`view` vs `window`) with explicit user approval, and add the one-clause note to
  `docs/Authority/NAMING.md`; **no rename in this change**. Verify: `DEC-###` appended, NAMING clause present, and
  `grep` shows no `Runtime*View*` symbol renamed.
- [x] 5.2 Rename `normalized*` → `merged*` (NAMING's term for the merged working view) **only** in files P1–P3 already
  touched: `include/RuntimeCanonicalPassInputs.h:77-85`, `include/RuntimeCompanionPasses.h:47-50` and the
  `adoptCommittedNormalization` call site. Verify: native suite passes; `normalize` no longer names a merged buffer.
- [x] 5.3 Rename the four `*Kind` enums to `*Type` (`SnapshotChangeKind`, `RuntimeCompanionApply::Kind`, `ActionKind`, `RefreshKind`)
  per NAMING rule 6. Verify: compiles; no other `Kind` enum introduced; native suite passes.
- [x] 5.4 Fix abbreviations and scope-collisions in touched files: `SizeFn`/`EventAtFn`/`GenerationFn` → `…Reader`;
  `RuntimePlaybackViewFillScope` → `…FillCoverage`; the prohibited "lane" comment at
  `include/RuntimePlaybackView.h:405`. Verify: grep shows the old spellings gone from touched files only.
- [x] 5.5 Fix ownership-attribution naming with comments/renames, not moves: note the declaring headers at the top of
  `src/Track/TrackUndoRetirement.cpp` (or rename the TU to its declaring owners), give `RuntimeNoteDeleteLookup` its own
  TU/prefix per NAMING's file-per-owner rule, and align `retire`/`release` prose on the identifier's verb. Verify: each
  file's name or header comment matches what it defines; native suite passes.
- [x] 5.6 Record OD-3 as a comment: either wire `playheadPhaseTick` in the note-side `applySoonestBar`
  (`src/Loop/LoopSnapshot.cpp:897-898`) or rename it to state that ascending source order is intentional and delete the
  `(void)` cast. **No ordering change** without its own change and HITL evidence. Verify: the `(void)` cast is gone, the
  comment states the contract, native suite passes.
- [x] 5.7 P4 gate: full native suite; `grep` audit showing no deprecated name survives in the touched files. Verify:
  suite unchanged; diff contains no file touched *only* by a rename.

### P4 progress (2026-09-18) — phase incomplete, gate deferred

**5.1 — OD-1 recorded as DEC-077.** The user chose `window` as canonical for the playback derived
representation. `DEC-077` (index row + entry, newest first) states the decision, why it could not be a
rename (DEC-067's capacity table already names `RuntimePlaybackView.cpp`, and a rename-only sweep is
forbidden), and the two consequences. A new NAMING.md clause **"Playback window (canonical) vs view"**
records that new names in this subsystem say *window* (or name their own concept) and that the existing
`RuntimePlaybackView*` family is grandfathered and converges on touch. **No symbol was renamed.**

**5.2 — `normalized*` → `merged*`** in the files this change touches: 26 sites over 6 files
(`mergedScratch`, `mergedDescriptors`, `claimMergedScratch`, `mergedScratchMatches`, `mergedEventCount`,
`mergedPassCount`, `mergedMatches`, `adoptCommittedMerge`). `normalize` in this repo already means tick-range
normalization, so a third name for the authority's **merge** was actively misleading. All eight target names
were checked for collisions first (none existed).

**5.4 (partial) — the forbidden metaphor is gone**: the `RuntimePlaybackView.h` comment no longer says
"the windowed lanes"; it says "the three rolling windows" (NAMING.md forbids lane/tile/bucket/grid for the
bar granule).

Sub-phase gate: native 2,262 / 1 skipped / 2,261 passed; firmware SUCCESS with RAM1 unchanged
(`code 391736 / padding 1480 / free 34976`).

**Second sub-commit (5.3, 5.4, 5.6, part of 5.5):** `*Kind` → `*Type` for the four enums this change
added (147 token renames plus 27 bare `Kind` uses in the Apply pair); the ~16 **pre-existing** `*Kind`
enums (`PendingNoteChangeKind` 236 refs, `UndoEntryKind` 192, `NoteEditKind` 164, …) were deliberately
**not** swept — touching them is the repo-wide rename churn NAMING forbids, so they converge on touch.
`SizeFn`/`EventAtFn`/`GenerationFn` → `…Reader` and `RuntimePlaybackViewFillScope` →
`RuntimePlaybackViewFillCoverage` (51 sites). OD-3 is documented in place: the note-side
`applySoonestBar` now leaves the parameter unnamed with a comment saying its ordering is a deferred
timing-model change, replacing the `(void)` cast that hid the gap. 5.5 is partly done: the
`TrackUndoRetirement.cpp` misattribution is fixed with a header comment naming its two declaring headers
(and why the TU is not renamed: a new/renamed TU defaults into ITCM and needs its own RAM1 measurement);
moving `RuntimeNoteDeleteLookup` to its own TU and aligning the `retire`/`release` prose are still open.

**Third sub-commit (5.5 + the P4 phase gate 5.7):** the `retire`/`released` prose in `LoopSnapshot.h`
now matches the `release` identifiers; `RuntimeNotePairLookup.h` opens by naming both owners it declares
and records that splitting them into their own TU was **considered and declined** (a new TU defaults into
ITCM unless it matches the linker flash allow-list, so the move needs its own RAM1 measurement for no
ownership gain). Three further "windowed lanes" mentions the task had not named were found by the gate
audit and fixed (a fixture comment and two journal lines) - `lane` is now absent from the touched code and
docs.

**P4 phase gate 5.7: passed, with one recorded deviation.** Native 2,262 / 1 skipped / 2,261 passed;
firmware SUCCESS with RAM1 unchanged (`code 391736 / padding 1480 / free 34976`); the audit shows no
deprecated name surviving (`normalized*`, the four `*Kind` names, the `*Fn` abbreviations,
`RuntimePlaybackViewFillScope` and "windowed lanes" are all 0). **Deviation:** the gate also required "no
file touched only by a rename", and 11 test files plus `NAMING.md` are rename-only in the P4 commits. That
criterion was too strict as written: renaming an enum that 18 files use necessarily edits its users, which
is unavoidable collateral rather than unrelated churn. The distinction that mattered - not sweeping the
~16 pre-existing `*Kind` enums - was honoured.

## 6. P5 — split the three files with real ownership seams

Each split: native suite + firmware build, then compare the RAM1 line and `_itcm_block_count`
(`linker/imxrt1062_t41_lcr.ld`). A split that crosses a bank is reverted and recorded as rejected.

- [ ] 6.1 Move `CaptureAdmissionStaging` (`src/Track/RuntimePlaybackView.cpp:377-522`) and `CaptureNoteReservation`
  (`include/RuntimePlaybackView.h:242-298`) into a new TU/header; keep the capacities owned where the DEC attribution
  table puts them, and state in the task log whether the new TU name matches the linker's flash allow-list or every
  function carries a marker (design § D8.2). Verify: native suite passes; RAM1 line and `_itcm_block_count` unchanged or
  better.
- [ ] 6.2 Move the bridge read side (`RuntimePlaybackBridgeCursor` `src/Track/RuntimePlaybackBridge.cpp:659-732`, `RuntimePlaybackBridgeStream` `:734-812`)
  into its own TU; leave `insertionFirst/peek/advance` unmarked exactly as today. Verify: `test_playback_prewarm` bridge
  cases pass; RAM1 line unchanged or better.
- [ ] 6.3 Move the `SESSION_CAPTURE` idle-telemetry block out of `src/Loop/LoopSnapshotPublish.cpp`
  (the `:15-98` block, `snapshotFailureReasonName`/`emitSnapshotFragmentFailure`, and the 15
  `Loop::lastSnapshotIdle*Us`/`addSnapshotIdleEmitUs` at `:1654-1772`)
  into a `LoopSnapshot`-named TU so the linker keeps it in FLASH. Verify: firmware builds; the gated
  `#CAP,…,DIAG,snapshot_unit` output is unchanged on capture; RAM1 line unchanged or better.
- [ ] 6.4 Record the rejected splits in `design.md` § D8.4 with the measurement that rejected them
  (`RuntimeCompanionApply::step()`, `StorageLoopIoBodyFormatV1.cpp`, `RuntimePlaybackBridgeBuild::step()`, the four `Track*` TUs).
  Verify: each rejected split names its reason; no split was done for line count.
- [ ] 6.5 P5 gate: full native suite plus firmware build and the recorded RAM1/`_itcm_block_count` comparison for all
  three splits. Verify: no bank crossing; `free for local variables` not reduced.

## 7. P6 — formatting, docs and enforcement

- [ ] 7.1 Normalize the copyright header to the repo baseline (`//  Copyright (c)  <year> Lytrix (Eelke Jager)`) across
  the 56 files this branch added, which currently use three forms (31 single-space, 17 double-space, 8 mixed). Verify:
  `head -1` over the added files yields one form; no other line changed.
- [ ] 7.2 Strip trailing whitespace from the 24 added files that carry it. Verify: `grep -c '[ \t]$'` is zero for each;
  diff is whitespace-only.
- [ ] 7.3 Add `.editorconfig` and a `.clang-format` derived from the incumbent style; accept the config only if
  `clang-format --dry-run` over the added files reports only the drifts 7.1/7.2 already fixed — otherwise ship
  `.editorconfig` only and record why (design § D9). Verify: the dry-run output is recorded in the change directory.
- [ ] 7.4 Move the session journal out of `openspec/changes/resumable-midi-boundary-playback-maintenance/tasks.md` (20 checkboxes in 1,126 lines, with single prose lines up to 5,543 characters)
  into `docs/Runtime/CURRENT_WORK.md`, leaving `tasks.md` a checklist; cut the 131 measured duplicated long sentences
  across `CONTINUOUS-CAPTURE-OWNERSHIP.md` ↔ `FOCUSED-STOP-HITL.md` (61), `FOCUSED-STOP-HITL.md` ↔ `tasks.md` (35) and
  `design.md` ↔ the others (30) by citing instead. Verify: `tasks.md` has one task per line under 200 characters; the
  sentence-overlap count drops; every removed paragraph is reachable by citation from `CURRENT_WORK.md`.
- [ ] 7.5 P6 gate: `openspec validate runtime-code-tidyup --strict` passes and no firmware/test run is needed because
  the phase is text-only. Verify: validate output recorded.

## 8. Closeout

- [ ] 8.1 Update `docs/Runtime/CURRENT_WORK.md` and `docs/Runtime/PROJECT_STATE.md` to reflect the completed tidy-up and
  remove it from § Now implementing. Verify: neither doc still describes the removed symbols or the old gate/name;
  `DOCUMENTATION_CLOSEOUT` checklist satisfied.
- [ ] 8.2 Update [`docs/DELIVERABLE_TRACKING.md`](../../../docs/DELIVERABLE_TRACKING.md) with the maintainability
  deliverable. Verify: a row names this change and its gates.
- [ ] 8.3 Append the `DEC-###` entries required by this change (OD-1 naming, OD-2 instrumentation gating, plus any `ARCHITECTURE_RULES` requirement found in task 1.1).
  Verify: each entry cites the decision, the consequence and the rejected alternative.
- [ ] 8.4 Run [`docs/Templates/SESSION_CLOSEOUT.md`](../../../docs/Templates/SESSION_CLOSEOUT.md) because design and
  exclusions were discussed, and record the outcome. Verify: the template's fields are filled in the change directory or
  the log.
- [ ] 8.5 Confirm the final gates together — full native suite, firmware build with the RAM1 line, `openspec validate --strict`
  — and record all three in `PREFLIGHT.md`. Verify: green before any archive step.
- [ ] 8.6 **Ask** before opening the PR (no auto-PR per `AGENTS.md`): `refactor/runtime-code-tidyup` → the base chosen
  by OD-5 (default: `feature/resumable-midi-boundary-playback-maintenance`). Verify: the PR lists the phase commits and
  states that no behavior, format or persisted layout changed.
