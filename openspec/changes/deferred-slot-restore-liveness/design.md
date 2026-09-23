## Context

See `proposal.md` — Why. This section records only the current state and the constraints that shape
the approach. All code claims below were read from the tree on 2026-09-23 (`feature/runtime-working-set-ownership`,
clean, HEAD `22ae260c`); device claims are cited to the sessions named in
`docs/Runtime/PROJECT_STATE.md` and `openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`
§ Device session — RWSO-24CAP.

**Owners and call paths that matter.**

- `StorageManager` owns the restore queue, the load job, the deferred save, and the persistence drain.
  No ownership moves in this change.
- `LoopSlotRestoreQueue.cpp` — `pendingLoopSlotRestores_` (capacity `NUM_TRACKS * MAX_LOOPS_PER_TRACK`),
  `loopSlotRestoreAttempted_`, `loopSlotPayloadOnSdInRam_`; helpers `appendBootLoopSlotRestore`,
  `queueDeferredLoopSlotRestore`, `popNextDeferredLoopSlotRestore`, `popFocusDeferredLoopSlotRestore`,
  `peekFirstPendingLoopSlotRestore`, `reprioritizeDeferredLoopSlotRestoreEntries`.
- `LoadLoopJob.cpp` — `beginLoadLoopJob`, `ensureActiveLoadLoopJobSelected`, `stepLoadLoopJob`,
  `stepSubmittedLoadJobsImpl`, `parkActiveLoadLoopJob`, `canRunBackgroundLoadLoopNow`,
  `deferredSaveBlockedByActiveSlotLoadSd`, `deferredSaveBlockedByPostLoadCommitHoldoff`.
- `main.cpp` — the boot drain frame: `setBootTitleLoadDrain(bootSlotLoadRefreshPending)` at entry;
  on `bootInteractiveReady()` it clears the flag, calls `enqueueRemainingLoopSlotRestoresFromSd()`,
  and from then on calls `enqueueRemainingLoopSlotRestoresFromSd()` every turn.
- `CurrentSetBootLoad.cpp` — the boot scan builds the boot-playback queue;
  `loadCurrentSetBundleAndActiveLoopSlots` clears the queue, clears the load job and resets
  `loopSlotRestoreAttempted_` — the **only** reset of that array.
- `DeferredSaveScheduler.cpp` / `SyncDrainSaveState.cpp` — the deferred-save gate and the blocking
  drain's gate classification.

**Constraints.**

- `docs/Authority/ARCHITECTURE_RULES.md`: `StorageManager` keeps persistence and load ownership; no new
  `*Manager`; no allocation on the action edge; no SD write on the stop hot path; deferred save via the
  existing admission path, never a full validate on stop.
- `docs/Authority/PROJECT_INTENT.md` §1 and DEC-081: the music never stops. A playing track is never
  stopped or held up for storage work, and audible playback is not a persistence gate.
- `scripts/ram1_budget.json` + `scripts/check_ram1_budget.py` enforce the post-link RAM1 floor
  (`padding 840` in the recorded line). **Any new ITCM code or state is a bank-cross risk**, and
  `code + padding` is already at the FlexRAM boundary — the placement rule for this change is
  FLASH-resident helpers (`STORAGE_PERSIST_MEM` / `FLASHMEM`) and externally allocated state.
- `lazy-slot-hydration` is the normative owner of hydration/deferred-load behavior; its current text
  *permits* the stall this change removes, so the delta modifies it rather than adding a parallel spec.
- HITL: a behavior observable over serial needs a registered or reused stimulus
  (`scripts/hitl/stimuli/`); canonical stimuli are never edited to chase a new condition. The existing
  temporary RWSO stimuli and the canonical base preset are the starting points.

**Read-only root-cause findings (evidence for TBD-1).** These are why the defect is treated as
liveness, with the queue as the spine:

| # | Mechanism (read from code) | Observed symptom |
|---|---|---|
| C1 | `canRunBackgroundLoadLoopNow()` is false while `sequencerRunning` or any track records or is audibly playing; `ensureActiveLoadLoopJobSelected` then starts nothing | `Queuing boot playback loop slot restore 6 pending; first 5/0` and `first 4/0` with `DIAG,lcr,skip,restore`; no `LoadLoopJob done` for those entries |
| C2 | `stepSubmittedLoadJobsImpl` parks a non-focus job when `!backgroundAllowed`, and only ever steps again when the same predicate allows | a job can begin while stopped and be parked for the rest of a playing session |
| C3 | `enqueueRemainingLoopSlotRestores()` returns immediately once `bootBackgroundFillAdmissionComplete_` is set; nothing re-arms an entry that was never processed | a payload discovered at boot is never offered again in that power cycle |
| C4 | `popNextDeferredLoopSlotRestore` takes the priority head and has no track/slot filter, so a head that cannot start blocks every entry behind it | one owned slot can stall the whole background fill |
| C5 | `deferredSaveBlockedByActiveSlotLoadSd()` is true while a `LoadLoopJob` exists (`SlotLoadSession` is created only by `beginLoadLoopJob`), and `SyncDrainSaveState` reports `BlockedBySlotLoad` only after the full stuck budget | `Persistence drain stuck after 33 iterations` → `Clear aborted: could not complete deferred save first` |
| C6 | `markLoopSlotRestoreAttempted` runs on every failure path (missing file, bad token, open/seek/read failure, parse failure), and `loopSlotRestoreAttempted_` is reset only by a full boot load | a transient failure latches that `(track, slot)` `EMPTY` for the power cycle; a later boot restores it, so boots disagree |

C6 is the best explanation for the census changing between boots. It is labelled an
**evidence-backed hypothesis**, not established fact: no log in hand captures the failure path that
set the flag. Separating C1–C6 is Slice A's job.

## Goals / Non-Goals

**Goals:**

- Make a queued restore a bounded liveness obligation with exactly one terminal outcome per request,
  observable without a bisect.
- Remove the specific stalls C3–C6 above, in evidence order, with a discriminating fixture per stall.
- Reconcile the restore obligation with the persistence drain so CLEAR cannot spin a stuck budget on a
  wait that cannot progress.
- Keep `StorageManager` ownership, the RAM1 budget, and MIDI-path timing unchanged.
- Leave `runtime-working-set-ownership`'s contract untouched; this change only removes its blocker.

**Non-Goals:**

- No new manager, queue framework, or persisted format; no change to the SD catalog write path.
- No change to boot-playback-first commit, the hydration lifecycle, or the priority *order*.
- No attempt to make restore synchronous, or to restore every SD payload before interactive ready.
- No re-work of `runtime-working-set-ownership` device legs inside this change; they stay with that
  change and are re-run after this one lands.
- Not deciding TBD-2/TBD-3 from the proposal here; they are recorded in Open Questions as deferrable.

## Decisions

### D1 — The unit of correctness is the restore **request**, not the job

The queue entry becomes the tracked request with one of three terminals: `committed`, `unnecessary`
(slot already has committed content, or the payload is gone/unverifiable), `refused` (named reason).
The load job is an implementation detail of servicing a request.

*Why:* C3/C4/C6 are all "the request left the queue without an outcome the user can see". A job-centric
fix would keep the same blind spot at the queue layer.

*Alternative considered:* track only the job and re-drive it. Rejected — the job exists only after a
request is popped, so it cannot describe the queued-but-never-started state (the observed one).

*State placement:* compact per-slot terminal record (`terminal` + `reason` per `(track, slot)`), stored
outside ITCM next to the existing restore arrays. No dynamic allocation; no growth in the per-turn cost.

### D2 — Background restore work is admitted a bounded slice by reason, not by transport idleness

Add a background-restore invocation reason with its own bounded per-turn budget, serviced in the
existing deferred frame. Background work on any slot in the background fill SHALL be able to advance
while playback continues; recording keeps its existing deferral (see Open Question O1).

*Why:* C1/C2 are the primary starvation. The device runs the canonical base preset with transport
started, so "wait for idle" can mean "never". DEC-081 already established that audible playback must
not gate storage work; this extends the same ruling from the save side to the restore side.

*Alternative considered:* raise restore to the focus/High class. Rejected — that would make every
background slot compete with the focused slot and the MIDI path, which is what the Low class exists to
prevent.

*Bound:* the slice budget is a declared constant analogous to the mid-pass one-chunk flush; the MIDI
service budget and the `overflow_free` gate are standing acceptance conditions.

### D3 — Queue service skips a non-startable head, preserving the blocked entry

Add a startability-aware selection that walks the priority order and starts the first request that may
begin, leaving a blocked entry queued with its reason. The existing pop helpers keep their
done-test semantics and the S2b.5 `queuedRestoreMayBegin` liveness predicate stays the gate.

*Why:* C4. `popNextDeferredLoopSlotRestore` has no filter and the two `popNext` call sites cannot see
past the head; one live-owned slot stalls everything behind it.

*Alternative considered:* remove the done-test from the pop and re-queue. Rejected — re-queueing is
inert today (`queueDeferredLoopSlotRestore` refuses unless `needsSlotLoad`, which is false once
attempted), which is exactly the S2b.5 trap.

### D4 — The drain's slot-load wait is reconciled, not merely classified

The blocking drain SHALL be able to observe that the restore it waits on is progressing and that it
can terminate it, instead of counting identical no-progress iterations. The existing
`BlockedBySlotLoad` classification stays and gains a progress term.

*Why:* C5. The 2026-09-23 classifier named the gate but the gate was a state the caller could not
leave; CLEAR still aborted.

*Alternative considered:* have the drain cancel or supersede the in-flight restore. Rejected as the
default because it changes persistence ownership/durability semantics and needs its own `DEC`
(proposal TBD-3). The preferred path is to make the restore progress so cancellation is unnecessary;
if Slice A/B evidence shows it is necessary, that returns as a decision, not a patch.

### D5 — A transient restore failure is retryable within the session; the latch is not the fix

Separate "we tried and it failed" from "there is nothing to try". A failed restore is retryable up to a
declared bounded attempt count per power cycle, then reaches the `refused` terminal with its reason;
`unnecessary` is reserved for genuinely absent/invalid payloads.

*Why:* C6. A single transient SD failure currently latches the slot for the whole power cycle and makes
boots disagree.

*Alternative considered:* leave the latch and re-probe more aggressively at boot. Rejected — the latch
is also what stops a permanent retry loop on a genuinely bad file; a bounded attempt count addresses
both.

### D6 — Observability is capture-tier and event-shaped

Emit one capture line per restore terminal (with `(track, slot)` and reason), plus an on-demand
queue/state probe reusing the existing `probeActiveLoadLoopJob` shape. Boot reconcile reports a named
outcome (`ok` / `repaired` / `failed`) instead of only the current warning.

*Why:* every device round in this area has been blocked by having to infer state from absence of
lines. Event-shaped lines are near-zero cost when idle, which satisfies the spec's hot-path clause.

*Alternative considered:* a per-turn state line. Rejected — the `DIAG,stop_grain` history in
`CURRENT_WORK.md` shows per-turn families overflow the capture ring and evict the rows the run is read
for.

## G2 execution packets (one per slice)

Slices are ordered so every behavior edit is preceded by a discriminating fixture, per DEC-080 and
`docs/Authority/IMPLEMENTATION_READINESS.md`. Each slice is one buildable commit boundary and carries
its own validation. Numeric task ids live in `tasks.md`; the packet ids here are the review sections.

### Packet A — characterize and observe (no behavior change)

- **Dependency closure:** `test/test_storage_manager_load` (already links `LoadLoopJob.cpp`,
  `LoopSlotRestoreQueue.cpp`, the in-memory SD card and `test_support/track_harness.inc`), plus the
  `SESSION_CAPTURE` restore-terminal lines and the queue probe.
- **Transition oracle:** for each of C1–C6, a fixture that drives the **production** entry
  (`StorageManager::selectSubmittedLoadJobs` / `stepSubmittedLoadJobs`,
  `enqueueRemainingLoopSlotRestoresFromSd`, the drain) and asserts the current wrong state, so each
  fixture is red before its fix and green after. No copied lifecycle, no success-returning stub.
- **Controllable fixtures:** test-controlled transport state (already a variable in the suite),
  test-controlled `isAudiblePlaybackActive` / `isRecording` inputs, a synthetic queue with a
  deliberately non-startable head, an injected SD read failure, and a controlled
  `loopSlotRestoreAttempted_` state.
- **Outcome semantics:** each fixture names which mechanism it discriminates and which terminal it
  expects; a fixture that cannot separate two mechanisms is reported, not merged.
- **Evidence:** the red run per fixture, the full native suite, the capture-serial build, and the RAM1
  line before any behavior change.
- **Exit criteria:** C1–C6 are each either reproduced by a fixture or explicitly withdrawn with the
  observation that falsified it.

### Packet B — background restore liveness (D2, D3, D5)

- **Prerequisite:** Packet A fixtures for C1–C4 and C6 red.
- **Transition oracle:** with transport running and a track audibly playing, a queued background
  restore advances to `committed` within a bounded number of production turns; with a non-startable
  head, a lower-priority startable request begins and the head keeps its reason; a transient read
  failure retries and then reaches `refused` with that reason after the bounded attempt count.
- **Must preserve:** MIDI continuity and the per-turn service budget; recording still defers the load;
  the S2b.5 liveness guard (a finalizing predecessor still blocks a load into *its* slot only);
  exactly-once commit; no new ITCM-resident code.
- **Evidence:** green fixtures, full native, capture-serial build with the RAM1 post-link check, and
  the observed capture lines.
- **Exit criteria:** the spec requirements "Every queued slot restore reaches exactly one terminal
  state", "Restore queue progress is independent of queue-head blockage", and "Background restore is
  not starved by playback" hold under test.

### Packet C — drain reconciliation (D4)

- **Prerequisite:** Packet B green, since the drain fix depends on a restore that can progress.
- **Transition oracle:** a deferred save owed while a restore is in flight completes once the restore
  terminates, with no cancellation; a restore that cannot progress yields the named blocked-by-restore
  reason rather than a generic stuck verdict, and CLEAR does not abort on a wait that is progressing.
- **Must preserve:** DEC-081 (audible playback is not a gate), the recording gate, the mid-pass
  one-chunk flush, and the single persist grain's existing (unbounded-by-design) latency.
- **Evidence:** green fixtures plus a device run showing a CLEAR that completes with a restore in
  flight, and 0 `Clear aborted` / `drain stuck`.
- **Exit criteria:** the spec requirement "A pending slot restore does not block persistence
  indefinitely" holds; the device gate is taken.

### Packet D — boot reconcile outcome (D6)

- **Prerequisite:** Packet A's observability.
- **Transition oracle:** a malformed index produces the named failure outcome plus the existing
  self-healing repair; a healthy index produces `ok`; the two are distinguishable in the capture.
- **Must preserve:** `reconcileSetIndexOnSd`'s self-healing behavior and the SD temp-write fix.
- **Evidence:** a native fixture over the existing `test_storage_manager_load` / storage suites, the
  capture-serial build, and a device boot showing the outcome line.
- **Exit criteria:** the spec requirement "The boot catalog reconcile outcome is reported" holds.

## Risks / Trade-offs

- **[RAM1 bank cross] The observable/terminal state and the new admission reason add ITCM code and
  `.bss`.** → Every new function is `STORAGE_PERSIST_MEM`/`FLASHMEM`; terminal state lives beside the
  existing restore arrays outside ITCM; the post-link `check_ram1_budget.py` gate is run on the
  capture-serial build in every slice and the floor is never auto-updated.
- **[MIDI starvation] Admitting background restore while playing consumes SD and CPU.** → One
  declared bounded slice per turn, serviced after MIDI service, with the existing `overflow_free`
  gate and the standing MIDI-gap acceptance; the slice is tuned by measurement, not guesswork.
- **[Durability] A save that waits on a restore can delay a CLEAR the user is pressing.** → The drain
  completes as soon as the restore terminates, and the fix aims to make that bounded; if cancellation
  proves necessary it returns as a `DEC`, not as a silent change.
- **[Scope creep into runtime-working-set-ownership] The device legs become re-runnable here.** →
  Non-goal: this change is judged only by its own fixtures plus one CLEAR/restore device run.
- **[C6 fix causing retry storms] A permanently bad payload retried every turn.** → Bounded attempt
  count per power cycle with a named terminal.
- **[False root cause] If Slice A falsifies the C3–C6 reading, the follow-on packets are built on sand.** →
  Packet A is explicitly allowed to withdraw a mechanism and re-target; Packet B/C tasks are only
  checkable once their listed Packet A fixtures are red.
- **[HITL regression risk] The change touches a path with zero native coverage before this change.** →
  Building the native presence *is* Packet A's first task, and no behavior edit is allowed before its
  fixture is red.

## Migration Plan

No data migration and no format change. Behavior changes ship behind their fixtures inside this
change; rollback is reverting the slice commit, because the restore/liveness state is per-boot and
nothing about it is persisted. The only persisted interaction is the existing save path, which this
change does not modify.

## Open Questions

- **O1 (deferrable, TBD-2 restated).** Whether a background restore may also advance while a track is
  *recording*, or keeps deferring. Deferrable because the observed defect is the *playing* case, and
  the recording deferral is separately justified by SD-session ownership; resolving it later changes a
  bound, not the spec or the packet structure.
- **O2 (deferrable).** The exact bounded slice size and attempt count. These are measurements from
  Packet B/C, deliberately not pre-decided.
- **O3 (deferrable).** Whether the terminal record needs per-attempt history or only the last terminal.
  Start with the last terminal; expand only if a device diagnosis needs it.
