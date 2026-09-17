# Current work (implementation scope)

> **From now on, prove audible content with the `hitl-content-verification` pipeline, not the
> base-run verifier.** `host_midi_hitl.py run --preset base` is still the content-creator, but its
> own playback verdict is not the check: the managed harness aborted four consecutive runs on
> setup-state races (`successor-wrap-48/49/50`: `transport still active after stop; clear blocked`
> then `Record stop did not reach PLAYING`, each with `loop length 0` and no host note ever reaching
> the device) while the device was fine. What actually proves playback is
> `.agents/skills/hitl-content-verification/scripts/dump_and_capture.py` +
> `verify_playback_content.py`, which records the device's MIDI output and compares it to the
> committed inventory row for row. DEC-071 is device-verified that way; the "Result:
> INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)" that the base report still prints is a setup-oracle
> verdict and is not evidence against the content.

> **Implementation scope:** the authoritative scope is
> [§ Now implementing](#now-implementing). Everything above that section is a
> reverse-chronological session journal (newest first); read it for evidence, not
> for scope. `AGENTS.md` step 2 points here.

**2026-09-17 OpenSpec 5.12 / D8 LANDED and device-verified: a staged capture pass is owned by its
interval (DEC-072), and the `CompanionHistory` checkpoint that had been red for two days is green.**
The 5.5 blocker was that one `Loop` scalar, `pendingCapturePass_`, served two intervals: a deferred
predecessor staged its pass at `PrepareTransfer`, a successor's `beginCapture` discarded it
unconditionally, the predecessor's `beginPreparedCompanionHistory` was then refused forever by its own
`!hasPendingCapturePass_` precondition, and its job held `captureStopOwner` and the frozen role for
the rest of the session. Guarding the discard only preserved the `AlreadyPending` collision at the
successor's own staging, so the fix moves ownership instead:

- `CaptureStopJob::pendingPass` + `ownsPendingPass`; the job stages into the Loop slot and
  immediately takes the receipt (`Loop::takePendingCapturePass`, chunk-list swap, non-allocating) in
  the same non-yielding `PrepareTransfer` grain. `beginCapture` keeps its unconditional
  producer-local clear, and a deferred job never depends on the slot afterwards.
- `Loop::transferCapturePass` / `canTransferCapturePassWithoutAllocation` /
  `commitPreparedCapturePassAtomic(transferred, pass)` plus
  `commitPreparedCaptureAndCompanionsAtomic(transferred, owner, pass)` for a specified receipt; the
  Loop-slot forms are thin wrappers, so the live path is unchanged.
- `PreparedCompanionHistory::capturePass` is bound by address at
  `beginPreparedCompanionHistory` and validated by `preparedCompanionHistorySourceCurrent`, so a
  successor can neither destroy nor rebind the predecessor's verification source. A caller that names
  no receipt still resolves the Loop's own staged pass (the live producer path and fixtures); a
  deferred job names `&job.pendingPass` unconditionally at its one production call site, so a missed
  ownership transition denies instead of silently reading a successor's receipt.
- Reader sweep in the same commit: the undo identity in `stepDeferredUndoCollection` and the two
  `canPublish` sites; `requiresBridgeRefresh`; `commitStopped[WithUndo]` / `commitWithUndo` /
  `commitSupersededCausalBase`; `stopPathChunkRefCount`/`stopPathEventCount` and
  `collectStopPathStorageStats`/`logOverdubStopStage` (the deferred debt-probe call site passes
  `Track::ownedPendingCapturePass()`); `#STOP_HISTORY` also reports `pass_id`; and the job releases and
  clears its receipt at retirement.
- RAM1: `CaptureStopJob` now owns a `PendingCapturePass`, so the implicit
  `Track::CaptureStopJobs::CaptureStopJobs()` became non-trivial and, emitted into ITCM, cost one
  32 KB FlexRAM bank (`locals 6,400 -> -26,368`). Defined out-of-line and FLASH-resident, the build is
  back to RAM1 variables 91,904 / code 425,816 / padding 168 / **locals 6,400**.

Evidence. `ONLY_COMPANION=1 pio test -e native -f test_continuous_capture` green (the former red
reproducer); `REENTRY_RED=1 pio test -e native -f test_continuous_capture` 57/57; full native 2,221
cases / 1 skipped / 0 failures; `test_playback_prewarm` 173/173. New
`test_simultaneous_interval_owned_staged_passes` asserts the invariant directly: job A owns pass A
with the Loop slot empty, B stages a distinct pass (different identity, chunk storage and session
index), A's transfer appends exactly one canonical pass by pointer move with the canonical row vector
unchanged in capacity and address, B's receipt is untouched, and B then consumes B exactly once.
Device, managed HITL `base` (8-bar record / 2-bar overdub, temporary `d8-ownership` stimulus variant),
[`captures/successor-ownership-06`](../../captures/successor-ownership-06/session_20260917_230432.log):
**`Result: PASS`**, `overlap=True denied_stop=True retirement_ok=true` — 50.903 s A's stop admitted;
51.392 s B starts 1,122 ms before A's release; 51.935 s B's quick stop refused
(`Overdub stop refused: stop_job_pending phase=9 dest=0`) with B still recording; 52.514 s A
`Reentry released` while B stays live; 52.979 s B's own stop admitted and 52.996 s B's job retired.
0 `FATAL` / `#MEM_DENIED` / `#MEM_EXT_REFUSED`, no `stop_job_pending phase=7`, no
`companion path stalled`, `overflow_free=true`, display `overdub_ok`/`transport_ok` true.

Harness correction in the same change: `_verify_successor_reentry` accepted only the DEC-067 denial
wording (`Overdub stop deferred: deferred/frozen admission role is occupied`), so every
current-firmware probe reported `successor_reentry_stop_not_denied` while the device log carried the
refusal (`successor-ownership-01`/`-03`). It now accepts both wordings (`REENTRY_DENIAL_MARKERS`) with
a regression test; `scripts/test_capture_transitions.py` 42/42. OpenSpec 5.5 stays open for its own
remaining work (the complete parameterized checkpoint set, and promoting
`test_adversarial_reentry_at_a_companion_checkpoint` into the default gate — it is still behind
`REENTRY_RED`).

**2026-09-17 multi-pass overdub playback is device-verified (DEC-071), and the base-run harness
was the thing that was broken.** The user's report was "only the wrapped part of the overdub plays";
the shape is a *multi-pass* overdub (8-bar record, 8-bar overdub starting at bar 3, so the wrap
commits one pass and the stop commits a second). `Loop::tryGatherPublishedSnapshotEventsInWindow`
enumerated queued passes but not **wrap-journaled** ones, so the wrap pass was in neither the
published root nor the stale-root overlay once its session closed -- the display resolved it from
`passes`, playback never emitted it. Fixed in `2b948d2a`.

- Native: `test_stale_root_overlay_keeps_a_journaled_wrap_pass_after_session_close` fails without
  the fix (`Expected 1 Was 0`); full native 2,215 / 1 skipped / 0 failures, RAM1 locals 6,400.
- Device, the reported shape (`captures/content-verify-52` + `captures/content-verify-52-playback`):
  storage `384 + 157 == 541`; **all five playback cycles `inventory_equal: true`,
  `exact_order: true`, 0 missing, 0 extra**; per-bar effective
  `{0:38, 1:48, 2:62, 3:72, 4:72, 5:72, 6:72, 7:72, 8:33}` -- the pre-wrap bars are in the audible
  output. (That record sealed at 6,912 ticks = 9 bars for an 8-bar request: the known stop-time
  quantisation slop, unrelated.)
- The base-run verifier also had two real defects, both fixed: `_prepare_deferred_save_before_clear`
  toggled the transport blind and blocked clear before the note stream (`c1456fa9`), and
  `dump_and_capture.py` opened MIDI after the serial open, which resets the board and removes the
  ports (`7e9810ee`).
- Capture observability: `DIAG,stop_grain` was 71% of a run's `#CAP` bytes and overflowed the ring,
  evicting the `snapshot_unit` rows the run is read for. It is now sampled 1-in-16 and the ring is
  1 MiB; `[env:teensy41-capture-serial-no-grain]` (`SESSION_CAPTURE_GRAIN=0`) drops the family for
  acceptance runs. A runtime serial flag was tried and reverted: it cost 4 KB of the RAM1 local
  allowance (2,304 left) even factored out as a FLASHMEM helper, so the switch is a build flag.
- **RESOLVED 2026-09-17 by OpenSpec 5.12 / D8 (DEC-072)** -- kept for the diagnosis: the OpenSpec 5.5
  `CompanionHistory` checkpoint used to be red. A predecessor paused there never re-admitted its
  companion work once a successor started, so it held `captureStopOwner` and the frozen role for the
  rest of the session -- the same wedge as the device's `Overdub stop refused: stop_job_pending
  phase=7 dest=1`. Measured then: the loop's prepared companion horizon was **unowned**
  (`preparedCompanionHistoryInFlight == false`) and parked at `Prewarmed`, so the driver's
  `preparedCompanionHistoryCanStep` was false while `beginPreparedCompanionHistory` was re-entered
  every turn; a `prewarmPreparedCompanionHistory` reset of an in-flight preparation and a transport
  command-epoch mismatch were both ruled out by measurement. The reproducer
  (`test_adversarial_reentry_at_a_companion_checkpoint`) is green now; it is still gated behind
  `REENTRY_RED=1`, and promoting it into the default gate is 5.5's remaining work.
- Root cause, located on the third turn and fixed by DEC-072: the successor's own
  `Track::startOverdubbing` -> `Loop::beginCapture` discarded the predecessor's staged pass with an
  unconditional `discardPendingCapturePass()`. `pendingCapturePass_` was one Loop scalar shared by
  both intervals, so the predecessor's companion admission (`!hasPendingCapturePass_`) was refused
  forever, the work stayed at `Prewarmed`, and the job held `captureStopOwner`. A guarded
  `beginCapture` (discard only a pass that retires the live capture) made the `CompanionHistory`
  checkpoint retire but broke the successor's own staging at `PrepareTransfer` (`AlreadyPending`),
  so the landed fix gives the staged pass an owner: the job takes it in the same `PrepareTransfer`
  grain, binds it in the companion history by address and consumes it at its own `AtomicTransfer`.
  See the DEC-072 entry at the top of this journal. Iterate with
  `ONLY_COMPANION=1 pio test -e native -f test_continuous_capture`.
- Ignored per the owner: one deterministic coalesced note-off on the 5-bar shape
  (`content-verify-51-playback`, tick 128 / ch 4 / note 78 / velocity 0).

**2026-09-17 pacing FIXED and verified; the successor device case is blocked by a firmware
guard, not the harness.** [`deadline-pacing-01`](../../captures/deadline-pacing-01/session_20260917_133833.log)
after a fresh flash: `record loop length (final ticks): 6144/6144 (raw 6380)`, **Result: PASS** -
an 8-bar request now records an 8-bar loop, with the assertion that caught the 10-bar records
still strict. Item 2 is closed.

Successor-reentry device case: the probe and its oracle are implemented and the presses now fire
from the main flow (a background thread's mido sends never reached the device - no
`MIDI Button A` line), but the device never re-enters OVERDUBBING after the probe press.
The reason is a firmware gate, not the harness: `TrackManager::startOverdubbingTrack`
(`src/TrackManager/TrackManagerCaptureQueue.cpp:212`) returns early while
`track.hasPendingDeferredOverdubStop()`, logging "overdub admission deferred while finalization
is pending". That manager/Button-A successor guard is the one task 2.3 deliberately left in
place, so the *button* path cannot open a successor interval while the predecessor finalizes -
even though the Track-level admission the native tests exercise does permit it. The probe will
be the device gate for that case the moment the guard is removed (the next RC); nothing further
is owed on the harness side.

**2026-09-17 the over-run is per-step sleep drift, not a bar-grid problem - and the BAR signal
is not available during a take.** Two measurements closed this out:

- The session log for [`bar-anchor-08`](../../captures/bar-anchor-08/session_20260917_133506.log)
  contains only **4 `BAR` rows** (bars 0-3, i.e. before the note stream starts) out of a take that
  ran 10 bars, and no `ST,Track,...,RECORDING` row either. The device/capture tier simply stops
  delivering `BAR` rows once notes flow, so a device-bar end condition can only ever see the
  first few bars - BAR-anchored pacing is not viable in this capture configuration without a
  firmware tier/emission change.
- With that ruled out, the remaining cause is host-side and measured: the wall-clock streamer
  paced with `time.sleep(seconds_per_clock)` **per step**, which adds the per-iteration MIDI send
  and loop overhead on top of the 20.83 ms interval - a nominal 16 s (8-bar) stream ran ~20 s and
  the device therefore recorded 10 bars. The streamer now paces to an **absolute deadline**
  (`start + n * seconds_per_clock`), so the total is `target_clocks * seconds_per_clock`
  regardless of per-step cost.

The device verification of that change ([`bar-anchor-09`](../../captures/bar-anchor-09/session_20260917_133556.log)) came back
`SPARSE_CAPTURE` with a capture-ring overflow, so it produced no length evidence; that is the
third time in this stretch that runs went sparse on a long-running device, and a re-flash has
restored healthy evidence twice. Re-verify the deadline pacing after a fresh flash, then run the
successor probe.

**2026-09-17 BAR probe visible at last: the device is already 3 bars in, and no BAR rows arrive
during the stream.** After making the parser prefix-tolerant and instrumenting every per-track
stats block, `bar-anchor-07` reports `record_device_bar_start = 3`, `record_device_bar_latest =
-1`, `relative_bars = 0`. Two facts follow: the device's bar counter is at **3** when the record
begins (so the host's note stream starts ~3 bars into the device's count, and a stream-relative
target of 8 lands 10-11 bars later), and the probe sees **no BAR rows at all** during the stream -
the capture snapshot the harness polls does not advance while it is streaming, so the device-bar
end condition never fires and the stream falls back to its duration. The next step is to find out
why the collector's snapshot is frozen during streaming (managed-capture follow path buffering),
or to read the BAR rows from the session log file directly; the assertion is already truthful, so
the run correctly fails `record_loop_length_mismatch` until then.

**2026-09-17 the record-length assertion was vacuous (fixed).** `_verify_record_loop_length`
accepted `actual == bar_aligned`, but the firmware's `final_length` *is* the bar-aligned
quantisation of `raw_length`, so that clause was true for any raw length and the requested bar
count was effectively unenforced - which is why an 8-bar request recording 10 bars passed
`bar-anchor-04` while earlier runs failed (it depended only on whether the raw length landed
inside the final bar). The check now compares against the request, keeping the intended one
bar of stop-time quantisation slop; the proxy-timed tolerance is unchanged. Verified on the
observed values (6,144 passes; 6,912 slop passes; 7,680 is a mismatch; proxy-timed 7,680 is
reported). With a truthful assertion, the remaining pacing fix is to anchor the device-BAR
baseline at the **record-arm** boundary: the device begins counting at the arm/transport,
about two bars before the host's first streamed note, so an 8-bar note stream still yields a
10-bar loop.

**2026-09-17 BAR-anchored stream stop landed; the record is still ~2 bars over.**
`stream_pattern_for_bars` now ends on the device's own `BAR,<tick>,<index>` (advanced by
`target_bars` from a lazily captured baseline) and both the record and overdub paths pass a
probe over the capture tail; `_latest_device_bar_index` is unit-tested (parser cases) and the
probe reports `device_bar_start/latest/relative_bars` in its timing dict. Device results:
8,448-9,216 ticks (11-12 bars) on the clock-count path before, **7,680 ticks (10 bars) after**
- better, but still 2 bars over the 6,144 (8-bar) target, and `record_loop_length_mismatch`
remains. Next: surface the probe counters in the per-track report to see whether the baseline
is captured a bar or two late (the stream starts mid-bar and the first probe call lands 0.5 s
in), then close the residual. Two earlier runs in this stretch were `SPARSE_CAPTURE` on a
degraded device; a re-flash restored healthy evidence, so the harness itself is not the cause
there.

**2026-09-17 successor reentry unguarded through all three routes; one ST line per transition.**
OpenSpec `successor-capture-reentry-unguarding`, the change the parent task 2.3 split out. Its
preflight (section 1) was already resolved; the predecessor work this round needed was 2.3's
remaining assertion breadth, which 5.2 owns - so 5.2 landed first, then 3.1-3.3 and 4.1.

- **5.2 isolation (closes 2.3):** new `test_continuous_capture` case
  `test_deferred_predecessor_completion_leaves_successor_interval_untouched` asserts that a
  predecessor's deferred completion during a successor's capture leaves the successor's rows,
  pending notes, preview, live-role reservations and track state untouched and forces no
  transition on it; `test_track_reentry_does_not_depend_on_manager_guard` gained the same
  completion-during-capture phase.
- **3.1:** `TrackManager::startOverdubbingTrack` no longer refuses a new interval because a
  deferred stop is pending. START is bounded by the live admission role, which
  `Track::startOverdubbing` proves with every storage/ownership precondition; the cross-track
  maintenance predicates still consult `hasPendingDeferredOverdubStop()` unchanged.
- **3.2 / 3.3:** `test_manager_reentry_with_a_held_same_pitch_note` passes (A's held same-pitch
  closure retained, B's physical Off closes B's note only), and the step gate ran green.
- **4.1:** `MidiButtonActions::handleToggleRecord` no longer swallows the press while a
  finalization is pending, so Button A admits the successor on the same terms as the other
  routes; the resolved reproducer
  `test_button_a_successor_stop_denied_until_deferred_role_available` passes, reading its
  finalized output from the job's sealed range (an admitted transfer empties the live store).
- **Firmware telemetry:** `Track::setState` emitted the `ST,Track` capture line twice (once via
  `logger.logStateTransition` -> `SC_STATE`, once directly). The duplicate is removed, so each
  transition appears once and the host's dedupe is now only a compatibility guard.

Evidence: full native **2196 cases, 1 skipped, 0 failures** - the Track, manager and Button A
reentry cases are green for the first time; `test_continuous_capture` 37/37;
`teensy41-capture-serial` RAM1 code 425560 / padding 424 / locals 6464. Device
([`successor-reentry-13`](../../captures/successor-reentry-13/session_20260917_134731.log)): the
successor interval **now opens on hardware** - `PLAYING->OVERDUBBING: 2`,
`OVERDUBBING->PLAYING: 2`, `intervals={'OVERDUBBING': 2, 'OVERDUB_STOPPING': 2}` and the record
still lands at 6,144 ticks. The probe's *overlap* verdict is not yet captured: its press landed
+1.7-1.9 s after the stop against a ~1.4 s predecessor window (the confirmation wait consumed
the window; the press-then-probe reorder landed but that run came back sparse).

Still open in that change: 4.2 (three-route agreement test), 5.1 and 5.3-5.6 (capacity,
transport-stop ordering, supersession, adversarial mid-grain timing, reentry telemetry), 6.2
(device acceptance) and 6.3 (closeout).

**2026-09-17 deferred-stop confirmation fixed, and the record over-run root-caused to
clock-count pacing.** Two more host-side oracle items, plus the real reason the record
length keeps missing.

- `_serial_has_overdubbing_stopped` accepted only the human `Overdubbing stopped` marker or
  the direct `OVERDUBBING -> PLAYING` edge - neither of which the deferred path emits - so
  **every** deferred stop ran out the full state-sync budget as `EVIDENCE_UNAVAILABLE`. That
  is the same defect family as the earlier counting/pairing fixes, and it is why the
  successor probe fired 8,638 ms after the stop it was meant to overlap. It now also accepts
  `OVERDUBBING -> OVERDUB_STOPPING` and `OVERDUB_STOPPING -> PLAYING`, so a deferred stop
  confirms promptly.
- The probe additionally bounds its own stop-confirmation wait to 1.2 s (a timeout there is
  already non-fatal), so its presses can land inside the predecessor's window.
- **Root cause of the 8-bar -> 10-34 bar record over-run, now identified:** in device-clock
  mode `resolve_wall_tempo_for_proxy` returns `None` (by design - it only resolves a tempo for
  the serial proxy), so `stream_pattern_for_bars` takes its `in_port` clock-message path and
  paces by **counting clock messages** against `target_bars * MIDI_CLOCKS_PER_BAR = 8 * 96 =
  768`. The device's bar cadence is exactly **2.00 s/bar at 384 ticks/s** (measured from its
  own `BAR,<tick>,<index>` rows), so 768 host clocks should be 16 s; the observed streams ran
  23-68 s (11-34 device bars), i.e. far fewer than 96 clock messages per device bar reach the
  host. An attempt to force the clock path explicitly (`wall_clock_tempo_bpm=None`) made it
  worse - 20 bars, because the stream then ran to its `max_seconds_guard` - and is reverted
  with that evidence recorded at the choice site.
  The fix is to stop the stream on the **device's own `BAR` index** (or measure clocks-per-bar
  empirically at the start and scale the target), not to count host clock messages or a host
  seconds-per-bar estimate. Until then the strict length check keeps flagging these runs, and
  it is right to.
- Consequence for the successor probe: the two most recent probe runs were inconclusive
  (`SPARSE_CAPTURE` - the over-long stream floods the relevant evidence), so the
  successor-reentry device case is **blocked behind the `BAR`-anchored stop**, not by the
  probe's own design.

**2026-09-17 successor-reentry probe: instrumented and unit-tested, device evidence still
open (two device attempts, both diagnosed).** The probe and its oracle are implemented
(`--successor-reentry-delay-ms` / `--successor-reentry-stop-delay-ms`, default off;
`_verify_successor_reentry` reads the deduped transition stream plus
`ODUB,stop,bridge_ready` and requires the successor's `PLAYING -> OVERDUBBING` edge before
the predecessor's completion, with the quick stop producing no stop edge before it). Four
host tests cover the branches; `test_capture_transitions` passes 30/30.

Device attempts:
- [`successor-reentry-07`](../../captures/successor-reentry-07/session_20260917_130811.log):
  the probe fired **8,638 ms** after the predecessor's stop (window was 1,816 ms), because it
  ran after `request_state_transition` returned - the deferred stop reads as
  `EVIDENCE_UNAVAILABLE` for the whole state-sync window. Fixed by timing the presses from
  the stop *press* on a timer.
- [`successor-reentry-08`](../../captures/successor-reentry-08/session_20260917_131145.log):
  the timer version **raced the harness's own blocking confirmation** - no
  `OVERDUBBING -> OVERDUB_STOPPING` edge was emitted at all, the successor-reentry block
  reported `None` throughout, and the run ended ARMED (inconclusive). So the probe needs its
  own observation path (or a bounded `--state-sync-timeout-ms` for probe runs) instead of
  racing `request_state_transition`. That is the next step for this case.

**Also reproduced while probing: the record stream is paced longer than requested.** The
device recorded 11 bars (8,448 ticks) and 12 bars (9,216 ticks) for an 8-bar (6,144-tick)
request; the stream ran 23.0 s against a 16 s request at the device's own 2.0 s/bar cadence
(`BAR` markers every 2.0 s, `EMPTY -> RECORDING` 254.13 s -> `RECORDING -> STOPPED_RECORDING`
277.10 s). The device is quantizing correctly for what it received - the host stream and stop
moment are the problem. This is the remaining half of the wall-clock item: when the device's
own `BAR`/`BPM` markers are available, the stream duration and the stop moment should follow
them rather than a host-side seconds-per-bar estimate. Until that is fixed, the strict
record-length check will keep flagging these runs, and it is right to.

**2026-09-17 bridge tail profiled: it is cadence-bound, not work-bound.** Goal step 3, from
[`c3-derived-reserve-01`](../../captures/c3-derived-reserve-01/session_20260917_124359.log) -
the deferred Playing stop, `ODUB,stop,bridge_base` 54.929612 s to `ODUB,stop,bridge_ready`
55.997826 s = **1,068 ms**, profiled from the firmware's own `DIAG,stop_grain` /
`DIAG,stop_phase` telemetry (`stopGrainTiming` reports `startUs`/`endUs` per grain, so the
grain duration is `endUs - startUs`):

| phase (`PendingStopPhase`) | grains | grain work | max grain | phase span | crossed |
|---|---|---|---|---|---|
| 5 `CanonicalTail` | 11 | 1.1 ms | 115 us | 18 ms | 5 |
| 7 `CompanionHistory` | 4 | 0.5 ms | 204 us | 8 ms | 0 |
| 8 `UndoCollection` | 3 | 0.2 ms | 82 us | 2 ms | 0 |
| **9 `CompanionBridge`** | **466** | **65.4 ms** | 314 us | **1,057 ms** | **29** |
| 10 `AtomicTransfer` | 1 | 0.2 ms | 201 us | - | 0 |
| 11 `DeferredCleanup` | 5 | 0.6 ms | 394 us | 6 ms | 1 |
| total | 490 | **68.0 ms** | | 1,108 ms | 35 |

So **94% of the tail is idle between grains**: the bridge phase issues 466 grains of ~140 us
each at a **1.2-3.2 ms cadence** (roughly one grain per main-loop turn), i.e. ~300-800
grains/s against a ~7,000 grains/s work capacity. `CompanionBridge` owns 466 of the 490
grains and 65.4 ms of the 68.0 ms of work; the firmware's own deadline counter marks 29 of
them as crossing slack (~6%). `CompanionHistory` is now **4 grains** - C3's work shaping is
directly visible, since the same stop previously paid ~2,030 `Phase::Trim` units there.

Conclusion for the DEC question: the remaining stop-to-final-output tail is **not** the
bridge map/compile algorithm, so an optimization DEC would be about the deferred-stop
**owner's turn cadence / per-turn grain budget**, and it must be argued against the
MIDI-boundary budget (grains run after MIDI service, must not stack, and `overflow_free` is
a standing gate). That is a scheduling decision with a deadline analysis, not a knob tweak;
no DEC is proposed yet - this profile is the input to one.

**2026-09-17 HITL oracle repaired: the deferred stop is now first-class in the assertions.**
Host-side only; no firmware change. The firmware was ahead of its oracle: every deferred
overdub stop looked like a firmware regression.

Four oracle defects, all in the host, all now fixed:

1. `_effective_transition_actual` required the direct `OVERDUBBING -> PLAYING` edge, so the
   legitimate deferred shape counted as zero stops. It now sums the direct edge with the
   deferred completion hop (`OVERDUB_STOPPING -> PLAYING`); each completed stop produces
   exactly one of the two shapes.
2. `_pair_overdub_sessions` collected only `OVERDUBBING -> PLAYING/STOPPED` as an overdub
   stop, so a deferred stop left the phase session open - and `missing_phase_boundaries`,
   `overdub_first_note_missing` and a zero overdub note-on count all followed from that one
   gap. It now also accepts `OVERDUBBING -> OVERDUB_STOPPING` (the interval's last admissible
   input, and the earliest matching stop) and `OVERDUB_STOPPING -> PLAYING/STOPPED`.
3. `_extract_overdub_memory_note_count` matched the direct-edge marker string only.
4. The firmware reports one track state change from **two observers** (the Track state
   facade and the logger hook), so every `ST,Track` edge appears twice within microseconds.
   Counting both doubled every transition row (`PLAYING->OVERDUBBING: 4` for two real
   entries) and, worse, let the phase pairing bind a successor interval's start to the
   predecessor's stop - which manufactured a bogus second-overdub window and a false
   `second_overdub_out_of_range_notes` firmware contradiction. `hitl.serial.protocol` now
   exposes `track_transitions()`, collapsing an identical `(from, to)` edge that repeats
   inside 5 ms, and both the counters and the phase pairing read from it.

Wall-clock proxy: the proxy cannot predict the device's own bar grid, so a requested bar
count is not an authoritative length there. The runner records `transport_timing_source`;
when it is `wall_clock_proxy`, a longer bar-aligned length is reported
(`record_loop_length_proxy_timed` timing warning) instead of raised as
`record_loop_length_mismatch`. Without the proxy the strict check is unchanged, and a
non-bar-aligned length is a mismatch in both modes.

Evidence:
- `scripts/test_capture_transitions.py` gains five regression tests (deferred hop counting,
  deferred phase pairing, immediate pairing unchanged, proxy-timed length reported,
  non-bar-aligned still rejected): **26/26 pass**; `test_hitl_serial_timing` 30/30 and
  `test_hitl_transport_reset` 25/25 stay green.
- Device [`oracle-fixed-01`](../../captures/oracle-fixed-01/session_20260917_125305.log):
  **Result: PASS**. `OVERDUBBING->PLAYING: 1 (ST 0, min 1)` - the deferred hop counted; the
  overdub phase now counts **16/16** notes (was 0/0); the 9,216-tick proxy-paced length is a
  report, not a mismatch; the deferred stop, SEVT content, cleanup and both display checks
  pass. The only remaining note is the advisory `overdub_first_note_too_late` warning
  (19 clocks against 16 with the BAR bump) - a host stream/ST skew, not a failure.
- Replayed on `successor-reentry-01`: `PLAYING->OVERDUBBING` 4 -> 2, effective
  `OVERDUBBING->PLAYING` 2, and the second overdub pairs to its own window.

**Successor-reentry device case: still open, and now measured precisely.** With the repaired
oracle and a fresh device, the canonical second-overdub run **passes**
([`successor-reentry-06`](../../captures/successor-reentry-06/session_20260917_130431.log)):
`PLAYING->OVERDUBBING: 2 (min 2)`, `OVERDUBBING->PLAYING: 2 (ST 0, min 2)` (both stops
counted through the deferred hop), second overdub 8/8 notes, record 128/128, both SEVT spans
`count_ok/span_ok/bars_ok`, both display checks green. But it is **sequential, not
overlapping**: the measured timeline is OD1 start 34,072.9 ms -> OD1 stop 41,064.8 ms
(deferred, `bridge_base` 41,178.6, `bridge_ready` 42,269.1) -> **OD2 start 49,713.4 ms**, an
**8,648 ms gap** against a ~1.2 s deferred window. Removing the post-stop undo/redo did not
change that gap, so the second-overdub start is bar/grid-scheduled rather than
stop-relative - the canonical knob cannot express an overlapping successor.

What the case needs, concretely: a millisecond-scheduled successor press - after the first
overdub stop press, press the same button again after a bounded delay (~200-400 ms) without
waiting for a bar boundary, then optionally press stop again inside the successor's own
pending window to reach the denied-stop branch - plus a verification that a
`PLAYING -> OVERDUBBING` edge lands **before** the predecessor's `bridge_ready`/`prefix_reuse`
completion (or that the phases overlap), and a `!STOP_STATUS queue=depth/capacity` read.
That is a small, self-contained addition to the legacy runner (a new delay flag and one extra
button action) plus a verifier predicate; it is the next harness increment. Native already
covers both branches
(`test_button_a_successor_stop_denied_until_deferred_role_available`,
`test_successor_stop_denied_by_occupied_deferred_capacity_is_non_destructive`).

**Device-state hazard found while doing this (not caused by the oracle work): a deferred save
can wedge, and it then blocks every subsequent HITL run.** After an aborted run the clear
ritual reports
`[StorageManager] ERROR: Persistence drain stuck after 256 iterations (max 256, expected ~2362 slice steps, ~158848 SD bytes)`
and the firmware aborts the clear (`Clear waiting for deferred save completion` ->
`Clear aborted: could not complete deferred save first`), so no run can clear its slot.
Increasing the harness boot settle to 45 s did not help. The device also does **not** reset on
serial open in this setup - uptime climbed to 1,195 s across the aborted runs, and a 1200-baud
touch did not reset it. **Recovery: re-flash (`pio run -e teensy41-capture-serial -t upload`,
which hard-resets); after that the device booted at 6.8 s uptime and the next run passed.**
Two follow-ups worth a decision: make the harness force or verify a fresh boot before a run
that matters, and look at why the blocking drain makes *zero* progress in that state
(158 KB pending, `chunks_free=362`, `snap=3389KB`) rather than merely being slow.

**2026-09-17 C3 increment 2 LANDED and device-measured: the companion-row reserve is derived
from the loop, and the deferred stop is twice as fast to its prepared window.** The reserve
half of DEC-066, plus the measurement increment 1 could not get (that run took the
transport-stop path; this one took the deferred Playing path).

What changed:
- `Loop::companionRowHorizonBound()` derives the row bound from the loop's canonical payload
  event count plus the open-note reservation capacity as slack, clamped to
  `kRecordsPerBridge`. Rationale, from the per-owner measurement: a companion row exists only
  for a Delete/Length transform, `upsertSourceTransform` keeps one such transform per
  **source** note identity, and every source note contributes at least one payload event - so
  payload count is a sound, generous bound that still reaches the ceiling for a dense loop.
- `RuntimeCaptureCompanions::begin` now admits against the **eligible rows** the frozen change
  set will produce, not the whole change vector: the vector also carries the interval's `Add`
  entries, which become the capture pass and never become rows. Without this, a derived
  reserve could deny a stop whose captured engine is dense but whose transform set is small.
- `RuntimeCaptureCompanions::ensureCapacity` grows the row reserve at a cold point only, and
  never shrinks it, so a later denser interval still fits a smaller earlier grant.
  `Track::prewarmPlaybackForSlot` grants the derived bound to both companion owners;
  `Track::startOverdubbing` re-grants it after a commit could have grown the canonical
  content. Both grants are tag-bracketed; a failed grant reports
  `#MEM_DENIED op=overdub_start,reason=prewarm_failed`.

Measured against the step-E baseline on the same loaded-set 8-bar recipe
([`c3-derived-reserve-01`](../../captures/c3-derived-reserve-01/session_20260917_124359.log)):

| metric | step-E baseline | C3 increment 1+2 |
|---|---|---|
| arm delta (1 s accounting line) | 1,469 KB | **1,315 KB (-154 KB, -10.5%)** |
| `tag 2` (views/bridges + frozen companions) | 790 KB | **715 KB (-75 KB)** |
| companion-row grants (tag 6 / row reserve) | 80 KB | derived; ~0-5 KB on this loop |
| stop press -> `ODUB,stop,bridge_base` | 220 ms | **112 ms (-49%)** |
| stop press -> `ODUB,stop,bridge_ready` | 1,127 ms | 1,180 ms (unchanged; the bridge map/compile dominates, exactly as the per-owner analysis predicted) |
| allocations inside the stop window | 0 blocks | **0 blocks** (dLive/dBlocks = 0 through `bridge_ready`) |
| SEVT / canonical | ok | **ok** (overdub 16/15, count/span/bars ok; record 128/128) |
| `FATAL` / `#MEM_DENIED` / `#MEM_EXT_REFUSED` | 0/0/0 | **0/0/0** |

The companion path completed (`bridge_ready ... context_retained`), so the derived grant was
sufficient on this content - it did not silently fall back. Minimum internal heap over the
run was 60 KB and 140 KB in the stop window, against the 48-65 KB of the step-E runs.

Fixtures for the three classes (native, `test_playback_prewarm`):
`test_capture_companions_reserve_bounds_eligible_rows` (a 4-row reserve admits a change vector
of 8 Adds + 1 transform and produces exactly one row; five transforms over four rows is a real
denial), `test_companion_row_horizon_scales_with_content_up_to_the_ceiling` (300 payload
events -> bound 428; payloads at the ceiling -> bound 2048, so the full envelope is retained),
and `test_companion_history_prep_units_scale_with_demand_not_reserve` (one produced row over a
512-row grant reaches Ready inside 32 quantum units). `pio test -e native`: **2196 cases,
2 pre-existing failures, 1 skipped, 2192 succeeded**.

Remaining, recorded rather than fixed: the event-horizon owners
(`canonicalPassInputs`, `companionPasses`, `apply`, `reconstruction`, views/bridges) are
ceiling-bound because their interval term *is* the staging admission bound and their reserve is
taken at the arm; shrinking them would either cap the interval (rejected) or move the reserve to
the seal and make it fallible (a stop-contract change). The maximum-envelope case is
fixture-covered on a dense loop, not device-covered: this device's loaded content derives a
small bound, so the ceiling path was exercised in the fixture rather than on hardware.

**2026-09-17 C3 increment 1 LANDED: per-episode work shaping in the prepared companion
history (DEC-066).** The measured per-owner capacity analysis is in
[DECISION_LOG DEC-066](../DECISION_LOG.md) - six distinct determinants, so one shared
"interval horizon" constant is provably wrong, and the event-horizon owners cannot shrink
below their ceiling soundly because their interval term *is* the staging admission bound
and their reserve is taken at the arm. What landed is the work half, which needed no
capacity change:

- `Loop::beginPreparedCompanionHistory` now sizes the working row vector to this interval's
  actual demand (`work.rows.resize(work.targetCount)`), inside the same grant, so
  `Phase::Trim` has nothing to pop. Previously each stop paid
  `grantedCompanionCapacity - actualCompanionRows` quantum units of pure trimming - about
  **2,030 units**, i.e. ~64 prep turns at `kDeferredStopPreparationQuantum = 32`.
- The row-fit check is now **capacity**-based (`work.rows.capacity() < historyCount +
  rows.size()`) instead of size-based, so a later, larger interval is still admitted against
  the same reserve after a small interval shrank the size.
- `Phase::RecycleGrow` restores the usable horizon with one batched resize instead of one
  row per quantum unit.

Evidence. New fixture
`test_companion_history_prep_units_scale_with_demand_not_reserve` (`test_playback_prewarm`)
prepares one companion row over a **512-row grant** and requires `Ready` inside 32 quantum
units, then asserts the grant is untouched for a later interval; the old walk needed ~511
units, so the bound is a real regression guard. Full native suite:
**2194 cases, 2 failed (pre-existing), 1 skipped, 2190 succeeded** - the case count moved by
exactly the one fixture added. Device
([`c3-trim-shaping-01`](../../captures/c3-trim-shaping-01/session_20260917_123329.log),
rebuilt and flashed): **0 `FATAL`, 0 `#MEM_DENIED`, 0 `#MEM_EXT_REFUSED`**, overdub content
verified (16/16 on/off, SEVT `count_ok/span_ok/bars_ok`), and both display checks pass
(`overdub_ok=True`, `transport_ok=True`).

**Honest gap in this increment:** that device run took the *transport-stop* path
(`OVERDUBBING -> OVERDUB_STOPPING -> STOPPED`), so it did **not** exercise the deferred
overdub-to-PLAYING preparation and therefore gives no before/after on the
stop -> `bridge_base` (220 ms) / `bridge_ready` (1127 ms) baseline. The latency win is
proven at the unit level (fixture) and still needs a run that takes the deferred Playing
path; that measurement, plus the reserve-shaping half of DEC-066, is the remaining C3 work.

**2026-09-17 C3 baseline pinned (before any capacity shaping).** Three facts the
demand-shaped-capacity decision and its device comparison must build on, all verified from
the step-E end-to-end log:

1. **The deferred-stop latency baseline**, from
   [`stepE-e2e-01`](../../captures/stepE-e2e-01/session_20260917_121721.log): stop press
   50.666 s -> `OVERDUBBING`/`OVERDUB_STOPPING`/`PLAYING` state transition at 50.667 s
   (+19 us, so audibility is not gated on the deferred work) -> `ODUB,stop,bridge_base`
   50.887 s (**+220 ms**) -> `bridge_begin` 50.894 s -> `bridge_ready ... context_retained`
   51.795 s (**+1127 ms**). The comparison must separate the companion preparation (before
   `bridge_base`) from the bridge map plus compile (the ~900 ms after `bridge_begin`).
2. **The real horizon cost is `Phase::Trim`, not a slice emitter.**
   `Loop::prewarmPreparedCompanionHistory` resizes `work.rows` to
   `passes.editPasses.size() + companionCapacity` (`src/Loop/LoopPreparedCompanionHistory.cpp`),
   and `Loop::stepPreparedCompanionHistory`'s `Phase::Trim` pops **one unused row per
   quantum unit** until `rows.size() == targetCount`. `kDeferredStopPreparationQuantum` is
   32 (`src/Track/TrackOverdubLifecycle.cpp:34`), so a 2048-row horizon with ~16 frozen
   companion rows pays ~2030 units, i.e. **~64 prep turns of pure trimming** per stop. That
   is derivable and removable without any capacity change: `beginPreparedCompanionHistory`
   already knows `targetCount`, so the working vector can be sized to it (allocation-free,
   capacity unchanged) and `Trim` becomes a no-op safety net.
3. **The ~65 `DIAG,prepare,slice,prep` lines are not this owner.**
   `LoopContentResolution`'s `Phase::RebuildPrepare` emits them, with `resume` =
   `prepMerged.size()` and a bounded per-slice event budget
   (`src/LoopContentResolution.cpp:2258-2331`); that cost is content-driven and belongs to
   the canonical/visual rebuild after the stop, not to the horizon.

Open question the per-owner measurement must still answer before the DEC: which owners'
worst-case demand is `canonicalContent + intervalBound` (and therefore saturates at 2048
unless the interval admission bound itself is reduced - a policy ceiling the user has
explicitly ruled out) versus which are genuinely canonical-only and can therefore shrink
soundly. The event-horizon workers look like the former; the playback view/bridge group
(~710 KB of the ~1.47 MB arm) is the case that decides whether C3 can deliver a material
arm-time memory win or only the latency win.

**2026-09-17 step E IMPLEMENTED: capture staging is now admission-provisioned, and the
loaded-set 8-bar recipe completes with no denial, no refusal and no fault.** DEC-065.
**Status: validated on device (user-accepted 2026-09-17)** - the timing invariant was
preserved while steady-state memory moved from boot lifetime to admission lifetime
(+1.19 MB boot headroom, both roles reserved before capture mutation, zero
allocations inside the stop interval, the formerly failing record->overdub path
completing with 0 fatal/denial/refusal, native coverage unchanged).
Separate follow-up, deliberately not part of this change: the harness's
`OVERDUBBING->PLAYING` edge expectation and its clock/`overdub_first_note`
mismatches need repairing against the deferred state machine.
Code: boot (`Track::prewarmPlaybackForSlot(s, false)`) no longer reserves either staging
role; the record arm keeps reserving both through `prewarmViews`, and `Track::startOverdubbing`
now reserves both on its own cold turn (tag-bracketed) **before** any mutation, replacing the
"staging must already exist" rejection with the same reserve-then-require order the arm uses.
On failure it reports `#MEM_DENIED op=overdub_start,reason=prewarm_failed`.

Measured on the loaded device
([`stepE-staging-admission-01`](../../captures/stepE-staging-admission-01/session_20260917_121455.log),
[`stepE-e2e-01`](../../captures/stepE-e2e-01/session_20260917_121721.log), both rebuilt and
flashed from this tree):

- **Boot headroom: PSRAM free after prewarm 3,772,224 -> 4,989,312 B (+1,189 KB)**, and
  `tag 3` reads **0 KB at boot** (was 1334 KB). The ninth slot's phase went from 156 KB to
  7.9 KB (horizon plus canonical copy directory only).
- **Arm delta is now 1,469 KB** (was 1,240 KB before staging moved in; the difference is the
  two stagings plus the runtime struct, `tag 3 = 148 KB`), and the tag vector at the arm is
  `80/790/148/0/7/80` - stop jobs, views/bridges, staging, horizon, and the history reserve
  that previously failed, all resident. Pool headroom at the arm is ~1.19 MB.
- **The arm no longer needs the heap.** The remaining blocker after the boot change was the
  point-of-use gate, not capacity: the companion-history reserve was denied `reason=heap_low,
  need=81920B, heap_free=76KB` with `pool_refused=0/0B` and ~1.27 MB of pool free. The 80 KB
  vector is `InternalHeapFirstAllocator` (malloc, then extmem), so the fallback would have
  served it - the heap-floor proxy was refusing an available operation. `admitOperationMemory`
  now treats the probe as the **authority** for `CompanionHistoryReserve` (the probe tests
  exactly the malloc-then-extmem pair the allocator will use), so the heap floor no longer
  vetoes a request the pool can serve. Denial is still explicit when the probe fails or the
  pool refused within its window.
- **End-to-end on the same recipe: 0 `FATAL`, 0 `#MEM_DENIED`, 0 `#MEM_EXT_REFUSED`.**
  Record: 128 note-ons captured, loop stopped and published. Overdub: opened, 16 events
  captured, `SEVT` verification drained (count/bars ok), and the deferred stop completed
  `OVERDUBBING -> OVERDUB_STOPPING -> PLAYING` at 50.667 s with no allocation failure
  (`#CAP,50667213,ST,Track,OVERDUB_STOPPING,PLAYING`, `ODUB,stop,bridge_ready,...,context_retained`).
  Heap floor over the run 48 KB, pool live peak 7,489 KB of 8,182 KB.
- **Host-side gates, not firmware failures:** the harness reports
  `ST OVERDUBBING->PLAYING: 0` because the deferred design routes through
  `OVERDUB_STOPPING`, and it flags `overdub_first_note_missing` plus a 7,680-tick (10-bar)
  loop against the requested 8 bars - the run's own config line says it fell back to a
  wall-clock tempo proxy because the Teensy clock was not on USB MIDI in. These are the
  known host/gate-defect class and are not part of this change.

Residuals, and one attribution this entry originally got wrong. **CORRECTED 2026-09-17:**
the ~65 `DIAG,prepare,slice,prep` lines in the e2e log are **not** the companion-history
horizon. They are `LoopContentResolution`'s `Phase::RebuildPrepare` canonical
materialize/merge (`src/LoopContentResolution.cpp:2258-2331`), where `resume` is
`prepMerged.size()` and each slice spends `kDeviceGateEventsPerSlice` events - so that
cost is **content-driven** (the 7,680-tick loop's passes), not horizon-driven, and it
starts after the stop while the canonical/visual rebuild runs.

The companion-history prep does still walk unused tail rows, but by a different mechanism
that is derivable rather than log-labelled: prewarm resizes `work.rows` to
`historyCount + companionCapacity` (`src/Loop/LoopPreparedCompanionHistory.cpp:74`) and
`Phase::Trim` pops **one row per quantum unit** until `rows.size() == targetCount`
(`:195-198`), where `targetCount = historyCount + rows.size()` of the actual frozen
companion rows (`:123`). So each stop pays `companionCapacity - actualCompanionRows` units -
around 2,030 for a 2048 horizon with an 8-note overdub - inside the deferred preparation.
Its share of the measured 1.13 s stop-to-bridge-ready (50.666 s stop -> 51.795 s
`bridge_ready`) is **not yet measured**, so C3's latency argument is currently analytical,
not measured; the device comparison must separate the prep from the bridge map/compile
before claiming it. What is already certain is the memory side.

The seven companion/lookup workers and the history rows remain owned by the record arm
(DEC-065 scope boundary), so a loaded-slot overdub still uses the degraded
canonical-prefix/companion path until a record arm upgrades the slot.

**2026-09-17 step E: the 1334 KB is slot-provisioned boot staging, and it is the headroom
the arm needs.** `TrackManager::prewarmPlaybackRuntime` (`src/TrackManager.cpp:59-80`) calls
`prewarmPlaybackForSlot(s, false)` at boot for every slot that is enabled or has committed
passes, and that call reserves **both** capture-admission stagings plus the slot runtime
itself - capture-only, no views, no workers. The step-D run shows the cost directly in its
own boot lines: phase 1 prewarms 8 slots and PSRAM free falls `5,140,416 -> 3,772,224`
(**1,336 KB**), phase 2 adds one more slot and falls `3,766,404 -> 3,606,348` (**156 KB**),
i.e. **~157 KB per slot** (2 x 61,952 staging + the runtime struct) plus ~10 KB per *track*
for the stop-jobs ring (`tag 1 = 80 KB` over 8 tracks). So `tag 3 = 1334 KB` is a
per-slot, boot-resident reservation for slots that may never record, not an admission
requirement: staging is only needed by a slot that records.

The step-E decision is therefore **slot-provisioned -> admission-provisioned**: move the
staging reserve from boot to the arm (the same cold allocation window the arm already uses
for the 1.24 MB view/worker set), keeping the frozen role reserved at the same moment so
punch-out still never allocates. That returns ~1.33 MB at boot - more than the 1.24 MB arm
increment that currently fails - and it makes the arm's true requirement explicit instead
of paying it for every content slot at boot. The counter-obligation to re-check is the
documented capture-only boot ordering, which uses staging readiness
(`ExecutionBounds.md`, "staging-only boot"): that contract must be restated in terms of
"staging is reserved when the slot is admitted for capture" rather than "at boot". This is
a design conclusion for the user's decision, not an implemented change; it needs a DEC with
the C3 capacity decision because both change a gated execution bound.

**2026-09-17 step D: the loaded-set 8-bar recipe now denies instead of hanging, measured.**
Run [`stepD-arm-delta-01`](../../captures/stepD-arm-delta-01/session_20260917_115207.log)
(rebuilt and flashed from this tree; `--record-bars 8`, one overdub, track 4 slot 1, the
device's own loaded workspace). Before this RC the same recipe ended in
`[ExternalMemoryFirstAllocator] FATAL` and a silent hang; now:

```
#MEM_ADMIT,5411558,op=boot_probe,need=16384B,heap_free=184KB,pool_refused=0/0B   <- positive control
#MEM_EXT_REFUSED,14399844,bytes=81920,count=1,live=8095KB,blocks=637,heap_free=76KB,
  tag=6,tag_live=0KB,tags=80/790/1334/0/7/0/0/0/0/10/2/64/66/68/96/144/0
#MEM_DENIED,14399858,op=companion_history_reserve,reason=reserve_failed,need=81920B
#MEM_DENIED,14399880,op=record_start,reason=prewarm_failed,need=0B
```

- **Zero `FATAL`, zero `Recording started`, device alive**, and playback continued
  (`STOPPED_RECORDING->PLAYING: 4`); the harness then aborts its own run because RECORDING
  is not confirmed, which is the intended consequence of a denied arm.
- **The arm delta is measured at 1,240 KB** on the 1 s accounting line (`dLive=+1240KB,
  dBlocks=+104, dHeapFree=-76KB`) - exactly the audit's prediction of tag 2's 790 KB plus
  the seven separately-tagged prewarms' 450 KB, and the whole set is retained because only
  the *last* reserve failed. The refusal line's full tag vector confirms every other
  contributor was already resident (`tag 6 = 0` at the first request).
- **Full arm requirement on a fresh slot = 1,320-1,400 KB**: 1,240 KB of views/workers plus
  the two `2048 x 40` history vectors, which is what the 80 KB refusal was.
- Headroom at denial: pool `8095/8182 KB`, heap **76 KB** on the first attempt and 64 KB on
  the second, `heap_min` 64 KB over the run - far above the 32 KB floor, so this denial is
  "the request exceeds what remains", not a floor heuristic. The two attempts 8.3 s apart
  produce identical refusals, which is the idempotence the slot prewarm is supposed to have.
- Honest residual: a denied arm **keeps** the 1,240 KB it did establish (it is the same
  idempotent slot prewarm), so on a device in this state every further arm attempt fails the
  same way and no slot can start a record until memory is freed or the boot reservation is
  reduced - which is exactly why step E matters for the fix, not just for footprint.

**2026-09-17 step C design: the stop working set cannot be shared - the levers are
capacity shaping and a single deferred-stop working set.** Design only; implementation
needs a DEC because the interval horizon is a gated execution bound
(`ARCHITECTURE_RULES.md` row referencing DEC-043). Grounding numbers are the measured
device sizes in
[ExecutionBounds](../Authority/Architecture/ExecutionBounds.md#record-arm-reservation-buffers-bytes-and-lifetimes-2026-09-17).

**What the design must preserve.** Every reserve is taken on a cold user-action turn (the
arm) so that the MIDI-tick deferred-stop grains and the atomic transfer allocate nothing
and no vector in them can grow; the second aborting allocator (`InternalHeapFirstAllocator`)
must never be reachable from a grain. Any proposal that reserves at punch-out makes the
*stop* fallible, which is worse than denying the arm, so it is out.

**C1 - one arena with typed views across the seven workers: rejected.** The seven are live
simultaneously (`apply::begin` binds five children at once; `Reconstruct` borrows
`spans`+`pair`; the overlap walk reads reconstruction's live `notes_`; the merge reads apply
while writing `companionPasses` and `canonicalPassInputs.normalizedScratch`), and the same
workers are driven by live canonical-prefix service while playing/overdubbing, so they are
not stop-only either. Aliasing would not be phase-exclusive by construction.

**C2 - alias `frozenCompanionBridgeMap.records_` (68 KB) onto the apply group: possible but
marginal.** It is the one pair whose live windows do not obviously overlap (the map is
built after the merge completes), but its `prepared_`/`baseOrigins_`/`base_` borrows must
survive through bridge compile and it can be re-entered via `requiresBridgeRefresh`. Saving
is at most 68 KB of 1.24 MB (~5%) for a proof that adds a new lifetime invariant, so it is
not worth taking alone - only as part of C3.

**C3 - shape the capacity from the admitted interval horizon instead of the global
maximum.** Every 2048 comes from three admission constants: `kEventsPerView`,
`kRecordsPerBridge` and `kCaptureStagingEvents` (all 2048). They are *limits*, not content:
the arrays are sized so that the worst case a user can add in one interval never has to
grow. The arm therefore reserves ~1.24 MB even for a loop holding a few hundred events.
This is the only lever that moves the number materially. Two sub-shapes:
- **(a) derive from the slot's committed shape.** `min(horizon, committedEvents + intervalHorizon)`
  and `min(2048, committedRows + 1)`. On its own this saves nothing for a long take,
  because `committedEvents + intervalHorizon` saturates at the horizon - which is exactly
  the measurement D should capture per slot.
- **(b) reduce the interval horizon itself.** Halving the three constants to 1024 nearly
  halves the whole reservation (staging 116 -> 58 KB/slot, view set 727 -> ~370 KB, the
  seven workers 450 -> ~225 KB, history pair 160 -> 80 KB). The cost is a product limit:
  a single interval could then admit at most 1024 captured events and the history at most
  1024 rows. For scale, a dense 8-bar 4/4 pass is a few hundred events, so 1024 is still
  ~2x a dense 8-bar take, and 2048 is ~4x. **This is the decision to take.**

**C4 - one deferred-stop working set per device instead of per slot.** The stop-only
buffers are per slot runtime today: `frozenCaptureCompanions` 80 KB + `bridgeMap` 68 KB +
the second 80 KB `EditPass` history vector + the frozen staging role 58 KB. Deferred-stop
admission is per *track* (`beginDeferredOverdubStop` is gated by that runtime's
`captureStopOwner`, `TrackOverdubLifecycle.cpp:209-241`), so two tracks can be stopping at
once; a device-wide singleton would restrict that to one concurrent deferred stop (and the
frozen staging role would need a device-wide owner). Benefit: ~228 KB per armed non-stopping
slot and ~58 KB per slot at boot, i.e. it attacks step E's 1334 KB too. Cost: a real
behaviour restriction and a new global owner, which needs its own DEC.

**Recommendation.** Take **C3(b) first** (a capacity decision that is user-facing and
reversible via the constants), with C3(a) as the derivation mechanism so a small loop
reserves only what it can use; keep C2 as a follow-up only if the arena is being built
anyway; treat C4 as the step-E answer rather than part of this stop-storage change. The
727 KB view set is playback infrastructure, not stop scratch, so it is deliberately not
redesigned here - it is E's question (slot-provisioned vs admission-provisioned).

**What D must measure to settle the choice:** the arm's new delta and post-arm headroom on
the loaded-set recipe, plus the per-slot `committedEvents`/`committedRows` at the moment of
the arm, so C3(a)'s saturation claim ("a long take already needs the full horizon") is
measured rather than assumed.

**2026-09-17 step B audit: the arm reservation is byte-exactly accounted, and the seven
workers are one concurrent working set - not phase-exclusive scratch.** Full table with
write/read windows and lifetime classes:
[ExecutionBounds](../Authority/Architecture/ExecutionBounds.md#record-arm-reservation-buffers-bytes-and-lifetimes-2026-09-17).
What the audit established, and the corrections it forced:

- **Device element sizes were measured, not assumed** (device toolchain, env flags):
  `MidiEvent` **20**, `EditPass` 40, `Pass`/`SealedPass` 8, `Origin` 4, `DisplayNote` 16,
  `Action` 16, `PlaybackBridgeRecord` 28, `MidiEventVec` 12. Every buffer then reconciles
  exactly with the measured tags: `canonicalPassInputs` 147,456 B = 144 KB (its eight
  arrays, not the three the architecture doc still described), `companionPasses` 98,304,
  `bridgeMap` 69,632, `apply` 67,584, `reconstruction` 65,536, `pairLookup` 10,240,
  `spans` 2,048 - the 450 KB group; `frozenCaptureCompanions` 81,920 and views+bases
  358,400 + bridges 368,640 = 727,040, which with the companions is exactly tag 2's 790 KB.
- **The seven workers are live simultaneously and are NOT stop-only.** `apply::begin` binds
  `candidate_`, `pair_`, `deletion_`, `spans_`, `reconstruction_`, `overlaps_` at once;
  `Reconstruct` borrows spans+pair; the overlap walk reads reconstruction's live `notes_`;
  and the merge reads apply data while writing `companionPasses`' alternate buffers and
  `canonicalPassInputs.normalizedScratch`. Worse for a "stop working storage" framing:
  `Track::processDeferredCanonicalPrefix` runs the *same* workers while a track is merely
  playing or overdubbing (`TrackDeferredMaintenance.cpp:855-877`), so their capacity is
  required **during RECORDING**, not only from punch-out. The only plausible disjoint pair
  is `bridgeMap.records_` (68 KB) against the apply group, and its `baseOrigins_`/
  `prepared_` borrows must survive to bridge compile and can be re-entered via
  `requiresBridgeRefresh`; even then an arena is sized to the max, not the sum.
- **Tag-scoping corrections that change the arm arithmetic.** Tag 2 is not
  "prewarmViews total": the inner prewarms are tagged 10-16, so `prewarmViews` costs
  **1,240 KB** (790 + 450), and the earlier "~340 KB `viewSet->prewarm()` plus
  `frozenCaptureCompanions`" was a double-subtraction. Tags 1 (80 KB), 3 (1334 KB) and
  5 (7 KB) are **boot aggregates** over tracks/enabled slots - not per-arm costs, so the
  "2.3 MB" and "1.8 MB" arm figures are both withdrawn. The arm increment is ~1.24 MB plus
  the two 80 KB history vectors on first use.
- **The two 80 KB history vectors are a permanent rotating pair.** At commit,
  `publishPreparedCompanionHistory` swaps `preparedCompanionHistory_.rows` into
  `passes.editPasses`, so both 2048-row `EditPass` vectors stay resident (160 KB) and the
  pair's capacity becomes canonical history; only the *content* is stop-only.
- **The earlier `0xE00 == 128 * sizeof(MidiEvent)` attribution was wrong** and is corrected
  in place here and in `PROJECT_STATE.md`. With `sizeof(MidiEvent) = 20`, `0xE00` does not
  decode to element 128 of a `MidiEvent` store; the 28-byte stride is
  `PlaybackBridgeRecord`, and the faulting object is unidentified. The gather-destination
  guard stays as defence in depth; the reproduced allocator-exhaustion hang is unaffected.

Consequence for step C: a single shared buffer cannot absorb the 450 KB group, so the
reduction has to come from capacity shaping (2048-row horizons sized from actual content
with explicit admission denial) and from revisiting whether a full view set (727 KB) must
be provisioned per armed slot - which is step E's question. Recorded per-buffer lifetimes
the design must respect: live-ingress capacity (`liveCaptureStaging`, `capture.store`,
`pendingNoteChanges_`, `overdubPasses`), MIDI-tick service capacity (`canonicalPassInputs`,
`companionPasses`, `apply`, `reconstruction`, `spans`, `pairLookup`), stop-only capacity
(`frozenCaptureCompanions`, `bridgeMap`, the history pair), and published/borrowed
artifacts that cannot be repurposed (views, selected bridge, `bridgeBases`, history rows,
transfer-horizon rows).

**2026-09-17 step F: the transport-start 81920-byte owner is the deferred record arm, not
a separate transport path.** Identification only - no firmware change, and no transport
admission gate is added (the user kept this out of the RC). The chain is: a record request
while the clock is stopped is *parked*, not started - `TrackManager::startRecordingTrack`
sets `pendingRecord[i]` and `TRACK_ARMED` and returns
(`src/TrackManager/TrackManagerCaptureQueue.cpp:53-77`) - and the transport start releases
it through `ClockManager::toggleTransport` -> `TrackManager::updateAllTracks` ->
`TrackManager::handlePendingRecordStart` -> the same `Track::startRecording` ->
`Track::prewarmPlaybackForSlot(..., prepareCapture=true)` ->
`Loop::prewarmPreparedCompanionHistory`. There, two same-size reserves are the only 81920
requests on the path: `passes.editPasses.reserve(rowLimit)` (the canonical side) and
`preparedCompanionHistory_.rows.resize(rowCount)` (the candidate side), both
`2048 * sizeof(EditPass)` with `sizeof(EditPass) = 40` on device. There is no other
81920-byte allocation reachable from a transport start: playback start
(`Track::startPlaying`) and `prewarmPlaybackForSlot(..., prepareCapture=false)` never touch
the `EditPass` row horizon, and boot prewarm deliberately uses `prepareCapture=false`, which
is why the *first* record command is what pays it.

Consequence worth recording: because it is the same arm, step A already gates it. The
"transport start has no admission gate" concern is closed for this allocation - a pending
record start on an exhausted device now emits `#MEM_DENIED op=record_start` and leaves the
track armed instead of aborting. Boot prewarm (`src/TrackManager.cpp:59-80`) still
establishes the transfer horizon without this row horizon, unchanged.

Log-only evidence cannot separate the two same-size reserves (both are in the same
admission-gated call and the fixed build prints two identical `need=81920B` lines), so the
two are recorded as one owner: `Loop::prewarmPreparedCompanionHistory`. Distinguishing them
would need a per-reserve tag, not a new gate.

**2026-09-17 step A landed: a failed required prewarm now denies the record arm.**
The correction below left the arm gated only at the companion-history reserve; every
other arm-time prewarm was still advisory. `Track::prewarmPlaybackForSlot` returned
`void`, so its caller could not distinguish "reserved" from "failed and ignored", and
`Track::startRecording` proceeded into the take reset and RECORDING regardless. The
device therefore still reached the deferred stop with an unestablished prerequisite -
the deferred risk the point-of-use probe only converted from an immediate death.

`prewarmPlaybackForSlot` now returns whether every required reserve was established, and
`startRecording` consults it as its **first statement** - before the take reset, the
baseline stamp, the pre-roll move and the state transition. A denied take leaves the pass
timeline, the armed pre-roll, the pending-note set, the capture baseline and the track
state exactly as they were. The denial is explicit and distinct from the headroom gate:

```
#MEM_DENIED,<us>,op=companion_history_reserve,reason=reserve_failed,need=81920B,...
#MEM_DENIED,<us>,op=record_start,reason=prewarm_failed,need=0B,...
```

A **missing runtime slot** is now a denial too: without it there is no staging and no
canonical output, so the take cannot be admitted on the terms the reserve exists to
guarantee. That is a deliberate tightening for a slot whose `editPasses` exceed the
bridge row horizon (previously such a take was admitted with no reserves).

Evidence. Device ([`arm-admission-01`](../../captures/arm-admission-01/session_20260917_113447.log)):
exactly one `op=companion_history_reserve`, exactly one `op=record_start`, **zero**
`Recording started`, zero `FATAL`, device alive. `native`: **2193 cases, 2 failed (both
pre-existing unguarded-reentry failures), 1 skipped, 2189 succeeded**, including the new
`test_record_admission_denied_when_required_prewarm_fails`, which asserts the denial is
non-destructive *and* that the same call still arms when the reserve is available - so the
assertions cannot pass by the state machine refusing everything. The continuous-capture
fixture now links `TrackMidiEventRouting.cpp` / `CommittedMidiScratch.cpp` because the
admitted arm reaches `Track::invalidateCaches`; only the edit-session neighbours stay
off-path stubs.

Stale claim corrected in place: the earlier note that a boundary probe could not be made
predictive is superseded for this arm - the required prewarm's own result *is* the
prediction, and acting on it needs no margin heuristic.

**2026-09-17 CORRECTION: the record arm is NOT refusal-tolerant - it aborts too.**
The failure-semantics entry above left the record boundary ungated on the reasoning that
"the record completed through 834 refusals". That conflated two different things: those
834 refusals are chunk retries **during** recording, which really are tolerated; the
**record arm** has its own fatal allocation. Measured twice
([`attrib-tags-02`](../../captures/attrib-tags-02/session_20260917_105922.log),
[`attrib-tags-03`](../../captures/attrib-tags-03/session_20260917_110118.log)):

```
[15.341] Button press: Loop 1 (short)
[15.361] Loop 1: Start Recording
#MEM_EXT_REFUSED,15616436,bytes=81920,count=1,live=8095KB,blocks=637,heap_free=56KB
[InternalHeapFirstAllocator] FATAL: both internal heap and external memory are exhausted!
```

Note `count=1`: this is the **first** refusal of the run, so a gate consulting
"has the pool refused recently?" would not have denied it, and the heap (56 KB) is above
the 32 KB floor — an 80 KB request simply exceeds what remains. A record-arm gate would
need a margin derived from that measured 80 KB, which is a real trade-off (it would deny
records on healthy devices with under ~112 KB of heap), so it needs the user's decision
rather than a unilateral threshold.

**Record-arm reservation attributed: capture staging dominates.** The record arm's first
action is `LoopPlaybackRuntime::prewarmViews()`, whose first line is
`prewarmCaptureStaging()`; the boot tag measurement puts `prewarmCaptureStaging` at
**1334 KB retained** (`tags=[84, 0, 1334, 0, 8, 0, 0]`, tag 3), matching the +1240 KB the
arm adds. The class profile from
[`attrib-record-start-03`](../../captures/attrib-record-start-03/session_20260917_103705.log)
splits consistently: **+17 blocks of ~47 KB (792 KB)** is `FixedPlaybackEventStorage`'s
raw arrays (2048-entry `events`/`order`/`phases`/`mergeScratch`/`retained`, ~57 KB each,
across the live and frozen roles), and **+82 blocks of ~4.8 KB (390 KB)** is the companion
and lookup prewarms in the same function (`frozenCompanionPairLookup`,
`frozenCompanionSpans`, `frozenCompanionReconstruction`, `frozenCompanionApply`,
`frozenCompanionBridgeMap`, `frozenCompanionPasses`, `canonicalPassInputs`, each sized to
`kEventsPerView`/`kRecordsPerBridge`). The first part is measured; the second is inference
from the prewarm list plus the class shape, not yet individually tagged.

**2026-09-17 point-of-use reserve denial WORKS - the record arm no longer kills the
device, and the overdub gate is re-verified on the current build.** The failed arm probes
above were taken *far* from the allocation they were predicting. The fix is to check at the
**point of use** instead: `Loop::prewarmPreparedCompanionHistory` now pre-checks the two
`EditPassVec` reserves (the `passes.editPasses` row horizon and the `work.rows` horizon)
immediately before each allocation, through
`MemoryMonitor::admitOperationMemory(OperationKind::CompanionHistoryReserve, bytes)`, and
returns the `false` its contract already allows instead of reaching
`InternalHeapFirstAllocator::abort()`. Nothing allocates between the probe and the real
reserve, so there the probe *is* predictive.

Verified ([`point-of-use-reserve-01`](../../captures/point-of-use-reserve-01/session_20260917_112246.log)):

```
#MEM_DENIED,...,op=companion_history_reserve,reason=reserve_failed,need=81920B,heap_free=76KB
#MEM_DENIED,...,op=companion_history_reserve,reason=reserve_failed,need=81920B,heap_free=64KB
#MEM_DENIED,...,op=overdub_start,reason=heap_low,need=16384B,heap_free=28KB
#MEM_DENIED,...,op=overdub_start,reason=heap_low,need=16384B,heap_free=28KB
```

**No `FATAL`, device alive (290 KB of serial in a 6 s probe), and because the arm survived,
the run reached the overdub where the round-1 gate denied again** - so the original
acceptance criterion now holds on the current build, not just on the build that first
proved it. The only refusals in the run are 6 x 32768 (the tolerated chunk retry storm) and
2 x 81920 (the two reserve probes).

Residuals to be honest about:

- The arm **continues** after the denied reserve, because the callers ignore
  `prewarmPreparedCompanionHistory`'s result; `Recording started` appears twice in that log.
  The prewarm exists so "stop grains never grow vectors or relocate canonical history", so
  on this path that invariant is not held and a later stop can still grow or abort. Denial
  at the point of use trades an immediate death for a deferred risk - better, not complete.
  **SUPERSEDED 2026-09-17:** the arm no longer continues; `prewarmPlaybackForSlot` reports
  its result and `Track::startRecording` denies before any mutation (see the step-A entry at
  the top of this journal). The deferred risk is closed for this route.
- The probe's own failed `extmem_malloc` goes through the `--wrap` accounting and therefore
  **registers as a pool refusal**, which is why the first denial already reads
  `pool_refused=1/81920B`. Harmless for the gate (it only reinforces `poolRefusing`) but it
  means the refusal counter counts probe attempts as well as real ones.

**2026-09-17 NEGATIVE RESULT: a boundary probe cannot make the record arm safe; reverted.**
Tried the "reserve required memory, and if reservation fails the operation does not start"
pattern from the user's plan, at the record-arm boundary, using a direct allocate-then-release
probe rather than an estimate. Two attempts, both failed, both reverted:

| probe | result |
|---|---|
| single block, the measured largest request (81920 bytes) | **passed**, arm still aborted at that same 80 KB allocation ([`reserve-gate-01`](../../captures/reserve-gate-01/session_20260917_111603.log): refusal at `heap_free=76KB, tag=6`) |
| 16-block profile of the arm's shapes (~307 KB total) | **passed**, arm still aborted identically ([`reserve-shape-01`](../../captures/reserve-shape-01/session_20260917_111749.log)) |

Why: the arm allocates roughly **960 KB across ~100 blocks** (tag 1 `80 + tag 2 `790 + tag 5 `7
+ tag 6 `80), so a single-block probe validates headroom that the arm's own earlier
allocations then consume, and a 16-block probe still under-reserves by 3x. Probing the
aggregate as one block would be fragmentation-hostile and would deny records that could
succeed; probing enough blocks to be faithful is no longer a probe.

So a per-boundary **probe** cannot predict this operation, and the honest options are:

1. **Shrink the arm's footprint** — the named levers are the seven companion/lookup
   prewarms at **450 KB measured** (`canonicalPassInputs` 144, `companionPasses` 96,
   `companionBridgeMap` 68, `companionApply` 66, `companionReconstruction` 64,
   `pairLookup` 10, `spans` 2) and capture staging at **1334 KB per slot**, which is
   prewarmed per track/slot and is the largest single line item in the whole budget.
   This is the recommended path.
2. **Hold a real reservation** rather than probe: acquire the arm's blocks up front and
   release them to the owner instead of back to the pool. That is a design change with
   ownership consequences, not an admission check.
3. **Coarse aggregate margin gate** (~1 MB at the record arm). It would deny records on a
   loaded device, which is arguably correct since the arm would abort there anyway, but it
   is a user-visible capability change and needs the user's call.

Reverted rather than left in place: a probe that cycles ~300 KB through the pool on every
record arm, and still does not prevent the abort, is worse than no gate at all.

**2026-09-17 record-arm reservation MEASURED per contributor - and it is NOT staging.**
The refusal line now dumps the whole tag vector, which matters because the process dies
there, so one arm gives every contributor's retained size. Measured
([`attrib-arm-vector-01`](../../captures/attrib-arm-vector-01/session_20260917_111029.log)):

```
tags (KB) = 80 / 790 / 1334 / 0 / 7 / 0 / 0 / 0 / 0 / 10 / 2 / 64 / 66 / 68 / 96 / 144 / 0
tag ids:    1    2    3     4   5   6   7   8   9   10  11  12   13   14   15   16
```

| contributor | measured |
|---|---|
| 1 `prewarmCaptureStopJobs` | 80 KB (boot aggregate over 8 tracks) |
| 2 `prewarmViews` tagged portion | **790 KB** |
| 3 `prewarmCaptureStaging` (cumulative, mostly boot) | 1334 KB |
| 5 `prewarmActiveOverdubTransferHorizon` | 7 KB (boot aggregate) |
| 6 `prewarmPreparedCompanionHistory` | **0 KB - this is the call that aborts** |
| 10 `frozenCompanionPairLookup` | 10 KB |
| 11 `frozenCompanionSpans` | 2 KB |
| 12 `frozenCompanionReconstruction` | 64 KB |
| 13 `frozenCompanionApply` | 66 KB |
| 14 `frozenCompanionBridgeMap` | 68 KB |
| 15 `frozenCompanionPasses` | 96 KB |
| 16 `canonicalPassInputs` | **144 KB** |
| 4 `prewarmCanonicalCaptureOutput`, 7 `getPlaybackOrder`, 8 `resetPassTimeline`, 9 `prepareFirstRecordingBar` | 0 KB |

**CORRECTED 2026-09-17 (step B audit, see
[ExecutionBounds](../Authority/Architecture/ExecutionBounds.md#record-arm-reservation-buffers-bytes-and-lifetimes-2026-09-17)):
tag 2 is not "prewarmViews total".** The seven inner prewarms are tagged 10-16, so
`prewarmViews` costs **1,240 KB** = tag 2's 790 KB + those 450 KB, and the earlier
sentence calling "~340 KB `viewSet->prewarm()` plus `frozenCaptureCompanions`"
double-subtracted the 450 KB group. Tag 2's 790 KB is exactly views+bases 350 KB +
bridges 360 KB + `frozenCaptureCompanions` 80 KB. Tags 1/3/5 are **boot
aggregates** over tracks or enabled slots (`TrackManager::prewarmPlaybackRuntime`
prewarms every enabled, non-empty slot with `prepareCapture=false`), so they are
not per-arm costs: the arm's incremental cost is 790 + 450 + the two 80 KB history
vectors (once) + 0 for staging/stop-jobs/horizon. Device element sizes were
measured with the device toolchain (`sizeof(MidiEvent) = 20`, not 28) and every
buffer reconciles exactly with these tags.

**This corrects the entry above.** Staging does dominate the *boot* footprint (1334 KB,
and tag 3 barely moves on the arm because that slot's staging was already prewarmed), but
the arm itself is dominated by **`prewarmViews` at 790 KB**, of which the seven
companion/lookup prewarms are **450 KB measured** (`canonicalPassInputs` 144,
`companionPasses` 96, `companionBridgeMap` 68, `companionApply` 66,
`companionReconstruction` 64, `pairLookup` 10, `spans` 2) and **790 KB** is
`viewSet->prewarm()` plus `frozenCaptureCompanions.prewarm(kRecordsPerBridge)` (350 KB
views+bases, 360 KB bridges, 80 KB companion rows). The
round-4 sentence calling the ~390 KB companion group "inference" is now measurement, and
its size was under-estimated by ~60 KB.

**CORRECTED 2026-09-17 (step B): the "~340 KB" in the sentence above was wrong; it is 790
KB, and the seven prewarms are *not* a subset of it.** See
[ExecutionBounds](../Authority/Architecture/ExecutionBounds.md#record-arm-reservation-buffers-bytes-and-lifetimes-2026-09-17).

So arming one record on a slot costs roughly **1.24 MB of new reservation**, not 2.3 MB:
790 KB tagged `prewarmViews` + the 450 KB seven-prewarm group, plus the two 80 KB
companion-history vectors the first time, plus 0 for staging, stop jobs and the transfer
horizon, which are already boot-resident. The 1334 KB figure is the *boot aggregate* of
both staging roles over every enabled slot (~116 KB per slot), and the 80 KB stop-jobs
figure is a boot aggregate over eight tracks - neither is a per-arm cost, so adding them
to the arm double-counts. Against the ~1.3 MB a loaded device has free at boot, the 1.24 MB
arm is still the whole reason a device with several slots loaded cannot arm a record - and
`prewarmPreparedCompanionHistory`'s 80 KB row horizon is simply the last
allocation in that sequence, which is why it is the one that aborts.

**2026-09-17 the 80 KB fatal request is NAMED and measured: the companion-history row
horizon.** The `#MEM_EXT_REFUSED` line now carries the active bracket tag, so a request
that kills the process still identifies the step that made it. On the record arm
([`attrib-arm-tag-01`](../../captures/attrib-arm-tag-01/session_20260917_110509.log)):

```
[16.961] Loop 1: Start Recording
#MEM_EXT_REFUSED,17217439,bytes=81920,count=1,live=8095KB,blocks=637,heap_free=56KB,tag=6,tag_live=0KB
[InternalHeapFirstAllocator] FATAL: both internal heap and external memory are exhausted!
```

`tag=6` is `TagCompanionHistory` and `tag_live=0KB` says the failing allocation was that
call's **first**. So the arm completed `prewarmCaptureStopJobs`, `prewarmViews` (with its
staging), `prewarmCanonicalCaptureOutput` and `prewarmActiveOverdubTransferHorizon`, and
died in **`Loop::prewarmPreparedCompanionHistory()`**.

That resolves the size exactly: `PreparedCompanionHistory::rows` is an `EditPassVec`
(`std::vector<EditPass, InternalHeapFirstAllocator<EditPass>>`, matching the aborting
allocator), all four of `EditPass`'s enums are `: uint8_t`, so `sizeof(EditPass) = 40`, and
the reserve is `passes.editPasses.size() + companionCapacity == kRecordsPerBridge = 2048`
rows: **2048 x 40 = 81920 bytes, exactly the request**.

So it is the **designed row horizon, not an overflow and not a doubling** — the round-4
hypothesis that `81920 = 4096 x 20` was a doubled 2048 horizon is **withdrawn**. Nothing
overran `kRecordsPerBridge`; the arm simply asks for one more 80 KB block when the pool is
at 8095/8182 KB and the heap at 56 KB, and neither can supply it.

**Record-arm reservation, now attributed step by step** (order is
`prewarmPlaybackForSlot`): capture stop jobs, then `prewarmViews` (staging, plus the seven
companion/lookup prewarms), then canonical capture
output, then the overdub-transfer horizon, then the 80 KB companion-history rows, then the
playback order. **CORRECTED 2026-09-17 (step B):** the "1.8 MB" here is superseded by the
measured 1.24 MB arm increment (tag 2's 790 KB + the 450 KB seven-prewarm group + the two
80 KB history vectors), because the staging, stop-job and horizon figures quoted in this
paragraph are boot aggregates, not arm costs. See
[ExecutionBounds](../Authority/Architecture/ExecutionBounds.md#record-arm-reservation-buffers-bytes-and-lifetimes-2026-09-17) -
read that section, not the sums in this entry, for the arm reservation.

**2026-09-17 narrowing the 80 KB `InternalHeapFirstAllocator` request (offline).** The
fatal transport-start request is 81920 bytes. Element sizes were computed on the host:
`CausingTargetPair` 8, `ConstrainedNoteGeometry` 20, `PendingNoteChange` 20, `NoteId` /
`PassId` / `EditPassId` / `MidiEvent*` / 32-bit `size_t` 4, `uint16_t` 2, and
`EditSessionInteraction` **36** (its `NoteBaseline` is 12 bytes, not 8 — an earlier
hand-count of 28 was wrong). So possible element counts are 10240 x 8, 4096 x 20,
20480 x 4 or 40960 x 2, and `EditSessionInteraction` plus `MidiEvent` are **eliminated**
because neither divides 81920.

**CORRECTED 2026-09-17:** the parenthetical that `MidiEvent` is "28 bytes by the
snapshot-gather stride in `0x6002C97C`" is wrong - `sizeof(MidiEvent)` is **20 bytes** on
device, measured by instantiating the headers with the
`teensy41-capture-serial` toolchain (`midi::MidiType` is `: uint8_t`). `0xE00` is therefore
*not* `128 * sizeof(MidiEvent)`; the stride that makes `0xE00` exactly 128 elements is 28,
which is `sizeof(PlaybackBridgeRecord)`. Which object owned that address is not
established by the stride, so the DACCVIOL attribution built on the old arithmetic (a
snapshot-gather fill writing element 128 through a lost destination base) is
**unverified** and is corrected in place below. The landed gather-destination guard stays
useful regardless; the reproduced allocator-exhaustion hang is unaffected.

No `reserve()` in the tree takes 4096/8192/10240 literally, so the allocation is almost
certainly a **`std::vector` doubling**, and `81920 = 4096 x 20` is exactly the doubling of
a **2048-row horizon** (`RuntimePlaybackViewConfig::kRecordsPerBridge = 2048`). The
obvious 2048-row candidate, `Loop::prewarmPreparedCompanionHistory`, is not claimed *by
this entry*: at this point its guard was thought to preclude growth. **SUPERSEDED
2026-09-17:** it *is* the owner, and the request is the designed row horizon rather than a
doubling (see the entry above and the step-F entry at the top).

**Device is offline as of this note** (`/dev/cu.usbmodem154944801` not enumerated): the
transport-start abort left it hung, and it no longer answers the loader handshake — the
Teensy Loader reports *"Please press the PROGRAM MODE BUTTON on your Teensy to upload
your sketch."* Reading the record-arm tags and any further device verification needs a
physical PROGRAM MODE press / power cycle.

**2026-09-17 there are TWO aborting allocators, and the cliff is reached during
prepare, so the fatal allocation lands on whichever operation comes first.**
`ExternalMemoryFirstAllocator` (extmem -> malloc -> abort) is the one the failure-semantics
gate covers. `InternalHeapFirstAllocator` (malloc -> extmem -> abort, line ~82 of
`include/Utils/InternalHeapFirstAllocator.h`) is a **second** abort site, and it fires on
paths with no gate. Observed in
[`attrib-tags-01`](../../captures/attrib-tags-01/session_20260917_104406.log), right
after a Global Transport press:

```
[23.219] Button press: Global Transport (short)
[23.219] [MIDI] OUT MIDI Start (DIN=1 USB=1)
#MEM_EXT_REFUSED,23485180,bytes=81920,count=1,live=8104KB,blocks=643,heap_free=56KB
[InternalHeapFirstAllocator] FATAL: both internal heap and external memory are exhausted!
```

An **80 KB** request with 56 KB of heap free and the pool at 8104 KB. It aborts even
though the pooling heap had 56 KB — the request is simply larger than the remainder, so
a headroom gate would have caught it exactly as the overdub one does.

**This reshapes the remaining work.** The device reaches the cliff during the harness's
*prepare* phase, so the first allocation that cannot be served lands on a different
operation run to run: a transport start here, the record arm in
[`attrib-record-start-03`](../../captures/attrib-record-start-03/session_20260917_103705.log).
A gate at one mutating boundary therefore only fixes that boundary, which is the case for
gating **every** mutating boundary or for making the allocators themselves non-fatal on
audited paths.

**Tag-scoped attribution landed and is validated on the boot path.** `beginTag`/`endTag`
brackets the five steps of `Track::prewarmPlaybackForSlot`, and the tag is stored per
shadow entry so a free decrements the tag that allocated the block. The 1 s line gained
`tags=` (net KB for tags 1-7). Boot samples read `tags=[80, 0, 1334, 0, 8, 0, 0]` — i.e.
`prewarmCaptureStaging` retains **1334 KB**, `prewarmCaptureStopJobs` 80 KB,
`prewarmActiveOverdubTransferHorizon` 8 KB. Boot takes the staging tag
(`prepareCapture == false`); the record arm takes the `prewarmViews` tag, which is the one
still unread. The brackets sit in a `TRACK_COLD_MEM` unit so they cost **zero ITCM**
(`RAM1 code` unchanged at 425256); the 12-byte shadow entries cost 32 KB of RAM2
(`variables` 149664, 374 KB still free).

**Still open:** read the record-arm tags (the run must get past the transport-start
abort), gate the transport-start boundary, and decide whether the allocators themselves
should be non-fatal on audited paths.

**2026-09-17 record-start reservation attributed to the first record arm, with a
size profile and two candidates eliminated.** Sampling moved to 1 s with an
`ExternalPoolAccounting` **live-bytes-by-size-class** breakdown (`cls=` live KB and
`clsn=` live block counts for <=128 B, <=1 KB, <=8 KB, <=32 KB, <=128 KB, >128 KB),
plus `chunks_free=` (`LoopEventStore::freeChunkCount()`) and `snap=` (`snapshotAllocByteCount()`),
both already-existing O(1) counters. Evidence:
[`attrib-record-start-03`](../../captures/attrib-record-start-03/session_20260917_103705.log).

The whole reservation lands in **one 1 s interval** and reads:

```
t=15.08  dLive=+1240KB  dBlocks=+104   dHeap=-168KB
         class <=8KB:   +82 blocks = +390KB   (~4.8 KB each)
         class <=32KB:   +4 blocks =  +58KB   (~14.5 KB each)
         class <=128KB: +17 blocks = +792KB   (~47 KB each)
         chunks_free unchanged (384)   snap=1819KB unchanged
```

Correlating with the control-event log, that interval contains `Loop 1: Start
Recording` at 14.55 and `Recording started @ tick 484` at 14.80 — the harness's
**first arm on the freshly selected slot**, inside its clear ritual. The actual 10-bar
record at 22.91 adds **nothing** (`dLive` stays within +1-2 KB per interval). So the
~1.24 MB is a **one-time first-record-arm / slot-prewarm cost, not a per-record
cost**.

The **-168 KB heap drop lands in the same 1 s interval** as the pool jump, so the
objective's "+1.26 MB pool / +168 KB heap" pair is a **single event: the first record
arm**, and the coarse 5 s sampling in
[`hang-8bar-memacct-01`](../../captures/hang-8bar-memacct-01/session_20260917_100202.log)
could not separate them. Two candidates are **eliminated**: it is not LoopEventStore
chunk storage (`chunks_free` never moves), and not snapshot slabs (`snap` holds at
1819 KB and does not rise across the jump). Naming the individual buffers needs per-site
tagging; the profile above (17 x ~47 KB, 82 x ~4.8 KB) is the input to that.

Reading note, so nobody re-derives the wrong thing: a first pass mis-mapped the
adjacent `snap=` and `heap_free=` fields in the log analyser and briefly recorded the
snapshot as *released* at the jump. The corrected reading is the one above; verify the
field mapping before trusting any delta from that line.

Also landed: the accounting functions (`recordPoolAllocation` / `recordPoolFree` /
`recordPoolRefusal` / `reset` / `logExternalPoolAccounting`) are now **`FLASHMEM`**.
They were in ITCM and growing them crossed the FlexRAM bank boundary again; moving them
to flash **freed 688 bytes of ITCM** (`RAM1 code` 425944 -> 425256, `padding` 40 -> 728,
`free for local variables` 6624). The emitter being flash-resident is what makes adding
fields to the 1 s line free in ITCM terms.

**Still open:** name the individual buffers behind the 17 x ~47 KB and 82 x ~4.8 KB
allocations, and decide whether any unaudited boundary needs its own gate.

**2026-09-17 failure-semantics fix landed: overdub admission denies instead of
aborting (verified before/after on the same recipe).** `MemoryMonitor::admitOperationMemory`
is a pre-emptive admission gate called from `Track::startOverdubbing` **before** the first
mutation, so a denial is a clean refusal with nothing to unwind. It reads only O(1)
signals: the internal-heap free counters and the external pool's *refusal* record
(`ExternalPoolAccounting::counters().poolLastRefusalMs`, set by the `--wrap` wrapper).
Deliberately not the live-byte counter, which over-reports and cannot size headroom.
Denies when heap free is below a 32 KB floor plus a per-operation margin (16 KB for
overdub start, 8 KB for record start / snapshot reserve) or when the pool refused an
allocation within 2 s. Emits `#MEM_DENIED` with operation, reason, required bytes, heap
free, and the pool refusal count/size/age. The operation is passed as a numeric
`OperationKind` and the gate is `FLASHMEM`, so the call site carries no literal and the
whole slice cost **zero ITCM** (`code:425944 + padding:40 = 425984`, still 13 banks;
`free for local variables` 6656).

**Acceptance, same 8-bar track 4 slot 1 recipe:** before,
`[ExternalMemoryFirstAllocator] FATAL` and a silent device
([`hang-8bar-refusal-03`](../../captures/hang-8bar-refusal-03/session_20260917_101436.log));
after,
[`hang-8bar-admit-01`](../../captures/hang-8bar-admit-01/session_20260917_101926.log) logs
two `#MEM_DENIED`s, **no `FATAL`**, no `Overdubbing started` line, and the device stayed
responsive (487 KB of serial in a 6 s probe against silence before) with the loop playing
until the harness's own transport stop. Not a single ownership, reservation or state
change happened, because the gate sits before all of them.

**Discrimination proven on-device** with a one-shot boot probe that evaluates the same
requirement as the overdub gate:

```
#MEM_ADMIT,  op=boot_probe,   need=16384B, heap_free=216KB, pool_refused=0/0B
#MEM_DENIED, op=overdub_start, need=16384B, heap_free=4KB,  pool_refused=715/32768B, since_refusal=1ms
```

So the denial is a threshold crossing, not a gate that always refuses.

**Deviation from the written plan, deliberate:** the allocator itself was *not* changed to
return null. The reach audit showed `ExternalMemoryFirstAllocator` backs
`SessionMidiEventVec`, `DisplayNoteVec`, `PlaybackOrderVec`, `PresentNoteVec`,
`NoteSpanVec`, the undo vectors and the note-edit map allocator across ~29 files, most of
which assume infallible allocation; a global null return would convert the abort into
unchecked null dereferences. The gate achieves the objective's outcome (operation-level
denial, transport untouched) without that risk. **Residual:** an allocation failure
*outside* a gated boundary can still reach `abort()`. Record start was audited and
deliberately left ungated, on the reasoning that it was refusal-tolerant because the record
completed through 834 refusals. **That reasoning was wrong** — those refusals are
mid-record chunk retries, and the record *arm* has its own fatal allocation; see the
CORRECTION entry above.

**Observed consequence, not a regression:** on this device the canonical 2-bar recipe also
drives the pool to ~99% after its record, so the gate denies its overdub too
([`admit-regression-2bar-01`](../../captures/admit-regression-2bar-01/session_20260917_102138.log)).
Previously that path aborted; now it refuses and keeps playing. The capability gap is
real and is what defect 2 must close — the gate makes it visible instead of fatal.

Landing note: `test_continuous_capture` compiles `TrackOverdubLifecycle.cpp`, so the gate
needed a native stub in `MemoryMonitor.cpp`'s `PIO_UNIT_TEST_NATIVE`/host sections (it
always admits there). Without it that suite ERRORED at link and the run silently reported
2157 cases instead of 2192 — worth watching, because a dropped suite looks like a clean
pass. With the stub: native **2192 cases, 2188 passed, 1 skipped, the same two known
unguarded reentry failures**.

**Still open:** attribute the record-start reservation (+1.26 MB pool / +168 KB heap) to
named contributors, and decide whether any unaudited boundary needs its own gate.

**2026-09-17 failure-semantics audit: the pool was already denying allocations for most
of the record; one 5 KB request with no denial handling is fatal.** Added
`#MEM_EXT_REFUSED` to `__wrap_extmem_malloc` (distinct-size logging plus a
`refused=count/largest` field on the 5 s accounting line). Evidence:
[`hang-8bar-refusal-03`](../../captures/hang-8bar-refusal-03/session_20260917_101436.log).

What the refusals show on the same 10-bar record plus overdub recipe:

- A **32768-byte** request is refused **354 -> 834 times** while RECORDING, at pool
  `live~8143KB` (99.5%) and heap `4KB` free, roughly 25 refusals/second. **That path is
  already tolerant** — the record completes 7680 ticks and the transport keeps running,
  so a refusal-tolerant consumer already exists in this codebase.
- The **fatal** allocation is **5120 bytes**, refused once at `count=842`, and the
  `FATAL` follows within ~60 ms of `Overdubbing started @ tick 433`.

So exhaustion is not a surprise at the overdub: the device had been denying allocations
for most of the record and coped. One specific admission path has no denial handling and
reaches `ExternalMemoryFirstAllocator::abort()`. The failing request is small, which
means a headroom check at the **overdub admission boundary** would deny it cleanly long
before the retry storms.

Reach audit (why the allocator cannot simply return nullptr): `ExternalMemoryFirstAllocator`
backs `SessionMidiEventVec`, `DisplayNoteVec`, `PlaybackOrderVec`, `PresentNoteVec`,
`NoteSpanVec`, `UndoEntryVec`/`SessionUndoEntry` vectors and the note-edit
`ExternalMemoryUnorderedMapAllocator` maps, across ~29 files. Most callers assume
infallible allocation, so a global null return would convert the abort into unchecked
null dereferences. The semantics change must therefore stay on an audited path.

Still open: name the 5120-byte consumer. Arithmetic narrows it to 320 x `DisplayNote`
(16 B) or 256 x `NoteSpan` (20 B); `PresentNote` (12 B) and `MidiEvent` (28 B) do not
divide it. Per-instantiation probing is unaffordable here (only ~104 bytes of ITCM
headroom left), so pin it with a host-side sizeof check rather than firmware code.

**2026-09-17 pool accounting landed via linker wrappers; the hang is a record-start
reservation with the pool already 84% full.** The `--wrap` route works: `-D
EXTERNAL_POOL_WRAP=1` plus `-Wl,--wrap=extmem_malloc,--wrap=extmem_free` in
`env:teensy41-capture-serial`, with `__wrap_extmem_malloc/__wrap_extmem_free` in
`src/Utils/ExternalPoolAccounting.cpp`. It costs **no call-site code** — the ITCM grew
528 bytes and padding absorbed all of it, so the FlexRAM bank count is unchanged
(`free for local variables` 6720 → **6688**). `extmem_free` takes no size, so the
wrapper shadows pointer → bytes in an 8192-entry table in RAM2 (64 KB; RAM2 variables
51360 → 116896) and **counts** anything it cannot shadow rather than silently dropping
it — a first cut at 512 entries saturated immediately (`untracked=2047 unknown=1909`),
which is why the table is large and why both counters are on every line. Emitted every
5 s straight to serial (`MemoryMonitor::logExternalPoolAccounting`), deliberately *not*
gated on transport idle.

**Timeline, 10-bar record plus overdub on scratch track 4 slot 1
([`hang-8bar-memacct-01`](../../captures/hang-8bar-memacct-01/session_20260917_100202.log)):**

| point | pool live | blocks | internal heap free |
|---|---|---|---|
| idle before the test | 6836 KB of 8182 (**84%**) | 522 | 172 KB |
| record start (arm + clear) | **8095 KB (99%)** | 637 | **4 KB** |
| through the 10-bar record | 8095 → 8137 KB (flat) | 669 | 4 KB |
| overdub start → `FATAL` | — | — | — |

So the record phase does **not** leak: `live` is flat while `alloc` climbs 8500 → 9987 KB
and `freed` 404 → 1849 KB, i.e. heavy churn at the ceiling. The damage is done at
**record start**, which reserves ~1.26 MB of pool and ~168 KB of heap on top of an
already 84%-occupied pool. The **overdub start** is then the first allocation that
cannot be served, `ExternalMemoryFirstAllocator` hits `abort()`, and the device goes
silent with no crash record.

Two independent defects fall out of this, in priority order:

1. **`abort()` turns a transient allocation failure into an unrecoverable silent hang.**
   Both pools exhausted is a recoverable condition for a looper — drop the operation,
   log it, keep the transport alive. This is what makes the failure mode so bad, and it
   is fixable without touching the footprint at all.
2. **Idle occupancy leaves no headroom.** 84% of the pool is live at rest (prewarmed
   staging, view sets, snapshot slabs per slot), so a long record cannot fit. Either the
   steady-state footprint comes down or the record start needs less.

Calibration caveat: absolute `live` over-reports against the core's own stats
(`live=6836 KB` at a point where `[Memory]` reports `used=4661 KB`), while
`untracked`/`unknown` are 0, so the accounting is internally consistent but the absolute
figure is not yet trustworthy — read **deltas**. Native 2192 cases unchanged.

**2026-09-17 external-pool pseudo-counting: design sound, blocked by zero ITCM headroom.**
To see what grows during a long record plus overdub without probing the pool, the
pseudo-counting route is the right one — the pool cannot be queried from the transport
thread, and the repo already does exactly this for snapshots
(`snapshotAllocByteCount()` → `#CAP,DIAG,snapshot_alloc`). Built the same shape for the
external pool: O(1) counters (`liveBytes` / `highWaterBytes` / `liveBlocks` /
`cumulativeAllocated` / `cumulativeFreed`, plus a fallback-bytes counter for requests
that ask for PSRAM and get the internal heap), fed from the external-first allocators
that know the byte count on **both** allocate and deallocate, emitted as a short
direct-serial line every 5 s so it stays visible while PLAYING/OVERDUBBING where the
`#CAP` ring never flushes.

**It does not fit, and the reason is structural.** The committed tree sits exactly on
the FlexRAM bank boundary: `code:425224 + padding:760 = 425984 = 13 * 32768`. Padding
absorbs any growth up to roughly 760 bytes and then the next byte of ITCM code costs a
**whole 32768-byte DTCM bank**, so the accounting's ~4 KB of call sites (one call per
allocator instantiation, and there are hundreds) produced
`code:429304, padding:29448, free for local variables:-26080` and a hard
`Error program exceeds memory space`. Making the record functions out of line halved
the growth and still overflowed. **Remaining ITCM budget on this tree: ~700 bytes.**
Reverted; `teensy41-capture-serial` builds again at **6720**.

Options, in preference order: (1) link-time `-Wl,--wrap=extmem_malloc,--wrap=extmem_free`
so the counters cost **no** call-site code at all — one small FLASHMEM wrapper covers all
57 raw sites, and `extmem_free` needs no size because live *block* count plus cumulative
allocated bytes is enough for a trend; (2) free ITCM first by moving ~4 KB of cold code
to flash (the diagnostic/emitter units are the honest donors, since the accounting would
be paid for out of diagnostic code); (3) sample only counters that already exist
(chunk usage, snapshot bytes/nodes/slabs, heap free/min-ever) from one small emitter,
which fits the ~700-byte budget but misses the container-vector consumers.

**2026-09-17 8-bar record + overdub reproduces the hang: allocator exhaustion, not
a spin.** Temporary variant of `STIM-BASE-2-ONE-OD-01` (`--record-bars 8
--overdub-bars 2 --second-overdub-bars 0`) on scratch track 4 / slot 1, which was
verified empty first (`!CAPTURE_CONTENT` → `length=0`, zero record and effective rows)
so nothing musical was cleared. Evidence:
`captures/hang-8bar-track4-slot1-01/session_20260917_094002.log`.

The device recorded an 11-bar loop (8448 ticks; the wall-clock tempo proxy overshot 8
bars), stopped at 152.142 s, started the overdub at 153.369 s (`Overdubbing started @
tick 768`), and **about five seconds later printed
`[ExternalMemoryFirstAllocator] FATAL: both external memory and internal heap are
exhausted!` and went permanently silent** — 0 bytes in a 6 s probe with the USB port
still enumerated. That is the musician's hang, reproduced on the bench.

Two things this settles:

- **The hang is `abort()`, and it leaves no crash record.** `crashlog.txt` was unchanged
  at 394,625 bytes after the event and the next boot reported `crash=0`. `abort()` does
  not raise a Teensy `CrashReport`, which is why this class of failure has always read
  as a silent hang over serial and why "USB dropped" was the only host-visible signal.
  It is not a spin loop, and the earlier spin/`DACCVIOL` framing was wrong for this
  event.
- **Memory is already deep in the red before the record starts.**
  `[Memory] heap free=68 used=393 total=461 KB min_ever=68 KB` at 127.7 s uptime, against
  `heap free=280 used=181` at 6.3 s in the 00:39 session: roughly **212 KB of internal
  heap consumed in about two minutes** with four boot-playback slots loaded, before this
  test's own record. The 11-bar record plus its overdub then finished the job.

Harness verdict was `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)`, abort reason *"serial
heartbeat lost (20.0s since last meaningful line)"*. Worth noting for the harness: the
device kept emitting `CC ignored` DEBUG spam while the heartbeat filter saw nothing
meaningful, so the reported abort preceded the real terminal event by ~5 s; the
allocator line is the authoritative one. The follow-on DACCVIOL this session started
from is most likely the same pressure family — element 128 written through a lost
vector header is what a corrupted header looks like, and pressure is what corrupts it.

**Next:** trace what grows the external pool and the internal heap across a long record
plus overdub. The periodic 60 s `[Memory]` line is far too coarse and this run logged no
PSRAM free/used at all; the useful capture is a bounded per-phase memory sample
(record start/stop, overdub start, each deferred grain) with the allocator's own
pool counters.

**2026-09-17 `!RESTORE_WORKSPACE` — the missing inverse of quarantine (shipped to
the device).** The workspace quarantine had no restore path, so it was one-way from
the device's point of view: nothing in the firmware read a `.bad.` path back. Added a
stopped-transport HITL command in `StorageManagerHitlSerial.cpp` with three forms —
`!RESTORE_WORKSPACE list` (inventory only), `!RESTORE_WORKSPACE <stamp>`, and a bare
`!RESTORE_WORKSPACE` (newest). It **renames only**: the live tree is parked as
`.bad.<now>` rather than deleted, so the operation is reversible in both directions.
Landing it re-exposed the same RAM1 trap as the instrumentation work — plain
`Serial.print`/`printf` literals in that unit tip DTCM `.rodata` by a whole 4096-byte
block (`free for local variables` 6720 → 2624 in one step, not gradually); every new
string is now `printf_P` + `PSTR` and headroom stays **6720**.

**Ground truth from the first inventory:** `/MidiLooper` carries **20 quarantined
`current` trees and 21 `checkpoints` trees**, with stamps from 2314 ms to 1476949 ms —
i.e. accumulated across many boots, most of them predating this session, several from
the ~2-7 s boot window. Quarantine touches only `current` and the checkpoints;
`/MidiLooper/sets/` (saved loops and revisions) is never in scope, so **no musical
content was ever at risk**. The stamp is device `millis()` at rename time and cannot be
derived from the host.

**How the session's tree was found, and the trap:** the stamp the quarantine script
printed (`121898`) did **not** exist on the card. That line was almost certainly stale
buffered serial output from an earlier user-run quarantine, not this session's — the
script prints whatever the port delivers, and 20 historical trees mean it has been run
many times. This session's quarantine actually produced `1476949`; the first restore
consumed it into the live tree, and the second attempt (chasing the phantom `121898`)
parked it as `current.bad.35610`. Restoring `35610` reproduces the pre-quarantine
boot signature — `Boot focus track=3` (display track 4, the track the musician was on)
with `Queuing boot playback loop slot restore 4 pending` — against the unrelated
`975846` tree that booted `focus track=4` with nothing pending. Identification must use
the boot signature, never the name. Rule, ask template and the `list`-first procedure:
[`Destructive-SD-Operations.mdc`](../../.cursor/rules/Destructive-SD-Operations.mdc).

**2026-09-17 overdub-start fault attributed; snapshot-gather destination guarded
(instrumentation only, not flashed for acceptance).** The 2026-09-17 00:39 manual
session (`captures/session_20260917_003908.log`) that ended right after
`[TRACK] Overdubbing started @ tick 624` is a **hard fault with auto-reboot**, not a
hang: the SD crashlog for that boot reads `Code was executing from address
0x6002C97C`, `CFSR: 82 (DACCVIOL)`, `Accessed Address: 0xE00`. Resolved against the
flashed ELF that is `Loop::tryGatherPublishedSnapshotEventsInWindow`
(`src/Loop/LoopSnapshotPlayback.cpp`). **CORRECTED 2026-09-17 (step B):** the arithmetic
that follows is wrong. `sizeof(MidiEvent)` is **20 bytes** on device (measured with the
device toolchain), so `0xE00` is *not* `128 * sizeof(MidiEvent)`; the stride that makes
`0xE00` exactly 128 elements is 28, which is `sizeof(PlaybackBridgeRecord)`, so the
faulting instruction's object is not identified. The original claim - the faulting
instruction is the 28-byte `MidiEvent` block store and `0xE00 == 128 * sizeof(MidiEvent)`,
i.e. a fill writing element 128 through a **null destination base** - and the
"lost vector header, not exhaustion" conclusion drawn from it are **unverified**.
`ExternalMemoryFirstAllocator` still cannot return null (extmem, then `malloc`, then
`abort`), which is why the guard diagnostic was worth shipping, but it is now defence in
depth rather than a fix for an attributed fault. USB stayed enumerated across the reboot and
`capture_session.py` only reports a reconnect on a read error, so the fault was
invisible; two independent blind spots (no `#CAP` flush while PLAYING/OVERDUBBING,
and no CDC liveness check) hid it. Retrieved report and the RAM2 map are in
`captures/crashlog_retrieval_20260917_0049.log`.

Diagnostic landed (SESSION_CAPTURE-gated): a destination-storage guard at the gather
entry that reports and returns a gather miss instead of faulting, plus a
`displayManager` tail canary — `DisplayManagerInternal::liveDisplayEventBuffer` is
linked flush against the end of `displayManager` in `.bss.dma`, a section that is
NOLOAD and never zeroed, so any overrun there lands on that 12-byte vector header.
Reported a real regression while landing it: the first cut used `Serial.printf`, which
put the format strings in DTCM `.rodata` and cost **4128 bytes** of RAM1, dropping
`free for local variables` from 6752 to 2624 — below the documented floor of 4000 —
and the device then crash-looped at boot with `IACCVIOL` executing from `0x2001792C`
(`Accessed Address: 0x200175C4 (Stack problem)`). Moved to `printf_P` + `PSTR`, out of
line and FLASHMEM: headroom is now **6720** for a 32-byte cost. Device run
`captures/dma-canary-reentry-04`: record plus four OVERDUBBING entries, `crash=0`, no
canary clobber and no lost destination — the crash did **not** reproduce under the
canonical `STIM-BASE-2-ONE-OD-01`-shaped fixture. HITL was blocked first by a
reproducible `Persistence drain stuck after 256 iterations` in the Clear gate (the
docs already classify the sync-drain stuck detector as a progress-accounting gap);
cleared with the supported, non-destructive `!QUARANTINE_WORKSPACE`
(`/MidiLooper/current` → `/MidiLooper/current.bad.121898`, checkpoints likewise; loop
slots untouched). Full native **2192 cases — 2188 passed, 1 skipped, and exactly the
two known unguarded manager/Button A reentry failures**, i.e. unchanged from the
`640cc02d` baseline; `teensy41-capture-serial` builds with **6720** bytes RAM1
headroom. The instrumentation is on the device and is protective: a recurrence logs
`#GATHER_DEST_CORRUPT` (destination address, `data`/`size`/`capacity`) or
`#DMA_STAGING_CORRUPT` (canary plus the neighbour's header) instead of faulting.
Next: reproduce on the manual sequence that actually crashed — long loop, overdub
started about one second after the record stop — since the canonical fixture did not.
**User's repro conditions (2026-09-17, authoritative from the musician's side):** the
fault shows up when **several tracks already hold playing loops** and you then record
a loop of **8 bars or more with overdub**. That is a materially different shape from
`STIM-BASE-2-ONE-OD-01` (2 bars, one track, 32 notes, no other track playing) and is
the condition any next attempt must reproduce: seed loops on multiple tracks and keep
them playing while recording an 8+ bar pass, then overdub immediately after the stop.
Likely coupling: more simultaneously playing slots means more per-slot playback
windows, so more concurrent gather targets and more resident snapshot/bridge state —
a direction that **used to be** argued from `0xE00 == 128 * sizeof(MidiEvent)`. That
arithmetic is withdrawn (see the correction above: `sizeof(MidiEvent) = 20`), so the
coupling is a hypothesis without a stride argument; it still does not affect the
reproduction recipe or the allocator-exhaustion finding.
**2026-09-16 task 2.3 causal predecessor/source identity — increment B (approved
superseded rule).** The user approved option (A): a deferred stop whose recorded
causal predecessor is no longer the published root must retire without publishing a
successor window. Implementing it falsified one premise of the audit sketch — the
canonical pipeline compares its owner identity **by address** against the live Loop
scalars and its ordering phases need one coherent live epoch, so resolution must stay
live (a recorded base makes the phases never complete, stalling before staging; a
measurement confirmed the live revision and root are unchanged during a normal
finalization). The recorded `causalBase` is therefore the **supersession reference**:
`causalBaseCurrent` is evaluated at the start of `CompanionBridge` (before any bridge
is built) and at the start of `AtomicTransfer`, and when false the interval retires
its ranked credit through `PendingStopAdmission::retireSupersededPreparation()` and
commits via `TrackPreparedStopCommit::commitSupersededCausalBase` — the staged
accepted capture pass is committed, no bridge is selected or published
(`causal_base_superseded`), and a prepared protected undo entry is published in the
same turn. The unpublished horizon returns to `Prewarmed` via the new allocation-free
`Loop::restoreSupersededPreparedCompanionHistory(owner)` (the recycle receipt guard
cannot admit it by design), with discard-and-reprewarm as fallback. Verified by the
superseded assertions in
`test_deferred_stop_records_and_pins_its_causal_predecessor_source`; full native 2,191
cases — 2,186 passed, 1 skipped, the same three intentional public-reentry failures;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom. Recorded deviation:
the superseded route publishes the capture pass only, relying on the newer root to
already carry the companion rows. Remaining in 2.3: successor-capture unguarding.

**2026-09-16 task 2.3 causal predecessor/source identity — increment A.** An audit for
requirement (f) found that a deferred stop re-derives its predecessor source **live** at
every consumer turn: admission computes `baseSource` for credit sizing and discards it,
the worker recomputes `baseSource`/`targetSource`/`frozenSource`/`currentSource` when the
grain runs, and the atomic commit recomputes `base` from `loop.playbackRevision` +
`loop.publishedSnapshotIdentity()`. A successor that published during finalization could
therefore change what the predecessor resolved against. `CaptureStopJob` now records
`causalBase` (the admission-time `PlaybackViewSourceIdentity`), pins its root with an
`ImmutableLoopSnapshotRef`, and sets `causalBaseRecorded`; the pin happens before the
raw-range ownership transfer (admission denies `causal_base_unavailable` when a non-null
published identity cannot be pinned) and retirement clears the receipt. Increment A
records only — consumers still read live state, so behavior is unchanged — and increment B
(next) switches the worker and the atomic commit to the recorded identity, deciding the
epoch-qualified publication rule including STOPPED recovery. New native
`test_deferred_stop_records_and_pins_its_causal_predecessor_source`; full native 2,191
cases — 2,186 passed, 1 skipped, the same three intentional public-reentry failures;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom.

**2026-09-16 task 2.3 prepared companion history interval qualification.** The last
item of the session/preview/undo/snapshot receipt slice: the Loop-owned
`preparedCompanionHistory_` is borrowed worker scratch that outlives one grain, so a
successor interval could observe, advance, commit, recycle or discard a
predecessor's in-flight preparation. Every consumer now carries the owning interval
identity, `PendingStopAdmission::intervalToken()` — a value (frozenGeneration +
stopTick), not an object address, so a copy of an admission made for credit probing
or retargeting still identifies the same interval while a reused ring descriptor
gets a fresh token; zero means the live session path. `preparedCompanionHistoryOwnedBy`
treats Cold/Prewarmed as receipt-free and owner-agnostic; `begin` records the token
and recycle completion/discard clear it. The deferred stop worker, the stop-commit
helpers and `beginCollectOverdubStopUndoPasses` pass `stop.intervalToken()`; default
arguments keep the live path source-compatible. New native
`test_prepared_companion_history_is_job_qualified`; the two Track-level fixtures in
`test_playback_prewarm` now begin the preparation with the admission that consumes it
and had their stale unqualified consumers qualified rather than deleted. Full native
2,190 cases — 2,185 passed, 1 skipped, the same three intentional public-reentry
failures; `teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom (the Loop
member lives in the externally allocated pool, so the token adds no RAM1 variables).

**2026-09-16 task 2.3 session-exit handoff receipt ring.** The session-exit
`SessionExitSnapshotHandoff` receipt is no longer a single-slot unique pointer: it is
a Loop-owned FIFO ring of `kSessionExitReceiptCapacity = 4`, external-memory-first
allocated and cold-prewarmed by `Loop::prewarmSessionExitSnapshotHandoff()`. This
resolves the "Finding 2" blocker from the deferred-stop receipt audit — the receipt
outlives the stop job, so it cannot be job-owned. `canHandoffSessionExitSnapshot()` is
now a *free-slot* gate; "one receipt per open session" moved inside
`handoffSessionExitSnapshot()` behind `sessionExitHandedOff_` (set by the handoff,
cleared by `openOverdubSession()`), so a successor session is never serialized behind
an unconsumed publisher turn while a repeat handoff for the same session is still
refused. The idle publisher consumes exactly the oldest receipt per turn and retains
it when construction is disallowed; playback's stale-root overlay folds every pending
receipt (passes and companion ids) rather than one. New native
`test_session_exit_receipt_ring_is_fifo_and_not_serialized`; the old
"unconsumed receipt closes the gate" assertions in `test_playback_prewarm` and
`test_loop_take_survival` were rewritten to the ring contract, not deleted. Native:
`test_playback_prewarm` 165/165, `test_loop_take_survival` pass, full suite 2,189
cases — 2,184 passed, 1 skipped, the same three intentional public-reentry failures.
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom (13 banks, code
425,208). No new boot allocation, so the increment A/B device memory check (build
`91e6b38c…`) still covers this slice.

**2026-09-16 task 2.3 increments A/B boot abort — bisected, root-caused, fixed.**
The memory check found the increment A/B firmware would not boot (`a7f10733…` emitted
only `boot_reset,crash=1` before the USB port dropped; the 32-entry-prewarm variant
`8c03dcfc…` behaved the same). A one-commit-per-flash hardware bisect isolated it to
`0f398715` (increment A): `7a275d1f` boots (21386 B, crash=0) while `0f398715` and
`162df28f` produce 0 bytes. Root cause: `prewarmCaptureStopJobs` reserved
`kRecordsPerBridge` (2048) rows for each job's `undoPassIds`/`undoCompanionIds`, both
`InternalHeapFirstAllocator` vectors — 4 ring entries × 8 tracks × 2 lists × 2048 × 4 B
= 512 KB demanded from a ~340 KB internal heap at boot, so the allocation aborted
before the sketch could stream. Fixed by bounding that reserve to
`kUndoCollectionReserveRows = 64` (16 KB); a session longer than that publishes no
prepared undo entry and takes the plain commit. The fixed tip is flashed and verified:
build `91e6b38c…`, 22091 bytes, 0 warnings, prewarm `PSRAM 5,141,952 -> 3,775,872`,
`heap 352,256 -> 290,816` — about +79 KB PSRAM and +20 KB internal heap over the
reference (`captures/task23-undo-prewarm-fix/result.json`). Native: 2188 cases, 2183
passed, one skipped, the same three reentry failures. Lesson recorded: a per-entry cold
reserve multiplied by ring capacity and track count must be sized against the
allocator's heap tier, not the policy maximum — both this and the 512-entry undo
prewarm were caught only by the boot sample.

**2026-09-16 task 2.3 deferred protected undo publication wired (increment B).**
The normal deferred overdub stop now records its `OverdubPassAdded` entry, closing
the long-standing "protected undo publication not connected to the production stop
pipeline" gap for the prepared PLAYING route. `TrackUndoPublication` is externally
allocated (same pattern as `captureStopJobs_`) and cold-prewarmed by
`prewarmProtectedUndo()`, which also reserves the undo stack's own entry capacity
(`Config::ABSOLUTE_MAX_UNDO_ENTRIES`, 512) — that is a publication precondition,
because `begin` rejects a transaction whose cursor reached capacity; its device
PSRAM admission must be measured. `stepDeferredUndoPublication` runs `begin` +
bounded `stepPrepare` inside the existing `UndoCollection` phase (no new phase). The
atomic transfer proves `canPublish(...)` first and only then calls
`advanceAtomicTransferWithUndo`; otherwise it takes the plain commit and the
transaction is restored rather than left half-open. `stepProtectedUndoDrain` runs one
bounded quantum per DeferredCleanup turn (`stepReleaseRedo` → `beginTrimAfterReclaim`/
`stepTrim` → `finish`, plus a `beginRestore`/`stepRestore` + `recoverPreparedEntry`
path), so `protectedState()` is Idle again before the job retires.

Evidence: `test_deferred_stop_prepares_job_owned_undo_entry` now also runs the stop to
completion and asserts the transaction returned to Idle and exactly one
`OverdubPassAdded` entry for the job's slot is published. `test_continuous_capture`
30 pass with the same three intentional reentry failures; full native 2,188 cases —
2,183 passed, one skipped, the same three; `teensy41-capture-serial` builds with
6,752 bytes RAM1 headroom (code 425,208). Known deviations: the DEC-063 bounded
disabled-pass reclaim continuation is still unwired (trim pressure is measured
without it), and STOPPED/empty routes publish no protected entry because they skip
companion preparation.

**2026-09-16 task 2.3 deferred undo collection, job-owned prepared entry (increment A).**
The deferred overdub stop now collects its session undo identities in bounded grains
and builds the `OverdubPassAdded` entry from the job's own slot and baseline. New
`PendingStopPhase::UndoCollection` runs on the prepared PLAYING route between
`CompanionHistory` and `CompanionBridge` (the collection needs the current prepared
companion receipt); the STOPPED/empty routes skip companion preparation and never
enter it, and `retireSkippedCompanionPreparation` retires the estimate. A dedicated
`remainingUndoCollectionGrains`/`undoCollectionQuantum` class (sized from
`Loop::requiredSessionStopUndoCollectionGrains`) is included in
`finalizationComplete()`, so a saturated estimate denies admission instead of
stalling a phase. `Track::stepDeferredUndoCollection` drives the resumable collection
and calls `TrackUndo::prepareOverdubSessionEntry`; the collection's private outputs
are pre-reserved in `prewarmCaptureStopJobs` (the API never grows them, and an
unreserved output is a denied collection, not silent corruption).
`Loop::reclaimUnreferencedDisabledEditPasses` moved to flash to hold 13 ITCM banks.

Evidence: new native `test_deferred_stop_prepares_job_owned_undo_entry` (production
route: entry kind/slot/loop from the job, baseline pointer identical to the job's).
`test_continuous_capture` 30 pass with the same three intentional reentry failures;
full native 2,188 cases — 2,183 passed, one skipped, the same three;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom. Increment B (next):
externally allocate `TrackUndoPublication`, begin/stepPrepare from the prepared entry,
flip the atomic transfer to `commitWithUndo`/`commitStoppedWithUndo`, and drain
`ReleaseRedo → AwaitReclaim → Trim → Complete` in bounded DeferredCleanup turns.

**2026-09-16 task 2.3 deferred-stop receipt audit — design decision required.**
Auditing the last two receipt items found the consumer side is not wired, which
changes what "job-owned" must mean. (1) The normal deferred overdub stop publishes
**no** `OverdubPassAdded` entry: `pushOverdubPassAdded` is reached only from
`Track::finalizeCommitSideEffects` via the fault fallback `commitCaptureForStop`,
while `processDeferredOverdubStopGrain` calls `TrackPreparedStopCommit::commit` with
`undo = nullptr` (`commitWithUndo`/`commitStoppedWithUndo` have no callers) — the
standing "protected undo publication not yet connected to the production stop
pipeline" gap. So the job-owned undo baseline from the previous slice is correct
preparation but currently unreachable in production, and wiring the deferred
publication is what makes it load-bearing. (2) The session-exit receipt cannot be
job-owned: it is deliberately retained past the job and consumed later by the idle
snapshot publisher while playback reads it for the stale-root overlay, so it needs a
Loop-owned multi-slot ring; `canHandoffSessionExitSnapshot()` should test a free slot
rather than an unconsumed singleton. Open questions: receipt ring size and the
overlay's behaviour with more than one pending receipt. See
CONTINUOUS-CAPTURE-OWNERSHIP.md ("Deferred-stop receipt wiring — audit and decision
required"). No code was changed for these two items.

**2026-09-16 task 2.3 job-owned overdub-session undo baseline.** The stop-commit
path (`TrackUndo::pushOverdubSessionOnStop` -> `pushOverdubPassAdded`) built the
`OverdubPassAdded` entry's `beforePublishedSnapshot` from the live
`Track::overdubSessionBeforePublished_`, which `TrackUndo::beginOverdubSession`
sets at session open — so a successor session would have overwritten the
predecessor's baseline. `CaptureStopJob` now owns
`ImmutableLoopSnapshotRef beforePublished` / `hasBeforePublished`; admission moves
the live baseline into the job and clears the live member, and the consumer prefers
the head job's owned baseline (then the live session's, then a fresh capture). An
unconsumed baseline is retired with the job.

Evidence: new native `test_stop_job_owns_overdub_session_undo_baseline` (job owns
the baseline, live member clear, a later successor baseline leaves the job's
intact, flag gone after cleanup); `test_continuous_capture` 29 pass with the same
three intentional reentry failures; full native 2,187 cases — 2,182 passed, one
skipped, the same three; `teensy41-capture-serial` builds with 6,752 bytes RAM1
headroom. Still open in this slice: the session-exit snapshot handoff receipt and
the prepared-companion history.

**2026-09-16 task 2.3 job-owned capture preview.** The deferred cleanup seals the
interval's preview, but it consumed the *live* `Loop::capturePreview`, which a
successor's `beginCapture` clears — so a successor would have made the predecessor's
deferred seal retain the wrong rows. `CaptureStopJob` now owns `CapturePreview
preview`, transferred at admission by constant work (`CapturePreview::swapWith`,
per-vector swap, no copy and no allocation) with a job-identified read borrow.
`Loop::capturePreviewView()` is the single readable accessor — the borrowed job
preview while bound, otherwise the live member — and is deliberately out of line in
flash, because inlining the branch at 40+ display call sites cost a whole 32 KB RAM1
bank. Writers keep the `capturePreview` member; 45 read-only display/Edit sites were
migrated (no display/Edit code writes the preview). `TrackPreparedStopCommit::completeDeferredCleanup`
now takes the job's owned preview, so the deferred seal retains this interval's rows.

Evidence: new native `test_stop_job_owns_capture_preview` (production route) and
`test_capture_preview_borrow_release_is_owner_identified`; `test_continuous_capture`
28 pass with the same three intentional reentry failures; full native 2,186 cases —
2,181 passed, one skipped, the same three; `teensy41-capture-serial` builds with
6,752 bytes RAM1 headroom (13 ITCM banks). The per-descriptor capacity prewarms were
dropped: the transfers are allocation-free regardless, and they only added ~640 bytes
of new ITCM template code (14th bank). Still open in this slice: the overdub
session/undo before-snapshot, the session-exit snapshot handoff receipt, and the
prepared-companion history.

**2026-09-16 task 2.3 job-owned pending note changes.** First piece of the
session/preview/undo receipt slice. The worker previously read the *live* session's
`Loop::pendingNoteChanges()` while resolving the canonical tail, so a successor
interval would have mixed its Add/Shorten/Hide intents into the predecessor's
finalization. `CaptureStopJob` now owns `PendingNoteChangeVec pendingChanges`:
admission swaps the live set into the job in constant work and leaves the live
session the job's prewarmed empty vector, so a successor starts independent and
neither the swap nor the next interval allocates. `Loop::pendingNoteChanges()` is
the single readable accessor — it returns the job's owned set while a
job-identified borrow is bound, otherwise the live member — and writers keep using
the live member directly. The borrow is released with the job and is
identity-guarded, and the worker now resolves
`beginFrozenCanonicalTail(frozenSource, job.pendingChanges, targetSource)`.

Evidence: new native `test_stop_job_owns_pending_note_changes` (production route,
real Shorten intent, borrow read, independent live set after cleanup) and
`test_pending_change_borrow_release_is_owner_identified`; the existing
shorten-and-complete stop test now exercises the job-owned path.
`test_continuous_capture` 26 pass with the same three intentional reentry
failures; full native 2,184 cases — 2,179 passed, one skipped, the same three;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom. Still open in this
slice: `capturePreview` ownership across the stop, the overdub session/undo
before-snapshot, the session-exit snapshot handoff receipt, and the
prepared-companion history. Task 2.3 stays unchecked.

**2026-09-16 task 2.3 sealed raw-range ownership wired (approved transfer).**
`CaptureStopJob` now owns `LoopEventStore::CaptureRange rawRange`.
`Track::beginDeferredOverdubStop` proves `LoopEventStore::canSealCaptureRange`
before any mutation, seals the producer extent at punch-out and binds a
job-identified read borrow on `Loop`; a denied admission performs no seal and no
transfer, and an empty capture (no extent) skips the seal. `Loop::captureReadSource()`
exposes the producer and finalizing extents separately (mutual exclusivity is not
encoded) and `copyCaptureIntervalEvents` orders only a scratch copy, so the borrowed
sealed extent is never mutated; `findLastCaptureNoteOnIdentity` resolves closure
NoteId from the finalizing extent first. `Track::retireJobCaptureSourceGrain` retires
the owned extent in bounded grains, drops the borrow only while it still identifies
that job, and re-arms the descriptor's reserved destination (failure → explicit
denial at the next admission). `resetCaptureExtentPreservingReservedStorage` lets
`beginCapture` keep the successor directory a cut left behind. DisplayManager is
unchanged; the successor-capture guard stays in place deliberately.

Evidence: five new native regressions (seal transfer + successor capture
preservation, read-source row parity with byte-identical sealed extent after an
ordering merge, closure identity from the sealed extent, owner-identified borrow
release, denial performs no transfer) plus three migrated ownership assertions.
`test_continuous_capture` 24 pass with the same three intentional public-reentry
failures; full native 2,182 cases — 2,177 passed, one skipped, the same three;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom. All new functions
are FLASHMEM and `Loop::beginCapture`/`discardCapture` moved to flash to hold 13
ITCM banks. Device boot prewarm admission **PASS** on this build (`41b30d18…`,
`captures/task23-sealed-range-prewarm/result.json`): PSRAM free 5,142,912 ->
3,856,320 B (160,824 B/slot, unchanged from the dual-staging build), internal heap
352,256 -> 311,296 B, pool chunks 512 -> 416 — the +4 chunks/slot are exactly the
four `CaptureRange::reserveTail()` destinations per track; all slots `ready=1` with
8 credits and zero warnings. Task 2.3 stays unchecked.

**2026-09-16 task 2.3 FIFO/queue-head binding landed; raw-range wiring needs an ownership decision.**
The stop-descriptor ring is now the explicit admission authority: `captureStopJobTail()` is the
admission target (null when full, reported as a distinct `queue_full` stop stage), the worker
binds `captureStopJobHead()` once per grain instead of re-resolving the ring mid-step, and
`!STOP_STATUS` now reports `queue=<depth>/<capacity>`. Behaviour is unchanged while one job is
admitted (tail == head); the binding is what makes multi-job admission well-defined. New native
`test_stop_ring_admits_at_tail_and_binds_worker_to_head` and
`test_full_stop_ring_denies_admission_and_preserves_accepted_capture`; full native 2,178 cases —
2,173 passed, one skipped, the same three reentry failures; `teensy41-capture-serial` builds with
6,752 bytes RAM1 headroom.

The sealed raw-range audit then found a real blocker: during the deferred stop, `Loop::capture.store`
is still read by the Loop display-window merge helpers (`mergeCaptureStoreIntoWindowedEvents` /
`...MaterializedEvents`, which also *sort* it in place) and by `Track::appendCaptureNoteOffAtPhase`
for physical-closure note identity; `Loop::beginCapture` also frees a pre-reserved successor tail.
Sealing at punch-out without a Loop-internal read fallback would empty the live-capture overlay for
the whole finalization window. The recommended design (Loop-owned producer + job-owned
`CaptureStopJob::rawRange` + a Loop-internal producer-or-sealed read fallback, so DisplayManager is
untouched) is recorded in CONTINUOUS-CAPTURE-OWNERSHIP.md. Because it moves capture-interval
ownership between `Loop` and the admitted Track job, the architecture ownership-transfer protocol
applies and it needs approval before implementation. Task 2.3 stays unchecked; no upload occurred
in this increment.

**2026-09-16 task 2.3 ingress/staging isolation — dual capture-admission roles landed.**
Split the runtime's single `RuntimePlaybackViewSet::captureStaging` into two independently
prewarmed roles in the same owner (`captureStagings[2]` with `liveStagingIndex` /
`frozenStagingIndex`). Live ingress (`Track::recordMidiEvents` admission, `Track::noteOn`
reservation, the cross-wrap continuation, and the `startOverdubbing` reset) writes only the
live role; the one admitted stop job borrows the frozen role. `Track::beginDeferredOverdubStop`
freezes the live role and exchanges the two indexes in constant work
(`LoopPlaybackRuntime::exchangeCaptureStagingRoles`), so the job owns the rows it captured and
the successor interval gets a physically separate, clean ingress buffer.
`Track::processDeferredOverdubStopGrain` resets only its own retired frozen role at cleanup.
No new owner/module, no canonical/persistence/SD ownership change, no action-edge copy; the
exchange is two index bytes. Physical NoteOffs of already-open notes select the frozen role
while a job is pending and the live role otherwise.

Evidence: new native `test_playback_prewarm::test_capture_staging_roles_exchange_without_sharing_physical_storage`
and `test_continuous_capture::test_stop_job_and_live_ingress_own_distinct_staging` (production
Button A punch-out route). Focused: `test_playback_prewarm` 164/164; `test_continuous_capture`
18 pass with the same three intentional public-reentry failures. Full native 2,176 cases —
2,171 passed, one skipped, the same three reentry failures. `teensy41-capture-serial` build
passes: RAM1 local-variable headroom 6,752 bytes, RAM2 free 472,928, ITCM padding 232.
Cost: one extra 2,048-event staging buffer per fully-prewarmed runtime slot (external-first),
now counted by `RuntimePlaybackViewSet::fixedExternalBytes()`. Boot prewarm admission was
measured on device (`captures/task23-staging-prewarm/result.json`, build `fb4ab3a9…`): PSRAM
free 5,143,968 → 3,859,680 B across the 8 boot slots (160,536 B/slot — two stagings per slot,
no internal-heap fallback), all slots `ready=1` with 8 credits, no warnings, 46.1 % of the
8.38 MB pool still free after prewarm. The measurement used a capture-only extra boot line
(`DIAG,capture_prewarm,phase=before|after`, SESSION_CAPTURE-gated, no behavior change).
Task 2.3 stays unchecked — FIFO/queue-head binding, sealed raw-range wiring, causal
predecessor/source, per-job session/preview/undo/snapshot receipts, the selected-slot
full-prewarm/dense-content footprint and the three reentry cases remain open. See the active
change's CONTINUOUS-CAPTURE-OWNERSHIP.md and DEC-064 follow-up.

**2026-09-16 SEVT device verification — operative cause is the deferred overdub-stop completion, not #1.**
Re-evaluation requested at the SPLIT gate ("re-evaluate whether #1 is necessary from fresh
evidence"). A session-capture-gated decision trace was added to the SEVT lifecycle
(`DIAG,stored_verify,queue_skip_loop|queue_skip_unarmed|queue_accepted|drain_reset_loop|
drain_reset_heap|drain_reset_no_events|drain_emit_start|drain_complete`) and run on device.

- **Evidence (fresh boot, `captures/sevt-diag-stop/session_20260916_191231.log`):** a
  transport Stop while the selected track is OVERDUBBING produced **zero** `stored_verify`
  markers — no `armed`, no `queue_*`, no `drain_*`. The `DIAG,stop_grain` trace advances the
  deferred job to its terminal phase and `PERS`/`SAVE` complete afterwards, so the dump is not
  "cancelled": **it is never requested.**
- **Causal chain:** `Track::stopOverdubbingToStopped()` normally takes
  `beginDeferredOverdubStop(...)` and returns; the overdub pass is staged and later finalised
  through `TrackPreparedStopCommit::completeDeferredCleanup` ->
  `Loop::handoffSessionExitSnapshot(OverdubStopToStopped)`, which never reaches
  `Track::finalizeCommitSideEffects`. The only call to `queueDeferredStoredMidiVerification()`
  is inside `finalizeCommitSideEffects`, and it is reached from the *synchronous* fallback
  `commitCaptureForStop(OverdubStopToStopped, ...)` at `TrackOverdubLifecycle.cpp`. The
  deferred path — the normal one — has no equivalent call.
- **#1 as previously framed does not fix this.** Arming before state dispatch in
  `TrackManager::handleTransportStop()` only makes an *existing* queue call succeed; on this
  path there is no queue call to arm. Keep #1 frozen/closed and re-scope to the missing call.
- **#2 remains valid but is not the operative cause here** either: it protects a *queued* dump
  from an ineligible re-queue. In this flow nothing was ever queued. Keep the landed fix and
  its native regressions; do not bundle it with the new scope.
- **What did emit SEVT (`164505`)**: its `armed,1`/`armed,0` pair comes from
  `Track::stopPlaying()`, i.e. a transport stop while the selected track was *PLAYING*, not
  overdubbing. `stopPlaying()` re-arms and `rearmDeferredStoredMidiVerification()` calls the
  queue. That path is why the good run scored SEVT=511 and why runs that stop *during*
  overdubbing score zero.
- **Fix landed (was the re-scoped #1):** `Track::processDeferredOverdubStopGrain` now calls
  `queueDeferredStoredMidiVerification()` immediately after a successful
  `TrackPreparedStopCommit::completeDeferredCleanup`, so the deferred completion requests the
  dump at the same point the synchronous `finalizeCommitSideEffects` route does. One owner per
  commit route; the non-deferred fallback is untouched. Native regression
  `test_deferred_overdub_stop_completion_queues_stored_verification` in
  `test_continuous_capture` fails before the fix and passes after (the STOPPED destination
  itself cannot be driven to completion natively because it calls `silenceTrackMidiOutput`, so
  the shared completion is asserted on the PLAYING destination).
- **Device verification (deterministic, sanctioned stop-reproduction recipe
  `scripts/hitl_stop_interruption.py`):** SEVT=98 (`captures/sevt-fix-verify1`) and SEVT=196
  (`captures/sevt-fix-verify2`), each with `queue_accepted` -> `drain_emit_start` ->
  `drain_complete`. Verify2 also shows the one-shot contract intact: a second cycle
  re-arms/queues/emits, and the following redundant stop logs `queue_skip_unarmed` without
  destroying a dump.
- **The plain canonical `--preset base` still cannot demonstrate SEVT, for host reasons only:**
  on an empty slot its Loop-row select starts recording and the following clear wipes it
  (`skipped_empty`, `captures/sevt-fix-verify4`), and on a seeded slot it aborts at the
  overdub-stop press and tears the capture down
  (`captures/sevt-fix-rerun10`). Restoring `sevt_gate_required = True` therefore still needs
  either a canonical host that observes past the stop press, or an accepted decision that the
  stop-interruption recipe is the SEVT gate.
- **Temporary artifacts still in the tree (remove together after the gate is restored):**
  temporary C (host advisory SEVT gate) and the committed session-capture SEVT decision
  markers `bf8fb6a4`.

**2026-09-16 stored-MIDI verification lifecycle — bugfix #2 landed, #1 frozen (SPLIT gate).**
RCA for the missing `VERIFY stored overdub span (SEVT ticks)` produced two independent
causes, so the fix was split rather than bundled:

- **#2 (landed):** `Track::queueDeferredStoredMidiVerification` called
  `resetDeferredStoredMidiVerification()` *before* the eligibility guards, so a redundant
  or ineligible call (arm already consumed, or loop not representable) cleared an
  already-pending dump. Invariant now explicit: **an ineligible queue call must not
  destroy an already-queued verification dump.** The guards are evaluated first; the
  reset/replace happens only once the call is accepted. The arm/pending bookkeeping is no
  longer `SESSION_CAPTURE`-only so native tests can drive the lifecycle. Native regression:
  `test_redundant_stored_verification_queue_keeps_pending_dump` and
  `test_ineligible_stored_verification_queue_keeps_pending_dump` in
  `test_continuous_capture` (fails before the fix, passes after; that TU had no native
  coverage before). Firmware `teensy41-capture-serial` build SUCCESS.
- **#1 (frozen, gate OPEN):** transport-stop arming. `Track::stopOverdubbingToStopped()`
  never re-arms the one-shot dump; `Track::stopPlaying()` is the only re-arm. Ordering is
  resolved: the overdub branch reaches `commitCaptureForStop` -> `finalizeCommitSideEffects`
  -> `queueDeferredStoredMidiVerification()` **synchronously inside** the
  `TrackManager::handleTransportStop` loop, so any arm must be established **before state
  dispatch**, restricted to `isOverdubbing() || hasPendingDeferredOverdubStop()`, preserving
  the existing PLAYING re-arm. Do **not** implement until the rerun below says it is needed.
- **Cause #2 may already explain the 175335 failure** (it logged two `ODUB,stop,prefix_reuse`
  finalize attempts). Next: rerun the 175335-style canonical base on the #2 firmware and
  see whether SEVT appears without touching transport arming. If it does, #1 becomes a
  separate robustness question; if not, proceed to the #1 gate with cleaner evidence.
- **Temporary C stays until deterministic device verification.** The host gate reports a
  missing SEVT dump under `VERIFY advisory` (`sevt_gate_required = False`) rather than
  failing the run. Restore it to required only after the rerun deterministically emits SEVT.

**2026-09-16 dense snapshot representability + fragment transaction (openspec 2.3 /
immutable-loop-snapshot-passes 3.7):** the captured record/overdub pair is now a
native snapshot regression (`test_dense_device_capture_snapshot_preserves_every_committed_row`,
fixture `dense_capture_fixture.h`) that failed `Expected 734 Was 710`. Root cause:
bar 2 needs 260 event references, above the fixed 16-page (256-reference) per-bar
membership ceiling; `LoopSnapshotConstruction` failed at `appendCurrentReference`,
and because `appendNextPassChangeFragment` had already advanced
`PendingPassChangeProgress`, the failed fragment was dropped and a truncated
candidate published. Two bugs: representability and atomicity (DEC-047 item 6).
Fix: explicit `kSnapshotMembershipMaxRefsPerBar = 4096` envelope with derived
page-count limit applied equally to event-bar and note-bar membership and lazily
allocated pages; page counters widened to `uint16_t`; fragment incorporation is now
transactional (build on a working copy, commit authoritative progress only after
successful incorporation, discard the partial candidate on failure); failure reason
distinguishes transient/allocation retry from persistent representability, which
parks publication after two consecutive failures (`DIAG,snapshot_fragment`,
capture-gated). DEC-047 amended and ExecutionBounds rows 21/22 updated. New focused
tests: 257-reference boundary, partial-apply retry exactly-once, and publication
parking. Focused `test_loop_take_survival` 132/132; full native 2166 passed / one
skipped / three known task-2.3 reentry failures; `teensy41-capture-serial` SUCCESS
with RAM1 free for locals 6752 (unchanged).

**2026-09-16 hardware verification (representability PASS):** uploaded build
`5bb90b47bdae299d630be640…` and reran the dense record/overdub recipe plus the
content-dump/playback capture (`captures/task23-dense-snapshot-fix-record`,
`captures/task23-dense-snapshot-fix-playback`). Stored 766 effective rows
(record 190 + overdub 576) with bar 2 at **270 refs (> the old 256 ceiling)**.
Four restart cycles each played 765 rows with **0 duplicates**; the only absent
row is the pre-existing terminal loop-boundary Off (`NoteId=0`, tick 3839), which
is ledger/pairing semantics rather than the capacity defect. Exact ordering still
differs (pre-existing). So the 24-row omission class is fixed on hardware.

**2026-09-16 dense re-stress (representability re-confirmed):** after a fresh
upload/reset, `captures/task23-dense-fix-rerun3` stored 732 effective rows with
bar 2 at **260 refs (> 256)** and the focused interruption checkpoint **PASS**
(stop +10.1 ms, all-notes-off 3.0 ms, 19.0 s silence). Four restart cycles played
730 rows with **0 duplicates**; the two absent rows are ledger/pairing semantics,
not capacity — the loop-boundary Off (`NoteId=0`, tick 3839) and one of two
coincident same-pitch Offs (`NoteId` 26/138, tick 1669). The base preset's own
setup verdict stayed `SETUP_UNCONFIRMED` (`overdub_unmatched_open_notes`), which
is the dense generator leaving notes held at stop, unrelated to the snapshot fix.

**2026-09-16 HITL harness exit hardening:** the managed-capture runner could leave
a `capture_session.py` child holding the USB serial port if the scenario stalled
or the harness was killed (observed after the dense verification run). Fix:
`run_with_managed_capture` now runs the scenario behind a wall-clock watchdog
(`HITL_MANAGED_RUN_TIMEOUT_S`, default 360 s; `0` disables) and returns 3 while
the context stops the capture child; `capture_session.py` exits when its owner
dies via a parent-death pipe (`--owner-fd`) with a reparent fallback; `stop()`
closes the pipe and escalates SIGINT→SIGKILL. Host coverage:
`scripts/test_managed_capture.py` 11/11 (watchdog return/abort/disabled/exception
+ owner-pipe orphan exit).

**2026-09-16 latest checkpoint (14:29):** descriptor build a9a7217f uploaded on
retry; dense hardware test then lost USB during initial recording, before overdub.
No reboot marker or fault cause retained; serial port absent. Native full run has
2,159 pass / one skipped / three known reentry failures; final focused guard run
has 14 pass / the same three failures. Stored dense content verification on the
prior fixture passed its scoped input comparison; audible timing and restored
playback remain open. Device reconnection is required to continue hardware
investigation. No capture reader is running. See active FOCUSED-STOP-HITL.md.

**2026-09-16 dense verification / FIFO descriptor slice:** stored dense overdub
content matches input (568 events, three chunks, five expected short-pair removals).
Timing is NOT passed: a stop ordering grain crossed a service deadline and two
late clock samples are retained. Restored musical playback produced no target
channel rows in the probes; output policy/readiness remains under investigation.
Stable external Track descriptors replace the singleton stop receipt; sequential
rotation passes, but staging/session/predecessor isolation and three reentry tests
remain open. Full native: 2,159 pass, one skipped, three expected reentry failures;
final focused admission/rotation: 14 pass and the same three failures. Build passes
with 6,752 bytes RAM1 headroom. See active FOCUSED-STOP-HITL.md for evidence and
latest upload status; no task checkbox closes.

**2026-09-16 dense reboot fix verified:** matching fault report recovered into
crash-log-retrieval.log: stack-boundary DACCVIOL in _svfprintf_r. Overlap diagnostic
scratch moved to RAM2; local frame reduced 1,372 -> 76 bytes. Two dense automated
runs now complete without reboot; adjusted Stop-window run passes focused silence
(18.9 s observation). Broad baseline/content/timing checks remain inconclusive;
no FIFO/reentry/task-2.3 completion. Firmware hash and details are in active
FOCUSED-STOP-HITL.md. All serial/MIDI runners are closed.

**2026-09-16 automated dense rerun:** same track-5/slot-1 recipe reproduced the
reboot; corrected boot marker is crash=1. Serial missed the early fault text;
SD crashlog has not been read. Runner/capture finished. The dense gate remains
open; no manual reproduction is required. See task23-dense-crash-report-rerun
and active FOCUSED-STOP-HITL.md.

**2026-09-16 overlap reboot follow-up:** user confirms spontaneous reboot during
record-pass/overdub overlap. Native Shorten/stop regression passes; root cause
remains unresolved. Fixed destructive double-read of Teensy CrashReport: startup
now preserves the crash flag and renders once to Serial and SD. Build/upload pass,
with unchanged 5,600-byte RAM1 headroom. Human overlap reproduction on track 5 /
slot 1 is the next checkpoint; serial capture is in task23-overlap-crash-report.
See active FOCUSED-STOP-HITL.md for firmware hash and evidence limits.


**2026-09-16 device checkpoint (track 5 / slot 1, authorized):** verified firmware
uploaded. Two normal stop cleanups, focused transport-Stop silence, and three
restart content cycles pass. The denser run disconnected/rebooted before its stop
trace drained and is INCONCLUSIVE_CAPTURE; reset cause is unknown. No dense or
full task-5.3 acceptance. Updated host verifier recognizes pending-job completion
after visible PLAYING and passes 24 regression tests. Original reports and a
separate reevaluation are preserved under captures/task23-reset-*. See the active
FOCUSED-STOP-HITL.md checkpoint for exact build identity and limits. FIFO/reentry
implementation remains unfinished; no task checkbox closes.


**2026-09-16 pending-job reset protection / intermediate HITL checkpoint:**
An admitted stop job now retains its runtime staging and canonical-copy output
across cache reset. Reset retargets STOPPED and yields; Track invalidation preserves
the borrowed generation until terminal recovery. Native coverage exercises partial
copy and every stop phase, including held-note closure and receipt release.
A Tier-A prewarm record exposes output credits and pool/heap headroom. Verify
boot/load memory and single-job copy/Stop timing on hardware before expanding the
FIFO reservations: prior eager prewarm exhausted device PSRAM. This checkpoint is
not task-2.3 acceptance; the same three reentry fixtures remain red, and FIFO,
causal-source and preview/session/undo/snapshot migration remain unfinished.
No upload has been performed. Exact results and build identity are in the active
change's CONTINUOUS-CAPTURE-OWNERSHIP.md; the procedure is in FOCUSED-STOP-HITL.md.

**2026-09-16 task 2.3 worker-owner integration:** The existing stop worker now
uses a stopped-interval receipt for Loop/slot/phase/geometry and its own pending
notes. Closures target the saved owner; one-node retirement never clears live
notes, and completion cannot transition a newer Track interval. Production
empty, paired and held-note stops complete in native tests. Public A/B/C reentry
still fails: raw capture, staging, preview and undo/snapshot session migration
and the fixed FIFO remain open. Affected tests: 266 pass, the three known reentry
cases fail. Capture-serial builds with 5,600 bytes RAM1 local headroom; no upload.
Task 2.3 is not complete. See the
[ownership checkpoint](../../openspec/changes/resumable-midi-boundary-playback-maintenance/CONTINUOUS-CAPTURE-OWNERSHIP.md).



**2026-09-16 task 2.3: public-route reproducer and raw range foundation:**
The actual Button A → TrackManager → Track route and MidiHandler musical input
now run in a native fixture. First-interval capture/stop passes; A/B/C reentry,
manager same-pitch held-note reentry and direct Track reentry remain intentional
failing requirements. None of the three admission guards has been removed.

LoopEventStore now supports pre-reserved exclusive capture-range rotation,
read receipts, incremental release and stale rewrite/pool-lifetime rejection.
The focused range/persistence/event-store/playback-prewarm run passes 209 tests.
The full native run had 2,136 passes, one skip and only the three expected reentry
failures (before the final persistence-prewarm correction). This API
is not yet wired to Track; it does not fix immediate reentry. Job-owned staging,
closure credits, pending changes, undo/snapshot receipts, causal FIFO resolution,
cleanup isolation and device verification remain open. See the
[ownership checkpoint](../../openspec/changes/resumable-midi-boundary-playback-maintenance/CONTINUOUS-CAPTURE-OWNERSHIP.md).
Capture-serial build passes with 6,176 bytes RAM1 local-variable space after
placing the bounded rewrite maintenance worker in flash. Device timing remains
unverified. Task 2.3 stays unchecked; no firmware upload or archive was performed.

**2026-09-16 continuous preparation across punch-in/out:**
Canonical preparation now runs during PLAYING as well as OVERDUBBING, using the
existing VisualCache owner and otherwise-unused wrap-append turns. Existing
record, stop, wrap-append and source-span priority remains. A confirmed newest
pass append with unchanged companion history preserves completed normalized
passes and resumes the first unfinished/new pass; snapshot identity publication
at the same content revision also preserves completed normalization. Unproven
source changes or changed history retain the existing safe invalidation path.

The companion merge now retains final per-pass output, in its original order,
in the canonical owner's existing raw scratch buffer. One extra prewarmed pass
descriptor array is required (16 KiB at 2,048 entries on Teensy); no additional
event buffer or active allocation is introduced. Each existing merge addition
also writes its normalized scratch entry, within the same Q-entry grain.
Successful stop cleanup adopts these results with pointer swaps after matching
source/worker generations, including when the committed companions changed
history. Reselecting current descriptors/history is resumable; completed pass
normalization is not replayed. Selected bridge/base and audible cursor remain
untouched. A scratch-owner receipt prevents another builder from being mistaken
for the retained result. `prefix_reuse` / `prefix_restart` records describe the
low-frequency ownership transitions rather than logging every prework grain.

Task 2.3 and HITL acceptance stay open. This does not claim instant reentry:
frozen-tail/bridge/finalization work remains, and new history during a wrap may
invalidate normalization for all potentially affected passes. The provisional
6,240-grain baseline remains unchanged; added bounded stores need device timing
validation alongside rapid punch-in/out, wraps and original-Off preservation.



Continuous-preparation validation: full native suite 2,130 passed / one skipped;
final focused suite 194 passed, including superseded scratch-owner rejection.
Post-commit adopted per-pass events match fresh canonical replay across the
existing stop variants, including companion edits and the large-capture fixture.
Strict OpenSpec and diff checks pass. Capture-serial build succeeds with RAM1
local headroom 6,176 bytes, unchanged. Upload and one retry failed: the Teensy
loader reported no boards on USB and requested the physical program button;
no `cu.usbmodem` port was present. This firmware is built but NOT uploaded or
HITL-validated. Reconnect/program-button action is needed before retrying.

**2026-09-16 live canonical-base prework and stop estimate correction:**
The `003056` manual capture exposed a small-tail estimate incorrectly used as
candidate capacity. Stop now estimates from the prewarmed capture owner and the
prepared canonical base (or its conservative full capacity when unavailable).
The pass estimate includes record, existing overdubs and the incoming capture.
It no longer infers capacity from a rolling emission window or final tail count.

Live overdub and Stop share `stepCanonicalBase`: prefix normalization followed
by the existing pass merge and view fill/order worker, one child per owner turn.
The live VisualCache turn can prepare the immutable candidate bridge base while
recording the overdub. Paint dirtiness alone does not block this work while
visual reconstruction is disabled; wrap append and source-span work retain
priority. Source/storage changes invalidate prework; current playback and the
selected bridge remain untouched. Stop resumes partial work or reuses a ready
base. Frozen-tail, companion and cleanup work still remains after Stop, so this
is not proof of immediate reentry or a realtime deadline pass. Task 2.3 and the
HITL gates remain open; the provisional 6,240-grain baseline is unchanged.



Validation: full native suite 2,126 passed / one skipped; final focused suite
189 passed after the prefix-owner validation adjustment. Strict OpenSpec and
`git diff --check` pass. Capture-serial build and upload succeeded; serial
`/dev/cu.usbmodem154944801` returned. RAM1 local headroom remains 6,176 bytes.
The device upload finished at 00:44:27 local time on 2026-09-16. Manual reentry
and deadline acceptance remain open.

**2026-09-16 bridge boundary reconciliation implementation:**
`refreshFrozenCompanionBridge` now invokes the shared compiler's
`rebindBoundary` rather than resetting bridge storage. Records, mapping, base
ranks, sorted base order and complete insertion order survive. The existing
merge scratch retains the complete insertion permutation after sorting; steady
compaction no longer destroys the order needed by another boundary binding.
Reconciliation runs only inheritance, affected-record checks, closure resolution
and cycle-index output in the existing entry quantum. No allocation, copy of
canonical events, or sort is introduced during this path. Original carried Offs
are enabled only by a matching live closure and disappear after it closes.

Admission checks the relevant subset of the ledger using fixed pitch/identity
summaries built from all changed records and inherited closures. Identity hash
collisions conservatively request reconciliation; they cannot bypass protection.
Changes to unrelated notes no longer invalidate preparation. Changes to affected
notes, cycle or original-Off deadline retain the closure-only reconciliation
path; no note protection or command-epoch check is removed. These smaller steps
still require device service/reservation validation; no unconditional deadline
or maximum stop-latency claim is made.

Native coverage compares repeated bindings against fresh compilation over six
cycles, changes unrelated notes every preparation turn, verifies unchanged map
and bridge storage generations, and preserves an inherited original Off during
a second source change. Capture refresh telemetry now says `closure_only`.
The provisional 6,240-grain baseline remains unchanged. Full native validation
reports 2,124 passed and one skipped; final focused validation passes 153 tests.
Firmware build and upload retry succeeded with 6,176 bytes RAM1 local headroom;
serial returned. Task 2.3 and hardware acceptance remain open.


**2026-09-16 repeated bridge refresh blocks the third overdub:**
`captures/session_20260916_000904.log` confirms another overdub starts at
61.028 s. Its stop at 69.606 s remains pending. The retained trace contains
23 completed bridges immediately followed by refresh, from 72.194397 through
75.511650 s, at intervals of 144,372–158,456 us. Compilation captures a live
ledger/cycle/phase; admission checks exact ledger equality and the retained
Off horizon. Normal emission advances these while compilation runs, and
`refreshFrozenCompanionBridge` resets and recompiles the whole bridge. Repeated
ready/refresh is therefore not bounded progress toward admission. The first
stop finishes; the second stop does not, so later Button A presses correctly
retain the pending transaction. Ring overflow prevents a complete timing verdict.

Required correction: separate stable canonical mapping/ordering from bounded
current-boundary closure reconciliation, preserving progress under continuous
NoteOn/Off emission. Do not remove ledger validation, pause playback, wait for
silence, or increase the quantum as a substitute for a progress proof. Add a
moving-ledger regression with multiple consecutive source changes and verify
original Offs exactly once before treating another upload as a fix. This is
diagnosis and an implementation requirement, not implemented or device-verified.
Task 2.3 remains open.


**2026-09-16 post-finalization reentry starvation:**
`captures/session_20260916_000034.log` shows the populated bridge ready at
30.256907 s and phase-10 cleanup completed at 30.354501 s. Button A reaches
Live Overdub at 31.709, 35.371 and 37.227 s, then the combined transfer/snapshot
admission guard rejects it. Code inspection and a failing native reproducer
identify `serviceBridgeWindow` discarding a completed but unpublished future
window. It immediately rebuilds that window on subsequent turns, starving the
existing snapshot receipt consumer and leaving its admission slot occupied.
The Complete branch now releases the builder with `discard(&target)`, preserving
the ready/build destination. No snapshot admission policy is relaxed. The native
regression fails before this change and passes after, including repeated idle
yields, immutable future windows and the existing multi-cycle MIDI continuity
fixture (152 focused tests pass). Admission warnings now distinguish transfer
capacity from an occupied snapshot receipt. Full native validation: 2,123 passed, one skipped. Build/upload succeeded
with 6,176 bytes RAM1 local headroom; serial returned. Device reentry remains
unverified; task 2.3 and HITL/deadline gates remain open.


**2026-09-15 manual clean-stop routing regression:**
`captures/session_20260915_235602.log` records wrap commit at 48.314 s, Stop
Overdub at 48.428 s, and `bridge_begin` denial at device 48,438,934 us
(mask 1652 / 0x0674). Later Button A presses reach the finalization-pending
handler. The empty post-wrap candidate bypasses canonical/bridge preparation,
but the AtomicTransfer refresh guard incorrectly required a bridge for every
PLAYING destination and routed it back to CompanionBridge. The guard now
requires an actual pending capture pass. Empty stops proceed to existing
cleanup; populated PLAYING commits retain mandatory bridge preflight and
transport Stop retains precedence. Shared native routing coverage exercises
all three cases. The capture has ring overflow, so it cannot establish deadline
acceptance. Full native suite: 2,123 passed, one skipped. Firmware build/upload pass with
6,176 bytes RAM1 local headroom; serial port re-enumerated. Manual device
confirmation and task 2.3 remain open.


**2026-09-15 production closure ownership and rolling-window integration:**
The stop producer now builds its complete immutable canonical bridge base in
one of two external-memory source slots paired with selected/candidate bridges.
These bases are separate from the three rolling emission windows and survive
canonical/history worker cleanup. This removes the manual `partial_open_ledger`
trigger from production without weakening that guard for an unproved partial
base. There is no short/long-loop threshold and no new event-count admission test.

`RuntimePlaybackBridgeStream::source()` exposes the same compiled cycle cursor
to the existing coverage fill/order worker. `serviceBridgeWindow` schedules one
setup/fill/order/promotion turn and uses the existing three-view lifecycle.
Ordinary PLAYING consumes matching cycle-qualified windows. During source handoff
or delayed refill it continues the already compiled bridge cursor, with bounded
index acknowledgement to prevent replay when switching representations. This
compatibility path does not gather, sort or wait for a window. It remains until
reservation/HITL gates permit removal under task 5.4.

Admission uses the latest serviced phase and cycle. A changed live ledger or a
passed original-Off boundary refreshes only bridge compilation from the completed
mapping; canonical preparation remains intact. A second source change inherits
still-live original closure obligations from the selected bridge through the
existing inheritance/copy/mask phases. Its extra copy/sort estimate is separate
from the unchanged provisional 6,240-grain baseline, never a stop-admission ceiling.
Two source bases add 192,512 accounted fixed external bytes per fully prewarmed
runtime on the native layout; allocation remains in prewarm. Runtime reset is
placed in the existing cold flash section to avoid crossing an ITCM bank.

Native coverage now includes the production base producer, cleanup survival,
live-ledger refresh, rejection at an already serviced Off boundary, delayed window
refill with exact MIDI continuity, and an additional source edit before the
original Off. Device reentry and timing are not yet verified for this integration.
Validation: full native suite reports 2,123 passed and one skipped; the capture
firmware build passes with 6,176 bytes RAM1 local headroom. Logs are
`/tmp/finish-closure-all-native-final.log` and
`/tmp/finish-closure-firmware-final.log`. Upload completed successfully (`/tmp/finish-closure-upload.log`), but the
post-upload check found no `usbmodem` port; boot/runtime remain unconfirmed.
Task 2.3 and the full HITL/reservation gates remain open.


**Earlier checkpoint, superseded by the production integration above: initial-window closure inheritance foundation:**
`RuntimePlaybackBridgeBuild::beginInitialWindow` projects a completed immutable
closure context into an initial canonical window using the existing successor
Inherit/CarryOffs/cycle-mask phases. The context is preparation-only and need not
be audibly selected. It is generation-bound until compilation completes; the
initial ledger and cycle/phase must match. No new allocation or independent
closure algorithm is introduced. The native regression starts with an Off outside
the first window, crosses two successors, and checks original Off exactly once
then revised steady playback, changed-ledger denial and retired-context denial.
All 149 native prewarm tests pass; strict OpenSpec validation passes.

Production integration is NOT complete: no production caller currently invokes
`beginSuccessor`, and the active consumer still resolves full-loop views. A durable
initial closure-context producer, source lifetime through cleanup, current-phase
admission and shared runtime successor scheduling/handoff must be connected before
removing `partial_open_ledger`. The guard remains; this patch is not uploaded and
must not be presented as fixing the manual reentry failure. Task 2.3 remains open.


**2026-09-15 post-upload manual reentry: bridge closure guard confirmed:**
`captures/manual-reentry-after-fix-status-20260915.log` retains phase-8
CompanionBridge grains for track 5/slot 1, generation 7, PLAYING destination.
At device time 27,654,516 us, `bridge_compile` reports `partial_open_ledger`,
ledger count 10, coverage length 768 ticks. `RuntimePlaybackBridgeBuild::begin`
explicitly rejects a partial canonical window with a nonempty playback ledger
because closure obligations beyond that window are not yet owned across rotation.
Thus this run progresses beyond CompanionHistory but still cannot finish normal
stop or admit the next overdub. This is playback ledger state, not evidence of
held physical input notes. At retrieval the device was already STOPPED with no
pending stop and matching history generations (7); no transport command or Clear
was sent for retrieval. Ring overflow was reported, so only retained evidence is
claimed. Next implementation must carry closure obligations through shared rolling
coverage, not remove the guard or wait for silence. Manual reentry and task 2.3
remain open. The preceding fix was uploaded successfully before this retest.


**2026-09-15 companion-history begin binding fix (partial task 2.3):**
`beginPreparedCompanionHistory` now validates existing reserved row/ID storage
against current history and binds its storage generation/history count at begin.
A wrap's `saveNoteEditPass` publication can therefore advance canonical history
between prewarm and stop without permanently rejecting unused preparation
storage. In-flight generation checks remain strict; no worker is silently
restarted and no canonical-only publication bypass is restored. Setup remains
constant-time and does not resize/reserve containers.

The native regression failed at begin before the fix. It now verifies wrap
metadata retention, stable reserved buffers, second preparation after recycling,
capacity rejection and rejection of a canonical mutation after begin. All 148
prewarm tests pass. Capture firmware builds with 6,176 bytes RAM1 headroom.
This fixes a reproduced code path consistent with the manual phase-7 stall;
the original overflowed trace does not prove its exact initial invalidation.
Firmware has not been uploaded for this fix. Repeat the manual populated-wrap
DROID test before claiming device reentry is fixed; task 2.3 remains open.


**2026-09-15 manual DROID reentry regression remains open:**
`captures/manual-overdub-reentry-diagnostic-20260915.log` records first overdub
start at 155.001 s, a populated wrap at 157.426 s, stop request at 161.360 s,
and Button A at 163.858 s rejected with "Overdub finalization continues in
playback". Later retained grains stall in phase 7 (CompanionHistory), including
after transport Stop. This differs from the prior phase-8 bridge stall and
supersedes any inference that automated two-pass success proves manual reentry.
The user subsequently issued Stop/restart and long-press Clear; the read-only
status capture (`captures/manual-overdub-reentry-status-20260915.log`) therefore
cannot establish the original invalidation: it shows pending phase 7, prewarmed
history storage generation 5 versus current 9, and invalid canonical inputs.
Investigate prewarmed history invalidation across populated wrap publication;
do not bypass joint publication or simply relabel the visible state. The capture
ring overflowed, so missing early worker diagnostics are not evidence of success.
Task 2.3 and HITL acceptance remain open.

**2026-09-15 running-cycle bridge anchors (partial task 2.3):** Track's window,
bridge and atomic anchors now use `LoopPlaybackRuntime::currentCycleCursor`
instead of cycle zero. Serviced ordinary/overdub wraps advance that same runtime
counter when no bridge stream is selected. This prevents a newly published
bridge from beginning with next/steady-cycle masks because the runtime has
already advanced. Native closure regression runs at initial cycles 0 and 3,
checking the original cross-wrap Off emits once and retires; 147 tests pass.
Host probe/transition tests: 27 pass. The probe can send a late note in each pass
to exercise populated stop tails after a longer overdub interval.

Build/upload passed with 6,176 bytes RAM1 headroom (ITCM code 425,784 bytes).
Transport projection reanchoring now uses the existing cold flash section.
`captures/stop-cycle-wrapped-late-two-pass-20260915/result.json` passes: two
eight-second overdubs with late notes, two normal cleanups, no atomic-preflight
denials, final transport stopped. Service/emission row counts remain zero.
The phase component still uses the frozen stop phase. Current serviced-phase
admission, rolling coverage, protected undo, bounded cleanup and deadline proof
remain open; this is not completion of task 2.3 or a real-time reservation.

**2026-09-15 mandatory joint publication and Add-only revision fixed (task 2.3):**
the caller formerly fell through from a failed joint canonical/bridge preflight
to a canonical-only PLAYING transfer. Atomic dispatch now lives in the existing
`TrackPreparedStopCommit` helper, and a populated PLAYING denial leaves the
candidate pending. This exposed a target-revision error: pending Add changes
were counted as companion edits, preparing +2 while canonical commit uses +1.
The reserved tail now derives that revision from the existing bounded row
worker's completed count before preparing derived passes. No new input scan.

The production-dispatch/Add-only native regression passes with all 147 prewarm
tests. Build/upload passed with 6,176 bytes RAM1 headroom. The two-populated-pass
checkpoint `captures/stop-joint-revision-two-pass-20260915/result.json` passes:
two normal cleanup completions, no atomic-preflight diagnostic denials, no probe
errors, final transport stopped. Earlier cleanup-only passes did not establish
joint publication because the fallback was still reachable. Traces still lack
service/emission correlation; no MIDI deadline acceptance is claimed.
Next task 2.3 work: replace the stop-time handoff anchor with the continuously
serviced cycle/phase cursor and integrate rolling coverage without delayed launch;
protected undo and bounded cleanup remain open as well.

**2026-09-15 second-stop window ownership fixed (partial task 2.3):** the
selected bridge's canonical base remained in the ready slot after publication,
and cleanup left its completed window receipt attached. The next prefix could
finish, but its window request denied against that old receipt. A native second
window reproducer failed before the correction and passes now. Bridge publication
uses the existing preflighted ready-to-current lifecycle handoff; cleanup discards
the window receipt while preserving the now-current audible base. The infrequent
lifecycle handoff uses the existing flash-placement macro, avoiding a new ITCM
bank and preserving 6,176 bytes RAM1 local headroom.

Uploaded two-populated-overdub checkpoint
`captures/stop-window-handoff-two-pass-20260915/result.json` passes the strengthened
gate: two normal cleanup completions before final transport Stop, no probe errors.
The cleanup grains took 287/299 µs; complete stop-to-cleanup intervals were
240,823/244,832 µs. Neither duration is an emission-deadline verdict: this trace
has zero service/emission rows. Native playback-prewarm and take-survival suites
pass 147 + 124 tests. The failure below is superseded by this checkpoint.
Task 2.3 remains open for full current-phase/rolling handoff, protected undo and
bounded cleanup integration; task 4.1/5.3 real-time gates remain open.

**2026-09-15 normal-stop cleanup continuation (partial task 2.3):** removed
normal-stop silence, clearing of every playback ledger and cursor reanchoring.
The existing `TrackPreparedStopCommit` owner now performs terminal canonical
bookkeeping, retaining the selected audible bridge and retiring both completed
bridge and frozen-tail workers. Native tests exercise snapshot-handoff retry,
held-note Off emission after cleanup and next-prefix preparation using that
production body. Playback-prewarm: 147 passed; host probe/transition: 27 passed.
Capture build/upload passed with 6,176 bytes RAM1 headroom.

The tightened two-pass HITL is **not a pass**. In
`captures/normal-stop-cleanup-tail-two-pass-20260915/result.json`, both visible
OVERDUB_STOPPING→PLAYING edges occur, but only the first normal cleanup completes
(224 µs cleanup grain; this is not total stop latency). The second stays in
CanonicalPrefix through the one-second observation and completes only after
transport Stop retargets it. The prior state-only probe incorrectly passed the
same pattern in `captures/normal-stop-cleanup-two-pass-20260915/`; the probe now
requires completed PLAYING-destination cleanup for every pass. There are no
service/emission rows in these traces, so no audible/deadline verdict is possible.
Next: distinguish the second prefix builder's denial from canonical-window
readiness/ownership blocking, then rerun the same checkpoint before advancing
rolling current-phase handoff integration. Task 2.3 and all wider gates stay open.

**2026-09-15 successor-window closure integration (partial task 2.3):** the
shared bridge compiler now carries canonical row ranks and effective-cycle masks
into a contiguous successor window. An inherited held-note Off is copied in
bounded work, emits only in the window/cycle containing its due phase, and is
removed from steady insertion playback. The compiler rejects a retired or
changed predecessor before consuming more input. The existing 6,240-grain
reference estimate is preserved.

Validation was last run before the host usage limit blocked a fresh invocation;
the prior focused run passed 270 cases and the capture build retained 6,176
bytes of RAM1 local headroom. `git diff --check` and strict OpenSpec validation
pass. Production Track selection/refill and device HITL remain open; do not
upload this unverified successor integration.

**2026-09-15 successor closure integration (partial tasks 2.3–2.4):** the
existing RuntimePlaybackBridgeBuild now has a same-source successor entry point.
It checks contiguous coverage/cycle boundaries and a stable selected predecessor,
then inherits owned closures in bounded grains. A copied Off is inserted only in
the window containing its original due phase and only in its original cycle.
Steady insertion ordering compacts already-read indexes so closure-only Offs cannot
repeat in later loops; the existing MIDI cursor consumes the resulting streams.

Typed shared view fills now retain each row's complete canonical merge rank.
Compiled bridges retain canonical cycle masks across the complete rank horizon,
so later windows inherit gating decisions without reconstructing them from NoteIds.
Masks shift at cycle advance and survive windows containing no events. Replaced
old base rows cannot overwrite the effective canonical row's mask. Both bridge and
view buffer generations protect builders and cursors against reuse. All new slabs
are prewarmed; the existing 6,240-grain reference-case estimate remains unchanged.

Native prewarm/take-survival: 270 passed. Fixtures verify original Off emission,
no repeated closure in steady cycles, carry through two windows at quanta 1/32,
shortening the revised Off before the old closure, stale predecessor/source denial,
and canonical ranks surviving source-worker reuse. Capture firmware build passed:
RAM1 local headroom remains 6,176 bytes. This is shared compiler/cursor integration,
not completed production Track dispatch or a device normal-stop verdict.

Still required before enabling rolling Track selection: initial partial-window
closure context for already-open notes (its guard remains); coverage-qualified
cursor consumption across wrapped windows; atomic current-phase view/bridge handoff;
ordinary successor refill; and normal-stop cleanup without silence/reanchor.
No upload, task checkbox closure or HITL acceptance is claimed.

**2026-09-15 bridge-owned held-note closure state (partial task 2.3):** the
existing bridge compiler now stores its affected-open-note flags, closure time
and next-On phase in the bridge's fixed ledger-sized storage. When the existing
closure walk finds the original Off, it also copies that event into the bridge.
The copied closure survives compiler discard and base-window reuse. The old
base index remains only for the current compiled stream; it is not a transferable
closure receipt. No alternate pairing or closure-resolution algorithm was added.
Bridge reset retires closure metadata by count without clearing every payload;
compilation rejects a changed ledger count. Unresolved or incomplete receipts
are not exposed by the closure accessor.

Native prewarm/take-survival: 266 passed, including the unchanged cross-wrap MIDI
trace, all 128 ledger slots, base reuse, bridge reuse and changed-ledger rejection.
Capture build passes with 6,176 bytes RAM1 local headroom. Successor inheritance,
closures outside the initial window, current-phase selection and rolling playback
consumption remain required; the partial-window held-note guard stays in place.
This is ownership preparation, not completed cross-window playback or a normal-stop
fix. No upload or HITL acceptance; task 2.3 remains open.

**2026-09-15 ready-window bridge integration (partial task 2.3):** frozen
companion bridge preparation now borrows the exact prepared canonical ready
window, falling back to the existing full current base when needed. Mapping and
compilation no longer re-resolve through the audible-current index. Admission
checks that the preparation anchor lies inside this base coverage. Retirement,
provenance loss and coverage changes invalidate the borrowed base, including
completed candidates. Physical views now advance a storage generation on reuse;
mappers, bridge compilation, selection and stream cursors reject stale indexes
even when source identity and coverage repeat. The stopped diagnostic identifies
`prepared_window` separately from a missing canonical base.

The integrated native fixture covers prefix -> ready window -> frozen tail ->
bridge compilation -> the existing read-only MIDI cursor, with nonwrapped,
wrapped and empty coverage and equal-phase Off-before-On output. It verifies that
retiring the unrelated old current does not invalidate the ready base, while
retiring/reusing the borrowed base does. Audible Track selection remains guarded:
held-note closure carry, current-phase admission and following-window playback
are not complete, so this is not a normal-stop or HITL pass. No firmware upload.
Native prewarm/take-survival: 265 passed; capture firmware build passed with
6,176 bytes RAM1 local headroom. Strict OpenSpec validation and diff checks pass.

**2026-09-15 stop-owner canonical window preparation (partial task 2.3):**
CanonicalPrefix now prepares a one-bar canonical base when the current view has
no usable full canonical provenance. It merges the normalized prefix through the
existing RuntimeCompanionPasses worker, fills/orders a spare view through the shared
RuntimePlaybackViewBuild, and publishes an immutable ready view without changing
audible current. Fill, order and publication resume on separate turns. The pass
worker is released before frozen-tail processing; copied origins survive its reuse.
A busy shared builder stays pending, and source/storage changes cancel only the
owned preparation. Both initial-ready and post-record no-ready lifecycle shapes
are covered. No new event buffers or active allocations were introduced.

Native prewarm/take-survival: 265 passed. Capture build passes with 6,176 bytes of
RAM1 local headroom. This does not fix normal-stop liveness yet: the bridge selector
and PLAYING consumer still require the full-loop compatibility base. Current-phase
admission and held-note closure carry through subsequent window rotation remain
required before enabling the new ready base. No upload, HITL verdict or task closure.

**2026-09-15 canonical window implementation (partial task 2.3):** the existing
RuntimePlaybackViewFill and RuntimePlaybackViewBuild now accept typed canonical
coverage-scoped sources, copy events and pass provenance together, and keep fill
and order on separate bounded turns. Whole-source length need not fit the view;
only retained rows consume destination capacity. The existing companion bridge
mapper now filters the prepared per-pass result to that same coverage and checks
the borrowed base's coverage, publication, storage and provenance receipt on every
turn. The bridge compiler accepts a matching partial coverage with an empty ledger;
open-note obligations remain denied until carried across subsequent window rotation.
No raw/projected source is silently accepted as canonical provenance.

Native verification covers nonwrapped, wrapped and empty windows, one-entry quanta,
source larger than destination, provenance surviving worker reuse, per-pass MIDI
parity, changed coverage and unsupported held-note closure rejection. Final focused
prewarm suite: 138 passed; edit suite: 124 passed. Capture firmware build passed.
This is shared-worker implementation, NOT completed Track integration: stop-owner
base-window preparation, current-phase admission, closure-carrying rotation and the
ordinary PLAYING consumer must be connected together before enabling normal stop.
The new pieces were not uploaded; the device retains the verified display fix and
transport-Stop recovery. Normal-stop device verification and task 2.3 stay open.

**2026-09-15 companion-bridge checkpoint:** user confirmed first-record display
responsiveness after the background-cache dispatch correction. The remaining
normal-stop stall is now explicitly attributed on device:
`ODUB,stop,bridge_base,...,missing_full_loop_base`. A first recording retains a
one-bar raw playback view; the compatibility bridge mapper, selection preflight,
and hot-path consumer still require a full-loop view with canonical provenance.
Do not fix this by synchronously materializing a full-loop base or weakening
provenance/coverage checks. Task 2.3 must integrate coverage-aware canonical
successor preparation with the existing window lifecycle and task 2.4 consumer,
including current-phase cursor/ledger and subsequent-window handoffs.

Transport-Stop recovery from CompanionBridge is repaired: completed history skips
audible bridge work and calls the existing stopped canonical/companion commit
body, shared with its undo-aware entry point. Frozen-command/owner checks and
history cleanup remain enforced. Native stop/prewarm and take-survival suites:
259 tests passed, including no-published-base recovery and stale-command rejection.
Capture build/upload passed. Device `captures/bridge-stopped-recovery-20260915/`
shows OVERDUB_STOPPING -> STOPPED, retained correlated state/grain evidence, no
trace drops and no ring overflow. The normal-stop verifier still fails, correctly;
this is recovery evidence, not normal-stop, silence-matrix, or realtime acceptance.
The device was left STOPPED. No task checkbox closes.

**2026-09-15 first-record display regression:** user confirmed the screen now
continues updating in OVERDUB_STOPPING, but reported multi-second frame gaps during
the first two recording bars. A recording-only mode in the existing stop probe
isolates that interval from the stalled stop trace flood. Before the scheduler
fix, stopped background tracks rebuilt 56/353/1429-row display caches during the
recording owner turns: idle maintenance reached 281,346 us, a sampled display frame
538,356 us, and the window MIDI-service gap 281,883 us. The realtime dispatcher now
services realtime tracks only; pending stopped-track wrap work cannot block the
active display. The after-run sampled frames were 14,260–15,942 us, with no reported
long idle-maintenance span. Both captures are overflow-free. The after window still
has a 180,003 us MIDI-service maximum around recording entry; this is not deadline
acceptance. Evidence: `captures/record-only-background-fix-20260915/comparison.json`
and both linked session logs. Capture build/upload succeeded; six existing host
probe tests pass. The recording sequence completed and transport ended STOPPED.
CompanionBridge stop liveness remains open; no OpenSpec acceptance gate closes.
A stopped-only `!TRACE_DRAIN` diagnostic retrieves retained logs on explicit request.

**2026-09-15 stop/display stall investigation:** device `!TRACE_FENCE` answered
while the interface appeared frozen: main-loop liveness was retained. A stopped-only
`!STOP_STATUS` query now exposes the selected track's stop and history worker phases.
`captures/normal-stop-status-20260915/stopped-status.log` proves a stall in
CompanionHistory, with canonical tail complete. Recording start prewarmed resources
before resetting their canonical generation; the reset now precedes prewarm, and
snapshot cleanup retains the reserved companion-ID destination. The next device
report (`captures/normal-stop-resources-fix-20260915/stopped-status.log`) proves
matching generations but history Trim parked with zero canonical estimates.
Preparation/recycle counters now saturate as estimates rather than denying further
valid worker turns. Completed workers retire unused estimates without padding turns.
The display owner reuses its sliced paint continuation during OVERDUB_STOPPING.
Native prewarm/edit suites passed 259 tests after resource fixes; final continuation
prewarm/survival suites passed 259 tests (edit suite also passed 124). Capture firmware
built/uploaded. The next device run advanced through history to CompanionBridge
(phase 8), with history Ready and bridge Idle; normal stop still fails. Evidence:
`captures/normal-stop-continuous-display-20260915/stopped-status.log`. The current
full-loop bridge requirement and transport-stop retarget need further investigation.
The user-observed rerun also failed normal-stop verification:
`captures/normal-stop-display-user-verify-20260915/result.json`. Transport Stop was
sent for cleanup; completion was not established. Screen responsiveness still needs
the user observation. No task 2.3, timing reservation, or HITL acceptance gate closes.

**Current DRY direction:** first recording reuses the existing playback view,
fill/order, coverage lifecycle, bridges and MIDI cursor. The separate recording
window/store/stream prototype was removed. Shared coverage-scoped fill and retained return coverage are tested
(129 focused tests); Track source/lifecycle integration and original-boundary readiness
remain open. Raw coverage and preparation-only rotation now use the shared
workers; pre-roll now receives the new recording origin before ingestion.
Earlier prototype test counts are historical, not acceptance of the
shared recording path. No delayed launch or new recording rejection is authorized.

**2026-09-15 shared first-record refresh:** record entry installs an empty raw
bar-1 view through the normal playback lifecycle before capture begins. The
same fill/order workers can borrow the chunked `capture.store` through a
generation-checked source descriptor; one bounded grain refreshes the retained
window during recording, and an append invalidates an in-flight build for a
clean restart. Record stop binds final geometry before the existing audible
handoff, while cache reset preserves the retained raw view. Focused native
coverage/prewarm tests pass 132/132 and both Teensy builds pass. Deferred
maintenance is routed to FLASH to preserve the flexRAM bank boundary; the
capture-serial image now has 6,496 bytes of RAM1 local headroom. Device HITL
and the remaining parity/admission tasks are still open.

The recovered capture image was uploaded for track 5 / slot 1 HITL. The
transport-Stop interruption verifier passed focused precedence and silence
checks; the normal-stop rerun did not confirm the overdub-stop transition and
remains inconclusive. No HITL acceptance gate is closed.


**2026-09-15 first-record direction corrected by user:** no delayed launch is
acceptable. Prepare rolling one-bar playback windows during recording, with
bar 1 established at record start and retained for the existing final-bar return
boundary. Extend the currently overdub-only staged input path; prove late input,
held-note closure, final-length binding, and subsequent window handoffs without
rewinding transport or replaying historical events. The verified first-record
snapshot delay/catch-up defect remains unfixed. The deferred-launch proposal is
rejected history. See the active `resumable-midi-boundary-playback-maintenance`
design and specs; task and HITL gates remain open.


**2026-09-15 device-clock stop timeline ready:** capture-only telemetry now
records every `OVERDUB_STOPPING` grain with frozen identity, requested
destination, preceding actual MIDI service/due/deadline and exact grain span.
It queues following service and NoteOn/Off emission records from the timing
path for safe main-loop capture, and reports any trace loss. The host report
rejects malformed, dropped, or uncorrelated timeline evidence. Native focused
telemetry/tier: 43 passed; host timing/stop tests: 40 passed. Capture build:
flash code 947,864; RAM1 variables 91,808; RAM1 code 425,544; locals 6,496;
EXTRAM 2,240 bytes. This prepares the next focused normal-stop and transport
Stop HITL; Task 4.1 remains open for the non-stop ownership/result fields.

**2026-09-15 forced wrap Stop PASS:** track 5/slot 1 reached
`OVERDUB_STOPPING`; MIDI Stop arrived 8.689 ms later. The prior replay during
CC123 fan-out was repaired with an ISR-visible per-track output barrier. All
Notes Off followed in 83 us, STOPPED in 173.607 ms, and the 18.9 s observation
contained no target-channel NoteOn. Evidence:
`captures/resumable-handoff-wrap-forced4-barrier-hitl/`. Native focused suites:
242 passed; host fixture tests: 38 passed; capture build RAM1 locals: 6,496
bytes. Task 2.3 remains open for its complete bridge/publication/parity scope;
layered and deadline HITL gates remain open. See `FOCUSED-STOP-HITL.md`.

**2026-09-14 restart omission repaired and HITL PASS:** the prepared-stop path
now hands prewarmed session identities to the idle snapshot owner before close;
the stale-root overlay reads that receipt immediately. Companion publication
updates both note and MIDI-event membership. Three complete restart cycles each
played all 49 expected notes exactly once with matching NoteOffs, after a focused
Stop-interruption PASS. Evidence: `captures/resumable-handoff-admission-hitl/`
and `captures/resumable-handoff-restart-hitl/`. Firmware uploaded; transport stopped.
Native full suite 2,083 passed/1 skipped, final focused suite 124 passed, host tests
38 passed; RAM1 locals 6,496 bytes. Tasks remain 11/19. An earlier wrap-adjacent
run took legacy stop and is inconclusive; reproduce that admission fallback next
with the new capture-only denial diagnostics. Protected cleanup/undo, fully
bounded publisher/overlay work, layered parity and timing acceptance remain open.
See `FOCUSED-STOP-HITL.md` for scope and separate baseline verdicts.

**2026-09-14 restart survival FAIL:** after the focused interruption PASS, three
complete restart cycles emitted only base pitches 48–79; overdub pitches 24–39
and held 83 were absent. Canonical/display evidence still reports 49 notes while
playback's overlay has 32. Prioritize task 2.3's reserved bounded session-exit
snapshot handoff before clearing session identity; the prepared-stop production
path currently omits it. Do not insert the unbounded legacy queue call into an
active grain. Layered HITL is deferred until single-slot survival passes.
Evidence: `captures/resumable-stop-restart-survival/restart-result.json` and
`FOCUSED-STOP-HITL.md`. Transport stopped; firmware unchanged; tasks 11/19.

**2026-09-14 focused interruption PASS:** repaired concrete-output-port fixture
and stale-PLAYING skip; actual MIDI Stop observed 9.253 ms into OVERDUB_STOPPING,
All Notes Off after 70 us, then STOPPED and 9.49 s without resumed target notes.
Evidence: `captures/resumable-stop-hitl-strict-wrapper/interruption-result.json`.
This closes only the single-track interruption/silence checkpoint. Restart
survival, layered playback, broader baseline and full timing acceptance remain
open. Tasks remain 11/19; see `FOCUSED-STOP-HITL.md` for scope and next gates.

**2026-09-14 MIDI deadline correlation checkpoint:** uploaded capture-only retained
send/due/deadline timestamps and per-stop-phase summaries. Native 2,080 passed,
1 skipped. A normal stopping interval measured 97.760 ms; one canonical-order
turn crossed its entry service deadline (248 us duration, 33 us slack). Another
run retained 24.678 ms clock and 26.049 ms NoteOff lateness. This is partial
hardware evidence, not deadline acceptance. Scratch clears succeeded with the
revised host precondition; the timed interruption fixture was subsequently repaired and its focused
checkpoint passed (see the newer entry above). See `FOCUSED-STOP-HITL.md`; tasks remain 11/19 complete.

**2026-09-14 protected stop continuation:** bounded undo publication/restore,
STOPPED-only joint recovery, disabled-pass reclamation and session-close helpers
now have native coverage. Production playback honors the latched STOPPED
emission policy; full joint stop integration remains open (11/19 tasks).
See the change's `BRIDGE-PROGRESS.md` and `FOCUSED-STOP-HITL.md` for the focused
Stop regression checkpoint and explicit remaining integration. Upload and a focused HITL attempt were approved and performed on 2026-09-14.
The initial boot exhaustion is fixed: boot now reserves staging only; full
bridge readiness remains separately guarded. Device startup and normal UI are
confirmed, with 4,401 KB free PSRAM. Native: 2,077 passed, 1 skipped. The exact
managed-log binding is fixed. Normal stopping was observed; the interruption
retest remains INCONCLUSIVE_CAPTURE after a deferred-save clear refusal. See
`FOCUSED-STOP-HITL.md` for evidence and remaining gates.

**Highest operational priority.** Defines what to implement **now**. Load with [PROJECT_STATE.md](PROJECT_STATE.md) before planning or coding.

Last updated: 2026-09-12 (§6.6.5 GAP-8b reverted; §6.6.7 stage 7 in progress)

Authority hygiene: execution bound class is a gated property — [ExecutionBounds.md](../Authority/Architecture/ExecutionBounds.md), [DEC-043](../DECISION_LOG.md#dec-043-execution-bound-class-is-a-gated-architectural-property). Architecture-checkpoint question 3 is a hard stop for new unbounded interval work. No interval reservation / `RuntimeWorkBudget::admit`.

---

## Now implementing

**2026-09-17 — parent change task 2.4 (active clock path consumes prepared views): slices 1 and 2 are
landed and the slice-3 decision is made (not required); 2.4 ticks on the 5.3 device measurement.** This is
the active code work on `resumable-midi-boundary-playback-maintenance`, alongside the successor change's
own pick-up order (best next there: 5.5). Where it stands:

- **2.4 slice 1 (`4bd368ca`).** `openspec/changes/resumable-midi-boundary-playback-maintenance/ARCHITECTURE-REVIEW.md`
  is new (the change had no phase-gate artifact) and carries the per-phase gate for 2.4 -- ownership / state
  transition / execution bound all **NO**, reuse **YES** -- plus the architecture-checkpoint answers.
  `Track::resetPlaybackOrderHotRebuildCount()` / `playbackOrderHotRebuildCount()` count content-sized
  order builds (heavy merged build, `rebuildPlaybackOrder`, full `reanchorPlaybackIndex`) that run while
  `anyTimingCriticalTrackActive()`. The counter is in **RAM2/DMAMEM**: a 4-byte DTCM addition here costs
  4 KB of RAM1 local allowance through the alignment edge, and a `Diagnostics::Counter` slot was rejected
  for the same reason. Two native tests prove the detector fires on active transport and stays clean for
  the STOPPED-oracle build.
- **2.5a parity oracle (`f44e0077`, `f76fa22f`) -- prerequisite, not 2.5 completion; 2.5 still finishes
  after 2.4.** Differential harness in `test_continuous_capture`: `DifferentialRow`/`DifferentialTrace`
  capture (tick, kind, channel, note, value) in **emission order**; `consumePairOverRange` drives a
  prepared `(mergedEvents, playbackOrder)` pair through `advancePlaybackCursor`; `differentialTracesEqual`
  compares ordered rows. Fixtures landed in the owner's priority order: plain pair parity, the permanent
  **negative control**, **seal admitted into the pair** (the transition that invalidated literal
  request+hold: the stale pair cannot emit the admitted rows, a full replacement does, and the streams
  differ), **same-tick Off-before-On**, and **the anchor/cursor seed is part of the pair**.
- **Finding that narrowed 2.4.** `Loop::tryGatherPublishedSnapshotEventsInWindow` returns false when there
  is no published root and the merged consumer then holds empty, so the post-commit publish gap is not a
  second active rebuild case; the reachable active rebuild is concentrated in seal admission.
- **2.4 slice 2 — LANDED 2026-09-17 (immutable pair + atomic publication); the measurement decides
  against slice 3.** Pinned invariant: *playback never observes `mergedEvents` and `playbackOrder`
  from different generations; a pair becomes consumable only when both members and their cursor/reanchor
  state are complete for the same source generation.* `PreparedPlaybackPair`
  (`include/TrackPlaybackRuntime.h`) carries `generation`, the live source identity, the full-loop
  `coverage`, a pointer to a dedicated prewarmed `FixedPlaybackEventStorage`, `eventCount` and the
  `cursorSeed` resolved against the publication phase. `LoopPlaybackRuntime::publishPreparedPlaybackPair`
  fills that buffer and replaces the whole descriptor in one assignment inside a non-yielding turn; the
  off-clock owner is `Track::publishPreparedPlaybackPairFromWorkingWindow` /
  `Track::servicePendingPreparedPlaybackPair`, serviced one preparation per turn from
  `runDeferredLoadAndDisplayFrame` (`main.cpp`). `resolvePreparedPairConsume`
  (`src/Track/TrackPlaybackWindowBuild.cpp`, shared with the fixtures) is the clock path's entire
  decision: consume a current pair, otherwise record exactly one request and keep consuming the
  published P0. `playCommittedLoopMidi` no longer calls `::rebuildPlaybackOrder` /
  `reanchorPlaybackIndex` for the full-loop merged path; the pair's identity `order` feeds the existing
  fixed-stream adapter and the new seed is copied on the first tick after publication (a wrap rewinds to
  0). `Loop::hasPendingOverdubSealAdmission()` is the non-allocating source-change signal for the seal
  receipt. The long-loop windowed gather and the cold first audible turn keep the in-place builder,
  which the slice-1 detector still counts. Measurement (native, one bar = 768 ticks = 24 MIDI clocks):
  `prepared_pair_request_count` 1, `prepared_pair_publish_count` 2, **`prepared_pair_hold_ticks` 8**,
  **`prepared_pair_max_latency_ticks` 7**, `prepared_pair_rebuild_units` = the replacement window's rows
  (4 -> 6 in the fixture), `playbackOrderHotRebuildCount` 0 across the decision loop and 1 for the
  off-clock order rebuild. Against the audibility edge: 8 ticks is ~1% of the one-bar budget (768) and
  exactly one MIDI clock, i.e. far inside the next 16th-note step (48) — **slice 3 (b), a bounded
  incremental order update, is not required for 2.4**. Evidence: `test_continuous_capture` 67/67,
  `REENTRY_RED=1` 68/68, `ONLY_COMPANION=1` 1/1, `test_playback_prewarm` 173/173, full native **2,232 / 1 skipped /
  0 failures**, RAM1 variables 91,904 / code 425,496 / padding 488 / **locals 6,400**. Remaining before ticking 2.4: the same counters on device (5.3); 5.4 stays blocked.
- **2.5a real-gather fixtures LANDED 2026-09-17; the view/bridge matrix is what remains of 2.5.**
  `buildFixtureRoot` in `test_continuous_capture` now builds a published snapshot root carrying both
  independent snapshot roots (note membership *and* the MIDI-event membership the playback gather
  reads, mirroring the publisher: an Off at/beyond the loop length clamps to `loopLength - 1`, a wrap
  note splits into head+tail with the same `NoteId`), and `assertPairMatchesSynchronousPathFromAnchor`
  anchors both consumers at the pair's own `cursorSeed` so the comparison is about the pair rather than
  about where each cursor started. The four owner-priority gathering fixtures exist: wrap + loop-edge
  note, source change mid-window (hold, 8-tick latency, added row audible), long-loop window (the
  documented `pairApplicable == false` boundary plus the windowed stream against an independently
  phase-sorted full-gather window), and the record-stop publication gap (an empty gather publishes no
  pair, then the session-exit root lands and the pair matches the synchronous path). Harness note:
  `consumePairOverRange` starts at `fromTick + 1`, so a phase-0 row is never emitted by either
  consumer; fixtures keep their plain note after tick 0. **Still owed for 2.5:** the view/bridge
  scenarios — record stop, phase-zero wrap admission, pre-seal invalidation, rejected-On/later-Off,
  accepted-On/matching-Off, wrap-held closure, append-and-stop, the explicit deferred-stop trace,
  promotion before wrap, max phase advance, denied refill, three-buffer rotation, source-change
  preemption near handoff, source prep midway through two coverage windows, second-source bridge
  replacement, coverage fault, layered slots, slot switch, first/second revision changes. Evidence:
  `test_continuous_capture` 71/71, `REENTRY_RED=1` 72/72, `ONLY_COMPANION=1` 1/1,
  `test_playback_prewarm` 173/173, full native **2,236 / 1 skipped / 0 failures**, RAM1 unchanged
  (variables 91,904 / code 425,496 / padding 488 / **locals 6,400**; test-only increment).

**2026-09-17 — OpenSpec 5.12 / D8 interval-owned staged pass LANDED (DEC-072), and the 5.5
`CompanionHistory` blocker is cleared.** A deferred job now owns its staged capture pass from its own
`PrepareTransfer` grain (`CaptureStopJob::pendingPass` + `ownsPendingPass`,
`Loop::takePendingCapturePass`), the companion history binds that receipt by address, and the job's
terminal transfer consumes it directly (`Loop::transferCapturePass` and the explicit-pass overloads).
`beginCapture` keeps its unconditional producer-local clear; no deferred reader falls back to
`Loop::pendingCapturePass_`. Native: `ONLY_COMPANION=1` green, `REENTRY_RED=1` 57/57, full native
2,221 / 1 skipped / 0 failures; RAM1 locals 6,400. Device:
[`captures/successor-ownership-06`](../../captures/successor-ownership-06/session_20260917_230432.log)
managed HITL `base`, `Result: PASS`, `overlap=True denied_stop=True retirement_ok=true` (A retires
inside B's live interval; B then stages and transfers its own pass). Host oracle corrected to accept
the current `Overdub stop refused: stop_job_pending` denial wording. **Next (reviewed 2026-09-17 — the
pick-up order is recorded at the top of that change's `tasks.md`):** the open items are now only 5.5,
5.9, 5.11, 6.1, 6.2 and 6.3 (4.3 closed), and the best next step is **5.5** — the only host-only item,
the one D8 directly unblocked, and the one still hiding coverage behind `REENTRY_RED=1`: add the
missing "after the raw-range stage" checkpoint (`CanonicalRewrite`; `ReservedClosure` is the
alternative), then delete the `REENTRY_RED` gate so the case runs in the default suite. Then plan
**5.9 + 5.11 + 6.2 as one device session** (5.9 still needs the `hitl-content-verification` audibility
proof for "all overdubbed bars stay audible"; 5.11 closes on that run; 6.2 still needs a successor
that *carries rows* plus a boot-prewarm headroom read), then re-run **6.1** and finish **6.3** (which
includes ticking the parent change's task 2.3, already annotated with the deferred three-deep capacity
case).

**Also reviewed 2026-09-17: the parent change `resumable-midi-boundary-playback-maintenance` re-checked, task 2.3
closed.** Its own recorded acceptance list (two public reentry cases + device evidence + the deferred three-deep
capacity case named) is satisfied, so 2.3 is ticked with that evidence. Seven of its items stay open and now carry
inline status: **2.4 is genuinely open** -- `playCommittedLoopMidi` still runs `ensurePlaybackMergedMidiEventsBuilt` +
`rebuildPlaybackOrder` + `reanchorPlaybackIndex` synchronously when `fixedView == nullptr` on the active path, and no
instrumented test fails on it -- and it blocks 5.4; 2.5 still needs the differential trace harness; 3.3 is one clause
short (the complete atomic Commit-body measurement); 3.4/4.1 have landed behaviour but unevidenced verify clauses; 5.3
is a device matrix; 5.4 cannot land before 2.4. The recommended order for that change is recorded at the top of its
`tasks.md`.

**2026-09-17 — transport-stop retarget re-route (DEC-070, implemented + device-verified; openspec
5.3/5.7/5.8).** The user's stuck state machine after the HITL runs was a deferred job admitted for
PLAYING, retargeted to Stopped by `stopOverdubbingToStopped` and left on the PLAYING phase path: its
companion work is bound to the pre-stop transport command epoch, so every grain denied silently, the
job parked in `CompanionHistory` (`Overdub stop refused: stop_job_pending phase=7 dest=1`) and it kept
`captureStopOwner`, the frozen staging role and its session-exit receipt — every later stop refused
until reboot. `requestDeferredOverdubStopToStopped` now re-routes that job on the same edge: retire the
companion/undo estimates a STOPPED-admitted job never contracts, drop the half-prepared companion
horizon, continue at `AtomicTransfer` and retire through deferred cleanup. Native
`test_transport_stop_retarget_retires_a_midflight_playing_job` fails without it; full native 2,213 /
1 skipped / 0 failures; RAM1 locals 6,400. Device `captures/successor-wrap-40`: `Transport stopped`
74.873 s with the successor's job pending, `Reentry released` 77.053 s, then two admitted enter/stop
cycles (97.1/99.2 s, 105.6/107.7 s) with no refusal. Diagnosis instrumentation (one-shot
`Deferred stop stalled: <reason>`, `Overdub stop refused: <resource>` + parked phase) stays in place.
**Follow-up regression (user, 2026-09-17):** after the re-route the first overdubbed notes vanish from the *display* while remaining audible, with only the wrapped notes visible. Cause: the re-route discarded the prepared companion horizon, which carries the note-edit representation the display resolves from. The re-route now keeps a verified horizon (published by `commitStopped`) and drops only an incomplete one, and logs `Overdub stop retarget: companions=<published|raw_only> phase=<n>` so the next reproduction names the sub-case. Native capture suite 50/50; RAM1 locals 6,400. **Reproduced on device (`captures/successor-wrap-41`, temporary `--post-save-play-wait-ms` play-from-stopped step): `overdub_ok=False`, `frame=178 … visual=124 buffer=178`, and `Overdub stop retarget: companions=raw_only phase=6` — the job was still at `PrepareTransfer`, so its companion horizon was never prepared and the raw-only commit dropped the transforms the display resolves the overdubbed notes from. **Implemented:** `retargetedFromPlaying` keeps the companion preparation on the retargeted STOPPED route (published via `commitStopped`), with a bounded 512-grain per-phase fallback that commits the raw pass alone if it truly cannot complete, so the slot never parks. Native 2,213 / 1 skipped / 0 failures; RAM1 locals 6,400. Device `captures/successor-wrap-42` flips the display verifier to `overdub_ok=True` (from `False` in `-41`), but reports `transport_ok=False` alongside a `capture_ring_overflow`/`SERIAL_INCOMPLETE`, so the post-stop window check needs a clean re-run (openspec 5.9).


**2026-09-17 — the wrap is the third owner of the slot's canonical preparation (DEC-069; hardening
landed, revision gate awaiting the owner's choice).** The user's manual three-overdub session
[`captures/session_20260917_172651.log`](../../captures/session_20260917_172651.log) fails on the
DEC-068 firmware: OD1 (570.855 s) wraps and retires normally, OD2 (586.564 s) stops at 590.687 s
and **never emits `Reentry released`**, OD3 (591.518 s) wraps at 593.456 s and then every NoteOff
for the notes held across that wrap is rejected (`Capture admission rejected: unmatched NoteOff
ch=4 note=72`, nine times), and OD3's own stop at 598.245 s is refused as
`deferred/frozen admission role is occupied` for the rest of the session.

Root cause, proven natively: `Track::commitOverdubWrap` commits the live interval's next capture
pass, and a pass commit advances `Loop::playbackRevision`. The pending job's
`RuntimeCanonicalPassInputs` frozen binding is pinned to the revision it was admitted against
(`valid()` compares the live `playbackRevision`), so the advance makes `ready()` false forever,
`CanonicalTail` denies, `completeDeferredCleanup` never runs, `runtime.captureStopOwner` stays
owned, and no later stop of the live interval can be admitted. The live interval's queued wrap
publication also stays behind the unresolved predecessor publication (FIFO), which is why the
third overdub never shows at the wrap and its notes sound lengthened into the previous overdub.
Instrumented native probe of the failing interaction: `revBefore=1 revAfter=2`, frozen binding
intact, `ready()` false, job in `CanonicalTail` for 20,000 grains.

Landed here (independent of the gate): the wrap no longer calls `resumeCanonicalAfterAppend`
(whose `resumeCurrentAfterAppend` resets the frozen capture binding) nor discards the frozen
order/rewrite builders while `hasPendingDeferredOverdubStop()` — the same DEC-067 ownership rule
the start and stop paths already follow. Two new native cases
(`test_successor_wrap_keeps_the_predecessors_frozen_builders`,
`test_successor_wrap_keeps_a_held_note_reservation_for_its_physical_note_off`) fail without the
guard; full native 2211 cases / 1 skipped / 0 failures; `pio run -e teensy41-capture-serial`
SUCCESS with RAM1 locals 6,464. **DEC-069 Option A is implemented** (owner's choice): `maybeCommitOverdubWrap` records the owed
commit in `Track::overdubWrapCommitPending_` and seals nothing while a job is pending; the per-tick
playback turn retries it once the ring is empty, and the interval's own start/stop consume the flag.
The straddling rows stay in the producer, so the held note's reservation is never reset and the
live interval's `unmatched NoteOff` rejections disappear with it. Three native cases cover the gate
(two fail without it); full native 2,212 / 1 skipped / 0 failures; RAM1 locals 6,464; no new
human-log literal (one added string in that translation unit costs 4,096 bytes of DTCM, so the
deferral is observed through marker ordering). **Device acceptance closed** (`captures/successor-wrap-36`, firmware at the Option A commit): the
successor's crossing of tick 6144 was deferred, `Overdub wrap committed @ tick 6460` follows it
(only the deferred retry can report a later tick than the crossing), the predecessor's
`Reentry released` follows at 60.534 s — the outcome the old path made impossible — the successor's
own stop is admitted at 62.736 s, and the run has no `unmatched NoteOff` and no
`Capture admission rejected` row. Regression `captures/successor-wrap-33` (DEC-068 flags) is a clean
harness PASS (`overlap=True`, `denied_stop=True`, both retirements). Two route notes for the
reproducer: the successor must own rows *before* its crossing, which the canonical preset cannot
supply once the predecessor's stream ends, so the acceptance used a temporary
`--successor-reentry-stream-bars` flag (default 0, canonical behavior unchanged, promotion to a
registered `STIM-…-02` pending); and the predecessor's stop phase must sit ~0.5 bar before loop end
(`A_stop_phase ~= (A_start_phase + delay_bars + overdub_bars) mod record_bars`, with A starting
~1 bar after the record stop). Earlier attempts are recorded as inconclusive, not negative:
`successor-wrap-30` used `--record-bars 1`, which never establishes a loop (0/768).


**2026-09-17 — reentry session ownership — LANDED (DEC-068, device PASS
`captures/successor-reentry-27`).** Each interval now owns its session-exit tail: A's is captured
at its stop admission (swap, not copy), released with its own pass id at its own commit, and the
publisher consumes only ready tails FIFO; a successor opens a fresh session instead of joining the
draining one, so a retiring job cannot end the successor's session or hand off its identities.
Device: A's release 51.620 s, B's admitted stop 52.098 s, **B's own retirement 52.992 s**
(`retirement_ok`), with `overlap_ok`/`denied_stop_ok` true and the run's own result **PASS** — the
first clean reentry run. Six native cases in `test_continuous_capture` cover the decision's gates;
full native 2209, 1 skipped, 0 failures. The defect this replaced, for the record:

**2026-09-17 — reentry session ownership: a retiring job consumed the live successor's
session (RESOLVED by DEC-068).** Tasks 5.1(d)/(e) landed (`test_successor_same_interval_transfers_exactly_once`,
`test_start_request_while_successor_is_live_is_not_a_second_interval`; net full-native 2205,
1 skipped) and the (d) case immediately exposed a job-locality defect in the same family as
the two DEC-067 findings. With A's deferred job pending and B live,
`TrackPreparedStopCommit::completeDeferredCleanup` (the phase every deferred stop ends in)
unconditionally calls `loop.handoffSessionExitSnapshot(...)` and `loop.closeOverdubSession()`:

1. the receipt is built from the **current shared session state**, which under reentry is the
   successor's, so the retiring interval's session-exit publication carries the wrong pass ids;
2. `closeOverdubSession()` closes **B's** session while B is still recording — the new case
   pins it (`loop.hasOverdubSession()` false for a live interval, asserted as the diagnostic);
3. B's own `DeferredCleanup` then cannot hand off (`!hasOverdubSession()`), so B's job reaches
   phase 11 (`DeferredCleanup`) and never retires: its descriptor leaks and its session-exit
   publication never queues. Reachable on device in exactly the DEC-067 sequence
   (`captures/successor-reentry-25`: A released at 52.195 s while B had been live since 49.986 s).

Root cause: `Loop::beginOverdubSession` early-returns when `hasOverdubSession()`, so a successor
interval **joins the predecessor's session** instead of owning one, while `sessionExitHandedOff_`
and the receipt ring are written as if a session belonged to one interval. Fix shape (needs a
DEC, since it changes when a session-exit receipt is created): capture the interval's receipt at
its own stop admission (before any successor state exists), consume only that receipt in the
cleanup, close the session only when the job still owns the live interval (`ownsTrackState`),
and let a successor open its own session/index while the predecessor's is still draining.

**2026-09-17 — successor-capture reentry: finalization admission is stop-directed (DEC-067).**
User decision after the device exposed the mismatch: *"I would fix the gates to match the
architecture, rather than narrow the architecture to preserve an accidental reservation
lifetime ... a resource should block the transition that first requires ownership of that
resource."* Landed and device-verified — see item 2 below for the audit table, the markers,
the native cases and the `successor-reentry-25` sequence. Next in this scope: the three-route
agreement case (4.2), the remaining 5.1 breadth (interval transfer exactly once, the
"start while B is still live" denial), 5.3–5.6, then 6.1/6.3 closeout once the shed-capture
observability gap is named. Explicitly not taken: option B (a canonical output per interval —
it would make two finalization working sets concurrent) and option A (narrowing the declared
capacity).

**2026-09-17 — first-record audible launch from the prepared bar-1 window (user-directed;
leads the successor-reentry device step).** User: *"we explicitly had a plan to build a
prepared first bar to be ready to be used when record stop is executed to directly be used as
playback. that should be leading and remove the decision for empty."* The authority is the
approved contract in
[`resumable-midi-boundary-playback-maintenance/design.md`](../../openspec/changes/resumable-midi-boundary-playback-maintenance/design.md)
§ *First-record window preparation preserves the existing launch boundary* and the 2026-09-15
user correction in its `tasks.md`: at record start the slot runtime establishes prewarmed bar-1
playback coverage, accepted input keeps it current, the retained raw bar is bound to final loop
geometry at the stop, and **the complete bar-1 source/order/cursor/ledger bundle is selected at
the existing transport-clock boundary with no global rewind, no omitted notes and no delayed
catch-up**. The measured defect this replaces: after a first-record stop playback is empty for
`~620 ms` (the session-exit publication gap, 6.3.1 hold-empty) and then flushes the missed grid
steps as a catch-up burst. Device evidence: `captures/deadline-pacing-01`
(`STOPPED_RECORDING→PLAYING` at 39.8156 s, first `snapshot_unit,kind=publish` at 40.4297 s,
`playback_build,snapshot,...,currentPhase=473` at 40.4304 s, first `MO` at 40.4355 s).

- **S1. Preparation must be re-armable across takes — LANDED (`b6dd67bf`).** A take that ends
  without the record-stop handoff (transport Stop during RECORDING, empty/skipped commit) left
  the prepared raw bar-1 view retained, so the *next* take's `prepareFirstRecordingBar` was
  denied and the prepared-first-bar path never existed. Device evidence: `First-record bar-1
  playback preparation unavailable` in `deadline-pacing-01`, `successor-reentry-13`,
  `bar-anchor-02` and `-06`, each immediately after a `RECS,stopToStopped,...` empty take. The
  new take's preparation boundary releases the stale window
  (`releaseFirstRecordingBarReturnWindow`) and the denial warning names the failing
  precondition.
- **S2. Select the bound bar-1 bundle as the audible source — LANDED, device-verified
  (`first-record-launch-04`).** `playCommittedLoopMidi` had two fixed sources and both require
  `publishedSnapshotIdentity()`, so the bound raw bar-1 view was never audible and the legacy
  merged path held empty (`GatherMissHoldEmpty`) until the root published. Now: the bound window
  is flagged (`RuntimePlaybackView::firstRecordReturnWindow`) so `LoopPlaybackRuntime::reset`
  preserves it across the launch's playback-cache resets; the hot path adopts it while
  `isPlaybackSessionExitPublicationGap()` holds and the playhead is inside its coverage
  (`adoptFirstRecordingBarReturnWindow`, DIAG `first_record_adopt,<loopLength>,<retainedEvents>`);
  the published root replaces it through the source identity. **The bundle includes its cursor**:
  adoption selects the first event after the launch phase, because the emission otherwise treated
  the launch turn (`prevTickInLoop == tickInLoop`) as a loop-start catch-up and dumped the
  already-played bar-1 prefix in bursts (seen in `first-record-launch-02`: notes 48–52 within
  400 µs). Device verdict, canonical 2-bar `base` PASS: stop 31.0072 s → PLAYING 31.0186 s →
  `BAR,346` → `first_record_adopt,1536` 31.0216 s → first `MO` 31.0994 s (**93 ms** after PLAYING),
  then note-ons at **125.0/125.0/125.0/122.4 ms** = the 16th grid, no burst and no omission
  (pre-fix `deadline-pacing-01`: first note 620 ms after the stop, then a catch-up burst).
- **S2b. The window's retained extent must cover the launch phase — OPEN.** The service refresh is
  incremental (`serviceFirstRecordingBar`, one fill/order grain per VisualCache owner turn), so at
  the stop the window can hold only a *prefix* of bar 1 (`first-record-launch-02` held 5 of 16
  notes). The cursor reanchor makes that prefix skip instead of burst, but notes between the
  prefix end and the root's arrival are then **omitted** rather than delayed — the same audibility
  as the old hold-empty for that take, not a regression, and not yet the design's "no omitted
  notes". Close it by keeping the prepared window current through the last accepted input (owner
  turn budget) or by a bounded drain at the stop, and carry the bar-1 closure obligations
  (`LedgerBoundarySummary`) so a note whose Off lies in a later bar is closed by the window itself.
- **S3. Coverage continuation — PLANNED.** Cover each following bar without omission or delayed
  refill (bar-1→bar-2 handoff for multi-bar takes) and verify an 8-bar take on device. The 2-bar
  canonical take is already covered: the root publishes ~0.4-0.6 s into bar 1 (2 s), well before
  the window's coverage ends.

Explicitly not in scope for these slices: C1-a compat-gather restoration (6.3.1 stays retired),
DEC-045 idle publication scheduling, and any new fixed user-visible capacity.

**2026-09-17 — HITL oracle repair RC (user-directed), then the bridge tail.** C3/DEC-066 is
closed as validated, so the next priority is trustworthy device evidence rather than more
memory work. In this order:

- **1b. Record-stream pacing — OPEN (found while probing).** The host streams longer than
  requested (23.0 s for an 8-bar/16 s request at the device's own 2.0 s/bar cadence), so the
  device legitimately records 11-12 bars. When the device's `BAR`/`BPM` markers are available,
  the stream duration and stop moment must follow them rather than a host seconds-per-bar
  estimate. This is the remaining half of item 2 and it keeps flagging strict length checks.
- **1. Oracle repair — LANDED.** Deferred stop (`OVERDUBBING -> OVERDUB_STOPPING -> PLAYING`)
  is now first-class in the transition counts and the phase pairing; duplicate observer ST
  edges are collapsed; a wall-clock-proxied stop reports its length instead of failing it.
  Device `oracle-fixed-01` = **PASS**. See the oracle entry below.
- **2. Successor-reentry device case — LANDED, DEVICE-VERIFIED (DEC-067; `successor-reentry-25`).**
  Path: the probe could not hit the predecessor's deferred window because the deferred stop
  floods the serial stream the harness parses (`-18`: +400 ms requested, +1535 ms measured),
  which exposed a firmware contract mismatch: `Track::startOverdubbing` gated on three
  *finalization* resources (canonical copy target, active transfer admission, session-exit
  receipt) that the interval does not use before its own stop edge, so the declared depth-2
  capacity was unreachable (`-19`/`-20`: `Overdub start rejected: canonical copy chunks are
  not reserved` at +500/+909/+1116/+1515 ms against `bridge_ready` +1458/+1681 ms).
  User decision: **C** — fix the gates to match the architecture (DEC-067). Landed:
  START keeps only the live staging role, the stop-descriptor ring and the interval's own
  memory admission; `beginDeferredOverdubStop` owns the finalization checks (each with its own
  denial reason); the start path no longer discards a live job's frozen order/rewrite builders;
  a stop denied for those reasons is now refused atomically instead of falling into the legacy
  synchronous stop. Two edges are emitted for the harness (`Reentry window open slot=<n>` and
  `Reentry released` on the prompt human channel, plus `DIAG,reentry_window_open` and
  `DIAG,canonical_release` on the evidence tier), and the probe/oracle drive and verify the
  sequence from them.
  Device `captures/successor-reentry-25` (managed HITL, `base`, 8-bar record / 2-bar overdub,
  build identity in its `metadata.json`), re-verified on the current firmware as
  `captures/successor-reentry-26` (48.857 s `Reentry window open`, 49.399 s B START 2,011 ms
  before A's release, 49.942 s B's stop refused, 51.410 s `Reentry released`, 52.109 s B's stop
  admitted; oracle PASS): 44.253 s A `Overdubbing started`; 49.403 s
  `Reentry window open`; 49.986 s **B START, 583 ms after A's stop and 2,209 ms before A's
  finalization released**; 50.500 s **B's stop refused** (`Overdub stop deferred: deferred/frozen
  admission role is occupied`) with B still recording; 52.195 s `Reentry released`; 52.778 s
  **B's stop admitted**. Oracle: `overlap_ok=True`, `denied_stop_ok=True`,
  `evidence_source=human_markers`, no successor issue in the report.
  Caveat: because the 8-bar deferred run sheds `#CAP` tiers (`RING,shed`), the *baseline*
  verifier's unrelated record/persistence checks report `INCONCLUSIVE_CAPTURE` in the same run;
  the reentry gate itself is proven from the prompt channel. Closing 6.2 fully needs that
  observability gap addressed (a separate item: the shed loses the `ST`/`REVT` rows the
  baseline checks need).
- **2b. Device-state hazard (new, needs a decision).** An aborted run can wedge the deferred
  workspace save (`Persistence drain stuck after 256 iterations`, 158 KB pending); the clear
  ritual then aborts and **every** later run fails its preconditions until the device is
  re-flashed (the device does not reset on serial open, and a 1200-baud touch did not reset
  it). Recovery is `pio run -t upload`. Decide whether the harness should force/verify a fresh
  boot, and whether the firmware's blocking drain deserves its own look.
- **3. Bridge map/compile profiling — DONE (profile below).** The 1.07 s tail is
  **cadence-bound, not work-bound**: 466 of 490 deferred-stop grains are the
  `CompanionBridge` phase, they cost only 65.4 ms of the 1,068 ms window, and they issue at a
  1.2-3.2 ms cadence (~one grain per main-loop turn). `CompanionHistory` is down to 4 grains,
  so C3's work shaping is visible in the profile. Any optimization would target the owner's
  turn cadence/per-turn grain budget and must be argued against the MIDI-boundary budget -
  that is the DEC question, and no DEC is proposed until the successor-reentry device case
  lands.
- **Explicitly not pursuing:** C4 (device-wide deferred-stop working set), event-horizon
  ceiling reduction, seal-phase reserve.

**2026-09-17 — memory-exhaustion admission RC: close record-arm semantics, then footprint
(user-directed, seven steps).** Both boundaries now deny explicitly at the point of the
failure (overdub: `admitOperationMemory`; record arm: the required prewarm's own result),
so no operation reaches a fatal allocator. The remaining work is to make the *reservation*
itself smaller and better attributed, in this order:

- **A. Record-arm admission closure — LANDED.** `prewarmPlaybackForSlot` returns its
  result and `Track::startRecording` denies before any mutation. Do not regress to
  "advisory prewarm".
- **RC CLOSED (2026-09-17, user-accepted):** the exhaustion RC's acceptance criterion -
  *before: fatal/silent device; after: explicit operation denial with playback continuing* -
  is met by [`stepD-arm-delta-01`](../../captures/stepD-arm-delta-01/session_20260917_115207.log).
  A successful record+overdub in the constrained state was explicitly **not** a prerequisite;
  it became E's device gate instead, and E now passes it
  ([`stepE-e2e-01`](../../captures/stepE-e2e-01/session_20260917_121721.log): 0 denial, 0
  refusal, 0 FATAL, record + overdub + deferred stop to PLAYING).
- **B. View/history lifetime audit — DONE.** All buffers byte-accounted against the measured
  tags; the seven prewarms are one concurrently-live working set, not phase-exclusive, and
  live canonical-prefix service needs them during RECORDING. Table and citations:
  [ExecutionBounds](../Authority/Architecture/ExecutionBounds.md#record-arm-reservation-buffers-bytes-and-lifetimes-2026-09-17).
  Corrections it forced (device `sizeof(MidiEvent)=20`, 1.24 MB arm, boot-aggregate tags,
  the 0xE00 stride argument) are applied in place.
- **C. Bounded shared/reusable stop working storage — DESIGN DONE, DECISION OPEN.** Sharing
  the seven workers is disproved (C1); the levers are capacity shaping from the admitted
  interval horizon (C3, halves ~1.24 MB -> ~0.62 MB if the interval horizon halves) and one
  device-wide deferred-stop working set (C4, ~228 KB per armed slot, restricts concurrent
  deferred stops). Implementation needs a DEC either way. Full text: the step-C entry below.
- **D. Loaded-set / 8-bar rerun — DONE.** `captures/stepD-arm-delta-01`: zero FATAL, zero
  `Recording started`, device alive, playback continuing, one explicit
  `op=companion_history_reserve` + one `op=record_start` denial, arm delta measured at
  1,240 KB, headroom 64-76 KB heap at the refusal. Boot probe `#MEM_ADMIT` in the same run.
- **E. The 1334 KB steady staging — IMPLEMENTED (DEC-065).** Boot no longer reserves
  staging; the record arm and `Track::startOverdubbing` do, reserve-then-require, before any
  mutation. Boot PSRAM headroom +1,189 KB; arm delta 1,469 KB; the loaded-set 8-bar recipe
  now runs end to end with zero denials, refusals and faults. Also fixed the point-of-use
  gate that was denying the 80 KB `InternalHeapFirstAllocator` reserve while ~1.27 MB of pool
  was free. **C3 is now the user-directed next lever**: demand-shaped capacity per owner
  under the existing 2,048 maximum (never a smaller fixed cap), whose certain benefit is the
  arm-time working set. Its latency side is the `Phase::Trim` walk in
  `Loop::stepPreparedCompanionHistory` (one unused row per quantum unit), not the ~65
  `prepare,slice,prep` lines - those are `LoopContentResolution`'s content-driven canonical
  rebuild. C4 is explicitly **not** being taken.
- **F. Transport-start 81920-byte owner — DONE (identification only, no gate).** It is the
  deferred record arm released by `handlePendingRecordStart`; step A already gates it.

Wait: step 5 of the goal phrasing used "record-arm exhaustion is not yet fully an
operation-level denial" - that is now closed (A). Overdub exhaustion was already
operation-level.

**2026-09-16 — task 2.3 continuous queued capture: ingress/staging ownership isolation (user-directed).**
Continue the user-approved `CONTINUOUS-CAPTURE-IMPLEMENTATION-PLAN.md` sequence for
OpenSpec `resumable-midi-boundary-playback-maintenance` task 2.3. The next named missing
owner was the capture admission staging shared between live ingress and the frozen stop job.

Landed this session: `RuntimePlaybackViewSet` now holds two independently prewarmed
capture-admission roles (`captureStagings[2]`, `liveStagingIndex`/`frozenStagingIndex`).
Live ingress writes only the live role; the one admitted stop job borrows the frozen role.
Punch-out freezes the live role and exchanges the two indexes in constant work
(`LoopPlaybackRuntime::exchangeCaptureStagingRoles`); job cleanup resets only its own retired
frozen role. No new owner/module, no canonical/persistence/SD change, no action-edge copy.

Next in this scope, in order: (1) **FIFO/queue-head binding landed** — the ring is
now the admission authority (`captureStopJobTail()` admits, `queue_full` is an
explicit denial, the worker binds `captureStopJobHead()` once per grain,
`!STOP_STATUS` reports `queue=depth/capacity`); (2) **sealed raw-range ownership
landed** — the approved transfer is wired: `CaptureStopJob::rawRange` owns the
stopped extent, `beginDeferredOverdubStop` proves `canSealCaptureRange` before any
mutation and seals at punch-out, `Loop` keeps a job-identified read borrow whose
read source exposes producer and finalizing extents separately, ordering is
scratch-only, closure NoteId resolves from the finalizing extent, and
`retireJobCaptureSourceGrain` retires the owned extent in grains and re-arms the
descriptor. DisplayManager is unchanged and the successor-capture guard is
deliberately still in place. Then (3) **job-owned session/preview/undo/snapshot
receipts — pending note changes, `capturePreview` and the undo baseline landed**
(the worker resolves against `job.pendingChanges`, seals `job.preview`, and records
the undo entry against `job.beforePublished`; the live session keeps independent
state and the Loop's readable accessors serve the job-identified borrows);
**the session-exit snapshot handoff receipt landed as a Loop-owned FIFO ring of 4**
(`prewarmSessionExitSnapshotHandoff`, free-slot gate, one receipt per open session
enforced inside the handoff, publisher consumes the oldest per turn, playback's
stale-root overlay folds every pending receipt) **and the prepared companion history
is interval-qualified** (`PendingStopAdmission::intervalToken()` on every in-flight
consumer, owner-agnostic Cold/Prewarmed), which closes the session/preview/undo/
snapshot receipt slice. Then (4)
causal predecessor and source identity per job; (5) only then remove the Button A,
manager and Track reentry guards and turn the three public reentry cases green —
explicitly not in the same change as the ownership layer. Device PSRAM admission
was measured for the staging change and **passes** (160,536 B/slot,
3.86 MB of 8.38 MB free); the sealed-range build adds four destination directories
per track (+4 pool chunks/slot, −32 KB internal heap) and its boot-prewarm admission
was measured and **passes** (`captures/task23-sealed-range-prewarm/result.json`);
the ring adds no new boot allocation, so the increment A/B device memory check
(build `91e6b38c…`) still covers it.
Task 2.3 and the task-5.3 HITL gates stay open.

**2026-09-16 — canonical base SEVT observation (host/gate defect; firmware RCA closed).**
Firmware RCA **CLOSED / fix verified** (`842166a7`); SEVT lifecycle fix **SHIPPED**. Transport
pre-dispatch arming proposal **REJECTED** as unnecessary for this RCA; reset-before-guard
retained as independent hardening. The remaining defect is host-side: the canonical `base`
managed run cannot observe a legitimate firmware SEVT result.

Goal: allow the managed capture to survive the overdub-stop operation through deferred
completion and the SEVT drain.

Acceptance criteria (pinned before editing the host):

- `base` reaches a genuine overdub commit on a known non-empty loop.
- The overdub-stop press does **not** terminate observation.
- Capture stays alive through `queue_accepted` -> `drain_emit_start` -> `drain_complete`.
- SEVT inventory is evaluated only after that completion or a bounded timeout.
- Missing SEVT becomes a **required** failure again once this works deterministically.
- `scripts/hitl_stop_interruption.py` stays an *additional* interruption-path test, never the
  canonical SEVT authority.
- No firmware behavior changes bundled.

Two host failures, kept separate until diagnosed:

1. **Seeded-slot abort at the overdub-stop press** — the primary blocker, because it prevents
   observing a legitimate firmware result (`captures/sevt-fix-rerun10`).
2. **Empty-slot `skipped_empty`** — likely a fixture/precondition defect: if SEVT verification
   requires committed overdub content, canonical setup must establish that precondition rather
   than accidentally entering record on the Loop-row select and then clearing it
   (`captures/sevt-fix-verify4`).

**Diagnosis (recorded before editing the shared fixture; evidence `captures/sevt-host-probe`).**
The seeded-slot path cannot produce canonical SEVT with a host-only change, because the one-shot
arm is consumed before any overdub content exists:

- `Playback stopped` (the pre-clear transport stop) -> `armed,1` -> `queue_accepted` -> the dump
  is queued against the *pre-existing* seed content.
- `Track cleared` empties the loop -> the idle drain logs `drain_reset_loop` and discards the
  pending dump.
- The overdub stop's deferred completion then logs `queue_skip_unarmed` — the new owner fires,
  but the arm is already spent. This is the designed one-shot contract, not a defect.
- Separately, the run tears the capture down at the press because the canonical
  `PLAYING_AFTER_OVERDUB_STOP` request has no terminal-state confirmation for that landing.

The **empty-slot path is therefore the primary one to fix**, and it matches the fixture-defect
reading: `captures/sevt-fix-verify4` (fresh boot, cleaned slot) has **zero** `stored_verify`
markers, i.e. the arm is still intact — nothing consumed it. Its observed failure is the
precondition, not SEVT: the Loop-row select starts a recording, the pre-clear transport stop
ends it, and the clear long press then fails to confirm `EMPTY`
(`[error] Preconditions failed on track 5: clear step did not confirm EMPTY state`), so the run
aborts before any genuine record; the record seal reported `skipped_empty`.

Consequence for the plan: fix the empty-slot precondition (establish "empty, stopped, ready to
record" without a clear long press) and the observation window together. Do **not** try to make
the seeded-slot path emit canonical SEVT — that needs the arm/drain semantics the user has
closed. Record the seeded-slot limitation rather than working around it.

**Precondition + observation fix landed as `95680fbe` (host-only, no firmware behavior
change).** `_selected_slot_provably_empty` accepts the newest of the ST/DISP evidence (a fresh
`EMPTY` edge beats a lagging DISP row) and `_await_selected_slot_empty` polls it instead of
reading DISP once; the clear request's `already_satisfied` now also accepts a provably-empty
*selected* slot. A deferred overdub stop whose state stays unobservable past the state-sync
timeout is now `EVIDENCE_UNAVAILABLE` and no longer aborts the run (only a genuine disproof
does), and the canonical final transport stop is followed by a bounded
(`STORED_VERIFICATION_DRAIN_TIMEOUT_S = 20 s`) wait for the drain's terminal marker before the
take is scored. Regressions: `test_provably_empty_accepts_state_edge_before_slot_disp_row`,
`test_stored_verification_drain_wait_requires_terminal_marker`.

**Device status:** `captures/sevt-host-verify2` is one full canonical pass — clear
`ALREADY_SATISFIED requested=EMPTY state=EMPTY`, both overdub stops logged "continuing so the
stored-MIDI verification drain stays observable" instead of aborting, the final transport stop
ran, and the capture covers `queue_accepted` -> `drain_emit_start` -> `drain_complete` with
**SEVT=112** and the stored-content contract satisfied (`count_ok/span_ok/bars_ok`,
`count=16/15`, `bars=3/2`). Repeatability is **not** yet established:
`captures/sevt-host-verify3` aborted at the clear precondition because the select-started ghost
recording was still live when the clear ran (DISP `RECORDING` length still climbing), and the
earlier deferred-save stall also recurs. So temporary C stays, `sevt_gate_required` stays
`False`, and the next step is making the empty-slot ghost-capture stop deterministic (the
bounded stop/await loop) before repeating the pass.

**Remaining blocker after that (host/scenario, not firmware): the drain needs idle.** `processDeferredStoredMidiVerification` is gated on
`!isAudiblePlaybackActive() && !isRecording() && !isStoppedRecording()`, and `--preset base`
finishes on an overdub stop that returns to PLAYING. The dump therefore stays pending; the run
then tears down at the overdub-stop confirmation timeout. Measured on the working
stop-interruption run (`sevt-fix-verify1`): `queue_accepted` 1.4 ms after the stop, but
`drain_emit_start` -> `drain_complete` spans **~10.2 s** across five phases, which exceeds the
8 s `state_sync_timeout_ms`. So even a STOPPED landing loses the dump unless the host keeps
observing.

Resolved: the user approved finishing the canonical take on a transport stop. `base` already
sets `--stop-after-overdub` by default, so no scenario flag changed — the run simply never
reached that stop because the overdub-stop confirmation aborted it. Making that outcome
non-fatal (above) is what lets the existing final stop run, which is why no new ending was
added to `canonical_baseline_legacy_args()`.

Guard: success is **not** `drain_complete` alone. The host must retain the stored-content
contract (expected stored span/inventory), so the gate cannot silently degrade into "the
diagnostic machinery ran".

Closure (one commit, after repeated device passes): restore `sevt_gate_required = True`, remove
temporary C / advisory wording, and decide the fate of `bf8fb6a4`'s markers — keep them as
permanent `SESSION_CAPTURE` diagnostics if they stay cheap; they are gated out of production and
have proven useful for exactly this class of HITL diagnosis.

**2026-09-13 — `resumable-midi-boundary-playback-maintenance` (user-selected).**
DEC-060 records user approval of canonical per-pass companion application for
combined capture preparation. Preserve immutable pass membership and apply rows
independently before the canonical merge; flattened admission is not the oracle.
The bounded per-pass owner now applies and merges immutable input descriptors
with pass/event origins and generation/capacity guards. Per-pass mapping now
validates an explicit published-base provenance table and feeds the ranked bridge
compiler/cursor. Full-loop view fill now constructs and owns the table alongside
events from a completed effective-pass baseline. A generation-checked sealed-pass
reader now copies raw chunks in bounded event/chunk visits without allocation
or partial-result exposure (2026-09-14; event-store tests 34/34). Production canonical pass
lifetime integration remains open. Bounded history preparation
now covers Create/Delete/Length/Pitch/Velocity/NoteRange, including contiguous row
collapse and Create identity reactivation. DEC-061 records the approved stable
Create sort correction. A runtime-owned continuation now connects frozen
sealed-pass descriptors through history application to separate effective-pass
inputs, with a generation-bound companion consumer. A direct LoopPasses overload
now selects canonical passes and filters active history in bounded grains;
a frozen-source entry point now checks live revision/generation/epoch, loop
length and capture state even on completed-result access. The typed companion
consumer now also binds the prepared row owner, rejecting same-generation row
reuse and mismatched capture owners. A frozen-overdub entry now orders capture
with private indexes and normalizes it as the final pass without changing the
shared audible arrays. Companion continuation now preserves historical Delete/
Create membership across the stage boundary through a bounded prefix scan.
End-to-end native bridge traces now cover 24 canonical history/capture variants,
a four-cycle open-note capture trace, and epoch cancellation/rebuild in mapping
and compilation. Ranked mapping/compilation now also feeds runtime admission
with live owner/generation checks retained through final selection preflight.
Executable uint64 bounds now compose ranked preparation, mapping and compilation;
the conservative full-horizon result exceeds the existing stop budget, so
completed sealed prefixes can now be reused without selection/read/history replay.
A runtime-owned prefix continuation now binds live revision/generation/length,
latches denial until the source changes, separates cleanup from restart, and
yields ownership to a frozen tail. The VisualCache owner now schedules at most
one prefix child across all tracks, with Q=32 and a global early return. Stop,
recording and existing active visual work retain priority; eligible overdub
tracks rotate without allocating missing runtime storage. A runtime coordinator now extends the owned prefix
through frozen-tail normalization, row preparation and canonical per-pass
companions in separate grains. It binds all child generations and retains
live source/epoch checks through ranked bridge preflight. Production stop routing,
atomic admission, admitted-tail/service proof and HITL remain open. Ranked-tail
bounds now accept an explicit maximum pass count and published-base event count;
C=2048/P=2/B=2048 with no history/companions still needs 7648 additional grains
at Q=32. Invalid quantum, out-of-staging event counts and overflowing legacy
contract totals now deny before stop admission. The order builder now checks
retained adjacency during fill and bypasses merge/copy only when the exact
comparator proves the input already ordered (64 grains for 2048 rows at Q=32).
Unordered inputs retain the merge path; worst-case bounds and production stop
limits are unchanged. Frozen-tail normalization now imports completed canonical
capture indexes into its private array in Q-bounded steps. Shared-order reuse
invalidates an in-flight import; after copying, audible ordering is independent.
Canonical and audible order bindings now also record that storage generation,
so discarding a later audible order cannot revive an older canonical completion.
Stale bindings deny resume/rewrite/readiness; tail admission uses private sorting
until a current canonical order is explicitly rebuilt.
When canonical order is already complete/current, capture normalization has a
196-grain bound at C=2048,H=0,Q=32 instead of 1604. The initial canonical sort is
still charged. The executable prepared-order aggregate now gives 6240 additional
grains for C=2048/P=2/B=2048,H=R=0,Q=32. The general 7648-grain aggregate and
legacy order/rewrite credits are unchanged. DEC-062 now approves a separate
provisional 6240-grain ranked-preparation allowance for continued integration.
Determine realtime reservations and elapsed stop latency with HITL once the
complete path can run; the old 3072 is not a product latency ceiling.
`PendingStopAdmission` now accepts an optional uint64 ranked-work bound at begin,
validates the 6240 allowance and combined counter capacity before mutation,
and tracks ranked credits separately. Finalization requires all three counters
empty; transport Stop preserves credits and changes epoch/destination. Existing
production callers still request zero ranked credits pending typed tail/bridge
routing; exhausting credits does not replace prepared-source readiness checks.
Reserved tail/bridge step entry points now bind the pending stop's frozen
generation, epoch and admitted quantum, charge one credit before one child,
and deny exhausted/stale reservations without advancing it. Completed readiness
queries consume no credit. Native end-to-end coverage uses these entries;
production stage dispatch, setup charging and atomic publication remain next.
A STOPPED-only typed oracle publisher now owns canonical baseline events and
pass origins together, upgrading flat same-identity views before bridge mapping.
It preserves the existing view on invalid baseline/coverage preflight and needs
no allocation. Track's all-stopped merged-event preparation now invokes canonical
baseline construction using idle prewarmed workers. It publishes typed origins
only when the complete canonical event sequence matches the existing oracle;
otherwise it retains flat publication. Temporary workers are released on success
or denial, without cancelling an already-owned prefix. This is synchronous
STOPPED throughput, never an active grain. Production ranked-stop stage dispatch
and atomic canonical/audible handoff remain open. A joint audible preflight now
requires AtomicTransfer phase, exhausted stop counters, matching command epoch,
PLAYING destination, explicit base/target identities and ready bridge/ledger/
cursor state. It is read-only and does not establish canonical companion-row
publication. Final bridge selection also rejects phase >= loop length instead
of accepting it through modulo coverage. Canonical companion staging is still
required before enabling the combined production commit. Loop-owned companion
history staging now prewarms a separate EditPass vector while STOPPED and copies
history metadata, Create payload events and prepared companion rows in Q-bounded
units. Prospective IDs do not advance nextPassId; canonical rows/revisions stay
unchanged. Source/storage/row-owner/ID changes hide the candidate. Logical discard
retains storage. The history handoff now swaps prebuilt canonical rows and the
snapshot companion-ID list, advances IDs/revision, and retains an immutable ID
receipt for the existing undo owner. Snapshot clear/replacement and append guards
reject stale candidates. Old history stays allocated until STOPPED prewarm.
A joint canonical helper now preflights the reserved capture pass and history,
transfers capture, then publishes history without a stale revision recheck.
It retires capture using scalar writes and preserves the one-revision-per-seal
convention. Track's prepared-stop entry now preflights exact canonical/companion owners,
source identities, stop epoch/credits/destination, cursor and ledger, then commits
canonical data and publishes the bridge without a second fallible selection.
Native fixtures verify matching identities and retained selected-bridge storage.
Loop's prepared-history continuation now verifies every staged sealed capture
event against the raw frozen canonical order in bounded event/chunk visits.
The joint handoff requires this proof; same-count payload/order mismatches and
missing/extra events deny publication. The rewrite cursor also binds its original
store. Stop admission now reserves canonical preparation separately from the 6240
ranked grains, rejects combined counter overflow before mutation, and requires
both counters exhausted for commit. An admitted step checks Q, frozen epoch and
owners before charging exactly one canonical grain; completed queries are free.
STOPPED prewarm now reserves both swap sides. After receipt consumption, a
bounded recycler retains allocations and restores preparation within the remaining
row/snapshot horizon. Two consecutive commits reuse the same buffers; exhausted
capacity or intervening canonical/mailbox changes deny reuse. Stop admission now
reserves post-commit history recycling separately, checks the combined credit
limit, and permits consumption only in DeferredCleanup after preparation finishes.
The joint commit requires and preserves that reservation; transport Stop also
preserves it. The native joint fixture charges setup and recycling while retaining
canonical revision and selected-bridge data. Track's recycling entries now bind
that committed Loop and preparation generation, charge receipt-consumed setup
separately, and enforce the admitted Q for each later grain. Wrong owner or
replacement preparation cannot consume the old reservation. Completed queries
remain free only while canonical/mailbox identity stays current. Loop now offers
bounded session undo-ID collection into caller-reserved private lists, preserving
the legacy cursor prefix and duplicate/order semantics. Session mutations invalidate
paused and completed collection. The maximum 2048-row/2048-companion collection
fits 193 grains at Q=32, separate from ranked preparation and recycling. The
collector now also appends the final capture and prepared companion receipt in
bounded stages, preserving the legacy final-pass inclusion and duplicate rules.
Receipt generation/lifetime and canonical identity guard paused and completed
collection. TrackUndo now prepares a private UndoEntry by setting metadata,
retaining the supplied before-snapshot and swapping the bounded ID outputs;
single-pass entries retain the legacy scalar representation. Preparation consumes
the collection without releasing vector buffers or existing snapshot roots.
Global publication and redo/trim retirement still need bounded integration.
DEC-063's accepted lifetime helpers now drain immutable snapshot graphs, persisted
snapshot/chunk payloads and complete UndoEntry resources in bounded units. A
private-stack trim continuation releases each entry before rechecking physical
pressure and shifts surviving entries one per unit. Shared snapshot readers remain
unchanged in per-grain native checks. Focused suites pass 37/37 and 43/43; full native passes 2069 with 1 skipped.
Firmware passes with RAM1 code 425736; strict OpenSpec and whitespace checks pass. These helpers require exclusive unpublished ownership
beyond a no-fail point; production locking, attachments, commands, reservations and
joint dispatch remain open. See the current BRIDGE-PROGRESS lifetime section.
A native pressure-feedback reproducer now proves that retaining removed roots
can change the surviving undo prefix (10 versus 8 entries in the controlled case).
The [undo publication reassessment](../../openspec/changes/resumable-midi-boundary-playback-maintenance/UNDO-PUBLICATION-REASSESSMENT.md)
records the accepted DEC-063 protected transaction with bounded reclamation.
The latest deferred Undo/Redo request wins per affected Track and executes once
after finalization under the destination policy; Transport Stop stays immediate.
Feedback trimming remains unchanged. Implementation and native proof precede
runnable HITL. Focused pool-budget validation passes **18/18**; this review changed no
firmware behavior. Production dispatch,
receipt/undo sequencing and active snapshot-box refill remain open. This canonical staging work is separate from the
6240 ranked-preparation allowance. A required live storage-generation guard now rejects timeline
reset/revision reuse, disabled-history reclamation and deferred edit appends
that can relocate rows without a playback revision change. The marker advances
before reset releases chunks, reclamation moves rows, or append reallocates
history, and remains bound across frozen-tail handoff. Native lifetime and
scheduler policy coverage do not establish an on-device latency bound.
Empty-history normalization and empty-companion merging now bypass
the apply worker and its extra copies, with corresponding tighter bounds.
Per-pass bridge mapping now uses a bounded sorted origin index and binary
search instead of quadratic scans; its maximum Q=32 bound is 2564 grains.
Production atomic transfer/routing, remaining combined-history/companion parity, duplicate
mergeSequence parity, production lifetime enforcement, capture freezing,
and admission remain open. See the
[Create reassessment](../../openspec/changes/resumable-midi-boundary-playback-maintenance/CREATE-HISTORY-REASSESSMENT.md).
See
[scope reassessment](../../openspec/changes/resumable-midi-boundary-playback-maintenance/COMPANION-SCOPE-REASSESSMENT.md).
Task 3.5 is complete: existing MIDI polls and save exclusions cover deferred
OVERDUB_STOPPING. Task 2.3 now has bounded bridge compilation and a production
selected-bridge consumer with native parity, retained cross-wrap closure, and
safe changed-pair instance tests. Frozen ordering now resumes across owner
turns with generation checks and charged filtered rows; frozen capture/companion
bridge filling and compilation now have runtime-owned continuation and an
epoch-bound source admission with read-only publication preflight. Canonical
companion payloads now prepare in runtime-owned, prewarmed storage and share
legacy Hide/Shorten encoding. DEC-059 explicitly corrects the pairing reference
to stable Off-before-On runs within the filtered channel/pitch lane, preserving
LIFO and wrap fallback. The bounded NoteOff lookup passes revised-reference
differential tests. Bounded wrap-head lookup and complete Delete index selection
now match the actual resolver, including retained rows and inferred loop length.
Length overlap actions now walk in bounded quanta, keeping all shortenings
before deletions and holding each action until consumed. Reconstruction wrap
queries now run in two bounded passes and match the distinct reconstruction
reference rules. Complete stack/span/projection/dedupe reconstruction now
produces a prewarmed, source-bound pitch snapshot with no prefix exposure.
Bounded Delete/Length application now executes those actions on a private
candidate, including final ordering and sequential frozen batches with base
provenance. Bounded companion bridge mapping now produces records plus final
canonical ranks, with apply-generation invalidation. Ranked bridge compilation
now owns those ranks through insertion/base ordering and cursor merge, preserving
unchanged-row ties and cross-wrap original closures. Combined capture insertion
and production routing remain open. Aggregate
stop-grain/latency proof, companion bridge publication, and production
capture-side bridge creation,
canonical/audible handoff, and stop recovery remain open.
See [bridge progress](../../openspec/changes/resumable-midi-boundary-playback-maintenance/BRIDGE-PROGRESS.md)
for validation and the remaining integration gates. No firmware upload or
device timing closure is claimed.

**Parallel — [`record_stop_snapshot_publication_gap_display_bugfix.md`](../Plans/record_stop_snapshot_publication_gap_display_bugfix.md) (2026-09-12).**
**Stage 6C-A + 6C-B SHIPPED** — capacity extension + `kExtraBars = 2`. Native 18/25-bar fixtures PASS; device freeze-scaling [`144434`](../../captures/session_20260912_144434.log). **6B DEFERRED**. **GAP-8 SHIPPED** (§6.6.4) — device [`191442`](../../captures/session_20260912_191442.log). **GAP-8b REVERTED** (§6.6.5) — not on branch after `3c9dcd4a`; deferred with GAP-8c design. **GAP-8c REVERTED** (§6.6.6) — [`204310`](../../captures/session_20260912_204310.log). **NOW:** **Stage 7** (§6.6.7) — `missed_preview` / `compose_miss` post-publish; anchor [`130411`](../../captures/session_20260912_130411.log); plan [`record_stop_stage_7_preview_compose_parity_bugfix.md`](../Plans/record_stop_stage_7_preview_compose_parity_bugfix.md).

**RC-P1 SHIPPED (2026-09-12).** Resumable `LoopSnapshotTail` + A4.2 `writeLoopSnapshotPayloadByteSlice` quota fix. Device A4 PASS [`164947`](../../captures/session_20260912_164947.log): `loop_snapshot_tail,done` → `payload_done` → `SAVE,completed`. Plan frozen:
[`deferred_persist_lsv1_edittail_slice_bugfix.md`](../Plans/deferred_persist_lsv1_edittail_slice_bugfix.md). RC-P2 **DEFER**.

**OpenSpec §6.6 partial closeout** — **SHIPPED:** 6C-B, RC-P1-SORT, RC-P1.3, GAP-8 (6.6.4). **REVERTED/DEFERRED:** GAP-8b (6.6.5), GAP-8c (6.6.6). **NOW:** stage 7 (6.6.7). Tracking: [`tasks.md` §6.6](../openspec/changes/immutable-loop-snapshot-passes/tasks.md#66-post-closeout-polish-linked-plans--non-blocking).

**OpenSpec 6.4 SHIPPED (2026-09-12).** Sign-off on device [`164947`](../../captures/session_20260912_164947.log)
(LSV1 tail + `SAVE,completed`); RAM1 **6848**; native **1707/1707** at RC-P1 close — gates not re-run.
HITL `base` re-run waived; 6.3.7 anchor [`030210`](../../captures/session_20260912_030210.log) unchanged.

**Now — OpenSpec 6.5 (archive/sync).** Runtime docs, deliverable tracking, spec sync + archive.
Parent plan:
[`immutable_loop_snapshot_6_3_compatibility_machinery_removal_refinement.md`](../Plans/immutable_loop_snapshot_6_3_compatibility_machinery_removal_refinement.md) (**SHIPPED**).

**Prior — OpenSpec 6.3.7 SHIPPED (2026-09-12).**
`isPlaybackSessionExitPublicationGap` retained for visual hold-only (6.3.2); dead B1
`session_exit_compat` telemetry removed. Host closeout gates finalized
(`test_6_3_machinery_closeout_gate.py`). Native **1699/1705**.

**Prior — OpenSpec 6.3.6 SHIPPED (2026-09-12).**
`copyEffectiveCommittedEvents` snapshot-only; LCR `Phase::Materialize` removed;
`materializeToEventVector` confined to `LoopPasses` (oracle). Native **1699/1705**; RAM1
**6880**. HITL `base` PASS [`030210`](../../captures/session_20260912_030210.log) /
report [`030255`](../../captures/host_midi_automation_baseline_20260912_030255.json).

**Prior — OpenSpec 6.3.5 SHIPPED (2026-09-12).**
Capture construction replay retired — `gatherConstructionCommittedEvents` snapshot-only;
`copyCommittedPassReplayEvents` removed from `LoopCapture`. Native **1699/1705**; RAM1
**6880**. HITL `base` core PASS [`024434`](../../captures/session_20260912_024434.log) /
report [`024519`](../../captures/host_midi_automation_baseline_20260912_024519.json) — persist
`result` stage flake (5.7 class), not 6.3.5.

**Prior — OpenSpec 6.3.4 SHIPPED (2026-09-12).**
Retired `gatherCommittedEvents*` / `mergeMaterializedPassesWithCapture`; callers use
`copyEffectiveCommittedEvents*`. Native **1699/1705**; RAM1 **6880**. HITL `base` core PASS
[`023806`](../../captures/session_20260912_023806.log) / report
[`023847`](../../captures/host_midi_automation_baseline_20260912_023847.json) — persist
`result` stage flake (5.7 class), not 6.3.4.

**Prior — OpenSpec 6.3.3 SHIPPED (2026-09-12).**
Removed `passesMaterializedStore_`, `materializeEditViewFromPasses`, `midiEvents()` /
`getMidiEvents()`; `copyEffectiveCommittedEvents` → `copyCommittedPassReplayEvents`;
`CommittedMidiScratch` snapshot-only. Native **1830/1835**; RAM1 **6880**. HITL `base` PASS
[`022843`](../../captures/session_20260912_022843.log) / report [`022928`](../../captures/host_midi_automation_baseline_20260912_022928.json).
First attempt [`022633`](../../captures/session_20260912_022633.log) failed on persistence
`already_pending` wedge (`persistence_stage_missing:result`) — same 5.7 flake class, not 6.3.3.

**Prior — OpenSpec 6.3.2 SHIPPED (2026-09-12).**
Session-exit gap visual continuity — cache no-op + draw-source
`shouldHoldSessionExitPublicationGapHandoff`. HITL `base` PASS
[`020309`](../../captures/session_20260912_020309.log). Plan:
[`immutable_loop_snapshot_6_3_2_post_record_visual_continuity_refinement.md`](../Plans/immutable_loop_snapshot_6_3_2_post_record_visual_continuity_refinement.md).

**OpenSpec 4b.3** (documented, not closed): [`030210`](../../captures/session_20260912_030210.log)
(post-6.3.7 HITL) confirms owner split — `ODUB,stop,display` 4.3/7.1 ms; first scored
post-OD PLAYING `midi_gap` 43.7/44.2 ms (`persist_save` ~43 ms + `late_on`/`late_clk` on
OD2; first-OD `track_order` ~17 ms). Bound ref [`233642`](../../captures/session_20260901_233642.log)
OD2 **6.9 ms** when `persist_save` absent. Keep open.

**Prior — OpenSpec 6.3.1 C1-a hold-empty (2026-09-12).**
Retired B1/B3/B4 `gatherCommittedEvents*` and B2/B5 gap `materializeEditViewFromPasses`;
DEC-045 idle-stepped session-exit publish unchanged.

**Prior — OpenSpec 6.2 compatibility consumer retirement (2026-09-12).**
B1–B8 consumer compat retired; construction APIs remain until 6.3.2+.
Plan: [`immutable_loop_snapshot_6_2_compatibility_retirement_refinement.md`](../Plans/immutable_loop_snapshot_6_2_compatibility_retirement_refinement.md).

**Prior — OpenSpec 5.7 + 5.7.5 (2026-09-12).**
Boot-drain stall fix: prioritize workspace save drain bypasses slot-load SD gate
between work items, transport defer on new items, and `otherSdIoActive` writer
policy. HITL `base` PASS [`012147`](../../captures/session_20260912_012147.log)
(`loop_persist=217 sliced=True`). Plan:
[`immutable_loop_snapshot_5_7_persistence_work_queue_stall_hardening_refinement.md`](../Plans/immutable_loop_snapshot_5_7_persistence_work_queue_stall_hardening_refinement.md).

**Prior — OpenSpec 6.2.1b B1 C1-a (2026-09-11).**
HITL `base` PASS
[`231439`](../../captures/session_20260911_231439.log).

**Prior — OpenSpec 6.2.1 C1 inventory (2026-09-11).**
Tool: [`analyze_b1_c1_gather_miss_inventory.py`](../../scripts/analyze_b1_c1_gather_miss_inventory.py);
[`190525`](../../captures/session_20260911_190525.log) gap **759 ms**.

**Prior — Immutable OpenSpec 6.1 SHIPPED / APPROVED (2026-09-11).**

**Prior — Immutable OpenSpec 5.6 SHIPPED (2026-09-11).**
Large-loop persist progress, slice bounds, active snapshot during PLAYING save,
stop-path admit-only, timing-critical call-site gates. HITL `base` PASS
[`203803`](../../captures/session_20260911_203803.log)
`VERIFY deferred persist large-loop bounds` `windows=5 progress=True max_pers=44624us
max_save=44931us active=True stop=True ok=True`. Report
[`203843`](../../captures/host_midi_automation_baseline_20260911_203843.json). Plan:
[`immutable_loop_snapshot_5_6_large_loop_persist_bounds_refinement.md`](../Plans/immutable_loop_snapshot_5_6_large_loop_persist_bounds_refinement.md).

**Prior — Immutable OpenSpec 5.5 SHIPPED (2026-09-11).**
Persist execute stays on `StorageManager::processDeferredSaveState`; HITL `base`
[`200657`](../../captures/session_20260911_200657.log)
`VERIFY deferred persist nonblocking` `loop_persist=295 sliced=True drain=0 mo=600`.
Plan:
[`immutable_loop_snapshot_5_5_deferred_persistence_ownership_refinement.md`](../Plans/immutable_loop_snapshot_5_5_deferred_persistence_ownership_refinement.md).

**Prior — Immutable OpenSpec 5.4 SHIPPED.**
Native matrix, HITL `base` [`181945`](../../captures/session_20260911_181945.log),
reboot `LoadLoopJob` [`182529`](../../captures/session_20260911_182529.log), SD LSV1 byte probe
[`192637`](../../captures/session_20260911_192637.log) after routing fix
[`deferred_persist_loopid_track_routing_bugfix.md`](../Plans/deferred_persist_loopid_track_routing_bugfix.md).
Plan: [`immutable_loop_snapshot_5_4_lsv1_verification_refinement.md`](../Plans/immutable_loop_snapshot_5_4_lsv1_verification_refinement.md).
Authority: [DEC-057](../DECISION_LOG.md#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write).

**Prior — Immutable OpenSpec 5.2 APPROVED (docs/decision only).**
[DEC-057](../DECISION_LOG.md#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write):
LoopSnapshot body format version 1; `LSV1` + `formatVersion == 1`; dual-write.
Plan:
[`immutable_loop_snapshot_5_2_storage_layout_approval_refinement.md`](../Plans/immutable_loop_snapshot_5_2_storage_layout_approval_refinement.md).

**Prior — Immutable OpenSpec 5.1 SHIPPED.**
`beforePublishedSnapshot` / `afterPublishedSnapshot` on GUS; deferred after-attach
by originating `UndoEntryId`; pass/ClearSlot restore retained. Native **1841/1**;
RAM1 **6880**; HITL `base` PASS [`150709`](../../captures/session_20260911_150709.log).
Plan:
[`immutable_loop_snapshot_5_1_undo_identity_select_refinement.md`](../Plans/immutable_loop_snapshot_5_1_undo_identity_select_refinement.md).

**Prior — Immutable OpenSpec 4.6 Stage 6 SHIPPED (task 4.6 closed).**
`noteEditFocusMaterializePublishedSnapshotIdentity_` + key match; §5 exclusion
documented (undo/load remain 5.1–5.6). Native **1839/1**; RAM1 **6976**; HITL
`base` PASS [`141547`](../../captures/session_20260911_141547.log). Plan:
[`immutable_loop_snapshot_4_6_snapshot_identity_freshness_refinement.md`](../Plans/immutable_loop_snapshot_4_6_snapshot_identity_freshness_refinement.md).

**Prior — Immutable OpenSpec 4.6 Stage 5 SHIPPED.**
`CommittedMidiScratch` for `legacyMidiEventsFromCommitted`. HITL `base` PASS
[`140021`](../../captures/session_20260911_140021.log).

**Prior — Overdub snapshot bar-crossing membership Stage 5 COMPLETE.**
RC-A hand-off + Stage 4 triangulation + full consumer unwind (6→5→3→1→2)
**SHIPPED** (2026-09-11). Final HITL [`114744`](../../captures/session_20260911_114744.log).
Plan:
[`overdub_snapshot_bar_crossing_membership_bugfix.md`](../Plans/overdub_snapshot_bar_crossing_membership_bugfix.md).

**Prior — Overdub snapshot membership Stage 2 RC-A + Stage 4 SHIPPED.**
`handoffLiveRecordProgressToCommittedPass`; HITL
[`102837`](../../captures/session_20260911_102837.log).

**Prior — Overdub live wrap Stage 2b SHIPPED** (shared NoteId head/tail collapse).
Device [`193615`](../../captures/session_20260910_193615.log): STOPPED
`frame`=`reconstruct`=58; wrap pitches keep On→loopEnd + head. HITL `base`
PASS [`194531`](../../captures/session_20260910_194531.log). Plan:
[`overdub_live_wrap_playhead_tail_display_bugfix.md`](../Plans/overdub_live_wrap_playhead_tail_display_bugfix.md).

**Prior — Immutable OpenSpec 4.6 Stage 4 SHIPPED** (NOTE_EDIT committed-base cache
identity). HITL `base` PASS [`172224`](../../captures/session_20260910_172224.log).
Plan: [`immutable_loop_snapshot_4_6_snapshot_identity_freshness_refinement.md`](../Plans/immutable_loop_snapshot_4_6_snapshot_identity_freshness_refinement.md).

**Prior — LOOP_EDIT length/start fader ignored during RECORD/OVERDUB — SHIPPED.**
`766d1b33`; HITL base PASS [`171420`](../../captures/session_20260910_171420.log).
Plan: [`loop_edit_length_fader_during_capture_bugfix.md`](../Plans/loop_edit_length_fader_during_capture_bugfix.md).

**Prior — Immutable OpenSpec 4.6 Stage 3 — overdub span-cache identity SHIPPED.**
Stage 3: `overdubSourceSpanCachePublishedIdentity_` + reusable match;
native A→B@R; RAM1 **7008**; native **1823/1**; HITL `base` PASS
[`155756`](../../captures/session_20260910_155756.log); occupy machine PASS
[`160337`](../../captures/session_20260910_160337.log).

**Prior — Immutable OpenSpec 4.6 Stage 2 — DrawSourceGatherKey identity SHIPPED.**
Device gate [`154842`](../../captures/session_20260910_154842.log) (`base`).
Host: first-note latency is advisory; serial-follow does not advance offset on
partial lines (was dropping `RECS,stage,publish`).

**Prior — Immutable OpenSpec 4.6 Stage 1 — VisualCache identity stamp SHIPPED.**
Commit `125f8941`; device gates [`151433`](../../captures/session_20260910_151433.log)
(`base`), [`151700`](../../captures/session_20260910_151700.log) (occupy).

**Prior — Immutable OpenSpec 4.5 — SHIPPED.** HITL `base` PASS
([`122812`](../../captures/session_20260910_122812.log)); `overdub_occupy_paint`
PASS ([`123445`](../../captures/session_20260910_123445.log)); compose `02`
PASS **14/14** ([`142119`](../../captures/note_edit_overdub_compose_20260910_142119_serial.log))
after RC1+RC-A. Plan:
[`immutable_loop_snapshot_4_5_cross_gate_verify_refinement.md`](../Plans/immutable_loop_snapshot_4_5_cross_gate_verify_refinement.md).

**Prior — NOTE_EDIT apply PassMaterialize deferral — RC-A shipped.**
Selector Snapshot|VisualCache only; stale VisualCache legal on the request.
Commit `33f21150`. Plan:
[`note_edit_apply_passmaterialize_deferral_bugfix.md`](../Plans/note_edit_apply_passmaterialize_deferral_bugfix.md).

**Prior — NOTE_EDIT F1 select display lag — RC1 shipped (device PASS).**
One `resolveFader1SelectTarget` per inbound F1 pitchbend.
[`095539`](../../captures/session_20260910_095539.log) 77/87 `DNTE`;
[`100844`](../../captures/session_20260910_100844.log) 150/149. Do not
restore PLAYING `timed_burst`. Plan:
[`note_edit_fader1_select_display_lag_bugfix.md`](../Plans/note_edit_fader1_select_display_lag_bugfix.md).

**Prior — NOTE_EDIT control-plane RC3 PLAYING motor ValueOnly — shipped.**
`shouldRunBlockingMotorBurst` is false while PLAYING or OVERDUBBING.
Device [`232026`](../../captures/session_20260909_232026.log) `sequence=value_only`
judged 3. Do not edit compose `01`/`02`/`03`/`04`. Plan:
[`note_edit_control_plane_record_display_stall_bugfix.md`](../Plans/note_edit_control_plane_record_display_stall_bugfix.md).

**Prior — Immutable OpenSpec parent 4.4 closed — inventory only.** Sub-slices
4.4a–4.4h and 4.5a already shipped. Plan:
[`immutable_loop_snapshot_4_4_closeout_refinement.md`](../Plans/immutable_loop_snapshot_4_4_closeout_refinement.md).

**Prior — NOTE_EDIT close must not invent mover Length to loop end — RC1 open.**
[`192140`](../../captures/session_20260909_192140.log) `ChangeLength`
noteId=58 `80–2303` after in-edit overdub. Extend
`shouldRejectMoverPreCommitRow` (inclusive `loopLength - 1`, same skip as
overlap RC3). Do not edit compose `01`/`02`/`03`/`04`. Plan:
[`note_edit_record_first_note_loopend_display_bugfix.md`](../Plans/note_edit_record_first_note_loopend_display_bugfix.md).

**Prior — NOTE_EDIT exit persist parked mover — shipped.** Close commits
parked Visible `currentSpan` ≠ `committedSpan` and does not drop
`applyOwnedEditPassRows` when canonical is empty. Living guide:
[`MOVE_NOTE_LOGIC.md`](../Guides/MOVE_NOTE_LOGIC.md) § Commit / macro
seal. Compose `02` [`225149`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_225149.json)
**13/13**. Do not edit compose `01`/`02`/`03`/`04`. Plan:
[`note_edit_exit_persist_selected_mover_bugfix.md`](../Plans/note_edit_exit_persist_selected_mover_bugfix.md).

**Prior — NOTE_EDIT exit during overdub drops the live piano roll — RC1+RC2 device PASS.**
[`223532`](../../captures/session_20260909_223532.log): close while
OVERDUBBING keeps `frame,65` (not `56`); first STOPPED `DISP,0` is
`2304/65` (not `6144`/`50688`). Compose `02` [`222902`](../../captures/session_20260909_222902.log)
**12/12**. Do not edit compose `01`/`02`/`03`/`04`. Plan:
[`note_edit_overdub_exit_piano_roll_revert_bugfix.md`](../Plans/note_edit_overdub_exit_piano_roll_revert_bugfix.md).

**Reject same-pitch NoteOn while pending — RC1 shipped.** `202226`
On@435 then On@520 then Off@529 left `48@435` unpaired; `203112` paints
`DNTE,48,435,435,1869`. `Track::noteOn` rejects the second On so Off@529
closes 435 (length 94). Not replace-on-retrigger. Not MIDI-input reject.
Do not edit compose `01`/`02`/`03`/`04`. Plan:
[`capture_pending_same_pitch_note_on_reject_bugfix.md`](../Plans/capture_pending_same_pitch_note_on_reject_bugfix.md).

**Prior — NOTE_EDIT control-plane record + display stall — RC6 implementing.**
RC2 shipped (`3fc5744c`). RC4/RC5 in tree (not bundled). RC5 motors
accepted on [`190408`](../../captures/session_20260909_190408.log).
RC6: capture-session mover stays in `projectNoteEditDisplayNotes` after
overlap Hide + Pitch (`190408` type=5 of note 94 @2481, no `DNTE`).
Do not edit compose `01`/`02`/`03`/`04`. Plan:
[`note_edit_control_plane_record_display_stall_bugfix.md`](../Plans/note_edit_control_plane_record_display_stall_bugfix.md).

**Prior — NOTE_EDIT reselect moved note on stacked sixteenth — shipped.**
F1 picks among every note start in the aimed sixteenth (`161839` off-grid
`628` on grid `624`; `153550` exact-tick stack). Device PASS
[`164516`](../../captures/session_20260909_164516.log)
`note_idx` 3 then 4 at dest `5@240`. Native **1806 succeeded, 1 skipped**.
RAM1 locals **7008**. Plan:
[`note_edit_reselect_moved_stacked_note_bugfix.md`](../Plans/note_edit_reselect_moved_stacked_note_bugfix.md).

**Prior — NOTE_EDIT select/pitch during overdub hang — shipped.** `145634` full-loop
gather + `delay()` motor burst. Device PASS
[`152705`](../../captures/session_20260909_152705.log)
`sequence=value_only`; HITL-04 machine PASS 4/4
[`152745`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_152745.json).
Native **1804 succeeded, 1 skipped**. RAM1 locals **7008**. Plan:
[`note_edit_overdub_select_pitch_hang_bugfix.md`](../Plans/note_edit_overdub_select_pitch_hang_bugfix.md).

**Prior — Compose Shorten hold + reopen select — implementing.** RC1 clips
NOTE_EDIT Shorten onto `overdubSourceViewNotes_` and keeps rematerialize
Shorten against a longer visual. RC2 seals rematerialize-open current
state and lets sixteenth select hit an off-grid note in that step.
HITL `STIM-NE-OD-COMPOSE-04` / preset
`note_edit_overdub_compose_last_bar_reopen` (do not edit `01`/`02`/`03`).
Exit/re-enter is short press. NOTE_EDIT close arms loop-fader ignore
before LOOP_EDIT inbound routing.
Plan:
[`note_edit_overdub_compose_shorten_hold_select_bugfix.md`](../Plans/note_edit_overdub_compose_shorten_hold_select_bugfix.md).

**Prior — Compose last-bar beat-4 Shorten HITL — shipped.** Host-only. Stimulus
`STIM-NE-OD-COMPOSE-03` v1. Preset
`note_edit_overdub_compose_last_bar_shorten`. Device PASS
[`133714`](../../captures/session_20260909_133714.log) /
[`133744`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_133744.json):
live On **2113**, F2 to **2112**, Shorten `noteId=56` `2100–2111`. Do not
edit `01` or `02`. Plan:
[`note_edit_overdub_compose_last_bar_shorten_hitl_enhancement.md`](../Plans/note_edit_overdub_compose_last_bar_shorten_hitl_enhancement.md).

**Prior — First record-pass note display vs loop-end Off — parked.** Watch
HITL [`132708`](../../captures/session_20260909_132708.log) did not
reproduce. [`123953`](../../captures/session_20260909_123953.log) store pair
is `SEVT` `N,81` `F,135`. Plan:
[`note_edit_record_first_note_loopend_display_bugfix.md`](../Plans/note_edit_record_first_note_loopend_display_bugfix.md).

**Prior — STIM-NE-OD-COMPOSE-02 promoted.** Everyday compose bind. `01` stays
paint-only (live 12–23). TEMP-A is not registered. Device proof remains
[`123953`](../../captures/session_20260909_123953.log).

**Prior — NOTE_EDIT + overdub paint/overlap compose Stage 2 device PASS.**
Firmware `f7656b1e`. TEMP-A [`123953`](../../captures/session_20260909_123953.log)
HideNote against a live Capture 48 while the apply scratch stayed empty.
Do not change `STIM-NE-OD-COMPOSE-01`. Plan:
[`note_edit_overdub_paint_overlap_compose_bugfix.md`](../Plans/note_edit_overdub_paint_overlap_compose_bugfix.md).

**Prior — HITL stimulus contract Stage 4 shipped.** Everyday catalog:
`host_midi_hitl.py stimulus list`. BASE-N primitives:
`STIM-BASE-2-2-01`, `STIM-BASE-2-ONE-OD-01`, `STIM-BASE-2-FIXTURE-01`.

**Prior — NOTE_EDIT + overdub paint/overlap compose Stage 1 device PASS.**
[`112612`](../../captures/session_20260909_112612.log) open visual **48**,
in-edit overdub `DFRAME` **53 → 63** with 18 live notes. Required stimulus:
`STIM-NE-OD-COMPOSE-01` v1.
Prior FAIL [`110622`](../../captures/session_20260909_110622.log).

**Prior — NOTE_EDIT no-edit empty-store bake.** Shipped.
[`002450`](../../captures/session_20260909_002450.log)
`NoteEditPass close skip empty-store bake`. Plan:
[`note_edit_close_empty_store_bake_bugfix.md`](../Plans/note_edit_close_empty_store_bake_bugfix.md).

**Prior — DEC-056 stale-root companion admission.** Shipped. RAM1 locals
**7008**. Native **1790 succeeded, 1 skipped**. Current-root close
[`000237`](../../captures/session_20260909_000237.log) `root_rev` 4→6.
Miss-path [`000740`](../../captures/session_20260909_000740.log): open
`current=0` `root_rev=8`; close `skip stale-parent publish rows=3`;
source `id=93 p=85 s=1728 e=2016` at `root_rev=16` `play_rev=19`.
Plan:
[`note_edit_close_stale_snapshot_publish_bugfix.md`](../Plans/note_edit_close_stale_snapshot_publish_bugfix.md).

**Prior — Hydrate 4a mover-only seed.** Shipped. Current-root open does
not rematerialize. First focus rebuild seeds the selected mover only.
Move/Pitch PASS [`233644`](../../captures/session_20260908_233644.log),
[`000237`](../../captures/session_20260909_000237.log),
[`000740`](../../captures/session_20260909_000740.log). Session store
stays apply scratch until overlay project. Not 4b–5.
Plans:
[`note_edit_hydrate_enhancement.md`](../Plans/note_edit_hydrate_enhancement.md),
[`note_edit_selectedtick_lcr_resolution_architecture.md`](../Plans/note_edit_selectedtick_lcr_resolution_architecture.md),
[`note_edit_overlay_delta_architecture.md`](../Plans/note_edit_overlay_delta_architecture.md),
[`note_edit_overlay_delta_refinement.md`](../Plans/note_edit_overlay_delta_refinement.md).

**Prior — Hydrate 2–3 device functional PASS [`231008`](../../captures/session_20260908_231008.log).**
Prepared hit: `noteMembership(pitch, bar)` for hold bars. Miss:
`collectAnalyzeOverlapNoteIds` on the already-built visual cache.
`NOTE_EDIT_OPEN` `current=1` `root_rev=4` **75**. First Move
`UNDO_WARM,push,e00,621,75,3` then `candidates_hit`. Hit window (72):
resolve med **8743** µs, scope med **4**. Miss after snapshot not current
(258): resolve med **22206** µs, scan **72–73**. Hide/Shorten/Restore
fire on both paths. Reconstruct med **6411** µs remains. Fast-fader
`hostdrain` peak **1443580** µs is the miss-window leftover, not a
neighborhood or candidate-source fail. Hydrate 4a is the authorized
leftover. Do not start hydrate 4b–5.
Plans:
[`note_edit_hydrate_enhancement.md`](../Plans/note_edit_hydrate_enhancement.md),
[`note_edit_selectedtick_lcr_resolution_architecture.md`](../Plans/note_edit_selectedtick_lcr_resolution_architecture.md),
[`note_edit_overlay_delta_architecture.md`](../Plans/note_edit_overlay_delta_architecture.md),
[`note_edit_overlay_delta_refinement.md`](../Plans/note_edit_overlay_delta_refinement.md).

**Prior — DEC-054 Stage 1 firmware in tree — device recapture failed cadence.**
Select undo warm coalesces, settles 80 ms, and runs after a completed
display frame. [`204458`](../../captures/session_20260908_204458.log)
early select `DFRAME` is in band; back-and-forth select is 3.1–4.6 s /
30 frames (7 warms / 363 ms `focus_snap` in one window). Option B is
not the proposed closeout if DEC-055 is accepted. Plans:
[`note_edit_display_playing_stall_architecture.md`](../Plans/note_edit_display_playing_stall_architecture.md),
[`note_edit_display_playing_stall_refinement.md`](../Plans/note_edit_display_playing_stall_refinement.md).
Do not start Stage 2, hydrate Stage 4a, raise `N`, `T_RT_reserve`, 4.5,
or 4.6.

**Prior — OpenSpec 4.4h — display fallback/identity — Stage 1 shipped.**
`rebuildDisplayNotesInWindow` on a current root copies span notes
(`tryCopyCurrentSnapshotDisplayNotesInWindow`). Overlay gather is
rejected. Live-capture merge keeps the existing gather. RAM1 locals
**6848**; native **1774 succeeded, 1 skipped**. Device
[`201106`](../../captures/session_20260908_201106.log): second overdub
open `from=snapshot` `root_rev=5` `play_rev=5` `notes=114`. NOTE_EDIT
open `current=1` `root_rev=6` **137** / `only_*=0`. `DFRAME` **137**
after second stop through session end. First overdub open
`from=visual` is before `pending=0`. Do not start hydrate Stage 4a,
raise `N`, 4.5, or 4.6.
Plan:
[`display_fallback_identity_snapshot_refinement.md`](../Plans/display_fallback_identity_snapshot_refinement.md).

**Prior — OpenSpec 4.4g — analysis/validation leftover oracles — shipped.**
`validateAndCleanupMidiEvents` uses `Loop::copyIdleValidateEvents`
(current-root span-filtered snapshot events). Overdub analysis
current-root gather already skips pass flatten (4.4a). SEVT / `take_only`
unchanged. RAM1 locals **6848**; native **1773 succeeded, 1 skipped**.
Device [`193700`](../../captures/session_20260908_193700.log): second
overdub open `from=snapshot` `root_rev=7` `play_rev=7` `notes=159`.
[`194039`](../../captures/session_20260908_194039.log): continuation
NOTE_EDIT open `current=1` `root_rev=8` **177** / `only_*=0`. Close
`edits=3`, no `live capture baked`.
[`194438`](../../captures/session_20260908_194438.log): Stage 1 idle
validate at 600.015 s on current `root_rev=1` (record-to-STOPPED + 60 s).
Log-only 2 orphans, 116 events remaining; `DFRAME` **58**. Both overdub
opens `from=snapshot` on a current root.
Plan:
[`analysis_validation_oracle_snapshot_refinement.md`](../Plans/analysis_validation_oracle_snapshot_refinement.md).

**Prior — OpenSpec 4.4f — leftover NOTE_EDIT oracles.** Current-root open
skips `assignMissingNoteIds(loop.midiEvents())`. E: rows undo/redo fallback
uses `Loop::tryCopyNoteEditOpenedSnapshotEvents` (span NoteIds; drops
event-only 64). Pass rematerialize stays the no-current-root fallback.
`take_only` unchanged. RAM1 locals **6848**; native **1772 succeeded, 1 skipped**.
Device [`191346`](../../captures/session_20260908_191346.log): both
`current=1` opens `only_*=0` (120 then 119).
[`192155`](../../captures/session_20260908_192155.log): `current=0` open
keeps pass rematerialize (`only_store=5`); three `current=1` `root_rev=41`
opens are **141** / `only_*=0`.
Plan:
[`note_edit_leftover_oracle_snapshot_refinement.md`](../Plans/note_edit_leftover_oracle_snapshot_refinement.md).

**Prior — Wrap-head vs last-Off pairing RC1–RC3** — accepted without
174646 recapture. Native shipped; [`185428`](../../captures/session_20260908_185428.log)
RC3 analog PASS. Plan:
[`wrap_head_last_off_pairing_bugfix.md`](../Plans/wrap_head_last_off_pairing_bugfix.md).

**Prior — 4.4e Stage 3 + close bake closed.**

**Prior — NOTE_EDIT close bake — snapshot baseline:**
Empty-id close no longer treats snapshot-store vs pass rematerialize as
live-capture rows. Device PASS
[`174646`](../../captures/session_20260908_174646.log): no
`live capture baked`; empty third exit has no `NoteEditPassClosed`;
reopen stays `current=1` `root_rev=13` notes **126**. Plan:
[`note_edit_close_bake_snapshot_bugfix.md`](../Plans/note_edit_close_bake_snapshot_bugfix.md).

**Prior — OpenSpec 4.4e Stage 3 — commit rematerialize from the same root:**
`commitEditAction` persist/replay/session-store reload and
`materializedLoopEventsForNoteEditFocus` use `Loop::copyNoteEditCommitBase`:
pinned open-root span-filtered snapshot events plus this session’s edit
rows. Device PASS
[`173531`](../../captures/session_20260908_173531.log): both `current=1`
opens `only_*=0` (99 then 97). `overlap_baseline_diff` Delete noteId=56
and noteId=11 are on that root. `take_only` 227 vs rematerialize 196 is
capture-only. `current=0` `only_store=1` id `80` is not this gate.
RAM1 locals **6848**; native **1767 succeeded, 1 skipped**.

**Prior — OpenSpec 4.4e Stage 2 — NOTE_EDIT event membership (same root):**
`Loop::rematerializeEditView` on a current published root loads
`EditSession.store` from that root’s event objects whose `NoteId`s are in
`resolveDisplayNotes(*snapshot)` span membership. Device PASS
[`171444`](../../captures/session_20260908_171444.log): both `current=1`
opens `only_visual=0` `only_store=0` (116 then 114). `current=0` opens
after `playback_rev=16` vs `root_rev=11` stay on `passes.materialize`.
Plan:
[`note_edit_committed_oracle_snapshot_bugfix.md`](../Plans/note_edit_committed_oracle_snapshot_bugfix.md).

**Prior — NOTE_EDIT close — NoteRange snapshot reuses pitch:**
`appendEditRowChange` does not write `row.pitch` for `NoteRange`.
Close publish reuses the parent pitch, or the earlier same-`NoteId`
change when Pitch then NoteRange share one `tryPublishNoteEditSnapshot`.
RAM1 locals **6848**; native **1765 succeeded, 1 skipped**. Device PASS
[`163849`](../../captures/session_20260908_163849.log): moved note
`DNTE,63,1584,1584,287`; no pitch-0 `DNTE`; reopen **95**.
Plan:
[`note_edit_snapshot_note_range_pitch_bugfix.md`](../Plans/note_edit_snapshot_note_range_pitch_bugfix.md).

**Prior — OpenSpec 4.4e Stage 1 — NOTE_EDIT visual baseline from current snapshot:**
`resolveDisplayNotes(*snapshot)` copies note-span membership
(`appendSnapshotNotesForBarRange`). Event-flat reconstruct is not the
display interpreter (`155257` 99 vs 97). RAM1 locals **6848**; native
**1763 succeeded, 1 skipped**. Device PASS
[`162207`](../../captures/session_20260908_162207.log):
STOPPED / spans / `stored_notes` / open visual **98**. Wrap-held append
after current-root spans is skipped. Do not start Stage 2/3.
Plan:
[`note_edit_committed_oracle_snapshot_bugfix.md`](../Plans/note_edit_committed_oracle_snapshot_bugfix.md).

**Prior — OpenSpec 4.5a — post-stop snapshot publication convergence:**
Closed. `refreshViewportAfterOverdubStop` holds the last OVERDUBBING
composed frame (`why=stop,from=hold`); no pass-gather on the stop stack.
Drain + `sessionExitQueued` inserts an extra Snapshot turn after every
non-Snapshot owner. First new committed source is the current published
`LoopSnapshot`. Device
[`145633`](../../captures/session_20260908_145633.log): display child 0,
`post_stop` `root_rev == playback_rev`, next open `from=snapshot`.
RAM1 locals **6848**; native **1759 succeeded, 1 skipped**. Plan:
[`overdub_stop_snapshot_publication_convergence_refinement.md`](../Plans/overdub_stop_snapshot_publication_convergence_refinement.md).

**Prior — OpenSpec 4.4c — span-cache snapshot provenance:**
Closed. `overdubSourceSpanCacheReady()` stays a storage/coverage check.
Reuse at open/hold/prewarm requires `overdubSourceSpanCacheReusable()`,
which matches the current published revision when a root is current.
Fallback prewarm at the same `playbackRevision` no longer hides that
root (`135337`). OpenSpec 4.4c. `DEC-052`. RAM1 locals **6848**; native
**1757 succeeded, 1 skipped**.

**Next within 4.4:** NOTE_EDIT, overdub analysis, validation, and remaining
display identity/fallback caller migration remain open. Each sub-slice
requires the existing phase gate and its own rollback/verification evidence.

**Prior — OpenSpec 3.5 / 3.6 closed (yield proof parked):**
3.5 wrap-journal publication is in tree. 3.6 native wrap-journal parity and
device wrap-retain / reserve telemetry
[`101815`](../../captures/session_20260908_101815.log) /
[`104353`](../../captures/session_20260908_104353.log) stand. Remaining-
interval yield device proof is parked: `kSnapshotIdleReserveYieldEnabled`
is false after [`115043`](../../captures/session_20260908_115043.log)
(`remaining=0` until STOPPED). `allowSnapshotIdleReserve`,
`DIAG,snapshot_idle,yield`, and `!SNAPSHOT_YIELD` stay compiled.
`T_RT_safety_margin` stays unfrozen. N = 2. Plan:
[`snapshot_publication_reserve_admission_refinement.md`](../Plans/snapshot_publication_reserve_admission_refinement.md).

**Prior — OpenSpec 3.5 / 3.6 wrap publication notes:**
3.5 firmware is in tree: settled wraps queue bounded mutation journals;
session-exit starts at most one journal-capped fragment; publication waits
until journals, queued passes, construction, and companions complete.
`queueLoadedSnapshotPublish()` stays off after restore (`023222` /
[`024731`](../../captures/session_20260908_024731.log)). This slice does
**not** reconstruct a root from every loaded pass. Task 3.6 native wrap-journal
parity is in tree. Device wrap-retain recapture
[`101815`](../../captures/session_20260908_101815.log): both overdub stops
were still `seal=skipped`, but journals were kept (`exit=1`), `fragment`
advanced (`0/3→2/3`, `0/2→1/2`), and `root_rev` caught `playback_rev`
(`pending=0` `root_rev=3` `playback_rev=3`). Prior fail class
[`094930`](../../captures/session_20260908_094930.log)/`094228` discarded
in-flight wrap journals and left `root_rev` at 1. Host HITL overall still
FAIL for known non-wrap issues (`dispatch`, 2304-vs-1536, SEVT window,
second-overdub first-note). Focused native **83/83**. `T_RT_reserve` policy
is [DEC-053](../DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary)
(`T_RT_reserve_ceiling` 10,000 µs; per-turn `reserve_us`; margin not
frozen). Capture-gated `DIAG,snapshot_idle,reserve` is in
`InvestigationCaptureLog` (FLASHMEM): child emit arms one pending start;
`handleMidiInput()` enter pairs `reserve_us`. Unpaired overwrite emits
`reserve_us=0`. Device recapture
[`104353`](../../captures/session_20260908_104353.log): 1864 `reserve`
lines, `reserve_us` 102–4779 µs (none above the 10 ms ceiling), wrap
`pending=0` `root_rev=3`, both overdub `seal=skipped` still captured with
full `seal_fx_*` sequence. Serial close drain 309 trailing lines. Remaining-
interval admission is now in tree on `MidiServiceDrain` /
`Track::processDeferredIdleMaintenance`: two-unit cost must fit
`min(10000, remaining − margin)` or the turn yields. Capture-gated
`DIAG,snapshot_idle,yield` plus `!SNAPSHOT_YIELD` one-shot inject.
`SESSION_CAPTURE_SNAPSHOT_IDLE=1` is on `teensy41-capture-serial` for this
gate. Snapshot idle cardinality is **N = 2**; do not revert to 1. STOPPED
timings are excluded from the 3.6 ceiling. [`110034`](../../captures/session_20260908_110034.log)
live `reserve_us` max 5871 µs (zero over 10 ms); the 36 / 42 / 95 ms
samples are after `PLAYING → STOPPED`. Host: RAM1 locals **6848**; focused admission/yield native PASS;
full native **1753/1754** (1 skipped). Device yield HITL
[`115043`](../../captures/session_20260908_115043.log):
`snapshot_reserve_admission.py` PASS (`yield_count=6389`, first yield
`RECORDING` `root_rev=0`, complete `root_rev=9` `pending=0`). Every live
yield has `remaining=0`; the first `snapshot_idle` construction is after
`PLAYING → STOPPED`; `pending=0` is that STOPPED publish. No yield after
STOPPED. Host base still FAIL for the known non-slice checks
(`record_first_note_too_late`, 2304-vs-1536, `dispatch`, SEVT window).
Yield reject is parked (`kSnapshotIdleReserveYieldEnabled` is false).
`DEC-047`. `DEC-053`.

**Prior — Loaded hydration stability — device PASS [`024731`](../../captures/session_20260908_024731.log):**
Title exit waits for boot restore only. Adopt/restore do not auto-queue
loaded snapshot publication. After title, Stage 2.1 fallback fills from
passes (`root_rev,0`). Zero `ld=1`. Session-exit publication unchanged.
Plan: [`loaded_hydration_stability_bugfix.md`](../Plans/loaded_hydration_stability_bugfix.md).

**Prior — Visual-cache Stage 2.1 — snapshot membership copy:**
When `publishedSnapshotIsCurrent()`, `rebuildVisualCacheIdleSlice` copies
`LoopSnapshotNote` membership for the dirty bar range. Fallback / prepared
fill stays when no current root exists. Host: RAM1 locals **6848**. Native
**1727/1728** (1 skipped).

**Prior — Overdub MIDI boundary — Stage 4a device attribution [`235930`](../../captures/session_20260907_235930.log):**
Tags emit. Residual ≤ 24 µs; slot ≤ 26 µs. `tracks` ≈ `clk` ≈ `usbclk`.
OD1 peak `tracks` 13149 µs (T4 9007 + T6 3932) under `service_period`
20824. Dominant attributed span on that peak is `track_order` 7974 µs,
not rebuild multiplicity. Ring A is not authorized from this log.
`playCommittedLoopMidi` stays consumer, not optimization owner. Plan:
[`overdub_midi_boundary_timing_refinement.md`](../Plans/overdub_midi_boundary_timing_refinement.md).

**Prior — Overdub-stop first PLAYING frame parity — HITL PASS [`230846`](../../captures/session_20260907_230846.log):**
The first `OVERDUBBING → PLAYING` display frame now preserves the effective
committed handoff while deferred save starts. `refreshViewportAfterOverdubStop`
finalizes from `LoopContentResolution::resolveDisplayNotes` after cache
adoption, and `resolveDisplayNotesByDrawSource` preserves that
revision-matched handoff before normal source/cache rebuilding resumes. The
cache is a reuse seed, not committed-content authority. RAM1 locals **6848**;
focused native **162/162**; full native **1724/1725** (1 skipped). HITL
`DISP` and `dnte` frame/effective counts match on the first and settled frames
(`98`/`98`). RC closed.
Plan: [`overdub_display_snapshot_handoff_bugfix.md`](../Plans/overdub_display_snapshot_handoff_bugfix.md).

**Prior — First STOPPED D# next-On flash — HITL PASS [`225234`](../../captures/session_20260907_225234.log) ([`220319`](../../captures/session_20260907_220319.log) /
[`222432`](../../captures/session_20260907_222432.log) /
[`223121`](../../captures/session_20260907_223121.log)):**
`src,effective` is correct while `src,frame` still uses stale cache geometry.
The `f53cc59a` companion-replay workaround regressed to all-bars lengthening
in [`223121`]. It is reverted. [`224901`] showed the preserved overdub-stop
handoff still bypassed the direct resolver. STOPPED committed display now
calls `LoopContentResolution::resolveDisplayNotes` before that handoff,
reusing the same final-state code as `src,effective`. `af65a70e` remains
because it fixed the deferred effective/rebuild pairing path. RAM1 locals
**6848**. Focused native **232/232**; full native **1724/1725** (1 skipped).
HITL [`225234`] passes: `src,effective` and `src,frame` have matching
11-row D# geometry and `DISP` counts. RC closed.
Plan: [`overdub_stopped_frame_last_occupy_bugfix.md`](../Plans/overdub_stopped_frame_last_occupy_bugfix.md).

**Prior — Overdub same-start A loss and D# Off pairing ([`183043`](../../captures/session_20260907_183043.log) / [`185822`](../../captures/session_20260907_185822.log) / [`192322`](../../captures/session_20260907_192322.log) / [`195409`](../../captures/session_20260907_195409.log) / [`204112`](../../captures/session_20260907_204112.log) / [`211440`](../../captures/session_20260907_211440.log)):**
Stage 1–3, RC-A, overlay, and pending-paint device PASS [`211440`].
Stop D# Off pairing host in tree: deferred visual-cache fallback/full
rebuild, last-resort committed/window reconstruct (capture inactive), and
`dnte,src,effective` use `LoopContentResolution::resolveDisplayNotes` so
committed D# Offs `768→1440`, `1536→2208`, `2304→2976` survive flatten.
`Track::finalizePendingNotes` still closes one genuinely held final D#.
RC-B unmatched NoteOff admission is not started. Native pin
`test_visual_cache_keeps_dsharp_committed_offs_after_rebuild_211440`.
`teensy41-capture-serial` RAM1 locals **6848**. HITL recapture open. Plans:
[`overdub_same_start_note_pairing_bugfix.md`](../Plans/overdub_same_start_note_pairing_bugfix.md),
[`overdub_bar1_compose_miss_bugfix.md`](../Plans/overdub_bar1_compose_miss_bugfix.md),
[`overdub_overlay_identity_bugfix.md`](../Plans/overdub_overlay_identity_bugfix.md),
[`overdub_pending_paint_frame_drop_bugfix.md`](../Plans/overdub_pending_paint_frame_drop_bugfix.md),
[`overdub_stop_dsharp_noteoff_pairing_bugfix.md`](../Plans/overdub_stop_dsharp_noteoff_pairing_bugfix.md).

**Prior — Overdub occupy wrap `norowid` / currentTick temp Off ([`174151`](../../captures/session_20260907_174151.log)):**
Wrap cursor walk offers `(prev, L-1]` then `[0, S]` to
`applyPlaybackEvent`. Occupy span cache / last-pass merge recover
omitted committed NoteOn identities with that same untagged-Off / LIFO
rule (reconstruct can defer Off@`L-1`). Incomplete `visualCache` is a
cache miss, not absence. Paint tails stay downstream. Host pins PASS.
Native **1704/1705** (1 skipped). RAM1 locals **6848**. HITL recapture
open. Plan: [`overdub_occupy_wrap_norowid_noteoff_bugfix.md`](../Plans/overdub_occupy_wrap_norowid_noteoff_bugfix.md).

**Prior — Overdub display snapshot handoff ([`170537`](../../captures/session_20260907_170537.log)):**
Compose the paint window from a complete sealed committed source, then
cache for reuse. `visualCache.notes` is not committed-content authority.
Just-sealed overdub rows join the committed prefix before Hide/Shorten.
A smaller current snapshot must not replace a complete handoff (`id=112`).
MIDI-boundary Stage 3/4 stays out of scope.
Focused native PASS (`test_display_window_utils`, `test_loop_take_survival`,
`test_pending_note_change`, `test_overdub_source_view`). Full native PASS:
1700 succeeded, 1 skipped.
`teensy41-capture-serial` RAM1 locals **6848**. Full native + HITL open.
Plan: [`overdub_display_snapshot_handoff_bugfix.md`](../Plans/overdub_display_snapshot_handoff_bugfix.md).

**Prior — Overdub MIDI boundary timing — Stage 2 (USB batch composition):**
`MidiBatchComposition` plus `RuntimeTimingTelemetry` window tags
`usb_batch`, `usb_bkt_size`, `usb_bkt_clk`, `usb_bkt_disp`. Dispatch
order is unchanged. Native **1689/1690** (1 skipped). RAM1 locals **6848**.
Slave-clock [`170537`](../../captures/session_20260907_170537.log):
`clockrate` 47–48, `service_period` ≈ 20824 µs, `late_clk` = 0.
`usb_bkt_clk` all in 1–8; `clk_batch` max 3 (OD1/OD2 max 1); saturated 0.
OD2 `usbclk`/`tracks` 26.3 ms; `extclk_arrival` 66.6 ms / overCount 240.
Stage 3 FIFO is not authorized — no large clock batch. Internal-clock
occupy [`170249`](../../captures/session_20260907_170249.log) HITL **FAIL**.
Plan: [`overdub_midi_boundary_timing_refinement.md`](../Plans/overdub_midi_boundary_timing_refinement.md).

**Prior — Stage 1 (clock attribution):**
`extclk_arrival`, `extclk_dispatch`, `clk_batch`, `service_period` on
`RuntimeTimingTelemetry`. Occupy HITL [`165045`](../../captures/session_20260907_165045.log)
emits those tags (`service_period=2666`; clock-batch fields 0). Native
**1686/1687**. RAM1 locals **6848**. Stages 3–4 not started.

**Prior — One-frame drop after each next bar ([`142336`](../../captures/session_20260907_142336.log)) — device PASS [`143701`](../../captures/session_20260907_143701.log):**
After `004710` pending-first ordering, `applyCapturePlayheadTails`
still matched open holds by pitch+start only and rewrote a closed
same-start sibling. Capture preview also dropped assigned `NoteId`s
because append fed the raw input event into `applyCaptureEventToPreview`.
Tails now prefer `NoteId`; append feeds the stored `toAppend` event.
Host pins `test_same_pass_playhead_tails_keep_closed_bar_four_end` and
`test_append_capture_preview_keeps_assigned_note_id`. Device
[`143701`](../../captures/session_20260907_143701.log): shortened long
rows stay painted across each next `BAR` (`hidden,0`).
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Incoming long note hidden until NoteOff ([`004710`](../../captures/session_20260906_004710.log)):**
Overlap resolve writes pending transforms on NoteOn, but paint applied tails
before pending. A same-start closed row absorbed the open-hold tail update,
then pending Hide/Shorten removed that row, so the long incoming note was
missing until NoteOff. Paint now applies pending first and tails second in
both live resolve and draw-source gather. Host pin
`test_pending_then_tails_keeps_incoming_long_same_start_004710`.
Device: incoming long notes must appear at NoteOn and grow to `currentTick`
while overlapping earlier shorter notes.
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Earlier remnant dropped the new long occupy ([`003622`](../../captures/session_20260906_003622.log)):**
`002708` covering-occupy drop treated the older longer remnant
(`768–1344`) as the occupy that covers the new long Add (`816–1344`).
Paint kept the short keeper and removed the long Add. An ungrown open
hold on pitch 69 also dropped later A notes for a frame. Covering now
requires the earliest row to be the shortest keeper at its start, and
the hold must actually cover the inner. Host pins
`test_earlier_remnant_does_not_drop_new_long_occupy_003622` and
`test_ungrown_earlier_hold_does_not_drop_later_same_pitch_003622`.
`002708` inners under a true long keeper still drop. Device: long occupy
stays; record A notes do not vanish for a frame each bar.
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Long occupy leaves inner shortened notes ([`002708`](../../captures/session_20260906_002708.log)):**
After a second pass, a long occupy (`864–1344`) covers already-shortened
inners (`1056–1200`, `1296–1335`). Stop effective hid them; the frame
still painted them. Same-start Hide drop kept `1296–1335` as a shorter
keeper because the occupy starts earlier. Hide now drops an inner when
an earlier same-pitch occupy covers it. Hide absorbs a later Length of
the same NoteId. Host pin
`test_long_occupy_hides_inner_shortened_notes_002708`. Device: a long
occupy that covers several shortened notes must not show those inners
on the next `DISP` after the occupy NoteOn, including after wrap.
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Pending Shorten overwritten by sealed Length ([`002200`](../../captures/session_20260906_002200.log)):**
After wrap 2, occupy of already-sealed `73@768–1247` at `hs=1056` writes
pending `768–1055`. Paint applied pending first, then sealed Length
`1247`, so `painted_pending_shorten` never logged 73 and OLED kept the
older end until stop. Stop effective/frame already have `768–1055`.
Sealed overlay now applies first; this-lap pending wins. Host pin
`test_pending_shorten_wins_over_older_sealed_length_002200`. Device:
after two wraps, a further occupy Shorten must show `holdStart-1` on the
next `DISP` before the next `BAR`.
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Same-start Hide remnant ([`001145`](../../captures/session_20260906_001145.log)):**
After several occupy Shortens, a later same-start occupy Hides the
previous Add (`63@1152–1440` `hs=1152`). Hide removes that NoteId, but
the closed remnant stayed because the new occupy is an open hold and
shortest-wins never dominates it. That remnant is the old note that
returns before the new occupy lengthens. Hide now drops closed rows
whose end is at least the hidden source end. Shorter keepers
(`201956` 768–864) and open holds stay. Host pins
`test_same_start_hide_drops_closed_remnant_keeps_open_hold_001145` and
`test_same_start_hide_drops_remnant_when_hold_present_001145`. A Hide
whose target was never in the frame still leaves that row (`RC3`). Device: a
same-start Hide must not show the hidden source on the next `DISP`
before the occupy Add grows.
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Later occupy Shorten remnant ([`000402`](../../captures/session_20260906_000402.log)):**
`painted_pending_shorten` already rewrites later NoteIds (id 73 after
id 71; id 77 after id 75). OLED still shows the original length until
overdub stop because `drawAllNotes` also draws a longer same-pitch+start
sealed-preview remnant. Sibling drop now removes closed remnants at
named Hide/Shorten pitch+starts, including the live-capture suffix.
Open holds stay. Occupying Adds start at hold, not at the source start.
Gather upsert during capture is unchanged (`150650`). Host pins
`test_pending_shorten_drops_suffix_longer_remnant_000402` and
`test_later_occupy_shorten_drops_suffix_remnant_000402`. Device: every
occupy Shorten, not only the first, must show `holdStart-1` on the next
`DISP` before the next `BAR`, before and after wrap.
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Wrap sealed Shorten paint ([`235546`](../../captures/session_20260905_235546.log)):**
First sealed Shorten stayed after wrap; later ones did not. RC-C:
`recoverMissingSealedShortenSources` skipped a Length companion when
snapshot/source-view/visual missed that NoteId. Fallback is the sealed
Length row itself (`74@1536–1967` stayed; `76@2304–2735` must recover).
Host pin `test_wrap_seal_keeps_later_painted_shorten_235546`. Prior RC-A
suffix apply and RC-B recover owner stay.

**Wrap-held head occupy Shorten ([`225200`](../../captures/session_20260905_225200.log)) — Stage 1:**
Pin Failure A passed: wrap-held `69@4560→760` id 96 stays in source view
and `collectOverdubSourceHoldParticipantIds(48, 69)` contains it. Failure
B was occupancy: `startTick < holdStart` wrote Hide (`4560 < 48` is
false). `resolveOccupancyAtNoteOn` now Shortens a wrap-held row present
at a head hold (`hold > 0`, `hold != start`) to `hold-1`.
`composedShortenDegradesToHide` keeps wrap-held Shorten (`end < start`).
Linear Shorten/Hide and `hs=0` Hide are unchanged. Host pin
`test_wrap_held_record_row_occupies_head_225200`. Focused
`test_pending_note_change` + `test_overdub_source_view` PASS.
`teensy41-capture-serial` RAM1 locals **6848**. Native **1669/1672**
(pre-existing playhead-tails fail; one skipped). Device recapture:
post-wrap occupy of that wrap-held row at `hs=48` must be `a>=1` and
`painted_pending_shorten` before the next `BAR`. `63@hs=0` `n=0,a=0`
stays correct. Do not start Stage 2 (stop-hop foreign frame).
Plan: [`overdub_wrap_head_occupy_shorten_bugfix.md`](../Plans/overdub_wrap_head_occupy_shorten_bugfix.md).

**Prior — Wrap NoteOn Shorten recovery ([`223435`](../../captures/session_20260905_223435.log)):**
Post-wrap1 occupies wrote pending Shorten (`as=1536,ae=2208,hs=1824` /
`hs=1872`) but the next paint omitted `painted_pending_shorten`. Later
same-lap `as=2304,ae=2976,hs=2592` painted id 72; stop sealed
`oh shorten=2`. Recovery required VisualCache to still hold the same
`NoteId`. After wrap both compose and VisualCache can miss that identity
while source-view still has the row. `classifyPendingShortenSource` now
recovers whenever compose lacks the target and an authoritative row
exists. VisualCache identity remains `authoritative_absence` evidence
only. Host pins in `test_display_window_utils` and
`test_pending_note_change`. `teensy41-capture-serial` RAM1 locals
**6848**. Native **1668/1671** (pre-existing playhead-tails fail; one
skipped). Device recapture required: first post-wrap Shorten hold must
paint before the next `BAR`.
Plan: [`overdub_paint_wrap_noteon_shorten_bugfix.md`](../Plans/overdub_paint_wrap_noteon_shorten_bugfix.md).
Parent: [`overdub_paint_noteon_shorten_paint_bugfix.md`](../Plans/overdub_paint_noteon_shorten_paint_bugfix.md).

**Prior — reported unfixed undo/reappear after stop ([`221758`](../../captures/session_20260905_221758.log)):**
User reports that a previously undone overdub pass reappears after
overdub stop. That capture boots empty and contains **no undo or redo**.
Not this wrap-recovery RC. Blocked on a capture that contains the undo
and the reappearing notes.

**Prior — Overdub paint NoteOn Shorten on the first paint ([`221758`](../../captures/session_20260905_221758.log), paint gate met):**
[`215658`](../../captures/session_20260905_215658.log) wrote pending Shorten
at NoteOn (`as=768,ae=1440,hs=1104`) but the next paint still matched
VisualCache geometry. Live capture now invokes
`recoverMissingPendingShortenSources`, then applies pending on the paint
copy only. Compose stays original. Device: first occupy Shorten
`65928297` `63@2304–2928` `hs=2784` → `painted_pending_shorten` `65977618`
id **114** `2304–2783` before `BAR,19200,25`; row stays through the next
BAR. Same-start Hides do not emit. No `shorten_src` required. Parent
source recovery and Stage 1+2 stay closed.
Host: focused suites PASS; `teensy41-capture-serial` RAM1 locals **6848**;
native **1667/1670** (pre-existing playhead-tails fail).
Plan: [`overdub_paint_noteon_shorten_paint_bugfix.md`](../Plans/overdub_paint_noteon_shorten_paint_bugfix.md).
Parent: [`overdub_paint_shorten_source_recovery_bugfix.md`](../Plans/overdub_paint_shorten_source_recovery_bugfix.md).

**Prior — pending Shorten source recovery ([`212700`](../../captures/session_20260905_212700.log)):**
Compose can drop a Shorten target when the selected current snapshot
membership omits a row that VisualCache still has (`old,84,4560,4608` →
`new,0,0,0`). Hide can look correct from that absence; Shorten cannot.
`resolveDisplayNotesByDrawSource` recovers the authoritative committed
row by `NoteId` before the paint copy. VisualCache is mismatch evidence
only — never copied. Authoritative absence stays absent. Hide is not
recovered. Stage 1 paint-copy and Stage 2 wrap-append stay closed.
Host: focused suites PASS; `teensy41-capture-serial` RAM1 locals **6848**;
native **1663/1666** (pre-existing playhead-tails fail). Device recapture
superseded by [`215658`](../../captures/session_20260905_215658.log).
Plan: [`overdub_paint_shorten_source_recovery_bugfix.md`](../Plans/overdub_paint_shorten_source_recovery_bugfix.md).
Parent Stage 1+2: [`overdub_paint_noteon_pending_handoff_bugfix.md`](../Plans/overdub_paint_noteon_pending_handoff_bugfix.md).

**Prior — NoteOn pending handoff + wrap-append source guard:**
Stage 1 ([`202821`](../../captures/session_20260905_202821.log)): NoteOn
`pendingNoteChanges_` advances the live paint representation. Compose stays
untransformed; the next paint consumes a pending-painted copy. Overview does
not read a clean VisualCache while pending/overlay exists. Resolver, ledger,
and VisualCache publication stay unchanged.
Stage 2 ([`203406`](../../captures/session_20260905_203406.log)):
`applyDeferredVisualCacheWrapAppend` appends only when the deferred
`playbackRevision` still matches. A mismatch obtains the current-source
frame (`append_skip`, then `full` if the cache is not already current).
Host: `teensy41-capture-serial` RAM1 locals **6848**; native **1660/1661**
(pre-existing playhead-tails fail). Device recapture open.
Plan: [`overdub_paint_noteon_pending_handoff_bugfix.md`](../Plans/overdub_paint_noteon_pending_handoff_bugfix.md).
Do not start Bug 4, Bug 1, or Bug 3 until device recapture closes.

**Prior — Bug 2 occupy On paint named ([`202821`](../../captures/session_20260905_202821.log)):**
Occupy `#CAP,30465195` `63` `as=1536 ae=2208 hs=2016` → next draw-source
`after_pending` and `after_tails` are `id=73` end **2015**. Forks A, B
(at that On), and C are false. First-loop `id=0` + tails growth is the
open-hold skip. Do not patch `applyCapturePlayheadTails`. Plan:
[`overdub_occupy_paint_191913_findings_bugfix.md`](../Plans/overdub_occupy_paint_191913_findings_bugfix.md).

**Prior — Stage 0 RC3 NoteId Off pair reverted ([`191913`](../../captures/session_20260905_191913.log)):**
Restore exact pre-`44f47e94` reconstruct LIFO. Keep `175026` `52@664`
(E=144 vs occupy 363) as a parked finding, not a production pairing
contract. Diagnostic `diagnostic_only_reconstruct_175026_tagged_off_pairing`
is `TEST_IGNORE`. Do not add NoteId pairing fallbacks. 2304 is a legal
loop. Documented order after this pin: Bug 4 → 1 → 2 → 3. Plan:
[`overdub_occupy_paint_191913_findings_bugfix.md`](../Plans/overdub_occupy_paint_191913_findings_bugfix.md).

**Prior — RC3 tagged-Off reconstruct pair (reverted) ([`175026`](../../captures/session_20260905_175026.log) `52@664`):**
`44f47e94` paired tagged Offs by `NoteId`. That fixed one reconstruct
mismatch and is reverted. Parked diagnostic documents LIFO=144 vs
desired 376. Plan:
[`overdub_paint_shorten_live_stop_bugfix.md`](../Plans/overdub_paint_shorten_live_stop_bugfix.md).

**Prior — RC1 pending overlay eligibility ([`172734`](../../captures/session_20260905_172734.log)):**
`applyPendingHideAndShortenToNotes` skips only open capture holds
(`capturePreviewNoteIsOpenHold`). Closed prefix and sealed/closed suffix
rows take pending Hide/Shorten by `NoteId`. `liveCaptureBegin` stays the
sibling-drop / Hide-by-overlap prefix end. No pitch+start fallback.
Host `test_pending_shorten_172734_open_hold_eligibility_matrix`. Device
recapture required: live frame must show this-pass Shorten (`63@680→1207`)
before the next BAR.

**Prior — Capture-serial volume + undo labels ([`213738`](../../captures/session_20260901_213738.log)):**
`SESSION_CAPTURE_SNAPSHOT_IDLE`, `SESSION_CAPTURE_BPM`, and
`SESSION_CAPTURE_DRAW_SRC` default **0** on `teensy41-capture-serial`.
Re-enable independently or via `teensy41-capture-serial-investigation-diag`.
`DISP` / `DFRAME` / `DIAG,clockrate` / `ODUB,stop,*` / `ST` / `VCACHE` stay on.
`TrackUndo::undoForLoop` / `redoForLoop` log `Record undone` /
`Overdub undone` (and the other `UndoEntryKind` names) instead of calling
every non-clear apply `Overdub undone`. HITL still matches `Overdub undone`
/ `Overdub redone` for `OverdubPassAdded`. Not this slice: undo paint
(RC5b keep-larger) or OD1-stop `stale_range` visual shrink. **3.6 `T_RT_reserve`** stays open under [DEC-053](../DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary);
turn `SESSION_CAPTURE_SNAPSHOT_IDLE=1` back on when emitting `reserve_us`.

**Prior — RC1 occupy-paint closed; RC2 / RC3 withdrawn ([`172213`](../../captures/session_20260905_172213.log)):**
RC1d-d sealed closed Off shipped. Occupy-paint verifier **PASS** [`172322`](../../captures/host_midi_hitl_occupy_paint_20260905_172322.json): `rc1b.mismatches=[]`. **RC2 / RC3 withdrawn** — [`140729`](../../captures/session_20260903_140729.log) `77@336` / `69@5328` cannot be remade. Do not start sealed upsert or admission-miss from occupy-paint. Plan: [`overdub_paint_effective_frame_parity_bugfix.md`](../Plans/overdub_paint_effective_frame_parity_bugfix.md).

**Prior — occupy hold shared capture phase ([`162652`](../../captures/session_20260905_162652.log)):**
`Track::snapshotOverlapHoldCandidates` and `Track::collectOverlapHoldPlaybackNoteOn` compute `holdStart` via `Track::capturePhaseTick`, the same frame as capture storage and source-view ticks. Origin-0 `tickPhaseInLoop(..., 0, loopLength)` is off by `startLoopTick` and produced missed occupy, wrong-frame Shorten, and spurious occupy when transport was already running. Host pin `test_occupy_nonzero_start_loop_shortens_at_storage_hold`. HITL occupy_paint required in both transport conditions (`startLoopTick == 0` and nonzero). Plan: [`overdub_occupy_hold_phase_frame_bugfix.md`](../Plans/overdub_occupy_hold_phase_frame_bugfix.md).

**Prior — RC1d-c occupy HITL recapture [`162325`](../../captures/session_20260905_162325.log) (`fedcc25f`):**
Flashed `teensy41-capture-serial`. Occupy holds PASS: OD1 `shorten=0 hide=0`; OD2 `shorten=3 hide=11`, `from=snapshot`, visual 22, GUS 2/2. Dumps reconstruct 39 / effective 28 / frame 26. No two-length. `52@2172` absent. HITL FAIL is one 1-tick key: `52@2180` f=46 e=47 (E=R=47). Plan: [`overdub_paint_effective_frame_parity_bugfix.md`](../Plans/overdub_paint_effective_frame_parity_bugfix.md).

**Prior — RC1d-c first-STOPPED playhead tail ([`153848`](../../captures/session_20260905_153848.log) `52@2172`):**
Last OVERDUBBING playhead tail freezes inclusive 2230 on `liveDisplayNotes`. RC1b skip keeps that prefix after stop. `refreshViewportAfterOverdubStop` rewrites stop-phase tails from sealed closed Off (2231). Host pins PASS. `teensy41-capture-serial` RAM1 locals **6848**. Native **1647/1649** (pre-existing playhead-tails fail). Do not change RC1b skip, events, or E=59.

**Prior — RC1d Loop pin (investigation only, [`153848`](../../captures/session_20260905_153848.log) `52@2172`):**
Host `test_occupy_153848_52_2172_first_inclusive_2230_pin` PASS. Effective Off@2231 / dump 59. Draw-source prefix, sealed union, and companion overlay stay inclusive 2231. No pending/Length companion on the Add at 2230. **Not RC1d-a. Not RC1d-b.**

**Prior — RC1c occupy HITL recapture [`153848`](../../captures/session_20260905_153848.log):**
Flashed `565b87cb` `teensy41-capture-serial`. Occupy holds PASS: OD1 `shorten=0 hide=0`; OD2 `shorten=3 hide=11`, `from=snapshot`, visual 22, GUS 2/2. Dumps reconstruct 39 / effective 28 / frame 26. No two-length. No `F == R` remnant (`48@666` E=F=396 vs R=744). HITL FAIL is one 1-tick key: `52@2172` f=58 e=59. First run [`153705`](../../captures/session_20260905_153705.log) dropped USB before dumps. Extra-short hold-1 keys (268 / 164) are absent. RC2 / RC3 stay gated.

**Prior — RC1c window gather per-layer apply ([`144743`](../../captures/session_20260905_144743.log) / [`152012`](../../captures/session_20260905_152012.log)):**
`copyEffectiveCommittedEventsInRange` applies Active Note rows on each committed chunk list, then merges. Host `test_occupy_144743_48_616_materialize_vs_window_vs_pending` expects window gather **400**, On@885 absent, Off@1016 kept. `teensy41-capture-serial` RAM1 locals **6848**. Native **1644/1646** — pre-existing `test_same_pass_playhead_tails_keep_closed_bar_four_end` (does not call window gather).

**Prior — occupy HITL recapture [`152012`](../../captures/session_20260905_152012.log):**
Flashed `teensy41-capture-serial` (no RC1c firmware). Occupy holds PASS: OD1 `shorten=0 hide=0`; OD2 `shorten=3 hide=11`, `from=snapshot`, visual 22, GUS 2/2. RC1c FAIL — 9 `E != F`, no two-length; dumps reconstruct 39 / effective 28 / frame 25. Representative `48@666` frame 739 / effective 399 (same reconstruct-remnant class as [`144743`](../../captures/session_20260905_144743.log) `48@616`).

**Prior — occupy present-at-hold RC4 ([`134436`](../../captures/session_20260905_134436.log)):**
`resolveOccupancyAtNoteOn` may transform a ledger-selected note only when that note is present at the hold phase. Filter before geometric fallback. Do not change Hide-absorbs-Shorten. Do not retune snapshot fill. Host `a04e2bd5`.

**Prior — effective vs frame RC1b ([`154310`](../../captures/session_20260903_154310.log)):**
After stop, draw-source sealed union does not append a linear row whose pitch+start already exists in the committed prefix. During capture, suffix upsert stays (`150650`). RC1 overlay list stays. Named Hide/Shorten sibling drop now includes closed suffix remnants (`000402`); open holds and gather upsert stay. Do not add gather `keepShortest`. Occupy HITL phase windows close OD2 on `Overdubbing stopped` / `ODUB,stop,enter` when `ST,OVERDUBBING→PLAYING` is missing ([`133818`](../../captures/session_20260905_133818.log)).

**Prior — effective vs frame RC1 ([`140729`](../../captures/session_20260903_140729.log)):**
Display overlay appends sealed companion ids across wraps so frame realizes already-correct effective Hide/Shorten. `lastOverdubSealCompanionIds_` stays last-wrap-only for admission. Overlay list is a transitional bridge, not a second effective owner. Device first-STOPPED two-length FAIL [`154310`](../../captures/session_20260903_154310.log) is RC1b (sealed suffix), not an RC1 revert.

**Prior — RC-D2 `resolveUs` 8 ms (measured only):**
[`122730`](../../captures/session_20260903_122730.log) OVERDUBBING `DFRAME resolveUs` min 2149 / median 8724 / max 15285 (16 of 31 ≥ 8000). Do not start an index. `#41` Stage 2 / off-clock seal stay out. Plan: [`display_draw_source_reuse_empty_bar_bugfix.md`](../Plans/display_draw_source_reuse_empty_bar_bugfix.md).

**Prior — Shorten same-start sibling drop ([`123236`](../../captures/session_20260903_123236.log), [`140729`](../../captures/session_20260903_140729.log)):**
Display Hide/Shorten apply drops a longer same-pitch+start closed sibling of the kept short row, including suffix remnants (`000402`). Device **PASS** for the original prefix-only slice: first STOPPED `src,frame` has no two-length pair (`63@0` reconstruct 144/576 → frame 144 only). Host `88d24b6c`. Not that RC: `77@336` frame 48 vs effective 16; `63@1680` frame 288 while effective omits it. Do not add gather `keepShortest`. Plan: [`overdub_paint_shorten_same_start_sibling_bugfix.md`](../Plans/overdub_paint_shorten_same_start_sibling_bugfix.md).

**Prior — RC-S retain+merge ([`120321`](../../captures/session_20260903_120321.log), [`122730`](../../captures/session_20260903_122730.log)):**
`Loop::retainCapturePreviewAtSeal` retains uncovered sealed rows, then merges this pass’s closed preview via `upsertDisplayNoteAtPitchStart`. Device **PASS**: wrap 2 `sealed` 63→159 (159 > lap-2 preview 106), `frame` 316→311, not visual+latest-lap (`225`). Live `hidden` vs stop `DIAG,oh hide` are different counters (122730 hidden=4 vs hide=1); not a reuse owner. Host `030e1a08`. Snapshot completeness is not a substitute.

**Prior — draw-source reuse RC-D3 ([`101721`](../../captures/session_20260903_101721.log), [`115237`](../../captures/session_20260903_115237.log), [`120321`](../../captures/session_20260903_120321.log)):**
Reuse copies `liveDisplayCommittedPrefix_` and applies overlays/pending on a frame-local copy. [`115237`](../../captures/session_20260903_115237.log) `hidden` 0→2 before wrap 1. `teensy41-capture-serial` already has `SESSION_CAPTURE_DRAW_SRC=1`. Display stays `resolveDisplayNotesByDrawSource`. Do not promote `liveDisplayNotes` to committed-source authority.

**Prior — occupy snapshot source ([DEC-052](../DECISION_LOG.md#dec-052--occupy-consumes-the-draw-source-predicate)):**
`rebuildOverdubSourceSpanCache` consumes `shouldGatherPublishedSnapshotForDrawSource`. Predicate true → exclusive published-`LoopSnapshot` fill (`from=snapshot`). Predicate false → prepared → prior → covering visual → passes unchanged. Plan: [`overdub_occupy_snapshot_source_refinement.md`](../Plans/overdub_occupy_snapshot_source_refinement.md). Native occupy fixtures in `test_overdub_source_view` PASS. `teensy41-capture-serial` RAM1 locals **6848**. Host `test_occupy_paint_serial_verify` PASS. Full native **1628/1630** — one pre-existing fail in `test_same_pass_playhead_tails_keep_closed_bar_four_end` (does not call occupy fill). Device: `overdub_occupy_paint` OD2 open `from=snapshot` when paint `snap!=0`; `occupy_od2_snap_not_zero` is paint/occupy divergence only. Stop totals emit `#CAP,DIAG,oh` so `add`/`shorten`/`hide` fit `kMaxCaptureTextBytes` (192); [`150143`](../../captures/session_20260902_150143.log) `overlap_hold` clipped `hide`. OVERDUBBING reconstruct stays off. Stage 4 stays closed. `#41` Stage 2 stays out.

**Prior — occupy prepared source pin ([`134815`](../../captures/session_20260902_134815.log)):**
`srcskip` `not_finished` because the LCR device gate is STOPPED-only and occupy-paint never idles STOPPED with committed passes (`sess=0 prep=0`). Opens were `from=visual`. [`113651`](../../captures/session_20260902_113651.log) `from=prepared,notes=8` is leftover LCR on a restamped finished session (`prep=3072` at OD1 open), not occupy-paint owner. Plan: [`overdub_occupy_prepared_source_incomplete_bugfix.md`](../Plans/overdub_occupy_prepared_source_incomplete_bugfix.md).

**Prior — occupy-paint HITL + second-overdub stale snapshot ([`092344`](../../captures/session_20260902_092344.log)):**
Preset `overdub_occupy_paint`: record seeds loop length (pitch 72). First overdub writes the half-bar phrase (48/52/56/60). Second overdub occupies that phrase: same-start 56 (Hide), +1/4 on 48/52 (Shorten + wrap), then −1/2 from that (+1/4 − 1/2 = −1/4 vs source) Hide + wrap. Host verifier is `verify_occupy_paint` (OD1 must not Shorten/Hide; occupy gates are OD2). Plan: [`overdub_paint_second_overdub_stale_snapshot_bugfix.md`](../Plans/overdub_paint_second_overdub_stale_snapshot_bugfix.md). Persist/`usbdisp` cadence stays a follow-up. OVERDUBBING reconstruct stays off. Stage 4 stays closed. RC2b stays parked. Source-view wrap-held [`013709`](../../captures/session_20260902_013709.log) stays a separate plan.

**Prior — source-view wrap-held wrong head Off ([`013709`](../../captures/session_20260902_013709.log)):**
Occupy `39@3024→448` at `hs=288` while storage WRAP is `3024→64`. Effective `39@288` len **2784**. A 4-event reconstruct does not emit `3024→448` (Off@64 blocked by intervening On@288; 3024 finishes to 3071). Pin the source-view merge / collapse / companion path next. Plan: [`overdub_source_view_wrap_held_wrong_head_off_bugfix.md`](../Plans/overdub_source_view_wrap_held_wrong_head_off_bugfix.md). Equal-tick consume shipped `932a58d6`.

**Prior — sealed survives current snapshot ([`012714`](../../captures/session_20260902_012714.log), recapture [`013709`](../../captures/session_20260902_013709.log)):**
`sealedCapturePreviewIsCovered` uses `visualCache.builtFromRevision` only. Sealed unions only while capture or the overdub session is active. `openOverdubSession` clears leftover record sealed. Keep-larger during a live session is reverted. Plan: [`overdub_paint_sealed_survives_current_snapshot_bugfix.md`](../Plans/overdub_paint_sealed_survives_current_snapshot_bugfix.md).

**Prior — stop gather keeps adopted linear ends ([`011906`](../../captures/session_20260902_011906.log)):**
`!captureActive` was too wide: wrap seal still has an overdub session, so gather fell back to record `visual=65`. Session flag stays. Keep-larger reverted after `012714`. Plan: [`overdub_paint_stop_snapshot_wrap_end_bugfix.md`](../Plans/overdub_paint_stop_snapshot_wrap_end_bugfix.md).

**Prior — closed occupy + stale snapshot paint ([`005439`](../../captures/session_20260902_005439.log)):**
Device PASS. Occupy frame `63→82`. Wrap 2 frame **131→132** with `snap=0`. Overdub stop published (`adopt_partial` **144**, DISP **157**). Log ends mid-`LoopPersist` (`SAVE,in_progress`) after capture Ctrl+C; no HardFault line. Plans: [`overdub_paint_closed_preview_suffix_hide_bugfix.md`](../Plans/overdub_paint_closed_preview_suffix_hide_bugfix.md), [`overdub_paint_stale_snapshot_after_wrap_bugfix.md`](../Plans/overdub_paint_stale_snapshot_after_wrap_bugfix.md). Reconstruct stays off. Stage 4 stays closed. RC2b stays parked.

**Prior — overdub paint capture handoff ([`234726`](../../captures/session_20260901_234726.log)):**
Reconstruct stays off. While capture is active, `gatherDrawSourceBars` unions `capturePreview` across the window. Plan: [`overdub_paint_capture_handoff_bugfix.md`](../Plans/overdub_paint_capture_handoff_bugfix.md).

**Prior — OD2 visual reconstruct gated during overdub ([`233642`](../../captures/session_20260901_233642.log)):**
Stage 3 `load_frame` is one phase, so VisualCache idle fitted during OVERDUBBING. DEC-045 gate is `allowVisualCacheReconstructSlice`. Device [`234726`](../../captures/session_20260901_234726.log) held `visual` **63** until stop `adopt_partial` **95**.

**Prior — display frame Stage 3 host shipped:**
[`display_frame_phase_split_windowed_paint_refinement.md`](../Plans/display_frame_phase_split_windowed_paint_refinement.md).
`DisplayManager::update` runs one paint phase per admitted LoadAndDisplay turn. Device [`233642`](../../captures/session_20260901_233642.log): `load_frame` **4.7–5.4 ms**; `DFRAME` wall p50 **12.8 ms**; transfer **4496 µs**.

**OpenSpec 4b.1 / 4b.2 / 4b.2c:** closed under Ring B round-robin
([`immutable-loop-snapshot-passes/tasks.md`](../../openspec/changes/immutable-loop-snapshot-passes/tasks.md);
[`playback_window_round_robin_admission_refinement.md`](../Plans/playback_window_round_robin_admission_refinement.md)).
**4b.1** pass = one owner per cursor turn (no true same-turn snapshot/persist
stack). **4b.2** pass = first-record start after leaving bar 1 + live append
on the five-owner cursor. **4b.2c** is closed: capture
[`232633`](../../captures/session_20260826_232633.log) shows one in-flight
record construction (`construction=1`, `queued=0`, `applied=0`, `exit=1`,
`inflight=1`) through PLAYING/OVERDUBBING, then the final `pending=0` turn
publishes `root=0x7030ad1c` with `root_rev=1`/`playback_rev=1`. No later
PLAYING/OVERDUBBING `snapshot_idle` occurs. The apparent violation was the
first-root construction interval, not stacked successor overdub passes.
`DEC-045` remains the historical baseline; `DEC-047` supersedes its post-root
successor restriction only for the activated `3.5`/`3.6` slice. **4b.3**
remains open; exclude `T0/S6` when
scoring post-stop gaps. A capture-only `track_update` profiler now reports
per-track maxima and over-budget counts from `TrackManager::updateAllTracks`;
attribution from [`000834`](../../captures/session_20260827_000834.log):
PLAYING/OVERDUBBING `tracks`/`usbclk` peak is **11.3 ms** (T4 11114 µs,
T6 6520 µs); post-stop `usbdisp` **116.7 ms** is `usbtrans` 116729 µs,
not `updateAllTracks`. RAM1 locals **4640**.

**Snapshot slab packing (`DEC-048`):** `allocateSnapshotShared` bump-allocates
into 32 KB slabs owned by `Loop`. One mutable span root and one event root
are reused per `LoopSnapshotConstruction`. Slab deny fails construction.
Native fixture `test_snapshot_one_bar_node_count_and_unchanged_membership_share`
covers one-bar node counts and successor membership sharing. Native
**1484/1484**. `teensy41-capture-serial` RAM1 locals **5920**.

**Active OpenSpec slice:** task `3.5` firmware is in tree on
`feature/overdub-wrap-snapshot-publication`. Settled wraps queue bounded
mutation-journal fragments; `LoopSnapshotConstruction` uses persistent bar
cursors and fixed-capacity membership pages (16 refs × 8 pages). NoteOff
pairing copies the NoteOn `NoteId` before promotion. PLAYING/OVERDUBBING may
step wrap journals after the first root. Failed fragments are retained.
Task `3.6` remains open: native parity extensions plus the measured
`T_RT_reserve` device gate. Capture
[`173826`](../../captures/session_20260827_173826.log) showed successor
construction after the first wrap (`construction=2`, `root_rev=1`) but
`queued`/`applied` are pass-id fields and cannot tell wrap-journal fragments
from pass reconstruction. `snapshot_idle` now also emits `journals`,
`fragment=i/n`, and `parent_rev` (published revision when the front journal
parent is the selected root; `0` when the parent is an unpublished
candidate). RAM1 admission is restored without USB-host ownership changes:
`teensy41-capture-serial` now reports RAM1 locals **7008** (variables 91296,
code 420652, padding 5332). The follow-up admission fix now allows queued
session-exit snapshot work after the wrap journals drain, and allows the
bounded Snapshot owner to run when tracks are stopped but the transport clock
is still running. Focused native admission coverage passes; full native and
device recapture remain open. Construction now persists membership-copy,
mutation, append, and event-sort progress at one fixed-capacity membership
page per admitted unit; native snapshot/take-survival coverage passes and the
device recapture [`202635`](../../captures/session_20260827_202635.log) still
fails `T_RT_reserve`: `snapshot_idle` reached 79.169 ms because session-exit
start reconstructed a whole queued pass in one admitted unit. Existing journal
caps now also bound that start: `startNextPendingConstruction` walks committed
pass events through a persistent `CommittedEventRange` cursor and begins at
most one journal-capped change fragment. Capture
[`204136`](../../captures/session_20260827_204136.log) then published that
wrap candidate as soon as `journals=0` while `queued=3` session-exit passes
remained, so playback selected an incomplete snapshot and dropped the first
record pass. Wrap completion now keeps the candidate unpublished until queued
passes, construction progress, and companion changes complete. Device
recapture remains open.

**Prior — display watches parked after [`204043`](../../captures/session_20260901_204043.log)**
Not Now implementing. Reopen only if a later capture matches a gate below.
Closed on `204043` (do not reopen): DEC-051 seal-at-0; stop hide re-admit
of `63@768→1151`; wrap-held paint from the flatten `WRAP` scanner;
snapshot-current hide seam; same-start occupy vs reconstruct stacks.
Parked gates:
| Watch | Reopen if a future capture shows | Plan |
|------|----------------------------------|------|
| RC2b wrap-held rewrite | `draw_src` / frame turns a closed same-`NoteId` into wrap-held (`2296→672`, `2304→2976` / `576` / `1008`) | [`overdub_wrap_companion_and_pairing_bugfix.md`](../Plans/overdub_wrap_companion_and_pairing_bugfix.md) |
| `79@240` dual Off | compose/effective end **272** and drawn/frame end **288** with `SEVT` `F@272` and `F@288` | same + [`OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md`](../Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md) |
| Stopped-frame miss | `69@3024` (or another pitch+start) in `SEVT` + effective, absent from stopped frame | [`overdub_occupy_same_start_stop_hide_readmit_bugfix.md`](../Plans/overdub_occupy_same_start_stop_hide_readmit_bugfix.md) |
| Held-note retention | pitch-64 (or equivalent) live hold 40/48 vs MIDI ~624 after seal | [`display_seal_handoff_capture_retention_bugfix.md`](../Plans/display_seal_handoff_capture_retention_bugfix.md) |
Do not patch reconstruct pairing, `applyCapturePlayheadTails` from the red
host pin, or `keepShortestEndAtPitchStart` on the draw-source gather unless
a gate above fires.

**Prior — same-start occupy paint is last overdub + occupy, not a remnant**
Closed as intended on [`204043`](../../captures/session_20260901_204043.log).
Reconstruct still stacks every same-start length (occupy needs both
`noteId`s). Frame/effective follow the last COORD hold, then occupy
Shorten/Hide: `63@0` last COORD `0→576`, later `336→576` Shortens to
exclusive end − 1 = **335**; `768→95` is the same rule (`864−1`).
`1104`/`1536`/`2304`/`69@48` keep the last overdub length (240 / 232 /
240 / 48), not reconstruct's shorter sibling. Do not reopen to force
`0→192` or keep-shortest.

**Prior — same-start Hide must not re-admit the record row at overdub stop**
Device **PASS** [`204043`](../../captures/session_20260901_204043.log):
frame `63@768` len 95; no post-stop `draw_src`; after snapshot-current
`DFRAME` stays 81. Host fixtures shipped. Plan:
[`overdub_occupy_same_start_stop_hide_readmit_bugfix.md`](../Plans/overdub_occupy_same_start_stop_hide_readmit_bugfix.md).

**Prior — DEC-051 overdub wrap seals at loop end ([`193232`](../../captures/session_20260901_193232.log))**
Stage 1 host clock shipped. Device **PASS**
[`201956`](../../captures/session_20260901_201956.log) and
[`204043`](../../captures/session_20260901_204043.log) (wraps at phase 0,
no `WRAP,2304,96`, no `DNTE 0→3072`, SEVT 125/125 linear pairs).
Stage 2 stays gated (no `0→L`). Plans:
[`overdub_wrap_bar4_note_off_session_seal_bugfix.md`](../Plans/overdub_wrap_bar4_note_off_session_seal_bugfix.md),
[`overdub_wrap_loop_end_commit_refinement.md`](../Plans/overdub_wrap_loop_end_commit_refinement.md).

**Prior — overdub wrap RC2b ([`192435`](../../captures/session_20260901_192435.log))**
RC5b device **PASS**: first **U:** `Overdub undone`, `DISP` 75→63. Classic
`2296→672` was not in [`204043`](../../captures/session_20260901_204043.log).
Host tails pin stays **red** (`2976` → `2296`); no tails firmware from that
pin. Parked — reopen only on the capture gates in the parked-watches table
above. RC3 overlay re-admit is closed on `204043`. Plan:
[`overdub_wrap_companion_and_pairing_bugfix.md`](../Plans/overdub_wrap_companion_and_pairing_bugfix.md).

**Prior — display seal handoff RC1 (`153517` coverage over-claim)**
Idle OverlayRejected fallback was stamping `visualCache.builtFromRevision =
playbackRevision` while the gather still carried the stale snapshot's content
(8 vs 10). Retention released and the painter flashed previous-pass notes.
`visualCacheGatherCoversPlaybackRevision` forbids that stamp; `slice_keep` also
no longer claims coverage. Release predicate unchanged.
Committed `c74b8e21`.
Plan: [`display_seal_handoff_coverage_and_wrap_pairing_bugfix.md`](../Plans/display_seal_handoff_coverage_and_wrap_pairing_bugfix.md).

**Prior — display seal handoff (capture retention)**
Recapture [`150650`](../../captures/session_20260901_150650.log) confirms the wrap
publication fix: `root_rev` catches `playback_rev` within 150–220 ms at every wrap
(1, 3, 5, 7, 9, 11, 13, 15, 17), phase-0 holds resolve (`hs=0` reports `a=1`,
`as=0`, `ae=576` from the second pass), and `DIAG,draw_src,diff` is empty. The
STOPPED frame still differs from effective on exactly the two held pitch-64 rows:
frame `s0 len40` / `s768 len48` against effective `624`/`624`, where MIDI input
gives NoteOn storage 0 → NoteOff 624 and NoteOn 768 → NoteOff 1392.

Root cause is a **capture → committed-tier lifetime gap**, not cache arbitration:
the cache's last rebuild before the frame was `slice_clean` at `52023305`, four
seconds before those notes existed, and there is no `slice_keep` in the capture.
The pass sealed at `56135261`, the frame was painted at `56215949`, and `root_rev`
17 published at `56288500` with the covering slice at `56311186`.
`commitPendingCapturePass` cleared `capturePreview` at the seal, so the rows left
the Capture tier before either committed tier carried them.

`Loop` retains closed preview rows at the seal
(`retainCapturePreviewAtSeal`) and releases them only when the committed
representation the painter selects proves coverage
(`sealedCapturePreviewIsCovered`): only `visualCache.builtFromRevision` proves
coverage. A current published snapshot is not membership (`012714` wrap 2).
A later wrap must not `clear()` uncovered sealed rows (`120321`); retain
keeps them and merges the new closed preview.
`resolveDisplayNotesByDrawSource` unions the retained rows into the committed
prefix through `upsertDisplayNoteAtPitchStart`, which replaces only at equal
pitch+start and never acts as a general newer-wins rule.
`keepShortestEndAtPitchStart` is untouched. Retention is a lifetime refinement of
the existing Capture tier: `Loop` owns the rows, display only consumes them,
`VisualCache` only reports coverage, and `DrawSourcePath` is unchanged.
Plan: [`display_seal_handoff_capture_retention_bugfix.md`](../Plans/display_seal_handoff_capture_retention_bugfix.md).
Host gates: `teensy41-capture-serial` RAM1 locals **6848**, native **1566/1566**.
Parked — [`204043`](../../captures/session_20260901_204043.log) has no pitch-64
holds. Reopen only if a later capture shows the held-note retention gate.

**Prior — overdub wrap snapshot publication (tasks `3.5`/`3.6`)**
Capture [`134803`](../../captures/session_20260901_134803.log) proved
occupancy converges (`src,reconstruct` 13 pitch-64 rows → `src,effective` 4:
`0→576`, `768→576`, `1536→512`, `2304→576`) while the drawn frame kept the
raw 144-length rows, because `root_rev` stayed **1** against `playback_rev`
3…11 for the whole overdub. `remainingSessionExitPublishWork` gates wrap
publication on `companionsApplied`, which only ever becomes true on the
session-exit path, so no wrap candidate could ever publish.
`remainingWrapPublishWork` now scopes the companion condition to
`sessionExitQueued`, and each wrap journal carries the `targetRevision`
stamped by `recordLastOverdubSeal` so a candidate is never selected for a
revision it does not contain. Host gates: `teensy41-capture-serial` RAM1
locals **6848**, native **1559/1559**. Device recapture open.

**Prior — snapshot-first bar painter (`DEC-050`)**
Occupancy resolves at NoteOn into `pendingNoteChanges_`
(`resolveOccupancyAtNoteOn`). Paint-only `pendingPaintHides_` is removed.
`upsertSourceTransform` is monotonically restrictive. Deferred stored-MIDI
verification emits `DIAG,dnte,src,effective` after raw reconstruct. Stage B
paint uses `resolveDisplayNotesByDrawSource`: Snapshot when
`publishedSnapshotIsCurrent`, otherwise VisualCache, plus Capture union on
live bars; pending transforms then playhead tails. Stage C logs
`DIAG,draw_src,diff` against the old compose path and is read-only — it saves
and restores compose frame bookkeeping around the comparison. Compose is not
deleted yet. Open on the VisualCache tier: `keepShortestEndAtPitchStart`
(idle slice and wrap adopt) keeps the shortest dual-storage sibling, so that
tier still contradicts effective geometry when the snapshot is not current.

**Prior — visual cache wrap-stale (Stage 1)**
([#41](https://github.com/Lytrix/MidiLooper/issues/41),
[`display_visual_cache_snapshot_reuse_refinement.md`](../Plans/display_visual_cache_snapshot_reuse_refinement.md)):
Stage 1 device PASS
[`202745`](../../captures/session_20260831_202745.log). Three wraps on a
7-bar loop (`live=5376`). No wrap `VCACHE,stale` with `dsz=0` while
`notes>0`. Only `lcr,src` is `why=open,from=visual` at overdub start. First
wrap `DISP` `183,111` → next frame `184,180`. Second wrap first paint
`249,246` (not a hold of 180). Contrast [`201556`](../../captures/session_20260831_201556.log)
`visual=206` / `frame=134`.
**Stage 1b — wrap-pair length:** [`202745`](../../captures/session_20260831_202745.log)
`DNTE` after `OVERDUBBING → STOPPED` shows the same chords at three
lengths (39/45/51 @ 240 as 208, 256, and 304). NOTE_EDIT paint reconstructs
with wrap pairing off and drops the extras. Visual-cache merge of a sealed
wrap pass now uses merged reconstruct; `appendOverdubPassWrapPairedNotes`
skips a longer sibling of an existing pitch+start. Device recapture is
required before calling 1b closed. Native **1514/1514**. RAM1 locals **6944**.
**Stage 1c — wrap-held display:** [`204132`](../../captures/session_20260831_204132.log)
second wrap occupy already has `39@192–592` and `39@336–592`. STOPPED DNTE
is short (`192/16`, `336/112`). NOTE_EDIT open
`rebuildVisualCacheFromPasses` then `VCACHE,full` 291 notes and focus
`presence=0`. Visual-cache wrap append is wrap-held heads/tails only.
Source view keeps full wrap pairing.
**Stage 1d — no wrap-tick visual merge:** shipped `d35d64a7`. Device FAIL
[`205917`](../../captures/session_20260831_205917.log): wrap 2 idle
`slice_clean` `dsz=0` while longs stay in `visualCache` (`DISP`
`243,243`). Overlay fill is not current committed.
**Stage 1e — idle-slice current fill:**
[`display_visual_cache_idle_slice_current_fill_bugfix.md`](../Plans/display_visual_cache_idle_slice_current_fill_bugfix.md).
`rebuildVisualCacheIdleSlice` discards Overlay (`flat` only; no fallback
reset) and fills from prepared LCR or budgeted
`gatherCommittedEventsInWindow`. Playback Overlay contract unchanged.
Native **1518/1518**. RAM1 locals **6944**. Device recapture required
(`205917` shape: wrap 2 first neighborhood must match committed; wrap 3
must not be the first shorten).
**Stage 1f — STOPPED paint current fill:**
[`display_visual_cache_stopped_paint_current_fill_bugfix.md`](../Plans/display_visual_cache_stopped_paint_current_fill_bugfix.md).
`resolveDisplayNotesCommitted` last-resort discards Overlay and falls
through to `gatherCommittedEvents`. Playback Overlay and
`rebuildDisplayNotesInWindow` unchanged. Native **1519/1519**. RAM1
locals **6944**. Device FAIL
[`220337`](../../captures/session_20260831_220337.log): first 8-bar
STOPPED `DISP` **276 / 344** (no `paint_src`); wrap/stop
`stale_range` is `dcnt=8` on an 8-bar loop; PLAYING Snapshot idle then
**334 / 334**. Third stop `OverlayRejected` then **80 / 91**. Play is
still the first match.
**Stage 1g — wrap dirty span:**
[`display_visual_cache_wrap_dirty_span_bugfix.md`](../Plans/display_visual_cache_wrap_dirty_span_bugfix.md).
`markTickSpanDirty` marks wrap-held (`endTick < startTick`) On/Off bars
only. Linear pass spans stay inclusive. Native **1521/1521**. RAM1
locals **6944**. Device [`221925`](../../captures/session_20260831_221925.log):
4-bar loop, wrap `dcnt=4` from linear 39/45/51 in every bar (not a
wrap-interior fail). Wrap 1 `slice_clean` `63→102`, wrap 2 `102→124`.
Wrap 3 idle `OverlayRejected`/`src,None` — no `slice_clean`. First
STOPPED `DISP` **128 / 137** (no `paint_src`). Stop `overlap_hold`
`hide=8`. `DNTE` stacks `39@48` and `39@432` at **144 and 256**. MIDI 47
(B2) is absent. 1g does not own hide or wrap-pair length.
Recapture [`222349`](../../captures/session_20260831_222349.log) (same 4-bar
loop, later overdub): B3 is the only pitch held across stop (`MI` ON
`325318691`, seal `325433453`, OFF `325506187`); `CLN,wrap_synth,0`.
Each wrap first `DISP` drops (`102→67`, `119→101`, `135→121`, `157→136`,
`162→150`) then `slice_clean` restores. First slot0 STOPPED `DISP`
**159 / 31**; `paint_src,OverlayRejected` then **159 / 166**. No `DNTE` /
`SEVT` / `WRAP` dump in this file — length-to-loop-end is not in the log.
**Stage 1h — held-note wrap pair:**
[`display_visual_cache_held_note_wrap_pair_bugfix.md`](../Plans/display_visual_cache_held_note_wrap_pair_bugfix.md).
`appendCanonicalSpansFromMidiRange` stack fallback assigns `Off@0` only
when the last On is wrap-held (`startTick >= tailStart` and
`isPreferredWrapTailForHeadOffInSpanBuild`). Mid-loop B3 `1856` keeps
`F@1912` / `F@1920`. `finalizePendingNotes` still closes at playhead.
Native **1523/1523**. RAM1 locals **6944**.
**Stage 1i — wrap/stop first paint:**
[`display_visual_cache_wrap_stop_first_paint_bugfix.md`](../Plans/display_visual_cache_wrap_stop_first_paint_bugfix.md).
Dirty overdub layer holds before snapshot window gather. STOPPED assigns
dirty nonempty `visualCache` when the composed handoff is gone (slot
mismatch). Last-resort Overlay reject stays for empty cache.
Native **1525/1525**. RAM1 locals **6944**. Device FAIL
[`230400`](../../captures/session_20260831_230400.log): wrap 1 `105→66`
(cache 63). Hold is committed-prefix only; sealed capture is gone. Next: 1k.
[`225243`](../../captures/session_20260831_225243.log) first STOPPED
**147 / 198** kept RC5a handoff (same slot); the STOPPED hold did not run.
**Stage 1j — stop handoff playhead tail:**
[`display_visual_cache_stop_handoff_playhead_tail_bugfix.md`](../Plans/display_visual_cache_stop_handoff_playhead_tail_bugfix.md).
`applyCapturePlayheadTails` writes `liveDisplayPendingPaintNotes_` only.
`liveDisplayNotes` stays at capture-preview lengths so RC5a does not freeze
`endTick = currentTick` (`225243` D#2 ON@624 → stop phase 704). Unmatched
`N@2928` / `F@0` → `loopLength` is a later reconstruct RC.
Native **1526/1526**. RAM1 locals **6944**.
Device [`231953`](../../captures/session_20260831_231953.log) dump gate
PASS: `DIAG,dnte,src,reconstruct,count,221`, `src,frame,count,160`,
`WRAP` 144, `SEVT` 707. First STOPPED **160 / 148** matches the frame
dump. Stop `COORD` storage **1168**; frame dump has no note ending at
1168 (1j playhead freeze not on that frame).
**Stage 1k — wrap compose hold:**
[`display_visual_cache_wrap_compose_hold_bugfix.md`](../Plans/display_visual_cache_wrap_compose_hold_bugfix.md).
Grow compares cache to `max(committedCount, composed liveDisplayNotes)`.
**Stage 1l — Off@0 leftover tail pair:**
[`display_visual_cache_off_zero_tail_pair_bugfix.md`](../Plans/display_visual_cache_off_zero_tail_pair_bugfix.md).
Leftover tail-window On + leftover `F@0` wrap-splits to `L-1` (not
`tryPairWrappedTailOn`). Mid-loop linear pairs stay linear (1h).
**Stage 1b / occupy hide on hold (`003616`):**
[`overdub_occupy_hide_on_hold_current_tick_bugfix.md`](../Plans/overdub_occupy_hide_on_hold_current_tick_bugfix.md).
Occupy On / playback cross upserts a **paint-only** Hide
(`pendingPaintHides_`) so the earlier note drops on the currentTick frame.
**Wrap-seal RC (`005757`):**
[`overdub_wrap_seal_occupy_hide_deletes_shorten_bugfix.md`](../Plans/overdub_wrap_seal_occupy_hide_deletes_shorten_bugfix.md).
`commitOverdubWrapAtSessionStart` must not seal occupy Hide as Delete.
OverlapNoteOff accumulate is Shorten / Length again. NOTE_EDIT
`OverlapNoteOff` stays Shorten. Parent unique-key revert:
[`overdub_occupy_same_start_shorter_hides_longer_bugfix.md`](../Plans/overdub_occupy_same_start_shorter_hides_longer_bugfix.md).
**NoteOff OverlapNoteOn (`010926` RC1):**
[`overdub_wrap_occupy_noteoff_shorten_not_delete_bugfix.md`](../Plans/overdub_wrap_occupy_noteoff_shorten_not_delete_bugfix.md).
`accumulatePendingNoteChangesFromSourceNotes` Shortens OverlapNoteOn to
occupy exclusive end − 1. CompleteCover still Hides / Deletes.
**norowid paint + wrap cache overlay (`010926` RC2):**
`upsertPendingHidesForOccupiedNoteIds` falls back to present-at-hold
source-view rows when ledger ids have no source-view row. Idle slice,
`adopt_partial`, wrap append, and grown-cache assign apply pending paint
so wrap first `DISP` does not restore occupied rows. Focused native
pending **72/72**. Native **1539/1539**. RAM1 locals **6848**. Device
recapture required (`010926` shape).
**NoteOn paint + capture mirror (`104129` / `110022` / `111343`):**
[`overdub_occupy_on_noteon_paint_immediate_bugfix.md`](../Plans/overdub_occupy_on_noteon_paint_immediate_bugfix.md).
Geometric present-at-hold fallback when ledger occupy is empty (`n=0`).
Over-budget overdub resolve still mirrors `capturePreview` and playhead
tails, including after a committed-layer rebuild. Wrap adopt: paint Hide
on committed prefix only; same-start dedupe does not drop longer live
capture over shorter committed sibling. Compose counts clamp to the
vector after hide/dedupe; `liveDisplayNotes.resize` never grows
(`111343` `DNTE,0,0,0,0`). Overdub playhead tails stay on over-budget
frames (`112806`). Native **1544/1544**. RAM1 locals **6848**.
Device recapture required. Stop reconstruct still shows unshortened
first-overdub E4 `760–1920` beside later `768–144` (`overlap_hold`
`shorten=0`).
**Stage 1m — stop compose adopt (`000013`):**
[`display_visual_cache_stop_compose_adopt_bugfix.md`](../Plans/display_visual_cache_stop_compose_adopt_bugfix.md).
Other tracks STOPPED first and cleared `liveDisplayNotes` (141→31). Adopt
compose onto the outgoing loop when it is larger than that loop's
`visualCache` on a short loop. Wrap-hold adopts so cache tracks compose
(wraps 8–11). Full-loop idle must not replace a larger cache with a
smaller slice (`slice_keep`, Prepared 95→80). Occupy `hide=2` is not this
slice. Device recapture: first STOPPED on the overdub loop ≥ last
OVERDUBBING compose; later PLAYING must not fall to 80; cache must not
freeze at 95 while compose grows.
Native **1537/1537**. RAM1 locals **6848**. Occupy hide is in tree
(source-view reconstruct keeps both `noteId`s; adopt/idle keep the shortest
display end). Device recapture required for [`002145`](../../captures/session_20260901_002145.log)
`norow` 75/76 and pitch-63 frame 96+288.
Device [`002145`](../../captures/session_20260901_002145.log) (4-bar `len=3072`):
**1k PASS** — every wrap first OVERDUBBING `frame` ≥ last-before (odub1 wrap 1 `71→72`; odub2 wrap 1 `94→95`).
**1m PASS** — odub1 last `88/80` → `adopt_partial` **88** → first STOPPED **88/88**. Odub2 last `145/144` → `adopt_partial` **145** → first PLAYING **145/145**. Cache tracks compose (`88→94→103→113→120→128→136→144`). Settled STOPPED **145**, not 80. Global stop still paints other slot-0 loops (`32`, `42`, `50688/1429`) then returns to **145**.
**1b** reconstruct **PASS** (0 same-start stacks, count 73). Frame dump **FAIL** on that firmware: pitch **63** at `0/384/768/1152/1536/1920/2304/2688` still has lengths **96 and 288** (frame 88).
**1l not in this log** — no `SEVT` `F@0`.
Stage 2 is unblocked and not started. Do
not start all-track idle fan-out,
`T4 track_update`, N / pages knobs, or OpenSpec `3.5` / `3.6` checkoff.

**Shipped — live-record skip parent note lookup**
([`live_record_snapshot_start_skip_parent_note_lookup_bugfix.md`](../Plans/live_record_snapshot_start_skip_parent_note_lookup_bugfix.md)):
`fromCaptureStore` fragments do not call `findNoteAtPitch`. `openNotes` is
the sole pairing mechanism; the parent is not an identity source. Session-exit
keeps the lookup. Device [`192422`](../../captures/session_20260831_192422.log):
32 live-record starts, `note_lookup=0`; after-first `start` 167–233 µs.
The [`190654`](../../captures/session_20260831_190654.log) lookup-driven
`start` growth (375→1007 µs) is gone.

**Shipped — live-record snapshot start bound**
([`live_record_snapshot_start_bounded_fragment_bugfix.md`](../Plans/live_record_snapshot_start_bounded_fragment_bugfix.md)):
RECORDING `startNextPendingConstruction` walks `capture.store` from
`appliedLiveRecordEventIndex` through `tick < includeUntil` and emits one
journal-capped fragment via `appendNextPassChangeFragment`. No full-store
`copyEventsTo` / reconstruct. `openNotes` persist across fragments and bars.
Row-22 admission stays one start XOR one step. Native take-survival live-record
fixtures pass. Native **1507/1507**. RAM1 locals **6944**. Device non-growth
capture is required before calling the RC closed. Do not freeze
`T_RT_reserve`. Do not raise N.

**Shipped — overdub-stop incremental admission (`DEC-049`)**
([`overdub_stop_incremental_admission_bugfix.md`](../Plans/overdub_stop_incremental_admission_bugfix.md)):
`stopOverdubbing` reanchors traversal and keeps a still-valid full-loop
merged stream. The next `playCommittedLoopMidi` may admit the Rank-6 seal
receipt incrementally. `playback_build` labels `snapshot` / `overlay` /
`compatibility` from the branch actually taken. While a publication
backlog is present, Snapshot idle is 2 of every 6 turns. Native **1503/1503**. RAM1 locals **6944**.
Device [`152513`](../../captures/session_20260831_152513.log): T4 16-bar
Stage 2 PASS. Device [`153113`](../../captures/session_20260831_153113.log):
T4 11-bar Stage 3 drain PASS — 16.197 s after the last stop,
`pending=0` / `root=0x703fadf0` / `root_rev=7` / `playback_rev=7` /
`publish=9`, then `playback_build,snapshot`. Device test: Snapshot idle
passes `stepPendingSnapshotPublish(2)`
(`MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn`). Recapture
[`161532`](../../captures/session_20260831_161532.log): T4 16-bar, last
stop `incremental` `rev=8`, then 1.824 s / one `BAR` to
`pending=0` / `root_rev=8` / `playback_rev=8`. That drain already had
`root_rev=1`; it is not the empty-root 16.197 s class of
[`153113`](../../captures/session_20260831_153113.log). `start+step`
same-turn count is 61; parent max **13.350 ms** (`start=12891` +
`step=423`); 7 parents ≥ 8.82 ms. Drain-window parent max **4.371 ms**.
Device test now: `stepPendingSnapshotPublish(2)` and
`kSnapshotMembershipMaxPages = 16`. Recapture
[`165637`](../../captures/session_20260831_165637.log) (N=2, 16 pages):
T4 16-bar, last stop `incremental` `rev=9`, then **1.885 s** to
`pending=0` / `root_rev=9` / `playback_rev=9` with `root_rev=1` already
present. `start+step` = 58; parent max **12.138 ms**; 3 parents ≥ 8.82 ms.
Drain parent max **4.413 ms**. Compare
[`153113`](../../captures/session_20260831_153113.log) (N=1, 8 pages),
[`161532`](../../captures/session_20260831_161532.log) (N=2, 8 pages),
and [`164448`](../../captures/session_20260831_164448.log) (N=1, 16 pages).
`164448` + tail [`165027`](../../captures/session_20260831_165027.log):
T4 is 16 bars / 12288 ticks from full-loop `incremental`. `start+step` = 0.
Last stop `rev=13` @ 559.185 s; publish @ 608.693 s (**49.508 s**)
`pending=0` / `root=0x70498e30` / `root_rev=13` / `playback_rev=13` /
`publish=8`. `165027` has a 40.114 s gap with no `snapshot_idle` between
`journals=13` `fragment=9/18` and `journals=0` `applied=3`. Drain parent
max 4.330 ms in `164448`; tail parents ≤ 0.493 ms. 4 live-record parents
≥ 8.82 ms (start only, max 10.887 ms). ExecutionBounds still documents 8.
Do not freeze `T_RT_reserve`. Do not check off OpenSpec `3.5`/`3.6`.

**Shipped — wrap-tick derived rebuild**
([`overdub_wrap_derived_rebuild_clock_interval_bugfix.md`](../Plans/overdub_wrap_derived_rebuild_clock_interval_bugfix.md)):
Device recapture [`135510`](../../captures/session_20260831_135510.log):
wrap child line `total=41851,seal=36025,pass_apply=676,src_append=5150`;
`track_update[4]=41902` in that window (not the [`005527`](../../captures/session_20260828_005527.log)
77.618 ms class); no `why=wrap`; Rank 6 gather `playback_build` 7547 µs at
`57711324` is the next-tick owner this slice removes. Native **1496/1496**.
RAM1 locals **6944**.

**Immediate Stage 1**
([`snapshot_publication_timing_bugfix.md`](../Plans/snapshot_publication_timing_bugfix.md)):
`MidiServiceDrain::allowSnapshotIdleStep()` now admits pending session-exit,
in-flight, or wrap-journal Snapshot work in `STOPPED` even when
`visualCacheNeedsFirstPaint` is true. `STOPPED` ends active MIDI ownership;
it does not mean pending snapshot work is complete. Native policy fixture
`test_stopped_pending_snapshot_wins_over_first_paint` covers that
combination. Device
recapture [`214832`](../../captures/session_20260827_214832.log) does **not**
close Stage 1: wrap journals drained with `queued=4` and `root=0x0`, then
STOPPED first-paint `VCACHE` ran and no `STOPPED` `snapshot_idle` appeared.
[`215420`](../../captures/session_20260827_215420.log) also holds completeness
(`root=0x0`) and also has zero `STOPPED` `snapshot_idle` across a 16 s hold;
that hold is not first-paint (`VCACHE` `slice_clean` `notes=166` `dirty=0`
before stop, no post-stop `VCACHE`).
[`215845`](../../captures/session_20260827_215845.log) repeats that on a
120 s STOPPED hold: last PLAYING idle still `pending=1` `queued=3`
`journals=2`, then persist/`idle_maint` only, no `STOPPED` `snapshot_idle`.
STOPPED Snapshot admission now scans every slot and selects a track with
pending publication instead of the visual idle cursor. Device recapture
[`221054`](../../captures/session_20260827_221054.log) **closes Stage 1**:
first STOPPED `snapshot_idle` at +0.070 s, candidate stayed `root=0x0` until
+77.765 s, then `pending=0` published `root=0x70400ac8` / `root_rev=10`.

**Immediate Stage 2 leftover named**
([`snapshot_publication_timing_bugfix.md`](../Plans/snapshot_publication_timing_bugfix.md)):
Device recapture [`225659`](../../captures/session_20260827_225659.log)
closes the 224842 leftover: record-stop PLAYING idle at 30.164 s is
**20.984 ms** with `discard=15262` (`discardPendingIfLoopLengthChanged`),
`start=5704`, `enqueue=5`, exclusive leftover **13 µs**. Exclusive leftover
after `enqueue`/`discard` is ≤ 39 µs on every paired row. `enqueue` max is
14 µs. `emit` max is 52 µs and sits after the parent `us` stamp.
STOPPED session-exit `start`/`pass_change` reaches **19.996 / 19.968 ms**.
`step` max is 5.992 ms. Completeness held (`root=0x0` until the final
`pending=0` row). Publication closed at 183.121 s:
`root=0x70431844` / `root_rev=8` / `finish=671` / `publish=10`.
First STOPPED idle +0.066 s. Child-line budget held (31701/31701 complete
child rows). The Stage 2 falsification review is recorded in the plan.
Session-exit length handoff device PASS
[`003015`](../../captures/session_20260828_003015.log): first PLAYING
idle after record stop is `discard=47` (was `15262` on
[`225659`](../../captures/session_20260827_225659.log)). The candidate
stayed `root=0x0` until `pending=0` / `root=0x703248cc` / `root_rev=1`.
RECORDING used 2669 idles / 234 ms CPU, not a prefix rebuild. Native
fixtures cover the 9216/21504 fit pair and the applied-ticks overflow
discard. Host gates after inner timings: capture-serial RAM1 locals
**6944**. For
`startNextPassChangeFragment`, device recapture
[`004119`](../../captures/session_20260828_004119.log) names the 21 ms
STOPPED session-exit as `note_lookup=18293` on `pass_change=21050`
(86.9%). Every ≥10 ms fragment is 82–88% `LoopSnapshot::findNote`.
Caller-scoped pitch lookup device PASS
[`005527`](../../captures/session_20260828_005527.log): `note_lookup`
max **857 µs** (was 18293 on
[`004119`](../../captures/session_20260828_004119.log)); `pass_change`
max **3.219 ms**. Tail
[`005819`](../../captures/session_20260828_005819.log) publishes
`pending=0` / `root=0x70413fec` / `root_rev=10`. Same-pitch scan and
refill remain. No `step` rewrite is
justified yet. Host gates: capture-serial RAM1 locals **6944**; native
**1495/1495**.
Per-turn reserve measurement remains necessary, but freezing the minimum
observed reserve is rejected as a guarantee. Do not remove the +16
live-record bars. Do not check off OpenSpec `3.5`/`3.6`.

Direct consumer migration, persistence, and snapshot-identity undo remain
later phases. Decision:
[`DEC-047`](../DECISION_LOG.md#dec-047--bounded-overdub-wrap-snapshot-publication).

**Separate capture finding:** [`214703`](../../captures/session_20260826_214703.log)
confirmed `CLN,overdub,wrap_synth,3` after `finalizePendingNotes` reported no
pending notes/off events. Stored data contained existing head offs such as
`F@112` plus synthetic `F@6143` (`loopLengthTicks - 1`) for the same notes.
Track this in [`overdub_wrap_note_off_capture_bugfix.md`](../Plans/overdub_wrap_note_off_capture_bugfix.md);
do not fold it into 4b.2c.

**Immediate slice (side track):** Slice 1A measure gate **FAIL** on
[`213537`](../../captures/session_20260826_213537.log). Decomposition
[`playback_window_prepare_algorithm_decomposition_investigation.md`](../Plans/playback_window_prepare_algorithm_decomposition_investigation.md)
**D1** is **closed** — native + device PASS
[`221655`](../../captures/session_20260826_221655.log): switch track 0→1
while `chan resume=2560`, **0** `reset,dirty`, resume `chan 2568` on return,
then `Complete` at 178.738 s. Next LCR-prepare step would be **D2 design**,
but maintained-LCR prepare is not the snapshot north star; do **not** start
D2 firmware or 1B unless re-authorized. Parent remains
[`playback_window_build_owner_boundary_refinement.md`](../Plans/playback_window_build_owner_boundary_refinement.md)
([#38](https://github.com/Lytrix/MidiLooper/issues/38)).

G1 sizing is **satisfied** (doc-only). `representativeRebuildWall` = 358 µs
from [`202654`](../../captures/session_20260826_202654.log) `T4/S0`
`path=snapshot`; `prefetchTicks` = **272**; horizon stays playhead-centred
(`H_fwd` = 768). `T0/S6` compatibility is excluded. Slice 4 / Ring A firmware
is not authorized. Residual **4b.1b** (indivisible
`NoteUtils::reconstructDisplayNotes` in `Loop::rebuildVisualCacheIdleSlice`)
stays **parked**. Do not make a cooperative reconstruct state machine.
STOPPED/PLAYING post-overdub long-tail stays out of scope.

Ring B/RC2 shipped: five deferred owners rotate through one unconditional
cursor turn (snapshot step, visual-cache slice, wrap append, deferred
persistence, load/display). Visual-dependent turns skip only while wrapped
append is pending. RC2 refuses uncovered visual-cache reads on the overdub
paint path and falls back to committed-pass reconstruction when a dirty cache
cannot cover the overlap source span. Device PASS
[`183452`](../../captures/session_20260826_183452.log) / reconfirm
[`185141`](../../captures/session_20260826_185141.log): wrap `from=passes`,
`DISP` 286→466 / 271→437. First-overdub `midi_gap` 222.596→146.802 ms vs
[`175350`](../../captures/session_20260826_175350.log); later steady 24.325 ms
vs 366 ms. Reconstruct timings for 4b.1b live on the owner-boundary plan.

OpenSpec **4b.2** remains the first-take `loopLengthTicks` behavior:
snapshot idle starts from `capture.store` only after the playhead has left bar
1, then appends each later completed bar when the unpublished parent still has
tick room. Live RECORDING does not rebuild the settled prefix when the parent
is too short; the bar still being written is not included.
[`114012`](../../captures/session_20260826_114012.log) started at
`record_length=768` while bar 1 was still being written.
[`120020`](../../captures/session_20260826_120020.log) started after bar 1
and did not restart `snapshot_idle` during RECORDING; `DISP` still jumped
on every quantized `record_length` step. First-take display length stays
the continuous playhead.
[`121754`](../../captures/session_20260826_121754.log) rebuilt the whole
settled prefix on each later length step (2611 RECORDING `snapshot_idle`
steps, BPM drift, post-stop `midi_gap` 361 ms).
[`125527`](../../captures/session_20260826_125527.log) kept unpublished
snapshot in-flight through OVERDUBBING with repeated 2.6–27.4 ms
`snapshot_idle` turns and `midi_gap` spikes (35 ms, 432 ms); cadence
alternation is in tree for device verify.
[`130244`](../../captures/session_20260826_130244.log) still stacked
unpublished snapshot with long PLAYING/OVERDUBBING visual idle reconstruct
bursts (`idle_reconstruct` ~145–217 ms; `midi_gap` 415 ms OVERDUBBING,
242 ms PLAYING, 451 ms STOPPED). In tree: unpublished snapshot contention
keeps visual fallback constrained and cadence-gated (1-bar constrained
slice on every 4th eligible turn) while snapshot updates stay active.
[`131338`](../../captures/session_20260826_131338.log) still showed PLAYING
stalls (`midi_gap` 195 ms and 184 ms) with first-PLAYING stacked
`idle_maint`/snapshot/persist and later `usbdisp`/`load_frame`/`late_on`
windows. Refinement in tree: unpublished visual fallback constraint now
applies whenever unpublished snapshot work is pending (not only in-flight),
and live-record snapshot append starts now admit at most one completed bar
of new range per start to keep setup cost small.
[`131852`](../../captures/session_20260826_131852.log) confirms the parked
owners are still active: `snapshot_idle` and `PERS,work` still share turns
(`snapshot_idle us=26823` at 147.341 s then `PERS,work payload_slice=20806`,
plus repeated alternation while pending), OVERDUBBING still reaches
`midi_gap=236090` with `usbdisp=143649` / `load_frame=25638`, and STOPPED
reaches `midi_gap=430552` with `usbdisp=538063`, `load_frame=401498`,
`idle_maint=337246` while `ODUB,stop,display` remains 21.9-50.7 ms. The
same capture records `timing_max,DisplayResolveLiveCapture,215619`. In tree:
post-stop lane now blocks deferred save on turns where snapshot already ran;
device recapture is still required and 4b.1 remains open.
[`132627`](../../captures/session_20260826_132627.log) no longer shows
`PERS,work` while `snapshot_idle` is active (same-turn overlap not observed in
that run), but 4b.3/4b.4 owners remain: post-overdub PLAYING still reached
`midi_gap=175230` (`usbdisp=121603`, `load_frame=14728`, `late_on=184364`),
STOPPED reached `midi_gap=198666` (`usbdisp=319516`, `load_frame=191331`,
`late_on=30491`), and `timing_max,DisplayResolveLiveCapture` remained high
(`157754`). In tree for 4b.3: post-overdub drain now inserts MIDI polls during
`runDeferredLoadAndDisplayFrame` (after display and load-job slices) so
`load_frame` does not monopolize an entire drain turn before the next USB
dispatch. Device recapture required.
[`133517`](../../captures/session_20260826_133517.log) still showed
`snapshot_idle`/`PERS,work` interleaving while pending (e.g.
`snapshot_idle us=7360` then `PERS,work 86`), plus OVERDUBBING windows with
`midi_gap=200549` (`usbdisp=129828`, `load_frame=23600`, `late_on=26036`) and
`midi_gap=196173` (`usbdisp=68686`, `load_frame=15178`, `late_on=96834`,
`persist_save=32056`). Post-overdub PLAYING windows in that run were
26-52 ms class after stop-display lines (display 19.6-51.4 ms). In tree for
4b.1: lane arbitration now includes RECORDING alongside PLAYING/OVERDUBBING
for pending snapshot work, and persist lane toggles now use
RECORDING/PLAYING/OVERDUBBING pending snapshot detection (not transport-only).
Device recapture required.
[`134438`](../../captures/session_20260826_134438.log) showed no
`PERS,work` entries (`0` matches) while `snapshot_idle` remained active, so
same-turn overlap was not observed in that run. 4b.3 remains open:
post-overdub PLAYING still reached `midi_gap=242938`
(`usbdisp=149450`, `load_frame=53120`, `late_on=69979`) after
`ODUB,stop,display=44222`, and STOPPED still reached `midi_gap=323973` with
`loop_rem,idle_reconstruct=148417`, `loop_rem,idle_maint=180315`,
`idle_maint=319996`, and `load_frame=38737`. 4b.4 remains open:
`timing_max,DisplayResolveLiveCapture=225601`. In tree for 4b.3:
`MidiHandler::handleMidiInput` now caps per-call dispatch batch size to `32`
(from `128`) while `anyPlayingMidiDrainAfterOverdubStop()` is active, so
post-overdub drain turns cannot spend a full 128-message USB dispatch burst in
one `usbdisp` slice. `pio run -e teensy41-capture-serial` and
`pio test -e native` PASS; device recapture required.
[`135317`](../../captures/session_20260826_135317.log) kept `PERS,work=0`
while `snapshot_idle` remained active (4b.1 still open until repeated holds).
4b.3 remains open: first post-stop cycle stayed bounded at
`ODUB,stop,display=15428`, but later `midi_gap=181392` occurred in
OVERDUBBING after PLAYING re-entered overdub; later cycles were
`midi_gap=42033` and `22132`. 4b.4 remains open:
`timing_max,DisplayResolveLiveCapture=163008`. In tree for 4b.4:
`rebuildOverdubCommittedDisplayLayer` now avoids snapshot gather+reconstruct on
the overdub paint path when `visualCache` already has notes (falls through to
held-layer/window-filter branches), bounding this owner before recapture.
`pio run -e teensy41-capture-serial` and `pio test -e native` PASS
(`RAM1 free for local variables: 4608`).
[`140201`](../../captures/session_20260826_140201.log) kept `PERS,work=0`
while `snapshot_idle` remained active. 4b.3 improved but remains open:
post-stop PLAYING `midi_gap` reduced to `48361` (from `142534` in `135317`),
but OVERDUBBING still reached `midi_gap=133468` in first-bars windows. 4b.4
improved strongly in this run: `timing_max,DisplayResolveLiveCapture=7107`
(from `163008`). The same capture showed overdub reopen source drift
(`DIAG,lcr,src,why=open` notes `0`/`165`/`228` while nearby PLAYING `DISP`
totals were `240`/`253`/`308`) and a stop-path restamp miss
(`seal_publish_span_cache_restamp ... invalidated_missing_id` with
`capture hot stop verify: non-canonical storage (check=16)`). In tree:
`Loop::commitPendingCapturePass` now invalidates source-span cache on restamp
miss, and `Loop::rebuildOverdubSourceSpanCache` now seeds from a clean
`visualCache` when prepared spans miss and no prior cache seed exists.
`pio run -e teensy41-capture-serial` and `pio test -e native` PASS; device
recapture required.
[`141034`](../../captures/session_20260826_141034.log) confirms 4b.4 stays
bounded (`timing_max,DisplayResolveLiveCapture=8608`), so 4b.4 is closed.
4b.3 remains open: first-overdub windows still show repeated
`load_frame`+`usbdisp`+`late_on` lag signatures around bars 4-8, and the
wrap-reopen source drop (`src,why=open ... notes=0`) remains under active fix.
[`141838`](../../captures/session_20260826_141838.log) **reopens 4b.4** and names
two root causes — design session doc
[`playback_window_round_robin_admission_refinement.md`](../Plans/playback_window_round_robin_admission_refinement.md),
**no firmware authorized** (architecture checkpoint answers yes on ownership,
transitions, and execution bound; needs `DEC-046`).
**RC1 (bar 4–8 stalls):** one `TrackManager::updateAllTracks` call ran **344 ms**
inside clock dispatch (`midi_input`/`clk`/`tracks`/`usbdev`/`usbdisp`/`usbclk`
all `344xxx,3`; `midi_gap` only `26165`), producing `late_event,off,264136,4608`.
`PlaybackMergedMidiEventsRebuild=41` over 22 bar edges is **two** content-sized
`ensurePlaybackMergedMidiEventsBuilt` rebuilds admitted in the same interval,
because the reuse test expires exactly one bar after the last rebuild for every
enabled slot (9 enabled here). ~314 ms is unattributed: `recordPlaybackRebuild`
emits the **last** pending sample, not the max, and `cer_range`/`gather_*` are
compiled out at `SESSION_CAPTURE_INVESTIGATION_DIAG=0`.
**RC2 (overdub notes vanish at wrap):** wrap only records a pass id, no publish
happens between the record seal (52.157 s) and the stop seal (91.592 s),
`visualCache` is left dirty record-only (`VCACHE,stale,notes,270 … dcnt,18`),
`deferredDerivedViewMaintenance` excludes OVERDUBBING so the dirty bars never
drain, and the **4b.4 guard** `loop.visualCache.notes.empty()` in
`rebuildOverdubCommittedDisplayLayer` blocks the snapshot gather after
`visualCacheCoversFollowWindow` already failed — the exact condition task 4.4
forbids above. `DISP` `visualNotes` stays at `270` until
`STOPPED,…,504,504` at 106.118 s. Same stale cache also seeded overlap-resolve:
`srcafter,why=wrap,from=cache,after=240` against true content `452`.
**RC3 (separate, not bundled):** `snapshot_idle` burned ~1.0 s CPU per 2 s wall
from 16 s to 77 s (2992 turns) and published nothing.
**Review round 1 outcome:** architecture direction and the RC1/RC2/RC3
separation accepted; four changes applied to the design doc before `DEC-046`.
(1) **No bound reclassification** — Ring A caps admission multiplicity, which is
not an execution-bound class, so `ExecutionBounds.md` rows 1 and 2 both stay
`Unbounded` and no fourth class is introduced (DEC-043). (2) `prefetchTicks` is
**derived**, not chosen: `(slots-1) x clockInterval + worstCaseSingleRebuild +
margin`, binding at the **highest** supported tempo (`ClockManager` clamp
`[20,300]`). The originally proposed `TICKS_PER_BAR/4` is short at 120 BPM / 9
slots (500 vs 511 ms) and short by 2x at 300 BPM — gate **G1** blocks Slice 4
until the per-rebuild worst case is measured. (3) RC2 promoted to invariant
**R** (a derived representation may satisfy a query only when its coverage
corresponds to the requested interval and revision); the three repairs are its
enforcement points, and the `rebuildOverdubSourceSpanCache` repair is a
**semantic** gate independent of the display gate. (4) Missed-prefetch policy is
explicit (invariant **A1**): keep the existing immutable window, **never** a
synchronous rebuild on the clock path, and count
`DIAG,playback_window_starved` as an architecture failure. Slice order is now
measurement closure (0+1) → representation repair (2a/2b) → admission (3, 4).
**Review round 2 outcome:** sizing basis narrowed, rings unchanged. Ring A's
population is now the **playback-relevant** destinations whose window needs
preparation — a dynamic demand-driven set, not all enabled `(track, slot)` pairs
(invariant **A2**: no playback-relevant destination needing refresh may be
indefinitely displaced). A **design envelope** (16 tracks x 2 active slots,
≤ 240 BPM) is stated separately from absolute capacity; the 64-destination /
300 BPM calculation is **withdrawn as a blocking gate** and deferred to capacity
documentation. G1 is now representative envelope sizing, not worst-case
characterization. Investigating the reopened horizon question found
`ensurePlaybackMergedMidiEventsBuilt` centres the window on the playhead
(`winStart = playhead - winLen/2`), so the **forward horizon is already 1 bar**
(768 ticks) and 1-bar total content would halve it — the available lever is
window **bias**, not size. Within the envelope (`H_fwd`=1000 ms at 240 BPM) the
arbitration term is 73 / 156 / 323 ms at 8 / 16 / 32 eligible destinations, so
the design space is comfortable to ~16 even against the stricter
`lead <= H_fwd/2` target. Open architectural question of this phase is now
**what constitutes a playback-relevant destination** (`updateAllTracks` gates
only on `engineShouldRun(slotEnabled[i][s])` today, independent of track state),
plus horizon/bias.
**Review round 3 outcome:** four tightenings applied; sizing basis accepted.
The `(N-1) x T_clock` arbitration term is now labelled a **conservative** bound
for continuously-due destinations, not the expected steady state (self-staggering
is verified in Slice 4, not assumed), and the 32-destination row is the normal
upper envelope rather than a Ring A design target. **A1** is split into invariant
("under the design envelope, a playback-relevant destination must have its
replacement ready before its current window expires") and failure policy
(existing immutable window stays selected; no synchronous rebuild on the clock
path), so the failure branch is not unreachable by definition. Ring B's owner-3
dependency skip is given **normative non-priority semantics**: a skip consumes
the cursor position and transfers no turn. Content window **stays at 2 bars**;
only bias is open. **2a is not shippable ahead of Ring B** (Slice 3) — it lands
with native correctness tests and its device gate closes after Ring B, unless
recapture shows the restored gather path is unexercised (no
`DisplayCaptureFullGather` growth, `timing_max,DisplayResolveLiveCapture` inside
the 4b.4-closed class of 8608 us).
**Design session (round 4) — playback relevance derived; last blocking item
closed.** Finding: **no track-state check exists anywhere on the
playback-window path.** `PlaybackMidiOutput::engineShouldRun` is
`return slotEnabled;`, `Track::getEffectivePlaybackTick` returns the global
`currentTick` regardless of state, and `playMidiEvents` /
`playMidiEventsForSlot` return early only on `isStoppedRecording()` and
`!hasCommittedPasses() || loopLengthTicks == 0` — then
`playCommittedLoopMidi` calls `ensurePlaybackMergedMidiEventsBuilt` *before*
the `mergedEvents.empty()` return. So any enabled slot with committed content
rebuilds its 2-bar window once per bar forever, playing or not.
`141838` confirms it: only **one** track ever changes state (all nine
`ST,Track` lines), nine slots were enabled at boot, and
`DIAG,playback_build` window starts advance exactly one bar per ~2 s
(`0, 768, 1536, 2304, …`) from 15.409 s — record start. No spurious emission:
MO on every channel except the recorded channel 5 is 9–10, matching the nine
transition-driven all-notes-off broadcasts. Pure waste, which is why it never
surfaced as a correctness bug.
**Obligation** exists in `TRACK_PLAYING` and `TRACK_OVERDUBBING`; not in
`EMPTY`/`ARMED` (nothing started) or `STOPPED_RECORDING` (already excluded in
code). **`TRACK_STOPPED` is the leak.** Mute/solo/slot-mute do **not** remove
obligation — `PlaybackMidiOutput.h` documents that they suppress send, not the
engine, and the engine must keep cursor and ledger coherent for unmute.
Active-vs-layered selects `PlaybackEmitPolicy`, not obligation.
Predicate: `slotEnabled && trackPlaybackEngineRunning && hasCommittedPasses &&
loopLengthTicks > 0`, owned by `TrackManager` in
`TrackManagerPlaybackPolicy.cpp` as the engine-side counterpart of
`isTrackAudible` — no new module.
**Queued-destination prewarm keeps its commit-boundary exemption** (open item 3
resolved): `updateAllTracks` already refuses to commit a slot switch while
`!isPlaybackMergedMidiEventsReadyForSlot(targetSlot)` on a long loop, and
prewarm runs off the clock path via `ensurePlaybackMergedEventsForSlot`.
**Remaining open:** item **1a** — whether `TRACK_RECORDING` joins the engine-
running set for *other* enabled slots on the same track (product decision), and
item **2** — window bias (needs the LoopEnd-launch/clock-catch-up backward
margin requirement). Content window stays at 2 bars.
**`DEC-046` ACCEPTED 2026-08-26** as architecture direction — it does **not**
authorize firmware Slices 1–4. Item 1a resolved: **`TRACK_RECORDING` is included**
(layered-slot playback during recording is intentional), so `TRACK_RECORDING`
joins PLAYING and OVERDUBBING in `trackPlaybackEngineRunning`; the recorded loop
stays excluded by lack of committed passes. Only item 2 (window bias) remains
open, affecting `H_fwd`/G1 but not the admission model.
**Slice 1 firmware is in tree (user authorized this slice):**
`recordPlaybackRebuild` keeps the maximum duration and that sample's
track/slot/path, plus a rebuild count. Capture line is
`DIAG,playback_build,<path>,<us>,<winStart>,<winLen>,<rev>,<track>,<slot>,<count>`,
where `<path>` is `snapshot`, `compatibility`, or `note_edit`. The path is
captured at `Track::ensurePlaybackMergedMidiEventsBuilt`; the one-shot remains
deferred and `<count>` remains an aggregate of rebuild calls before emission.
Device recapture is required to attribute the six `cer_edit` signatures to
snapshot versus compatibility playback.
Ring B/RC2 is **closed**. RC2 device gate 4b.2b is closed by
[`183452`](../../captures/session_20260826_183452.log). Residual 4b.1b is
owned by the owner-boundary plan and parked.
**Slice 0 RAM1 blocker found and fixed (2026-08-26).**
`teensy41-capture-serial-investigation-diag` built at RAM1 **locals 512** —
a FAIL against the 4000 floor despite PlatformIO `SUCCESS`, so the board was not
flashed. ELF section diff attributed the cost to `.data` +1024 / `.bss` +3072
with ITCM unchanged (424796+1188 = 424860+1124 = 425984), and the confirmed
cause was `SESSION_CAPTURE_INVESTIGATION_DIAG` format strings **not**
flash-placed: `LoopMaterialization.cpp` and `EditApply.cpp` already had
`LOOP_DIAG_FMT`/`EDIT_DIAG_FMT` = `PSTR(...)`, but `LoopVisualCache.cpp`
(4 literals) and `LoopPendingNoteChange.cpp` (6 literals) had none, so their
`#CAP` literals landed in DTCM — contrary to
`Instrumentation-Session-Capture-Gating`. Fix adds
`LOOP_VCACHE_DIAG_FMT` / `LOOP_NOTECHANGE_DIAG_FMT` on the same pattern.
Result: investigation-diag RAM1 variables 97792 → **93696**, locals 512 →
**4608** — identical to `teensy41-capture-serial`, so the investigation build now
costs **no** DTCM over the default capture build. Flash absorbed the strings
(`data` 78692 → 79796). Both envs PASS; `pio test -e native` **1475/1475**.
Checkpoint: ownership no, transitions no, unbounded interval work no.
**Slice 0 DONE — attribution closed.**
[`153941`](../../captures/session_20260826_153941.log) on the investigation-diag
env reproduced the stall larger (`tracks=393818`, `midi_input=393845`, same
chain) and attributed it end to end. Of one `updateAllTracks` at **393818 us**:
`cer_range` **367412** (93%) → `edit` (`applyNoteEditPassSequence`) **343625**
(94% of that) → `ulen` **341642** → **`recon` 339073**. The windowed CER
traversal is only `append=23511`; `collect`+`sort` are 276 us. So **92% of the
rebuild is note reconstruction inside the edit projection.**
`cer_rows` is constant in all 50 non-empty samples: `active=217, comp=217`
(every Active row an overdub companion), `del=185`, `ulen=32` — 217 rows
replayed to emit **164** events in a 2-bar window. F11 proven: edit-projection
cost scales with EditPass count, not window size. `recon` is 98–99% of `ulen`
in every sample; `nlen` drives it (18→339 ms, 16→287 ms, 12→170 ms) but the
same `nlen=18` also measured 68989 us, so per-row cost is content-dependent
(~19 ms/row worst).
**Compatibility path, not snapshot path:** `cer_range` starts at 52.014 s,
before this session's long record at 61.168 s, and every publish belongs to the
one transitioning track — so the loop carrying 18 chunk lists and 217 companion
rows is a **loaded slot with no published root**, reaching
`copyEffectiveCommittedEventsInRange` via `gatherCommittedEventsInWindow`.
**New root cause RC4 (separate, not bundled):** the length-update path re-runs
note reconstruction per row on the compatibility gather. RC1 is admission
multiplicity; RC4 is unit cost. Against the envelope (`H_fwd_wall`=1000 ms at
240 BPM) a 367 ms rebuild clears `L < H_fwd` but **violates the `L <= H_fwd/2`
target from 16 eligible destinations upward** — admission scheduling alone only
just fits, with no margin. Candidate resolution is already in the architecture
direction (DEC-044/045): a published snapshot carries edits applied, so the
compatibility gather and this whole cost do not exist for a slot with a
published root. Whether loaded slots publish on restore is the open design
question.
[`160244`](../../captures/session_20260826_160244.log) **proves RC4 unit cost is
environment-dependent, not input-dependent.** Six recurring `cer_edit` input
signatures repeat in three rounds; five with byte-identical
`ndel/nlen/lgth/shrt/ev` cost **7.3x-9.7x** more in round 2 (long overdub,
`snapshot_idle` 48.4% of window) than in rounds 1 and 3 (28.9% and 0%).
`cer_range` splits into an uncontended band **29.5-98.9 ms** and a contended band
**306-363 ms**, so **367 ms is pathological, not representative** — `G1` must
consume the uncontended figure. Mechanism is **not** proven: wall-clock inflation
from interleaving (48.4% load alone gives ~2x, not 9x) versus
`InternalHeapFirstAllocator` spill to PSRAM (`min_ever` fell 340->120 KB, which is
pressure but not exhaustion). Discriminate before optimizing.
**Weighting between RC1 and RC4 revised:** `tracks` max 391.1 ms against a single
362.9 ms rebuild leaves no room for multiplicity as the main term, and observed
multiplicity is **pairs** (median `cer_range` spacing 1998 ms; only 8 gaps under
50 ms, all 11.9-16.8 ms). Ring A's benefit here is bounded by ~2x while RC4 is
~9x. Ring A stays architecturally correct but must not be presented as the fix
for the 391 ms.
**Slice 1 destination attribution closed by [`161834`](../../captures/session_20260826_161834.log)
and [`162522`](../../captures/session_20260826_162522.log).** The 94
`playback_build` maxima in `161834` identify four destinations: `T0/S6` (57
lines, max 166.325 ms), `T4/S0` (34, max 25.439 ms), `T6/S0` (2, max 10.965
ms), and `T2/S2` (1, max 9.406 ms). `162522` directly pairs all 15 repeated
`cer_edit` compatibility gathers with `T0/S6`; its maximum is 360.754 ms.
`T0/S6` is the dominant long-loop stream.
The telemetry `<count>` is rebuild calls before the next one-shot emit, not
distinct destinations per bar edge; no eligible-set count is inferred from it.
The small `lists=1/2, rows=0` gathers are separate edit-free destinations.
**RC4 mechanism remains open after `162522`:** `snapshot_idle` lines occur
after the inflated gather, so snapshot construction is not concurrent inside
the `g_reconstructUs` span. `165024` now rules out the measured
`InternalHeapFirstAllocator` spill route and nested clock dispatch: all 16
`cer_heap` rows report `spill=0`, `ext=0`, `clk=0`, and `iclk=0`. Higher-
priority interrupt elapsed time and `ExternalMemoryFirstAllocator`
service/cache effects remain unmeasured. `cer_recon` now splits
internal-heap `filt` from PSRAM-first `span`/`proj` and records
IntervalTimer ISR `itickus`.
[`171159`](../../captures/session_20260826_171159.log) closes that split:
`filt` is unchanged on the 7.9x `nlen=18` pair (`36` / `36`); `span` and
`proj` take the inflation; `itickus=0` on all 11 rows. Question 3 is closed
from code: `adoptPersistedSnapshot` / `restorePassesSnapshot` do not publish.
The new source-tagged telemetry is device-validated by
[`202654`](../../captures/session_20260826_202654.log): 82 emitted
`playback_build` samples classify as 74 `compatibility` and 8 `snapshot`
(no `note_edit`). The long-loop `T0/S6` stream remains compatibility, reaching
325.541 ms, while snapshot-backed `T4/S0` samples remain below 1 ms
(maximum 358 us). This confirms the loaded-slot compatibility path and the
published-root snapshot path coexist in one later capture.
RC4 question 5 is closed as `T0/S6` compatibility-only: the eight `lgth`
rows belong to that loaded slot's companion replay, not to a published
snapshot. Ignore further `T0/S6` lengthen-branch forensics.
**G1 satisfied:** `representativeRebuildWall` = 358 µs (`T4/S0` snapshot
maximum in `202654`); `prefetchTicks` = 272; `H_fwd` = 768. The same
capture's non-`T0/S6` compatibility ceiling is 11.859 ms and still fits
that lead. Ring A firmware remains unauthorized.
[`205447`](../../captures/session_20260826_205447.log) confirms leaving
`T0/S6` for `T0/S5`: S5 rebuild is 334 µs and S6 drops out of
`playback_build`. The same timestamp is a 377 ms `idle_maint` / `midi_gap`
stall after `VCACHE,stale_all`, not an S5 rebuild cost. RC4 conclusions
are in
[`playback_window_edit_projection_unit_cost_investigation.md`](../Plans/playback_window_edit_projection_unit_cost_investigation.md).
**RC4 sequencing decided (user, 2026-08-26):** investigation is **authorized now**
in [`playback_window_edit_projection_unit_cost_investigation.md`](../Plans/playback_window_edit_projection_unit_cost_investigation.md);
the **firmware fix is a separate change** and must not enter the Ring A slice.
Owner is proven: `applyChangeLengthById` reconstructs **only on the lengthen
branch** via `reconstructNotesForPitch`, which per row reallocates, rescans every
event, and re-runs the full canonical-span pipeline — `lgth=8` rows carry all
339 ms (~42 ms each) while `shrt=10` shorten rows contribute nothing.
Order: Slice 1 closure → RC4 investigation → measure RC4 resolution → **size G1
from the post-resolution cost** → Ring B/RC2 independently → Ring A last. Do
**not** lock the prefetch lead at 367 ms: that is ~35 clock dispatch periods at
240 BPM, and two samples with identical row counts differ 5x (339 vs 69 ms), so
representativeness is unestablished.
RC2 device gate 4b.2b is closed by
[`183452`](../../captures/session_20260826_183452.log). Do **not** check off
4b.3 until device verify.
Do not close **4b.1**. Do **not** split `atomicRenameTempFile`. 4b.0 wrap/hold
remains device PASS [`092755`](../../captures/session_20260826_092755.log).

Phase 3 task **4.4 display** remains in tree (not closed). Paint and visual-cache fill read
published `LoopSnapshot` events through
`tryGatherPublishedSnapshotEventsInWindow` (plus live capture when needed),
then `reconstructDisplayNotes`. A nonempty dirty `visualCache` prefix does
not authorize the paint window — `visualCacheCoversFollowWindow` must pass
or the window gathers from the snapshot after capture ends
([`235954`](../../captures/session_20260825_235954.log)). Live overdub
keeps the PLAYING committed handoff only while the paint window stays
inside the last gather ([`001048`](../../captures/session_20260826_001048.log)
`frame=0`; [`001907`](../../captures/session_20260826_001907.log) held the
first 16-bar slice on a 21-bar loop). Follow exit or wrap invalidate
snapshot-gathers once. A remainder of at most two gather margins is
included in the first gather so a 20-bar loop does not omit the last
2 bars and then gather on the MIDI path
([`002634`](../../captures/session_20260826_002634.log): window at 3072
at 117.1 s, last-bar notes at 119.2 s, `DisplayResolve` 81–196 ms).
After overdub stop, `ODUB,stop,display` holds the composed frame and does
not snapshot-gather. Device PASS [`004934`](../../captures/session_20260826_004934.log):
display **8.8 ms** / **12.2 ms** (was 97–154 ms on
[`003459`](../../captures/session_20260826_003459.log)). PLAYING after first
publish is **7.4–8.4 ms** `midi_gap`. Ring B/RC2 host implementation is now
in tree; RC2 device gate 4b.2b is closed by
[`183452`](../../captures/session_20260826_183452.log). 4b.3 post-overdub
load-frame drain polls remain separately device-verification work.
Compatibility gather remains
when no published root exists. Do not CER-gather on the overdub paint
path. Task **4.3** stays skipped. NOTE_EDIT, overdub analysis,
validation, 4.5, 4.6, and Phase 5 are **not selected**.

After the first published `LoopSnapshot` exists, PLAYING and OVERDUBBING idle
do not `step` successor construction. PLAYING visual stays off unless the
cache is empty. First-record starts during RECORDING after the playhead has
left bar 1 and continues on PLAYING (`DEC-045` 4b.2). Device PASS
[`233308`](../../captures/session_20260825_233308.log): four overdubs after
record; first publish at 97.50 s; **no** `snapshot_idle` from then until
STOPPED; PLAY4 BPM 119.966–120.049.

Phase 3 tasks 4.1–4.2 remain complete. Playback preparation reads the
published `LoopSnapshot` through `ensurePlaybackMergedMidiEventsBuilt`,
records snapshot identity for freshness, and falls back to compatibility
gather when no snapshot is selected. Cursor, order, ledger, and horizon stay
on `Track` / `LoopPlaybackRuntime`. `playbackEventPhase` stays the hot-path
scalar.

**Authorized exception:** queued-destination visual-cache invalidation — port of the
repair authorized on `feature/overdub-stop-lag-stage-c`. Owner:
`DisplayManager::invalidateForSlotChange`. Plan:
[`playback_preparation_queued_destination_visual_cache_invalidation_bugfix.md`](../Plans/playback_preparation_queued_destination_visual_cache_invalidation_bugfix.md).
Firmware in tree; host gates next. Does not change snapshot ownership or LCR.

**Branch:** `feature/immutable-loop-snapshot-passes`, forked from
`546169f66658b87c54e6166b79eba74628aa3d1e`.

**OpenSpec:** [`immutable-loop-snapshot-passes`](../../openspec/changes/immutable-loop-snapshot-passes/)
**Plan:** [`loop_pass_snapshot_effective_state_refinement.md`](../Plans/loop_pass_snapshot_effective_state_refinement.md)
**Active wrap slice:** [`overdub_wrap_source_snapshot_migration_refinement.md`](../Plans/overdub_wrap_source_snapshot_migration_refinement.md) — OpenSpec tasks `3.5`/`3.6` are closed; parent **4.4** closed (4.4a–4.4h + inventory).
**Applied Phase 0 spec patch:** [`loop_snapshot_pitch_bar_span_index_refinement.md`](../Plans/loop_snapshot_pitch_bar_span_index_refinement.md) — logical structure selected. Phase 1 native/offline model is complete. Phase 2 publication adapters, Phase 3 playback snapshot reads (4.1–4.2), stateless snapshot queries (4.3), and display/NOTE_EDIT/analysis/validation current-root migration (4.4) are complete. Next: **4.5** verify or **4.6** snapshot-identity freshness.

Phase 0 documentation now includes the project-scoped
[`MIDI_PROTOCOL_GUIDE.md`](../Guides/MIDI_PROTOCOL_GUIDE.md), based on
M2-104-UM v1.1.2, with a read-only audit of the current MIDI 1.0 logic. The
2026-08-25 reference-root refinement separates `LoopPass` from `LoopSnapshot`,
requires a persistent reference root, defines reachability and provisional
versus global snapshots, and loads only the latest saved snapshot after reboot.
The later 2026-08-25 pitch/bar amendment selects the logical indexes. The Phase
1 native measurement selects the sparse fixed-capacity paged directory
experiment; page capacity and memory characteristics remain measured
parameters. Formal reassessment and approval are recorded in
[`ARCHITECTURE-REASSESSMENT.md`](../../openspec/changes/immutable-loop-snapshot-passes/ARCHITECTURE-REASSESSMENT.md).
Later-phase gates are recorded in
[`PREFLIGHT.md`](../../openspec/changes/immutable-loop-snapshot-passes/PREFLIGHT.md)
and
[`DECISION_REVIEW.md`](../../openspec/changes/immutable-loop-snapshot-passes/DECISION_REVIEW.md).
The Phase 1 model is native/offline only; no production runtime behavior or
consumer selection changed.

The maintained `LoopContentResolution` lifecycle and its playback-readiness
continuation are not pursued on this branch. The existing `LoopContentResolution`
prototype remains historical compatibility context only; the proposed snapshot
model must not create a replacement maintained effective-state owner.

Source-level replace-on-retrigger pairing is deferred. This branch retains the
current `Track::recordMidiEvents()` / `pendingNotes` behavior and existing LIFO
pairing. The deferred future bugfix may add an internal synthetic NoteOff before
replacing an open same-channel/same-pitch capture entry; it is outside the
immutable snapshot Phase 0 scope.

## Historical base work

The sections below record work present at the fork point. They are not the active
execution queue for this branch and require explicit reauthorization before use.

### Slice 0 — docs + scaffolding delete

**Owner:** docs + `Loop` companion-projection members (`companionProjectionEvents_`, `rebuildCompanionProjectionForCurrentRevision`, `tryCopyCompanionProjectedWindow`, `clearCompanionProjectionCache`)

**Invariant:** Dead paired-projection scaffolding is gone. Interval gather still uses windowed `applyNoteEditPassSequence` (predictable-but-slow baseline). One derived-authority direction is documented; no second consumer cache.

**Status:** Shipped — native **1429/1429**. Dead scaffolding removed; gather still windowed `applyNoteEditPassSequence`.

### Slice 1A — cooperative prepare (next)

**Owner:** existing LCR device-gate prepare path only.

**Invariant:** Same prepare semantics; persistent progress; bounded work units; yield/resume without repeating completed work. Instrument `prepare,slice,*`. If F1–F7 cannot be subdivided without algorithm decomposition, **stop and report** (failure is valid).

**Do not in 1A:** optimize prepare, redesign algorithm, add cache, wire playback consume, enforce 60% × T_clock timer.

**Timing gate:** At 170 BPM, one MIDI clock interval is **14.71 ms**. The
**8.82 ms** value (60% of that interval) is only an experimental operating
target; no guaranteed maintenance reservation has been established. Measure the
proposed eight-candidate unit separately, including its indivisible path-copy,
index, replacement/tombstone, and allocation work. Treat p95 above 8.82 ms as
outside the initial target and any unit above 25 ms as an investigation failure;
these are not hard MIDI guarantees.

### Paired companion consume reverted (closed context)

**Evidence:** [`163135`](../../captures/session_20260822_163135.log) — `paired_proj` **0** / `win_apply` **33**; multi-second `idle_gather`; USB reconnect. Decision: `PairedCompanionState` reverted; tip cut at Stage C. Overlap never used paired projection (`ActiveNoteLedger` / `overdubSourceViewNotes_`).

### STOPPED undo / track-switch first-paint

**Evidence:** [`183229`](../../captures/session_20260821_183229.log) device PASS. Prior fail [`161804`](../../captures/session_20260821_161804.log) / [`165107`](../../captures/session_20260821_165107.log) undo `idle_maint` 850 ms. Plan: [`runtime_scheduling_owner_boundary_inventory_investigation.md`](../Plans/runtime_scheduling_owner_boundary_inventory_investigation.md) §34 / §36 Stage C.

**Owner:** `DisplayManager::invalidateForSlotChange` / `Track::processDeferredIdleMaintenance` → `Loop::rebuildVisualCacheIdleSlice`

**Invariant:** STOPPED undo and track switch keep `visualCache.notes` and fill through budgeted idle slices with wrap-append deferred. No wipe. No unbounded gather-plus-append in one STOPPED turn. No companion full replay on STOPPED.

**Status:** Device PASS [`183229`](../../captures/session_20260821_183229.log) — undo `stale_all` notes=2988, first `DISP` visual=2988, first post-undo `idle_maint` 224 ms; 68-bar switch notes=3040 / visual=3040. Native **1429/1429**. Closed.

### Overdub companion admission and source-span prewarm

**Evidence:** [`162928`](../../captures/session_20260821_162928.log) empty open + 136.77 s freeze. Device PASS [`165107`](../../captures/session_20260821_165107.log): 5/5 opens `notes` 1319–1394; 34/34 consume `idsel>0`; max `tracks` **190 ms**. Residual `norow` 1–4 with working select is parked.

**Owner:** `Track::processDeferredIdleMaintenance` → `Loop::prewarmOverdubSourceSpanCache` (Stage A empty-projection gather path removed with dead companion-projection scaffolding)

**Invariant:** Clock-path gather must not run full companion replay. Empty companion projection is not a successful gather. First overdub open hydrates a non-empty source view from a cache filled after boot first-paint, not from a same-turn `slice_clean` prewarm.

**Status:** Stage A+B device PASS [`165107`](../../captures/session_20260821_165107.log). §36 closed.

### Boot title `boot_commit` display stall

**Evidence:** [`161804`](../../captures/session_20260821_161804.log) device PASS. Prior gather hangs: [`161148`](../../captures/session_20260821_161148.log) (`boot_commit=70438665`, first `DISP` visual=0), [`154353`](../../captures/session_20260821_154353.log) (`boot_commit=75457996`, visual=0).

**Owner:** `runDeferredIdleMaintenance` / `runDeferredLoadAndDisplayFrame` / `TrackManager::onBootSlotLoadComplete`

**Invariant:** First piano roll filters the focus visual cache. `onBootSlotLoadComplete` must not clear `visualCache.notes`.

**Stage change shipped:** Boot idle stays on the focus track; `boot_commit` waits until that cache is clean; boot complete no longer calls `invalidateForSlotChange`. Guide: [`BOOT_LOAD.md`](../Guides/BOOT_LOAD.md).

**Status:** Device PASS [`161804`](../../captures/session_20260821_161804.log) — focus `slice_clean` 2638 notes at 10.9 s; USB at 11.484 s; first `DISP` visual=**2638** / 1224 window notes; `boot_commit=288557` (289 ms). `DFRAME` continues at ~15 ms.

### Boot title startup stall hardening

**Evidence:** [`161410`](../../captures/session_20260820_161410.log)

**Owner:** `Looper::setup`

**Invariant:** Startup does not stall at the title screen when SD bootstrap is transiently unavailable; boot continues without requiring repeated manual reboots.

**Stage change shipped:** `Looper::setup` now initializes SD with bounded retries (`3` attempts, `50 ms` delay). If SD remains unavailable, boot takes an explicit empty-runtime fallback path and skips `StorageManager::loadState` instead of blocking startup.

**Status:** Native verification pending.

### Overdub lag plan — baseline reset and reprioritized execution

**Evidence:** [`210319`](../../captures/session_20260820_210319.log) (new baseline), [`231845`](../../captures/session_20260820_231845.log), [`232951`](../../captures/session_20260820_232951.log), plus prior attribution captures [`165534`](../../captures/session_20260820_165534.log), [`165956`](../../captures/session_20260820_165956.log), [`170724`](../../captures/session_20260820_170724.log), [`174941`](../../captures/session_20260820_174941.log)

**Owner set (current priority order):**
1. `Track::stopOverdubbing` stop-path handoff (`ODUB,stop,display` envelope)
2. `Loop::sealPendingNoteChangesToEditPasses` + `TrackUndo::pushOverdubSessionOnStop` (dominant `seal_fx_*` contributors)
3. Post-stop scheduler debt window (`processDeferredIdleMaintenance`, `runDeferredLoadAndDisplayFrame`, `StorageManager::processDeferredSaveState`)

**Invariant:** Prioritize only runtime-blocking windows: stop display handoff and first post-stop scheduler stall. Stop commit semantics remain unchanged; stop return must not wait on newly discovered deferred debt.

**Runtime-stall baseline (from [`210319`](../../captures/session_20260820_210319.log)):**
- **Published stop cycles with complete first post-stop window:** 6
- **`ODUB,stop,display` elapsed:** **56.451-71.803 ms** (median **65.288 ms**)
- **First post-stop `DIAG,midi_gap`:** **78.634-145.373 ms** (median **107.569 ms**)
- First post-stop decomposition:
  - `idle_maint`: **78.619-145.357 ms** (median **101.576 ms**) — dominant
  - `load_frame`: **27.553-42.889 ms** (median **33.548 ms**)
  - `persist_save`: **13.295-122.704 ms** (median **47.168 ms**)
- Published stop `seal` duration (same 6 complete cycles): **45.461-61.129 ms** (median **56.006 ms**)
- `seal_fx_*` maxima in the same run:
  - `seal_fx_seal_pending_changes`: **41.310 ms** (dominant synchronous owner)
  - `seal_fx_push_overdub_undo`: **13.243 ms** (secondary synchronous spike)
  - `seal_fx_mark_display_ranges`: **2.828 ms**; remaining `seal_fx_*` are minor
- Additional published cycle without a complete first post-stop window in this capture: `display=39.965 ms`, `seal=29.498 ms` (tracked separately from the 6-cycle target sample).
- Two `seal=skipped` cycles still show large first gaps (**672.708-711.300 ms**) with small display (~11 ms), proving deferred scheduler debt can dominate independently of stop display.

**Reprioritized execution (based on measured stalls):**
1. **P1 — Stop display envelope trim (synchronous):** keep attribution on `seal_fx_seal_pending_changes` and `seal_fx_push_overdub_undo`; reduce and bound the `ODUB,stop,display` envelope before any broad scheduler tuning.
2. **P2 — Post-stop debt causality proof (deferred):** classify first-gap contributors as **pre-existing debt**, **stop-created debt**, **stop-amplified debt**, or **newly scheduled after stop**.
3. **P3 — Scheduler policy only after causality:** if P2 finds architectural stop-triggered invalidation/rebuild, fix that owner cause first; do not mask it with slice caps.
4. **P4 — Background arbitration:** prevent large `idle_maint` + `persist_save` stacking from consuming the same post-stop runtime window once causality is closed.

**Runtime scheduling roadmap linkage:** [`runtime_scheduling_owner_boundary_admission_refinement.md`](../Plans/runtime_scheduling_owner_boundary_admission_refinement.md)
- P1 remains pre-contract synchronous owner attribution on `Track::stopOverdubbing` / seal owners; it does not start interval reservation.
- P2 contributes Stage R owner-boundary evidence for post-stop debt classification (`pre-existing`, `stop-created`, `stop-amplified`, `newly scheduled after stop`).
- P3 enforces Owner-Boundary Gate ordering: if stop-triggered unnecessary invalidation/rebuild is found, fix that owner cause before slice caps or arbitration tuning.
- P4 is scheduling policy input only after owner causality is closed; interval reservation remains unauthorized until roadmap Stage C/A gates are explicitly satisfied.

**Runtime targets (for this overdub-lag slice):**
1. **Stop handoff target:** `ODUB,stop,display` is measured as a bounded owner envelope with dominant contributors attributed (`seal_fx_seal_pending_changes`, `seal_fx_push_overdub_undo`) and trimmed without changing stop commit semantics.
2. **First post-stop opportunity target:** first post-stop `DIAG,midi_gap` is reduced by owner-cause closure, not by masking unknown debt; each major contribution is classified as `pre-existing debt`, `stop-created debt`, `stop-amplified debt`, or `newly scheduled after stop`.
3. **Stop/deferred boundary target:** stop return does not wait on completion of deferred maintenance debt discovered in P2.
4. **Arbitration target:** long `idle_maint`, `load_frame`, and `persist_save` work does not stack into the same first post-stop runtime window after P4 policy work.
5. **Scheduling-contract target carryover:** no multi-second PLAYING/transport-active `DIAG,midi_gap` windows; interval reservation remains out of scope for this slice.
6. **Musical correctness gate carryover (when lateness hooks are present in the same evidence run):** `late_event_count == 0` and `max_lateness_us <= 0` for note-on, note-off, and clock.

**Target-aligned baseline checkpoints (same [`210319`](../../captures/session_20260820_210319.log) evidence):**
1. **Stop handoff target baseline:** complete published stop-display envelope is **56.451-71.803 ms** (median **65.288 ms**); dominant synchronous contributors remain `seal_fx_seal_pending_changes` (max **41.310 ms**) and `seal_fx_push_overdub_undo` (max **13.243 ms**).
2. **First post-stop opportunity target baseline:** first post-stop `DIAG,midi_gap` on complete published cycles is **78.634-145.373 ms** (median **107.569 ms**) with decomposition windows above.
3. **Stop/deferred boundary target baseline:** two `seal=skipped` cycles still show small stop-display windows (~**11 ms**) with large first post-stop gaps (**672.708-711.300 ms**), confirming deferred debt can dominate without large synchronous display cost.
4. **Arbitration target baseline:** in the 6 complete published windows, `load_frame` remains **27.553-42.889 ms** every cycle and `persist_save` reaches **47+ ms** in 4/6 windows (max **122.704 ms**), so stacked post-stop work remains an active scheduling risk.
5. **Scheduling-contract carryover baseline:** run-wide `DIAG,midi_gap` in this capture reaches **2.855 s** (3 windows over 1 s), so the no-multi-second-gap carryover target is not yet met globally.
6. **Musical correctness gate carryover baseline:** lateness hooks are present; this run reports non-zero late note windows (`late_on`: 5 windows, 23 events total, max lateness **42.458 ms**; `late_off`: 4 windows, 13 events total, max lateness **29.551 ms**; `late_clk`: 0 events).

**P1 re-measure after sorted transform lookup ([`213848`](../../captures/session_20260820_213848.log)):**
- Sample shape: **9** stop cycles, **7** published cycles with complete first post-stop windows, **0** skipped-complete cycles.
- **`ODUB,stop,display` (published + complete):** **26.751-134.121 ms** (median **58.246 ms**) — median improved vs baseline, but one long-tail outlier remains.
- **Published `seal` (same 7 complete cycles):** **18.250-125.144 ms** (median **48.153 ms**).
- **First post-stop `DIAG,midi_gap` (published + complete):** **43.875-189.583 ms** (median **104.331 ms**).
- First post-stop decomposition (published + complete):
  - `idle_maint`: **32.233-189.565 ms** (median **104.313 ms**)
  - `load_frame`: **27.147-36.773 ms** (median **31.289 ms**)
  - `persist_save`: **13.960-55.248 ms** (median **45.047 ms**)
- `seal_fx_*` maxima in the same run:
  - `seal_fx_seal_pending_changes`: **48.354 ms** (higher max than baseline)
  - `seal_fx_push_overdub_undo`: **6.650 ms** (lower max than baseline)
  - `seal_fx_mark_display_ranges`: **3.818 ms**
- Dominant outlier cycle: `ODUB,stop,display=134.121 ms`, `seal=125.144 ms`, `seal_fx_seal_pending_changes=48.354 ms`, `seal_fx_mark_display_ranges=3.818 ms` (same stop cycle).
- Carryover checks in the same run: run-wide `DIAG,midi_gap` max **2.890 s** (5 windows over 1 s); lateness remains non-zero (`late_on` max **64.220 ms**, 46 events; `late_off` max **87.453 ms**, 30 events; `late_clk` zero).

**P1 re-measure after publish-owner telemetry ([`215223`](../../captures/session_20260820_215223.log)):**
- Sample shape: **8** stop cycles, **7** published cycles with complete first post-stop windows, **0** skipped-complete cycles.
- **`ODUB,stop,display` (published + complete):** **32.702-124.432 ms** (median **50.174 ms**).
- **Published `seal` (same 7 complete cycles):** **23.529-115.351 ms** (median **42.042 ms**).
- **First post-stop `DIAG,midi_gap` (published + complete):** **50.388-160.096 ms** (median **102.466 ms**).
- First post-stop decomposition (published + complete):
  - `idle_maint`: **38.602-160.081 ms** (median **102.448 ms**)
  - `load_frame`: **27.039-36.331 ms** (median **30.616 ms**)
  - `persist_save`: **16.108-85.121 ms** (median **46.960 ms**)
- New publish-owner telemetry maxima in the same run:
  - `seal_fx_publish_pending_pass`: **62.448 ms**
  - `seal_publish_span_cache_restamp`: **62.323 ms**
  - `seal_publish_promote_pass`: **9 us**
  - `seal_publish_finalize_state`: **10 us**
  - `seal_publish_clear_capture`: **13 us**
- Existing `seal_fx_*` maxima in the same run:
  - `seal_fx_seal_pending_changes`: **44.809 ms**
  - `seal_fx_push_overdub_undo`: **69 us**
  - `seal_fx_mark_display_ranges`: **3.991 ms**
- Dominant outlier cycle attribution: `ODUB,stop,display=124.432 ms` with `seal=115.351 ms`, split primarily across `seal_publish_span_cache_restamp=62.323 ms` and `seal_fx_seal_pending_changes=44.809 ms`; publish owner is now directly measured.
- Carryover checks in the same run: run-wide `DIAG,midi_gap` max **1.907 s** (1 window over 1 s, 1 over 500 ms); lateness remains non-zero (`late_on` max **53.490 ms**, 36 events; `late_off` max **48.415 ms**, 27 events; `late_clk` zero).

**P2 re-measure after global post-overdub drain-window clamp ([`230507`](../../captures/session_20260820_230507.log)):**
- Sample shape: **10** stop cycles, **10** published cycles, **10** handoff rows, **6** unique first post-stop windows.
- **`ODUB,stop,display` (published):** **34.471-58.055 ms** (median **49.813 ms**).
- **Published `seal`:** **24.092-47.049 ms** (median **39.534 ms**).
- **First post-stop `DIAG,midi_gap` (unique windows):** **87.209-168.113 ms** (median **108.276 ms**).
- First post-stop decomposition (same unique windows):
  - `idle_maint`: **87.178-168.095 ms** (median **108.244 ms**)
  - `load_frame`: **29.352-34.071 ms** (median **32.430 ms**)
  - `persist_save`: **0.001 ms** (flat)
- Debt classification in the same run:
  - `ODUB,stop,debt_handoff`: **10/10 stop_created_debt**
  - `DIAG,odub_debt visual`: **10/10 stop_created_debt**
  - `DIAG,odub_debt save`: **9/10 pre_existing_debt**, **1/10 stop_created_debt**
  - Clamp contract check: `idle_slice=1` and `idle_bars=1` in **10/10** rows
- Delta vs pre-clamp reference [`225521`](../../captures/session_20260820_225521.log):
  - First post-stop `midi_gap` median **160.499 -> 108.276 ms**; max **506.253 -> 168.113 ms**
  - First post-stop `idle_maint` median **147.522 -> 108.244 ms**; max **498.002 -> 168.095 ms**
- Remaining synchronous stop owners in this run:
  - `seal_publish_span_cache_restamp`: **2.357-20.746 ms** (median **15.792 ms**)
  - `seal_fx_seal_pending_changes`: **2.709-23.835 ms** (median **17.319 ms**)

**P3/P4 re-measure after stop-owner trim ([`231845`](../../captures/session_20260820_231845.log), [`232951`](../../captures/session_20260820_232951.log), [`233633`](../../captures/session_20260820_233633.log), [`235214`](../../captures/session_20260820_235214.log)):**
- Sample shape in both captures: **12** stops = **9 published** + **3 skipped**.
- Published synchronous stop owners stay bounded:
  - [`231845`](../../captures/session_20260820_231845.log): `ODUB,stop,display` median **21.993 ms** (max **37.466 ms**), `seal_publish_span_cache_restamp` median **2.988 ms** (max **5.391 ms**), `seal_fx_seal_pending_changes` median **1.226 ms** (max **1.524 ms**).
  - [`232951`](../../captures/session_20260820_232951.log): `ODUB,stop,display` median **24.824 ms** (max **36.010 ms**), `seal_publish_span_cache_restamp` median **3.898 ms** (max **5.343 ms**), `seal_fx_seal_pending_changes` median **1.090 ms** (max **1.601 ms**).
- Remaining long-tail is deferred-window, not synchronous stop owner:
  - [`232951`](../../captures/session_20260820_232951.log) skipped stops: `ODUB,stop,display` **9.829-9.991 ms**, but first post-stop `DIAG,midi_gap` **595.365-603.237 ms**.
  - First post-stop unique windows in [`232951`](../../captures/session_20260820_232951.log): `midi_gap` median **134.773 ms**, max **603.237 ms**; `idle_maint` median **134.753 ms**, max **603.218 ms**; `persist_save` median **17.333 ms**, max **55.075 ms**.
- Debt causality remains classified and clamped (`ODUB,stop,debt_handoff`: **10 stop_created_debt**, **2 pre_existing_debt**; `idle_slice=1`, `idle_bars=1` in **12/12** rows).
- [`233633`](../../captures/session_20260820_233633.log) confirms the remaining long-tail owner is `idle_append` during post-stop PLAYING debt windows after `VCACHE,stale_all` (undo path), not synchronous stop publish owners:
  - `DIAG,midi_gap=850.101 ms` aligns with `DIAG,loop_rem,idle_append=797.766 ms` + `DIAG,loop_rem,idle_maint=844.487 ms`.
  - `DIAG,midi_gap=817.292 ms` aligns with `DIAG,loop_rem,idle_append=769.842 ms` + `DIAG,loop_rem,idle_maint=817.272 ms`.
- [`235214`](../../captures/session_20260820_235214.log) validates the Stage D arbitration + append guard on overdub windows:
  - Sample shape: **6** stops, **6 published**, **0 skipped**, `RING,overflow=0`.
  - First post-stop unique windows: `DIAG,midi_gap` **32.692-152.781 ms** (median **101.382 ms**), `idle_maint` **32.691-152.759 ms** (median **101.358 ms**), `persist_save` **0.001-0.188 ms** (median **0.001 ms**).
  - Compared with [`233633`](../../captures/session_20260820_233633.log): skipped windows removed (**2 -> 0**) and first post-stop gap max reduced (**850.101 -> 152.781 ms**).
- Remaining run-wide multi-second outliers in [`235214`](../../captures/session_20260820_235214.log) are not post-stop overdub windows: `DIAG,midi_gap` **1908-1910 ms** aligns with `loop_rem,boot_commit` **1632 ms** + `loop_rem,load_frame` **1642 ms** while state is `STOPPED`; keep this as separate non-overdub scheduling follow-up.
- **Stage D implementation scope (shipped):** defer `StorageManager::processDeferredSaveState` admission while post-overdub drain is active and deferred save work is pending, and skip wrap-pair append in that same stressed drain context, so `idle_maint` + `persist_save` + `idle_append` do not stack in one post-stop drain turn.

**Stage changes shipped:**  
- `applyPendingHideAndShortenToNotes` now applies `Shorten`/`Hide` transforms by rebuilding the note vector once, avoiding repeated in-place erase scans for each pending change.  
- `saveNoteEditPass` gained `deferDerivedInvalidate` for batch callsites.  
- `sealPendingNoteChangesToEditPasses` now defers derived invalidation during companion row inserts and performs a single `playbackRevision` + `notifyCommittedContentChanged` publish after the batch.
- Companion sealing now performs one heap-reserve admission check for the entire companion batch and skips repeated per-row heap checks once the batch is admitted.
- Companion sealing now reserves `passes.editPasses` capacity for the full companion batch before row insertion, removing vector growth churn from the stop path.
- Added one batch timing line per companion seal (`DIAG,seal_companion_batch`) so each stop cycle records rows/sealed/duration directly.
- Added Tier-A stop sub-stage timing lines (`DIAG,odub_stop`) for `companion_seal`, `undo_push`, `mark_range`, `publish_prepared`, and `persist_request` inside `finalizeCommitSideEffects`, so long-tail stop cycles can be attributed to one owner step even under ring pressure.
- Companion per-row telemetry is now capped (first 8 `DIAG,seal_companion` rows) to avoid line-format/append churn during heavy stop cycles while preserving batch-level timing visibility (`DIAG,seal_companion_batch`).
- `markAffectedDisplayCacheRanges` now marks companion row spans first, then does one note-cache pass against the companion target-note set, instead of scanning `visualCache.notes` once per companion row.
- `applyPendingNoteChangesToOverdubSourceView` Add-note merge batching is restored (user-directed) so pending Add notes are merged in one pass before `Shorten`/`Hide` projection updates.
- `applyPendingHideAndShortenToNotes` now keeps Shorten/Hide transforms sorted by `noteId` and uses bounded lookup, removing repeated linear transform scans from the overdub-stop seal path while preserving Shorten/Hide outcomes.
- Added `SESSION_CAPTURE`-gated stop publish telemetry for the previously uninstrumented overdub-stop publish owner (`Loop::commitPendingCapturePass`) with cold/Flash placement (`LOOP_COLD_MEM`): `seal_fx_publish_pending_pass`, `seal_publish_promote_pass`, `seal_publish_span_cache_restamp`, `seal_publish_finalize_state`, `seal_publish_clear_capture`.
- `commitPendingCapturePass` span-cache restamp now hard-bounds stop-path work: when the committed pass exceeds the stop restamp event budget (`2048` events), restamp is deferred (`seal_publish_span_cache_restamp=deferred_event_budget`) and revision restamp is skipped so heavy rebuild work moves out of the synchronous stop envelope.
- `commitPendingCapturePass` now skips full committed-pass note reconstruction when no `NoteOn` id is missing from the span cache, and reconstructs only when missing ids remain after fast eligibility filtering.
- `mergeDisplayNotesIntoOverdubSourceView` and overdub-source span-cache add paths now use sorted `NoteId` indices for bounded membership checks instead of repeated per-candidate linear scans.
- Added one-shot post-stop causality probe lines: `ODUB,stop,debt_handoff` plus `DIAG,odub_debt` on the first PLAYING idle-maintenance turn after overdub stop, classifying visual/save debt as `pre_existing_debt`, `stop_created_debt`, `stop_amplified_debt`, or `newly_scheduled_after_stop` using pre-stop vs post-stop dirty/pending snapshots.
- Causality capture [`224752`](../../captures/session_20260820_224752.log) confirms stop-created visual debt in the first PLAYING maintenance turn (`ODUB,stop,debt_handoff=stop_created_debt`, 10/10 stops). Follow-up scheduling trim: while `playingMidiDrainAfterOverdubStopActive()` is true, idle visual cache slices are capped to 1 bar and mixed-dirty long-loop fallback stays neighborhood-bounded (no `UINT32_MAX` distance override in that drain window).
- Drain-window clamp now applies to all tracks in the same loop turn (via `main()` `postOverdubPlayingMidiDrain` -> `Track::processDeferredIdleMaintenance(..., postOverdubDrainWindowActive)`), so non-owner tracks cannot run broader PLAYING idle visual slices while a post-overdub drain window is active.
- Drain-window policy now restricts PLAYING idle visual maintenance to the overdub-stop owner track while the post-overdub drain window is armed; non-owner tracks defer their visual idle slices until the owner drain window clears, preventing unrelated pre-existing debt from consuming the same first post-stop MIDI turn.
- Behavior scope guard: this drain-window policy is stop-only. It is armed by `stopOverdubbing` (`armPlayingMidiDrainAfterOverdubStop`) and reset by `startOverdubbing`; the clamp gate applies only to PLAYING/STOPPED_RECORDING maintenance. OVERDUBBING and overdub-wrap commit paths keep normal note-update processing (`commitOverdubWrapAtSessionStart` still seals companions with source-view update enabled).
- Added post-stop arbitration gate in `main()` via `MidiServiceDrain::allowDeferredSave`: while the post-overdub drain window is active and deferred save work is pending, `StorageManager::processDeferredSaveState` is deferred for that loop turn so `idle_maint` and `persist_save` do not stack in the same drain-window slot.
- Added post-stop visual slice append guard in `Track::processDeferredIdleMaintenance`: for the full post-overdub drain window, `Loop::rebuildVisualCacheIdleSlice` runs without wrap-pair append (`allowOverdubWrapAppend=false`) so expensive `idle_append` work does not stack into stressed post-stop runtime windows.
- Added constrained fallback gather mode for the full post-overdub drain window: `Loop::rebuildVisualCacheIdleSlice(..., constrainFallbackGather=true)` removes pad bars and skips active edit-row projection in fallback gather windows, so post-stop fallback slices stay bounded when prepared windows are unavailable.
- Added no-progress constrained-slice backoff in `Track::processDeferredIdleMaintenance`: when a constrained fallback slice does not reduce dirty bars, the next constrained slice is deferred for `250 ms` (`overdubDrainFallbackRetryAtMs_`) to prevent repeated high-cost fallback churn in adjacent loop turns.
- Added non-overdub STOPPED follow-up optimization in `StorageManagerInternal::enqueueRemainingLoopSlotRestores`: boot background-fill now reuses boot-scanned in-RAM slot payload flags and skips per-slot SD reprobe during full enqueue, removing one-turn `SD.exists`/token-scan churn from `boot_commit`/`load_frame`.
- Further optimized `enqueueRemainingLoopSlotRestores` bulk admission: per-entry priority computation and per-entry queue sort are now deferred during full boot enqueue, then applied once via final `reprioritizeDeferredLoopSlotRestoreEntries()`, reducing boot-commit scheduler churn without changing restore ordering semantics.
- `enqueueRemainingLoopSlotRestores` now admits background-fill queue entries incrementally across loop turns (`kBootAdmissionEntriesPerTurn=4`) and only finalizes reprioritize/sort after admission completes, so STOPPED boot does not pay full remaining-slot admission in one `boot_commit` / `load_frame` window.
- Background `LoadLoopJob` stepping now enforces a transport-clock-safe resume policy: Low (non-focus) load jobs are disallowed while global transport clock is running and are parked/deferred for retry later, so slot-load catch-up cannot compete with active MIDI timing service.
- STOPPED deferred-maintenance scheduling now uses cancel/resume policy in `main()`: when all tracks are STOPPED, run `Track::processDeferredIdleMaintenance` for one track per turn (round-robin), and skip STOPPED maintenance entirely while global transport clock is running.
- Realtime visual-cache slices now defer wrap-pair append (`idle_append`) while playback/capture states are active; deferred append is resumed in STOPPED idle (`Loop::applyDeferredVisualCacheWrapAppend`) so MIDI service keeps priority during PLAYING/OVERDUBBING while append work restarts later.
- Realtime visual-cache gather restart cooldown (`activeVisualSliceRetryAtMs_`) was rolled back after reboot evidence in [`012714`](../../captures/session_20260821_012714.log) to remove the newest strict throttle while retaining append defer/resume and post-overdub drain-window guards.
- New capture attribution from [`010055`](../../captures/session_20260821_010055.log): the two ~500 ms active-window outliers are deferred-maintenance owners, not stop-path owners — `DIAG,midi_gap=534.305 ms` aligns with `DIAG,idle_maint=519.178 ms`, and `DIAG,midi_gap=564.640 ms` aligns with `DIAG,idle_maint=535.469 ms` + `DIAG,playback_build=85.417 ms`.
- Follow-up capture [`011853`](../../captures/session_20260821_011853.log) confirms active-window outliers moved away from `idle_append`: the remaining ~500 ms windows align with `idle_gather` + `idle_maint` (`midi_gap=582.155 ms` with `idle_maint=567.506 ms`; `midi_gap=582.887 ms` with `idle_maint=568.300 ms`), while the only >500 ms `idle_append` (`949.485 ms`) occurs in STOPPED with immediate `VCACHE,append_resume`.
- Added resumable window traversal in `CommittedEventRange::appendWindowBudgeted` (cursor + per-slice event budget) and rewired fallback gather in `Loop::rebuildVisualCacheIdleSlice` to use a Loop-owned gather session keyed by `(windowStart, windowLength, playbackRevision, includeActiveEditRows)`.
- Fallback gather now has explicit invalidation semantics: any window/revision/mode mismatch resets the session; cache invalidation/rebuild paths call `resetFallbackGatherSession`; incomplete slices return before reconstruction so runtime turns stay bounded.
- Added active/stopped fallback budgets in `Track::processDeferredIdleMaintenance` (`96` events during post-overdub drain, `192` during other realtime maintenance, `768` in STOPPED idle) and capture diagnostics (`DIAG,lcr,vch,gather_progress|gather_sort|gather_edit`) for phase-cost attribution.
- RAM1 remained within budget after this slice by reusing cold sort helpers and removing additional `std::sort` template instantiations from the realtime path (`teensy41-capture-serial` locals headroom restored).
- Validation capture [`015645`](../../captures/session_20260821_015645.log) confirms bounded overdub-stop owners and isolates one active fallback-gather churn outlier:
  - Overdub stop (7 published cycles): `ODUB,stop,display` **20.700-33.276 ms** (median **25.352 ms**), `ODUB,stop,seal` **11.567-21.813 ms** (median **14.569 ms**), `seal_publish_span_cache_restamp` **3.410-5.500 ms**, `seal_fx_seal_pending_changes` **1.310-1.935 ms**.
  - Active (`PLAYING`/`OVERDUBBING`) `DIAG,midi_gap`: **28.734-827.234 ms** (median **34.755 ms**); single long outlier `827.234 ms` maps to `DIAG,idle_maint=826.425 ms` + `DIAG,load_frame=23.535 ms` in `PLAYING` (`#CAP,31432460` report block).
  - Outlier window proof (`#CAP,30217106` → `#CAP,31417617`): **122** consecutive `DIAG,lcr,vch,gather_progress,ev=192,total=192,done=0,resume=0` lines immediately precede the outlier, with no `gather_sort`/`gather_edit` completion in that suffix, proving active-window fallback gather restart/no-resume churn under the current session key.
- Follow-up owner fix attempt (in-flight session window hold) was reverted in the same session after startup instability report; keep active behavior on the last known-stable fallback-session semantics while root-causing a safer anti-reset strategy.
- `applyNoteEditPassSequence` now prefilters `Update/Delete` rows by currently present target `NoteId`s while preserving row order, and maintains the present-id set through `Create/Delete` so fallback gather avoids repeated no-op target scans in dense active edit-row sessions.
- `applyNoteEditPassSequence` Phase A planner is now active with strict barrier semantics: only contiguous `Update` rows for the same `targetNoteId` + same property collapse, and only for `Velocity`/`Pitch` updates; any other row remains an execution barrier.
- Added executor-equivalence coverage for the Phase A planner in `test_edit_apply` (`test_sequence_phasea_collapse_contiguous_velocity_and_pitch_equivalence`, `test_sequence_phasea_barrier_with_length_keeps_equivalence`, `test_sequence_phasea_randomized_equivalence_against_baseline`), validated against the non-collapsing baseline executor over deterministic randomized scenarios.
- `applyNoteEditPassSequence` and `planContiguousCollapsedRows` are now `NOTE_EDIT_MEM` to keep Phase A planner logic out of RAM1/ITCM on Teensy (`teensy41-capture-serial` locals headroom preserved).
- Phase B cache slice shipped in `applyNoteEditPassSequence`: added per-sequence NoteOn index reuse (`NoteOnIndexLookupCache`) with conservative invalidation semantics. Cache entries are positional indices and are cleared after structural row actions (`Create`, `Delete`, `NoteRange`, `Length`) and after any cache-stale skip path, so reuse is limited to ordering-safe windows (`Pitch`/`Velocity` updates and unchanged vector topology).
- Internal note-apply helpers now use cache-aware lookup (`findNoteOnByIdCached`) while preserving public behavior (`findNoteOnById` remains linear and unchanged for external callers); structural helpers also clear cache after in-function erase/sort paths (`applyDeleteNoteById`, `applyMoveNoteById`, `applyChangeLengthById`, `applyAddNote`).
- Added Phase B targeted equivalence coverage in `test_edit_apply` (`test_sequence_phaseb_length_then_same_note_update_equivalence`, `test_sequence_phaseb_length_with_interleaved_note_updates_equivalence`) to pin length-mutation invalidation safety before and after interleaved note updates.
- Validation capture [`082858`](../../captures/session_20260821_082858.log) confirms stop owners remain bounded after Phase A while active fallback edit work remains the outlier owner:
  - Overdub stop (11 published cycles): `ODUB,stop,display` **17.031-28.116 ms** (median **22.602 ms**), `ODUB,stop,seal` **9.855-17.730 ms** (median **14.207 ms**), `seal_publish_span_cache_restamp` **3.292-6.048 ms**, `seal_fx_seal_pending_changes` **0.440-1.999 ms**, `seal_fx_mark_display_ranges` **3.189-6.092 ms**.
  - Active (`PLAYING` + `OVERDUBBING`) `DIAG,midi_gap`: **29.052-856.062 ms** (median **73.630 ms**). Peak active report `#CAP,90758349` maps to `DIAG,idle_maint=854.985 ms` and `DIAG,lcr,vch,gather_edit,us=826242`.
  - Second active outlier `#CAP,95758554` (`midi_gap=600.711 ms`) maps to `DIAG,idle_maint=585.338 ms` with concurrent deferred work (`persist_save=11.345 ms`).
  - Phase A reduces worst `gather_edit` from **1,083,993 us** (`015645`) to **826,242 us** (`082858`), but `gather_edit` remains large enough to dominate active runtime stalls; cache-oriented Phase B remains required.
  - Non-active outliers stay outside the overdub-stop owner boundary (`midi_gap=23,391,338 us` at `#CAP,35316786` aligns with boot/load `loop_rem,boot_commit` and `loop_rem,load_frame` in STOPPED startup windows).
- Validation capture [`084213`](../../captures/session_20260821_084213.log) confirms the Phase B cache slice shifts fallback edit cost out of active runtime windows:
  - Overdub stop (10 published cycles): `ODUB,stop,display` **17.509-47.266 ms** (median **23.202 ms**), `ODUB,stop,seal` **9.558-36.016 ms** (median **13.686 ms**), `seal_publish_span_cache_restamp` **2.826-5.960 ms**, `seal_fx_seal_pending_changes` **1.149-2.038 ms**.
  - Active (`PLAYING` + `OVERDUBBING`) `DIAG,midi_gap`: **26.828-79.941 ms** (median **37.399 ms**, 21 samples), down from `082858` active max **856.062 ms** and median **73.630 ms**.
  - The two largest `DIAG,lcr,vch,gather_edit` events (`1,070,494 us` at `#CAP,164959999`; `464,307 us` at `#CAP,163097840`) occur in `STOPPED` state and map to `DIAG,loop_rem,idle_gather` / `DIAG,loop_rem,idle_maint`, not active MIDI-service windows.
  - The largest active `midi_gap` windows (`#CAP,90939663` at **79.941 ms**, `#CAP,75906773` at **78.345 ms**) show `persist_save` + `load_frame` stacking while `DIAG,idle_maint` stays low (about **5.3 ms**), and do not include active `gather_edit` outliers.
  - One stop-cycle display outlier (`ODUB,stop,display=47.266 ms`) is attributed to synchronous stop sub-stage `seal_fx_mark_display_ranges=26.886 ms` in the same cycle (`#CAP,111344311` series).
- Validation capture [`085458`](../../captures/session_20260821_085458.log) shows stop-path owners remain bounded but active runtime regressed back to idle-maintenance ownership:
  - Overdub stop (7 published cycles): `ODUB,stop,display` **20.501-33.291 ms** (median **24.063 ms**), `ODUB,stop,seal` **11.776-21.663 ms** (median **13.630 ms**), `seal_publish_span_cache_restamp` **4.019-6.123 ms**, `seal_fx_seal_pending_changes` **0.621-2.038 ms**.
  - Active (`PLAYING` + `OVERDUBBING`) `DIAG,midi_gap`: **23.825-2,893.664 ms** (median **36.546 ms**, 18 samples), vs `084213` active max **79.941 ms**.
  - Largest active windows map to `idle_maint` (not persist-save stacking): `#CAP,44306112` `midi_gap=2,893.664 ms` with `idle_maint=2,864.114 ms`, `load_frame=30.239 ms`, `persist_save=0.234 ms`; `#CAP,44282997` `midi_gap=2,865.744 ms` with `idle_maint=2,864.092 ms`, `load_frame=26.220 ms`, `persist_save=0.197 ms`.
  - OVERDUBBING outliers also map to fallback gather/edit cost: `#CAP,60224592` `midi_gap=842.661 ms` aligns with `idle_maint=840.200 ms`; immediately preceding owner lines show `DIAG,lcr,vch,gather_edit,us=813088` (`#CAP,58974437`) and `DIAG,loop_rem,idle_gather=817.671 ms`.
  - Additional active gather/edit spikes are present in OVERDUBBING (`gather_edit` up to **813.088 ms** and **753.577 ms**) with paired `loop_rem,idle_gather` / `loop_rem,idle_maint` windows, confirming the remaining owner is active fallback gather-edit churn.
- Active owner follow-up shipped: `main()` now gates `StorageManager::processDeferredSaveState` behind same-turn `load_frame` duration (`MidiServiceDrain::allowDeferredSaveAfterLoadFrame`) so timing-critical turns defer save when `load_frame >= 15 ms`; existing post-overdub drain-window save gate remains in effect. Native + `teensy41-capture-serial` build passed; recapture pending.
- Overdub-stop companion sealing now skips source-view mutation in the stop owner path (`sealPendingNoteChangesToEditPasses(!overdubStop)`), preserving the same persisted companions while avoiding source-view/spancache projection work during synchronous stop handoff.
- `sealPendingNoteChangesToEditPasses` is explicitly `LOOP_COLD_MEM` so this stop-only companion owner stays out of RAM1/ITCM budget.
- `commitPendingCapturePass` wrap-pair restamp guard now prefilters with head/tail window key sets before fallback scan, and missing-id collection batches then deduplicates once before reconstruction, reducing restamp bookkeeping churn in `seal_publish_span_cache_restamp`.
- Wrapped head/tail detection during span-cache restamp now uses bounded linear scan without the previous 17x128 stack table, removing large fixed stack footprint from the overdub-stop publish owner.
- `Loop::commitCapturePass`, `Loop::sealCapture`, and `Loop::finalizeCaptureWrapWindowAtStop` are explicitly `LOOP_COLD_MEM` to keep overdub-stop owner code on flash and restore RAM1 headroom in `teensy41-capture-serial`.
- `Track::stopOverdubbing` and the in-edit `handleNoteEditFold` stop path now use `SC_REC_FLUSH_PENDING_REVTS(128)` with Tier-A ODUB stop markers, preserving stop-stage observability without reintroducing broad flush churn in the synchronous owner path.
- `CaptureLineTier::isTierALine` now treats `ODUB,stop` lines as Tier-A so stop-stage telemetry remains visible even with deterministic `flush(8)` stop budgets under ring pressure.
- Companion-only projection fast path shipped in `Loop` window consumers: `copyEffectiveCommittedEventsInRange` and `advanceFallbackGatherSession` copy a ready Overdub companion cache only. A miss, including an empty projection, uses windowed apply. Full companion rebuild is not admitted from gather (Stage A; [`162928`](../../captures/session_20260821_162928.log)).

**Status:** Native **1399/1399** and `teensy41-capture-serial` build **PASS**. New baseline capture [`210319`](../../captures/session_20260820_210319.log) replaces older stop-lag envelopes for planning.

### Overdub-start begin-capture long-delay restoration — validated

**Evidence:** [`165534`](../../captures/session_20260820_165534.log), [`165956`](../../captures/session_20260820_165956.log)

**Owner:** `Loop::rebuildOverdubSourceSpanCache`, `Track::startOverdubbing` (`ODUB,stage,begin_capture`).

**Invariant:** Overdub start stays deterministic while an overdub session is active: if prepared spans are unavailable, start/wrap does not force a full-loop materialize on the timing-critical path.

**Root cause proved in captures:** `ODUB,stage,begin_capture` spiked to about **2.2 s** (`2194189 us`, `2248843 us`) in [`165956`](../../captures/session_20260820_165956.log), and `Live Overdub -> Overdubbing started` was delayed by **2143-2249 ms** on early starts. The same pattern appears in [`165534`](../../captures/session_20260820_165534.log) (~2102/2189 ms). This directly maps to `rebuildOverdubSourceSpanCache` falling back to `passes.materializeToEventVector` when prepared spans are unavailable.

**Stage change shipped:** Restored overdub-session cache fallback in `rebuildOverdubSourceSpanCache`: when prepared spans are unavailable during an active overdub session, reuse prior cache notes instead of forcing full-loop materialization on start/wrap. Non-session behavior remains unchanged (full materialize is still allowed outside active overdub sessions).

**Status:** Native **1397/1397**. `teensy41-capture-serial` build **PASS**. New baseline [`210319`](../../captures/session_20260820_210319.log): `ODUB,stage,begin_capture` stays bounded at **1851-6443 us** (median **2070 us**), confirming start stalls are no longer the active bottleneck.

**Historical outlier context:** [`170724`](../../captures/session_20260820_170724.log) contains a one-cycle stop-path outlier (`ODUB,stop,seal=310980 us`, `display=320406 us`). The new planning baseline is [`210319`](../../captures/session_20260820_210319.log) runtime windows above (published stop display 56.451-71.803 ms; first post-stop MIDI Input Gap 78.634-145.373 ms).

### Overdub-start reboot — reverted to baseline, blocked on instrumentation

**Evidence:** [`162146`](../../captures/session_20260820_162146.log), [`132145`](../../captures/session_20260820_132145.log), [`143518`](../../captures/session_20260820_143518.log)

**Owner:** `Track::startOverdubbing`, `TrackManager::startOverdubbingTrack`, `DebugSessionCapture` capture ring.

**Symptom:** USB drops immediately after `[TRACK] Overdubbing started @ tick N`, the final statement of `Track::startOverdubbing`. Reproduces on first and second overdub start.

**Not caused by the 2026-08-20 removals.** The reboot is present in [`132145`](../../captures/session_20260820_132145.log) and [`143518`](../../captures/session_20260820_143518.log), before any of them. Commits `4af158a`, `39b8e1d`, `983262d`, `98699b6`, `6ff4142` are reverted; the tree is byte-identical to `28cf6e3`. Pass accumulation is also ruled out — the first capture of the day already reached `pass,99` and the last reached `pass,101`.

**Capture blind spot (why eight successive fixes all "made no difference"):** two independent mechanisms starve overdub telemetry out of the ring.

| Mechanism | Owner | Effect |
|---|---|---|
| Flush budget `<= 8` takes the drop-only branch | `DebugSessionCapture::flushCaptureBuffer` | Discards up to 8 non-Tier-A head records and returns without transmitting. The overdub `SC_REC_FLUSH_PENDING_REVTS(8)` sites delete telemetry instead of sending it. |
| Tier-B append refused while ring is full and head is Tier-A | `DebugSessionCapture::appendCaptureRecord` | `ODUB,stage`, `MI`, `MO`, `LED` never enter the ring. `DIAG,lcr,` and `VCACHE,` are Tier-A per `CaptureLineTier::isTierALine`. |
| `formatPhaseLine` 1 s rate limit never engages | `LoopContentResolution` device-gate slice machine | `stepChanged` is true on every slice because the machine alternates `idx` ↔ `pair`, so the Tier-A `DIAG,lcr,phase` line emits per slice, not per second. |

In [`162146`](../../captures/session_20260820_162146.log) the last `#CAP,…,MI,…` is at 9.387 s and the last `LED` at 14.89 s, while `DIAG,lcr,phase` keeps emitting to 33.38 s (355 lines). The record button press at 35.198 s produced a text log line but **no** `MI` capture line, so Tier-B appends were already being refused when overdub started. `ODUB,stage,manager_enter` / `manager_done` are therefore absent for instrumentation reasons, not because that code did not run.

**Fault reason never observed.** `setup()` prints `CrashReport` before `logger.setup(LOG_DEBUG)`, and every reconnect boot in the 2026-08-20 captures begins at `Logger initialized with level: 3`. The pre-logger boot output is absent from all of them. The firmware also writes `CrashReport` to `crashlog.txt` on SD (`main.cpp` `setup()`); that file is the authoritative record and has not been read.

**Next:** read SD `crashlog.txt` to classify the fault (allocator abort vs hard fault vs watchdog) before any further code change. Do not attribute the reboot to a removal again without an `ODUB,stage` line or a crash record.

**Status:** Native **1397/1397**. `teensy41-capture-serial` RAM1 code **424572** / locals **4768**. Not flashed.

### Cleanup branch telemetry/fader compile-gating — shipped stage

**Branch:** `chore/cleanup-codebase-tidiness`

**Owner:** `MidiHandler::mirrorUsbFaderProbePassthrough`, `RuntimeTimingTelemetry` callsites, overdub stop telemetry callsites.

**Invariant:** Plain `teensy41` builds do not execute telemetry-only timing work (`micros()`/heap snapshots/local telemetry structs) when the owning capture or perf feature is disabled; fader USB-host helper logic is compiled only with `MIDI_USB_FADER_PROBE_PASSTHROUGH`.

**Status:** **Verified** `pio test -e native`, `pio run -e teensy41`, `pio run -e teensy41-capture-serial` with zero compiler warnings.

### Overdub session index reboot undo — HITL PASS

**Plan:** [`overdub_session_index_reboot_undo_bugfix.md`](../Plans/overdub_session_index_reboot_undo_bugfix.md)  
**Evidence:** [`003854`](../../captures/session_20260820_003854.log)

**Owner:** `Loop::openOverdubSession`, `deriveContentUndoUnits`, `StorageLoopIo` `OSI1`.

**Invariant:** One overdub session is one `OverdubPassAdded` before and after reboot. Wraps share `overdubSessionIndex` (same grouping role as `editPassIndex`). Missing `OSI1` stays one unit per pass.

**Status:** **HITL PASS** [`003854`](../../captures/session_20260820_003854.log). Native **1397/1397**. RAM1 code **425548** / locals **4768**. Three wraps + stop; reboot `Undo (entries=2)` `kind=1` `undo_count=179`; `DISP` 112→14; redo restores 112.

### Overdub-stop handoff flash

**Plan:** [`display_overdub_stop_handoff_flash_bugfix.md`](../Plans/display_overdub_stop_handoff_flash_bugfix.md)  
**Evidence:** [`000553`](../../captures/session_20260820_000553.log)

**Owner:** `DisplayManager::resolveDisplayNotesCommitted`, `preferPreservedOverdubStopHandoff`.

**Invariant:** While `visualCacheDirty` after overdub stop, the revision-matched composed frame stays paint authority. Follow re-filters that frame; it does not replace it with pre-commit `visualCache.notes`.

**Status:** **HITL PASS** [`001925`](../../captures/session_20260820_001925.log). Native **1391/1391**. RAM1 code **425276** / locals **4768**. Evidence [`000553`](../../captures/session_20260820_000553.log) remains the failing baseline (655→583 at `vis=2166`).

### Display window follow readiness

**Plan:** [`display_window_follow_readiness_bugfix.md`](../Plans/display_window_follow_readiness_bugfix.md)  
**Evidence:** [`232337`](../../captures/session_20260819_232337.log)

**Owner:** `DisplayManager::resolveDisplayNotesCommitted`, `resolveDisplayNotesLiveCapture`, `resolveWindowedDisplayNotes`, `visualCacheCoversWindow`.

**Invariant:** Long-loop committed paint filters `visualCache.notes` for the paint window plus follow margin/lookahead. A dirty cache does not keep a 16-bar `overdubSourceViewNotes` frame as display authority.

**Status:** **HITL PASS** [`000553`](../../captures/session_20260820_000553.log) — `verify_follow_window_readiness` ok (0 stale holds). Native **1390/1390**. RAM1 code **425276** / locals **4768**. Evidence [`232337`](../../captures/session_20260819_232337.log) remains the failing baseline.

### Overdub participant discovery (occupy / source-view RC closed)

Occupy source-view resolver geometry ([`overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md`](../Plans/overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md)) and wrap display D2-D ([`overdub_wrap_source_view_display_drops_committed_bugfix.md`](../Plans/overdub_wrap_source_view_display_drops_committed_bugfix.md)) are **FROZEN** — HITL [`174246`](../../captures/session_20260819_174246.log). Native **1384/1384**. Next on this branch: 64-bar `from=span` HITL **parked** (DEC-041); see § Parked below.

### Overdub ledger completion — shipped (Stages 2–4)

**Plan:** [`overdub_ledger_completion_enhancement.md`](../Plans/overdub_ledger_completion_enhancement.md)

**Owner:** `Loop::ensureOverdubSourceNotesForHold`, `Loop::accumulatePendingNoteChangesForIncomingNote`.

**Invariant:** Source-view rebuild and long-loop note-off hold hydration both read the source span cache (`from=cache`) with prepared spans when available and full-loop resolved spans otherwise; source-view path no longer calls `resolveWindow`.

**Status:** Native **1387/1387**. Baseline cost anchor [`202256`](../../captures/session_20260819_202256.log) (32× `from=win`, `merged=0`). Device check [`221834`](../../captures/session_20260819_221834.log): hold path `from=cache` with no `hold,miss`. Overdub-entry stall RC: companion sealing now restamps source-span cache revision only for active overdub sessions with full companion seal success, preventing stale-stamp rebuilds on the next overdub entry while preserving non-session rebuild behavior. Cold-start mitigation originally called `Loop::prewarmOverdubSourceSpanCache` from STOPPED idle after visual cache cleanup. `754db31` removed that caller because same-turn prewarm after `slice_clean` hung [`160519`](../../captures/session_20260820_160519.log). Stage B restores the caller on a later clean-cache turn after `boot_commit`. Device PASS [`165107`](../../captures/session_20260821_165107.log) after [`162928`](../../captures/session_20260821_162928.log) opened `notes=0` without it.

### Consume id-resolution completeness — FROZEN (Stages 1–2)

**Plan:** [`overdub_consume_id_resolution_completeness_bugfix.md`](../Plans/overdub_consume_id_resolution_completeness_bugfix.md)

**Owner:** `Loop::accumulatePendingNoteChangesForIncomingNote`. All holds → `effectiveOverlapNoteIds` (incoming ids + geometric union + long-loop hold fill), then `appendNotesForIds` only. `collectConsumeWindow` **removed**.

**Invariant:** Every geometric consume participant resolves via `appendNotesForIds`; no window scan.

**Status:** Native **1387/1387**. HITL Stage 1 [`195016`](../../captures/session_20260819_195016.log); Stage 2 [`201457`](../../captures/session_20260819_201457.log): zero `DIAG,consume` / `scanadd` / `norow`; occupy **86/86** `eq=1`. RAM1 code **425964** / locals **4768**.

**Closed predecessor:** [`overdub_consume_ledger_merge_enhancement.md`](../Plans/overdub_consume_ledger_merge_enhancement.md) Stage 1A/1B attribution — selection-change rejected on [`191133`](../../captures/session_20260819_191133.log).

### Consume candidate attribution — Stage 1A/1B COMPLETE (observability only, superseded)

**Plan:** [`overdub_consume_ledger_merge_enhancement.md`](../Plans/overdub_consume_ledger_merge_enhancement.md)

**Owner:** `Loop::accumulatePendingNoteChangesForIncomingNote`. Selection behavior **unchanged** — this does **not** start the parked consume merge.

**Invariant:** consume candidate selection is measured, not changed: every candidate is attributed to the occupy-id lookup, the window scan, or late JIT materialization.

**Status:** Counters + `DIAG,consume,select` shipped. **1B detail diagnostics shipped:** `DIAG,consume,norowid` (ids that failed to resolve to a source-view row) and `DIAG,consume,scanadd` (scan-added candidate ids with `in_ids=0|1`). **Consume merge selection change REJECTED** — HITL [`191133`](../../captures/session_20260819_191133.log): 3 of 5 attributed holds had a **non-empty** occupy set where the window scan was the only path to a participant (2× `idsel=1`, 1× `norow=1`). Follow-up HITL [`193024`](../../captures/session_20260819_193024.log): `norow` did not reproduce (`norow=0`, `norowid=0`), while `scanadd` persisted (18 lines, all `in_ids=0`). Empty-ids fallback remains load-bearing. Native **1386/1386**. RAM1 code **425964** / locals **4768**.

**Stage 1A guard check:** `overlapNoteIds` is written by `snapshotOverlapHoldCandidates` (open at `holdStart`) **and** `collectOverlapHoldPlaybackNoteOn` (NoteOns during the hold, from `sendMidiEvent` before the `playbackEmitMidiOutput_` gate). `appendNotesForIds` cannot reach a note absent from `overdubSourceViewNotes_`, so the long-loop JIT branch is the only path to a JIT-merged ahead note — proven by `test_consume_attribution_counts_late_note_for_jit_ahead_candidate`. Blocking additive candidates on non-empty ids therefore changes long-loop consume, which a 1-bar gate cannot observe.

**Long-loop JIT class did not reproduce:** `why=hold` **140** with long loops present (`live=18432`, `live=52224`), yet every line is `jit=0` / `late=0`. Reachable in native, not the device problem.

**Closed 2026-08-19.** Stage 1A delivered its purpose: the attribution evidence decided the selection question. Stages 1 and 2 (selection change) are **rejected** — do **not** block additive scan candidates on non-empty ids. The `norow` class is **tagged for investigation** in § Parked, no plan opened.

### Occupy source view keeps resolver geometry — FROZEN

**Plan:** [`overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md`](../Plans/overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md)  
**Closed:** 2026-08-19 — **Gate 5A** shipped; extra covering `a>n` **0** [`161349`](../../captures/session_20260819_161349.log). Display/source-view fix: sibling D2-D [`174246`](../../captures/session_20260819_174246.log). **Gate 5B withdrawn.** Do not reopen without new `a>n` or pin [`121141`](../../captures/session_20260819_121141.log) `n=1 a=2`.

### Overdub wrap display drop — FROZEN

**Plan:** [`overdub_wrap_source_view_display_drops_committed_bugfix.md`](../Plans/overdub_wrap_source_view_display_drops_committed_bugfix.md)  
**Closed:** 2026-08-19 — D2-D revision-freshness guard in `committedPlaybackNoteOnIdentityValid`. HITL [`174246`](../../captures/session_20260819_174246.log): DISP monotonic, `identity_invalid` **0**, ledger **95/95**. Lane **N** diagnostic only. Layer 1 `prep=0` [`170838`](../../captures/session_20260819_170838.log) **parked**.

### Occupy missing open identity (`n=1 a=2` extra covering span)

**Plan:** [`overdub_occupy_missing_open_identity_bugfix.md`](../Plans/overdub_occupy_missing_open_identity_bugfix.md)  
**Parent:** [`overdub_occupy_leftover_identity_bugfix.md`](../Plans/overdub_occupy_leftover_identity_bugfix.md) — leftover HITL **PASS** [`111819`](../../captures/session_20260819_111819.log)  
**Pin:** [`111819`](../../captures/session_20260819_111819.log) L948 pitch 24 `hs=72` `n=1 a=2` covering 5893 `0–743` + 5901 `0–168`

**Invariant:** A source-view / prepared identity counts as present-at-hold only if that `noteId` has a NoteOn in the complete committed playback stream.

**Status:** Native **PASS** 1374/1374. RAM1 **425948** / **4768**. HITL [`121141`](../../captures/session_20260819_121141.log): 5893-class **MET**. Remaining `n=1 a=2` **2 FAIL** — extra covering ids **have** a merged NoteOn (`on=1`). This RC’s root cause is invalid for those FAILs. **STOP** — do not widen the identity filter. Successor: source-view resolver geometry above.

**Does not reopen:** occupy catching up when `occupyPhase <= lastTick`; `playMidiEvents` from occupy; advancing `lastTick` from USB; Off stamping; FIFO; option B; leftover ledger erase; `isPlaybackCatchUpWindow` equal-tick contract.

### Occupy leftover identity after rematerialize

**Plan:** [`overdub_occupy_leftover_identity_bugfix.md`](../Plans/overdub_occupy_leftover_identity_bugfix.md)  
**Parent:** [`overdub_occupy_clock_duplicate_off_ledger_bugfix.md`](../Plans/overdub_occupy_clock_duplicate_off_ledger_bugfix.md) — HITL [`104654`](../../captures/session_20260819_104654.log) duplicate-Off apply **not** this FAIL  
**Pin:** [`104654`](../../captures/session_20260819_104654.log) 19× `n=1 a=0` pitch 12 `lid=5701` `lst=96`

**Invariant:** An open ledger identity is retained iff its NoteOn identity exists in a complete rebuilt committed playback stream.

**Native:** **PASS** 1371/1371. RAM1 **425932** / **4768**.  
**HITL [`111819`](../../captures/session_20260819_111819.log):** leftover **MET**. `n=1 a=0` **0**. `n=0 a=1` **0**. `led == n` **1/1**. `5701` gone. 11 `ledger,erase`. Remaining 1 mismatch is `n=1 a=2` pitch 24 `hs=72` (two covering spans, one Entry) — not this FAIL.

**Does not reopen:** occupy catching up when `occupyPhase <= lastTick`; `playMidiEvents` from occupy; advancing `lastTick` from USB; Off stamping; FIFO; option B; clearing the whole ledger; `isPlaybackCatchUpWindow` equal-tick contract; duplicate-Off ledger apply.

### Occupy clock duplicate Off skips ledger

**Plan:** [`overdub_occupy_clock_duplicate_off_ledger_bugfix.md`](../Plans/overdub_occupy_clock_duplicate_off_ledger_bugfix.md)  
**Parent:** [`overdub_occupy_duplicate_open_identity_bugfix.md`](../Plans/overdub_occupy_duplicate_open_identity_bugfix.md) — HITL [`103234`](../../captures/session_20260819_103234.log) `led == n` **MET**  
**Pin:** [`103234`](../../captures/session_20260819_103234.log) L6674 pitch 24 `n=3 a=2`; two `Off@71`

**Invariant:** A second committed NoteOff (or NoteOn) at the same phase, pitch, and type still applies to `ActiveNoteLedger`; only the MIDI wire is deduped.

**HITL [`104654`](../../captures/session_20260819_104654.log):** `led == n` **41/41**. `n=0 a=1` **0**. Pin two-Off dumps **0**. Leftover counts **FAIL**: `n=1 a=0` 18→19 (all pitch 12 `lid=5701` `lst=96`); `n=3 a=2` 1→2 (nested On@240 pair + leftover, not two Off@71). `n=2 a=1` 20→10. Do not reopen this apply. Successor: leftover identity above.

**Does not reopen:** occupy catching up when `occupyPhase <= lastTick`; `playMidiEvents` from occupy; advancing `lastTick` from USB; Off stamping; `rebuildPlaybackOrder`; duplicate `noteOn` no-op push.

### Occupy duplicate open identity (catch-up then clock)

**Plan:** [`overdub_occupy_duplicate_open_identity_bugfix.md`](../Plans/overdub_occupy_duplicate_open_identity_bugfix.md)  
**Parent:** [`overdub_occupy_catchup_open_note_stack_bugfix.md`](../Plans/overdub_occupy_catchup_open_note_stack_bugfix.md) — HITL [`101319`](../../captures/session_20260819_101319.log) extra-open remains  
**Pin:** [`101319`](../../captures/session_20260819_101319.log) L2318 `n=2` `led=4`

**Invariant:** An open playback identity occupies at most one ledger Entry. A second NoteOn with that `noteId` does not push; `applyPlaybackEvent` still returns true so clock can emit.

**HITL [`103234`](../../captures/session_20260819_103234.log):** `led == n` **42/42 MET**. `n=0 a=1` **0**. Extra unique ids remain (`n=2 a=1` 25→20). `n=1 a=0` rose 3→18 — successor above (clock duplicate Off skips ledger), not exclusive-end. All mismatches `cu=0`.

**Does not reopen:** occupy catching up when `occupyPhase <= lastTick`; `playMidiEvents` from occupy; advancing `lastTick` from USB; Off stamping; `rebuildPlaybackOrder`.

### Occupy catch-up per-phase Off then On (open-NoteOn stack)

**Plan:** [`overdub_occupy_catchup_open_note_stack_bugfix.md`](../Plans/overdub_occupy_catchup_open_note_stack_bugfix.md)  
**Parent:** [`overdub_occupy_active_note_ledger_cardinality_refinement.md`](../Plans/overdub_occupy_active_note_ledger_cardinality_refinement.md) — nested HITL **MET** [`095902`](../../captures/session_20260819_095902.log)  
**Pin:** [`095902`](../../captures/session_20260819_095902.log) pitch 12 `hs=336` `n=3 a=1` (ended 5218/5224 still open)

**Invariant:** `applyOpenClosedInterval` applies in-interval events in phase order, Off before On at each phase. An untagged Off can close a NoteOn that started in the same `(lastTick, occupy]` window.

**HITL [`101319`](../../captures/session_20260819_101319.log):** extra-open **not met** (`n>a` 35 vs 40 on [`095902`](../../captures/session_20260819_095902.log); `n=2 a=0` 15→1; `n=2 a=1` 6→25). One span-start `n=0 a=1` (pitch 23 `hs=384`). All 37 mismatches `cu=0`. Native fixture still holds. Do not widen catch-up; next is clock-path.

**Does not reopen:** Off stamping; option B; clock equal-phase Off before On; `playMidiEvents` from occupy; folding capture into `mergedMidiEvents`; occupy catching up when `occupyPhase <= lastTick`; `rebuildPlaybackOrder`.

### Occupy open-NoteOn ledger cardinality — DEC-042 (nested HITL MET)

**Plan:** [`overdub_occupy_active_note_ledger_cardinality_refinement.md`](../Plans/overdub_occupy_active_note_ledger_cardinality_refinement.md)  
**Decision:** [DEC-042](../DECISION_LOG.md#dec-042-same-pitch-active-note-identity-is-a-cardinality-problem-not-an-off-identity-problem)  
**Parent:** [`overdub_occupy_unmatched_off_ledger_investigation.md`](../Plans/overdub_occupy_unmatched_off_ledger_investigation.md) — geometry proven [`092336`](../../captures/session_20260819_092336.log)

**Invariant:** `ActiveNoteLedger` holds every open playback NoteOn. Occupy walks `forEachActive`. Untagged Off → LIFO; identified Off → exact or orphan (no LIFO fallback). `noteId()` is compatibility newest-on-lane only.

**HITL [`095902`](../../captures/session_20260819_095902.log):** nested `n=0 a=1` **0 / MET**. Structural `n=2 a=2` **2**; leftover `n=1 a=2` **1** (pitch 24 same-tick `On@456` pair). Unmasked extra-open: `n=3 a=1` **18**, `n=2 a=0` **15**. All 44 mismatches `cu=0`. Extra-open is the catch-up stack RC above — not a DEC-042 fail.

**Does not reopen:** Off stamping; option B; clock equal-phase Off before On; `playMidiEvents` from occupy; folding capture into `mergedMidiEvents`.

### Occupy unmatched Off vs ledger — observability (geometry proven)

**Investigation:** [`overdub_occupy_unmatched_off_ledger_investigation.md`](../Plans/overdub_occupy_unmatched_off_ledger_investigation.md)  
**Successor:** cardinality refinement above.

### Occupy clock same-tick Off before On — shipped

**Plan:** [`overdub_occupy_clock_same_tick_off_before_on_bugfix.md`](../Plans/overdub_occupy_clock_same_tick_off_before_on_bugfix.md)  
**Parent (catch-up two-pass shipped, gate not met):** [`overdub_occupy_same_tick_off_before_on_bugfix.md`](../Plans/overdub_occupy_same_tick_off_before_on_bugfix.md)  
**Pin:** [`235314`](../../captures/session_20260818_235314.log) — **4 `n=0 a=1`**; **0 `n=1 a=0`**; span-start L4811 / L4899 are USB-after-clock  
**HITL PASS:** [`001021`](../../captures/session_20260819_001021.log) — **0 `n=0 a=1`**; **0 `n=1 a=0`**; `hs=` on 82/82 occupies. L2715 `240–336` @ 240 is `n=1 a=1`.

**Invariant:** Clock playback at equal phase applies Off before On so abutting same-pitch replacement last-writes the new On. Occupy still reads `Entry.noteId`. Catch-up skip bound unchanged (`occupyPhase <= lastTick` still skips).

**HITL gate:** **met.** Occupy 12 @ `hs=0` sounding `n=1 a=1`. Do not treat `n=1 a=2` as this FAIL. 235314 interiors L4750/L4294 were not in this capture; `n=0 a=1` is 0 so unmatched-Off is not opened.

**Successor (observability):** [`overdub_occupy_unmatched_off_ledger_investigation.md`](../Plans/overdub_occupy_unmatched_off_ledger_investigation.md) — FIFO stamping rejected; geometry now **proven** as a nested same-pitch pair ([`090050`](../../captures/session_20260819_090050.log)).

**Does not reopen:** occupy repairing clock when `occupyPhase == lastTick`; `playMidiEvents` from occupy; folding capture into `mergedMidiEvents`; changing `applyPlaybackLedgerEvent`. Catch-up apply order vs stack: [`overdub_occupy_catchup_open_note_stack_bugfix.md`](../Plans/overdub_occupy_catchup_open_note_stack_bugfix.md).

### Occupy same-tick Off before On — catch-up interval

**Plan:** [`overdub_occupy_same_tick_off_before_on_bugfix.md`](../Plans/overdub_occupy_same_tick_off_before_on_bugfix.md)  
**Parent (interval catch-up shipped, gate not met):** [`overdub_occupy_on_tick_clock_catchup_bugfix.md`](../Plans/overdub_occupy_on_tick_clock_catchup_bugfix.md)  
**Pin:** [`233247`](../../captures/session_20260818_233247.log) — **11 `n=0 a=1`**; **0 `n=1 a=0`**; `hs=` on 100/100 occupies  
**HITL FAIL:** [`235314`](../../captures/session_20260818_235314.log) — **4 `n=0 a=1`**; **0 `n=1 a=0`**; `hs=` on 133/133 occupies. Remaining span-start fails were clock equal-tick — successor HITL **PASS** [`001021`](../../captures/session_20260819_001021.log). Global two-pass vs open-NoteOn stack: [`overdub_occupy_catchup_open_note_stack_bugfix.md`](../Plans/overdub_occupy_catchup_open_note_stack_bugfix.md).  
**USB `playMidiEvents` catch-up:** **reverted** [`214856`](../../captures/session_20260818_214856.log) — do **not** call `playMidiEvents` from occupy

**Invariant:** When reconstructing committed ledger state over `(lastTickInLoop, occupyPhase]`, equal-tick replacement resolves Off before On. Occupy still does not advance playback. Apply order is per-phase Off then On in `CommittedPlaybackLedgerCatchUp::applyOpenClosedInterval` ([`overdub_occupy_catchup_open_note_stack_bugfix.md`](../Plans/overdub_occupy_catchup_open_note_stack_bugfix.md)). Clock owns cursor, `nextEventIndex`, `lastTickInLoop`, send, wrap, and capture.

**HITL gate:** leftover `n=1 a=0` **met**. `n=0 a=1` not met (4) — successor above.

**Does not reopen:** wrap-S `(prev, S]`; loop-head Q16; folding capture into `mergedMidiEvents`; occupy fallback; DisplayManager patch; setting `lastTickInLoop` or `nextEventIndex` from USB; changing `applyPlaybackLedgerEvent`.

### Occupy on-tick before clock interval — ledger catch-up

**Plan:** [`overdub_occupy_on_tick_clock_catchup_bugfix.md`](../Plans/overdub_occupy_on_tick_clock_catchup_bugfix.md)  
**Parent (FROZEN leftover met):** [`overdub_occupy_merged_capture_ledger_bugfix.md`](../Plans/overdub_occupy_merged_capture_ledger_bugfix.md)  
**Pin:** [`231038`](../../captures/session_20260818_231038.log) — 6 `n=0 a=1` pitch 12; `n=1 a=0` = 0  
**HITL FAIL:** [`233247`](../../captures/session_20260818_233247.log) — interval catch-up in tree; leftover `n=1 a=0` met; remaining 11 `n=0 a=1` are successor above.

**Invariant:** USB occupy may advance the committed ledger to the USB phase; it must **not** advance playback. Occupy consumes existing committed-only `mergedMidiEvents` via `catchUpCommittedPlaybackLedgerToPhase` (`(lastTickInLoop, occupyPhase]`). Skip wrap-crossing (`shouldCommitOverdubWrap`) and `occupyPhase <= lastTickInLoop`. Interval is trusted **after** Off-before-On in that interval.

**Does not reopen:** wrap-S `(prev, S]`; loop-head Q16; folding capture into `mergedMidiEvents`; occupy fallback; DisplayManager patch; setting `lastTickInLoop` or `nextEventIndex` from USB.

### Occupy merged-capture ledger — FROZEN

**Plan:** [`overdub_occupy_merged_capture_ledger_bugfix.md`](../Plans/overdub_occupy_merged_capture_ledger_bugfix.md)  
**Parent (FROZEN):** [`overdub_occupy_capture_stream_ledger_bugfix.md`](../Plans/overdub_occupy_capture_stream_ledger_bugfix.md)  
**Pin (pre-fix):** [`224719`](../../captures/session_20260818_224719.log) — 8 `n=0 a=1`; 6 `n=1 a=0`  
**HITL leftover met:** [`231038`](../../captures/session_20260818_231038.log) — **0 `n=1 a=0`**. Remaining 6 `n=0 a=1` are successor above.

**Invariant:** Playback `runtime.mergedMidiEvents` is committed-only. Occupy reads `ActiveNoteLedger` written from that representation, wrap-pass, and loop-head. Live capture echo uses `playbackCursorAdvanceSendCapture` and must not last-write that ledger. Do not fold capture into `mergedMidiEvents` again.

### Occupy capture-stream ledger — FROZEN

**Plan:** [`overdub_occupy_capture_stream_ledger_bugfix.md`](../Plans/overdub_occupy_capture_stream_ledger_bugfix.md)  
Emit-only `playbackCursorAdvanceSendCapture` shipped. Remaining writer was live capture folded into `mergedMidiEvents` — successor above. Pin [`221334`](../../captures/session_20260818_221334.log). HITL FAIL [`224719`](../../captures/session_20260818_224719.log).

### Occupy `n=0 a=1` off tick 0 — display vs ledger (USB catch-up reverted)

**Investigation:** [`overdub_occupy_off_tick_display_investigation.md`](../Plans/overdub_occupy_off_tick_display_investigation.md)  
**Bugfix (reverted):** [`overdub_occupy_after_wrap_s_interval_bugfix.md`](../Plans/overdub_occupy_after_wrap_s_interval_bugfix.md)  
**Pin:** [`203948`](../../captures/session_20260818_203948.log) occupy 12 @ storage **96** `n=0 a=1 b=1` (`204277855`) after wrap 2 (S=**64**)  
**HITL FAIL:** [`214856`](../../captures/session_20260818_214856.log) — catch-up did not occupy the named spans; `playMidiEvents` from occupy **reverted**

**Display:** OVERDUBBING paint is `resolveDisplayNotesLiveCapture` from `overdubSourceViewNotes` + `capturePreview`. Occupy is not a paint input. Do not patch DisplayManager.

**Named write (native only):** wrap-committed On@96 is not in wrap-S `(prev, S]`. Clock `playCommittedLoopMidi` must walk `(S, occupyTick]` after wrap reanchor at S. Do **not** call `playMidiEvents` from occupy (wrap can commit on USB NoteOn). Occupy stays a reader. CAP `as=`/`ae=` stays. Successor for USB-before-clock occupy is ledger catch-up above.

**214856 `n=0 a=1` spans (not On@96):** 30 `528–544`; 12 `0–192`; 12 `144–232`; 12 `720–767`; 24 `216–408`. Wraps at storage **8**.

**Does not reopen:** wrap-S `(prev, S]`; loop-head Q16 (HITL PASS below). No occupy fallback.

### Loop-head playback ledger after wrap — HITL PASS

**Plan:** [`overdub_loop_head_playback_ledger_bugfix.md`](../Plans/overdub_loop_head_playback_ledger_bugfix.md)  
**Investigation:** [`overdub_loop_head_playback_ledger_investigation.md`](../Plans/overdub_loop_head_playback_ledger_investigation.md)  
**Fail pin:** [`185831`](../../captures/session_20260818_185831.log) occupy 60 @ storage **0** `n=0 a=1 b=1` (`54243271`)  
**HITL PASS:** [`203948`](../../captures/session_20260818_203948.log) occupy 12 @ storage **0** `n=1 a=1 b=1` (`214037034`, `232043429`)

**Invariant:** Overdub wrap seal does not run Q16 min-length. Wrap-committed NoteOn @ 0 stays in `lastCommittedPassId()` so the existing 0-clock `atLoopStart` walk writes `ActiveNoteLedger` before occupy.

**Owner:** `Loop::sealCapture` (`CommitReason::OverdubWrap` skips `removePairsShorterThanNoteMinLength`). Occupy stays a reader. Do not add a loop-head catch-up.

**Native:** 1350/1350.

**Does not reopen:** wrap-S `(prev, S]` (HITL PASS below). No occupy fallback. `n=0 a=1` off storage 0 in [`203948`](../../captures/session_20260818_203948.log) (12 @ 96 after wrap 2) is outside this invariant.

### Wrap-tick ledger catch-up from committed pass — HITL PASS

**Plan:** [`overdub_wrap_committed_pass_playback_bugfix.md`](../Plans/overdub_wrap_committed_pass_playback_bugfix.md)  
**HITL PASS:** [`185831`](../../captures/session_20260818_185831.log) wrap at storage **696**, occupy 71 @ **704** `n=1 a=1 b=1`. Fail pin remains [`180844`](../../captures/session_20260818_180844.log) wrap 15.

### Occupy — lookup `Entry.noteId`

**Plan:** [`overdub_present_at_tick_jit_enhancement.md`](../Plans/overdub_present_at_tick_jit_enhancement.md)  
**Architecture:** [`overdub_present_at_tick_jit_architecture.md`](../Plans/overdub_present_at_tick_jit_architecture.md)  
**Decision:** [DEC-041](../DECISION_LOG.md#dec-041-occupy-present-at-s-jit-not-full-loop-lcr-mat) — `collectOverdubNoteOnParticipantIds` reads `ActiveNoteLedger` at `currentTick`; do **not** say occupy = ledger  
**Evidence:** [`132806`](../../captures/session_20260818_132806.log), [`213401`](../../captures/session_20260817_213401.log), [`152940`](../../captures/session_20260813_152940.log), [`152745`](../../captures/session_20260818_152745.log)

**Owners:** `Loop` / `LoopContentResolution` = `LoopPasses`. `playCommittedLoopMidi` writes `LoopPlaybackRuntime::ledger` via `applyPlaybackLedgerEvent` on committed-only `mergedMidiEvents` / wrap-pass / loop-head. Capture emit does not write. `sendMidiEvent` emits. Occupy reads every open identity via `forEachActive` ([DEC-042](../DECISION_LOG.md#dec-042-same-pitch-active-note-identity-is-a-cardinality-problem-not-an-off-identity-problem)).

**Product:** zero or more open NoteOns per `(channel, pitch)`. No `length` on `Entry`. `LoopPasses` may still overlap (`max_same_pitch=322`). `sendMidiEvent` does not create those entries.

**Stage 0:** **Yes** — `PresentNote.noteId` and playback `evt.noteId` are both `MidiEvent.noteId`. Not a 1:1 pitch lookup.

**Now:** occupy lookup **shipped** (`forEachActive` on the lane). CAP `from=ledger`. `noteId()` is compatibility newest-only. Consume stays on `overdubSourceView`. Do not copy `PresentNoteVec`. No `length`. Wrap-S ledger catch-up **HITL PASS** [`185831`](../../captures/session_20260818_185831.log). Loop-head storage-0 **HITL PASS** [`203948`](../../captures/session_20260818_203948.log). Clock equal-tick Off-before-On HITL **PASS** [`001021`](../../captures/session_20260819_001021.log). Open-NoteOn cardinality HITL gate open.

**Parked:** `evaluateOccupyOverlap`; wait-STOPPED-for-`lcr,mat`; consume merge; “occupy = ledger” ownership transfer.

[`132806`](../../captures/session_20260818_132806.log): Stage 1c **held**. Do not wait STOPPED for `lcr,mat`. Do not reconstruct on the note.

### 64-bar source-view identity — Stage 1c shipped; `from=span` HITL parked

Membership, length identity, and dirty/save stall **shipped** ([`overdub_participant_64bar_source_view_identity_and_note_off_fill_bugfix.md`](../Plans/overdub_participant_64bar_source_view_identity_and_note_off_fill_bugfix.md)). [`132806`](../../captures/session_20260818_132806.log) Stage 1c held; 64-bar `from=span` not met (PLAYING during `prep`). That HITL is **parked** (DEC-041). Do not wait STOPPED for `lcr,mat`. Do not start consume merge.

### Overdub participant discovery — notes present at S (Phase 4 1-bar PASS)

**Plan:** [`overdub_participant_loop_content_architecture.md`](../Plans/overdub_participant_loop_content_architecture.md)  
**Parent:** [`consumer_window_budget_ownership_architecture.md`](../Plans/consumer_window_budget_ownership_architecture.md)  
**Evidence:** [`213401`](../../captures/session_20260817_213401.log)  
**Owner (shipped occupy):** `Loop::collectOverdubNoteOnParticipantIds` (`forEachActive` on the lane). Not a smaller source window.

**PresentNote** = LCR query snapshot at S (many `NoteId`s; no `endTick`). **ActiveNote** = open `ActiveNoteLedger::Entry` (several per `(channel, pitch)`). MIDI out = `midiHandler.sendMidiEvent`. Identity field: `noteId` on `Entry`; do not copy `PresentNoteVec`.

**Phase 4 1-bar HITL PASS** [`123803`](../../captures/session_20260818_123803.log): `collectConsumeWindow` skips `ensureOverdubSourceNotesForHold` when `loopLen <= overdubSourceWindowLengthTicks()`. Track 6 (768): **`why=hold` = 0**; occupy 102/130 `from=prep` `a=1,b=1` `eq=1`; consume still Hide (`hide` 3–11). Track 0 (50688): 36 `why=hold` remain (1 `merged=1`); consume still Add/Hide (`empty_sets=0`). `late_clk=0`. Native `test_note_off_skips_hold_fill_when_source_view_covers_loop`.

**Parked — 64-bar occupy via full-loop `from=span`:** [`122848`](../../captures/session_20260818_122848.log) `a=1,b=0` was RC1 (fixed). [`125542`](../../captures/session_20260818_125542.log) `a=1,b=0` = 0. Wait-for-`lcr,mat` **parked** ([DEC-041](../DECISION_LOG.md#dec-041-occupy-present-at-s-jit-not-full-loop-lcr-mat)). Empty occupy is **not** “no participants.” Consume stays on `overdubSourceView`. Do not start consume merge.

**Phase 3 1-bar HITL PASS** [`121933`](../../captures/session_20260818_121933.log): occupied 48/74 `a=1,b=1`; no note-on `why=hold`. Production occupy is now `Entry.noteId`.

Phase 0b **done:** `PresentNote` alone is not enough for RC8 LinearSpan (`endTick` dropped; present on `NoteSpan` / `DisplayNote`). Observation B now walks prepared `NoteSpan`s with `displayNotePresentAtHold` (same function A uses). `notePresentAt` is unchanged (playback / checkpoint fill). Prepared miss still returns false; no cold `resolveState` on note-on. PLAYING / STOPPED / MUTED participant HITL **skipped** ([DEC-040](../DECISION_LOG.md#dec-040-skip-playingstoppedmuted-overdub-participant-hitl)): overdub only runs in OVERDUBBING.

Phase 1 **device capture** [`002447`](../../captures/session_20260818_002447.log) (`b9b9336`): 1-bar (768). 31 `lcr,part`. **`eq=0` = 0.** After wrap 1 and wrap 2, occupied holds are `a=1,b=1` (`ao=0`,`bo=0`). [`232510`](../../captures/session_20260817_232510.log) wrap-2/3 `b=k` extras are gone. Enter before first wrap is still `from=miss` (6). Hold fill still `from=win` on that capture. Production occupy is now present-at-S (Phase 3).

**Phase 2a** `76623cd` + **O:** `a384a47`. Device [`011009`](../../captures/session_20260818_011009.log) **PASS** for wrap-undo source-view refresh: `why=undo` on peel, post-undo A `a=1`, hold `merged=0`. **Phase 2 B collect** uses RC8 hold on prepared spans. Production consume still A.

**013327 wrap-crossing fill shipped** (native A==B). Device [`021716`](../../captures/session_20260818_021716.log) pre-wrap `a=0,b=2` at storage 64 is finished opens on prepared `NoteSpan`s — MIDI reconstruct (`finishOpenNotes=false`) never produced them. **Source-view span fill** `b94bd2b` ([`overdub_participant_source_view_span_membership_bugfix.md`](../Plans/overdub_participant_source_view_span_membership_bugfix.md)). Device [`024225`](../../captures/session_20260818_024225.log) empty `from=span,notes=0` wiped RC12 display. **Display wipe PASS** [`025337`](../../captures/session_20260818_025337.log): `why=open,from=win,notes=1`; `DISP` `PLAYING` 1 → `OVERDUBBING` 1; prepared `eq=0` = 0. [`025916`](../../captures/session_20260818_025916.log) / [`030219`](../../captures/session_20260818_030219.log) hit `COORD` storage 64 **before** first wrap, but those holds are `from=miss,a=0` on a 1-note loop (`notes=1`); 030219 pitches are 86/60 (021716's pitches). After wrap, storage 64 is `eq=1`. **030219 RC12 flood:** wrap 1 `from=span,ev=8,notes=34` vs `vch notes=4` (no `lcr,mat` this capture). Span copy now keeps only window NoteOn ids. **RC12 flood PASS** [`030958`](../../captures/session_20260818_030958.log) / [`032228`](../../captures/session_20260818_032228.log): wrap `lcr,src notes` equals `vch notes` (032228 all 7 wraps delta 0). Hide-all-spans native `7760811`. 032228 wrap 6–7 `bo=1` is B overcount of Disabled wrap-layer companions; B collect now skips index orphans, Disabled-pass companion restore, and Disabled restore when an Active companion already targets that id. Occupy stays A. Device [`034455`](../../captures/session_20260818_034455.log) **PASS**: 64 `from=prep` `eq=1`, **`eq=0` = 0** after undo then more wraps. Wrap 1-frame undo flash: span copy now uses the same Disabled-companion restore guards as B collect; session undo/redo invalidates the live display cache. Device [`040236`](../../captures/session_20260818_040236.log) **PASS** (flicker gone; 103 `from=prep` `eq=1`, `eq=0` = 0). 021716 storage-64-before-wrap is **not** a blocking gate. **Phase 3 in tree:** note-on occupy is `Loop::collectOverdubNoteOnParticipantIds` — prepared present-at-S when ready, else the source-view walk. `Track::snapshotOverlapHoldCandidates` no longer calls `ensureOverdubSourceNotesForHold`. CAP `a` is source-view without fill; production occupy is `b` when `from=prep`. Native: `test_note_on_occupy_*` (64-bar NOTE ON outside the 16-bar window still occupies from prepared spans; miss does not fill). Do not delete `overdubSourceView`. Do not start the Experiment 1 1/2/4/8/16 rebuild series as production policy. NOTE_EDIT overlap is a sibling consumer.

### Consumer window budget — Experiment 1 (detach landed; series not next)

**Plan:** [`consumer_window_budget_ownership_architecture.md`](../Plans/consumer_window_budget_ownership_architecture.md)  
**Owner:** `Loop::kOverdubSourceWindowBars` / `overdubSourceWindowLengthTicks`. Default **16**. CAP `lcr,src` includes `bars=`.

Detach from display 16 is in tree. A timing-passing clamp is evidence, not policy. Successor is notes present at tick `S` (above), not a smaller geometric window. Do not shrink 2-bar gather.

### Playback gather Stage 1 — MIDI deadline lateness hooks (measurement landed)

**Plan:** [`playback_gather_lcr_consume_enhancement.md`](../Plans/playback_gather_lcr_consume_enhancement.md)  
**Owner:** `RuntimeTimingTelemetry`. No geometry change. No LCR consume.

Hot path accumulates `late_on` / `late_off` / `late_clk` (on time = sent before the next tick / one-tick clock window). ISR stores stay in ITCM (`noteClockPulse` region). Main loop `maybeEmit` drains a first-late one-shot (`DIAG,late_event`) and a gather rebuild one-shot (`DIAG,playback_build`), then the 5 s Tier-A `DIAG,late_*` window. No per-event `#CAP`. Native 1329/1329. `teensy41-capture-serial` links (RAM1 code 425852, locals 4768).

[`213401`](../../captures/session_20260817_213401.log): 2-bar `playback_build` 3–6 ms; 64-bar stall is source/hold on USB. Do not start Stage 2 stamp redesign, Problem B length, or LCR consume from this file.

### Display undo / wrap / pitch-move ghosts (RC-W1, RC-N1, RC-U1)

**Plan:** [`note_edit_display_undo_overdub_wrap_bugfix.md`](../Plans/note_edit_display_undo_overdub_wrap_bugfix.md)  
**Evidence:** [`144703`](../../captures/session_20260817_144703.log), [`144939`](../../captures/session_20260817_144939.log)

**Firmware committed** (`f0b0e66` / `142b95b` / `324ffdd` / `4aabf1c`). [`152627`](../../captures/session_20260817_152627.log): RC-W1 / RC-U1 **HITL PASS**. [`155450`](../../captures/session_20260817_155450.log): NOTE_EDIT exit `slice_clean`/`DFRAME` **5** (not [`153213`](../../captures/session_20260817_153213.log) `DISP 8`). [`153213`](../../captures/session_20260817_153213.log) exit 8 was stacked persist `60@64` (consume), not wrap live-cache.

Wrap invalidates live display cache; pitch commit retires persist-twin overlay and home pitch in `visualCache`; undo/redo uses idle visual-cache slices (`refreshVisualCacheAfterPassStateChange`). Do not reopen RC11/RC12. Do not change DEC-038.2 session undo grain.

### Wrap-crossing overdub consume — **shipped** (on device)

**Plan:** [`overdub_wrap_crossing_hold_head_consume_bugfix.md`](../Plans/overdub_wrap_crossing_hold_head_consume_bugfix.md)  
**Firmware:** `e1f57c5`. **HITL PASS** [`155450`](../../captures/session_20260817_155450.log).

One wrap-crossing incoming note occupies `[S, L) ∪ [0, E)` as a single hold. NOTE_EDIT tick 64 is `1/2` (86 + `60@64–240`); [`153213`](../../captures/session_20260817_153213.log) was `1/3`. `overlap_hold` `hide=1` `add=1`. Do not reopen RC11/RC12. Do not patch `applyNoteEditPass`.

### Loop length during overdub (queued — do not start firmware)

**Plan:** [`overdub_loop_length_during_overdub_enhancement.md`](../Plans/overdub_loop_length_during_overdub_enhancement.md)  
**Evidence:** [`140355`](../../captures/session_20260817_140355.log) @ 25.541 s — LOOP_EDIT CC `ch=15 cc=2 value=80` set 768 → 62208 ticks during overdub; MIDI length feedback lagged until ~38 s.  
**Parent authority:** [`overdub_lifecycle_representation_authority.md`](../Plans/overdub_lifecycle_representation_authority.md)

Do **not** start Stage 1–4 firmware until this file is explicitly in implementation (user approved the plan + tasks only). Do not reopen RC11 consume or RC12 `appendOverdubPassDisplayNotes` skip.

**RC11/RC12 FROZEN:** [`overdub_overlap_hold_display_cache_bugfix.md`](../Plans/overdub_overlap_hold_display_cache_bugfix.md) — HITL Gate 1–4 PASS [`140355`](../../captures/session_20260817_140355.log). Native 1294/1294. Commits `2e8f480` / `e1ebcbb`.

**RC10:** live capture paint applies pending Hide/Shorten on a copy.  
**RC9:** overdub transport stop finalizes pending before `sendAllNotesOff`.  
**RC8–RC6:** see parent [`overdub_overlap_hold_same_start_bugfix.md`](../Plans/overdub_overlap_hold_same_start_bugfix.md).

### NOTE_EDIT UNDO_WARM + commit-recon (investigation)

**Plan:** [`note_edit_undo_warm_missing_recon_investigation.md`](../Plans/note_edit_undo_warm_missing_recon_investigation.md)  
**Evidence:** [`143144`](../../captures/session_20260816_143144.log), [`145518`](../../captures/session_20260816_145518.log)

**Parked — wrap-move persist:** [`201446`](../../captures/session_20260816_201446.log) live wrap-move is linear (`EditSessionAction` 2832–3408, `DNTE` length **576**). Deselect persist LIFO-pairs false Off@2688 and leaves wrap Off@96; reselect `DNTE` length **336**. That failure is partly the current rematerialize / full-loop session-store structure. Do **not** add more LIFO / wrap-off persist patches in that structure. Re-evaluate after NOTE_EDIT hydrate if the 336/288 shorten remains. Do not patch `applyNoteEditPass` identity. Stage 2 / C5 / B2b / Layer D parked.

**Next Layer C (parked C5):** `OverlapCandidateLookup::appendNotesForIds` in overlay only — not overdub source-view / hold ids. Not reconstruct. Not empty-pair resolve.

### DEC-037 — LoopContentResolution parallel prototype

**Parked:** overlay loop picker (`set-revision-persistence` §4.8–4.10) — WIP stashed on `feature/set-revision-loop-picker`.

**Active:** native `LoopContentResolution` prototype. Do **not** optimize `materializeToEventVector` again. Do not wire resolution onto MIDI/display until three gates pass.

**Plan:** [`loop_event_sourced_resolution_architecture.md`](../Plans/loop_event_sourced_resolution_architecture.md)  
**6D investigation:** [`loop_content_resolution_incremental_commit_maintenance_refinement.md`](../Plans/loop_content_resolution_incremental_commit_maintenance_refinement.md)  
**6E overdub evaluation:** [`loop_content_resolution_overdub_state_evaluation_refinement.md`](../Plans/loop_content_resolution_overdub_state_evaluation_refinement.md) — **6E.1–6E.5 PASS**; [DEC-038](../DECISION_LOG.md#dec-038-overdub-wrap-commit-and-session-undo) **038.1 HITL PASS**; **038.2 landed** (one `OverdubPassAdded` `passIds` + STK3).  
**Handoff:** [`loop_content_resolution_stage9_handoff.md`](../Plans/loop_content_resolution_stage9_handoff.md)  
**Authority:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype)

**Now:** LED lookup Stage 1 **PASS** [`114736`](../../captures/session_20260816_114736.log) — [`post_undo_led_lookup_resumable_source_refinement.md`](../Plans/post_undo_led_lookup_resumable_source_refinement.md). No LED gather rem; BAR→LED 3.7 ms; PLAYING `clockrate` 47–48. Remaining PLAYING `midi_gap` 110–127 ms is idle/load, not lookup. Stage 2 **rejected**. Do **not** re-arm drain. Playback gather is a separate work path ([`playback_gather_lcr_consume_enhancement.md`](../Plans/playback_gather_lcr_consume_enhancement.md)) — do not start from this LED slice.

**Scheduler prep:** [`runtime_scheduler_lcr_consumer_grooming_refinement.md`](../Plans/runtime_scheduler_lcr_consumer_grooming_refinement.md). **Slice 1–4c device PASS.** Slice 4d firmware landed; [`143144`](../../captures/session_20260816_143144.log) moved NOTE_EDIT session work to the investigation above. **Slice 1b device PASS [`225626`](../../captures/session_20260816_225626.log)** — dirty PLAYING `midi_gap` = `idle_maint` 79–96 ms; child rem is `idle_append` only (72–82% of parent). **Slice 2b device PASS [`232423`](../../captures/session_20260816_232423.log)** — unprepared interior idle slices skip `appendOverdubPassDisplayNotes`; wrap-edge (bar 0 / last bar) still appends. 6B stops 3–7: no `idle_append` rem, `midi_gap` 24–32 ms, `clockrate` 47. **Slice 2c device PASS [`233323`](../../captures/session_20260816_233323.log)** — `stale_range` `dcnt` 4 / 5 / **1** / 4; cache notes rise; coverage `0–63`. Do not start 4e. Do not grain idle. Do not fold NOTE_EDIT hydrate into grooming — that is its own work path below. Do not optimize `LoadLoopJob` from PLAYING paint.

**FinalizeWorkspace slice** shipped (`48bd36f`). **LoopPersist finalize** relanded — boot **PASS** [`213246`](../../captures/session_20260816_213246.log); one PLAYING `persist_save` rem **206 ms** @ 25.683 s (later jobs no rem ≥ 50 ms).

**Parked:** 915 ms boot `load_frame` [`032803`](../../captures/session_20260816_032803.log) — `commitLoadLoopJobPublish` / `runDeferredLoadAndDisplayFrame`; not a freeze; not the revert.

**6A.1 HITL PASS** [`025651`](../../captures/session_20260816_025651.log) `match=1` `pmatch=1`. STOPPED `midi_gap` **72 ms** at LCR complete is a different sample (`6a,nat` 30 ms + `loop_rem,idle_maint` 58 ms); after that, max **13.5 ms**.

**Does not start:** Track A overlay, Stage 3b GUS replacement, interval reservation, RC-J patches, deleting `materializeToEventVector`. PLAYING `midi_gap` in [`192334`](../../captures/session_20260815_192334.log) was FinalizeWorkspace (sliced). Post-stop gap owner is idle visual cache, not LCR. NOTE_EDIT hydrate (below). Playback gather ([`playback_gather_lcr_consume_enhancement.md`](../Plans/playback_gather_lcr_consume_enhancement.md)).

### NOTE_EDIT hydrate (queued — own work path)

**Work identity:** [`note_edit_hydrate_enhancement.md`](../Plans/note_edit_hydrate_enhancement.md)  
**Architecture:** [`note_edit_selectedtick_lcr_resolution_architecture.md`](../Plans/note_edit_selectedtick_lcr_resolution_architecture.md) — DEC-037 amendment 2026-08-16; overlap retarget 2026-08-17; architecture **PASS**; stages **PASS WITH AMENDMENTS**.  
**Sibling:** [`overdub_participant_loop_content_architecture.md`](../Plans/overdub_participant_loop_content_architecture.md) §5 NOTE_EDIT sibling.

Overlap uses the **same participant query as overdub**, keyed by **selected/mover LinearSpan**, not a window around `selectedTick`. Select stays neighborhood navigation (`tickEvents` / `spanBoundaries` around `selectedTick`), not `resolveState`. Paint stays `visualCache` + `NoteEditCurrentState`. Stages 4a/4b/4c split.

Do **not** start firmware from “prepared LCR around `selectedTick`.” That mapping is withdrawn for overlap.

**Not** remaining `loop-content-resolution` 6.4 firmware. **Not** grooming Slice 5. **Not** `lazy-slot-hydration`. **Not** a resumable open-until-ready session. **Not** playback gather ([`playback_gather_lcr_consume_enhancement.md`](../Plans/playback_gather_lcr_consume_enhancement.md)).

**6C/6D for this consumer:** 6C consume-when-ready **closed** [`205928`](../../captures/session_20260815_205928.log) / [`210508`](../../captures/session_20260815_210508.log) (3b stays). 6D.4 **HITL PASS** same captures; not all of LCR live.

Wrap-move persist is **parked** (current-structure issue) — it is not a start gate. Do not start firmware until this file is in § Now implementing. Do not full-replace `sessionMidiEvents()` for audition. Do not resume wrap-move persist patches from this path.

### Playback gather (Stage 1 hooks — in Now implementing)

**Work identity:** [`playback_gather_lcr_consume_enhancement.md`](../Plans/playback_gather_lcr_consume_enhancement.md) — DEC-037 amendment 2026-08-17. Stage 1 measurement hooks landed (accumulators + rare one-shots; no per-event serial). Stage 2–3 not started. 2-bar geometry unchanged. Does not start Owner-Boundary Gate / interval reservation. **Not** remaining OpenSpec 6.3 firmware. **Not** hydrate. **Not** LED lookup.

### DEC-036 Layer D 3b — overdub entry without display reconstruct (shipped)

**Plan:** [`loop_layer_d_overdub_rebuild_architecture.md`](../Plans/loop_layer_d_overdub_rebuild_architecture.md)

**D0:** **PASS** [`035414`](../captures/session_20260814_035414.log) — 6.78 s `begin_capture`; source-view problem, not overdub FSM.  
**Device FAIL [`042909`](../captures/session_20260814_042909.log):** undo **14.3 s / 14.6 s** (`VCACHE,full`); overdub **7.1 s**.  
**Device PASS [`045556`](../captures/session_20260814_045556.log):** `slice_clean` 1809 notes then overdub `begin_capture` **2214 µs**.  
**Device PASS [`112909`](../captures/session_20260814_112909.log):** undo **3 ms** (`MIDI: Undo` 143.169 → `Overdub undone` 143.172, `kind=3`); no `VCACHE,full`. Boot `load_frame` ~9.9 s remains D3/D4.  
**OpenSpec:** [`loop-effective-event-source`](../../openspec/changes/loop-effective-event-source/) closeout **4.1/4.2**. Successor: DEC-037 (post-commit rebuild / pass-list walks).

**Does not start:** Track A overlay, Stage 3b GUS replacement, interval reservation, RC-J patches.

### NOTE_EDIT on lengthened loop + overdub entry (shipped this session)

**Fix 1:** `openNoteEditSession` stops active overdub (`stopOverdubbing` → PLAYING) before rematerialize so live capture is committed and editable. **RC2:** always `rebuildVisualCacheFromPasses` on NOTE_EDIT open. Short-loop overdub-stop full rebuild removed in grooming Slice 4 — keep existing notes (`020910` / `021959` stale was adopt_partial).

**Fix 2:** Lengthened loop (4-bar loop, 2-bar content) — NOTE_EDIT select skips detailed-window note filter; bracket clamps to committed content span; select nav slots trimmed past content; nav length extends to painted note tail when overdub exceeds bar-aligned content. Fixture: [`015731`](../../captures/session_20260814_015731.log).

**Fix 3 (Stage 2) device PASS [`024428`](../../captures/session_20260814_024428.log):** after reboot, selected slot 5 undoes immediately — `Undo (entries=17)` kind=4 `NoteEditPassClosed`, then `Undo (entries=16)` kind=1 `OverdubPassAdded`. `rebuildSlotFromLoopContent` keeps the tip on the selected slot so later LoadLoopJob restores do not steal it ([`024004`](../../captures/session_20260814_024004.log) was the FAIL).

### Overdub lost on NOTE_EDIT exit — device PASS

**Device PASS [`025322`](../../captures/session_20260814_025322.log):** overdub → NOTE_EDIT (`visual_notes=102`, `session_events=234`) → pitch/range on note 194 → exit `NoteEditPass replaced … rows=1 saved=1` (not 19 Deletes). Commit flats stay 234. After later overdub, STOPPED `DISP` **103** — not the boot record-only **91**. Contrast [`014553`](../../captures/session_20260814_014553.log) `rows=19` / `DISP` 48→32.

### OLED first-frame mismatch — RC3 device PASS

**RC3 PASS [`014553`](../../captures/session_20260814_014553.log)** — dcache flush before SPI + STOPPED boot restore paints. Plan: [`oled_dma_partial_frame_bugfix.md`](../Plans/oled_dma_partial_frame_bugfix.md).

### Loop content-only history (DEC-035 Layer A) — archived

**Shipped + archived 2026-08-14.** Stage 3 + follow-up device PASS [`030147`](../../captures/session_20260814_030147.log) / [`032227`](../../captures/session_20260814_032227.log). OpenSpec `openspec/changes/archive/2026-08-14-loop-content-history-persistence/`; normative `openspec/specs/loop-content-history/`. Do not start Stage 3b without new DEC.

Plan: [`loop_layer_history_persistence_architecture.md`](../Plans/loop_layer_history_persistence_architecture.md). Task [#33](https://github.com/Lytrix/MidiLooper/issues/33). Bug [#32](https://github.com/Lytrix/MidiLooper/issues/32) stall closed.

### Overdub-stop MIDI dump during PLAYING

LoadLoopJob PLAYING skip is device-proven in [`105505`](../../captures/session_20260813_105505.log). `UndoStacks` stall closed Stage 3 [`030147`](../../captures/session_20260814_030147.log). SlotMeta bundle on overdub stop closed follow-up [`032227`](../../captures/session_20260814_032227.log) — `LoopPersist` only ~139 ms. Scheduling: [`runtime_scheduling_owner_boundary_admission_refinement.md`](../Plans/runtime_scheduling_owner_boundary_admission_refinement.md). Do not patch RC-J.

### Real-time incremental work (RECORD/OVERDUB) — post–RC-C + S0 timing telemetry

**Now: S0e follow-through RC-K1–K3 shipped and device-verified.** Targeted fix of the overdub note-off cost attributed by S0e. Do not implement interval reservation, add extra `handleMidiInput()` call sites, patch RC-J, or chase overdub display frame-skip. Plan: [`realtime_incremental_work_overdub_note_change_bugfix.md`](../Plans/realtime_incremental_work_overdub_note_change_bugfix.md). Investigation [§31n](../Plans/archive/refinements/runtime_scheduling_timing_envelope_investigation.md#31n-s0e--split-overdub-note-off-path-observation-only) / [§31o](../Plans/archive/refinements/runtime_scheduling_timing_envelope_investigation.md#31o-s0e-follow-through--rc-k1--rc-k2--rc-k3). Evidence: [`204221`](../../captures/session_20260812_204221.log) (`notechg` 274 ms = `noterecon` 177 ms + `notepair` 98 ms).

**Baseline (proof of current stability):** [`191356`](../../captures/session_20260812_191356.log) on `752273d` (RC-H reverted). Multiple overdubs including overdub-over-overdub; display did not freeze (`slice_clean` covers the whole loop after every stop; final STOPPED paints the 16-bar window of a clean 1482-note cache). `clockrate` held **47–48** through PLAYING/OVERDUB (no dropped-clock / half-tempo). `midisvc` did **not** hold: 99–133 ms, then **218–221 ms** on the long overdub, 110–125 ms on later overdubs, against `clk`/`tracks` 4–10 ms. RECORD stays 0.3–0.8 ms. PLAYING↔OVERDUB `msi` spikes 237–433 ms. Post-stop clock drop is RC-J (48→24→0 and 48→36→0) — Owner-Boundary Gate, not S0b.

**S0e exit:** attributed in [`204221`](../../captures/session_20260812_204221.log) — `usbnote` is `notechg`; `noterecon` 177 ms is quadratic reconstruct dedup; `notepair` 98 ms is the per-candidate `noteIdHasChannel` scan. `noteappend` is 59 µs.

**S1 RC-K1 (shipped):** `reconstructNotesImpl` tracks seen `(note, startTick, endTick)` in an ordered set instead of `std::any_of` over the accepted list. Same key, same insertion order. Native `test_noteutils_reconstruct` PASS including many-identical-geometry collapse.

**S1 RC-K1b (shipped):** the ordered set allocated one PSRAM tree node per note. Boot visual-cache rebuild of 1430 notes measured 1.38 s ([`215128`](../../captures/session_20260812_215128.log) / [`215357`](../../captures/session_20260812_215357.log)). Dedup now ranks into one vector, `qsort`+unique, restore first-seen order. Native `test_noteutils_reconstruct` PASS. `teensy41-capture-serial` fits RAM1.

**S1 RC-K2 (shipped):** `accumulatePendingNoteChangesForIncomingNote` no longer scans source events for channel. Loop notes are loop-scoped (DEC-033). Native `test_pending_shorten_ignores_recorded_channel` PASS.

**S1 RC-K3 (shipped):** `overdubSourceViewNotes_` is reconstructed once in `establishOverdubSourceView` and cleared in `clearOverdubSourceView`. Note-off reads the member. Native `test_overdub_source_view` PASS.

**S1 RC-K1–K3 verified [`223033`](../../captures/session_20260812_223033.log):** the note-off gate is met — `noterecon` 0 in every window, `notechg` and `notepair` peak at 3.6 ms then hold 0.75–2.0 ms (were 274 / 98 ms).

**S1 RC-L1 (shipped, verified [`225803`](../../captures/session_20260812_225803.log)):** per-span `ProjectedIntervalVec` allocation in `projectDisplayNotes` / `projectNoteIntervals`. Four overdubs on a grown 64-bar loop: `begin_capture` 77 / 80 / 79 / 83 ms (was 1.378 s, and 58 → 410 ms as passes accumulated in [`223033`](../../captures/session_20260812_223033.log)). First USB note 108–242 ms after PLAYING→OVERDUBBING (was 1.76 s). `noterecon` 0; `notechg`/`notepair` 1.09 ms; `usbnote` 1.20 ms; overdub `clockrate` 47–48; overdub `midisvc` 4–14 ms. Remaining ~80 ms is the synchronous gather+reconstruct floor.

**Shipped (on `feature/overdub-participant-discovery`):** playback-observation overlap gates 0–4 + wrap-crossing consume `e1f57c5`. Plan: [`overdub_playback_observation_overlap_refinement.md`](../Plans/overdub_playback_observation_overlap_refinement.md). `PendingNote.overlapNoteIds` collection is wired. Note-off consumes the set via `appendNotesForIds` on `overdubSourceViewNotes_`, then geometry + `[S, E)`. Empty set is Add only. PLAYING idle prebuild reverted (`73f0489`). Option B stays withdrawn ([`021304`](../../captures/session_20260813_021304.log) 292 ms/note-off). Collection-wired overdub in [`154823`](../../captures/session_20260813_154823.log): `noterecon=0`, `notechg=2993`; 11.7 s post-stop stall is `LoopUndoHistory`, not this path.

**Gate 0:** `OverlapNoteIdSet` fixed capacity 128; overflow does not grow. Native PASS. Idle `stored_notes` measured in [`152940`](../../captures/session_20260813_152940.log): track 0 slot 4 (68 bars) `notes=1903 unique=1903 max_same_pitch=322`; track 6 `max_same_pitch=195`. Both exceed 128. Do not raise capacity without a decision. This diagnosis taxes the MIDI event runtime. Gate 0 count is recorded in [`152940`](../../captures/session_20260813_152940.log). Keep `maybeLogStoredNoteCount` as the total-notes / `max_same_pitch` inventory.

**Gate 1:** Production selection is normalized note geometry + `[S, E)` intersection (`existingNoteOverlapsIncomingHold`). `OverlapNoteIdObservation` is test/diagnostic only. Split-chunk and prior Shorten/Hide companion fixtures landed. 021304 same-pitch count still open on Gate 0.

**Gate 2 (native landed, device open):** enabled slots keep advancing playback; mute/solo/slot-mute suppress send on that track's MIDI channel and output ports (USB/DIN/USB Host). Track mute sends CC 123 on `midiChannel` only. Native proves cursor/wrap advance while MIDI send is suppressed, unmute does not resend crossed events, and mute does not clear the ledger. Device still owed: mute mid-note silences that channel on the output ports.

**Gate 3 (native landed):** empty candidate set does not look up spans and does not call `gatherCommittedEvents` / `reconstructDisplayNotes` on note-off. `OverlapCandidateLookup` copies from an already-available note list only. RC-K3 note-off still reads `overdubSourceViewNotes_`. `fullMaterializeCount` stays 0 after the work counter is reset, including when companion edit rows exist.

**Gate 4 (native landed):** `appendNotesForIds` is one pass over the available `DisplayNote` list and stops at the last matching id. It does not call `findLinearNoteSpanForNoteId` and does not build an index. 3714-note fixture with 3 ids examines 30 notes, not 3714×3.

**Hold-candidate collection (wired):** `PendingNote.overlapNoteIds` snapshots already-sounding same-pitch ids at incoming note-on and inserts playback note-on ids while the hold is open. Offs do not erase. Native: `test_overlap_hold_candidates`.

**Note-off consumes overlapNoteIds (wired):** `accumulatePendingNoteChangesForIncomingNote` looks up the set in `overdubSourceViewNotes_` (`appendNotesForIds`) and applies geometry + `[S, E)`. Empty set skips lookup (Add only; Gate 3). Native: `test_pending_note_change`.

**Wrap-crossing hold consume (head + tail, one hold) — shipped `e1f57c5`, HITL PASS [`155450`](../../captures/session_20260817_155450.log):** wrap-head `[0, E)` is the same incoming hold as the tail `[S, loopLength)`, not a second note-off. [`170449`](../../captures/session_20260813_170449.log) `hide=14` was the double-hold. NOTE_EDIT tick 64 is one 60 plus other pitches (`1/2`), not [`153213`](../../captures/session_20260817_153213.log) `1/3`. Plan: [`overdub_wrap_crossing_hold_head_consume_bugfix.md`](../Plans/overdub_wrap_crossing_hold_head_consume_bugfix.md).

**NOTE_EDIT mover wrap-length jump (RC1 device PASS in [`200154`](../../captures/session_20260813_200154.log)):** no `2351` / `2975`. Mover **22** `DNTE` stays **95** while overlap runs on many neighbors. Note **14** `ChangeLength` `2256–2304` stays on the wrap-stub plan. Plan: [`note_edit_mover_wrap_length_jump_bugfix.md`](../Plans/note_edit_mover_wrap_length_jump_bugfix.md).

**NOTE_EDIT leave-restore painted span (RC1 native shipped; [`201948`](../../captures/session_20260813_201948.log) device):** 76 Hide/Restore is `840–863` (not 2160). Note **5** first-select is already `DNTE` **1535** — cache span is `720–2255`, not painted `720–767`. Stop on a second owner here. Mover **100** length stays **144**; 39.397 commit still saves only 14 `2256–2304` (wrap-stub plan). Plan: [`note_edit_overlap_leave_restore_painted_span_bugfix.md`](../Plans/note_edit_overlap_leave_restore_painted_span_bugfix.md).

**NOTE_EDIT note-off pairing LIFO (native shipped; [`213920`](../../captures/session_20260813_213920.log) multi-overlap device PASS):** @45.971 seals `canonical=7 pre_commit=7` — Delete 4/3/2/115, Length 1 `0–95`, mover 77 `96–788` pitch 60. Cache 22→18. [`213533`](../../captures/session_20260813_213533.log) @104.240 emitted the same five actions, then left lane 60 before deselect so the @108.711 commit carried only the mover rows. Plan: [`note_edit_note_off_pairing_lifo_bugfix.md`](../Plans/note_edit_note_off_pairing_lifo_bugfix.md).

**NOTE_EDIT edit-pass replay row payload (native shipped; [`211832`](../../captures/session_20260813_211832.log) device PASS):** `Length 115 48→287` @35.640 and `Length 115 1008→1103` @55.510 both seal and hold. Replay no longer rewrites a later row's span from an earlier row for the same `targetNoteId`. Plan: [`note_edit_replay_row_payload_bugfix.md`](../Plans/note_edit_replay_row_payload_bugfix.md).

**NOTE_EDIT overlap shorten commit seal (native shipped; device gate open):** the deselect commit dropped the overlap `Length` row because `participatingNoteVisibleOverlapTailInProgress` fired on a parked mover, so the shorten either vanished (display reverted) or landed one commit late against an unrelated focus ([`204700`](../../captures/session_20260813_204700.log) @166.809 `canonical=1 apply_owned=2` → @166.848 `Length 10 960–1247`). Skip removed from `buildCommitOverlapRowsFromCurrentState`; predicate stays in leave-restore. Slice B of the resolver contracts plan is **withdrawn** — [`225025`](../../captures/session_20260807_225025.log) `len=287` was correct. Decisions: seal at the user-triggered commit; host tail stays truncated. Plan: [`note_edit_overlap_shorten_commit_seal_bugfix.md`](../Plans/note_edit_overlap_shorten_commit_seal_bugfix.md).

**NOTE_EDIT Length replay loop-boundary (native shipped; device gate open):** persisted `ChangeLength` 14 `2256–2304` was replayed as a wrap; same-pitch notes shortened to 2255. Owner: `applyChangeLengthById`. Plan: [`note_edit_length_replay_loop_boundary_bugfix.md`](../Plans/note_edit_length_replay_loop_boundary_bugfix.md). Do not fold note 5 cache pairing. No note ends at 2255 in [`204700`](../../captures/session_20260813_204700.log) / [`205054`](../../captures/session_20260813_205054.log); the wrap stub paints `DNTE,71,2256,…,48`.

**NOTE_EDIT wrap-stub commit (RC3 native shipped; device gate open):** RC2 device FAIL in [`193838`](../../captures/session_20260813_193838.log) / [`201948`](../../captures/session_20260813_201948.log) — 14 still `ChangeLength` `2256–2304` because cache has a non-zero span. RC3 skips loop-end Length unless painted end is `loopLength`. Plan: [`note_edit_overlap_action_drop_and_wrap_stub_bugfix.md`](../Plans/note_edit_overlap_action_drop_and_wrap_stub_bugfix.md). Do not fold note 5 cache pairing.

**NOTE_EDIT / LOOP_EDIT display split (Stages 8–9 shipped; [`192007`](../../captures/session_20260813_192007.log) device):** 3-bar / 60-note loop (not 181114’s 64). First NOTE_EDIT `DISP` **59/60**. Hide/Restore of **45** is `1440–1511` (not 2160). **76** as overlap target still Hide/Restore `840–2160`. No MoveNote of 76; select at tick 840 rebuilds 76 (no 168 reset). `GEOM_APPLY,resolve` 16.8–52.3 ms. Paint gap and undo-warm stay open. Plan: [`note_edit_visual_cache_display_unification_refinement.md`](../Plans/note_edit_visual_cache_display_unification_refinement.md).

**Withdrawn-path cleanup (removed):** Option A slice tests, Option B windowed matrix, `gatherOverdubSourceView*InWindow`, and `gatherCommittedNoteEventsForPitch`. `maybeLogStoredNoteCount` kept.

**RC-L2 (withdrawn helper removed):** `gatherCommittedNoteEventsForPitch` is gone. Production note-off uses `overlapNoteIds` + `overdubSourceViewNotes_`. `test_overdub_source_view` now covers establish/clear only.

**RC-L3 (shipped, HITL verify open) — stored-MIDI verification off the stop path:** `emitStoredMidiVerification` ran inside MIDI button dispatch at overdub stop and cost 342.7 ms / 325.5 ms in [`013917`](../../captures/session_20260813_013917.log) (flush itself under 1 ms): a full `mergeActiveCapturePasses`, one `SEVT` line per note event over 3554 events, the capped wrap-pair scan, then a second full `reconstructNotes` for at most 32 `DNTE` lines. The ring overflowed at both stops, so most `SEVT` lines were dropped and the `seal` / `finalize` / `set_state` stage lines were evicted with them. Now `queueDeferredStoredMidiVerification` only sets a flag; `processDeferredStoredMidiVerification` drains from `processDeferredIdleMaintenance` in 64-event slices (16 while `hasDeferredSaveWork`), flattening once on the first slice and phasing notes → wrap pairs → display notes. Functions are `TRACK_COLD_MEM` — in ITCM they pushed RAM1 past a 32 KB block boundary.

**RC-L3b (shipped, device verify open) — one-shot + idle-only drain:** deferring during PLAYING still blocked MIDI for seconds because the first slice does a full merge ([`020631`](../../captures/session_20260813_020631.log) `msi` 2.95 s). Verification is now **one overdub stop per boot** (`#CAP,DIAG,stored_verify,armed,0` when disarmed; reboot to re-arm for another HITL evidence run). Drain runs only when transport is fully idle (same gate as REVT), not during PLAYING/OVERDUB. Second+ overdub stops in the same session skip verification so manual retests are not penalized.

**Architecture:** [`realtime_incremental_work_capture_overdub_architecture.md`](../Plans/realtime_incremental_work_capture_overdub_architecture.md)  
**Scheduling contract:** [`runtime_scheduling_admission_model_architecture.md`](../Plans/runtime_scheduling_admission_model_architecture.md)  
**Scheduling roadmap:** [`runtime_scheduling_owner_boundary_admission_refinement.md`](../Plans/runtime_scheduling_owner_boundary_admission_refinement.md) (O–T–R–C–A–P; interval reservation not authorized)  
**S0 (shipped code):** `RuntimeTimingTelemetry` — Tier-A `DIAG,midi_gap` / `midi_input` / `clk` / `tracks` / `clockrate` (5 s); observation only. Historical captures used `DIAG,msi` / `midisvc` for the same two measurements. Native `test_runtime_timing_telemetry` PASS. Playback gather Stage 1 adds `DIAG,late_on` / `late_off` / `late_clk` (5 s) plus `DIAG,late_event` / `DIAG,playback_build` one-shots — not a line per MIDI event. Do not start Stage 2/3 from this scheduling slice.  
**S0 device runs:** [`141815`](../../captures/session_20260812_141815.log), [`144323`](../../captures/session_20260812_144323.log) — DIAG timing lines lost across the whole capture pass; two root causes fixed (see [investigation §31a](../Plans/archive/refinements/runtime_scheduling_timing_envelope_investigation.md#31a-s0-device-runs--first-results-2026-08-12)):
- **RC-S0a** `isTierATextLine` skipped two commas, so Tier-A classification was inert and the ring evicted every DIAG window. Parse extracted to `CaptureLineTier::isTierALine` (`test_capture_line_tier` PASS); Tier-A may now only be displaced by Tier-A.
- **RC-S0b** `MemoryMonitor::logStatus()` external-pool walk blocked the loop **593 ms** and lost external MIDI clock. Walk is now `setup()`-only (`logStatus(true)`); runtime reports `pool_size` (O(1)). Guide exemption removed.

**S0 run [`145555`](../../captures/session_20260812_145555.log):** DIAG timing windows survive through RECORD + both overdubs. `midisvc` 0.9–1.1 ms during RECORD vs **129–149 ms** during PLAYING/OVERDUB (762.5 ms at overdub entry), against `clk`/`tracks` ≤ 8.83 ms — `handleMidiInput()` duration is the dominant term and clock dispatch is not. **RC-S0c:** windows before 177 s were lost because `flushCaptureBuffer` never transmits Tier-A under the timing-critical budget; fix is a bounded Tier-A transmit allowance (roadmap O2 / [investigation §31b](../Plans/archive/refinements/runtime_scheduling_timing_envelope_investigation.md#31b-s0-run-145555--first-capture-phase-envelope)).  
**Regression vs [`9678c3d`](../Plans/archive/refinements/runtime_scheduling_timing_envelope_investigation.md#31d-regression-vs-9678c3d--display-lag-and-transition-feel):** overdub display lag is the new `resolveDisplayNotesLiveCapture` budget bailout — measured resolve is 23–31 µs·10³ against a 5000 µs budget, so `reuseLastValidFrame` holds a stale frame for most of both overdub passes. Transition feel is the Clock dispatch reorder (Clock no longer transport-first).  
**Shipped (observation only):** compose sub-step instrumentation — `DisplayCommittedRebuildTime`, `DisplayCaptureReplaceTime`, `DisplayCaptureSyncTime`, plus the previously dead `DisplayCaptureGatherTime` / `DisplayCaptureFullGather` slots and branch counters `DisplayCommittedWindowFilter` / `DisplayCommittedFullAssign`. Native 1034/1034; `teensy41-capture-serial` builds.  
**S0 run [`152948`](../../captures/session_20260812_152948.log):** resolve attributed — `rebuildDisplayNotesInWindow` 25.7 ms × 22 calls (the spikes) and `filterDisplayNotesByWindowInclusion` 5.48 ms × 622 calls (the sustained over-budget). `replaceCaptureLayer` 0.10 ms, `synchronizeCaptureLayer` 0.01 ms — not factors. See §31e.  
**RC-D (new, outranks the above):** the OLED repaints every loop iteration — ~14 ms frame period against a 30 ms cadence, ~93 % of loop time in `DisplayManager::update()` from boot. `invalidateProjectedNoteEditDisplayCache` is raised by the general `invalidateLiveDisplayCache` path with no note-edit precondition, but `markNoteEditDisplayPainted` only clears it when `isNoteEditActive()`, so the ungated `maybeUpdateDisplayForNoteEditSelection` path latches on permanently.  
**RC-D fix (shipped):** `markNoteEditDisplayPainted()` now runs unconditionally at the end of the normal paint path in `DisplayManager::update`. Note-edit behaviour unchanged (the guard was already true there). Residual: the load/save overlay branch returns before the ack by design, so the latch can still spin while that overlay is open. Native 1034/1034; `teensy41-capture-serial` builds.  
**RC-D verified [`155132`](../../captures/session_20260812_155132.log):** 32.7 fps (was 70–78); display 45 % of loop time (was ~93 %); idle `msi` 15.3 ms (was ~34 ms); RECORD `midisvc` 604–905 µs. See §31f.
**Still open — dominant path (historical):** `midisvc` 126–146 ms through PLAYING/OVERDUB was later attributed S0b–S0e (`usbnote` / `notechg`) and reduced by RC-K1–K3.
**RC-E fix verified [`165636`](../../captures/session_20260812_165636.log):** `adopt_partial` reports partial coverage and idle slices backfill the whole loop in ~0.6 s (359 → 1 352 notes, then 327 → 1 417). Permanent starvation gone.
**RC-F fix shipped:** clean-cache overdub filter now gathers the paint window widened by `kWindowedGatherMarginBars = 2` and records it — the committed layer follows auto-follow instead of freezing. Verified in [`172405`](../../captures/session_20260812_172405.log): `wNotes` holds at 300–313 across the second overdub.
**RC-F follow-up shipped:** the first fix let the staleness predicate fire during the post-commit dirty window, where every rebuild took the 27.9 ms full-gather branch (30 gathers, 70 of 241 frames over budget, tails on 6 of 241) — that was the bar-boundary stutter and the "notes only appear after a bar". The resolve path no longer gathers: while `visualCacheDirty`, the committed layer is held and recovery is left to `processDeferredIdleMaintenance`; `committedWindowStale` requires a clean cache, and `committedLayerCleanCacheReady` rebuilds once when idle finishes. See §31g-3.
**RC-H reverted (RC-I):** the overdub-stop handoff preserve (`liveOverdubStopHandoffActive_`) pinned the partial adopted frame permanently — [`183429`](../../captures/session_20260812_183429.log): `adopt_partial notes=394`, then `slice_clean notes=1105`, but `DFRAME 394` held from 209.6 s through 214.2 s. The 174843 window content was density-correct; static viewport at STOPPED is expected. Reverted to pre-RC-H behaviour; `preservedHandoffAuthority` covers the ~0.6 s dirty window. **Verified [`191356`](../../captures/session_20260812_191356.log):** no freeze across two overdub clusters; idle `slice_clean` covers the loop; STOPPED paints the 16-bar window of the completed cache.
**RC-J (open, Owner-Boundary Gate G6):** post-stop persistence stall — `timingCriticalTrackActive` goes false at STOPPED while the external clock still streams. Reproduced in [`191356`](../../captures/session_20260812_191356.log): first stop `clockrate` 48→24→0 `msi` 467 ms; final stop 48→36→0 `msi` 365 ms. Do not patch ad hoc; do not widen `timingCriticalTrackActive`.
**RC-G fix shipped:** `updateOverviewCaptureDensity` builds a per-bar × 8-pitch-band mask incrementally from the capture preview (O(new notes) per frame), and `drawOverviewStrip` renders O(loop bars) from it when no clean `visualCache` exists. Record overview now shows the whole loop. Device verify pending for both.
**Remaining findings:** S0e attributed in [`204221`](../../captures/session_20260812_204221.log); RC-K1–K3 / RC-L1 device-verified in [`225803`](../../captures/session_20260812_225803.log). RC-S0c still owed (roadmap O2). RC-J is Owner-Boundary G6, not S0b. Display frame-skip / rolling-window during overdub is RC-F follow-up + investigation §31d.
**Overdub display lag:** `DisplayResolveLiveCapture` 33.6 / 33.8 ms during the second overdub against a 5 000 µs budget — the §31d bailout skips `replaceCaptureLayer` and the tails, so live overdub notes are not composed.
**Also open:** display resolve is under budget only by margin — the window filter measures 4 595 µs against a 5 000 µs line, and the gather still peaks at 26.3 ms. A 140 ms post-stop `msi` stall with `midisvc` at ~0.1 ms is uninvestigated.  
**RC-C device:** [`115913`](../../captures/session_20260812_115913.log) — timing PASS; display D1–D2 FAIL.  
**Regression:** [`122003`](../../captures/session_20260812_122003.log) — dual idle slice caused MIDI lag / clock lost (reverted).  
**Now:** Device re-measure after RC-K1b is closed on [`225803`](../../captures/session_20260812_225803.log). Remaining scheduling work is the owner-boundary roadmap, not interval reservation. Plan [`realtime_incremental_work_overdub_note_change_bugfix.md`](../Plans/realtime_incremental_work_overdub_note_change_bugfix.md).

### Long record onset display freeze — [`012342`](../../captures/session_20260812_012342.log)

**Plan:** [`long_record_onset_display_freeze_bugfix.md`](../Plans/long_record_onset_display_freeze_bugfix.md) — RC-A shipped; RC-C promoted via architecture doc above.  
**Evidence:** fail [`012342`](../../captures/session_20260812_012342.log); interim PASS [`013747`](../../captures/session_20260812_013747.log).

### Stage 5 — memory / persistence pressure

**5a (closed):** 5a-1/5a-2 shipped; 5a-3 **`pool_alloc` proof abandoned** (2026-08-12).  
**5b:** boot deferred save + clear gating — **parked**.

### Codebase consistency & maintainability — Phase 4 + LR complete

**GitHub:** [#18](https://github.com/Lytrix/MidiLooper/issues/18) · **Plan:** [`codebase_consistency_phase4_extraction_boundaries_refinement.md`](../Plans/codebase_consistency_phase4_extraction_boundaries_refinement.md)  
**Shipped:** TrackManager #22, NoteEditGeometryApply #23, DisplayNoteResolve #24, NoteEditFocus header #25, Phase LR #26. **Next:** close #18; pick next queue item (HITL CLI Phase 3 or persistence — see table below).

### StorageManager TU extraction — shipped (PR #17)

**GitHub:** [#16](https://github.com/Lytrix/MidiLooper/issues/16) — closed · [PR #17](https://github.com/Lytrix/MidiLooper/pull/17) merged to `dev`

### HITL CLI rebuild — Phase 3 (`base` + `edit_full` only)

**OpenSpec:** [`openspec/changes/hitl-cli-rebuild/`](../../openspec/changes/hitl-cli-rebuild/)  
**Plan:** [`docs/Plans/hitl_cli_rebuild_enhancement.md`](../Plans/hitl_cli_rebuild_enhancement.md)

| Phase | Status |
|-------|--------|
| 0–2 — foundation, scenarios | **Done** (2026-08-04) |
| 3 — layered **`base`** + **`edit_full`** | **In progress** — 3.2 bridge stabilized; next: 3.3 device PASS |
| 4–5 — corpus docs, archive | Pending |

**Layered presets (active):** `base` (`record_overdub`), `edit_full`. Do **not** wire `edit_minimal`, `revision_*`, `load_save_*`, `fader_motor_*`, etc. in this change.

**Phase 3 exit:** Mode B device PASS for `--layered --preset base` and `--layered --preset edit_full`.

### Persistence — pick one track (not all parallel)

**Guides:** [`RUNTIME_STORAGE_AND_PERSISTENCE.md`](../Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md) · **Handoffs:** [`set_revision_persistence_handoff.md`](../Plans/set_revision_persistence_handoff.md), [`continuous_runtime_persistence_phase5_recovery_handoff.md`](../Plans/continuous_runtime_persistence_phase5_recovery_handoff.md)  
**Work-queue baseline (shipped):** [`current_set_persist_work_item_queue_enhancement.md`](../Plans/current_set_persist_work_item_queue_enhancement.md)

| Track | OpenSpec / plan | Remaining | When to pick |
|-------|-----------------|-----------|--------------|
| **A — Overlay loop picker** | `set-revision-persistence` §4.8–4.10 | **Parked** — WIP stashed on `feature/set-revision-loop-picker` | User requests overlay milestone |
| **B — Crash recovery** | `continuous-runtime-persistence` Phase 5 | `.sealj` / slot **prefix load**, quarantine tail, native fixtures | After Layer D gate; orthogonal to overlay |
| **C — Admit API migration** | [#18](https://github.com/Lytrix/MidiLooper/issues/18) Phase 1.3 | `admitLoopSlotPersist` → `admitLoopPersist(LoopId)` at domain call sites | Hygiene with #18 closeout |
| **D — Layer D overdub rebuild** | DEC-035 Stages 6–7 / DEC-037 | **3b shipped** — successor [`loop_event_sourced_resolution_architecture.md`](../Plans/loop_event_sourced_resolution_architecture.md) | Active prototype |
| **E — Parked** | overlay hang, 3.9 failsafe | [`persistence_overlay_large_slot_focus_restore_bugfix.md`](../Plans/persistence_overlay_large_slot_focus_restore_bugfix.md); set-revision §3.9 | Investigation only |

**DeferredJobScheduler Phase B:** **Archived** [`2026-07-19-deferred-job-scheduler`](../../openspec/changes/archive/2026-07-19-deferred-job-scheduler/). Specs: `deferred-job-scheduler/`, `lazy-slot-hydration`.

**DEC-020 note:** Phases 0–4 **shipped** on `dev`. Phase 5 was historically **paused** pending wrap-fix validation — that does not block overlay track A; confirm with user before starting B if wrap HITL is still open (see § Parked wrap investigation below).

---

## Recently shipped (2026-08)

| Slice | Decision / commit | Evidence |
|-------|-------------------|----------|
| Overdub session index reboot undo | DEC-038 amendment; `overdubSessionIndex` + `OSI1` | [`overdub_session_index_reboot_undo_bugfix.md`](../Plans/overdub_session_index_reboot_undo_bugfix.md); HITL [`003854`](../../captures/session_20260820_003854.log); native 1397/1397 |
| Overdub wrap source-view D2-D | Revision guard in `committedPlaybackNoteOnIdentityValid` | [`overdub_wrap_source_view_display_drops_committed_bugfix.md`](../Plans/overdub_wrap_source_view_display_drops_committed_bugfix.md); HITL [`174246`](../../captures/session_20260819_174246.log); native 1384/1384 |
| Occupy source-view Gate 5A + RC close | Equal-tick Off-before-On; sibling D2-D closes pin | [`overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md`](../Plans/overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md); HITL [`161349`](../../captures/session_20260819_161349.log), [`174246`](../../captures/session_20260819_174246.log) |
| Overdub pass overlap (G2) | DEC-031/032; archived `2026-08-12-overdub-pass-overlap-resolution` | Native 1016/1016; OLED PASS [`010000`](../../captures/session_20260812_010000.log); specs synced; merge PR pending |
| Live-record tick-0 NoteOn blip | `8de682c` | Native 999/999; device PASS [`182949`](../../captures/session_20260811_182949.log); pre-fix [`182528`](../../captures/session_20260811_182528.log) |
| Note edit current state | DEC-029; `3e9253e` | Native 969/969; [`PHASE8_CLOSEOUT`](../../openspec/changes/archive/2026-08-08-note-edit-current-state/PHASE8_CLOSEOUT.md); HITL [`112202`](../../captures/session_20260808_112202.log), [`115120`](../../captures/session_20260808_115120.log), [`032118`](../../captures/session_20260808_032118.log) |
| StorageManager TU remaining trim | PR [#17](https://github.com/Lytrix/MidiLooper/pull/17) → `dev` (2026-08-08) | [#16](https://github.com/Lytrix/MidiLooper/issues/16); [`storagemanager_translation_unit_extraction_refinement.md`](../Plans/storagemanager_translation_unit_extraction_refinement.md) |
| Codebase consistency Phase 4 — DisplayNoteResolve | PR [#24](https://github.com/Lytrix/MidiLooper/pull/24) → `dev` (2026-08-10) | Native 969/969; [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Codebase consistency Phase LR — NoteMovementUtils shims | PR [#26](https://github.com/Lytrix/MidiLooper/pull/26) → `dev` (2026-08-10) | Native 969/969; [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Codebase consistency Phase 4 — NoteEditFocus header | PR [#25](https://github.com/Lytrix/MidiLooper/pull/25) → `dev` (2026-08-10) | Native 969/969; [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Codebase consistency Phase 4 — TrackManager TU | PR [#22](https://github.com/Lytrix/MidiLooper/pull/22) → `dev` (2026-08-08) | Native 969/969; manual [`225737`](../../captures/session_20260808_225737.log); [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Codebase consistency Phase 1 — authority | PR [#19](https://github.com/Lytrix/MidiLooper/pull/19) merged to `dev` (2026-08-08) | Native 969/969; HITL [`163904`](../../captures/session_20260808_163904.log); [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Playing move/length audition | `15c5750` | [`113626`](../../captures/session_20260808_113626.log), [`115120`](../../captures/session_20260808_115120.log); [bugfix doc](../Plans/note_edit_playing_move_audition_bugfix.md) |
| Resolver §12 orthogonal state | DEC-030; `7af8671` | Native 969/969; HITL [`112202`](../../captures/session_20260808_112202.log) @88.669 |
| Edit-session-action-geometry archive | 2026-08-05 | [`openspec/specs/edit-session-action-geometry/`](../../openspec/specs/edit-session-action-geometry/) |
| Docs folder hygiene Phase 3 | 2026-08-08 | Root doc roles; `PROJECT_INTENT` redirect; `FEATURES` / `FEATURE_PLANS` banners |
| Docs folder hygiene Phase 2d | 2026-08-08 | 9 `Refinements/` logs → `Plans/archive/refinements/` |
| Docs folder hygiene Phase 2c | 2026-08-08 | 2 FROZEN `*_bugfix.md` → `Plans/archive/bugfix/` |
| Docs folder hygiene Phase 2b | 2026-08-08 | 10 `*_handoff.md` → `Plans/archive/handoff/` |
| Docs folder hygiene Phase 2a | 2026-08-08 | 43 `*.plan.md` → `Plans/archive/cursor-exports/`; 4 retained at root |
| Docs folder hygiene Phase 1.5 | 2026-08-08 | PascalCase `docs/` folders (`Authority/`, `Plans/`, `Runtime/`, `Agents/`, `Templates/`) |
| Docs folder hygiene Phase 1 | 2026-08-08 | Four-bucket `docs/README.md`; slim `Plans/README.md`; `HITL_ARCHITECTURE` → `Authority/Architecture/` — [`docs_folder_hygiene_refinement.md`](../Plans/docs_folder_hygiene_refinement.md) |

Normative specs: `note-edit-current-state`, `note-edit-modification-session`, `note-edit-session-undo`, `internal-heap-external-memory-routing`, `edit-session-action-geometry`.

---

## Parked / closed (queue references)

### Edit HITL from scratch — parked

**Plan:** [`docs/Plans/m8_edit_note_edit_hitl_automation_refinement.md`](../Plans/m8_edit_note_edit_hitl_automation_refinement.md)

Deferred from **edit-session-action-geometry** Phase 5 (D14 full matrix). Interim smoke: `edit_minimal` preset only.

### Firmware ownership / lifetime review — closed (2026-08-06)

**Plan:** [`docs/Plans/firmware_ownership_lifetime_review.md`](../Plans/firmware_ownership_lifetime_review.md) — P0/P1 **done**; Phase 5 recovery and layered **`base`** HITL **parked**.

### Note edit undo after reboot — done

**Plan:** [`.cursor/plans/note_edit_undo_reboot_58ef9394.plan.md`](../../.cursor/plans/note_edit_undo_reboot_58ef9394.plan.md) — HITL **PASS** [`session_20260805_212234`](../../captures/session_20260805_212234.log).

### Hygiene — codebase debt review — complete

**Plan:** [`docs/Plans/codebase_hygiene_technical_debt_review.md`](../Plans/codebase_hygiene_technical_debt_review.md) — safe hygiene **done** on `chore/codebase-hygiene-sprint1`.

### Note edit control-surface split — complete

**Plan:** [`docs/Plans/note_edit_control_surface_split_refinement.md`](../Plans/note_edit_control_surface_split_refinement.md) — Phases 0–8 on `dev`.

---

### OpenSpec — slot-performance-interaction Phase −1 (2026-07-19)

Rename-only merge-cache vocabulary (no behavior change). Remaining phases stay queued.

| Item | Status |
|------|--------|
| −1.1–−1.4 rename struct/field/APIs | Done |
| −1.5 native tests | Done (663) |

### Recently closed — Deferred job scheduler (Phase B)

**Archived:** `2026-07-19-deferred-job-scheduler`  
**Specs synced:** `deferred-job-scheduler` (new), `lazy-slot-hydration` (gate note).

| Step | Status |
|------|--------|
| B.1 thin `runFrame` | PASS [`231510`](../../captures/session_20260718_231510.log) |
| B.2 `stepSubmittedLoadJobs` | Done |
| B.3 select then step + `LoadLoopSelectionPolicy` | Done |
| B.4 native + device | PASS [`022107`](../../captures/session_20260719_022107.log) |

### Prioritized boot load isolation (merged to `dev`)

Shipped via PR #4 on `feature/memory-pressure-reclaim`.

---

### Recently closed — Unified commit lazy slot load (Phase A)

**Archived:** `2026-07-18-unified-commit-lazy-slot-load`  
**Specs synced:** `lazy-slot-hydration`, `loop-commit-semantics`, `multi-loop-slots`, `playback-runtime-prewarm`, `timeline-passes`.

| Item | Status |
|------|--------|
| A.6 parse split + A.7 PSRAM headroom fix | **PASS** [`230145`](../../captures/session_20260718_230145.log) |
| 6.2-device interactive | **PASS** [`224607`](../../captures/session_20260718_224607.log) |
| Parked | 6.1 DERIVED_READY; 6.3 undo/import Commit docs |

---

### Recently closed — Slot queue LOOP_EDIT depart

**Plan:** [`docs/Plans/slot_queue_loop_edit_depart_bugfix.md`](../Plans/slot_queue_loop_edit_depart_bugfix.md)

| Item | Status |
|------|--------|
| No mid-play geometry write on LOOP_EDIT depart | **PASS** |
| Device gate (queue slot, LoopEnd commit, no hang) | **PASS** [`004331`](../../captures/session_20260718_004331.log), reconfirmed [`010126`](../../captures/session_20260718_010126.log) |
| Queued-launch musical-time countdown | **In tree** — OLED field; LoopEnd commits present in `010126` |

---

### Paused — Memory pressure reclaim (M6 follow-on)

**Branch:** `feature/memory-pressure-reclaim`  
**Plan:** [`docs/Plans/memory_pressure_reclaim_refinement.md`](../Plans/memory_pressure_reclaim_refinement.md)

| Phase | Scope | Status |
|-------|--------|--------|
| **1A** | Advisory FSM + `#CAP,DIAG,pressure` | **Shipped** (`f09f547`) — validated in [`session_20260714_233620.log`](../../captures/session_20260714_233620.log) |
| **1B** | Low reclaim (`try*` owner APIs; background-first) | **Shipped** — pending manual gate |
| 2 | Critical undo trim + persistence inversion | Paused |
| 3 | Replace scattered heap thresholds | Paused |
| 4 | Manual gate 215312; idle defer; OpenSpec archive | Paused |

**Manual gate (1B):** Re-run stress session; confirm reclaim under Low without playback glitches / dropped MIDI on selected track.

---

## Recently landed (M6 Ph 1–2 — merged to `dev`)

**Commits:** `50e0f6e`…`11025ca` on `dev`

| Item | Status |
|------|--------|
| Phase A metrics | Shipped |
| Phase 1 playback window (DEC-016 chunk merge) | Shipped |
| Phase 2 display stale-while-revalidate | Shipped |
| Record-start headroom + live-record display | Shipped |
| Native | **608/608** |
| Manual | **PASS** no crash — [`session_20260714_215312.log`](../../captures/session_20260714_215312.log) (64-bar, 7-track overdub; 42 append failures remain) |

**Plan:** [`multi_track_playback_pressure_closure_refinement.md`](../Plans/multi_track_playback_pressure_closure_refinement.md)

---

## Recently landed (persistence work queue — on `dev`)

**Branch:** `feature/persistence-work-queue` — [`current_set_persist_work_item_queue_enhancement.md`](../Plans/current_set_persist_work_item_queue_enhancement.md)

| Phase | Status |
|-------|--------|
| B1–B4 | Work queue admission, scheduler, monolith retire |
| B4 follow-up | Sync-drain budget, clear-slot SD-restore, urgent save / mid-pass defer |
| B5 | **604/604** native; HITL **PASS** [`host_midi_automation_serial_20260714_153240.log`](../../captures/host_midi_automation_serial_20260714_153240.log) |

---

## Recently landed on `dev` (pre-queue)

### Bugfix: record-stop MIDI flood — **restore baseline playback, pending HITL**

**Evidence:** [`session_20260713_165557.log`](../../captures/session_20260713_165557.log) — 571 note-ons over 5 pitches in 704 ticks; BPM collapse; `RING,overflow`.

**Fix:** Restored `901c4d9` playback send/anchor (`reanchorPlaybackProjection`, inline `atLoopStart`, `loop.midiEvents()` window). Kept display storage-frame playhead + persistence grace.

**Plan:** [`docs/Plans/record_stop_playback_hang_bugfix.md`](../Plans/record_stop_playback_hang_bugfix.md) (phase 4c)

| Gate | Status |
|------|--------|
| Native | **562/562** |
| `teensy41-capture-serial` build | **SUCCESS** (2026-07-13) |
| HITL / manual | **User** — record→stop→PLAYING; no MO flood; playhead bar 0 |

---

### Bugfix: record-stop coordinate frame (phase 4) — **superseded by 4c flood fix**

---

### Bugfix: overdub wrap note-off pairing — **implemented, pending HITL**

**Follow-up to capture ownership refactor** ([`overdub_wrap_note_off_pairing_bugfix.md`](../Plans/overdub_wrap_note_off_pairing_bugfix.md)). Fixes wrong on/off pairing when notes are held across loop wrap.

**Changes:** `capturePhaseTick` / `appendCaptureNoteOffAtPhase`; finalize clears pending only after append; canonical `wrappedHeadOff`; stop diagnostics; 13 native tests in `test_capture_note_off_rules`.

| Gate | Status |
|------|--------|
| Native | **554/554** |
| `teensy41-capture-serial` build | **SUCCESS** |
| HITL | **User** — 132536 scenario: tail wrap pairs tail on before wrap, not N@0 |

---

### Bugfix: overdub wrap note-off capture ownership — **implemented, pending HITL**

**Scope:** Single close pipeline for open notes at record/overdub stop; playback read-only (no mid-wrap capture mutation). Aligns live capture with `buildCanonicalSpansFromMidi` wrapped linear storage.

**Changes:**
- Removed `closeOpenNotesAtLoopWrap`, `flushPendingNotesIntoCapture`, `removeCaptureNoteOffAt`
- `finalizePendingNotes(currentTick)` on all record/overdub stop paths (playhead close, not L-1)
- `Loop::sealCapture` passes playhead `openTailCloseTick` to `finalizeWrapWindowOnStore`
- Removed `recordMidiEvents` note-off repair (tick bump, L-1 removal)
- New native suite `test_capture_note_off_rules` (7 tests)

**Plan:** [`docs/Plans/overdub_wrap_note_off_capture_bugfix.md`](../Plans/overdub_wrap_note_off_capture_bugfix.md)

**Deferred:** playback wrap `double_on` ([`session_20260709_224935.log`](../../captures/session_20260709_224935.log)) — separate plan; do not touch `playMidiEventsForSlot` / shared `projectionCycleStartTick`.

| Gate | Status |
|------|--------|
| Native | **554/554** |
| `teensy41-capture-serial` build | **SUCCESS** |
| HITL wrap + capture | **User** — re-run 125437 scenario: balanced SEVT per pitch, no ch4 ghost offs, no lag regression |

---

### Bugfix: loop-wrap playback lag regression — REVERTED to `bc98491` (shipped `901c4d9`)

**Decision:** `19aa47a` ("Fix loop-wrap playback retrigger") and both follow-up wrap-tail rewrites (full-order scan; cursor drain) are **reverted**. `Track::playMidiEvents`, `Track::playMidiEventsForSlot`, `IntervalProjection` (`shouldPlaybackEmitWrapTailEvent` / `shouldPlaybackCrossEvent` helpers), and their tests are restored to `bc98491` — the state the user confirmed "worked perfectly." `closeOpenNotesAtLoopWrap` on overdub playback wrap is restored.

**Proof the wrap fix regressed playback (not the active track):** during the active track's overdub, background **slots** flooded ~1 MO per main-loop frame via `playMidiEventsForSlot`, starving the loop and freezing display ≈2 s ("lag around loop wrap"). MO by channel — good [`session_20260713_115434.log`](../../captures/session_20260713_115434.log) vs bad [`session_20260713_122107.log`](../../captures/session_20260713_122107.log): ch5 (active) 212→223 unchanged; ch4 (slot) 486→**3620**; ch4 ~1 ms-gap events 278→**3422**. `19aa47a` introduced it (ch4 486→2488); cursor drain worsened it (→3622).

**Deferred (not reintroduced yet):** original wrap **double-on** ([`session_20260709_224935.log`](../../captures/session_20260709_224935.log) — MO `double_on` at BAR, 54-bar loop). Any future fix must NOT touch per-frame slot playback / the shared one-per-track `projectionCycleStartTick`.

**Out of scope:** slot/bar queued seek `sendAllNotesOff`; unify pending-note flush at record/overdub stop; stop FSM changes.

| Gate | Status |
|------|--------|
| Native | **542/543** (known empty `test_capture_note_off_rules` suite) |
| `teensy41-capture-serial` build | **SUCCESS** |
| HITL wrap + stop | **User** — compare to 115434: no per-frame slot MO flood, no BAR bunching, no ~120 ms post-stop DISP lag |

---

### OpenSpec: [`continuous-runtime-persistence`](../../openspec/changes/continuous-runtime-persistence/) (DEC-020)

**Branch:** `dev` — Phases 0–4 shipped; **Phase 5 not started**

| Phase | Status |
|-------|--------|
| **0** Diagnostics | **Complete** |
| **1** Chunk lifecycle | **Complete** |
| **2** Persistence queue | **Complete** |
| **3** Cooperative scheduler | **Complete** — overdub-stop HITL passed (`f0ee520`) |
| **4** Mid-pass persistence | **Shipped** — native + **HITL passed** [`session_20260709_171043.log`](../../captures/session_20260709_171043.log) (64+64); boot restore [`session_20260709_171951.log`](../../captures/session_20260709_171951.log) |
| **5** Recovery | **Not started** — longest valid prefix load + quarantine tail — [**handoff**](../Plans/continuous_runtime_persistence_phase5_recovery_handoff.md) — **CURRENT_WORK pick track B** |
| **6** Full 64+64 HITL | **Mostly evidenced** (same log) — archive checklist + `oldestDirtyChunkAge` review remain |

| Gate | Owner | Status |
|------|-------|--------|
| Native | Agent | `pio test -e native` — **969/969** (2026-08-10) |
| `test_storage_loop_io` | Agent | Run before Phase 4 archive sign-off |
| Phase 4 HITL (64+64 record/overdub) | **User** | **PASS** — [`session_20260709_171043.log`](../../captures/session_20260709_171043.log) |
| Phase 4 HITL (cold-boot restore) | **User** | **PASS** — [`session_20260709_171951.log`](../../captures/session_20260709_171951.log) |
| Phase 5 native fixture | Agent | Prefix load from truncated `.sealj` / slot — **not started** |

**HITL policy:** stop/crash scenarios — user captures serial manually (`capture_session.py`); agent does not loop HITL. On crash, user bisects and shares log before stop-path patches.

| Doc | Role |
|-----|------|
| Agent map | [RUNTIME_STORAGE_AND_PERSISTENCE.md](../Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md) |
| **Phase 5 handoff** | [continuous_runtime_persistence_phase5_recovery_handoff.md](../Plans/continuous_runtime_persistence_phase5_recovery_handoff.md) |
| OpenSpec | [continuous-runtime-persistence](../../openspec/changes/continuous-runtime-persistence/) |

---

## Landed on branch (recovery merge — no further agent work unless HITL fails)

| Area | Commit / note | HITL |
|------|----------------|------|
| NOTE_EDIT move/pitch bracket | `736fa33` | User: `session_20260709_155307` replay |
| Split-focus slot switching | `0f086f4` (DEC-025) | User: preview slot + loop-end commit |
| Slot short-press / deferred restore | `f6b496a` | User |
| Playhead + undo geometry | `346f7ce` | User: `session_20260708_233241` |
| Loop-scoped undo (DEC-024 Ph 1) | `b1d3259` | Native only |
| Boot load + slot scan | `50ad01b` | User: cold boot ×5 |
| Slot clear + arm state | `2a565aa` | User |

---

## Parked

### Later — bound deferred wrap-append ([`183229`](../../captures/session_20260821_183229.log))

**Owner:** `Loop::applyDeferredVisualCacheWrapAppend`  
**Evidence:** `idle_append=1122618` at 44.5 s. Stage C moved wrap-append off the undo/switch turn; the later turn is still unbounded.  
**Plan:** [`runtime_scheduling_owner_boundary_inventory_investigation.md`](../Plans/runtime_scheduling_owner_boundary_inventory_investigation.md) §37. Do not start until a new slice is opened.

### Later — attribute 5 s STOPPED idle ([`183229`](../../captures/session_20260821_183229.log))

**Owner:** unknown until attributed.  
**Evidence:** `idle_maint=5028348` at 49.7 s between track 6 and 7; no `idle_gather` / `idle_append` child. Investigation first.  
**Plan:** §37. Do not start until a new slice is opened.

### Tagged for investigation — occupy id resolves to no source-view note (`norow`) — superseded

**Superseded by:** [`overdub_consume_id_resolution_completeness_bugfix.md`](../Plans/overdub_consume_id_resolution_completeness_bugfix.md) Stage 1 — hold fill before id lookup. Native fixture `test_consume_id_resolution_norow_repaired_by_hold_fill`. HITL gate open post-flash.

### OpenSpec: [`unified-capture-commit-owner`](../../openspec/changes/unified-capture-commit-owner/) (DEC-023)

Phases 1–3 prototype **reverted** at `40db4df` (boot bisect). Storage boot recovery helpers (`37f6b00`) landed. Retry Track/Loop slices only after stable boot + user approval.

### Derived note overlap (`edit-session-action-geometry`) — **archived 2026-08-05**

Pipeline shipped; normative [`openspec/specs/edit-session-action-geometry/`](../../openspec/specs/edit-session-action-geometry/). Phase 5 HITL matrix **parked** — see Edit HITL plan above. Historical handoff: [`derived_note_overlap_logic_handoff.md`](../Plans/derived_note_overlap_logic_handoff.md).

### Prior: [`runtime-derived-representation-heap`](../../openspec/changes/runtime-derived-representation-heap/)

M6 Ph 1–2 on `dev`; pressure reclaim + Ph 3–4 in [`memory_pressure_reclaim_refinement.md`](../Plans/memory_pressure_reclaim_refinement.md).

---

## Do not start without decision

- DEC-023 capture-commit Track slices (after Phase 5 or explicit user go)
- `currentset-savedset-storage-layout`
- D13 jam-recording (ROADMAP — post JamRecorder)


2026-09-16 task-2.3 integration checkpoint: production canonical stop preparation
now uses a separately reserved output Capture instead of mutating raw capture
chunks. Explicit-source transfer/preview APIs and full-pool admission checks are
covered by native tests. The service-paused Button A/manager/Track reentry tests
remain red: FIFO, live/head staging, preview/session/undo/snapshot ownership,
causal sources and generic-reset lifetime isolation are not implemented. Keep
2.3 and HITL open; see the active change's CONTINUOUS-CAPTURE-OWNERSHIP.md for
bounds, evidence and limitations. No firmware upload or device action occurred.
**DEC-070 probe-driven fix implemented:** release the retained tail cache at the retarget, keep the companion/undo estimates, walk to `commitStoppedWithUndo`, bounded fallback as a safety net only. Native 2,213 / 1 skipped / 0 failures; RAM1 locals 6,400; `captures/successor-wrap-44` overflowed the capture ring so the 5.9 display/save/undo acceptance still needs one clean run.
**DEC-070 second attempt (both caches released) still falls short** (`captures/successor-wrap-45`: `companions=kept` then `companion path stalled; raw pass only` 0.76 s later). The remaining denier is `beginFrozenCanonicalTail`'s `captureAttached()` guard against the predecessor's retained canonical inputs; clearing it invalidates the canonical prefix, which needs playback turns. The fix belongs at retention time: skip `retainCommittedCanonicalTail` in `completeDeferredCleanup` when a successor interval is already live (`job.interval != captureInterval_`). 5.9 stays open.
**DEC-070 retention guard (5.11) implemented** (`3079d8d9`): `completeDeferredCleanup` retains the committed canonical tail only while the job is still the live interval. Native green; `captures/successor-wrap-46` shows the save completing (`persistence handoff result=ok`) but the retarget still stalls (`companion path stalled; raw pass only`, `overdub_ok=False`), so the blocking `captureAttached()` belongs to the live interval's own canonical inputs, not to the predecessor's retention. Next: instrument where that attachment is set. 5.9 open.
