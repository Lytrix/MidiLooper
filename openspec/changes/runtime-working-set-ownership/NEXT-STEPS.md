# Continuation after recovery — 2026-09-26

## Current result and deferred display fault

Recovery/undo implementation and evidence are committed in `f29539e3`. The device
completed automatic metadata repair and subsequent boot with restore admission
complete, undo hydrated and transport stopped. The host copy passed 64 slot
roundtrips, 122-action reload and 276 repair-interruption cases. Hardware content
parity and actual Undo/Redo gestures remain unverified; ready counters do not close
those tests. The recovery incident hold was deliberately cleared for the approved
installation; the standalone reader is no longer the installed firmware.

The user has explicitly parked the DMA display fault **until after this entire
`runtime-working-set-ownership` OpenSpec**, not merely until the next slice.
Do not investigate or repair DMA as part of the current implementation queue.
DMA counters advance but the panel held its first piano-roll frame; changing only
`MIDILOOPER_DISPLAY_DMA=0` yielded a visibly updating screen, confirmed by the user.
That localizes the failing route, not its exact cache/SPI/driver cause.

The installed image uses blocking SPI. The repository default remains DMA-enabled;
an ordinary default build/upload would re-enable the fault. The exact comparator
configuration, identity, build/upload logs and selected device observations are
preserved in `evidence/2026-09-26-workspace-loss/display-dma-deferred/`. To reproduce
the workaround build, from the repository root use:

```
pio run -c openspec/changes/runtime-working-set-ownership/evidence/2026-09-26-workspace-loss/display-dma-deferred/platformio.ini -e teensy41-capture-serial
```

No upload is implicit. This profile preserves the capture-serial flags and changes
only the existing display switch plus isolated build directory. Blocking SPI is not
a realtime acceptance result. Applicable visual/MIDI timing requirements stay open;
record the transport in every result and do not equate its timings with the earlier
DMA baseline. Deferral does not waive any stage gate.

## S4.1 — persistence identity during handoff: DONE (native verified, 2026-09-26)

Closed as verified with **no firmware change**; the recommended slice below is superseded.
`DeferredSaveJobStages`' footer read is **not** a defect: the footer records the workspace focus at
**footer-write time** (the contract), while the interval's slot stays the **content** identity (its body is
a `LoopPersist` item keyed by `(track, slot)`, cleared only by its own token-verified write). The
alternative — substituting the interval's slot, or capturing the whole save at admission — is rejected:
the first red-proves as a reverted focus, the second would make the image internally inconsistent with the
live-per-stage bodies beside it. Recorded as **DEC-090**; packet and record in
[ARCHITECTURE-REVIEW.md](ARCHITECTURE-REVIEW.md) § "S4.1 — persistence identity during handoff".

**What was built:** four cases in `test_storage_drain_restore` — the real deferred worker paused with the
footer due and unwritten, an A→B handoff through `TrackManager::setSelectedSlotIndex`, then a reload with
content, loop identities and the **restored focus** asserted per slot; a no-switch control (restores A); a
handoff *after* the footer was written (the selection route's requeue lands it); and an SD write fault at
the footer (nothing published; the retry lands the handoff). **Red-proved** by pinning the footer's reads to
the interval's slot (three handoff cases fail on the restored focus, the control passes); the first attempt
was a false green and is recorded. Full native 2,465 passed / 7 skipped; capture-serial SUCCESS with
`padding 856` (floor 840) on an untouched `src/`.

**Found while building the fixture, deliberately not fixed here (needs its own packet):** a persistently
failing `FinalizeWorkspace` step retries **at the queue head** and blocks everything behind it. The failure
branch clears `pending` and requeues the item, and every retry re-enters `CurrentSetCompletion` — the shape
observed was `queueDepth=2`, `stage=CurrentSetCompletion`, `runtime.bundle.bin` size 0, still retrying after
4,096 turns. On device that is an SD error path, so the question is a bounded retry/failure policy
(drop-and-report vs. keep retrying), not a local fix. Host fixtures must drop the work queue rather than
drain it (`PersistenceWorkQueue::resetForTests()`), which is recorded in the fixture comment.

## S4.1 — working-set lease release on a same-track switch: DONE (native verified, 2026-09-26)

Closed with **no firmware change**, like the persistence slice before it. Three fixtures in
`test_runtime_working_set_ownership` (23/23) drive the production switch commit with a live owner latched on
the **incoming** slot (the retired slot is released, its generation advances and its pre-switch identity is
consumed, while the owned sibling is untouched), the refusal under a live finalization (`noLiveOwnerForSlot`
false, `releaseSlot` → `RefusedBusy`, lease and generation unchanged, the switch still committing), and the
release **after** retirement through the next same-track switch. Red-proved in three stages — predicate,
predicate + `reset()` refusal, then also the track-level early return — which showed the protection is
**triple-layered** and attributable layer by layer. Full native 2,468 passed / 7 skipped; capture-serial
SUCCESS with `padding 856`; `src/` untouched.

**Two corrections this slice recorded.** (1) The route that releases a retired slot is **not** the
retirement: `TrackPreparedStopCommit::completeDeferredCleanup` clears `captureStopOwner` and arms the
*capacity* marker but releases no working set, so the release happens on the **next playback reset** (a
switch), or on the memory-pressure/undo lanes. (2) The earlier "needs an emission that names the lease
release" was **stale**: `DIAG,ws_release,<slot>,<generation>,<outcome>` already exists and is Tier-A
(`bbedfeac`). S4.1's device leg therefore owes only a **run** in which that path fires for the slot under
test, plus an oracle that reads the row — no new instrumentation. S4.1 is checked with that recorded as an
evidence obligation, matching how S4.3 records its device byte delta.

## F4 / DEC-091 — `FinalizeWorkspace` retry policy: DONE (implemented, native verified, 2026-09-26)

Addressed in this session. The completion failure is now **terminal**: the item is discharged into the
queue's tombstone and the failure is reported once, instead of being re-queued at the **head** where a
deterministic step retried forever, blocked every other persistence item and kept `hasDeferredSaveWork()`
true — which refused the save-new-set pre-flight and made the read-only slot-payload observation answer
`busy` for the rest of the boot. The per-slot dirty set is left as the failure found it, which is the retry
the persistence contract names. Fixture
`test_f4_a_failed_completion_is_discharged_and_releases_the_queue` in `test_storage_drain_restore`, red-proved
by restoring the front-requeue (a **false green in the fixture's first cut** — a single-turn fault self-heals
on the retry — is recorded). RAM1 byte-identical (`code 392,360 / padding 856`); the named queue API the
first cut added cost 256 B of ITCM and was withdrawn rather than widening the floor. **Two consequences
recorded rather than hidden:** the blocking drain now reports `Failed` (not `TemporarilyBlocked/
waiting_commit_gate`) in the mixed shape where the save's completion fails *and* a restore is commit-gated —
the S2b.7 parked-instance fixture was re-premised on the drain's **ownership transfer** and pins the corrected
verdict; and the same front-requeue shape remains for **body/bundle** item failures, where the retry is the
content-durability intent (bounding those is a separate decision). **Device evidence owed:** an SD fault path
with no device fault injection.

## S4.1's device row for the lease release: DONE (2026-10-01)

Closed as **evidence**, not code, exactly as this packet asked — and the row turned out not to be rare at all.
A read-only survey of the corpus found `DIAG,ws_release` **33 times** in ordinary runs of the mandated base
preset, so the five 2026-09-27 probe attempts were chasing the wrong two of the three routes. It fires on the
**playback-reset/undo path** (`TrackUndo.cpp` → `Track::resetPlaybackStateForSlot` →
`invalidatePlaybackMergedMidiEvents` → `releaseEligiblePlaybackRuntimeSlots`): a same-track stop goes down the
*retained* route, where `invalidatePlaybackMergedMidiEvents` returns early while a deferred stop job is live, so
no sweep runs; and on a capture-switch shape the row that appears names the **incoming** slot. Observed for the
slot under test as `#CAP,3794271032,DIAG,ws_release,0,3,released` in the PASS-verdict base run
`captures/session_20260930_142350.log` (hex `577841d6…`) and reproduced three times at HEAD (`git_commit
f63d5a02`, same hex), each 1.2 ms after the device's own `Button press: Record/Overdub (double)` →
`MIDI: Undo (entries=3)` edge. The oracle is `hitl.lease_release` + `scripts/hitl_s41_lease_release_read.py`
(**13/13** tests), and `hitl.retention_probe` now takes the wire form from the same owner. **No firmware change,
and no new instrumentation or stimulus.** The campaign also found and fixed a host defect that made every base
run non-verdict-grade (`KeyError: 'DIAG,latch'` in the evidence drain; `test_managed_record_drain` **23/23**,
mutation red-proved). Recorded as **DEC-099**. Record: [`evidence/2026-10-01-s41-lease-release/EVIDENCE.md`](evidence/2026-10-01-s41-lease-release/EVIDENCE.md).

## S4.2 — the generation-mismatched release: DONE (2026-10-01)

The task's **A-releases / C-reuses / late-A-cleanup** sequence is now fixture-covered on both identities, and the
refusal is named as the device row names it. Two fixtures in `test_runtime_working_set_ownership`: A admits
through the production record-arm admission, A retires through the track-level sweep, C **reuses** the same
runtime object with its own identity, and A's late release answers `StaleGeneration` — **not** the silent
`NotHeld` an identity-free check returns for a slot that looks empty or has been re-admitted — with C's lease,
roles and generation untouched (the successor's own identity releases as the control); and the emitter's
outcome→literal mapping (`released`, `refused_busy`, `stale_generation`, `not_held`) is pinned to the vocabulary
the device reader decodes. Test-only: the rebuilt firmware hex is **byte-identical** (`577841d6…`), so RAM1 is
untouched. **Red-proved twice,** each restored byte-identically. Focused suite **30/30**; full native **2,645
cases / 2,638 succeeded / 7 skipped / 0 failed**; `teensy41-capture-serial` SUCCESS. Record:
[`ARCHITECTURE-REVIEW.md`](ARCHITECTURE-REVIEW.md) § "S4.2 — the generation-mismatched release, rejected and
named".

## Recommended next slice

**Check 7's repeated case and finding (a) are both done** (2026-10-01: `Omax` asserted as behaviour — bounded,
non-accumulating, nothing stolen, executing once capacity frees; and (a) turned out to be stale — the retention
carrier exists, so its slice is the denied-path reproducer), and the **cross-track** switch already had its
fixture. So the next native work is:

1. **DONE (2026-10-01) — the switch lane's acceptance surface.** `HARNESS_LINKS_PLAYBACK_SWITCH_LANE` links the
   playback hot path, the playback-policy owner and the transport-control owner and lifts the play branch out of
   the fail-closed stub surface (DEC-101 records the trade), and the populated-target queued switch is now
   pinned: retained while the predecessor finalizes, committing once the finalizer retires, with both failure
   modes (early switch, dead switch) red-proved. So a queued switch's **commit and its absence** are observable
   natively from here on, which is what the items below needed.
2. **PARTIAL (2026-10-01, two slices) — S4b.6 + S4b.7.** The occupancy-aware, allocation-free binding and the
   off-clock provisioning owner both landed: `slotIfAllocated` (never `trySlot`) with the commit keeping the
   switch pending when the inbound occupancy is absent, and — after the vehicle was corrected to what the binding
   revisions named (DEC-103) — `MidiServiceDrain::DeferredIdleOwner::SlotProvision` (six owners) whose body
   `TrackManager::servicePendingSlotProvision()` provisions the pending switch's target and builds its merged
   window, dispatched before the all-stopped guard so it is reachable in exactly the states a retained switch sits
   in. Eligibility is the **pending switch itself** (`slotProvisionNeeded`), so there is no request record and no
   new state. Two fixtures plus the drain suite's re-pinned owner cycle; red-proved (the occupancy term; the
   binding; the turn's provisioning — three fixtures). **DONE (2026-10-01, third slice; DEC-104) — both faces of
   the owed root cause.** The published-root fixture exists (`publishBarSpacedRootForSlot` through
   `Loop::storePublishedSnapshotForTest`), and on it a **demoted** 24-bar target goes all the way to **commit**;
   so (a) is discharged and (b) is closed too — removing the turn's `ensurePlaybackMergedEventsForSlot` now fails
   that fixture, where before all 36 stayed green. The **age/bound** landed with it (`SlotProvisionHold`,
   four bars, wrap-safe, age from the switch's own queued tick, **one report per episode**, terminal
   `slot_switch_hold_exceeded`) and is a **report, not a decision**: the demoted-without-root fixture asserts the
   switch is retained and reported once, and that a fresh episode reports again. Measured while writing it: the
   windowed gather reads the snapshot's **event** membership (a notes-only root gathers nothing) and its window is
   two bars centred on the playhead (root events must be spread). `tasks.md` S4b.6/S4b.7 carry the record.
3. **DONE (2026-10-01) — S4b.5**, one active-slot write authority (DEC-105): the guarded entry point over a split
   non-re-entrant commit point, the guard's `hasAcceptedUnexecutedInteraction()` term, the cancel-on-write, and
   `cancelPendingSlotSwitch` as the one owner of the trio (five open-coded/partial sites routed through it), with
   the named fixture red-proved twice — including the **partial-cancel defect** (a stale enabled-set replacement
   wiping the enabled set on an unrelated switch), now demonstrated rather than described. **Owed from it:** the
   guard term's own fixture — removing `hasAcceptedUnexecutedInteraction()` leaves the whole full native suite
   green (measured), so the reproducer (an accepted-but-unexecuted interaction beside a direct write, which must
   route through the capture switch rather than move) is the next thing to write.
4. **DONE (2026-10-01) — S4b.2** (DEC-106): the inbound-before-outbound ordering is asserted (the outbound
   survives the wait; the provisioning turn overlaps it; the release comes after and touches nothing on the
   inbound), red-proved by a release-early mutation; the entry's other clauses are cited from S4b.6/S4b.7 and
   S4b.1. Two findings recorded there: a **playback** switch's inbound has occupancy but **no lease** (a contract
   question for the architect), and the overlap's unit is not the outbound's capture-inclusive prewarm (1 vs 14
   vs 0 chunks measured), so the fixture asserts only the bound it can support.
5. **DONE (2026-10-01) -- S4b.3**: the boundary is measured on both quantizations the firmware uses (`LoopEnd`
   for the playback-switch gesture, `NextGrid` for the record carrier) -- the committing tick is not the queue
   tick, the `LoopEnd` tick is where the outgoing loop's playhead wrapped, the `NextGrid` tick is on the start
   grid, and the boundary tick allocates nothing (pool identical before/after). Red-proved by making `LoopEnd`
   ignore the boundary (which also trips S4b.1's lane fixture as a control) and by allocating at the boundary
   (which fails both new fixtures). Still to write: **S4b.4's** inbound-role fixture (the inbound holder serving
   record/overdub/passes/undo/redo/display with no stop and no allocation at the switch). S4b.2 now has a concrete immediate
   question: the `SlotProvision` turn is dispatched from the deferred-idle lane `main.cpp` gates, but the
   **ordering** it inherits is "provision *after* the predecessor's finalization retires" (the deferred frame
   returns early while a deferred overdub stop is outstanding, and the drain turn shares that lane). Pre-
   provisioning the inbound role *before* releasing the outbound one is what S4b.2 asks for; the turn kind makes
   it a scheduling question rather than a new owner.

S4.5 and the S4 gate close with S4f/P3, not before. Every remaining device leg (S4.2/S4.3/S4.4 device byte
deltas, phases B–E) and F4's SD-fault evidence needs a per-session device authorization.

**Device legs as of 2026-10-01 (image `cdab16f4…`, re-flashed unchanged several times, every build byte-identical).**
**S4b.8 is checked** (slot switch while the predecessor finalizes; record
[`evidence/2026-10-01-s4b8-switch-during-finalization/`](evidence/2026-10-01-s4b8-switch-during-finalization/EVIDENCE.md)).
**The S4b gate's timing half is now aimed** by a dedicated three-switch run (`overflow_free` and
`call_site_gate_ok` green, empty warning list), which also produced the S4b.7 hold terminal's **first device
observation** ([`evidence/2026-10-01-s4b-repeated-switches/`](evidence/2026-10-01-s4b-repeated-switches/EVIDENCE.md));
the gate itself stays `[~]` for its `S4 gates` half. **S4b.9 is CHECKED (2026-10-01).** The cross-track switch is accepted inside

A's finalization window, A's occupancy is released exactly once and only by A's own stop terminal, the
selection/display authority moves with the switch and the transport stays MIDI-continuous, and the leg's
*cold-boundary provisioning* clause is **observed on hardware**: the destination **track must be playing** (the
runner's `--preplay-track-number`/`--preplay-loop-slot`; after a cross-track switch the new track is selected but not
playing, so a press there is a selection/toggle). `captures/s4b9-cross-track-21` queues `Loop 3: Queued playback
switch` against a **cold** destination (`capture_prewarm,full=0,ready=0`), commits 2.99 s later
(`LoopEnd playback commit track=4 1->2`), has one `canonical_release` for A's own stop, gates green and zero hold
reports — the occupancy taken off-clock between press and commit
([`evidence/2026-10-01-s4b9-cross-track/`](evidence/2026-10-01-s4b9-cross-track/EVIDENCE.md)).

**A third, now FIXED device-state item (2026-10-01, firmware + host tooling): the diagnostic must not evict the
evidence.** The telemetry flood that made legs abort (a `PERS,work` row per persistence slice and per tail-write
step: 5 951 + 2 705 rows in one boot) is bounded (`Utils/TelemetryCadence.h` and its three call sites), the counters
are DMAMEM so RAM1 is untouched, and the successor-start gate now names the abort reason instead of blaming a
missing receipt. Measured: `PERS,work` 5 951/6 967 → 382, tail-write rows 2 705 → 29, `RING,wiped` 1 → 0, and the
legs' preconditions pass (`admitted after stop receipt`) with the device's large-library work still running.
Record: [`evidence/2026-10-01-telemetry-must-not-evict-evidence/FIX.md`](evidence/2026-10-01-telemetry-must-not-evict-evidence/FIX.md).
**Two further device-state fixes landed on the way there (same day, firmware):** a corrupt **head prefix** no longer
costs the ring (bounded resync, counted and named `RING,head_resync,…`; the wipe stays the fallback), and the rare
**save-completion receipt** is emitted where it is produced when USB has room — the ring is retained rather than
drained under transport, and a receipt delayed past its consumer is not a receipt. Rows and red-proofs:
[`evidence/2026-10-01-ring-resync-and-receipt-delivery/FIX.md`](evidence/2026-10-01-ring-resync-and-receipt-delivery/FIX.md).
**The image is now `a1f4e4c0…`** (the S4b.8 / S4b-gate evidence above was taken on `cdab16f4…`; re-running those on
the new image is a cheap regression check, and S4b.9's remaining clause is a *gesture* shape: the destination track
must be **playing** when its non-playing filled slot is pressed).

**Read these two device-state findings before re-running a device leg on this device.** (1) The census's
`chunks_free` counts **ids**, not backable capacity, so it can read 320 while zero events are admitted
([`evidence/2026-10-01-pool-free-but-append-denied/`](evidence/2026-10-01-pool-free-but-append-denied/FINDING.md)).
(2) One restore request can churn without progress because `loopSlotRestoreAttempts_` is never incremented, so
`kMaxLoopSlotRestoreAttempts` cannot fire
([`evidence/2026-10-01-restore-retry-churn/`](evidence/2026-10-01-restore-retry-churn/FINDING.md)) — that churn
starves the SD lane and floods the capture ring, which is the measured reason a leg's **precondition** gates
(the stop-completion receipt, the capture-ring integrity check) fail on this device: a **64-slot** boot restore
batch competes with the leg. Those failures are not the legs' own assertions; the S4b.9 attempts 05–11 aborted only
that way.

## S4c.1 landed — the readiness/currency specification (2026-10-01), and S2.1 is the next unblocked step

**S4c.1 is CHECKED (specification only; no firmware, no build, no device action).**
[`S4C1-READINESS-CURRENCY-CONTRACT.md`](S4C1-READINESS-CURRENCY-CONTRACT.md) turns `design.md` **Revision 11**
("immutable readiness at re-entry, not whole-loop publication") and its row 11 into nine acceptance criteria for
the sibling `windowed-playback-generation` — readiness per affected tick before the playhead returns; immutable,
identified windows; readiness as a predicate that **names its holder**; one definition of current with **no second
validity counter**; the one-pass-late rule as the only stale authority; nothing allocating or blocking on a
timing-critical edge (the S4b.6/S4b.7 `SlotProvision` turn is the cited precedent); the existing resumable machinery
as the mechanism; receipts that obey this change's two delivery rules; and length independence swept, not assumed.
It also fixes the **four interfaces S2.1 needs** — window identity `(slot, revision, interval)`, the `P`-class
accounting, the readiness predicate plus lag bound, and the off-clock owner — and names what stays undecided (the
window size `N` = S2.3/S2.6; the publication unit itself = the sibling's own DEC/reassessment; S4c.4/S4c.5; S4e).

**Why it was on the critical path:** S3 gates' remaining half is S2.2/S2.3's current-root per-class budget, and the
`P` class *is* the playback window lease that S2.1 may derive only once the phase-F contract's interfaces exist
(`IMPLEMENTATION_CONTRACT.md` §4). **S2.1 then landed the same day** — [`S2C1-PLAYBACK-WINDOW-LEASE.md`](S2C1-PLAYBACK-WINDOW-LEASE.md)
derives the `P` term from the resolved window with the composition measured on the **target ABI** (331 KiB per
holder windowed-only; a 2-bar window at typical density is 28.6 KB, ~12× smaller) — so the remaining step is
**S2.2/S2.3** (measure each class separately and prove the envelope). S2b.7(a)'s third conjunct (a track playing
at the same drain edge) remains the other named S3-gates item.
## Other candidates and why not first

- S4f.6 — accepted-start execution: the formerly missing arm-and-wait and
  failed-then-retried exactly-once fixtures **already exist and are registered** in
  `test_runtime_working_set_ownership`. Do not recreate them. The switch now calls
  `clearQueuedRecordingTrack(trackIndex, captureSlot)`; whole-track clearing is an
  obsolete remainder. Named acceptance reporting and the combined query still need
  their own source audit.
- S1.6c.2 — snapshot reuse capacity acceptance: recover the intended N=56 baseline
  and target eligibility by a read-only census and content comparison first. The
  copied library had 60 nonempty slots, and user interaction after restoration
  changed live state. Do not silently clear/rebuild a fixture or count an N≈60 run
  with another display transport as the original experiment.
- Recovery hardware content/Undo/Redo verification is a useful separate device
  step; it must not be inferred from `undo_hydrated=1`. No new device action is
  authorized by this documentation/commit request.
- S4b — immediate repeated handoffs still needs the explicit finalizer-capacity
  decision. Retaining a request does not prove immediate execution.

## Verification and closeout

Production hashes still match the checkpoint tested with 2,461 native passes /
7 skips; isolated ABI recovery tests cover those recovery skips. Reader/audit tests
rerun: 16 pass. Both affected OpenSpecs validate. **S4.1 persistence identity
(2026-09-26):** full native **2,465 passed / 7 skipped** (the four new cases are the
delta), capture-serial SUCCESS with `padding 856` (floor 840), `src/` untouched.
**S4.1 lease release on a same-track switch (2026-09-26):** focused
`test_runtime_working_set_ownership` 23/23 (three new cases), full native **2,468
passed / 7 skipped**, capture-serial SUCCESS with `padding 856`, `src/` untouched,
guards red-proved in three stages. **F4 / DEC-091 (2026-09-26):** focused
`test_storage_drain_restore` 15/15 (one new case; one pre-existing S2b.7 case
re-premised and recorded), full native **2,469 passed / 7 skipped**, capture-serial
SUCCESS with `padding 856` (byte-identical), red-proved, `include/` untouched. Snapshot spec hygiene preserves
previously omitted base scenarios and classifies two new requirements as ADDED;
no intended behavior changed in that reconciliation. No push or PR requested.
Capture remains open for the current diagnostic session; verify persistence idle
before any later close/reset. Exact captured CRLF bytes are preserved as evidence.
