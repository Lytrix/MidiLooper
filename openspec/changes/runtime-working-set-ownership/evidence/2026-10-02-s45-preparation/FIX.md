# Accepted-sequence preparation — root cause and verification

## Execution packet

- Scope: S4.5 — accepted-sequence acceptance; existing response-driven HITL preparation contract and DEC-092 — response-driven same-boot preparation. User requested root-cause investigation and a tested fix after the device runs below.
- Owner: `hitl.flows.accepted_sequence` prepares authorized targets; `stopped_preparation.clear_slot` classifies scoped clear evidence. Firmware owners and measured acceptance criteria are unchanged.
- Primary files: `scripts/hitl/flows/accepted_sequence.py`, `scripts/test_accepted_sequence.py` and CLI help for any changed preparation gate.
- Status: host defects fixed and regression-verified; firmware same-slot reuse fixed and native/build verified; hardware qualification remains preparation-blocked.
- Authorization: user explicitly allowed clearing/overwriting track 8 slots 4/5. No other slot clears, workspace quarantine or SD rename/delete. Capture firmware uploaded only for dedicated HITL evaluation after successful build.
- Preconditions: existing firmware, registered accepted-sequence scenarios and existing host fixtures. The full host/native suites were skipped at user request; focused regressions for this fix are authorized by the subsequent request to test a fix.
- Mutation: correct occupancy-to-emptiness conversion at the preparation callback; make an unconfirmed seed-release edge stop preparation before another target is touched. No firmware/scheduler/capacity changes, no acceptance weakening.

## Evidence before edits

- Record: `captures/s45-hitl-20261002-150151-rec/accepted-sequence-rec/` — `DROVE_LEG`, clean shutdown, stored-to-effective parity for both slots; timing owner reports overflow-free/call-site-clean, `deadline_safety_proven=false`. Sealed acceptance bundle replays intact but is `INSUFFICIENT_EVIDENCE` on L5b — start-route attribution and L7 — one-press correlation.
- Overdub: `captures/s45-hitl-20261002-150603-od/accepted-sequence-od/` — `PREPARATION_FAILED`, clean shutdown. B seed stop at device 262.117 s had no canonical-release row through capture end. A selection at 273.809 s queued a filled-slot playback switch; long press at 278.898 s did not report a clear. The host nevertheless reported `ALREADY_EMPTY`, then three arm attempts had no receipt.
- Verified defect: `target_emptiness` returned `_run_census` occupancy unchanged, while `clear_slot` treats True as confirmed empty. Occupied True was therefore falsely accepted as empty; unknown must remain unknown.
- Verified gate gap: `seed_take` recorded `seed_*_finalization_settled=false` and proceeded. Stop/persistence completion is not the seed's ownership-release edge; continuing crossed the preparation precondition.
- Heap observations: record 112 memory samples, overdub 72; minimum reported heap free 144 KB, allocation refusal counters always `0/0B`, no FATAL/CrashReport/allocator-refusal marker. The overdub end reports external live 6007 KB/high 6012 KB. These observations do not prove all heap behavior, but do not support exhaustion as this failure's cause.
- A preliminary background run `captures/s45-hitl-20261002-145956-rec/` ended before a completion report; excluded from acceptance. Foreground managed runs were used thereafter.

## Oracle and controls fixed before edits

1. Census occupied True -> clear callback returns False; occupied False -> True; missing/None -> None. Drive the real nested callback from the existing fake-capture fixture. Neither an occupied nor unknown slot may qualify as an empty target.
2. Seed release missing after the configured bounded wait -> preparation fails, reports the seed slot and timeout, sends no next-target/leg gesture, and retains normal managed shutdown.
3. Slot-scoped release present -> existing seed sequence continues; persistence result or stop receipt alone cannot substitute.
4. Retain existing authorization, tap spacing, wrong-target/refused-clear and content/identity/overlap gates. Normal measured windows remain continuously playing; preparation remains stopped-only.

## Verification recipe

- Establish focused failing host regressions before behavior edits; rerun only the focused preparation tests after the fix.
- Reuse the registered `accepted_sequence_od` entry point on track 8 slots 4/5 with no upload and no reset for same-boot preparation. A missing firmware release remains a named blocker, not a passing retry.
- Grade any driven leg with the existing oracle; report physical deadline proof separately from sparse timing gates.
- No task checkoff until all required acceptance evidence exists.

## Completion record

The host-only correction passed its two new red-to-green regressions. The same-boot device rerun
`captures/s45-prepfix-20261002-151549-od/` rejected a populated target as UNCONFIRMED rather than falsely
reporting ALREADY_EMPTY; it sent no seed/acceptance notes and shut down cleanly. Full suites remain skipped.

## Follow-up native investigation packet

Read-only device probe `captures/s45-prepfix-stop-status-20261002/status-probe.json` reports
PLAYING with global transport stopped, `pending=1,phase=0,canonical=137,queue=1/4`; an idempotent MIDI Stop
changes state to STOPPED but leaves the same pending job. Phase 0 is ReservedClosure, not the previously
fixed RecordPublished retirement wait. No active view builder is reported.

Hypothesis (not yet proven): retained frozen rewrite scratch from a completed same-slot record prevents
the next record's closure from binding. Overdub admission calls `discardFrozenCaptureCanonicalOrder`;
record admission does not. `stepDeferredRecordClosure` rejects a cursor from a different stop owner/generation.
Test scope: existing `test_shared_capture_finalization` production stop/worker harness, two complete records
on one slot with a production clear between them; assert full retirement and second-take content.
No firmware changes until the discriminating fixture identifies the failed invariant. Existing owners,
bounded deferred grains, undo/persistence handoff and runtime placement remain unchanged.

## Firmware execution contract — same-slot record reuse

- G0 — discovery: VERIFIED by production-path native reproduction. First record retires; after clear and
  re-record, the second stop retains a rewrite binding from the first interval and stalls at ReservedClosure
  for 4,361 worker turns. An explicit next-owner/generation assertion independently fails before driving.
- Existing owner/pattern: `Track::beginDeferredRecordStopForSlot`; reuse the ordering/rewrite discard calls
  already made by `beginDeferredOverdubStopForSlot` after proving no live finalization owner. Scratch remains
  on the same `LoopPlaybackRuntime`; raw range and pending notes remain owned by the same `CaptureStopJob`.
- Mutation: after all record admission predicates and before geometry/seal/handoff, discard retired frozen
  order/canonical-order cursors. Their reset is fixed-size, allocation-free, introduces no fields, and changes
  neither timing scheduling nor publication/undo/persistence semantics. No new owner or architecture trigger.
- Must preserve: denied admission leaves a live owner's state untouched; admitted stop owns its raw range,
  closure capacity and identity; no whole-take work or validation on the stop edge; exactly-once publication,
  undo and persistence, full retirement, and no premature release of successor resources.
- Decision review: DEC-092 — response-driven same-boot preparation; DEC-093 — shared capture finalization;
  DEC-096 — pending-finalization slot gestures; DEC-107 — bounded RecordPublished retirement. The observed
  ReservedClosure stall is distinct from DEC-107. Reuse YES; no migration or superseding decision required.
- Verification: `pio test -e native -f test_shared_capture_finalization`; both new Python preparation
  regressions; `pio run -e teensy41-capture-serial` including RAM1 guard; dedicated registered accepted-sequence
  HITL on the authorized slots after successful build. Full suites are not claimed. No acceptance checkoff
  from a fixed scratch lifetime alone; record oracle and complete campaign evidence remain separate obligations.
- Red evidence: `s45-native-reuse.log` and `s45-native-stale-binding.log` in session scratch, both fail the
  added reuse test; 57 existing cases report success, but PlatformIO also reports SIGHUP/ERRORED, so these are
  discriminating failing assertions, never a clean baseline suite pass.

## Final verification and remaining blockers

- Native red-to-green: `pio test -e native -f test_shared_capture_finalization` now **58/58 passed**, clean
  suite exit. The new `test_record_clear_record_reuses_finalization_scratch` both rejects a stale owner binding
  and verifies the second take's full retirement/content. Before the fix it failed first at the bounded
  retirement oracle, then at the explicit binding assertion. No firmware mutation was needed to create the failure.
- Host focused: `python -m unittest` for `StoppedPreparationTests`, `CensusPreparationTests`,
  `PreparationIntegrationTests` and the two new `EvidenceGateTests` cases: **26/26 passed**.
- Build: `pio run -e teensy41-capture-serial` SUCCESS; RAM1 variables 96,192 B, code 391,944 B,
  padding 1,272 B, locals 34,880 B; padding floor 840 B satisfied. No new runtime state.
- Upload: successful via the registered HITL entry point's `--build-upload`, on the active working branch.
  Built image SHA-256 `0a7c89f9bb7b4fd4d2180462abc9f63d71c67862abbac267fae54d7d92134719`;
  git base `ce7041333cb363b4d1d7f8ac1c5af845fc2ce54f` plus this dirty firmware change.
- Device attempts on that image: `captures/s45-reusefix-20261002-153003-rec/` failed fresh-boot confirmation
  before gestures; `captures/s45-reusefix-20261002-153058-rec/` cleared slot 5 but failed the idle-target check
  (ARMED evidence); `captures/s45-reusefix-20261002-153202-od/` stopped at UNCONFIRMED clear. All three report
  complete shutdown. None reached the same-slot reuse verification window, so **firmware HITL proof is still owed**.
- The remaining preparation state is explicit: `LIBOBS,S,t=7,s=4,...p=3,...k=0,...l=0,v=0` — verified SD
  payload, no loaded content, zero length. The slot short press arms recording and its long clear is silent.
  The census deliberately treats SD-present as occupied; the fixed callback correctly refuses to call this
  empty. Do not reverse that safety fix or bypass the SD-aware precondition to count a run as passing.
- Separate record-oracle findings (read-only, not fixed in this slice): L5b — start-route attribution slices
  by file position and misses the correctly timestamped carrier before delayed RECS/RECA rows; L7 — one-press
  correlation has no passing Record branch and does not consume the driver's gesture report. The fresh record
  run contains witnesses, but those must be correlated with recognition/request/execution, not inferred from
  one stop execution. A proper oracle fix needs negative coverage for stale/wrong-slot/duplicate gestures.
- S4.5 — accepted-sequence acceptance and S4f.11 — interaction latency remain open. No native full-suite,
  physical deadline-safety, three-consecutive-pass campaign or broader continuity claim is made.
- Session closeout: existing owner pattern reused, no new architecture/exclusion decision; decision-log append
  not required for this corrective implementation. No commit or push. All slot mutations stayed within the
  user-authorized track 8 slots 4/5; earlier scratch content there has been overwritten/cleared by the runs.

## Root causes of the remaining S4.5 validation blockers (2026-10-02, part 2)

### A — stale idle read (host; FIXED, verified)

`require_idle` read only the *arming* wire rows, so a target the device had already released stayed `armed`
in the host's view. The device's own rows prove it released it: `captures/s45-reusefix-20261002-153058-rec`
has `DISP,4,ARMED` at 22.335 s and the clear's `DISP,4,STOPPED,0,…` at 23.490 s. Fix: `_slot_ready_state`
now consumes the slot-scoped `DISP` state family as the device's current state (any non-arm state ends the
arm), ranks clocked rows by device clock and unclocked ones as live-by-construction (they are printed straight
to `Serial`), and uses file position only as a tie-break. Regressions: the 153058 shape, a late-flushed stale
`ARMED` row, and the existing armed-target refusal (kept).

### D — the record oracle's two unusable links (host; FIXED, verified)

**L5b** sliced the route decision by *file position*, but the ring flushes in batches: in
`captures/s45-hitl-20261002-150151-rec` the carrier sits at 102.841 s on line 3508 while A's
`RECS,stopToStopped` is at 102.374 s on line 11075 and B's `RECA` at 103.433 s on line 11659 — the route the
device named was outside the window and reported as an evidence gap. The record branch now selects route rows
by the device-clock window `[A stop, B start]`, with unclocked rows keeping the positional reading (synthetic
fixtures carry no clock).

**L7** had no passing branch for the record leg (`UNKNOWN` unless a second stop existed) and never consumed
the driver's gesture report, so "one press per stop" was unprovable by construction. `analyse` now takes
`driver=` (plumbed from `--run-report` on both the CLI and the registered-scenario route); it requires exactly
one emitted `b_switch_start` and one `b_stop` inside the measured window, each witnessed by the device's own
row, and reports duplicates as FAIL, missing report/witness/window as UNKNOWN — never a weakened pass.

Re-grading the existing record capture with these fixes yields **`ACCEPTED_SEQUENCE_PASS`** (L5b from
`[102.841] Loop 5: Record start carried on the switch`, L7 from `Button press: Loop 5 (short)` and
`Button press: Record/Overdub (short)`). Full `scripts.test_accepted_sequence` suite: **166/166 passed**;
six new L7 cases plus the two new idle-read cases.

### B — the authorized target cannot be prepared (device; needs a decision)

Root cause, from the device's own rows:
* `!LIB_OBSERVE` for track 8 slot 5 (firmware `t=7,s=4`): `st=2,p=3,o=2,q=5,m=1(terminal committed),k=0,kp=0,
  kr=0,kc=0,a=0,h=0,l=0,v=0,n=1`.
* A **fresh boot** restores that slot and reports success — `RRQ,7,4 … running` at 6.62 s, then
  `RRQ,7,4,0,0,terminal,none,committed` at 6.66 s — and the census still reads `k=0,a=0,l=0`. So the device
  commits a restore that installs nothing: the persisted body is **empty content**.
* The census maps payload *presence* to populated (`hitl/library_observation.slot_content_state`), so an empty
  persisted body reads as occupied.
* The clear gesture cannot act on it: `MidiButtonActions::CLEAR_TRACK_FOR_SLOT` returns before
  `handleClearTrack()` unless `Track::hasDataInSlot(slot)` is true, and that predicate is
  `Loop::hasData()` = `hasCommittedPasses() || loopLengthTicks > 0 || !capture.store.empty()` — RAM only.
  Measured: the long press leaves only `Loop 5 hold released`, no clear/refusal row, so the host's
  SD-aware emptiness gate can never pass on that slot.
* It is not a scratch-slot accident: a read-only observation of the whole library shows **all 64 slots**
  carrying a verified payload (`P=43` in the end frame, 21 with nothing loaded) and **zero** census-empty
  slots. There is no per-slot body-delete command in the firmware, so a cleared slot keeps an empty body.

Consequence for S4.5: the record leg's "genuinely empty and idle" target, and the overdub leg's B-seed
precondition, cannot be met on this board state through the production controls. Options (all need the owner's
call): (a) extend the library observation with an empty-body fact so such a slot classifies as empty — an
extension of the existing `Payload` vocabulary, no destructive change; (b) make the clear gesture act on a
card-backed slot (load-then-clear or body removal) — destructive semantics; (c) authorize a destructive
cleanup of the scratch slots' bodies (set-level; there is no single-slot delete) and keep today's semantics.
No firmware behaviour was changed for this finding.

### Still owed for S4.5 after the above

* Fresh-boot device legs on the current image (record leg, overdub leg) once a clearable target exists, with
  the accepted-stop-while-finalizing evidence (`accepted_stop_finalizer_wait`, busy-owner names).
* S4f.11 — per-interaction-class acceptance→execution latency numbers on device.
* The three-consecutive-pass campaign and the sealed bundles for both legs.

## DEC-110 — the card-backed clear (owner-decided 2026-10-02, implemented and device-verified)

The owner chose the destructive-route option: the clear gesture must be able to clear a slot whose content
exists only on the card. Implemented as **DEC-110 — a slot with no content keeps no persisted payload**:

* `StorageManager::removeLoopSlotPayload(track, slot)` removes the final and temp slot file plus the seal
  journal and updates the payload mirror (true when the slot is absent afterwards).
* `CLEAR_TRACK_FOR_SLOT` treats payload presence as content, and `handleClearTrack()`'s no-RAM-data branch
  removes the body, supersedes an in-flight restore for that slot, and reports
  `MIDI: Clear removed persisted slot N payload (absent)`.
* The save never writes a payload for a contentless slot and removes a stale one
  (`shouldWriteCurrentSetLoopSlot` / `currentSetSlotHoldsNoContent` / the per-slot stage decision), so a save
  cannot re-create the empty body.

Verification: `test_current_set_storage` **14/14** with the new policy case, red-proved by dropping the
emptiness term (`Expected FALSE Was TRUE`, restored byte-identically); `pio run -e teensy41-capture-serial`
SUCCESS with RAM1 unchanged (`code 391,944 + padding 1,272`, floor 840, no new state); uploaded image
`ae591d95…`.

**Device result — `captures/s45-clearfix-20261002-160609-rec3` (authorized track 8 slots 4/5):** preparation
now reports `prepare_slot_b … status ALREADY_EMPTY (census)` with `state None` (idle — no press was needed
because the payload was gone) and clears slot 4 through the bounded retry (`post_load_holdoff` x2, then
`CLEARED`), and the record leg drives end-to-end (`DROVE_LEG`, clean shutdown). Graded by the oracle with
content/timing/driver evidence, every lifecycle link is green — L1/L2/L3/L4/L5/L5b/L6/L7, the measured
window (continuous transport, stable clock), identity, teardown and the timing owner — and the **only**
remaining failure is the content-parity veto on slot 5.

**Remaining content-parity item (root-caused, host recipe):** the record leg sends B's single note blindly
20 ms after the switch press (`scripts/hitl/flows/accepted_sequence.py`, `b_switch_start`), while B's take
starts later — this run's `Recording started @ tick 29952` is at 105.347 s against a switch press at
104.42-104.94 s, and no `MI` row for note 79 exists in the capture at all, so the take finalized **empty**
(`record#0: 0`, `effective#0: 0`) and storage-to-effective parity is unprovable (`None` -> veto). The fix is to
gate B's content on the device's own B-recording evidence (the same `RECA`/`Recording started`/`DISP
RECORDING` evidence the other steps already wait on) or clock-time it like A's, with a regression that a
blind early send fails. Not yet implemented; no acceptance checkoff.


## Device outcome after DEC-110 (2026-10-02, current image `ae591d95…`)

**Record leg — `ACCEPTED_SEQUENCE_PASS` with a sealed bundle:**
`captures/s45-bconent-20261002-161255-rec`. Fresh boot, identity pinned, no ring loss, content parity for
**both** slots (slot 4 = 29 rows, slot 5 = 2 rows, storage⇄effective equal), timing owner clean, teardown
complete, and every lifecycle link green: L1 (A's stop accepted once), L2 (no capacity denial), L3 (A's
finalization window), L4 (A's release), L5 (successor start before A's release), L5b (record carrier route),
L6 (B's stop executes once), L7 (exactly one press per stop, each witnessed by the device), measured window
(continuous transport, stable clock). `deadline_safety_proven` remains false by the timing owner's own rule.

**The content-parity gap is fixed and device-verified:** B's content is now gated on the device's own
B-recording evidence and sent on the device clock (`b_record_content start_confirmed=True,
clock_source=device_clock, gate_clocks=2`) instead of the previous blind 20 ms send; the leg names
`B_RECORDING_NOT_CONFIRMED` / `B_RECORD_CONTENT_UNCONFIRMED` instead of silently producing empty content.

**Overdub leg — drove, not yet verdict-grade:** `captures/s45-odleg-20261002-161512-od`. All four seed
steps confirmed **including both finalization releases** (`seed_b_finalization_settled=True`,
`seed_a_finalization_settled=True` — the host's new release gate and the firmware's same-slot fix both
exercised), content parity for both slots, timing clean, L6/L3/L4/L5b/L7 green, measured window continuous.
Not a pass for three separate reasons: (1) the capture carries no boot banner because this leg was driven
on the same boot (`--no-boot-reset`) — a verdict-grade od leg needs `--boot-reset`; (2) `L1_a_stop_accepted_once`
found no `DIAG,reentry,bound` for A; (3) `L5_successor_started` reports B's admission **after** A's release
edge, i.e. the required overlap was not exercised — the already-recorded populated-successor scheduling
question (`CURRENT_WORK` `s4f13-mixed-*`), which needs a runtime decision rather than a test-order change.

**Resolved by the Phase 1a run below:** (1) the fresh-boot leg was driven with `--boot-reset`; (2) A's
`reentry,bound` **is** present on the leg interval (`reentry,bound,1770,3,3,0`) — the earlier leg never bound because
its take was empty; (3) the overlap **was** exercised, and the "after A's release" reading was this oracle taking the
*seed's* window and release out of a slot-wide first match.

## Phase 1a/1b — the overdub leg passes; the reading was the defect

**Device:** `captures/s45-odleg1a-20261002-163640-od` (image `ae591d95acf0df2ad731f1fd20808ec3e0b25ff6c276dd90519947b75d1184c2`,
commit `ce7041333cb363b4d1d7f8ac1c5af845fc2ce54f`, authorized track 8 slots 4/5), driven through the registered
scenario (`accepted_sequence_od`) with `--boot-reset --no-upload`: fresh boot banner, both targets cleared and seeded
with both finalization releases confirmed, the leg's full plan driven, teardown `complete` (2 drains reached verified
persistence idle), no latch.

| Link | Device row |
|---|---|
| A's interval start | `DIAG,reentry,admit,667,3,3,0` (45 140 156 us) |
| A's accepted stop (L1) | `DIAG,reentry,bound,1770,3,3,0` (48 012 717 us) |
| A's finalization window (L3) | `DIAG,reentry_window_open,3,1770,2304` (48 012 769 us) |
| A's release (L4) | `DIAG,canonical_release,3,32,2304,8` (49 535 980 us) |
| B's start (L5) | `DIAG,reentry,admit,2015,4,4,1` (48 649 488 us — 0.637 s into A's 1.523 s window) |
| B's stop (L6) | `DIAG,stop_state,7,4,7,5,2384,…` (49 617 732 us) |
| B's release | `DIAG,canonical_release,4,0,1536,8` (49 633 234 us) |
| Route (L5b) | `[48.182] [INFO] Loop 5: Switch gesture from slot 4` |

**Content:** the driver's `a_overdub_start_notes` / `b_overdub_start_notes` events are `confirmed=True` with
`clock_source=device_clock` and `gate_clocks=2` (16 ticks) from `bpm≈119.9`, `pulses=728`, so both takes carry real
events; the content owner confirms it from the device — slot 4 `effective#0=64 = record#0 32 + overdub#0 32`, slot 5
`effective#0=34 = 32 + 2`, `storage_matches_effective=true` for both, no unexplained difference. Timing owner:
`overflow_free=true` with no issue, `OVERDUBBING=2`, `OVERDUB_STOPPING=2`, `tier_a_degraded=false`. Verdict
**`ACCEPTED_SEQUENCE_PASS`** with the sealed `acceptance-bundle.json` (replay-verified digest, `claims_success=true`).

**Root cause of (3):** the od leg's preparation seeds A and B with *records* first, and a record stop opens a
finalization window and retires like any other interval — so one slot carries two windows and two releases
(`reentry_window_open,3,1994` at 35 451 737 us with `canonical_release,3,2` at 42 838 558 us for the seed, against
the leg's `…,3,1770` at 48 012 769 us with `canonical_release,3,32` at 49 535 980 us). `L3`/`L4`/`L5` took the
**first** slot-wide match, credited the leg with the seed's acceptance, and bounded B's start against the seed's
release — reporting "the successor began after the predecessor finished finalizing" for a leg whose successor started
0.637 s *inside* the window. `L1` and `overdub_interval_identity` were already interval-scoped, which is why the same
log showed the correct interval-3 chain beside the stale reading.

**Fix (host, `scripts/hitl/accepted_sequence_probe.py`):** A's window and release are selected for A's **own leg
interval** — floored by its start admission (`DIAG,reentry,admit` is the overdub lifecycle's row, so the record seed
carries none) with the accepted-stop binding as fallback; L4 reads the interval-qualified
`canonical_release_interval` (the pairing `overdub_interval_identity` already uses) and no longer falls back to the
slot-wide list, whose first row is the seed's release; human rows are placed by device stamp when a floor exists, and
excluded rows are annotated as such in the evidence. The `rec` and `mixed` legs are unchanged (their A stop is the
leg's only one).

**Red-proof:** with the selection reverted, the three new tests fail
(`test_a_seeded_legs_window_and_release_are_its_own_not_the_seeds`,
`test_a_leg_without_its_own_window_is_never_credited_with_the_seeds`,
`test_a_seeded_leg_without_its_own_release_does_not_borrow_the_seeds`) and this same device log re-grades
`SETUP_INCOMPLETE` naming the seed's rows (48 012 769 → 35 451 737 us), i.e. the exact reading that produced the
earlier verdict; the file was then restored byte-identically (sha256
`be777bc4c24d2fddcde1e7479ad2f6009e3bdb6835239f5ea763692a6b17849a` before and after).

**Phase 1b (content, host):** the leg's A/B overdub content is emitted on the device clock *after* the device names
its own start, and a take whose content never arrives is **named** (`OVERDUB_CONTENT_UNCONFIRMED`) instead of being
sent blindly: a blind 20 ms pair is 7.7 ticks at 120 BPM against `DEFAULT_NOTE_MIN_LENGTH_TICKS = 8` with removal
enabled, so the hot stop dropped it and the interval finalized `events=0` (measured, `s45-odleg-20261002-161512-od`),
which also meant no window opened for a successor to overlap. A missing clock reader is the same named failure, never
a silent empty take; a record-leg test that drove the od leg without a clock now supplies one, so the retention
prefix asserts the production route.

**Verification:** `test_accepted_sequence` **170/170**, corpus **3/3**, scenario **40/40**, nine neighbouring host
suites **194/194**; `captures/s4f22-dec096-od-13`'s L3/L4/L5 and `overdub_interval_identity` re-read **identically** to
its stored review (no regression — its remaining `INSUFFICIENT_EVIDENCE` is only the side reports that capture never
carried). `deadline_safety_proven` stays false by the timing owner's own rule and is not claimed here.

**Overlap wording settled (Phase 1d) — DEC-111:** the link stays required in **every counted pass**; a run whose
successor is admitted after the predecessor released is NOT_EXERCISED (`SETUP_INCOMPLETE`, setup class), preserved as
operational evidence, never a pass, and it breaks the streak. Decided on evidence rather than preference: this leg
produced the overlap with room to spare (0.637 s of a 1.523 s window), so a serialized run is an arrangement outcome
and no timeout, window or budget is widened for it. Recorded in [`DECISION_LOG.md`](../../../../../docs/DECISION_LOG.md),
`S4F-CLOSURE-PLAN.md` §5 and the S4.5 — P3 task note.

**Still owed for S4.5:** S4f.11 — per-interaction-class acceptance→execution latency numbers on device (Phase 2);
the three-consecutive-pass campaign with sealed bundles for both legs (record 1 of 3, overdub 1 of 3); and the

## S4f.11 — interaction latency by class, measured (Phase 2)

The measurement now has an owner (`hitl.interaction_latency`, CLI `scripts/hitl_interaction_latency.py`) and the
numbers are device-derived: both ends of every pair are device rows, a family's own event time is used where it
carries one (`stop_state`'s `at_us`, never the ring's flush prefix — the two are 169.5 ms apart on this leg), pairs
are bounded by their own interaction, and a class the capture does not exercise is a named gap. From
`captures/s45-odleg1a-20261002-163640-od` (fresh boot, image `ae591d95…`) and `captures/s4f20-odundo-10-3`:

| Class | Measured | Declared (Revision 17) | Reading |
|---|---|---|---|
| Stop overdub | **106 µs** / **87 µs** | microseconds (same service turn / next playhead boundary) | within budget |
| Record / overdub start | **103 µs** | prewarm-bound; under the arm deadline (no figure declared) | informational |
| Switch — route recognised | **177.7 ms** | S4b granularity 16–31 ms (today: one bar = 2,000 ms) | exceeds the forward target → event for S4b |
| Switch — capture handoff | **645.2 ms** | same | exceeds; this is the L5 path itself |
| Undo / redo | **47 ms** / **52 ms** (warm root) | ~1–2 s when the root is cold | warm case measured; cold is open |
| Load loop | **1.554 s** dispatch / **1.673 s** complete (own slot) | seconds | within "seconds"; the 64-slot boot burst's 51.5 s max is the restore queue |

Three exceedances are reported as events with their owner named and **no budget was adjusted**. **Open, and why S4f.11
is not checked off:** cold-root undo/redo has no device number (no registered scenario drives a cold reconstruction —
the recorded undo runs right after the interaction that created its root), and the switch class's Revision-17 subject
is the *queued playback* switch, which this leg does not drive. Full matrix, method and caveats:
[`LATENCY.md`](LATENCY.md).

**Still owed for S4.5:** the three-consecutive-pass campaign with sealed bundles for both legs (record 1 of 3, overdub
1 of 3), the cold-root latency route, and the closeout bookkeeping (tasks.md checkoff, design/evidence refresh).

## Phase 3 — the three-pass campaign: one sealed pass, then a device-side blocker (2026-10-02)

The campaign needs one boot and three **consecutive** passes (`S4F-CLOSURE-PLAN` §5), so the repeat attempts could
not reboot. That required a checkable substitute for the oracle's boot-banner gate, so the oracle gained
`--boot-anchor <capture-or-run-dir>`: a repeat is verdict-grade only when the anchor's capture carries the banner,
both name the same firmware image, this capture holds **no** device restart row, and the device clock continues past
the anchor's last stamp — taken modulo the 32-bit `micros()` counter, so a repeat measured after a wrap still links
while a capture that leads with drained rows is conservatively refused. `BootAnchorTests` **11/11** pins each part on
its own (a link that accepts one while another fails would be weaker than the banner it replaces), and reverting the
link red-proves the class. `device_time.first_row_clock_us` is the owner-side half.

**Pass 1** (fresh boot) is Phase 1a's sealed pass: `captures/s45-odleg1a-20261002-163640-od`,
`ACCEPTED_SEQUENCE_PASS` with a replay-verified bundle.

**Attempt 2** (`captures/s45-campaign-20261002-171929-od2`, same boot — the device clock reads 2570.5 s against pass
1's 62.4 s) reaches preparation and then **the accepted record stop never executes**:

| Step | Row |
|---|---|
| clear slot 5 | `MIDI: Clear selected slot 5` (2572.774 s), `DISP,4,STOPPED,0,…` |
| arm + record | `DISP,4,ARMED`, `ST,Track,ARMED,RECORDING` + `RECA,4,0` (2574.014) |
| commit stop **accepted** | `RECS,stage,record_stop,14,…,entered` (2579.057993), `RECS,stage,finalize,108,103,…,accepted_wait` (2579.058085) |
| … and then | **no** `RECS,stop*` and **no** retirement, with the slot still recording: `DISP,4,RECORDING,1912,32,0,16,32,0,0,3,16` (2579.106) |
| host verdict | `seed_b_committed=false` → `PREPARATION_FAILED` (preserved; the count resets) |

The interval stayed **live**, so the device carried an unreleased stop obligation into the next attempt.

**Attempt 3** (`captures/s45-campaign-20261002-172102-od2b`) cannot prepare at all, and names why:
`PERS,drain,transport_active,256,256,13,0,2339,39936` — 39,936 bytes of save owed whose drain refuses to run while
the transport is active — so its scoped clear is refused four times (`Clear refused: deferred save temporarily
blocked by transport_active; slot left untouched, save still owed`) and the run ends `SHUTDOWN_INCOMPLETE_TIMEOUT`.
The host's own clean-shutdown drains do not retire the obligation.

**What this means.** This is the acceptance's own subject, on hardware: an **accepted** stop that never executes,
leaving the interval live, the save owed and the following preparation refused. It is not a host timeout artefact —
the device emits no terminal row while the slot keeps recording, and the state survives the attempt and its
shutdown. The campaign is blocked at 1 of 3 passes with the count reset, and further repeats are pointless until the
device state is cleared: that needs a recovery (transport/board reset), which the plan deliberately does not allow
the harness to do automatically for repeat qualification.

**Next decision (owner):** either (a) recover the board and re-attempt, to see whether the stall is specific to a
long-lived boot (~43 min uptime, a slot whose payload had just been removed by DEC-110, and a save already pending),
or (b) treat the un-retired accepted stop as its own S4.5 blocker and investigate it first — its owners are the
record stop's deferred finalization and the persistence drain's `transport_active` gate, and the trigger is not yet
isolated. (a) is cheaper; (b) protects the acceptance.

**Campaign bookkeeping:** pass 1 sealed and preserved; attempts 2 and 3 failed with their rows preserved; the
consecutive count is reset per the plan's rule, so the od leg owes three fresh consecutive passes.

### Reproduction on a fresh boot, and where the stop is being refused (2026-10-02)

The reboot the owner authorized cleared the stuck state (pass 1 of `captures/s45-campaign3-20261002-172937-od1` is a
fresh-boot `ACCEPTED_SEQUENCE_PASS`: both-slot content parity, timing clean, teardown `complete`, L1–L7 +
`overdub_interval_identity` + measured window green, sealed bundle). The **next** attempt on that same boot
(`captures/s45-campaign3-20261002-173113-od2`) failed in exactly the same place as the earlier repeat — so the stall
is **not** an idle-time or 43-minute-uptime effect. It reproduces on the second preparation of a boot, ~2 minutes
after a clean pass:

| Row | Device time |
|---|---|
| `MIDI: Clear selected slot 5` (preparation clear) | 99.989 s |
| `PERS,work,…,LoopPersist,slot:7:0,…,done,ok` (the clear's own save) | 100.808 s |
| `Transport started` | 101.056 s |
| `DISP,4,ARMED`, `RECA,4,0`, `ST,Track,ARMED,RECORDING` | 101.0–101.1 s |
| `Transport stopped` (the host's commit press) | 105.996 s |
| `PERS,request,0,135168,135168,already_pending` | 105.996 s |
| `RECS,stage,record_stop,14,…,entered` | 105.963469 s |
| `RECS,stage,finalize,107,103,…,accepted_wait` | 105.963560 s |
| slot keeps recording: `DISP,4,RECORDING,1856,…` / `1860,…` | 105.998 / 106.017 s |
| **no** `PERS,drain` row anywhere in the capture; take never retires (capture ends ~135.4 s) | — |

`accepted_wait` is emitted **only** on the denial path of `Track::stopRecording`
(`src/Track/TrackCaptureStopCommit.cpp`): `beginDeferredRecordStopForSlot` returned false, the stop was latched as an
accepted obligation (`latchAcceptedStop`), and DEC-067's rule took over — "the interval keeps recording … and the
deferred drive admits the retained obligation exactly once when the finalization owner frees". Here the owner never
freed.

`Track::beginDeferredRecordStopForSlot` (`src/Track/TrackOverdubLifecycle.cpp`) has nine `return false` guards, and
the two that a *second* cycle can newly trip are exactly the ones that name a resource the previous cycle still
holds: **`runtime->captureStopOwner != nullptr`** (the slot's finalizer still owns an interval) and
**`!runtime->canonicalCaptureOutputAdmitted()`** (the private prepared output is still not admitted). Both are
consistent with the other half of the evidence: a 135,168-byte save bundle sat `already_pending` at the stop edge,
with no drain slice at all — and `classifySyncDrainVerdict` (`src/PersistenceSyncDrainBudget.cpp`) blocks a drain
whose gate is `transport_active`, i.e. **while a capture is live**. The take stays live because the stop was denied,
the drain is refused because the take is live, and whatever the previous cycle still holds is never released: a
circular wait between the record-stop admission and the persistence drain.

Which guard actually fires is not yet established — the admission logs no reason, only the caller's `accepted_wait`.
Two honest next steps, in increasing cost: **(i)** name the failing guard at the admission (a diagnostic-only firmware
change — one reason string on the denial path, the same "named, never silent" rule the rest of this work follows),
then re-run the repeat to read it; or **(ii)** write the native two-cycle reproducer first (record → stop → retire →
save left pending → record again → stop), which can discriminate the guards without a device. No firmware change has
been made; the campaign stays blocked at 1 of 3 od passes.

### (1) The refusal is named on the board: `raw_range_unsealable` (2026-10-02)

`beginDeferredRecordStopForSlot` now names each of its refusals, and the caller logs the name on the same edge as
`accepted_wait` (`RECS,stage,deny_reason,…,<name>`). The change costs **64 B of flash and no RAM1**
(`variables 96,192` unchanged; `code 392,008`, padding `1,208`, floor ≥840) — the name lives in one file-scope slot
rather than a `Track` member, because `Track` is the eight-way DTCM array whose size floor is asserted (the first
attempt added 8 B and the S3.0 static assertion refused it, correctly).

Fresh boot, upload, pass 1 `DROVE_LEG`, then the repeat that has failed three times before
(`captures/s45-deny-20261002-174528-od2`) — and the board says exactly which predicate refused:

```
#CAP,85084583,RECS,stage,deny_reason,116,0,4294967295,4294967295,32,0,raw_range_unsealable
#CAP,85084601,RECS,stage,finalize,134,131,4294967295,4294967295,32,0,accepted_wait
```

So the refusal is **not** the finalizer owner, the canonical output, the job ring, the held notes, the slot/interval
identity, the loop pool, the capture phase or the state transition: it is
`if (hasRawExtent && !loop.capture.store.canSealCaptureRange(job.rawRange))` — the **capture event store cannot seal
the second take's raw range** in that slot. The interval therefore keeps recording (invariant-31 latch), which keeps
the capture live, which holds the persistence drain's `transport_active` gate, which leaves the save owed — the
cascade this evidence recorded earlier is a *consequence*, and the pending-save signature was a symptom rather than
the cause.

**Why that is decisive for the campaign:** the second preparation of a boot ends with a store that cannot seal a new
take in a slot it has already recorded, cleared and re-armed — while the pipeline guards the earlier paragraphs
suspected are all satisfied. The first cycle on a boot works; every subsequent one in that slot hits this. The
campaign cannot proceed past one pass per boot until it is understood, and the next step is a **store-level**
investigation: which claims `Loop::capture.store` still holds after the clear/retirement, and whether
`canSealCaptureRange`'s requirement is a contiguous run, a reserved headroom, or an owner-pinned range. The native
two-cycle fixture (`test_second_record_cycle_is_admitted_with_every_guard_precondition_pinned`, 59/59) asserts the
ownership preconditions and is admitted, so the store's *claim* state is where the difference between the harness and
the board must live.

**Verification of this slice:** native `test_shared_capture_finalization` **59/59**; capture build SUCCESS with the
RAM1 budget line unchanged; upload SUCCESS; fresh-boot pass 1 `DROVE_LEG`; repeat named as above. No host tooling
change was needed beyond reading the new row.

## The `raw_range_unsealable` root cause is fixed and device-verified (2026-10-02)

**Root cause (named, one site).** `Track::stepDeferredRecordRetirement` retired the Record job by clearing
`job.ownsRawRange` **without** releasing the sealed source extent or re-arming the descriptor. The extent therefore
never returned to the pool, and the descriptor could never be sealed again — so the next Record stop in that slot was
denied by `loop.capture.store.canSealCaptureRange(job.rawRange)` (`raw_range_unsealable`) and the take kept
recording. The overdub route's retirement does call `retireJobCaptureSourceGrain`; the record route did not.

**Fix (one invariant).** A retired Record interval must return its sealed extent to the pool **and** leave its
descriptor re-armed: the retirement releases one grain and re-reserves the descriptor (`reserveTail`), naming a
refusal instead of swallowing it; `Track::serviceCaptureStopDescriptorReservations` — called from
`processDeferredIdleMaintenance`, one bounded grain per idle turn — finishes any descriptor that a route left in the
third state (released, or never reserved, and not re-armed), and reports `descriptor_rearm,ready|deny`. The service
distinguishes it from the two healthy states (`ready()`, and `CaptureRange::reservedEmpty()`), because without that it
re-reserved a healthy descriptor every turn (measured: 48,737 rows in `captures/s45-fixed-20261002-180416-od2`).

**Verification.** Native `test_shared_capture_finalization` **60/60** with the new fixture
`test_a_record_retirement_returns_its_extent_and_rearms_the_descriptor`, which pins the leak *before* the service runs
and then the recovery, and the two-cycle behaviour on top; reverting the retirement fix red-proves it (the fixture
fails on "a retired record interval must return its sealed extent to the pool", 59 others pass) and the restore is
byte-identical (`sha256 7238d9cc…`). Native `test_continuous_capture` + `test_capture_line_tier` green (185/185 in the
focused trio). Capture build SUCCESS with the RAM1 line unchanged (`variables 96,192`, `code 392,008`, padding 1,208);
upload SUCCESS.

**On device, the repeat works now.** With the fixed image: `captures/s45-c4-20261002-180740-od1b` (fresh boot),
`…-180856-od2` and `…-180957-od3` — **three consecutive `DROVE_LEG` on one boot**, each stop's `RECS,stage,finalize`
reading **`deferred`** (admitted) where the previous image denied `raw_range_unsealable` with `accepted_wait`, and
**zero** `descriptor_rearm` rows in the steady state (the retirement re-arms inline; the service is the safety net).
The stall that blocked Phase 3 is gone.

## Campaign evidence method: the anchor read the wrong boot (host; FIXED, verified)

The three-pass campaign needs per-pass content parity, which is a **device read between passes**, and the attempt after
the first sealed pass was refused with `the device clock did not continue`. The earlier reading here -- "a between-pass
read rebooted the board" -- is **refuted** by that attempt's own evidence, and the refusal was our own oracle's:

  * `captures/s45-fixcamp-20261002-181255-od2` carries **no** restart row anywhere, and its drained prefix still holds
    rows from **0.119 s** of the anchor's own boot: the ring survived, so the board was never reset;
  * its device advance is **64 919 717 us** against **65 348 073 us** of wall time between the two captures' closes --
    0.43 s apart, one continuous boot;
  * the refusal came from the **anchor's time base**: pass 1's capture leads with the *previous* boot's tail
    (`#CAP,194649907…`, glued to `#CAP,2167003,DIAG,boot_reset,crash=0`), so a maximum over every row read 194.65 s
    while the capture's own boot ended at **104 510 708 us**;
  * the attempt side was read with `first_row_clock_us`, whose minimum came from a **`#DMA_XFER`** row
    (`#DMA_XFER,119333…`) -- a family that stamps its *own* subsystem base, not `micros()`.

**Fix (host: `device_time` + the oracle).** A boot link now places the anchor on the boot its capture **ended** on:
`device_time.current_boot_last_clock_us` reads the newest `#CAP` stamp after the last restart row (`cap_row_clock_us`
ignores the other families), and `BootAnchor` carries that stamp together with the capture's own close time
(`Path.stat().st_mtime`). A repeat is linked only when it carries no restart row, names the same image, and its
device-clock advance matches the elapsed wall time (`CLOCK_WALL_TOLERANCE_US = 10 s`), which a reset -- restarting
`micros()` at zero -- cannot satisfy. `first_row_clock_us` is removed with its claim: a capture's *leading* rows cannot
place it, because a drained prefix is normal (the refused repeat opened at 119 333 us and its live window ran to
169 430 425 us).

**Verification.** `BootAnchorTests` **15/15** and `test_hitl_device_time` **18/18**; the fix red-proves in both halves
(a maximum over every row fails the segment test: `104510708 != 194649907`; switching the wall comparison off fails the
reset test), and both files restore byte-identically (`sha256 76e894f0…` / `bb320717…`). Re-grading the refused bench
attempt with the fixed oracle links it (`fresh_boot: ok`, "closed 64919717 us after the anchor on the device clock
against 65348073 us of wall time"), recorded beside the original as
`acceptance-review.anchored-after-fix.json`. The interleaved parity read is exonerated: it did not reset the board.

## The remaining non-pass: the successor serialization, and the two gate rulings it produced (open)

With the link fixed that attempt is still not a pass, for two reasons that are its own evidence:

1. **L5 -- the successor was serialized.** `reentry_window_open,3,1722,1536` at 154 906 363 us, the host's switch
   gesture **172 ms into the window**, A's release at 155 097 914 us (`canonical_release,3,0,1536,8`: the released
   interval held **0 events**) and B's admit only at 155 660 602 us, i.e. 563 ms *after* the release. The sealed pass
   on the same image overlapped instead (window open 89 575 600 us, admit 90 391 514 us, release 91 156 745 us, whose
   released interval held its 32 events). Per **DEC-111 — a serialized acceptance run is NOT_EXERCISED** this is a
   non-pass that breaks the streak, so the campaign needs attempts whose window carries events to overlap with.
2. **Content parity -- an active Hide, and the guide's own scoping.** The dump declares `record#0=31, overdub#0=32,
   effective#0=61`, and the effective group is missing exactly `(1238, On, 71)` and `(1250, Off, 71)` from `record#0`,
   which the overdub's own `(1225, On, 71)`/`(1241, Off, 71)` covers: `classifyEditSessionInteraction` +
   `resolveConstrainedGeometry` **Hide** the covered record note (`src/EditManager/*`), so `effective` is storage with
   the committed overlay applied (`LoopPasses::materializeToEventVector` -> `applyActiveEditPasses`). The guide states
   the invariant as `count(record) + count(overdub) == count(effective)` **"for this procedure's material"**
   (`docs/Guides/HITL_CONTENT_VERIFICATION.md:84-87`), which holds while no Hide/Shorten is active; the probe's raw
   `storage == effective` (`scripts/hitl/mixed_content_report.py:66`) therefore fails a slot that carries a resolved
   overlap even though nothing was lost. The dump exposes only `record`/`overdub`/`effective`, so the host cannot name
   the overlay from the dump alone. `record#0` also holds one unpaired `(79, On, 63)` (31 rows rather than 32): the
   guide's loop-boundary residual covers a *stored* terminal Off with `NoteId = 0`, not an absent one, so that row is
   recorded as an open question rather than explained.

Both are gate-definition questions and are recorded here rather than decided: either the reproducer is made
deterministic so the guide's invariant holds as written (the seeded loop length differed between the two boots --
2304 ticks in the sealed pass, 1536 in this one -- and the host sends its overdub notes clock-timed, where one clock
of jitter is 20 ticks at 480 PPQN), or the guide gains a third *expected residual* (a resolved Hide/Shorten, with the
device exposing the overlay in the dump so the difference is **named**, never silent) and the parity reader compares
against the resolved view.

### Resolution (2026-10-02, owner rulings; DEC-112)

Both questions above were put to the owner and both were answered, and the first is already implemented:

1. **The content gate validates the deduction as a positive pass** (owner: "it needs to validate the hidden notes
   deduction as a positive pass, not a non-pass as this is a logical outcome"). New owner
   `scripts/hitl/content_deduction.py` (cites the Hide/Shorten rule; re-derives nothing) + the mixed report's verdict
   split into `slot_passes`/`verdict_from_slots`: every effective row must be a storage row or a storage row whose
   NoteOff moved to a coverer's NoteOn − 1, and every missing storage row must be covered by a later producer's
   same-pitch row that is sounding there (wrap-aware; a note with no stored NoteOff sounds a full loop). Deductions are
   named in the report's `deductions`; an unexplained remainder still fails, named in
   `unexplained_manifest_differences`; the raw comparison stays reported as `storage_matches_effective` beside
   `resolved_storage_match`. `scripts/test_content_deduction.py` **15/15**, red-proved in both halves (an
   `accounted = True` mutation fails three validator tests; `slot_passes -> True` fails three verdict tests), both
   files restored byte-identically (`sha256 90d340fd…`, `223487f0…`). On the live device, re-reading this attempt's own
   slots reads `pass: true` with
   `deductions: ["slot 4: record#0 note 71 (On 1238, Off 1250) hidden by overdub#0 On 1225"]` and an empty
   `unexplained` list (`content-parity.resolved-deduction.json`), and re-grading beside the original reports reads
   `content_parity: ok` with the boot link `ok` (`acceptance-review.anchored-deduction.json`) — the serialized
   successor is this attempt's **only** remaining non-pass reason.
2. **The reproducer must be clock/response-driven** (owner: "don't rely on raw midi timings in the run, but use midi
   clock or valid responses from the serial"). The observed shape variance is the target: the seeded loop length
   differed between the two boots (2304 vs 1536 ticks), A's window collapsed to 192 ms with a 0-event release
   (`canonical_release,3,0,1536,8`) while the host pressed the switch at the same offset in both attempts (172 ms into
   the window). The flow change — wait for the device to name the predecessor's window on its own leg interval and
   start the successor inside it, with take/seed lengths following the clock rather than sleeps — is **owed**, not
   done; no window, timeout or budget is widened for it.

**Campaign status:** the od leg has **1 sealed pass** on the fixed image (`…-181057-od1`); the second attempt is
preserved as a non-pass (serialized successor) with its post-fix re-grade, its resolved-deduction content report and
both reviews beside it. The consecutive count resets. No task checkoff; S4.5 stays open. Also owed: the
clock/response-driven flow change, the record leg's campaign (1 of 3) and S4f.11's cold-root undo/redo route.

### Ruling 2 measured on device: the press cannot be placed on the window's *ring* row (2026-10-02)

Ruling 2 was implemented as specified -- the successor's start waits for the device to name the predecessor's
finalization window (`DIAG,reentry_window_open,<A>,…`) and presses on it -- and **measured on the bench**, and the
measurement says the row cannot carry that press:

  * `captures/s45-flow-20261002-191802-od1`: the device *did* open the window,
    `#CAP,4060262832,DIAG,reentry_window_open,3,1752,2304` (device **4060.262832 s**), and it closed 1.194 s later
    (`#CAP,4061457090,DIAG,canonical_release,3,32,2304,8`, device **4061.457090 s**, with its 32 events);
  * but that row is **ring-buffered**: it reached the capture file ~1,240 lines *after* the switch's own directly
    printed row (`[4060.431] [INFO] Loop 5: Switch gesture from slot 4`) and only in a later flush, after the window
    had closed. The flow's wait therefore timed out and refused the leg by name
    (`PREDECESSOR_WINDOW_NOT_NAMED`, exit 3 -- preserved as operational evidence). The leg itself was healthy: A's
    overdub carried its 16 clock-timed notes (`confirmed: true`) and the teardown reached verified persistence idle;
  * so the **directly printed** switch acknowledgement is the only real-time signal, and the press this flow already
    makes -- immediately after it -- is the earliest the arrangement can make. A press placed on the ring row cannot
    be inside a 1.2 s window, let alone a 192 ms one.

**Conclusion (the ruling's own alternative, now measured).** The overlap link's failure on
`captures/s45-fixcamp-20261002-181255-od2` was **not** a timing-source defect in the arrangement: that attempt's
window was **192 ms** with a **0-event** release (`canonical_release,3,0,1536,8`) while the device's own admission
followed its switch gesture by ~600 ms, so the successor serialized device-side. In this run -- same image, same
commands -- the window was 1.194 s with a 32-event release. The productive next step is therefore a **device
investigation of the short, 0-event predecessor window**, not more host pacing. The flow was reverted to the
verified behaviour (its press is already the earliest; the dead duplicate `od` branch in `leg_gestures` was removed
in the same pass), and two tests now pin the measurement: a run with no window row in the capture still drives, and
a stale (seed) window row places nothing. Verification: accepted-sequence **187/187**, scenario **40/40**,
`git diff --check` clean.

**Guard cleared on that evidence.** `captures/.device-shutdown-incomplete.json` (latched by the pre-fix stall at
17:23, `incomplete_timeout`, `drains: 0`) was cleared after this run's own teardown recorded
`2 stop+drain cycle(s) reached verified persistence idle` -- the device-known-good condition the guard's message
asks for. Nothing on the device was deleted or rewritten.

### The short-window anomaly, root-caused and reproduced — and its fix is *not* the host's pacing (2026-10-02)

**Mechanism (code-verified, delegation-traced).** The leg's stop arms its release marker from `stop.frozenGeneration`
(`src/Track/TrackPreparedStopCommit.cpp:223-225`, `src/Track/TrackOverdubLifecycle.cpp:1943-1945`), which the overdub
route takes from `staging.captureGeneration` (`TrackOverdubLifecycle.cpp:912` -> `PendingStopAdmission::begin`,
`TrackPlaybackRuntime.cpp:159`). A **mid-interval loop-end wrap** calls `CaptureAdmissionStaging::reset()` and reseeds
`captureGeneration = playbackGeneration` (`src/Track/TrackOverdubWrap.cpp:145-147`), and `playbackGeneration` is **0**
until a merged-MIDI invalidation bumps it (`include/Track.h:667`,
`src/Track/TrackPlaybackWindowBuild.cpp:566`). So a leg that crosses its loop end leaves the staging **empty** (the
wrap committed its content) with a **0** generation: the stop has nothing owed, the finalization window collapses to
the next idle turn, and the successor's admission -- ~600 ms behind its own switch gesture -- lands *after* the
release. `TrackPlaybackRuntime.h:150` declares 0 as "no admitted interval" and nothing enforces it (already recorded
as a conflict in the change's `ARCHITECTURE-REVIEW.md`), and no gate refuses a 0-generation transfer because it is 0.

**Reproduced, with the wrap as the differentiator.** Sealed pass `…-181057-od1` (generation 32, window 1.581 s) and
the earlier healthy run (generation 32, 1.194 s) never wrapped inside the leg; the serialized
`…-181255-od2` and this session's `captures/s45-clock-20261002-194915-od1` both have
`Overdub wrap committed @ tick 1536` **before** the switch (`…-od1`: leg start tick 677 at 5938.071 s, wrap at
5940.337 s, switch at 5941.173 s), a **0**-generation release
(`DIAG,canonical_release,3,0,1536,8` at 1646.231350 s) and a **204 ms** window
(`DIAG,reentry_window_open,3,1784,1536` at 1646.026891 s) -> L5 serialized, `SETUP_INCOMPLETE`.

**The host pacing change stands, but it is not sufficient.** `seed_take` now paces the take's **length** on the
device's own clock (one bar = four beats of clock; `hold_clocks` keeps the take open to the span's boundary) instead
of `bars * 2.0` seconds of wall time, and a missing or too-short clock is a **named** refusal
(`PreparationFailure`). On device the events confirm it: `seed_a_notes … "span_clocks": 192, "hold_clocks": 96,
"confirmed": true` with the device's own clock (`bpm 120.0026`, `jitter_ms 0.533`). **But the take's loop still came
out 1536 ticks (1.6 s) for the slot A seed and 768 ticks for slot B** -- i.e. a 3840-tick paced span produced
1536/768-tick loops, so the loop's length is *not* set by the span the host paces. That is the sharpened question and
the next investigation: which owner assigns `loopLengthTicks` for these takes (the transport stop's
`RECS`/`stopToStopped` path, `Loop::finalizeCapture`-equivalent, or a quantisation to the *previous* grid), and
whether the seeded take should instead be shaped by a device-named length rather than by host pacing.

**Also validated in passing:** the fixed boot link correctly refused this run's anchor with "the device clock reads as
a restart" -- the device had rebooted since the anchor (the run's device clock is ~1646 s against the anchor's
104.5 s), i.e. the anchor fix catches a real reboot instead of mis-reading a drained prefix.

Verification for the host change: accepted-sequence **190/190** (three new `ClockPacedTakeLengthTests` pin the span
boundary and both refusal shapes), `git diff --check` clean.

### The od leg's three-pass campaign is closed on one boot (2026-10-02)

**Result: three consecutive `ACCEPTED_SEQUENCE_PASS` on one boot**, each graded with the content owner, the timing
owner and the boot link, each sealed as a bundle that replay-verifies:

| pass | run | device clock | A's window -> release | B's admission | verdict / replay |
|---|---|---|---|---|---|
| 1 (fresh boot, `--boot-reset`) | `captures/s45-camp4-20261002-200100-od1` | banner | `reentry_window_open,3,1515,1536` -> `canonical_release,3,20,1536,8` | `reentry,admit,1827,4,4,1` | `ACCEPTED_SEQUENCE_PASS`, replay 0 |
| 2 (anchored to pass 1) | `…-od2` | continues | `,3,1490,1536` -> `,3,20,1536,8` | `reentry,admit,1804,8,4,1` | `ACCEPTED_SEQUENCE_PASS`, replay 0 |
| 3 (anchored to pass 1) | `…-od3` | continues | `,3,1440,1536` -> `,3,18,1536,8` | `reentry,admit,1712,12,4,1` | `ACCEPTED_SEQUENCE_PASS`, replay 0 |

Every non-ok check list is empty, `L5_successor_started` reads "0 after A's release edge" in all three (the successor
admitted **inside** the predecessor's window), content parity is exact per slot (`record#0 + overdub#0 == effective#0`:
52/52, 52/52, 50/50), and every release carries a **non-zero** generation -- against the two earlier failing attempts
that read generation 0.

### Correction: the pacing *did* work, and the wrap is a **phase** effect

The earlier note in this file ("a 3840-tick paced span still produced 1536/768-tick loops, so the pacing is not
sufficient") rested on a wrong tick assumption. The device runs at **192 ticks per beat**, not 480, which two
independent readings confirm (`…-181057-od1`: 1,626 ticks in 4.236 s; `…-clock-20261002-194915-od1`: 859 ticks in
2.266 s -- both 379-384 ticks/s = 2 beats/s at 120 BPM). At that rate the clock-paced seed came out at **exactly its
intended two bars** (1536 ticks = 8 beats = 4.0 s), i.e. `seed_take`'s clock pacing is correct and the `bars * 2.0`
wall span was the defect it replaced.

What actually decides the wrap is the leg's **phase**: the leg needs ~6 beats (a 4-beat burst plus the host's measured
~2-beat overhead), and the room left in the loop at the leg's own start tick was 8.5 beats in the two healthy runs and
4.5 in the two that wrapped. So `_run_plan` now sizes the burst from the **device's own numbers** -- the loop's length
from its `RECS,stopToStopped,…,<final>,…` row, this leg's `@ tick` phase from its `Overdubbing started @ tick` row, and
the run's own `bars` to convert ticks to clocks -- sends as many notes as fit, and refuses **by name**
(`NO_ROOM_IN_PREDECESSOR_LOOP`) rather than sending a burst into a wrap. On device the budget reported
`loop_ticks 1536, start_tick 665, notes 10`, the leg did not wrap, and the window came back at 1.593 s with generation
20 (`captures/s45-room-20261002-195932-od1`).

Verification: accepted-sequence **194/194** (four new tests pin the measured budgets -- 14 notes at 8.5 beats of room,
10 at 4.5, a named refusal at 36 ticks of room -- the modulo phase, and both row parsers); `git diff --check` clean.
