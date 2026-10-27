# 2026-10-27 — Queued-jobs retarget edge missing → ~2 s/job tail stall → session-exit obligation refuses starts

Attribution of the defect the owner reported against
`captures/session_20261027_172742.log` (after the DEC-167 stop-ring repair was
flashed): *"it does exit overdub after the 8th overdub but it won't allow to
start a new one"*. Attribution followed by the DEC-168 repair (implemented,
fixture-first, mutation-red-proved; see the Repair section at the end).

## Verdict

VERIFIED (device facts + code path) / ASSUMPTION (the one unobserved leg of
recovery, see "Open on evidence" below).

## Device facts (tail reads only; log has 50,054 lines)

Presses (grep `MIDI Button A`): **9 starts, 9 stops**, then 6 refused starts
(97.6, 99.3, 101.2, 102.7, 104.7, 105.6 s) — every refusal:

```
Overdub start refused: the session-exit publication obligation is still owned (slot=3 state=populated)
```

Stop machinery: `Overdub stop retarget: phase=1 dest=1 companions=kept` at
106.8 s (once, head only), then 7 × `Overdub stop: companion path stalled; raw
pass only (phase=5|7 retargeted=1)` at 109.2 / 111.3 / 113.4 / 115.5 / 117.7 /
119.8 / 121.9 s — **~2.1 s apart**, plus the two DEC-167 escape rows at 86.1 s /
95.7 s. Exactly **9** `ODUB,stop,atomic_transfer … causal_base_superseded` rows:
every interval job took the cause-based commit. Recovery-side
`DIAG,snapshot_unit` shows the publish backlog draining `q=10 → q=0,a=9` by
~130 s; heap tail (28 KB, editPins 778, reclaim `Critical`) is the known task-4.20
heap defect, not part of this attribution.

## Row decoding (pinned from `DebugSessionCapture.cpp:1522/1532`, emitted by
`RuntimeTimingTelemetry.cpp:1188-1269` via `TrackDeferredMaintenance.cpp:990`)

`DIAG,stop_phase|stop_grain = track, slot, phase, generation(= job admission
`frozenGeneration`), epoch, destination, …`. A row emits on identity change, so
the 109.2 s sequence — phase 10 (AtomicTransfer) → 11 (DeferredCleanup) at
`generation=7`, then phase 0/1/2… at `generation=19` — is **job N retiring
cleanly through the DEC-167 route into DeferredCleanup, then the next queued
job starting fresh**. The 2.1 s cadence is not a rewinding loop; it is
successive jobs each paying the DEC-070 512-grain `CanonicalTail` bound.

## Attribution (the code asymmetry)

`Track::requestDeferredOverdubStopToStopped()`
(`src/Track/TrackOverdubLifecycle.cpp:1884-1893`) at the transport stop:

- **head job**: full retarget edge — `discardFrozenCanonicalTail()`,
  `restoreSupersededPreparedCompanionHistory()`,
  `retargetStoppedRecovery()`, resets `retargetStallGrains_/Phase_`, logs the
  `Overdub stop retarget` row;
- **every queued job**: only `admission.requestTransportStop()`
  (`src/Track/TrackPlaybackRuntime.cpp:228-240`) — destination→Stopped,
  `retargetedFromPlaying=true`, and `++transportCommandEpoch` **on the job's own
  admission**.

Because the flip consumes the job's own epoch mismatch, the edge can never key
on it later: when a queued job reaches the head it arrives with
`retargetedFromPlaying=true` but **never gets** `retargetStoppedRecovery()`
(`src/Track/TrackPreparedStopCommit.cpp:236-243`, rebinds
`canonicalPassInputs` to the job's own epoch). The job waits out DEC-070's
512-grain bound (`stopPreparationEscape` → RawPassOnly,
`TrackOverdubLifecycle.cpp:2856-2866`), retires the companion preparation, and
commits through the DEC-167 superseded route. Cost per job ≈ 2.1 s; 7-9 jobs.

### Correction (verified in-fixture, not just in device forensics)

The *stale-epoch rebind* framing was incomplete. The fixture pinned the deeper
cause: a queued retargeted job's **canonical base was never built**, so
`beginFrozenCanonicalTail` cannot begin regardless of the epoch — the STOPPED
route skips `CanonicalPrefix` (`TrackOverdubLifecycle.cpp:2932-2936`), and only
the FIFO head was ever serviced as destination-Playing. At B's tail the fixture
observed `prefixPhase=Idle, prefixReady=0, attached=0, ready=0, retargeted=1`.
A "give each queued job its retarget edge at head-arrival" repair was implemented
and measured: the edge *did* run for the queued job (`retargetEdgeDone=1`), yet
`retargetStoppedRecovery` had nothing to rebind (`canonicalPassInputs` never went
live) and the tail still could not begin. That repair was reverted as not
load-bearing; the correct fix is the cause-based escape (DEC-168, below).

## Chain to the user-visible refusal (Task 4.20 / DEC-163 shape)

1. The 9th stop's receipt capture at 96.5 s finds the 8-slot receipt FIFO full of
   unconsumed receipts (publisher throughput < arrival rate) → the interval's
   tail goes to the reserved record → `Populated`.
2. That interval's job sits last in the stop FIFO and cannot commit until the
   8 jobs ahead pay their ~2.1 s tail stalls → commit ~122 s.
3. Meanwhile **every** `Live Overdub` press is correctly refused by
   `claimOverdubSessionExitObligation`
   (`src/Track/TrackOverdubLifecycle.cpp:514-531`): the obligation genuinely is
   pending. The refusal is legal-but-30-s-long; the user's duplicate presses hit
   the same gate with no progress signal.

## Open on evidence (ASSUMPTION)

No presses exist after 105.6 s, so the log cannot directly show
`Publishing → Empty` completion. Mechanism is unblocked: all 9 jobs reached
DeferredCleanup, `markSessionExitTailReady` fills the reserved record →
`Publishing` (`src/Loop/LoopOverdubSession.cpp:66-82`), the publisher drained
its queue (`q=0` by 130 s) and `moveReservedSessionExitIntoReceiptRing`
(`src/Loop/LoopSnapshotPublish.cpp:2161-2178`) drains to `Empty` on a free
FIFO slot. Expected recovery ≈ 130 s; unobserved but consistent.

## Repair (DEC-168 — implemented)

**Chosen fix (cause-based, mirrors DEC-167, not the edge-at-head sketch above):**
a retargeted job whose `canonicalBaseReady()` is false escapes raw-only at
`CanonicalTail` immediately. The sketch above (per-job edge at head-arrival) was
tried first and measured not load-bearing — reverted. The landed change is a
single block after the DEC-165 escape in the `CanonicalTail` branch:

- predicate `stop.retargetedFromPlaying && !runtime->canonicalBaseReady()`;
- `noteStallOnce("retargeted stop has no canonical base; raw pass only")` +
  matching log row, distinct from the DEC-167 wording;
- `retireSkippedCompanionPreparation(false)` (destination is Stopped, so the
  DEC-165 destination-gate concern does not apply), discard the prepared
  companion history, `setPhase(AtomicTransfer)`.

A retargeted job whose base *is* ready (the DEC-070 head flow) never enters it;
the DEC-165 non-retargeted escape above it is untouched. Output is identical to
the bound's existing escape (which already lost the companion representation),
so no content/undo/display regression — only the ~2.1 s/job latency is removed.

**Verification (fixture-first, as-executed):**

- RED: `test_a_retargeted_stop_without_a_base_escapes_without_the_stall_bound`
  (two-job queue, transport-stop retarget, assert B retires in < 512 grains of
  head-arrival). Pre-fix the job pays the 512-grain bound before escaping.
- M2 mutation red-proof: the escape predicate neutralized →
  `[FAILED]` at exactly the `<512` assert (`bTurns` crosses the bound); restored.
- `test_shared_capture_finalization` **113/113**, `test_playback_prewarm`
  **252/252** (DEC-165/DEC-070 decision-table asserts untouched), full native
  **3,525 / 7 skipped / 3,518 succeeded / 0 failed**, `teensy41-capture-serial`
  **SUCCESS** at RAM1 `variables 96,192` / `code 390,472 + padding 2,744`
  (floor 840, no re-base). No commit, no flash.

## Device verification (second capture, DEC-168 firmware — `session_20261027_172742.log`, reboot at line 2,726,829)

The fix is **verified on device**. The new capture's second session shows, after the transport-stop retarget:
- **0** `session-exit publication obligation … refused` (session 1 had 6) — the DEC-163 obligation is no longer the start blocker;
- **5** DEC-168 escapes (`no canonical base for retargeted job`), draining the queued jobs at **~64 ms each** (107.469 s → 107.789 s), versus ~2.1 s each pre-fix;
- **1** DEC-167 escape, **2** old 512-grain bound escapes (the base-ready head's known residual).

The remaining start refusals are now **task 4.20, not the stop path**: 7 ×
`#MEM_DENIED,op=overdub_start,reason=heap_low,need=16384B,heap_free=36KB` (plus 74 × `snapshot_reserve`).
`heap_free=36KB` < the 48 KB floor+margin, so `admitOperationMemory(OverdubStart)` denies — the same
false high-water metric §22 proved on hardware (a 16 KB internal malloc succeeds at a 24 KB tail). This
is the already-scoped, separate admission-metric slice, not a regression of DEC-167/DEC-168.

## Open on device (residuals, not regressions)

- The transport-stopped **head** job (the one DEC-167's device run showed at
  `CompanionHistory` stalling 512 before escaping) still pays that bound; its
  base was already built so the DEC-168 cause does not apply. It is a single job
  and drains during the session, not after the last press, so it does not block
  the reported start-refusal window.
- Restoring the note-edit (companion) representation for stopped-retargeted
  queued jobs would require building a base the STOPPED route deliberately
  skips; left as a separate decision.

## Separate defects intentionally not touched here

- task-4.20 heap tail (28 KB, `reclaim … Critical`, editPins 778) — separate RED
  + replacement-metric slice;
- transport-stop state row `Invalid state transition from STOPPED to STOPPED`
  (121.9 s) — cosmetic-level, logged for follow-up triage.
