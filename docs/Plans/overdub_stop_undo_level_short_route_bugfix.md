# Overdub-stop undo level on the deferred short routes (bugfix slice)

**Status:** implemented, native verified (fixtures red-proved), firmware build verified, device A/B on the
clean route verified. Not committed yet.

**Scope:** `runtime-working-set-ownership` / **S4f.17 — common interval and stop admission** (the deferred
overdub stop's publication). User decision for this slice: **deferred overdub route only; publish the undo
entry at pass publication, once per job; leave the existing full-route collection machinery intact.**

## Symptom

The sidebar `U:` level (`TrackUndo::undoDepthForLoop`) counted only the record pass while the loop was live:
`U:01` after recording plus overdubs, but `U:03` after a reboot. The pre-existing base check
`undo_redo_after_overdub_stop` logged `0 counts` against `expected_min: 1`. On device the difference correlates
with the *route* the overdub stop takes, not with loop length:

| device run | OD stop `stop_phase` trail | `undo_entries` across the OD1 stop |
|---|---|---|
| `captures/s4f20-odundo-02` (full route) | `1,2,3,4,6,5,7,8,9,10,11` | 5 → **6** |
| `captures/s4f20-odundo-07` (clean route) | `1,6,10,11` | 5 → **5** ✗ |
| `captures/s4f20-odundo-09` (full route) | `…7,8,9,10,11` | 5 → **6** |
| `captures/s4f20-odundo-10-1` (clean route, fixed) | `1,6,10,11` | 5 → **6** ✓ |

The "3-bar record" the user saw is the same defect surfacing through stimulus timing: a late record start is
quantized up to 3 bars (raw 2006 → 2304 > the 384-tick grace), which shifts the later steps so the overdub stop
lands as a **clean stop** (`1,6,10,11`) instead of the full route. Both routes are correct production routes;
only the full one published an undo level.

## Root cause

`Track::processDeferredOverdubStopGrain` (the deferred overdub stop drive) publishes the interval's pass, but the
`OverdubPassAdded` entry was only ever prepared in the `CompanionHistory`/`UndoCollection` phases. Routes that
skip those phases therefore committed the pass with no undo entry:

1. **Clean-stop jump** — `PrepareTransfer` with `SealOutcome::SkippedEmpty` (the interval's take was already
   committed by its wraps, so nothing is staged at the stop) jumps straight to `AtomicTransfer`. This is the
   dominant device route (`1,6,10,11`).
2. **STOPPED destination without verified companion preparation** — `TrackPreparedStopCommit`'s
   "transport Stop can recover the frozen canonical pass" fallback committed the pass and published **no** entry
   by design.
3. **Superseded causal base** — the superseded variant runs before the audible-bridge handoff used to sit, and
   `commitSupersededCausalBase` publishes only when the turn stages a pass.

Two further defects kept the deferred handoff from working once it existed: `job.undoPublicationReady` held
`begin`'s *return* (the transaction is opened in `DetachRedo`, not `Ready`), so the gate skipped the prepare turn;
and `TrackUndoPublication::canPublish` required a *staged* pass identity, which a clean stop never has.

## Fix

- **`Loop::gatherSessionExitReceiptUndoIdentities`** (new, `LoopOverdubSession.cpp`) reads the interval's **own**
  session-exit receipt (DEC-068) in `collectOverdubSessionUndoPasses` shape; the receipt type stays Loop-private.
- **`Track::prepareDeferredOverdubUndoEntryFromReceipt`** builds the canonical `OverdubPassAdded` entry
  (`TrackUndo::pushOverdubSessionOnStop` shape: `passId` = last identity, `passIds` = the whole set only when
  `>1`, companions flattened) from the receipt **plus** any staged final pass. Naming nothing = fail open (no
  entry, no stall).
- **Handoff at the top of `AtomicTransfer`**, once per job (`undoPublicationStarted`): prepare, then drive the
  protected transaction to `Ready` (`stepDeferredUndoPublication`), so **every** transfer variant can publish what
  it prepared: `commitWithUndo`/`commitStoppedWithUndo` (staged), the no-companion Stopped fallback, the
  **no-staged** case (its take was committed by its wraps), and `commitSupersededCausalBase` (now given the stack
  as the publishability witness). A refused `begin` retains the entry and retries, so a pass is never published
  without its level — the full route's existing policy.
- **`job.undoPublicationReady` means "publishable (`Ready`)"**, not "opened".
- **`TrackUndoPublication::canPublish(stack, loopId, kInvalidPassId)`** = "this turn commits no pass of its own";
  the entry's own identity is then the whole requirement.

## G2 execution packet (scaled to risk)

- **Scope:** deferred overdub stop publication only; no change to the full route's collection machinery, no new
  job fields, no size-pin change.
- **Transition oracle:** after a stopped overdub stop, the global stack gains exactly one `OverdubPassAdded`
  entry for that slot (`undoDepthForLoop` +1), and it names the interval's own identities; nothing is published
  when the interval names no identity, and the stop always retires.
- **Controllable fixture:** `test_stopped_overdub_stop_publishes_one_undo_entry`
  (`test/test_shared_capture_finalization/`) drives four legs on the production stop path — stopped (short route),
  a second stop (+2), a superseded causal base (+3, same fault injection as
  `deferredPersistence(false,true,false)`), and a **clean stop with no staged pass** (+4, identity seeded by
  `pushOverdubSessionPass` while the session is open, exactly how a wrap records it).
- **Verification evidence:** see below.

## Evidence

**Native.** `pio test -e native`: 2637 cases, 2630 succeeded, 7 skipped, 0 failed. Mutations red-proved, each
failing only the new fixture and each restored byte-identically (sha256 compared):

| mutation | effect |
|---|---|
| flag semantics back to `begin`'s return | leg 1 fails (entry never prepared) |
| remove the no-companion Stopped fallback publish | leg 1 fails |
| remove the prepare call site | leg 1 fails **and** `test_stopped_publication_requests_own_slot_once` stalls — the proof that the fail-open guard is load-bearing |
| remove the prepared-entry final-pass guard | SIGSEGV on leg 1 (the receipt names no passes there) |
| move the handoff back below the superseded branch | only the superseded leg fails |
| remove the no-staged publish | only the clean-stop leg fails |
| require a staged pass for the identity | only the clean-stop leg fails |

**Firmware.** `pio run -e teensy41-capture-serial` SUCCESS; `RAM1` ITCM 393,216 (code 391,288 + padding 1,928),
approved floor padding ≥ 840.

**Device A/B, clean route, identical command** (`--preset base --track 5 --loop-slot 1 --midi-channel 1
--boot-reset`): `captures/s4f20-odundo-07` (pre-fix) trail `1,6,10,11`, `undo_entries` 5→5, no short-route row;
`captures/s4f20-odundo-10-1` (fixed) same trail, `undo_entries` 5→6 and one
`#CAP,…,DIAG,gus,slot,…,src=deferred_short_route` row. `10-1`'s scenario report is not a clean pass (the capture
closed while the run was still finishing; no ring discard, fresh boot), so the undo-depth evidence there is the
device-log fact, not the scenario verdict. Full-route control, `captures/s4f20-odundo-09`: `undo_entries` 5→6 and
`MIDI: Undo (entries=3)` — the `U:03` a reboot's content reconstruction (DEC-088) reports.

## Device procedure note (learned here)

A device whose transport is still running refuses `!REBOOT` ("transport-gated"), after which the harness's
attach-freshness check (`expected at least 40 line(s)`) refuses to start the scenario — the run then cannot
recover by itself. Clear it first: send `stop` twice with the MIDI port held open (mido, port `Teensy MIDI`),
wait ~6 s for the deferred save, then run.

## Follow-ups (not part of this slice)

- `S4f.22` still needs response-driven R→R/OD→OD/mixed legs and content parity (`inventory_equal`).
- Remaining unrelated residuals in device runs: `persistence_stage_missing:result`.
- The pre-existing HITL "return to playing" case (`--normal-stop`) and the disturbance wrapper's setup still need
  a clean run.
