# S7.5b.8b — the seed was killed by a transport toggle read from a display frame

Host tooling only: no firmware line, no new serial/Tier-A marker, no stimulus edit, no
scheduler or timing-policy change. This closes the *seed* half of the "controlled seed/clock
pacing diagnosis" the hardware journal owed; the bounded-entry lane gate itself stays open.

## G2 — acceptance and scope (frozen before edit)

**Acceptance.** The record-arm transport precondition must press the global transport
**toggle** only on evidence that a running transport emits. A device that is stopped must not be
pressed, and a device that is genuinely running must still be pressed exactly once. Both are
measured: a fixture-first behavioral RED, and a replay of the real capture prefix that produced
the failed seed.

**Scope.** `scripts/hitl/serial_transport.py` (one new predicate) and its two decision sites in
`scripts/hitl/legacy_record_baseline.py::_ensure_transport_stopped_before_record_arm`, plus the
host tests that pin them. `_TRANSPORT_MARKERS` / `serial_line_is_transport` keep their meaning —
they answer "is the serial link alive", which is what the heartbeat and
`CaptureLineClock.last_transport_at` need, and three tests pin `DFRAME` there.

## G0 — the defect, measured on the real capture

`captures/live-current-root-entry-t8s4-04` (default HEX `9e8270b4…`, track 8 / slot 4, firmware
rows 7/3). Replaying the predicates against `session_20261010_131058.log` truncated at the press
(1,871 lines) gives `_transport_running_before_record_arm == False` — the transport had been
**stopped** since device 1590.044 s — while `_serial_sequencer_running == True`. The live tail's
only transport-marker rows were two `#CAP,<us>,DFRAME,…` rows; dropping `,DFRAME,` alone flips the
predicate to `False`.

`DFRAME` is the periodic display-frame monitor, not transport evidence: in the same capture it is
emitted every ~1.504 s (frame counter +30) straight through the stopped interval — 1591.943 and
1593.441 sit between `Transport stopped` (1590.044) and `Transport started` (1594.433).

**What that cost the seed** (`assertions.control_actions` of the base report, 1-based notes on
ch16, device stamps from the same log):

| # | caller | device | effect |
|---|---|---|---|
| 5 | `_ensure_transport_stopped_before_record_arm` | 1593.639576 | pressed the toggle on a **stopped** device → `Global Transport (short)` → `MIDI Start` 1594.066 → `Recording started @ tick 0` 1594.430 |
| 6 | `_restart_transport_after_clear` | 1594.630448 | pressed the same toggle → `Transport stopped` 1595.165 |
| 7 | `_send_record_stop` | 1599.012521 | record stop, after the take was already over |

→ `RECS,stopToStopped,3,384,0,384,768,0`: a **768-tick** take (the 1-bar default) at tick 384
instead of 1536, with **one** captured note and an unmatched open (ch8 note 60 On 1594.916986,
Off 1595.166393 — the off landed after the stop), and the fixture's other seven notes streamed
into a stopped transport. Reported as `expected_final_length 1536 / actual 768`,
`unmatched_open_notes 1`, first note `486.435 ms / 23 clocks` late.

## Fix

* `serial_transport.py`: `serial_lines_show_running_transport(lines, *, tail, after_index)` — the
  same `filter_live_lines` device-time window, counting only `,BAR,` (the device's own bar
  counter) and `,BPM,` (the learned clock rate). `DFRAME`/`MI`/`MO`/`ST` do not count: `DFRAME` is
  emitted stopped, `MI` is the host's own gestures, `MO`/`ST` are device feedback that also flows
  stopped.
* `_ensure_transport_stopped_before_record_arm`: the precondition now uses that predicate in place
  of `_serial_sequencer_running`, and the post-stop re-check is **windowed** —
  `_serial_lines_show_running_transport(snapshot, after_index=post_stop_baseline) or
  serial_global_transport_running(snapshot, after_index=post_stop_baseline)`. The re-check
  deliberately does **not** reuse `_transport_running_before_record_arm`: its whole-log first check
  (by design, `after_index=0`) re-reads the very evidence that authorized the press and pressed a
  second time — caught as a RED by the new positive control before it shipped.
* `_prepare_deferred_save_before_clear` (same file): the clear-time precondition and both retry
  checks had the same wide-liveness defect, on a path the seed always runs when the clear is
  enabled. Its third term is now the running-row predicate, and its two re-checks are
  `_transport_started_since(snapshot, after_index=<baseline at that press>)`.

## Independent review, and what it changed

One focused adversarial review pass (read-only, whole diff). It found one **defect this change
introduced** and two things it left inconsistent; all three concrete findings were fixed and
re-tested, and each fix is now pinned by a test that fails when it is reverted (mutations M1–M3,
restored byte-identically):

* **The re-check could no longer see the mis-press it exists to correct.** The firmware answers a
  control press at `press_ms` + its own 300 ms debounce (≈0.42 s at `press_ms` 120), while the
  window was opening at `settle_s = max(phase_wait_ms, 300) ms` — i.e. *after* the device had
  already replied (the capture confirms it: press 1593.639576 → `MIDI Start` 1594.066). So the
  first cut made the correction unreachable and left the arm free to land on a transport the press
  had started. **Fixed** by taking `press_baseline` at the press and by asking the start-edge
  question (`_transport_started_since`), so the window contains the answer. M2 (revert to a late
  baseline + the wide predicate) fails `test_a_press_that_started_the_transport_is_corrected`.
* **Two of the new tests did not discriminate.** Deleting the precondition's running-row term kept
  all of them green, and the re-check's positive direction was unpinned. **Fixed** with
  `test_a_device_bar_row_alone_still_stops_the_transport` (M1 kills it), the correction test above,
  and `test_a_display_frame_in_the_press_window_is_not_a_restart`.
* **`_prepare_deferred_save_before_clear` was left behind** (above), including its retry checks
  whose false positive does not merely press wrongly — it **blocks the clear** and aborts the whole
  seed fixture. M3 (revert its precondition term) fails
  `test_display_frames_alone_do_not_press_or_block_the_clear`.
* Comment/commentary accuracy: the `,BPM,` note now says what the firmware does (emitted only where
  the session-capture BPM sink is enabled, and only while the device is *slaved* — the default
  `teensy41-capture-serial` build emits none, so `BAR` carries the predicate there), and the stale
  `serial_sequencer_running` reference in `_transport_running_before_record_arm`'s docstring is
  corrected.

**Named, deliberately not fixed here** (real, but latent or out of this acceptance criterion):
`scripts/hitl/legacy_edit_baseline.py::_stop_transport_and_flush_revt` has the *same* defect with
the *heartbeat* leg (`after_index=0`), so its "is the transport active" answer is essentially always
true and its press is unconditional — reachable only through `--stop-transport-before-edit`, which
no preset passes today (owner: the edit baseline, not this path). `,BAR,` is emitted on a bar-index
*change*, so a tick realign while stopped (`TrackCaptureStopCommit`) can still put one in the
window — low probability, and a net improvement over the display-frame leg it replaced. The widest
predicate remains correct for the callers whose question is "is capture still alive"
(`_should_reset_transport_for_recovery`, the recovery skip at `run()`), and narrowing those would
make their press decisions fire *more* often; the resulting asymmetry with this site is named, not
silently unified.

## Fixture first, behaviorally red

**Nine new host tests and three rewritten ones.** `scripts/test_hitl_transport_reset.py`: display
frames are not running-transport evidence (and the wider predicate still counts them); a device
`BAR` row is; a historical `BAR` row behind `after_index` is not.
`scripts/test_record_target_precondition.py` (`RecordArmTransportStopEvidenceTest`): the real
stopped-with-display-frames shape presses nothing (RED before the fix: `Expected
'_send_short_press' to not have been called. Called 1 times … note=39`); a `BAR` row alone still
presses once (this is the test the precondition's running-row term is pinned by); a press that
started the transport is corrected by a second press; a display frame in the press window is not a
restart; and two positive controls (human `Transport started` plus a `BAR` row, and a running
track-state row) still press exactly once. `scripts/test_clear_before_record_precondition.py`
(`DeferredSaveClearTests`): its three former tests drove the removed liveness mock, so they were
rewritten to model the device answering each press, and a new case forbids the
display-frames-alone press and block.

## Verification

* **Replay of the real capture** (the failed seed's own prefix, 1,871 lines): precondition
  `pressed=False`, zero transport presses, for `after_index` 0/1500/1700/1800 (was one press before
  the fix). The earlier call in the same capture that ran with the transport genuinely running
  (started 1587.473, no later stop) still reports `pressed=True` with exactly one note-39 press.
* **Focused:** the three suites above — **93/93 OK** (51 in the two arm suites, 42 in the clear
  suite).
* **Mutation-proved:** M1 (delete the precondition's running-row term) fails only
  `test_a_device_bar_row_alone_still_stops_the_transport`; M2 (late baseline + the wide predicate in
  the arm re-check) fails only `test_a_press_that_started_the_transport_is_corrected`; M3 (the clear
  precondition's third term back to the wide predicate) fails only
  `test_display_frames_alone_do_not_press_or_block_the_clear`. Each restored byte-identically
  (`sha256 2aaaddee163dea91dece893e85db31f5e46483d2c3f9cae58db33eb36e4f92b5` before and after).
* **Full host sweep A/B** (`unittest discover -s scripts -p 'test_*.py' -t scripts`): baseline
  1,292 tests / 7 failures + 1 error; with the fix 1,302 tests / **the same 7 failures + 1 error**,
  byte-identical failure-id sets (pre-existing firmware-source allowlists, compatibility-consumer
  inventories and the occupy-paint/layered verifiers).
* `git diff --check` clean and `openspec validate runtime-working-set-ownership --strict` valid on
  the five touched files.

## Authorized hardware rerun

Owner authorization for this session: clear/overwrite **only** track 8 / slot 4; reuse the firmware
already on the board. No upload, no reset, no other slot, no quarantine/rename/delete.
`metadata.json` records `firmware_hex_sha256 9e8270b41551ae4609b44362ad7fb696a2a6e6f0a4ad7e39184f757ce90b3dec`
— the same image the journal pinned. Invocation: `host_midi_hitl.py run --preset
note_edit_reselect_stacked --track-number 8 --loop-slot 4 --midi-channel 8 --no-boot-reset
--capture-boot-wait 30` → `captures/live-current-root-entry-t8s4-05` (clear/seed now run, so
`--no-clear-before-record` is deliberately absent).

**Which revision that run covers, exactly.** It ran the *pre-review* host revision: its seed was
produced by the final precondition term (that term did not change afterwards), but its arm-phase
re-check and its clear-phase decision were still the first cut. The review-driven changes to those
two were therefore **not** on hardware. A re-run on the final revision was attempted into
`captures/live-current-root-entry-t8s4-06` and could not start: `capture_session did not finish
boot settle` — the Teensy was **absent from USB** (`/dev/cu.usbmodem*` gone, six bounded rechecks
over ~20 s, no device in the USB tree; the session log is 0 bytes). No device action preceded that
(the only thing after run-05 was the canonical drain, which reached `complete/2/ok`), no latch was
written, and per the standing rule no reset/flash/retry was attempted while the device is
unreachable. **This re-run is owed**: reconnect the board, verify a stopped persistence drain, then
repeat the same authorized invocation into a fresh `--out-dir`.

**The seed is repaired** (base report `host_midi_automation_baseline_20261010_140110.json`):

| reading | attempt04 | this run |
|---|---|---|
| `record_loop_length` | expected 1536 / actual **768**, `final_length_ok False` | expected 1536 / actual **1536**, `final_length_ok True` |
| `record_phase` | 1 On / 0 Off, `unmatched_open_notes 1` | **8 On / 8 Off**, `unmatched_open_notes 0` |
| `record_note_span` | `note_on_count 1` | `note_on_count 8`, `raw_span_ok True` (1742 ≥ 1339) |
| first note | 486.435 ms / **23 clocks** | 2.626 ms / **0 clocks** |
| `record_notes_sent` / grid steps / clock pulses | 8 / 8 / 192 | 8 / 8 / 192, jitter 0.0 |

Stored content (`REVT` rows) is the canonical `EDIT_RECORD_FIXTURE` exactly —
ticks `2, 192, 384, 576, 864, 1056, 1248, 1344` against steps `0,4,8,12,18,22,26,28` × 48 — and
the control-action sequence is the intended one (`67` clear, `38` long LOOP_EDIT, `53` slot
select, `39` `_prepare_deferred_save_before_clear`, `67`, **`36` `_send_record_arm_press`**,
`39` `_restart_transport_after_clear`, `36` `_send_record_stop`) with **no**
`_ensure_transport_stopped_before_record_arm` press at all.

**Named, not smoothed.** The base report's `overall_ok=false` does not come from the seed: it is
the host's 16th-grid expectation (`record_phase.sequence_mismatch_count 6`,
`stored_record_grid.bad_delta_count 6 > budget 2`) measured against `TICKS_PER_16TH_STEP` while
this fixture's own steps are beat-and-seam spaced. `record_only` skips that row in the issue path
and `base_report_record_seed_ok` (≥7 sent notes) passed, which is why the composed preset
continued. It is an inapplicable host expectation, not a repaired pass.

**The lane gate stays open.** The leg itself still fails its own oracle —
`selected_note_moved`, `stacked_reselect_after_deselect` (`move_index null`,
`dest_distinct_note_idxs [2]`) — while `note_edit_entered`, `select_apply_highlight_follows` and
the motor/notegate sub-oracles pass, and the run logs
`Timed out waiting for stable select at tick 384 (±24) note_idx=3`. The leg's own target ticks
(2/192/384) are intact in the stored content, so this is **not** a seed-target artifact; it is the
S7.5b.8b live-entry measurement, still a FAIL for now. Nothing here is a lane pass and no task is
checked off.

**Terminal state.** One canonical managed capture session with `hitl.device_shutdown.DeviceSession`
on the same port: `status=complete, drains=2, ok=True` ("2 stop+drain cycle(s) reached verified
persistence idle"), no `captures/.device-shutdown-incomplete.json` latch, no reset/upload, no
other-slot or SD operation.

## Not claimed

No firmware change, so `pio test -e native` was not rerun and no capture build/RAM1 line is owed
from this slice. No timing/envelope bound, no new marker, no stimulus edit, no lane PASS, no
S7.5b.8b/8c/8d checkoff. The two residual seed readings — the first stored note sitting at tick 2
rather than 0, and the take reporting `raw_length 1742` against the 1536-tick boundary — are
recorded, not explained here, and belong to the seed/clock pacing work still owed. The review-driven
re-check and clear-phase changes have fixture, replay and mutation evidence but **no hardware
run** (the device left USB before the re-run), and the named latent sibling in
`legacy_edit_baseline.py` is deliberately untouched.

## 2026-10-10 — the seed's *retrieval* asked for a boot-global balance it could never have; the lane guard read a mid-burst snapshot

Host tooling only again: no firmware line, no new marker, no stimulus edit, no task checkoff, and the
lane gate stays open. Two host defects were fixed with fixture-first evidence, and the rerun then hit
a **third, named, and not-fixed** blocker in the base leg's pre-record confirmation.

### 1. End-of-leg Record retrieval: a shed row made `tx == enq` unsatisfiable

`captures/live-current-root-entry-t8s4-14` aborted `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)` with
`Abort reason: !TRACE_DRAIN: response fence missing` after `drains=20`. The device's own rows show
why: the retrieval's `!CAP_IDENTITY_STATS` reply carried `RID entered=7,enq=7,tx=6` and the same for
`RECA` — one Record row that entered the ring and was **shed before this managed log opened**. The
counters span the boot, so `drain_record_evidence`'s completion gate (`all(tx == enq)`) demanded
drain rounds until the deadline: 21 `!TRACE_DRAIN` requests were forwarded (capture owner
`command accepted`, 21 of each), and the 21st was issued as the deadline expired, so its window was
never polled. Both fences arrived 3 ms later
(`#CAP,2253438046,TRACE_FENCE,2cc9…` / `#CAP,2253438085,TRACE_FENCE,8b09…`) — the abort named a lost
fence that was not lost. A live read-only probe (`ring_probe.py`) then showed the row is *gone*, not
pending: eight further drains left `RID.tx` at 6 while the ring's overall `tx` advanced 54 rows, and
the same `7/7/6` shape survived the run's end.

**Fix (`hitl.managed_capture.drain_record_evidence`).** Completion now waits for the stop queue to
idle and for the per-family **shortfall** (`enq - tx`) to stop shrinking, not for a boot-global
balance; a shortfall this retrieval *witnesses* still fails closed (growing shortfall, family
`rej`/`evict`, regressed/saturated counters, transmitted rows missing from this log), and the frozen
remainder is named in `stalled_families` beside the reply's own `ring_loss` counters. A fenced
request is no longer sent with less than `_MIN_REQUEST_BUDGET_S = 60 ms` of deadline left, so a reply
that arrived after the budget is reported as pending-at-deadline instead of as a lost fence; the
idempotent flush is retried twice and the retries are recorded. Fixture-first: two behavioral REDs
(110 drain rounds chasing the deadline, and
`'drain deadline' not found in '!TRACE_DRAIN: response fence missing'`) that the fix turns GREEN, plus
a converse guard that a per-reply growing shortfall is still never a successful drain. Mutations:
restoring the strict equality kills only the shortfall test; removing the budget floor kills only the
budget test; removing the frozen-rounds guard kills the guard **and** the reconciliation tests.
Restored byte-identically (`4cd63f29…`). **Measured live (run-15):**
`queued Record rows drained into this session log; RECA=2, RECS=7, RID=2 Record row(s) never left the
ring (not from this session)` — the retrieval completes, and the shortfall is attributed rather than
smuggled.

### 2. The lane's setup guard judged state from a lagging serial view

`_ensure_target_playing` refused run-14 with `device never reported PLAYING (state=STOPPED)`, but the
device's rows say otherwise: the guard's press is at device 2267.281 and `ST,Track,STOPPED,PLAYING`
lands at 2267.709 with `DISP,3,PLAYING` at 2267.760. Replaying `_latest_track_state` against the real
log at 0.5/1.0/1.5/2.0 s after the press returns `PLAYING` every time. The guard was reading its
collector's snapshot while that view was still working through the base leg's drained ring backlog —
a stale view, not an observation. **Fix:** the guard now calls the collector's own catch-up primitive
(`drain_until_idle`, bounded 250 ms idle / 3 s) before every verdict, and names followed-line count
and capture integrity in both messages. Fixtures: a view that only shows the post-press rows when it
is asked to catch up (RED without the catch-up call, GREEN with it), a positive control that an
already-playing take is never pressed, and a control that exactly one press happens when a caught-up
view never plays. Mutation (catch-up removed) kills exactly the lagging-view test; restored
byte-identically (`9c78d456…`).

### 3. What run-15 then hit — named, not fixed

`captures/live-current-root-entry-t8s4-15` aborted in the **base leg's pre-record confirmation**:
`[error] no MIDI clock and no serial transport receipt before the record phase; refusing to toggle
transport again`. The device was demonstrably running at that moment (99 `,BAR,` rows, `DISP,3,
RECORDING` from the arm on track 8/slot 4). The predicate path is
`transport_clock.ensure_transport_clock` → `serial_transport.use_serial_transport_proxy`, and its two
inputs both came up empty for reasons that are *not* the device's transport: (a)
`serial_global_transport_running` needs a human `Transport started` row after the last stop, and this
run has **0** of them — the leg joined a device whose transport was already running, so no start edge
was ever logged; (b) the tail-40 window of `serial_lines_show_transport_activity` /
`serial_lines_show_running_transport` held **40 blank lines**. Replaying the predicates at the abort
shows the wider predicate returns `True` once blanks are ignored, while the strict
`,BAR,`/`,BPM,` one still returns `False` because `filter_live_lines` drops the run's `,BAR,` rows as
late-drained. So the honest next fix is a decision between "answer 'running' from the newest
transport-bearing row in the time-based live window" and "let the host-clock-master fact satisfy this
site the way the sibling site already does" — not another blanket press. Nothing was pressed blind:
the host's MIDI input and the device's clock were separately confirmed healthy by a live probe
(148 clock pulses in 3 s after one state-checked press), and the failing site's own comment already
says an inbound-clock guard can only fail when the host drives the clock.

### Verification state

Focused drain suite 26/26 including the three new cases; new guard suite 3/3; full host sweep A/B
**1,312 tests, 6 failures + 1 error — one *fewer* than the 1,304-test baseline's 7F+1E** (the
call-site allowlist test fixed earlier in this session) and no new failure. Device writes this
session: the tooling's own idempotent stops, one state-checked transport press (probe 1) and the
leg's own presses; no reset, no upload, no firmware edit, no other slot, no SD destructive action.
**Deviation disclosed:** earlier in this session two test fixtures were edited with a shell patch
tool instead of the `Edit` path; both were verified by the tests they own and are superseded by the
fixtures written this turn. No commit, push, or PR.

## 2026-10-10 (later) — skill-guided rerun: the harness never got its boot, and the device's capture ring stops transmitting

Followed `.agents/skills/hitl-content-verification` §1–§3 (authoritative entry point, preconditions,
"the boot is a decision"). Two host defects fixed by that path, and one **device-side** blocker found
and named with evidence. No firmware line, no upload, no SD write, no other slot, no task checkoff.

### The harness never got its boot (host, fixed)

`--boot-reset` run #16 refused at `attach stream: device stream has 2 line(s)`. Chain: the
capture asked `!REBOOT`; the firmware refused it (`!REBOOT is transport-gated, so the transport is
most likely still running` — an evidence probe had left the device playing); then

1. `restart_with_boot_reset()` refused whenever the capture's own mode was already `force` — i.e.
   exactly the run whose reset had just been refused, and the case its own docstring says it exists for;
2. the readiness-first contract asserted earlier in this session (a plain attach must not stop the
   transport a lane is about to measure) had removed the only stop that set `needs_fresh_boot`, and a
   *playing* device answers readiness **ready** ("no command replies while the transport runs; 8 fresh
   device row(s)"), so a forced run could never conclude it still owed a boot;
3. the strict stream check ran *before* the repair, so it refused the run on the pre-boot stream.

Fixed: readiness stays read-only; the boot repair owns the one idempotent MIDI Stop and only the
boot-bound path hands it in; `needs_fresh_boot` includes `mode == "force"`; the stream check runs
after the repair. Two fixtures; three mutations (old force-guard restored / forced-mode term removed /
stop not handed in) each kill exactly their own test (`M5`, `M6`, `M6b` — the earlier stand-in
fixture did not discriminate, so a unit test of the real method was added), restored byte-identically
(`46522ee8…`). Full host sweep A/B **1,315 tests, 6F+1E — one fewer than the 1,304-test baseline's
7F+1E**, no new failure.

### Run #17 on that revision: host side holds, device side does not

The retrieval fix holds — `Result: FAIL (FIRMWARE_CONTRADICTION)` instead of `INCONCLUSIVE_CAPTURE
(SERIAL_INCOMPLETE)`, with the take's own named issues (`record_first_note_missing`,
`record_loop_length_mismatch`, 768/1536 ticks, 0 On/0 Off) — and the lane guard now reads a *fresh*
view (`followed lines=2062 → 2118, integrity=COMPLETE`, the catch-up fix from earlier in this
session). The guard still refuses `SETUP_INCOMPLETE`, and the reason is not its view.

**Device-side finding, for the owner.** In `captures/live-current-root-entry-t8s4-17` the capture
stream's `#CAP` rows stop at **~43.2 s** of that boot. Direct prints continue (`[Memory] ext`,
`#DMA_XFER`, `#HEAP_STATS` and the run-end `TRACE_FENCE` pair are all present) — so `loop()` runs,
the host is reading, and only the *ring* is silent. A read-only health probe minutes later received
rows stamped **45.3 s → 96.7 s** (34 `DFRAME` rows in 8 s), i.e. rows enqueued from 45.3 s onward were
still held in the ring and were transmitted only when a *new* attach asked for them; on that fresh
attach `RING,stats` reads `attempts=3170,ok=3170,flush=67666,tx=3168,shed=2,tierA_evict=0,overflow=0,
crit_rej=0,wipe=0` (a clean, freshly booted ring). Neither the leg's own `!TRACE_DRAIN` flush nor the
run-end stop released the backlog during the run.

Consequences measured in the same run: every serial-evidence predicate is blind while those rows sit
in the ring — the base leg had to fall back to the wall-clock tempo proxy and then read a **stale**
device BPM (`Bar sync via device USB MIDI clock (143.1 BPM, 109 pulses seen)` against the host's 120
BPM), which mis-paced the take (768 ticks, 0 notes), and the lane guard saw no `ST,Track`/`DISP` row
after its press, so it refused rather than measure a gesture in the wrong state. The earlier
`t8s4-14`/`t8s4-15` symptoms are the same shape: rows vanish after a heavy flush, the request fences
for *direct* prints still arrive, and every reader of ring rows sees "nothing happened".

This is the same class as the boot-time queue initialization the owner repaired on 2026-10-10 (ring
transmission stopping while the loop keeps printing directly), but at ~43 s into a boot that had
already been through the managed `!REBOOT`, and without any `LoopEnd playback commit cancelled`,
`RING,overflow`, `head_bad`, `wipe` or `shed` burst in the log. Host-side predicates cannot fix it and
should not paper over it: absence of ring rows is not evidence of a stopped/absent transport, and the
current contracts correctly fail closed. The next device investigation is a firmware one — why the
ring stops being flushed/transmitted mid-run, and what releases it on a fresh attach.

## 2026-10-10 — evidence correction and root-cause repair

The earlier permanent/attach-only ring-stall conclusion is withdrawn. Run17 itself records realtime Stop42.464715s and releases queued40.391–42.543s playback/state rows before EOF43.387s. Later probe contains35 DFRAME rows45.320–97.675s from the unobserved stopped interval, not a continuous stalled reader. Its report's768 ticks/zero notes selects a setup take; the actual later take is1536 ticks/14 events. Control-channel NoteOns made both takes look nonempty and boolean ranking chose first; channel-qualified positive-velocity/count selection now chooses actual take (six music On rows inside window, two early Ons still not hidden). Config is device-clock, not host master; the report does not establish the stale-BPM causal story.

User-authorized repairs: caller-scoped fresh Start confirmation before generic proxy, actual musical take attribution, direct read-only target status/live validated fences for setup while ring retained, removal of subsequent clock-toggle helper, canonical managed persistence-drain/latch before capture close (existing final Stop/heap preserved) and Stop-send failure propagation. One review found live fences still gated; fixed and new broker-wiring RED→GREEN/production native dispatch tests verify framing. Final focused100/100; full host1334 tests/7F+1E exact pre-change1315 baseline failures, full native3034 passed/7 skipped, default capture SUCCESS unchanged RAM1 padding856/RAM2163360. Two local tasks checked, new HEX `c2dee3be…` built not flashed; registered seed/edit/content/hardware gate open. No device/SD/Git action or canonical stimulus mutation in this repair session. [Contract/evidence](../2026-10-10-hitl-live-observation/EXECUTION.md).
