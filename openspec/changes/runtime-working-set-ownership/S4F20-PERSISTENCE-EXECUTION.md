# S4f.20 — deferred publication persistence handoff

Status: handoff slice native/build verified, 2026-09-27; parent/device gates open. Baseline `e108944f`; unrelated uncommitted HITL changes preserved.
This is a bounded prerequisite within the authorized S4f.17–S4f.21 connected implementation,
not completion of record finalization or of S4f.20. Contract: [shared finalization](SHARED-CAPTURE-FINALIZATION.md),
DEC-093 — shared capture finalization. No new owner, queue, format or admission policy.

## G0 — verified trace and historical review

- Track's real `processDeferredOverdubStopGrain` validates the job against Track's slot-to-LoopId mapping, pointer and
  geometry before every grain (the Loop object identity gap is recorded below). Ordinary Playing, Stopped, retargeted raw-only and superseded
  causal-base transfers all move AtomicTransfer to DeferredCleanup on success.
- None of those successful worker transfers calls the storage admission owner. Synchronous
  `finalizeCommitSideEffects` does mark/admit/request, including a skipped-empty overdub tail
  (earlier wraps can already have committed material). Existing strict fixtures expose the gap.
- StorageManager owns dirty state, slot-keyed queue admission and deferred dispatch. Calls do
  not write SD. Queue lookup is capped by its existing 256-entry table; heap sampling reads
  the arena tail, not an allocator walk. No new per-job or Track bytes are required.
- Publication already changes canonical revision; its phase transition is the once-only receipt.
  Retrying a denied transfer leaves AtomicTransfer intact. Later cleanup grains must not re-request.
- Existing queue coalesces Queued/Writing keys. This slice proves handoff, not durability of an
  already-writing body or the entire save scheduler. Persistence-in-flight correctness remains
  an explicit parent gate; no claim of revision-pinned on-card content follows from queue state.
- Decision review searched stop-directed admission, interval ownership and persistence handoff:
  DEC-067, DEC-068, DEC-070, DEC-078, DEC-093. Extend Track's worker and call StorageManager;
  no responsibility transfer. G0 PASS for this handoff slice.

## G1/G2 — implementation contract and adversarial review

Reuse the FLASH-resident `requestLoopSlotPersistAndSaveState` helper to submit the validated job slot through the existing
mark/admit/request API, only after a successful atomic transfer, before returning that grain.
All atomic routes must call it, including superseded and stopped recovery. No selected slot
lookup, no dispatch/save, no caller-side second request, no extra completion flag.

COVERED: slot/track focus and successor capture do not change the job identity; stale LoopId or
geometry parks before publication; false transfer does not admit; DeferredCleanup cannot replay
AtomicTransfer; transport retarget uses the same success boundary. Empty tails retain existing
synchronous persistence semantics for earlier committed wraps. Undo/bridge/pass ordering remains
inside TrackPreparedStopCommit; this helper runs after that transaction succeeds.
TIGHTENING: queue state alone coalesces duplicate admissions. Tests must acknowledge the queued
items and clear the request at an I/O boundary before stepping cleanup, then assert no new work.
TIGHTENING: native tests prove no synchronous SD writes, correct requested identity and once-only
handoff; device timing, queue saturation, persistence in flight and shared Record publication
remain parent integration gates. G2 PASS WITH TIGHTENING incorporated below.

## Fixed oracle before firmware edits

1. Existing four rotations assert dirty + LoopPersist queue for both modes unconditionally;
   remove only the known missing-persistence exception. Record deferred-ownership assertions stay.
2. Real OD job A on slot 0; live B on slot 1; focus may move. Step real maintenance to the
   AtomicTransfer boundary. Before successful publication: neither slot dirty, no queue/request.
   After publication: A's canonical pass + dirty + SlotMeta/LoopPersist + save request, B untouched.
3. Acknowledge real queued items at the persistence I/O boundary; clear pending/dirty observation.
   Step remaining cleanup and repeated idle drives: A stays acknowledged, no second admission.
4. Stopped destination and superseded causal-base repeat the same oracle. A stale job identity
   cannot publish or admit; restore identity and the original request may complete once.

Use existing native clock, real Track/Loop/worker/undo and StorageManager/PersistenceWorkQueue
from `test_shared_capture_finalization`. No state-owner stand-in on assertions. Existing
phase credits plus the 20,000-grain fixture watchdog bound drains and print last state.
Focused `pio test -e native -f test_shared_capture_finalization`, full `pio test -e native`,
`pio run -e teensy41-capture-serial` (RAM1 padding >=840), strict OpenSpec and diff checks.
Reused registered `STIM-BASE-2-ONE-OD-01@v1` is the eventual device baseline; no device result
is inferred from native queue admission. Firmware upload/device runs are not part of this checkpoint.

## Implementation findings and verification in progress

The stale-owner fixture exposed an additional identity hole: the existing worker compared the
job with Track's slot-to-LoopId map and pointer, but not the Loop object's own LoopId. The worker
now checks all three before advancing. This enforces the existing contract; it adds no identity
owner. The fixture restores its injected LoopId before asserting, even on pre-fix failure.

Pre-fix run: three new publication cases and three existing rotations fail missing dirty
admission; the stale fixture additionally fails (AtomicTransfer advanced to DeferredCleanup).
The failing native process also trapped at exit; that run is red evidence, not a clean baseline.
After the firmware change: **15/15** production fixture cases pass (6.981 s). Full native and
capture-serial results pending below. Registered stimulus list confirms
`STIM-BASE-2-ONE-OD-01@v1`; it was not edited or run on hardware.

The final implementation reuses `requestLoopSlotPersistAndSaveState`; no new Track helper or
field remains. Review found that the load-only suite also retires a real stop job before its
restore retry. Its previously absent persistence boundary first failed linking, then the
fail-closed probe demonstrated that it is reachable. The load fixture now observes exactly one
outgoing request for that Track/slot. It does not stand in for persistence admission evidence:
that is asserted against the real helper, StorageManager and queue in the publication suite.
Affected suites: **61/61** (load 35, publication 15, library observation 11), 11.569 s.

Final capture-serial build: PASS, 49.012 s. RAM1 variables 96,160; code 392,376;
padding **840**; local allowance 34,912. RAM2 variables 155,232; EXTRAM variables 9,312.
All equal the baseline. FLASH code is 1,041,280 (+80 bytes relative to `e108944f`).
No data-field or layout growth. No upload, device timing or on-card durability claim.
Full native final run and strict contract results follow at completion.

Review/closeout: existing DEC-093 implemented; no new owner, policy, undo meaning or exclusion.
Execution bounds and the deferred-persistence guide updated. Overlap/ledger logic unchanged.
Deliverable shipping status unchanged: no device release. All parent checkboxes remain open.
Earlier unrelated HITL edits are preserved outside this local checkpoint.

Full-run harness follow-up: the final helper reuse exposed the same missing outbound symbol in
legacy continuous capture. That suite already isolates dirty/admit/request; the new helper uses
the same explicitly inert boundary under its existing storage-family guard. Suites claiming
persistence opt out and link the production helper. Continuous-capture regression: **104/104**,
6.502 s. No production change after the final firmware build. Earlier full runs with link errors
are not counted as passing. The final complete rerun includes both harness corrections.

## Final verified checkpoint

- `pio test -e native`: **2,501 passed / 7 skipped**, 80.769 s; no failed or errored suites.
- Final `pio test -e native -f test_shared_capture_finalization`: **15/15**, 1.064 s.
- Strict executable (`S4F16_REQUIRE_CONTRACT=1`): **12 passed / 3 expected failures**, exit 3.
  OD→OD now passes including each job's dirty/queue handoff. R→R and R→OD still fail missing A
  deferred ownership; OD→R still fails B's accepted deferred-stop obligation. No assertion waived.
- Capture-serial: final build/memory values above; no production changes since that build.
- `openspec validate runtime-working-set-ownership --strict` and `git diff --check`: pass.
- Native ring remains 3,984 bytes / job 984; device layout pins pass. No resource fields added.
- No hardware upload, MIDI timing run, SD write or device acceptance. The registered stimulus
  is reserved for the connected integration gate; this checkpoint is not device-qualified.

S4f.20 remains unchecked for mode-correct record undo/publication, persistence-in-flight and
exact on-card content. S4f.17–S4f.21 remain open as recorded in tasks.md. Next connected firmware
work is bounded frozen Record source preparation and its first-bar/onward playback handoff;
record admission cannot be enabled safely until those consumers and continuations are ready.
