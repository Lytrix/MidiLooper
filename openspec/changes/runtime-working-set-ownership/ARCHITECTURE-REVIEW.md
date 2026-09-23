# Execution packets and phase review — runtime-working-set-ownership

Authority: [G2 execution-packet workflow](../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice),
[IMPLEMENTATION_CONTRACT.md](IMPLEMENTATION_CONTRACT.md) (DEC-079), and the stage-specific tasks.
This file makes acceptance executable without changing the approved runtime behavior.

**Status: P0 specified and partly evidenced; P1–P8 specified only.** P0's S1.2 characterization is
implemented and its native red/green results are recorded under *Implementation review and evidence
record* below, together with the S0 decision verification. No P1–P8 fixture, native, build or device
result is claimed there, and every P1–P8 task remains unchecked. Use one packet per reviewable slice;
follow dependencies rather than numeric task order. Later architecture-gated work remains blocked until
its own contract exists. Do not implement the umbrella in a single pass.

## Architecture gate — common to every packet

Before the packet's first production edit, record in the session:

- Owner module and production entry/worker/commit: use the packet and linked task contract.
- Primary invariant and DEC: cite the relevant task and handoff section.
- Ownership/state-transition/execution-bound changes: name those already approved for this packet;
  any additional change follows G3, not a fixture workaround.
- Behavior-preserving: measurement/harness packets yes; behavior packets no, confined to listed outcomes.
- Reuse decision: existing owner and existing production-path harness; no copied lifecycle implementation.
- Phase scope: packet ID, task IDs and actual files to edit, including prerequisite test seams.

The following common controls and evidence fields are part of every applicable packet, not a separate phase.

## Native fixture contract and repeatable verification

Existing production-path harness:
`test/test_continuous_capture/test_continuous_capture.cpp` includes the Track lifecycle, deferred maintenance,
prewarm and manager implementation; `manager_capture.cpp` includes manager capture entry points.
`TrackCaptureStopTestAccess::step` already advances a stop grain; its queue/interval accessors expose ownership.
`test/test_playback_prewarm/test_playback_prewarm.cpp` covers runtime capacity, ordering and prepared-stop owners.
Reuse these paths. New fixture IDs below are **planned assertions**, not existing tests or evidence.

Required harness extensions, before the behavior they verify:

- Controllable native tick and transport-running state. Replace the harness's always-true
  `ClockManager::isTransportRunning()` boundary stub with test-controlled state; reset it in setUp.
  Where epoch retargeting is under test, call the actual production retarget/epoch entry rather than
  merely changing the fake clock. Add production transport/slot/load units when a case requires them.
- Pause by withholding a selected owner turn, then step the real grain or real deferred dispatch explicitly.
  Direct worker stepping proves worker logic; a separate dispatch case proves scheduler reachability,
  especially all-stopped/non-focus tracks. Never claim the first proves the second.
- Deny/re-enable the relevant canonical reservation through a test-only allocator/admission boundary or
  real pool-credit exhaustion. Do not mock `beginDeferredOverdubStop`, publication, liveness or load guards.
- Read-only census of `(track, slot, interval, generation)`, pending requests, live ring entries, raw ranges,
  canonical chunk refs, publication/undo commit counts and allocation counts. Extend existing test access;
  no runtime telemetry allocation or new production owner. Unsupported on-path stubs must fail the test.
- Fixed MIDI input with independently enumerated expected committed note rows. Bounded drains use the
  declared grain bound; report last phase, owner and pending identities if exhausted. No sleeps.
- Direct private slot mutation can prepare a fault injection, but cannot establish that a production switch
  or button path works. The acceptance case issues its action through the production entry point.

### Recipe R — native/build

From the repository root, run these commands in order for each firmware packet (reuse focused suites only
when applicable; record omissions with reasons):

```sh
pio test -e native -f test_continuous_capture
pio test -e native -f test_playback_prewarm
pio test -e native
pio run -e teensy41-capture-serial
```

If the packet introduces a suite, add its exact `pio test -e native -f <name>` command to this block before
implementation checkoff. Verify it is selected by the native environment. Required cases must actually run.
Pass requires exit 0, expected suites/cases present, no unexpected failures/errors/skips, and the packet's
assertions satisfied. Record case-count changes; do not freeze a stale repository-wide count. Preserve a
known baseline exception only under its exact case ID/assertion and remove it when the responsible fix lands.
A compiler failure cannot serve as the pre-fix behavioral failure. Record RAM1/banks/padding versus baseline.

### Recipe H — device

For packet-specific device cases, use the authoritative preset or stop-interruption wrapper and the
`hitl-content-verification` inventory/playback pipeline. First list registered stimuli:

```sh
.venv/bin/python scripts/host_midi_hitl.py stimulus list
```

Record the selected stimulus or TEMP parent/suffix, complete invocation, content oracle and timing verifier
before running. Tasks S4b/S4f/S7 define the sequences; do not invent a new runner, replay command.json, edit a
canonical stimulus, or mutate device SD without session authorization. Setup failure is inconclusive, not pass.
The commands in the HITL skill are the executable source for device setup and verification, not guesses here.

## Per-phase gates

### P0 — S0 decisions and S1 measurement

Owner: documented role owners and per-owner accounting, no runtime ownership mutation.
Preconditions: DEC-076/078/079 and current Config constants; current resident library, not assumed 60 long slots.
Files: tasks/design evidence plus existing accounting/capture diagnostics; instrumentation stays gated.
Mutation/commit: one characterization slice with fixed input traces and owner-attributed measurements.
Non-goals: boot reservation removal, latch/pool implementation and playback decoupling.

Oracles: M01 records inactive-slot growth with active-role count held constant; M02 separates canonical payload,
canonical reservation, roots, undo and derived bytes; M03 records concurrent track/role holder counts and RAM1.
Record the observed failing memory-law assertion as red characterization with its exact assertion and input,
not an unexplained failed native suite. Synthetic 60-descriptor and real resident-library observations are distinct.

Gate: recipe R when accounting firmware changes; strict OpenSpec validation for decisions only; recipe H for
measured slope. Evidence includes sizes, counts, density, loop lengths, failure point and original trace path.
This is measurement evidence, not proof the final capacity fits.

### P1 — S2b first boot/admission slice

Owner/files/scope: [handoff §1](IMPLEMENTATION_CONTRACT.md#1-executable-first-slice-bootload-canonical-admission).
Preconditions: P0 evidence and approved S2b.0 prerequisite closure. S2/F/S6 are not prerequisites.
Commit boundary: fixture controls and all S2b.0–S2b.6 behavior together; no boot-removal-only intermediate release.
Outcome vocabulary: accepted stop retains its identity while waiting; admission consumes that obligation once;
publication and retirement remain distinct; epoch change retargets destination; load wait retains its request.
Non-goals: full switch/start supersession, reusable pool, cold undo, 60-long-slot content residency.

Pin these fixtures before behavior edits:

- **B01 boot cold reservation:** create N committed inactive slots, record canonical payload independently,
  call production prewarm → canonical-copy reservation delta is zero as N grows; no transfer horizon admitted.
  On today's code the unconditional canonical reservation is the discriminating failure.
- **B02 loaded-slot stop waits:** load/prepare committed content without record arm → start overdub → append a
  known note pair → deny canonical-copy credit → issue Stop once → request remains accepted, same live interval,
  no partial seal/job or publication. Step under denial → same ownership/content. Restore credit → step admission
  → exactly one job bound to the original slot. Drain → exact committed rows and one stop/undo publication.
- **B03 transport retarget:** stop latched under credit denial → production transport Stop → advance epoch →
  restore credit/drain → no TRACK_PLAYING transition and no request loss. A fake running-clock toggle alone fails
  coverage; the production retarget route must be linked.
- **B04 load retained:** queue restore for a job-owned slot → run real load selection/admission → request still
  queued, no reset of that Loop. Retire owner → run selection again → request consumed once and restored content
  exact. Link the real queue/pop/admission path; a stubbed load function returning “deferred” proves nothing.
- **B05 reservation release:** pause canonical copy mid-grain → attempt release → retained refs/credits and copy
  progress intact; complete/retire → release twice → second release has no refcount effect.
- **B06 dispatch reachability:** retain an accepted stop on a non-focus track while another plays, then repeat
  with all transport stopped → run the real deferred eligibility/dispatch path → stop eventually gets a grain.
- **B07 no action-edge allocation:** count allocations around the actual Stop action only, then around deferred
  retry separately → action count zero, permitted deferred allocation attributed to canonical admission.

Gate: recipe R, B01–B07 all present and passing; recipe H's resident-library one-stop recovery and reservation
slope evidence, synthetic 60-descriptor boot fixture. Keep final 60-long-slot and total-residency acceptance open.

### P2 — S2 accounting, S3 and S4 lease identity/release

Owner: TrackPlaybackRuntime lease storage and Track liveness; primary files from S3/S4; extend P1 helpers.
Preconditions: P1 verified; S2.2/S2.3 sizes/cardinalities measured and the proposed pool envelope actually fits.
S2.1/S2.5/S2.6 behavior stays blocked on phase F. Commit: pool identity/release/reuse with its fixtures.
Non-goals: broadening admission concurrency, denying accepted interactions to make capacity fit, root decoupling.

L01: A acquires → job pins A → A ceases sounding → release denied without mutation → job retires → release →
C reuses capacity → late A cleanup → C's generation/content/refcounts unchanged. L02: browsing acquires nothing.
L03: each liveness term independently prevents release/load; retired ring slots outside live window do not.
L04: simultaneous tracks/overlap counted under handoff §2; observe constant inactive derived residence and no
net growth for recycle. Existing APIs/reset/ledger callers must exercise these checks, not only the new helper.

Gate: recipe R plus `test_runtime_working_set_ownership` once added by S3; recipe H holder census/reuse trace.
No fit or missing counters means budget unproved, not permission to shrink maxima silently.

### P3 — S4b, S4.5 and remaining S4f accepted interactions

Owner: Track interval stop and TrackManager pending switch/start, SlotStateMachine target/commit.
Files/changes: S4b/S4f lists; P1 Stop path is extended, not duplicated. Preconditions: P2 ownership and occupancy.
Commit: one coherent acceptance/transfer slice; state shape and supersession are fixed by handoff §5.
Non-goals: unbounded request history, additional finalizer capacity, progress UI S4f.12.

I01–I07 correspond one-for-one to handoff §5's seven transition bullets. For each, pause workers, issue actions,
assert retained requests/immutable interval slot after every action, then step provisioning/admission/commit.
Assert ordered exactly-once execution or the explicitly specified supersession event for every request.
I08: demoted short and long targets → off-clock provisioning → no trySlot/allocation at switch commit.
I09: different tracks retain independent jobs; one cannot consume the other's requests. I10: park diagnostic
terminal → later resource recovery → accepted stop still executes once. I11: production dispatch visits pending
start/switch work even with no live interval; cancellation/target replacement retires unpublished occupancy.

Gate: recipe R and recipe H for S4b.8/.9 and S4f.13/.14. Record latency by interaction class (S4f.11), comparing
only with declared numeric budgets; undefined budgets remain measurement findings and cannot be called met.
S4f.12 stays deferred until its own mitigation-review condition is satisfied.

### P4 — S5 cold undo, demotion and runtime regeneration

Owner: TrackUndo reconstruction, Loop canonical rows/published authority, Track retained stop job.
Files/preconditions: S5 tasks, P2/P3 identity/request semantics; handoff §3/§4; current playback root still required.
Commit: separate reviewable sub-slices for S5.2f regeneration and S5.2a–e/g undo/demotion, using the same packet
controls with their own evidence entries. Non-goals: changing LSV1 or removing the active playback root.

U01: demote A → roots drop except live pins → reactivate → exact current content → Undo/Redo → exact expected
historical/current rows and truthful coverage. U02: pause cold undo → push/trim/load/switch/second undo → original
entry identity intact, no skipped older undo, eventual one commit. U03: exceed ClearSlot cap → whole entry evicted,
cursor repaired, no null-root undo wiping content. U04: budget pressure removes only reconstructable prefixes,
charges canonical pins separately and never evicts current/dirty content. U05: cold save/reload content parity.
R01: runtime loss before prepare → deny regeneration allocation → same ring owner/extent remains → recover →
one publication/undo. R02: loss after commit receipt → recover → cleanup only, no repeated publication.
R03: missing authoritative input → diagnostic fault with retained ownership, no empty pass or recycled entry.
Run dispatch reachability for stopped/non-focus reconstruction as well as direct worker stepping.

Gate: recipe R plus recipe H cold undo/save/reactivation and retained-job recovery where injectable on device.
Native fault injection proves logical preservation; no claim that it measures device allocation latency.

### P5 — S4c dependency contract and phase F

Owner: architect for dependency definition; this change implements no publication-unit switch.
Preconditions: existing root/consumer inventory. Commit: contract-only S4c evidence. Gate: strict OpenSpec
validation and source-cited inventory of the five gather sites/checkpoint consumers; no firmware tests needed.
Phase F implementation, S2.1/S2.5/S2.6 and S4d playback integration are **blocked** until the planned sibling has
its own approved contract/DEC and execution packet. Do not create green placeholder acceptance for missing work.

### P6 — S4e visual tier and S2.8 cap accounting

Owner: Loop envelope/cache validity, DisplayManager consumption, existing idle renderer.
Preconditions/files: S4e named paths and P2 liveness; S2.8 measured cap and approved eviction policy before mutation.
Commit: envelope/detail cap/released-state slice. Non-goals: new renderer, new validity counter, root-free playback.
V01: cold selection immediately reports correct slot envelope, no lease churn; deferred detail replaces it only
at matching identity. V02: mutation while build paused → stale candidate never published. V03: cache release →
explicit released/dirty state → fallback then rebuild (not clean-empty forever). V04: fill/evict repeatedly at
fixed cap → bytes bounded including nested capacity, no stale frame after switch. Gate: recipe R, relevant
existing display native suite identified in the packet before editing, and recipe H S7.4d visual/cap evidence.

### P7 — S6 persistence-backed canonical residency (blocked on architecture)

Owner: Loop content and StorageManager durable generation acknowledgment. This is **not Apply-ready**: S6.0
must choose and approve the residency policy and its DEC before creating its concrete code packet.
Required oracles are fixed: dirty G → pressure → pinned; durable ack G → only G evictable; G+1 still dirty →
old ack does not unpin; storage failure → accepted content retained; eviction/reload → byte/content parity.
The later packet must name the actual I/O acknowledgment seam, eviction boundary, resident byte cap and failure
outcomes. Gate stays the S6 architecture amendment plus native fault tests and real 60-slot load/reload evidence.
A “choose policy” task is design, not permission for the Apply agent to invent one during firmware edits.

### P8 — S4d standby, S7 acceptance and closeout

Owner: existing playback readiness/standby producer (defined by F) and measurement harness.
Preconditions: F and S6 implemented/verified for standby integration/final scaling; S2.7 cap policy fixed.
Commit: standby implementation is its own packet instantiated from F's owner contract, then separate acceptance
and documentation closeout. No publication authority can be chosen here while F is absent.
Oracles: capped candidate residency; mutation invalidates standby; stale excerpt never played; inbound readiness
before switch; S7.1–S7.6 exact scenarios, including per-owner census and the continuous/cross-track session.
Gate: recipe R for firmware changes, recipe H for final runs, all required content/timing outcomes asserted,
no missing counts or unperformed scenario reported as pass. Archive only after all dependencies/gates close.

### Safeguards and optional work

G.2 failed arm admission uses P1/P3 controls: failure retains ARMED and input intent, no partial authority;
retry after capacity recovery completes once. G.3 companion-buffer splitting uses P2 capacity/ownership controls
plus exact reconstruction output parity; it is a separate sub-slice, not unrelated cleanup inside P1.
G.4 is an exclusion, not implementation. S4f.12 progress presentation remains gated; its eventual packet must
assert accepted-action status without altering budgets or suggesting a second press. These inherit the workflow
when authorized; no new permission or implementation scope is inferred from this coverage list.

## Implementation review and evidence record

Before checking off any packet, record task IDs, revision/dirty context, actual command, required case IDs,
exit/count/skip/error results, build RAM1/banks, and device stimulus/log/content/timing evidence where required.
Place actual records below under packet ID; until then the status above remains specified.

Reviewer checklist for each packet:

- Oracle came from approved behavior and was fixed before production mutation; counterexample distinguished.
- Production action, dispatch and commit paths exercised; relevant on-path success stubs removed.
- Pending request and resource identities verified across waits, failure, recovery and supersession.
- No missing suites, weakened expectations, invented capacities or unmeasured timing guarantees.
- Task completion matches its own evidence; device/dependency gates still open are stated explicitly.
- Durable docs/ExecutionBounds and required decision updates match shipped behavior.

## P0 — S0 decisions and S1 measurement (2026-09-23)

### Packet

- **Packet/task IDs and approved contract:** P0 (D0.1), S0.1–S0.7, S1.1–S1.5; preconditions of
  [handoff §1](IMPLEMENTATION_CONTRACT.md#1-executable-first-slice-bootload-canonical-admission);
  DEC-076 (points 1, 2, 8), DEC-078, DEC-079, DEC-080; invariants 1, 25, 34.
- **Status:** S0 decisions **verified (documentation)**; S1.2 **fixture implemented + native red
  characterization recorded**; S1.1/S1.5 **partly** evidenced; S1.3, S1.4 and **every device leg
  NOT RUN** (no hardware in this session).
- **Owner and production entry → worker → commit:** no runtime ownership mutation in this packet.
  The measurement drives the production boot/load admission
  (`TrackManager::prewarmPlaybackRuntime` → `Track::prewarmPlaybackForSlot(s, false)`) and the
  production active-role admission (`Track::prewarmPlaybackForSlot(s, true)`, the record-arm route)
  from the existing production-path harness.
- **Primary files:** `test/test_continuous_capture/test_continuous_capture.cpp` (fixture plus a
  read-only `TrackCaptureStopTestAccess` extension). **No firmware file is touched**, so the
  firmware artifact is unchanged by this packet.
- **Preconditions present:** DEC-076/078/079 and the current `Config`/`PassConfig` constants; the
  current supported resident library. **Not** a 60-long-slot library (S0.5/S1.3).
- **Prerequisites included in this slice:** none.
- **Mutation / postconditions:** a native characterization exists and is reproducible; nothing
  runtime-visible changes.
- **Explicit non-goals:** boot reservation removal, the latch/interaction pool, playback
  decoupling, S6 canonical residency, and any claim about the 60-slot envelope.
- **Commit boundary:** measurement and characterization only; independently buildable.

### Oracle fixed before behavior edits

**M01 — working-set residence at a constant active-role count (S1.2; invariants 1, 25, 34).**
Initial state: fresh `TrackManager` and `LoopEventStore` pool; `recordedSlots` ∈ {1, 4} small
committed record passes on the selected track; no transport active. The active set is one slot in
both variants, and the `TrackManager` default enabled set is identical in both, so it cancels.
Ordered actions: commit content → record committed payload → `prewarmPlaybackRuntime()` →
`prewarmPlaybackForSlot(0, true)` → census.
Observations **after each step** are recorded, not inferred: the payload is content-proportional,
and the two law assertions are:

- `reservedChunks(1 slot) == reservedChunks(4 slots)`, with `reservedChunks = admitted − payload`;
- `residentWorkingSetSlots(1 slot) == residentWorkingSetSlots(4 slots)`.

Counterexample that must fail (and does today): `reserved 96 → 120`, `residents 8 → 11`.

**M02 — canonical payload separated from the reservation (S1.1/S1.5 input).** The fixture records
the committed payload *before* admission and the admitted total *after*, so the boot reservation
slope is read separately from the content it copies. Observed natively: payload 1 → 4 chunks
(content), reservation 96 → 120 chunks — a slope of exactly **8 chunks per admitted boot slot**,
i.e. `PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS` reserved by
`LoopPlaybackRuntime::prewarmCanonicalCaptureOutput`. `reserved` for one committed slot is 96
because `TrackManager`'s default enabled set is slot 0 on each of the 8 tracks
(`TrackManager.cpp:39`), so eight slots are admitted at boot independent of recorded content; the
count is identical in both variants and cancels in the comparison.

### Controls and coverage

- **Seams reused:** the `test_continuous_capture` production-path harness (Track/manager lifecycle,
  prewarm, real `Loop` capture/seal/commit) plus a read-only `TrackCaptureStopTestAccess` extension
  (`runtimeAllocated`, `hasResidentWorkingSet`). No mock, stub or copied lifecycle sits on the
  asserted path; `hasResidentWorkingSet` reads only existing public state
  (`viewSetIfPrewarmed`, `liveCaptureStagingIfPrewarmed`, `canonicalCaptureOutputAdmitted`).
- **Busy/stale/failure/recovery controls:** not applicable to a boot-time measurement — the
  denial/recovery controls belong to P1 (B01–B07).
- **Independent oracle:** the expected relation comes from the law (invariant 34 / DEC-076 point 1)
  and from `PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS` (8), not from the implementation's own output.
- **Baseline failure:** reproducible behind `WORKING_SET_RED=1`. The default gate keeps only the
  well-formedness case, so a repository-wide `pio test -e native` stays green while the
  characterization cannot rot. **Removal condition:** delete the gate and promote both law cases
  into the default gate when S2b.1 (and the S3/S4 release path) lands — RECIPE R, "a known baseline
  exception only under its exact case ID/assertion".
- **Bounded drain / fuzz:** none needed; no deferred service runs and there are no sleeps.

### Verification recipe

```sh
pio test -e native -f test_continuous_capture              # default gate; must be green
WORKING_SET_RED=1 pio test -e native -f test_continuous_capture   # the red characterization
pio test -e native                                        # full native
pio run -e teensy41-capture-serial                        # RAM1 line
```

### Completion record

Baseline revision `7f90d996`, clean tree, **before** any edit: `test_playback_prewarm` 179/179;
`test_continuous_capture` 77/77; full native **2,262 cases / 1 skipped / 2,261 succeeded / 0 failures**.

| check | actual command | result |
|---|---|---|
| default focused | `pio test -e native -f test_continuous_capture` | **PASSED, 78/78** (+1 well-formedness case) |
| red characterization | `WORKING_SET_RED=1 pio test -e native -f test_continuous_capture` | **2 failed, 78 succeeded**: `test_working_set_residence_does_not_grow_with_recorded_slots` — `Expected 96 Was 120`; `test_inactive_committed_slot_retains_no_resident_working_set` — `Expected 8 Was 11` |
| full native | `pio test -e native` | **2,263 cases / 1 skipped / 2,262 succeeded / 0 failures** |
| firmware build | `pio run -e teensy41-capture-serial` | **SUCCESS**; RAM1 `variables 96,128 / code 392,344 / padding 872 / locals free 34,944`; FLASH `code 1,009,608` |

- **Oracle/contract unchanged**, or authorized change and supporting evidence: unchanged.
- **Fixture implemented and discriminating assertion demonstrated:** yes — the two failures above
  are native assertion failures carrying the measured numbers, not build errors.
- **Native verification:** S1.2 satisfied. S1.1 and S1.5 partly evidenced (the reservation slope and
  the admission path); the per-owner byte census (views, staging, companion
  reconstruction/apply/bridge-map/passes, canonical inputs, transfer horizon, stop-job capacity,
  prepared pair), per-loop derived bytes (snapshot slabs, whole-loop `VisualCache`) and the lease
  holder count are **NOT RUN** here — they need the device/S1.4 pass.
- **Build/RAM1 verification:** done. Only a test file changed, so the firmware artifact is
  unaffected and `padding 872` is the recorded line.
- **Device verification:** **NOT RUN** (S1.3 slope and failure point, S1.4 undo/canonical-pin
  baseline, and every device leg of the S1 gate remain open). Setup failure is inconclusive, not pass.
- **Required reviews and durable docs:** predecessor review recorded below (S0 verification).
- **Task IDs eligible for checkoff:** S0.1, S0.1a, S0.2, S0.3, S0.3a, S0.4, S0.5, S0.6 (with the
  observation below), S0.7, S0 gates; and **S1.2**. D0.1, S1.1, S1.3, S1.4, S1.5 and the S1 gate
  stay open.
- **Stage gates still open:** S1 device legs, S1.4, D0.2–D0.5, and all of P1/S2b.

### S0 decision verification (P0, D0.1) — verify, do not re-decide

Every S0 item was checked against its landing site; the search covered `docs/DECISION_LOG.md`,
`docs/Authority/NAMING.md` and this change's artifacts, and the predecessors named by `tasks.md`
(RC-MEM-2, RC-MEM-3, RC-MEM-5, `immutable-loop-snapshot-passes` §5.13) were found at their
DEC-076 cross-references. **No code change is required by any of them.**

| task | recorded at |
|---|---|
| S0.1 role tiers | DEC-076 point 1; `design.md` Revision 2 and Revision 12 (F5: the tiers are a view of `activeLoopIndex`/`CapturePhase`/`TrackState`) |
| S0.1a role classes and cardinalities | DEC-076 point 2 as amended by DEC-078 items 5–7 (finalization is per `(track, slot, interval generation)`, `LiveCaptureLease` 1 per track, no queue of stopped intervals) |
| S0.2 lease identity | DEC-076 point 1 (`owner + generation + role`); `design.md` Revision 3; invariant 6 |
| S0.3 two-tier cold undo | `design.md` Revision 4, superseded in part by Revision 18 § 7 (pinned canonical bytes charged, ClearSlot excluded, missing body defers); DEC-076 point 7 |
| S0.3a eviction priority | `design.md` Revision 4 (§ eviction order) and Revision 18 § 6 |
| S0.4 canonical residency is a gated phase | `design.md` Revision 7; S6.0 requires its own DEC amendment |
| S0.5 topology pin | `tasks.md` S0.5 and `design.md` Context (`NUM_TRACKS = 8` × `MAX_LOOPS_PER_TRACK = 8` = 64 slots, `runtimeBySlot_`); `PROJECT_STATE.md` carries the same N-derived statement |
| S0.6 resumable work-item contract | `design.md` Revision 16 (the contract) and DEC-076 point 8 (the vocabulary is part of the decision); the normative definition is `NAMING.md` § Pipeline of role-owned stages |
| S0.7 acceptance + per-interval finalization | DEC-078 (accepted, amending DEC-067 and DEC-076 point 2) and `design.md` Revision 18 § 1–2 |
| S0 gate | decisions recorded; no production code change in this packet |

**Recorded observation, not a blocker:** S0.6 asks for the work-item contract "as a convention plus
`DEC`". There is **no dedicated `DEC-###` id** for the work-item convention itself — the binding
decision is DEC-076 point 8 together with Revision 16 and `NAMING.md`. Recorded here so a reviewer
can require a dedicated id if the vocabulary must be citable on its own; nothing in this change
depends on one, because the contract is header-only vocabulary with no new owner.


## P1 — S2b first boot/admission slice (2026-09-23, controls and B01 only)

### Controls implemented (D0.2, partial) — test file only

- **Controllable transport state.** The harness's always-true `ClockManager::isTransportRunning()`
  boundary stub is now test-controlled (`nativeTransportRunning`), reset in `setUp`. A case can stop the
  transport and then drive the production retarget/epoch entry instead of only moving the fake clock.
- **Reservation denial/recovery by real pool-credit exhaustion.** `PoolCreditControl::denyAdmission(keepFree)`
  holds real `LoopEventStore::ChunkReservation` credits, so a canonical reservation cannot grow (a failed
  `reserveChunks` rolls back to its prior credits); `restore()` returns them. Nothing mocks
  `beginDeferredOverdubStop`, publication, liveness or the load guards.
- **Read-only ownership census.** `TrackCaptureStopTestAccess` gained `runtimeIfAllocated`,
  `canonicalReservedChunks(track, slot)`, `canonicalOutputEmpty`, `canonicalOutputAdmitted`,
  `liveRingEntrySlot(index)` / `liveRingEntryCount`, `slotOfActiveLoop`, `transferHorizonAdmitted`,
  `runtimeAllocated` and `hasResidentWorkingSet`. All read existing production state; no new production
  counter, owner or allocation.
- **Not implemented, and not implied by the above:**
  - the worker/dispatch pause-and-step control for B06;
  - the linked production load path (B04) and transport-stop-retarget path (B03);
  - the allocation census for B07. It is **not available natively**: the wrappers that feed
    `ExternalPoolAccounting` are `-Wl,--wrap=extmem_malloc/extmem_free` on
    `teensy41-capture-serial` only (`platformio.ini`), so B07's "no allocation on the action edge" needs
    either a device run or a test-only allocator boundary that does not exist yet. Recorded as a
    limitation, not as coverage.

### Oracle fixed before behavior edits

**B01 — boot cold reservation: implemented and RED.** With four committed slots on the selected track
and one active role (slot 0), the three committed-but-inactive slots each hold **8** canonical-copy
credits, totalling **24** over the inactive set, and a transfer horizon is admitted for them (today).
The law is zero for both, which is what S2b.1 must deliver. Run
`WORKING_SET_RED=1 pio test -e native -f test_continuous_capture`:
`test_b01_inactive_committed_slots_reserve_no_canonical_copy_or_horizon` — **Expected 0 Was 24**
(`total(inactive)=24 over 3 slots, per-slot 8/8/8 (active slot 0 = 8)`). The transfer-horizon clause is
only reached once the reservation clause is green.

**B02–B07 — specified, not implemented; recorded as missing-API limitations, not as red tests.**
B02/B03 need the slot-parameterised stop admission and the transport epoch introduced by S4f.0/S4f.4;
B04 needs the `StorageManager` load path linked into a native harness; B05 needs the S2b.4 release API;
B06 needs the dispatch control; B07 needs the allocation census.

### Verification

| check | command | result |
|---|---|---|
| default gate | `pio test -e native -f test_continuous_capture` | **PASSED, 78/78** |
| B01 + the two S1.2 law cases (red) | `WORKING_SET_RED=1 pio test -e native -f test_continuous_capture` | **3 failed, 78 succeeded** — `Expected 96 Was 120`; `Expected 8 Was 11`; `Expected 0 Was 24` |

- **Task state:** D0.2 **partial** (transport, denial/recovery and census controls in place; dispatch,
  load and allocation controls outstanding). D0.3 **partial** (B01 implemented and red; B02–B07 recorded
  as missing-API limitations). **S2b.0–S2b.6 are untouched — no production behavior has changed**, and
  the firmware artifact is unchanged because only a test file was edited.
