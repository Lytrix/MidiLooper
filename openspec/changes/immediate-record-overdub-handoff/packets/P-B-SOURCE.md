# P-B — private-source preparation and immediate ingress

G2 execution packet for [CONTRACTS.md](../CONTRACTS.md) C3.1–C3.6.
Index: [packets](README.md). Template: [EXECUTION_PACKET](../../../../docs/Templates/EXECUTION_PACKET.md).

## Identity and scope

- **Packet / task IDs:** P-B. Implements C3 — private-source preparation and deadline. OpenSpec spec:
  `capture-state-guards` — *Source preparation does not block first-overdub input or lose overlap
  correctness* (both scenarios). **DEC-159 — immediate first-overdub sealed custody.**
- **Status:** **implemented and verified natively** (`IMMEDIATE_HANDOFF_CASE=source` GREEN, focused 18/18,
  full native 3,462 passed / 0 failed, build at the approved pins); C3.3/C3.4 amended by owner ruling
  2026-10-25. Device gates and the P-D continuation remain open. [Execution](../evidence/PB-SOURCE-EXECUTION.md).
- **Owner and path:** `Loop::establishOverdubSourceView` → `Loop::rebuildOverdubSourceView` →
  `Loop::rebuildOverdubSourceSpanCache` (source view), plus a per-interval resumable cursor stepped
  from the existing derived-view branch of `Track::processDeferredIdleMaintenance`. Consumed at
  note-on/off (`Track::snapshotOverlapHoldCandidates`, `Loop::resolveOccupancyAtNoteOn`,
  `Loop::accumulatePendingNoteChangesForIncomingNote`) and sealed at
  `Loop::sealPendingNoteChangesToEditPasses`.
- **Primary files:** `src/Loop/LoopCapture.cpp` (source view/span cache, origin selection),
  `include/Loop.h` + `src/Loop/LoopPendingNoteChange.cpp` (causal record, cursor state),
  `include/Track.h` (cursor lives in the custody receipt from P-A),
  `src/Track/TrackDeferredMaintenance.cpp` (the one grain at the existing `"idle"` rebuild site).
  Harness: `test/test_immediate_capture_handoff` (existing `CASE=source` RED), `test/test_overdub_source_view`.
- **Preconditions already present:** the derived-view branch is **already reachable** while this Track
  is `OVERDUBBING` and while another Track records, and it already calls
  `rebuildOverdubSourceView(refreshTick, "idle")` when `isOverdubbing() && !overdubSourceSpanCacheReady()`
  (`src/Track/TrackDeferredMaintenance.cpp:1107–1122`). The sealed extent is already borrowable
  (`Loop::borrowSealedCaptureRange`). **Prerequisite in this packet:** a custody receipt per interval
  (P-A step 1) to own the cursor and its identity. **External blockers:** none.
- **Mutation:** add `SourceOrigin::PrivatePredecessor` with
  `PlaybackViewSourceIdentity{origin, captureContentGeneration = receipt.frozenCaptureContentGeneration}`
  (C3.1); add a resumable cursor shaped on `RecordSourceStaging::Cursor`
  (`begin` / `step(quantum)` / `discard`, `Step`, `DenyReason`) held by the receipt (C3.2); admit the
  private extent as an overlap **source** while refusing it as committed currency; step **one** grain at
  the existing `"idle"` rebuild site (C3.3); make note-on/off record the causal pair without waiting for
  the built source (C3.4); discard the cursor on identity change (C3.5).
- **Non-goals:** no relabel of private rows as `Snapshot`/`Prepared`/committed; no whole-take derived
  vector; no second reader; no device-gate producer as the wakeup (it is `SESSION_CAPTURE`-gated and
  transport-idle — see P-D); no budget increase; no guard deletion; no undo/geometry change (P-C).
- **Commit boundary — two independently buildable commits:**
  1. **Origin + cursor + reachable grain**, with the private origin reachable but still inert for
     committed readers. No behavioural delta to committed paths; all existing suites green.
  2. **Causal ingress switch** — note-on/off stop depending on a built source for this interval,
     which is the behavioural change and the only commit that may move `CASE=source` to GREEN.

## Oracle fixed before behaviour edits

| Case | Initial state | Ordered steps | Expected after each step |
|---|---|---|---|
| `P-B-1` `test_private_record_is_the_same_pitch_overlap_source` | A sealed private (pitch60/NoteId1/12..96), no committed pass | start B at phase48, accept a same-pitch held pair, drain the grain to completion | same-pitch participants include A's accepted `NoteId1`; the pair's overlap decision equals the sequential canonical oracle for that pitch set |
| `P-B-2` differential | A sealed private; B and C each accept a distinct same-pitch pair | stop/restart the cursor between the two | B's and C's decisions equal an independent sequential canonical run over the same pitch set; no cross-interval leakage |
| `P-B-3` mid-resolution publication | the cursor is mid-scan over A's extent | publish A (bump `playbackRevision`) | the private partial is discarded, the committed origin is used, and no private prefix is mixed with a committed suffix |
| `P-B-4` bounded units | dense 33-bar predecessor | one grain | at most `kDeferredStopPreparationQuantum` (32) units; each unit is exactly one row read, one pair decision, or one pending-change upsert |
| `P-B-5` denial | pool pressure mid-scan | one grain under `Pressure` | the row stays unconsumed, position is exact, no partial transform is applied |

- **Completion / exactly-once:** each accepted pair is materialised once; a repeated grain at
  `Complete` changes nothing.
- **Preserved invariants:** committed currency (`committedPlaybackNoteOnIdentityValid`, root/pass
  readers, save) refuses the private origin; A's tuple inventory is unchanged; the serialized
  canonical worker is untouched.
- **Counterexample that must fail:** the current `CASE=source` RED — "private predecessor A is absent
  from B's same-pitch hold source", with source note count and participants at zero.

## Controls and coverage

- **Seams reused:** the suite's real button/manager/Track/Loop/source path with native clock and
  allocator boundaries. Direct `startOverdubbing` where the manager route is covered elsewhere.
- **Production paths asserted:** `establishOverdubSourceView`, `rebuildOverdubSourceView`,
  `rebuildOverdubSourceSpanCache`, `snapshotOverlapHoldCandidates`, `resolveOccupancyAtNoteOn`,
  `accumulatePendingNoteChangesForIncomingNote`, `sealPendingNoteChangesToEditPasses`.
- **Cases beyond the oracle table:** clear / slot switch / load / transport stop / record re-entry
  while a cursor is live; A publishing before the cursor starts; empty A (no extent) producing no
  cursor; the existing canonical control (same query after real publication) staying GREEN.
- **Independent expected content:** the sequential canonical run over the same pitch set — never a
  second copy of the resolver.
- **Numbers:** quantum 32 (`kDeferredStopPreparationQuantum`); no timing threshold is asserted here.
  The device service envelope stays a separate gate.
- **Untestable here:** device input-service gap and command→ingress latency — measured only under a
  registered stimulus.

## Verification recipe

```sh
pio test -e native -f test_immediate_capture_handoff
bin=/Users/eelkejager/Library/Caches/pio-build-250513-215524-teensy41/native/program
IMMEDIATE_HANDOFF_CASE=source "$bin"   # must move RED -> GREEN in step 2 only
pio test -e native -f test_overdub_source_view
pio test -e native                     # expect 3,454 total / 7 skipped / 0 failed
pio run -e teensy41-capture-serial     # build only; no upload
```

- **Pass criteria:** the focused suite builds and runs; `CASE=source` flips only in step 2; the
  canonical control stays GREEN; full native 0 failed; shipping build succeeds at unchanged pins with
  any cursor storage delta attributed.
- **Device:** NOT RUN. Requires a registered stimulus and separate authorization; native ordering is
  not hardware timing.
- **Evidence path:** `evidence/PB-SOURCE-EXECUTION.md`.

## Completion record

- Oracle/contract unchanged: pending
- Fixture implemented and discriminating assertion demonstrated: **PASS** for `P-B-1` —
  `IMMEDIATE_HANDOFF_CASE=source` exits1 with `FAIL: private predecessor A is absent from B's
  same-pitch hold source`, and the printed oracle row is `id=1,start=12,end=96,pitch=60,participants=0`
  ([fixture receipt](../evidence/PACKET-FIXTURES.md)). `P-B-2..P-B-5` need the cursor to exist.
- Native verification: NOT RUN
- Build/RAM1 verification: NOT RUN
- Device verification: NOT RUN (out of scope)
- Required reviews and durable docs: NOT RUN
- Task IDs eligible for checkoff after all of the above: none alone; P-B is the implementation half of
  P1 task 1.5's contract, and 2.2 still requires P-C
