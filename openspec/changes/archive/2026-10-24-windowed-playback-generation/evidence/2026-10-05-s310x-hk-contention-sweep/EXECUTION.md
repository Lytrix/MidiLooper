# Task 3.10 / 6.3 — the `H x K` contention sweep

Execution packet, written **before** the fixture was implemented. Authority:
[IMPLEMENTATION_READINESS.md](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice),
[`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md).
Scope chosen by the owner this session: **the `H x K` contention sweep — measure the declared envelope** (test-only; no
device, no production behaviour change, no RAM1 re-base).

## Identity and scope

- **Slice:** windowed-playback-generation task 3.10 (fair, bounded, deadline-ordered service), the envelope half its own
  records leave open ("the `H x K` sweep"), plus task 6.3's *contention* axis and task 5.4's honest-envelope obligation.
  Contract: **DEC-108** §13b (`Delta-max x H x bounded per-track work`). Reviews that named this remainder:
  increment 25 (`evidence/2026-10-04-s310t-declared-envelope/EXECUTION.md`, "Owed: … the `H x K` sweep + device leg") and
  increment 9 (`evidence/2026-10-02-s310i-service-gap-unit/EVIDENCE.md`, "`grains x (denied + 1)` is therefore **not yet a
  complete bound** … the `H x K` contention inside the lane").
- **Status:** specified here; fixture implemented next; native verification to be recorded in `EVIDENCE.md`.
- **Owner and production entry → deferred worker:** `TrackPlaybackWindowBuild.cpp` —
  `Track::playbackWindowServiceOffer` (per-track, most urgent holder) → `MidiServiceDrain::selectPlaybackHolderRoundRobin`
  (one track per turn) → `Track::stepPlaybackViewMaintenance` (one grain, within-track EDF) →
  `Track::serviceAdmittedDeadlineMisses` (the promise's own deadline). `src/main.cpp`'s
  `runDeferredIdleMaintenance(PlaybackViewMaintenance)` composes exactly these, so the fixture composes the same symbols
  rather than copying a state machine.
- **Primary files:** a new header-only fixture include, `test/test_support/window_contention_sweep.inc` (the cell
  helpers plus the three cases), and two lines in
  `test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp` (the include and the `RUN_TEST`
  registrations). No production line is touched, so the sweep cannot move a declared constant, a capacity or a gate.
- **Preconditions already present:** the one-grain-per-turn rule, the across-track rotation
  (`maximumConsecutiveDeniedTurns = Config::NUM_TRACKS - 1`), the within-track EDF (`selectPlaybackHolder`), the per-track
  outstanding-work charge (`playbackViewOutstandingGrains` + `playbackViewAdmissionKeepsAdmittedDeadlines`), the derived
  cost (`CoverageServiceContract::deriveGrainsFrom`), and the declared `Delta-max`
  (`Track::kPlaybackViewPermittedAdvancePerTurn = 64`). The sweep consumes them; it adds none.
- **Explicit non-goals:** no production requester, no activation of `sInterferenceBudgetEnforced`, no gate flip, no
  capacity or constant change, no device leg, no claim that the derived admission is enabled, and no claim about the
  *rolling* selector (the sweep drives the lane's service composition while the transport is not playing, which is the
  state the envelope is about).
- **Commit boundary:** test-only and independently buildable/relinkable; no compatibility shim.

## Oracle fixed before behavior edits

The declaration is a **tick-denominated promise per window**: at admission,
`admittedWork.deadlineTick = admittedTick + handoffLeadTicks(phase, interval.startPhase, loopLength)`, and the admission
only admits when `canCompleteWithinLead(available, additionalGrains)` holds, i.e. when
`(ownGrains + chargedOthers) x (denied + 1) <= (available - leadTicks) / Delta-max`. The sweep therefore drives the same
composition in the declaration's **own currency**: one lane turn = one permitted advance of `Delta-max` ticks.

| Case | Initial state | Ordered actions | Expected observation |
|---|---|---|---|
| **S1** inside-envelope grid | `H` tracks, each of `1 + K` slots holding a current root published in bars 12–13 with `notes` notes; every holder requested at **one shared stamp** | lane turns: every track's `serviceAdmittedDeadlineMisses` + `playbackWindowServiceOffer`, then `selectPlaybackHolderRoundRobin`, then `stepPlaybackViewMaintenance`; clock advances `advance` ticks per turn | every holder completes; each window's completion tick `<= admittedTick + (own+others) x H x Delta-max`; the target's measured consumption equals the composition's own prediction |
| **S2** the K half is load-bearing | as S1 with `K >= 1` | same | the target's measured consumption **exceeds** its own-grains-only charge `ownGrains x H x Delta-max`, so charging siblings as work is necessary rather than conservative |
| **S3** the H half is load-bearing | as S1 with `H = 8` (the declared rotation) | same | measured consumption **exceeds** the same shape's charge at `H = 4`: a smaller across-track factor would under-charge; and completion turns grow monotonically with `H` across the swept values |
| **S4** density and span | as S1 at `notes in {5, 20, 80}` and `loopBars in {16, 40}` | same | the measured consumption per charged grain is stable across density, and unchanged between the two loop lengths: the loop's length is not an input to the composition |
| **S5** the boundary, predicted then measured | one shape just inside the declared charge and one just outside it (larger `K`), admission unenforced | same | inside: completed, no `one_pass_late`; outside: completion passes the promised deadline and the **named** `one_pass_late` row appears -- the declaration predicts where the boundary is |
| **S6** falsification control | an in-envelope shape (S1) | same, but driven at **2 x** the permitted advance | the promise breaks and `one_pass_late` is named: the oracle can fail, and the unit (`Delta-max` per turn) is the load-bearing one |

- **Counterexample that must fail the fixture:** S6 (over-advance) breaks the promise; S2/S3's inequalities are measured on
  the composition, so a run that recomputes them from the declaration alone would pass vacuously -- they are read from
  `activeViewBuildWork()`/`playbackViewOutstandingGrains` and from the driven clock.
- **Preserved invariants:** nothing is published, adopted or re-positioned by the sweep; no deadline is re-granted; the
  fixture never stamps a request backwards (one shared stamp per cell); every expectation is read back from a production
  surface.

## Controls and coverage

- **Reused seams:** the production symbols above; the suite's existing `TrackCaptureStopTestAccess::runtimeIfAllocated`,
  `commitRecordPassIntoTrackSlotOfLength`, `publishNotesInBarsRootForSlot`, `prewarmPlaybackForSlot`,
  `windowHolderAdvanced`, and the `SC_PLAYBACK_BUILD_ROW` recorder (`PlaybackPathRows::countOf`) for the named rows.
- **A new test-only helper is needed and minimal:** the per-slot request source the build borrows across turns is one
  buffer per **(track, slot)**; the suite's `windowSourceScratch` is indexed by slot only, so it cannot carry `H` tracks
  at once. The helper adds a `[track][slot]` buffer array for this fixture alone, and a fresh-world reset for a cell
  (`trackManager.~TrackManager(); LoopEventStore::resetPoolForTests(); new (&trackManager) TrackManager();` -- the same
  triple `setUp()` performs) so several cells can be swept in one fixture without one cell's pool occupancy leaking.
- **Production path exercised:** offer → rotation → one-grain turn → the admitted promise's deadline judgment. No stub,
  no mock, no fabricated publication, no test-only timing budget: the only invented input is the *advance per turn*,
  which is the declared field's own unit and is taken from the declaration.
- **Busy/stale/failure cases relevant here:** the boundary cell (S5) and the over-advance control (S6). Recovery,
  preview, undo and paging belong to their own slices and are **not** claimed here.
- **Numeric limits:** every limit is read from the contract (`Delta-max`, the denial allowance, `leadTicks`, the derived
  grain counts) or from the retained work records; the only pinned literals are *measured* lane-turn counts, which is what
  makes the fixture sensitive to a mutation of the measurement.
- **Bounded drain:** a hard cap per cell (measured cost plus margin) that fails the case when exceeded, so a stalled
  composition is a failure rather than a hang.

## Verification recipe

- Focused: `pio test -e native -f test_runtime_working_set_ownership` (expected: all cases pass, count recorded).
- Broad: `pio test -e native`.
- Build: `pio run -e teensy41-capture-serial` (expected SUCCESS with the approved RAM1 line unchanged; test-only slice).
- Also: `git diff --check`, `openspec validate windowed-playback-generation --strict`.
- Device: **NOT RUN** — this slice makes no device claim. The device leg remains 6.5's, separately authorized.
- Evidence path: this directory (`EXECUTION.md`, `EVIDENCE.md`, `hk-contention-verification.json`).

## Completion record

- [x] Oracle/contract unchanged (no production line edited) — recorded in `EVIDENCE.md` (pre/post hashes equal)
- [x] Fixture implemented and discriminating assertions demonstrated (MC1–MC4, byte-identical restores)
- [x] Native verification: focused **135/135**; full **2789 / 2782 succeeded / 7 skipped / 0 failed**
- [x] Build/RAM1 verification: `teensy41-capture-serial` **SUCCESS**, `variables 96,192` / `padding 1,640` (no re-base)
- [x] Device verification: NOT RUN (not claimed)
- [x] Docs: runtime state + this change's task records
- [x] Task IDs eligible for checkoff: none on its own (3.10 and 6.3 stay open; this supplies the contention axis)
