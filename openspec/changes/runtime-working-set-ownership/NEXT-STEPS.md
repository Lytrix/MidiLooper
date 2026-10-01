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

1. **The harness's linked closure for the pending-switch lane** — the named prerequisite, measured: a
   *populated* target queues a **playback** switch that commits inside `TrackManager::updateAllTracks`'s
   multi-slot-commit lane, and driving that entry from `test_runtime_working_set_ownership` fails to link for
   exactly four symbols: `Track::playMidiEvents(uint32_t, bool)`, `Track::playMidiEventsForSlot(uint8_t,
   uint32_t, bool)`, `TrackManager::isTrackAudible(uint8_t) const`, `Track::shouldCommitQueuedPlaybackStart(
   uint32_t) const`. Add them through the `HARNESS_LINKS_*` pattern without shadowing them in the fail-closed
   stub surface. Until this exists, any fixture for a queued switch can only observe the *request*, not its
   commit or its absence — which is precisely the "silent dead switch" risk.
2. **S4b.6 + S4b.7** — the occupancy-aware, allocation-free binding and the off-clock provisioning owner it
   needs (`TrackManager::provisionSlotForSwitch` on the deferred lane, with a pending switch making its track
   eligible for a deferred turn). This is the largest remaining piece and the one that removes the "silent dead
   switch" risk the review named.
3. **S4b.2 / S4b.3 / S4b.5** as written, then S4b.4's inbound-role fixture.

S4.5 and the S4 gate close with S4f/P3, not before. Every remaining device leg (S4.2/S4.3/S4.4 device byte
deltas, S4b.8/S4b.9, phases B–E) and F4's SD-fault evidence needs a per-session device authorization.

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
