# Display source / loop-wrap handoff (open)

**Date:** 2026-10-30
**Branch:** `feature/unified-rolling-playback-path`
**HEAD:** `ebaa6ebe`
**Worktree:** heavily modified, **nothing committed**. Includes the prior DEC-219..232 work plus the display changes below.
**Board:** diagnostic image `9e43bb51…` (boot proved: `boot_reset,crash=0`, `BOOT,load,ok`). The per-tick circular-window image `e4513d1f…` was rejected by the user for the regressions listed below.
**Scratch:** slot 4 holds the `-42` 20-bar take. Do not clear/rename outside that slot without explicit permission.
**Reproducer:** **20-bar chain only.** The 4-bar chain is dropped from this investigation.

## Latest user-visible issues (authoritative, 2026-10-30)

1. **Record pass after bar 2:** the record-pass events are not played for a few beats after bar 2.
2. **Bar 6 of the last loop wrap:** the notes still disappear at bar 6 of the last loop-wrap overdub session.
3. Older, still open: after the last loop wrap many overdub notes are removed and redrawn later.
4. Older, still open: during the first overdubs (4th/5th), 2-3 top chord notes disappear and reappear; `bug2` shows pitch 63 / start 1536 `present 0` between `after_pending` and `after_tails`.

Do not treat #1/#2 as display-only until the audible `MO` rows are checked. The previous record-pass report was audible.

## Latest 20-bar evidence (`midifile-chain-42`, reverted image)

- Session: `captures/session_20261030_210458.log`
- Chain: `captures/midifile-chain-42/midifile-chain.json`
- Record stop at **59.56 s**: `draw_src,layers visual=0, preview=316, frame=0, missed_preview=315, compose_miss=315`; panel holds `n=256` from **60.0 to 65.14 s**.
- First overdub at **75.35 s**: `visual=316, frame=262, hidden=60`; the 60 rows past the 16-bar window are hidden.
- 4th/5th stop at **94.07 s**: panel `n=381 -> 377`, then redrawn.
- Loop wrap: display gaps **95.49 -> 111.49 s (16.0 s)** and **65.14 -> 87.5 s (22.4 s)**.

These are the prior display-source/window/overlap faults, not a remaining circular-coordinate shift.

## What landed in this session (uncommitted, display-specific)

- `include/Track.h`, `src/Track/TrackOverdubLifecycle.cpp`
  - `Track::hasCaptureStopPreviewForSlot(slot)`.
  - `recordStopPreviewForSlot()` no longer skips `RecordPublished`; intended to retain the Record preview through the Snapshot publication gap.
- `src/DisplayManager/DisplayNoteWindowGather.cpp`
  - The content hold is exempted for any admitted stop preview (Record or Overdub), not only the Record stop preview.
- `src/Loop/LoopPendingNoteChange.cpp`
  - `applyPendingNoteChangesToDisplayNotes()` uses the borrow-aware `pendingNoteChanges()` (the stop job's changes are borrowed into the Loop at admission). Previously it read the now-empty `pendingNoteChanges_` member.
  - `applyPendingHideAndShortenToNotes()` keeps the committed row when a replacement span is degenerate/too short (`end <= start`, or below `noteMinLengthTicks` when enabled). This is the committed-row authority seed.
- `scripts/ram1_budget.json`
  - Added the diagnostic-only `teensy41-capture-serial-investigation-diag` budget entry: variables 100,480 / locals 30,592 / code 391,272 + padding 1,944; total unchanged versus the capture env.
- `test/test_pending_note_change/test_pending_note_change.cpp`
  - `test_committed_pitch63_start1536_survives_degenerate_pending_shorten` (RED before the guard; now green).
- `test/test_display_window_utils/test_display_window_utils.cpp`
  - Restored to the pre-circular expectations; no 128-bar geometry test currently exists.

Focused native: `test_display_window_utils` + `test_pending_note_change` = **259/259 passed**.
Full native on the current reverted tree: **3,660 cases, 7 skipped, 3,651 succeeded, 0 failed**.
Default build: RAM1 `96,192`, ITCM `393,216` (code `391,064` + padding `2,152`), locals `34,880`.
Diagnostic build: RAM1 `100,480`, ITCM `393,216` (code `391,272` + padding `1,944`), locals `30,592`.

The full run on the rejected circular code had 5 failures; that result is invalid and superseded by the green reverted-tree run above.

## Reverted attempt: per-tick circular window origin

The first circular implementation changed `DisplayWindowUtils::resolveDetailedPaintWindow()` to advance the stored window origin by the minimum tick step and removed the `loopLength - length` clamp. It was rejected on device for:

- record stop not playing/painting until the end of bar 1;
- recorded notes not drawn past bar 16;
- overdub notes painted relative to the window origin instead of tick 0.

The origin is the **paint coordinate**, so moving it per tick shifts the frame. The circular work must not change the origin per tick. `src/Utils/DisplayWindowUtils.cpp` is reverted to the prior centered/clamped resolver.

## Architectural direction (user, binding for the next slice)

1. **Keep the detailed window bounded at 16 bars.** Never expand it to loop length.
2. Make **coverage** circular over the loop, not the paint origin:
   - logical interval: `windowStartTick`, `windowLengthTicks <= 16 bars`, `loopLengthTicks`;
   - `relative = (tick + loopLength - windowStartTick) % loopLength`; `inside = relative < windowLengthTicks`;
   - a window crossing the end is **one generation** with two physical spans: `[start..loopEnd)` + `[0..remainder)`.
   - crossing the wrap must not invalidate the source, request a new one, or blank/hold for the wrap duration. Previous qualified content remains until replacements are ready.
3. When the window advances by one bar, only the newly exposed bar is owed. Do not rebuild all 16 bars.
4. **Committed-row authority:** a committed note remains authoritative until a qualified replacement operation explicitly proves how it changes. Pending Hide/Shorten may not temporarily erase it because the replacement representation is incomplete. Never publish `On@1536, Off@1536` or another invalid tiny surrogate; serve the committed row, then atomically switch to the shortened/hidden result once the complete operation is known. This applies to both display and playback source view.
5. Give the two fixes separate REDs:
   - a 20-bar coverage fixture plus a **128-bar geometry-only** case (do not hard-code 20);
   - the exact pitch-63 / start-1536 committed row, asserting it never disappears between committed and qualified replacement states.

## Next steps (in order)

1. **RED: record pass after bar 2 / record stop.**
   - Reproduce the 20-bar record stop on `-42`; inspect the `MO` rows and playback base, not only the panel.
   - The `-42` record stop shows `visual=0, preview=316, frame=0, missed_preview=315`, then `n=256` 60.0-65.14 s. Check `recordStopPreviewForSlot`, `appendRecordStopPreviewForBar`, `completeAdmittedRecordStop`, and the first published record pass. The `recordStopPreviewForSlot` change did not produce a painted first bar.
2. **RED: circular coverage without moving the paint origin.**
   - Keep the 16-bar window and bar-stable origin; add the modular coverage/filter path and the two-span composition.
   - Prove with the 20-bar chain and the 128-bar geometry test that a wrap does not blank/hold and only newly exposed bars are prepared.
   - Do not use the per-tick resolver again.
3. **RED: bar-6 last-wrap disappearance.**
   - Reproduce with the 20-bar chain; identify the last-wrap overdub tick and the bar-6 rows.
   - The `-42` wrap gap 95.49-111.49 s and the 60 hidden rows at the first overdub are the current evidence.
4. **RED: committed-row authority at pitch 63 / start 1536.**
   - Keep the degenerate-Shorten guard; add the Hide/incomplete-replacement case and the valid atomic switch.
   - The `-42` 4th/5th stop still drops `n=381 -> 377`; determine whether the missing rows are a Hide, a valid-but-too-short Shorten, or the overlap source view.
5. Then, and only then, 20-bar device verification, focused native, full native, target/diag builds. No 4-bar run.

## Session continuation — 2026-10-30 (step 1 closed negative; circular coverage REDs landed)

### Step 1 (record pass / record stop): **not reproduced** in `-42`; the MO gap is sampling

- `MO` rows are **pressure-sampled**. `DebugSessionCapture::midiOut` calls `shouldSampleMidiOutCapture()`,
  which keeps **1 in 8** rows while `captureRingPressureHigh()` is true (`used >= 768 KiB` of the 1 MiB ring,
  or an overflow pending). In `-42` the MO rate steps from ~140/s to ~17/s at 71.5 s — exactly 1/8 — and
  never recovers while the ring stays above the threshold.
- The independent ground truth is `#PAINT_BEAT ... out=<note-on delta>`, which is the real
  `gMidiOutNoteOnCount` delta and is **not** sampled. It stays **54-78 note-ons/s** across the record stop
  (59.5 s), the transport restart (71.4 s, realtime `Start` = `MI,U,250`), the first overdub (74.0 s) and the
  4th/5th stop (94.1 s). The record pass is being played.
- Per-channel MO bursts that look "consecutive" survive because the per-turn emission count is ~8, so the
  same slot in the turn is kept each time; they are not proof of contiguous playback.
- **Consequence:** absence in `MO` is not evidence of missing playback in this capture. The audible
  record-pass report is still open, but `-42` cannot support it, and no RED was written for it.
- Record-stop paint: `DISP` at 59.887 s carries `windowStart=0` but `frameNotes=269` (the **pre-stop**
  window-2904 frame, `windowNoteCount=209`), and `draw_src ... missed_preview=47` names exactly that
  mismatch. At 59.968 s the frame is `256 = windowNoteCount`. So the first paint is **stale for one frame
  (~80 ms)**, then correct; this is not the "first bar never painted" shape the landed
  `recordStopPreviewForSlot` change was aimed at.

### The 16-22 s window gap is real, but the hold is **not** its owner

`disp_window` for `t=7,s=3` in `-42`: `w=3072` from **97.586 s to 110.972 s**, then `w=0` at 111.514 s.
The playhead wraps the 15360-tick loop at ~99.5 s, so the requested window should have returned to 0
twelve seconds earlier. The publication-gap hold in `syncDetailedPaintWindow` is **excluded**: it requires
`!track.hasCaptureStopPreviewForSlot(slot)`, and `disp_src` reports `stops=1, stopnotes>0` for this slot
throughout 98.5-111.0 s.

So the origin was pinned by something else while the paint lane kept running (`#PAINT_BEAT dma=` advances
~9/s). Candidates to read on the next device run: a `centerDetailedWindowOnPlayhead` /
`DisplayCacheLifecycle` write of the stored start, or the paint path returning before
`syncDetailedPaintWindow` updates the slot's stored start. **Not claimed.** The window jump at 111.514 s
does coincide with `crev/cpass` 14 -> 16 and a 10-row frame drop, so the release is publication-adjacent.

### Landed in this slice (prerequisites only; no origin change)

- `include/Utils/DisplayWindowUtils.h`
  - `CircularWindowCoverage` + `windowCoverageRelativeTick` / `windowCoverageContains` /
    `windowCoverageSpanCount` / `windowCoverageSpan` / `windowCoverageLoopBar` — the handoff's exact
    modular rule, one generation with two physical spans.
  - `resolveCircularBarWindowStart(playhead, length, loop, ticksPerBar)` — bar-stable circular origin
    (head floor, then one bar per bar, crossing at the tail). **Not wired yet**; see next.
  - `selectDrawSourceReuseKind(..., loopLength)` — the one-bar slide test is circular, so last bar → bar 0
    is one bar and owes one bar.
- `include/VisualCache.h` — `visualCacheCoversWindow` walks the dirty bars circularly instead of
  truncating at the loop end (a crossing window's head span is checked too).
- `src/DisplayManager/DisplayNoteWindowGather.cpp` — snapshot membership resolves the **physical** loop
  bar (`windowCoverageLoopBar`), so a crossing window's head span gathers; the hold guard accepts any
  `windowLength <= loopLength` instead of refusing a crossing interval.
- `src/DisplayManager/PianoRollDraw.cpp` — grid lines draw in window-relative ticks (no absolute window
  end), the overview strip draws the crossing window's head span and its inside-note overlay, and the
  bracket/playhead membership uses the circular coverage.
- `test/test_display_window_utils` — four new cases: 20-bar crossing coverage, 128-bar geometry-only
  crossing coverage (nothing hard-coded to 20 bars), bar-stable circular origin (head floor, one bar per
  bar, crossing tail), circular one-bar slide. Plus `test_resolve_detailed_paint_window_keeps_the_clamped_centred_origin`,
  a **deliberate pin**: wiring the circular origin without the circular committed-base gather is what the
  boundary suite rejects.

### Step 3 evidence (`-42`): the committed visual cache stops rebuilding once the chain starts

Read `#PAINT_BEAT`, not `DFRAME`/`draw_src`: it is a direct `Serial.printf_P` row (never ring-shed,
never pressure-sampled). `DFRAME` and `draw_src` are ring records and the close prints
`RING,stats ... shed=25,320, tierA_evict=1,040, overflow=1`, so their absence after ~80.8 s is shedding,
not a frozen lane (`#PAINT_BEAT dma=` keeps incrementing 1429 -> 1493, ~9 transfers/s).

Visual cache for `t=7,s=3`:

- `VCACHE,slice_clean` at **65.3 s = 316 notes / 20 bars** (the record pass), then one **1-bar, 5-note**
  slice at 67.9 s, and **no slice rebuild for the rest of the run**.
- `#PAINT_BEAT` reports `dirty=1, vcache=316` from **80.17 s** to the end, i.e. the committed display base
  is a frozen pre-chain snapshot while every overdub row on the panel comes from a live overlay.
- `DIAG,idle_maint` rows exist at **74-77 s**, then none until the transport stop at **130.9 s**: the
  idle owner is the transport-bound stop lane for the whole chain, and the visual-cache child is the
  unbounded one that is deferred (DEC-212/216/217/227). This is the starvation, not a wrap coordinate.

The painted set then drops exactly when an overlay retires and the committed source does not yet carry the
pass (`#PAINT_BEAT notes=`):

| time | `notes` | window | note |
|---|---|---|---|
| 110.675 | 435 | 3072 | |
| 111.677 | **425** | 0 | 10 rows drop |
| 117.885 | **401** (was 447) | 0 | 46 rows drop mid-overdub |
| 121.003 | 469 | 0 | redrawn |
| 125.049 | **458** | 0 | 11 rows drop |
| 128.088 | 469 | 216 | redrawn |

That is issue #3's shape ("removed and redrawn later"). The candidate owners are (a) a stop retiring its
preview before the committed draw source for the current window serves the pass, and (b) the visual-cache
idle lane never running during the chain. Both are step 3/4 work; neither is fixed by this slice.

### Device run `-43` (2026-10-30, image `9b37dccb…`): the window pin reproduces

Flashed `teensy41-capture-serial-investigation-diag` (`sha256 9b37dccb…`, RAM1 `100,480`,
ITCM `391,272 + 1,944`) and boot-proved (`boot_reset,crash=0`, `BOOT,load,ok`, `BOOT,ram1,192512`).
Chain `captures/midifile-chain-43` (`--no-clear --hold-clock-on-settle --stop-bars 8 --record-bars 20`):
record `clocks=1914`, overdub `clocks=2640`, **12/12 capture windows**, `behind_pulses=0`, total 6002
pulses. Session `captures/session_20261030_214116.log`.

It reproduces the `-42` shapes on a different image, so they are not one-off:

- `disp_window` **pins `w=3072` from 332.7 s to 346.1 s (13.4 s)** and jumps to 0 at 346.6 s. The loop
  wrapped at ~334.7 s, so the release is **publication-adjacent, not wrap-adjacent** — same as `-42`
  (97.6 -> 111.0 s). The owner is still unnamed (a stop preview is present, so the sync-level hold is
  excluded; `disp_src` was evicted before the pin, so `stops/pending` is unknown in this window).
- `#PAINT_BEAT`: record stop at ~295 s paints `269 -> 256` (one stale frame, then the correct 16-bar
  frame for window 0); `vcache=0` until 305.6 s, then `vcache=316`; **`vcache=316, dirty=1` from
  315.8 s to the end** while `notes` grows 338 -> 469. So the committed visual cache is starved exactly
  as in `-42`, and the painted set is again committed-base + overlays.
- Drops: 446 -> 428 at 347.3 s (window jump), 480 -> 469 at 364.6 s (chain end).
- Capture limits in this run: `RING,stats ... shed=20,999, tierA_evict=2,884, overflow=0`, so the
  decisive `disp_src`/`playback_read` rows are evicted after ~16 s and `MO` is unusable from 25 s
  (`MO` rate 96 per 5 s at 10-14 s, then 12, then 0/5 s). Read `#PAINT_BEAT` (direct serial) for
  content and transfer facts.

### Clear-verification gap found and fixed on the host side of the rig

The first two chain launches refused to start: the verified-clear path could not confirm slot 4.
Cause (firmware): the **selected-slot long press with an already-empty slot returns silently**
(`MidiButtonActions`, `!slotHasData && !loopSlotHasPayloadOnSd`), while the rig's `empty` outcome is
only classified from `Clear ignored — selected slot is empty (track %u slot %u)`. One clear had
succeeded (the user confirmed the track was already cleared), and every retry was then a silent no-op.
Landed: that branch now logs the same `Clear ignored — selected slot is empty (track %u slot %u)` row
(the literal already exists in `handleClearTrack`), so "already empty" is a named success. The run above
was completed with `--no-clear` on the already-cleared scratch slot; no other slot was touched.

### Owner feedback on `-43`/`-44`: the open-tail length is a **wrap-split** problem (freeze withdrawn)

Owner reports, in order:

1. `-43` image: "the first overdub lengthened the notes to current tick for 1 frame after overdub 2
   stopped" (and the same after overdub 3).
2. `-44` image (closure freeze): "the 3rd overdub gets briefly lengthened when the window starts
   scrolling", and "after loop wrap the note ons get lengthened wrongly to the end of the piano roll,
   instead of current tick".

**Withdrawn:** the closure-tick freeze (`PendingDeferredStopIdentity.closeTick` +
`resolveCaptureTailCloseTick`) is reverted. It fixed the stop-edge length but fed the wrap-split branch a
tick that can precede an open note's start, which is exactly the `tailEnd = loopLength - 1` stretch the
owner then saw. Both display tail paths are back to the playhead; the helper, the field and its tests are
removed. Image `7203b624…` flashed (diag, RAM1 `100,480`, ITCM `391,272 + 1,944`), boot rows
`boot_reset,crash=0`, `BOOT,ram1,192512`, `BOOT,load,ok`.

**Named next target (do not fix by guessing):** `applyCapturePlayheadTails`
(`DisplayNoteResolveLiveCapture.cpp`) takes the wrap-split when
`isLiveWrapHeadContinuationDisplay(open.tick, closeTick, loopLength, true)` — i.e. `closeTick < open.tick`
and the note started in the last wrap window — and then sets `tailEnd = loopLength - 1`. That is the
"lengthened to the end of the piano roll" shape. The other shape ("briefly lengthened when the window
starts scrolling") happens on a frame where the draw-source window moves; the frame is a slide, so the
tail copy and the window key move in the same turn.

**Measurement before change:** one investigation-tier row per painted open tail, e.g.
`DIAG,draw_src,tail,id,<id>,s,<start>,e,<end>,close,<closeTick>,win,<windowStart>,split,<0|1>`, emitted
only while `openNoteIndices` is non-empty and rate-limited. It names the branch directly in the next
run instead of inferring it from `MO`/`DFRAME` (both lossy).

### Already-empty CLEAR is now a named outcome

The first two chain launches refused to start because the verified-clear path could not confirm slot 4:
the selected-slot long press with an already-empty slot returned **silently**, while the rig's `empty`
outcome is only classified from `Clear ignored — selected slot is empty (track %u slot %u)`. The
long-press branch now emits that row through
`MidiButtonGesturePolicy::kClearIgnoredEmptySlotLogFormat`, with
`classifyClearSlotOutcome(hasRam, hasPayload)` and `test_clear_slot_outcome_names_already_empty` pinning
the decision and the text. `-44` then cleared and confirmed on the first attempt
(`MIDI: Clear selected slot 4`, `cleared: true`).

### Correction (2026-10-31): the "no overdubs" run was a rig bug, not the firmware

`-45`/`-46`/`-47` reported zero overdubs and no audible chords. The cause was a leftover keyword in
`scripts/hitl_midifile_chain.py` (`capture_log=` passed to `run_stream()`, which does not take it): the chain
raised `TypeError` at the overdub-stream call, so the host never sent the chord stream or the window
presses. The device was healthy; `usb_batch` falling to 0 was the missing host input, not a firmware input
regression. Removed; the rig is back to the blind pulse-grid player it was an hour earlier (no state
polling, no resync, no retries).

With the rig fixed, the owner's stop-edge change is confirmed on device (`-48`, image `8134d61f…`):
clear confirmed first try, **12/12 windows**, `behind_pulses=0`, 12 start + 12 stop presses handled with
the right branch (`odub_press`), state 5 ↔ 6 through the stream, frame notes **256 -> 469**, steady
55-68 note-ons/s, no frame drop > 10, final `#STOP_TRACE stop=0, passes=1`.

Tail instrumentation limit: the `draw_src,tail` row (248 rows) ends at the record stop; during the
overdub windows `loop.capturePreview.openNoteIndices` is empty, so `applyCapturePlayheadTails` is not on
that paint path and the row cannot see the window-scroll / post-wrap tail shapes. Those must be probed on
the stop-preview/committed path if the owner still sees them.

### Next (exact dependency, do not skip)

Wiring `resolveCircularBarWindowStart` into `resolveDetailedPaintWindow` fails
`test_live_capture_display_boundary` because the **committed display base** cannot represent a crossing
window:
- `DisplayManager::buildCommittedDisplayBase` expands with `expandWindowWithMargin` (clamps the gather
  end to `loopLength`) and `rebuildDisplayNotesInWindow` filters `[windowStart, windowStart+windowLength)`
  linearly, so the head span is dropped; `paintWindowInsideGather` is linear too.
- `test_moving_window_gap_never_mixes_old_prefix_with_new_viewport_overlay` and
  `test_committed_display_base_is_independent_of_the_overlap_view` are the fixtures that fail
  (`Expected 6128 Was 11520`, and the committed note is not drawable).
So the order is: (a) circular gather/`paintWindowInsideGather` for the base (two spans, one generation),
(b) wire the circular origin, (c) remove the hold's content-interval substitution so the window keeps
advancing while the retained content serves the bars it covers. Then the 20-bar device run.

### Gates for this slice

- Focused (final tree): `test_display_window_utils` **131/131** + `test_midi_button_gesture_policy`
  **2/2** + `test_live_capture_display_boundary` **47/47** = **180/180**. Earlier batches on the same
  slice: boundary/display/midi-button **203/203** and midi-button/dispatch **23/23**.
- **Full native: 3,726 cases / 7 skipped / 3,719 succeeded / 0 failed.**
- `teensy41-capture-serial` **SUCCESS**: RAM1 `variables 96,192`, ITCM `391,064 + 2,152`.
  `teensy41-capture-serial-investigation-diag` **SUCCESS** at its pinned `100,480 / 391,272 + 1,944`.
- Device: image `9b37dccb…` flashed and boot-proved; chain `-43` completed 12/12 windows with
  `behind_pulses=0`. The clear-outcome row landed after that run, so the next run's image will differ.

## Commands

Reader:
```sh
.venv/bin/python scripts/capture_session.py --boot-reset-mode never --out-dir captures/
```
20-bar chain:
```sh
.venv/bin/python scripts/hitl_midifile_chain.py \
  --hold-clock-on-settle --stop-bars 8 --record-bars 20 \
  --record-pattern "/Users/eelkejager/Documents/Ableton/2025/droid_midilooper_test Project/16ths.mid" \
  --overdub-pattern "/Users/eelkejager/Documents/Ableton/2025/droid_midilooper_test Project/chords.mid" \
  --out-dir captures/midifile-chain-43
```
Diagnostic build/flash (for `draw_src`):
```sh
pio run -e teensy41-capture-serial-investigation-diag
pio run -e teensy41-capture-serial-investigation-diag -t upload
```
Focused native:
```sh
pio test -e native -f test_display_window_utils -f test_pending_note_change
```

## Files to read/change

- `src/Utils/DisplayWindowUtils.cpp` — resolver/filter; current runtime is the reverted centered/clamped behavior.
- `src/DisplayManager/DisplayNoteWindowGather.cpp` — detailed window request, content hold, stop preview append, pending transforms.
- `src/DisplayManager/DisplayNoteResolve.cpp`, `DisplayCacheLifecycle.cpp`, `PianoRollDraw.cpp` — source/paint coordinates; do not move the origin per tick.
- `src/Loop/LoopPendingNoteChange.cpp` — committed-row authority, pending Hide/Shorten, validity/min-length.
- `src/Loop/LoopVisualCache.cpp` — committed display base.
- `src/Track/TrackOverdubLifecycle.cpp`, `include/Track.h` — stop previews, record preview, pending changes borrow.
- `src/Track/TrackCaptureStopCommit.cpp` — record-stop playback `startPlaying` / record pass.
- `scripts/hitl_midifile_chain.py` — 20-bar rig.
- `test/test_display_window_utils`, `test/test_pending_note_change`, `test/test_live_capture_display_boundary`, `test/test_display_note_count` — RED locations.
- Plan: `/Users/eelkejager/Downloads/MidiLooper_persistence_stop_display_fix_plan.md`.

## Do not

- Do not expand the detailed window to loop length.
- Do not advance the paint origin per tick.
- Do not invalidate/hold the display source merely because the loop wrapped.
- Do not drop a committed note for an incomplete Hide/Shorten replacement.
- Do not run or use the 4-bar chain for this investigation.
- Do not commit/merge the current tree until the full native and 20-bar device gates pass.
- Do not clear/rename the device SD outside the authorized scratch slot.

## Historical captures

- `-42` `session_20261030_210458.log` — reverted image, current 20-bar evidence.
- `-41` `session_20261030_210038.log` — rejected circular image, regressions.
- `-39` `session_20261030_202353.log` — partial display image; record-stop `n=0` 0.22 s; 4/5 `373 -> 370`; p63 `bug2 present 0`; wrap 15.99/16.04 s gaps.
- `-38` `session_20261030_195653.log` — 4-bar diag; 6th-stop diff id 144 `old 2296 / new 2304`; 2nd wrap.
- `-37` `session_20261030_194716.log` — 20-bar baseline before the partial display changes; 22 s gaps.
