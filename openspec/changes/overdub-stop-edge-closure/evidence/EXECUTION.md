# Execution evidence — overdub stop-edge closure (DEC-233)

Baseline: `ebaa6ebe` on `feature/unified-rolling-playback-path`, pre-existing dirty tree retained.
Host: macOS, PlatformIO native env; firmware env `teensy41-capture-serial`.

## 1. Fixture-first transition oracle

### 1.1 Principal capacity-wait fixture — pre-fix RED (before any production edit)

Command:

```
pio test -e native -f test_shared_capture_finalization
```

Trace at the first immediate-closure assertion (production unchanged, only the fixture added):

```
test/test_shared_capture_finalization/test_shared_capture_finalization.cpp:11203:
test_stop_edge_closes_input_while_the_canonical_output_waits:
the Stop press must leave OVERDUBBING even when finalization waits   [FAILED]
=========== 152 test cases: 1 failed, 150 succeeded ===========
```

That is the defect the slice exists for: an accepted Stop whose finalization resources are unavailable
left the input interval recording. After the implementation the same fixture passes and additionally
pins: the cut is admitted while the canonical worker is **not** bound, the interval's own phase/identity
and sealed rows are preserved, late NoteOn/NoteOff/CC add nothing, and ordinary deferred turns bind the
worker exactly once at the stored phase.

### 1.2–1.5 Fixture registration and mutation-red discrimination

Fixtures registered in `test/test_shared_capture_finalization/test_shared_capture_finalization.cpp`:

| Task | Fixture |
| --- | --- |
| 1.1 | `test_stop_edge_closes_input_while_the_canonical_output_waits` |
| 1.2 | `test_stop_edge_successor_overlap_via_manager`, `..._via_direct_track_start` |
| 1.3 | `test_stop_edge_uncontended_admitted_stop_seals_and_publishes_once` |
| 1.4 | `test_stop_edge_transport_stop_retargets_the_retained_interval`, `..._a_queued_predecessor_and_the_retained_successor`, `test_stop_edge_slot_switch_waits_for_the_unbound_stop_to_bind`, `test_stop_edge_populated_canonical_output_is_preserved_until_the_owner_releases_it` |
| 1.5 | `test_stop_edge_stopped_preview_is_closed_and_playhead_independent_across_wrap` |

Reused, deliberately amended fixtures (closure semantics changed, identity assertions kept):

- `test_replaced_loop_supersedes_accepted_stop`, `test_changed_capture_mode_supersedes_accepted_stop`:
  the latch is now taken through its production owner (`latchAcceptedStop`); a canonical reservation no
  longer denies admission, so `occupyCanonicalOutput` could not produce a retained latch any more.
- `test_6_4_outstanding_stops_never_share_the_last_transfer_row`: the third retained Stop is now
  "Playing + accepted latch + no job" instead of "still Overdubbing".
- `test_6_5_rapid_overdub_queue_does_not_grow`: passes again once decision 4 restores the lapsed transfer
  horizon for the closed retained interval (previously `retainedStop=16`).

Mutation-red evidence (each mutation applied alone, then reverted byte-identically):

| Mutation | Result |
| --- | --- |
| M1 `handOffRetainedStopBeforeSuccessorStart` always returns true | both 1.2 legs fail: "the successor intent is retained beside the Stop, not stacked" |
| M2 `retargetRetainedStopToStopped` returns false | both transport fixtures fail ("never consumes it"; no job produced) |
| M3 `inputClosedOverdubPreviewForSlot` returns nullptr | 1.5 fails at "the input-closed producer must stay readable while it waits" |
| M4 `bindCaptureStopWorker` drops the canonical readiness check | 1.1 ("a withheld canonical output must not bind the finalizer") and 1.4d ("Expected FALSE Was TRUE") fail |

## 2. Implementation slices

- **Decision 1** — `Track::stopOverdubbing` closes input on the press for a retained Stop
  (`closeOverdubInputForRetainedStop`), keeps the interval identity/rows/reservations/held notes/
  pending changes/preview/session claim, and `driveAcceptedInteraction` retries with the latch's stored
  `phaseTick` and `inputClosedRetry` (bypasses only the completed state transition).
- **Decision 2** — the canonical output is no longer a descriptor/cut admission condition; the eager
  `setCaptureStopOwner` was replaced by `bindCaptureStopWorker`, and deferred turns top up an empty,
  unowned output only (`replenishEmptyCanonicalCaptureOutput`). A busy canonical finalizer therefore no
  longer queues the successor.
- **Decision 3** — `startOverdubbing` hands a retained Stop off before any prewarm/staging reset/claim;
  on genuine physical exhaustion the successor intent is retained in the latch (`successorStart`) and
  transfers into the existing Start carrier after the Stop binds. `serviceAcceptedInteractionTurn` and
  the pending-stop lane drive that Start while the predecessor is still queued.
- **Decision 4** — slot switch waits for this slot's unbound Stop (`finalizeCaptureAndSelectSlot`);
  transfer-horizon replenishment is eligible for the closed retained interval; transport Stop retargets
  (never consumes) a retained Stop and keeps its phase; `sendAllNotesOff` preserves the retained job's
  held-note closure state.
- **Decision 5** — `Track::inputClosedOverdubPreviewForSlot` exposes the closed producer and its
  immutable closure tick through the existing display-source gather, composed as a frozen source.

## 3. Verification log

(filled by the gate tasks)

## 3. Verification log (in progress)

### 3.1 Focused and broad native

Focused suite after the implementation and the deliberate fixture amendments:

```
pio test -e native -f test_shared_capture_finalization   -> 159/159 PASS
pio test -e native -f test_continuous_capture             -> 107/107 PASS
```

`test_continuous_capture` needed deliberate amendments because DEC-233 changes the closure contract it
encoded. The amended cases keep their identity/content assertions and now assert the new contract:
`test_successor_stop_denied_by_occupied_deferred_capacity_is_non_destructive`,
`test_button_a_successor_stop_denied_until_deferred_role_available`,
`test_reentry_routes_agree_on_admission_and_capacity_denial`,
`test_successor_same_interval_transfers_exactly_once`,
`test_start_request_while_successor_is_live_is_not_a_second_interval`,
`test_successor_stop_is_denied_when_the_canonical_copy_target_is_busy`,
`test_b02_accepted_stop_waits_for_canonical_credit_then_executes_once`,
`test_s2b2_loaded_slot_stop_acquires_its_own_horizon_and_canonical_target`,
`test_s2b6_park_terminal_never_cancels_an_accepted_stop`,
`test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes`,
`test_s4f10e_an_undo_replacing_the_interval_supersedes_the_accepted_stop`,
`test_s4f10c_accept_and_latch_then_transport_stop_supersedes_before_the_drive`,
`test_task22_a_retained_stop_reports_its_own_sequenced_identity_evidence`,
`test_task22_the_consumption_reason_names_the_edge_that_took_it`, and
`test_s4f4_...` renamed to `test_s4f4_the_stop_boundary_is_the_first_press_phase_not_the_execution_tick`
(the DEC-078 rederivation clause DEC-233 explicitly reverses).

One production ordering defect was exposed by those fixtures and fixed in the slice:
`processDeferredIdleMaintenance` ran the DEC-231 off-edge view preparation first, and that unit returns
every turn while a request is pending, so it starved the accepted-stop drive. A retained Stop now takes
the lane ahead of that unit (a retained Start keeps preparation first, because preparation readies it).

### 3.2 Target build and flash (image `e1a053b2…`)

```
pio run -e teensy41-capture-serial            -> SUCCESS
  RAM1 variables 96,192; ITCM 391,080 + padding 2,136 (floor 840); locals 34,880
sha256 firmware.hex = e1a053b2814e45b91d09af978283398772222b63a10d3f0442d43a94edb7899f
pio run -e teensy41-capture-serial -t upload   -> SUCCESS (teensy-gui, no-press path)
```

Boot proved separately from `captures/session_20261030_230849.log`:
`DIAG,boot_reset,crash=0`, `BOOT,load,ok`, `BOOT,ram1,192512`, `BOOT,usb_host,begin`.
The flashed image predates the prewarm-ordering fix above (a later native-only iteration); no second
upload was made.

### 3.3 Device 20-bar chain — NOT PASSED (host rig), start/stop path proven manually

Run `captures/midifile-chain-48` (`--no-clear`; the 1-based slot 4 was verified empty by
`Clear ignored — selected slot is empty (track 7 slot 3)`, so nothing was overwritten):

- Record phase completed: 20 bars, `length=15360`, `RECS,stopToStopped`, `cap=1`.
- The rig's overdub **window presses never reached the device**; only the clock stream arrived. The
  script then hung in its stream phase (still sleeping after >8 min) and was stopped.
- Decisive control: a manual Record control press at device time 360.5 s opened
  `Overdub session opened` / `Overdubbing started @ tick 56235`, and its Stop produced
  `Reentry window open slot=3`, `ST,Track,OVERDUB_STOPPING,PLAYING`, `Reentry released slot=3`.
  The slice's press/stop path therefore works on hardware; the 20-bar **content** gate did not run to
  completion and is left open.

Recorded limits: no row-level Stop/Start content verdict for the chain; no claim about the reported
already-closed-note visual symptom.

### Slice boundary (2.6)

Files authored for this slice: `include/Track.h`, `src/Track/TrackOverdubLifecycle.cpp`,
`src/Track/TrackDeferredMaintenance.cpp`, `src/Track/TrackPlaybackHotPath.cpp`,
`src/TrackManager/TrackManagerCaptureQueue.cpp`, `src/DisplayManager/DisplayNoteWindowGather.cpp`,
`test/test_shared_capture_finalization/*`, `test/test_continuous_capture/*`,
`test/test_runtime_working_set_ownership/*`, `docs/Authority/Architecture/ExecutionBounds.md`,
`docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md` and this change's artifacts.

The working tree was already dirty when this session began (the design records that condition), and no
commit was requested, so a **staged-by-hunk** slice diff does not exist yet. Those files still carry
pre-existing hunks from earlier work; a later commit must stage only the hunks above. No SD format or
undo-schema code was touched.

### 3.2b Final image after the native amendments (image `30304838…`)

A clean native rebuild was required once (a concurrent `pio test` had corrupted the shared native cache:
one suite hit `no such file ... CapturePhaseState.o`); the clean run is the authoritative gate.

```
pio run -e native -t clean
pio test -e native      -> 3737 cases / 7 skipped / 3730 succeeded / 0 failed / 0 errored (6:10)
pio run -e teensy41-capture-serial   -> SUCCESS, RAM1 variables 96,192 / ITCM 391,080 + padding 2,136
sha256 firmware.hex = 30304838ee06482553b93121f843bd91fc1473db87817d04f74ef2449b93ccf3
pio run -e teensy41-capture-serial -t upload -> SUCCESS (teensy-gui)
```

Boot proved separately from `captures/session_20261030_234318.log`: `boot_reset,crash=0`,
`BOOT,load,ok`, `BOOT,ram1,192512`, `BOOT,usb_host,begin`. The exact HEX is retained in
`captures/30304838/` with hash, head, status, diff and build flags. This image carries the
prewarm-ordering fix and supersedes the earlier `e1a053b2…` flash.

Directly affected suites in the clean run: `test_shared_capture_finalization` 159/159,
`test_continuous_capture` 107/107, `test_runtime_working_set_ownership` 257/257,
`test_live_capture_display_boundary`, `test_display_window_utils`, `test_display_note_count`,
`test_display_dma_health` all PASSED.

### 3.2 Focused disposition against `evidence/REVIEW.md`

| Review finding | Disposition |
| --- | --- |
| 1. Eager `setCaptureStopOwner` bypass | Removed; every binding goes through `bindCaptureStopWorker` (mutation M4 red: 1.1 and 1.4d fail). |
| 2. Successor reset before claim | `startOverdubbing` hands the retained Stop off before prewarm/staging reset/claim (mutation M1 red: both 1.2 legs). |
| 3. Slot-switch bypass while Playing | `finalizeCaptureAndSelectSlot` refuses on this slot's unbound Stop (1.4c). |
| 4. Recovery producer lost | Transfer-horizon top-up is eligible for the closed retained interval; `test_6_4_outstanding_stops_never_share_the_last_transfer_row` and `test_6_5_rapid_overdub_queue_does_not_grow` pass again (the latter previously kept `retainedStop=16`). |
| 5. Transport ownership loss | Transport Stop retargets (never consumes) and preserves the phase; `sendAllNotesOff` keeps the job's held notes (1.4a/1.4b; mutation M2 red). |
| 6. Preview tightening without a blanket freeze | `inputClosedOverdubPreviewForSlot` + the frozen composition in the draw-source gather; 1.5 (mutation M3 red). Successor previews stay live. |

Additional production finding closed in the same slice: the DEC-231 off-edge view preparation returned every turn
while a request was pending and could starve an accepted Stop on the event lane; a retained Stop now takes the
lane first. No independent follow-up review PASS is claimed (the initial review was BLOCKED and its findings are
what this table closes).
