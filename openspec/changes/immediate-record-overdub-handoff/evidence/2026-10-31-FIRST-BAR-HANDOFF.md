# 5.2 — first-bar playback handoff (2026-10-31)

Scope: `tasks.md` § 5.2 only. Owner report: after Record stop the take sounds only from bar 2.
Capture: `captures/session_20261031_121645.log`, track 7 / slot 3 (zero-based), 20-bar take.

## Reproduced interleaving

The capture is **Record → transport Stop → transport Start**, not a Record stop into PLAYING:
`RECS,stopToStopped,3,15336,…,15360` at 160.372 s, then `Playback started @ tick 15336` at 160.717 s.
`startPlaying` re-anchors the projection, so phase zero is the Start tick and bar 1 is owed at once
while the Record job is still private. Readiness rows: the frozen `[0,768)` ready completes, is retired
before any adoption attempt, and the first adoption is at the bar-2 boundary (162.720 s).

Native fixture (production Track/clock path, no stop drain, no pre-admitted window, one owner grain
per tick plus `serviceRollingPlaybackWindow` / `stepPlaybackViewMaintenance` / `playMidiEvents`):
`exerciseFirstBarAfterStopToStoppedRestart` in `test/test_playback_prewarm/test_playback_prewarm.cpp`,
registered in `test_frozen_record_reader_survival`. 20-bar take, distinct note per bar (bar 1 = pitch
60 at 12..96), stop 24 ticks (capture shape) and 2 ticks before the wrap, 16 stopped owner turns, then
Start at the same tick; a third case runs three owner turns per tick. The oracle asserts the per-tick
emission count from phase 0 and the exact NoteId / pitch / storage tick / velocity / channel of bars 1
and 2, each exactly once (four events total, no repeat at the next boundary).

## Two defects, each independently necessary

1. **Stale raw bar 1 served after a STOPPED stop.** The PLAYING stop route refuses a retained raw bar
   prepared before the take's input inside `bindFirstRecordingBar`. The STOPPED route returned before
   it, so the deferred closure grain's `bindDeferredRecordReturnWindow` bound that pre-input (empty)
   bar as the `firstRecordReturnWindow`. The hot path served it for bar 1 (0 events) and, as `current`,
   it blocked mid-window adoption of the real frozen window; the owner then retired that ready as late.
   **Fix:** the discard is one helper, `LoopPlaybackRuntime::discardStaleFirstRecordingBar`, used by
   `bindFirstRecordingBar` and now also on the STOPPED destination of the Record stop edge.
2. **Mid-window Capture adoption served one tick.** DEC-136 — frozen return reader covers the
   playhead's interval adopts a complete Capture-token ready mid-window in
   `Track::adoptOrServiceOnwardRecordWindow`, but its "keep the already-served current" fallback
   accepted only `Committed` origin, so on the next tick the adopted window was dropped for the empty
   merged fallback. **Fix:** the fallback accepts the same token-qualified Capture identity as the ready
   branch, and only for the exact window that branch adopted: `LoopPlaybackRuntime::frozenCaptureAdoptedStorage`
   holds the low 16 bits of that window's `storageGeneration`. It sits in existing padding, so the host
   (`9,984`) and target runtime size pins and RAM1 are unchanged. No new owner, allocation or clock-edge
   work; the frozen reader contract is unchanged.

## Independent review

REQUEST CHANGES, two findings, both addressed:

- **F1 (medium):** a live rolling Capture view carries the same content token as the frozen range
  (`captureContentGeneration` does not advance on append), so a token-only fallback could serve a live
  view kept as `current` after a refused inline PLAYING-stop handoff (note held at stop, truncation
  rewind), missing its closure Off. Repaired by the adopted-window identity above. The added control
  `test_held_note_playing_stop_never_serves_the_live_capture_view` passes **with and without** the
  identity, so it is a regression control only; it does **not** reproduce F1. A discriminating fixture
  for F1 remains open.
- **F2 (low):** the STOPPED-route discard ran unmasked. `discardStaleFirstRecordingBar` now takes
  `PlaybackSourceReadGuard` itself (primask save/restore, safe nested in `bindFirstRecordingBar`).

Mutation check: reverting (1) alone leaves the empty first-return window `current@0` (the device
shape); reverting (2) alone leaves the frozen window `current@0` with the reader idle. All three cases
are RED under either mutation and GREEN with both.

## Gates

| Gate | Result |
|---|---|
| New cases (`test_frozen_record_reader_survival`) | RED before (bar-1 NoteOn missing at phase 12); 13 / 13 after (3 first-bar cases + held-note control) |
| Full native, pre-change tree + new REDs | 3,749 cases / 7 skipped / 3,739 succeeded / 2 failed (the two new REDs only) |
| Full native, final tree (isolated build dir) | **3,750 cases / 7 skipped / 3,743 succeeded / 0 failed**, no ERRORED suite |
| Target `teensy41-capture-serial` | SUCCESS — RAM1 `variables 96,192`, ITCM `393,216` (code `391,336` + padding `1,880`), `locals 34,880`; unchanged |
| HEX (final, post-review) | `sha256 04fe99481f41d3c9c8285b790686965585ac05c1c00d1344b4e4952f109c1234`, retained under `captures/first-bar-handoff-04fe99481f41/` |
| Flash / boot (final) | Uploaded (`teensy-gui`, exit 0); fresh serial `boot_reset,crash=0`, `BOOT,load,ok`; USB `16C0:0489` Teensy MIDI. This image is on the board. |
| Superseded pre-review image | `9821c2a0…`, retained under `captures/first-bar-handoff-9821c2a03f94/`, flashed and boot-verified, then replaced |
| Independent review | REQUEST CHANGES (F1 medium, F2 low); both repaired, F1 without a discriminating fixture |
| Device content gate | **PASS ×2 on `f761c65d…`** (runs 4–5, owner rig sequence, `16ths.mid`): bar 1 16/16 at the recorded phases, no extras; bars 2–3 complete. Run 2's "silence" was a test artifact (see run 2 correction) |

The 5.1 review blocker (equal inclusive endpoints) was already fixed in the working tree before this
slice; both of its fixtures pass in the full native run above.

## Device run 1 — single notes, device clock (owner-authorized scratch: track 8, slot 4)

Stimulus: TEMP `STIM-BASE-2-FIXTURE-01-TEMP-FIRSTBAR` (unregistered; scaffold output and driver
`captures/stimuli/STIM-BASE-2-FIXTURE-01-TEMP-FIRSTBAR.{yaml,recipe.md,run.py}`). The driver uses
`hitl.actions` gestures and the device's own MIDI Start/Clock (96 clocks per bar). It selects track 8,
clears slot 4, proves LOOP_EDIT, presses transport Start, arms slot 4, and sends pitch 60+j on
channel 8 at bar offsets. It then presses transport **Stop** inside the last bar, waits about 345 ms
(measured 0.47 s), presses Start and records 3 bars of device output. Evidence:
`captures/firstbar-stopstart-04fe9948-01/` (`serial.log`, `midi-events.json`, `run.json`).

- Take: `RECS,stopToStopped,3,1499,986,3585,3072,0`, so record start was tick 986 and the final length
  is 3,072 (4 bars). Sent notes land at take phases 574 (pitch 60), 1342 (61), 2110 (62), 2878 (63).
- **Oracle (stated before the run): PASS.** After the second Start, pitch 60 sounds once at clock 72
  (expected 574 / 8 = 71.75), 61 once at 168 (167.75) and 62 once at 264 (263.75). There are no
  duplicates and no repeat at the next boundary.
- **Unexplained (blocks acceptance):** 7 extra channel-8 NoteOns in bar 1, pitches 81→75 (velocity
  100) every 6 clocks from clock 4 to 40. `MO` rows put them on channel 8 (tick 24, then +48 each).
  They stop exactly when track 8's Record root publishes (`playback_build,record_overlay` /
  `snapshot` at 3919.73 s, about 0.98 s after Start). They did **not** sound in bar 1 of the first
  transport pass, when every other track also started from phase 0. Only track 8 slot 4 is active on
  that track. Before the clear, slot 4 held committed content (`disp_src … crev=1,cpass=1`). The
  content is identified by run 2's pattern: `16ths.mid` is the descending run 84→69, one note every
  6 clocks at velocity 100, so these 7 notes are the slot's old `16ths` take. The serving route that
  played it before publication is still **unidentified**. With the
  stale raw bar now refused (`Deferred Record return bind refused slot=3; frozen source retained`),
  the ready built while STOPPED was the stop-phase window `[768,…)`, not `[0,768)`, so bar 1 had no
  frozen window before publication.
- Not compared against the pre-fix image on the device. Whether this wrong pre-publication content
  is new or was previously hidden behind the silent bar is not known.

Next: identify the serving route for track 8 in `[3918.756, 3919.733)` (`fixedView` source: committed
current fallback, merged fallback or paged). A native fixture should record over a slot that held a
committed take and was cleared, then assert that nothing but the new take sounds before publication.

## Device run 2 — owner rig shape, `16ths.mid`, host clock (owner-authorized scratch: track 8, slot 4)

The driver was rebuilt on the owner's rig (`scripts/hitl_midifile_chain.py` helpers, reused):
`MidiClockMaster` host clock, verified slot clear, LOOP_EDIT proof and view-prewarm wait. It records
`/Users/eelkejager/Documents/Ableton/2025/droid_midilooper_test Project/16ths.mid` for 20 bars on
channel 1, presses Record before host Start, and stops ~5 clocks before the boundary with global
transport Stop. The clock is held (`send_realtime_stop`, as `--hold-clock-on-settle`). Host Start
follows 0.345 s later (measured 0.349 s), then 3 bars of device output are captured. Evidence:
`captures/firstbar-16ths-04fe9948-01/` (`run.json`, `midi-events.json`, `serial-log-path.txt` →
`captures/session_20261031_143956.log`).

- Preconditions: LOOP_EDIT proven, clear `cleared` on attempt 1, view prewarm ready.
- Take: `RECS,stopToStopped,3,15336,120,15216,15360,0`, identical field for field to 121645.
- **Oracle (stated before the run): FAIL.** Bars 1–3 after Start: 0 of 16 pattern NoteOns on
  channel 8, and no other channel-8 NoteOn. The first channel-8 NoteOn is pitch 81 at pulse 314
  (bar 4), and the pattern then runs normally.
- Serial (ring `overflow=0` but `tierA_evict=23,904`, so absent rows are not evidence): with the host
  clock held, device ticks keep advancing while stopped, and the frozen owner had `[0,768)` **ready
  at Start** (`adopt_wait,e2_boundary … 0,768`, mask 76, 189.470 s). Start resets the device clock
  (`Playback started @ tick 0…5`), and 3 ms later the ready role is empty with no `adopted` row.
  `[768,…)` and `[1536,…)` then complete and wait. The first `adopted` is at 193.945 s, and channel 8
  sounds from pulse 314.
- The device saw Start ~6.8 s after the transport Stop (`RECS` 182.66 s, `Playback started`
  189.47 s), although the host measured 0.349 s between its Stop and Start sends. Unexplained.
- **Correction (superseded):** the "silence" is a **test artifact**, not the firmware. A short press acts
  only after the device's tap window, so the record-ending transport press fired after the driver's own
  realtime Start and stopped the transport again (`Transport started` 89.393 s → `Transport stopped`
  89.506 s in the diagnostic rerun). The device stayed stopped until the driver's final press. The
  "bar-0 ready lost at the clock reset" reading above is withdrawn. Once the transport actually ran, bar 1
  was 15/16 (the next section).

## Runs 3–5 — the rig's own restart, route diagnostic, third defect

**What the rig really does.** The rig ends the take with a transport press plus a realtime Stop (clock held).
The Stop ends the take at once (`stopToStopped,3,15336,…`). The press acts after its tap window, finds the
transport stopped and **restarts** it (`Global Transport (short)` → `Playback started @ tick 15336` →
`Transport started`, ~345 ms later). That is exactly the 121645 sequence, so the driver (v3) adds no restart
gesture. It anchors bar 1 on the serial-arrival pulse of `Transport started`, fits the exact origin on bars 2–3
(±24 clocks, never on bar 1), and expects the take's own phases (the pattern rotated by the `RECS` record start).
Scorer: `captures/stimuli/STIM-BASE-2-FIXTURE-01-TEMP-FIRSTBAR.score.py`.

**Diagnostic image `9a4711b8…`** adds a session-capture-only row, `win_frozen_readiness,…,route,turn,…`
(`Track::reportFirstBarRouteForSlot`). Route: 0 none, 1 bridge, 2 view, 3 paged, 4 merged, 7 no audible
source, 8 runtime reset. Cost: ITCM +336 B (padding 1,880 → 1,544).

- **Run 3** (`captures/firstbar-16ths-9a4711b8-05/`, serial `captures/session_20261031_145510.log`): bar 1
  **15/16**. The first NoteOn (81, phase 24) is missing; bars 2–3 are complete. Rows sorted by `#CAP` time:
  while STOPPED the owner prepared `[14592,768)` (the stop phase's bar 20). After Start the projection is
  re-anchored to phase 0, bar 1 is merged-fallback silent (`route=4`, 0 events), the bar-20 ready is retired
  (`record_ready_late`), and `[0,768)` is built only after Start and adopted at phase 40.
- **Third defect and fix** (DEC-235): while STOPPED the frozen owner's phase is 0, so bar 1 is prepared before
  Start. Native RED first: a note at phase 2 after a stop-to-STOPPED restart (`…early_event…`) was missing at
  phase 2 with no ready window; it is GREEN after the fix.
- **Runs 4–5 on `f761c65d…`** (HEX `f761c65d9068f05ee046f053ea1c68609e875511641a7fba0f9874611a11f86a`, retained
  in `captures/first-bar-stopped-phase0-f761c65d9068/`; boot `boot_reset,crash=0`, `BOOT,load,ok`,
  `16C0:0489`): `captures/firstbar-16ths-f761c65d-01/` and `-02/` (serial
  `captures/session_20261031_150718.log`, `shed=4`, `tierA_evict=0`, `overflow=0`). **Bar 1 16/16** at the
  recorded phases, 0 extras; bars 2–3 16/16. Serial: `complete_ready … 0,768` 80 ms after the stop while
  still STOPPED; after Start bar 1 is served from that frozen view (`route=2`, 31 events); no `record_ready_late`.

| Gate (final tree) | Result |
|---|---|
| Frozen-reader suite | 23 / 23 (incl. 10 new first-bar cases and the early-event RED → GREEN) |
| Full native (isolated build dir) | **3,760 / 7 skipped / 3,753 succeeded / 0 failed**, no ERRORED suite |
| Target | SUCCESS — RAM1 `variables 96,192`, ITCM code `391,672` + padding `1,544`, `locals 34,880` |

## Follow-up capture `session_20261031_151347.log` (owner, 12-overdub MIDI-file chain) — read-only

No boot banner, so the image is unproven from the log; the board's last flash was `f761c65d…`. Metrics are
measured inside the overdub chain (first `ODUB,stage,enter` → last `OVERDUB_STOPPING → PLAYING`) and compared
with 25 earlier rig sessions (scripts in the session scratchpad; method only, no new tooling).

- **MIDI timing:** the median per-window MIDI-service gap is 101 ms, against 37–83 ms on 10-30. Worst
  `late_on` in the chain is 176 ms, within the historical 59–196 ms. The 513 ms `late_on` after 558 s is a rig
  tail artifact: the closing transport press restarts the transport after its tap window (541.665 s), then
  `MIDI clock lost, switching to internal`, and every track measures ~513 ms late on the unsynchronised clock.
- **Named cost:** display frames over 50 ms (`loop_rem,display_frame`) number **189** (median 85, p90 149,
  max 154 ms), against 0–16 per 10-30 session and 45 in 121645. `cer_win` (committed-range window gather ≥ 10 ms,
  `lists=1, chunks=3`, 512–654 events: the 20-bar Record pass) runs **206** times, against 13–29 before.
  Stale published roots during the chain are normal in every session (`lcr,src … root_rev < play_rev`: 31 on
  10-30 19:47, 45 now). **Attribution (high confidence, not instrumented):** the stale-root draw-source fallback
  (`DisplayNoteWindowGather.cpp`, committed as `acb64a4c`, present in every image built on 10-31) calls
  `LoopContentResolution::resolveDisplayNotesInWindow` over all committed passes for the whole 16-bar window on
  each gather. Before, a stale root was drawn directly: cheap, but it blanked committed overdub rows (the
  DEC-199 defect). `gDrawSourceCommittedPassGathers` is not reported in any row, so this is correlation plus the
  matching call shape, not a counted proof.
- **Not the cause:** the circular paint origin. Unaligned window starts occur at the same rate (12–20% of
  beats per state) in every session since 10-30.
- **Temporary hides:** painted-note drops of 27–59 that recover within ~3–6 s or at the overdub commit
  (500.8, 515.2, 530.5 s; one right after a raw-only stop). The same class exists on 10-30 19:47 (−42) and
  20:23 (−55), so it **predates** the 10-31 display work: task 5.3, with identities unproven (no `draw_src`
  rows in this capture).

### Proof and fix — stale-root pass walk per live-input frame (DEC-236)

- **Mechanism.** `drawSourceCommittedSourceKeysMatch` compares `captureDisplayRevision`, which advances on every
  accepted live note. With an overdub open and the root stale, every frame failed reuse and fell to the full
  rebuild, which for a stale root is `resolveDisplayNotesInWindow` over every committed pass for the whole
  16-bar window.
- **Native proof (RED → GREEN).** `test_stale_root_committed_base_is_reused_while_live_overdub_input_arrives`
  (`test_live_capture_display_boundary`) runs production `resolveDisplayNotesByDrawSource` for 12 live-input frames
  at one window. Before: **12 pass walks** (one per frame). After: 0. The record and committed-overdub rows are
  present on every frame.
- **Fix.** One predicate, `committedDrawSourceIsStaleRootPassGather`, now drives both the gather branch and the
  reuse key. For that source only, the key ignores `captureDisplayRevision` (live rows stay an overlay,
  re-applied on every compose). Committed changes (`playbackRevision`, root, visual cache) still force a rebuild.
- **Gates.** Display suite 53/53. Full native **3,761 / 7 skipped / 3,754 / 0**. Target SUCCESS, RAM1 unchanged
  (`96,192`, ITCM padding 1,544). HEX `e7f134a51fc262c46698bae9ef8bba0178076152970e615e6c3840e1665f58dd` is retained in
  `captures/display-reuse-e7f134a51fc2/`. Boot verified (`boot_reset,crash=0`, `BOOT,load,ok`, `16C0:0489`).
- **Device (owner rig command → `captures/midifile-chain-49/`, serial `captures/session_20261031_153427.log`,
  `shed=1`, `tierA_evict=0`, `overflow=0`; 12 overdubs, same 47 s span):**

  | Metric | 10-30 19:47 baseline | 151347 before | 153427 after |
  |---|---|---|---|
  | MIDI gap median / max | 58 / 200 ms | 101 / 262 ms | 85 / 214 ms |
  | `late_on` max | 108 ms | 176 ms | 104 ms |
  | display frames > 50 ms | 0 | 148 (max 109 ms) | 22 (max 87 ms) |
  | `cer_win` ≥ 10 ms | 13 | 165 | 27 |

  Worst lateness is back at baseline, and long frames and pass walks fall by 85–90%. **Not fully closed:** 22 frames
  > 50 ms and the median gap remain above the 10-30 baseline. Painted-note drops ≥ 10: one temporary (−32), against
  four before.

### Residual slow frames after DEC-236 — the per-tick follow origin (attributed; a bar-stable attempt was reverted)

- **Attribution** (`session_20261031_153427.log`): the 22 remaining frames > 50 ms are one burst, 119.6–123.2 s.
  `disp_turn` shows the auto-follow window start moving every frame, off the bar grid (`req=16, 80, 160, 280, …, 784`).
  Reuse covers only "same start" or "exactly one bar", so each frame is a full committed rebuild (one `cer_win`
  each). The cause is `resolveDetailedPaintWindow` → `resolveCenteredWindowStart` (per-tick centred, clamped),
  which moves only while the playhead crosses the middle bars of a 20-bar loop. Unaligned starts occur at the same
  rate in every session since 10-30: the policy is old, and only its rebuild cost is new.
- **Bar-stable attempt (DEC-232's bar half), reverted.** Snapping the centred start to whole bars (end clamp kept)
  ran on image `1f21af3f…` (`captures/bar-stable-origin-1f21af3f8423/`; rig chain `captures/midifile-chain-50/`,
  serial `captures/session_20261031_155235.log`). It cut frames > 50 ms 22 → 8 and `cer_win` 27 → 17, but at
  **every one-bar slide** the first frame lacked ~60 notes (one bar) for ~1 s (`disp_turn req=768 … n=314` at
  125.895 s; the bar returns only when the tile owner publishes the new window at 127.198 s,
  `idle_turn,cov=tile` 63 ms). During an overdub the committed source is the windowed visual cache, which holds only
  the window it was built for. A whole-bar jump exposes a bar it lacks; the per-tick origin hid this as a leading-edge
  sliver. A blank bar per slide is worse than the frames saved, so the snap is **reverted**. Prerequisite for any
  bar-stable origin: prepare the next bar's tile ahead of the slide.
- **Kept:** `test_follow_slide_keeps_the_entering_bar_with_{stale,current}_root`, origin-agnostic guards (every
  committed note inside the built window present on the slide frame). They pass with the centred origin and do not
  exercise the visual-cache tile path, so they cannot prove the tile gap.
- The board is back on the committed image `e7f134a5…` (byte-identical rebuild, boot verified).
