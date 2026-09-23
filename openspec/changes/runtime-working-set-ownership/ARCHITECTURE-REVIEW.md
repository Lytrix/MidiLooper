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
2. **The useful knowledge survives.** For this harness: a slot's content must be appended *before* the arm is
   prewarmed, or through the staged ingress. That is the missing piece for any future dense-content fixture,
   including S1.5's third clause, and it is why the earlier sweep's helper failed at its first commit.

### Where S1.5's third clause stands

Unchanged in substance and now free of a wrong intermediate claim: it needs a **staging-correct** dense-loop
construction (append through the staged ingress, or append before prewarming), and only then can the refusal
point be measured — or it is answered on device, where a loaded library is read from SD rather than sealed.

### Verification

| check | result |
|---|---|
| `test_continuous_capture` | **91/91** (fixture and helper reverted) |
| full native | **2,281 cases / 1 skipped / 2,280 succeeded / 0 failures** |

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
| `free_chunks` after boot | **400** of 512 | **480** of 512 |

The discriminating lines are explicit. Baseline sequence B (lines 44, 50 of its log) resolves `track=1,slot=2`
and `track=4,slot=1` at `ready=1,credits=8` and drops 8 chunks each; HEAD sequence B (lines 118–132) resolves
the **same two slots** at `ready=0,credits=0` and drops **nothing** — every `free_chunks=480`. Both revisions
restored the same 2 committed slots, so this is a like-for-like comparison of the term the change removes:
**8 chunks per committed slot → 0**, with boot's own enabled-set cost falling 12 → 4 per track at the same time.

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

- **S1.5 device leg:** the per-committed-slot figure (8 chunks, baseline) and `free_chunks` after boot (400 of
  512) are **recorded**; the "smallest N refusing a 24-bar overdub" clause is **open**, and the native attempt
  did not reach an admission probe.
- **S1.1:** partially answerable now (tags 1–7, size classes, slab census); tags 8–16 and `VisualCache`/lease
  fields need the instrumentation increment named above. Fix the record-density setup first, or the census
  understates a real working set (`notes=4` against a `min 31` gate).
- **S1.3:** not started. It needs a *grown* library, which means committing more slots on device — that writes
  loop content, so it needs the owner's word on which slots are scratch.
- **S2b.3/S2b gates:** the HEAD image is built and hashed but **not yet flashed**; the device half of the
  zero-reservation comparison is therefore not taken.
