# S7.5b.8b — bounded entry hardware clear diagnosis

## G2 — host observation repair execution packet (frozen before edit)

Acceptance: the existing single-owner capture broker forwards only validated read-only `!SLOT_BYTES <track> <slot> <offset>` (0..7, 0..7, uint32) and `!CAPTURE_CONTENT_SLOT <slot>` (0..7), framed by distinct 32-character lowercase-hex fences. Existing argument-free observations remain compatible. Mutators, unknown names, extra/missing arguments, non-ASCII decimals, control/newline injection, and invalid fences produce no serial writes.

Scope: capture broker validation and host parser/wire tests only. No firmware, stimulus, EMPTY/readiness predicate, timing policy or serial marker changes. Tests drive existing `parse_command_request` and `_service_command_requests` before implementation; valid scoped queries currently return None and write nothing (behavioral RED).

Device scope: operator track 8 / slot 4 only, firmware 7/3, channel 8. Clear authorized only for that slot; no quarantine, SD rename/delete, other-slot clear. Uploaded default HEX SHA-256 `9e8270b41551ae4609b44362ad7fb696a2a6e6f0a4ad7e39184f757ce90b3dec`; no further upload/reset planned. Use managed capture and canonical shutdown on every exit. Selected-track content query requires fresh stopped target proof first.

## Existing attempts

Attempt 01 uploaded successfully but setup failed on reboot acknowledgement/short capture, before clear or record. Attempt 02 selected 8/4; first clear refused post-load holdoff, retry logged persisted slot-4 payload absent, but runtime EMPTY/clear completion was not confirmed. Host aborted before recording or NOTE_EDIT. Capture Tier-A evictions=185: missing rows do not prove runtime failure or successful emptiness. Recovery reached two verified stop/persistence-idle cycles. Scoped slot-byte query was rejected by host broker, not firmware.

Hardware lane/content/timing gates remain open. Persisted payload absence is not a runtime EMPTY oracle.

## G2 — disabled fixture stop-trigger repair (frozen before edit)

Attempt 03 used no-clear after fenced runtime-empty proof, same image/no reset. Setup passed but real `_stream_fixture_record` raised `UnboundLocalError` when stop-ahead=0; no valid seed or edit pass. Recovery used idempotent Stop then two verified persistence-idle cycles, runtime target still zero rows/length. Scope: initialize existing optional trigger to None, preserving negative at-end and positive stop-ahead behavior. Regression drives real streamer with fake MIDI ports/master and mocks only stop send: one bar emits exactly its clock count, zero stop for disabled, one for negative/positive. Run fixture first for behavioral RED, then relevant host suites. No firmware/stimulus/timing-policy change.

## Verification and terminal device state

- Broker baseline: three valid scoped parser cases fail; control-whitespace acceptance also exposed. Initial wire test incorrectly treated two physical lines as one request; corrected before green. Parser still explicitly rejects embedded LF. Scoped grammar rejects all mutators/extra arguments/invalid fences/control/non-ASCII/overflow values. Combined capture suites97 tests OK.
- Disabled stop-trigger baseline reproduces exact UnboundLocalError; negative/positive modes remain green. Initializing existing optional trigger to None yields combined98 tests OK (`python -m unittest scripts.test_fixture_record_stop_trigger scripts.test_capture_observation_commands scripts.test_managed_capture scripts.test_capture_transitions scripts.test_hitl_capture_integrity`). No firmware rebuild/native rerun: firmware unchanged from prior3034 total/3027 pass/7 skip checkpoint.
- Fenced probe `captures/live-current-root-entry-t8s4-observe/session_20261010_130906.log` proves STOP_OUTPUT track7/slot3, length0, record0/effective0, persisted missing; then two verified shutdown cycles.
- Attempt03 `captures/live-current-root-entry-t8s4-03/session_20261010_130940.log` aborts on host stop-trigger error. Recovery `session_20261010_130959.log` proves empty target, then two verified shutdown cycles.
- Attempt04 authoritative invocation: `.venv/bin/python scripts/host_midi_hitl.py run --preset note_edit_reselect_stacked --track-number 8 --loop-slot 4 --midi-channel 8 --no-boot-reset --capture-boot-wait 30 --no-clear-before-record --out-dir captures/live-current-root-entry-t8s4-04`, exit1. Base sent8 notes but serial report only1 On/open; length768 vs1536, unmatched-open/span/call-site issues, first-note latency23 clocks vs12 (advisory). Scenario entered NOTE_EDIT once but fixture source step8 pitch67 unavailable; three source select attempts failed. `NOTE_EDIT_OPEN,sync` is explicit compatibility-path evidence, not live-lane admission.
- Terminal recovery `captures/live-current-root-entry-t8s4-observe/session_20261010_131153.log`: fresh target7/3 stopped, scoped content length768 and exact record/effective note60 On328/Off384, persisted payload377 bytes. One idempotent Stop resolved running transport; two further canonical stop/drain cycles reached persistence idle. No incomplete-shutdown latch, reset/upload or repeat clear. Preserve this failed take.
- Ring cumulative Tier-A eviction count remains185, overflow0/critical rejection0; no claim of loss-free full boot capture. New lane/content parity/timing gates remain unqualified. Next work is controlled seed/clock pacing diagnosis plus settled-current-root admission proof, not blind repeat runs.
- **Seed/clock pacing done (2026-10-10) — see [SEED-TOGGLE-REPAIR.md](SEED-TOGGLE-REPAIR.md).** The seed failure was a *host* transport-toggle defect: the record-arm precondition read `_serial_sequencer_running` (whose marker list counts `#CAP,DFRAME`, the display-frame monitor a stopped device emits every ~1.504 s) as a running transport, so it pressed the toggle onto a stopped device, started the transport, and `_restart_transport_after_clear` stopped it 1 s later. Fixed with `serial_lines_show_running_transport` (`,BAR,`/`,BPM,` only) plus a windowed post-stop re-check. Rerun `captures/live-current-root-entry-t8s4-05` (authorized clear/overwrite of track 8 / slot 4 only, same HEX `9e8270b4…`, no upload/reset) committed **1536 ticks / 8 On + 8 Off / 0 unmatched**, first note 2.626 ms (0 clocks), stored ticks exactly `2,192,384,576,864,1056,1248,1344`, and no spurious transport press. Terminal `DeviceSession` drain `complete/2/ok`, no latch. The leg's own oracle still fails (`selected_note_moved`, `stacked_reselect_after_deselect`) — the S7.5b.8b live-entry measurement stays open, not a seed artifact.


## 2026-10-10 — post-boot display delay: the boot-screen handoff, and a corrupted edge-effect repeat loop

Owner report: after the board came back on USB the **display showed nothing for minutes**, then came up.
Read-only diagnosis (managed capture with `boot_reset="never"`, one allowlisted fenced query, no reset and
no MIDI) — `captures/device-health-probe/session_20261010_151952.log`:

- **It is not a boot failure and not the bootloader.** The board enumerated as the firmware's own USB device
  (`Teensy MIDI`, VID `0x16c0`, **PID `0x0489`**) with `/dev/cu.usbmodem154944801` present, not as HalfKay
  (`0x0478`). It **answered** a fenced `!CAP_IDENTITY_STATS` (5 rows), so the main loop and MIDI service run.
  It had genuinely rebooted: the cumulative ring counters were *lower* than the 14:01 session
  (`attempts 182,515` vs `315,220`, `flush 954` vs `3,559,747`), and the device clock read 175–183 s at
  15:19:52 → boot ≈15:16:50, matching the device node's mtime.
- **The display's normal UI is gated, deliberately** (`src/main.cpp:430`): `finishBootSetup()` — the handoff
  from `drawBootScreen()` to the live UI — runs only while `bootSlotLoadRefreshPending &&
  StorageManager::bootInteractiveReady() && !timingCriticalTrackActive`, where `timingCriticalTrackActive`
  means *any* track is `isRecording()` or `isAudiblePlaybackActive()`. Until it runs, the panel keeps the
  boot screen; the boot restore itself is sliced per frame (`LoadLoopBudget::resolveLoadLoopSliceBudgetUs`),
  so it needs main-loop iterations to finish.
- **The loop was saturated and corrupting.** In the 8 s window: **0** `DISP`, **0** `DNTE`, **0** `DFRAME`
  rows, but **2,944** `LED` rows (a full control-surface refresh cycling at ~17 Hz), and
  `RING,stats … shed=145650, tierA_evict=675, overflow=0, crit_rej=0` (14:01 comparison: `196,136`/`5,639`).
  The direct logger lines were a **repeat loop**: `[175.345] [INFO] LoopEnd playback commit cancelled
  track=76 target=194 (no RAM data)` every ~169 ms. The same row family is in
  `captures/live-current-root-entry-t8s4-02/session_20261010_124554.log` (12:45, 1,045 rows split
  **523× `track=17 target=3`** and **522× `track=12 target=194`**, from ~81 s after that boot).
- **Those values are corruption, not a legal state.** `TrackManager::drainEdgeEffects()` (main loop) logs
  them straight out of `gEdgeEffectQueue`, and the only recorder,
  `TrackManager::recordEdgeEffect(kind, track, fromSlot, toSlot, …)` in
  `src/TrackManager/TrackManagerTransportTick.cpp`, is called with a track index `0..7` and a
  `targetSlot` `0..7`. `track` 12/17/76 and `target` 3/194 are outside both ranges.

**Answer to the report, with its limit stated:** the minutes-long wait is the *boot-screen handoff* waiting
on an un-finished, starved boot restore and on no track being audible/recording — while the main loop spends
its iterations on the corrupted edge-effect drain, and the capture ring sheds the display's own rows. That
explains a late UI; it does not prove the panel was unlit rather than showing the boot screen. **Named, not
claimed:** the corrupted `EdgeEffectRecord`s are a firmware defect of their own and were **not** investigated
or touched here (no firmware authorisation, and the tree is mid-flight); the heavy ring shedding means the
missing `DISP`/`DFRAME` rows are not proof that the UI was down. No device write beyond the allowlisted
read-only query; no reset/upload/SD action.

## 2026-10-10 — S7.5b.8b live-lane rerun on the edge-effect-boot fix: the criterion is not exercised

Owner asked for a rerun to validate that NOTE_EDIT entry / Select preparation is MIDI-boundary
resumable aware, with the criterion *"switching to note edit needs to keep the state at playing"*.
The board was flashed with the owner's **edge-effect boot fix** (`src/TrackManager.cpp`,
`src/TrackManager/TrackManagerTransportTick.cpp`, `include/Utils/EdgeEffects.h`, 16:04; uploaded
16:40) and that fix is **confirmed working**:

- Post-upload read-only probe `captures/edge-effect-boot-upload-post`: `RING,stats attempts=113,
  ok=113, **shed=0, tierA_evict=0**, overflow=0` (pre-upload `shed=65324, tierA_evict=226`; my
  15:19 probe `shed=145650, tierA_evict=675`), **0** `LoopEnd playback commit cancelled` rows (was
  a ~6 Hz repeat), boot markers `BOOT,scan,t1..t7 / BOOT,scan,done / BOOT,load,ok`, and normal row
  proportions (`DISP 1, DFRAME 8, LED 151` vs `0/0/2944`). The corrupted-edge-effect repeat and the
  display stall are gone.

**Rerun verdict (three legs: `captures/live-current-root-entry-t8s4-07/08/09`): the leg FAILS and,
more importantly, the stated criterion is never exercised.** All three report the same issues —
`selected_note_moved`, `stacked_reselect_after_deselect` — with
`Timed out waiting for stable select at tick 384 (±24)` and **`playing_motor_sync_judged: 0`**, i.e.
the verifier found no playing interval at all during the edit gestures. In every leg:

- the scenario leg's own attach sends the idempotent realtime Stop
  (`MI,U,252,13,0,0` — run-08 742.363183, run-09 885.610960);
- 78 ms later the take's playback stops (`Playback stopped @ tick 1652`,
  `ST,Track,PLAYING,STOPPED`);
- **no host gesture restarts it**: the next press is the track-select, ~12 s later, and there is no
  later `ST,…PLAYING` row before the NOTE_EDIT entry; the gestures therefore run on a **stopped**
  track.

**Host-side fix carried in this rerun (verified, but not sufficient):** `clock_seen_within`
(`hitl/transport_clock.py`) answered "is a clock flowing?" from messages that had arrived *before*
the question — including through the port-owning reader's own `poll()` — so
`_ensure_transport_running` saw the clock the device had sent while the take was still playing and
skipped its start press. It now discards that backlog first (bound 256, via `hitl.midi_io`'s drain
owner, which gained an optional `max_messages`). Two new tests, and the full host sweep A/B keeps
the identical pre-existing failure set (1,304 tests). **Measured limit of the clock proxy:** with
the drain in place, `_ensure_transport_running` *still* saw a fresh clock and still did not press,
while the device's own `!STOP_STATUS` reported `state=STOPPED` (probe
`captures/transport-state-after-attach-stop`) — so on this firmware **a MIDI clock flows while the
transport is stopped**, and "fresh clock" is not a valid proxy for "the take is playing".

**Owner fork, stated rather than guessed:** either (a) the firmware's realtime-Stop handling leaves
the take stopped while still clocking (and the entry must keep/restore PLAYING), or (b) the preset's
second leg needs a serial-verified "target track is PLAYING before the edit gesture" precondition
(the base leg already ends with the take playing; the attach is what stops it). (a) is firmware and
was not touched; (b) needs the right resume gesture, which is firmware semantics — so no change was
made beyond the clock predicate.

No firmware edit, no marker, no stimulus change, no upload/reset by this session; the only device
writes were the tooling's own idempotent stops and allowlisted read-only queries. Terminal canonical
`DeviceSession` drain `complete/2/ok`, no latch.

No architecture/firmware owner change, new marker, canonical stimulus edit, timing-policy change, other-slot
clear, quarantine, SD rename/delete, commit/push/PR. Strict active OpenSpec validation and whitespace checks
pass.

## 2026-10-10 — edge-effect boot initialization: investigated and repaired locally

The bounded follow-up found a concrete target initialization defect: `gEdgeEffectQueue` and its reported-drop baseline reside in uncleared RAM2 `.bss.dma (NOLOAD)`. The cached ELF/HEX exactly matched the uploaded `9e8270b4…` image, with no queue dynamic initializer and only ordinary DTCM `.bss` cleared by Teensy startup. Thus stale positions/payload can be consumed repeatedly without any accepted producer write. A sanitized scratch probe passed262144 valid FIFO records/four uint16 rollovers/16384 full rejections; a controlled stale-memory image produced65535 phantom records/4096 identical invalid cancellation rows. Captures confirm50 identical76/194 rows in health probe and1045 alternating12/194 (523) and17/3 (522) rows in attempt02; the earlier523/522 attribution above was reversed. Actual first queue bytes and an additional stray writer remain unobserved; display recovery is not established by this mechanism alone.

Owner authorized OpenSpec capture and repair. `TrackManager::setup` now initializes queue positions/drop count and reported baseline before clock startup; payload stays untouched/unpublished. Production fixture RED at depth65535 (plus SIGHUP/ERRORED), then focused190/190; full native3030 passed/7 skipped/0 failed,112 suites. Default capture build SUCCESS, unchanged RAM1 variables96192/code392360/padding856 >=840 and RAM2163360; linked cold initializer/four metadata stores/setup order verified; one focused review APPROVE. Local task checked; separate hardware boot/display task open. New HEX `29b62f4d829f28937b558422ececfccd2ddf261960754eecd4bf8950c38df50d` built NOT uploaded. No reset/serial/MIDI/SD action, canonical stimulus/marker edit, budget rebase, commit/push/PR. [Full execution evidence](../2026-10-10-edge-effect-boot/EXECUTION.md).

## 2026-10-10 — authorized repaired-image upload and boot smoke

Owner approved upload-only. Preflash managed capture saw60 repeated corrupt cancellation rows; two idempotent Stop/fenced STOPPED/verified-persistence drains completed before flashing. Default `pio run -e teensy41-capture-serial -t upload` SUCCESS, repaired HEX `29b62f4d…` unchanged, RAM1 budget unchanged; serial reconnected. Postflash managed capture answered identity/stop queries, emitted fresh crash0 boot marker, boot_commit14.169s and1 DISP/8 DFRAME rows, with0 cancellation/drop-warning rows. Initial fenced ring stats shed0/Tier-A eviction0/overflow0/critical rejection0; sample precedes boot_commit, not a terminal loss guarantee. Postflash two Stop/drain cycles also complete, no latch. No recording/slot clear/SD-destructive action. Repeated drain is absent in this bounded capture and display activity returned, but physical panel illumination/content integrity/normal queued effects were not qualified. Hardware task remains open. [Upload evidence](../2026-10-10-edge-effect-boot/EXECUTION.md#authorized-upload-and-bounded-boot-smoke-2026-10-10).
