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

## P1 — accepted-stop latch, drive and park implemented (2026-09-23)

The first production behavior of the change: **an accepted stop is retained and executes once when
the canonical-copy target returns**, instead of being dropped and needing a second press (DEC-078
items 1–3, invariant 31). This is the S2b.0 prerequisite set for the Stop path only; **the boot/load
reservation removal (S2b.1) is deliberately not in this step**, so phase A is not complete.

### What changed

| task | implementation |
|---|---|
| **S4f.0** slot-parameterised admission | `Track::beginDeferredOverdubStopForSlot(destination, tick, slot, interval)`; `beginDeferredOverdubStop(destination, tick)` is the live-edge wrapper passing `activeLoopIndex`/`captureInterval_`. Inside the body every `Loop`/runtime-derived guard and binding (`getLoop`, `slotIfAllocated`, the DEC-067 finalization chain, staging, canonical output, `job.slot`, the successor-window telemetry) resolves the **interval's** slot. A drive whose `interval != captureInterval_` or whose slot is out of range is a superseded request, reported as `stale_interval`, not a refusal of a user interaction. |
| **S4f.1** the latch (Stop variant) | `Track::PendingInteractionLatch {kind, destination, slot, switchTarget, interval, phaseTick, transportEpoch, waitCount, waitReported}` — one record per live interval, no descriptor, no ring capacity. A same-`(kind, slot, switchTarget, interval)` repeat is a no-op. `consumeAcceptedStopObligation()` clears **only** the matching stop obligation. |
| **S4f.2** dispatch | The branch sits in `processDeferredIdleMaintenance` immediately after the admitted stop's grain and before the owner-specific branches, factored into the named production entry `Track::serviceAcceptedInteractionTurn()` (driven only when the ring is empty, so an accepted request can never stack a second job). |
| **S2b.6** the park | `Track::parkAcceptedStopForCanonicalOutput(slot)` re-runs that slot's canonical reservation on a deferred turn (legal there, as `replenishCanonicalCaptureChunk` already shows), reports `accepted_stop_finalizer_wait` **once per episode**, counts every wait in `waitCount`, and never cancels the request. |
| **S4f.4** transport stop (partial) | `stopOverdubbingToStopped()` consumes the accepted obligation, because that path admits the stop itself or completes it synchronously; the deferred drive re-derives `Playing` vs `Stopped` from `clockManager.isTransportRunning()` rather than trusting the stored destination. |
| **S3.0** placement | Measured, not asserted — see below. |

### S3.0 placement evidence (the pin earned its keep)

The first implementation held the latch as `std::unique_ptr<PendingInteractionLatch>` beside
`captureStopJobs_`. That is only 8 B per track, but `Track` is an 8-element DTCM array, so it cost
**64 B of RAM1 variables and 16 B of the 872 B padding**: `variables 96,192 / code 392,360 /
padding 856 / locals free 34,880` — **below the documented floor**. The latch is now a value member
of the already-externally-allocated `CaptureStopJobs` object (`captureStopJobs_->interaction`), which
is one instance, not a ring descriptor, and consumes no `entries`/`count` capacity. Re-measured:
`variables 96,128 / code 392,344 / padding 872 / locals free 34,944` — identical to the pre-change
line. This is the S3.0 measurement the stage requires, and it is the reason the rule exists.

### Oracle and result

**B02 — accepted stop waits for canonical credit then executes once** (default gate). Load/prepare
committed content, start overdub, append a known pair, deny the slot's canonical-copy target through
the harness's existing `drainCanonicalChunks` seam, press Stop **once**:

- accepted and retained: same live interval, `hasAcceptedUnexecutedInteraction()` true, no job, no
  descriptor, unchanged committed rows and interval identity;
- the latch carries the interval's own `slot` and `interval`;
- stepping the real dispatch entry makes the park re-run the reservation and then the drive bind
  **exactly one** job to that slot; the accepted obligation is consumed (not re-armed);
- draining retires the job: the stop completes once and the content is committed.

Pre-fix run is **not possible** and is recorded as such (`IMPLEMENTATION_READINESS.md`: a new API
prevents it): before this change the denial path stored nothing, so `hasAcceptedUnexecutedInteraction()`
did not exist. The pre-fix behavioral evidence remains the still-green
`test_successor_stop_is_denied_when_the_canonical_copy_target_is_busy` and
`test_button_a_successor_stop_denied_until_deferred_role_available`, which assert exactly the drop
that B02 replaces. The discriminating assertion is B02's `hasAcceptedUnexecutedInteraction()` +
exactly-one-job + exactly-once-completion sequence.

### Verification

| check | command | result |
|---|---|---|
| focused | `pio test -e native -f test_continuous_capture` | **PASSED, 79/79** (+B02) |
| full native | `pio test -e native` | **2,264 cases / 1 skipped / 2,263 succeeded / 0 failures** |
| firmware | `pio run -e teensy41-capture-serial` | **SUCCESS**; RAM1 `variables 96,128 / code 392,344 / padding 872 / locals free 34,944` (unchanged) |
| residence red gate | `WORKING_SET_RED=1 pio test -e native -f test_continuous_capture` | **3 failed, 79 succeeded** — the S1.2/B01 characterizations are still red, as intended |
| OpenSpec | `openspec validate ... --strict` | valid |

### Recorded limitations — NOT coverage

- **Scheduler reachability (B06) is not proven.** B02 drives the real dispatch *decision*
  (`serviceAcceptedInteractionTurn`, the same entry `processDeferredIdleMaintenance` calls) but not
  `processDeferredIdleMaintenance` itself: no fixture in this suite has ever called it, and doing so
  newly requires the StorageManager leaves (`bootInteractiveReady`, `isBootTitleLoadDrain`,
  `SlotLoadSession::isActive`, `PersistenceWorkQueue::queueDepth/writingWorkItemCount`,
  `StorageManagerInternal::storageSession/undoSnapshotsPending_`). Linking or fail-closed-stubbing
  those is a harness decision for the B06 case, not something to fake here.
- **S4f.5 is not done.** The 49 `hasPendingDeferredOverdubStop()` consumers are unchanged. While the
  latch is set the predicate is false, so every consumer behaves as it does today for a dropped stop
  — no regression, but the stop is not yet visible to start admission, wrap, hot-path emission or
  persistence as in-flight work.
- **S4f.4 is partial**: no TrackManager-level transport epoch counter yet (the drive re-derives from
  `isTransportRunning()`), and the `switchTarget`/start supersession rules are untouched.
- **S2b.6 is partial**: the retire-path invariant (drain `canonicalCaptureOutput.store` on the
  committing route, clear it on a skipped route) and the named `accepted_stop_park_terminal` are not
  implemented.
- Still untouched: **S2b.1/S2b.2/S2b.3** (boot/load reservation removal and its evidence),
  **S2b.4** (release + `noLiveOwnerForSlot`), **S2b.5** (load admission), **S4.0**, **S4f.3**,
  **S3.0**'s static_asserts for the remaining new members, and fixtures **B03–B07**.

### Next step: S2b.1, and the two hazards that must ship with it

Confirmed by reading the tree (not assumed), so the next slice does not guess:

1. **The transfer horizon must be admitted at overdub start, not in the stop park.**
   `Track::startOverdubbing` (`TrackOverdubLifecycle.cpp:1417-1448`) admits capture staging and the
   companion rows only — it never calls `prewarmActiveOverdubTransferHorizon`. Today that is invisible
   because every committed slot is boot-prewarmed with a horizon; once S2b.1 removes the boot
   reservation, a loaded slot's stop fails the DEC-067 chain on `transfer_denied` forever, exactly the
   rerun blocker in `tasks.md` S2b. That admission belongs **at the overdub start's cold point**, beside
   the existing staging/companion grants and before any mutation — *not* in
   `parkAcceptedStopForCanonicalOutput`. `prewarmActiveOverdubTransferHorizon` reserves
   `passes.overdubPasses` and `prewarmedCommittedChunkIdPool_`, so calling it mid-interval from the park
   could relocate `overdubPasses` under a borrowed reference; at the pre-mutation admission turn it is
   the same call the record arm already makes.
2. **S2b.1 must ship with S2b.6's retire-path invariant.** `prewarmCanonicalCaptureOutput` requires
   `canonicalCaptureOutput.store.empty()`. The retire block clears `pendingPass`/`owner`/`loopId` and
   advances the ring but never touches that store, so a skipped or failed route can leave it populated
   with no ring entry. With the boot reservation present, the next `prewarmPlaybackForSlot(_, true)`
   masks it; with the boot reservation removed, the park cannot admit and the accepted stop waits
   forever — the exact failure mode the change exists to remove, inverted. The retire path must drain
   the store on the committing route and clear it on a skipped route, reporting a named terminal if it
   can do neither, **before** the boot reservation comes out.

Both are recorded here rather than implemented in a hurry: a rushed S2b.1 risks a permanent stop wait on
a device whose product rule is that a stop always completes from one press.

## P1 — S2b.1/S2b.2 boot/load canonical admission (2026-09-23)

The residency law is now green: **boot/load no longer reserves canonical-copy capacity or a transfer
horizon per committed slot**, and a loaded slot's accepted stop acquires what it needs itself.

### What changed

| task | implementation |
|---|---|
| **S2b.1** remove the per-slot boot reservation | `Track::prewarmPlaybackForSlot(s, false)` no longer calls `prewarmCanonicalCaptureOutput()`, and the transfer horizon is admitted only for `prepareCapture` (the old `\|\| !anyTimingCriticalTrackActive()` clause existed to let boot reserve it for every committed slot). Boot still establishes the runtime index, the stop-descriptor ring and the canonical-copy directory, so hot-path admission stays O(1). |
| **S2b.2** start is not gated; the start admits its own reserves | `Track::startOverdubbing` now calls the FLASH-resident `Track::admitOverdubStartWorkingSet()`, which establishes the record arm's set for a slot boot left cold: the view/bridge/companion scratch, the transfer horizon and the prepared-companion-history horizon. The canonical copy is **not** admitted here — START is deliberately not gated on it. |
| **S2b.6** retire-path invariant | `Track::processDeferredOverdubStopGrain`'s retire block now drains `canonicalCaptureOutput.store` (and releases the frozen canonical builders) when a retiring job leaves it populated, reporting `canonical_output_drained_at_retire`. This is what makes S2b.1 safe: `prewarmCanonicalCaptureOutput` requires an empty store, so an orphaned store would make the park deny forever. |

### Two findings from implementing it (both were pinned in advance, both confirmed)

1. **The overdub start needed the view set, not only the horizon.** `requiredStopRankedGrains` returns
   `UINT64_MAX` when `viewSetIfPrewarmed()` is null, so a loaded slot's stop denied as
   `preparation_service_unavailable` regardless of the horizon. It also needed the Loop-level
   `prewarmPreparedCompanionHistory` horizon, without which the job admitted and then never retired.
2. **That companion grant must not touch a live job.** Granting it unconditionally broke
   `test_adversarial_reentry_at_the_publication_checkpoint` ("a predecessor paused at CompanionHistory
   must still retire after a successor starts") — `prewarmPreparedCompanionHistory` resets the Loop's
   prepared history, which is exactly the DEC-072/5.12 successor hazard. It is now gated on
   `!hasPendingDeferredOverdubStop()`, which is sound because a live job's own admission already proved
   that reserve.

### Oracle and result

The three characterizations written on 2026-09-23-as-red are now **green and promoted into the default
gate**; the temporary `WORKING_SET_RED` gate is **deleted** (RECIPE R: remove a known baseline exception
once the implementing slice lands):

- `test_working_set_residence_does_not_grow_with_recorded_slots` — reserved chunks now flat (was 96 vs 120).
- `test_inactive_committed_slot_retains_no_resident_working_set` — resident working sets flat (was 8 vs 11).
- `test_b01_inactive_committed_slots_reserve_no_canonical_copy_or_horizon` — zero credits and zero
  horizons on inactive slots (was 24 credits over 3 slots).

New **`test_s2b2_loaded_slot_stop_acquires_its_own_horizon_and_canonical_target`** (default gate):
boot-only admission leaves the slot with no canonical reservation and no horizon → overdub start
establishes the horizon, the prepared-companion-history horizon and the view set while **not** being
gated on the canonical output → one Stop press is accepted and retained → the park acquires the
canonical target → exactly one job binds → the drain retires it, leaving the canonical store empty.

### Verification

| check | command | result |
|---|---|---|
| focused | `pio test -e native -f test_continuous_capture` | **PASSED, 83/83** |
| full native | `pio test -e native` | **2,268 cases / 1 skipped / 2,267 succeeded / 0 failures** |
| firmware | `pio run -e teensy41-capture-serial` | **SUCCESS**; RAM1 below |
| OpenSpec | `openspec validate ... --strict` | valid |

### Reported deviation — the RAM1 threshold is missed by 16 B

`teensy_size` now reports `RAM1: variables 96,128 / code 392,360 / padding 856 / locals free 34,944`.
S3.0 requires `padding >= 872`, so this is **16 B short** and is reported rather than redefined:

- `code + padding` is **constant at 393,216** across the change (392,344 + 872 → 392,360 + 856), so the
  16 B is linker alignment the new ITCM call site consumed, not a size regression in disguise.
- **No FlexRAM bank crossed.** The documented symptom is a 32,768 B drop in `free for local variables`;
  it is unchanged at **34,944**, which is the invariant the bank rule protects.
- Mitigation attempted and kept because it is right regardless: the whole admission body was moved out
  of ITCM-resident `Track::startOverdubbing` into the FLASH-resident
  `Track::admitOverdubStartWorkingSet()`, which recovered 288 B (568 → 856) and keeps future growth of
  that path out of ITCM. The remaining 16 B resisted further argument/denesting changes.

**This needs the architect's call**: either the 872 line is re-based with the arithmetic above, or the
16 B must come from elsewhere in ITCM before S3.0's static_assert work lands. It is not silently accepted.

## P1 — S4.0 per-slot liveness and S2b.4 reservation release (2026-09-23)

### What changed

- **`Track::noLiveOwnerForSlot(slot)`** (S4.0, invariant 33). Terms, in order:
  1. the ring test scans the **live window only** (`[head, head + count)`), so a retired entry that
     still carries `slot == 0` is not a stale positive;
  2. the slot's `captureStopOwner`, defensively — it can be set with no ring entry, and releasing
     underneath it would strand a live job;
  3. `preparedPairRequestPending`, i.e. a prepared handover still being **built**. The published pair is
     deliberately not a term: it is the slot's playback authority and stays valid while the slot sounds,
     so counting it would make release unreachable for every sounding slot;
  4. an accepted interaction targeting the slot (S4f.1's latch).
- **`LoopPlaybackRuntime::releaseCanonicalCaptureReservation()`** (S2b.4): refuses while
  `canonicalCaptureOutput.store` is non-empty (the store's chunks must be released with it, and
  `prewarmCanonicalCaptureOutput` requires an empty store anyway); otherwise returns the credits and is
  idempotent.
- **`Track::tryReleaseCanonicalCaptureReservation(slot)`** enforces `noLiveOwnerForSlot` itself, so the
  guard is not a convention a production caller can bypass. `tryReleasePlaybackMergedMidiEventsMemory`'s
  per-slot loop calls it *before* the merged-window checks, so a slot with no window still releases.

### Oracle and result

`test_s4_0_release_respects_the_live_ring_window_and_is_idempotent` (default gate): an idle slot with no
job, latch or owner reports no live owner; an **admitted** job for slot 0 makes `noLiveOwnerForSlot(0)`
false and the release entry point returns false with the 8 credits intact; after the job retires the
descriptor is out of the live window but still carries `slot == 0`, and the predicate reports no live
owner (the B6 stale-positive discriminator); the release then succeeds, a second release is a no-op
success, and re-reserving re-retains the 8 credits.

### Verification

| check | result |
|---|---|
| focused `test_continuous_capture` | **PASSED, 84/84** |
| full native | **2,269 cases / 1 skipped / 2,268 succeeded / 0 failures** |
| firmware `teensy41-capture-serial` | **SUCCESS**; RAM1 unchanged at `variables 96,128 / code 392,360 / padding 856 / locals free 34,944` — every new function is FLASH-resident, so the reported 16 B deviation did **not** grow |

### Recorded limitations — NOT coverage

- **S4.0 is partial.** Two listed terms are not implemented and are recorded rather than assumed:
  (a) the **job-owned undo root** term — the ring scan covers job-owned roots while the entry lives, but
  the protected undo publication is track-level and has no per-slot identity to test; S5.2a's cold
  descriptor is where it becomes expressible; (b) the **deferred-reconstruction** term, which S5.2e
  introduces. There is no `S5.2f` regeneration replacement of the stop grain's stall-and-hold yet.
- **S2b.4 is partial.** The listed fixture's "mid-copy release attempt → no `Denied` grain" is not yet
  exercised: the refusal is implemented (non-empty store) but no fixture drives a populated
  `canonicalCaptureOutput.store` through the release. The "role leaves the active set" trigger is the
  existing pressure-reclaim loop, which is the only such surface until S5.1 adds demotion.

### S2b.3 boot budget — native leg (2026-09-23)

`test_s2b3_boot_consumes_only_committed_content_and_keeps_the_pool_reserve` (default gate) measures the
same two variants as S1.2 and asserts what S2b.3 asks for, at native scale:

- **boot's per-committed-slot pool cost is exactly zero** — `reservedChunks` (admitted − payload) is
  identical at 1 and 4 recorded slots. Measured: payload 1 → 4 chunks, admitted 41 → 44 chunks.
- **the pool's own reserve survives boot** — `freeChunkCount()` stays at or above
  `PassConfig::CHUNK_RESERVE` (16), the floor admission refuses below.

Recorded, because it is a real number a reader would otherwise have to guess: boot's **constant** cost is
**40 chunks** in both variants — the stop-descriptor ring's sealed-range directories and the playback
order for the **default enabled set** (slot 0 of each of the 8 tracks), i.e. ~5 chunks per enabled slot.
It is constant in the committed-slot count and bounded by the enabled set, not by the library, which is
what the law requires. The device-scale figure S2b.3 also asks for (boot `live` against a 24-bar
overdub's peak reservation, ~191 chunks) remains **S1.5's device leg and is NOT RUN**.

## P1 — S4f.5's stop-related in-flight consumers (2026-09-23)

### Two real holes the audit found (not redundant after all)

The contract required the existing **start/switch guards to include the Stop latch** in this
intermediate slice. Reading them showed that most sites already hold transitively, because a latched
stop leaves the interval in `OVERDUBBING` and those guards test `isOverdubbing()`. **Two did not:**

1. **`TrackManager::startRecordingTrack`** checked `hasPendingDeferredOverdubStop()` **alone**. With a
   stop accepted-but-unexecuted that predicate is false, so a record start was admitted on top of an
   unadmitted stop. Now `hasStopInFlight()`.
2. **`TrackManager::finalizeCaptureAndSelectSlot`**'s switch path called `t.stopOverdubbing()` and then
   **fell through to `t.setActiveLoopIndex(newSlot)`** regardless of whether the stop was admitted —
   moving the active slot underneath an unadmitted stop, which the contract forbids explicitly
   ("Do not move the active slot underneath an unadmitted stop in this intermediate slice"). It now
   returns while `hasAcceptedUnexecutedInteraction()`, i.e. the slot moves only once the stop was
   admitted and bound (DEC-078 item 4). S4f.3 replaces this with the switch's own accepted-interaction
   carrier.

### Predicates added

- `Track::hasLiveCaptureInterval()` — `isRecording() || isOverdubbing()`.
- `Track::hasStopInFlight()` — `hasPendingDeferredOverdubStop() || hasAcceptedUnexecutedInteraction()`.
  `isRecording()` is deliberately not a term: a record interval with no stop obligation is not a stop
  in flight.
- Converted to them at the sites the contract names: the three persistence predicates
  (`PersistenceAdmit.cpp`, `StorageManager/Internal.cpp`, `MidPassChunkPersist.cpp`) and the primary
  resolver `resolveActiveCaptureTrackSlot`. At those sites the addition is currently **redundant**
  (the latched interval is still `OVERDUBBING`), and is made explicit because S4f.3 lets a stop outlive
  a state change.

### Oracle and result

`test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes` (default gate): with a stop
accepted-but-unexecuted (canonical target denied), `hasStopInFlight()` is true even though no job
exists; a record start on the track is refused; a slot switch leaves `activeLoopIndex` at 0 and the
latch intact; after the park admits the job the latch is consumed and `hasStopInFlight()` is still true
(the admitted job), so the switch stays refused under the pre-existing DEC-067 rule; after retirement
nothing is in flight.

### Verification

| check | result |
|---|---|
| focused `test_continuous_capture` | **PASSED, 86/86** |
| full native | **2,271 cases / 1 skipped / 2,270 succeeded / 0 failures** |
| firmware | **SUCCESS**; RAM1 `variables 96,128 / code 392,376 / padding 840 / locals free 34,944` |

### Recorded limitations — NOT coverage

- **The RAM1 deviation grew from 16 B to 32 B**: `padding` is now **840** against the recorded 872 line
  (`code + padding` still constant at 393,216; still no bank cross; `locals free` unchanged). The two new
  ITCM call sites are the cost: `hasStopInFlight()` is FLASH-resident, so calling it from the ITCM
  manager entries needs a long-call veneer, and `finalizeCaptureAndSelectSlot`'s added latch test is
  ITCM. A first version that also added `hasLiveCaptureInterval()` to the start guard cost the same, so
  it was narrowed to exactly the contract's requirement. Making the manager entries FLASH-resident
  themselves is a deliberate placement decision for the architect, not something to slip in here.
- **Still not converted** (recorded, not assumed): the `getActiveLoopIndex()`-keyed persistence reads in
  `TrackCaptureStopCommit.cpp:139,263` and `TrackStopTelemetryColdHelpers.cpp:161,166`, which need `slot`
  threaded through `commitCaptureForStop`/`finalizeCommitSideEffects` (S4.1's erasure-surface list); and
  `resolveCaptureTrackSlotForMidPass`'s selected/active fallback, whose removal is a persistence behavior
  change with no fixture in this suite.
- **The positive switch case is not assertable here**: a successful move calls
  `StorageManager::prioritizeLoopSlotRestoreForFocus`, which is a fail-closed off-path stub in
  `test_continuous_capture`.

## P1 — S3.0 placement and size pins (2026-09-23)

The pulled-forward S3.0 prerequisite for the fields this slice introduced. Two locations, because the
private nested types need a member context:

- **`Track::placementPins()`** (`include/Track.h`) — a never-called member function, so the private
  nested types are nameable and `Track` itself is complete. Pins the platform-independent latch layout
  (`sizeof == 40`, trivially copyable, `slot` at offset 2, `interval` at offset 8) and, device-only,
  `sizeof(Track) == 384` and `sizeof(Track::CaptureStopJobs) == 2736`.
- **`src/Track/TrackPlaybackRuntime.cpp`** — device-only pins for `sizeof(LoopPlaybackRuntime) == 6344`,
  `sizeof(Loop) == 984` and `sizeof(TrackPlaybackRuntime) == 32`.

**Measured device sizes** (harvested with `template <size_t N> struct Probe; Probe<sizeof(X)> p;`, the
recipe recorded in the pin comments):

| type | device size |
|---|---|
| `LoopPlaybackRuntime` | 6,344 |
| `Loop` | 984 |
| `TrackPlaybackRuntime` | 32 |
| `Track` | 384 |
| `Track::CaptureStopJobs` | 2,736 |
| `Track::PendingInteractionLatch` | 40 |

**The pins are effective, verified rather than assumed:** temporarily breaking the latch pin to `41`
failed the device build in **all 8 translation units** that include `Track.h`, so an unreferenced inline
member body is not silently skipped.

**`padding` is unchanged at 840 / `code` at 392,376** — the pins emit no code, which is the point.

### Recorded limitation

**S3.0's own gate is not met.** Its evidence clause requires `padding >= 872`; the measured line is
**840** (32 B below), from this change's ITCM additions. `code + padding` is constant at 393,216, no
FlexRAM bank crossed, and `locals free` is unchanged at 34,944. The pins now make any *further* drift a
build error, so the number cannot move silently again — but the decision to re-base the 872 line (or to
move the ITCM-resident manager entries to FLASH) is the architect's, and is the one open item blocking
a clean S3.0.

## S3.0 — reviewed RAM1 re-base and post-link budget enforcement (architect decision 2026-09-23)

### Decision

**The explained 32-byte ITCM increase is accepted and the padding floor is re-based 872 → 840 for this
slice. The ITCM manager entries stay in ITCM.** Moving `startRecordingTrack` /
`finalizeCaptureAndSelectSlot` to FLASH would be a separate execution-placement change on record and
slot-selection paths, and is not justified merely to recover 32 bytes; it needs its own placement
analysis and latency verification.

The arithmetic behind the decision: ITCM allocation stays **12 banks / 393,216 B**, RAM1 variables stay
**96,128 B**, and the local allowance stays **34,944 B**. The change consumed **32 bytes of headroom before
the next bank boundary**, not 32 bytes of current stack allowance. `code + padding` is constant at
393,216 across the whole change.

The historical line is preserved rather than overwritten: `scripts/ram1_budget.json` carries
`padding_floor_history` with 872 (until 2026-09-23) and 840 (since, with the reason).

### Correction accepted: structure pins do not close the placement gate

The earlier claim in this artifact that "the pins now make any further drift a build error" was **too
strong** and is withdrawn. `static_assert` on a structure's size protects that structure's layout. It
cannot see additional instructions, linker veneers, alignment changes, or unrelated RAM1 growth — none of
which is a type-size change. Size assertions alone do not satisfy S3.0.

### Enforcement added

- `scripts/ram1_budget.json` — the approved budget (ITCM banks/bytes, RAM1 variables, local allowance,
  padding floor) plus the floor history. Edited deliberately by a human in the same commit as the change
  that needs it, with the reason recorded here. **Never auto-updated.**
- `scripts/check_ram1_budget.py` — a **post-link** check registered as
  `env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", ...)` from `[env:teensy41]`, so every firmware env
  inherits it. It runs the platform's own `teensy_size` on the linked ELF and fails the build when the
  ITCM region, RAM1 variables, the local allowance, or the padding floor no longer match the approved
  budget. A floor breach reports the numbers and says the floor is a reviewed allowance, not an automatic
  reset.

### Evidence

| run | result |
|---|---|
| `pio run -e teensy41-capture-serial` | **SUCCESS** — `[RAM1 budget] variables 96,128 / ITCM 393,216 (code 392,376 + padding 840) / locals 34,944 \| approved floor: padding >= 840` |
| the same build with `padding_floor` temporarily raised to 900 | **FAILED as designed** — `padding below the approved floor: 840 B < 900 B`, then the baseline was restored and the build passed again |

### Remaining tightness, and the planned follow-up (kept separate from this slice)

**840 B is still tight.** Crossing the next bank boundary with `variables` unchanged would drop the local
allowance to about **2,176 B**. A deliberate cold-code placement improvement is therefore planned as its
own change — **not** as part of this runtime-correctness slice — and the post-link check is what keeps the
situation visible in the meantime.

## P1 — B03 transport retarget, and why the obligation is consumed there (2026-09-23)

### Oracle and result

`test_b03_transport_stop_retargets_an_admitted_playing_stop_without_losing_it` (default gate) drives the
**production** transport-stop entry — `Track::stopOverdubbingToStopped()` →
`requestDeferredOverdubStopToStopped()`, not a fake running-clock toggle — on an admitted
`Playing`-destination stop and asserts:

- the stop survives the transport stop (`hasPendingDeferredOverdubStop()` still true) and is **retargeted**
  to `PendingStopDestination::Stopped`;
- the accepted obligation is clear at that point (see below);
- the job keeps its identity: exactly one descriptor, bound to the interval's slot 0, with its interval
  unchanged — nothing dropped, re-created or rebound).

### `consumeAcceptedStopObligation()` at the transport-stop entry is load-bearing

`stopOverdubbingToStopped()` clears the accepted obligation before it admits or synchronously completes
the stop. **Removing that would be a defect, not a nicety:** if the transport-stop leg admitted the stop
while leaving the latch set, or took the synchronous fallback, the latch would survive an interval that no
longer exists. It would then keep `hasStopInFlight()` true forever — blocking every later record start,
slot switch and release through the guards S4f.5 just fixed — and its deferred drive would be refused by
`isOverdubbing()` on every turn with nothing to clear it.

### Recorded limitations — NOT coverage

- **The `Stopped`-destination drain is now covered** (updated same day). `Track::silenceTrackMidiOutput()`
  was converted from a fail-closed `OFF_PATH()` stub to an **observable counting boundary**
  (`g_nativeSilenceTrackCalls`, reset in `setUp`) and B03 drains the retargeted job: it resolves to
  `STOPPED` and never `PLAYING`, silences the track **exactly once**, clears the accepted obligation and
  commits one pass. The stub was over-broad: silencing is reachable from *both* stop destinations, so
  failing closed on it left the entire `Stopped`-destination retire path -- production behavior, and one
  the accepted-stop work touches -- with no native coverage at all. Counting keeps it observable without
  pretending the physical silence is under test.
- **The latched (accepted, not admitted) transport-stop leg is not assertable either**, for the same
  reason plus the `commitCaptureForStop` stub: with credit denied the transport-stop path takes the
  synchronous fallback, which those stubs exist to keep off this suite's path. On device that fallback is
  the pre-existing transport-stop behavior; the deferred drive is what this change added.
- **B06 (dispatch reachability) is not implemented as a fixture.** The scheduler's eligibility decision
  lives in `src/main.cpp` (`hasRealtimeIdleWork`, the all-stopped guard) and the sketch is not
  native-linkable, so proving "a stopped or non-focus track still gets a grain" needs either a production
  extraction of that decision or a device run. For this slice a latched stop keeps its track in
  `OVERDUBBING`, which `hasRealtimeIdleWork` already accepts, so the drive is reachable — and it is why
  retargeting at transport stop was **not** taken: a retargeted-but-unadmitted stop on an all-stopped
  transport would need B06 first or it would hang in `OVERDUBBING`.

### Verification

| check | result |
|---|---|
| focused `test_continuous_capture` | **PASSED, 87/87** |
| full native | **2,272 cases / 1 skipped / 2,271 succeeded / 0 failures** |
| firmware + RAM1 budget check | **SUCCESS** — `variables 96,128 / ITCM 393,216 (code 392,376 + padding 840) / locals 34,944` |

## P1 — S2b.5 status: half done, half blocked on a harness that does not exist (2026-09-23)

### The predicate half is already done and verified

S2b.5's requirement is that `beginLoadLoopJob` must not destroy a live owner, "the predicate must be
defined against the ring entry's `job.owner` **and** `job.slot`". That predicate **is**
`Track::noLiveOwnerForSlot(slot)` (S4.0), and S2b.5's own rerun correction adds "S2b.4 must never release
while a ring entry with that `job.slot` exists" — also done.

Its **scope** is now asserted rather than assumed. The S4.0 fixture additionally requires that with a live
job admitted on slot 0, `noLiveOwnerForSlot(1)` is **true**: a finalizing predecessor must block a load into
*its* slot and must not block any other slot of the same track. A track-level "capture active" flag — the
shape S4.0 corrects — fails that assertion. That is exactly the property a load decision needs.

### The call-site half is blocked, and this is the concrete plan

Remaining S2b.5 work is small and local: `LoadLoopJob.cpp`'s callers **pop the restore entry before calling
`beginLoadLoopJob`** (`:339, :367, :372, :377`) and re-queueing is inert
(`queueDeferredLoopSlotRestore` early-returns unless `needsSlotLoad`), so a load begun for a slot whose
freedescriptor is still owned destroys it. The fix is a filtered, done-tested peek before the pop, or a
forced enqueue.

It cannot be implemented this round without shipping an unverified change to a path with **zero** native
coverage, so the harness is pinned instead:

- **Gap:** there is no SD emulation anywhere in the native suites. `test/test_support/firmware/SD.h` is two
  lines (`struct File {};`), and neither `test_storage_loop_io` nor `test_sd_load_adopt` links
  `StorageManager/LoadLoopJob.cpp` or `StorageManager/LoopSlotRestoreQueue.cpp`.
- **What a B04 suite must link:** `LoadLoopJob.cpp`, `LoopSlotRestoreQueue.cpp`, `SlotLoadSession.cpp`,
  `CurrentSetStorage.cpp`, `CurrentWorkspaceStorage.cpp` (path/format + save-file token helpers), plus an
  in-memory `SD`/`File` emulation providing `exists`, `open`, `size`, `seek`, `read`, `close`.
- **What must be stubbed, and how:** the physical boundaries only (`Track::silenceTrackMidiOutput` as a
  counting boundary, display/telemetry no-ops). `resetLoopSlotToEmpty` must **not** be stubbed — observing
  that it is *not* called is the fixture's whole point.
- **B04 assertion sequence:** queue a restore for the slot of an admitted finalizing job → run the real
  load selection/admission → the entry is **still queued**, the Loop was **not** reset, the finalizing job
  is untouched → retire the owner → run selection again → the request is consumed exactly once and the
  restored content is exact.
- **Ordering:** this harness is also what unblocks B06's non-focus leg and S2b.4's mid-copy fixture, so it
  is worth building as one shared `test_storage_manager_load` suite rather than three one-off stubs.
- `hasPendingDeferredReconstruction()` does not exist yet; its term joins the S2b.5 predicate when S5.2e
  introduces that owner, as the contract states.

## S3.0 — two defects found in the enforcement itself (2026-09-23, same day)

Checking that the new budget check covered **every** firmware env — instead of assuming inheritance
worked — found two real defects in what had just been committed.

### 1. The absolute size pins were configuration-specific

`teensy41` and `teensy41-capture` **failed to compile**: `static_assert(sizeof(Track) == 384)` holds only
where `SESSION_CAPTURE` / `PERF_TELEMETRY` are defined. `Track`'s layout depends on build flags, so an
absolute size is not a device constant. The device pins are now scoped to the configuration they were
measured in
(`#if defined(__IMXRT1062__) && defined(SESSION_CAPTURE) && defined(PERF_TELEMETRY)`), with the
configuration-independent guard named as the post-link budget check. The latch pins stay unscoped because
the latch holds no pointers and no flag changes its layout.

Measured, per configuration:

| env | RAM1 variables | ITCM (code + padding) | locals |
|---|---|---|---|
| `teensy41-capture-serial` (and `-no-grain`) | 96,128 | 393,216 (392,376 + **840**) | 34,944 |
| `teensy41` (and `-capture`) | 79,168 | 393,216 (383,448 + **9,768**) | 51,904 |

Both hold ITCM at 12 banks; the capture-serial configuration is the tight one at 840 B, while the plain
configuration has 9,768 B of slack. That difference is exactly why a global baseline was wrong.

### 2. The budget baseline has to be per environment

The check then failed correctly on both plain envs, because the single global baseline described only
`teensy41-capture-serial`. `scripts/ram1_budget.json` is now keyed by `$PIOENV`, with a **named
reference** for envs whose budget is identical (`teensy41-capture` → `teensy41`,
`teensy41-capture-serial-no-grain` → `teensy41-capture-serial`), and an env with **no** entry fails with
"no approved baseline for environment X; measure it and add an entry deliberately" rather than being
silently skipped. The `padding_floor` stays shared at 840 as a configuration-independent early warning
before the 13th bank.

### Evidence

| run | result |
|---|---|
| `teensy41`, `teensy41-capture`, `teensy41-capture-serial`, `teensy41-capture-serial-no-grain` | all **SUCCESS**, each printing its own resolved budget and reference |
| `teensy41-capture-serial` with `ram1_variables` temporarily set to 96,120 | **FAILED as designed** on the exact-value check, then restored |
| full native | **2,272 cases / 1 skipped / 2,271 succeeded / 0 failures** |

**Lesson recorded:** the enforcement added to satisfy the architect's requirement was itself unverified
until it was exercised on every env. Checking the check is part of shipping it.

## P1 — two decisions the implementer cannot take alone (2026-09-23)

### 1. S4f.4's transport-epoch counter costs RAM1 and has no behavioural use left

The slot-parameterised drive **already satisfies the epoch's actual requirement**: `driveAcceptedInteraction`
re-derives the destination from live transport state (`clockManager.isTransportRunning()`) and re-derives
the boundary tick from `clockManager.getCurrentTick()` — it never trusts the stored absolute
`request.phaseTick`. The contract's own reason for the epoch is *"an absolute tick is diagnostic only —
MIDI Start, seek and load reset the tick domain"*, and that property holds today.

What the contract specifies in addition is a **new `TrackManager`-level monotonic counter** bumped by
transport stop, `resetToLoopStart`/`setCurrentTick`, load and MIDI Start. There is no existing tick-domain
epoch to reuse (checked `ClockManager`/`TrackManager`), so it is genuinely new state, and:

- `Track`'s `.bss` additions are documented as costing **4 KB of lost local allowance each** on this tree,
  and `TrackManager` is a global — so the counter lands in RAM1 `variables`, which changes the *locals*
  figure and needs a second deliberate budget re-base;
- its only use would be diagnostic: the latch already stores `transportEpoch`, but nothing consumes it
  because the drive does not need it.

So the field is currently **dead but declared**, which is worse than either wiring or removing it. Three
options, for the architect: (a) accept the re-base and wire the counter for evidence; (b) keep the field
unset and record it as an S4f.4 residual; (c) drop the field (shrinking the latch 40 → 32 bytes and
updating its pin) and record the epoch as satisfied by live re-derivation. **Recommended: (c)** — it is the
only option that removes dead state and costs nothing, and the behavioural requirement is already met.

### 2. The load harness is a multi-slice build, with increment-1 findings

A one-build probe of `StorageManager/LoopSlotRestoreQueue.cpp` against the native stubs produced concrete
findings that replace the earlier estimate:

- `scripts/native_test_link.py` puts the Arduino/SD stub path on the include path **only** for
  `test_continuous_capture`; a new suite must be added to that allow-list (the file currently hardcodes one
  suite name, so the allow-list wants generalising).
- The firmware `SD.h` stub declares only `struct File {};` — there is **no `SD` object** and no
  `read`/`seek`/`size`/`open`/`close`, so `SD.exists(...)` does not even compile. An in-memory SD/File
  emulation is required, not a header tweak.
- `CurrentSetStorage::verifySaveFileTokenAtPath` is not visible from that TU, so `CurrentSetStorage` (and
  `CurrentWorkspaceStorage`) must be linked, not stubbed.

That is a suite of its own, to be built once for S2b.5 + B04 + B06's non-focus leg + S2b.4's mid-copy
fixture. It is the right next build, but it is not a single-slice change, and nothing was committed from
the probe: the tree is unchanged and the 87-case suite still passes.

## P1 — shared load harness, increment 1: the load path links natively (2026-09-23)

The architect's scoped build, increment 1. **`StorageManager/LoopSlotRestoreQueue.cpp` now compiles, links
and runs under `pio test -e native`** in the new `test/test_storage_manager_load` suite. That is the
milestone: the load path had no native presence at all, and the reasons are now fixed rather than worked
around.

### What had to change, and why each was the right boundary

| blocker | fix |
|---|---|
| The firmware `SD.h` stub was `struct File {};` — no `SD` object, no `read`/`seek`/`size`/`open`/`close`, so `SD.exists(...)` did not compile. | `test/test_support/firmware/SD.h` is now an **in-memory card** (`SDClass` with `exists`/`open`/`writeFile`, and a `File` with `size`/`seek`/`read`/`available`/`close`). Only the physical card is replaced. |
| `CurrentSetStorage`'s SD-dependent declarations — including `verifySaveFileTokenAtPath` — sit behind `#if defined(ARDUINO)`. **Not defining it is precisely why this path was never natively testable.** | The harness defines `ARDUINO 1` for itself, so production's SD paths are visible instead of compiled out. |
| That activated allocator failure-reporting paths using `Serial.println`/`Serial.flush`, absent from the native serial stub. | `test/test_support/firmware/Arduino.h`'s `NativeSerial` gained `println`/`flush`. Additive; `test_continuous_capture` still passes 87/87. |
| `native_test_link.py` hardcoded one suite name for the Arduino/SD include path. | It is now an explicit **suite allow-list** (`test_continuous_capture`, `test_storage_manager_load`). |

### Tests, and two facts that shape the S2b.5 fix

- **Ordering:** `sortPendingLoopSlotRestoresByPriority` is **ascending** by `restorePriority` (a lower
  number is served first). The first version of the test asserted descending and failed, which is how the
  real contract got pinned rather than assumed.
- **`peekFirstPendingLoopSlotRestore` has no done-test**: with `loopSlotRestoreAttempted_[2][3]` set, peek
  still returns `(2,3)`. This is the contract's finding, now asserted, and it is why the caller must add
  the done-test (or use a forced enqueue).
- **A forced enqueue already exists**: `appendBootLoopSlotRestore` appends regardless of the attempted
  flag or committed content — the contract's "or a dedicated forced enqueue" alternative is already in the
  tree, so S2b.5 does not need to invent one.

### Verification

| check | result |
|---|---|
| new suite | **3/3 PASSED** |
| full native | **2,275 cases / 1 skipped / 2,274 succeeded / 0 failures** (`test_continuous_capture` 87/87 unchanged) |
| firmware + budget | **SUCCESS**, `variables 96,128 / ITCM 393,216 (code 392,376 + padding 840) / locals 34,944` |

### Next increment

Link the suite's `TrackManager`/`Track`/`Loop` set (the same production objects `test_continuous_capture`
already links) so `queueDeferredLoopSlotRestore`'s `needsSlotLoad` guard and
`popFocusDeferredLoopSlotRestore`'s done-test become live, then drive `beginLoadLoopJob` itself for **B04**.
Only then is the S2b.5 call-site fix implementable against a fixture, per the architect's order.

## P1 — load harness increment 2: the structural choice, and why it is not free (2026-09-23)

Increment 2 needs `Track` / `TrackManager` / `Loop` linked, because
`StorageManager::needsSlotLoad` (`StorageManager.cpp:243`) ends in
`track.getLoop(slot).hasCommittedPasses()`. Two structures are possible, and both were **measured** by
probing rather than estimated.

### (a) Join the existing `test_continuous_capture` TU — rejected

The queue was added to that suite's include list. It produced exactly three error classes:

1. `CurrentSetStorage::verifySaveFileTokenAtPath` is invisible, so the suite would have to define
   `ARDUINO 1` — and that define changes what **every** production TU included there compiles, since the
   SD-dependent declarations across the tree sit behind `#if defined(ARDUINO)`. The suite's 87 passing
   tests currently depend on those guards being **off**; flipping it is a boundary change to a suite that
   is not about the load path.
2. `off_path_deps.h` already stubs `StorageManager::hasLoopSlotPayloadOnSdInRam` and
   `refreshLoopSlotPayloadOnSdInRam`, which the queue file now defines for real — two duplicate
   definitions that would have to be deleted from the stub header.
3. Those two deletions and the global define would then expose whatever else the newly-compiled guards
   reference, an unknown cascade through 87 unrelated tests.

### (b) Extract the shared harness include block — the recommended next step

Move the production include set (Loop/Track/TrackManager and the stub surface) into one shared header, e.g.
`test/test_support/track_harness.inc`, and include it from **both** `test_continuous_capture` and
`test_storage_manager_load`. The new suite then links the load path with the Track objects it needs, the
existing suite's boundary surface is untouched, and the include set stops being duplicated. This is the
next increment; it is a test-infrastructure refactor, not production code.

Nothing was committed from the probe: the continuous-capture TU was restored from a backup, the tree is
clean, and it passes **87/87**.

## P1 — load harness increment 2 landed: the load path links with the full Track harness (2026-09-23)

`test/test_storage_manager_load` now links **the real load queue plus the Track/Loop/TrackManager
objects**, against the in-memory SD card. That is the structural step increment 1 could not reach, and it
was done by the route the earlier probe recommended rather than the one it rejected.

### What landed

- **`test/test_support/track_harness.inc`** — the shared production include block (Loop, the `Runtime*`
  owners, `Track*`, `TrackManager`, `MidiButtonActions`, the undo/commit units) plus the harness globals
  and the native clock boundary, **extracted verbatim** from `test_continuous_capture` and included by both
  suites. Include order is significant (a `#define` for `LoopCaptureTestDeps`, and the runtime owners before
  the objects they serve), so it moved as a block rather than being reorganised. The include set stops being
  duplicated instead of gaining a second copy.
- **`off_path_deps.h` gained one explicit seam**: `HARNESS_LINKS_LOOP_SLOT_PAYLOAD_ON_SD`. The load suite
  defines it because it links the real `hasLoopSlotPayloadOnSdInRam` /
  `refreshLoopSlotPayloadOnSdInRam`; every other suite keeps the fail-closed route unchanged. This is what
  made the earlier rejected route's duplicate-definition problem disappear instead of being worked around.
- **`MidiHandler.cpp`** is linked by the load suite directly: `TrackManager`'s constructor references the
  global MIDI handler, and this suite has no second TU to carry it the way
  `test_continuous_capture/midi_input.cpp` does.

### Verification

| check | result |
|---|---|
| load suite | **3/3 PASSED** (now linking the full harness) |
| `test_continuous_capture` after the extraction | **87/87** — the extraction is behaviour-neutral, verified at each of the two moves |
| full native | **2,275 cases / 1 skipped / 2,274 succeeded / 0 failures** |
| firmware + budget | SUCCESS, `variables 96,128 / ITCM 393,216 (code 392,376 + padding 840) / locals 34,944` |

### Next: B04 itself

The harness can now drive the real path. **B04** is the next step and is a test of its own: build a
finalizing interval on a slot, queue a restore for that slot, run the real selection/admission, and assert
the entry is **still queued**, the Loop was **not** reset and the finalizer is intact — then retire and
assert exactly one restore. Only after that fixture is red does the S2b.5 call-site fix get made against it.

## P1 — B04 is one linkage step away, and the exact step is now known (2026-09-23)

The harness can now drive the *queue* against the real Track objects, so the defect mechanism was written
as a fixture — and it immediately named the three symbols that stand between the suite and B04. **Nothing
was committed from it**: the fixture was reverted and the suite stays at 3/3.

### The defect mechanism, derived rather than assumed

Writing the fixture sharpened B04's trigger. `LoadLoopJob.cpp`'s callers pop the restore entry **before**
`beginLoadLoopJob`, and `queueDeferredLoopSlotRestore` is **inert** for a slot that has committed passes
or was already attempted (`needsSlotLoad`). So:

1. the **pop destroys the request** — it cannot be put back;
2. the slot's **destruction** (`resetLoopSlotToEmpty`) happens later, and only when the loader runs for a
   slot it was told to load and the payload is absent or invalid.

So B04's discriminating state is **a live finalizing owner on a slot whose queued restore has no valid
payload**, not merely "a load begun for an occupied slot". That refinement decides what the fixture must
build (a committed slot plus a missing in-memory SD file), and it is why the request-loss half is
observable without any SD at all.

### The three missing symbols, and the recommended fix for each

| symbol | defined in | recommendation |
|---|---|---|
| `StorageManager::needsSlotLoad` | `src/StorageManager.cpp:243` (the monolith) | **extract it** into its own small TU (e.g. `src/StorageManager/SlotLoadAdmission.cpp`). It is a ~25-line predicate over `Config`, `isLoopSlotRestoreAttempted`, `SlotLoadSession::isActiveFor`, the active/parked job queries and `track / getLoop(slot).hasCommittedPasses()`. Extracting it makes the load *decision* linkable without the monolith, which is the same mechanical split the repo's translation-unit workflow already sanctions. |
| `CurrentSetStorage::formatLoopSlotPath` | `src/StorageManager/CurrentSetStorage.cpp` | link the TU (paths and the save-file token are the production definitions the load path must use, not something to stub). |
| `CurrentSetStorage::verifySaveFileTokenAtPath` | same | same. |

The next increment is therefore: extract `needsSlotLoad`, link `CurrentSetStorage.cpp`, re-add the B04
fixture (red), and only then make the S2b.5 call-site fix. The in-memory SD card and the full Track harness
are already in place, so the remaining work is mechanical rather than exploratory.

## P1 — load harness increment 3: the predicate is extracted and the SD card is complete (2026-09-23)

Three verified steps landed, and the B04 fixture is now exactly **one linkage step** from being writable.

### Landed

1. **`StorageManager::needsSlotLoad` moved to its own TU** (`src/StorageManager/SlotLoadAdmission.cpp`),
   out of the `StorageManager.cpp` monolith, so the load *decision* is linkable without the whole
   persistence layer. It is a pure predicate over slot/capture state — no I/O, no allocation, no mutation —
   and it stays **FLASHMEM** so it consumes no ITCM: the firmware's RAM1 line is byte-identical
   (`code 392,376 / padding 840 / locals 34,944`), which the post-link budget check confirms.
2. **`CurrentSetStorage.cpp` is linked by the load suite** and `SlotLoadAdmission.cpp` with it, so
   `formatLoopSlotPath` and `verifySaveFileTokenAtPath` are the real production definitions rather than
   stubs.
3. **The in-memory SD card is complete for the write path**: `SDClass` gained `rename`, the stub gained
   `FILE_WRITE`, and `File` gained `write` plus a `writable` flag and create-or-truncate `open`. Only the
   physical card is replaced; the read path is unchanged and `test_continuous_capture` still passes 87/87.

### The B04 fixture is one triple of symbols away

Re-adding the fixture now fails to **link** on exactly the dependencies `needsSlotLoad` itself has:

| symbol | defined in |
|---|---|
| `SlotLoadSession::isActiveFor` | `src/StorageManager/SlotLoadSession.cpp` |
| `StorageManagerInternal::isActiveLoadLoopJobFor` | `src/StorageManager/LoadLoopJob.cpp` |
| `StorageManagerInternal::isParkedLoadLoopJobFor` | `src/StorageManager/LoadLoopJob.cpp` |

So the next increment is: link `SlotLoadSession.cpp` and `LoadLoopJob.cpp`, re-add the B04 fixture (which is
already written and validated as far as the linker), watch it go **red**, then make the S2b.5 call-site fix
against it. Nothing was committed from the fixture; it was reverted again so the suite stays green at 3/3.

### Verification

| check | result |
|---|---|
| load suite | **3/3** |
| full native | **2,275 cases / 1 skipped / 2,274 succeeded / 0 failures** |
| firmware + budget | SUCCESS, RAM1 unchanged |
| `test_continuous_capture` | 87/87 (the shared SD stub change is additive) |

## P1 — B04 is reproducible, and the defect is worse than "a lost request" (2026-09-23)

The last linkage step landed and the fixture runs. It changed the finding.

### The fixture, and what it shows

`test_b04_pop_then_requeue_loses_the_request_for_a_committed_slot` (default gate, `test_storage_manager_load`):

- re-queueing is **inert** for a slot with committed passes — `queueDeferredLoopSlotRestore` enqueues
  nothing, because `needsSlotLoad` refuses committed content;
- seeding the request through the forced path and then doing what `LoadLoopJob.cpp`'s callers do —
  **pop first** — does not merely take the entry: `popFocusDeferredLoopSlotRestore`'s done-test sees the
  committed passes, treats the request as **already satisfied, returns false and silently discards the
  entry**;
- re-queueing after that still enqueues nothing, so the obligation is **unrecoverable**, with no signal at
  the pop;
- the slot's content — what the live owner depends on — is untouched at this stage.

So the correct statement of the S2b.5 defect is not "the pop loses the request" but **"for a committed slot
the pop silently discards an outstanding restore request, and the queue will not take it back."** The
destruction of the Loop is the *later* consequence, when the loader runs for a slot it was told to load and
the payload is absent or invalid. A finalizing predecessor's slot has committed passes, so it is exactly this
state.

### Linkage completed in this increment

| TU | why |
|---|---|
| `SlotLoadSession.cpp` | `isActiveFor` — a `needsSlotLoad` term |
| `LoadLoopJob.cpp` | the active/parked job queries and the admission path itself |
| `LoopPendingNoteChange.cpp` | `applyPendingNoteChangesToDisplayNotes`, newly live under `LoadLoopJob` |
| `TrackManagerSlotMatrix.cpp` | `getSelectedSlotIndex`, called by `currentDeferredRestorePriority` |

Two explicit seams were needed, both documented and both no-ops for firmware:

- `HARNESS_ARDUINO_STUB` — `LoopPendingNoteChange.cpp` carries its own `inline micros()` under
  `PIO_UNIT_TEST_NATIVE`; a suite that links it *and* the Arduino boundary stub would define `micros()`
  twice in one TU, so that fallback now defers to the stub. On firmware `PIO_UNIT_TEST_NATIVE` is
  undefined, so the branch is unchanged.
- `HARNESS_LINKS_LOOP_SLOT_PAYLOAD_ON_SD` (from the previous increment) — the load suite links the real
  loop-slot-payload functions, so `off_path_deps.h` omits those two stubs for it.

### Verification

| check | result |
|---|---|
| load suite | **4/4 PASSED** (B04 included) |
| full native | **2,276 cases / 1 skipped / 2,275 succeeded / 0 failures** |
| firmware + budget | SUCCESS, RAM1 line unchanged (`code 392,376 / padding 840 / locals 34,944`) |

### Next

The fixture is **green as a characterization of the pre-fix behaviour**, which is what S2b.5 must change.
The fix is now a bounded edit: make the callers peek/pre-check (or use the forced enqueue) so a slot with a
live owner keeps its request, and re-point the fixture at the fixed behaviour — the entry **still queued**,
the Loop not reset, and exactly one restore after the owner retires.

## P1 — B04's live-owner fixture: the goal narrows and the target is precise (2026-09-23)

Attempting the *real* B04 fixture produced a sharper statement of both the fixture and the fix, and left the
suite verified at 4/4. Nothing was committed from the attempt.

### What the attempt established

1. **The owning predicate is a live *job*, not committed content.** `noLiveOwnerForSlot` — correctly — does
   not treat committed passes as an owner. So S2b.5's guard is about a live deferred job on the slot (ring
   window, `captureStopOwner`, prepared handover in flight, the accepted stop latch), and the fixture must
   build that state rather than a merely-committed slot.
2. **That state can be built without the MIDI/note-edit layer**, which matters for suite size: open the
   interval on the Loop directly (`beginCapture(CapturePhase::Overdub)` + `forceSetState(TRACK_OVERDUBBING)`)
   and admit through `Track::beginDeferredOverdubStop` via the **existing** friend contract
   (`TrackCaptureStopTestAccess`, which `Track.h` already names as a friend — a suite defines only the
   helpers it needs, as `test_continuous_capture` does). Going through `midiButtonActions` instead drags in
   `analyzeEditSessionInteractions` and `resolveConstrainedGeometry`.
3. **The remaining linkage, in the order it appeared:** `StorageLoopIo.cpp` +
   `StorageLoopIoBodyFormatV1.cpp` (they define `releasePersistedLoopSnapshotChunks`, despite the name of
   `PersistedLoopSnapshotRelease.cpp`), then `LoadLoopBudget.cpp` (`shouldFinishActiveBeforePreempt`). The
   chain advances one TU per probe and is converging, but it is linkage, not analysis.
4. **The fix itself is designed and local.** Guard the four pop sites in `LoadLoopJob.cpp`
   (`ensureActiveLoadLoopJobSelected`'s parked and empty `BeginFocusFromQueue` branches, its
   `popNextDeferredLoopSlotRestore` fallback, and `EmptyIdleAction::BeginBackgroundFromQueue`) with
   `noLiveOwnerForSlot(entry.track, entry.slot)` **before** the pop: when a live owner holds the slot, leave
   the entry queued and return. For the `popNext` sites the entry must be peeked first, since the pop has no
   filter. That is a bounded edit confined to one file plus its test.

### Verification (unchanged state)

| check | result |
|---|---|
| load suite | **4/4** |
| full native | **2,276 cases / 1 skipped / 2,275 succeeded / 0 failures** |
| firmware + budget | SUCCESS, RAM1 line unchanged |

### Next increment, fully specified

Link the three remaining TUs, add the live-owner fixture (design above), watch it **fail**, then apply the
four-site guard and re-point the fixture at the fixed behaviour: entry **still queued**, Loop not reset,
exactly one restore after the owner retires.

## P1 — the destructive path is now linkable, and the budget check earned its cost (2026-09-23)

### Landed: `resetLoopSlotToEmpty` extracted, and it *improved* the RAM1 line

`StorageManagerInternal::resetLoopSlotToEmpty` — the destruction the load path performs on a slot whose
payload is absent or invalid, and the function B04 exists to prove is **not** called — moved out of the
`StorageManager.cpp` monolith into `src/StorageManager/SlotReset.cpp`. It is small and self-contained (it only
calls `Loop` methods) and now **FLASHMEM**.

The move turned out to be a net win, and the enforcement caught both halves of it:

| step | measured |
|---|---|
| first cut, function left without `FLASHMEM` | `code 392,440 / padding **776**` — the post-link budget check **FAILED**, correctly: the extraction had pushed 64 B of code into ITCM |
| with `FLASHMEM` applied | `code 392,184 / padding **1,032**` — **+192 B of headroom** over the previous line, reported as headroom rather than absorbed |

So the change does not merely pay for itself; it recovers 192 B of the margin this change had spent, and the
floor stays at 840 (the check reports the headroom and does **not** re-base anything automatically, as
instructed). A useful side effect of the split: a suite can now link the destructive function without the
monolith, which is exactly what B04 needs.

### The live-owner fixture: one symbol from linking

Re-adding it now fails to link on a **single** symbol,
`StorageManagerInternal::storageIoFromFileRead(File&)`, which lives in the heavyweight boot-load TUs
(`CurrentSetBootLoad.cpp`, `SavedSetIo.cpp`, `LegacyMonolithLoad.cpp`) and would pull a large part of the
persistence layer behind it. That is a deliberate stopping point, not an unknown: the fixture's design is
recorded above and the remaining work is "link a boot-load TU, or stub the one read adapter at the SD
boundary".

Nothing was committed from the fixture; it was reverted so the suite stays green at 4/4.

### Verification

| check | result |
|---|---|
| load suite | **4/4** |
| full native | **2,276 cases / 1 skipped / 2,275 succeeded / 0 failures** |
| firmware + budget | SUCCESS, `variables 96,128 / ITCM 393,216 (code 392,184 + padding **1,032**) / locals 34,944` |

## P1 — S2b.5 landed: a load no longer destroys a live owner's slot (2026-09-23)

The fixture went **red** through the real production entry, and the fix makes it green.

### The red fixture

`test_b04_real_load_selection_keeps_the_request_while_the_slot_is_owned` builds a genuine live finalization
owner — a committed slot, an interval opened on the Loop, an overdub admitted through
`Track::beginDeferredOverdubStop` — force-enqueues a restore for **that** slot through the boot path, and runs
the production `StorageManager::selectSubmittedLoadJobs()`. Before the fix it failed exactly as predicted:

```
Expected 1 Was 0. a queued restore for a slot with a live finalization owner must stay queued
```

So `selectSubmittedLoadJobs()` popped and consumed a restore request for a slot a live owner still held — the
defect S2b.5 describes, reproduced through the production entry rather than inferred.

### The fix

Four pop sites in `LoadLoopJob.cpp`, each now guarded **before** the pop by one predicate and one peek helper:

- `ensureActiveLoadLoopJobSelected`: the parked `BeginFocusFromQueue` branch, the empty
  `BeginFocusFromQueue` branch, and its `popNextDeferredLoopSlotRestore` fallback;
- `EmptyIdleAction::BeginBackgroundFromQueue`.

`queuedRestoreMayBegin(trackIndex, slotIndex)` is `trackManager.getTrack(track).noLiveOwnerForSlot(slot)` —
the S4.0 predicate, so liveness is **per slot**: a finalizing predecessor on one slot does not block a load
into another. The two `popNext` sites have no track/slot filter, so `backgroundHeadMayBegin()` peeks the head
(which is what the pop would take) and checks liveness against *it*. A blocked slot now leaves its request
**queued** and the load is admitted once the owner retires; nothing is discarded and no reset runs.

### RAM1 side effect: the prerequisite paid for itself

Extracting `resetLoopSlotToEmpty` to FLASH (see the previous section) moved code *out* of ITCM, so the line
improved rather than degraded: `teensy41-capture-serial` `padding 840 → **1,032**` and `teensy41`
`padding 9,768 → **9,960**`. Both are recorded in `scripts/ram1_budget.json`; the **floor stays 840** and the
check keeps reporting headroom rather than re-basing anything.

### Verification

| check | result |
|---|---|
| load suite | **5/5 PASSED** (B04 green in the default gate) |
| full native | **2,277 cases / 1 skipped / 2,276 succeeded / 0 failures** |
| firmware + budget, both envs | SUCCESS; `teensy41-capture-serial` padding **1,032**, `teensy41` padding **9,960** |

S2b.5's own acceptance also wants "after retirement, restoration happens exactly once" — that half is the
next increment's assertion, on the same fixture, and is not claimed here.

## P1 — S2b.5's retirement half: the blocker is a harness artifact, and it is named (2026-09-23)

The fixture's second half — "after retirement, restoration happens exactly once" — needs the admitted job
drained to retirement (`Track::processDeferredOverdubStopGrain`), and that surfaced a **harness** problem
rather than a production one. Nothing was committed from the attempt; the suite stays at 5/5.

### The chain, and why it is an artifact

- Draining the job makes `Track::stepDeferredUndoCollection` live, which needs
  `TrackUndo::prepareOverdubSessionEntry` from `src/TrackUndo.cpp`.
- Linking `TrackUndo.cpp` collides with the already-linked `MidiButtonActions.cpp`:
  **both define `restorePlaybackAfterSlotClear`** — `TrackUndo.cpp` at namespace scope, and
  `MidiButtonActions.cpp:118` inside an **anonymous namespace**. Separate translation units never collide;
  an include-based harness concatenates them into one, so the two definitions meet.
- Adding the obvious seam (`HARNESS_OMITS_MIDI_BUTTON_ACTIONS`, guarded in `track_harness.inc` and left in
  place for the next attempt) makes the build fail differently: `off_path_deps.h` stubs
  `ControlSurfaceManager`, `MidiFaderActions` and friends whose **headers** arrive only by way of
  `MidiButtonActions.cpp`. Omitting the TU removes the declarations too.

So the obstacle is the include-based harness's single-TU assumption plus `off_path_deps.h`'s reliance on a
neighbouring TU's includes — not the load path, and not anything S2b.5 changes. The two workable routes for
the next attempt are (a) let the load suite carry its own minimal stub surface instead of reusing
`off_path_deps.h`, or (b) give the shared harness an explicit "include the button headers, not the button
TU" split. Either is a contained test-infrastructure change.

### Verification (unchanged)

| check | result |
|---|---|
| load suite | **5/5** |
| full native | **2,277 cases / 1 skipped / 2,276 succeeded / 0 failures** |
| `test_continuous_capture` | 87/87 |
| firmware + budget | SUCCESS, `teensy41-capture-serial` padding **1,032** |

S2b.5 remains **open** on this half plus the device gate. Its primary acceptance — a load must not destroy a
live owner's slot, proven through the production selection entry — is landed and green.

## P1 — route (a) done: the load harness carries its own stub surface (2026-09-23)

The obstacle recorded in the previous section is gone. The load suite no longer reuses
`test_continuous_capture/off_path_deps.h`; it carries `test/test_storage_manager_load/load_harness_stubs.h`
and **links `src/TrackUndo.cpp`**, which is what the retirement half needs.

### How it was resolved, and the measurement that made it cheap

Removing `off_path_deps.h` and rebuilding listed exactly **six** missing symbols — so the minimal stub
surface was measured, not guessed:

| symbol | treatment |
|---|---|
| `resolveTrackIndexForPersistence(const Track&)` | a pointer comparison over the track array; stubbed locally |
| `vtable for EditNoteHomeState` / `EditSelectNoteState` / `EditStartNoteState` / `EditLengthNoteState` / `EditPitchNoteState` | emitted by out-of-line virtuals; the five state headers are included and their virtuals are defined fail-closed |

With that in place the note-edit button TU is omitted from this suite
(`HARNESS_OMITS_MIDI_BUTTON_ACTIONS`, the guarded seam added earlier), which is what lets `TrackUndo.cpp`
link at all. The earlier failure was circular — omitting the button TU removed the edit-state headers
`off_path_deps.h` needed — and owning the stub surface breaks the cycle. Nothing in the load path itself is
stubbed: the queue, selection policy, admission, the SD adapter, `needsSlotLoad` and `resetLoopSlotToEmpty`
are all real production code.

### Verification

| check | result |
|---|---|
| load suite | **5/5** |
| full native | **2,277 cases / 1 skipped / 2,276 succeeded / 0 failures** |
| `test_continuous_capture` | 87/87 |
| firmware + budget | SUCCESS, padding **1,032** |

### Next, and it is a small step

The retirement half's assertions are written and were one edit from landing when this stretch ran out: drain
the job with `TrackCaptureStopTestAccess::step`, assert `noLiveOwnerForSlot(0)` is then true, run
`selectSubmittedLoadJobs()`, and assert the retained request is consumed **once**
(`pendingLoopSlotRestores_.count == 0` and `isLoopSlotRestoreAttempted(0, 0)`), with a second selection
consuming nothing. The harness now links everything that needs.

## P1 — B04 complete: both halves green (2026-09-23)

`test_b04_real_load_selection_keeps_the_request_while_the_slot_is_owned` now covers the whole S2b.5
acceptance:

1. **While the slot is owned:** a queued restore survives the production `selectSubmittedLoadJobs()` — the
   entry is still queued and the slot's content is untouched. (This is the assertion that was **red** before
   the fix; it is the discriminating one.)
2. **After the owner retires:** the retained request is resolved exactly once. Building the interval through
   the production `Track::startOverdubbing` (rather than poking Loop state) is what makes the job able to
   finalize — a hand-built interval admits a job that can never retire, which the first attempt at this half
   discovered. Once it retires, the committed content **satisfies** the queued restore, so the request is
   resolved as unnecessary: no load is begun, no attempt is marked, and nothing resets the slot. A second
   selection consumes nothing.

That correction matters for the oracle: "restoration happens exactly once" for a slot whose finalizing
interval *committed its content* is a resolution, not a load. The fixture asserts the outcome that is
actually correct rather than the one the task text's phrasing suggested.

### Harness additions this increment

- `test/test_storage_manager_load/load_harness_stubs.h` gained the note-edit fold stubs
  (`commitAllPendingNoteEditActions`, `foldLiveCaptureIntoNoteEditSession`, both fail-closed — the production
  start reaches them only with a note-edit session active, which a load fixture never has) and the
  `silenceTrackMidiOutput` counting boundary (the counter itself is one of the shared harness globals).

### Verification

| check | result |
|---|---|
| load suite | **5/5 PASSED** |
| full native | **2,277 cases / 1 skipped / 2,276 succeeded / 0 failures** |
| `test_continuous_capture` | 87/87 |
| firmware + budget | SUCCESS, `teensy41-capture-serial` padding **1,032** |

**S2b.5's native acceptance is now met.** The task stays open only for its device gate, which the S1 resident-library
baseline owns.

## P1 — B06's eligibility decision extracted, and the predicate is now discriminating (2026-09-23)

The architect's step 3: lift `main.cpp`'s scheduler eligibility into testable production code so the
stopped-track reachability gap has a single, assertable home.

### What landed

- **`Track::hasRealtimeDeferredIdleWork()`** — `isRecording() || isAudiblePlaybackActive() ||
  isStoppedRecording() || hasAcceptedUnexecutedInteraction()`. The last term is the point: a track holding an
  **accepted, not-yet-executed** interaction must never be skipped, or its drive would never be reached.
- **`src/main.cpp`'s `hasRealtimeIdleWork` now delegates to it**, keeping its name and both call sites
  unchanged, so the scan has one owner and a later stage (a pending switch, a pending reconstruction) adds its
  term in one place instead of editing the sketch.

### The fixture is discriminating, not decorative

`test_s4f2_deferred_eligibility_includes_an_accepted_interaction_on_an_idle_track` puts the track in
**`TRACK_STOPPED`** and asserts it has no realtime deferred work, then latches an accepted interaction
directly (through the friend surface, so no admission happens) and asserts it now does. In `STOPPED`,
`isRecording()`, `isAudiblePlaybackActive()` and `isStoppedRecording()` are all false, so **only the latch
term can satisfy the predicate** — the assertion cannot pass for any other reason.

What this does **not** prove, and is recorded as such: that the scheduler actually reads the predicate for a
stopped track. That is still `src/main.cpp`, which is not native-linkable; the extraction makes the *decision*
testable, and the scheduler's use of it remains a device-observable claim.

### RAM1 side effect, again positive

Moving the scan out of the sketch recovered a further 64 B of ITCM: `teensy41-capture-serial`
`padding 1,032 → **1,096**`. Across this change's S2b.5/B06 extractions the padding line has gone
**840 → 1,096**, so the 32 B this change originally spent is more than repaid. `scripts/ram1_budget.json`
records the new measured margin; the floor stays 840.

### Verification

| check | result |
|---|---|
| `test_continuous_capture` | **88/88** (+1 eligibility case) |
| load suite | 5/5 |
| full native | **2,278 cases / 1 skipped / 2,277 succeeded / 0 failures** |
| firmware + budget, both envs | SUCCESS; capture-serial padding **1,096**, plain env **9,960** |

## P1 — the latch drive's declared bound, and the canonical-prefix yield (2026-09-23)

The last two S4f.2 items.

### The canonical-prefix child yields to the accepted interaction

`Track::processDeferredCanonicalPrefix` already refused while an *admitted* stop was pending
(`hasPendingDeferredOverdubStop()`). It now also refuses while one is merely **accepted**
(`hasAcceptedUnexecutedInteraction()`), so background canonical preparation cannot compete for the deferred
turn the accepted stop's drive needs.

**No new fixture, and the reason is recorded rather than glossed:** the change is a strict narrowing — it can
only *prevent* background work, never add any — and a discriminating oracle would have to construct a state
where the function would otherwise proceed to real canonical work. The guard-clause assertion would be
non-discriminating (a `PLAYING` track can decline for unrelated reasons such as no allocated runtime or
`canonicalPreparationHasPriorityWork`), so asserting it would have looked like coverage without being any.
The existing full native suite plus the firmware build are the checks for this step, which is why the task note
says the fixture is *not* claimed.

### `ExecutionBounds.md` row 31

The latch drive is now a declared owner with a stated bound, as the Closeout section requires:

> `Track::serviceAcceptedInteractionTurn` → `driveAcceptedInteraction` →
> `parkAcceptedStopForCanonicalOutput` — every deferred idle-owner lane, immediately after the admitted stop's
> grain and before the owner-specific branches. **At most one accepted-interaction grain per turn**: one
> admission attempt for the interval's slot plus, on refusal, one canonical-reservation replenish and one wait
> report. The drive is skipped while the ring is non-empty, so an accepted request never stacks a second stop
> job. The park never freezes, never exchanges staging roles, and never cancels the request.
> **Structurally bounded**; device latency for the acceptance-to-execution wait remains open.

### Verification

| check | result |
|---|---|
| full native | **2,278 cases / 1 skipped / 2,277 succeeded / 0 failures** |
| load suite | 5/5 |
| firmware + budget | SUCCESS, capture-serial padding **1,096** (unchanged) |

## P1 — S4f.4 transition 3: a stale request is superseded, not parked (2026-09-23)

Writing the load/replacement transition found a **defect in the drive**, so this is a fix, not only a fixture.

### The defect

`driveAcceptedInteraction` re-derived the destination and the boundary tick from live state — correct — but on
any refusal it went straight to `parkAcceptedStopForCanonicalOutput`. For a request whose **interval is gone**
(a load or a replacement left the same slot number occupied by different content) the admission refuses with
`stale_interval`, and the park then waits for a canonical reservation that has nothing to do with the failure.
The obligation would be retained forever, and a slot belonging to a different interval could never be started
or switched past, because `hasStopInFlight()` stays true.

### The fix

The drive now recognises the case explicitly, before any admission attempt:

> a request whose `slot` is out of range or whose `interval != captureInterval_` is **superseded** — it is
> reported (`"Accepted stop superseded: its interval is no longer live"`), the obligation is consumed exactly
> once, and nothing is admitted.

That is the contract's own rule for a replaced interval; the difference from the pre-fix behaviour is that it
is *decided* rather than left to a park that cannot succeed. A load/replacement can therefore never bind an
old request to replacement content, and it can never wedge the track either.

### The fixture, and it is discriminating

`test_s4f4_a_request_whose_interval_is_gone_is_superseded_not_bound`: latch an accepted stop for slot 0, capture
its interval identity, advance the interval (the same slot number now belongs to replacement content), drive
once, and assert the obligation is **retired**, no job is admitted, the slot's content is untouched, and a
second drive has nothing to do. Pre-fix this assertion fails at the first step: the drive parks and the latch
remains set.

### Verification

| check | result |
|---|---|
| `test_continuous_capture` | **89/89** (+1) |
| load suite | 5/5 |
| full native | **2,279 cases / 1 skipped / 2,278 succeeded / 0 failures** |
| firmware + budget | SUCCESS, capture-serial padding **1,096** (unchanged) |

### S4f.4 status

Transitions **1, 3 and 4** are now green natively. Transition **2** (seek/reset/MIDI Start) has no native
entry — the `ClockManager` seek/reset/MIDI-Start entries are harness stubs — so the task stays open on that one
plus the device gate.

## P1 — S4f.4 transition 2: the boundary is recomputed at execution (2026-09-23)

The last of the four paused-worker transitions reachable natively.

`test_s4f4_the_boundary_is_recomputed_in_the_tick_domain_at_execution`: with the interval live, latch an
accepted stop at tick 96 and record the latch's stored `phaseTick`; then move the tick domain (a seek) before
the request executes; drive; and assert the admitted job's boundary is **not** the stored tick and lies in the
new domain. Pre-fix — if the drive had used `request.phaseTick` — the two values would be equal and the first
assertion would fail, so the case discriminates on the property that matters.

**Coverage limit, stated in the fixture itself:** `ClockManager`'s own seek / reset / MIDI-Start entries are
harness stubs, so the tick domain is moved with the harness's controlled clock rather than through a
production seek. That proves the *decision* (the drive passes the live tick, never the stored one) but not the
production entry that moves the domain; the latter is device territory.

### S4f.4 status — all four transitions green natively

| # | transition | status |
|---|---|---|
| 1 | Transport Stop → Start: no PLAYING regained, no resurrected start | green (B03) |
| 2 | Seek/reset/MIDI Start: boundary recomputed in the new tick domain | green (this case; decision-level) |
| 3 | Load/replacement: a stale request is superseded, not bound | green — and it fixed a real wedging defect |
| 4 | Repeated retries: exactly one stop commits | green (B03) |

### Verification

| check | result |
|---|---|
| `test_continuous_capture` | **90/90** (+1) |
| load suite | 5/5 |
| full native | **2,280 cases / 1 skipped / 2,279 succeeded / 0 failures** |
| firmware + budget | SUCCESS, capture-serial padding **1,096** (unchanged) |

**S4f.4's paused-worker transition set is met natively.** The task stays open only for the device leg of the
`ClockManager` seek/reset entry.

## Device session — pre-change baseline revision and the measurement recipe (2026-09-23)

The architect's instruction: *"Preserve a reproducible pre-change firmware revision for the baseline
measurements; measurements of the modified firmware alone cannot establish the original slope."* This section
pins that revision so the device session can produce a **comparison**, not a snapshot.

### The pinned baseline

**`83be2e19`** — *"test(working-set): add the P1/S2b harness controls and the B01 red oracle"* — the last commit
before this change's first firmware edit (`7b998d0e`, the accepted-stop latch).

It is **firmware-identical to the branch base `7f90d996`**, and that is proven rather than asserted:

```sh
git diff --name-only 7f90d996 83be2e19 -- src include lib platformio.ini   # -> empty (0 files)
git diff --name-only 83be2e19 HEAD     -- src include platformio.ini       # -> 22 files
```

So the baseline binary is exactly the pre-change firmware, and the 22-file list is this change's whole firmware
footprint. The baseline was **measured** on that revision (this session's first build, recorded in § P0):

> `RAM1: variables 96,128 / code 392,344 / padding 872 / locals free 34,944`; `FLASH: code 1,009,608`

versus the current revision:

> `RAM1: variables 96,128 / code 392,120 / padding **1,096** / locals free 34,944`; `FLASH: code 1,010,976`

Both hold ITCM at 12 banks. **The change improved the padding line by 224 B** (872 → 1,096): one ITCM bank
cross was never approached, and the extractions of `needsSlotLoad`, `resetLoopSlotToEmpty` and
`hasRealtimeDeferredIdleWork` to FLASH more than repaid the 32 B the latch spent.

### Reproducing the baseline

`lib/SSD1322_OLED` is a **git submodule**, so a fresh worktree of the baseline needs it initialised first:

```sh
git worktree add --detach /tmp/rwso-baseline 83be2e19
cd /tmp/rwso-baseline && git submodule update --init --recursive
pio run -e teensy41-capture-serial -t upload     # baseline firmware
```

(The first attempt at this failed with `SSD1322.h: No such file or directory` — that is the un-initialised
submodule, not a source problem, and it is recorded here so the device session does not lose time to it.)

### What the device session must record, per leg

**S1.1 per-owner accounting** — working-set bytes by owner (views, capture staging, companion
reconstruction/apply/bridge-map/passes, canonical pass inputs, transfer horizon, stop-job capacity, prepared
pair), per-loop derived bytes (snapshot slabs, whole-loop `VisualCache`), lease holder count, owning slot,
role. The tag vector and the slab census already exist (`ExternalPoolAccounting`, `DebugSessionCapture`); no
new telemetry is required, and instrumentation stays behind the session-capture gate.

**S1.3 device slope to the current failure point** — grow the library until residency or canonical capacity
fails; record the slope, the failure point and the error. *This is the leg that needs the baseline*, because
the claim being tested is that the **boot reservation slope** is gone: on `83be2e19` boot reserves 8 chunks
(41,056 B) per committed slot, so ~N committed slots should show a slope that the current revision does not.

**S1.4 undo residency and canonical pinning (B10)** — how much snapshot residency cold slots retain through
undo roots, and how many canonical chunks `SlotPassReferences` pins.

**S1.5 boot pool census** — `freeChunkCount()` after boot with N committed slots (the native leg already
asserts the per-committed-slot reservation is **zero** and records boot's constant 40-chunk descriptor cost),
plus the smallest N at which a 24-bar overdub is refused.

**S2b device gate** — the S1 resident-library baseline with room for canonical content: demonstrate the
per-slot **reservation** slope removed and a 24-bar overdub where the measured capacity permits it, reporting
canonical payload, reservation and derived bytes separately (total pool live is not expected flat yet).

**HITL entry point** — the authoritative preset or the `hitl_stop_interruption.py` wrapper, with content proven
by the `hitl-content-verification` pipeline (not the base run's own verdict). Nothing may be flashed without the
owner's go-ahead, and device SD is user data: quarantine, never delete or rename, without same-session
permission.

## P1 — S1.5's third clause does not reproduce natively, and that is recorded (2026-09-23)

S1.5 asks for three things: the chunks/bytes `prewarmPlaybackRuntime` reserves per committed slot, the free
chunk count, and **the smallest N at which a 24-bar overdub is refused**. The first two are answered
(§ P1 — S2b.3: the per-committed-slot reservation is **zero**, and boot's own cost is a constant 40 chunks for
the default enabled set). The third was attempted as a native sweep and **reverted**, because it measured
something else.

### What the sweep did, and why its answer is not the asked-for number

The fixture committed 24-bar loops (~72 events/bar) into slots one at a time, re-ran
`prewarmPlaybackRuntime()` after each, and probed the load-time admission with
`prewarmPlaybackForSlot(probeSlot, true)` on a slot deliberately left empty. It reported its first refusal at
**N = 2**.

That number cannot be the capacity boundary the task means:

- the native pool is **heap-backed**, so it does not truly exhaust the way the device's fixed PSRAM pool does —
  it is bounded only by `POOL_CHUNK_COUNT` (512 ids), and two 24-bar loops hold on the order of 32 of them;
- so the refusal at N = 2 comes from a **different admission condition** inside the arm prewarm (a
  windowed-only / companion-horizon / whole-take admissibility branch), not from exhaustion.

Publishing "2" as "the smallest library that refuses a 24-bar overdub" would therefore have been a misleading
number — exactly the kind of unverified capacity claim invariant 3 forbids. The fixture and its helper were
reverted; the suite stays at 90/90.

### What this means for the task

S1.5's third clause needs either (a) a construction that reaches the **id** ceiling deterministically (fill the
pool with a known chunk cost, probe at the boundary, and assert the refusal *reason*), or (b) the device leg,
where the pool is real and the failure point is the one S1.3 is specified to record. Recording that the naive
form does not work saves the next attempt the same detour.

### Verification

| check | result |
|---|---|
| `test_continuous_capture` | **90/90** (sweep reverted) |
| full native | **2,280 cases / 1 skipped / 2,279 succeeded / 0 failures** (run after the revert) |

## P1 — CORRECTION: the S1.5 "N=2 refusal" was a misread seal failure (2026-09-23)

**The earlier statement in this section and in `tasks.md` that the sweep "reported its first refusal at N = 2"
is wrong, and the reasoning built on it is withdrawn.** Re-running the sweep with the refusal step classified
showed the truth: the failure was at the *first commit*, not at an admission probe —

```
test_continuous_capture.cpp:4268: Expected 0 Was 2
```

Line 4268 is the fixture's own `TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(kTicks, RecordReason::RecordStop))`,
and `SealOutcome` value 2 is **`FailedValidation`** (`LoopPasses.h:25-31`: Ok, SkippedEmpty, FailedValidation,
AlreadyPending, PoolExhausted). So the dense 24-bar loop **never sealed**, the sweep never reached an admission
probe, and no refusal was measured at all. I read "Was 2" as a slot count; it was an enum.

Two consequences, both recorded rather than smoothed over:

- **S1.5's third clause is untouched by the earlier finding** — it is not "attempted and shown not to reproduce",
  it is **not yet attempted with a valid construction**. The open question is now concrete and upstream: a
  24-bar loop at ~72 events/bar fails `sealCapture` validation in this harness, so the scenario needs a
  construction that seals first. That is the next thing to establish, and it is a property of the *capture
  validation path*, which is worth knowing in its own right.
- The arm-repeatability guard added alongside it is unaffected and still correct: three successive
  `prewarmPlaybackForSlot(0, true)` grants do succeed.

This is a reminder the project's own rules anticipate: a failing assertion is evidence only once its **exact**
assertion is identified — "Expected 0 Was 2" was read as a measurement when it was an enum.

## P1 — S1.5's refusal-N: the cause cannot be distinguished natively (SUPERSEDED by the correction above)

One hypothesis for the reverted sweep's N=2 refusal was a **non-idempotent record arm** — a real defect if
true, and one that would also mean re-granting contributed to the per-slot slope the change removes. Probed
directly: three successive `prewarmPlaybackForSlot(0, true)` grants on an otherwise idle slot all succeed, so
the arm is **exonerated**. The probe is kept as `test_record_arm_admission_is_repeatable`, because it also
guards that re-granting never becomes a slope.

That leaves the actual cause of the N=2 refusal unknown, and the reason is structural rather than incidental:

> **`prewarmPlaybackForSlot` returns only a bool.** The reason for a refusal is emitted by
> `MemoryMonitor::reportPrewarmAdmissionDenied` as `#MEM_DENIED`, which is a **device** diagnostic. Natively
> the admission answers true or false and nothing more, so a refusal sweep cannot distinguish "the pool is
> exhausted" from "some other admissibility branch declined" — which is exactly the ambiguity that made the
> reverted number misleading in the first place.

So S1.5's third clause is **not natively answerable without a new seam**. The two honest options, in the
project's own terms:

1. **A test-only reason capture at the admission boundary** — DEC-080 permits test-only adapters at
   "clock, allocator, I/O or observation boundaries", and this is an observation boundary. It would let the
   sweep assert *why* it refused, which is what makes the number a capacity characterisation rather than a
   coincidence.
2. **The device leg**, where `#MEM_DENIED` carries the reason and S1.3 already records the failure point.

Neither is a native quick win, and inventing a number without the reason is the thing invariant 3 forbids. The
recorded outcome of this round is therefore: one hypothesis eliminated with a kept regression guard, and the
clause's real requirement (a reason-capturing seam, or the device) stated precisely.

### Verification

| check | result |
|---|---|
| `test_continuous_capture` | **91/91** (+1: the arm-repeatability guard) |

## P1 — the dense-loop seal: one half established, one half open (2026-09-23)

Following the S1.5 misread correction, the upstream question was *why a dense 24-bar loop fails
`sealCapture`*. Progress, stated with its exact limits:

### Established (and kept as a guard)

`test_dense_loop_seal_requires_the_transfer_horizon` asserts that a dense 24-bar loop **fails validation when
no transfer horizon has been admitted** — `SealOutcome::FailedValidation`, discriminating in the sense that the
same helper seals a small loop against an untouched pool. Read against `Loop::sealCapture`, the failing branch
is the transfer precondition (`preparePendingCapturePassTransfer`), which is why the requirement exists at all.
This is the production reason S2b.2 admits the horizon **at the arm** rather than at the stop: without it, a
dense capture cannot even be sealed, so it could never reach a stop.

### Open, and deliberately not guessed at

**Admitting the arm's transfer horizon first does not make that construction seal** — it still returns
`FailedValidation`. So the horizon is **necessary but not sufficient**, and the remaining cause is not
identified. It is not content volume either: halving the loop to 1,200 events still failed, well inside the
horizon's 8-chunk (2,048-event) credit. The fixture therefore asserts only the proven half and names the
converse as unestablished, rather than encoding a guess.

### What this means for S1.5's third clause

Still not measurable, and now for a *known* reason at the first step: the scenario needs a 24-bar capture that
seals at all, and this harness's dense construction does not. Either the remaining seal failure is diagnosed
next, or the clause is answered on device where the load path reads from SD rather than sealing a fresh capture
— which is, after all, what a *loaded* library is.

### Verification

| check | result |
|---|---|
| `test_continuous_capture` | **92/92** (+1: the horizon-necessity guard) |
| full native | **2,282 cases / 1 skipped / 2,281 succeeded / 0 failures** |

## P1 — RETRACTION: the "seal needs the transfer horizon" finding is falsified (2026-09-23)

The previous section's established half is **withdrawn**, by its own bisect. One round after publishing it, the
control case settled it:

- the dense loop **with** the horizon admitted also returns `FailedValidation`, and so does a **tiny** loop
  (4 note pairs) with the horizon admitted.

So the horizon is not the discriminator and my ordering was the variable I had failed to control. What the
bisect actually shows:

**`loop.appendCaptureEvent` is not the production ingress once the arm has admitted capture staging.** The
working harness pattern (`preparePlayingTrack`) records into an untouched slot and prewarms the arm *after*
committing; the production path prewarms the arm first and then feeds input through `Track`'s staged ingress,
not through `Loop::appendCaptureEvent`. My control case prewarmed the arm and *then* appended through the Loop,
so the seal sourced a capture that did not contain those events — `FailedValidation` from the empty/detach
branch, not from the transfer precondition.

Two consequences, both recorded:

1. **The "horizon is necessary" claim is retracted.** The original (a)-case failure is equally consistent with
   the same empty/detach branch, because I never identified which of the four `FailedValidation` returns fired —
   I inferred it from reading the code after seeing a failure that my own construction produced. The fixture and
   its helper are reverted; nothing is claimed. This is exactly the "evidence only once the exact assertion is
   identified" rule again, one level deeper.
2. **The "useful knowledge" is also withdrawn (2026-09-23, later the same day).** The staging-order rule was
   itself an artifact of the uncontrolled variable: with pairs >= `DEFAULT_NOTE_MIN_LENGTH_TICKS` apart, all
   three append/prewarm orders seal. The real cause is the hot-stop **min-length removal** running before the
   seal's empty check — see § P1 — S1.5 clause 3. Kept visible rather than deleted, because it is the second
   consecutive inference drawn from a construction that was itself the problem.

### Where S1.5's third clause stands

Unchanged in substance and now free of a wrong intermediate claim: it needs a **staging-correct** dense-loop
construction (append through the staged ingress, or append before prewarming), and only then can the refusal
point be measured — or it is answered on device, where a loaded library is read from SD rather than sealed.

### Verification

| check | result |
|---|---|
| `test_continuous_capture` | **91/91** (fixture and helper reverted) |
| full native | **2,281 cases / 1 skipped / 2,280 succeeded / 0 failures** |

## P1 — S2b.4's mid-copy fixture: the refusal is the safety property (2026-09-23)

S2b.4's outstanding item was the mid-copy fixture: a populated canonical store must **refuse** release.
`test_s2b4_midcopy_populated_store_refuses_release` now drives it, and the implementation was already correct —
this increment is the evidence, not a fix.

**How the mid-copy state is reached.** Through production APIs only. `prewarmCanonicalCaptureOutput()` reserves
the credits, then one `LoopEventStore::appendReserved(event, canonicalCaptureChunks)` puts an event in the store
— which is exactly what `stepFrozenCaptureCanonicalCopy` does between quanta
(`TrackPlaybackRuntime.cpp:1949`, the same store through the same reservation). No member is poked; the event's
payload is irrelevant because the predicate only asks whether the store is empty. The two hooks
(`appendCanonicalEvent`, `clearCanonicalOutput`) live in the fixture's `TrackCaptureStopTestAccess`.

**The fixture is discriminating, and that was verified by removal.** With
`if (!canonicalCaptureOutput.store.empty()) return false;` deleted from
`LoopPlaybackRuntime::releaseCanonicalCaptureReservation`, the suite goes **1 failed / 91 succeeded**, failing on
precisely the intended assertion — *"a populated store must refuse release: releasing mid-copy strands the
copy"*. The guard was restored; `test_continuous_capture` then reports **92/92**.

**A first assertion of mine was wrong, and the failure was informative.** I had captured the credit count
*before* appending and asserted it was unchanged across the refused release; the run answered
`Expected 8 Was 7`. That is not a leak: **appending the copied event spends the credit that backs it**, so the
reservation legitimately drops to 7 — and that is the mid-copy signature itself, and the reason releasing there
would strand the copy (the credit is already committed to an event the pool would otherwise re-issue). The
fixture now asserts that signature explicitly (`afterAppend < reserved`) and then that the refused release
returns no *further* credit. The slot is deliberately given no live owner, so the populated store is the only
admissible refusal reason — and the same call succeeds after the store is drained, so the test cannot pass on an
unrelated guard.

**The refcount census is now part of the fixture, and it took three corrections to get right.** The final
assertions are: `preparePlayingTrack()` has already admitted the slot's reservation, so `usedChunkCount() = 13`
*includes* the 8 held credits and `freeChunkCount() = 499`; the append into a held chunk moves **neither**
counter while spending one credit (8 → 7); the refused release moves nothing at all; and `clear()` + release
returns exactly the reservation's own credits — `used` 13 → **5**, `free` 499 → **507**. That is the leak
property S2b.4 asks for: the release gives back its own credits and nothing else.

The three wrong cuts are recorded because the pool's accounting is easy to mis-model, and each failure was the
evidence:

| my predicted model | pool's answer | what it established |
|---|---|---|
| reservation raises `used` by 8 (21) | **13** | `preparePlayingTrack()` had already taken the reservation, so my baseline was post-reserve |
| the appended event raises `used` by 1 (14) | **13** | writing into an already-held chunk allocates nothing new |
| `used` excludes held credits, so release leaves it at 13 | **5** | `usedChunkCount()` counts *allocated* chunks — held **and** committed |

So `usedChunkCount()`/`freeChunkCount()` are complements over all allocated chunks, and a reservation is visible
in them; the invariant worth asserting is the exact ±credit-count symmetry across release, which is what the
fixture now asserts.

### Verification

| check | result |
|---|---|
| `test_continuous_capture` | **92/92** |
| same suite, guard removed | **1 failed / 91 succeeded** on the intended assertion |
| full native | **2,282 cases / 1 skipped / 2,281 succeeded / 0 failures** |

## P1 — S2b.6: a stale annotation, and a design/spec conflict the terminal must not paper over (2026-09-23)

Two findings from picking up S2b.6 — one correction, one decision this change must not make by accident.

**The "pool-pressure fixture" S2b.6 listed as outstanding already exists and is registered.** No new fixture
was needed:

- `test_b02_accepted_stop_waits_for_canonical_credit_then_executes_once` drains the canonical credits, issues
  the stop **exactly once**, and asserts the accepted stop stays in the live interval with no job, no descriptor
  and identical capture rows, then admits when capacity returns and retires exactly once with no second request.
  That is the task's fixture clause almost verbatim.
- `test_s2b2_loaded_slot_stop_acquires_its_own_horizon_and_canonical_target` covers the "loaded slot" variant the
  task text names.

Both are in `RUN_TEST` and green in the current suite (92/92), so the annotation was stale and has been corrected
rather than re-implemented.

**`accepted_stop_park_terminal` is blocked on a design/spec conflict, and resolving it in the park would break the
invariant I just proved.** `design.md:1104` says the park "routes a busy owner + non-empty store" to a new
`clearCanonicalCaptureOutput()` (or to `regenerateFrozenCaptureStaging`) *and* reports the terminal. But clearing a
populated canonical store **while a copy is in flight is exactly what S2b.4 forbids** — the invariant
`test_s2b4_midcopy_populated_store_refuses_release` now proves by guard removal. The normative spec scenario
(`spec.md:290-296`) sides with S2b.4, not with the design note: the terminal is a **report-only
fault/escalation**, the accepted interaction "remains authoritative and executes exactly once when the owner
frees", the park "never abandons the stop", and "the user is never asked to press again".

So the terminal must be **report-only**: it cannot clear, drain or cancel. Two consequences are recorded rather
than guessed at:

- The design note's "route ... to `clearCanonicalCaptureOutput()`" should be read as describing the **retire**
  route, which already clears inline past `DeferredCleanup` (so no live copy is disturbed) — not as licence for
  the park to clear a store a live owner holds.
- A fixture for the terminal needs an **observable counter** (the way `waitCount` backs
  `accepted_stop_finalizer_wait`), and `PendingInteractionLatch` is size-pinned by S3.0's placement asserts
  (`sizeof(CaptureStopJobs) == 2728`), so whether to spend a latch byte or key the report off the existing
  `waitCount` is a deliberate choice with a placement consequence.

**IMPLEMENTED 2026-09-23, report-only and red-proven.** `parkAcceptedStopForCanonicalOutput` now reports
`accepted_stop_park_terminal` when waiting has become **stalled**: the canonical target is held by a live owner
(a populated store the park must not clear) and the reservation cannot be acquired, so no deferred turn can admit
the stop until that owner frees. It clears nothing, drains nothing and cancels nothing — invariant 31 and
`spec.md:290-296` keep the accepted stop authoritative, so it still executes exactly once when the owner frees.

Two implementation choices worth recording:

- **No new latch field.** `PendingInteractionLatch` is size-pinned by S3.0's placement asserts
  (`sizeof(CaptureStopJobs) == 2728`), so the report keys off the existing per-latch `waitCount` with `== 8`.
  The count passes through the threshold once however long the wedge lasts, which is exactly "once per episode".
- **Zero RAM1 cost.** ITCM padding is unchanged at **1,032 / 192 B above the floor**, because the park is
  `TRACK_COLD_MEM` (FLASH-resident) — so unlike the S1.4 pin census, a diagnostic call here spends no ITCM.

**The fixture is discriminating against the reading I rejected.** `test_s2b6_park_terminal_never_cancels_an_accepted_stop`
establishes the wedge (populated store + accepted stop), drives **40** deferred turns — five times past the
threshold — and asserts the request is still authoritative: latch intact, no job stacked, queue empty, the
interval still live with its rows unchanged, and the store still populated. Freeing the owner then admits and
retires it exactly once. Implementing `design.md:1104` literally (adding `store.clear()` to that branch) makes
the fixture fail on *"the park terminal must never abandon the accepted stop"* (**1 failed / 92 succeeded**) —
under the literal reading the stop does not stay parked at all: it proceeds by destroying the in-flight copy
rather than waiting for the owner, which is precisely the regression S2b.4 protects. The literal sentence in
`design.md` has been corrected accordingly.

The **diagnostic line itself** is emitted only under `SESSION_CAPTURE` (the firmware capture env), so the native
fixture evidences the normative *behaviour*; the line's compilation is carried by the firmware build, and an
end-to-end device observation would need the wedge reproduced on hardware (not attempted).

The conflict is resolved by the documented authority order rather than by preference: a change's **spec delta is
normative** and its `design.md` is explanatory, so the report-only reading governs — and `design.md` has been
fixed to match.

## Device session — the baseline is flashed, and boot/load residency is measured on device (2026-09-23)

The owner's go-ahead ("flash and create + run dedicated HITLs for each measurement") opened the device
session. This section records what was flashed, the first device measurement, and the three corrections that
measurement forced. **Scope: the boot/load half of S1.5 and the baseline half of S2b.3.** Nothing else is
claimed here.

### What is on the board, and the build hazard that was fixed first

`platformio.ini` sets `build_dir = ~/Library/Caches/pio-build-250513-215524-teensy41`, which is **not keyed by
revision**. A baseline worktree of `83be2e19` therefore built into the *same* object directory as this branch.
That is a live reproducibility hazard for exactly the two-revision comparison S1.3 exists to produce, so each
revision was built into its own directory with `PLATFORMIO_BUILD_DIR`:

| revision | build dir | RAM1 | FLASH code | `firmware.hex` sha256 |
|---|---|---|---|---|
| baseline `83be2e19` | `/tmp/rwso-base-build` | variables 96,128 / code **392,344** / padding **872** / locals 34,944 | **1,009,608** | `a4bc37e6…040920` |
| HEAD `9a2e5b9b` | `/tmp/rwso-head-build` | variables 96,128 / code **392,120** / padding **1,096** / locals 34,944 | **1,011,072** | `ea5e6d03…5a2c4ff` |
| the shared cache dir | `~/Library/Caches/…` | (unused) | — | `cfceda48…b554f4b1` |

The baseline row **reproduces the pinned baseline exactly** (`392,344 / 872`, FLASH `1,009,608`), which is the
point of pinning it. Two consequences are recorded rather than glossed:

- **The shared cache directory holds a third artifact** whose hash matches *neither* revision. The HITL run
  metadata picks its `firmware_hex` up from `platformio.ini`'s `build_dir`, so the `firmware_hex_sha256` it
  wrote for this run is `cfceda48…` — **not the flashed baseline**. The run identity used here is the
  `/tmp/rwso-base-build` hash above, and the metadata field must not be cited. Fix for later runs in this
  session: export `PLATFORMIO_BUILD_DIR` to the matching per-revision directory when invoking the HITL.
- FLASH code at HEAD is **1,011,072** today, where the earlier record in § P0 says **1,010,976**. The RAM1 line
  is bit-identical (`392,120 / 1,096`), so the placement gate is unaffected; the 96 B FLASH discrepancy is
  **flagged, not overwritten**, because two builds of the same firmware should not differ.

`lib/SSD1322_OLED` is a gitlink with **no `.gitmodules` entry**, so `git submodule update --init` fails
(`fatal: No url found for submodule path`). The library is only checked out in the main tree; the baseline
worktree was given a copy of `include/ src/ lib/ library.json` and built from that. This is recorded so the next
baseline worktree does not lose the same time.

### The measurement, and its honest verdict

Entry point: the authoritative preset, on the established scratch target (track 6 / slot 1 / channel 6 —
`captures/tidyup-ab-base-track6-slot1/metadata.json`), with the documented flag set:

```sh
.venv/bin/python scripts/host_midi_hitl.py run --preset base \
 --midi-out "Teensy" --midi-in "Teensy" \
 --track-number 6 --loop-slot 1 --midi-channel 6 \
 --record-bars 2 --overdub-bars 2 --no-fixed-grid-notes --start-transport \
 --overdub-start-delay-bars 0 --overdub-start-delay-beats 1 \
 --phase-wait-ms 500 --final-wait-ms 3000 --press-ms 120 --undo-redo-delay-ms 3000 \
 --out-dir captures/rwso-BASE-s11-s14-census
```

Log `captures/rwso-BASE-s11-s14-census/session_20260923_191135.log`. **Fresh boot confirmed**:
`#CAP,3005004,DIAG,boot_reset,crash=0` (line 1) and the `BOOT,scan,t0…t7` / `BOOT,load,ok` banner at lines
22–34; no `RING,overflow`.

The run is **`INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)`** and is **not** content evidence:
`PHASE FAIL: track 5 record notes=4 (min 31)` and `PHASE BLOCKED: track 5 record clocks=24
reason=clock_count_mismatch`. (`track 5` is 0-based — `_user_track_number(track_index) = track_index + 1`,
`legacy_record_baseline.py:500-502` — so it is the intended track 6.) The state machine nevertheless executed
the whole cycle (`ARMED->RECORDING` 3, `RECORDING->STOPPED_RECORDING` 2, `STOPPED_RECORDING->PLAYING` 2,
`PLAYING->OVERDUBBING` 2, `OVERDUBBING->PLAYING` 2, undo 1, redo 1). The density failure is a **setup** defect
to fix before any leg rests on content volume; it does not touch the boot lines below, which precede all
content.

### S1.5 / S2b.3 baseline half, measured on device

The firmware already emits the census needed here — `#CAP,<us>,DIAG,capture_prewarm,…` with
`free_chunks=`, `credits=`, and the resolved `slot=`. Two sequences bracket the boot:

**A. The default enabled set (lines 2–20)** — `phase=before free_chunks=512` (`psram_free=7,242,204`,
`psram_pool=8,379,288`, `heap_free=253,952`), then tracks 0…7 in order, each `slot=0,full=0,ready=1,credits=8`:

```
t0 500   t1 488   t2 476   t3 464   t4 452   t5 440   t6 428   t7 416
```

That is a flat **12 chunks per enabled track / 96 chunks for the 8-track set**, and `phase=after` closes at
`free_chunks=416`, `psram_free=6,425,988`. This is *bounded by the enabled set, not by the library*, and it is
the same constant the native leg recorded (40 chunks for its smaller enabled set).

**B. The committed slots (lines 40–58)** — `phase=before free_chunks=416`, then only the tracks whose slot
resolved to something other than 0 spend anything:

| line | track | slot | free_chunks | delta |
|---|---|---|---|---|
| 42 | 0 | 0 | 416 | 0 |
| 44 | 1 | **2** | 408 | **−8** |
| 46 | 2 | 0 | 408 | 0 |
| 48 | 3 | 0 | 408 | 0 |
| 50 | 4 | **1** | 400 | **−8** |
| 52–56 | 5,6,7 | 0 | 400 | 0 |
| 58 | — | — | `phase=after` **400** | |

**This is the slope the change exists to remove, observed on the pinned baseline firmware: exactly 8 chunks
per committed slot** (= `PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS`), 2 committed slots resident from the SD
library, and `free_chunks` 512 → 400 across the whole boot. On the current revision the same figure is asserted
to be **zero** per committed slot by the native fixture (S2b.3). The device half of that comparison still has
to be taken, on the HEAD image whose hash is pinned above.

### S1.1 baseline half, measured on device (tags the tick carries)

The 1 Hz tick carries `tagBytes[1..7]`. Over the 71 ticks of the same baseline run, the per-owner maxima are:

| owner (tag) | max KB |
|---|---|
| CaptureStopJobs | 412 |
| ViewPrewarm | 715 |
| CaptureStaging | 148 |
| CanonicalOutput | 320 |
| TransferHorizon | 8 |
| CompanionHistory | 0 |
| PlaybackOrder | 58 |
| **tagged total** | **1,661** |

Peak `live` on the same ticks is **6,221 KB** (`blocks` max 765). So these seven owners account for
**1,661 KB of a 6,221 KB peak**; the remainder is untagged or covered by the separate snapshot-slab census
(`slab_live` max 78 slabs, `slab_used` max 2,453 KB, `slab_unused` max 49 KB, `snap` max 2,153 KB). The
`live` series is `min 3,973 → max 6,221 KB` and `chunks_free` `383 → 362` over the run. These are **baseline
figures on a run whose content gate failed** (`notes=4`), so they are a floor for the owners that scale with
content, not a representative working set. `CompanionHistory=0` and `TransferHorizon=8` are consistent with a
near-empty loop; a dense record is required before those two numbers mean anything.

### S2b.3 device half — the per-committed-slot reservation is gone on HEAD

The HEAD image (`ea5e6d03…5a2c4ff`, flashed from `/tmp/rwso-head-build`) was put through the *same* preset, the
same scratch target and the same flags, into `captures/rwso-HEAD-boot-census-s2b3/`
(log `session_20260923_191536.log`). This time the run's own verdict is **`Result: PASS`** — no `PHASE FAIL`,
no `PHASE BLOCKED`. The boot census, taken identically:

| figure | baseline `83be2e19` | HEAD `9a2e5b9b` |
|---|---|---|
| boot sequence A: cost per track (`slot=0`, enabled set) | **12 chunks** | **4 chunks** |
| boot sequence A: total spend | **96** | **32** |
| boot sequence A: `credits` / `ready` per track | **8 / 1** | **0 / 0** |
| boot sequence B: cost per committed slot | **8 chunks** | **0 chunks** |
| boot sequence B: `credits` / `ready` | **8 / 1** | **0 / 0** |
| `free_chunks` after prewarm (**pre-restore** — see correction below) | **400** of 512 | **480** of 512 |

The discriminating lines are explicit. Baseline sequence B (lines 44, 50 of its log) resolves `track=1,slot=2`
and `track=4,slot=1` at `ready=1,credits=8` and drops 8 chunks each; HEAD sequence B (lines 118–132) resolves
the **same two slots** at `ready=0,credits=0` and drops **nothing** — every `free_chunks=480`. Both revisions
restored the same 2 committed slots, so this is a like-for-like comparison of the term the change removes:
**8 chunks per committed slot → 0**, with boot's own enabled-set cost falling 12 → 4 per track at the same time.

**CORRECTION — those `free_chunks` figures are PRE-restore, and the post-restore value is the one S1.5 asks
for.** A bounded boot-only capture (`scripts/capture_session.py`, no MIDI, no SD write) exposed the ordering:

```
line   7  [StorageManager] Queuing boot playback loop slot restore 6 pending; first 5/0
line  20  [StorageManager] Current workspace loaded successfully.
line  21  [StorageManager] WARN: could not reconcile MidiLooper/sets/index.bin
line  41  Track 6 loaded state: EMPTY
line 261  [StorageManager] LoadLoopJob done 5/0
```

Boot **queues** the loop restores and applies them through `LoadLoopJob` afterwards — `LoadLoopJob done 5/0`
lands well after the prewarm sequence, so the `phase=after` figure (400 baseline / 480 HEAD) is the pool
*before* the library is loaded back. Log `captures/rwso-BOOT-restore-probe/session_20260923_192650.log`; the
same capture's last tick, at 27.1 s, shows HEAD settling at **`free_chunks = 440`** — the restore costs
**40 chunks**, so the correct "after boot, with N committed slots" figure for HEAD is **440 of 512**, not 480.
The baseline's post-restore value is **not measured** (no baseline probe was taken); the like-for-like
comparison above therefore stands on the prewarm rows, which are exact and revision-matched. Two further facts
fall out and they are exactly what S1.3's sweep needs: the restored slot reports `Track 6 loaded state: EMPTY`,
and `index.bin` does not reconcile — so **recording into the scratch slot does not currently grow the restored
library**, and that is a prerequisite S1.3 must solve before a slope-to-failure sweep can run.

A second, controlled comparison falls out of the tick line. The two runs' first ticks sit at almost the same
uptime, so the same-moment state can be read directly:

| `[Memory] ext` tick | baseline @ 11.026 s | HEAD @ 11.124 s |
|---|---|---|
| `live` | 3,973 KB | 3,791 KB |
| `chunks_free` | 383 | **458** |
| `tags` (`1..7`) | `412/0/0/320/8/0/0` | `408/0/0/0/0/0/0` |
| **`CanonicalOutput`** | **320 KB** | **0 KB** |
| **`TransferHorizon`** | **8 KB** | **0 KB** |
| `cls` (>128 KB class) | 1,986 KB | 1,344 KB |

At the same point after boot, HEAD holds **no canonical output and no transfer horizon** where the baseline
holds 320 KB and 8 KB. That is invariant 34 — a committed-but-inactive slot reserves no canonical-copy
capacity — observed on device rather than asserted. Over the whole run the tag maxima agree on four owners
(CaptureStopJobs 412/408, ViewPrewarm 715/715, CaptureStaging 148/148, PlaybackOrder 58/58 — capacity-sized, so
equality is expected) and differ exactly where the change acts: **CanonicalOutput 320 → 0 KB**, TransferHorizon
8 → 1 KB.

### S1.1 device census — the owner gap is closed for tags 8..16

The owner approved a session-gated per-owner line, so the census no longer depends on a pool refusal.
`MemoryMonitor::logWorkingSetOwners()` emits `#MEM_OWNERS` on a **4 s** cadence from `main.cpp`
(session-capture gated, O(1) bookkeeping, direct `Serial` so it survives PLAYING/OVERDUBBING). It reuses
`ExternalPoolAccounting::formatTagVector` — which already covered tags 1..17 but had exactly one caller, the
refusal path — and adds the snapshot-slab census. Cost, measured rather than assumed:

| | before | after |
|---|---|---|
| RAM1 ITCM code / padding | 392,120 / **1,096** | 392,184 / **1,032** |
| FLASH code | 1,011,072 | 1,011,376 |
| native | — | **2,281 cases / 1 skipped / 2,280 succeeded / 0 failures** |
| `firmware.hex` sha256 | `ea5e6d03…5a2c4ff` | `6490ee60…c91101b4a` |

Padding spends **64 B** of the 224 B the change had recovered, leaving **192 B above the 840 floor** — inside
the approved budget, so the floor is untouched. The instrumented image is what the run below used, and the
boot census **reproduced identically on it** (sequence A `512 → 480`, 4 chunks/track; sequence B `480 → 480`,
committed deltas `[0]`), which is a consistency check that adding a printf did not move the mechanism.

Run: `captures/rwso-HEAD-owners-census/` (log `session_20260923_192254.log`), dense and **`Result: PASS`** —
`VERIFY record on/off/open/out/seq: 32/32/0/0/0`, 32 notes against the `min 31` gate. Per-owner maxima over 19
`#MEM_OWNERS` samples:

| owner (tag) | max KB | | owner (tag) | max KB |
|---|---|---|---|---|
| CaptureStopJobs (1) | 412 | | CompanionPairLookup (10) | 10 |
| ViewPrewarm (2) | 715 | | CompanionSpans (11) | 2 |
| CaptureStaging (3) | 148 | | **CompanionReconstruction (12)** | **64** |
| CanonicalOutput (4) | 0 | | **CompanionApply (13)** | **66** |
| TransferHorizon (5) | 1 | | **CompanionBridgeMap (14)** | **68** |
| CompanionHistory (6) | 0 | | **CompanionPasses (15)** | **96** |
| PlaybackOrder (7) | 58 | | **CanonicalPassInputs (16)** | **144** |
| ResetPassTimeline (8) | 0 | | **tagged total** | **1,776** |
| PrepareFirstBar (9) | 0 | | peak `live` | 6,144 |

`untracked_max = 0` and `unknown_frees_max = 0`, so `live` is trustworthy by the accounting's own rule. Every
owner S1.1 names — views, capture staging, companion reconstruction/apply/bridge-map/passes, canonical pass
inputs, transfer horizon, stop-job capacity — is now a **measured device number**, not an assertion.

Two S1.1 fields remain open, and neither is a telemetry oversight:

- **whole-loop `VisualCache` bytes** — `LoopVisualCache` exposes no byte/size accessor, so this needs a real
  accessor, not a print. Not measured.
- **lease holder count / owning slot / role** — **no playback-window lease exists yet.** Every `lease` in the
  tree is a `release()` method; a `Lease` struct/class does not exist (S2.1 is what derives the playback window
  lease). This clause therefore **cannot be measured before S2**; it should be re-scoped to S2 rather than
  re-attempted here. Recording it as "0 holders" would be a fabricated number.

### S1.4 undo-residency baseline — clause 1 measured, clause 2 measured but not landable

**Clause 1 (snapshot residency cold slots retain through undo roots), measured** from
`captures/rwso-HEAD-owners-census/session_20260923_192254.log` — the dense PASS run, no new telemetry:

| tick series | start | end | note |
|---|---|---|---|
| `snap` (cumulative allocator) | 0 KB | 2,470 KB | monotonic — **not** a retention signal |
| `slab_live` | 0 | 85 | live slabs |
| `slab_used` | 0 KB | 2,668 KB | retained payload |
| `slab_unused` | 0 KB | 20–51 KB | oscillation only |
| `pin1` (`slabsAtMostOneNode`) | 0 | **0** | no stranded mostly-empty slab |
| `pin24` (`slabsTwoToFourNodes`) | 0 | 1 | |
| `chunks_free` | 480 | 427 | 53 chunks consumed over the run |

The undo/redo cycle is the measurement. Across the undo (ticks at 66.08 s → 67.08 s) `slab_live` moved
84 → 83, `slab_used` 2,652 → 2,635 KB and `slab_unused` 35 → 20 KB; across the redo (to 70.08 s) they return
to 84 / 2,659 KB. So the baseline answer is: **the undo root retains essentially the whole snapshot arena —
≈2,635–2,659 KB over 83–84 slabs — releasing only ~17 KB and re-taking it on redo**, and it does so *densely*
(`slab_unused` ≤ 51 KB, `pin1 = 0` throughout), so this is retention, not a stranded mostly-empty slab. That is
the baseline S1.4 exists to record.

**Clause 2 (canonical chunks the roots pin via `SlotPassReferences`) — measured at 8 / 0 / 2, but the emission
does not fit the RAM1 budget, so it is *not landed*.** Nothing emitted the pinned side of the ledger:
`SC_PASS_RECLAIM` reports only what was *reclaimed*, and only when something was. A temporary `#MEM_PINS` line
that accumulated from the `refs` already built in `reclaimUnreferencedDisabledPasses` (deliberately no extra
walk, and no `InternalHeapFirstAllocator` construction on the transport thread) observed, constant over the run:

> `capture_pins = 8`, `edit_pins = 0`, `slots_with_pins = 2`

Two defects in that first cut were found and fixed before it was withdrawn, and both are worth keeping:

- **It flooded.** Gated on "pins exist" it wrote ~2,000 lines into one session log, because the reclaim path
  runs far more often than its nine call sites suggest. Change-detection (emit on transition) is the right
  gate, not a non-zero test — a lesson about any future per-tick instrumentation here.
- **It does not fit.** As a second `printf_P` it cost **192 B** of ITCM padding (1,032 → 840, landing exactly on
  the floor with zero headroom); wrapping it in `FLASHMEM` changed nothing (the `printf_P` is *already*
  flash-resident — only the argument setup is ITCM), and adding `noinline` made it worse at **824, below the
  floor, so the budget check correctly FAILED**. The pin census was therefore reverted; padding is back to
  **1,032 (192 B of headroom)** and the tree is green.

**LANDED 2026-09-23 as the `SC_PASS_RECLAIM` fold, and measured on device.** The three pin fields now ride the
**existing** reclaim line (`DebugSessionCapture::passReclaim`) instead of a second `printf`, with
change-detection in the guard so it stays quiet. On the dense PASS run
`captures/rwso-HEAD-pins-landed/` (hex `cba22f1d…`), the line carries the pinned side of the ledger for the
first time:

| t | `capture_pins` | `edit_pins` | `slots_with_pins` | `reclaimed` / `released` |
|---|---|---|---|---|
| 9.23 s | 0 | 0 | 0 | 0 / 0 |
| 10.68 s | 5 | 0 | 1 | 0 / 0 |
| 15.70 s | **8** | 0 | **2** | 0 / 0 |
| 34.08 s | 3 | 0 | 1 | 0 / 0 |
| 70.26 s | 7 | 0 | 2 | 0 / 0 |

So the undo roots pin **0 → 8 capture passes across 0 → 2 slots**, with **`edit_pins` = 0 throughout** in this
scenario — the B10 baseline. Note every row has `reclaimed=0` and `released=0`: the lines appear purely because
the pin triple *changed*, which is exactly what the change-detection is for. Five lines, against the ~2,000 the
naive "pins exist" gate wrote.

**A correction to my own reasoning about the cost.** I predicted the fold would be cheaper than a trimmed
standalone line because it avoids a second `printf`. Measured, it is **not**: the fold lands at padding **904**
(64 B above the floor) — the same 904 the trimmed standalone line reached. What actually saved bytes was
**trimming the format arguments**, not avoiding the second call; the floor-bound cost here is the accumulation
work (three counters and the per-slot loop body), which both shapes pay. The change remains net positive against
the original 872 baseline (+32 B), but the margin is now thin and a further diagnostic here should be expected to
need its own budget decision.

**Still not the *chunk* figure.** `SlotPassReferences` pins **pass ids**, so what is measured is pinned passes and
the slots holding them — not the byte/chunk footprint those passes reference. Converting passes to chunks needs
each pinned pass's chunk list, which this instrumentation does not reach; with ITCM headroom down to 64 B that
conversion is deliberately left to whoever next owns the budget rather than smuggled in here.

### CORRECTION: the baseline FAIL / HEAD PASS asymmetry is a host-harness coin flip (2026-09-23)

This section first read the verdict asymmetry as "the change, or differing library state — this evidence does not
separate them". A diagnosis of the harness **supersedes that reading**, and it is recorded here rather than
quietly rewritten.

The asymmetry is **not** firmware. A real PASS run exists on an *older* build —
`captures/tidyup-base-track6-slot1-r2` (`overall_ok=true`, **32 notes / 192 clocks**, `device_bar` start =
latest = 3) — and a run on the **same** build as that PASS (`captures/tidyup-base-track6-slot1`,
`session_20260918_221703.log`) reproduced the *identical* failure signature **4 notes / 24 clocks /
start=1 / probe=4 / relative=3**. All three stored configs are byte-identical apart from output paths. So no
omitted flag and no firmware change is necessary for the failure: **the same build gives both outcomes.**

Root cause, host-side: the record phase is ended by the device-bar probe at
`scripts/hitl/transport_clock.py:340-351`, tested only when `phase_clock_count % 24 == 0` — hence exactly 24.
It fires on the *first* probe because two host defects disagree about which bar the phase started on:

1. `_device_bar_index_at_record_start` (`scripts/hitl/legacy_record_baseline.py:1186-1203`) anchors on the
   **first** `RECORDING` row in its window. `--loop-slot 1` forces the clear precondition
   (`legacy_record_baseline.py:1878-1879`), and because transport is already running, the clear's Loop-1 press
   starts an **incidental** recording that supplies a stale `device_bar_start=1`.
2. `_device_bar_probe._from_log` (`legacy_record_baseline.py:1217-1227`) seeds `state["lines"] = 0`, so its
   first call scans the **whole file** and returns the newest `BAR` row in the log (`4`) — from the *previous*
   transport epoch. The device re-bases `BAR` per transport start, so the two numbers are incomparable:
   `4 - 1 = 3 >= target_bars = 2`, and the phase is cut at 24 clocks. The record then holds only the notes at
   clocks 1, 7, 13, 19 (4 notes), which is exactly the observed `record loop length 768/1536 (raw 433)`.

The overdub phase survived only because it uses a different helper (`_latest_device_bar_index` over the last
400 lines), which happened to return the same value; the condition is unreliable in **both** directions —
inert when the two stale values agree (the PASS), truncating when they disagree. Two consequences for this
change's evidence:

- **No firmware claim may rest on this asymmetry**, and none does. The boot/load census above is exact and
  independent of it (it is taken from the ring-buffered boot lines, and the identical per-slot figures
  reproduce across revisions).
- **A failed content gate must be retried, not reinterpreted.** The failure is a harness coin flip, so a
  content leg that matters has to be re-run to a PASS rather than explained.

**Also corrected:** `captures/tidyup-ab-base-track6-slot1` — the directory cited earlier in this file for the
established scratch target — is **not** a passing run and must not be used as a content or PASS reference. It
has no wrapper artifacts and **zero** `RECA` / `RECORDING` rows; its utility here was its `metadata.json`
(track 6 / slot 1 / channel 6), which remains valid and is used only for that. The real PASS reference is
`captures/tidyup-base-track6-slot1-r2`.

### Two transports, and what that means for the timestamps

The boot `#CAP,…,capture_prewarm` lines appear in the log at 3.0–3.5 s even though the capture process only
opened well after that, because `#CAP` goes through the **ring buffer**, which replays. The `[Memory] ext` tick
goes to **direct `Serial`** and is not ring-buffered, so the first tick in the log is at 11.0 s while the boot
markers sit at 3.0 s. Two consequences that must not be conflated:

- the **chunk** census at boot (sequence A/B above) is **exact** — it is the device's own record of the boot;
- the tick's `live`/`tags` figures are only available **from the moment capture opens**, so `live=3,973 KB` at
  11.0 s is *not* the boot value. Boot-time `live` is only in the `#CAP,BOOT,ram1,188416` marker and the
  prewarm lines' `heap_free=`.

### The repeatable recipe

Because nothing on the host parsed these lines, the reduction is now a script:

```sh
.venv/bin/python scripts/parse_working_set_census.py <capture-dir-or-log>... [--json OUT] [--tick-table]
```

It emits the per-sequence `free_chunks` spend with the committed-slot deltas kept **separate** from the
zero-slot rows (the first sequence walks the default enabled set and costs a flat amount *per track*; only the
second resolves the library), the tick summary, and any `#MEM_EXT_REFUSED` with its full tag vector. Its output
reproduces the table above exactly.

### Corrections this measurement forced

1. **`pin1`/`pin24` are not canonical-chunk pins.** The tick line names them `pin1=`/`pin24=`, but the arguments
   are `slabCensus.slabsAtMostOneNode` and `slabCensus.slabsTwoToFourNodes` (`MemoryMonitor.cpp:374-375`), i.e.
   snapshot-**slab** occupancy buckets. The earlier device-session plan in this file read them as canonical
   pinning for 1-bar/24-bar intervals; they are not, and **S1.4's second clause (canonical chunks pinned via
   `SlotPassReferences`) has no telemetry at all** — `SlotPassReferences` is referenced only in
   `PassReclaim.*`/`Loop*.cpp` and is never serialized.
2. **"No new telemetry is required" was wrong as stated.** The 1 Hz `[Memory] ext` tick prints only
   `tagBytes[1..7]` (`MemoryMonitor.cpp:359-365`) — CaptureStopJobs, ViewPrewarm, CaptureStaging,
   CanonicalOutput, TransferHorizon, CompanionHistory, PlaybackOrder. The owners S1.1 names —
   companion reconstruction / apply / bridge-map / passes and canonical pass inputs (tags 12–16), plus
   `VisualCache` bytes and the "lease holder count, owning slot, role" fields — are **not** on any tick line.
   The full 18-slot vector exists only in `formatTagVector` (`ExternalPoolAccounting.cpp:71-86`), whose single
   caller is the **pool-refusal** path (`#MEM_EXT_REFUSED`, `ExternalPoolAccounting.cpp:224-234`). So the full
   per-owner census is obtainable today only at a refusal, which is precisely S1.3's failure point; a
   healthy-path per-owner census needs a small session-gated instrumentation increment.
3. **Nothing on the host parses this line.** No Python under `scripts/` reads `[Memory] ext`, `#MEM_EXT_REFUSED`,
   `#MEM_DENIED` or `#MEM_ADMIT`; `scripts/parse_memory_capture.py` handles only the older `[Memory] heap` /
   `psram` boot lines. Every number above was read out of the raw session log.

### What this leaves open, precisely

- **S1.4 — undo residency baseline:** clause 1 **measured** (the undo root retains ≈2,635–2,659 KB over 83–84
  slabs, releasing ~17 KB); clause 2 **measured at 8/0/2 but not landed** — the line costs 192 B of ITCM padding
  and 824 B padding with `noinline`, i.e. below the floor, so it was reverted. Landing it needs either the
  cheaper `SC_PASS_RECLAIM` fold or an explained budget change. See § S1.4 undo-residency baseline.
- **S1.5 device leg:** the per-committed-slot figure (**8 chunks → 0**) and `free_chunks` after boot
  (**pre-restore** 400 → 480; **post-restore HEAD 440**) are **recorded**. The "smallest N refusing a 24-bar
  overdub" clause is **open** — the native attempt never reached an admission probe, and the device route needs
  a grown library (below).
- **S1.1: closed for tags 1..16** by the instrumentation increment (§ S1.1 device census) — every owner the task
  names is now a measured device number. Still open: whole-loop `VisualCache` bytes (no accessor) and the
  **lease holder / owning slot / role** triple, which cannot be measured before **S2.1** introduces the
  playback-window lease. The record-density flakiness that understated the baseline census is a **host** defect
  (below), so a content leg is retried to a PASS.
- **S1.3: clause 2 done, device clause still blocked.** The synthetic 60-descriptor characterization is
  **implemented and green** (§ P1 — S1.3 clause 2): reservation flat at 40 chunks (205,280 B) from N = 8 to
  N = 60, payload and the runtime directory reported separately, red-proven against the pre-S2b.1 state. The
  **device** slope-to-failure sweep still cannot run: growing the restored library by recording into the
  scratch slot does **not** work today — the restored slot reports `Track 6 loaded state: EMPTY` and
  `index.bin` does not reconcile — so there is no way to raise N across reboots. The scratch range is
  approved (**track 6, slots 1–8**) for when it can.
- **S2b.3 / S2b gates:** the device half **is taken** — HEAD was flashed (`ea5e6d03…`, then the instrumented
  `6490ee60…`) and the zero-per-committed-slot result reproduces on both HEAD images. The **synthetic
  60-descriptor half is now done too** (§ P1 — S1.3 clause 2). What remains open on this gate is the
  S1.3/S1.4 device material it depends on.

## P1 — S1.3 clause 2: the synthetic 60-descriptor characterization (2026-09-23)

**Execution packet (DEC-080, measurement slice — no production file is touched).**

- **Scope.** S1.3's **second clause** only: the synthetic metadata-only 60-descriptor characterization of the
  residency law, which `S2b gates` additionally requires ("a synthetic 60-descriptor fixture covers boot
  admission independently of S6's content residency", [IMPLEMENTATION_CONTRACT.md](IMPLEMENTATION_CONTRACT.md) §1).
  Owner is the test harness. **Non-goals:** S1.3's device slope to the failure point (still blocked by the
  restored scratch slot not surviving a reboot), S1.5's refusal-N, S1.4's pass→chunk conversion, and every S2/S3
  lease behavior. The genuine 60-long-slot and total-residency gates stay in S6/S7.
- **Why the existing helper cannot be reused.** `measureWorkingSetResidence()` commits every descriptor into
  **one** track's slots (`for (slot = 0; slot < recordedSlots; ++slot)`) and `Config::MAX_LOOPS_PER_TRACK` is
  **8**, so `recordedSlots = 60` indexes out of bounds. Sixty descriptors must span the 8 × 8 = 64-slot matrix.
- **Transition oracle.** Reset pool + `TrackManager` → commit `N` one-note record passes into
  `(track = i / 8, slot = i % 8)` → record committed canonical payload → run the production boot/load admission
  (`TrackManager::prewarmPlaybackRuntime`) → admit exactly one active role through the production record-arm
  entry (`Track::prewarmPlaybackForSlot(activeSlot, true)`) → census canonical-copy credits and
  transfer-horizon admission per committed slot, and resident/allocated runtime slots across all 64. Final
  state: `N` committed descriptors, exactly one active role, everything else inactive.
- **Controllable fixture.** Reuses the existing production-path harness in `test_continuous_capture` (real
  chunk pool, real `TrackManager`, real admission functions). No stub and no copied state machine.
- **Independent observables.** At a constant active set: (a) `reservedChunks()` (admission minus payload) is
  equal at N = 8 and N = 60; (b) `residentWorkingSetSlots` is equal; (c) every *inactive* committed slot holds
  zero canonical credits and no transfer horizon; (d) the pool keeps at least `PassConfig::CHUNK_RESERVE` free
  chunks at N = 60, so a flat result cannot be a capacity artifact. **Payload and the runtime directory are
  reported, not asserted flat** — the contract requires them separated.
- **Recipe.** `pio test -e native -f test_continuous_capture`; then full `pio test -e native`; then
  `pio run -e teensy41-capture-serial` with its post-link RAM1 budget check.

### Measured (2026-09-23, native) — the law is flat at the 60-descriptor envelope

Two cases, both in the default gate: `test_s1_3_sixty_descriptor_characterization_is_well_formed` and
`test_s1_3_residence_is_flat_across_sixty_descriptors`. Both variants hold **exactly one active role**
and commit one-note descriptors distributed `(track = i / 8, slot = i % 8)`, so `N = 60` spans all eight
tracks and the 8 x 8 = 64-slot matrix.

| term | N = 8 (one track) | N = 60 (eight tracks) | reading |
|---|---|---|---|
| committed descriptors | 8 | 60 | input |
| canonical **payload** | 8 chunks | 60 chunks | content; grows by construction, reported separately |
| admitted (payload + reservation) | 48 chunks | 100 chunks | |
| **reservation** (admitted − payload) | **40 chunks** | **40 chunks** | **the law: flat** |
| free chunks after admission | 412 | 412 | 100 of 512 used; reserve intact |
| canonical credits on inactive descriptors | 0 over 7 | 0 over 59 | |
| transfer horizons on inactive descriptors | 0 | 0 | |
| the active slot's canonical credits | 8 | 8 | `PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS` |
| resident working sets | 1 | 1 | only the active role |
| runtime **directory** slots | 15 | 60 | reported; **not** flat (see below) |

The constant 40-chunk reservation decomposes as boot's stop-descriptor tail reserve (**32 chunks = 8
tracks x 4 `CaptureStopJobs` entries x 1 chunk**, bounded by the enabled set, not the library) plus the
one active role's own canonical copy (**8 chunks**). It is therefore *not* a claim of zero reservation —
it is a claim that nothing is reserved **per committed descriptor**.

Device conversion, kept separate from the chunk census as `IMPLEMENTATION_CONTRACT.md` §1 requires
(`sizeof(EventChunk)` = **5,132 B** on the device, from the recorded 41,056 B per 8 chunks):

- **reservation: 40 chunks = 205,280 B, constant** between 8 and 60 descriptors;
- **payload: 8 -> 60 chunks = 41,056 -> 307,920 B** (content, by construction);
- **runtime directory: 15 -> 60 x `LoopPlaybackRuntime` 6,344 B (S3.0 pin) = 95,160 -> 380,640 B** —
  per-committed-descriptor, so not flat. It is descriptor/directory state, not a working set, and it is
  the term S3's reusable lease pool is meant to make active-owned; it is reported here, not asserted flat.
- **Native byte figures were deliberately not used:** native `sizeof(EventChunk)` is **8,208 B**, not
  5,132 B, because `MidiEvent`'s `sysexData` union holds a 64-bit pointer. The chunk counts are the
  device-identical unit and are what the law is asserted on.

**Discrimination proven, not assumed.** Re-introducing the state S2b.1 removed — making
`prewarmCanonicalCaptureOutput()` unconditional in `Track::prewarmPlaybackForSlot` — fails **both** cases:
the law case reports `reserved(8)=152` vs `reserved(60)=452` (~8 chunks per descriptor, the RC-MEM-5
slope) and the well-formedness case catches the pool exhausting (`admitted=512`, `free=0`) before sixty
descriptors can be admitted at all. Seven cases in the suite go red under that mutation, including the
1-vs-4 law cases and B01, which is the expected cross-check. The mutation was reverted immediately
(`src/` is byte-identical; `git diff` on `src/` is empty) and the suite is green again at 95/95.

## P1 — S1.5 clause 3: the dense seal is diagnosed, and the refusal point is outside the supported envelope (2026-09-23)

**Execution packet (DEC-080, measurement slice — no production file is touched).**

- **Scope.** S1.5's third clause: *"the smallest N at which a 24-bar overdub is refused"*. Owner is the test
  harness. **Non-goals:** the device leg, S1.3's slope, and the byte-budget terms. The **recorded obstacle**
  was upstream — a dense 24-bar construction did not seal at all, and two findings about why were withdrawn.
- **Identify the exact branch first (the rule both withdrawn findings broke).**
  `Loop::sealCapture` runs `CaptureIncrementalSanity::removePairsShorterThanNoteMinLength` **before** its
  empty check (`LoopCapture.cpp:2266-2284`), so a construction whose note pairs are closer together than
  `Config::DEFAULT_NOTE_MIN_LENGTH_TICKS` (**8**) has every pair removed, the live store empties, and the seal
  returns `FailedValidation` from the **`capture.store.empty()` branch** — *not* from the transfer
  precondition (`preparePendingCapturePassTransfer`), which is what both withdrawn findings had inferred from
  reading the code after seeing a failure their own construction produced.
- **The discriminating signature.** The events really are in the live store before the seal; `beginCapture`
  holds exactly one tail chunk (free = 511); and the failed seal **releases** it, so the pool returns to fully
  free (**512**) — nothing retained. The positive control at exactly 8 ticks seals `Ok` and keeps its chunk
  (511). Same density, same ordering, only the pair length differs.
- **Ordering was not the cause.** All three orders seal with pairs >= 8 ticks apart: never prewarm, prewarm
  before `beginCapture`, and prewarm between `beginCapture` and the appends. The earlier ordering inference is
  therefore also withdrawn; the pair length was the uncontrolled variable.
- **The construction.** 24 bars at the task's recorded ~72 events/bar = 864 pairs = 1,728 events, pairs 8 ticks
  apart, committed across the 8 x 8 slot matrix, then the production boot admission, then the arm/overdub
  reserve admission probed on the next empty slot (`prewarmPlaybackForSlot(slot, true)`).
- **Transition oracle.** Reset -> commit N dense descriptors -> `prewarmPlaybackRuntime()` -> read
  `freeChunkCount()` -> probe the reserve admission on slot N -> read `freeChunkCount()` again.
- **Outcome at the boundary.** The probe answers **admitted / refused** only, and the native refusal carries
  no reason (`MemoryMonitor::reportPrewarmAdmissionDenied` is a no-op stub off-device), so the reason is
  classified from **pool state** instead — the part that *is* independently derivable. The fixture asserts
  that arithmetic rather than a diagnostic string.

### Measured

| term | value |
|---|---|
| boot's own cost (`n = 0`, free before probe) | 480 of 512 -> **32 chunks** |
| chunks per dense 24-bar loop | **7** = `ceil(1,728 / 256)`, asserted, constant in N |
| `n = 63` (the whole matrix bar the probe slot) | free before probe **39**, after **31** |
| arithmetic | `512 - 32 - 63*7 = 39`; `39 - 8` (`ACTIVE_OVERDUB_TRANSFER_CHUNKS`) `= 31` |
| refusal within the supported matrix | **none** — the probe is admitted at every N up to 63 |
| library events at `n = 63` | 108,864 of the 512 x 256 = 131,072 ceiling (83%) |

**So S1.5 clause 3's native answer is that the smallest refusing N lies outside the supported 8 x 8
envelope.** A library filling the matrix with dense 24-bar loops still admits the 24-bar reserve set, with
**31 chunks free** — 7 above `CHUNK_RESERVE + ACTIVE_OVERDUB_TRANSFER_CHUNKS` (24).

**Red-proven against the pre-S2b.1 architecture.** With the per-slot canonical reservation restored (making
`prewarmCanonicalCaptureOutput()` unconditional in `Track::prewarmPlaybackForSlot`), the same construction
refuses at **N = 30** (free = 6 before the probe, below the 8-chunk reserve), and at `n = 63` the fixture fails
on its admission assertion with `free before probe=0`. So the HEAD answer is a **capacity result of the S2b.1
removal**, not a property of the pool. The mutation was reverted immediately; `src/` is byte-identical.

**What this does not establish.** The device's refusal point is **byte**-limited (PSRAM), not id-limited, and
it is S1.3's device leg; nothing here claims a device capacity number. The ~191-chunk figure the task cites
for a 24-bar overdub's peak reservation is **not reproducible natively**, because the reserve set's byte terms
(views, companion rows, snapshot inputs) are heap-backed off-device — which is exactly why the native refusal
must be reported as an *id-ceiling* result and the clause's device half left open.

### Verification

| check | result |
|---|---|
| `test_continuous_capture` | **97/97** (+2: the capacity law and the seal diagnosis) |
| full native | **2,287 cases / 1 skipped / 2,286 succeeded / 0 failures** |
| firmware + post-link RAM1 budget | SUCCESS, `code 392,312 / padding 904` — unchanged (`src/` untouched) |

## Device session — RWSO-24CAP: the S2b/S1.5 gate on hardware, the restored library survives a reboot, and a clear-drain blocker (2026-09-23)

**Run identity.** Firmware HEAD **`26e96539`** (clean tree), `teensy41-capture-serial` (RAM1
`code 392,312 / padding 904`), `firmware.hex` sha256 **`673e20ca1a83a548…`** — recorded in both runs'
`metadata.json` as `git_commit` / `firmware_hex_sha256`, so the evidence is pinned to a build. Device
`/dev/cu.usbmodem154944801`, scratch slot **track 5 / slot 1** (the authorized default), MIDI channel 5.

**Stimulus.** New temporary variant **`STIM-BASE-2-2-01-TEMP-RWSO-24CAP`** (not registered), forked from
canonical `STIM-BASE-2-2-01` with a recorded reason: the canonical 2-bar recipe cannot reach the capacity
boundary. Shape = the canonical base recipe (record + OD1 + OD2 + undo/redo) with `--record-bars 24
--overdub-bars 24` through the legacy passthrough. Sidecar:
`captures/rwso-24cap/stimuli/STIM-BASE-2-2-01-TEMP-RWSO-24CAP.yaml`.

### Leg 1 — the per-committed-slot canonical reservation is gone at boot (S2b.3 / S1.5)

From the boot census in `captures/rwso-24cap/session_20260923_201842.log` (fresh boot, `#CAP,…,DIAG,capture_prewarm`):

| term | baseline `83be2e19` | HEAD `26e96539` |
|---|---|---|
| default enabled set (8 tracks) | 96 chunks (12/track) | **32 chunks** (4/track) |
| per committed slot: canonical reservation | **8 chunks** (41,056 B) | **0 chunks** |
| `free_chunks` pre-restore | 400 of 512 | **480 of 512** |
| `free_chunks` after boot in the run | 400 | **441** |
| transfer horizon per committed slot | 8 KB | **0** |

The HEAD census has two phases. Phase 1 walks the enabled set: `512 -> 480` (32 chunks). Phase 2 is the
**library restore walk** and is the claim: `phase=before free_chunks=480` -> eight `(track,slot)`
rows -> `phase=after free_chunks=480`. Every row reads `full=0,ready=0,credits=0`, and two of the eight are
non-`slot=0` committed library slots (`track=1,slot=2` and `track=4,slot=1` as the log prints them), so the
walk genuinely visited committed slots and spent **zero** chunks on them. That is the per-slot **reservation**
slope removed on hardware; canonical **payload** is separate and is what moves `480 -> 441`.

### Leg 2 — a 24-bar overdub is admitted (S2b gate)

`captures/rwso-24cap-retry2/session_20260923_202246.log`:

- `record loop length (final ticks): 18432/18432 (raw 18778)` — 24 bars exactly.
- **`Overdubbing started` twice** (ticks 755 and 22,836) with **0** `op=overdub_start` denials, 0
  `#MEM_DENIED`, 0 `#MEM_EXT_REFUSED`, 0 `FATAL`. The pre-change RC-MEM-5 `before` leg recorded **zero**
  starts against 4 denials, so this is the capacity path this change must not regress — and does not.
- `stored overdub span (SEVT ticks): count=192/191 … bars=24/24 count_ok/span_ok/bars_ok = true`;
  `overdub wrap storage: pairs=248 sevt=1164 ok=True`.
- `persistence handoff: result=ok duration_us=46479`; `deferred persist nonblocking: loop_persist=203
  sliced=True ok=True`; display `overdub_ok=True transport_ok=True`.
- The base report's own verdict is `INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)` — the known **host
  setup-oracle** class, which per [CURRENT_WORK.md](../../../docs/Runtime/CURRENT_WORK.md) is not evidence
  against the content. The content check is Leg 4.

### Leg 3 — the restore works once, but is NOT stable across reboots (a claim of mine, corrected)

`dump_and_capture.py` opens serial with a 25 s settle and opening serial resets the board, so its dump is taken
*after a reboot and an SD restore*. It reported:

> `CAPTURE_CONTENT,begin,track=5,slot=0,length=18432`

so immediately after the 24-bar slot's own save, that slot came back with its exact length.

**I over-generalised that to "the library can grow across boots", and the growth attempt falsified it.** A
second forced reboot (`captures/rwso-growth/boot-n2/`, re-flashed to hard-reset) shows the recorded obstacle
reproducing, and it is *track-specific and catalog-level* rather than general:

- `[StorageManager] WARN: could not reconcile MidiLooper/sets/index.bin`
- `[StorageManager] Queuing boot playback loop slot restore 6 pending; first 4/0`
- `Track 6 loaded state: EMPTY` and `Track 7 loaded state: EMPTY` (tracks 0–5 report `STOPPED`)
- `#CAP,…,DIAG,lcr,skip,restore`
- and the census's non-zero-slot set **changed between boots**: Leg 1 walked `{track=1,slot=2,
  track=4,slot=1}`, this boot walks `{track=1,slot=2}` only.

**One datapoint of mine is withdrawn as a wrong-slot read.** I first reported "selecting track 5 reads
`3 bars (2304 ticks)`, not the 24-bar loop" as evidence of an unfaithful restore. That was a **measurement
error**: I pressed only the track button, which reports whatever slot that track had selected, not slot 1.
Pressing track 5 **and then slot 1** (note 50) reads `Sent loop length CC feedback: 24 bars (18432 ticks)`, and
the next `dump_and_capture` again reports `CAPTURE_CONTENT,begin,track=5,slot=0,length=18432`. So track 5
slot 1's 24-bar loop **does** survive the hard reset, and the earlier "3 bars" reading is withdrawn. What
remains solid is the **catalog** evidence above (the reconcile warning, `Track 6 loaded state: EMPTY`, the
changed non-zero-slot set, `lcr,skip,restore`) — the restored **set** is unstable even though this slot
survives. **Growing the library across boots is therefore not reliable on this
firmware**, and S1.3's slope sweep stays **blocked** — with a sharp diagnosis (SD catalog reconcile + skipped
track-6 restore) instead of the earlier broad claim. The scratch range track 6 slots 1–8 was never the
obstacle; the catalog is.

Leg 3's narrow fact survives and is still worth keeping: a slot's content *can* come back byte-exact right after
its own save+reset. What does **not** survive is the assumption that successive boots accumulate a library.

### Leg 4 — content verification: STORED and PLAYING, but NOT a verified PASS

`captures/rwso-24cap-retry2-playback-2cyc/content-review.json` (two anchor recurrences, 48.001 s apart —
exactly one 24-bar cycle at 120 BPM, so the capture really covered two cycles):

- `storage_matches_effective: true`; `effective_rows_all = expected_note_cc_rows = 1,746`.
- `per_bar_effective` is dense and even across all 24 bars (70–84 rows; `max_bar_refs` 84).
- The verifier reports **one** inter-anchor cycle: **1,744 of 1,746 matched**, `inventory_equal: false`,
  `residual_missing_count: 0`, `unexplained_missing_count: 2` — both in **bar 0**:
  `[128, 5, 76, 0]` (a NoteOff) and `[176, 5, 74, 101]` (a NoteOn).

Neither matches a documented residual class ([HITL_CONTENT_VERIFICATION.md](../../../docs/Guides/HITL_CONTENT_VERIFICATION.md)
§ Expected residuals: loop-boundary terminal Off at the final tick; coincident same-pitch Offs), so per the
guide this run is **not** `inventory_equal: true` and is recorded as **not content-verified**. Two honest
observations bound it, without explaining it away:

- the first row's tick (128) is **before** the verifier's anchor (tick 144), so it lies outside the cycle
  window by construction;
- **both events are present in the raw output in quantity** (note 76 Off ×157, note 74 On ×264 over the
  capture), so this is not loss of the notes themselves.

**Both rows are now shown to be present in playback, per cycle — the verifier's verdict is a cycle-local
matching artifact, not content loss.** Locating each row against the two anchor occurrences in that capture
(the anchor is the effective row `[144, 5, 12, 98]`, recurring 48.001 s apart) finds **one matching occurrence
per cycle for each row**: `note76` Off near `anchor − 80 ms` in *both* cycles, and `note74` On near
`anchor + 1 ms` and `+ 44 ms`. So the played output carries both events once per cycle; the verifier's
cycle-local inventory simply does not bind them. The exact tick attribution is **not** established — the
observed offsets sit ~40 ms (≈15 ticks) off the nominal `16/384 s` and `32/384 s`, and a same-note multiple-Off
match cannot be excluded — so this is stated as *presence per cycle*, not as a corrected tick mapping.

**A third attempt with a longer window did not help, and its cause is the catalog instability.** Re-capturing
over 160 s (`rwso-24cap-playback-3cyc`, `--capture-seconds 160`) returns
`INCONCLUSIVE_CAPTURE: no unique effective event recurs in the playback window` — no anchor, no cycles — while
its dump still reports `track=5,slot=0,length=18432` and the same 1,746 effective rows. The playback mix had
**changed between resets** (channels 1–2 now dominate where channel 5 did before), because which slots the
unstable catalog brings back differs per boot. So **content verification of this slot is confounded by the same
persistence defect**, and the guide's formal bar (`inventory_equal: true` on every cycle) is **not met** —
recorded as not verified, with the content itself shown present.

`capacity_path_exercised: false` — the densest bar is 84 refs against the old 256 ceiling, so this is a
**parity run, not a capacity re-stress**, exactly as the guide distinguishes.

### The mute lever was tried, and it does not work — the playing set itself changes (2026-09-23, later)

The non-destructive lever for a formal PASS is to silence the other tracks (MIDI mute leaves their SD content
alone; clearing them would be destructive and is not permitted). Track mute is a **double press** on the track
button (`MidiButtonConfig.cpp`: `onDoublePress(ActionType::MUTE_TRACK)`, ch16 note `60+i`); seven tracks were
muted and `Track 1..4, 6, 7, 8 mute toggled` was confirmed on serial. It does **not** produce a verifiable
capture, and the reason is a finding rather than a nuisance:

- With the other seven muted, a 160 s capture yields **no musical notes at all** — only LED feedback on
  channel 15 and transport clocks — and the verifier returns `INCONCLUSIVE_CAPTURE` again. Muted tracks still
  run playback (six `Playback started` rows), so the silence is suppression, not a stopped transport, and the
  capture script does start transport (`port.send(mido.Message("start"))`).
- Measuring the played channels directly: **all unmuted** gives note-ons on 1-based channels 1, 2, 3, 4, 6
  (+ LED 15/16); **with the others muted** it gives none. So the slot under test — track 5 slot 1 — contributes
  **no** playback notes even though the dump reports its content as present
  (`track=5,slot=0,length=18432`, 1,746 effective rows, `per_bar_effective` 70–84 on **every** bar, so a 15 s
  window cannot be an empty region).
- This is **state drift, not a property of the slot**: in the earlier 2-cycle capture on channel 5 the anchor
  `[144,5,12,98]` *was* present (two occurrences), so track 5 did play then and does not now.

So the set of tracks that actually **play** changes across resets, in the same way the restored set does, and
the mute lever removes the very track that needs verifying. **A formal content PASS therefore needs the
persistence/catalog defect fixed first**; it is not reachable by capture configuration. The mute state was
**restored** (all seven unmuted) and the transport stopped, so the device's audible state is as it was.

### Round 3 — re-recording the slot does not restore a faithful capture (2026-09-23, later)

The one remaining route to a formal PASS was to reproduce the conditions of the near-parity run: re-record the
24-bar slot and capture immediately, before the set drifts. With the transport stopped and the save drained
first (the Leg-5 prep), the re-record (`captures/rwso-24cap-r3/`) came back **degraded instead of faithful**:

- `record loop length (final ticks): 19200/18432 (raw 19200)` — a **25-bar over-run** where the earlier run
  sealed at exactly 18,432;
- `stored overdub span (SEVT ticks): count=0/191 … count_ok=False span_ok=False bars_ok=False`, and
  `overdub_ok=False transport_ok=False` — the harness could not see the overdub evidence at all, though the
  firmware log does contain **two** `Overdubbing started` rows (0 `FATAL`, 0 `#MEM_EXT_REFUSED`);
- `persistence handoff: first_missing=request result=None`, and the log carries **no** `PERS,request` /
  `SAVE,*` markers, so the re-recorded content was **not persisted**;
- the session log is **1,574 lines** against the faithful run's **45,909** — the capture-sparsity mode, with
  `heap_free=80KB, heap_min=76KB`.

So the device cannot currently produce a faithful, stable capture of the slot under test at all, and the
failure mode has widened from "the verifier cannot bind two rows" to "the content creation run itself does not
reproduce". No firmware or SD content was changed by this round; the device is left with the transport stopped.

**Why a formal PASS is not claimed.** The guide's bar is `inventory_equal: true` on every cycle
([HITL_CONTENT_VERIFICATION.md](../../../docs/Guides/HITL_CONTENT_VERIFICATION.md)). The substantive claims that
*are* established: the committed inventory matches the effective inventory (`storage_matches_effective: true`,
1,746 rows, all 24 bars dense), and both rows the verifier called missing are present in playback once per cycle.
What is **not** established is a cycle-clean PASS, because the device cannot currently hold a stable playing
set long enough to capture one.

### Blocker found and worked around: CLEAR cannot proceed while transport is active

The first two attempts aborted at the harness's clear precondition:

> `[StorageManager] ERROR: Persistence drain stuck after 33 iterations (max 33, expected ~1 slice steps, ~0 SD bytes)`
> `[ERROR] Clear aborted: could not complete deferred save first`

The mechanism, from the code and the logs:

1. `StorageManager::processDeferredSaveState` **returns immediately** while
   `isTransportActiveForPersistence()`. The blocking clear-drain (`saveState`) therefore cannot advance a
   single grain while transport runs, spins its stuck budget and fails.
2. The drain has **no transport-gate classification**. `maybeAdmitFinalizeWorkspaceAfterDrain()` returns
   `true` whenever work is merely *queued*, so the loop never takes the `AdmissionRefused` fast path and
   reports the generic `Stuck` — naming neither the transport gate nor the heap/pool state. This is a
   **different** wedge from the 2026-09-18 tombstone exhaustion the fail-fast path was added for.
3. The host did not step in because `_prepare_deferred_save_before_clear` saw neither save activity nor a
   running transport: the failed log contains **zero** `PERS,*` and **zero** `SAVE,*` rows (only the
   `DIAG,persist_save` / `DIAG,hostdrain` **timing telemetry**), so `save_active` was false; and
   `serial_global_transport_running(lines, after_index=track_baseline_len)` cannot see a transport start that
   precedes its anchor. The successful run's log, by contrast, carries 298 `PERS,*` and 10 `SAVE,*` rows, so
   the marker families exist — they were absent in that window.

**Workaround, and it is the device-prep step this session needed:** short-press the global transport (note 39,
control channel 16) to stop it, leave the device idle until the owed workspace save drains, then run. The
third attempt cleared and completed the full 24-bar run. **This is not a fix** — the drain's missing
transport-gate classification and the host's pre-clear detection gap are a separate, pre-existing defect pair
outside this change's scope, recorded here because they block every content HITL run on a device that boots
with a pending workspace save.

### The growth-sweep attempt, and why it stopped

The sweep was attempted as `STIM-BASE-2-2-01-TEMP-RWSO-GROWTH` (forked from `STIM-BASE-2-ONE-OD-01`,
`--record-only --record-bars 4`, track 6 slot 1). Two device facts stopped it, both worth recording:

1. **A boot census needs a forced reboot.** `capture_session.py` / the managed harness open serial, which only
   *sometimes* resets the Teensy — the first growth step's log has **no** boot section at all (78 lines, device
   uptime ~885 s), so no `capture_prewarm` census. Per-step censuses therefore require a re-flash between
   steps, not just a serial open.
2. **Repeated runs without a reboot accumulate working-set state.** Before the re-flash the same device read
   `live=8,170KB` with `heap_free=52KB, heap_min=52KB` and `chunks_free=431` — against `live=4,658KB` and
   `chunks_free=441` at the fresh boot of Leg 1 — i.e. **~3.5 MB more live residency after a handful of runs,
   with the internal heap pinned at its 52 KB minimum.** The per-owner vector at that point is the S1.1 census
   (`tags=404/715/148/0/1/0/58/0/0/10/2/64/66/68/96/144/0`, `tag_total=1776KB`). This is the residency the change
   exists to make active-owned, observed as accumulation within one power cycle — recorded as an observation,
   not as a measured slope, since no per-N boot census exists to attribute it to.

The sweep is therefore left **unrun**, with its blocker now the catalog reconcile defect above rather than
scratch-slot approval.

### What this session did not get

- The **S1.3 slope-to-failure sweep** (N committed slots across boots): blocked on the SD catalog reconcile
  defect above. Growing the library across boots is not reliable, so there is no trustworthy per-N census.
- The **content-verified PASS** (Leg 4), which needs the cycle-window re-capture above.
- A baseline (`83be2e19`) comparison run for the 24-bar shape; the pre-change RC-MEM-5 leg is the recorded
  comparator and the boot census baseline is already on record, so nothing here re-derives them.

## P1 — the ~22 MB `#MEM_EXT_REFUSED` abort is a malformed bar-index size, not an allocator defect (2026-09-24, second session)

**Found by** `deferred-slot-restore-liveness`'s A3.7 shape (b) work: the clear-then-reload route the gate
needs aborted the firmware with `#MEM_EXT_REFUSED bytes=22369620` / `bytes=22369612` followed by
`[InternalHeapFirstAllocator] FATAL: both internal heap and external memory are exhausted!` (three
fresh-boot reproductions; a plain-take control records cleanly). This change owns the investigation; the
deferred change stays open on the device gate.

### The caller, not the allocator

The request is **not** an allocator fault. `22369620 = 4 × 5,592,405` and `22369612 = 4 × 5,592,403`: a
4-byte-element vector. The two sites are `LoopEventStore`'s bar index, `BarIndexVec =
std::vector<size_t, InternalHeapFirstAllocator<size_t>>` (`size_t` is 4 bytes on the target):

* `LoopEventStore::appendToTailChunk` — `const uint32_t bar = evt.tick / BAR_TICKS; barFirstIndices_.resize(bar + 1, …)`
* `LoopEventStore::rebuildBarIndex` — the same computation over adopted chunks (SD load, snapshot restore,
  deep clone), which bypass `append`.

Neither had a bound. `BAR_TICKS` is 768 and
`floor(0xFFFFFFFF / 768) = 5,592,405` — exactly the observed element count. So the input is a **tick near
`UINT32_MAX`**, i.e. an uninitialised/sentinel tick (a negative tick cast to `uint32_t`, or a torn
payload byte range), not a content-derived count. The reported tag is `tag=0` (untagged), which is why the
owner was not visible in the refusal line.

**Lifetime through clear/reload:** the store belongs to the reloaded `Loop`. `clear()` drops
`barFirstIndices_`; the next `append` (or a `rebuildBarIndex` after SD-load staging sets `barIndexDirty_`)
rebuilds it from the chunk events, so a corrupt tick in an adopted/reloaded chunk sizes the index directly.
The allocation is a single `resize`, which is why it aborts rather than degrading.

### Reproducer (red → green) and the fix

* **Reproducer:** `test/test_loop_event_store` → `test_out_of_range_tick_cannot_size_the_bar_index`.
  Red on the old code (the event was accepted and the index grew to 5.6M entries); green now.
* **Owner fix (`LoopEventStore`):** a supported-tick ceiling
  (`LoopEventStoreConfig::MAX_INDEXED_BARS = 4096`, `MAX_INDEXED_TICKS = 4096 × BAR_TICKS`), with
  `append` **refusing** `evt.tick > MAX_INDEXED_TICKS` as `LoopEventStoreAppendDeny::TickOutOfRange`
  (named and logged, deliberately **not** silently clamped — a cap that hides the corrupt producer is the
  failure mode to avoid), and `rebuildBarIndex` skipping an out-of-range bar defensively for adopted
  content. 4096 bars is far above the 64-bar target: every real loop and over-run take is unaffected, and
  the index is capped at 16 KB instead of 22 MB.

### What is **not** established (the fix claim is bounded)

**What is fixed:** the **oversized allocation** is bounded. A tick beyond `MAX_INDEXED_TICKS` can no longer
size `barFirstIndices_` to ~5.6M entries, and `rebuildBarIndex` no longer grows the index past
`MAX_BAR_INDEX`. That removes the observed ablation, and nothing more.

**What is NOT resolved**, and must not be read into the fix:

* **the producer of the corrupt tick.** It is still unidentified, and it is intermittent: the full a37b
  sequence re-run with the guard raised nothing at all (`captures/prod-probe-01/`), so the raise is
  state-dependent and was not caught.
* **adopted-content integrity.** `rebuildBarIndex` now *skips* an out-of-range bar, but the invalid event
  **stays in its chunk**. Every other reader of that chunk — playback gather, display projection, content
  resolution — still sees the bad `tick`. The bound protects one index, not the content.
* the route that produced the raise is **not** shown safe or semantically correct by this bound, and
  `deferred-slot-restore-liveness` keeps reload-on-clear **disabled**.

**Next step (read-only first):** the committed-versus-empty trace below, then a fixture that can assert its
own preconditions before any further device attempt. Catching the raise (repeat, allocation-site context, or
SD-load-staging instrumentation) remains open but is not the next step.

Evidence: `captures/a37b-01/`, `captures/a37b-02/`, `captures/a37b-03/` (aborts),
`captures/a37-control-02/` (clean control), `captures/prod-probe-01/` (no raise).

### Bounded clear/reload probe (2026-09-24, third session) — the corrupt tick is **not** reproduced by a clean clear-then-reload

With the reload-after-external-reset route **temporarily enabled** in an investigation build (reverted
afterwards; `src/StorageManager/LoopSlotRestoreQueue.cpp` restored byte-identical), the clear/reload probe
(`scripts/hitl_clear_reload_probe.py`) cleared three populated slots and watched the reloads:

| CLEAR (1-based) | reload request | outcome |
|---|---|---|
| track 3 slot 2 | `RRQ,2,1,1,0` queued → running → committed | clean, **no** `out-of-range tick` |
| track 3 slot 3 | `RRQ,2,2,1,0` queued → running → committed | clean, **no** refusal |
| track 3 slot 4 | reload window not captured (probe closed 8 s after the press) | unknown |

No `#MEM_EXT_REFUSED`, no FATAL, and no `LoopEventStore refused out-of-range tick` line appeared in any
captured window, so **the clean clear-then-reload of a populated slot does not produce the
near-`UINT32_MAX` tick** in the two windows that were read, and the "torn payload from the clear's own
save" hypothesis is **falsified for this condition**. Two caveats
recorded rather than smoothed over: the reloads landed ~9 s after each CLEAR, i.e. **after the probe's 8 s
window**, so the per-slot counters printed `requests=0` even though the reloads appear later in the log; and
the a37b reproductions additionally had a **long record + deferred save in flight** at the CLEAR moment,
which this probe did not, so the producer is still unidentified and its trigger is state-dependent.

**Side effect to disclose.** Clear-then-reload re-commits the cleared slot from its payload, so the CLEAR
attempts did **not** remove the track 3 slots 2–4 content — the reload restored it. Removing that content
needs a clear on the **normal** firmware (route off, so no automatic reload) and then a check of whether the
payload file was itself removed or merely left for a later boot scan.

Evidence: `captures/cr-probe-01/session.log`.

### Producer reproduction attempt (2026-09-24, fourth session) — intermittent; the raise did **not** recur

The a37b sequence was re-run with the reload route temporarily enabled (reverted byte-identical) and the
guard in place: prepare track 6 slot 1 with a short pass, pre-CLEAR it, record 8 bars on scratch track 5
slot 1, then CLEAR. Result (`captures/prod-probe-01/session.log`):

* the record completed and the CLEAR took the drain path —
  `Clear waiting for deferred save completion` → `State saved successfully (v4)` →
  `Clear ignored — selected slot is empty`;
* **no** `LoopEventStore refused out-of-range tick` line, **no** `#MEM_EXT_REFUSED`, **no** FATAL.

**This is not evidence that the guard prevented it.** Had the corrupt tick reached `append`, the guard
would have logged the refusal; no refusal appeared, so the corrupt tick simply **did not occur** in this
run. The a37b raise is therefore **intermittent/state-dependent**, not deterministic — one clean
reproduction attempt cannot identify the producer.

Two secondary observations, recorded because they bound the next attempt:

* the pre-CLEAR's reload request (`RRQ,5,0,1,0,queued`) was **queued but never ran** — no
  `running`/`committed` followed — because the scratch track was still overdubbing when it was selected, so
  restore *preparation* was denied by capture. A queued request is not a `LoadLoopJob`.
* that is also why the CLEAR's drain **completed** rather than naming a wait: no slot-load session was
  active, so `deferredSaveBlockedByActiveSlotLoadSd()` was false. **A3.7 shape (b) was therefore not
  exercised** by this run — a queued request is not "a restore in flight".

**Next step, revised:** to identify the producer, catch the raise rather than assume it. Either (a) repeat
the a37b sequence enough times to catch the intermittent tick and read the refusal's context, or (b) add
allocation-site context to the refusal (the store's owner) so a single catch names the producer, or
(c) instrument the SD-load staging path, which is the only caller of `rebuildBarIndex` outside `append`.
Do not re-enable the route in the change until the raise is either caught or shown unreachable.

### A3.7 shape (b) construction attempt with the guard (2026-09-24, fourth session) — fixture could not hold the condition

A second construction (`scripts/hitl_a37_shapeb_v2.py`, route temporarily enabled) sequenced clear → long
record → re-select → clear to put a `LoadLoopJob` in flight at the save-owed edge. It failed to hold the
condition for a fixture reason, recorded rather than retried:

* the reload target was **already RAM-empty** at the first CLEAR (`Clear ignored — selected slot is empty`
  at 17.579 s), so **no reload was queued**;
* re-selecting it at 45.867 s therefore **started a record** (`Loop 2: Start Recording`) instead of a
  restore, and that record produced an empty take (`Recording stopped … length=0`);
* the final CLEAR found no owed save (`MIDI: Clear selected slot 2`, no `Clear waiting`), so the drain did
  not run.

So the run proves nothing about shape (b) — no restore in flight, no named wait, no completion. The fixture
assumes a target with committed RAM content that survives the boot restore; the target came back from
`RRQ,2,1,0,0,committed` yet read as empty to `hasDataInSlot`.

**That mismatch is now explained and is not a defect.** The earlier probe (`captures/cr-probe-01/`) had
CLEARed track 3 slot 2, whose persistence rewrote the slot payload as **empty**, and the reload that
followed (`RRQ,2,1,1,0`) committed that empty payload (`captures/cr-probe-01/session.log`: clear at 20.772 s,
reload committed at 29.927 s). The next boot therefore restored an empty-but-committed payload, which
correctly has no committed passes and so reads empty to `hasDataInSlot`
(`captures/a37b2-01/session.log`: restore committed at 10.339 s, `Clear ignored — selected slot is empty` at
17.579 s). It does show a quirk of the route-enabled behavior worth naming for the route decision: after a
CLEAR, the stale request makes `needsSlotLoad` true, so the just-emptied slot is re-requested and commits an
empty payload — redundant work the normal (route-off) firmware avoids by keeping the stale terminal as
"attempted". **Conclusion for the gate:** closing A3.7 shape (b) needs the reload-on-clear route
enabled **deliberately in the change** (with the guard) plus a target whose committed content is verified at
the CLEAR edge, not an ad-hoc temporary construction. That is an owner decision on the route, not another
script iteration.

**Device state disclosure:** the attempt cleared/recorded on track 3 slot 2 (already RAM-empty) and left a
failed empty-record attempt on the approved scratch slot track 5 slot 1.

Evidence: `captures/a37b2-01/session.log`.

## A5.1 handoff — census checkpoint, the post-restore budget baseline, and the slope blocker (2026-09-24)

Recorded here per `deferred-slot-restore-liveness` A5.1, which is a handoff, not a second implementation.

### The fixture this change was waiting for is finished and device-verified

`StorageManager::emitPostRestoreCensusIfReady` emits one timestamped Tier-A marker per boot
(`#CAP,<micros>,DIAG,post_restore_census,…`) once every readiness condition holds — background admission
complete, both job instances idle with no live `SlotLoadSession`, undo hydrated, transport stopped, no
post-load holdoff or in-flight persistence work, and no deferred playback prewarm owed. Reader:
`scripts/hitl_boot_census.py`. Final-firmware evidence `captures/census-final-03/`:
`checkpoint_reached=True`, `readiness_attested=True`, `timestamped_capture_line=True`,
`free_chunks=454`, S2b.3's `>= peak + 16` condition met, 0 reconcile failures.

### S2b.3 — post-restore budget baseline: measured and stable

Three consecutive fresh boots (`captures/s13-slope-final/`, sweep updated to read the marker) report
`banner=True`, `reconcile_warnings=0`, `N=29` committed boot-playback-set requests and
**`post_restore_free_chunks=452` of 512 on every boot**, with the restore walk adding no reservation
(512 → 480 both times). That is the figure S2b.3's `post_restore_free_chunks >= peak_capture_reservation + 16`
is read against.

### S1.3 — the slope to failure remains OPEN, with a measured blocker

N is the **boot playback set** (`RRQ … committed`), deliberately independent of the library, so recording
into an *unselected* scratch slot adds a HEADER_READY payload and cannot raise it; the fixture must make each
recorded slot part of the boot playback set before the reboot. The approved scratch range is
track 5 slot 1 and **track 6 slots 1–8, which is fully populated**, so N could not be grown inside it. A
sweep into empty slots **outside** that range grew the payload set while the committed boot-playback set
**shrank** (N 27 → 27 → 26) as each recording evicted other committed payload slots (track 6 lost slots 7–8,
then 6, then 4–5) — a genuine capacity/eviction finding for this change, **not** a valid increasing-N slope.
Those boots also logged `Track 6/7 loaded state: EMPTY` and `DIAG,lcr,skip,restore`, recorded as
observations without a claimed cause.

**Owner direction (2026-09-24):** leave the slots written outside the approved range untouched; prefer
reusing the existing scratch range; clear a defined subset only once explicitly authorised; record both the
SD payload count and the successfully restored committed-slot count at every step. **No further growth sweeps
until then**, so this leg stays open.

### Unchanged from their recorded device runs

S1.1's per-owner census (tags 1..16), S1.4's undo-residency clauses, and S1.5's device refusal point are
unchanged by this session; their evidence stands as recorded earlier in this file. No leg is reinterpreted.

---

## S2b.8 — set-change epoch invalidation / external resets (2026-09-24, G2 execution packet)

Transferred from the archived `2026-09-24-deferred-slot-restore-liveness` A2.1. Authority:
`openspec/specs/lazy-slot-hydration/spec.md` § *Restore requests have identity, named waits, and one terminal
outcome* (and its *A set change supersedes outstanding requests* scenario), **DEC-082** clause 1, and
`tasks.md` S2b.8. Status at packet write: **specified** — no fixture, native run, build or checkoff yet.

### Identity and scope

- **Packet/task:** S2b.8. **Owner:** `StorageManagerInternal::RestoreRequestState` (record + external-reset
  API) and the `MidiButtonActions` clear/undo/redo-clear routes. **No new owner, no ownership transfer**, so
  no formal reassessment trigger fires.
- **Production entry → deferred worker → commit/retirement:** none new. The slice corrects one record
  function and adds two call sites on existing action handlers.
- **Primary files:** `src/StorageManager/RestoreRequestState.cpp` (`noteRestoreSetEpoch`),
  `include/StorageManagerInternal.h` (one read-only identity accessor), `src/MidiButtonActions.cpp`
  (`handleUndoClearTrack`, `handleRedoClearTrack`).
- **Preconditions present:** the record with its full identity, `advanceRestoreRequestGeneration`'s
  original-identity-then-advance pattern, the pre-publication revalidation in `commitLoadLoopJobPublish`,
  and the `refuseStorageCallbackDuringDrain` guard. **Prerequisites in this slice:** none. **External
  blockers:** none — this task is native-completable.
- **Mutation:** (1) `noteRestoreSetEpoch` records `superseded` **before** moving the epoch, so the terminal
  stays under the identity the request was created with, and emits the one `RRQ` transition per decided
  request; (2) `handleUndoClearTrack`/`handleRedoClearTrack` advance the request generation when the
  undo/redo actually applies.
- **Postconditions:** every outstanding request for a changed set, or for a slot whose content an external
  owner replaced by undo/redo-clear, is terminal `superseded`, its record is no longer current, and an
  in-flight job cannot publish into the replacement content.
- **Explicit non-goals:** no runtime set-switch feature is introduced; the commit gate is untouched; the
  conservative `isLoopSlotRestoreAttempted` done-test is unchanged; the drain-advancement clause is S2b.7;
  the "commit by a different owner" clause is **not** re-wired here (see the audit).
- **Commit boundary:** independently buildable and behavior-complete for the external-reset class it names.

### Audit of every external-reset / set-change path (the task's first clause)

| Path | Today | S2b.8 |
|---|---|---|
| User CLEAR (`handleClearTrack`) | **wired** — `advanceRestoreRequestGeneration`: superseded under the original identity, then advance | unchanged (cite) |
| Undo-clear (`handleUndoClearTrack`) | **NOT wired** — `TrackUndo::undoForLoop` restores the pre-clear snapshot into the slot with no generation advance | **wired here** |
| Redo-clear (`handleRedoClearTrack`) | **NOT wired** — `TrackUndo::redoForLoop` removes the slot's content again with no generation advance | **wired here** |
| Set change | **No runtime entry point exists.** The loaded current set is established at boot (`StorageManager::loadCurrentWorkspaceAtBoot` → `loadCurrentSetFromDirectory` → `loadCurrentSetBundleAndActiveLoopSlots`), which calls `resetAllLoopSlotRestoreAttempted()` → `resetAllRestoreRequestState()` — a *full* record reset with `setEpoch = 0`, stronger than `superseded`, and it precedes any request because nothing has been queued yet in that session. `loadCurrentWorkspaceFromSd` (same directory load) has **zero firmware callers**. | **Recorded, not faked.** `noteRestoreSetEpoch` stays the record-level API for the boundary — corrected and native-tested. A production call site is not invented because none exists; the *equivalent external reset* the task allows is the undo/redo-clear class. The existing record-level scenario test is kept and strengthened. |
| A different owner committing that slot | **Already terminates** such a request: `markLoopSlotRestoreAttempted` records `unnecessary`, and `commitLoadLoopJobPublish` revalidates the captured identity before the token check and publishes nothing when stale. | **Not re-wired**, with the reason: the request already reaches a terminal and publication is already blocked; converting it to a generation advance would change the B04/S2b.5 oracle (`unnecessary`) for no safety gain. Recorded as a **spec-wording gap** (the spec's generation clause names it, the delivered mechanism terminates it another way), not a defect. |

**Decision (explicit, as the task requires): undo-clear and redo-clear ARE external resets.** They replace
the slot's content by an external owner (the user), which is exactly the class the spec lists for
`superseded` ("the slot was cleared by an external owner") and for the `requestGeneration` advance ("a user
clear"). Leaving them unwired lets an in-flight restore job publish parsed payload over the content the user
just restored or re-cleared, because its captured identity would still read current.

**Two defects the audit found in `noteRestoreSetEpoch` itself, both in scope:** it recorded the superseded
terminal with the **new** epoch stamped into the record (so the outcome no longer described the identity the
request was created with, and the record read as *current-but-terminal*), and it emitted **no** `RRQ`
transition, so the reason was not observable — which the spec's scenario requires ("AND the reason is
observable").

### Oracle fixed before behavior edits

| Case | Initial state | Ordered actions | Expected observation after each step | Counterexample that must fail |
|---|---|---|---|---|
| **S2b.8-C1** set change, queued | records reset; `beginRestoreRequest(0,2)`; epoch 0 | `noteRestoreSetEpoch(7)` | terminal `superseded`; `restoreRequestEpochFor(0,2) == 0` (original identity); `restoreSetEpoch() == 7`; `restoreRequestRecordIsCurrent(0,2) == false` | epoch-stamped record (`record.epoch == 7`) — the pre-fix behavior |
| **S2b.8-C2** set change, running | `beginRestoreRequest(0,2)` + `markRestoreRequestRunning(0,2)` | `noteRestoreSetEpoch(7)` | same as C1; the running state is superseded, not left running | a mark that skips a running record |
| **S2b.8-C3** exactly-once / no re-decision | after C1 | second `noteRestoreSetEpoch(8)`; then `markRestoreRequestTerminal(0,2,Committed)` | terminal stays `superseded`; the second call returns **false**; `restoreRequestEpochFor(0,2)` still 0 | re-stamping the record with epoch 8, or overwriting `superseded` with `committed` |
| **S2b.8-C4** stale work cannot publish (set route) | load suite: request driven to commit-pending | `noteRestoreSetEpoch(7)`; then one load service turn | `hasCommittedPasses()` false; `hasLoadLoopJobCommitPending()` false; terminal `superseded` | publishing the parsed body over the changed set |
| **S2b.8-C5** undo-clear is an external reset | reentry suite: content + CLEAR, then a live request on the slot | `handleUndoClearTrack()` | generation advances again; terminal `superseded`; `recordIsCurrent == false`; content restored | no advance (the pre-fix behavior) |
| **S2b.8-C6** redo-clear is an external reset | after C5, live request again | `handleRedoClearTrack()` | generation advances; terminal `superseded`; `recordIsCurrent == false`; content removed | no advance |
| **S2b.8-C7** retry keeps identity (unchanged) | existing `test_restore_request_own_failure_cleanup_keeps_identity_as_retry_wait` | restore-path own cleanup | `waiting_retry`, identity kept, **not** current-mismatched by this slice | routing the retry through an external advance |

### Controls and coverage

- **Seams reused:** the load suite's in-memory SD + production load/queue path; the reentry suite's real MIDI
  dispatch route, real `MidiButtonActions`, real `TrackUndo`, and real `PersistenceDrainScope`. One new
  **read-only** accessor `restoreRequestEpochFor(track, slot)` exposes the record's own epoch so "under the
  original identity" is checkable rather than asserted.
- **Production paths exercised:** `noteRestoreSetEpoch` (record), `handleUndoClearTrack`/`handleRedoClearTrack`
  (action), `handleClearTrack` (existing), the real publication revalidation. No copied lifecycle, no
  success-returning stub on an asserted path.
- **Busy/stale/overlap:** queued, running, and in-flight-at-commit-pending are covered; the parked-instance
  case is already covered by `test_a21_stale_identity_is_rejected_immediately_before_publication`, which this
  slice extends with the set-epoch route.
- **Numbers:** counts from the suite run, not estimated. **Untestable-before-link:** none new.

### Verification recipe

- Focused: `pio test -e native -f test_storage_manager_load -f test_drain_reentry_callback_safety`.
- Broad: `pio test -e native` (expect 2344 + the new cases, 1 skipped, 0 failures).
- Build: `pio run -e teensy41-capture-serial` — the post-link `check_ram1_budget.py` must pass at the
  per-env floor (capture-serial padding ≥ 840).
- **Device:** none claimed. No behavior observable only on hardware is introduced.
- **Discrimination:** each new case must fail when its own mutation is reverted (epoch-order for C1/C3;
  the undo/redo call for C5/C6); the revert is applied, observed, and reverted byte-identical.

### Completion record

- **Oracle/contract unchanged.** No threshold was weakened. The only oracle change is an **addition**: the
  read-only `restoreRequestEpochFor(track, slot)` accessor, which makes the already-normative
  "under the identity it was created with" clause checkable instead of assumed.
- **Fixture implemented and discriminating assertion demonstrated.** Mutation 1 (restore the pre-fix
  epoch-first + re-stamp behavior) → `test_storage_manager_load` **3 failed / 32 succeeded**, failing
  exactly the original-identity assertions (`Expected 0 Was 7`, `Expected 0 Was 8`) and the in-flight stale
  check. Mutation 2 (remove the two handler calls) → `test_drain_reentry_callback_safety` **2 failed /
  6 succeeded**, failing exactly the two new cases on `an undo-clear must advance the request generation
  like CLEAR does` (`Expected 2 to be greater than 2`) and its redo twin (`Expected 3 to be greater than
  3`). Both mutations reverted; `grep -rn "TEMPORARY MUTATION" src/` returns nothing.
- **Native verification.** `pio test -e native` → **2,348 cases / 1 skipped / 2,347 succeeded /
  0 failures** (baseline 2,344 / 1 skipped / 0 failures; +4 cases). Focused:
  `test_storage_manager_load` **35/35**, `test_drain_reentry_callback_safety` **8/8**. Revision context:
  working tree on `feature/runtime-working-set-ownership`, clean before this slice.
- **Build/RAM1 verification.** `pio run -e teensy41-capture-serial` → **[SUCCESS]**; the post-link budget
  check reports `variables 96,192 / ITCM 393,216 (code 391,080 + padding 2,136) / locals 34,880 |
  approved floor: padding >= 840`, i.e. **1,296 B of headroom**. `code + padding` is constant at 393,216
  against the pre-slice `391,064 + 2,152`, so the +16 B of code moved within the bank and **no FlexRAM bank
  was crossed**; `variables` is unchanged.
- **Device verification.** **NOT APPLICABLE** — the slice adds no hardware-observable behavior. It is
  record/action logic fully covered natively, and no device class is claimed.
- **Required reviews and durable docs.** `DECISION_REVIEW` → no formal trigger (decision ladder steps 1–3:
  extend the existing owner, follow **DEC-082** clause 1); no new `DEC-###`. `tasks.md` S2b.8 checked with
  the full record; `CURRENT_WORK` and `PROJECT_STATE` carry the session entry.
- **Task IDs eligible for checkoff:** **S2b.8**. **Stage gates still open:** the S2b gate (device slope,
  content PASS, census stability) and every S1/S2b device leg, all unchanged by this slice; **S2b.7** is the
  next native slice.

---

## S2b.7 — the drain now advances the restore it waits on (2026-09-24; measured, then delivered)

Task: `tasks.md` S2b.7. Authority: `design.md` **D4** (points 1, 3, 6) of the archived
`2026-09-24-deferred-slot-restore-liveness`, its A3.1 verify list, **DEC-082** clause 3, and
`resumable-midi-boundary-playback-maintenance` **task 3.3** (the bounded-commit prerequisite; the commit gate
is **not** relaxed). Status: **fixture implemented / native verified / behavior DELIVERED — task checked.**

### First the measurement (kept, because it is what justified the change)

The archived change claimed the drain "advances rather than cancels", but its own evidence table recorded the
only observed completion as reaching the *save* through `shouldPrioritizeWorkspaceSaveDrain()` bypassing the
slot-load gate for an urgent owed save — the **save** advanced while the restore stayed where it was. A
measurement fixture was built first and showed the gap plainly: with another track audibly playing, a restore
mid-preparation holding the SD session and a save owed, the real `drainPersistenceWorkBlockingDetailed`
returned `TemporarilyBlocked` / `slot_load_active` with `restoreProgressCounter()` **1 → 1** and
`restoreWorkPhase()` **1 → 1** — reported, never advanced. That is the baseline the delivery is discriminated
against.

### What was implemented

- **`StorageManager::serviceDeferredRestoreForDrain()`** (`src/StorageManager/LoopSlotRestoreQueue.cpp`) — one
  `StorageManager`-owned entry (no new owner; declared beside `processDeferredLoopSlotRestore`). It runs the
  **same** selection/admission + bounded step the deferred-load frame uses (`DeferredJobScheduler::runFrame` →
  `selectSubmittedLoadJobs` + `stepSubmittedLoadJobs`) with a budget from the same resolver. No second loader,
  queue or admission policy.
- **The drain loop calls it once per iteration** (`src/StorageManager/SyncDrainSaveState.cpp`) whenever
  `deferredSaveBlockedByActiveSlotLoadSd()` is true, *before* `processDeferredSaveState`, so a release in that
  iteration lets the save proceed in the same turn.
- **D4 point 6 — termination.** Once the restore reaches a wait the drain cannot clear, the episode ends with
  that wait classified and reported, instead of the drain spending its whole budget re-servicing a gated
  request. The first working version omitted this and produced `ExceededSliceBudget` → `Failed` with the save
  still owed; the termination clause is what turns it into the named `waiting_commit_gate` wait.
- **D4 point 1 — active **and** parked instances — needed no new code.** The existing selection step already
  promotes a parked instance (`ensureActiveLoadLoopJobSelected` → `LoadLoopSelectionPolicy::PromoteParkedLow`).
  An explicit `promoteParkedLoadLoopJobForDrainService()` was written, **proven redundant by removing it** (the
  parked case stays green), and deleted rather than left as parallel surface.
- **The commit gate is untouched.** `stepSubmittedLoadJobsImpl` still requires the global idle gate for
  commit/cleanup and records `waiting_commit_gate`; the drain never forces it. Bounding that stays task 3.3.

### Behavior, measured after delivery

| Shape | Result |
|---|---|
| Another track audibly playing (commit denied) | progress **1 → 8**; `TemporarilyBlocked` / **`waiting_commit_gate`**; the loop **does not publish**; the request stays `waiting` with no terminal; the save stays owed |
| Focused slot, nothing timing-critical | the drain advances the restore to **`committed`** and discharges the owed save, with **no external advancement** in between |
| Session held by the **parked** instance alone | serviced (**progress advances**), still publishes nothing while the gate denies |
| Restore already released (control) | the same drain is no longer gated by the slot load and discharges the save |

**Recorded nuance, not a defect:** with the transport idle and the restore on a **non-focus** slot, focus policy
parks it before it can be stepped, and the drain's urgent-save priority then discharges the save *without*
advancing the restore. That is the shape the archived A3.7 shape (b) device run recorded, so the gate-admits
fixture uses the focused slot and the parked/playing cases assert the commit gate rather than the save's
discharge.

### Completion record

- **Oracle unchanged**; no threshold weakened. **Discriminating by removal:** disabling the drain's service
  step reverts to the baseline above and fails exactly the three advancement assertions (`Expected 1 Was 1`,
  `progress=2->2`, `Expected 3 Was 3`); the mutation was reverted and `grep "TEMPORARY MUTATION"` is empty.
- **Native:** `test_storage_drain_restore` **10/10**; `test_drain_reentry_callback_safety` **8/8**; full native
  **2,352 / 1 skipped / 0 failures** (2,350 before the two added cases). One build fix: the reentry suite now
  links `DeferredJobScheduler.cpp`, because the real drain reaches it.
- **Build/RAM1:** `teensy41-capture-serial` **SUCCESS**, `variables 96,192 / code 391,080 + padding 2,136`
  (1,296 B above the 840 floor; `code + padding` constant, no bank cross).
- **Device:** **NOT RUN.** Every claim here is native. A hardware re-run of the archived A3.7 shape (b) would
  need the clear-then-reload route the owner deliberately kept disabled, so **no device class is claimed**.
- **Task IDs eligible for checkoff:** **S2b.7**.

---

## Handoff from `restored-library-stability` — the restored library is stable, with the 36/8 disposition (2026-09-24; handoff only, **no stage started**)

Task: `restored-library-stability` **4.1 — runtime handoff**. Its verify is "the handoff is recorded in that
change; this change does not start S1, S2b or S3". **Nothing below is claimed as delivered here**, and no
stage was started: this is the evidence transfer the blocked device legs were waiting on.

**Evidence handed over** (read-only device observation; the only things sent were `!LIB_OBSERVE` and the
upload that performs the reliable reset):

- report `captures/s13-libobs-stability-3boot.json`; identity
  `captures/s13-libobs-stability-3boot.identity.json` (`git_commit 74cadc7b`,
  `firmware_hex_sha256 ede704b614b7e9cd…`, env `teensy41-capture-serial`).
- **three consecutive fresh boots identical in the observed per-slot manifest** (0 differences, 64/64
  slots); the two earlier eligible boots agree, so five complete-manifest observations share the observed
  manifest sha256 `4e451a07d0a94c18fc177967521344c31ba4e9c8774a42d19aa1114f46e963f8`.
- **Claim recorded precisely: no differences in the observed fields; no destructive commands issued.**
  Identical observed records — or even identical file hashes — would not prove unchanged payload bytes or
  the absence of a write. **What it does not prove:** unchanged bytes, capacity, or eviction. It removes the
  *stability* obstacle; it does not measure the slope.

**Disposition — 36 / 8 / 8 are three different populations** (full detail:
[`restored-library-stability/ARCHITECTURE-REVIEW.md`](../archive/2026-09-24-restored-library-stability/ARCHITECTURE-REVIEW.md)
§ Packet B → *Disposition*):

- **36** = payload-present slots (RAM mirror + SD file + valid token).
- **8** = slots with in-RAM committed content in the **pass** representation
  (`loop.passes.hasRecordPass() || !overdubPasses.empty()`). This was read as "the loaded working set"
  before the observability correction below; it is the pass-backed **subset**, not the loaded population.
- **8** = initial playback-required (`isBootPlaybackSlot`, the selected/active union) — a **different** set
  from the pass-backed one (overlap 4; pass-backed-only `(1,0) (1,1) (2,0) (5,1)`; required-only
  `(2,1) (4,0) (6,0) (7,0)`).
- the remaining **28 payload-present slots are not unloaded — they are published-root-backed**. They hold
  resident, playable content installed as a published snapshot root (`Loop::storePublishedSnapshot` on the
  LSV1 load path), and their `passes` is empty. The corrected observation reports them as loaded (see
  § S1.3 — observability correction). Separately, **28 slots are payload-absent**. The two 28s are a
  numeric coincidence and must not be conflated.

**What each handed-over leg may now use it for** — the leg still owns its fixture, oracle and verdict:

- **S1.3 — device slope:** the stability obstacle recorded in `tasks.md` S1.3 is **removed** (a scratch
  slot's payload survives boots), and the "explain the 28 unloaded slots" question is **answered**: they are
  published-root-backed and resident (§ S1.3 — slope-fixture definition). The slope fixture must **name and
  vary the loaded population** (select/enable each scratch slot so boot actually loads it), not merely
  create an SD payload; the payload inventory (36) and the loaded set must be reported separately, with the
  pass-backed and root-backed subsets beside the union. The failure point and error remain **unmeasured**.
  The fixture definition must answer the remaining open "why" question below and is **not** established by
  the repeatable observation.
- **S2b.3 — post-restore budget evidence:** the "baseline's post-restore value is not measured" gap is
  unchanged; this stable library is the fixture that can hold the measurement. Not measured here.
- **S2b gates:** the device half's *slope-to-failure* blocker is now only the fixture, not library
  stability. No gate is checked here.
- **S3 — lease ownership:** the library is stable enough to hold a lease-acceptance run; the stage order is
  not bypassed and S3 does not start here.

**Transferred into this change (2026-09-24), owned here and unchecked:** `restored-library-stability`
**2.3 — drain fixture** and **Packet C — S2b.7 drain device confirmation** are now the three unchecked
"S2b.7 (TRANSFERRED …)" entries in `tasks.md` (the condition fixture, the device leg with the in-drain-call
progress definition, and the independence clause). They are independently reportable, gate neither the
runtime legs above nor the library closeout, and the native S2b.7 delivery recorded above continues to claim
**no device class**.

**Next runtime work (recorded, not started at handoff): S1.3 — slope-fixture definition.** At handoff it had
to explain **why 28 verified payloads read as unloaded** and **why only four initial-playback slots overlap
the pass-backed set**. *Answered 2026-09-24, sixteenth session, by the read-only trace below:* the 28 are
published-root-backed and resident; the four required-but-not-pass-backed slots are two root-backed
(`(2,1)`, `(4,0)`) and two payload-absent enabled slots (`(6,0)`, `(7,0)`). **The three-boot observation is
repeatable in the fields it measured** — it is **not** validation of the pass-backed content count, which
the correction below shows under-reported the loaded population. Repeatability does **not** establish
correct hydration or a usable increasing-population fixture, and S1.3 is not proven by it.

---

## S1.3 — slope-fixture definition: the loaded population traced read-only (2026-09-24; **definition only, no sweep**)

> **Superseded in one respect (2026-09-24, seventeenth session).** The per-slot attribution below — "8
> pass-backed and 28 published-root-backed" — was an **inference from the publish branch**, and the
> corrected per-slot observation **contradicts it**: the revalidation boot reports **9 content-bearing
> slots of 36 verified payloads**, every other payload slot adopting zero passes and installing no root.
> See § *S1.3 — observability correction* → *Task 3*. The **owner totals** recorded here (27 pool chunks,
> 2,155 KB / 78 slabs of snapshot arena) reproduce exactly; the **per-slot** reading does not.

Task: `tasks.md` S1.3. Scope: **read-only** — the archived `restored-library-stability` captures
(`captures/s13-libobs-full-02/`, and the identical `s13-libobs-stab-01/02/03/`), the firmware source, no
device action, no firmware edit, no SD write. **The premise supplied with this task — "28 verified payloads
remain unloaded" — is falsified below.** S2b.7's device confirmation is a separate transferred task and is
untouched by this section.

### The trace — enumeration → request → publication → later residency

The boot playback set is the per-track `enabledMask` (`[StorageManager] Boot transport t=… enabledMask=…`):
`t0 s0`, `t1 s2`, `t2 s1`, `t3 s0`, `t4 s0`, `t5 s0`, `t6 s0`, `t7 s0` — exactly the observation's
`required_for_initial_playback` set. Boot queues it focus-first (`Boot focus track=4 active=0 selected=0`,
`Queuing boot playback loop slot restore 6 pending; first 4/0`), then the background admission scan offers
**every remaining payload slot** (the RRQ `queued` burst from ~7.1 s). The observation's `offered` flag
matches: 36 offered, 28 not offered, and **every offered slot has an RRQ record ending `terminal/committed`**
(36/36 — parsed from the boot log; the observation's `request`/`terminal` columns reproduce it exactly).

| slot | payload | last RRQ | `k` | `a/l/v` | publication branch | resident |
|---|---|---|---|---|---|---|
| `(0,0)` *required* | verified | terminal/committed | 1 | 2 / 15360 / 729 | legacy → `adoptPersistedSnapshot` moves `snapshot.passes` into `Loop::passes` | yes, **pass-backed** |
| `(2,0)` | verified | terminal/committed | 1 | 1 / 6912 / 196 | legacy | yes, pass-backed |
| `(5,1)` | verified | terminal/committed | 1 | 1 / 3072 / 192 | legacy | yes, pass-backed |
| `(2,1)` *required* | verified | terminal/committed | 0 | 0 / 0 / 0 | **LSV1** → `storePublishedSnapshot` published **root** | yes, **root-backed** |
| `(4,0)` *required* | verified | terminal/committed | 0 | 0 / 0 / 0 | LSV1 root | yes, root-backed |
| `(4,7)` | verified | terminal/committed | 0 | 0 / 0 / 0 | LSV1 root | yes, root-backed |
| `(3,1)` | verified | terminal/committed | 0 | 0 / 0 / 0 | LSV1 root | yes, root-backed |
| `(6,0)` *required* | **absent** | *never RRQ'd* | 0 | 0 / 0 / 0 | none — nothing to restore | **no** |

Representative full trace, `(2,1)`: enumerated by the scan and offered; `RRQ,2,1,0,0,queued` at
3,122,760 µs → `running` at 6,284,641 µs → `terminal,committed` at 6,321,844 µs. Publication runs
`commitLoadLoopJobPublish` → `applyLoadedLoopSlotBodyCommit`; the branch is
`usesLoopSnapshotBodyFormatV1 = (probeLoopSnapshotBodyFormat(…) == SupportedV1)`.

**Why the pass count is not the loaded count (VERIFIED by the two exhaustive publish branches).**

- **Legacy payload** → `applySnapshotToLoop` → `Loop::adoptPersistedSnapshot`, which does
  `passes = std::move(snapshot.passes)` and sets `loopLengthTicks` — so `passes` is populated and the
  observation's `has_committed_content` (`loop.passes.hasRecordPass() || !overdubPasses.empty()`) is `1`.
- **LSV1 payload** → `applySnapshotToLoop` receives an empty legacy oracle, so `passes` stays empty and
  `loopLengthTicks` is 0; the content is installed as a **published snapshot root**
  (`loop.storePublishedSnapshot(std::move(loadedLoopSnapshot), 0, kSnapshotCoverageCurrent)`).
  `publishedSnapshot()` is exactly what playback reads (`Loop::tryGatherPublishedSnapshotEventsInWindow` →
  `publishedSnapshot()` → `publishedSnapshotIsCurrent()`), so the slot **is loaded and playable**.

The observation has no root-presence field, so those 28 slots read `has_committed_content = 0`,
`loop_length_ticks = 0`, `committed_event_count = 0`.

**Later residency (the "unloaded" 28 are consuming residency).** From the same boot log's `#MEM_OWNERS`
ticks:

| uptime µs | `slab_live` | `slab_used` KB | `snap` KB | `chunks_free` |
|---|---|---|---|---|
| 4,845,405 (before the restore drain) | 0 | 0 | 0 | 480 |
| 8,819,368 | 61 | 1,934 | 1,698 | 461 |
| 12,820,394 (after settle) | 78 | 2,455 | 2,155 | 453 |
| 16.8 s … 40.8 s | 78 | 2,455 | 2,155 | 453 (flat) |

So the boot restores cost **27 pool chunks (480 → 453)** and **2,155 KB of snapshot arena across 78 slabs**,
and the residency is **flat after settle**. The `capture_prewarm` walk reports `credits=0` on all 16 rows
(S2b.1: inactive slots hold no canonical capture credits; the enabled set walks 512 → 480, the library walk
480 → 480) — the *canonical reservation* is flat; the *payload and root residency* is what grows.

### The four initial-playback slots absent from the pass-backed committed set

`(2,1)` and `(4,0)`: payload present, offered, restored to `terminal/committed`, **root-backed** (LSV1) — they
are loaded; they differ from the pass-backed set only in representation.
`(6,0)` and `(7,0)`: `enabledMask=0x1` says the boot playback set wants slot 0, but the payload is **absent**
and neither slot is ever offered or RRQ'd — the recorded `Track 6 loaded state: EMPTY` /
`Track 7 loaded state: EMPTY`. These are the only two genuinely-unloaded required slots, and the cause is a
**payload-less enabled slot**, not a restore failure. `(0,0)`, `(1,2)`, `(3,0)`, `(5,0)` are required **and**
pass-backed.

### Does the observation report current content accurately?

| observed field | verdict | basis |
|---|---|---|
| `payload` presence/token | **accurate** | RAM mirror + SD file + token verify; 36 verified, all 36 offered and restored |
| enumeration / `offered` | **accurate** | matches the RRQ set exactly (36 offered, 28 not, 0 populated-unoffered, 0 offered-without-payload) |
| request state / wait / terminal | **accurate** | 36/36 last RRQ `terminal/committed` matches the observation |
| `required_for_initial_playback` | **accurate at the instant** | matches the boot `enabledMask` set |
| `committed_terminal_in_window`, window framing | **accurate** | all restores completed before the 13.0 s window; `i=0`, `restore_completions=0` |
| **`has_committed_content` / `committed_at_checkpoint` / content identity `a,h,l,v`** | **NOT a complete content signal — under-reports** | pass-backed only; excludes the 28 published-root slots, which hold resident, playable content |

**Consequence:** the `36 payload / 8 committed` gap is **not** evidence that 28 slots failed to hydrate. It is
`N_payload = 36`, `N_pass = 8`, `N_root = 28` (inferred from the two-branch publish path and corroborated by
the snapshot-arena growth), plus the two payload-absent enabled slots. The observation is **spec-non-conformant
for root-backed slots** — the requirement says the content field reports "whether the slot holds committed
content", and a published root is committed content. This is an **observability finding owned here**, not a
library-loss finding; the archived change's firmware is not edited and no new sub-change is created.

### Controlled fixture definition (device slope) — vary the loaded population, hold active roles fixed

**Populations (report separately, never conflate).**

- `N_payload` — payload-verified slots; the population boot restores today (36).
- `N_pass` — pass-backed subset (`has_committed_content`; 8 today). **Not** the loaded population.
- `N_root` — published-root subset (28 today; inferred, no per-slot field exists yet).
- `N_active` — active roles: per-track `active`/`selected`/`playing` + transport state. **Held constant.**

**Controlled variation.**

1. **Fix the active role** — one active slot, transport stopped, no record/overdub/edit during the measurement
   boot (the observation runs already do this: focus `t4 s0`, all other tracks `playing=0`).
2. **Vary `N_payload` one slot per boot, inside the approved scratch range only** (track 5 slot 1, track 6
   slots 1–8): save the slot, re-upload for a fresh boot, boot, measure. Because background admission offers
   every payload slot, each added payload grows the loaded population by exactly one while `N_active` is
   unchanged. This is the controlled increasing-population fixture the observation lacked.
3. **Per boot record** — `N_payload`, `N_pass`, `N_active`; the post-restore census marker (`free_chunks`,
   readiness fields); the `#MEM_OWNERS` progression (`slab_live`, `slab_used`, `snap`, `chunks_free`) once
   settled; the per-owner tag census (`scripts/parse_working_set_census.py`); the `capture_prewarm` walk rows;
   `LIBOBS` for the manifest; RRQ for the restore outcomes (assert every populated slot is offered and
   `terminal/committed`).
4. **Report separately:** canonical **reservation** (expect flat in inactive-slot count — S2b.1/S2b.4),
   canonical **payload/chunks** (expect growth with `N_payload`), **root/snapshot residency** (expect growth),
   and the **runtime directory** (expect growth).
5. **Failure point** — the first N at which the pool or the snapshot arena refuses (a `#MEM_EXT_REFUSED`, a
   `refused` restore terminal, or `chunks_free` at/below the reserve), plus the recorded error.
6. **Stop conditions** — any `FATAL`; any non-terminal request; any unexplained manifest difference (the 2.2
   comparator); any write outside the approved range. **Do not** use `committed_at_checkpoint` as N.

**Non-claims.** Active roles fixed means the *active* working set is constant, so residency growth is
attributable to the inactive loaded population — it is not a claim about active-role scaling. The definition
is not a run: **no sweep has been performed**, and the failure point is still unmeasured.

### Open (recorded, not implemented here)

- ~~A **per-slot root-presence / representation field**~~ — **implemented** by the observability correction
  below (2026-09-24); this change owns it and the library change is not reopened.
- The two **payload-absent enabled slots** `(6,0)`/`(7,0)` are a library/config fact the fixture must not
  mistake for a restore failure.

---

## S1.3 — observability correction: content is a union of two representations (2026-09-24; **firmware + host, native-verified; device legs pending a re-plug**)

Task: `tasks.md` S1.3, the observation half. The read-only trace above found the observation
**spec-non-conformant for root-backed slots**: `has_committed_content` / `committed_at_checkpoint` reported
only the **pass** representation, so the 28 published-root-backed slots read as `0` and the count was read
as the loaded population. This slice corrects the representation and nothing else: no lease pool, no
restore change, no firmware behavior outside the observation.

**What changed (production path).**

- `LibraryObservation::SlotInput`/`SlotResult` carry the two representations separately
  (`hasPassContent`, `hasRootContent`) and `hasCommittedContent` is their **union**.
  `stateIsPopulated` and `observed absence` both read the union, so a root-backed slot is `populated` and a
  slot carrying both is populated once.
- `LibraryObservationSession::observeSlot` observes `hasRootContent = Loop::publishedSnapshotIsCurrent()`
  (definition D4's "valid published-root content" — an LSV1 restore leaves `passes` empty) and the union.
- **Invalidation now includes the root publication identity.** `contentRevisionIdentity` reads the pass
  side only, so a root replaced between slices was previously invisible. Per observed slot the session
  records the root's validity and an FNV-1a hash of the non-owning root identity, its publication revision
  and the committed-pass revision it covers, and `recordStillCurrent` re-checks it immediately before
  emission. The root check is **guarded by `loopsAllocated()`** because `Track::getLoop` calls
  `ensureLoopsAllocated()` on its const overload: an unguarded check would itself allocate a track between
  slices and invalidate records for the observation's own side effect (found by the O01 regression while
  implementing this).
- The wire carries the split: the slot record adds `kp`/`kr` beside `k` (the union), and the `LIBOBS,E`
  frame adds `P` (pass-backed) and `R` (root-backed) beside `m` (the union). The frame stays inside
  `kMaxCaptureTextBytes` (192 B).
- Host: `scripts/hitl/library_observation.py` decodes all three and exposes
  `content_representation_observed` (a capture written before the change reports the two subsets as
  **absent**, never as two false flags); `scripts/hitl/library_manifest.py` compares both representations
  and both counts, so a representation change fails the manifest even when the union is unchanged.

**Transition oracle and evidence.**

- New classifier cases prove pass-only / root-only / both / empty and the union counts
  (`test_content_representation_pass_only_root_only_both_and_empty`), and that invalidation clears both
  representation bits.
- New session case **O09** proves the root-backed shape end to end through the service hook: a current root
  is content without passes, and **replacing the root between slices invalidates the record** under an
  unchanged request identity *and* an unchanged pass identity.
- New session case **O10** proves the end record reports the loaded count as the union with one of each
  representation beside it.
- Host: 3 new parser cases (new wire, root-backed shape, pre-correction capture) and 1 new comparator case.

**Non-claims.** This corrects what the observation reports; it does not change what the runtime loads and it
does not by itself settle the slope. The four required-but-not-pass-backed slots remain two root-backed and
two payload-absent enabled slots. **The scratch-range limit is not a capacity failure.**

**Device status at the end of this session.** The firmware built (`RAM1 variables 96,192 / code 391,160 /
padding 2,056 / locals 34,880`), the full native gate is green, and after a re-plug the corrected firmware
was flashed. The read-only revalidation boot then ran **twice**, and the slope sweep was attempted. Both
results are below.

### Task 3 — library revalidated with corrected telemetry: the trace's per-slot premise is CONTRADICTED

Two independent read-only boots of the same library, same firmware, `complete=1`, `turn=64`,
`representation_observed=true` (`captures/s13-corrected-02/`, `captures/s13-corrected-03/`) agree on
**every** field:

| quantity | value | meaning |
|---|---|---|
| `payload_inventory` | **36** | payload present *and* token verified (tracks 0–5) |
| `initial_playback_required` | 8 | the boot playback set |
| `observed_restore_completions` | 0 | window-scoped; the restores landed before this window |
| `committed_at_checkpoint` | **9** | slots holding content in **either** representation |
| `pass_backed_at_checkpoint` | **9** | of those, the pass-backed subset |
| `root_backed_at_checkpoint` | **7** | of those, the published-root subset (7 of the 9 also have passes) |

The content-bearing set is identical across both boots: `(0,0) (1,0) (1,1) (1,2) (2,0) (3,0) (4,0) (5,0)
(5,1)`. **Every other payload slot reports `passes=0`, `loop_length_ticks=0`, `committed_event_count=0`, and
no published root at all** — not a stale root, not a root that fails the currency test:
`Loop::publishedSnapshot()` itself is null (the `kc`/`kr` instrumentation was added specifically to separate
those cases and it reports neither).

**This contradicts the sixteenth-session trace's per-slot attribution** ("8 pass-backed and 28
published-root-backed"). The publish branch it cited does install a root whenever it adopts
(`applyLoadedLoopSlotBodyCommit` → `loop.storePublishedSnapshot(std::move(loadedLoopSnapshot), 0,
kSnapshotCoverageCurrent)`, `StorageLoopIo.cpp`), so a slot that hydrated by either path would show content
here. It does not: **27 of the 36 token-verified payload slots hydrate nothing.** The observation was
corrected, and the correction is what falsifies the inference — this is the representation fix doing its
job, not a new defect in it.

So the corrected count is **not 36 loaded slots**: it is **9 loaded slots out of 36 verified payloads**.
`has_committed_content` is no longer an under-count of a 36-slot working set; it is the accurate count of a
9-slot one. The question the trace set out to answer is re-opened with better data: *why do 27 offered,
token-verified, `terminal/committed` payloads adopt zero passes and install no root?* Two candidates remain
and this observation cannot separate them: the payload bodies genuinely hold no events (the token verifies
the file, not its contents), or the restore's publication is discarded after the terminal is recorded.

**Owner census on the same boot** (`#MEM_OWNERS`, parsed by `scripts/hitl_library_growth_sweep.py`
`parse_mem_owners`): settled `chunks_free 450`, `slab_live 78`, `slab_used 2,455 KB`, `slab_unused 40 KB`,
`snap 2,155 KB`, `tag_total 404 KB` — the same owners the sixteenth-session trace recorded for its
36-payload boot (453 / 78 / 2,455 / 40 / 2,155), flat from 173 s to 209 s. The **owner total** attribution
stands; the **per-slot** attribution does not: 2,155 KB of snapshot arena is held by something this boot,
and only 9 slots are loaded.

### Task 4 — S1.3 slope sweep: attempted, blocked at the first step, no refusal

`scripts/hitl_library_growth_sweep.py --track 6 --slots 8 --first-slot 1 --record-bars 4`
(`captures/s13-slope-01/`) recorded and saved (6,1) — `CurrentSet saved successfully` ×4 — then rebooted.
After that reboot: `Track 6 loaded state: EMPTY`, `N=36` (**no new payload was enumerated**), and the
settled owner census is **identical to before the save** (78 / 2,455 KB / 2,155 KB / `chunks_free 450`).
The next step's reset did not take (the board left the USB bus again), so the sweep stopped at **1 of 8**
steps with **no refusal of any kind**: no `#MEM_EXT_REFUSED`, no `refused` terminal, `chunks_free` never
near the reserve.

Per the pinned rule this is **not** a capacity failure and **not** an S1.5 result — it is a **fixture-path
blocker**: the saved scratch payload did not survive the reboot, which is the obstacle the sixteenth-session
handoff recorded as removed, and the three-boot repeatability did not cover it (an untouched library
reproducing its observed manifest is not evidence that a *new* payload survives). **S1.5 — refusal point
stays open, and the measured population range is 0 added slots.** **S2b.3** was separately measured on the
revalidation boot: `post_restore_census free_chunks=450` against a required
`peak_capture_reservation + 16 = 16` → **met**, with the canonical reservation walk `512 → 480` (enabled
set) and `480 → 480` (library set), `credits=0` on all 16 rows.

**Recommended next step (not taken here).** The fixture cannot grow until the save/restore survival of a
*new* scratch payload is resolved — the same question `runtime-working-set-ownership` S1.3's slope depends
on. That is a state-changing investigation (it needs a write and a reboot), not another read-only trace, so
it is recorded as the blocker rather than started under this observation fix. **Update, same day:** that
investigation was then authorized and run — see the section below.

---

## Save/restore survival of one approved scratch slot (2026-09-24, eighteenth session; owner-authorized write + reboot, read-only trace first)

Owner direction: investigate **one approved scratch slot** end to end with a fixed oracle (pin the slot
before recording; prove the payload body holds the events before reboot; follow the same payload through
parse → publication → settled observation after reboot and name the first boundary where content differs).
Read-only first; repeat the write within the existing scratch authorization only; clearing needs separate
permission. Two claims corrected: *27 empty results do not prove 27 lost recordings*, and *S2b.3 needs the
full `peak capture reservation + 16`, not just 16*.

### Oracle 1 — the slot, pinned

- User-facing: **track 6, slot 2** (`Loop 2`, 1-based). Zero-based `(trackIndex 5, slotIndex 1)`.
- Payload path: `/MidiLooper/current/slots/loop_05_01.bin` (`CurrentSetStorage::formatLoopSlotPath`,
  `snprintf("%s/loop_%02u_%02u.bin", kCurrentSlotsDir, trackIndex, slotIndex)` with
  `kCurrentSlotsDir = "/MidiLooper/current/slots"`); temp `.tmp`; seal journal `.sealj`.
- Authorized range: track 5 slot 1 + track 6 slots 1–8. **No clear was issued.**

### Negative result — the 27 empty observations are NOT evidence of 27 lost recordings

The revalidation boot loads all 36 token-verified payloads and logs **zero** payload-body failures:
`grep -c "treating slot as empty|loop read failed|token mismatch"` over
`captures/s13-corrected-03/boot-census.log` and `captures/s13-slope-01/step00_slot1/boot.log` is **0** (the
only boot-hygiene lines are workspace/bundle temps). A payload whose body cannot be read logs
`treating slot as empty` and resets the slot; none does. So the 27 slots that report content-less are
**empty-bodied payloads that were enumerated, probed and hydrated without error**, consistent with the
device's own `stepDeferredEmptyLoopPersist` path writing a metadata-only body for a slot with no passes.
`payload=present_verified` + `terminal/committed` establish a **valid token and a completed restore**, never
a nonempty body — exactly the owner's caveat. **The 27 results are an inventory fact, not a loss claim.**

### Positive result — the first boundary where content differs is the **persistence target at the record stop**

Run: `scripts/hitl/legacy_record_baseline.py --track 6 --loop-slot 2 --record-only --record-bars 2
--record-seconds 6` (`captures/s13-survival-01/`, report `host_midi_automation_baseline_20260924_225250.json`).
`persistKeyForSlot(trackIndex, slotIndex)` labels itself `slot:<trackIndex>:<slotIndex>` **zero-based**
(`formatPersistKeyLabel`, `PersistenceWorkItemPersist.cpp`), so `slot:5:0` = track 6, **slot 1** and
`track:5` = track 6. Read it that way throughout this section.

- **Target confirmed on the device:** `Button press: Track 6 (short)` → `Selected track 6`;
  `Button press: Loop 2 (short)` → `Loop 2: Start Recording`; `Recording started @ tick 192`; the stop
  reports **`Record pass undo reset for slot 1`** and `Recording stopped @ tick 564 (recStart=192
  length=768)`. So the take ran on **track 6, slot 2** (zero-based `(5,1)`) with a real 768-tick recorded
  pass.
- **Oracle 2 (before reboot) FAILS at the save, and the target is off by one slot.** The record stop's
  persistence telemetry admits **`slot:5:0`** (= track 6 **slot 1**) and `TrackMeta,track:5`, and
  **`LoopPersist` never appears at all in the run** (0 occurrences; the only `PERS` work is a startup
  `FinalizeWorkspace` plus that one `track:5`/`slot:5:0` metadata pair). So the recorded interval's payload
  file (`/MidiLooper/current/slots/loop_05_01.bin`) was **never written**, and the save that did run
  targeted the **adjacent slot**. `persistence_loop_persist_not_sliced` and
  `persistence_stage_missing:request` are the harness's own names for the missing stages.
- **The boundary is the target resolution, not the writer.** `writeLegacyLoopPersisted` /
  `writeCapturePassSlotFileHeader` do serialize the pass headers **and** the chunk event payload from the
  pool, and `probeLoopSlotPayloadOnSdFromSd` (`SD.exists` + token-at-EOF) plus
  `readCapturePassSlotFileHeader` read it back — the write→probe→read chain is intact for the slots that do
  get persisted. The recorded interval simply never entered it.
- **Oracle 3 (after reboot) is consequently moot for this attempt** and is not claimed: with no payload
  written for the pinned slot, a reboot cannot restore it. The `#MEM_OWNERS` settled census after the
  step-0 sweep reboot was already byte-identical to before (78 / 2,455 KB / 2,155 KB / `chunks_free 450`),
  consistent with the slot contributing nothing.

**Why the earlier "payload did not survive the reboot" reading was wrong.** The sweep's first step reported
`Track 6 loaded state: EMPTY` and `N=36`, and that was read as a saved payload failing to survive. The
persistence telemetry in the same run (`captures/s13-slope-01/step00_slot1/record.log`) shows the save wrote
**`slot:5:0`**, not the recorded slot, and the sweep's own record log shows the take began as
`Overdubbing started` rather than `Recording started`. **The fixture never wrote the slot it claimed to
grow.** No firmware content-loss is established; a **persistence-target defect in the record-stop path** is
the first candidate, and it is consistent across both runs.

**Open, recorded not solved.** Why the record-stop persistence resolves to `track 5 slot 1` when the take
ran on track 6 slot 2 is unresolved here. The owner-facing selection is right (`Selected track 6`, `Loop 2:
Start Recording`), so the candidates are the record-stop slot/track resolution
(`TrackCaptureStopCommit.cpp` → `resolveTrackIndexForPersistence(*this)` → `markLoopSlotMaterialDirty` /
`admitLoopSlotPersist`, with `persistSlotIndex = slot`) or the interval's slot identity at that point. This
is the next falsifiable boundary and it is **one approved write away** from being pinned: a run in which
the admitted persist key is asserted to equal the recorded slot before the reboot, aborting if not.

### Claim corrections applied with this record

1. **Zero `LoopPersist` lines does not prove no payload was written** — the owner is right, and the
   coverage gap is now enumerated. `PERS,work` is emitted by **one** of **five** paths that can write a
   current-set slot payload:
   | path | writes the slot file | emits `PERS,work` |
   |---|---|---|
   | deferred persistence worker (`stepLoopPersistWorkItem`) | yes | **yes** (`LoopPersist`) |
   | revision load (`RevisionLoad.cpp`, `openRevisionLoadLoopSlotTemp` → finalize) | yes | no |
   | revision commit (`RevisionCommit.cpp`) | yes | no |
   | boot recovery / hygiene (`BootRecovery.cpp`) | removes temps only | no |
   | saved-set copy (`SavedSetIo.cpp`, `copyFileBinary` → atomic rename) | yes | no |
   So the demonstrated fact stays **wrong-target admission**, and the payload write must be verified
   separately (file size and a boot that reads the events back). `scripts/hitl/record_stop_persist_target.py`
   states this non-claim in its own output rather than staying silent.1. **"27 lost recordings" is withdrawn** (see the negative result): a valid token and a `committed`
   terminal do not establish a nonempty body, and the boot logs report no payload read failure. Likewise,
   an unchanged payload count cannot detect replacing an existing slot — `(5,0)` loaded with the same
   `passes=1 / 768 ticks / 2 events` before and after the sweep's overdub, so those fields are not a
   content-identity signal either.
2. **S2b.3 — post-restore budget stays conditionally checked, and the substitution is withdrawn.** The
   `capture_prewarm` walk's `credits` on a settled boot is the **cold** reservation count, not peak
   capture reservation, so `450 ≥ peak + 16` was never actually evaluated: at the moment that walk runs the
   workload that would peak the reservation has not been driven. **S2b.3 gets no checkoff from a boot
   marker.** Its term is the peak capture reservation **during the specified capture workload** — the
   maximum reservation while the fixture records/overdubs — against `+16`, on a verified growing fixture.
   Neither the workload peak nor the verified fixture exists yet, so the task stays open with its full
   arithmetic unexercised.

### Payload oracle, first attempt (2026-09-24, eighteenth session, third round)

Bounded to the chain `take -> stored payload -> restored content`. The read side is now in place; the
**write side is blocked on the fixture's arm sequence**, which is a harness problem, not a firmware one.

**Landed (read side, all read-only).**

- `!SLOT_PAYLOAD <trackIndex> <slotIndex>` — stopped-only, read-only digest of one slot's **stored**
  payload (`src/StorageManager/SlotPayloadProbe.cpp`): path, file size, epoch wrapping, format, per-pass
  `id/type/sealedAt/events`, and an FNV-1a digest over the exact event bytes. It streams the file, never
  writes, publishes, adopts or touches the pool, and the wire format it decodes is the writer's own
  (`writeLegacyLoopPersisted` + `writeCapturePassSlotFileHeader`).
- **Format-aware, because the first attempt mis-parsed a real file**: an LSV1 body is a sectioned snapshot
  container, and decoding it as a legacy header produced `passes=393216, events=16777216` on a 3,524-byte
  file. The probe now runs `probeLoopSnapshotBodyFormat` first and answers
  `SLOT_PAYLOAD,not_decodable,...,reason=non_legacy_body_use_boot_content_dump` for a non-legacy body
  instead of inventing numbers. **A legacy payload is therefore required for this oracle** (or the boot
  content dump for an LSV1 slot).
- The single-session fixture gained channel-readiness retry (measured: silent at +2 s after a flash,
  answering by +5 s) and reads the stored payload on the same connection before the close.

**Blocked: the fixture cannot arm a fresh take on the pinned slot with a single record press.**
Ordered device evidence (`captures/s13-payload-04/session.log`):

| time | device |
|---|---|
| 6.365 s | already in `LOOP_EDIT` at boot (so a `Loop N` press refreshes it, it does not cause it) |
| 9.001 s | `Global Transport` → `Transport started` |
| 10.308 s | `Loop 2` short press |
| 11.604 s | `Global Transport` → `Transport stopped` |
| 13.206 s | `Record/Overdub (short)` → **`Toggle Play/Stop`** |
| 18.629 s | `Record/Overdub (short)` → **`Live Overdub`**, `Overdub session opened: events=0` |

So a short `Record/Overdub` press is **context-dependent** on this state and neither outcome arms a fresh
take; the take never reaches `Recording started`, and the fixture now refuses to stream notes or read a
payload in that case (it exits with `take_not_armed` rather than producing an unarmed "payload result").
The record harness succeeds because `_run_record_arm_until_capturing` drives an **arm loop** with serial
confirmation, not one press; the next bounded step is to adopt that loop in the single-session fixture.
**No reboot, no clear, and no payload claim is made from the unarmed runs.**

### Pinned fixture parameters (S1.3 slope, S1.1 owner census, S2b.3 post-restore budget)

### Recorded-slot → persistence-target correctness, instrumented (2026-09-24, eighteenth session, second round)

Bounded, as instructed, to **recorded-slot → persistence-target correctness**: no new sub-change, no wider
sweep, and **no clearing**.

**What landed (the correlation the owner asked for).**

- **`RID` record family** (`DebugSessionCapture::recSlotIdentity`, capture-gated, at most two bounded lines
  per take): one at record start and one at the persistence admission, carrying the same take id and the
  whole chain — `t` track, `ss` start slot, `sp` stop slot, `cs` the slot passed to
  `finalizeCommitSideEffects`, `pt`/`ps` the admitted persistence key, `r` reason, `w` work kind, `chg`
  whether the target moved. It is classified **critical** in the capture tier (`CaptureLineTier`) because a
  Tier-A ring displaced it on the run that admitted `slot:5:0`; at most two lines per take, so it cannot
  flood. Contract pinned in `test_capture_line_tier`.
- **One production resolver** — `resolveRecordStopPersistTarget(track, recordedSlotIndex)` — is now the
  single expression both record-stop persist sites and the native regression use, so the target cannot be
  resolved one way in production and asserted another way in a test. The recorded slot is passed in and
  never re-read from `activeLoopIndex`.
- **Host gate** — `scripts/hitl/record_stop_persist_target.py` (pure correlator, 10/10 unit tests) plus
  `scripts/hitl_record_stop_target.py` (the fixture driver). On a mismatch the driver **withholds the
  reboot** (`reboot_allowed: false`, exit 2) and prints the divergent boundary: a fixture that wrote a
  different slot than it claims to grow cannot produce evidence about payload survival.
- **Native regression** — `test_record_stop_persist_target_follows_the_recorded_slot` in
  `test_continuous_capture`: records into a non-zero slot while the **adjacent slot already holds a
  committed pass**, asserts the take identity names the recorded slot, and asserts the production resolver
  returns the recorded slot on the owning track. The full `Track::stopRecording` route is an off-path
  boundary in that suite by design, so the transition itself stays a device check and the resolution is
  locked natively.
- **Native regression covering the transition itself** is **not** possible in the suites that stub
  `Track::stopRecording` (`off_path_deps.h`, the A34A suite): adding the real record-stop path there would
  un-stub the very boundary those fixtures assert on. Recorded as a gap, not hidden.

**What the device showed.** `captures/s13-target-02/` (gated run, no reboot): the deferred worker admitted
**`LoopPersist slot:5:0`** — track 6, **slot 1**, the adjacent slot — with 7 payload slices, while the take
recorded on track 6 loop 2. On the earlier `captures/s13-survival-01/` run the same key appeared with the
take having logged `Record pass undo reset for slot 1`. The wrong-target admission is therefore
**demonstrated and reproducible**, and it is what the fixture gate now refuses to reboot past.

**Open blocker (recorded, not worked around).** The `RID` rows do not reach the host, and the bounded
counters now localise **where** that happens rather than assuming it.

**Counters added (capture-gated, fixed size, no blocking output in the record/persist path).** The emitter
counts its own entry and its enqueue outcome (`entered` / `enq` / `rej`, plus the last take's identity);
the ring counts identity evictions and identity transmissions (`evict` / `tx`). They are read back through a
**read-only, stopped-only** command, `!CAP_IDENTITY_STATS`, which emits its one row **directly** rather than
through the capture ring — because the ring is the thing under suspicion and a host-requested, stopped-only
read-back has no reason to be buffered. That single design point was itself informative: when the row went
through the ring it never arrived.

**RESOLVED — the single-session route recovered the evidence (2026-09-24, same day).**
`scripts/hitl_identity_one_session.py` holds **one** serial connection with one reader for the whole run:
baseline counters → take → transport stop + stopped confirmation → second request, all before the close.
It answers the question the counters existed for, and it does so **without** a retained-counter design
(which is therefore not justified) and without touching capture buffering:

| reading | value |
|---|---|
| baseline (no take), same connection | `entered=0 enq=0 rej=0 tx=0 evict=0` |
| after the take, same connection | `entered=2 enq=2 rej=0 tx=1 evict=0` |
| last identity (from the stats row) | `take=1 t=5 ss=1 sp=1 cs=1 pt=5 ps=1 ev=a` |

- **The emitter does execute.** `entered=2` for one take = record start + persistence admission.
- **Nothing is gated, disabled or rejected**: `rej=0` and neither row was dropped at the enqueue
  (`enq=2`), so capture was enabled and admission open.
- **One of the two rows is transmitted, one is not** (`enq=2`, `tx=1`). The stats row is the one that
  arrives; the `start` row is enqueued and then never drained. That is the capture-visibility defect,
  now localised to the ring's drain rather than to emission, gating or rejection — and it **no longer
  blocks the identity question**, because the stats read-back carries the identity.

**The chain correlates, so the "wrong target" attribution is withdrawn for this take.** The take
recorded into `t=5, ss=1`; the stop passed `cs=1`; the admitted persistence key was `pt=5, ps=1`; and the
counters' last identity reports the same slot. **The persist target followed the recorded slot
exactly** — there is no off-by-one in the take's own identity chain. Combined with the single-session
log showing **no `LoopPersist` work item at all** for the take (its only `PERS` rows are
`GlobalMeta`/`SlotMeta`/`TrackMeta`), the earlier `LoopPersist slot:5:0` observation is
**unattributed pending work from an earlier deferred save, not this take's admission** — exactly the
alternative the owner flagged. Also note `slot` is **zero-based** in these records while the harness
names the slot 1-based, so `ps=1` is the *same* slot the pinned, user-facing "loop 2" targets; the
mismatch was in my labelling, not in the firmware.

**What remains open from this chain:** the pinned oracle also wants the *payload path* and the payload
write verified before a reboot is evidence. The take's `LoopPersist` did not run in the single-session
run (its `PERS` coverage is worker-only, one of five write paths), so the payload file for `(5,1)` is
still unverified, and the reboot stays withheld until that is established.

**Historical note on the route that failed.** Before the single-session fixture, the read-back through
the record harness could not be taken: the command channel stops answering once a capture session closes
(`!CAP_IDENTITY_STATS`, `!STOP_STATUS`, `!CRASH_LOG` all silent) while telemetry keeps streaming, and
only a re-upload recovers it — which clears the counters. `!CRASH_LOG` after the next upload shows a
normal boot and no crash record, so that was a stalled command path, not a crash. **Keeping the
connection open for the whole run sidesteps it entirely**, which is why the single-session fixture is the
supported route and no retained-counter design is needed.

**Superseded wording.** On a **fresh boot with no take**, the read-back works and
reads `entered=0 enq=0 rej=0 tx=0 evict=0` — so the counters and the command are both live, and any
non-zero value after a take would attribute the loss. That before-reading is now the fixture's baseline.
The **after-reading is blocked by a device-state defect, not by the counters**: after a capture session
closes, the command channel stops answering (`!CAP_IDENTITY_STATS`, `!STOP_STATUS` and `!CRASH_LOG` all go
silent) while telemetry keeps streaming, and it only recovers on re-upload — which clears the counters.
The device is **not** crashed: `!CRASH_LOG` after the next upload shows a normal boot and no crash record,
so this is a stalled command path, reproducible across three runs. **Ordinarily this read-back is exactly
the kind of measurement that must not require a reboot, and here a reboot is what destroys it** — so the
identity question stays open until the command path survives a run (or the counters are reported at the
next boot).

**Status, stated precisely (per owner direction).**

- **Native/build gates are green; device identity verification remains open.** Full native 2,379 /
  1 skipped / 0 failures; `teensy41-capture-serial` SUCCESS with RAM1 `variables 96,160 (was 96,192) /
  ITCM 393,216 (code 391,304 + padding 1,912) / locals 34,912 (was 34,880)`. The RAM1 baseline was
  re-based deliberately (`scripts/ram1_budget.json`) because the change **returns 32 B of DTCM to the
  local allowance** — the mutable counters and last-take identity live with the capture-ring
  state in DTCM (`sCaptureRing`) while only the counter-handling functions (`recSlotIdentity`,
  `emitIdentityPathStats`) are FLASH-placed, and the take identity is packed into one 32-bit word in the
  `TrackCaptureInput` TU, so `Track`'s pinned `sizeof(384)` is untouched.
- **Adjacent-slot persistence work is suspicious, not attributed.** `LoopPersist slot:5:0` (track 6,
  slot 1) is admitted while the take records on track 6 loop 2, reproducibly. But without take correlation
  it **could be unrelated pending work** — the device autosaves across runs, and every run shows
  `GlobalMeta`/`SlotMeta`/`TrackMeta` items that outlive the take. So this is a lead to attribute, not a
  proven wrong-target admission for *this* recording.
- **Zero payload-read errors supports "no reported read failures".** It does not establish that every empty
  payload was intentionally empty: a body could be written with metadata and no events, or written by a
  path that never emits a read failure. The 27 content-less slots are an inventory fact, not a loss and
  not proof of intent.
These are the parameters the measurements must hold fixed. They are **pinned here before any sweep**; a run
that departs from them is not the pinned measurement.

1. **Authorized slots to empty/populate.** Track **5 slot 1** and track **6 slots 1–8** only
   (`docs/Runtime/CURRENT_WORK.md` scratch authorization). Each added slot is *selected* (and left
   *enabled*) so boot's own playback set and the background admission scan load it. Nothing outside that
   range is written, cleared or renamed; no `CLEAR` is issued for this measurement.
2. **Fixed enabled/selected roles.** Transport stopped, no record/overdub/edit during the measurement boot,
   one active role at a time (the S1.3 definition's "fix the active role"). The active role is held
   constant across the population steps, so residency growth is attributable to the inactive loaded
   population and not to active-role scaling.
3. **Equal content per added slot.** Every added slot is written by the **same recipe** (same bar count and
   note shape), so the slope is per-added-slot and not per-content-size. The recorded recipe and its
   measured payload bytes are part of each step's evidence.
4. **One payload format per measurement.** The slope is measured on **legacy** payloads (a recorded slot
   saved and re-adopted, `passes` populated) *or* on **LSV1** payloads (a published root, `passes` empty),
   never mixed in one series. The two formats are reported as separate series because the corrected
   observation now distinguishes them; a mixed series cannot attribute residency to either.
5. **Separate cost classes.** Canonical **reservation** (the `capture_prewarm` credits walk and the
   `free_chunks` checkpoint), canonical **payload** (chunk ids held by committed passes), **pool chunks**
   (`#MEM_OWNERS` `chunks_free`/`slab_*`), **snapshot roots** (the snapshot arena) and the **runtime
   directory** (per-slot directory state) are reported **separately** at every step. `credits=0` on a
   restored slot is expected (S2b.1) and is not evidence about growth.
6. **Failure point = first refusal** (`#MEM_EXT_REFUSED`, a `refused` restore terminal, or `free_chunks`
   at/below the reserve) **within the authorized population**. If the sweep reaches the end of the
   authorized range with no refusal, that is **not** a capacity finding: report the measured range and
   leave **S1.5 — refusal point** open.


## S1.3 — takeover: restore a discriminating single-session identity fixture (2026-09-25)

Scope: host fixture repair, removing the unfinished payload probe from the firmware command path.
Preserved the incoming draft and diff in `/tmp/runtime-takeover-20260925/`. The draft had removed
note generation, record stop and transport stop, stopped selecting the requested slot, and accepted
`Loop ` as recording evidence. It could not establish a take or payload survival. Its synchronous
legacy-only SD decoder had no load/persistence admission and rejected LSV1; it is not an acceptable
payload oracle. No persistence or capture behavior change is part of this repair.

Execution packet: reuse the existing one-connection Session and RID identity fields. Require a fresh
RID start for the requested track/slot before sending notes, then record-stop confirmation, global
transport-stop confirmation, and a fresh after-take admission identity matching every boundary.
A queued but untransmitted RID is reported as outstanding at the sample, never inferred lost.
Missing/ambiguous evidence aborts with nonzero status; reboot remains withheld even on identity PASS,
because stored event parity is a separate, still-unimplemented oracle. Explicit serial selection must
never silently choose another device. No CLEAR, reboot, payload probe, or new firmware instrumentation.

Tests: fake serial/MIDI adapters execute the production host orchestration. Cases cover normal order,
false Loop/overdub markers, stale/wrong-slot start, absent stop, absent or mismatched admission, and
configured note channel. Baseline regression: incoming draft never streams/stops and its generic Loop
marker accepts non-recording UI. Host parser and identity tests plus Python compilation are required.
Device result: NOT RUN until fixture repair passes; no firmware gate is claimed from host tests.
Decision review: existing HITL Session/RID owners reused; no new runtime owner, timing policy, SD format
or architecture decision. Relevant standing contracts are capture-gated observation and bounded SD
work; no speculative persistence fix or allocator change.

### Takeover result (supersedes the preceding uncommitted payload-probe draft)

The draft probe was withdrawn, including its serial entry point: it rejected LSV1 and bypassed
storage scheduling. Historical draft claims that its read side "landed" are not current status.
Backup: `/tmp/runtime-takeover-20260925/`. Firmware files now match HEAD.
The host fixture restores note generation and stop actions, requires fresh RID start identity,
checks every admission boundary, rejects missing baseline evidence before controls, and returns
exit 2 for inconclusive evidence. A stopped-only stats response establishes global stopped state;
an arbitrary Track transition does not. Pending ring output is not classified as loss.
Seven host tests exercise production orchestration and note streaming; no hardware run was made.
Stored event parity and survival remain open. Do not infer payload success from file existence/size.

## S1.3 — empty-target, stopped-session continuation (2026-09-25)

Execution packet: extend the existing single-session diagnostic, host-only. The oracle is a
complete 64-slot observation, settled restore requests, verified/absent payload for the target,
and zero target committed content/length/events before a recording action. Existing metadata-only
payloads may become nonempty: count the loaded union, not a change in payload-file count.
Reuse the registered arm sequence (Record -> ARMED -> transport start), protocol transition parser,
and existing CAPTURE_CONTENT command. No missing-clock transport retry, CLEAR, firmware change,
reboot, or direct SD probe. An explicit MIDI Stop precedes baseline only when requested.
Tests cover an armed start and reject populated, pending, invalidated, and incomplete targets.
On-device scope: at most one take in empty user track 6 slot 3 if the preflight passes;
track 6 slot 2 is already populated. Reboot remains withheld until stored-event parity is proved.

Read-only preflight plus explicit MIDI Stop: `captures/s13-takeover-preflight/`,
`captures/s13-takeover-observation/`, `captures/s13-takeover-stopped/`,
`captures/s13-takeover-settled/`. The ungated library request was acknowledged before Stop;
after Stop, identity stats and library frames answered. This does not establish a dead command
channel or a close/reopen fault. Settled observation: payload=36, union=9, pass=9, root=8;
user track 6 slots 3-8 have verified payloads but zero committed content, length and events.
`consistent_view=false` is expected for multi-turn collection (`processLibraryObservation` sets
it after turn 1); no invalidated rows or pending requests remain in the settled observation.
Firmware identity of these attaches is not independently attested; no boot result is claimed.

### Continuation outcome — device refusal; no slope checkoff

Host tests: `PYTHONPATH=scripts .venv/bin/python -m unittest
scripts/test_hitl_identity_one_session.py scripts/test_record_stop_persist_target.py` — **23/23**.
OpenSpec validates; `git diff --check` passes. No firmware source changed, built, or flashed.
The temporary stimulus descends from STIM-BASE-2-FIXTURE-01 but is an identity-only diagnostic:
host notes 48-59, six-clock spacing, two bars, no clear/overdub/undo. It is not a canonical baseline pass.

Device attempts (all on the same boot, no reset):
- `s13-takeover-take-01`: user track 6 slot 3 reached Recording; RID stayed buffered and the
  host sent no stimulus notes. The first driver did not stop on failure. The next run's explicit
  MIDI Stop stopped this empty take (`length=0`). This lapse is fixed with failure-stop handling.
- `-02`: track 6 slot 4 armed but never reached Recording; explicit failure Stop confirmed by
  stopped-only identity stats. No notes. `-03`: missing-clock arm handling exposed a clock/button
  toggle race; no Recording and no notes. `-04`: selection had already armed the slot; a redundant
  Record press cancelled it. The fixture now reuses that arm. No notes.
- `-05`: track 6 slot 4 passed empty-target preflight and armed; record admission refused:
  `#MEM_EXT_REFUSED ... bytes=4096 ... live=8389KB ... heap_free=4KB ... tag=13`;
  `companion_history_reserve/pool_refusing`, then `record_start/prewarm_failed`.
  See `captures/s13-takeover-take-05/refusal-analysis.json` and `session.log`.
  No note stream began; failure MIDI Stop followed by stopped-only stats confirms stopped state.
  Final counters remain take=1, entered=1, enq=1, tx=1 (the earlier empty slot-3 take only).

The final driver recognizes the explicit record-start refusal instead of reporting only the timeout.
It supports explicit MIDI Start/Stop, supplies clocks during armed-start and stop waits, and requires
final admission identity to match exactly baseline take+1 and all pinned slot boundaries. A fresh
human recording-start line is provisional when RID is buffered; final direct readback is mandatory.
This adjusts the previous pre-stream RID requirement based on observed buffering, not the final oracle.

**S1.3 stays open, measured added-population range=0.** This is an actual admission/capacity refusal
in the present warmed runtime, not a controlled increasing-N failure point. Do not check S1.5 off.
The initial RAM census changed during fixture attempts; do not fit a slope from these runs.
The installed firmware hash is not independently attested, so these are diagnostic attaches, not a
new final-firmware gate. No CLEAR, SD deletion/rename, or reboot was performed.

Next bounded work: reproduce/attribute prewarm residency across arm/cancel in native tests before
another hardware growth attempt. Existing owner is Track playback/capture prewarm; do not introduce
an allocator workaround or start S3 implicitly. Separately, `!CAPTURE_CONTENT` reads **RAM** passes
(Track::printStoppedCaptureContentForHitl), not an SD payload. Pre-reboot stored-event parity still
requires a bounded verification path reusing StorageManager's load readers for both legacy and LSV1;
no such command is currently exposed. A token-only observation or RAM dump cannot substitute.

Read-only owner trace after refusal: `Track::startRecording` denies before resetting passes when
`prewarmPlaybackForSlot(activeLoopIndex, true)` fails. The latter obtains the slot runtime and
calls `prewarmViews`, canonical-output reservation and later preparation while accumulating `ok`;
this is not an atomic all-or-nothing allocator transaction. Retained per-slot derived capacity is
within the main change's ownership problem. The existing native `test_record_arm_admission_is_repeatable`
asserts three successful grants on **one slot**, not byte stability or cross-slot cancellation.
Its former claim that it "exonerates" arm residency was removed, along with the stale test comment
claiming device-proven wrong-target persistence. No test behavior changed by those corrections.

## S1.3 — native failed-admission/cancel retention packet (2026-09-25)

Scope: characterization in the existing continuous-capture harness, no firmware change.
Use real Track::startRecording and TrackManager::cancelPendingRecordArm; force the existing
MemoryMonitor reserve-denial boundary, then observe prewarmed view/staging capacity and canonical
credits before/after cancel. Compare repeat attempts on one slot with attempts on distinct slots.
The denial injection identifies partial-admission ownership, not the device allocation byte limit.
No new lease policy, cleanup behavior, or hardware slope checkoff is authorized by this measurement.
Required evidence: successful control already covered by the admission test; new fixture must show
no admitted capture or populated content, real cancelled arm, and report retained owner capacities.

### Native retention result

`test_s13_failed_arm_cancel_retains_distinct_slot_capacity` passes through the real
`TrackManager::startRecordingTrack` stopped-clock arm, `Track::startRecording` with the existing
forced reserve denial, and `TrackManager::cancelPendingRecordArm`. It verifies no capture or
committed content was admitted and the pending arm clears. After cancel, each touched slot retains
**16,384 fixed event-capacity units and 8 canonical chunk credits**. Three distinct slots retain
16,384 -> 32,768 -> 49,152 event-capacity units. A second attempt on the same slot preserves the
measured capacity and live-staging allocation address. These counts cover view, bridge-base,
staging and prepared-pair event capacities; they are neither total resident bytes nor device bytes.
The canonical credits are additional ownership and must not be summed as event capacity.

Harness correction: `ClockManager::shouldQuantizeRecordStart` was hardcoded true even when
`nativeTransportRunning=false`. It now returns `isTransportRunning()`, matching production's
clock-running predicate. The old stub drove the new test down the running-clock reclaim path
instead of the stopped arm path; the fail-closed boundary exposed this before any false result.

Validation: focused **99/99**; full native **2,379 passed / 1 skipped / 0 failed**; host **23/23**;
OpenSpec and diff checks pass. Verbose repeat collected the otherwise hidden capacity output.
Evidence: `captures/s13-native-cancel-retention/{measurement.json,native-focused.log,native-all.log}`.
Firmware/build/upload NOT RUN (test-only changes); board untouched this slice.

Conclusion: rejected admission plus cancelled arm can leave per-slot working capacity resident.
The measured buffers do not grow on the second same-slot attempt; this is not proof that every
allocation is stable or that it alone explains the device's 4,096-byte refusal. Production cancellation
clears flags/state/pre-roll and does not release these runtime owners. The full ownership solution is
already scoped by S3.2 (liveness predicates) and S4.1 (guarded release); no blanket free-on-cancel was
added. S1.3 and S1.5 remain open. Future device measurement must begin from a controlled working-set
baseline, not the accumulated arm-attempt state, and still needs the pre-reboot SD-content oracle.

## S1.3 — bounded SD byte readback execution packet (2026-09-25)

G0 VERIFIED: CAPTURE_CONTENT is RAM-only. LIBOBS verifies only the file token. LoadLoopJob
supports both body formats but its error paths reset live slots/retry and its success publishes;
it is not a read-only inspection API. CurrentWorkspaceStorage supplies the outer-header reader,
and StorageLoopIo supplies legacy/LSV1 host-compatible parsers. LibraryObservationSession already
owns SD observation admission. DEC-083 bounds observation; DEC-073 governs root currency.
G0 PASS for an observation extension, no ownership transfer or format change.

G1: Stateless 64-byte page output, existing StorageManager owner/admission, SESSION_CAPTURE gate.
One open/seek/read/close per request plus bounded token/header inspection. No file handle or parsed
snapshot survives a board command. No per-slot/global RAM1 state, no growing PSRAM allocation.
Stopped only; reject active/queued persistence, load session/job or capture. Explicit slot indices,
fixed path formatter; no arbitrary paths. Response carries size, body offset, set epoch and RAM
publication fingerprints. Host rejects changing identity/size/offset, malformed/short pages and
non-token files. Two identical full reads are required, then native production parsers decode the
payload; compare event rows against the stopped RAM oracle, not merely byte count or token.

G2 adversarial review: busy SD owner -> refuse, never wait; invalid slot/offset -> refuse before
read; no publication/retry/reset on missing/corrupt file; stopped capture/overdub -> refuse; serial
backpressure -> do not read until sufficient TX capacity; partial response -> host fails closed;
mid-read save/change -> busy or fingerprint mismatch, plus repeated full read equality. This is a
controlled idle diagnostic, not an atomic file snapshot under concurrent external card modification.
No transport hot-path work, no whole-file board parse, no new lease/loader. Native page tests and
production-decoder format tests precede full native/build gates. Device upload only for dedicated
HITL evaluation; no clear/delete/rename. Runtime stage gates remain unchanged.

## S1.3 — controlled one-slot survival closeout (2026-09-25)

**2026-09-25 — S1.3 controlled one-slot survival PASS (uncommitted branch work).**
Track 6 slot 3 / `(5,2)` was verified empty, then received one two-bar take. Take identity
matched start/stop/commit/admitted target. Two full SD reads were identical; the production
legacy parser with an explicit Teensy event-layout adapter decoded **62 events / 1,536 ticks**,
exactly equal to stopped RAM. Only then was the unchanged firmware re-uploaded for the survival
boot. Restored RAM and two new SD reads match all events and the original file hash. Loaded
union **9 -> 10** (passes 9 -> 10, roots 8 -> 8), payload inventory stays 36 because an existing
empty payload gained content. Settled free chunks **450 -> 449**; after-boot attested checkpoint
449. No clear/delete/rename, no non-scratch recording. Evidence:
`captures/s13-controlled-01/` and `captures/s13-controlled-survival-01/`.
This closes the one-slot fixture blocker, not S1.3's multi-point slope or S1.5's refusal point.
Next: define fixed-focus/active-role cohorts within the remaining approved scratch slots, compare
complete content sets (detect eviction), and measure through the first classified failure. The
selected/initial-playback slot changed in this fixture, so its one-chunk difference is descriptive,
not an isolated per-slot slope. S2b.3 still needs workload peak reservation; S3 remains gated.


Implementation: `readSlotPayloadPageForHitl` provides stateless, stopped/admission-gated
64-byte reads. `!SLOT_BYTES` waits for sufficient TX room before opening SD and returns directly;
it never publishes/adopts a slot. Host performs two identical reads, checks the envelope/token,
and compares decoded events with the stopped RAM dump. Firmware SHA256 for both uploads:
`b289af534577e25b57c52146b27f66c245c97f43a5028fc10a37b9a8a9bbd208`.
File SHA256 before/after: `1b943c589051ff4b89ff56d4045fe61f460611dbb54bf2aad09a411dbc3955c2`.
Firmware: RAM1 variables 96,160 / code 391,304 / padding 1,912 / locals 34,912; no rebase.

The first host verdict was inconclusive; read-only reanalysis corrected two verifier assumptions:
1. Epoch file CRC uses `PersistenceSchema::crc32Continue` in 256-byte grains, including SAVE.
   Its complement convention differs from a single zlib whole-body CRC. The stored CRC matches
   the existing firmware exactly; no checksum or file-format firmware change was made.
2. Legacy event bytes embed the Teensy ABI (20 bytes). Native `MidiEvent` is larger because its
   SysEx union includes a host pointer. The host IO adapter converts explicit target fields before
   passing event batches to the production legacy parser. Target SysEx/LSV1 fail closed. Existing
   native LSV1 tests exercise native-format bytes, not a target LSV1 file. The earlier execution
   packet's both-format host assumption is narrowed; extending target LSV1 is future observer work.
The target legacy test is a literal 20-byte event fixture plus production tails; truncated input
is rejected. CRC test uses an independent bitwise continuation. Firmware was not changed or
reflashed to repair either host assumption. Original failed verdict retained alongside reanalysis.

The host sent 32 note pairs; this slice compares the 62 committed rows, not intended-stimulus
parity. It does not certify playback, note-boundary completeness, undo, peak capture reservation,
or a 64-bar workload. No S1/S2b gate is checked from that narrower result. The after-boot observation
precedes track selection; payload contents and loaded union are therefore restore evidence.
Scope stops at the authorized one-slot fixture. No new architectural owner or DEC is introduced:
this extends DEC-083 observation and retains existing runtime ownership/stage gates.

Validation: full native **2,383 passed / 1 skipped / 0 failed** (2,384 total); target decoder
focused **50/50**; host **29/29**. Capture-serial build/upload and OpenSpec validate pass;
diff check clean. Population set comparison adds only `(5,2)`, removes no previously loaded slot.
Evidence source copies/hash manifest include untracked host files, not just git diff. Work remains
uncommitted on the feature branch; this is branch verification, not a dev release.

## S1.3 — fixed-role growth execution packet

**2026-09-25 — S1.3 fixed-role growth sweep (authorized continuation).**
Reuse the verified two-bar single-session stimulus and SD parity gate. Add one verified-empty
track-6 scratch slot per cold boot, starting at user slot 4; never overwrite populated content.
Return selected track/slot to track 6 slot 3 before reboot and wait for persistence admission
through the existing SD-page oracle. At the next boot require identical transport footer roles,
all post-restore readiness fields, and complete LIBOBS; compare the full loaded set and retained
slot content signatures, not payload inventory. Verify the new payload/RAM after reboot against
its pre-reboot bytes/events. Record owner telemetry at the same settled pre-interaction checkpoint.
Stop at the first named resource refusal, fixture failure, eviction, or authorized-range exhaustion;
do not relabel a host/fixture failure as a capacity boundary. No firmware changes or clearing.
G2 oracle: N -> N+1, only the target added, zero losses or existing content-field changes, roles
unchanged. Reboot gate: stored events match RAM and return-to-focus storage admission succeeds.
Evidence is one directory per attempt with upload identity, command logs, source hashes and raw
observations. This measures the two-bar workload, not the 64-bar capacity envelope or peak capture
reservation. S1.3 stays open if the first genuine resource boundary cannot be established.

### S1.3 host interoperability correction during fixed-role sweep

The slot-4 take chose LSV1, so the earlier legacy-only host limitation was reached. No new
firmware was added. The host adapter now converts the legacy-oracle event arrays and root-event
records from the 20-byte Teensy ABI into host representations, preserving metadata and membership
sections, then runs the production LSV1 parser and resolver. A captured synthetic target-format
body is a golden fixture; 62 events / 1,536 ticks match RAM. Pointer-bearing SysEx remains rejected.
The earlier closeout's target-LSV1 rejection is superseded by this extension. The adapter uses the
serialized note-field stride (16 bytes), not the production estimate constant (20 bytes); no firmware
size-estimate or on-card format change was made. Full native 2,384 passed / 1 skipped / 0 failures;
focused decoder suite 51/51.

The initial whole-file-hash survival condition was stronger than content survival and rejected a
valid restore: slot 4's epoch, nextPassId and persisted geometry changed after normal focus/save
activity, while its LoopSnapshot section remained byte-identical. Both snapshots independently
passed repeated full read/CRC/SD-to-RAM equality and all 62 events and length matched across reboot.
The host now requires exact event multiset and length equality, explicitly reports both hashes and
file_bytes_equal, and never treats this as byte invariance or absence of writes. A discriminating
host test rejects changed length, missing events and duplicate events despite permitting metadata
hash changes. No comparison oracle was dropped: raw-file equality remains separately reported.

Setup attempts slot4-01 (GUI Auto-mode failure), slot4-02 (bundled CLI cannot parse target HEX), and
slot4-03 (missing required host serial argument) sent no recording stimulus. The GUI loader was
restarted; the normal PlatformIO post-compile uploader then re-uploaded the exact pinned image.
Slot4-04 recorded only once; its stopped evidence was reanalysed offline, fixed focus restored,
and survival checked without re-recording. Preserve all original verdicts alongside corrections.

## S1.3 — fixed-role sweep outcome and remaining gate

**2026-09-25 — S1.3 fixed-role growth MEASURED, range 10–15 loaded slots.**
Five two-bar takes into user track 6 slots 4–8 passed exact SD/RAM event-and-length parity
before and after reboot (62 rows / 1,536 ticks each). All ten boot checkpoints used the same
stopped transport and boot playback footer; focus was restored to track 6 slot 3. Each loaded-set
comparison added only its intended target and lost none; existing observed content fields stayed
unchanged. Four repeated intermediate baselines reproduced every reported owner total exactly.
Payload inventory stayed **36**; content union/pass count **10 -> 15**, root count **8 -> 13**.

Post-restore free chunks: **449, 448, 447, 446, 445, 444** at N=10..15. Cold reservation walk
remained **512 -> 480**, all walked credits zero (not workload peak). Snapshot telemetry grew
**2,187 -> 2,410 KB**, slab-used **2,491 -> 2,745 KB**, live **4,221 -> 4,799 KB**; heap_free
stayed 84 KB. Live increments are **65, 352, 64, 33, 64 KB**: retain the N=11->12 jump,
not a single linear per-slot attribution. Its responsible owner is not identified by this census.
No observed MEM_EXT_REFUSED, MEM_DENIED, FATAL or RING overflow in the ten measurement logs.

**Stopped because all approved empty scratch slots are now populated, not because of capacity.**
S1.3 and S1.5 remain unchecked: failure point not reached, two-bar workload only. S2b.3 remains
open because workload peak reservation is unmeasured; S3 does not start. Next device growth needs
explicit authorization for additional verified-empty slots outside the prior scratch range (the final
manifest lists candidates), or permission to recycle a named scratch subset. No clear/delete/rename
or recording outside track 6 slots 4–8 occurred this slice. Board left stopped, focus track 6 slot 3.

Evidence: `captures/s13-slope-evidence/slope.json`; detailed runs `s13-slope-slot4-04` and
`s13-slope-slot5-01` through `s13-slope-slot8-01`. Full native **2,384 passed / 1 skipped / 0 failures**;
decoder 51/51; host 35/35; OpenSpec/diff checks pass. Firmware unchanged from the prior green
capture-serial image (SHA256 `b289af53…`, code 391,304 / padding 1,912); only host tools/tests/docs
changed this slice. Work remains uncommitted on `feature/runtime-working-set-ownership`.

The two-bar fixture is now reproducible and growable. Earlier “zero added slots” and “restore
survival blocked” entries are historical; do not restart that investigation. The next unknown is the
resource boundary beyond this measured population, not token validity or take attribution.
All new takes persisted as LSV1. Whole-file hashes changed across normal epoch/geometry persistence;
that is reported separately from event survival and never counted as absence of writes. Target LSV1
host support now uses a golden Teensy-format synthetic fixture; no target storage format changed.
No architectural decision was introduced: implementation/evaluation under existing DEC-083 and
DEC-076; no new owner, no cleanup policy, no stage bypass. Session closeout docs and task status
are reconciled. UI/clock note-boundary parity and 64-bar density remain outside this small workload.

## S1.3 — extended empty-slot sweep execution packet

User authorized other verified-empty slots, preserving recordings. G2 oracle unchanged: each
step adds exactly one loaded slot, with exact stored/RAM event survival and identical boot
roles. Host extension accepts track+slot and restores the target track's original populated
selection before global focus 6/3; refuses an empty restoration anchor before any recording.
Start with track 5 slots 2–8. Keep a cross-track admission refusal distinct from loaded-population
residency exhaustion; no automatic S1.3/S1.5 checkoff for a confounded first extended point.
Host-only change; pinned firmware and no-clear/no-overwrite policy remain unchanged.

### S1.3 — canonical backing-block attribution (read-only follow-up)

The large N=11->12 increase has a concrete owner: `LoopEventStore::allocChunk` opens backing
storage in `POOL_CHUNKS_PER_BLOCK=64` groups before marking the first id in that block used.
Free chunks 448->447 means settled use 64->65. On Teensy `EventChunk` is 5,132 bytes, so the next
backing block costs 328,448 bytes (320.75 KiB), despite only one additional chunk being occupied.
The capture's largest allocation class grows 1,344->1,665 KB and its live allocation count 2->3;
a new snapshot slab contributes the remaining ~32 KB of the 352 KB rounded live increase.
This matches the allocation boundary and telemetry to their rounding precision; it is not a new
per-slot working-view bundle or a runtime-directory vector doubling (runtimeBySlot_ is an array).
Do not fit a uniform per-slot total slope across this block boundary. No firmware changes or extra
device probes were needed for this attribution. It supersedes the prior unowned-jump note.

### S1.3 — empty-selection anchor fixture extension

To continue the authorized empty-slot sweep beyond populated-anchor tracks, add an explicit
host opt-in for restoring an originally empty selected slot. Production path: stopped
`startRecordingTrack` queues an arm without capture; `handleTransportStop` clears pending
record actions and exits ARMED. Before any take on such a track, exercise that route with no
Start/clocks/notes, require no increase in record identity emitter entries, an empty selected-slot
RAM dump, unchanged observed content over all 64 slots, and restored global focus. After the
take, explicitly Stop the arm before any dump/SD request; the normal before/after boot-role and
content comparisons remain mandatory. Failure stops the sweep; do not infer empty-selection
restoration from code alone. No firmware change, cleanup, overwrite, or relaxed publication gate.

**Empty-selection anchor device gate PASS:** `captures/s13-empty-anchor-t3s3-01`.
Preflight identity entries stayed zero; all observed content fields unchanged. The subsequent
62-event take grew N=41->42, survived SD/RAM parity across reboot, and preserved the original
boot footer roles including the empty selected slot. Fixed stopped global focus 6/3 restored.
Host suites 39/39. Continue candidate tracks 3 slots 4–8, 7 slots 2–8, 8 slots 2–8 with the same
preflight, stopping at first failure. Empty original anchors themselves are not recorded into.

**Accounting interpretation correction (during extended sweep):** do not subtract the logged
ExternalPoolAccounting `live/high` from physical PSRAM capacity. The wrapper records every
successful `extmem_malloc` return without classifying its address. The pinned Teensy core
`cores/teensy4/extmem.c::extmem_malloc` tries `sm_malloc_pool` then falls back to `malloc`.
Consequently `#MEM_EXT_REFUSED` is a null return after that fallback, not the earliest physical
PSRAM denial; the wrapper comment claiming it is the earliest pool refusal is incorrect. The
N=53->54 episode reports tracked live/high above the boot-reported 8,182 KB pool with zero
wrapper refusals. This proves the counter is not a valid physical-free oracle; it does not by
itself identify all accounting drift. Preserve the raw measured tracked-byte slope and the
independent chunk/slab/snapshot counts. No allocator or instrumentation change in this sweep;
no PSRAM-headroom claim or first-PSRAM-refusal checkoff from these counters.

## S1.3 — extended sweep closeout (2026-09-25)

**2026-09-25 — S1.3 extended residency sweep: N=10..56; first publication denial recorded.**
46 two-bar additions passed exact 62-event / 1,536-tick SD/RAM survival before and after reboot.
All boot roles stayed fixed; no other observed content fields changed; 45 repeated intermediate
owner baselines matched exactly. Free chunks 449->403; snapshot telemetry 2,187->4,196 KB;
slab-used 2,491->4,774 KB; tracked live 4,221->6,846 KB. Tracked live/high includes the core
`extmem_malloc` internal fallback and is NOT a physical PSRAM-free oracle.

First named denial occurred at N=55 while recording the 56th slot (user track 8 slot 3):
`snapshot_reserve / heap_low`, 40,960-byte margin + 32,768-byte floor, heap 48 then 44 KB;
17 denial lines, zero wrapper allocation refusals. Publication deferred; the take persisted in
legacy format and survived reboot exactly. This is a temporary publication-admission boundary,
not a hard residency/canonical failure. S1.3 — residency slope remains OPEN on that distinction;
S1.5 — 24-bar overdub refusal and S2b.3 — workload-peak reserve remain OPEN. S3 is not started.

Growth stopped at that boundary. The supervisor initially advanced because survival passed;
the next target (track 8 slot 4) was interrupted during preflight before recording. The host
runner now returns a stop status on named memory denials even if survival succeeds. Final fresh
read-only boot confirms 56 loaded, same observed content/roles, stopped focus track 6 slot 3.
No clearing/deleting/renaming or populated-slot overwrite. User authorization for empty slots
was sufficient; this stop is a measured boundary, not exhausted authorization.

Evidence: `captures/s13-slope-evidence/extended-slope.json`, `extended-episode-peaks.json`,
`captures/s13-empty-anchor-t8s3-01/`, `captures/s13-sweep-closeout-01/`. Host tests 40/40;
OpenSpec validates. Firmware unchanged this slice (SHA256 `b289af53…`, code 391,304 / padding
1,912); prior native evidence 2,384 passed / 1 skipped remains applicable, not newly rerun.
Changes remain uncommitted on `feature/runtime-working-set-ownership`.

**Owner-level interpretation.** `admitSnapshotPublicationTurn` calls
`MemoryMonitor::admitOperationMemory(SnapshotReserve, 40960)`. Its own contract explicitly
keeps publication pending on denial and permits passes-authoritative persistence. The gate
requires 72 KiB internal-heap headroom; the first denial reports 48 KiB, later 44 KiB. Both
pre/post-reboot production decodes of this take report `legacy`, 62 events / 1,536 ticks.
Thus the first *observed named denial* in this ordered sweep is established; neither the
smallest universal refusal N nor a failed recording or physically exhausted pool is established.
S1.3's hard-failure wording is not silently weakened to accept temporary backpressure.

**Host correction disclosure.** Track 5 slot 2 initially timed out because returning to the
already-selected populated global anchor pressed its slot again and toggled playback. The
original failed result remains. `resumed-result.json` verifies the same take after the navigation
fix; it was not rerecorded. Populated-anchor steps then passed through N=41. Empty-anchor
preflight was proven at N=41->42 before expanding to tracks 3, 7, 8. Each later step repeated
it: no Start/clocks/notes, zero new identity entries, empty local dump, unchanged content fields.
Final tests also pin the stop-on-memory-denial behavior that the original batch runner missed.

**Next bounded packet, not started:** use the current 56-slot library as a verified baseline;
keep the snapshot-denial result as a backpressure result. Establish a denser production-path
pressure fixture in a remaining verified-empty slot if a hard-capacity point is required by S1.3.
Record its changed workload explicitly; do not splice it into the homogeneous two-bar slope.
Correct physical-pool-vs-core-fallback accounting before claiming PSRAM headroom or first
physical-pool refusal. S1.5 separately requires a valid 24-bar overdub, and S2b.3 requires actual
peak reservation; neither is established by the cold walk or this publication gate. No new
architecture decision, allocator policy, or publication behavior was introduced this slice.

## S1.3 — dense pressure probe execution packet (2026-09-25)

Scope: user accepted the next denser take. Host-only temporary variant of the listed
STIM-BASE-2-FIXTURE-01, named S13-DENSE-4BAR; production start/capture/stop/persist/restore
owners unchanged. Baseline: 56 loaded slots, fixed boot roles, track 8 slot 4 verified empty.
Workload: 4 bars at 120 BPM / 24 clocks per quarter, step 1 clock, gate 1 clock, cyclic pitches
0–127 on channel 8. Emit Off before the next On and omit a final On whose Off would lie
outside the clock window: 384 clocks, 383 balanced note pairs supplied. Actual committed
inventory is measured independently; SD/RAM parity is not a claim of lossless MIDI ingress.
Reuse the no-take empty-anchor preflight, complete 64-slot observation, recorded-slot identity,
two equal full SD reads + checksum/production decoder + event/length parity before reboot,
and exact growth/unchanged other content fields/boot roles after reboot. Preserve 6/3 focus.
Native controls: sparse generator unchanged; dense generator has 384 clocks, 383 pairs,
one-clock gates, requested channel and no remaining active notes; profile propagates to driver.
Run bound 360 s. Named snapshot denial alone is backpressure; failed admission, exhausted
allocator or missing publication/persistence is classified at its actual boundary. No timeout
is capacity proof. A hard crash ends writes; preserve evidence before recovery. No new
firmware build/upload image, no cleanup, no lease-stage change. Broad native/build evidence
is reused because this packet edits only host stimulus/driver/tests/docs. Completion requires
captured result and classification, not a planned pass. S1.5's 24-bar overdub remains separate.

## S1.3 — dense-pressure completion

**2026-09-25 — S1.3 residency slope COMPLETE: dense probe reached hard allocation failure.**
The fixed-role two-bar slope remains N=10..56 (46 verified additions). A separate dense
four-bar probe at N=56, user track 8 slot 4, supplied a one-clock note cadence across pitches
0–127. Recording started; at 41.448245 s device time, before record stop, the allocator refused
8,192 bytes with 4 KB internal heap free (`tag=0`) and emitted the fatal exhaustion line.
This is the measured failure point for this library/workload, not a universal smallest N or a
64-bar capacity claim. The untagged allocating caller is not identified. The last sampled free
chunk count was 393: do not classify this as chunk-count exhaustion.

The failed take had no verified persisted payload; the fixture withheld its survival reboot.
Separate crash recovery and a final read-only boot confirm the original 56 loaded slots,
unchanged other observed content fields, matching boot roles, and an empty failed target.
Board is stopped at track 6 slot 3. No clear/delete/rename or existing-slot overwrite.

S1.3 is checked: measured slope plus hard failure/error, alongside the previously completed
synthetic 60-descriptor characterization. S1.5 — 24-bar overdub refusal, S1.4 — undo/pin baseline,
S2b.3 — workload-peak budget and the other device gates remain open. No S3 — lease ownership start.
Host tests **42/42**, OpenSpec/diff checks pass. No firmware edits/build this slice; pinned image
`b289af534577e25b57c52146b27f66c245c97f43a5028fc10a37b9a8a9bbd208` reused.
Evidence: `captures/s13-dense-evidence/pressure-result.json`, `captures/s13-dense-t8s4-01/`,
`captures/s13-dense-recovery-01/result.json`; existing slope in `captures/s13-slope-evidence/extended-slope.json`.
Work remains uncommitted on `feature/runtime-working-set-ownership` (HEAD `1ccff16a`).


**Verification and classification.** Baseline/empty-anchor preflight passed with no new take
identity entries and unchanged observed content. Selected track 8 / Loop 4 then started recording
at 35.986 s. Snapshot publication denied at heap 36, 20 and 16 KB before the 8 KiB allocation
refusal at 41.448245 s; `ExternalMemoryFirstAllocator` reports both external and internal
allocation failure. The host reports `record_stop_unobserved`, but that timeout is not the
capacity oracle: the direct refusal and fatal lines are. Tag zero leaves the precise caller
unattributed; no allocator fix or speculative ownership attribution is included.

The host generated a separate 4-bar/120-BPM/384-clock profile, step=gate=1, with 383 balanced
pairs (final clock is Off only). It continues to record configured/supplied stimulus separately
from committed content: no 383-note capture completeness claim after an abort. Sparse default
behavior retains its prior test. The two-bar slope is not relabelled as dense or 64-bar evidence.
`extmem_malloc` fallback/accounting caveats from the previous entry continue to apply.

`reboot_after_take_allowed=false` remains in the failed result. `s13-dense-recovery-01` is a
separately labelled crash recovery, not a survival reboot: no content-creation actions, original
56-slot observed set preserved, failed target absent/empty. Final fresh boot matches original
role footer and stopped focus 6/3. No denial gate was relaxed. No new DEC entry is owed: this is
the authorized measurement under the existing runtime ownership contract, with no new owner,
policy or exception. Session closeout updates tasks, project/current state and deliverable tracking.
Remaining device gates, including undo/pins and 24-bar overdub refusal, retain their own oracles.

## S1.5 — device refusal-point execution packet (2026-09-25)

Scope: S1.5 clause 3's **device** half — the smallest N at which a 24-bar overdub is refused — on the
library S1.3 left at **N = 56 loaded slots**, plus the boot `live` figure against the ~191-chunk peak
reservation. Owner: the host HITL harness and a read-only census; **no firmware edit**. Non-goals: the
native id-ceiling result (already answered: admitted at every N up to 63), S1.3's slope (complete),
S1.4's undo/pins, and S2b.3's peak-reservation gate (its own owner and arithmetic).

Fixture: the verified `STIM-BASE-2-2-01` base preset through the legacy passthrough with
`--record-bars 24 --overdub-bars 24`, forked as a temporary, unregistered stimulus variant with a
recorded reason (the canonical 2-bar recipe cannot reach the byte boundary). Target: **user track 8
slot 4**, the S1.3 dense-probe target, verified empty and **authorized for this session only**; channel
8. No other slot is written, and no clear/delete/rename is issued.

Preconditions: pinned capture-serial image `b289af534577e25b57c52146b27f66c245c97f43a5028fc10a37b9a8a9bbd208`
(`captures/s13-controlled-01/firmware.hex`), re-uploaded for a fresh boot; read-only `!LIB_OBSERVE`
pre-flight proving **N = 56** loaded, **(8,4) empty**, and the boot roles/focus unchanged (stopped,
focus track 6 slot 3).

Transition oracle:
- **Pre:** 56 loaded slots; `(8,4)` has no committed content.
- **Record phase:** `record loop length (final ticks): 18432/18432` for the 24-bar request.
- **Overdub admission:** count `Overdubbing started` against `op=overdub_start` denials.
  `Overdubbing started` present with **0** denials → **ADMITTED at that N**; any `op=overdub_start`
  denial, `#MEM_DENIED`, `#MEM_EXT_REFUSED` or the fatal allocator-exhaustion line → **REFUSED**,
  classified at its named boundary.
- **Memory terms:** `#MEM_OWNERS` (`chunks_free` / `live` / slab-used / snapshot) at the settled
  pre-take and post-take checkpoints, `DIAG,capture_prewarm` reservation walk, and peak `live` during
  the overdub against 191 chunks.
- **Boundary discrimination (required):** a failure in the **record** phase (snapshot-publication
  denial or allocator refusal before any overdub) is S1.3's hard-failure class, **not** the overdub
  reserve refusal, and must not be reported as S1.5's refusal point.
- **Content follow-up:** if the take commits, run the `hitl-content-verification` pipeline
  (`dump_and_capture.py` + `verify_playback_content.py`); the verdict is read from
  `content-review.json` (`storage_matches_effective`, `inventory_equal`, `max_bar_refs`, classified
  residuals), never from the base report's setup oracle. `INCONCLUSIVE_CAPTURE` is not a pass.
- **Entry point:** `host_midi_hitl.py run --preset base …`; a captured `command.json` is never
  executed directly.

Evidence: one directory per step with upload identity, command logs, the session log and raw
observations; `metadata.json` `git_commit` / `firmware_hex_sha256` are the run identity. Bounded run
360 s. Stop at the first classified refusal or fixture failure, preserve evidence before any recovery,
and leave the library otherwise untouched.

## S1.5 — device refusal-point result: the 24-bar scenario is refused in its RECORD phase at N = 56 (2026-09-25)

**Run identity.** Pinned capture-serial image `b289af534577e25b57c52146b27f66c245c97f43a5028fc10a37b9a8a9bbd208`
(recorded as `firmware_hex_sha256` in `metadata.json`), git `1ccff16a`, device
`/dev/cu.usbmodem154944801`. Target **user track 8 slot 4**, verified empty and authorized for this
session only. Fixture `STIM-BASE-2-2-01-TEMP-S15-24BAR` (canonical base shape with
`--record-bars 24 --overdub-bars 24`, `--no-clear-before-record`, managed capture) — the same
RWSO-24CAP shape used on 2026-09-23, now on the S1.3 library. Evidence:
`captures/s15-24bar-t8s4-01/` (session log, `refusal-analysis.json`, run report), pre-flight
`captures/s15-preflight-01/`, recovery `captures/s15-recovery-02/`.

**Pre-flight (read-only, fresh boot).** All readiness fields attested, `reconcile=0`, `chunks_free 403`,
`live 6846 KB`, `slab_used 4774 KB`, `snap 4196 KB`, `heap_free 84 KB`, `heap_min 72 KB`; library
observation complete 64/64 — payload inventory 59, initial playback 8, committed at checkpoint 56
(pass-backed 56, root-backed 53); target `(8,4)` empty.

**The take.** The device entered RECORDING at 89.169 s (`Recording started @ tick 172`, `Loop 1: Start
Recording`). From **91.961 s** the `snapshot_reserve` admission was denied every ~1 s (`need=40960B`,
`reason=heap_low`); `heap_free` decayed **68 → 64 → 60 → 56 → 52 → 40 → 32 → 0 KB** over **39** denials.
At **130.538 s** the allocator refused the 8,192-byte unnamed request with `heap_free=0KB`, `tag=0`,
`live=8527KB`, `chunks_free=391`, `snap=4196KB`, and `[ExternalMemoryFirstAllocator] FATAL: both
external memory and internal heap are exhausted!` fired. The log ends there — **no crash report and no
reboot**. The host aborted with `record_stop_unobserved` / `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)`;
that is the setup oracle, not the capacity verdict.

**Classification (per this section's execution packet) — record-phase refusal.** `Overdubbing started`
= **0** and `op=overdub_start` denials = **0**, so the **overdub reserve admission was never reached**.
The refusal is the **S1.3 hard-failure class** (internal-heap `snapshot_reserve` → `MEM_EXT_REFUSED`
8,192 B, `tag=0`, `heap_free=0`), here reproduced with the **canonical 24-bar base workload** rather than
only the dense 1-note-per-clock probe. It is **not** the S1.5 overdub-reserve refusal and must not be
reported as it. The boundary is the **internal heap**, not the chunk pool (`chunks_free` 391 of 512) and
not the snapshot arena (`snap` did not grow from its boot 4,196 KB).

**Reading.** At N = 56 the 24-bar **record prerequisite** is refused, so *"the smallest N at which a
24-bar overdub is refused"* is **not isolated** at N = 56 — the device refuses the scenario earlier, in
the record. **S1.5 clause 3's device half stays open.** The bracket is now **(2, 56]**: the scenario was
**admitted** at the 2-slot library of RWSO-24CAP (2026-09-23) and **refused** at the 56-slot library
here; the S1.3 2-bar slope additionally recorded its first named `snapshot_reserve` denial at **N = 55**
(a 2-bar take, which persisted anyway).

**Peak figures (S2b.3 input, not an S1.5 checkoff).** Boot `live` **6846 KB** / `chunks_free` **403**;
peak at the refusal `live` **8527 KB** / `chunks_free` **391** / `heap_free` **0**. The ~191-chunk peak
*reservation* figure still has no telemetry: no `capture_prewarm` rows are emitted during a take, and
the reserve admission was not reached.

**Library integrity.** A fresh recovery boot (banner, `reconcile=0`, all readiness fields,
`checkpoint_reached`) reports the **identical** observation — payload 59, initial playback 8, committed
56 (pass-backed 56 / root-backed 53) — with **0 content-field differences across all 64 slots**, target
`(8,4)` still empty, `free_chunks 403`, `live 6842 KB`. Nothing was cleared, deleted or renamed; only
the authorized target was addressed.

**Next bounded step (not started).** Isolate the **overdub** reserve admission, which needs a 24-bar loop
present at an N with internal-heap headroom — either an authorized empty target at a **lower** library N
(record the 24-bar loop, then overdub it), or an explicit decision to treat the N = 56 record-phase
refusal as the S1.5 / S2b.3 capacity boundary. No firmware change, no new owner, policy or exception —
so no `DEC-###` entry is owed.

## S1.6 — heap accounting and allocation-failure attribution (2026-09-25)

User priority: repair heap logic before further capacity sweeps. This packet supersedes the
lower-N-or-reclassify next-step recommendation; S1.5 and S2b.3 remain open.

G0 discovery: VERIFIED: Teensy extmem_malloc includes an internal malloc fallback; wrapper
accounting currently shadows that pointer but allocator deallocation can call free directly.
Same-address replacement also fails to retire its old tag. getInternalHeapFreeBytes measures
_heap_end minus __brkval (unused arena tail), not reusable malloc free blocks. The shadow
metadata is 8192 * 12 = 98,304 bytes in RAM2, sharing the internal malloc address space.
UNKNOWN: owner of the fatal 8192-byte request, actual fragmentation and retained fallback
bytes. Snapshot live bytes unchanged does not exclude backing allocation pressure.
G0 PASS for accounting repair and attribution only; owner-lifetime repair awaits attribution.

G1 contract: extend ExternalPoolAccounting only. Shadow physical external addresses only,
using the core's external-address classification; internal fallback is not pool occupancy.
Keep allocation routing, capacity, admission thresholds and MemoryMonitor's conservative
heap-tail metric unchanged. Retire old size/class/tag on address replacement; reuse tombstones
when the table has no never-used entry. Report wrapper caller PC on capture-build refusals,
without stack unwinding, extra allocation, pool walk, or per-call-site instrumentation.
Existing refusal output is reused; caller PC requires the exact flashed ELF to resolve and
may identify an allocator instantiation rather than a unique domain owner.

G2 review PASS with tightening applied: fallback free bypass is covered by exclusion from
physical accounting; address reuse and full-table tombstones require real implementation tests;
reset must reset tag scope as well as counters. Async completion, undo/load/save, switching,
publication and transport retain existing ownership and policy. No new owner, reserve, SD
operation or snapshot lifetime. All callers pass through the same linker wrapper; direct core
calloc/realloc are outside its coverage, so this is wrapped live payload, not pool free space.
No heap free-list walk or allocation probe added on the MIDI path. Existing table bound remains
8192 probes and RAM2 footprint is held fixed. DEC-018 current-heap admission and DEC-073 snapshot
publication admission remain in force; no new architectural decision is required for this repair.

Oracle fixed before edits: external allocation adds size once; free removes original tag even
under a different current tag; internal fallback followed by direct free contributes zero;
same pointer reallocation retires the old tag; a saturated then entirely freed table accepts
another allocation with no new untracked count. Native fixture includes the production cpp
and linker wrapper with fake underlying extmem endpoints, not a copied accounting model.
Failure PC is supplemental and must not alter allocation/refusal return values.

Verification: focused test_external_pool_accounting, full pio test -e native, capture-serial
build and RAM1 pins. Device follow-up reuses the canonical temporary 24-bar stimulus at N=56,
only the existing authorized empty target, with exact ELF/hex identity and recovery observation.
No capacity gate closes from native checks. No broad sweep or SD clear/delete/rename.
Status at packet creation: specified; native/build/device NOT RUN.

### S1.6 completion record — accounting implemented; physical exhaustion persists

Three discriminating failures before repair: fallback live bytes 8192 instead of 0; old tag
1024 instead of 0 after address reuse; full-table free/reuse increments untracked 1→2.
After repair: focused 5/5; full native 2,389 passed / 1 skipped / 0 failures; observation classifier
18/18; OpenSpec valid. Capture-serial SUCCESS: code 391,304 + padding 1,912, variables 96,160,
locals 34,912; RAM2 static 155,200, malloc arena 369,088. No budget rebase or table-size change.

Flashed image SHA256 `c7684714c75587a933beeb226584b47089ff18bafeda48c61f14ce789bcdb57d`;
ELF SHA256 `50c49bdc05a5798d0a56b365e34c0a29d3d7d11df8d3b2c7e6f322773de6e584`.
HEAD remains 1ccff16a with pre-existing dirty work; patch and image identity saved in
`captures/heap-accounting-image/`. New preflight: N=56, payload59, target user8/4 empty,
403 free chunks, all readiness fields, no reconcile warning, untracked=unknown=0.

Reused temporary canonical 24-bar stimulus via host_midi_hitl.py run --preset base,
no-clear-before-record, user8/4. Recording began97.930s; at138.642338s a 5,216-byte request
failed (different from historical 8,192). Caller0x6000949b resolves to DisplayManager's
resolveDisplayNotesByDrawSource; disassembly confirms a call to __wrap_extmem_malloc at
0x60009496. Symbols are present but source line DWARF is absent. This function uses a
frame-local candidate, committed-prefix copies and pending-paint copy; exact expression and
retained/fragmented attribution remain open. Do not infer a leak just from this symbol.
Corrected external wrapped live8142KB, blocks1165, untracked0/unknown0, heap-tail4KB;
snapshot4196KB, chunks391. Allocation exhaustion persists after accounting repair.
Host verdict SERIAL_INCOMPLETE due to record stop never reaching PLAYING: not a content PASS,
not S1.5 overdub refusal. Caller evidence in `captures/heap-attribution-24bar-01/`.

S1.6a checked; S1.6b partial; S1.6c remains open. Existing snapshot admission and allocator
routing unchanged. No new ownership decision, migration, or lowered gate. Further owner
fix needs a production display allocation/lifetime fixture, including pending overlays and
handoff preservation, before any claim of improved capacity. No library clearing authorized.

Recovery: `captures/heap-accounting-recovery-01/` reaches the timestamped readiness
checkpoint, N=56 / payload59 / 403 free chunks. `recovery-comparison.json` compares ten
observed content fields across all64 slots: zero differences; target user8/4 remains empty.
No clear/delete/rename was issued. This proves unchanged observed fields, not no writes.
The boot-census helper still prints a cold-credit S2b.3 substitution; that output is explicitly
NOT used for checkoff. Board recovered on the same new image; no further growth attempt.

Remaining implementation boundary: the caller evidence identifies a display allocation, but
not whether its required capacity, duplicate frame lifetime, fragmentation, or cold-library
retention is the defect. A pressure-based skip of display work would hide rather than resolve
that question and could violate overlay/handoff correctness. S1.6c is not implemented or
claimed complete. Resume with a production display memory-lifetime fixture, not another sweep.

## S1.6d — explicit stopped heap diagnostics

User authorizes direct allocator inspection only while stopped. G0: existing internal metric
is unused arena tail; pinned newlib has 258-word malloc bin array, top at word2, 127 bin
sentinels starting word2; mallinfo updater walks these bins. Core smalloc uses 12-byte
headers/trailers and scans free space in header increments. Both allocators are single-owner
in this firmware (newlib lock functions are no-ops). Existing external wrappers remain the
live bookkeeping; no additional allocation hook, shadow table or automatic scan is introduced.

G1/G2: MemoryMonitor owns explicit !HEAP_STATS report. Reject transport, external clock,
active tracks/capture/stop work, save/load/restore work before touching allocator metadata.
Collectors allocate nothing, never probe with malloc/free, never mutate allocator lists,
and have bounded visits (4096 internal free nodes; pool_size/12 external steps). Internal
scan validates addresses/sizes/links; incomplete means invalid/unsupported or visit bound,
not a capacity result. Report raw free chunks (including allocator overhead), largest raw
chunk, top chunk and unextended tail separately; external reports physical free gaps and
largest request under pinned smalloc geometry. No admission policy consumes these values.
Live logging stays existing bookkeeping only. No scan on rejection, no periodic scan, no
scan on record/stop transition. Stopped is necessary but not sufficient: active external MIDI
clock is rejected. User request explicitly permits this guarded diagnostic exception to the
setup-only pool-walk rule; automatic logStatus(true) stays setup-only.

Oracle: denied request invokes neither scanner; valid synthetic bins/gaps return exact totals
and largest sizes; cycles/out-of-range metadata terminate incomplete; known split holes can
have enough aggregate free but insufficient contiguous space. Device: explicit command at
settled boot emits complete report, active transport refuses; no SD mutation/stimulus take.
No leak claimed by one snapshot; compare matching settled states/content and account for
intentional retained owners. Allocation provenance remains the existing refusal PC, not a
complete malloc live-allocation ledger. Tests/native/build/HITL required before checkoff.

S1.6d device falsification: `stopped-heap-02/heap-refusal.json` failed: clock pulses while stopped do not select the external clock source (`onMidiClockPulse` returns early), so `isExternalClockPresent()` alone admitted inspection. Corrected with capture-only raw pulse timestamp/seen bookkeeping before that early return; explicit setup initialization, one-second quiet interval and wrap-safe subtraction. No malloc hooks or live allocator reads. Native clock-quiet/wrap test added; `stopped-heap-03` is the revalidation fixture. The first two captures are pre-correction evidence, not the final refusal gate.

Verification: focused diagnostic 5/5; full native 2,395 cases = 2,394 passed / 1 skipped; capture-serial SUCCESS. RAM1 variables 96,160 / code 391,368 / padding 1,848 / locals 34,912; RAM2 static extent remains 155,200 (the capture-only timestamp fields fit existing alignment space). No budget baseline changed. Initial stopped samples have internal arena 283,072, used chunk bytes 177,288, free chunk bytes 105,784, largest/top 105,768, tail 86,016; external payload 7,006,576, occupied 7,035,612, free 1,343,676, largest request 1,326,876, blocks 1,025. These are settled N=56 baseline readings, not failure-time measurements. No capacity threshold or leak claim is inferred.

Final device gate **PASS**: `captures/stopped-heap-03/heap-stats.json` both complete; `heap-refusal.json` returns exactly `#HEAP_STATS,refused=external_clock` for the clock-only temporary stimulus. Final image SHA256 `a71b50eb1a8dc4a91f95838891d437335b19a516d9de8aa3218a7e6164faa9ae`, dirty-tree identity/patch/build/native evidence preserved in `captures/stopped-heap-final-image/`. No take, slot selection, CLEAR, SD delete/rename or payload write command was issued. Census N=56 / chunks=403 is context only: the helper's old cold-credit S2b.3 arithmetic is NOT workload-peak proof. Library observation reports consistent_view=false, so it is not used as a new library-stability attestation. S1.6d checked; S1.6b attribution and S1.6c owner correction remain open. Next useful measurement: explicit stopped snapshots before and after the same controlled workload with transient owners retired; do not run inspection during recording to obtain a failure-time sample.

### S1.6b — repeat the original exhaustion workload with stopped diagnostics

User requested the same failing HITL. Reuse baseline preset and temporary HEAP-24BAR stimulus; effective settings are 24-bar record + 24-bar overdub, second overdub 2 bars, user track 8/slot 4/channel 8, 120 BPM, no pre-record CLEAR. Historical metadata/stimulus summaries show 2 bars, but the original host report config shows 24/24 and is the fixture authority. Preflight requires N=56 and the target empty. Use the existing pinned diagnostic image, a fresh boot and explicit stopped heap reading. Never query the heap during recording. Outcome oracle: named allocation refusal/FATAL versus completed record/overdub; a hard failure precludes a same-boot post-stop snapshot, and recovery-boot statistics must not substitute for one. No source or allocator fix in this rerun.

Rerun result: `captures/heap-rerun-24bar-01/result-summary.json`, firmware `a71b50eb1a8dc4a91f95838891d437335b19a516d9de8aa3218a7e6164faa9ae`. Effective host config matches `heap-attribution-24bar-01` exactly except output/log paths (including 24/24 bars and no CLEAR). Recording began 77.652 s; fatal at 109.728275 s (~32.08 s into recording), before record stop or any overdub. 29 `snapshot_reserve` denials; refused 8192 bytes, caller `0x6008c919`, external live 8144 KiB / 1176 blocks, reported internal tail 8 KiB. Pinned disassembly resolves this to the shared out-of-line `vector<DisplayNote, ExternalMemoryFirstAllocator<DisplayNote>>::_M_realloc_insert` helper (512 × 16-byte elements). It does not identify which display vector called the helper. This is the same hard-failure class, not the exact previously observed 5216-byte copy-assignment site. Last live telemetry: chunks=392, snapshot=4215 KiB, slabs=152; one more slab than the preflight, unlike the previous run's flat snapshot. Do not erase that timing-sensitive difference.

The managed harness ends `INCONCLUSIVE_CAPTURE / SERIAL_INCOMPLETE`, exit 3, because record stop never reaches PLAYING. Firmware's explicit refusal/FATAL proves exhaustion independently of that incomplete content verdict. Zero `#HEAP_STATS` during the workload. Preflight stopped scan: free internal chunks 105784 / largest 105768 / untouched tail 86016 bytes; external free 1343676 / largest request 1326876 bytes. Fatal prevents a same-boot stopped after-sample; recovery readings are a new baseline, not leak/fragmentation proof. S1.6b attribution and S1.6c owner correction remain open; S1.5 overdub refusal and S2b.3 workload peak remain unclaimed.

Recovery complete: `heap-rerun-recovery-01` restored N=56, free_chunks=403, target (7,3) empty/payload absent. `recovery-comparison.json`: 64/64 rows both boots, zero differences in the ten compared content fields. Observations have consistent_view=false, so this is a limited field comparison rather than a stability attestation. No CLEAR/delete/rename commands. Recovery stopped heap scan complete and returns the same allocator metrics as preflight (duration aside); this reset baseline is not proof of runtime release. Board recovered, no code or script edits in the rerun, OpenSpec validates.

### S1.6c — known owner repairs execution packet

G0 VERIFIED: releaseWholeTakeViews refuses selected bridges then releases base.storage only; RuntimePlaybackView destructor alone frees origin/rank arrays, retaining 24,576 bytes across two 2048-event bases while fixedExternalBytes drops them. Production native probe reproduces it. Display resolveDrawSource constructs a fresh candidate every frame, then swaps away and destroys the previous compose allocation; pending paint separately copy-assigns. Slide filtering creates another vector. These cause unnecessary allocation churn, not proven malloc leaks. G0 PASS for these concrete repairs; exact shared realloc-insert caller and ultimate capacity sufficiency remain UNKNOWN.

G1 contract: extend existing owners, no new persistent buffer/member. RuntimePlaybackView::release resets metadata, frees event storage and both side arrays, nulls pointers; destructor and whole-take release share it. Selected bridge refusal must precede all mutation. Re-prewarm after release must recreate usable arrays. Draw-source compose borrows the already-owned pending-paint buffer while that prior paint is no longer in use; return old compose storage before copying/transforming the new paint. Guard restores borrowed storage on early exits. No drawing runs reentrantly inside this resolver. Handoff branch retaining liveDisplayNotes must keep its content; paint transforms must never mutate live compose. Slide-filter notes in place with identical predicate/order. No memory scans, policy/threshold change, new reserve or early long-record release.

G2 adversarial review: selected bridge must preserve pointers/metadata (covered test); release/re-prewarm/release and destructor cannot double-free (test); early scope exit returns scratch; retained-handoff branch returns gathered buffer then copies authoritative old compose (test); steady-size repeated frames retain the original two allocations (counting allocator test); slide inclusion parity includes wrapped notes and zero geometry (test). Storage, undo, stop publication and clock paths unchanged; no async callback during buffer borrow. No new RAM1 state, heap footprint should drop; existing display detail/window/content semantics remain authoritative. Historical search: DEC-076 role-owned working sets, existing RC-MEM-2 release policy and DisplayManager derived-view ownership; repair within those owners, no new architecture decision.

Oracles: side arrays null when reported released, selected view unchanged, re-admission usable; display rows identical and no allocator calls over repeated stable-size frames with existing buffers. Full native, capture-serial budget, then same authorized track8/slot4 24/24 no-CLEAR stimulus. A repeated fatal keeps aggregate S1.6c open rather than claiming capacity fixed.

First repair image `15370a07…` retains the N=56 failure: `heap-ownerfix-24bar-01`, 4112-byte request at 123.383424 s (record start 91.013 s), caller 0x600094f9 in draw-source resolver. Pinned disassembly shows replacement allocation then copy then free of the old vector at DisplayManager offset 180, the pending-paint member. This exposes the remaining known allocate-before-release spike at paint copy growth. Tighten the same owner contract: when capacity is insufficient, discard obsolete paint backing before allocating its replacement; live composed source remains untouched. No fallible display fallback, source truncation, reserve policy or geometry change. Add counting-allocator test enforcing peak source+replacement, excluding obsolete paint. This narrows allocation overlap but does not promise global capacity sufficiency.

Paint-copy regression oracle: host libc++ assignment already frees first, so a peak-only host test passed the old implementation and was insufficient. The test now observes whether backing has been retired on entry to the production copy helper, independent of the host STL. It fails red (`Expected TRUE Was FALSE`) under the old helper and passes after explicit obsolete-backing retirement. The target allocate-copy-free order is separately pinned by disassembly. This is not a claimed host reproduction of Teensy malloc internals.

Final known-owner repair verification: focused 301/301; full native 2400 cases = 2399 passed / 1 skipped; capture-serial SUCCESS, RAM1 variables 96160 / code 391368 / padding 1848 / locals 34912; RAM2 static 155200 unchanged. Image `10695e07e73fdcfb9db111b9d61fd7d40815b614e0bbdc3ad5fb8f54a56000da`, exact ELF/patch/red/green/build evidence in `captures/heap-ownerfix-final-image/`. Final HITL `heap-ownerfix-24bar-02` matches the prior effective config exactly except log/output paths, but is NOT a pass: recording starts81.996s, fatal129.615111s (~47.62s), 6096 bytes, caller0x6008cc6b resolves to the shared DisplayNote vector::reserve helper; live8145KiB, blocks1177, internal tail0KiB, chunks390, snapshot4215KiB. No record stop/overdub or same-boot stopped after-sample; no live heap query. Native evidence closes only S1.6c.1's known lifetime defects. Which display reserve caller and whether its failure is fragmentation versus total live demand remain unresolved; do not equate longer survival with a capacity fix. S1.6c, S1.5 and S2b.3 remain open. No allocator/admission policy, full-take admission timing or display horizon was changed.

Final recovery `heap-ownerfix-recovery-02`: N=56, chunks403, target user8/slot4 remains empty; 64/64 slot records, zero differences in ten compared content fields versus `heap-ownerfix-preflight-02`. Observation consistent_view=false remains a limitation, not a stability gate. Both stopped heap completeness flags true and baseline allocator values restored after reboot. No same-boot after-sample, no leak verdict. No CLEAR/delete/rename commands. Final known-owner fixes remain uncommitted; broad capacity gates unchanged.

## S1.6c.2 — snapshot construction reuse (2026-09-25)

User approved implementation after the code/host allocation investigation. Owner remains
LoopSnapshotConstruction under DEC-048; published graph ownership, slab routing, geometry,
SD format and publication gates are unchanged. The existing note/event apply workspaces
retain their private pitch/page/membership edits for one construction, rather than copying
those pages anew for every change. Workspaces never cross construction boundaries.

Oracle: 128 notes across 12 pitches plus 80 events in one bar require at most 256 snapshot
nodes after the empty parent (objects + distinct affected directories/pages + freeze), with
all identities preserved. A parent and interleaved sibling construction remain unchanged
at every single-unit step. Crossing membership pages preserves all references and the
4096-reference ceiling; failed candidates cannot publish. Zero-unit steps do no work.
Baseline host probe: 128 notes alone allocated 517 nodes, retained 4 slabs (128 KiB) with
167 live nodes; root release freed all slabs. Host sizes are not device byte estimates.
`slab_used` is bump high-water, not live occupied bytes: old dense-retention claims do not
exclude retired node holes.

Implementation/review constraints: cached membership tails and page counts must be updated
when a chain grows, or reuse can overwrite a chain/bypass the ceiling. Only private copies
may mutate; no use_count heuristic authorizes mutation. Construction completion/freeze must
ignore retained idle workspace pointers, and further step calls cannot mutate frozen roots.
Existing per-unit membership copy/removal/sort remains resumable; cache lookup is affected-
content-sized, not newly claimed constant work. No static RAM1 state or live heap scan.

Verification: red/green production-builder tests in test_loop_event_store; full native;
teensy41-capture-serial build and RAM1 pins; fresh read-only boot census and the reused N=56
24-bar base HITL on authorized track 8 slot 4, no clear/delete/rename. Device memory savings
and capacity success are separate from native allocation proof. Capture ring unchanged.
Role-specific reservation work follows only with its own call-path/lifetime contract; no
lease-pool or window-generation dependency is claimed implemented by this slice.

Status: implementation and native/boot-memory verification complete; intended workload acceptance remains open (see result below).

#### S1.6c.2 adversarial review

- COVERED — parent and sibling isolation: construction caches contain only nodes allocated/copied by that construction; interleaved single-unit builds verify the parent after every turn. No uniqueness/refcount mutation of published nodes.
- COVERED — cached chain growth: cached tail and page count update after append; 4,097 same-bar references fail with MembershipCapacity and cannot freeze. Zero-unit step allocates nothing.
- TIGHTENING applied — event membership copy state survived workspace reuse. Reset `sourceWasEmpty` when selecting a cached membership so subsequent event appends sort every page through the existing bounded sort state. Regression reverses canonical order within five membership pages.
- COVERED — terminal lifetime: workspaces can remain allocated after completion without remaining active; subsequent step cannot mutate the frozen root. Destruction returns private references, and the batch regression checks live slabs return to the entry count.
- LIMIT — workspace lookup vectors now span the construction's affected edits. This reduces snapshot-node/slab allocation, not necessarily peak internal scratch. Device revalidation remains required; no constant-time lookup claim or new allocator authority.
- Role-specific reservation finding: `Track::prewarmPlaybackForSlot` explicitly guarantees resources for deferred stop and subsequent overdub. `LoopPlaybackRuntime::prewarmViews` admits whole-take views for unknown/short geometry, alongside first-record return-window resources. Removing these based solely on the RECORD state would weaken the current admission contract. This slice leaves that policy unchanged; the role-transition contract and production-path short-take/immediate-overdub fixtures are required before its replacement.

#### S1.6c.2 verification and fixture failure

**2026-09-25 — S1.6c.2 — snapshot construction reuse implemented, device gate open.** Candidate-private pitch/page/membership edits now persist across one construction (DEC-085), with cached tail/count updates and cached-event sort-state reset. Native **2,402 passed / 1 skipped**, focused snapshot **42/42**, target build green; RAM1 unchanged (code391,368 + padding1,848; variables96,160; locals34,912). Image `94b09b51…`. Fresh N=56 census: external payload **7,006,576 → 3,238,256 bytes**, snapshot slabs **151 → 36**; all 64 observed slot records match the earlier census (both lack consistent-view attestation). This saves **3,768,320 bytes (~3.59 MiB)** without deleting content or shrinking the capture ring.

The stress rerun is **INVALID_TARGET_INTERRUPTED**, not acceptance: configured track8/slot4 but the no-clear harness armed selected **track8/slot1**, recorded/overdubbed and persisted `slot:7:0`. Interrupted and explicitly stopped; `STOP_STATUS` confirms STOPPED/pending=0. No clear/delete/rename or cleanup. User reports device85.4BPM, explaining host24-bar timing yielding17devicebars (13,056ticks). No fatal/external refusal, but seven late `snapshot_reserve/heap_low` rows and minimum heap tail12KiB mean overall heap correctness remains open. Host precondition now fails before gestures unless the latest device-time DISP since track selection verifies the configured slot; host tests56/56. No further content runs until the selection fixture is corrected. Role-specific reservations remain unchanged pending their transition/admission contract. Evidence: `captures/snapshot-reuse-preflight/`, `captures/snapshot-reuse-24bar/`, `captures/snapshot-reuse-image/`. Uncommitted.

Red→green: the production-builder node-ceiling test failed before reuse (915 nodes >256) and passes after reuse. Parent/sibling isolation, partial failure, zero-unit work, frozen-root stability and the 4,097-reference refusal pass. Event ordering is adversarial within five pages. Full-native final log `/private/tmp/snapshot-reuse-native-final.log`; firmware build `/private/tmp/snapshot-reuse-build-final.log`. No ABI/budget rebase.

The late low-heap diagnostic is a **tail** observation, not proof of 12KiB aggregate free memory or a malloc leak. It occurs after second overdub, with external live peaking5,123KiB, high-water5,131KiB; do not claim the complete heap hang fixed. A stopped-only allocator read was requested after explicit STOPPED confirmation.

Harness cause verified: `_send_record_arm_press` discards `loop_slot`; selection is performed only by `_run_clear_before_record`, which the authorized no-clear recipe skips. The added `_require_observed_record_slot` guard does not perform selection or clear; it refuses absent/mismatched evidence before any record-arm gesture. Five new host tests include the production call path sending nothing on mismatch. The unintended take is left intact; no cleanup authorization inferred.

Session closeout: decision, scope, behavior/spec delta, task status, current/project state and deliverable record updated. Full capacity, exact24bar fixture, role-specific reservation changes and inactive-root demotion remain unclaimed.

Stopped-only follow-up (`captures/snapshot-reuse-24bar/stopped-heap.log`): internal arena356,800B, allocated chunk bytes199,392B, free chunks157,408B, largest free chunk122,768B, unused tail12,288B; external payload5,246,288B, free3,100,380B, largest request3,046,620B. Both scans complete. This disproves interpreting the *stopped* 12KiB tail as total available heap. It does not retroactively measure free lists at the earlier live refusals; tail-based snapshot admission remains the next hypothesis to verify without live scans. No leak/fragmentation verdict from this one stopped reading.

### S1.6c.2 fixture repair — no-clear arm selects the target slot (2026-09-25; host-only)

G0 VERIFIED (read + recorded evidence): `--no-clear-before-record` skipped the clear **and the
selection**. Slot selection exists only inside `_run_clear_before_record`, so the no-clear arm acted
on whatever slot was already selected — the recorded `INVALID_TARGET_INTERRUPTED`
(`captures/snapshot-reuse-24bar/review.json`: configured track 8 slot 4, armed slot 1,
`slot:7:0` persisted). `_send_record_arm_press` deliberately does not select (a Loops-row tap starts
an empty-slot capture and would skip ARMED→play, `session_20260910_122343`), so the selection is a
separate pre-arm step, not an arm-press parameter. The S1.6c.2a guard correctly failed closed, but a
guard cannot run a fixture.

Implementation (`scripts/hitl/legacy_record_baseline.py`, `scripts/hitl/legacy_edit_baseline.py`):
`_select_loop_slot_before_record_arm` stops the transport first when it is selecting on the no-clear
path, sends the Loops-row select, and reuses `_stop_empty_slot_select_recording_before_clear` to
retire a select-started ghost capture. The proof is **slot identity, not an EMPTY state**: selecting a
stopped slot arms it, so `_await_observed_record_slot` waits (bounded) for a device-time DISP row for
the target slot and then falls through to the existing fail-closed guard. `_run_record_arm_until_capturing`
calls it on the no-clear path, baselined at the select, so slot proof and arm-timing proof come from the
same window. `select_loop_slot=False` suppresses a second tap for callers that already selected; the edit
path shares the helper instead of its inline tap + sleep.

The transport stop is not cosmetic. Device evidence from the first corrected run
(`captures/rwso-select-24bar-20260925_230706`): the clear path selected Loop 4 while the transport was
still running, so the empty-slot tap started capture immediately (`Loop 4: Start Recording`); the
subsequent stop left the track `ARMED`, the arm step reported `ACTION ALREADY_SATISFIED … state=ARMED`
and skipped its press, and the resumed transport produced a 376-tick take whose stop never returned to
PLAYING (`record stop did not reach PLAYING`, other slot left empty). With the transport stopped before
the tap the same selection only arms; the proof then succeeds and the arm sequence runs on a known
state. This is a harness-ordering defect, not firmware.

A later probe run exposed the second half of the same class: the bounded wait exists because the
stopped display emits on change, so a quiet display must be waited out rather than read once as a
mismatch. Oracles: the select note is `LOOP_SELECT_NOTE_BASE + slot - 1`; the no-clear path requests
the transport stop before the select press and the clear path does not (it already owns that stop); a
slot that does not move still refuses and sends no arm press; the baseline is taken before the select
press. `scripts/test_record_target_precondition.py` — 17 tests green. Full host script suite unchanged
from baseline: 48 files green, the same 8 pre-existing failures
(`test_clear_before_record_precondition` and 7 unrelated) fail identically with this slice stashed.

Also recorded: the 2026-09-25 stress rerun's `metadata.json` shows `record_bars=2`/`overdub_bars=2`
(the base preset default) even though the run was read as a 24-bar workload, and the device recorded
13,056 ticks (17 bars) because the fixture streamed 24 bars of notes at the host's 120 BPM while the device ran
its internal clock at **85.4 BPM** (`#CAP,…,DIAG,clockrate,0` — no external clock; the reported
length matches 24 × 85.4/120 = 17.08 bars). Tempo and slot are therefore independent fixture
parameters; the corrected run pins both.

### S1.6c.2 fixture support — loop-edit tempo fader (DEC-086)

The tempo defect above had no workaround: nothing in the firmware could set the device BPM, so a HITL
could only guess it. Added loop-edit fader 4 on the loop-edit control channel — **ch16 CC102, CC
0..127 → 72..200 BPM** — because ch16/CC101 already establishes that pattern for loop length and CC102
was free on that channel. `LoopEditManager::handleTempoInput` owns the mapping and endpoint
(`ClockManager::setBpmFloat`, which already maintains the global `bpm`); the law lives in the
dependency-free `TempoMapping` header so the host tests and firmware cannot drift; session entry sends
the current tempo back so the motor parks at the live value. `MidiFaderConfig` carries the binding
(`FADER_TEMPO` / `SET_TEMPO`) and `MidiHandler` keeps the CC out of the fader feedback-only path.

Device-verified on image `870f2948f0676d60…` with `scripts/hitl_tempo_fader_probe.py`
(`captures/tempo-fader-01/`): CC32 → 104.3, CC100 → 172.8, CC64 → 136.5 BPM, each within 0.1 BPM of
the linear law and applied live, plus `Sent tempo CC feedback: 172.8 BPM -> CC=100` on session entry.
Native `test_tempo_mapping` 8/8, full native **2,421 passed / 1 skipped**; RAM1 unchanged
(`variables 96,160 / code 392,024 / padding 1,192 / locals 34,912`), 352 B over the approved floor.
The probe is a read-only control-surface check: no record/overdub, no clear, no content writes.

### S1.6c.2 remaining gate — the target slot must be empty again

The failed runs left takes in the authorized witness: the 20260925_231212 run committed a 768-tick
take in track 8 slot 4, and the 20260925_230706 run persisted one to `slot:7:0`. The last probe
therefore found slot 4 **populated** — the select tap logged `Loop 4: Toggle Play/Stop`, not
`Loop 4: Start Recording` — and the guard refused with `latest observed slot unobserved`, which is
fail-closed and correct. No clear was issued: the library is user data and the firmware has no
restore path. The 24-bar capacity revalidation resumes only after the owner clears or re-verifies an
empty target slot, and it must pin the tempo (DEC-086) rather than inherit the persisted value.

## S3.1 — `WorkingSetLease` identity and declared pool (2026-09-25, G2 execution packet)

### Identity and scope

Owner stays `TrackPlaybackRuntime`; S3.1 adds **vocabulary and bookkeeping only** and does not move a
resource. Today the only per-slot working-set identity is the `LoopPlaybackRuntimePtr` array
(`TrackPlaybackRuntime.h:659`), created by `trySlot` with no generation and no role: `trySlot` cannot say
*which* role holds a slot, so a late cleanup cannot be distinguished from a live one (invariant 6, S4.2)
and release has no identity to reject against (S4.1).

Slice boundary: an identity type (`slot + generation + role`), a composable role set, and a **declared
per-role pool with a cap that reports rather than allocates**. Routing today's prewarm call sites through
it is in scope only as bookkeeping — the resource calls themselves stay where they are, in the same order,
with the same admission answers. Releasing, recycling, stale-owner rejection and window sizing are S4/S4f/
phase F and are **not** implemented here; the types must merely be able to express them.

### Audit of the existing prewarm call sites (read, not changed)

| Site | Role it establishes | Cardinality source |
|---|---|---|
| `Track::prewarmPlaybackForSlot(slot, false)` at boot/load | none (runtime index only, S2b.1) | `TrackManager::prewarmPlaybackRuntime` skip predicate |
| `Track::prewarmPlaybackForSlot(slot, true)` (record arm) | `PlaybackWindow` + `LiveCapture` | 1 per track (handoff §2) |
| `Track::admitOverdubStartWorkingSet` (overdub on a boot-cold slot) | `PlaybackWindow` + `LiveCapture` | 1 per track |
| accepted stop's park (S2b.6) | `DeferredFinalization` | 1 per admitted interval (DEC-078) |
| edit/undo/redo/display | `Interactive` | 1 |

The pool's declared caps are the design's: `PlaybackWindowLease` **N derived (8 today)**,
`LiveCaptureLease` **1 per track**, `DeferredFinalizationLease` **per admitted interval**,
`InteractiveLease` **1**, `TransferOverlap` **measured/budgeted**. S3.1 declares them as the measured
envelope; it does not provision new memory for them.

### Oracle fixed before behavior edits

- Identity equality is `slot + generation + role`; a role set is a bitmask so one slot can hold
  `PlaybackWindow | LiveCapture` at once (invariant: classes are composable).
- A release carrying a stale generation is a **rejected, reported no-op** — the vocabulary must return a
  distinguishable outcome, whether or not S4 calls it yet.
- Acquisition beyond a declared cap is **reported** (refused or queued per the S4.4 policy decision); it
  never grows a container on the action edge.
- The pool reports holders per role class, so S4.5's "holder count rising and falling with
  activation/deactivation" is measurable without new instrumentation.

### Controls and coverage

Dependency-free header (`include/Utils/WorkingSetLease.h`) + native suite
(`test/test_working_set_lease`) so the contract is enforced on the host with no device or
`runtimeBySlot_` dependency. Cases: generation-qualified identity mismatch; composable roles on one
slot; independent generations per role class; cap refusal reported without mutation; release of an
unheld identity; total-holder census per class.

Behavior preservation is structural: no resource call moves in this slice, so
`test_playback_prewarm` and the full native suite are the regression gate.

### Completion record (2026-09-25)

Landed `include/Utils/WorkingSetLease.h` — composable `WorkingSetRole`, generation-qualified
identity, `WorkingSetLeaseSlot` (the per-slot lease a `LoopPlaybackRuntime` carries),
`WorkingSetLeaseTable<MaxSlots>` (the multi-slot census S4.1 needs), and declared `WorkingSetLeaseCaps`.
Every operation is O(1) and allocates nothing; `CapReached` / `InvalidRole` / `NotHeld` /
`StaleGeneration` are reported rather than resolved by growing or asserting.

**Wired to the real admission points (behavior unchanged):**

- `Track::prewarmPlaybackForSlot` — after the reserves it already established succeed, the record
  arm records `PlaybackWindow | LiveCapture` on that slot's runtime. `ok` still decides admission;
  no resource call moved, and capture admission is not newly gated on the lease.
- `Track::admitOverdubStartWorkingSet` — the boot-cold overdub-start path records the same set, so
  the two cold admission points cannot disagree about who owns the slot.
- `TrackManager::prewarmPlaybackRuntime` now calls the new `Track::prewarmBootPlaybackRuntimeSlots()`,
  which carries the **same** predicate and the same `prewarmPlaybackForSlot(s, false)` body, so boot
  behavior is identical while the slot's owner holds the admission rule.

**RAM1 accounting (why the lease sits on the per-slot runtime):** the first cut put the table on
`TrackPlaybackRuntime`, which is a by-value member of the 8-way `Track` array, so 16 B there costs
128 B of DTCM. The `sizeof(Track) == 384` pin rejected it — correctly, and it also caught a real
defect in that edit (the cleanup had deleted `runtimeBySlot_`, which the pin reported as the wrong
size). The table is not needed per-track: `LoopPlaybackRuntime` **is** the per-slot working set and is
externally allocated, so the 3-byte `WorkingSetLeaseSlot` member there costs no DTCM.
`sizeof(LoopPlaybackRuntime)` stays 6,344 and `sizeof(Track)` stays 384 (both pins still active). The
capture-serial budget moved `code 391,960 → 391,912` and `padding 1,256 → 1,304`: **no `variables`
growth**, +48 B of code.

**Two contract facts the fixtures corrected:** a composite role set is valid and must not be read as
an undeclared role (the first cut tested the single-class cap lookup against the combined bitmask), and
the generation check must precede the empty-role check in `release`/`releaseAll` — otherwise the
previous holder's late release after reuse is misreported as `NotHeld` instead of `StaleGeneration`,
which is exactly the S4.2 discrimination.

`test/test_working_set_lease` — 11 cases green; full native **2,434 passed / 1 skipped**, including
`test_playback_prewarm`, so the routing is behavior-preserving.

**Still open (S3.1 remainder):** nothing calls `releaseWorkingSetLease` / `releaseAllWorkingSetLeases`
yet — release belongs to S4.1, which is why the generation never advances in this slice and the lease
is currently write-only. S4.1's release should call `releaseAllWorkingSetLeases()` on the slot runtime,
and S4.2's `captureStopOwner` generation should read `workingSetGeneration()`. The S3.2 liveness
predicates are the other half of this task.

## S3.2 — explicit liveness predicates per owner kind (2026-09-25, G2 execution packet)

### Identity and scope

`noLiveOwnerForSlot` was one function with four inline clauses. S3.2 makes each owner kind a **named
term** so a fixture can hold exactly one kind and prove the composite guard is discriminating rather
than merely true, and so a new owner kind is added by name. No term's *meaning* changes: this slice
must not widen or narrow release.

### The owner-kind matrix, audited (read, then named)

| Owner kind | Term | Status |
|---|---|---|
| Admitted / in-flight capture stop job | `Track::hasLiveCaptureStopJobForSlot` — ring live window only | named in S3.2 |
| Slot finalization owner (`captureStopOwner`) | `LoopPlaybackRuntime::ownsLiveFinalization` | named in S3.2 |
| Prepared playback handover **being built** | `LoopPlaybackRuntime::ownsInFlightPreparedHandover` | named in S3.2 |
| Accepted interaction (S4f.1 latch) | `Track::hasAcceptedInteractionForSlot` | named in S3.2 |
| Job-owned undo root outliving its ring entry | — | **cannot be expressed yet**: the protected undo publication is track-level and has no per-slot identity. Arrives with **S5.2a**. |
| Pending deferred reconstruction | — | arrives with **S5.2e**. |
| **Published** prepared pair | deliberately **not** a term | it is the slot's playback authority and stays valid while the slot sounds; counting it would make release unreachable for every sounding slot |

The ring scan stays over the **live window only** (`[head, head + count)`): a retired entry outside it
keeps a stale `slot` (retirement clears `owner`/`loopId`, not `slot`) and must not block release (B6).

### Oracle fixed before behavior edits

Holding exactly one owner kind SHALL make `noLiveOwnerForSlot` false for that slot and true for
another; retiring that kind SHALL make it true again. Two inverse controls are required, because a
predicate that always returns false would pass every positive case: an unowned slot must allow release,
and a slot holding only a **published** pair plus an admitted working set must allow release.

### Controls and coverage

`test_continuous_capture` — `test_s3_2_liveness_predicates_are_discriminating_per_owner_kind`, driving
production state for each kind: the latch through `latchAcceptedStop` + `consumeAcceptedStopObligation`,
the finalization owner through the real record/overdub/stop route, the ring through the admitted stop,
and the in-flight handover through the state that consume path sets (there is no production API that
requests a replacement without a consume miss). Control (b) retires the job through the production
grain drive and asserts release while the working set stays admitted.

### Completion record (2026-09-25)

`Track::noLiveOwnerForSlot` now reads as a list of four named terms
(`hasLiveCaptureStopJobForSlot`, `hasAcceptedInteractionForSlot`,
`ownsLiveFinalization`, `ownsInFlightPreparedHandover`), with the two inexpressible terms recorded
in place with their owning stages rather than dropped. Behavior is unchanged — same terms, same
order of refusal, no resource or release call moved — and the new fixture is `PASSED` in the
continuous-capture suite (101 cases). Full native suite `2,436 passed / 1 skipped` (clean build,
97 suites) — no regressions from the rename. RAM1 `variables` flat (`code 391,912 / padding 1,304`,
unchanged from the S3.1 routing); capture-serial builds.

## S3.3 — selection acquires nothing (2026-09-25, G2 execution packet)

### Identity and scope

The law is `active roles x bounded window`, not `recorded slots x whole-loop state`. Plain selection
and browsing must acquire and release **nothing**, and selection latency must not depend on
working-set availability. S3.3 adds no production behavior: it fixes the observation and proves the
requirement against the production selection mechanism.

### The observation (no new state)

The lease mutates only through `acquire`/`release`, so a slot whose held role set stays
`WorkingSetRole::None` and whose generation stays 0 across a browse is a **no-churn witness**. No
counter is needed. The first cut added acquire/release churn counters to `WorkingSetLeaseSlot`; the
`sizeof(LoopPlaybackRuntime) == 6,344` pin rejected it, and re-pinning to grow the per-slot runtime
purely for test observation is the wrong trade for this change. The counters were removed and the
fixture reads the existing `workingSetRoles()` / `workingSetGeneration()`.

### Oracle fixed before behavior edits

Browsing many recorded slots SHALL leave every slot's role set `None` and generation 0, and SHALL
leave no slot runtime allocated (selection cannot depend on working-set availability). A positive
control SHALL show the cold admission boundary setting the roles, and a repeated admission SHALL
leave the held set unchanged. Without the control, a witness that can never change would pass.

### Controls and coverage

`test_continuous_capture` — `test_s3_3_selection_and_browsing_acquire_no_working_set`: three
committed slots, three browse passes through `SlotStateMachine::setSelectedSlotIndex` (the mechanism
the UI gesture ends in), then the role-set/generation/runtime assertions per slot; the control is the
record arm on one slot plus a repeated admission.

**Boundary, recorded not worked around:** the *manager* entry `TrackManager::setSelectedSlotIndex`
also calls `StorageManager::prioritizeLoopSlotRestoreForFocus`, which is a fail-closed off-path stub
in this suite — the same boundary `test_stop_in_flight_refuses_the_move_to_a_new_slot` already
records. The storage half of a switch is owned by the load/restore suites; the selection half is this
requirement.

### Completion record (2026-09-25)

Fixture green; `test_continuous_capture` 101/101. Full native suite `2,436 passed / 1 skipped` (clean
build, 97 suites — 4 suites are in the native `test_ignore` list). RAM1 byte-identical to the S3.1
routing (`variables 96,160 / code 391,912 / padding 1,304 / locals 34,912`), so this slice costs
nothing in firmware. No production line changed.

## S3 gate — acceptance suite landed; the gate itself is not closed (2026-09-25)

`test_runtime_working_set_ownership` now exists and is linked as a firmware-boundary suite
(`scripts/native_test_link.py`, the continuous-capture harness's measured stub set). It is the stage's
acceptance over the **production admission paths**, deliberately not a restatement of the unit suites
(`test_working_set_lease` keeps the lease arithmetic; `test_continuous_capture` keeps the per-owner-kind
matrix and the selection fixture):

1. **Boot/load is an admission boundary** — a committed slot leaves `prewarmBootPlaybackRuntimeSlots()`
   holding no working set, and the *same* slot gains `PlaybackWindow | LiveCapture` at the record arm.
   Asserting both on one slot is what makes the absence meaningful.
2. **A repeated admission does not churn** — the held set and the generation are unchanged, because the
   lease is identity-qualified.
3. **Release is guarded by liveness** — an accepted interaction refuses release and frees it when
   consumed; an in-flight prepared handover refuses and frees it when retired; a stop admitted through
   the production entry (interval opened by `startOverdubbing`, then
   `beginDeferredOverdubStopForSlot`) refuses and, once driven to retirement through the production
   grain path, frees the slot.
4. **Selection acquires nothing** — browsing leaves every slot's role set `None`, generation 0 and no
   runtime.

4 cases green; full native suite `2,440 passed / 1 skipped` (98 suites — 4 are in the native
`test_ignore` list). Firmware RAM1 byte-identical (`variables 96,160 / code 391,912 / padding 1,304 /
locals 34,912`): the suite is test-only.

**What still blocks the gate, named rather than implied:**

- **S2b gates** — S2b.3's post-restore half, S2b.5's device gate and the transferred S2b.7 device legs are
  device measurements, not native work.
- **S2.2/S2.3 current-root budget** — per-class bytes (`P` playback window, `C` live-capture extension,
  `D` deferred, derived root hours) are device measurements and remain open.
- `test_playback_prewarm` **unchanged** — ✅ verified in every run of this stage.

One honest limit inside the suite: the stop-admission case asserts through
`beginDeferredOverdubStopForSlot` with an interval opened by `startOverdubbing` rather than the MIDI
button route, because the button route drags the note-edit input layer into this boundary — the same
choice `test_storage_manager_load` documents. The ring term is additionally held alone by
`test_continuous_capture`'s matrix.

## S4.1 — release when the slot leaves the active set (2026-09-25, G2 execution packet, first slice)

### Identity and scope

The release surface named by S4.1 is track-global and predicate-free: `TrackPlaybackRuntime::resetAll`
and `clearAllLedgers` loop every **allocated** slot, discarding each slot runtime's `reset()` refusal.
The first slice fixes the reachable corruption the acceptance criterion names — *B-stop while A
finalizes on the same track → A's ledger and canonical inputs survive* — and leaves the memory-pressure
conversions (which need a fixture this change has not built) to the same task's remainder.

### Audit of the call sites (read, then classified)

| Site | Scope it operates on | Keep / convert |
|---|---|---|
| `Track::sendAllNotesOff` (global transport stop) | every slot, deliberately — the transport silences the whole track | **keep track-global** |
| `Track::handleNoteEditFold` `endInPlaying` (in-edit fold completing *this interval's* stop) | one interval, on its own slot | **convert** |
| `Track::stopOverdubbing` fallback (`endInPlaying == false`) | one interval, on its own slot | **convert** |
| `Track::stopOverdubbingToStopped` | one interval, on its own slot | **convert** |
| `Track::releasePlaybackMergedMidiEventsMemory` → `resetAll(false)` | every slot, under memory pressure | **remainder** — needs the memory-pressure fixture |
| `Track::invalidatePlaybackMergedMidiEvents` → `resetAll(preserveLedger)` | every slot, guarded only by `hasPendingDeferredOverdubStop` | **remainder** — same fixture |
| `DeferredSaveJobStages` footer save reads `getActiveLoopIndex()` | one interval | **remainder** — needs the persistence fixture |

The stop paths are the corruption: while a predecessor finalizes on one slot, a sibling slot's stop
clears **every** allocated slot's ledger, erasing the predecessor's open-note suppression state. The
canonical inputs are not touched by the ledger clear itself; they are released by
`tryReleaseCanonicalCaptureReservation`, which already evaluates `noLiveOwnerForSlot` (S2b.4).

### Oracle fixed before behavior edits

Stopping one interval SHALL clear only its own slot's ledger. A sibling slot that still holds a live
finalization owner SHALL keep its ledger and its canonical inputs. The global transport stop SHALL
still clear every slot's ledger (the control that the conversion did not narrow a deliberate sweep).

### Controls and coverage

Fixture in `test_runtime_working_set_ownership`: slot 1 becomes a finalizing predecessor (production
interval start + production stop admission) with a marked ledger entry; slot 0 then takes a stop
through the production route; the fixture asserts slot 1's ledger entry and canonical admission
survive while slot 0's own ledger is cleared. Control: `sendAllNotesOff()` clears both.

### Completion record (2026-09-25)

`Track::clearStoppingIntervalLedger()` is the named per-interval owner: it clears **only** the
stopping interval's slot ledger. Both converted stop routes call it — `handleNoteEditFold`'s
`endInPlaying` branch and `stopOverdubbing`'s fallback — and `Track::sendAllNotesOff` keeps the
deliberate track-global sweep, which now loops the per-slot `LoopPlaybackRuntime::clearLedger()` so the
sweep has one implementation.

**The fixture was red-proven.** The first version drove `stopOverdubbing()` on an ordinary (non-edit)
interval and passed even with the fix reverted — the ordinary route takes the **deferred admission**
path, so neither converted site ran. That is a false green, and it is recorded here rather than
quietly replaced. The fixture was retargeted at the named owner, and with the scoping reverted to
`playbackRuntime.clearAllLedgers()` it fails exactly as intended: *"a sibling slot's ledger must
survive the stop"*. With the fix it passes, and the control (the track-global sweep) still clears both
slots.

Fixture `test_a_stop_clears_only_its_own_slots_ledger` — green, red on the pre-fix behavior. Full
native `2,441 passed / 1 skipped`; capture-serial builds with `variables 96,160` unchanged and
`code 391,912 → 391,960` / `padding 1,304 → 1,256` (the named function; 416 B of headroom remains).

**Open remainder of S4.1 (unchanged by this slice):** `releasePlaybackMergedMidiEventsMemory` and
`invalidatePlaybackMergedMidiEvents` still call the track-global `resetAll` and still discard its
per-slot refusal — the memory-pressure conversions, which need their own fixture;
`DeferredSaveJobStages`' footer save still reads `getActiveLoopIndex()`; and the
`(slot, generation)` release entry with the generation carried onto `captureStopOwner` (S4.2) is not
built yet, which is why the lease's `releaseWorkingSetLease`/`releaseAllWorkingSetLeases` remain
uncalled.

## S4.2 — generation-qualified release and finalization identity (2026-09-25, G2 execution packet + record)

### Identity and scope

`captureStopOwner` was a raw `PendingStopAdmission*` cleared by **address identity**
(`if (runtime.captureStopOwner == &stop)`). Ring entries are reused at **stable addresses**, so a
predecessor's late cleanup could null a successor's marker on the same runtime object. S4.2 qualifies
that identity by the owner's generation.

### What was built

- `LoopPlaybackRuntime::captureStopOwnerGeneration` records the identity alongside the pointer, and
  `setCaptureStopOwner(admission, generation)` / `clearCaptureStopOwner(admission, generation)` are the
  only writers. The clear is address- **and** generation-qualified; a mismatch is a rejected no-op.
- Set site: `beginDeferredOverdubStopForSlot` now records `job.admission.frozenGeneration` (the
  interval identity the admission already carries).
- Clear site: `TrackPreparedStopCommit::completeDeferredCleanup` passes `stop.frozenGeneration`, so the
  cleanup only clears the owner it actually holds.

**Cost: none.** The field fit the struct's existing padding — `sizeof(LoopPlaybackRuntime)` stays
**6,344** and the capture-serial RAM1 line is byte-identical (`variables 96,160 / code 391,960 /
padding 1,256 / locals 34,912`).

### Oracle fixed before behavior edits

A cleanup presenting a different generation SHALL be rejected and SHALL leave the live marker intact;
a cleanup presenting the matching identity SHALL clear it; a repeat of the same identity SHALL be a
no-op. The middle case is the control that keeps the rejection from being a clear that never works.

### Coverage

`test_runtime_working_set_ownership` — `test_a_late_cleanup_cannot_clear_a_successor_finalization_marker`:
a real owner established through the production admission, then a stale-generation cleanup (rejected,
marker intact), then the matching identity (cleared), then the same identity again (no-op).
**Red-proven:** with the generation check removed the fixture fails on *"a generation-mismatched
cleanup must be rejected"*.

Full native `2,442 passed / 1 skipped`; capture-serial builds.

### Not yet done in S4.2 — named

The per-slot **runtime** generation is still not bumped on release. `WorkingSetLeaseSlot` supports it
(`releaseAll` bumps), but calling `releaseAllWorkingSetLeases()` at the stop edge would drop a
finalizing interval's lease, which is exactly what must not happen; it belongs with S4.1's
`(slot, generation)` release entry once "the slot leaves the active set" is expressed for the release
path. `captureStopOwnerGeneration` is therefore a self-contained identity, not yet an alias of the
lease generation. The lease's `releaseWorkingSetLease` / `releaseAllWorkingSetLeases` remain uncalled
for that reason.

## S4.1/S4.2 — the `(slot, generation)` release entry (2026-09-25, G2 execution packet + record)

### Identity and scope

`TrackPlaybackRuntime::resetAll` was predicate-free and discarded every slot's refusal. The
`(slot, generation)` release entry replaces it, so release is identity-qualified and its refusals are
reported rather than swallowed.

### What was built

- **`TrackPlaybackRuntime::releaseSlot(slot, generation, preserveLedger)`** — the single release entry.
  It reports `NotHeld` (out of range / no runtime / nothing to release), `StaleGeneration` (the identity
  does not match), `RefusedBusy` (the slot runtime's own `reset()` refused because accepted capture
  still owns its scratch), or `Released`.
- **`TrackPlaybackRuntime::slotGeneration(slot)`** — the identity a caller must present.
- **`Track::releaseEligiblePlaybackRuntimeSlots(preserveLedger)`** — the group operation: for each slot,
  skip unless `noLiveOwnerForSlot(slot)`, then call the entry with that slot's generation.
- `Track::releasePlaybackMergedMidiEventsMemory` and `Track::invalidatePlaybackMergedMidiEvents` (the
  two sites S4.1 names) now call it. `TrackPlaybackRuntime::resetAll` was **deleted** — it had no
  remaining callers, and leaving a predicate-free release surface would invite its reuse.

**No new state.** The generation is the slot's existing working-set lease generation
(`WorkingSetLeaseSlot`, S3.1), so `sizeof(TrackPlaybackRuntime)` stays **32** and
`sizeof(LoopPlaybackRuntime)` stays **6,344**. The capture-serial RAM1 line is byte-identical
(`variables 96,160 / code 391,960 / padding 1,256`).

### Oracles, both red-proven

| Fixture | Assertion | Red proof |
|---|---|---|
| `test_the_slot_generation_release_entry_is_identity_qualified_and_preserves_refusals` | a stale generation is a rejected no-op and keeps the held set; the matching identity releases and advances the generation; the previous identity then cannot release the reused slot; unheld and out-of-range report `NotHeld` | with the identity check removed: *Expected 5 Was 1 — a stale generation must be a rejected no-op* |
| `test_the_release_sweep_skips_a_slot_with_a_live_owner` | the sweep leaves a latched slot's generation and working set untouched, and releases it once the latch is consumed (control) | with the liveness gate removed: *Expected 0 Was 1 — the sweep must not release a slot with a live owner* |

Both fixtures were **first run invalid**: a failed `cd` in one shell call left a stale working
directory, so the edit script silently wrote nothing and the "probe" tested an unmodified tree. The
red proofs above were re-run with absolute paths, and the file state was verified after each restore.
Recorded because a probe whose edit silently no-ops is exactly how a false green survives.

Full native `2,444 passed / 1 skipped`; capture-serial builds.

### Still open

- `DeferredSaveJobStages`' footer read (`getActiveLoopIndex()`) — needs the persistence fixture.
- The release entry is now callable and called, but **no caller has yet been added for the
  activation/deactivation sweep S4.3/S4.4 describe**; S4.1's remaining sites are the persistence reads.
- `captureStopOwnerGeneration` (S4.2) and the lease generation remain separate identities: the lease
  generation advances only on release, so a finalizing interval keeps its lease until its slot is
  actually released. Wiring them into one alias is not required by any current caller.

## S4.3 — reactivation reuses released capacity (2026-09-25, G2 execution packet + record)

### What the reuse actually is (read, then asserted)

`releaseSlot` retires a slot's **working set** — it does not free the slot's runtime object:
`TrackPlaybackRuntime::runtimeBySlot_` holds the `LoopPlaybackRuntimePtr`, and only the runtime's
destructor/deleter frees it. So "reuse" is **capacity retained at a stable address**: the next
activation of that slot re-admits its working set into the same object, and the per-slot state
(generation, roles) advances rather than accumulating.

### Oracle and control

Four release/re-activate cycles on one committed slot SHALL each return to the baseline state: the
lease unheld after the sweep, the slot generation advanced, the **same runtime object address** on
re-admission, the same role set, and the same `LoopEventStore::freeChunkCount()`. A control activates
the slot once before the cycles, because "no growth" is only meaningful against a real working set.

The fixture is `test_reactivating_a_released_slot_reuses_its_capacity` in
`test_runtime_working_set_ownership`.

### Red proof

With the sweep disabled (`releasePlaybackMergedMidiEventsMemory` a no-op) it fails on *"the sweep must
release the slot's working set"*, and the companion S4.1 fixture fails on *"the sweep must release a
slot with no live owner"*. Restored and re-verified.

**Measurement limit, recorded rather than papered over:** the native build has no PSRAM, so
`extmem_malloc` is a no-op there and `ExternalPoolAccounting` cannot report external bytes. This
fixture therefore observes reuse **structurally** (object identity, generation, role set, chunk pool),
not as a PSRAM byte delta. The byte-level claim on device remains a device measurement, like the S2.2/S2.3
budget items.

### Production caller

The entry has a live caller today: `TrackManager::startRecordingOnTrack` →
`releaseBackgroundPlaybackMergedMidiEventsMemory` sweeps every **other** track when a take starts, which
is exactly the role transition that should return background capacity. The sweep is also driven from the
memory-pressure path (`Track::tryReleasePlaybackMergedMidiEventsMemory`). What is **not** built is an
explicit sweep on a same-track slot switch — recorded here as S4.3's remaining integration rather than
implied done.

Full native `2,445 passed / 1 skipped`; capture-serial builds with RAM1 byte-identical.

## S4.4 — declared cap policy: reported, not enforced stricter than the model (2026-09-25)

### What I built first, and why it was wrong

The first cut enforced the declared caps at the activation entry: `Track::admitsEnvelopeRole` refused
once a class was held at its declared cardinality. It was red-proven and it broke the suite —
**two fixtures failed, and the shared `preparePlayingTrack()` change then failed nine more**. The gate
was refusing a second `LiveCapture` role on a track, which the declared envelope (design.md) says is one
per track.

That is the useful finding: **the declared envelope is stricter than the resource model.** The
implementation supports per-slot capture roles, and 85 fixtures in the continuous-capture harness arm a
second slot. Enforcing the declaration would reject supported flows, so the enforcement was wrong — not
the fixtures. The fixtures were reverted; only the observation stayed.

### What is in place now

- `Track::reportEnvelopeBreachIfAny(slot, role)` — read-only, **no storage**, driven from the activation
  entry before resources are taken. When a class is already held at its declared cap it reports
  `#MEM_DENIED,op=…,reason=cap_reached,class=N` through the new
  `MemoryMonitor::reportDeclaredCapDenied`, which is deliberately a **separate reason from an allocation
  failure** (`prewarm_failed`): "we exceeded a declared cardinality" and "the pool ran out" are different
  facts and must not be read as each other.
- The activation **proceeds**. The enforcing caps remain the lease's own (`WorkingSetLeaseTable`), which
  refuse at the point of custody and are covered by `test_working_set_lease`.
- `class=N` is a role-class index, not a name: the device `printf` path takes no `%s`.

Fixture `test_a_declared_envelope_breach_is_reported_and_does_not_block_activation`: the first activation
is admitted with no breach; a second live-capture role on the track is admitted **and** both slots really
do hold the class, so the state the envelope calls a breach is established rather than asserted away.

**Honest limit:** the report itself is serial output, so the fixture cannot assert the emitted line on
the host — it asserts the policy path is taken and does not block, and the emitted reason is a device
observation.

### What S4.4 has not settled

The refuse-vs-queue question is **not** answered by this slice: nothing is refused, so nothing is queued.
Deciding it requires either a declared cap the resource model also enforces (which today it does not) or
a change to the per-track cardinality, and both are architecture decisions rather than implementation.
Recorded as the task's open remainder instead of being implied resolved.

Full native `2,446 passed / 1 skipped`; capture-serial builds with RAM1 byte-identical.

## S4b.1 — role transfer: retracting the S4.4 conclusion, and the real blocker (2026-09-25)

### Correction to S4.4

S4.4 concluded that "the declared envelope is stricter than the resource model" because enforcing a
one-live-capture-per-track cap broke fixtures that arm a second slot. **That conclusion was wrong.**
The check counted `PlaybackWindow | LiveCapture` for a slot that was merely *prepared*; a slot holding an
**outgoing finalizer** was therefore labelled `LiveCapture` and counted against a live-capture cap. The
mislabel was in the check. `LiveCapture` on two prepared slots does not prove the one-live-capture
contract is wrong — the outgoing role is `DeferredFinalization` (per admitted interval, DEC-078), not a
second live capture. The required behavior is an **outgoing finalizer plus an incoming live capture**,
which the contract wants; two live captures on one track were never required. The cap policy remains
**reported, not enforced** (S4.4), but its justification is now the mislabeled census, not a claim that
the envelope is unsatisfiable.

### The acceptance requirement, executable

`test_switching_starts_the_new_take_while_the_previous_take_still_finalizes` in
`test_runtime_working_set_ownership` drives the exact gesture: A overdubbing on slot 0 through
`midiButtonActions.handleToggleRecord` + the real MIDI input route → A's stop admitted and **deliberately
not advanced** (finalizer paused) → switch to B → B must start and record MIDI while A still finalizes.
It asserts through `TrackManager::finalizeCaptureAndSelectSlot` (the production switch) and asserts B's
recorded events, not a prewarm result.

**Result: S4b.1's requirement is NOT met, and the reason is a carried prerequisite.**

The switch path returns early while `hasAcceptedUnexecutedInteraction()` or `hasStopInFlight()`, so the
active slot stays on A and B cannot start. That guard is deliberate and documented: S4f.5 requires that
the active slot not move under an **unadmitted** stop
(`test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes`), and `IMPLEMENTATION_CONTRACT.md`
§1 carries the guard until **S4f.3** supplies the switch's accepted-interaction carrier and commits the
switch after the stop binds.

Removing the guard was tried, verified to make the acceptance pass, and then **reverted**: it fails
`test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes`, i.e. it lands S4b by breaking a
requirement S4b depends on. S4b.1 therefore needs S4f.3 first.

The fixture carries both modes:

- default gate — asserts the **current** characterization (switch holds the active slot; B has not
  started), so the suite stays green and the prerequisite is a recorded fact;
- `S4B_ACCEPTANCE=1 pio test -e native -f test_runtime_working_set_ownership` — runs the requirement
  itself, currently **red** with the exact reason. The branch is condition-guarded, so the moment S4f.3
  lands the acceptance assertions run automatically.

### Still to do for S4b, in order

1. **S4f.3** — the switch's accepted-interaction carrier (the prerequisite above).
2. Then the remaining S4b cases, none of which is proven by this slice: the **cross-track** switch;
   **repeated switches B → C while A still finalizes**, which needs the explicit bounded overlap policy
   (`Omax = NUM_TRACKS` inbound holders; "an outbound live/finalizing role is already counted in C/D and
   is never released to satisfy O") rather than an assumed-unbounded accumulation.
3. The finalizer-pause lever is currently "admit the stop and do not call the grain drive". A
   discriminating fixture that pauses a *specific* finalization phase would make the assertion tighter.

Full native `2,447 passed / 1 skipped`; capture-serial builds with RAM1 byte-identical. No production
behavior changed in this slice (the switch-guard edit was reverted); the deliverable is the executable
requirement plus the corrected S4.4 attribution.

## S4b.1 — fixture tightened after review (2026-09-25, second pass)

The review found the first fixture overstated what it proved. Three qualifications were correct and are
fixed:

1. **It did not prove A had an *admitted* finalizer.** Its precondition accepted an
   accepted-but-unexecuted stop. The acceptance case now asserts
   `hasPendingDeferredOverdubStop` **and** asserts `!hasAcceptedUnexecutedInteraction`, and the
   unadmitted stop is **separate coverage** — waiting for admission is a different requirement from
   admitted-and-paused.
2. **B bypassed the user-facing routes.** The fixture called `startOverdubbing` and
   `loop.appendCaptureEvent` directly. It now drives `MidiButtonActions::handleToggleRecordForSlot`
   (the normal per-slot gesture, which finalizes A's capture, commits the switch and starts an empty
   target) and sends input through `midiHandler.handleMidiMessage`, so the claim is that the **gesture
   and incoming MIDI reach B**, not that a buffer accepts an append. The target is the requested
   **empty slot**.
3. **"Flips automatically" was not a durable gate.** The characterization branch is documented as
   something to be **deleted** when S4f.3 lands, not made conditional again, so a later regression to
   refusing the switch cannot pass by returning early.

### What the tightened fixtures now establish

- **Admitted-and-paused finalizer (`test_switching_starts_the_new_take_while_the_previous_take_still_finalizes`):**
  still **red** under `S4B_ACCEPTANCE=1` — the switch holds the active slot while the stop is in flight,
  so B never starts. The requirement is unmet and blocked on **S4f.3**.
- **Unadmitted stop (`test_an_unadmitted_stop_retains_the_switch_and_start_requests`):** **PASSES**, and
  that is a real result rather than a skip: with the stop merely accepted-and-unexecuted, the switch and
  start are **retained** and execute once admission becomes possible, **without a second gesture**. The
  user's requirement for that case already holds; it is now pinned by a fixture instead of assumed.

### Cost of driving the production route

Reaching B through the gesture crosses four boundaries the harness kept fail-closed. They are storage
and display boundaries, not the role handoff, so they are observable for suites that drive the switch:
`StorageManager` persistence requests (`markLoopSlotMaterialDirty`, `admitLoopSlotPersist`,
`requestDeferredSaveState`, `requestWorkspaceFooterPersistWhenSafe`), SD payload visibility/refresh
(`hasLoopSlotPayloadOnSdInRam`, `refreshLoopSlotPayloadOnSdInRam`), the restore-focus hook, and
`EditManager`'s selected-slot hooks. The edit-**depart** closure was tried as a real link and rejected:
it pulls the whole note-edit machinery (reopen/persist/display-refresh) for a no-op when no session is
open, so the observable stub is the honest choice and is documented as such.

### Still open, unchanged

- **S4f.3** — the accepted-interaction carrier. It is the blocker for the admitted-finalizer acceptance
  and therefore for S4b.1.
- **Cross-track switch** — not covered.
- **Repeated B → C while A finalizes** — the user's correction is recorded: `Omax` (inbound preparation
  overlap) does **not** by itself solve this; it also depends on **finalizer capacity**. Kept explicitly
  open rather than folded into the overlap policy.

Full native `2,448 passed / 1 skipped`; capture-serial builds with RAM1 byte-identical. No production
behavior changed: the deliverable is the tightened, honest fixture plus the finding that the unadmitted
case already holds.

## S4f.3 — accepted-switch handling: the admitted-stop half landed; carrier dispatch open (2026-09-25)

### What changed

`TrackManager::finalizeCaptureAndSelectSlot` no longer refuses to move the active slot when the interval's
stop is **admitted**. The removed branch was the `hasStopInFlight()` early return; the
`hasAcceptedUnexecutedInteraction()` guard is **kept**, because S4f.5 requires the active slot not to move
under an *accepted-but-unexecuted* stop whose interval slot the latch has not yet bound.

The removal is safe because the deferred job already carries its own identity:
`Track::processDeferredOverdubStopGrain` finalizes from `Loop& loop = *job.owner` and
`playbackRuntime.slotIfAllocated(job.slot)`, with an explicit guard
(`&getLoop(job.slot) != job.owner`) and an `ownsTrackState` check that merely reports whether the job still
owns the *active* slot. Finalization therefore no longer depends on the active slot staying on the
predecessor.

### Test consequence, asserted not assumed

`test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes` asserted the old rule — that the
admitted job still holds the active slot. That assertion was **obsolete under S4f.3**, and its own comment
already recorded that the positive half ("a switch *does* move once nothing is in flight") could not be
asserted in that suite. It now asserts the corrected rule: the **unadmitted** case still refuses the move
(unchanged first assertion), and once the stop is admitted the move happens and the finalization completes
from the job's own identity while the active slot sits on the successor. 101/101 green.

`DisplayManager::invalidateForSlotChange` and `EditManager`'s selected-slot hooks are observable in the
continuous-capture and ownership harnesses (`HARNESS_OBSERVABLE_SLOT_CHANGE`) because a committed switch
now reaches them; they are display/edit boundaries, not the handoff.

### Still open — the carrier's dispatch half

**B still does not start.** With the switch committing, the per-slot gesture
(`MidiButtonActions::handleToggleRecordForSlot`) returns immediately after
`finalizeCaptureAndSelectSlot`:

```cpp
if ((track.isRecording() || track.isOverdubbing()) && previousSlot != slotIndex) {
    trackManager.setSelectedSlotIndex(trackIdx, slotIndex);
    trackManager.finalizeCaptureAndSelectSlot(trackIdx, slotIndex, now);
    logger.info("Loop %d: Switched from slot %d (capture finalized)", …);
    return;
}
```

So the requested start is never dispatched to the target. S4f.3's remaining half is exactly what its task
text specifies: the `SlotStateMachine` pending switch is the authoritative carrier and **dispatch at the
switch's commit point** replaces the unconditional pending-switch clear in `TrackManagerTransportTick`.
Until that exists, the acceptance sequence needs its documented second gesture — matching the user's own
phrasing ("switch to B, then start B") — and the fixture's start is currently not carried across the
handoff.

The fixture carries both modes: the default gate asserts the current characterization (switch commits,
start not yet dispatched) and `S4B_ACCEPTANCE=1` runs the requirement, red with that reason.

### Remaining S4f.3 work, named

1. A `Switch` latch that identifies its prerequisite stop (not a second target owner).
2. Dispatch of the retained switch (and the start bound to it) **after the interval's stop binds**.
3. Replacement of the unconditional `clearPendingSlotSwitch` in `TrackManagerTransportTick` with
   consumption at successful switch commit.
4. Fixture: queued switch + undo + stop admission in one window → the target is never lost.

Full native `2,448 passed / 1 skipped`; capture-serial builds with RAM1 byte-identical.

## S4f.3 / S4b.1 — root cause of "B does not start" (2026-09-25, third pass)

The switch now commits (S4f.3 first half) and the fixture drives the production routes, so the remaining
failure is isolated to one mechanism rather than a suspected carrier gap.

### The observation

```
B did not record the note: recording=0 overdubbing=1 captureActive=1 size=0 loopLen=0 active=1
```

B is **overdubbing with `loopLengthTicks == 0`**, and the note is not captured.

### The mechanism, from the code

1. `startOverdubbing` opens with a guard on the **track** state and the **active** loop's phase:
   ```cpp
   Loop& loopRef = getActiveLoop();
   if (trackState == TRACK_OVERDUBBING && loopRef.capture.phase == CapturePhase::Overdub) {
     return;   // already overdubbing -- does nothing for the successor
   }
   ```
2. The switch commits the active slot to the empty target, but the track's state remains
   `TRACK_OVERDUBBING` from the predecessor, and the successor's capture phase is `Overdub`.
3. The start gesture's overdub branch (`handleToggleRecord` → `track.isOverdubbing()` →
   `stopOverdubbing()` in `handleToggleRecordForSlot`) therefore never routes the empty target through
   the **record** path that establishes `loopLengthTicks`.
4. With `loopLen == 0` the capture append has no loop to append into, so `size` stays 0 and the note is
   dropped silently.

### Why this is a state-coherence defect, not a carrier gap

An overdub interval **needs an existing loop to overdub onto**. After a handoff to an *empty* target the
track cannot still be "overdubbing" in a way that describes the successor: the predecessor's finalization
is carried by the deferred job (`job.slot`, the latch, `captureStopOwner`), not by the track's live state
flag. So `trackState == TRACK_OVERDUBBING` must not survive to describe an empty successor, and the
successor's record admission must be the **record** route.

### Next change (not yet made)

On switch commit, when the target has no committed content and no live interval, the track must leave the
overdubbing state so the successor routes through record admission and establishes its length. Candidate
sites, to be decided against the contract rather than guessed:

- `TrackManager::finalizeCaptureAndSelectSlot` after the switch commits, or
- `Track::startOverdubbing`'s guard, which currently returns silently instead of reporting that the
  requested successor is empty (a silent no-op is what made this expensive to find).

The existing S4f.5 refusal (accepted-but-unexecuted stop holds the active slot) and the S4f.3 first half
(admitted stop releases it) must both stay intact.

Status: default gate 12/12 (characterization records the above); `S4B_ACCEPTANCE=1` red with the exact
state; full native 2,448 passed / 1 skipped; capture-serial RAM1 byte-identical.

## S4f.3 / S4b.1 — the successor is left in an Overdub capture session (2026-09-25, fourth pass)

### The decisive datum

```
B did not record the note: recording=0 overdubbing=1 captureActive=1 size=0 loopLen=0 active=1
  beforeTick=768 beforeLoopLen=0 onTick=776 phase=2 hasOverdubSession=1
```

`phase=2` is `CapturePhase::Overdub` and `hasOverdubSession=1`, on a slot whose `loopLengthTicks` is
**0**. So an actual overload-session start ran on the successor.

Only two production sites set `capture.phase = CapturePhase::Overdub`:
`TrackOverdubLifecycle.cpp:1671` (the tail of `startOverdubbing`) and `TrackOverdubWrap.cpp:101`
(the overdub wrap). `TrackManagerCaptureQueue.cpp:223` additionally reads
`track.isOverdubbing() && loop.capture.phase == CapturePhase::Overdub`. So `startOverdubbing` ran on the
successor and reached its session start, which establishes an overdub session on a loop that has no
length — the empty-slot contradiction.

### What this rules out

The stale `TRACK_OVERDUBBING` is **not** the root cause on its own: I implemented the state clear in
`finalizeCaptureAndSelectSlot` (empty successor → leave the live-capture state), confirmed S4f.5 stayed
green (101/101), and the acceptance state was **byte-identical afterwards**. It cost 48 B of code, so it
was reverted. The successor's overdub session is therefore established **after** the switch, by a route
that has not yet been identified.

### Next probe (targeted)

Find which of the two `beginCapture(CapturePhase::Overdub, …)` sites executes on the successor:

- if it is `startOverdubbing`'s tail, the caller that reaches it is the one to fix: for an empty active
  loop the start must take record admission, which establishes the length first;
- if it is the wrap path (`TrackOverdubWrap.cpp:101`), the successor is being treated as a wrapping overdub
  of a loop it does not own, which is a different defect again.

The guard at `TrackOverdubLifecycle.cpp:1552` now logs when it is asked to overdub an empty active loop
(previous pass), so the log distinguishes these two without further instrumentation.

Status: default gate 12/12; `S4B_ACCEPTANCE=1` red with the datum above; full native 2,448 passed /
1 skipped; capture-serial RAM1 byte-identical.

## S4f.3 / S4b.1 — the zero-length overdub is refused; the carrier dispatch is what remains (2026-09-25, fifth pass)

### Milestone measurement that settled the fork

The probe now records the state **at the switch commit**, before the start step. The fork I was choosing
between last pass is closed:

```
afterSwitch phase=0 session=0 loopLen=0        <- the successor is clean at commit
... later ...
phase=2 hasOverdubSession=1 loopLen=0          <- and in an Overdub session afterwards
```

The switch therefore does **not** establish the successor's overdub session, and neither does the wrap
path. It is the start route: `TrackManager::startOverdubbingTrack` had **no validation of the target** --
it read `track.getActiveLoop()`, checked only "already overdubbing", and called
`track.startOverdubbing()`. On an empty successor that opens an overdub session on a loop with no length,
and the capture append then has nothing to append into, which is exactly `size=0`.

### Landed: the refusal

`TrackManager::startOverdubbingTrack` now refuses an overdub start when the active loop's
`loopLengthTicks == 0`, with a logged reason and the slot. An overdub needs an existing loop to overdub
onto; a zero-length loop is a target that needs **record** admission, which is what establishes the
length. The refusal cannot reject a legitimate flow, because such a session can never accept an event --
and the full native suite confirms it: **2,448 passed / 1 skipped**, no regression.

This is the second diagnosability improvement in the same area: the first made `startOverdubbing`'s
empty-active-loop guard log instead of no-op; this one stops the invalid session being opened at all, one
level up, where the target is chosen.

### What remains

The acceptance sequence still does not complete: after the refusal, the state is
`playing=1 overdubbing=0 recording=0 pendingStop=1 active=1` -- the switch committed (active on B), A's
finalizer is still in flight, and B is no longer *incorrectly* overdubbing, but B has **not started
recording**. So the remaining work is the route selection at the caller: for an empty successor the start
must go through record admission (which establishes the length) rather than being attempted as an overdub
and refused. That is the S4f.3 carrier's dispatch half, now with a clean invariant underneath it instead
of a silent contradiction.

Status: default gate 12/12; `S4B_ACCEPTANCE=1` red with the state above; RAM1 `variables` unchanged,
`code 391,960 -> 392,024` / `padding 1,256 -> 1,192` (the refusal and its log; 352 B of headroom).

## S4f.3 / S4b.1 — the target's focus is the missing link; the start still does not dispatch (2026-09-25, sixth pass)

### Finding: the gesture acts on the *selected* slot, which the switch deliberately leaves behind

With the zero-length overdub refused, the state after the fixture's start step was

```
playing=1 overdubbing=0 recording=0 pendingStop=1 active=1 selected=0
```

`active=1` (the switch committed and the audio moved to B) but `selected=0` -- the switch **keeps focus on
the predecessor** on purpose (`setSelectedSlotIndex(trackIndex, captureSlot, SyncPlayback::No)` in
`finalizeCaptureAndSelectSlot`, so the piano roll and LEDs stay on the capture that was just taken). The
arm/record entry (`MidiButtonActions::handleToggleRecord`) and the record-arm branch both act on the
**selected** slot, so the start was being aimed at A, not at the active target B.

The fixture now selects the active target before starting, which is what a user does when the switch moved
the audio to B. State afterwards:

```
playing=1 overdubbing=0 recording=0 pendingStop=1 active=1 selected=1
```

Coherent -- no false overdub session, focus on the target -- but **B still has not started recording**.
So the refusal removed the contradiction and the focus fix removed the mis-targeting, and what is left is
the dispatch itself.

### Where the remaining work is

The start reaches the record-arm path but no recording begins. The candidates, in order:

1. **`handlePendingRecordStart` phase/ref-slot condition.** It gates on
   `pendingRecordRefSlot` against the reference loop's `loopLengthTicks`/`startLoopTick`; after a handoff
   the reference is the predecessor, so the boundary mathematics may never satisfy `shouldStart`.
2. **The queued start not surviving the handoff** -- `pendingRecordSlot`/`pendingRecord` may be set for a
   slot that the switch's bookkeeping clears (`finalizeCaptureAndSelectSlot` clears `pendingRecord`,
   `pendingRecordQueuedAtTick` and every `pendingRecordSlot` for the track).
3. The record-arm branch requiring a state the successor does not have.

Candidate 2 is the most suspicious given the code read: the switch clears exactly the per-slot record
bookkeeping that would carry the requested start to the successor, which is the "retain the requested
switch and start" half of S4f.3's specification.

Status: default gate 12/12; `S4B_ACCEPTANCE=1` red with the coherent state above; full native 2,448 passed
/ 1 skipped; RAM1 unchanged from the refusal commit.

## S4b.1 — successor capture handoff: behavioral contract (2026-09-25, owner-provided)

This is the **authoritative contract** for S4b.1. It replaces the earlier framing of the task, which
described implementation fields rather than behavior. Field changes (for example clearing
`pendingRecordRefSlot`) are implementation choices and are **not** the acceptance; the evidence does not
show that any one field is the only remaining defect.

### Acceptance and ownership

A switch and subsequent record/overdub request retain their **target track, slot and requested mode**
until executed or explicitly superseded. No request is discarded because a predecessor is finalizing.

Before switching away from A, A's stop must be **admitted and bound to A's interval**. Once bound, A's
finalization may continue independently while B becomes active and starts capture. B must not wait for A's
finalization or persistence to complete.

### Start timing

The handoff preserves the requested start mode's musical timing. **Finalization adds no extra delay.**

- For an **empty or successfully cleared B**, a quantized record start uses the **transport's bar grid**.
  B establishes its recording origin at the actual start tick; it does not require an existing loop
  length or phase.
- For a **populated B**, overdub uses **B's playback phase** under the existing overdub-start policy.
- A's mutable loop timing must **not** become an accidental dependency of B's start. Any intentionally
  requested synchronization to another loop must be explicit and remain valid across the handoff.

### Execution and completion

Start executes **exactly once** after its prerequisite stop binds, its target becomes active, resources
are ready, and its timing condition is satisfied. The request is **consumed only** after successful start
or an explicit supersession/cancellation.

A's later completion must not change B's capture, active-slot identity, timing origin or recorded content.

### Timing interpretation (pinned, because it changes the implementation)

"Immediately" here means **no finalization-induced delay, while preserving quantization**. It does **not**
mean an unbounded wait is acceptable, and it does **not** mean "begin capturing on the gesture itself,
even between bar boundaries" -- that would be a separate **immediate start mode** and clearing a phase
reference would not implement it.

### Required acceptance checks

1. A has an **admitted, identified finalizer**, held unfinished.
2. Switch/start and B's incoming MIDI use **production routes**.
3. B starts on the **specified eligible boundary** and records the expected notes.
4. An **unadmitted** A stop retains switch/start requests; they execute after admission **without another
   gesture**.
5. Completing A leaves B **unchanged**.
6. A **failed** start retains its request; a **successful** start cannot execute twice.
7. Coverage: **same-track and cross-track** targets; **empty, cleared and populated** targets.

**Separate capacity acceptance case (not implied here):** repeated **B -> C handoffs while A finalizes**.
This contract must not be read as permitting unlimited retained finalizers.

### Correction to the previous pass's diagnosis

The dispatcher **already** handles a zero-length reference by falling back to the transport bar grid
(`handlePendingRecordStart`: `if (L == 0 || currentTick < st)` -> `currentTick % barTicks == 0`). So
"accommodate a target with no loop length" was **not** a sufficient diagnosis, and the earlier
`pendingRecordRefSlot` proposal was premature. The next step is a **full review of the path** -- acceptance
-> switch -> timing -> provisioning -> start -> consumption -- because a green retention probe proves the
request is retained, not that it completes.

### Path review result: the start is refused and the request is stranded (not lost to a field)

The full path review (acceptance -> switch -> timing -> provisioning -> start -> consumption) locates the
defect at **start**, and the code names its own owner.

`TrackManager::startRecordingTrack` opens with:

```cpp
// S4f.6 replaces this refusal with the accepted-start carrier and the stop > switch > start composition.
if (tr.hasStopInFlight()) {
  logger.log(CAT_TRACK, LOG_INFO,
             "Track %d: record admission deferred while a stop obligation is live", trackIndex);
  return;
}
```

After the handoff, A's admitted stop is exactly `hasStopInFlight()`, so:

- the **timing** step is satisfied -- the dispatcher already falls back to the transport bar grid for a
  zero-length reference, so `shouldStart` is reachable (the earlier `pendingRecordRefSlot` proposal was
  wrong and is withdrawn);
- the **start** step is **refused** -- and because the refusal happens inside `startRecordingTrack`, the
  dispatcher's consumption lines (`pendingRecordSlot[i][targetSlot] = false; pendingRecord[i] = false;`)
  are never reached;
- the **request is therefore stranded**: retained (the retention probe is green) but never consumed, so it
  neither executes nor is superseded.

So the missing piece is not a timing field: it is the **accepted-start carrier** that this refusal's own
comment assigns to **S4f.6**, together with the `stop > switch > start` composition. The queue already
holds the request correctly, which is why retention passes while completion does not. A green retention
probe proves retention, not end-to-end completion -- exactly as the contract warns.

Dependency chain, corrected: **S4b.1 -> S4f.6** (accepted-start carrier / composition), with S4f.5's
unadmitted-stop refusal and S4f.3's admitted-stop release both staying intact. The earlier claim that
S4b.1 was blocked on S4f.3 alone was incomplete: S4f.3's first half was necessary but not sufficient.

Acceptance status against the owner-provided contract:

| Check | Status |
|---|---|
| A has an admitted, identified finalizer, held unfinished | **met** (fixture asserts it) |
| Switch/start and B's MIDI use production routes | **met** |
| B starts on the specified eligible boundary and records the notes | **not met** -- refused at start |
| Unadmitted A stop retains requests and executes after admission without another gesture | **met** (passing fixture) |
| Completing A leaves B unchanged | **not reachable** until B starts |
| Failed start retains its request; success cannot execute twice | **not met** -- the failed start strands it |
| Same-track / cross-track; empty / cleared / populated | same-track empty covered; the rest **open** |

Repeated **B -> C while A finalizes** remains its own capacity acceptance case and is not implied by this.

> **Superseded in part by § S4f.6 (2026-09-25).** The statuses above record the state *before* the carrier
> landed. One sentence in this path review is **retracted**: the refusal did not "strand" the request by
> short-circuiting the dispatcher's consumption lines -- it **discarded** it, because `startRecordingTrack`
> returned `void` and the consumption lines followed unconditionally. See § S4f.6 for the corrected
> mechanism, the landed carrier, the red-proof and the current statuses.

## S4f.6 — accepted-start carrier: the successor starts while the predecessor finalizes (2026-09-25, landed)

**Scope (pinned).** Make S4b.1's acceptance execute end-to-end: predecessor A's stop is admitted and bound
to A's interval and held unfinished, the switch moves the active slot to B, and the start request for B
executes on its eligible boundary and is consumed **exactly once**. Out of scope, and recorded below rather
than implied: carrying the start on the switch's **own** gesture (S4f.3's remaining half) and repeated
B -> C finalizer capacity.

**Transition oracle.**

- Given A overdubbing with an **admitted, unfinished** deferred stop, when the target slot is selected, the
  record gesture is pressed through `MidiButtonActions::handleToggleRecordForSlot`, and the production
  dispatcher advances to the next bar boundary, then B becomes the active slot, starts capture, records the
  injected note (on + off), and A's finalizer is still outstanding.
- Given the same precondition and a live stop obligation on the **target's own** slot, when the start is
  attempted, then it is refused and the request is **retained** -- not consumed -- so the next eligible
  boundary retries it.
- Given A's finalizer then runs to completion, then B's active-slot identity and captured events are
  unchanged.

**Implementation.**

- `TrackManager::startRecordingTrack` returns `bool`: every refusal (a live stop obligation, an
  already-committed slot, a refused state change, and the re-queue path) returns `false`.
- The refusal is **slot-scoped**. `Track::hasStopInFlightForSlot(uint8_t)` reads `captureStopJobHead()->slot`
  and `interactionLatch()->slot`, so a stop bound to A is no longer read as blocking a start on B. The
  track-wide `hasStopInFlight()` stays for the unadmitted-stop guard it was written for.
- `handlePendingRecordStart` sets the target active **first** (the contract requires the target to be active
  before the start executes), then consumes the request only when the start happened.
- Native stub: `HARNESS_OBSERVABLE_START_RECLAIM` keeps `reclaimUnreferencedDisabledPasses` observable in
  suites that drive a start, instead of failing closed on the start's last line.

**Retraction -- the path review's mechanism sentence was wrong.** The sixth-pass review said the refusal
"short-circuits the dispatcher's consumption lines", leaving the request "retained but never executed and
never superseded". The committed dispatcher did the opposite: `startRecordingTrack(...)` returned `void` and
the four consumption lines followed it **unconditionally**
(`git show HEAD:src/TrackManager/TrackManagerCaptureQueue.cpp`, lines 296-302), so a refusal **discarded**
the request. The recorded symptom (B never starts) stands; the mechanism sentence did not. This matters
because "retained" and "discarded" call for different carriers, and it is why the retention probe could be
green while completion was not.

**Evidence.**

- `test_switching_starts_the_new_take_while_the_previous_take_still_finalizes`: both characterization
  branches are **deleted** (as this packet's own note required) and the claims are unconditional. The
  start-dispatch loop no longer has a fallback press, so a start that only happens because the fixture
  pressed again cannot be mistaken for the requirement. Green in default and `S4B_ACCEPTANCE=1` modes.
- `test_an_unadmitted_stop_retains_the_switch_request_without_a_second_gesture` (renamed from
  `..._retains_the_switch_and_start_requests`): the old name **overclaimed** -- the fixture only ever proved
  the switch, and a temporary probe (turning its refused-branch early return into an unconditional failure)
  confirmed it takes the retained branch. It now also drives a start after the latch-admitted stop and
  asserts B starts, while labelling that press as a second gesture rather than counting it as "without
  another gesture".
- **Red-proof:** restoring the pre-S4f.6 `hasStopInFlight()` guard fails both fixtures at "B must start ..."
  (`2 failed, 10 succeeded`). The fix was restored and the tree verified free of the probe.
- Native regression: **2,448 passed / 1 skipped** (exit 0).
- Firmware `teensy41-capture-serial`: SUCCESS. RAM1 `variables 96,160 / code 392,040 / padding 1,176 /
  free for locals 34,912` -- byte-identical to the pre-change build, so the carrier costs no RAM1.

**Acceptance status against the owner-provided contract.**

| Check | Status |
|---|---|
| A has an admitted, identified finalizer, held unfinished | **met** (fixture asserts it) |
| Switch/start and B's MIDI use production routes | **met** |
| B starts on the specified eligible boundary and records the notes | **met** (unconditional fixture) |
| Unadmitted A stop retains switch/start requests without another gesture | **switch half met; start half open** |
| Completing A leaves B unchanged | **met** (fixture step 5) |
| A failed start retains its request; success cannot execute twice | **retention met** (refusals now return `false` and the dispatcher keeps the request); the "cannot execute twice" leg has no dedicated fixture yet |
| Coverage: same-track / cross-track; empty / cleared / populated | same-track empty **met**; the rest **open** |

**Remaining, explicitly open.**

1. **S4f.3's carrier, three parts.** (a) A retained **switch** request that identifies its prerequisite stop;
   `SlotStateMachine`'s pending switch / `TrackManager::requestSlotSwitch` is the existing extension point.
   (b) **Consumption at successful commit** instead of the unconditional `clearPendingSlotSwitch` in
   `TrackManagerTransportTick`. (c) Dispatch of the retained **start** at that commit, so the switch's own
   gesture carries it and one press suffices.
2. **New finding, recorded not fixed.** `TrackManager::finalizeCaptureAndSelectSlot` clears
   `pendingRecord[trackIndex]`, `pendingRecordQueuedAtTick[trackIndex]`,
   `pendingRecordRefSlot[trackIndex]` and every `pendingRecordSlot[trackIndex][*]` at its **top**, before it
   knows whether the switch will commit (it can still return early on an unadmitted stop at
   `if (t.hasAcceptedUnexecutedInteraction()) return;`). A switch therefore **discards** a retained start
   request for any slot. The landed fixture cannot see it because it commits the switch before queuing the
   start; the carrier in (1) will queue first and must fix this ordering, or the contract's "no request is
   discarded because a predecessor is finalizing" fails on exactly the path S4b.1 is about.
3. **Check 7 coverage**: cross-track targets and cleared / populated targets have no fixture yet; the
   populated-target case is the overdub-on-B's-playback-phase branch of the contract.
4. **Repeated B -> C while A finalizes** stays its own **capacity** acceptance case (finalizer capacity, not
   the inbound overlap policy); `Omax` alone does not cover it.

## S4f.3 — the carrier's commit case: the switch's own gesture carries the start (2026-09-25, landed)

**Scope (pinned).** Close S4b.1's contract check 4 for the **committed** switch: one press on the target
slot while a capture is live on the predecessor must both commit the switch and carry the requested record
start, so the user does not have to press again. Out of scope, and recorded below: the **uncommitted**
switch (the stop is admission-denied for capacity) and the unconditional pending-record clear.

**Transition oracle.**

- Given A overdubbing with an **unadmitted** (latched) stop, when the target slot's gesture is pressed
  **once** through `MidiButtonActions::handleToggleRecordForSlot`, then the switch commits, a record start
  for the target is **queued**, and driving the production dispatcher across the target's bar boundary
  starts capture on the target -- with **no further press** and with A's finalizer still outstanding.
- Given the same precondition but a target that already holds committed MIDI, then the switch commits and
  **no** record start is carried (the gesture asked for the slot, not for a take).

**Implementation.**

- `TrackManager::finalizeCaptureAndSelectSlot` returns `bool`: the **commit point** now reports whether the
  switch committed. It returns `false` on the invalid-argument guard and on
  `if (t.hasAcceptedUnexecutedInteraction()) return false;` -- the S4f.5 rule that the active slot must not
  move under an accepted-but-unexecuted stop. Callers may ignore the result, so the other three call sites
  are unchanged in behavior.
- `handleToggleRecordForSlot`'s capture-switch branch captures that result and, when the switch committed
  and the target has no committed passes, queues the record start with
  `queueRecordingTrack(trackIdx, slotIndex, Config::INVALID_LOOP_SLOT)`.
- **The phase reference is deliberately `INVALID_LOOP_SLOT`.** `refSlotPhaseForQueue(track, previousSlot)`
  -- what the ordinary arm path uses -- returns the predecessor's slot whenever that loop has a length, and
  the predecessor here always does. Using it would make **A's mutable loop timing an accidental dependency
  of B's start**, which the owner-provided contract forbids by name; with no reference the dispatcher falls
  back to the transport's bar grid, which is what the contract requires for an empty or cleared target.

**Evidence.**

- `test_the_switch_gesture_carries_the_start_without_a_second_press` (renamed from
  `test_an_unadmitted_stop_retains_the_switch_request_without_a_second_gesture`; the old name and its
  second press are gone): asserts the request exists (`recordQueuedForSlot(0, 1)`) right after the single
  gesture, drives the dispatcher with **no** further press, and asserts the target is recording with A's
  finalizer still outstanding.
- **Red-proof:** disabling the carrier line fails exactly this fixture (`1 failed, 11 succeeded`). The
  two-gesture acceptance fixture still passes in that run, so the new fixture **discriminates** the carrier
  rather than restating the acceptance.
- Green in default and `S4B_ACCEPTANCE=1` modes; full native **2,448 passed / 1 skipped** (which includes
  `test_continuous_capture`, whose `test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes`
  asserts the S4f.3-corrected rule).
- Firmware `teensy41-capture-serial`: SUCCESS. RAM1 `variables 96,160 / code 392,104 (was 392,040) /
  padding 1,112 (was 1,176) / free for locals 34,912` -- 64 bytes of code for the carrier, padding still
  above the approved 840-byte floor.

**Check 4 is now met for the committed case; the uncommitted case is the reachable remainder.** The
uncommitted switch is not hypothetical: `Track::stopOverdubbing` **denies** admission when
`hasPendingDeferredOverdubStop() || finalizationResourcesBusy`, keeping the interval recording and the
obligation latched (DEC-067's capacity answer). In that window `finalizeCaptureAndSelectSlot` returns
`false`, and with this slice nothing is carried -- the switch is neither performed nor **retained**, so the
press is dropped. Carrying it needs the switch itself to be retained with its prerequisite-stop identity
(the `SlotStateMachine` pending switch / `TrackManager::requestSlotSwitch`) and consumed at a later commit.
That is S4f.3's original "queued switch + stop admission in one window -> the target is never lost" fixture,
and it lands on the **finalizer-capacity** question this change deliberately keeps open -- `Omax` alone does
not answer it.

**Still open, unchanged by this slice.**

1. **The uncommitted switch** (above): retain rather than drop, with the prerequisite stop identified.
2. **The unconditional pending-record clear** at the top of `finalizeCaptureAndSelectSlot` (see § S4f.6
   remaining item 2). This slice queues *after* that call, so the carrier's own request survives it -- but a
   start queued by any other route **before** a switch is still discarded. With the carrier in place this is
   now the only known path that discards a live start request, and it should be scoped to the finalized slot
   when the retained-switch work lands.
3. S4b.1 check 7 coverage (cross-track, cleared and populated targets) and check 6's "cannot execute twice"
   leg.
4. Repeated **B -> C while A finalizes** remains a capacity acceptance case.
## S4f.3 — accepted-switch retention under unavailable stop admission (2026-09-25, landed)

**Scope (pinned).** The switch/start request must survive when the predecessor's stop **cannot be admitted**,
and execute later without another gesture. Consumption happens only at a successful start or an explicit
supersession. Exactly-once. The existing capacity model is preserved: no new capacity, no new request owner.

**The defect, precisely.** `MidiButtonActions::handleToggleRecordForSlot`'s capture-switch branch committed
the switch and returned. When the switch could not commit -- `finalizeCaptureAndSelectSlot` returns false
while the interval's stop is accepted-and-unadmitted -- the branch had **nothing** to retain, so the press
was dropped: neither the switch nor the start existed anywhere afterwards.

**What was already correct, and is now relied on rather than duplicated.** The stop side already owns
retention with exactly-once binding: `Track::latchAcceptedStop(destination, slot, interval)` writes the
obligation against the interval's own identity, and `driveAcceptedInteraction()` (reached through
`serviceAcceptedInteractionTurn()` at the top of `processDeferredIdleMaintenance`) re-runs the admission
guard chain on a deferred turn, binds it exactly once when the owner frees, supersedes it if its interval is
gone, and otherwise parks. The switch therefore **waits** for that obligation instead of re-driving it.

**Implementation.**

- `TrackManager::retainRecordStartForSlot(trackIndex, slotIndex)` retains a record start in the **existing**
  manager pending-start owner (`pendingRecord` / `pendingRecordSlot` / `pendingRecordRefSlot` /
  `pendingRecordQueuedAtTick`) **without** the enabled-set change `queueRecordingTrack` performs: while the
  switch waits, the predecessor is still the active audible interval, so the audible set must not move. The
  enabled set is applied by `startRecordingTrack` when the start actually executes.
- `handleToggleRecordForSlot` now retains on the uncommitted path instead of dropping, so the same gesture
  that could not switch still leaves a live request.
- `finalizeCaptureAndSelectSlot` no longer clears the whole track's pending-start state. It calls
  `clearQueuedRecordingTrack(trackIndex, captureSlot)`, which is **explicit supersession scoped to the slot
  being finalized** and already recomputes `pendingRecord` and resets the queued tick. It also drops the
  retained request's phase reference when that reference is the finalized slot, so the predecessor's
  just-committed loop timing cannot become an accidental dependency of the successor's start.
- The overdub branch of `finalizeCaptureAndSelectSlot` is now **slot-scoped and non-re-entrant**:
  `hasLiveCaptureStopJobForSlot(captureSlot)` means this interval's stop is bound and the switch may
  proceed; `hasAcceptedInteractionForSlot(captureSlot)` means it is accepted-but-unadmitted and the switch
  must wait; otherwise this is the live stop edge and the stop is requested. `hasPendingDeferredOverdubStop()`
  is deliberately **not** used here: it is **track-wide**, so it would let a different slot's admitted job
  stand in for this interval's unadmitted stop -- exactly the "A finalizing while B's stop waits" case.
  Re-entering `stopOverdubbing()` on an already-admitted stop is also harmful, because
  `beginDeferredOverdubStopForSlot` returns `false` for a pending job and the capacity guard then
  **re-latches** the obligation, blocking the very switch being made.

**The activation hazard, found by the new fixture and fixed.** `handlePendingRecordStart` activated the
target with `Track::setActiveLoopIndex`, the **unconditional** setter. That bypassed the switch path
entirely, so "activate the target first" moved the active slot straight to the target even while the
predecessor's stop was unadmitted -- and the start then ran against it. Activation now goes through
`TrackManager::setActiveLoopIndex`, the production write authority that routes a live capture through
`finalizeCaptureAndSelectSlot`, and the dispatcher asserts the invariant explicitly: if the active slot did
not reach the target, the **only** accepted explanation is "the active slot is unchanged and the
predecessor's stop is in flight" (`hasStopInFlightForSlot(predecessorSlot)`); anything else refuses the
start and reports. The refusal is unconditional; the identifying detail is gated behind `SESSION_CAPTURE`
per the repo's instrumentation rule.

**Deviation from the review's suggested owner, recorded rather than silently taken.** The recommendation was
to use the `SlotStateMachine` pending-switch owner **and** the manager pending-start owner. This slice uses
the pending-start owner as the single home of the request. The `SlotStateMachine` pending switch is the
**playback** switch owner, and its consumer cancels a target with no loop data
(`TrackManagerTransportTick`: "pending target no longer has loop data, cancel it") -- which is precisely a
record target, so a record-carrying request parked there would be cancelled by the playback lane. Carrying
the target in both owners would also store the same target twice, against the "avoid another target owner"
instruction. The switch itself is still executed by the existing switch path; only its *request* lives in the
pending-start owner.

**Evidence.**

- `test_a_blocked_switch_and_start_survive_until_its_prerequisite_stop_binds` (new, one track, slots A/B/C):
  A's stop is admitted and **held** (capacity occupied); B becomes active through the production active-slot
  authority and overdubs; B's stop is requested and **denied on capacity**, latching against B's own slot; a
  switch/start is requested on C and is retained; **repeated dispatch** (768 x 3 ticks) leaves both requests
  intact and never moves the active slot off B; capacity is released and B's stop is driven through
  `serviceAcceptedInteractionTurn` until it binds; dispatch then commits the switch to C, starts C, and C
  records the incoming note; further dispatch re-executes neither request and does not disturb C.
  - Note for the record: a fresh **record** stop is *not* capacity-governed (`TrackManager::stopRecordingTrack`
    is synchronous), so only an **overdub** stop can be made to wait. B is therefore an overdub interval,
    which is the "capture on B" the contract needs.
- **Red-proved three times, each with a distinct failure line:**
  - retention disabled (`retainRecordStartForSlot` not called) -> "the switch must retain C's record start
    rather than drop it" (`1 failed, 11 succeeded`);
  - activation reverted to `Track::setActiveLoopIndex` -> "the active slot must not move under B's
    unadmitted stop, on any dispatch" (`1 failed, 11 succeeded`);
  - the previous unconditional pending-record clear restored -> "C's switch/start must survive repeated
    dispatch" (`1 failed, 11 succeeded`).
- A **false green was found and removed**: the fixture this replaces
  (`test_the_switch_gesture_carries_the_start_without_a_second_press`) asserted
  `activeLoopIndex == 1` immediately after its gesture, and with the retention change it began taking an
  `activeLoopIndex != 1` characterization branch that returned early in the default gate -- passing while
  holding nothing. The branch is gone; the replacement asserts the requirement unconditionally.
- Green in default and `S4B_ACCEPTANCE=1` modes; full native **2,448 passed / 1 skipped**.
- Firmware `teensy41-capture-serial`: SUCCESS.
  **RAM1 is now exactly on its approved floor: `variables 96,160 / code 392,376 / padding 840`, floor
  `padding >= 840`.** This slice spent 272 bytes of the 1,112 that were available (`code 392,104 -> 392,376`).
  Gating the diagnostic did **not** recover any of it, because the format strings are FLASH-resident. **The
  next slice in this area must free RAM1 or revisit the floor decision before adding code.**

**Still open, explicitly.**

1. **Immediate repeated handoffs** (B -> C starting while A still finalizes). Retention is now proven; it does
   **not** prove immediate execution. This needs the separate **finalizer-capacity** decision: the required
   bounded finalization overlap and its memory cost must be established before that behavior is promised.
2. S4b.1 check 7 coverage: cross-track targets and cleared / populated targets.
3. S4b.1 check 6: a successful start "cannot execute twice" has no dedicated fixture (the blocked fixture
   asserts it for the retained request, not for the ordinary double-dispatch case).

## S4f.3 — HITL attempt on the registered entry points (2026-09-26): no pass, and the new path is uncovered

Run identity, both runs: `git_commit e743b97c185884e77c5706917639888ea8e2166b`,
`firmware_hex_sha256 14a1aea74af01e581ca20dfe6bee83a47c7ac3f34fab7b760ecdfccc7cef9d00` (the flashed image is
the verified one). Device prep followed the skill: upload, wait for the port, documented scratch slot
**track 5 / slot 1**.

**Entry point 1 -- stop-interruption wrapper (`scripts/hitl_stop_interruption.py`), twice.**

- First attempt was **my procedural error** and is recorded as such: run on track 8 / slot 4 without a fresh
  boot, immediately after another run. It aborted on preconditions with `#CAP lines observed: 3`,
  `INCONCLUSIVE_CAPTURE (SPARSE_CAPTURE)` -- the skill's stale-device signature (section 3). Corrected by
  re-uploading and using the documented scratch slot.
- Second attempt (`captures/s4f3-stop-t5s1-120bpm-20260926_141559`) reached the fixture properly:
  `ST STOPPED_RECORDING->PLAYING: 1`, `ST PLAYING->OVERDUBBING: 1`, record on/off `32/32`,
  `deferred persist nonblocking ... ok=True`, `MIDI-boundary intervals {OVERDUBBING: 1, OVERDUB_STOPPING: 1,
  PLAYING: 8, RECORDING: 1}` with `stopping=1`, and display `overdub_ok=True transport_ok=True`. The
  intervention **was** sent.
- Verdicts: focused `INCONCLUSIVE_CAPTURE` with `no_device_stop_inside_stopping`; base `FAIL
  (FIRMWARE_CONTRADICTION)` with `overdub_unmatched_open_notes`, `overdub_out_of_range_notes`.
- The base issues are the skill's documented **setup** verdicts ("dense generator left notes held at stop;
  setup verdict only"). The focused issue is **not a timing flake in the ordinary sense**: the session log
  shows why it cannot be hit --

  ```
  #CAP,136629878,ST,Track,OVERDUBBING,OVERDUB_STOPPING
  #CAP,136629899,ST,Track,OVERDUB_STOPPING,PLAYING     <- 21 microseconds later
  #CAP,136639414,ST,Track,PLAYING,STOPPED             <- transport stop, +9.5 ms
  ```

  The `OVERDUB_STOPPING` window lasts **21 us**, because the admitted stop defers finalization (DEC-078) and
  returns the track to PLAYING immediately (the finalize stage itself took 7,383 us, and it runs *after* the
  state edge). The checkpoint requires the host transport Stop to land **inside** that state, so no
  `--intervention-delay-ms` value can reliably hit it. This is an **architectural mismatch between the
  fixture's premise and the deferred-finalization design** that S4f.3/S4b.1 depend on -- not evidence about
  this change.

**Entry point 2 -- registered baseline preset (`--preset base`, `STIM-BASE-2-2-01`).**
`INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)`: `RECORDING: 0` and `OVERDUBBING: 0` with
`record_first_note_missing`, aborting at "second overdub transition not confirmed". Host->device MIDI did
not arm a record. This is the same **base-preset setup race** already recorded for this change
("... present with DMA off too and owned by the still-open `runtime-working-set-ownership` selection
fixture"), and it is not attributable to this slice. Tempo was **correct** in this run
(`record loop length 1536/1536`), so the race is not a tempo artefact.

**Tempo note.** The device was found at **160.0 BPM**, not the 120.0 previously pinned; the DEC-086 fader
pinned it back to 120.0 (`CC102=60 -> 120.0 BPM`, `scripts/hitl_tempo_fader_probe.py --set-tempo 120`,
`captures/s4f3-tempo-pin-20260926_141525/`). The pin does not survive a reboot, and every HITL entry point
opens (and may reset) the serial port, so **tempo must be re-pinned per run** -- worth automating before the
next device session.

**Conclusion, stated plainly: the HITL step did not produce a pass, and it could not have covered this
slice.** Per the skill, `INCONCLUSIVE_CAPTURE` is not a pass. More importantly, **no registered stimulus
exercises the behaviour this slice implements** -- a switch/start requested while the predecessor's stop is
capacity-denied -- so these runs are regression context only. Device coverage of the retention path needs a
new stimulus; per the repo rule that is a **temporary variant** first, promoted to a new `STIM-...-02` only
once the owner accepts it as the reproducer. The owner declined the variant for this session, so the
retention path remains **host-verified only** and that limitation is recorded here rather than papered over.

## S4f.3 — device-retention stimulus and oracle: prepared, NOT run (2026-09-26)

Prepared under the owner's direction. The recorded refusal of a temporary variant **still stands**: nothing
here was run on the device, and no target slot is assumed authorized.

### Assessment (step 2a): the telemetry and the exposure both already exist

Two purpose-built records already carry the requirement's two edges, and both were confirmed on the wire in
this session:

- `DIAG,reentry_window_open,<slot>,<stopTick>,<loopLengthTicks>` — DEC-067 `successor_start_window_open`,
  documented as *"the deferred predecessor stop has frozen its interval and handed the live capture role
  back, so a successor interval may start ... one shot per deferred stop"*. This is **A's finalizer admitted
  and outstanding, named by slot**.
- `DIAG,canonical_release,<slot>,<loopLengthTicks>,<availableChunks>` — DEC-067
  `canonical_finalization_capacity_released`, *"one shot per retired stop"*. This is the moment a successor's
  stop *may* be admitted, i.e. the end of the capacity-denied window.
- The denial itself is already recorded per identity: `DIAG,reentry,deny,<tick>,<interval>,<slot>,<depth>`
  (and `deny_resource`), one shot per decision **from the decision owner**, plus the stop-side
  `ODUB,stop,admission,...,successor_stop_denied_capacity`; waiting is `accepted_stop_finalizer_wait` /
  `accepted_stop_park_terminal` with the queue depth.
- The successor's start is recorded per slot: `RECA,<slot>,<tick>`, so exactly-once is a count.

So the requirement is decidable from **existing** telemetry keyed on slot and interval identity -- never on
the 21 us `OVERDUB_STOPPING` state that defeated the interruption fixture.

**Exposure is measured, and comfortable.** From the real device session
`captures/s4f3-stop-t5s1-120bpm-20260926_141559`:

```
136629769  DIAG,reentry_window_open,0,16387,1536
137965158  DIAG,canonical_release,0,1536,8
```

A capacity-denied window of **1,335,389 us (1.335 s)** for a 1536-tick (2-bar) loop. Two host gestures need
tens of milliseconds, so the state is reachable with a **natural workload** and **no finalizer-pause control
is required**. The gated pause is therefore *not* proposed for implementation; its design is kept below only
so the option is not lost.

> **Corrected 2026-09-26 -- see the retention-oracle-tightened section below.** "No finalizer-pause
> control is required" is **withdrawn**: a window duration is not a staged run, and the session that produced
> it had no boot banner. The measurement supports *trying* a natural workload under a fresh boot; it does not
> establish that the hook will never be needed. The hook stays a contingency.

Caveat, because it decides how the number may be cited: that session's log has **no boot banner** and 108 s
of uptime, so it is not verdict-grade -- the oracle fails `fresh_boot` on it, correctly. The 1.335 s figure is
a timing observation to be **reconfirmed under a fresh boot** in the authorized run.

### The stimulus (temporary; prepared, not authorized to run)

Authored in the sanctioned form -- `STIM-BASE-2-ONE-OD-01-TEMP-RETENTION-A`, forked from
`STIM-BASE-2-ONE-OD-01` with `host_midi_hitl.py stimulus temp` (explicitly **not registered**), sidecar at
`openspec/changes/runtime-working-set-ownership/stimuli/`.

Recipe: seed slot A with a 2-bar take; overdub A; stop A and let the deferred finalizer run; verify the
window is open (`reentry_window_open,<A>`); **while it is open** start B's overdub, request B's stop (expect
`deny` for slot B), and request the switch/start on C, confirming the request survives repeated host
dispatch; after `canonical_release,<A>`, confirm B's stop binds, C starts (**exactly one** `RECA,<C>`), C
records the injected MIDI, and later dispatch executes neither request twice.

The pass criteria are **the oracle's checks**, not the sidecar's `must_prove`: the sidecar inherits the
parent's criteria verbatim (the fork mechanism copies them) and the exporter defect below makes it
unreadable anyway.

### The oracle (step 1)

`scripts/hitl/retention_probe.py` writes `retention-review.json`. Verdict vocabulary and **distinct exit
codes** keep setup apart from retention:

| Verdict | Exit | Meaning |
|---|---|---|
| `SETUP_INCOMPLETE` | 3 | the scenario was not staged; says nothing about retention |
| `RETENTION_NOT_EXERCISED` | 2 | staged, but the capacity-denied state was never entered |
| `RETENTION_FAIL` | 2 | denied state entered, requirement violated |
| `RETENTION_PASS` | 0 | every check below holds |

Setup checks: `fresh_boot`, `tempo_verified` (a **read-back** is required), `seed_content_on_a`,
`finalizer_admitted_on_a`, `capacity_released_after_a`, `capacity_denied_window_wide_enough` (floor 150 ms for
staging, far below the measured 1,335 ms).
Retention checks: `successor_stop_admission_denied` (the denial must fall **inside** A's window),
`successor_started_exactly_once`, `activation_invariant_held`, `no_stop_obligation_replay`.

**Validated without a device**, which is the point of building it first:
- `scripts/test_retention_probe.py` -- 11 host tests, all green. They include: a complete sequence passes and
  reports the measured window; a missing tempo read-back is `SETUP_INCOMPLETE` **with no retention check
  deciding**; a missing boot banner is `SETUP_INCOMPLETE`; no denial is `RETENTION_NOT_EXERCISED`; a denial
  *outside* the window does not count as staged; two successor starts fail exactly-once; a start on the wrong
  slot does not satisfy the requirement; an activation-invariant violation fails loudly; a narrow window is an
  exposure/setup failure; an empty log is `SETUP_INCOMPLETE`; a replayed stop obligation after release fails.
  One of these caught a **bug in the test fixture itself** (it moved the denial's tick but not its timestamp),
  which is recorded rather than quietly corrected.
- Run against the **real** device log above: `SETUP_INCOMPLETE` (no boot banner, no tempo read-back) while
  still parsing `finalizer_admitted_on_a` and `capacity_released_after_a` from real lines and measuring the
  1.335 s window -- i.e. a non-staged run is reported as setup/not-exercised and **never** as a retention
  result.

### Setup determinism (step 3)

- **Authorized targets: not yet established, and not assumed.** The documented scratch slot is track 5 /
  slot 1, which authorizes *one* slot; this scenario needs three targets on one track (A, B, C). The owner
  must authorize them before any run.
- Tempo is applied and **read back after the final serial open** (the pin does not survive the reset), enforced
  by `tempo_verified`.
- Freshness: re-upload and wait for the port, enforced by `fresh_boot`.
- Per-transition confirmation: A's window open, the denial inside it, and the release are each required; a
  missing step yields `SETUP_INCOMPLETE`, and setup failures exit 3 against retention failures at 2, so the two
  cannot be conflated.

> **Corrected 2026-09-26:** exit codes classify *verdicts*; they never established a *cause*. The tightened
> oracle blocks a pass on any base-report issue that is not explicitly allowlisted, and records an allowlisting
> as an **operator claim**, not as evidence.

### Contingency: bounded, gated finalizer pause (design only, NOT implemented)

If a future workload narrows the window below the staging floor, the control must (a) hold the deferred stop
outstanding, (b) keep MIDI input and the scheduler running -- no blocking delay or busy-wait, (c) be bounded,
auto-releasing after a deadline, and (d) be **compile-time gated off by default** so the shipped image and the
RAM1 floor (`padding` is exactly 840) are untouched. Sketch: `#if defined(HITL_FINALIZER_PAUSE)` around a
skip in the deferred-stop grain owner while a deadline-bounded flag is set, the flag set only by a
capture-build command. **Not implemented** (corrected 2026-09-26): the hook stays a **contingency** -- the 1.335 s
measurement does not establish that it is unnecessary, only that a natural workload is worth trying first.
Adding code at the RAM1 floor still needs its own justification.

### Found defect, reported not fixed (adjacent to this deliverable)

`scripts/hitl/stimuli/yaml_view.py`'s `dump_yaml` emits **invalid YAML**. The dict branch tests
`isinstance(item, list)`, but `StimulusSpec`'s list-ish fields are **tuples**, so they fall to the `else`
branch and are inlined after the key: the generated sidecar contains `overdubs: - name: "OD1"` on one line and
detaches the second `must_prove` entry to column 0. Nothing reads the YAML back, which is why it survived.
**Not fixed here** -- it changes a canonical stimulus view, and silently rewriting an authority-adjacent
artifact is not this slice's call.

## S4b.1 check 7 and check 6 — cross-track, cleared, populated and the committed-start exactly-once half (2026-09-26, landed)

Native-only slice (commit `27b537e6`, tests plus two guarded observable stubs; `src/` and `include/` byte-identical
to HEAD, no firmware build needed, RAM1 floor untouched). This closes the coverage gaps this packet had been
carrying as "open" under check 7 and check 6.

| Fixture | Contract item |
|---|---|
| `test_a_cross_track_successor_starts_while_the_predecessor_finalizes` | check 7 -- cross-track |
| `test_a_cleared_successor_is_a_record_target_while_the_predecessor_finalizes` | check 7 -- cleared |
| `test_a_populated_successor_overdubs_on_its_own_phase_while_the_predecessor_finalizes` | check 7 -- populated |
| `test_the_switch_carried_start_executes_exactly_once` | check 6 |

Two modelling details worth keeping, because they are what make the fixtures falsifiable:

- The **populated** variant must switch **while still capturing**. Pressing stop first leaves the track
  PLAYING, and the gesture then routes down the *playback*-switch lane instead of
  `finalizeCaptureAndSelectSlot` -- so the naive version would have tested a different path entirely.
- Its successor is given a **different loop length** (768 predecessor vs 1,536 successor) so that "the
  successor overdubbed on its own phase" is a falsifiable claim rather than an assumption.

**The exactly-once gap identified and now covered.** The blocked fixture proves exactly-once for a request
that *waited* for admission. The **switch-carried committed start** had no consumption assertion and was never
re-dispatched after the start. `test_the_switch_carried_start_executes_exactly_once` asserts consumption
(`recordQueuedForSlot` / `anyRecordQueued` false), no re-arm, still recording, an unchanged active slot, and an
unchanged captured-event count across **two further bar boundaries**.

**Red-proofs** (each mutation reverted; tree verified clean; production diff empty):

| Mutation | Failure line |
|---|---|
| refusal while *any* track has a stop in flight | "the successor track must start while the predecessor finalizes" |
| `Track::clear()` no longer drops pass ownership | "a cleared slot must be a record target" |
| carry a start even onto a populated target | "a populated target must NOT be armed for a record start" |
| a successful start does not consume its request | "the executed start must be consumed exactly once" |

The cross-track one is recorded honestly as a **mutation rather than a revert**: the fixture guards against
cross-track coupling, and no earlier version of the code had coupling to revert.

**Finding: the populated-target negative is defended twice, so it proves the property without attributing it.**
The refusal is enforced both by the carrier's own `!hasCommittedPassesInSlot` predicate and independently by
`queueRecordingTrack`'s committed-passes refusal. Disabling **either alone** leaves the suite fully green; the
red-proof required disabling both. The fixture records this rather than implying the carrier is what enforces
it, and no production change was made.

**Checked and recorded as not a defect:** the ordinary immediate start cannot leave a stale request behind --
`handleArmedRecordPress` cancels any queued arm and returns before the immediate `startRecordingTrack` call is
reached.

**Counts:** suite 16/16 in default mode and 16/16 under `S4B_ACCEPTANCE=1`; full native **2,452 passed /
1 skipped** (2,448 + 4). Runs were sequential, never concurrent.

**Still open, and recorded in-fixture rather than implied away:** the **arm-and-wait re-queue** path
(`startRecordingTrack` -> `!shouldQuantizeRecordStart()` re-queues and returns false) and a committed start
that **fails and is retried** still have no exactly-once fixture of their own. Two observable stubs were added
to this suite's `off_path_deps.h` (`HARNESS_OBSERVABLE_TRACK_SELECT`, `HARNESS_OBSERVABLE_SLOT_CLEAR`); both
stay `OFF_PATH()` when their macro is undefined, so no other suite's fail-closed semantics moved.

## HITL stimulus YAML export — latent invalid-YAML defect found and fixed (2026-09-26)

Found while authoring the retention stimulus sidecar, reported, then fixed as approved hygiene with a
red-proved regression test.

**Defect.** `scripts/hitl/stimuli/yaml_view.py`'s `dump_yaml` emitted **invalid YAML** for every list-ish
`StimulusSpec` field. `spec_to_mapping` is `dataclasses.asdict(spec)`, which preserves **tuples** as tuples
(`overdubs`, `must_prove`, `human_checks`, `interactions`), but the mapping branch tested
`isinstance(item, list)` only. Tuples therefore fell through to the scalar `else` branch and were inlined
after their key:

```yaml
overdubs: - name: "OD1"     # a block sequence may not start on its key's line
  bars: 2
must_prove: - "base_recorded"
- "committed_od1"           # detached to column 0: no longer a value of must_prove
```

Nothing reads the YAML back, which is why it survived.

**Fix.** Both sequence tests in `dump_yaml` now treat tuples as sequences
(`isinstance(item, (list, tuple))`), and the mapping branch normalises the tuple to a list before recursing.
The regenerated sidecar is structurally valid (`overdubs:` then indented `- name: …`; `must_prove:` then
indented entries).

**Regression test.** `scripts/test_stimulus_yaml_view.py` -- 3 tests, no YAML parser needed (none is installed
in this environment, which is why the defect was latent): over **every** registered spec and the full registry
export it asserts that no block sequence starts on its key's line and that no sequence entry is detached to
column 0, plus a positive control that the previously broken fields still emit indented entries and that the
literal historical symptom `overdubs: - name:` is absent. **Red-proved** by reverting the fix: all 3 fail.

## Authorized targets for the retention stimulus (owner, 2026-09-26)

The owner authorized **track 5, slots 1 / 2 / 3** as A / B / C for the prepared retention stimulus, replacing
the assumption that a single documented scratch slot covered three targets. Recorded here as authorization for
the *next* device session only: the recorded refusal of a temporary variant still stands, so the stimulus
remains prepared-and-not-run, and each target must still be verified empty (or cleared with permission) before
the run.

## S4f.3 — the retention oracle tightened: an ordered chain, or an explicit "insufficient evidence" (2026-09-26)

The first oracle could report `RETENTION_PASS` on evidence that did not prove the requirement. The owner
reproduced two false passes from its own synthetic fixture, and both are now refused. This section supersedes
the evidence model in the section above; the three claims called out below are corrected in place there.

### The two reproduced false passes

| Reproduction | Old behaviour | Now |
|---|---|---|
| C's start at `100200000 us`, A's release at `101335389 us` | **passed** -- the start was only required to follow the *denial* | `RETENTION_FAIL`, exit 2: L3 and L6 both fail with "a start for slot 2 executed at 100200000 us, before capacity was released at 101335389 us" |
| the identity-specific `DIAG,reentry,deny` removed, generic stage record left | **passed** -- `successor_stop_denied_capacity` substituted for it | `INSUFFICIENT_EVIDENCE`, exit 4: "only the generic 'successor_stop_denied_capacity' stop-stage record is present inside the window ... it cannot establish that B's stop was the one denied" |

### Required ordered chain

Each link needs its own evidence, and a missing or reordered link is never a pass:

| Link | Evidence |
|---|---|
| L1 A's finalizer outstanding | `DIAG,reentry_window_open,<A>,<stopTick>,<loopLen>` |
| L2 B's stop denied, **with B's slot and interval** | `DIAG,reentry,deny\|deny_resource,<tick>,<interval>,<slotB>,<depth>` **inside** A's window |
| L3 C's request retained | the retained-request row for C, ordered after the denial and before the release, **and** no start for C before the release |
| L4 capacity released | `DIAG,canonical_release,<A>` |
| L5 that same B stop admitted and bound | see the gap below |
| L6 C starts exactly once, **after the release** | exactly one `RECA,<C>`, timestamped after L4 |
| L7 C recorded the expected MIDI | see the gap below |
| L8 bounded follow-up, no duplicates | >= `--followup-us` of telemetry after the last required event, no second start, no post-release denial, no extra seal |

Plus integrity gates that were previously absent and could not stop a pass: `identity_pinned` (metadata must
carry `git_commit` **and** `firmware_hex_sha256`), `capture_integrity` (no `RING,overflow` / `RING,tier_a_evicted`,
and `capture_integrity == COMPLETE` in the base report), `observation_tail_complete` (log not cut mid-line,
`capture_complete` true), `setup_transitions_confirmed`, and `reported_issues_resolved`.

### Where existing telemetry cannot prove a link

Reported as `INSUFFICIENT_EVIDENCE` and named, never inferred:

1. **L5's identity link is unavailable.** The denial names `captureInterval_`, which is emitted **only** by
   `reentryDecision` (`TrackOverdubLifecycle.cpp`: the `admit`/`deny`/`deny_resource` sites). Nothing names
   that identity when a stop later **binds**: the stop-tier records (`stop_phase`, `stop_grain`) carry
   `captureStopJob().admission.frozenGeneration`, which `beginDeferredOverdubStopForSlot` sets from
   `staging.captureGeneration` -- a *different* identity space. So L5 is provable at **slot** granularity
   only. Without an explicit `--accept-slot-scoped-bind`, the oracle reports
   `INSUFFICIENT_EVIDENCE`; with it, it passes **and records the concession**, never a proof.
   *The record that would close it:* one emission on the binding edge carrying the interval the request
   named -- the natural place is `beginDeferredOverdubStopForSlot`'s success path, whose `interval` parameter
   is exactly that identity.
2. **L7 is count-level, not pitch-level.** `storedNoteEvent` / `storedNotes` / `queueStoredNoteOn` /
   `recStoredNoteOn` have **no callers** in `src/`, so there is no per-note capture record. What exists is the
   seal stage's `eventCount` (`RECS,stage,seal,...,<eventCount>,...`), which proves *that* a take was sealed
   with N events, not *which* pitches. The oracle therefore requires either the content pipeline's review
   (`--content-review`, requiring `inventory_equal` per cycle) for a pitch-level pass, or an explicit
   `--accept-event-count-only` concession.
3. **L3 rests on a human log line.** The only evidence that a successor request was *retained* is the
   `logger.info("Loop %d: Switch and record start retained ...")` row added by this change -- there is no
   `#CAP` record for a retained request. If that row is absent the oracle says
   `INSUFFICIENT_EVIDENCE`, and the detail names the missing record. *The record that would close it:* a
   one-shot `#CAP` emission when the carrier retains, `DIAG,retain,<kind>,<slot>,<interval>`.

### Correction: what exit-code separation does and does not establish

The section above implied the setup/retention split accounted for the accompanying verdicts in the base leg,
`overdub_out_of_range_notes` in particular. **It does not.** Exit codes classify *verdicts*; they prove nothing
about a cause. The oracle now reads the base report's `issues` and **blocks a pass** on any issue that is not
explicitly allowlisted (`--allow-setup-issue`), and when one is allowlisted it records the statement as an
**operator claim** that the issue is a setup artefact. Nothing in the oracle establishes that cause; the
owner's caution stands.

### Corrections to the previous section's conclusion

- "**no finalizer-pause control is required**" and "the measurement above shows it is unnecessary" are
  **withdrawn**. The 1.335 s window was measured on a session with **no boot banner**, and a window duration
  is not a staged run: it supports *trying* a natural workload under a fresh boot, which is what the prepared
  stimulus does. The hook stays a **contingency**, and whether it is ever needed is decided by that run, not
  by the measurement alone.
- The 1.335 s figure remains valid as a **timing observation** and is what `capacity_denied_window_wide_enough`
  will re-measure.

### Oracle bugs found by its own tests (recorded, not quietly fixed)

Writing one negative per link exposed three defects in the oracle itself:

1. **The verdict precedence let an unknown link mask an affirmative violation.** A wrong-slot start produced
   L6 = fail *and* L7 = unknown, and the unknown won, reporting `INSUFFICIENT_EVIDENCE` instead of
   `RETENTION_FAIL`. Fixed: an affirmative retention violation now outranks an unknown link, because hiding a
   proven break behind "we could not tell" is the wrong default.
2. **An early return masked setup failures.** With no denial at all the code returned
   `RETENTION_NOT_EXERCISED` before evaluating setup, so a log with no boot banner and no tempo read-back was
   reported as a behavioural result. Fixed, and covered by a test.
3. **The follow-up tail used file order.** A record that was later in the file but earlier in time shrank the
   computed tail and produced a spurious "follow-up too short". Fixed to use the maximum timestamp.

### Evidence

- `scripts/test_retention_probe.py`: **29 host tests, all green**, including the complete chain under
  concessions, the same chain **without** concessions reaching `INSUFFICIENT_EVIDENCE` rather than a pass, and
  one negative per link: missing boot banner, missing identity, ring overflow, truncated log, incomplete base
  report, unconfirmed setup transitions, unexplained report issue, allowlisted issue recorded as a claim,
  missing tempo read-back, narrow window, no finalizer window, no denial, generic-only denial, missing
  retained row, a start that was not held, missing bind link, two starts, wrong-slot start, zero-event seal,
  unexpected event count, pitch-level content review, short follow-up, re-issued denial, invariant violation,
  and setup failures not masked by an unstaged denial. 29 tests, one per way to lose a link.
- CLI reproduction of both owner-reported cases gives exit 2 and exit 4 respectively.
- The real device log (`captures/s4f3-stop-t5s1-120bpm-20260926_141559`) now returns
  `SETUP_INCOMPLETE` (exit 3) while still measuring the 1335.4 ms window -- a non-staged run is reported as
  non-evidence, not as a behavioural result.
- Host-script suite: 574 tests, 6 failures + 2 errors, **all pre-existing** and unrelated (they concern C++
  consumers in `TrackOverdubLifecycle.cpp` and `TrackPlaybackHotPath.cpp`, which this work never touched);
  the count matches the previously recorded baseline.

## S4f.3 — the concession loophole closed, and the evidence gaps (2026-09-26)

### 1. Conceded results are now explicitly partial

The oracle previously returned `RETENTION_PASS` with **exit 0** whenever every link was satisfied "on a
conceded basis". Recording concessions separately did not stop an automated consumer from reading exit 0 as
full acceptance, so that was a real acceptance loophole.

- **`RETENTION_PARTIAL` (exit 5)** is now its own verdict, and `RETENTION_PASS` (**exit 0**) is reserved for
  the **complete, unconceded** chain. A conceded result can never exit 0.
- Every operator claim that substitutes for evidence now produces a concession, including
  `--allow-setup-issue`: allowlisting a base-report issue is the operator asserting it is a setup artefact,
  which is a claim and not evidence. The oracle's own detail text says so, and the verdict is at best
  PARTIAL. A partial result must not close the device gate.

### 2. L5 closed: an identity-grade binding record

`beginDeferredOverdubStopForSlot` now emits, on the edge where admission is **committed**:

```cpp
DebugSessionCapture::reentryDecision("bound", currentTick, interval, slot, captureStopQueueDepth());
```

It reuses the **existing** `reentryDecision` emitter and wire format
(`DIAG,reentry,bound,<tick>,<interval>,<slot>,<depth>`), so no new family and no new format string were
added. `interval` is the parameter the caller passed -- `captureInterval_` via `beginDeferredOverdubStop`, or
the latch's stored interval via `driveAcceptedInteraction` -- which is exactly the identity the **denial**
reports. A denied request and its later binding can therefore be **correlated** instead of inferred, which is
what L5 previously could not do.

The record keeps the previously identified telemetry gap honest in both directions: L5 now passes
**unconceded** only when a `bound` record names the *same* slot **and** the *same* interval as the denial. A
`bound` record for the denied interval that appears **before** capacity was released is a `RETENTION_FAIL`,
because the capacity model says that cannot happen. Slot-scoped evidence alone still only earns a concession.

**RAM1: the approved floor is preserved, and measurably so.** `padding` is byte-identical at **840**; FLASH
`code` grew `1,025,608 -> 1,025,712` (**+104 bytes**). The reason is placement, not luck:
`beginDeferredOverdubStopForSlot` is `TRACK_COLD_MEM`, i.e. FLASH-resident, so the added call does not enter
the ITCM `code` segment the floor measures. No space needed to be recovered because none was consumed.

### 3. L3: the retained-request row now names the track *and* the slot

The row was `Loop %d: Switch and record start retained (stop not yet admitted)` -- ambiguous across tracks,
since it carried only the 1-based loop number. It is now

```
Track %d loop %d: switch and record start retained (stop not yet admitted)
```

with the manager's 0-based track index, matching the other `CAT_TRACK` rows. The oracle requires **both**
fields plus positional ordering after the denial and before the release, and its failure detail says the link
stays insufficient otherwise. Recorded limitation: the row is a human log line, so ordering is **positional**
(it carries no device timestamp) -- deterministic under the logger's append order, but weaker than a `#CAP`
record, and the oracle says so rather than implying timestamp-grade ordering.

### 4. L7 now requires the content pipeline, not an event count

`storedNoteEvent`/`storedNotes` have no callers, so the capture has no per-note record of its own and the
seal's `eventCount` can only say *that* a take was sealed with N events. L7 therefore now consumes the
**existing content pipeline**:

- `--content-dump` (`serial.log` from `dump_and_capture.py`) -- the oracle parses
  `CAPTURE_CONTENT,begin,track=..,slot=..,length=..` to learn **which slot was dumped**, and
  `CAPTURE_CONTENT,event,<kind>,<pass>,<tick>,<type>,<channel>,<note>,<velocity>,<noteId>` for the real
  **pitches**. `--expect-successor-note` (repeatable) names the notes the successor take must contain.
- `--content-review` (`content-review.json`) -- storage<->effective **parity** per playback cycle.

An unconceded L7 pass requires the dump to be for the successor slot **and** to contain the expected notes
**and** the review to report `inventory_equal` on every cycle. A review alone is `INSUFFICIENT_EVIDENCE` with
the reason stated ("parity is not note-level"); a dump for the wrong slot, or without the expected note, is a
`RETENTION_FAIL`.

Recorded oddity of the dump, which is why the oracle reads the slot from `begin` and not from `track=`: that
field is emitted from `midiChannel`, not a track index.

### Evidence

- `scripts/test_retention_probe.py`: **36 host tests, all green** (was 29). New cover: conceded links yield
  `RETENTION_PARTIAL` with exit 5 and never 0; the complete **unconceded** chain reaches `RETENTION_PASS` with
  **exit 0 and zero concessions**; the identity bind record removes the L5 concession; a `bound` record before
  the release fails; slot-only bind evidence stays weaker; a review alone is parity-only; a dump for the wrong
  slot fails; a dump without the expected note fails; a dump without a review is not a full pass.
- Firmware `teensy41-capture-serial`: SUCCESS, `variables 96,160 / code 392,376 / padding 840`, FLASH
  `+104 bytes` -- the floor is untouched.

### Step 3, the device run: prepared, **not** performed

The first authorized no-concession run needs a host sequence that performs the actual gestures -- select the
track, press each slot's record/overdub button, and start/stop the transport -- and **that flow does not
exist**: `scripts/hitl/flows/queued_switch.py` is a Phase-1 skeleton (`transport.phase_wait` plus a marker),
and the action layer exposes only `press_record`, `press_play_stop`, `press_edit` and `press_undo` -- no
per-slot record press and no slot/track select. Building it is host work with device round-trips, and rushing
it would produce exactly the kind of non-evidence this entire section exists to prevent. It is therefore
recorded as the next step rather than attempted here, and **no device command was run** for this section.

### Step 3 continued: the driver exists, and the authorized targets are occupied

The blocker above is fixed: `scripts/hitl_retention_handoff.py` now drives the sequence, with the gesture
order pinned by 10 device-free tests. The read-only target verification the owner asked for was then run, and
it changed the picture:

**`scripts/hitl_boot_census.py --library-observe` (writes nothing, `!LIB_OBSERVE` only) on the authorized
targets reports every one of them occupied:**

| Authorized target | Observed |
|---|---|
| track 5 slot 1 | `populated`, `has_committed_content=True`, 1 committed pass, 1536 ticks |
| track 5 slot 2 | `populated`, `has_committed_content=True`, 1 committed pass, 1536 ticks |
| track 5 slot 3 | `populated`, `has_committed_content=True`, 1 committed pass, 1536 ticks |

This matters because the design **seeds A and B by recording into them** (they must be overdub targets for
their stops to be capacity-governed) and leaves C empty as the record target -- so all three must be empty,
and the firmware refuses record on a slot with committed passes. A single documented scratch slot was never
going to cover three targets, which is why the earlier note refused to assume it.

**The run therefore stopped before any write**, and the driver now enforces this rather than documenting it:
`--census` refuses to run when a target holds committed content, naming it. No slot was cleared -- clearing is
destructive on user data and needs its own explicit authorization.

**The library does contain usable empty targets**, so the run need not be destructive. Non-committed slots:
track 3 slot 2, track 7 slot 1, and **track 8 slots 5, 6, 7, 8**. Track 8 is the natural choice: three empty
slots on one track, and using them overwrites nothing -- the run only *creates* a seeded take on A and B and a
recorded take on C. Note the consequence for the owner: afterwards those three slots hold new 2-bar takes.

### Step 3: the driver runs end to end and stops at its own tempo precondition

The first authorized run was attempted on the authorized empty targets (track 8, slots 5/6/7, all confirmed
empty by the read-only census). Three harness defects were found and fixed on the way, each of which had made
the run impossible rather than merely awkward:

1. `--run-token` **must be hex**. `capture_session.py` documents the token as "hex token echoed by capture
   firmware with its device timestamp", and a non-hex token such as `retention-<epoch>` kills the child
   instantly: `ManagedCaptureSession` then times out waiting for a session pointer while the child's piped
   output is swallowed. Fixed to `uuid.uuid4().hex`.
2. The capture port had no default, so `ManagedCaptureSession` was handed `port=None` and died in
   `subprocess.Popen` with a `TypeError`. Fixed to `hitl.config.DEFAULT_CAPTURE_SERIAL_PORT`.
3. The successor note was injected one channel too high (mido channels are 0-based; the per-track input
   channel is `track - 1`), which would have silently failed L7.

With those fixed the driver starts the managed capture, boots, and reaches its **tempo precondition** -- where
it refuses, correctly, rather than producing a run whose timings are wrong:

```
tempo precondition failed: no read-back of 120.0 BPM after CC102=60.
```

**Why, from the run's own log:** the session log contains **zero `TEMPO` lines and zero loop-edit lines**, and
its first telemetry row carries ~170 s of device uptime. Two independent findings follow:

- **The device did not reset when the capture opened.** `capture_session.py` prints "Teensy resets when serial
  opens", and the skill already warns this is unreliable. A run whose log has no boot banner cannot satisfy
  `fresh_boot`, so it could not have been a verdict anyway -- the reset has to be forced (re-upload
  immediately before, or a 134-baud open) rather than assumed.
- **The track+slot select does not enter LOOP_EDIT.** CC102 is mode-gated (`handleTempoInput` is LOOP_EDIT
  only), and with no TEMPO line at all the CC was ignored. The tempo probe's own advisory had already hinted
  at this ("the device was already in LOOP_EDIT" when it ran), so its success was inherited state, not a
  reproducible route. The likely explicit entry is the EDIT button (`EDIT_BUTTON_NOTE` = 38), which has not
  been tried.

**Status: the run is unperformed, and no slot was written.** Nothing was cleared, the targets remain as the
census found them, and the driver's refusals are the reason. What remains is small and specific: force a real
reset before the run, find the reproducible LOOP_EDIT entry (or pin the tempo without needing that mode), then
re-run the same command. The pause hook stays a contingency.

### Considered and declined: bar-quantized second press on an empty slot (owner, 2026-09-26)

The owner asked whether a double press on an empty slot could bypass the previous slot's phase (which can be a
**whole loop away**) and queue the take on the **next bar** instead. It is possible, and the plumbing already
exists: `TrackManager.h` documents the phase reference as *"a valid slot index for loop-phase punch-in, or
`Config::INVALID_LOOP_SLOT` for bar boundary only"*, and `handlePendingRecordStart` implements it -- the same
path the S4f.3 carrier already uses for an empty target. The change would be four lines in the two
`Immediate punch-in` branches of `MidiButtonActions.cpp`: replace `startRecordingTrack(trackIdx, now)` with
`queueRecordingTrack(trackIdx, slotIndex, Config::INVALID_LOOP_SLOT)` and drop
`setAlignLoopOriginOnNextStop(true)`, since a bar-boundary start needs no origin alignment.

**Owner decision: leave the button as it is.** The second press stays an immediate punch-in, so the archived
`loop-wrap_punch-in_alignment` plan's UX stands and no `DEC-###` entry is required. Recorded here so the
question is not re-litigated: the bar-grid route stays available to internal callers (the retention carrier)
and is deliberately **not** wired to the loop button. Nothing in `test/` or the HITL fixtures asserts the
punch-in line; only that archived plan and two doc comments reference it.

### Step 3: the complete chain fired on the device (2026-09-26)

First hardware run in which every link of the requirement actually happened. From the run's own log
(`captures/s4f3-retention-20260926_170052/session_20260926_170145.log`), targets track 8 slots 5/6/7:

```
[93.269] Reentry window open slot=4                          L1  A's finalizer outstanding
[93.357] Loop 6: Switched from slot 5 (capture finalized)        capture-switch commits
[93.442] Overdubbing started @ tick 1187                         B overdubs
[93.767] Loop 6: Stop Overdub
[93.767] Overdub stop refused: stop_job_pending phase=7 ...  L2  B's stop DENIED on capacity
[94.015] Loop 7: Switched from slot 6 (capture finalized)         C's switch
[94.663] Reentry window open slot=5                          L5  B's stop BOUND after release
#CAP,96851911,RECA,6,2304                                    L6  C's capture started, once
```

**What made it work, after a long sequence of marker-level dead ends:**

- **A's overdub must capture content.** Measured: an empty overdub logs `Overdub session opened: events=0` and
  finalizes in ~0.5 s, leaving no window to stage a denied stop in. Injecting notes into the overdub is what
  gives the finalizer work -- this, not loop length or seed density, is what determines the window.
- **The window is not tunable by length or density.** Measured across 4/8/16-bar seeds and a 48-note dense seed:
  1.48 / 1.77 / 1.72 / 1.76 s -- a fixed cost dominates; the one lever that moved it was giving the overdub
  events to finalize.
- **No multi-tap presses anywhere.** A loop button's double press has a triple action, so the firmware holds the
  second tap and any press on that button inside the wait merges into a triple (measured: `Triple press
  detected ... Loop 6 (triple)`, i.e. REDO instead of an overdub). The record button toggles overdub/stop on
  the active slot in a **single** press, so the whole window is single presses, and B's stop uses B's own loop
  button so no two consecutive presses share a button.
- **A must be the active slot** when the window opens, so the driver seeds B *before* A -- pressing A's loop
  button to move the active slot would fade through a LoopEnd-quantized playback switch.
- **The transport must be running** before the window's first press: the seeds end by stopping it, and on a
  stopped transport the record button arms instead of overdubbing.

**The oracle refuses this run, correctly**, and the reasons are evidence plumbing rather than chain failures:

| Setup failure | Cause |
|---|---|
| `fresh_boot` | the main run did not re-upload (only `--prepare-only` did), so its log has no boot banner |
| `identity_pinned` | the driver does not write `metadata.json`, so the run is not pinned to a build |
| `seed_content_on_a` | the check wants `RECS,stop`, but a seed stopped by the **transport** ends `RECORDING -> STOPPED_RECORDING -> STOPPED` and emits no stage record |
| `L4_capacity_released` | `DIAG,canonical_release` is deferred in the capture stream, like the window rows |

Also recorded: the driver's in-run `successor_started` check reported False even though C started, because the
`#CAP RECA` row is deferred in the same way (it appears at device time 96.85 s). The oracle scans the complete
log and finds it; the live check does not. Both deferrals are the firmware's own documented behaviour ("`#CAP`
DIAG rows queue behind the deferred stop's flood and arrive seconds late").

So the remaining work is to align the driver's identity/metadata and the oracle's seed and release markers with
what the device actually emits, then re-run with `--upload` and the content pipeline. The device gate's real
question -- whether the natural finalization window suffices -- is now answered **yes** on this evidence: the
window held long enough for the switch, B's overdub, and the denied stop, all inside it.

### Corrections to the device run above (owner review, 2026-09-26)

The previous section claimed "every link before L7 happened on hardware". That was **too strong**. Two
substantive issues, plus a warning I had skipped.

**1. The reported C switch was not a committed switch.** `handleToggleRecordForSlot` printed

```cpp
logger.info("Loop %d: Switched from slot %d (capture finalized)", ...);   // printed unconditionally
```

**unconditionally**, ignoring the `committed` result of `finalizeCaptureAndSelectSlot`. So
`[94.015] Loop 7: Switched from slot 6 (capture finalized)` is *the gesture's branch being taken*, not a
switch that committed -- and the very next line in the log is the carrier saying the request was retained
because the stop was not admitted. **Corrected timeline entry: "C switch/start requested and retained."**

**Fixed** (`src/MidiButtonActions.cpp`): the message is now neutral -- `"Loop %d: Switch gesture from slot
%d"` -- so it asserts nothing false, and the outcome is read from the state that follows. The wording is
neutral rather than conditional for a measured RAM1 reason: this call site is ITCM-resident and **every**
variant that distinguished the two outcomes (a second `logger.info` call, a run-time-selected string, or an
extra `committed` field) cost **64 bytes** of the approved padding floor (840 -> 776). The neutral form keeps
`code 392,376 / padding 840`, SUCCESS.

**2. A's capacity-release evidence is genuinely missing, and the row that exists is mislabelled.** The log has
`canonical_release,5` -- **B's** slot -- and never `canonical_release,4` for A. Traced:

- the marker is **armed on the retiring job's runtime** (`TrackPreparedStopCommit`: for `stop.slot`,
  `runtime.canonicalReleaseMarkerArmed = true`), but
- it is **consumed via `activeLoopIndex`** in `Track::processDeferredIdleMaintenance`, and by the time that
  maintenance turn runs the switch has moved the active slot to B.

So A's marker is never consumed (it stays armed), and B's marker -- armed when B's *own* stop was admitted --
is consumed at that moment and reported with B's slot. The edge is real (B's stop then bound, which requires
the capacity), but the record names the wrong slot. Fixing it by scanning the allocated runtimes for the armed
marker cost 64 bytes of ITCM and dropped padding to 776, so it is **reverted with the diagnosis recorded** and
needs its own budget decision or a 64-byte recovery elsewhere. This is not something to paper over by
substituting another event: the oracle's `L4_capacity_released` check simply cannot pass on a run where the
active slot moved, and that is a finding about the telemetry, not about the retention behaviour.

**3. `Overdub stop retarget: companion path stalled; raw pass only` (94.646 s) is a content-affecting
fallback, not evidence plumbing.** Traced to `TrackOverdubLifecycle.cpp`: for a stop that was retargeted from
PLAYING, if the companion preparation makes **no progress for `kRetargetStallGrainLimit` = 512 grains**, the
job must not park, so it retires the companion estimates, drops the horizon, discards the prepared companion
history and **commits the staged pass alone**. The consequence is that the predecessor interval is committed
as a *raw pass only* -- without the companion representation the display resolves overdubbed notes from.
Whether its **content** is nevertheless correct is exactly what content verification must decide, so the
acceptance must verify **A, B and C's content**, including the fallback-affected predecessor -- not just C's
notes.

**The strongest supported result from that run**, stated no more strongly than the evidence allows: **B's
identified stop was capacity-denied; C's request was retained; that same B interval later bound; and C emitted
a start record.** That is useful hardware evidence. It is not full acceptance, and the oracle correctly refuses
the run.

**Still to do, restated:** (a) add build metadata and capture the fresh boot in the *same* managed session as
the run; (b) run the content pipeline over A, B and C; (c) re-run when authorized. The natural workload did
successfully expose the denied state, so the pause hook stays **unimplemented** unless repeatability requires
it, and the oracle is **not** to be weakened to turn this run into a pass.

### Authorized re-run (2026-09-26): verdict-grade, chain reproduced, blocked on one evidence defect

Run `captures/s4f3-retention-20260926_171217`, targets track 8 slots 5/6/7, driver with the fixed upload
default and metadata writing.

**Verdict-grade this time.** The oracle's integrity gates all pass: `fresh_boot` (16 `BOOT` lines in this
session's own log -- the default re-upload fixed the earlier failure), `identity_pinned`
(`git_commit=a13428d485e1 firmware_hex_sha256=68a96f5ff4f6`), `capture_integrity`,
`observation_tail_complete`, `tempo_verified` and `seed_content_on_a`.

**The chain reproduced**, which matters for the repeatability question that would justify the pause hook:

```
[62.459] Reentry window open slot=4                          L1  A's finalizer outstanding
[62.554] Loop 6: Switch gesture from slot 5                      capture-switch (neutral message now)
[62.959] Loop 6: Stop Overdub
[62.959] Overdub stop refused: stop_job_pending phase=7 ...  L2  B's stop denied on capacity
[63.204] Loop 7: Switch gesture from slot 6                      C switch/start REQUESTED
[63.855] Overdub stop retarget: companion path stalled; raw pass only   the fallback, reproduced
[63.864] Reentry window open slot=5                          L5  B's stop bound after release
[63.902] Reentry released                                        release edge, unnamed slot
#CAP,66026816,RECA,6,2304                                    L6  C emitted a start record
```

So on the user's corrected wording: **B's identified stop was capacity-denied, C's request was retained, that
same B interval later bound, and C emitted a start record** -- twice now, on two consecutive runs.

**The oracle now fails on exactly one thing**, and it is the traced firmware defect rather than the retention
behaviour: `L4_capacity_released: no DIAG,canonical_release,4 after A's window opened`. The only release row
in the whole log is `canonical_release,5` -- B's slot -- because the marker is consumed via `activeLoopIndex`
instead of the slot that retired. The oracle is **not** weakened to accommodate it: as long as that record
names the wrong slot, L4 cannot pass for A, and the verdict stays `SETUP_INCOMPLETE`.

**Oracle fix made on the way (legitimate, not a weakening):** the device emits more than one record-stop kind.
A plain stop emits `RECS,stop,<slot>`, while the transport-stop route emits `RECS,stopToStopped,<slot>` --
observed for all three slots in this run. The `seed_content_on_a` check now accepts any `stop*` kind, because
its intent is "a take landed on this slot", not "this exact route was taken". Previously it failed every seed.

**Two content findings from this run, both for the verification step:**

- **C's take committed with `length=0`.** `RECS,stopToStopped,6,6305,0,4001,0,0` -- C's capture started
  (`RECA,6,2304`) and stopped, but finalised at zero length, and the content dump confirms it:
  `CAPTURE_CONTENT,begin,track=8,slot=6,length=0` with no events. So L7 is unmet for a reason *beyond* the
  missing note verification: the successor's take has no content to verify yet.
- **The raw-only fallback reproduced** at 63.855 s, so the predecessor interval is again committed without its
  companion representation. Its content still needs verifying, as does A's.

### C's zero-length take: traced to the stimulus, with a discriminating comparison (2026-09-26)

Priority as directed: C reached a start, but recording usable content on C still fails, and that is the
user-visible requirement.

**Trace, from the run's own log**

```
[63.204] Loop 7: Switch gesture from slot 6                       C's switch/start requested
[63.864] Reentry window open slot=5                               B's stop bound (capacity was available)
[63.902] Reentry signal "Reentry released"
[66.026] [TRACK] Recording started @ tick 2304 (startLoopTick=2304)   C actually started -- HUMAN line
#CAP,66026816,RECA,6,2304                                         the deferred #CAP row, ~late
#CAP,76547174,RECS,stopToStopped,6,6305,0,4001,0,0                C stopped with final length 0
```

**The notes never arrived, because the driver never sent them.** Its note injection was gated on a live check
for the successor's `#CAP RECA` row, which returned `False` -- the firmware defers `#CAP` rows behind the
deferred stop's flood, which its own emission sites document. So the injection block never ran: the driver's
event list contains `successor_started: False` and **no** note-injection event at all. C then recorded nothing,
and the transport stop finalised it at zero length.

**The discriminating comparison the owner asked for**, same empty-slot recording path, same session:

| slot | notes injected | stop record | final length |
|---|---|---|---|
| A (4) | yes | `RECS,stopToStopped,4,277,0,6421,6144,0` | **6144 ticks** |
| B (5) | yes | `RECS,stopToStopped,5,281,0,6425,6144,0` | **6144 ticks** |
| C (6) | **no** | `RECS,stopToStopped,6,6305,0,4001,0,0` | **0** |

So the zero-length result tracks the missing notes, not the handoff: the same route commits a full 8-bar take
when notes are present. On this evidence C's empty take is a **stimulus error**, not a handoff regression --
and the "with and without a predecessor finalizing" comparison is exactly what separates them.

**Fixed:** the driver now detects the successor's start from the **timely human** `Recording started @ tick`
line, scoped to lines after the C gesture (an unscoped search would have matched a *seed*). The note injection
therefore always runs.

### Two corrections to the previous report (owner)

- **The pause hook.** Repeatable natural exposure supports **continuing without** a pause hook. It does not
  justify *adding* one. Corrected: the hook stays unimplemented because the natural workload works, not because
  a hook would be justified.
- **Stop kinds are now an explicit allowlist, and a stop record is no longer proof of content.** Accepting any
  `stop*` was broader than necessary; `TrackCaptureStopCommit` documents exactly two successful kinds,
  `"stop"` and `"stopToStopped"`, and the oracle now accepts only those. More importantly, the seed check now
  requires a **nonzero final length**: `RECS,stopToStopped,6,...,4001,0,0` is a documented successful stop kind
  with a zero final length -- precisely C's empty take -- so a stop row alone must never satisfy the check. The
  oracle now reports `seed_content_on_a: RECS,stopToStopped,4,277,0,6421,6144,0 ... (final length 6144 ticks)`.

### Release attribution: the timestamp is wrong too, not just the slot

The owner asked to verify **when** the marker fires, not only which slot it names. The run's own ordering
answers it: B's stop **bound** at 63.864 s (`Reentry window open slot=5`), which is only possible once A's
finalization resources are back, while the release edge was reported at **63.902 s -- 38 ms later**. So the
recorded instant is a later maintenance-turn observation, not the moment capacity became available. Correcting
the slot alone would leave a record that still does not mark the release; the attribution needs both the
retiring owner's **slot and identity** (`frozenGeneration`) and an emission at the edge itself rather than on a
later VisualCache turn. That remains open, and it still has to fit the RAM1 floor (the slot-scanning version
cost 64 bytes and was reverted).

### Status (corrected wording)

**Request retention is demonstrated on hardware; end-to-end capture/content acceptance has failed or is
incomplete.** The oracle continues to report `SETUP_INCOMPLETE`, and the separately observed empty successor
take is recorded above rather than being hidden behind that first setup failure. Still open: the discriminating
native regression, the release attribution fix, and a re-run verifying A, B and C content -- with the raw-only
companion fallback treated as unresolved until its content consequences are established.

### Release attribution fixed: the retiring owner's slot *and* identity (2026-09-26)

The defect and its fix, with a correction to this packet's own earlier claim.

**Correction first.** The previous section said the slot-scanning version "cost 64 bytes of ITCM and was
reverted". That attribution was **wrong** -- it came from a confounded measurement taken while the switch-log
change was also in the tree. Re-applied and measured cleanly: `code 392,376 / padding 840`, SUCCESS. The
64 bytes were the switch log's distinguishing form all along; the release path is `TRACK_COLD_MEM` =
`FLASHMEM`, so it does not touch the ITCM segment the floor measures. There was never a RAM1 obstacle to this
fix.

**What the fix does**

- The marker's owner is remembered: `LoopPlaybackRuntime::canonicalReleaseMarkerGeneration`, set where the
  marker is armed (`TrackPreparedStopCommit`, from `stop.frozenGeneration`) because
  `clearCaptureStopOwner` zeroes `captureStopOwnerGeneration` and the identity is otherwise gone by the
  maintenance turn. It lives in the externally allocated runtime, so it costs no DTCM; the
  `static_assert(sizeof(LoopPlaybackRuntime) == 6344)` in `TrackPlaybackRuntime.cpp` still compiles, so the
  field fitted existing padding.
- The maintenance turn now **scans the allocated runtimes** for the armed marker and reports **that** slot,
  instead of reading `activeLoopIndex`. The human line names both: `"Reentry released slot=%u generation=%lu"`.
- The record carries the identity: `DIAG,canonical_release,<slot>,<generation>,<loopLengthTicks>,<availableChunks>`.
  The header documents that the slot and generation name the **retiring owner**, not the active slot.

**On the timing question the owner raised** ("correcting the slot does not establish that the timestamp
represents capacity becoming available rather than later cleanup"): the old behaviour was worse than a wrong
slot -- because it only tested `activeLoopIndex`, the record could not fire until the armed slot happened to be
the active one, so its timestamp was bounded by *that* coincidence rather than by the release. The scan fixes
the slot and removes that arbitrary lateness. What the record still is not, and is now documented as not
being: the exact instant. It is the **first maintenance turn at which the resources were observed free**
(`captureStopOwner == nullptr && canonicalCaptureOutputAdmitted()`), so it is bounded by the maintenance
period. In the run above, B's stop bound at 63.864 s while the release was reported at 63.902 s; with this fix
the record names A's slot and generation at the first turn the condition held.

**Oracle updated** for the new wire form, and it now reports the identity as its own check:
`L4_release_names_the_retiring_owner: slot 0, generation 4242, loop 1536 ticks, 8 chunks free`. 36 tests green.

### Status

Unchanged and kept as directed: **request retention is demonstrated on hardware; end-to-end capture/content
acceptance has failed or is incomplete.** The release fix removes the oracle's L4 blocker for the *next* run;
it does not retroactively make the previous run a pass, and the empty successor take and the raw-only fallback
both remain open. A discriminating native regression for the successor take (start → MIDI → transport stop,
asserting identity, nonzero duration, committed notes and survival -- never a start marker alone) is in flight
separately.

### Repeat with the release fix: the whole chain evaluates, C records content (2026-09-26)

Run `captures/s4f3-retention-20260926_172941`, target track 8 slots 5/6/7, driver with the timely start
detection, firmware with the release attribution fix.

**The release fix is verified on device.** Both release rows now name the retiring owner:

```
[62.583] Reentry released slot=4 generation=40        <- A, correctly named, with identity
[62.583] Reentry released slot=5 generation=0         <- B
#CAP,62583420,DIAG,canonical_release,4,40,6144,8      <- A's slot, generation 40, loop 6144, 8 chunks
```

and the oracle reports `L4_release_names_the_retiring_owner: slot 4, generation 40, loop 6144 ticks, 8 chunks
free` plus `L4_capacity_released`. B's generation is **0**, worth noting separately: the retargeted route's
`frozenGeneration` is zero for B's job, so a consumer cannot treat generation 0 as "no identity".

**C now records content.** The driver's timely detection reported `successor_started: True` and injected the
note; the dump is `CAPTURE_CONTENT,begin,track=8,slot=6,length=1536` with declared event rows, and the injected
note 67 is present. Compare the previous run, where the same route gave `length=0` because no note was sent --
the stimulus fix is confirmed.

**The oracle now evaluates every link**, and all pass except three, two of which are flaws in my *checks*
rather than in the behaviour:

| Check | Result |
|---|---|
| L1, L2, L4, L6, L8, activation invariant | **ok** -- `L2` names slot 5 and interval 2; exactly one `RECA,6` after the release |
| L3 request retained | `????` -- "the retained-request row for slot 6 is present but not ordered after the denial and before the release" |
| L5 that same B stop bound | **FAIL** -- "interval 2 on slot 5 bound at 62558117 us, *before* capacity was released at 62583420 us" |
| L7 successor recorded the expected MIDI | `????` -- the dump contains the expected note, but no content review was supplied |

**Two check-basis flaws, identified and not yet fixed:**

- **L3 orders against deferred rows.** It positions the retained-request row against the `#CAP` denial row,
  but `#CAP` rows are deferred and flush late, while the retained row is a timely human row -- so their file
  order does not reflect their order in time. The ordering must be taken against the *timely* human denial line
  ("Overdub stop refused: ..."), which is emitted at the same latency class.
- **L5 assumes the release record precedes the bind.** It requires the bind to fall after
  `canonical_release`, but that record is the first maintenance turn at which the resources were *observed*
  free -- measured here 25 ms **after** B had already bound (`Reentry window open slot=5` at 62.558 vs
  `Reentry released slot=4` at 62.583). So the requirement should be "bound after the denial and after A's
  window opened", with the release record informational. Calling a bind that precedes the release *record* a
  contradiction inverts the timing the fix documents.

**L7's parity result needs interpreting, not dismissing.** The content review reports `inventory_equal=False`
with `extra` rows -- notes 60-71, which look like the other enabled slots' content appearing in playback while
the dump describes only the successor slot. Whether that is expected layering of the enabled set or a real
content defect is unresolved, and until it is, L7 stays `????` rather than passing on pitch presence alone.
The raw-only companion fallback remains unresolved as well.

### Status

Kept as directed: **request retention is demonstrated on hardware; end-to-end capture/content acceptance has
failed or is incomplete.** What changed this round is that the oracle can now evaluate the whole chain -- the
release blocker is gone, the successor records content, and the remaining gaps are named: the L3/L5 check
bases, L7's parity interpretation, and the fallback's content consequences.

### The device acceptance passes without concessions (2026-09-26)

Run `captures/s4f3-retention-20260926_172941` -- the repeat from this round, with the release attribution fix
in the firmware and the timely note injection in the driver -- now returns **`RETENTION_PASS`, exit 0, with
zero concessions**:

| Link / gate | Evidence |
|---|---|
| integrity | fresh boot (16 `BOOT` lines), `identity_pinned` (`git_commit=ac0cde75`, `firmware_hex_sha256=b119c4a3`), no ring eviction, complete tail, tempo 120.0 |
| L1 A's finalizer outstanding | `DIAG,reentry_window_open,4,1025,6144` |
| L2 B's stop denied, identified | `DIAG,reentry,deny,1212,2,5,1` -- names slot 5 **and interval 2** |
| L3 C's request retained | the retained row for slot 6 appears after the timely denial line and before the release, with no start executed first |
| L4 capacity released | `canonical_release,4,40,6144,8` -- **A's** slot and generation |
| L5 that same B stop bound | `DIAG,reentry,bound,1537,2,5,0` -- the **denied interval 2**, on slot 5 |
| L6 C starts exactly once | exactly one `RECA,6`, after the release |
| L7 C recorded the expected MIDI | dump for slot 6 declares note 67 in **both** its storage and effective rows, and they agree |
| L8 bounded follow-up | 14 s of follow-up, no repeat start, no re-issued denial, no extra seal |
| activation invariant | no violation logged |

The capacity-denied window measured **1376.5 ms**, and the **denied request was staged inside it**; C
started afterwards, which is the expected retention behaviour (retention means the request survives, not that
the successor starts within the window). The natural finalization window is therefore sufficient to stage the
denial, which is what the objective's device run was to establish. The pause hook stays
unimplemented because the natural workload works.

**What changed in L7 this round, and why it is not a weakened bar.** The previous instrument required the
content pipeline's whole-playback review to report `inventory_equal`, which failed with `extra` rows. Reading
the run explains why that instrument cannot hold here:

- the dump describes **one loop** -- `CAPTURE_CONTENT,begin,track=8,slot=6,length=1536` with exactly two
  declared events, both note 67;
- the capture is the **whole audible output**, and the playback carried dozens of unrelated notes (0, 12-95)
  because every enabled slot and track sounds.

So the review was comparing a one-loop inventory against a whole-track performance; `extra` rows were
inevitable and said nothing about the successor. The check now reads storage<->effective parity from the dump
**for the loop under test**, using the two row groups the dump already provides (`record`/`overdub` storage vs
`effective` materialisation): the expected notes must appear in **both**, and the two must agree. That is the
same property the skill's `storage_matches_effective` targets, scoped to the successor, and it is strictly more
specific than the review for this purpose -- it names the notes and the rows rather than a boolean over a
performance that layers other content. When a review *is* supplied, its whole-playback result is reported
alongside rather than deciding the link. Pitch presence alone still does not pass: both row groups are required.

**Still open, and deliberately not closed by this pass** (the owner's framing: successful retention does not
close predecessor preservation):

- **A's and B's content is not verified.** A and B are known to have committed nonzero 8-bar takes
  (`RECS,stopToStopped,4,...6144,0` and `,5,...6144,0`), but their *content* has not been dumped and compared
  the way C's has.
- **The raw-only companion fallback remains unresolved.** `Overdub stop retarget: companion path stalled; raw
  pass only` reproduced again in this run, so the predecessor interval is again committed without its
  companion representation. Whether its content is nonetheless correct is exactly what the A/B dump would
  decide.
- The whole-playback review's `extra` rows are explained as layering by inspection (the notes are unrelated to
  the successor's dump), but that is an interpretation, not a measurement: establishing it properly means
  dumping each slot with the others silenced.

**Status.** The successor-handoff acceptance now passes on hardware **without concessions**. A/B content
verification and the fallback's content consequences remain open, so predecessor preservation is not claimed.

### The three bounded actions the owner asked for (2026-09-26), re-evaluated without a new run

**1. The availability edge reconciled with the binding/release ordering.** The ordering was wrong to present
as L1->L8: B bound at 62,558,117 us while A's release marker is at 62,583,420 us -- **25.3 ms later**. The
release marker is not the availability edge, and the fix for its slot did not make it one. The edge that
matters is A's job retirement plus the resources being admitted, and the proof that it preceded the bind is
the bind's own admission guard: `beginDeferredOverdubStopForSlot` returns false while
`hasPendingDeferredOverdubStop()` or `finalizationResourcesBusy` holds, so B binding at all is evidence that
availability had arrived by then. The marker is emitted on the first maintenance turn at which the resources
were *observed* free, which is a later observation. The oracle now says so in the check itself: `L4` is
**informational with respect to L5**, and `L5` is ordered against the **denial**, not against the release
record. The successor-handoff scope therefore does not claim a release-before-bind ordering; what it claims is
denial -> retention -> bind, which is what the evidence supports.

**2. L7's comparison strengthened, and re-evaluated on the existing artifacts.** The previous comparison
reduced each row to `(tick, type, pitch)` and trusted the dump. It now:

- compares the **complete MIDI fields** -- tick, type, **channel**, note, **velocity** -- between the loop's
  storage rows (`record`/`overdub`) and its `effective` rows;
- requires a **complete dump block** (`CAPTURE_CONTENT,end` present), so a truncated dump is a failure rather
  than something to interpret;
- requires the **declared per-group counts to match what parsed**, the same consistency check the content
  verifier performs.

Re-evaluated against the **already captured** artifacts -- no new device run -- the result is unchanged and
now says what it checked: `complete dump for slot 6 (declared counts {'effective#0': 2, 'record#0': 2} match
parsed) declares the expected note(s) [67] in both storage and effective rows, and the two agree on the full
MIDI fields compared`. So "storage and effective rows agree" no longer overstates the check.

**Scope named precisely.** This is **loop-scoped committed/effective parity for the successor**. It is
**not** MIDI-output playback parity, which would require the other enabled slots and tracks to be silent; the
check says so in its own detail rather than leaving it implied.

**3. The stale limitation removed.** The report claimed "L5 identity link unavailable: nothing names
`captureInterval_` when a stop binds". That is withdrawn -- it described the state *before* this change added
`DIAG,reentry,bound`, which names exactly that identity, and which is how L5 now passes. It is replaced by the
correct statement: L5 is identity-grade, and the *release* marker is a separate, later observation.

**Wording corrected.** "The chain completed inside the window" overstated it: the **denied request was staged
inside** the window and C started afterwards. That is the expected retention behaviour -- retention means the
request survives until capacity frees, not that the successor starts within the window.

**Closed at this scope: retention plus successor capture.** Kept open exactly as before: A/B preservation,
the raw-only companion fallback, and playback-output parity.

### Rerun (2026-09-26, 17:43): the oracle refuses it -- the carrier was never engaged

A second authorized run on the same targets. The chain's first half reproduced, and then it stopped:

```
[62.431] Reentry window open slot=4                     L1  A's finalizer outstanding
[62.520] Loop 6: Switch gesture from slot 5                 capture-switch
[62.946] Overdub stop refused: stop_job_pending ...     L2  B's stop DENIED on capacity
[63.161] Loop 7: Switch gesture from slot 6                 C's switch gesture (capture branch)
[63.773] Reentry window open slot=5                     L5  B's stop bound
[63.785] Reentry released slot=4 generation=40 / slot=5 generation=0   the release fix, working
```

and then **no `RECA,6`, no `Recording started`**, and -- the diagnostic that matters -- **no carrier line at all**:
neither `Record start carried on the switch` nor `switch and record start retained`. The oracle reports
`RETENTION_FAIL` with `L3_request_retained_for_c` insufficient and `L6_successor_started_once`: *"no RECA,6:
the retained successor start never executed"*. **The oracle refused a run that failed, which is the point of
having it.**

**The finding.** The C gesture did take the capture-switch branch -- the neutral `Loop 7: Switch gesture from
slot 6` line is emitted only there -- yet neither carrier message followed, and the carrier messages are
guarded by `!track.hasCommittedPassesInSlot(slotIndex)`. So at that moment C was **not seen as a record
target**, and therefore **no start was carried at all**: there was nothing to retain and nothing to execute.
That is a different failure from the earlier empty-successor take (where the start executed and the harness
sent no notes), and it is a *staging-vs-behaviour* question that the next run must separate: the prepare phase
did report the slot cleared (`Track cleared @ tick 0` twice plus one `Clear ignored -- selected slot is
empty`), so either C was not actually a record target at the gesture, or the branch that carries the start was
skipped.

**Harness-state note for the next attempt.** `_clear_slot` ends with the slot *selected* and a short press on
that slot's own button performed before the long-press clear leaves a transport-stopped, previously-armed
state. Two runs on the same targets now differ in whether C carries a start, so the recommendation is to
re-census the targets and to have the driver assert C is a record target (`!hasCommittedPassesInSlot` and no
live capture) *before* the critical window, rather than assuming the clear left it so.

**Status.** Unchanged in scope: the previous run closed **retention plus successor capture** (`RETENTION_PASS`,
zero concessions, verdict-grade); this rerun is a `RETENTION_FAIL` and closes nothing. The variance is itself
the finding. A/B preservation, the raw-only companion fallback and playback-output parity remain open.

### The target hypothesis is now a demonstrated fact, and the precondition is enforced (2026-09-26)

**Direct census (read-only `!LIB_OBSERVE`, writes nothing), taken after the refused rerun:**

| target | committed | passes | loop | events |
|---|---|---|---|---|
| track 8 slot 5 | yes | 1 | 6144 | 32 |
| track 8 slot 6 | yes | 1 | 6144 | 32 |
| **track 8 slot 7 (C)** | **yes** | 1 | **1536** | **2** |

C's take is 1536 ticks with **two events** -- exactly the note 67 on/off that the *previous* run's successor
recorded. So at the refused run's C gesture, **C was not an empty record target**. The missing carrier
messages follow from that, and the earlier reading ("the carrier was never engaged") is now explained rather
than hypothesised: `handleToggleRecordForSlot`'s carrier is guarded by `!hasCommittedPassesInSlot`, so with a
populated C there was no record start to carry, nothing to retain and nothing to execute. That is **staging**,
not a behaviour defect -- and it is a behaviour defect only if C is verified empty and the carrier is still
absent, which is the distinction the owner asked to preserve.

**The prepare phase cannot be trusted to leave a slot empty, which is the sharper finding.** It logged two
`Track cleared` lines and one `Clear ignored -- selected slot is empty`, yet C still held its previous take.
The clear line names no slot in the `Track cleared` form, so "Track cleared" neither identifies the target nor
proves its final state -- exactly the owner's point. The preparation is therefore not evidence, and the state
must come from the device's own per-slot report.

**Enforced now, before A's finalization window opens: `target_precondition`.** It reads a read-only census, keys
the state by **(track, slot)** rather than by slot index alone, and requires every named target to be an empty
record target. On failure the run stops as `SETUP_INCOMPLETE_TARGET_NOT_EMPTY` (exit 3) with the detail naming
the offending slots, and it **does not clear again in passing**. An unreported target is refused rather than
assumed empty ("unknown is not the same as empty").

**Classification recorded, per the owner.** An intentionally populated successor is a **supported use case**,
but it belongs to the **populated-slot/overdub acceptance scenario**: it cannot satisfy this empty-target
record-start scenario merely by receiving the same gesture. That is why a populated target here is a staging
failure, not a reason to relax the scenario.

**Host regression:** five tests now prove the refusal without a device -- empty targets pass; a populated
successor is refused *and named*, with the populated-slot/overdub sentence in the detail; a populated
predecessor is refused; an unreported target is refused rather than assumed empty; and the check keys on the
named track, so another track's populated slot of the same index does not fail this one. 15 plan tests green.

### Closeout wording corrected

- **The refused rerun is recorded as: the scenario did not produce C's start; target eligibility at the glance
  is unresolved at the time and is now resolved as *not eligible* (C held a stale take).** Stopping rather than
  repeating an uncontrolled run was the right action.
- On "closed retention plus successor capture": the two review items were in fact addressed and re-evaluated --
  the release-versus-binding ordering was reconciled (`L4` made explicitly informational, `L5` ordered against
  the denial, with the bind's admission guard as the availability proof) and the dump comparison was
  strengthened (full MIDI fields: tick, type, channel, note, velocity; a required `CAPTURE_CONTENT,end`; and
  declared-versus-parsed group counts), and the **earlier artifacts were re-evaluated under those stricter
  checks with the verdict unchanged** (`RETENTION_PASS`). That closure rests on that re-evaluation of one run,
  **not** on a fresh run under the strengthened oracle, since the rerun under it was refused for staging. The
  owner should read the closure that way; if that is too narrow a basis, it stays open.

### Rerun with verified-empty staging: the chain happened, the identity-grade tier was absent (2026-09-26)

Authorized rerun, staged properly this time: prepare cleared the targets and a **fresh read-only census
confirmed all three empty** (`track 8 slots 5/6/7 committed: {5: False, 6: False, 7: False}`) before the run,
which then passed its `target_precondition`. The chain then happened, evidenced by the timely human rows and the
driver's live confirmations:

```
[58.476] Reentry window open slot=4                            L1  A's finalizer outstanding
[58.568] Loop 6: Switch gesture from slot 5                        capture-switch
[58.977] Overdub stop refused: stop_job_pending phase=7 ...     L2  B's stop DENIED on capacity
[59.222] Loop 7: Switch gesture from slot 6                        C's switch/start requested
[59.835] Reentry window open slot=5                            L5  B's stop bound
[59.846] Reentry released slot=4 generation=40
[59.847] Reentry released slot=5 generation=0
[62.118] Recording started @ tick 2304                         L6  C's capture started
```

and the driver reported `successor_stop_denied`, `successor_stop_bound` and `successor_started` all confirmed,
with the note injected and the dump for slot 6 at `length=1536`.

**But this run's log contains none of the deferred `#CAP` DIAG rows**: zero `DIAG,reentry_window_open`, zero
`DIAG,canonical_release`, and no `DIAG,reentry,deny` for L2. The oracle therefore returns
**`INSUFFICIENT_EVIDENCE`**, which is the honest verdict: the chain structurally occurred, but the
identity-grade records that would name the denied slot/interval and the retiring owner are missing.

**Three checks now prefer the timely human row and say when they used it** -- L1 (the window, whose device
timestamp is parsed from the logger's `[58.476]` prefix), L4 (the release, including the owner identity parsed
from `generation=`), and L2. At the time of that writing L2 stayed **insufficient** because the human denial row
(`Overdub stop refused: <reason>`) carried **no slot and no interval** -- only the `#CAP` row did. **That is no
longer true**: the denial now names both on the human row (see § "The evidence edges are Tier-A now"). L2 still
declines to *pass* on the human row alone, for a reason that survives the fix: L5 orders the bind against
`denial.us` in **device micros**, while the human row's timestamp is **host milliseconds**, so promoting it to
the pass would put two time bases into that comparison. It now distinguishes a denial naming another slot
(fail) from a matching denial whose device-timestamped row is absent (insufficient). The checks never
substitute the human row silently; the detail names the fallback.

**Finding to carry forward: the `#CAP` DIAG tier is not reliably present.** *(RESOLVED 2026-09-26 — it was
not a deferral or flush problem at all: these tags were in the timing-critical **shed** class, which drops
non-Tier-A diagnostics silently and reports no loss. They are Tier-A now. See § "The evidence edges are Tier-A
now"; the investigation this paragraph asks for is answered there.)* The firmware documents that these
rows "queue behind the deferred stop's flood and arrive seconds late", but measured here they were **absent
from a completed run's log entirely**, while `capture_integrity` reported no ring eviction and the tail was
complete. Whether the tier is dropped, or flushed after the session closes, needs its own investigation -- and
it matters because the *identity-grade* half of this acceptance (L2's denied slot/interval, L4's retiring owner,
L1's loop length) lives only there. Until that is understood, an acceptance run may legitimately end at
`INSUFFICIENT_EVIDENCE` even when the behaviour is correct, and that is what happened here. *(No longer
expected: with the tags promoted to Tier-A, this specific `INSUFFICIENT_EVIDENCE` cause is removed.)*

**Status.** This rerun: `INSUFFICIENT_EVIDENCE` -- chain evidenced, identity-grade records absent. It closes
nothing and refutes nothing. The earlier run (17:29), whose log does carry the full `#CAP` tier, remains the
one that reaches `RETENTION_PASS` under the strengthened checks. A/B preservation, the raw-only companion
fallback and playback-output parity remain open.

---

## The evidence edges are Tier-A now, and the clear path names its target (2026-09-26)

**This closes the cause of the `INSUFFICIENT_EVIDENCE` rerun at 17:56.** That run behaved correctly and was
refused by the oracle because **zero** deferred `#CAP` rows existed -- 0 window rows, 0 release rows, no
`reentry,deny` -- while `capture_integrity` reported no eviction and the tail was complete. It was recorded as
an unexplained gap ("the `#CAP` DIAG tier is not reliably present", "needs its own investigation"). It is
explained: **it was never a deferral or a flush problem. The lines were in the deliberately-shed class.**

`CaptureLineTier::isTierALine` decides what the timing-critical shed may drop, and it drops non-Tier-A
diagnostics **silently, by design, without reporting a loss**. None of the three identity-grade tags were in
its allowlist -- `grep` count was 0 for `DIAG,reentry,`, `DIAG,reentry_window_open,` and
`DIAG,canonical_release,`. So the shed removed exactly the evidence the acceptance is computed from, and the
integrity report was right that nothing was *evicted*: shedding is not eviction, which is why the signature was
"absent, with no loss reported".

**Fix.** The three tags are now Tier-A. Tier-A may only be displaced by Tier-A, and these are a handful of
one-shot edge rows per run, so they cannot monopolise the ring. The predicate lives in `isTierALine`, called by
a `FLASHMEM` (`SC_MEM_ATTR`) function, so this costs **no RAM1**: `code 392,376 / padding 840` before and after,
at the approved floor.

**A latent bug in that fix, caught before it shipped.** The comparison was first written as
`strncmp(tag, "DIAG,canonical_release,", 24)`. `strlen` of that literal is **23**; passing the length *including*
the NUL makes `strncmp` stop at the NUL and match only a tag that ends exactly there, so the real row would
**never** have matched and the evidence would have been shed again -- silently, with the same signature this
section exists to remove. The length is now `strlen(...)` and
`test_deferred_stop_identity_match_has_no_length_boundary_gap` pins the boundary. Red-proved: restoring the
length to 24 fails that test (`Expected TRUE Was FALSE`) and the promotion test with it.

**The timely edges now carry identity too**, so attribution does not depend on the ring at all:

  - `Overdub stop refused: <reason> slot=<n> interval=<n>` -- the denial, on the same admission edge, in the
    human log. Previously it named neither, so a reader could see that a denial happened but not for which slot.
  - `Track cleared @ tick <n> (active_slot=<n>)` -- appended via `logTrackEvent`'s existing format parameter, so
    the established `Track cleared @ tick N` prefix survives every existing substring matcher
    (`legacy_record_baseline`, `hitl_stop_timeline_probe`, `test_clear_before_record_precondition`).
  - `Clear ignored — ... (track <t> slot <n>)` -- both forms, which is what made the failed preparation
    unauditable: the run logged two `Track cleared` and one `Clear ignored — selected slot is empty` and the
    slot still held content.

**Consequence: preparation is now auditable.** `_clear_slot` in the retention handoff previously accepted *any*
`Track cleared`/`Clear ignored` marker, which is precisely how a preparation reported success on a slot it never
cleared. It now parses the identity and returns pass / fail / unknown: the intended slot cleared, a clear naming
a **different** slot (retry, not success), or no identity line at all -- recorded as `identity_grade: false`
rather than silently counted as proof. Base convention is explicit because mixing it is the trap: `active_slot=`
and the `Clear ignored` `slot` field are 0-based, matching `#CAP` identity and the census; the older `MIDI: Clear
selected slot N` line is 1-based and is deliberately **not** used for identity. Red-proved: removing the slot
comparison fails `test_a_clear_line_naming_another_slot_is_a_failure`.

**Also fixed under this change:** the oracle's own wire-form docstring documented `DIAG,canonical_release` with
three fields where the emitter and the regex both use four (`slot,generation,loopLen,chunks`); and a **stale
host test** (`test_active_after_stop_blocks_clear_without_retry`) that supplied two sequencer probes for a retry
path that makes three, so it errored on `StopIteration` regardless of this change (confirmed pre-existing by
stashing). It is replaced by two cases that assert the current, stricter behaviour: still-active after the retry
blocks the clear (two presses), and the retry recovers a transport the first press accidentally started.

**Verification.** `pio run -e teensy41-capture-serial` SUCCESS, `padding 840` (floor), all four new log strings
present in the fresh ELF. `pio test -e native` **2,457 succeeded / 1 skipped** (up 2, the new tier tests).
Oracle `test_retention_probe.py` 39 green (up 3), gesture plan 22 green (up 7), clear precondition 28 green,
capture transitions 48 green. Both new guards red-proved by mutation.

**Scope.** This is a logging/telemetry fix plus its guards. It does **not** close the device gate: retention
plus successor capture still needs a rerun in which `RETENTION_PASS` is reached with zero concessions. What it
does change is that the previously-unexplained `INSUFFICIENT_EVIDENCE` should no longer be reachable for this
reason. A/B preservation, the raw-only companion fallback and playback-output parity remain open.

---

## The rerun after the Tier-A fix: RETENTION_PASS, zero concessions (2026-09-26)

Run `captures/s4f3-retention-20260926_181732`, identity
**`git_commit 5629e7e4f96d` / `firmware_hex_sha256 da59a7eeb3e0`** -- a firmware hash distinct from every
earlier run, so this is the promoted image and not a stale one.

**Verdict: `RETENTION_PASS`, exit 0, 20/20 checks `ok`, zero concessions.** Every link is identity-grade; no
link was accepted on a weaker basis, and `concessions` is empty.

**The point of the run: the rows that used to be missing are present.** This is the direct test of the Tier-A
promotion, and the same records whose *absence* forced `INSUFFICIENT_EVIDENCE` at 17:56 are now in a completed
run's log:

    DIAG,reentry_window_open,4,1121,6144        (17:56: zero rows)
    DIAG,canonical_release,4,40,6144,8          (17:56: zero rows)
    DIAG,reentry,deny,1311,2,5,1                (17:56: no denial row at all)
    DIAG,reentry,bound,1706,2,5,0

`capture_integrity` again reports **no ring eviction**, which is the signature that distinguished this cause
from eviction: the evidence was being *shed*, not evicted, so nothing was ever reported as lost.

**The chain, as the oracle reads it.** `reentry_window_open` for slot 4 at 58,173,344 us -> `deny` for slot 5
on interval 2 at 58,670,400 us -> C's retained-request row for slot 6 **staged between the denial and the
release, with no start executed before the release** -> `canonical_release,4,40,6144,8` at 59,723,633 us ->
`bound` for slot 5 on **the denied interval 2** at 59,698,903 us -> exactly one `RECA,6` at 61,759,537 us,
**after** the release -> the complete dump for slot 6 -> a clean bounded follow-up. The capacity-denied window
measured **1550.3 ms**.

**L5's ordering is the one the owner corrected, and it holds here.** The bind at 59,698,903 us precedes the
release record at 59,723,633 us by **24.7 ms**, and the oracle now reads the bind as the availability edge and
the release as the first maintenance turn at which the resources were *observed* free -- informational with
respect to L5, exactly as the corrected reading requires. The earlier inversion (requiring the bind after the
release) is gone.

**L7 is loop-scoped, and the run shows why that scoping is necessary rather than convenient.** The verifier's
whole-playback review returns `FAIL` with `inventory_equal=False` on all 36 cycles, because track 8's slots 1-4
still hold committed content and their notes sound alongside the loop under test -- the playback carries those
as `EXTRA`. That is the pre-existing instrument limitation the owner named, not a defect in the successor: the
deciding evidence is the dump's own per-slot completeness (`declared counts {'effective#0': 2, 'record#0': 2}
match parsed`) with note 67 present in **both** the storage and effective rows and the two agreeing on the full
MIDI fields compared (tick, type, channel, note, velocity). `storage_matches_effective` is `True`. The oracle
records the review's result alongside rather than letting it decide, and states in the check detail that this is
**not** MIDI-output playback parity.

**Staging was verified before A's window opened, and the clear is now attributable.** The targets were cleared
as a **separate, deliberate** step (never silently, inside the run), then a read-only census re-verified
eligibility: the library's committed count moved **61 -> 58**, exactly the three cleared slots, and
`target_precondition` returned true. The run was gated on that census and passed `--no-clear`, so nothing
touched the verified state.

**A driver bug found by running this, and fixed (commit `5fc74bf9`).** The prepare step reported
`clear_5_wrong_slot` for all three targets while the device had cleared exactly the right slots: target 5 ->
`active_slot=4`, target 6 -> `active_slot=5`, target 7 -> `active_slot=6`. The firmware identity fields are
**0-based** (matching `#CAP` and the census) while the driver's `slot_number` is **1-based** (the CLI), and the
new check compared them raw. The presses were always correct; only the verification was. The conversion now
lives in one place and the regression test pins both directions; replayed against the real prepare log all
three targets verify true. Note the shape of this: the bug was *invisible* before the slot identity was added,
because any clear marker was accepted -- which is exactly how a preparation could report success on a slot it
never cleared. Adding the identity exposed it, and running exposed the identity's base.

**One driver observation, recorded rather than smoothed over:** the driver's live `transport_stopped`
confirmation returned `false`, yet the successor's take committed and L7 reads a complete dump. The oracle's
evidence is authoritative here; the live check is a host-side convenience that should not be treated as a gate.

**Scope closed.** *Retention plus successor capture*, at this precise scope: request retention, the
capacity-denied window, the identity-grade denial and bind, the exactly-once start after the release, and the
successor's own committed/effective content. The denied request was **staged** inside the window and C started
afterwards, which is the expected retention behaviour; the natural 1550.3 ms window sufficed, so the pause hook
remains unimplemented contingency.

**Still open, and not claimed by this pass:** A's and B's content preservation (the dump covers the active slot,
C, only), the `companion path stalled; raw pass only` fallback's content consequences, and **MIDI-output
playback parity**, which is a separate property from this loop-scoped parity. The whole-playback review remains
unable to decide it while other slots are enabled.

---

## S4.1 reconciliation — the A/B/C device run *is* the release acceptance (2026-09-26)

**The owner's correction, verified:** the accepted-switch retention run is not merely "related to" S4.1. It is the
device acceptance for the release path S4.1 defines, and the tasks.md entry still describing a live "rerun
blocker" is stale. The chain, read from the code rather than inferred:

- **S4.1 asks:** *release when the slot leaves the active set and the guard reports no live owner; release is
  total.*
- **"leaves the active set"** — A switches to B, so A's slot stops being active while its deferred finalization
  still holds the occupancy.
- **"the guard reports no live owner"** — the release marker (`TrackDeferredMaintenance.cpp`) fires only when
  `runtime->captureStopOwner == nullptr` **and** `canonicalCaptureOutputAdmitted()`. That first clause is the
  `ownsLiveFinalization` term of `Track::noLiveOwnerForSlot` (S3.2) — the same liveness guard S4.1 names.
- **identity-qualified** — L4 reported `canonical_release,4,40,6144,8`: the retiring owner's slot **and**
  generation, which is S4.1/S4.2's identity requirement on device rather than only natively.
- **the working-set release: NOT evidenced by this run — corrected below.** `TrackManager::startRecordingTrack`
  -> `releaseBackgroundPlaybackMergedMidiEventsMemory(trackIndex)` sweeps `if (i != captureTrackIndex)`, i.e.
  every track **except the one capturing**. The whole A/B/C sequence runs on **track 8**, so C's start sweeps
  tracks 0-7 and **excludes track 8** — A's own working set is not released by it. An earlier draft of this
  section claimed C's start performs A's working-set release; that is **wrong for a same-track run** and is
  withdrawn here. The paths that can release a same-track slot are the memory-pressure release
  (`TrackManagerMemoryPressure` -> `tryReleasePlaybackMergedMidiEventsMemory`) and the playback-reset/undo path
  (`resetPlaybackState`/`resetPlaybackStateForSlot`/`TrackUndo` -> `invalidatePlaybackMergedMidiEvents`, which
  early-returns while a deferred stop is pending), plus the cross-track take-start sweep when the take starts on
  a **different** track.
- **reuse** — C starting after A releases **is** S4.3 verbatim ("activating C after A releases uses A's
  capacity"); S4.3 is checked with its *device* leg recorded open, and this run supplies that leg.

**Honest boundary, restated after the correction above.** What this run evidences is the
**finalization-capacity/occupancy** release, by name and identity-qualified (L4), and C starting after it. What it
does **not** evidence is the **working-set lease** release for the retired slot: that path is not on the
same-track take-start sweep, and it is not asserted by name anywhere. So S4.1's device acceptance is
**partial**: its occupancy release is demonstrated on device, its working-set lease release is demonstrated only
natively (S4.3's reuse fixture) and is *inferred* on device. Closing that gap needs two things, and neither is a
new stage: an emission that names the lease release, and a run in which that path actually fires for the slot
under test (a cross-track take, or memory pressure, rather than a same-track sequence).

### The "rerun blocker" text was stale — verified against the tree

S4.1's tasks.md entry still demands that the predicate-free release APIs "must be converted, not supplemented".
They have been. Verified 2026-09-26:

| Named site | State in the tree |
|---|---|
| successor stop reaching `clearAllLedgers()` | **Converted.** `Track::clearStoppingIntervalLedger()` clears **its own slot** via `LoopPlaybackRuntime::clearLedger()`, called from `handleNoteEditFold` and `stopOverdubbingToStopped`; the comment names the old behaviour as the S4.1 rerun blocker. |
| `TrackPlaybackRuntime::resetAll` | **Deleted.** Replaced by `releaseEligiblePlaybackRuntimeSlots()`, which applies `releaseSlot(slot, generation)` per slot behind `noLiveOwnerForSlot`. The remaining `clearAllLedgers()` caller is the deliberate whole-track `sendAllNotesOff` sweep, which is correct. |
| slot-threaded persistence reads (`TrackCaptureStopCommit`, `TrackStopTelemetryColdHelpers`) | **Converted.** `TrackStopTelemetryColdHelpers` documents "the caller names the interval's slot; resolving `getActiveLoopIndex()` here would key the persistence of a captured interval to whichever slot happens to be active at commit time", and `TrackCaptureStopCommit` uses `persistSlotIndex = slot`. |

### What was genuinely still wrong: the mid-pass journal resolver

`resolveCaptureTrackSlotForMidPass` (the mid-pass chunk journal's identity) still resolved the slot as
**`getActiveLoopIndex()`** for a **stop in flight**. That is the same defect class the table above records as
fixed elsewhere: a stop in flight is pinned to *its* interval's slot across an active-slot change (S4f.3), so if
the interval is on slot 5 and the user has moved the active slot to 7, the interval's mid-pass chunks were
journalled under **7**. The selected-track/active-index fallback in the same function keyed a queued chunk to
whatever slot happened to be selected when the drain ran.

**Fix, placed on the owner.** The rule moved to `Track::captureWorkSlot(uint8_t&)` — the slot a capture work
item must be keyed to — so it has one definition and one fixture instead of being re-derived per consumer
(matching S3.1's own principle that the slot's owner holds the rule). The live capture's slot *is* the active
slot, because only the active slot may RECORD/OVERDUB; a stop in flight resolves through the new
`Track::pinnedStopSlot()` (the deferred job's `job.slot`, else the latch's `slot`); and with nothing in flight
it returns **false**, so the resolver requeues instead of guessing. The resolver is now a thin loop over the
owner's rule, and the selected-track fallback is gone.

**Oracle and red proof.** `test_s4_1_capture_work_slot_uses_the_pinned_interval_slot` in
`test_runtime_working_set_ownership`: a stop latched for slot 1 with the active slot moved to 3 must resolve to
**1**, with the active index asserted to be **3** so a rule that read it cannot pass; a control consumes the
obligation and requires a refusal ("must refuse rather than guess"); and a second control puts the track in
RECORDING to show a live capture resolves to the active slot, so the refusal is not a rule that never resolves.
**Red-proved:** substituting `getActiveLoopIndex()` for the pinned slot fails it with *"Expected 1 Was 3 — the
pinned interval's slot must key the work item"*. Full native **2,459 passed / 1 skipped**; capture-serial
SUCCESS with `code 392,376 / padding 840` (at the approved floor — the rule is `TRACK_COLD_MEM`, FLASH-resident,
and the resolver is `STORAGE_PERSIST_MEM`).

**Recorded so it is not rediscovered:** the resolver **cannot** be exercised from
`test_storage_drain_restore`, which includes the TU. It linked only because the resolver had no caller and the
linker discarded it; the first caller pulls in a closure (cold overdub-stop logging, `EditManager`,
`DisplayManager`, `TrackTransportControl`) that this suite deliberately omits. That is why the rule and its
fixture live on `Track` in the ownership suite rather than being stubbed into the storage suite — the seam chose
the design, and the design is better for it.

### The `DeferredSaveJobStages` footer read is a design question, not a local defect

Investigated and **not** changed. The footer writes each track's `activeLoopIndex`/`selectedSlotIndex` from live
`trackManager` reads, and S4.1's entry lists it as a remaining site. But the deferred save is **not** an
admitted-image snapshot: the loop-slot stage reads the **live** `track.getLoop(poolCursor)` as it walks
(`DeferredSaveJobStages.cpp`), so the footer reading live state is *consistent* with the rest of the job.
Capturing only the footer at admission would make the persisted image internally **inconsistent** — a footer
describing a moment the loop bodies were not written at — which is worse than the status quo. Making the whole
save snapshot-at-admission is a persistence-semantics change beyond S4.1 and would owe its own `DEC`. Recorded
rather than implemented, per the entry's own note that it "needs the persistence fixture".

---

## Workspace-load failure on the device, and the host shutdown defect behind it (2026-09-26)

**The device's current workspace no longer loads and the runtime comes up empty.** Post-loss boot:

    [StorageManager] ERROR: CurrentSet meta missing /MidiLooper/current/temp/runtime.bundle.bin
    [StorageManager] Current workspace load failed.
    [StorageManager] Boot recovery chain exhausted; starting empty.
    BOOT,load,fail

with `post_restore_census ... free_chunks=480` (of 512). The read-only library observation agrees: all 64
slots empty, `committed_at_checkpoint=0`, `payload_inventory=0`, and the observation is **complete**
(`incomplete_window: false`, 64/64 slots observed). Minutes earlier the same library read **58 committed /
~386-403 free**. This is not an observation artifact -- the chunk pool corroborates it independently.

**Cause: the host closed the port while a workspace save was writing.** The deliberate clear of the
authorized target (track 8 slot 4, `identity_grade: True`) marked the slot dirty and admitted a deferred
workspace save; `--prepare-only` in `hitl_retention_handoff.py` then **returned immediately**, closing the
capture session and the serial port mid-write. That session's last line is
`PERS,work,...,TrackMeta,track:7,start,ok` with no completion. The next run's `--upload` reset the board on
top of it; boot hygiene discarded the incomplete temp; the manifest the loader needs was then absent. The HITL
skill documents this exact failure mode and prescribes stop -> drain -> stop -> drain with the port held open.
The driver implemented none of it -- **a defect in this session's own tooling**, not a firmware fault.

**What is established vs not.** Established: the load failure and the empty runtime. **Not** established:
that the slot payloads are erased (a *load* failure is not an erasure -- the card may hold every payload and
every saved set), or the exact failure chain (the interrupted save is the leading suspect, not a proven
cause). The SD card has not been touched and **no further boot was induced**, because boot hygiene is itself
evidence-modifying -- it has already discarded an incomplete runtime bundle temp.

**Fix (host-only, committed).** `scripts/hitl/device_shutdown.py`:

- `shutdown_device` performs the canonical two stop+drain cycles and delegates the idle decision to the
  existing owner, `deferred_save_idle_in_tail` -- completion is **verified, never slept**.
- The decision is taken from the log **tail**, not from "lines since I last looked". A new-lines-only window
  is empty at the instant it is opened, and the idle owner correctly calls an empty suffix idle, so anchoring
  at "now" reported success *before the save was admitted*. That vacuous pass was a real defect in the first
  cut of this module, caught by `test_the_incident_log_is_never_reported_complete`.
- A raising press or a reported disconnection is `incomplete_disconnected`; a drain that never settles is
  `incomplete_timeout`. An incomplete shutdown is **latched**, and `refuse_reset_if_latched` blocks the
  follow-up upload/reset in both the driver and `hitl_boot_census`.
- `DeviceSession` is a context manager, so the drain runs on **every** exit -- normal return, early return,
  and exception -- instead of at each `return`, which is how `--prepare-only` was missed. A structural test
  asserts every `return` inside the capture session sits inside a `DeviceSession`.

**Device validation of this fix remains PENDING.** It is host-tested only: the drain has never run against
the board, and it cannot be device-validated while the incident hold is latched. Its tests support the
correction; they do not demonstrate it on hardware. Recording that plainly so a later reader does not read
"fixed" as "proven on the device".

**A false green in that structural test, found by red-proving it.** An earlier visitor returned early from
`visit_FunctionDef`, so `main`'s body was never traversed and the assertion passed even against a planted
`return` that bypassed the drain. Fixed, and re-red-proved (the planted return is now flagged at its line).

**Oracle: 15 host tests, using the incident's own preserved bytes as the decisive fixture.** The clear-run log
reads active/not-idle while the clean retention-run log reads idle, so the check discriminates rather than
self-confirms -- and a fixed sleep provably could not tell those two logs apart. The reset-gate test proves
`hitl_boot_census.main` returns 4 **without** invoking the upload when latched.

**The hold is enforced, not merely stated:** the latch is set with the incident context, so any upload/reset
now refuses until it is deliberately cleared once the device is known good.

**Recovery path (a), preservation first -- awaiting authorization.** The decisive artifacts are preserved
under [`evidence/2026-09-26-workspace-loss/`](evidence/2026-09-26-workspace-loss/MANIFEST.md) (copied out of
gitignored `captures/`, with per-file sha256, the exact commands, build identities, and the driver verbatim at
the faulty revision; `.gitattributes` keeps them byte-exact so the hashes stay valid). The read-only SD
imaging and inspection procedure -- full image from the raw device, verify, then inspect **the copy** for the
current-workspace payloads, manifests, saved sets and revisions -- is written up and **not executed**:
[`SD-IMAGING-PROCEDURE.md`](evidence/2026-09-26-workspace-loss/SD-IMAGING-PROCEDURE.md). No quarantine, no
saved-set load, no rebuild.

**S1.6c.2 -- capacity revalidation is BLOCKED.** It requires the loaded N=56 library, and an empty-library
run cannot replace that experiment. This is a hard block, not a delay.

## USB folder reader — local incident-support preparation (2026-09-26)

**2026-09-26 — USB folder reader prepared locally; no device access.** User requests only
`/MidiLooper` over USB, leaving the card in place and unrelated data excluded. Added
`tools/midilooper-reader/`: separate read-only utility firmware (no looper boot recovery/save
code) plus a streaming Mac downloader with file CRC32, SHA-256 manifest, path checks and
incomplete-transfer handling. Seven host tests and the standalone Teensy build pass.
NOT uploaded or device-verified; incident hold remains set. Installing the reader replaces
and resets the running firmware and needs explicit approval. Folder-only copying supersedes
full-card imaging as the requested approach, without authorizing upload, repair or device access.
Contract and commands: `tools/midilooper-reader/README.md`.

Execution packet and decision review are in `tools/midilooper-reader/README.md`. No production
firmware stage or device recovery gate is closed by this tool. No upload/reset command was issued.

## USB folder reader — authorized hardware copy completed (2026-09-26)

**2026-09-26 — USB folder copy COMPLETE, authorized reader installed.**
Copied only `/MidiLooper` through the standalone read-only reader with the card left in place:
**1,007 file entries / 6,837,184 bytes**, transfer CRC32 and independent local SHA-256/length/CRC
verification all pass. Output: `captures/midilooper-folder-copy-20260926-195228/download-05/`;
report: `captures/midilooper-folder-copy-20260926-195228/verification.json`.
Contains 65 current files (64 slots plus workspace metadata), 73 saved-set files, and historical
`current.bad.*` trees. This proves surviving files, not their semantic validity or recoverability.
One duplicate path, `current.bad.211210/workspace.bin`, has distinct 154-byte and 77-byte entries;
both are preserved (second under `duplicate_entries/000854.bin`), not overwritten. Initial transfers
are retained as incomplete evidence; USB per-line acknowledgements fixed unreliable bulk sending.
Reader firmware SHA256 `a915b18c5a12175ef7c25dddcc8932840418fd61018ca4201ade467e78cacce2`;
9 host tests pass. No SD writes, normal-looper boot, recovery or restore occurred. Reader remains
installed; normal-looper upload/recovery hold stays latched. User's explicit approval covered this
reader installation and folder copy only. Recovery analysis can now use the host copy.

## Offline recovery audit — 2026-09-26



- Current workspace has 64 slot files and `workspace.bin`, but no runtime bundle.
- All 64 current slot files pass the on-device 256-byte incremental body checksum
  and SAVE footer checks. Metadata identifies 60 nonzero loop lengths and four zero lengths.
  These are **not** decoded-note or successful-load counts.
- Current workspace metadata passes its checksum, epoch 930.
- 60/64 slot loop lengths agree with the preserved pre-loss census. Differences
  (human one-based track/slot): 8/4: 19200→0; 8/5: 0→6144;
  8/6: 0→6144; 8/7: 0→1536. This comparison does not prove note preservation
  or attribute any change to a specific command.
- 69 revision containers pass their header and firmware payload checksums.
  54 also pass this audit's current-format chunk walk; 15 older S0001 revisions
  fail that walk. Treat those as unsupported/rejected by this audit, not proof
  of corruption: historical layout compatibility has not been investigated.
- The largest revision candidates have only 22 nonzero-length slot chunks
  (S0003/v0017); latest S0003/v0018 has 21. Saved sets are not a replacement
  for the current large library.
- Seven historical runtime-bundle files pass the chosen epoch/footer checksum
  checks, including two `.bad.*` files. This does not make any of them compatible
  with the current slots. No bundle was selected for transplantation.

## Checksum compatibility finding

`PersistenceSchema::crc32Continue` complements its input before a helper that
also complements its input; it is not standard incremental CRC32. The audit
reproduces current device algorithms: epoch finalizer 256-byte chunks seeded via
Continue(0); revision file reader 512-byte chunks with first chunk via crc32.
A direct compile of the production PersistenceSchema.cpp confirms
Continue(0, "123456789") = d202d277. Standard CRC rejection is therefore not
sufficient to label these files corrupt. No firmware checksum behavior changed.

## Execution packet / limits

Oracle: manifest mismatch aborts audit; bad footer/checksum, truncated section or
chunk, duplicate slot chunk and count mismatch fail the relevant structural check.
No source files are rewritten. Seven focused audit tests plus nine reader tests
pass (16 total). Report is reproducible with:

`python3 tools/midilooper-reader/audit_backup.py captures/midilooper-folder-copy-20260926-195228/download-05`

Report: `offline-audit.json`, SHA256 `3031219a40c92e0800a4491f3d5b28197a08b9651d47fcea6d6fd694d4ae6536`.
The report explicitly excludes full note/pass decoding, revision directory/body
agreement, runtime-bundle semantic compatibility and restore readiness.

## Next bounded slice

Use the production slot decoder against the 64 current host files, validate notes,
passes and effective roots, and establish exactly which runtime metadata a recovered
workspace needs. Prepare and validate a candidate **on the host only** before
requesting any recovery write. Do not combine a historical bundle with current
slots merely because both have valid checksums. Normal upload/recovery hold remains.
Capacity acceptance stays blocked until a recovered library is loaded and measured.

## Offline production decoder — 2026-09-26


All 64 copied current slot bodies decode; 60 nonempty and four empty.
Capture passes contain 7,840 raw MIDI events. 57 slots contain effective snapshots,
with 2,766 distinct-per-slot snapshot note identities. Three nonempty slots use
raw pass format instead (human identities 6/3, 8/1, 8/3); absence of a serialized
snapshot in that format is not evidence of content loss. No SysEx content passed
through the audit: it explicitly rejects SysEx in passes or snapshots.
These counts do not prove original-note preservation or audible playback parity.

## Host ABI trap, resolved in isolated tooling

Initial direct-native decoder: four empty bodies accepted, 60 nonempty rejected.
Initializing the event pool did not change that. Production StorageLoopIo serializes
`sizeof(MidiEvent)` bytes, and its union contains a pointer. Mac compile measured
size=32/data offset=16; an ARM Cortex-M7 compile of the same header confirmed
size=20/data offset=12 with static assertions. This explains why host round-trip
fixtures can pass while device files fail on a native build.

`prepare_native_decode_headers.py` copies headers to a new temporary directory,
replaces only the SysEx pointer representation with an opaque 32-bit token, and
copies StorageLoopIoBodyFormatV1.cpp changing its pointer-to-integer check to read
that token. Its parser algorithm is otherwise unchanged. Layout assertions pin
20/12. SysEx bytes cannot be dereferenced and SysEx content is rejected by the
harness. This is an explicit ABI adapter, not an unmodified native binary.
Production `src/` and `include/` are unchanged.

## Execution packet and verification

Owner/path: host reader → production raw/LSV1 decoding → scoped candidate destruction.
No runtime publication, device session, SD access or firmware build/upload.
Input: verified download-05; wrapper verifies every manifest hash before parsing.
Every raw parse must consume the whole body. LSV1 uses its production parser,
validates stored object/membership data and constructs the snapshot. Per-slot
results plus native log are preserved beside this report. Decoder objects are
released between slots: this is not an all-library memory/capacity measurement.

Oracle: known one-note record round-trip yields two events and length 1536;
truncated raw/LSV1 headers are refused; all 64 device identities must be reported
exactly once, with matching 20/12 ABI, a successful native exit and all decoded.
Native test has an explicit skip if real files or the target-layout adapter are
absent; the wrapper will not mistake such a skip for successful decoding.
Final repeatable wrapper run: three tests passed, zero skips in selected suite.
Ordinary native control run: two tests passed and copied-files test skipped.
16 existing Python audit/reader tests remain passing.

Command:
`python3 tools/midilooper-reader/decode_backup.py captures/midilooper-folder-copy-20260926-195228/download-05 <new-output-directory>`

Evidence: `production-decode.json` includes source SHA256s and manifest identity;
`production-decode-native.log` contains the 64 results and controls.

## Missing runtime metadata (verified from CurrentSetBootLoad.cpp)

`readCurrentSetFilePreamble` requires the container/anchor header, tempo, transport
state, master length, and track count. `readCurrentSetTrackSlotMetadata` reads each
track state/mute and every slot enabled/mute/loop-id. `readCurrentSetFileEpilogue`
requires selected track, active and selected slots, undo stacks and completion
markers (plus optional metadata trailer). Those settings are not recoverable merely
by decoding the current slot files; the current runtime bundle is absent.

Next: prepare a host-only reconstruction candidate and test it through the real
bundle/load path. Keep every slot byte-for-byte; make replacement transport,
selection/mute/enable and undo choices explicit. Do not transplant a historical
bundle's undo references without validating compatibility. No candidate was built
or approved in this slice; no restore write is authorized. S1.6c.2 — capacity
revalidation remains blocked until a loaded library can be measured.

## Host reconstruction accepted — 2026-09-26


`captures/midilooper-folder-copy-20260926-195228/reconstructed-03/candidate/MidiLooper/current/`

All 65 original current-workspace files (64 slots and workspace.bin) are byte-identical
to verified download-05: 442,803 bytes. Only runtime.bundle.bin was added: 628 bytes.
Total candidate: 443,431 bytes (about 433 KiB). Epoch 930 matches current workspace metadata.
Bundle SHA256: `6302d6e7428b87fb566a98ccba9e06f27c9c4e0bd5c1a7c7fd87e3f5a0acbdaf`.
A byte-exact copy is `candidate-runtime.bundle.bin` beside this report; originals
and per-file SHA256s are listed in `reconstruction.json`.

Explicit replacement settings, not recovered originals:
- Transport idle/stopped, tempo 120 BPM, master length 0 (derive during use).
- First nonzero-length slot on each track selected and enabled; other slots disabled.
- Tracks and slots unmuted; selected track 1.
- Fresh global undo stack, default anchor fields/timestamps. Original global undo
  history and performance mix/selection are not recovered. Serialized pass/edit
  history in each untouched slot remains present; runtime may rebuild local history.

## Production-path test

`RecoveryRuntimeBundle.h` writes through existing CurrentSetStorage,
CurrentWorkspaceStorage and RuntimeBundleFooter serializers. It owns no filename,
scan, deletion, rename or installation step. Caller supplies an isolated File.
`test_offline_workspace_recovery` reuses the real persistence/load harness with
an in-memory SD boundary. A temporary header wrapper exposes only the private
production directory loader; no production header was edited. The previously
validated Teensy ABI adapter is used (20-byte MIDI event, opaque SysEx token).

Checks completed:
- Missing bundle refused; truncated bundle refused.
- Invalid selected-slot defaults refused before writing, and closed output refused.
- Reconstructed bundle accepted by `loadCurrentSetFromDirectory`.
- Loaded tempo/transport, enabled/muted flags and active slot indices match defaults.
- Production background-admission and restore jobs drain in 2,863 simulated turns,
  with a 100,000-turn bound. This is not a device performance measurement.
- All 64 restored lengths match. All **7,840 capture-pass MIDI event records** match
  source-decoded event bytes, with matching record/overdub pass identities/counts.
- All 57 serialized snapshot note inventories match source counts (2,766 note identities).
- All original in-memory SD files remain identical after restore.
- Output epoch checksum and SAVE footer pass; host copy rehashed after testing.

The first harness cut stopped after one admission slice; the second drove admission
but left nativeMillis frozen, so the production 2-second holdoff never expired.
Both were corrected in the test, not bypassed in production. No target timing claim.

Final repeatable wrapper run: source decode **3/3**, reconstruction **2/2**.
Ordinary-build affected-suite regression: **12 passed / 2 explicit skips** (real-file
recovery fixtures require isolated ABI configuration), including the existing drain
suite. Python audit/reader tests: **16 passed**. No full native run, firmware build,
device run, power-loss test or original-vs-current audible parity claim.
Evidence: reconstruction-native.log, reconstruction-regression.log, reconstruction.json
(source hashes), and the candidate bundle. Production src/ and include/ unchanged.

Run:
`python3 tools/midilooper-reader/reconstruct_backup.py <verified-download-directory> <new-output-directory>`

## C++ recovery on the Teensy: feasible, not shipped

The reusable writer is C++ with existing File/serialization dependencies and no Mac
API. Its host acceptance supports reusing this work on Teensy, but does not establish
firmware fit, device timing, SD durability or safe automatic recovery.

A device implementation belongs to StorageManager, as a metadata-salvage fallback
when intact current slot files survive but the runtime bundle is missing/unreadable.
It must validate the slot generation/format/content first, state the replacement
settings, write a separate candidate, close/drain/reopen/validate it, then install
with a crash-safe commit retaining recoverable previous state. Interrupted candidate
writes must never replace valid workspace data. Power interruption at each commit
boundary, repeated boot, malformed slots, no surviving content, firmware RAM1 floor,
and bounded stopped-only work need device acceptance. Do not modify the deployed
checksum algorithm incidentally: historical files use its existing continuation semantics.

This procedure does not recreate erased or corrupt notes. Do not infer that every
`.bad.*` bundle is usable, or transplant its undo references into current slots.
Normal boot cleanup removes named `.tmp` files, not the completed runtime bundle;
the present test exercises directory load and deferred restore, not the entire
RTC/index-reconcile/hardware boot chain.

Recovery installation and normal firmware upload remain unperformed and outside
this host-only authorization. The hold stays latched. S1.6c.2 — capacity revalidation
remains blocked until the library is loaded and measured on hardware.

## 2026-09-26 — host undo reconstruction acceptance

**2026-09-26 — undo reconstructed and host-tested; reload blocks installation.**
Existing C++ content-history owner derives 122 actions (60 record, 11 overdub,
51 boundary changes) across all 60 nonempty loops. Full Undo → Redo preserves
materialized MIDI bytes, loop lengths and published note identities/content on all
60; four empty slots have no actions. Diagnostic bundle: 8,846 bytes. Production
load/hydration fixture retains only 9 actions after reload, so the wrapper exits 2
and the candidate is not safe to install. Original files byte-identical; firmware
unchanged; no device access; hold remains. Original cross-slot chronology, clear
snapshots and old redo branch remain unrecovered. Evidence: active-change
`evidence/2026-09-26-workspace-loss/UNDO-RECONSTRUCTION.md`.

**2026-09-26 — correction: reconstructed undo after reload needs a policy change.**
The prior “next fix” diagnosis missed DEC-057 — snapshot body format and the active
working-set spec: LSV1 load deliberately starts with undo depth zero. The older
content-history reconstruction spec has a newer snapshot delta that changes it.
The 122-action host recovery remains valid; it is not authorization to reverse the
load contract. Proposed change: derive per-slot undo from sufficient saved passes,
redo empty, saved snapshot authoritative, no persisted undo stack. Approval is
required before that normal-load behavior changes. No firmware/device action.
See active-change `evidence/2026-09-26-workspace-loss/UNDO-LOAD-POLICY-REVIEW.md`.

## Approved undo load fix — G0/G1/G2 execution packet (2026-09-26)

**Authority:** user approval; DEC-088 — load-time undo reconstruction. Prior
UNDO-LOAD-POLICY-REVIEW.md conflict resolved. Owners unchanged: deferred load commit
installs Loop content; TrackUndo derives runtime history. No device operations.

**G0 verified:** LoadLoopJob Commit is STOPPED-only, validates token/request identity
before applyLoadedLoopSlotBodyCommit. That function clears LSV1 undo but derives raw
undo. Deferred hydration reads legacy stacks then derives loaded track content;
public Undo completes hydration before mutation. Rebuild currently resets all-track
cursor; reposition also resets cursor and moves entries even for an absent slot.
These must not turn another slot's undone entries into applied entries.

**G1 contract:** common successful slot commit derives that slot's units after root
installation; no history on failed/missing-root commit. No new root reconstruction
on ordinary LSV1 load. Existing TrackUndo append inserts new loaded units before the
redo partition; focus reordering preserves applied/redo classification and identities.
No new allocations beyond metadata vectors used by existing cold rebuild/reorder;
no new runtime state, persistent format or full-library scan at slot commit.

**G2 red-team:** COVERED stale/token failure (existing load gate before commit);
COVERED saved-root authority (root unchanged); TIGHTENING background load after Undo
(requires dedicated red proof and cursor-preserving owner helpers); COVERED public
Undo-before-hydration (existing gate); COVERED redo invalidation on new musical commit
(existing dropRedoBranch unchanged). Timing/RAM1 require build and remain device-
unverified; no hardware claim. PASS with these test requirements incorporated.

**Oracle:** pin existing recovered action inventory independently; load a bundle
with empty undo headers, step production background restore/hydration before/during/
after, compare complete action identities, require redo empty, then Undo/Redo all
slots without calling rebuild in the test. Reload same empty-header bundle and
repeat. While focus slot has a runtime redo entry, commit another slot; focus,
content, entry identity and redo remain. Repeated same-content rebuild has no duplicate
units. Public load fault regressions remain in storage suites.

**Sequence:** first make current firmware fail the no-test-rebuild acceptance and
background-redo assertions; then implement common slot reconstruction and partition
preservation. Run isolated recovery runner, native affected suites, full native,
capture-serial build (RAM1 floor >=840). No upload. Completion recorded below.

**2026-09-26 — approved load-time undo implemented; host recovery passes.**
DEC-088 — load-time undo reconstruction supersedes the earlier zero-after-load
policy. All 122 actions derive from saved passes on load/reload with empty undo
headers; all 60 nonempty slots pass Undo/Redo, and background commits preserve
runtime Redo. Redo starts empty and is created by Undo. The isolated runner verifies
hydration before/during/after restore. Candidate reconstructed-undo-04 preserves all
65 original files plus a 628-byte bundle (443,431 bytes total). Capture-serial build
passes with RAM1 padding 840, unchanged. No upload/device/card action; standalone
reader and incident hold remain. Hardware recovery is still unverified. Evidence:
active-change `evidence/2026-09-26-workspace-loss/UNDO-LOAD-FIX.md`.

**Verification complete:** full native 2,461 passed / 5 skipped; focused native 68
passed; recovery load/reload 122 actions and all 60 nonempty slot roundtrips;
Python 16 passed; OpenSpec validation passes. Capture-serial firmware SUCCESS,
RAM1 padding 840. No device actions. See UNDO-LOAD-FIX.md and preserved logs.

## Automatic boot repair — execution packet (2026-09-26)

G0 verified: loadState currently cleans temp files before validation; directory load
can quarantine a malformed bundle, and revision/checkpoint fallbacks admit a save.
SD open modes do not truncate; atomicRenameTempFile deletes destination. None is
appropriate before preserving the corrupt workspace. Existing serializers and slot
parsers provide reusable format authority; runtime bundle host writer is validated.
G0 PASS. Read authority/storage guide; DEC-087 host defaults, DEC-088 reconstructed
undo, DEC-021 deferred restore. User approved automatic boot repair (DEC-089).

G1: existing StorageManager owns boot inspection and repair. No new Manager or format.
BootRecoveryJob holds a per-boot block; main and save owners consult it. Recovery writes
only unique metadata candidates/backups, preserves all source payloads, and requires
complete validated matrix to avoid silent partial restoration. Normal deferred loader
and undo owner publish content after metadata repair. No full-library resident graph.

G2 adversarial review: covered interrupted candidate write (original remains), crash
between renames (slots/workspace and backup remain), failed promotion (blocked, retry),
namespace collision (choose unused bounded names, never overwrite), save-on-empty
(fail-closed latch across main and save entry/worker), bad epoch/unsupported slot
(reject before first write). Cold boot timing unbounded by content size but outside
active service; no runtime scheduling extension. Physical SD/FAT corruption remains
hardware verification, not an asserted power-loss guarantee. PASS with fault tests.

Oracle: run production loadState against in-memory SD populated from preserved bytes;
assert no cleanup before refusal; candidate CRC/grammar validated before any final
replacement; normal deferred restore gives all source event bytes and undo depth.
Inject SD write/rename faults (physical I/O boundary only), snapshot card after each
mutating operation, reset runtime and replay each snapshot as a fresh boot. Blocked
save requests/drains/worker must leave file map unchanged. Healthy boot no repair
writes; no modification outside MidiLooper. Run native, full native, firmware build
without upload; preserve logs, source/artifact hashes and exact tests below.

### Boot repair verification and closeout

**2026-09-26 — automatic boot metadata repair implemented and host-verified.**
DEC-089 — boot metadata repair validates the complete workspace before generating
runtime metadata; preserves source files and corrupt originals; blocks saves after
refusal. Actual boot owner restores the copied 64-slot library and derives 122 undo
actions; healthy reboot retains them. Six isolated cases include 276 interruption
cuts. Full native 2,461 passed / 7 skipped; Python 16; capture-serial SUCCESS with
RAM1 padding 856 (floor 840). No upload/device action; hardware acceptance and the
incident hold remain open. Evidence: active-change
`evidence/2026-09-26-workspace-loss/BOOT-REPAIR.md`.


Session closeout: decision, authoritative storage behavior, OpenSpec delta/design/tasks,
runtime state/current scope and deliverables updated. No archive or device checkoff.

**2026-09-26 — recovery firmware uploaded with explicit user authorization.**
`teensy41-capture-serial` upload SUCCESS; SHA256
`4b13749ab9d97b0699fb02f813a3a9a26b7475a60391e19ffa784c21cec93255`
matches the host-verified image. Confirmed standalone read-only reader before upload;
archived and cleared the incident hold for this approved recovery installation.
Device reports runtime metadata repaired, `BOOT,load,ok`, and a settled checkpoint:
`admission_complete=1`, `interactive_ready=1`, `undo_hydrated=1`,
`transport_stopped=1`, `deferred_prewarm_idle=1`, `free_chunks=391`.
Serial held open through verified persistence idle, then closed. No content/clear
stimuli. This establishes successful recovery boot, not per-loop content parity,
hardware Undo/Redo roundtrips or second-boot acceptance; those remain open.
Evidence: active-change workspace-loss `authorized-recovery-upload/`.

### OLED frozen-first-frame investigation (2026-09-26)

User confirms the normal piano roll with the initial 20-bar track-1 content remains
frozen while MIDI responds. Live serial attached at 115200 without reboot (uptime
continues). Original `DISP` length 15360; later display records length 768 and
STOPPED → PLAYING → OVERDUBBING → STOPPED while DMA frames continue increasing,
with no reported timeout/rejection. These counters prove software progress, not
updated pixels on the panel. Root cause remains UNKNOWN. Existing snapshot
preconditions and recovery are not patched on this evidence.

Built the existing blocking-SPI comparator (`MIDILOOPER_DISPLAY_DMA=0`) using the
same capture-serial environment and unchanged source in an isolated build directory.
Build SUCCESS; not uploaded. Configuration, log and image hash:
`captures/display-blocking-diagnostic-20260926/`. Serial capture remains open in
`captures/recovery-display-hang-20260926/`; latest user-triggered save completed.
Hardware comparison requires explicit upload approval per repository rule; this is
a diagnostic variant, not a production scheduling fix or a closed display gate.

**2026-09-26 — blocking-SPI diagnostic uploaded with explicit user approval.**
Persistence idle verified before reset. Capture-serial upload SUCCESS using
`captures/display-blocking-diagnostic-20260926/platformio.ini`; actual device
`#DMA_XFER` shows init=0, dma_frames=0, increasing blocking_frames. Serial capture
remains open. Visual outcome awaiting user; no display fix or timing gate claimed.
Recovery source unchanged. Image/config identity is pinned in that capture directory.

**2026-09-26 — blocking-SPI visual comparison PASS (user observed).**
User confirms “Yes, the screen updates now.” Same recovery/renderer source, existing
`MIDILOOPER_DISPLAY_DMA=0` switch only. Device reached load=ok, restore admission
complete, undo hydrated and transport stopped. This isolates the frozen-screen
behavior to the DMA display route; it does not establish the precise DMA defect.
Blocking SPI remains installed as a diagnostic workaround; no real-time timing
acceptance or permanent DMA fix claimed. Serial capture remains open.
Evidence: `captures/display-blocking-diagnostic-20260926/metadata.json` and session log.

### User-directed deferral and continuation

DMA repair is parked until after this OpenSpec. No architecture/timing gate is waived.
Current device profile, evidence, resolved stale task clauses and source-backed next
S4.1 — persistence identity investigation are in [NEXT-STEPS.md](NEXT-STEPS.md).

---

## S4.1 — persistence identity during handoff (2026-09-26, G2 execution packet + record)

### Identity and scope

S4.1's remainder named one persistence site: `DeferredSaveJobStages`' footer stage writes each track's
`activeLoopIndex` / `selectedSlotIndex` from **live** `trackManager` reads at footer-write time, while the
loop bodies are written under the **interval's** slot identity. The question (raised in
[NEXT-STEPS.md](NEXT-STEPS.md), which also warns that the read is *not* proven wrong) is whether a capture
handoff during a save makes the persisted image internally inconsistent, or loses a focus change.

- Owner and production entry → deferred worker → commit point: `Track::requestLoopSlotPersistAndSaveState`
  (slot-keyed admission) → `StorageManager::processDeferredSaveState` slices → the runtime-bundle rename in
  `finalizeDeferredMetaTempFile()` and the epoch anchor in `writeWorkspaceMetaAfterDeferredSave()`.
- Status: **native verified; no production behavior changed.** The live read is the contract; the
  alternative (snapshot-at-admission, or the interval's slot substituted for the focus) is rejected with a
  fixture that red-proves it. Recorded as **DEC-090**.
- Files: `test/test_storage_drain_restore/test_storage_drain_restore.cpp` (fixture),
  `test/test_storage_drain_restore/drain_harness_stubs.h`,
  `test/test_storage_manager_load/load_harness_stubs.h` (harness seam). No `src/` change.
- Explicit non-goals: no SD format change, no new admission rule, no attempt to make the whole save a
  snapshot; the device legs of the S4.1 release acceptance are untouched by this slice.

### G0 trace — the five links, read from the code

1. **Stop A → slot-keyed persistence admission.** `Track::requestLoopSlotPersist` is
   `markLoopSlotMaterialDirty` + `admitLoopSlotPersist`; the latter admits **`SlotMeta(track, slot)` then
   `LoopPersist(track, slot)`** (FIFO, coalesced by `(type, key)`), and
   `requestLoopSlotPersistAndSaveState` adds `requestDeferredSaveState` (the owed full save).
2. **The workspace header/footer write is the bundle work item.** A `SlotMeta`/`TrackMeta`/`GlobalMeta`/
   `WorkspaceFooter` item is a *runtime-bundle* write: `beginDeferredRuntimeBundleWrite()` opens the meta
   temp, and the FSM walks `CurrentSetMeta` → `TrackHeaderAndSlots` → (loop bodies are **skipped** for bundle
   items) → `Footer`. The footer is emitted one field per slice: `SelectedTrack`, then one
   `activeLoopIndex` per track, the selected-slot extension token, one `selectedSlotIndex` per track, the
   GUS token.
3. **The loop body is the `LoopPersist` item**, keyed by `(track, slot)`, and it clears
   `currentSetLoopSlotDirty[track][slot]` **only** after a token-verified write + rename. Content identity
   and workspace focus therefore advance on separate work items; neither can silently stand in for the other.
4. **The commit point.** `finalizeDeferredMetaTempFile()` writes the SAVE token, stamps the epoch CRC into
   the bundle's own header and renames temp→final: that rename is when a footer becomes durable. The owed
   full save adds `FinalizeWorkspace`, which only *completes* the epoch (patch `lastActiveUnix`, CRC,
   `workspace.bin`) — it does not re-read or re-write the footer.
5. **Handoff → load.** `readCurrentSetFileEpilogue` reads the footer; `applyLoadedTransportFooter` applies
   it through `TrackManager::loadTransportSlotIndices` (while stopped, `active := selected`), and
   `isBootPlaybackSlot` = **union(file active, file selected)** decides which slots are prewarmed at boot,
   while `TrackUndo::rebuildTrackFromLoopContent(track, selectedSlotIndex[t])` keys undo reconstruction to
   the **selected** slot. So the footer is not only a display preference: it selects the restored working
   set and the undo-reconstruction subject.

### The consistency point, and whether later selection requeues

The deferred save is **not** an admitted-image snapshot: every stage reads live state in its own slice (loop
bodies from `track.getLoop(poolCursor)`, the footer from live indices). The footer is therefore always at
least as new as the bodies beside it, and capturing only the footer at admission would make the image
internally inconsistent — which is why the alternative was rejected rather than implemented.

A later focus change is **not** lost:

| Route | What it does |
|---|---|
| `TrackManager::setSelectedSlotIndex` (selected track) | `requestWorkspaceFooterPersistWhenSafe()` **and** `requestDeferredSaveState()` |
| `TrackManager::setSelectedTrack` (track change) | same pair |
| `maybeAdmitDeferredWorkspaceFooter()` | admits the deferred footer only when no capture and **no stop in flight**, so a footer cannot be written mid-handoff |

The queue is FIFO, so a requeued footer lands *after* an in-flight write. A bare `activeLoopIndex` move that
does not go through a selection change does not itself request persistence; that is consistent with the load
contract, which derives the playing slot from the **selection** while stopped (link 5), and the next save's
footer reads live state regardless. Recorded as an observation, not a defect.

### Oracle fixed before the fixture ran

With the worker paused with the footer **due and unwritten** (`stage == Footer`,
`footerWriteStage == SelectedTrack`, `footerTrackCursor == 0`), the A→B handoff performed through the
production manager route, then the worker resumed and the workspace reloaded from the card:

- **content identity** — A's body lands under A's own loop identity and note, B's under B's, and neither
  carries the other's content; A's dirty flag clears only through its own body write;
- **workspace focus** — the restored focus is **B**, the selection when the footer was written;
- **no-switch control** — the same sequence without the handoff restores **A** (so "B" cannot pass by
  construction);
- **failure** — a footer write refused by the card publishes nothing (the previously durable bundle stays
  byte-identical) and the retry lands the post-handoff focus.

### Controls and coverage

- Fixture: four cases in `test_storage_drain_restore`, driving the **real** deferred worker
  (`StorageManager::processDeferredSaveState`) and the **real** loader
  (`StorageManager::loadCurrentWorkspaceFromSd`), with two committed slots seeded through the production
  payload writer and the production restore service. The worker is paused by the *driver* simply not calling
  the next slice — no hook, gate or test-only owner was added to production code.
- The handoff uses the production gesture route `TrackManager::setSelectedSlotIndex(..., SyncPlayback::Yes)`,
  so the case exercises the selection route's own persist requests rather than restating them.
- Harness seams, recorded rather than silent: the suite now links `TrackStopTelemetryColdHelpers.cpp`
  (so the slot-keyed admission is the production composite, and `resolveTrackIndexForPersistence` is the
  real one — `load_harness_stubs.h`'s copy is behind `HARNESS_LINKS_REAL_STOP_TELEMETRY`); the nine
  capture-finalization neighbours that `setActiveLoopIndex` references but a **stopped-track** move never
  runs are fail-closed (`TEST_FAIL_MESSAGE`) in `drain_harness_stubs.h`; the slot-change note-edit session
  lifecycle is counted, matching the existing track-change pair.
- **Found while building the fixture (worth keeping):** a case must **drop** the persistence work queue
  (`PersistenceWorkQueue::resetForTests()`), not merely drain it. A leftover `FinalizeWorkspace` entry
  outlives the image it was admitted against and sits at the queue head failing forever — its first step
  patches `runtime.bundle.bin`, which creates the file empty and then fails the seek (`queue=2`,
  `stage=CurrentSetCompletion`, bundle size 0). That was the first false lead of this slice.

### Verification recipe

- Focused: `pio test -e native -f test_storage_drain_restore` — 14/14 (10 pre-existing + 4 new).
- Broad: full `pio test -e native`; build: `pio run -e teensy41-capture-serial` with the RAM1 padding read
  (floor 840). No device claim: this slice changes no firmware behavior and needs no stimulus.

### Completion record (2026-09-26)

- **No production code changed.** `git diff` on `src/` is empty; the only changes are the two test files and
  the harness seam. The footer's live read is confirmed correct by evidence, so the S4.1 remainder item
  "`DeferredSaveJobStages`' footer read" is **closed as verified**, and the snapshot-at-admission variant is
  rejected (DEC-090).
- **Fixture implemented and discriminating.** Focused suite 14/14; the four cases assert the pause point
  itself (stage, footer stage, cursor) before the handoff, so a fixture that never reached the footer cannot
  pass.
- **Red proof (run, then reverted).** With the footer's two live reads temporarily replaced by the
  interval's slot — pinned across *every* bundle write of the sequence, which is what a mechanical
  "replace it with the outgoing capture slot" change means:

  ```
  test_s4_1_the_footer_records_the_focus_at_footer_write_time_not_the_interval_slot:
      focus after a handoff before footer emission: restored selectedTrack=0 active=1 selected=1
      (expected track 0 slot 3)                                            [FAILED]
  test_s4_1_control_the_no_switch_sequence_restores_the_unmoved_focus     [PASSED]
  test_s4_1_a_handoff_after_the_footer_was_written_is_requeued_and_lands:
      focus after a handoff past the footer: restored active=1 selected=1 (expected 3)  [FAILED]
  test_s4_1_a_failed_footer_write_never_publishes_and_the_retry_lands_the_handoff:
      focus after the retry: restored active=1 selected=1 (expected 3)     [FAILED]
  ```

  The control passing in the same run is what makes the three failures a discrimination rather than a
  constant. **A first red-proof attempt was a false green and is recorded rather than replaced:** pinning
  only the `Slot`-keyed writes left the handoff's own requeued `WorkspaceFooter` write reading live state, so
  it repaired the footer and the case passed. That is a real robustness property of the route, and it is why
  the proof has to pin every write.
- **Red proof also corrected the fixture.** The original `establishDurableFocusOnA` asserted only
  `workerIsIdle()`, which a *failed* save satisfies after it drops its obligation (`pending = false`,
  `lastCompletedOk = false`). It now asserts the completion and the presence of the bundle, and parks the
  focus first so the selection route really changes it (the route returns early when the target is already
  selected, which would have owed no save at all).
- **Native verification:** focused `test_storage_drain_restore` 14/14; full native run and capture-serial
  build recorded in the session closeout (see `docs/Runtime/CURRENT_WORK.md`).
- **Task state:** S4.1 stays **unchecked**. Its release acceptance is still partial (the working-set *lease*
  release on a same-track slot switch is not asserted by name anywhere; the S3 gate's device legs remain),
  and this slice closed only its persistence-identity remainder.

---

## S4.1 — working-set lease release on a same-track slot switch (2026-09-26, G2 execution packet)

### Identity and scope

The remaining leg of S4.1 after the persistence-identity slice: the A/B/C device run evidences the
**finalization-capacity** release (`canonical_release,4,40,6144,8`) and C's reuse, but nothing asserts that
the **working-set lease** of a retired slot is released on a **same-track** switch. For a same-track
sequence the take-start sweep excludes the capturing track, so this packet establishes which production
route actually can release it, and pins that route with a fixture.

- Owner and production entry → release → observable: `TrackManager::finalizeCaptureAndSelectSlot` (the
  switch commit) → `Track::resetPlaybackStateForSlot` → `Track::invalidatePlaybackMergedMidiEvents` →
  `Track::releaseEligiblePlaybackRuntimeSlots` → `TrackPlaybackRuntime::releaseSlot(slot, generation)`.
- Status: **packet fixed before the fixture ran.** No behavior edit is authorized by this packet unless the
  fixture exposes a defect; the expected outcome is "no change, contract pinned".
- Files: `test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp` (fixtures only).

### G0 trace — the release surface and the same-track routes

1. **The entry.** `TrackPlaybackRuntime::releaseSlot(slot, generation, preserveLedger)` answers `NotHeld`
   (no runtime object / out of range), `StaleGeneration` (identity first), `RefusedBusy`
   (`LoopPlaybackRuntime::reset()` refused, which happens only while `captureStopOwner` is **pending**), or
   `Released` (`releaseAllWorkingSetLeases()` bumps the lease generation).
2. **The gate.** `Track::releaseEligiblePlaybackRuntimeSlots` skips every slot whose
   `noLiveOwnerForSlot(slot)` is false (ring job, accepted-interaction latch, live finalization, in-flight
   prepared handover), and reports non-`NotHeld` outcomes through the SESSION_CAPTURE telemetry only.
3. **The routes that reach it on one track.** The **slot-switch commit**
   (`finalizeCaptureAndSelectSlot` → `setActiveLoopIndex(newSlot)` → `resetPlaybackStateForSlot(newSlot)`),
   the mute/unmute and transport-tick playback resets, the memory-pressure lane
   (`tryReleasePlaybackMergedMidiEventsMemory`), and the undo/redo lane
   (`TrackUndo.cpp` → `invalidatePlaybackMergedMidiEvents(false)`). `releasePlaybackMergedMidiEventsMemory`
   is **cross-track only**: `TrackManager::releaseBackgroundPlaybackMergedMidiEventsMemory` takes
   `captureTrackIndex` and sweeps `i != captureTrackIndex`.
4. **A live finalization suppresses the whole sweep, track-wide.** `invalidatePlaybackMergedMidiEvents`
   early-returns while `hasPendingDeferredOverdubStop()` (a track-level predicate) and merely requests a
   stop-to-stopped; the per-slot predicate is not even consulted on that pass.
   `TrackPreparedStopCommit::completeDeferredCleanup` then clears `captureStopOwner` and arms
   `canonicalReleaseMarker*` — but it does **not** release the working set, and the maintenance edge that
   consumes the marker only reports the **capacity** release. So on a same-track sequence the retired slot's
   lease is released by the *next* playback-reset/switch (or memory pressure / undo), never by retirement
   itself.
5. **Device boundary, restated.** The retention run's `canonical_release,<slot>,…` row is that capacity
   marker (`captureStopOwner == nullptr` **and** `canonicalCaptureOutputAdmitted()`), not a lease release,
   and no lease-release emission exists on the device path — which is why this leg is native-only and why
   the emission would be its own decision.

### Oracle fixed before behavior edits

- **A — the switch releases the retired slot, per slot.** With A (retired) and B (incoming) both holding an
  admitted working set, and a live owner latched on **B**, the production switch A→B SHALL release **A**
  (`leaseHoldsWorkingSet(A)` false, `slotGeneration(A)` advanced, the pre-switch identity answering
  `StaleGeneration`, the new identity `NotHeld`) and SHALL leave **B** untouched (lease still held,
  generation equal). Control: with B's owner retired, the switch back B→A releases **B** the same way.
  A track-global release that ignored the predicate would release B in the first half and must fail.
- **B — refusal while a live owner needs the slot.** With a live finalization on A, `noLiveOwnerForSlot(A)`
  SHALL be false; `releaseSlot(A, genA)` SHALL return `RefusedBusy` with the lease held and the generation
  unchanged; and the production switch A→B SHALL still commit while leaving A's lease held. A release that
  ran under the finalizer must fail.
- **C — released once the finalization retires.** After the production grain retires A's stop, the next
  same-track switch (B→A) SHALL release A (unheld, generation advanced, pre-switch identity stale).
- Preserved invariants: `src/` unchanged unless a case fails; the release remains identity-qualified and
  per-slot; no new owner, state or emission is introduced by the fixture.

### Completion record (2026-09-26)

**No production code changed; the contract held.** Three fixtures in `test_runtime_working_set_ownership`
(23/23 in the suite), all three covering exactly the routes the G0 trace names:

| Case | Route exercised | Asserted |
|---|---|---|
| `test_s4_1_a_same_track_switch_releases_the_retired_slots_lease` | the switch commit (`finalizeCaptureAndSelectSlot` → `resetPlaybackStateForSlot` → the sweep) with a live owner latched on the **incoming** slot | the retired slot is released, its generation advances, its pre-switch identity answers `StaleGeneration` and the current one `NotHeld`; the owned sibling is **untouched** (lease held, generation equal). Control: retire the owner and switch back → the same route releases the other slot |
| `test_s4_1_a_live_finalization_refuses_the_retired_slots_lease_release` | a live finalization on the retired slot; the entry called directly, then the switch | `noLiveOwnerForSlot` false; `releaseSlot(A, genA)` → **`RefusedBusy`** with the lease held and the generation unchanged; the switch still **commits** (an admitted stop does not hold the active slot, S4f.3) and the lease survives it |
| `test_s4_1_the_retired_slots_lease_is_released_once_its_finalization_retires` | the production grain retires the stop, then the next same-track switch | the predecessor keeps its lease **through** the handoff, `noLiveOwnerForSlot` is true after retirement, and the next switch releases it (unheld, generation advanced, old identity stale) |

**Which route actually releases it — recorded because it is not the retirement.** The release is *not* a
consequence of `TrackPreparedStopCommit::completeDeferredCleanup` (it clears `captureStopOwner` and arms the
capacity marker, and releases no working set). The route that reaches the retired slot on one track is the
**next playback reset**, i.e. the next slot switch's `resetPlaybackStateForSlot` — or the memory-pressure /
undo lanes. That is what case C drives.

**Red proofs (run, then reverted; `src/` ends clean).** Each guard the fixtures rely on was removed
temporarily, in two steps, to attribute the failures:

- **R1 — `releaseEligiblePlaybackRuntimeSlots` without its `noLiveOwnerForSlot` gate** (the predicate-free
  sweep the pre-S4.1 `resetAll` was):
  `test_the_release_sweep_skips_a_slot_with_a_live_owner` and case **A** fail
  (*"a slot with a live owner must survive the switch"*). Cases B and C still pass — the *finalization* kind
  is protected a second time by `reset()`'s own refusal, so the predicate is not the only layer there.
- **R1 + R2 — also dropping `LoopPlaybackRuntime::reset()`'s pending-finalizer refusal**: case **B** fails,
  *"Expected 6 Was 1. a live finalization must answer RefusedBusy"* (6 = `RefusedBusy`, 1 = `Released`), and
  four pre-existing fixtures fail — the guards are load-bearing beyond this packet.
- **R1 + R2 + R3 — also removing `invalidatePlaybackMergedMidiEvents`' track-level early return on
  `hasPendingDeferredOverdubStop()`**: case **C** fails, *"the predecessor must keep its lease while it
  finalizes"*.

  So the live-finalization protection at a switch is **triple-layered** (track-level early return →
  per-slot predicate → the runtime's own `reset()` refusal), and each layer is independently observable in
  the fixture set. That is a stronger statement than the packet assumed, and it is why R1 alone is green for
  B and C.

**The reconciliation's "needs an emission" clause was stale.** `DIAG,ws_release,<slot>,<generation>,<outcome>`
already exists — added in `bbedfeac` *("make the working-set lease release observable by name")*, emitted
from the sweep with the refusal reported instead of discarded (`NotHeld` stays silent), and already **Tier-A**
(`include/Utils/CaptureLineTier.h`), so the timing-critical shed cannot remove it. What remains for S4.1's
device leg is therefore only a **run** in which the path fires for the slot under test (a cross-track take
start, memory pressure, or a same-track switch after retirement), and an oracle that reads the row — not new
instrumentation.

**Verification:** focused `test_runtime_working_set_ownership` 23/23; full native and the capture-serial
build recorded in the session closeout (`docs/Runtime/CURRENT_WORK.md`). No device action, no upload, no
stimulus: no firmware behavior changed.


---

## F4 — a failed workspace completion is discharged, not retried at the queue head (2026-09-26, G2 packet)

### Identity and scope

Found while building the S4.1 persistence fixture: a persistently failing `FinalizeWorkspace` step retries
**at the queue head** and blocks everything behind it. This slice fixes that failure policy; it is a
deliberate firmware change with its own decision (**DEC-091**), unlike the two verification slices above.

- Owner and entry: `StorageManager::processDeferredSaveState` → `stepPersistenceWorkItem` →
  `stepFinalizeWorkspaceWorkItem` → the `CurrentSetCompletion` sub-stages (`patchLastActiveUnix`, the bundle
  CRC refresh, `workspace.bin`).
- Status: **packet fixed before the behavior edit.**
- Files expected: `include/StorageManagerInternal/PersistenceWorkQueue.h`,
  `src/StorageManager/PersistenceWorkQueue.cpp`, `src/StorageManager/PersistenceWorkItemPersist.cpp`, plus
  the fixture in `test/test_storage_drain_restore`.

### G0 trace — what a completion failure does today

1. **The obligation is dropped but the item is not.** `stepFinalizeWorkspaceWorkItem`'s failure branch sets
   `pending = false`, `lastCompletedOk = false`, `failedAtMs = millis()`, emits `PERS,result,…,failed`, and
   then calls `completePersistenceWorkItem(job, "finalize", false)`, whose `ok == false` arm is
   `PersistenceWorkQueue::requeueWritingItem(job.item)` — a **`queuePushFront`**, so the same item is the
   head again on the very next slice.
2. **Every retry re-enters the same sub-stage.** `stepDeferredWorkspaceFinalizeSlice` only calls
   `beginCurrentSetCompletion()` when the stage is *not* already `CurrentSetCompletion`, so the retry resumes
   the failed step deterministically. Nothing else in the queue can run: `beginWriteQueuedItem` peeks the
   head, and the head is this item.
3. **The consequence is not confined to persistence.** `hasDeferredSaveWork()` is
   `hasPersistenceWorkPending()` = `pending || queueDepth > 0 || writing > 0 || itemActive`. With `pending`
   cleared but the item still queued, the predicate stays **true forever**, and two unrelated operations read
   it: the save-new-set pre-flight (`SavedSetIo.cpp`, refuses while it is true) and the read-only slot-payload
   observation (`readSlotPayloadPageForHitl` → `PSTR("busy")`, `LibraryObservationSession.cpp`). So one SD
   error at completion disables save and the `!SLOT_PAYLOAD` observation for the rest of the boot.
4. **What the contract already says.** `docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md` scheduler rule 5:
   *"Dirty flags — edit/loop dirty state clears only after `PERS,result,…,ok`. Failed or incomplete saves
   leave dirty set for retry."* The retry the contract names is the **dirty set** driving the next save
   trigger, not an unbounded same-turn re-run of a deterministic step.

### Oracle fixed before behavior edits

With the completion's first write refused by the card while a slot-keyed save is in flight:

- the `FinalizeWorkspace` item SHALL **not** be requeued: within a bounded number of worker turns
  `queueDepth() == 0`, `writingWorkItemCount() == 0` and no item is active;
- `StorageManager::hasDeferredSaveWork()` SHALL be **false** afterwards, and the read-only observation
  entry SHALL answer `"ok"` rather than `"busy"` (the user-facing consequence being fixed);
- the failure SHALL be **reported, not silent**: `lastCompletedOk == false`, `failedAtMs != 0`;
- the **dirty set SHALL be preserved**, and a later slot-keyed save SHALL complete
  (`lastCompletedOk == true`) once the card is healthy — the documented retry;
- the same card fault must **not** be able to hold a later-admitted item behind it.

Counterexample that must fail: the current `queuePushFront` requeue (the item keeps the head, and
`hasDeferredSaveWork()` never clears). Boundary kept deliberately out of scope and recorded: the same
front-requeue shape exists for the body/bundle slice failures (`LoopPersist`, `TrackMeta`/`SlotMeta`/
`WorkspaceFooter`), where retrying is the *durability* intent for content; whether those should be bounded
is a separate decision, not silently folded into this one.

### Completion record (2026-09-26)

**Implemented (DEC-091).** `abandonPersistenceWorkItem` (FLASH-resident) reports the failure once
(`PERS,result,…,failed` + the `emitPersistenceWorkTelemetry` row) and lands the writing entry in the queue's
existing terminal tombstone via `markItemPersisted` — not Queued, not Writing, reclaimable by a later
admission — instead of `completePersistenceWorkItem(…, false)`'s front-requeue. The completion failure branch
calls it; nothing else changed. The dirty set is untouched, so the retry the contract names — the next save
trigger — still runs.

**RAM1, measured rather than assumed.** The first cut added a *named* queue entry point
(`abandonWritingItem`) so the call site would read "abandoned" rather than "persisted". It cost **256 B of
ITCM** — the queue TU is ITCM-resident, unlike the `STORAGE_PERSIST_MEM` persistence TUs — and the budget
check refused it (`padding 856 → 600`, floor 840). The API was **withdrawn** rather than the floor widened:
the terminal state it wrote is exactly `markItemPersisted`'s, and the naming is served by DEC-091 plus a
call-site comment. Final build: `code 392,360 / padding 856`, byte-identical to the pre-slice firmware.

**Fixture** `test_f4_a_failed_completion_is_discharged_and_releases_the_queue` (in
`test_storage_drain_restore`, reusing the S4.1 harness): it pauses when the owed save is down to its
completion, holds the card in write-refusal from the completion's first sub-step, and asserts the failure is
**reported** (`lastCompletedOk` false, `failedAtMs` set), the item is **discharged** (queue depth, writing
count and active item all zero), the guard the unrelated consumers read is false and **stays** false
(`hasDeferredSaveWork()`), the dirty set is **preserved**, and a later save trigger **completes** once the
card is healthy.

**Red proof (run, then reverted; `src/` ends with only the DEC-091 change).** Restoring the front-requeue
fails the fixture exactly on the defect:

```
test_f4_a_failed_completion_is_discharged_and_releases_the_queue:
    Expected 0 Was 1. the failed completion must be discharged, not left holding the queue
    (stage=6 … queue=1 writing=0 itemActive=0 … completedOk=0)                          [FAILED]
```

**A false green in the first cut of the fixture is recorded rather than quietly replaced.** The fault was
originally injected for a single turn, and the requeued item then *succeeded* on its retry — which is exactly
what a front-requeue is for — so the reverted policy passed. The fault is now held for the whole assertion
window; the case under test is a **persistent** failure.

**One pre-existing acceptance fixture was re-premised, and this is the interaction to review.**
`test_s2b7_the_drain_services_a_parked_instance_holding_the_session` asserted the restore-progress counter
advancing, which the drain could only achieve by looping — and it looped only because the failed completion
was re-queued at the head. Two things changed with DEC-091, and both are asserted directly now:

1. the drain services the parked instance for the single frame its episode has, and that frame performs the
   **ownership transfer** the case is about — probe-verified: before `parked=1 3/2`, after `active=1 3/2
   ph=2 parked=0`. The counter form is gone because the spin that produced a second frame is gone;
2. the verdict for this fixture's shape (a save whose completion **fails** at the same time as a commit-gated
   restore) is now `Failed` with no named wait, where it used to be
   `TemporarilyBlocked/waiting_commit_gate`. The old verdict was the spin breaking out on the gate before the
   completion was ever stepped; a failed save must be reported as failed rather than masked by the restore's
   wait. The **purely blocked** shape keeps `waiting_commit_gate` — its sibling case
   (`…_advances_a_session_holding_restore_and_names_the_commit_gate`) still passes unchanged.

**Verification:** focused `test_storage_drain_restore` 15/15; full native and the capture-serial build
recorded in the session closeout. No device action: this is an SD **fault** path, and no device fault
injection exists for it, so device evidence is owed rather than claimed (recorded in the task entry).


---

## S4.1 device leg + S4f.3 no-concession run — first authorized campaign (2026-09-27)

The first authorized end-to-end attempt at the two device legs S4.1 and S4f.3 still owe. The run **executed on
the authorized targets** and its identity is pinned, but its verdict is **`SETUP_INCOMPLETE` (exit 3) — not a
pass**, and the working-set lease release was **not** observed. Both device legs therefore stay open. This
section records what ran, what it proves, and the three findings that must be settled before the next attempt.

### Authorization and targets, from the device's own census

`scripts/hitl_boot_census.py --library-observe` (read-only: resets by re-upload, sends only `!LIB_OBSERVE`,
writes nothing, clears nothing, never moves the transport) established the library state first, because the
retention scenario needs **three empty slots on one track** and the recorded authorization was a single
scratch slot:

| census | committed slots | note |
|---|---|---|
| baseline (`captures/s41-track5-census-20260926_235718`) | **60 / 64** | every slot retains an SD payload (`payload_inventory=64`); **track 5 has no empty slot**; the only empty slots on the device were track 3 slot 2, track 7 slot 1, track 8 slots 4 and 8 |
| after preparation | 59 | track 8 slot 5 cleared |
| after the run | 62 | track 8 slots 4, 5, 8 now hold the seeded/recorded takes |

No track had three empty slots, so the user authorized **clearing exactly one slot**: track 8, with A = 4,
B = 5 and C = 8. Preparation ran `hitl_retention_handoff.py --prepare-only`, and every clear came back
**identity-grade** (`cleared_slot_from_lines` matched the firmware's own `(active_slot=N)`, not a bare
`Track cleared` marker). A second read-only census then confirmed all three targets `committed=False` before
any gesture was sent.

**Target identity is proven by state, not by a log label.** The run log contains `Button press: Track 8
(short)` / `Selected track 8` (1-based, user-facing) beside `[TRACK] Track 7 armed` — the arm site logs the
**0-based `trackIndex`**. The two bases are adjacent in one log and cannot be reconciled by reading; the
census diff settles it, and it shows the writes landing exactly where authorized:

```
slots that CHANGED during the run:  track 8 slot 4, track 8 slot 5, track 8 slot 8   (pre=False -> post=True)
track 7: unchanged in every slot
```

`track 7` in that message is track index 7, i.e. the eighth track. **This cost the first reading of the run**:
the log alone was read as "the run went to the wrong track", which the census disproved. The lesson is recorded
because the same trap will recur in every future device session against a log with two track-number bases.

### What the run produced

Identity: `git_commit 5849d620ebc5`, `firmware_hex_sha256 adbb079fd552`. Fresh boot, pinned identity, no ring
eviction, complete observation tail, tempo 120.0 BPM verified by read-back, seed content confirmed on A.
The oracle reports every chain link present in order:

```
  [ok  ] (setup)          fresh_boot / identity_pinned / capture_integrity / observation_tail_complete
  [ok  ] (setup)          tempo_verified 120.0 BPM / seed_content_on_a (RECS,stopToStopped,4,2091,0,2091,2304,0)
  [ok  ] (retention/L1)   L1_finalizer_outstanding   DIAG,reentry_window_open,4,1709,2304 at 37379958 us
  [ok  ] (retention/L4)   L4_release_names_the_retiring_owner  slot 4, generation 0, 2304 ticks, 8 chunks
  [ok  ] (retention/L4)   L4_capacity_released       DIAG,canonical_release,4,0,2304,8 at 37414736 us
  [FAIL] (setup)          capacity_denied_window_wide_enough: 34778 us (34.8 ms) against a 150000 us floor
  [????] (evidence/L2)    L2_denial_identified_for_b: the denial happened, but its human row carries no slot
                          or interval and the identity-grade DIAG,reentry,deny row is absent
verdict: SETUP_INCOMPLETE (exit 3)
```

The device drove the whole sequence — A's overdub, the switch to B, B's overdub, B's stop **denied on
capacity**, C's switch/start **retained**, capacity released, C started once and recorded — and the timely
human rows show it:

```
[35.816] [TRACK] Reentry window open slot=3
[35.921] Loop 5: Switch gesture from slot 4
[36.329] Loop 5: Stop Overdub
[36.329] [TRACK] Overdub stop refused: stop_job_pending phase=7 dest=1 ...
[36.545] Loop 8: Switch gesture from slot 5
[36.545] Track 7 loop 8: switch and record start retained (stop not yet admitted)
[37.379] [TRACK] Reentry window open slot=4
[39.416] Recording started @ tick 2304
```

Content: the dump declares 2 expected rows, `storage_matches_effective=True` and `max_bar_refs=2`. The
whole-playback review returns FAIL/`inventory_equal=False` on every cycle, which is **expected here and not a
parity failure**: track 8's other slots still hold committed content whose notes sound alongside the loop
under test (reported as `EXTRA`), exactly the reading S4f.3's L7 reassessment recorded. The dump's own
completeness is the deciding evidence.

### Three findings the campaign produced

1. **The oracle's window-width measurement mixes two timing sources, and the 34.8 ms it reports is not
   trustworthy.** `capacity_denied_window_wide_enough` computes `release.us - window_open.us` from the
   **deferred `#CAP` rows**, while the same file documents that those rows "queue behind the deferred stop's
   flood and arrive seconds late" and uses the timely **human** rows as the edge everywhere else. In this run
   both intervals' releases were flushed in one burst 48 us apart (`canonical_release,3` at 37414688,
   `canonical_release,4` at 37414736), so slot 4's release timestamp is *the flush time*, not the availability
   edge — the same defect class as the "prefer the timely human rows" fix in `6e13c210`, but in the one check
   that was not converted. The human rows put the two window opens **1.56 s apart** (35.816 → 37.379), i.e. the
   same order as the 1.335 s/1.55 s figures the earlier passes measured. **So the window may well have been
   wide enough and the check cannot tell.** This is an oracle defect to fix before the next run, and it is
   deliberately **not** fixed here: the run above is the evidence for it.
2. **L2's identity gap is real, and it is a half-converted emission — verified in the source, not inferred.**
   `Track::stopOverdubbing` (in `TrackOverdubLifecycle.cpp`) has **two** refusal rows, and only one of them
   carries identity:

   * the `headJob != nullptr && headJob->admission.pending` branch emits
     `Overdub stop refused: %s phase=%u dest=%u closure=%u recycle_owner=%u recycle_started=%u undo_ready=%u`
     — **no slot, no interval**. This is the row this run produced
     (`stop_job_pending phase=7 dest=1 closure=0 recycle_owner=0 recycle_started=0 undo_ready=0`);
   * the `else` branch emits the identity-bearing
     `Overdub stop refused: %s slot=%u interval=%lu`, with the comment that the human row is the timely edge and
     "without the identity it cannot attribute the denial".

   So the S4f.3 logging fix covered the **non-pending** refusals, and the one reason that governs the
   successor's stop in exactly this scenario — a predecessor stop job still pending — was left out. That is
   either an unfinished conversion or a deliberate scope limit; it is **not** yet a regression, and it is
   **not** the `successor_stop_denied_capacity` reason either. Closing it is one emission on that branch and
   would let L2 be read from the timely row (and needs the RAM1 check, since this TU is not obviously
   FLASH-resident). Recorded as a finding, not fixed here.
3. **The working-set lease release is not exercised by this shape — measured, not inferred.**
   `DIAG,ws_release` appears **zero** times in a run that completed the full A/B/C chain. The G0 trace already
   explains why and the run confirms it: the same-track handoff excludes its own track from the take-start
   sweep, so no sweep runs over the retired slot; the cross-track sweep only releases slots on *other* tracks,
   and no such start happened while another track's slot still held a lease. S4.1's packet names three routes
   for the device row (a cross-track take start, memory pressure, or a same-track switch **after** retirement)
   — this campaign exercised the chain but **none of those three**, so the row remains owed and the route must
   be chosen deliberately rather than hoped for. **Superseded by the re-run below, which retires route (a)
   outright:** the cross-track sweep cannot fire the row *at all*, because leases are only ever acquired on the
   track that armed the record. See § "S4.1's lease-release device row: the cross-track route is a structural
   dead end".

### Reproduction

```
# 1. read-only state (resets by re-upload; writes nothing)
.venv/bin/python scripts/hitl_boot_census.py --out-dir captures/<census> --library-observe \
    --settle-s 25 --observe-window-s 30
# 2. prepare the authorized targets (identity-grade clears, stopped transport)
.venv/bin/python scripts/hitl_retention_handoff.py --track 8 --slot-a 4 --slot-b 5 --slot-c 8 \
    --out-dir captures/<prep> --prepare-only
# 3. re-census and require all three empty, then run with the census enforced
.venv/bin/python scripts/hitl_retention_handoff.py --track 8 --slot-a 4 --slot-b 5 --slot-c 8 \
    --out-dir captures/<run> --census captures/<census>/census.json
# 4. content pipeline, then the oracle
.venv/bin/python .agents/skills/hitl-content-verification/scripts/dump_and_capture.py --out-dir captures/<pb>
.venv/bin/python .agents/skills/hitl-content-verification/scripts/verify_playback_content.py captures/<pb>
.venv/bin/python scripts/hitl/retention_probe.py --session-log captures/<run>/session_*.log \
    --metadata captures/<run>/metadata.json --slot-a 4 --slot-b 5 --slot-c 8 --track-b 8 \
    --content-dump captures/<pb>/serial.log --content-review captures/<pb>/content-review.json \
    --expect-successor-note 67 --out captures/<run>/retention-review.json
```

**Device state after the run:** shutdown verified `complete` (2 stop+drain cycles reaching verified
persistence idle), no latch. Track 8 slots 4, 5 and 8 now hold the campaign's takes, and the content of
track 8 slot 5 was cleared under the user's explicit authorization for this run.

**No acceptance is claimed.** S4.1's lease-release device row and S4f.3's no-concession run both remain open;
what changed is that the campaign is now executable, its identity is pinnable, and its three blockers are
named with evidence instead of assumed.


---

## S4f.3 no-concession device gate — CLOSED `RETENTION_PASS`, and the lease-release route corrected (2026-09-27)

The re-run the first campaign's findings called for, on the same authorized targets (**track 8, A=4, B=5,
C=8**), with both fixes committed (`675a115a`) so the flashed image is pinned to them. **Result:
`RETENTION_PASS`, exit 0, 20/20 checks `ok`, `concessions: []`.** Identity `git_commit 675a115a8225` /
`firmware_hex_sha256 e76e1818e069`.

```
  [ok] (setup)       fresh_boot / identity_pinned / capture_integrity / observation_tail_complete
  [ok] (setup)       tempo_verified 120.0 / seed_content_on_a (RECS,stopToStopped,3,2051,0,2051,2304,0)
  [ok] (retention/L1) L1_finalizer_outstanding   DIAG,reentry_window_open,3,1117,2304 at 40659414 us
  [ok] (setup/L1)     capacity_denied_window_wide_enough  bounded open->denial 531.1 ms vs 150 ms floor
  [ok] (retention/L2) L2_denial_identified_for_b  DIAG,reentry,deny,1321,2,4,1 at 41190532 us, slot 4 interval 2
  [ok] (retention/L4) L4_capacity_released        DIAG,canonical_release,3,40,2304,8 at 42284312 us
  [ok] (retention/L3) L3_request_retained_for_c   retained row for slot 7 between the denial and the release
  [ok] (retention/L5) L5_same_b_stop_bound        DIAG,reentry,bound,1717,2,4,0 -- the denied interval 2 on slot 4
  [ok] (retention/L6) L6_successor_started_once   exactly one RECA,7 at 44257122 us, after the release
  [ok] (retention/L7) L7_successor_recorded...    note 67 in storage AND effective, fields agree
  [ok] (retention/L8) L8_bounded_followup_clean   no repeat start, no re-issued denial, no extra seal
```

**The two fixes did what they were for, and each is visible in the result.** L2 now reads the identity from
the **device-timestamped** deny row (`slot 4 interval 2`) instead of falling back to `UNKNOWN`, which is what
the `stop_job_pending` identity emission bought. And the exposure gate reports **531.1 ms bounded** where the
old formula would have said **34.8 ms** — the release turn's delta was 1624.9 ms here, so the two quantities
are now named apart in the report (`bounded_open_to_denial_*` vs `release_turn_delta_*`) and only the bounded
one gates. L3's match also needed the base conversion this run exposed: the retained row logs
`Track 7 loop 8` for the target selected as 1-based track 8 / slot 8, because `trackIndex` is 0-based.

**Three host defects were found by running, and fixed rather than worked around.** (1) `exit_code` was a
computed property that `report_dict` never serialized, so **every** review file omitted the authoritative
code — a consumer reading the record could not tell a pass from a partial; it is now a field. (2) The oracle's
CLI is **0-based** for slots *and* tracks (matching the firmware's own fields), while the HITL driver's CLI is
1-based; passing the driver's numbers makes a correct run look like it had no denial and no retained request
at all. That convention is now stated in the oracle's docstring and in each argument's help text, and the
human denial row's `slot=` is converted explicitly, because the firmware logs it 0-based. (3) The first cut of
the exposure fix computed its bounded interval from the **host**-timestamped row when the device one was
absent — subtracting host seconds-since-capture-start from device micros-since-boot, which produced a
*negative* window. That arithmetic is invalid for the reason the codebase already refuses to mix the two bases
for L5, so the width is now reported ungated instead.

**Content, per the content pipeline:** the `!CAPTURE_CONTENT` dump for the successor slot declares counts
`{'effective#0': 2, 'record#0': 2}` matching what was parsed, `storage_matches_effective: true`, and note 67
present in **both** storage and effective rows agreeing on tick/type/channel/note/velocity. The
whole-playback review returns `inventory_equal=False`, which is the known reading, not a parity failure: track
8's other slots still hold committed content that sounds alongside the loop under test.

### S4.1's lease-release device row: the cross-track route is a structural dead end

**`DIAG,ws_release` is absent again (0 rows) in a run that completed the full chain** — and this time the
reason is established in the source rather than inferred. A working-set lease is acquired **only** when
`Track::prewarmPlaybackForSlot` is called with `prepareCapture == true`:

```
  if (ok && runtime != nullptr && prepareCapture) {
    runtime->acquireWorkingSetLease(WorkingSetRole::PlaybackWindow | WorkingSetRole::LiveCapture);
  }
```

The only callers that pass `true` are the **record arm** and the **boot-cold overdub start**. Boot's
`prewarmBootPlaybackRuntimeSlots` passes `false` deliberately ("Boot reserves staging, not eight full
view/bridge/companion bundles before SD load"). And the cross-track sweep
(`TrackManager::releaseBackgroundPlaybackMergedMidiEventsMemory`) **excludes the capturing track**. Therefore
a lease can only exist on the track that armed the record, so a take start can never sweep a slot that holds
one: **S4.1's packet route (a) — "a cross-track take start" — cannot fire the row at all**, and this run is the
measurement that retires it.

What remains is route (c) in the packet, and it is the one the native fixtures already pin: **a same-track
switch after retirement**. The arm-then-start sequence in this run does not produce a release for A either,
because the successor's `startRecordingTrack` sweeps *other* tracks, while A's retired lease on the **same**
track is released by the next playback reset — `resetPlaybackStateForSlot` on the next same-track switch. The
device leg is therefore still owed, but it is now a **reachable** protocol (switch away from the retired slot
after its finalization retires) instead of a hoped-for one.

**Device state after the run:** shutdown verified `complete` (2 stop+drain cycles to verified persistence
idle), no latch. The campaign's own content occupies track 8 slots 4, 5 and 8; no user content was touched by
this re-run (preparation cleared campaign-authored test data only).

**Reproduction, with the bases as the oracle requires them (0-based):**

```
.venv/bin/python scripts/hitl_retention_handoff.py --track 8 --slot-a 4 --slot-b 5 --slot-c 8 \
    --out-dir captures/<run> --census captures/<preverify>/census.json
.venv/bin/python scripts/hitl/retention_probe.py --session-log captures/<run>/session_*.log \
    --metadata captures/<run>/metadata.json --slot-a 3 --slot-b 4 --slot-c 7 --track-b 7 \
    --content-dump captures/<pb>/serial.log --content-review captures/<pb>/content-review.json \
    --expect-successor-note 67 --out captures/<run>/retention-review.json
```



## S4f.13 — accepted-sequence driver correction (2026-09-27)

Execution packet: host-only, user-authorized; specified before edits. Owner: `hitl_accepted_sequence.py`, its gesture plan and managed `DeviceSession`; oracle: `accepted_sequence_probe.py`. Contract: task S4f.13 and DEC-078 — every interaction is accepted.

Source trace: `handleToggleRecordForSlot` reaches the capture-switch branch **before** the SD-aware playback branch, only while capturing. The proven retention plan’s B switch is A’s single stop; inserting a separate A stop removes that precondition. B must be populated for overdub. An empty C recording is a separate retention leg, not evidence of B overdubbing.

Controls/oracle fixed before edits: plan must match the retention prefix through B stop; no separate A stop and no record carrier assumed for a populated B. An unexpected queued/reaffirmed playback switch aborts before the next gesture; generic Playback-started lines cannot prove target identity. Abort must survive shutdown into the JSON report and nonzero exit. Record-only RECA/RECS evidence cannot pass an overdub leg. A window-open row cannot prove release, a wrong-slot or post-release start cannot prove overlap.

Verification: focused host tests and retention-plan regression, OpenSpec validation, Python compile and diff checks. Firmware/native build not applicable (host-only, no push/PR); no upload, SD operation, or hardware acceptance. Device-only timing/content and identity evidence remain open.

Completion record: host verified. `.venv/bin/python scripts/test_accepted_sequence.py`: 30/30;
`.venv/bin/python scripts/test_retention_handoff_plan.py`: 23/23;
`.venv/bin/python -m unittest discover -s scripts -p 'test_device_shutdown.py'`: 15/15.
Mutation reinstating the generic Playback-started guard: the new actual-driver regression fails once,
zero errors. This is a discriminating assertion, not a predicate-only test. A mocked managed run verifies
B-before-A seeding, shutdown before capture close, a written abort report and exit 3. No hardware was opened.
`openspec validate runtime-working-set-ownership --strict`: valid; Python compilation and diff checks pass.
No firmware/native build (host-only, no push/PR), no hardware acceptance. S4f.13 remains open.

Oracle boundary discovered: RECA/RECS originate in record owners; overdub lifecycle uses different,
currently uncorrelated evidence. An overdub invocation therefore marks record-specific links UNKNOWN,
not a firmware failure or acceptance. Counting successful stops cannot establish how many inputs were
sent, so one-press evidence is also UNKNOWN pending driver-report correlation. This slice does not
invent identity from generic human rows or weaken the task’s content/timing gates. The record leg’s
existing setup is not device revalidated by this overdub correction. No architectural decision changed;
no DEC appended. Nothing shipped to device, so deliverable shipping status is unchanged.

## Shared record/overdub finalization — contract review (2026-09-27)

Scope: [firmware contract](SHARED-CAPTURE-FINALIZATION.md), design Revision 25;
S4f.15 — contract through S4f.22 — integration gates. Read-only code trace and documentation
amendment; no new firmware or test execution. Existing 2,486 native / 33 Python fixture results
belong to the preceding HITL slice and are not proof of this lifecycle.

**Intent audit:** already planned at proposal live-capture role/cardinality, spec accepted
interactions/finalization rotation and design Revision 23 item 6 (record A then overdub B
stops while A finalizes). Missing: mode-complete implementation contract. The earlier suggestion
to weaken the record oracle is withdrawn; DEC-093 — shared capture finalization preserves intent.

### Adversarial findings and resolutions

- **BLOCKER addressed — raw record input is not overdub staging.** `recordMidiEvents` reserves
  staging only for overdub. Contract requires job-owned frozen raw record source and bounded
  normalization/preparation; copying the whole take or pretending staging is populated is
  forbidden. S4f.18 tests exact content and capacity/quantum behavior.
- **BLOCKER addressed — record identity and undo are not mode-neutral today.** `captureInterval_`
  advances only for overdub; current worker creates session undo. Contract extends actual-start
  identity and job-owned mode/geometry, with record-specific entry/pruning inside the existing
  protected publication. COMMON-IDENTITY and UNDO/STORAGE detect aliasing/incorrect grouping.
- **BLOCKER addressed — record callers assume synchronous completion.** Manager stop/switch/global
  Stop consume length, persist or clear notes immediately. Contract migrates every caller and
  carries geometry readiness separately from terminal publication. TRANSPORT/GEOMETRY verifies
  delayed admission, once-only updates, held notes and no early active-slot move.
- **BLOCKER addressed — prepared first bar need not be current at stop.** Raw coverage may need
  final-length normalization or still be building. Contract requires matching generation and
  normalized geometry, retained source lifetime and onward coverage before deadlines. No stale
  flag promotion or full-loop launch build. PLAYBACK-COVERAGE holds finalization across wrap.
- **BLOCKER addressed — delayed record undo pruning could destroy B's history.** Job stores its
  own baseline/pass and pruning is staged in protected publication; after-snapshot attachment
  identifies the entry, not the later stack tip. UNDO/STORAGE asserts exact per-interval effects.
- **TIGHTENING incorporated — same-slot restart and background readers.** Record start currently
  resets the producer; contract requires gating before mutation and rebinds immutable readers
  to their owning receipt. MODE-ROTATION and replacement cases cover stale pointers/leases.
- **TIGHTENING incorporated — claim of boundedness without per-unit evidence.** Full stop scans,
  normalization, map walks and slot-history pruning cannot be moved wholesale to an idle call.
  Contract assigns bounded work, with exact counters/byte accounting required in each packet.

Remaining attack surface is covered by explicit cases, not claimed as implemented:
other tracks and slot changes; concurrent live/frozen roles; record→stop→record and
OD→stop→OD; transport/clock changes; undo/redo; clear/load/save and persistence in flight;
long/dense loops and maximum supported working set; RAM1/PSRAM/source lifetime; MIDI starvation;
display/playback generations; compatibility wrappers and runtime regeneration. No new full
validate at stop and no second authority for activation or finalization.

**G2 review: PASS WITH TIGHTENING for the contract.** Required tightening is written into the
contract/tasks: actual per-slice closure, scalar/role byte accounting, production fixtures and
bounded-grain assertions before behavior edits. No unresolved product alternative remains.
This is not blanket firmware readiness: each execution packet must prove its own prerequisites;
resource/readiness/timing failures remain failures and cannot waive the shared-mode acceptance.

## S4.1 device leg — the working-set lease release row, observed (2026-10-01, G2 packet + record)

**Obligation.** S4.1 is checked on native evidence; what it owed was *evidence*, not code: a run in which
`Track::releaseEligiblePlaybackRuntimeSlots` fires for the slot under test, observed as
`DIAG,ws_release,<slot>,<generation>,<outcome>` (already emitted, Tier-A since `bbedfeac`), read by name rather
than inferred.

**The route, corrected by measurement.** A read-only survey of the whole capture corpus found the row **33
times** in ordinary runs of the mandated **base preset**, so it was never unreachable — the five 2026-09-27
probe attempts chased the wrong two of the three routes the retention oracle's own limitation text names. The
route that fires it is the **playback-reset/undo path**: `TrackUndo.cpp` →
`Track::resetPlaybackStateForSlot` → `invalidatePlaybackMergedMidiEvents` →
`releaseEligiblePlaybackRuntimeSlots`. Two structural reasons the probe shape cannot fire it, both confirmed in
the code: a same-track stop goes down the *retained* route and `invalidatePlaybackMergedMidiEvents` returns
early while a deferred stop job is live (`captureStopJob().mode == Record`) so no sweep runs; and on a
capture-switch shape the row that does appear names the **incoming** slot
(`captures/s41-lease-20260927_013834`: `ws_release,4,1,released`, nothing for the retired slot A).

**Observation.** `#CAP,3794271032,DIAG,ws_release,0,3,released` in the **PASS**-verdict base run
`captures/session_20260930_142350.log` (hex `577841d6…`), and `#CAP,69967546,DIAG,ws_release,0,0,released`
(plus two more) at HEAD in the 2026-10-01 runs, each landing **1.2 ms after** the device's own
`Button press: Record/Overdub (double)` → `MIDI: Undo (entries=3)` edge and before `VCACHE,stale_all`, for the
slot under test (`Loop 1` = firmware slot 0), identity `git_commit f63d5a02` / `firmware_hex_sha256 577841d6…`.

**The oracle half.** `hitl.lease_release` owns the row's wire form and its reading (three outcomes, `NotHeld`
deliberately silent, one 0-based→1-based conversion), `scripts/hitl_s41_lease_release_read.py` gates on the slot
under test and optionally on a required generation (0 observed / 2 not observed / 3 unreadable), and
`hitl.retention_probe` now takes the wire form from that owner instead of defining its own. `13/13` tests:
verbatim rows with provenance, the `RING,record_stats,DIAG,latch` read-back proved not to parse as a release, a
sibling slot proved not to stand in, absence and the refusal outcomes proved not to count as success, and the
edge selection proved to be by **device clock** (a managed capture writes the console and `#CAP` streams apart,
so the human row that caused the reset sits thousands of lines from the rows it explains).

**Found by running, fixed in the same campaign (host tooling).** Every base run at HEAD ended
`INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)` on `Record evidence cleanup failed: 'DIAG,latch'`:
`drain_record_evidence` reconciled *every* counted family against a row counter that only sees
`RECA`/`RECS`/`RID`, so task 2.2's latch family raised `KeyError: 'DIAG,latch'`. The reconciliation now states
which families carry a row cross-check and which are reported only, so a family is named rather than dropped;
`test_managed_record_drain` **23/23** (two new) and the mutation red-proved (iterating `stats` reproduces the
exact `KeyError`), restored byte-identically.

**Not claimed.** No firmware or behaviour change: the native fixtures remain S4.1's behaviour acceptance, and the
retired-slot case stays covered natively. Today's three HEAD runs reproduced the row while their *baseline*
verdicts were non-PASS for unrelated, named reasons (the drain defect above; `overdub_stored_span_too_short` plus
a missing persistence-result stage; `persistence_active_snapshot_unavailable_during_save`) — the documented
non-repeatable-green baseline problems, not S4.1 findings. Evidence, excerpts and the reader's JSON records:
[`evidence/2026-10-01-s41-lease-release/EVIDENCE.md`](evidence/2026-10-01-s41-lease-release/EVIDENCE.md).

Recorded as **DEC-099** (the lease release is read from its own row by one owner, and the evidence drain
reconciles only the families its log can count).

## S4.2 — the generation-mismatched release, rejected and named (2026-10-01, G2 packet + record)

**Scope.** S4.2 asks that a generation-mismatched release be *rejected and reported*, evidenced by a native
fixture reproducing **A-releases / C-reuses / late-A-cleanup**. Two halves landed on 2026-09-25: a generation
on `LoopPlaybackRuntime::captureStopOwner` (so a predecessor's late cleanup cannot null a successor's
finalization marker — fixture `test_a_late_cleanup_cannot_clear_a_successor_finalization_marker`, red-proven)
and the `(slot, generation)` lease entry (`releaseSlot` / `slotGeneration`, whose generation advances on
release). What the task's parenthetical still owed was the *sequence* with a live successor in place, and the
*naming* of the refusal.

**Added (test-only; no `src/` change — the rebuilt firmware hex is byte-identical, `577841d6…`).**
`test_s4_2_a_retired_owners_late_release_cannot_release_a_successors_working_set` drives the whole sequence on
the lease path: A admits through the production record-arm admission (`prewarmPlaybackForSlot(slot, true)`),
A retires through the track-level sweep (`releasePlaybackMergedMidiEventsMemory` → the entry S4.1 converted;
lease dropped, generation advanced), C **reuses** the slot (same runtime address, same role set, its own
identity — re-admission allocates nothing and bumps nothing), and then A's late release must answer
`StaleGeneration`: **not** the silent `NotHeld` an identity-free check returns for a slot that looks empty or
has been re-admitted. C's lease, roles and generation are asserted untouched, with the successor's own identity
releasing as the control. `test_s4_2_the_release_outcomes_are_named_as_the_device_row_names_them` pins the
emitter's outcome→literal mapping (`released`, `refused_busy`, `stale_generation`, `not_held`) — the vocabulary
`scripts/hitl/lease_release.py` decodes and `test_capture_line_tier` classifies — so "reported" ties to the
`DIAG,ws_release` row rather than to an enum ordinal.

**Red-proofs, each restored byte-identically.** Removing `TrackPlaybackRuntime::releaseSlot`'s identity check
(`rt->workingSetGeneration() != generation`) fails the new fixture (*"Expected 5 Was 1. the retired owner's
identity must be rejected as stale, not answered as NotHeld"*) plus the release-entry fixture and two S4.1
fixtures — the check is load-bearing for the whole family. Renaming the `stale_generation` literal fails the
vocabulary fixture (*Expected 'stale_generation' Was 'stale'*).

**Gates.** Focused suite **30/30**; full native **2,645 cases / 2,638 succeeded / 7 skipped / 0 failed**;
`teensy41-capture-serial` **SUCCESS** with the RAM1 line unchanged (`variables 96,192 / ITCM 393,216
(code 391,288 + padding 1,928) / locals 34,880`, floor 840).

**Recorded, not claimed away.** (1) A device run in which a *stale* release actually fires has never been
observed: the corpus carries only `released` rows, and the device refusal that has fired is `refused_busy`
(S4.1's live-owner case). The stale literal is therefore native-pinned and reader-consumed, not
device-observed — the rejection itself is native-verified, and the reader (`hitl.lease_release`) treats a
non-`released` outcome as "named, not a success". (2) The two identities stay **unaliased by design**:
`captureStopOwnerGeneration` is the interval's `frozenGeneration`, while the lease generation is the
slot-runtime lifecycle counter. No current caller needs them unified, and unifying them would have to respect
that a finalizing interval keeps its lease until its slot is actually released. (3) The `NotHeld` silence is a
rule of the *sweep* (`if (outcome != WorkingSetOutcome::NotHeld)`), not of the label, so it is
device-observable only — which is exactly why the reader treats an absent row as absence rather than failure.

## S4.4 — the declared cap policy: accept-and-schedule, and the report names its holder (2026-10-01)

**The question, and where its answer already lived.** S4.4 asks what happens when an activation is requested
beyond the declared envelope — refuse or queue — with the breach *reported* and never resolved by allocating.
The 2026-09-25 slice reported the breach and deliberately left the question open. It is not open: **DEC-078 item
1** logs the contract this change exists to implement — "acceptance is unconditional; execution is scheduled …
the user never re-issues" — so the activation proceeds and the existing accepted-interaction latch and retained
requests do the ordering. Refusing at the admission entry would *change the admission envelope*, which
`IMPLEMENTATION_CONTRACT.md` §2 returns to the architect, and would reject supported multi-slot flows (the first
cut broke eleven fixtures). "Resolved by allocating" is excluded by the same contract: the enforcing caps are the
lease's own (`WorkingSetLeaseTable`), which refuse at the point of custody, and §2 forbids pre-provisioning.
Recorded as **DEC-100**; no new product decision was needed, and this slice implements the policy's
consequences.

**What was still missing, and is now built (diagnostic-only; no admission, ordering or resource change).**
1. **The report was anonymous.** `#MEM_DENIED,…,reason=cap_reached,class=N` named a class bit and nothing else,
   so an operator could not tell "the envelope was really exceeded" from "the outgoing finalizer was counted as a
   second live capture" — the exact mislabel § S4b.1 corrected the record with. `Track::reportEnvelopeBreachIfAny`
   now returns the **holder** (`kNoWorkingSetClassHolder` = 0xFF when there is nothing to report) and the row
   carries `holder=<slot0|255>`; the first holder in slot order is named when several hold the class.
2. **The policy path was device-only.** § S4.4's own limit was "the emitted line is a device observation, not
   host-assertable". The `PIO_UNIT_TEST_NATIVE` branch of `MemoryMonitor` now records what the owner asked to
   report (`nativeTestCapDenials()` / `resetNativeTestCapDenials()`) — the same seam pattern as
   `setNativeForcedReserveDenied` — so the breach, its class and its holder are assertable on the host.

**Fixtures.** `test_a_declared_envelope_breach_is_reported_and_does_not_block_activation` (tightened) asserts the
empty census reports nothing and records nothing, that the check allocates no slot runtime — not even for the
slot it inspects, which is what separates censusing from preparing — and takes no pool chunks, that the breach
names slot 0 as the holder with class bit 1 (`LiveCapture`), that the activation is admitted, and that the
*production* admission path reports the breach itself (one report per check).
`test_s4_4_the_breach_report_names_the_finalizing_holder_instead_of_hiding_it` (new) drives the pair the
contract requires — an admitted (live) finalizer on slot 0, the incoming activation on slot 1 — and asserts the
report names the **finalizing** slot as holder while the incoming activation is admitted and the finalizer stays
in flight. That fixture is deliberately a *recorded fact*, not a fix: the census counts a slot whose capture has
stopped as a `LiveCapture` holder, and correcting it is S4b's role-transfer work (DEC-078 item 5, § S4b.1).

**Red-proofs, each restored byte-identically.** (1) Dropping the holder from the report/return fails both
fixtures (*Expected 0 Was 255*). (2) **Excluding** finalizing holders from the census — the candidate correction
— fails the finalizing-holder fixture, so the two possible corrections are discriminated rather than either being
blessed in passing. (3) Letting the census allocate the slot it inspects fails the no-allocation assertion.

**Gates.** Focused suite **31/31**; full native **2,646 cases / 2,639 succeeded / 7 skipped / 0 failed**;
`teensy41-capture-serial` **SUCCESS** with the RAM1 line **byte-identical** (`variables 96,192 / ITCM 393,216
(code 391,288 + padding 1,928) / locals 34,880`, floor 840), consistent with the change sitting in
`TRACK_COLD_MEM`/`FLASHMEM` code rather than in RAM1 state. Nothing stores the census: the check still reads the
per-slot lease state and returns its answer by value.

**Not claimed.** The policy is not enforced, so nothing is refused and nothing is queued *by this check* — the
queue is the interaction layer's, already covered by its own fixtures
(`test_the_arm_and_wait_requeue_retains_then_executes_exactly_once`,
`test_a_start_refused_by_its_own_live_stop_obligation_retries_to_a_single_take`), and cited rather than
duplicated here. The new `holder=` field has no device observation yet: no run has emitted a `cap_reached` row
since the change, and nothing under `scripts/` parses `#MEM_DENIED`, so the field is additive to the row's
readers.

## S4b.1 — the repeated switch while a predecessor finalizes: measured, and its bounded overlap asserted (2026-10-01)

**Why this slice.** § S4b.1's own "still to do" list put the cross-track and **repeated B → C switches while A
still finalizes** next, with the policy attached: "*Omax = NUM_TRACKS inbound holders; an outbound
live/finalizing role is already counted in C/D and is never released to satisfy O*". The cross-track case already
has a fixture; the *repeated same-track* case had none, and the earlier records described the mechanism in prose
only (the 2026-09-25 note on `(a)`/`(b)`, and the retention record). The deliverable here is therefore
**characterization first, then the acceptance fixture** — and the characterization corrected two of my own
assumptions, which is recorded below rather than smoothed over.

**Measured mechanism (this is new information, not a restatement).** Sequence: A overdubs on slot 0 → A's stop
admitted and paused → first switch to slot 1 commits and B records/overdubs → **second switch to slot 2 while A
still finalizes**. Measured on the fixture:

* the active slot **does not move** and the target's runtime is **never allocated** — the gesture retains rather
  than provisions;
* B's stop is **accepted and latched against B's own slot**: `hasAcceptedInteractionForSlot(1)` is true while
  `hasLiveCaptureStopJobForSlot(1)` is false, and the target's record start stays **queued**
  (`isRecordingQueued(0, 2)`), so the S4f.6 *carrier* plus the S4f.1 *latch* is what holds the request;
* A's live finalizer and lease and B's lease all survive — nothing is released to satisfy the inbound;
* the pool does **not** grow across the gesture (490 → 490 chunks), and once A retires and the deferred lane
  turns, the retained request **executes with no further gesture**: C becomes active, provisioned, B's latch
  clears, and the steady state neither drifts nor keeps consuming chunks.

**Two assumptions the probe corrected, both worth recording.** (1) My first red-proof mutated the *overdub*
branch of `finalizeCaptureAndSelectSlot`, and nothing failed: instrumenting the route showed the repeated switch
takes the **record** branch, because the empty target made B a *record* capture (`handleToggleRecordForSlot(2)` →
`finalizeCaptureAndSelectSlot(0, 2, capture=1)`). A red-proof that "passes" on the wrong branch proves nothing,
so the fixture is red-proved on the branch it actually exercises. (2) The accepted interaction is a **Stop**
latch — `PendingInteractionLatch` declares `Switch` and `Start` kinds, but `latchAcceptedStop` is the only
writer, so a *switch* is never latched as such: it is carried by the target's queued start plus the predecessor's
accepted stop. The fixture's assertions were rewritten to say exactly that.

**Fixture and red-proofs.** `test_s4b_1_a_repeated_switch_while_a_finalizes_is_bounded_and_executes_once_capacity_frees`
(test-only; the firmware hex is byte-identical to the S4.4 image). Red-proved twice, each restored
byte-identically: provisioning the target inside the record branch's wait fails it (*"must not provision its
target on the gesture path"*), and removing that wait fails it (*"Expected 1 Was 2. the repeated switch may not
move the active slot"*). A third, deliberate **control** — emulating the pre-fix **unscoped** clear that S4b.1's
finding `(b)` describes — leaves this fixture passing and fails
`test_a_blocked_switch_and_start_survive_until_its_prerequisite_stop_binds` and
`test_the_successor_take_commits_with_content_after_a_transport_stop`: `(b)` is fixed in the code (the scoped
`clearQueuedRecordingTrack(trackIndex, captureSlot)`, with its own comment) and covered **by those two fixtures**,
so the attribution is now explicit instead of assumed. Focused suite **32/32**; full native **2,647 cases /
2,640 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS with the RAM1 line unchanged.

**Not claimed.** Check 7's *repeated* case is covered; `(a)`'s uncommitted-switch case (a capacity-denied stop is
latched and the switch gesture returns without a committed target) and `(d)`/`(e)` remain open as recorded in
§ S4b.1. `Omax` is asserted as **behaviour** (bounded, non-accumulating, nothing stolen) rather than as a new
named constant or budget object: the effective inbound bound is the per-track pending state, and the design's
`TransferOverlap` term stays a budget-arithmetic term owned by S2's accounting rather than by this stage.

## S4b.1 (a) — the "press dropped" reading was stale, and the real remainder has a named prerequisite (2026-10-01)

**What (a) said, and what the code does.** S4b.1 (a) recorded that "when `Track::stopOverdubbing` denies
admission on capacity, the switch returns `false` and the press is **dropped** rather than retained — that needs
the switch's own retained carrier". Reading the gesture rather than the note: the carrier exists.
`MidiButtonActions::handleToggleRecordForSlot` calls `finalizeCaptureAndSelectSlot`, and when it returns false,
the empty-target branch calls `TrackManager::retainRecordStartForSlot(trackIdx, slotIndex)` and logs "Track %d
loop %d: switch and record start retained (stop not yet admitted)" — explicitly, "the request is **retained**,
not dropped … `handlePendingRecordStart` then re-attempts activation on every eligible boundary and consumes the
request only at a successful start."

**The reproducer on the denied path.** `test_s4b_1_a_denied_stop_on_an_uncommitted_switch_retains_the_targets_start`
(test-only; the firmware hex is unchanged): a second slot's admitted finalization occupies the track's single
finalization capacity (DEC-078 item 6), the switch's predecessor then captures live beside it — the required
outgoing-finalizer + incoming-capture overlap — and *its* stop is denied. The fixture asserts the whole
contract on that path: the active slot does not move; the target's start **is** retained
(`isRecordingQueued`); the predecessor's stop **is** accepted-and-latched against its own slot; the occupant's
job and lease are untouched; and retiring the occupant's finalization plus turning the lanes makes the retained
start **execute with no further gesture** (target active and recording, latch cleared, nothing stolen).

**Two red-proofs, each restored byte-identically.** Dropping `retainRecordStartForSlot` from the gesture fails
this fixture (*"the target's start must be retained, not dropped with the gesture"*), the sibling repeated-switch
fixture, and the two existing retention fixtures — the retention is load-bearing, and now covered on both the
admitted and the denied path. Dropping the denied-stop latch fails this fixture at *"Expected 0 Was 1. an
uncommitted switch must not move the active slot"*: **the latch is what holds the active slot in place**, so what
(a) read as "the press was dropped" was the S4f.5 protection working. That distinction matters for the next
slice: nothing here is a defect to fix.

**The real remainder, with its prerequisite measured.** The *populated*-target variant does not take the record
carrier at all: with a target that has committed passes, the gesture queues a **playback** switch
(`queuePlayingSlotSwitch` → the state machine's pending switch plus `pendingSlotEnabled`), and that queued switch
commits in the **multi-slot-commit lane inside `TrackManager::updateAllTracks`** — the production per-tick entry.
Driving that entry from this suite **fails to link**, for exactly four symbols:

    Track::playMidiEvents(uint32_t, bool)
    Track::playMidiEventsForSlot(uint8_t, uint32_t, bool)
    TrackManager::isTrackAudible(uint8_t) const
    Track::shouldCommitQueuedPlaybackStart(uint32_t) const

So the pending-switch lane — where a queued switch actually commits and where the review's "silent dead switch"
risk lives (S4b.6/S4b.7) — is not yet drivable natively, and extending the shared harness's linked closure for
those four (following the `HARNESS_LINKS_*` pattern, without shadowing them in the fail-closed stub surface) is
the prerequisite for those fixtures. Recorded as a prerequisite rather than worked around: the alternative,
driving only the *dispatch* helpers the suite already links, cannot see a switch that commits in the tick lane.

Full native **2,648 cases / 2,641 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS with the
RAM1 line unchanged.

## S4b.1 — the switch lane's acceptance surface, and the populated-target queued switch (2026-10-01)

**Why a harness slice.** The previous slice recorded, measured, that the S4b.5/S4b.6/S4b.7 fixtures cannot be
written yet: a **populated** target does not take the record-start carrier, it queues a **playback** switch that
commits in `TrackManager::updateAllTracks`, and driving that production per-tick entry from this suite failed to
link for exactly four symbols. This slice discharges the prerequisite — and, doing so, measures the lane's
behaviour and pins it.

**What the closure needed.** Four symbols live in three TUs (`Track::playMidiEvents`/`playMidiEventsForSlot` in
`TrackPlaybackHotPath.cpp`, `TrackManager::isTrackAudible` in `TrackManagerPlaybackPolicy.cpp`,
`Track::shouldCommitQueuedPlaybackStart` in `TrackTransportControl.cpp`); including them collided with **ten**
stubs the suite's fail-closed surface defines — i.e. with the **whole play branch**. So the suite gains the macro
`HARNESS_LINKS_PLAYBACK_SWITCH_LANE`, and those ten stubs move behind it. Two consequences are stated rather than
absorbed: (1) a removed `OFF_PATH()` abort cannot have been *reached* by any fixture that was passing, so no
existing coverage was silently weakened — the change only replaces an abort with the real owner for routes that
previously could not run at all; (2) the loop-edit manager stays stubbed at its three referenced symbols (its
callback still fails closed), so the lane cannot leak into an edit session. Recorded as **DEC-101**.

**Two fixtures that observed through a removed stub now observe the real owner.** `setMasterLoopLength` was
stubbed *counted* (`g_nativeAdoptedMasterLengthCalls`), and the counter is what two S4f.21 fixtures asserted. With
the real policy owner linked, they assert `trackManager.getMasterLoopLength()` instead — the stop edge must adopt
nothing, the published geometry must be adopted, a second dispatch must change nothing ("exactly once" in state
terms), and a filtered take must adopt nothing. That is strictly stronger than the counter: it checks the state
the firmware will use, not a stub's bookkeeping.

**What the lane does, measured.** `test_s4b_1_a_queued_playback_switch_waits_for_the_predecessor_and_then_commits`:
a populated target pressed while the predecessor's stop is admitted queues a playback switch
(`hasPendingSlotSwitch` true, `getPendingSlotIndex` = target) and moves nothing; three full loops of
`updateAllTracks` later the request is **still pending**, with the predecessor's finalizer and lease intact; once
the finalizer retires the switch **commits with no further gesture** — active slot moved, pending cleared, enabled
set replaced with the target. **The review's "silent dead switch" does not occur on this shape.**

**Both named failure modes are now red-proved, each restored byte-identically.** Committing at any tick
(ignoring the boundary) fails the fixture at *"the queued switch must not commit while the predecessor
finalizes"*; never running the commit branch fails it at *"the retained switch must commit once the predecessor
retires"*. So the lane can neither switch early under a finalizing predecessor nor dead-end silently.

**Recorded, not claimed.** The wait is **emergent**, not an explicit guard: the pending switch is held by the
loop-boundary math (`shouldCommitLoopEndSwitch` with `lastTickInLoop` frozen while the predecessor finalizes), so
the fixture pins the two *sides* of the behaviour rather than an internal condition — a future refactor that makes
the hold explicit is welcome and this fixture will still describe it. And the commit's occupancy binding
(`Track::ensurePlaybackMergedEventsForSlot` on the clock path) is now reachable natively, which is precisely the
surface S4b.6 ("occupancy-aware, allocation-free binding") and S4b.7 (its off-clock provisioning owner) must
change; no claim is made here about that binding's current durability beyond this shape. Full native **2,649
cases / 2,642 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS with an unchanged firmware hex
and RAM1 line.

## S4b.6/S4b.7 — the occupancy the switch binds to, and who provides it (2026-10-01)

**The two stages, and the one sentence that joins them.** S4b.6 asks that the switch bind to the target slot's
**occupancy** (its playback runtime) without allocating on either hot path; S4b.7 names who may take that
occupancy instead. Both are about the same object seen from two sides, and the stage's whole risk is that doing
each half alone produces the opposite failure: `slotIfAllocated` alone leaves the switch **silently dead** (the
binding finds no runtime and the commit either short-circuits or lands on an empty slot), while provisioning
alone leaves allocation on a MIDI edge or on the clock.

**What landed.** The binding (`Track::ensurePlaybackMergedEventsForSlot`) now reads
`playbackRuntime.slotIfAllocated` and is a documented no-op when the slot has no runtime — so the *clock path*
that commits a switch and the *MIDI edges* that request one cannot take a runtime. The predicate has a named
owner beside it, `Track::hasPlaybackOccupancyForSlot`, and the commit branch in `TrackManager::updateAllTracks`
carries the matching term: an absent inbound occupancy keeps the switch **pending** rather than committing into a
slot whose runtime does not exist. Provisioning is a new off-clock owner,
`TrackManager::provisionSlotForSwitch` (idempotent; `prewarmPlaybackForSlot`-style; refusal reported as its own
`OperationKind::SlotSwitch`), reached through the **existing** deferred request owner: `requestSlotSwitch` marks
the request and `main.cpp`'s deferred frame takes the mark, provisions, then prewarms. No new lane, no new
eligibility state, and no new RAM1 state.

**The eligibility question, answered by reading the consumer rather than the predicate it points at.** S4b.7
warned that a pending switch "must make its track eligible for a deferred turn" because `hasRealtimeIdleWork` and
the snapshot cursor are state-based. The consumer turns out **not** to be gated by `hasRealtimeIdleWork` at all —
that predicate belongs to the maintenance scan (`processDeferredIdleMaintenance`) — so a marked request needs no
new term there. What bounds it is the deferred frame's own shape: the prewarm slot sits after the frame's early
return while a deferred overdub stop is outstanding, and the mark is cleared **only** by the take. A switch that
arrives beside a finalizing predecessor therefore provisions on the first eligible turn *after* that finalization
retires: a bounded delay, not a dead end — and the same ordering the S4b.1 lane measured. Changing that ordering
(provisioning the inbound role *before* releasing the outbound one) is S4b.2's own stage, not this one.

**A measurement that changed the fixture, not the code.** A forced `TRACK_PLAYING` plus `selectSlot` leaves
`lastTickInLoop` at `0` across 5,576 ticks: the loop-end wrap is never detected, so a queued switch can never be
*due* and the occupancy binding would never be exercised. The fixtures therefore reach the production playing
state the way the product does — an overdub **start** on a populated slot enables it, plays it and publishes its
pair — and they retire the predecessor's finalizer **before** the press, because a live finalizer freezes the
playhead (the S4b.1 finding) and would hide the term under test instead of testing it.

**Red-proved twice, each restored byte-identically.** Replacing the commit's occupancy term with `false` fails
both fixtures at *"the switch may not commit before its occupancy exists"* (`Expected 0 Was 2`: the commit landed
early, on a slot whose runtime did not exist). Reverting the binding to `trySlot` fails both at *"the MIDI edge
must not provision inline"*. So the two halves are pinned as complementary: neither alone is sufficient, and
neither can be relaxed without a fixture noticing.

**The finding that is owed, and it is not a missing fixture.** The entry asks for the same assertions on a
"demoted long loop". Measured with a 24-bar target (`> kMaxDetailedWindowBars x TICKS_PER_BAR` = 12,288 ticks)
carrying bar-spread committed content: after the off-clock owner has taken its occupancy the switch is **still
pending** (`ready=0 occ=1 pending=1`), because the commit's long-loop hold term
(`isPlaybackMergedMidiEventsReadyForSlot`) requires a **non-empty** windowed `mergedMidiEvents`, and the windowed
gather (`Loop::tryGatherPublishedSnapshotEventsInWindow`) holds the window *empty* (`GatherMissHoldEmpty`) unless
a published root supplies it. That is the review's "silent dead switch", on that shape only, and it is a
**hold/readiness** question rather than an occupancy one: either the shape needs a published-root fixture on the
windowed publication lane (S4c/F), or the hold needs an architect decision. The two committed fixture shapes stay
deliberately below the demotion bound and say so in their doc block. No claim is made here that a demoted
target's switch commits.

**Evidence.** Focused suite **36/36**; full native **2,651 cases / 2,644 succeeded / 7 skipped / 0 failed**;
`teensy41-capture-serial` **SUCCESS**, RAM1 `variables 96,192 / code 391,304 + padding 1,912 / locals 34,880`
(floor 840; the preceding slice measured `code 391,288 / padding 1,928`, so the binding and the occupancy term
cost 16 B of ITCM and no RAM1 state), firmware hex sha256 `ceb79f0a…`. **Recorded, not claimed:** the device leg
for this shape is S4b.8 and has not been run; the deferred-frame sequencing note above is a reading of the
consumer, not a device measurement; and the demoted-target hold is owed with the reason named rather than
deferred silently.

## S4b.7 — the provisioning turn the contract named (2026-10-01, second slice)

**Why this slice exists: the first one had the contract right and the vehicle wrong.** S4b.6/S4b.7 landed the
*shape* the review asked for — one off-clock provisioning owner, a commit that binds to occupancy without taking
it, a refusal with its own operation kind — but the vehicle was a reuse of the **slot-restore** prewarm request.
That is a shared `(track, slot)` record, so a restore's prewarm and a switch's provisioning could consume each
other's request, and a switch's provisioning was reachable only through the restore lane's gating
(`!timingCriticalTrackActive`, and the deferred frame's early return while a deferred overdub stop is
outstanding). The change's own binding corrections had already named the vehicle — Rev 20.3/21.7 and Rev 22 §6:
a **`SlotProvision` turn kind** in `MidiServiceDrain::DeferredIdleOwner`, "a pending switch makes its track
eligible", the turn running **both** the provisioning and `ensurePlaybackMergedEventsForSlot(targetSlot)`, exempt
from the all-stopped guard, with `kDeferredIdleOwnerCount` bumped to 6 and the cursor/cycle tests updated — and
the S4b.7 task text did not repeat that correction. Reading the correction rather than the task line is what
fixed it. Recorded as **DEC-103** (supersedes DEC-102's *vehicle*, not its contract).

**What is built.** `DeferredIdleOwner::SlotProvision = 5`; `kDeferredIdleOwnerCount = 6`; the turn body is
`TrackManager::servicePendingSlotProvision()`, dispatched from `runDeferredIdleMaintenance` **before** the
all-stopped guard, which is Rev 22 §6's exemption made structural: the turn returns before the guard is reached,
so it is reachable in exactly the states a retained switch sits in. The turn serves the first track whose
**pending switch** has no inbound occupancy and runs both halves: `provisionSlotForSwitch` (idempotent, reporting
its refusal) and then `ensurePlaybackMergedEventsForSlot(targetSlot)`. **The eligibility is the pending switch
itself** — `MidiServiceDrain::slotProvisionNeeded(hasPendingSlotSwitch, inboundOccupancyPresent)` — so
`requestSlotSwitch` now marks nothing and there is no request record to be consumed elsewhere. The restore lane
keeps its own `provisionSlotForSwitch` call, because that is the *restore's* occupancy need at the slot boundary
and it is what `ensurePlaybackMergedEventsForSlot` used to provide implicitly.

**What the tests had to change, and why that is the contract speaking.** The owner cycle is observable purely
through its header, so the drain suite re-pins it: the rotation, the six-owner absent cycle, the interleaved
session-exit sequence, and the backlog sequence with its per-period tally — now `two_of_seven`, because the extra
Snapshot after `LoadAndDisplay` still rides the backlog rule while `SlotProvision` joins the ordinary rotation.
A new pin asserts the turn's work is the pending switch and that `SlotProvision` does not depend on the visual
cache (a switch must be provisionable while a wrap append is pending). The S4b.6 fixtures model the real turn
instead of the request/take pair.

**Red-proof, and one measured coverage gap that is named rather than left silent.** Removing the turn's
provisioning fails three fixtures (*"the retained switch must commit once the predecessor retires"* and both
S4b.6 *"provisioning must have taken the target's occupancy"* assertions), restored byte-identically. Removing the
turn's `ensurePlaybackMergedEventsForSlot(targetSlot)` call, however, leaves **all 36 fixtures passing**: the
merged window is gathered from the loop's **published snapshot root**, a hand-committed slot has none, so the
gather misses (`GatherMissHoldEmpty`) and the window stays empty. That is the same root cause as the
demoted-target hold S4b.6 owes, and it is why the fixture now pins the harness fact (`ready == false`) as a
tripwire instead of asserting a readiness it cannot produce, and names the discharge path: a published-root
fixture on the windowed publication lane (S4c/F). Also still owed from the same contract line: the long-loop hold
"carries an age/bound", and a switch still pending after provisioning is not yet reported with an age.

**Evidence.** Focused suite **36/36**; deferred-idle-owner suite **36/36** (six owners, new pins); full native
**2,652 cases / 2,645 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` **SUCCESS** with RAM1
`code 391,288 + padding 1,928 / locals 34,880` (floor 840) — ITCM returns to its pre-slice value because the new
turn is `FLASHMEM`, and FLASH code grows by 120 B; firmware hex sha256 `257adab1…`. The board was flashed with
the *previous* image in this session (authorized); this image is **not** flashed.

## S4b.7 — the demoted target, the published root, and the bounded hold (2026-10-01, third slice)

**One root cause had two faces, and this slice closes both.** The previous slice recorded two owed items and named
their cause: the merged window is gathered from the loop's **published snapshot root**, so for a hand-committed
fixture slot the gather misses and the window stays empty (`GatherMissHoldEmpty`). That made (a) the *demoted*
target — above `kMaxDetailedWindowBars`, 16 bars, i.e. the windowed policy — unable to commit even after
provisioning, and (b) the `SlotProvision` turn's `ensurePlaybackMergedEventsForSlot` half **unprovable**: removing
it left all 36 fixtures green.

**The fixture side: a root that a restored slot would have.** `publishBarSpacedRootForSlot` builds a `LoopSnapshot`
through `LoopSnapshotConstruction` and installs it with the existing test seam
(`Loop::storePublishedSnapshotForTest`), asserting the root is *current*. Two details were measured rather than
assumed: the windowed gather walks the snapshot's **event** membership (`gatherPublishedSnapshotMembership` reads
`eventMembership(bar)`), so the change set must carry `SnapshotEventChange`s and not only notes — the first attempt
populated notes alone and the probe showed `gathered=1 events=0`; and the demoted loop's window is two bars centred
on the playhead (`kMergedMidiEventsGatherBars`), so the root's events must be spread across the loop rather than
clustered at tick 0. With both, the probe reads `gathered=1 path=Snapshot events=4`.

**The behavior side: an age/bound, and a terminal that reports instead of deciding.** `SlotProvisionHold`
(`kBoundBars = 4`, converted with `Config::TICKS_PER_BAR`) with a wrap-safe `holdExceeded`; the age comes from the
pending switch's own `pendingQueuedAtTick` (the state machine already stamps it, so the bound adds no timestamp);
one packed bit per track makes the report **once per episode** (cleared when the track has no pending switch); and
the check runs on the off-clock turn **regardless of whether that turn could make progress**, because the case the
bound exists for is a switch whose occupancy *was* taken and which still cannot commit. The terminal names the
switch (`slot_switch_hold_exceeded`, with track/target/age) and **nothing moves** — the pending switch, the latch,
the enabled-set replacement and any retained start stay as they were, so DEC-078 item 1 survives the bound.

**What is asserted, and how it was attacked.** Two fixtures on the same helper, differing only in whether the root
exists. With it: press → nothing inline → clock lane does not commit → the turn takes the occupancy *and* builds
the window → `isPlaybackMergedMidiEventsReadyForSlot` **true** → **commit**. Without it: the occupancy is taken and
the window can never be ready → no commit, switch **retained**, **exactly one** report at/after the bound naming
track 0 and the target, no re-report on further turns, and a **second episode** (cancel, a turn while nothing is
pending, press again) reporting on its own account. Red-proved three times, each restored byte-identically:
removing the published root, removing the turn's build (which now fails, closing (b)), and disabling the age test.
The drain suite pins the age test's own edges (unset sentinel, one-tick-short, inclusive bound, wrap safety,
zero-means-unbounded).

**Evidence.** Focused suite **38/38**; `test_midi_service_drain` **37/37**; full native **2,655 cases / 2,648
succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` **SUCCESS** with RAM1
`variables 96,192 / code 391,288 + padding 1,928 / locals 34,880` (floor 840) — the hold's mask fits the class's
existing padding, so the slice costs no RAM1 variable and no ITCM. Firmware hex `d3b9d2b1…`, **not flashed** (the
board still runs the earlier image; an upload needs a fresh ask). **Recorded, not claimed:** the terminal has no
device observation yet, and the 4-bar bound is argued from the two known waits rather than from a device
measurement of the hold's distribution.

## S4b.5 — one active-slot write authority, and the cancel that owns the trio (2026-10-01)

**Two defects, one stage.** The active slot had four writers (the manager entry, two direct
`Track::setActiveLoopIndex` sites, and an entry that would move the slot *underneath* a queued switch — two
transfers instead of one), and cancelling a queued switch was open-coded at five sites and done **partially** at
several others. The partial cancels are the real find: clearing only the state machine left the enabled-set
replacement armed, and the next switch that armed no replacement inherited it and wiped the enabled set.

**What is built.** `TrackManager::setActiveLoopIndex` is the authority and `applyActiveLoopIndex` (new, private)
is the **non-re-entrant commit point** that performs the write and its two side effects — Rev 21.3's split, so
the capture-switch commit point can land a write without re-entering the switch it is committing. The entry's
guard gains `hasAcceptedUnexecutedInteraction()`, the "accepted but not executed" state a switch's own latch sits
in before any job exists. A direct write now **cancels** a queued switch rather than racing it, and cancelling is
**one call**: `cancelPendingSlotSwitch(trackIndex)` owns the trio, and `stopPlayingTrack`, `handleTransportStop`,
`queueBarPlaybackStart` (which had *forgotten* the selection hold), `updateAllTracks`' three cancel branches and
`MidiButtonActions::discardQueuedSlotActions` all go through it. The two commit branches (the playback-switch
commit and the transport-stop remap) land at the commit point, because both have already decided.

**The minimal harness is part of the contract here.** `capture_storage_harness.inc` stubs this surface with a
counter (`A34aBoundary::slotMutationCalls`) that a boundary test asserts on, so the two new symbols are defined
there *through* its counted stubs: a cancel still counts exactly the three slot-mutation calls it did before, and
the commit point counts as the write `setActiveLoopIndex` used to. The link failure that revealed this (two
suites could not resolve the new symbols) is why the harness is edited rather than the suites.

**Fixture, and what the red-proofs show.** `test_s4b_5_a_direct_write_cancels_the_queued_switch_without_a_stale_
replacement` queues a **replace-enabled-set** switch through the production gesture, drives the **undo route's
own write** (plus its playback-state reset, which the fixture mirrors because without it the new active slot has
no anchored playhead and nothing can be due), then queues an **unrelated** switch through a route that arms no
replacement. Asserted: the queued switch is cancelled, the write lands in one transfer, the pool does not grow
across the cancel, the unrelated switch commits, and the enabled set survives. **Red-proved twice, restored
byte-identically:** removing the cancel-on-write fails *"a direct active-slot write must cancel the queued
switch, not race it"*; making the cancel partial (dropping the replacement clear — the pre-fix shape) fails
*"no stale enabled-set replacement may reach an unrelated switch"*. The second is the defect demonstrated rather
than described.

**Recorded as a measured coverage gap, not assumed.** Removing the guard's `hasAcceptedUnexecutedInteraction()`
term leaves the **entire** full native suite green (2,656 cases, measured, twice). The term is contract-mandated
and stays, but it has no fixture of its own yet; the reproducer — an accepted-but-unexecuted interaction beside a
direct write, which must route through the capture switch rather than move the slot — is the named next step for
this entry, and DEC-105 says the same.

**Evidence.** Focused suite **39/39**; full native **2,656 cases / 2,649 succeeded / 7 skipped / 0 failed** (and no
ERRORED suites — the harness repair above); `teensy41-capture-serial` **SUCCESS** with RAM1
`variables 96,192 / code 391,416 + padding 1,800 / locals 34,880` (floor 840; ITCM +128 B, padding 1,928 →
1,800), firmware hex `cdab16f4…`, **not flashed**.

## S4b.2 — the inbound is provisioned before the outbound retires, and the overlap is measured (2026-10-01)

**The ordering, asserted at the sequence level.** `test_s4b_2_the_inbound_role_is_provisioned_before_the_outbound_
is_released` presses an **unoccupied** inbound target while the outbound holds a working set, then checks the
order in three steps: (1) the clock lane neither commits nor allocates nor releases -- the outbound's working set
**survives the wait**, so a refused or delayed switch never leaves the track with *neither* role; (2) the
`SlotProvision` turn takes the inbound occupancy while the outbound is **still held**; and (3) only after the
commit is the retired slot releasable, and the release touches nothing on the inbound. **Red-proved, restored
byte-identically:** `provisionSlotForSwitch` mutated to release the outbound first (the ordering this stage
forbids) fails it at *"the outbound must still be held while the inbound is provisioned"*.

**What "the inbound role" means, per path -- a finding the fixture forced.** The outbound's working set is
observed through its lease, but a **playback** switch's inbound has **occupancy and no lease**: the production
provisioner runs with `prepareCapture = false`, and the lease is a capture admission's (S3.1). Every earlier
fixture that asserted an inbound lease had got one because the *test* prewarmed with `prepareCapture = true` --
so the assumption would have been inherited silently. The fixture asserts the occupancy instead, and DEC-106
records the lease question (and its `WorkingSetRole` bits) for the architect rather than inventing it at the
last mile.

**The overlap's bound, measured rather than assumed.** The obvious unit -- the outbound's own prewarm cost --
is the wrong one: it carries the capture horizon (14 event-pool chunks) while the switch turn dropped the pool
by **1**, and a probe slot provisioned the same way as the inbound dropped it by **0**. So part of that 1 chunk
is the turn's other work (merged-window build / prepared-pair publication), not the occupancy alone. The fixture
asserts the bound it can support (`inboundCost <= outboundCost`; the probe's cost no larger) and records the raw
numbers, leaving the chunk split to S2's budget accounting.

**Cited, not duplicated.** "The switch point performs no allocation" is the S4b.6/S4b.7 fixtures' subject (the
MIDI edge and the clock path both assert it), and "must not steal from a still-live capture or
deferred-finalization owner" is S4b.1's two "nothing stolen" fixtures (the outgoing finalizer, its lease and the
successor's lease all survive the gesture). This record says so instead of re-asserting them here.

**Evidence.** Focused suite **41/41**; full native **2,658 cases / 2,651 succeeded / 7 skipped / 0 failed**;
`teensy41-capture-serial` **SUCCESS** with the RAM1 line unchanged (`code 391,416 + padding 1,800`) and a
**byte-identical** firmware hex (`cdab16f4…`) -- this slice changes no firmware.

## S4b.3 — the boundary the switch lands on, and the cold half of it (2026-10-01)

**What "land the switch on a MIDI boundary" means in this firmware.** The commit is gated by the request's own
quantization: `SlotQuantization::LoopEnd` (the playback-switch gesture's) commits where the outgoing loop's
playhead wraps, `NextGrid` (the record carrier's) commits on the start grid. Both are now measured on the
**tick that actually moved the active slot**, not asserted by name: the fixture queues each switch *off* the
boundary (`loopStart + 211`), drives the production lane until the active slot changes, and requires that the
committing tick is not the queue tick, that the `LoopEnd` tick is where the outgoing loop's playhead wrapped
(phase zero, or below the previous tick's — read modulo the loop length, since the effective tick lives in the
projection domain), and that the `NextGrid` tick is a multiple of the start grid.

**Why "cold" is a property of the *owner*, not of the boundary.** S4b.2/S4b.6/S4b.7 moved provisioning off the
boundary entirely: the inbound role is taken by the `SlotProvision` turn, and the boundary only binds a role
that already exists. The fixture measures that at the tick itself — `freeChunkCount()` is identical immediately
before and after the committing `updateAllTracks` — so "cold admission boundary by construction" is evidenced as
*two* things the boundary does (land on the quantization; bind a prepared role) rather than one thing it avoids.

**Red-proved twice, each restored byte-identically.** Making `shouldCommitPendingSlotSwitch`'s `LoopEnd` case
ignore the boundary fails the `LoopEnd` fixture at *"a LoopEnd switch must land where the outgoing loop's
playhead wrapped"* — and, as a control that the change really was load-bearing, S4b.1's lane fixture at *"the
queued switch must not commit while the predecessor finalizes"*. Making the commit branch allocate at the
boundary fails **both** new fixtures at *"the boundary tick itself may not take or return pool capacity"*
(measured: 8 chunks taken on the committing tick).

**Evidence.** Focused suite **43/43**; full native **2,660 cases / 2,653 succeeded / 7 skipped / 0 failed**;
`teensy41-capture-serial` **SUCCESS** with the RAM1 line unchanged (`code 391,416 + padding 1,800`) and a
**byte-identical** firmware hex (`cdab16f4…`) — this slice changes no firmware. **Not claimed:** the fixtures
prove the boundary's quantization for these two quantizations on a one-bar loop; a device leg would be what
shows the same on hardware timing (S4b.8/S4b.9, owed).

## S4b.4 — the undo route and the display authority on the holder that took over (2026-10-01)

**Two clauses, and both needed a measurement before they could be asserted.** S4b.4's matrix had four of its six
services already attributed to existing fixtures; these are the other two, and each taught something about how the
firmware routes them.

**Undo.** The production call is `TrackUndo::undoForLoop(track, track.getActiveLoop())` (the shape
`MidiButtonActions` uses), and it gates on the slot. Driving it needed two link-closure symbols (the driver's
geometry flush, kept fail-closed, and its `hasPendingGeometry` question, answered no) plus two observable no-ops,
and it forced one stub to be **reclassified**: an applied undo restores published geometry and *notifies* the edit
manager through `onGlobalGeometryRestored`, a route the earlier comment assumed only a live edit session could
reach. DEC-101's argument applies (a removed `OFF_PATH()` cannot have been reached by a fixture that was passing),
and the manager's session entries stay stubbed, so no edit session can run here. **The red-proof took two tries for
an instructive reason:** `undoForLoop` checks the slot **twice** — the outer check and, inside the apply loop, the
same comparison as a `break` — so each single-gate mutation left the gate intact and "survived" (measured).
Removing the gate as a whole fails the fixture (`Expected 0 Was 4242` on the retired slot's geometry). The double
gate is worth knowing before anyone "simplifies" it: behaviour alone will not catch that edit.

**Display.** The switch commit's own call is `displayManager.invalidateForSlotChange(i, previousPlaying,
getPreviewSlotIndex(i))`, and this harness already keeps that boundary observable — so the stub now **records**
(`SlotChangeObservation{count, track, from, to}`, the harness's own observation pattern) and the fixture asserts
the pair after a **production press**: the retired slot out, the inbound holder in. The first version drove the raw
`requestSlotSwitch` and measured `(outgoing, outgoing)` — the panel asked to focus the slot it was leaving — which
is exactly why the fixture uses the gesture: the press is what moves the preview slot the commit names. Red-proved
by making the commit name the predecessor.

**Evidence.** Focused suite 45/45; full native 2,662 cases / 2,655 succeeded / 7 skipped / 0 failed;
`teensy41-capture-serial` SUCCESS with an unchanged RAM1 line and a byte-identical firmware hex (`cdab16f4…`).

## The diagnostic must not evict the evidence it explains (2026-10-01, device session)

Found while trying to run S4b.9 against a device whose library this session had filled. The leg's *precondition*
gates (the successor-start stop-completion receipt; the capture-ring integrity check) refused, and the reason was
**not** the restore work the device was doing but the **telemetry about that work**:
`PERS,work` rows were emitted once per persistence slice — 241 per slot body, **5 951 in one boot**, plus 2 705
rows from the loop-snapshot tail write's per-step row. That volume made the capture ring shed
(`RING,shed`) and once wipe (`RING,wiped`), shedding exactly the record/phase rows the acceptance surface
fences on; the resulting `SERIAL_INCOMPLETE` ended the run, and the gate then reported *"no predecessor
stop-completion receipt"* while the receipt was in the capture. Two silent behaviours hid the chain: the tail
emitter's progress signature includes the write offset (so `sigChanged` made its 512-step interval irrelevant),
and the gate's abort branch returned without naming the abort.

The rule this records: **diagnostics are sheddable; evidence is not.** A telemetry emitter whose rate scales with
the work it instruments can evict the evidence it exists to explain, so its cadence must be bounded, must keep the
phases an acceptance surface requires, and must carry the running maximum rather than a sample (a bound check over
sampled rows would silently weaken). The fix is `Utils/TelemetryCadence.h` plus its three call sites, with the
counters in OCRAM so DTCM (and the RAM1 budget) is untouched. Record:
[`evidence/2026-10-01-telemetry-must-not-evict-evidence/FIX.md`](evidence/2026-10-01-telemetry-must-not-evict-evidence/FIX.md).
