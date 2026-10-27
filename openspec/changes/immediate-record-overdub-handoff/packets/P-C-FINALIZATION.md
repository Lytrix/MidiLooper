# P-C — successor-safe finalization, undo and empty cleanup

G2 execution packet for [CONTRACTS.md](../CONTRACTS.md) C4.1–C4.7, authorised by
**DEC-160 — a predecessor's finalization is local to its own interval**.
Index: [packets](README.md). Template: [EXECUTION_PACKET](../../../../docs/Templates/EXECUTION_PACKET.md).

## Identity and scope

- **Packet / task IDs:** P-C. Implements C4 — successor-safe finalization, undo and empty cleanup.
  OpenSpec spec: `capture-state-guards` — *Late Record finalization cannot mutate a live or sealed
  successor* (all three scenarios). Authority: **DEC-160** (ratified 2026-10-25), **DEC-159** custody,
  **DEC-142** presentation lifetime split.
- **Status:** specified; fixture demonstrated (named RED), **not** implemented and not measured.
- **Owner and path:** `Track::stepDeferredRecordEmptyResult` and `stepDeferredRecordPublication`
  (deferred finalization) → `TrackUndo::pushRecordPassAdded` (undo attachment) →
  `Loop::resetPassTimeline` / `sealPendingNoteChangesToEditPasses` (global Loop state) →
  `requestLoopSlotPersistAndSaveState` (persistence). Service entry unchanged:
  `Track::processDeferredIdleMaintenance`.
- **Primary files:** `src/Track/TrackOverdubLifecycle.cpp` (empty/publication steps),
  `src/TrackUndo.cpp` (entry build + erasure predicate), `include/TrackUndo.h`
  (`UndoEntry::interval`), `src/Track/TrackStateFacade.cpp` (successor query),
  `src/Track/TrackStopTelemetryColdHelpers.cpp` (persistence marking).
  Harness: `test/test_immediate_capture_handoff` (`CASE=empty` RED), `test/test_shared_capture_finalization`,
  `test/test_record_preparation`.
- **Preconditions already present:** `job.pendingChanges` is already swapped out of the live session at
  admission, so pending-change ownership is interval-local today; the phase machine already gives
  exactly-once publication; persistence already dedupes per `(type, key)` while queued.
  **Prerequisite in this packet:** the custody receipt from P-A (for `hasLiveOrSealedSuccessorForSlot`)
  and the DEC-160 ratification. **External blockers:** none.
- **Mutation (DEC-160 items 1–5):** add `UndoEntry::interval`; add the read-only
  `Track::hasLiveOrSealedSuccessorForSlot(slot, loopId, interval)`; gate the empty-result cleanup on it;
  scope the undo prune to `interval <= job.interval`; assert per-receipt publication and slot-scoped
  persistence in tests rather than adding receipts.
- **Non-goals:** no new undo kind, no changed routing, no early private root authority, no second reader,
  no per-interval persistence receipt, no custody change (that is P-A), no source or visual change.
- **Commit boundary — two independently buildable commits:**
  1. **`UndoEntry::interval` + scoped erasure + the successor query**, with the query not yet used.
     All existing suites must stay green; the ABI delta (**+12** target bytes) is re-measured in the same
     commit with the preserved probe.
  2. **Empty-cleanup switch + publication-order independence**, the behavioural change and the only
     commit that may move `CASE=empty` to GREEN.

## Oracle fixed before behaviour edits

| Case | Initial state | Ordered steps | Expected after each step |
|---|---|---|---|
| `P-C-1` `test_filtered_empty_record_does_not_clear_live_B` | A uses the known filtered-empty recipe (On780/Off800, min48, stop1536); B accepts two distinct rows and starts interval 2 before A's worker runs | drain A's empty result | B's interval and its two raw rows survive **and** loop geometry is unchanged (not 768 → 0); A released (raw 0, range generation 1 → 2); no committed Record pass |
| `P-C-2` successor undo | A publishes while B holds an accepted interval with one undo entry | drain A's publication | A adds its own `RecordPassAdded`; **B's entry survives**; stack order equals causal order |
| `P-C-3` order independence | same content, two runs | (a) A then B publication; (b) B then A publication | identical final content, geometry and undo chronology in both orders |
| `P-C-4` exactly-once | A published, ring drained once | drain again | ring count and receipt state unchanged; one persist admission per `(type, key)`, not two |
| `P-C-5` stale identity | A private, then clear / slot switch / load / transport stop | late A worker runs | A's result applies to nobody; no successor is reset and no undo entry is resurrected |

- **Completion / exactly-once:** `RecordPassReady → RecordPublished` is consumed once per receipt; the
  persistence dirty flag is never treated as proof of an admission.
- **Preserved invariants:** no private intermediate state is persisted or advertised complete;
  `activeLoopIndex` remains the activation authority; B/C content, geometry and undo survive any A outcome.
- **Counterexample that must fail:** the current `CASE=empty` RED —
  `Expected 768 Was 0. empty A cleanup reset geometry still needed by accepted B`.

## Controls and coverage

- **Seams reused:** the real button/manager/Track/Loop/worker path with native clock and allocator
  boundaries; `test_shared_capture_finalization` for the nonempty-publication control; no success stub.
- **Production paths asserted:** `stepDeferredRecordEmptyResult`, `stepDeferredRecordPublication`,
  `TrackUndo::pushRecordPassAdded`, `eraseUndoEntriesForSlot` predicate, `resetPassTimeline` reachability,
  `requestLoopSlotPersistAndSaveState`.
- **Cases beyond the oracle table:** nonempty A preserving B (existing control must stay GREEN); all-filtered
  A; clear/load/slot/focus/transport/clock supersession; two slots in the same track; a receipt still
  retiring (P-A) while an empty result lands.
- **Independent expected content:** seeded identities and explicit endpoints; undo chronology compared by
  entry identity and order, not by count alone.
- **Numbers:** `UndoEntry` 132 → **144** target bytes (measured, append-at-end worst case); no timing claim.
- **Untestable here:** device RAM2/link impact of the `UndoEntry` field — reported by the build, and the
  C2.4 pre-flight (P-A step 1) owns the RAM1 question.

## Verification recipe

```sh
pio test -e native -f test_immediate_capture_handoff
pio test -e native -f test_shared_capture_finalization
pio test -e native -f test_record_preparation
bin=/Users/eelkejager/Library/Caches/pio-build-250513-215524-teensy41/native/program
IMMEDIATE_HANDOFF_CASE=empty "$bin"    # must move RED -> GREEN in commit 2 only
pio test -e native                     # expect 3,454 total / 7 skipped / 0 failed
pio run -e teensy41-capture-serial     # build only; no upload; report RAM1/RAM2 delta
python3 openspec/changes/immediate-record-overdub-handoff/evidence/probes/run-custody-probes.py \
  --out-dir "$PI_SCRATCH_DIR/custody-pc"   # confirm UndoEntryWithInterval = 144 target bytes
```

- **Pass criteria:** commit 1 changes no behaviour and every suite stays green; `CASE=empty` flips only in
  commit 2; full native 0 failed; shipping build succeeds with the `UndoEntry` delta attributed; the probe
  still reports 144 for the append-at-end shape.
- **Device:** NOT RUN. Needs a registered stimulus and separate authorization for the A/B/C stop sequence.
- **Evidence path:** `evidence/PC-FINALIZATION-EXECUTION.md`.

## Completion record

- Oracle/contract unchanged: pending
- Fixture implemented and discriminating assertion demonstrated: **PASS** for `P-C-1` —
  `IMMEDIATE_HANDOFF_CASE=empty` exits1 with `FAIL: Expected 768 Was 0. empty A cleanup reset geometry
  still needed by accepted B` ([fixture receipt](../evidence/PACKET-FIXTURES.md)). `P-C-2..P-C-5` need
  `UndoEntry::interval` and the successor query to exist.
- Native verification: NOT RUN
- Build/RAM1 verification: NOT RUN
- Device verification: NOT RUN (out of scope)
- Required reviews and durable docs: NOT RUN (DEC-160 already records the decision; the shipping delta
  amendment is part of the implementing commit)
- Task IDs eligible for checkoff after all of the above: the C4 implementation half of 2.2; P-C is the last
  of the four packets, so 2.2 no longer waits on packet generation
