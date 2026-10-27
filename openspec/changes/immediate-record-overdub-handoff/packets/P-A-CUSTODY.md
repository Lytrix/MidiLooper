# P-A — custody, admission and the early-Stop input cut

G2 execution packet for [CONTRACTS.md](../CONTRACTS.md) C1.1–C1.7 and C2.1–C2.5.
Index: [packets](README.md). Template: [EXECUTION_PACKET](../../../../docs/Templates/EXECUTION_PACKET.md).

## Identity and scope

- **Packet / task IDs:** P-A. Implements C1 — independent sealed custody and early Stop; executes the
  C2.4 pre-flight (task 2.2.8). OpenSpec specs: `capture-state-guards` —
  *A successor overdub can stop while its Record predecessor finalizes* (both scenarios) and
  *Custody capacity is established before the overlap*. **DEC-159 — immediate first-overdub sealed
  custody** (direction, now contracted).
- **Status:** **step 1 applied and verified** (receipt + slab + size re-base, zero behaviour change);
  step 2 authorized by option A, implementation/verification pending — see the approval addendum below.
- **Owner and path:** `Track::beginDeferredOverdubStopForSlot` (admission edge) →
  `Track::stepOverdubStopAdmission` — the existing bounds-inventory **row 28** owner, instantiated
  here as one declared grain per turn — → `Track::stepDeferredRecordRetirement` and
  `Track::serviceCaptureStopPresentations` (retirement) → `Track::serviceCaptureStopDescriptorReservations`
  (bounded rearm). Service entry: `Track::processDeferredIdleMaintenance`
  (`src/Track/TrackDeferredMaintenance.cpp:900`), immediately after the accepted-interaction turn.
- **Primary files:** `include/Track.h` (receipt, slab, predicates), `src/Track/TrackOverdubLifecycle.cpp`
  (allocation site, admission, seal, release, retirement), `src/Track/TrackStateFacade.cpp`,
  `src/Track/TrackOverdubWrap.cpp`, `src/Track/TrackPlaybackHotPath.cpp`,
  `src/Track/TrackDeferredMaintenance.cpp` (one new grain).
  Harness: `test/test_immediate_capture_handoff` (existing suite, existing RED case).
- **Preconditions already present:** `Track::prewarmCaptureStopJobs` performs the single
  `ExternalMemoryFirstAllocator<CaptureStopJobs>::allocate(1)` placement-new;
  `LoopEventStore::canSealCaptureRange` / `Loop::sealCaptureRangeForJob` /
  `Loop::borrowSealedCaptureRange`; the four-entry job ring; `presentationGenerationFor`;
  `deny_at_retire` diagnostics. **Prerequisites in this packet:** the receipt, the slab, the
  interval-qualified predicate, the input cut. **External blockers:** none.
- **Mutation:** add `CaptureCustodyReceipt` (C1.1) and `custodies[kCaptureCustodyCapacity]` to
  `CaptureStopJobs`; add `Track::beginCustodyForInterval` / `releaseCustodyObligation` /
  `hasLiveOrSealedSuccessorForSlot`; make the admission predicate interval-qualified per the C1.4
  call-site table; cut input at Stop acceptance (C1.3); route release through the obligation mask
  (C1.5); add `custody_cap_denied` (C1.6).
- **Non-goals:** no undo/geometry/persistence semantics (that is P-C), no source preparation (P-B),
  no visual work (P-D), no second canonical writer, no guard deletion, no `presentations[]` removal
  from the ABI in this packet (it becomes a handle in step 2 only), no upload or content action.
- **Commit boundary — two independently buildable commits:**
  1. **Receipt + slab + release protocol**, with `presentations[slot]` still authoritative and
     admission unchanged. No behavioural delta; every existing suite must stay green. This is the
     commit that also runs the **C2.4 pre-flight** (slab declared in the image).
  2. **Interval-qualified admission + input cut + grain**, which is the behavioural change and the
     only commit that may move the RED cases to GREEN.

## Step 2 is a lease change, not a predicate edit

Recorded after step 1 landed, so the next run starts from the right subject.

Step 1 declares the slab and changes nothing. Step 2 as originally drafted ("make the admission
predicate interval-qualified, cut input at Stop, add the row-28 grain") is bounded work, but it does not
by itself make B's Stop admissible while A finalizes. That needs the **single finalization lease** to
become per-interval:

- `runtime->captureStopOwner` holds one owner, and a second admission is refused as `finalizer_busy`;
- `canonicalCaptureOutputAdmitted()` admits one canonical output;
- `stepDeferredRecordRetirement` pops the ring head and consumes that owner.

C1.4 asks for a serialized canonical *writer* with an admission *receipt per interval*. Converting the
lease is exactly the "a supposedly single finalisation lease turns out to need a second lifetime owner"
case, so it should be its own packet run with that conversion as the named subject — not folded into a
predicate edit. It stays native-verifiable: `IMMEDIATE_HANDOFF_CASE=stop` is the oracle, and `P-A-2`
(A, B and C sealed together) is the shape that proves the lease is per-interval.

**And the lease is not the first constraint.** Starting task 2.2.14 showed the blocking resource is *frozen
staging*: `RuntimePlaybackViewSet::captureStagings` holds exactly two roles (one live, one frozen), a sealed
interval keeps its frozen role through canonical ordering, and the live role is what lets a successor record.
So a successor can **record** after a Stop but cannot **stop** while the predecessor is unfinished — the
per-track gate is correct for the roles the firmware has. The trace needs four roles at ≈62,004 target bytes
each (+2 ≈ +124,008 B external per capturing slot), or a sealed range that carries the reservations the frozen
role holds. **Choose that before writing admission code**; [staging finding](../evidence/PA-STEP2-STAGING.md),
[C1.4a](../CONTRACTS.md).

## Oracle fixed before behaviour edits

Case IDs and steps are fixed now; no assertion may be written after seeing the result.

| Case | Initial state | Ordered steps | Expected after each step |
|---|---|---|---|
| `P-A-1` `test_early_stop_ends_B_without_reusing_A_custody` | A sealed, private (64 rows, gen1), no committed Record pass | Record button stop → direct `startOverdubbing` B at phase48 → accept pitch60 NoteId1 pair → Stop B at tick T → send one further pair | After Stop acceptance: B's interval endpoint latched, `captureInterval_` advanced, the further pair **does not** enter B (producer count unchanged). `A`'s 64-row tuple inventory byte-identical at every step |
| `P-A-2` | as P-A-1, B stopped | Start C, accept a distinct pair, Stop C, then drain A's worker | A, B and C each own a distinct receipt; three extents coexist; C's seal does not reuse A's or B's record |
| `P-A-3` custody cap | `K = 5` receipts occupied | attempt the 6th admission | denial token `custody_cap_denied`; accepted content retained; all five prior receipts still releasable |
| `P-A-4` stale receipt | A sealed, gen1 | release A; attempt to borrow with `(custodyIndex, interval)` of A | refused by the 64-bit interval/generation comparison (C1.2); no reinterpretation as the new occupant |
| `P-A-5` retirement overlap | A published, retiring with `kReader` outstanding | seal B | B seals into a free slot while A's receipt still holds the reader obligation; A's bytes stay readable until `clearPresentationOwner` |

- **Completion / exactly-once:** each receipt's `RecordPublished` edge is consumed once; a second
  drain leaves the ring count and the receipt state unchanged.
- **Preserved invariants:** one canonical worker and one canonical output; `activeLoopIndex` remains
  the sole activation authority; no private row ever becomes committed currency.
- **Must stay failing until step 2:** P-A-1 currently fails at "B remains live/appends after its
  requested Stop" — the producer grows 2 → 3. That is the RED this packet exists to flip.

## Controls and coverage

- **Seams reused:** the suite's real button/manager/Track/Loop/worker path with native clock,
  allocator and physical-I/O boundaries. No success stub, no copied state machine.
- **Production paths asserted:** button stop, `beginDeferredOverdubStopForSlot`, `sealCaptureRangeForJob`,
  `borrowSealedCaptureRange`, the retirement lane, the descriptor rearm. Direct
  `Track::startOverdubbing` is used where the manager route is already covered separately.
- **Cases required beyond the oracle table:** clear / slot switch / load / transport stop while a
  receipt is live; `queue_full`; `finalizer_busy`; `raw_range_unavailable`; `descriptor_rearm:deny`
  then a successful rearm; empty B (no extent) not taking a custody slot.
- **Independent expected content:** built from the seeded identities and explicit endpoints, not from
  a second copy of the resolution algorithm.
- **Numbers:** `K = 5`, `R_rec = 5 chunks` ([cap derivation](../evidence/CAP-DERIVATION.md)); receipt
  price 120 target bytes ([price annex](../evidence/CONTRACT-PRICE.md)). No numeric timing limit is
  asserted here.
- **Untestable before link:** the RAM1 stack high-water of the retirement lane and target allocator
  execution; they are the pre-flight, reported as measured, not asserted as thresholds.

## Verification recipe

```sh
# focused: the suite that owns the RED cases
pio test -e native -f test_immediate_capture_handoff
bin=/Users/eelkejager/Library/Caches/pio-build-250513-215524-teensy41/native/program
IMMEDIATE_HANDOFF_CASE=stop  "$bin"   # must move RED -> GREEN in step 2 only
IMMEDIATE_HANDOFF_CASE=start "$bin"   # unaffected by P-A; must not regress

# broad
pio test -e native                     # expect 3,454 total / 7 skipped / 0 failed

# cap pre-flight (step 1 commit; build only, never upload)
pio run -e teensy41-capture-serial          # link + RAM1/map figures
pio run -e teensy41-capture-serial -t envdump
python3 openspec/changes/immediate-record-overdub-handoff/evidence/probes/run-custody-probes.py \
  --out-dir "$PI_SCRATCH_DIR/custody-pa"
```

- **Pass criteria:** focused suite builds and runs; only the intended cases change state; full native
  0 failed; shipping build succeeds with RAM1 reported against the existing pins and the slab's
  608-byte delta attributed; the probe prints the receipt/slab rows unchanged from the annex.
- **Device:** NOT RUN by this packet. Timing and first-cycle behaviour need a registered stimulus and
  separate authorization.
- **Evidence path:** `evidence/PA-CUSTODY-EXECUTION.md`, recording command, revision, counts and the
  pre-flight figures; baseline `faa089b8` plus this packet's diff.

## Completion record

All fields are NOT RUN at specification time; none may be copied as a pass.

- Oracle/contract unchanged: pending
- Fixture implemented and discriminating assertion demonstrated: **PASS** for `P-A-1` —
  `IMMEDIATE_HANDOFF_CASE=stop` exits1 with `FAIL: B remains live/appends after its requested Stop
  while A finalizes` ([fixture receipt](../evidence/PACKET-FIXTURES.md)). `P-A-2..P-A-5` need the
  custody receipt to exist, so they are not yet expressible.
- Native verification: **PASS for step 1** — `pio test -e native` 3,454 / 7 skipped / 3,447 passed /
  0 failed ([execution](../evidence/PA-CUSTODY-EXECUTION.md)); step 2 is not implemented, so its cases stay RED
- Build/RAM1 verification: **PASS for step 1** — shipping build SUCCESS, post-link RAM1 identical to
  baseline (variables 96,192 / code 392,120 + padding 1,096 / locals 34,880); ring re-based 29,704 →
  30,304 (+600 = 5 × 120 measured) with the reason recorded at the pin
- Device verification: NOT RUN (out of scope)
- Required reviews and durable docs: NOT RUN
- Task IDs eligible for checkoff after all of the above: 2.2.8; P-A completes the C1 implementation
  half of 2.2 but **not 2.2 itself** (P-C is still gated)

## Step 2 approval addendum — option A

Owner explicitly approved **A — additive frozen staging roles** in this session. This resolves the
choice gate in the historical step-2 analysis above, not its implementation checks. Follow C1.4a and
DEC-161 — additive frozen staging roles: keep closure reservations in each role; provision independent
frozen roles before Capture admission; retain one canonical writer/output and generation/interval-qualified
release. Four total roles are the A/B/C+live minimum (+124,008 requested external target bytes), not proof
of the next-Stop or every K5 occupancy; actual cardinality and shell/staging deltas must be measured.

The existing fixed oracle and non-goals are unchanged. [Step-2 execution receipt](../evidence/PA-STAGING-LEASE-EXECUTION.md)
owns actual results. No claim of GREEN, device timing or completion follows from this approval. P-B,
P-C and P-D remain separate; do not waive their source, undo/empty or visual gates.
