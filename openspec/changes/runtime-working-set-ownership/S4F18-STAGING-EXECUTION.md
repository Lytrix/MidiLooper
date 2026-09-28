# S4f.18 — resumable job-owned pass staging

Status: native/build verified; Record integration remains open. Baseline c0db8a50; unrelated dirty HITL work preserved.
Authority: SHARED-CAPTURE-FINALIZATION.md, DEC-093 — shared capture finalization,
and S4F18-TRANSFER-EXECUTION.md. Existing owners and admitted capacity are reused.

## G0 / G1 review

The store cursor already bounds moves and order-restoring swaps. PrepareTransfer
still drains it synchronously via the Loop pending slot. Extend Loop staging to
construct directly in the job receipt. Each worker turn performs at most one move
or swap; begin does scalar preflight only. The receipt owns partial references but
is explicitly unready until the cursor completes. Publication and companion readers
must reject it, including alternate direct callers. The worker keeps its existing
identity validation, phase ordering, empty/retargeted branches and cleanup ownership.
No Record admission, normalization, new queue, horizon increase or device claim.
Cursor state belongs to the existing externally allocated job ring. Measure layout
before updating its pin; Track/Loop/runtime resident layouts must remain unchanged.

## G2 transition oracle fixed before implementation

1. A populated real OD job reaches PrepareTransfer without a pass. Begin retains
   output and creates an unready job receipt; Loop pending slot remains empty.
   Each later worker grain moves at most one chunk or swaps one pair. No pass is
   committed, borrowed or persisted while unready. Complete preserves exact rows.
2. A successor may start while transfer is partial. Its live capture and identity
   survive predecessor completion; the predecessor publishes/persists its own slot
   once. Existing playing/stopped/retargeted/rotation tests remain authoritative.
3. Direct Loop begin rejects live producer, occupied target/cursor, insufficient
   admission or cold source before ownership moves. Wrong destination/source and
   stale generation deny. An incomplete receipt cannot transfer even with capacity.
4. Source and candidate retain disjoint references on denial; completion clears
   source phase and marks readiness only after directory order is restored. Cursor
   resets on job retirement; no stale receipt can govern a reused job.

Fixtures use the real shared-finalization harness, stepping the existing deferred
idle entry one grain at a time. Limits derive from directory count plus half that
count for reversal, not sleeps. Mutation must demonstrate readiness or grain-bound
assertions. Full native, capture-serial build (padding >=840), strict OpenSpec and
whitespace checks required. Existing STIM-BASE-2-ONE-OD-01@v1 remains the eventual
integration stimulus. Hardware timing and Record integration remain NOT RUN/open.

## Observed completion defect included in the connected slice

The 282-row production fixture reaches AtomicTransfer with joint-pass, companion
verification and canonical-owner checks all true, but `canCommit` false: closure,
order, ranked and canonical credits are zero, undo credits are **2**, history **10**.
The undo collector and protected publication preparation have completed. Existing
small fixtures happen to consume the estimate; dense rows do not. This is the
same completed-estimate retirement already used by the other preparation phases.
Extend `retireCompletedPreparation` for UndoCollection and call it only after both
existing completion predicates return true. This belongs to the already authorized
S4f.20 — publication/undo integration. Preserve obligations while either is busy;
mutation removing retirement must reproduce the bounded fixture failure. No undo
policy or ownership change and no added finalization owner.

ARM object-size probe (exact capture-serial compiler flags, temporary header copy
only removes the old ring assertion for measurement): PendingCapturePass **36**,
Track **384**, job **736**, four-entry ring **2,984** bytes (was 2,728).
The cursor costs 64 bytes/job; the readiness flag uses existing pass padding.

## Verification and closeout

- Final focused shared-finalization suite: **31/31**, including denied undo
  collection, refused publication/retry, cursor retirement and exactly-once checks.
- Three independent mutations rejected: removing readiness permits premature
  transfer; quantum 32 drains two chunks in a turn expected to move one; skipping
  undo-estimate retirement reproduces the AtomicTransfer stall. All restored.
- Final capture-serial build PASS. FLASH code **1,044,504**; RAM1 variables
  **96,160**, code **392,376**, padding **840**, locals **34,912**; RAM2 **155,232**;
  static EXTRAM **9,312**. Resident pins unchanged, measured ring pin updated.
- Strict OpenSpec validation PASS. Registered stimuli listed; existing
  STIM-BASE-2-ONE-OD-01@v1 retained. No upload or hardware acceptance.
- Final `pio test -e native`: **2,527 passed / 7 skipped**, 2,534 total, no failures.
  Evidence logs: `/tmp/s4f18-staging-final-native.log`,
  `/tmp/s4f18-staging-final-build.log`, `/tmp/s4f18-staging-undo-retry.log`, and
  `/tmp/s4f18-staging-mutation-{readiness,quantum,undo-retirement}.log`.
- No new decision/owner/exclusion: implements DEC-093 through existing extension
  points. Storage guide, execution bounds, runtime handoff and task evidence
  updated. Branch checkpoint only, not shipped; deliverable status unchanged.
  No parent task is eligible for checkoff (50/176 remain completed).

Review: alternate direct transfer and companion entry reject partial receipts;
job interval and store/pool/destination receipts protect deferred continuation;
successor live capture is separate; existing slot/Loop identity checks still gate
work, and liveness prevents clear/load from replacing pinned owners. Canonical
reservation replenishment only changes the reservation, and prewarming an already
admitted source directory does not change its ownership generation or contents.
Publication/undo/persistence retain existing owners. Device clock/latency and full
Record normalization/playback are open gates, not covered by this native evidence.

The first broad run found one old continuous-capture assertion requiring pass
construction and phase advance in the same turn (2,526 passed, 7 skipped, one
failed assertion). Its checkpoint now explicitly requires an **unready**, empty
job-owned receipt in PrepareTransfer, a free Loop producer slot and denied
publication. The original successor identity/content/retirement assertions remain.
This strengthens the checkpoint for the new resumable contract rather than waiting
past it. The full suite is rerun after the fixture correction; firmware is unchanged.

Final review tightening: a denied collection can report “finished” without a usable
entry. Successful `TrackPreparedUndo::prepareOverdubSessionEntry` consumes a
collectable receipt back to Cold; require that proof before publication preparation
or estimate retirement. A refused protected-publication begin retains its entry;
reset the attempt flag and retry without leaving UndoCollection. The fixture first
injects a denied collector and restores it, then refuses publication with an invalid
next-entry identity, verifies the retained entry and phase, restores admission and
completes exactly once. The denial fixture failed against the unguarded retirement.
No existing undo omission policy is used to erase an accepted obligation.
