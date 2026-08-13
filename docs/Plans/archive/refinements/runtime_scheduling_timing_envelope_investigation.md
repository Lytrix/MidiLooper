# Runtime scheduling timing-envelope investigation log

**Status:** Archived investigation evidence (2026-08-12–2026-08-13)  
**Date:** 2026-08-13  
**Parent contract:** [`runtime_scheduling_admission_model_architecture.md`](../../runtime_scheduling_admission_model_architecture.md)  
**Roadmap:** [`runtime_scheduling_owner_boundary_admission_refinement.md`](../../runtime_scheduling_owner_boundary_admission_refinement.md)

This file preserves the chronological S0 device-run narrative and root-cause log formerly in the architecture document (§31a–§31p). It is **not** implementation authority. Current status and next work live in the parent contract and the owner-boundary roadmap. In this log, “S1” means the withdrawn interval-reservation stage, not persist `admit*`.

**Telemetry naming (2026-08-13):** firmware now emits `DIAG,midi_gap` / `DIAG,midi_input` from `RuntimeTimingEnvelope::noteMidiInputEnter` / `noteMidiInputExit`. Tables below cite historical captures that still contain `DIAG,msi` / `DIAG,midisvc`. Mapping: `msi` = MIDI Input Gap; `midisvc` = `handleMidiInput()` duration. Do not rewrite measurement tables.

Section numbers below keep their original §31* labels so existing capture and plan links remain locatable.

---

## 31a. S0 device runs — first results (2026-08-12)

**Status:** envelope partially measured; RECORD/OVERDUB window blocked by a capture-transport defect, now fixed and awaiting re-run.

### Capture delivery defect (RC-S0a) — fixed

[`141815`](../../../../captures/session_20260812_141815.log) and [`144323`](../../../../captures/session_20260812_144323.log) both lost **every** DIAG envelope window across the capture pass (21.9 s–318.6 s and 32.8 s–304.1 s respectively). No `#CAP` line of any tag survived those ranges; `RING,overflow` fired three times in each session.

Root cause: `isTierATextLine` in `DebugSessionCapture` skipped **two** commas when locating the tag in `#CAP,<micros>,<tag>,...`, so `tagStart` landed one field past the tag and every `strncmp` failed. Tier-A classification returned false for all lines, which made both protections inert since they were written:

- the drop-only path in `flushCaptureBuffer` (timing-critical budget of 8) dropped Tier-A along with everything else;
- the eviction guard in `appendCaptureRecord` never recognised a Tier-A head record.

Fixes: parse extracted to header-only `CaptureLineTier::isTierALine` with host coverage (`test_capture_line_tier`), and the eviction rule tightened so **Tier-A may only be displaced by Tier-A** — protecting the envelope from `SEVT`/`REVT`/`DFRAME` volume while keeping the newest windows and making it impossible to wedge the ring.

### RC-S0b — external memory pool walk, 593 ms, loses MIDI clock

`MemoryMonitor::logStatus()` called `getExternalMemoryPoolFreeBytes()` / `getExternalMemoryPoolUsedBytes()`, both of which run `sm_malloc_stats_pool` over the whole 8 MiB pool. Measured directly in [`141815`](../../../../captures/session_20260812_141815.log):

```
[330.989] [Memory] heap free=204 used=265 total=469 KB min_ever=204 KB
[331.582] [Memory] psram chip=8 MB free=4669 used=3522 pool=8191 KB
[331.585] MIDI clock lost, switching to internal at 118.4 BPM
```

593 ms of main-loop block, clock lost 3 ms later, repeating on the 60 s cadence (`msi` max 592893 / 592954 / 592994 µs at 334.1 s, 394.3 s, 454.4 s). External clock was still streaming — `BPM` lines run through 330.822 and the track went `PLAYING → STOPPED` from a local button, not a transport stop.

The `!timingCriticalTrackActive` gate could not prevent this: it is derived from track state only, so it cannot tell whether a clock is live. The exemption in `INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md` that permitted the walk in "idle / stopped diagnostics" has been removed; the walk is now `setup()`-only via `logStatus(true)`, and runtime reports `pool_size` (O(1)).

### Measured so far

| Window | `msi` max | `midisvc` max | `clk` max | `tracks` max | `clockrate` |
|---|---|---|---|---|---|
| Boot slot restore (11.7 s) | 845 ms | 20 µs | 0 | 0 | 0 |
| Overdub stop (329.1 s) | 122.3 ms | **173.2 ms** | 3.37 ms | 3.36 ms | 48 |
| Memory-log windows | **592.9 ms** | 3.72 ms | 2.57 ms | 2.56 ms | 16 |
| Steady idle (66 windows) | 33.8 ms avg, 109.4 ms worst | ~34 µs | 0 | 0 | 0 |

Three conclusions already hold:

1. **MIDI service is itself a dominant path.** `midisvc` 173.2 ms against `clk` 3.37 ms and `tracks` 3.36 ms in the same window: overdub stop runs synchronously inside `handleMidiInput` via button dispatch (`ODUB,stop` stages — seal +101.6 ms, `set_state` +118.0 ms, flush +118.8 ms, display +121.7 ms). No admission over *background* work can bound MSI while a MIDI-dispatched control action is the largest in-service cost.
2. **Idle MSI is ~34 ms, not 2 ms.** `overCount` averages 212 per 5 s window (~42/s over 5 ms) with `midisvc` at ~34 µs and `clk`/`tracks` at 0, so essentially all of it is main-loop display work — `PlaybackBuildTime` 74.7 ms, `DisplayUpdateTotalTime` 43.7 ms, `DisplayBuildTime` 30.3 ms, `DisplayResolveLiveCaptureTime` 29.6 ms.
3. **`clockrate` is validated.** 48 pulses/s against 119.6 BPM external (expected 47.8).

### Still owed

The RECORD and OVERDUB windows — the point of S0. Re-run the ≈100-bar scenario on the rebuilt firmware and confirm DIAG windows are continuous from arm through the second overdub stop.

---

## 31b. S0 run [`145555`](../../../../captures/session_20260812_145555.log) — first capture-phase envelope

The Tier-A parse fix worked: envelope windows now survive **through RECORD, both OVERDUB passes, and the stops**, from 177.2 s to the end. The runtime pool walk is gone (`[265.666] [Memory] psram chip=8 MB pool=8191 KB`, no free/used) and no 593 ms `msi` sample appears.

**Transitions:** `RECORDING → STOPPED_RECORDING` 220.30 s · `→ PLAYING` 220.79 s · `→ OVERDUBBING` 223.09 s · `→ PLAYING` 247.39 s · `→ OVERDUBBING` 251.67 s · `→ PLAYING` 264.69 s · `→ STOPPED` 265.52 s.

### The envelope splits sharply by phase

| Phase | `msi` max | `midisvc` max | `clk` max | `tracks` max | `clockrate` |
|---|---|---|---|---|---|
| RECORD (182–217 s) | 37.8–57.7 ms | **0.9–1.1 ms** | 0.16–0.17 ms | 0.14–0.15 ms | 47–48 |
| Record stop → PLAYING (222.4 s) | 487.4 ms | 8.7 ms | 2.71 ms | 2.70 ms | 47 |
| Overdub entry (227.7 s) | 69.8 ms | **762.5 ms** | 8.63 ms | 8.62 ms | 46 |
| OVERDUB 1 + 2 (232.9–268.1 s) | 58.7–132.3 ms | **140.2–149.0 ms** | 3.46–8.83 ms | 3.44–8.82 ms | 47–48 |
| After stop (273.1 s) | 52.4 ms | 129.5 ms | 0 | 0 | 0 |
| Idle (278.1 s+) | 23.1–23.6 ms | 2 µs | 0 | 0 | 0 |

Three things follow directly.

**RECORD is clean.** `midisvc` around 1.1 ms, `clk` 0.17 ms, `tracks` 0.15 ms. Whatever the runtime problem is, it is not in the record capture path.

**PLAYING and OVERDUB cost ~140× more MIDI service than RECORD.** Every window from 232.9 s to 273.1 s carries a `midisvc` maximum between 129.5 ms and 149.0 ms, and it persists into the window *after* transport stopped. This is a per-window maximum, so it is one very expensive `handleMidiInput()` call per window rather than a sustained load.

**`clk` and `tracks` do not explain it.** They track each other within ~20 µs and peak at 8.83 ms, so `ClockManager::onMidiClockPulse` and `TrackManager::updateAllTracks` account for at most 6 % of the 149 ms. The cost is elsewhere inside `MidiHandler::handleMidiInput`.

The 762.5 ms sample sits in the window covering 222.4–227.7 s, which contains the `PLAYING → OVERDUBBING` entry at 223.09 s.

`DIAG,timing_max` corroborates the display side: `DisplayResolveLiveCapture` reads 7.76 ms during RECORD but 23.05 ms and 30.75 ms during the two overdub passes, with `DisplayUpdateTotalTime` at 48.0 ms and `PlaybackBuildTime` at 70.4 ms.

### Why nothing before 177 s survived (RC-S0c)

Not a classification failure this time — a transport failure, and the code is explicit about it:

```648:660:src/Utils/DebugSessionCapture.cpp
    while (dropped < maxRecords && sCaptureRing.used >= sizeof(CaptureRecordHeader)) {
      if (headRecordIsTierAText()) {
        break;
      }
      const size_t usedBefore = sCaptureRing.used;
      discardOldestRecord();
      if (sCaptureRing.used >= usedBefore) {
        break;
      }
      ++dropped;
    }
    return;
  }
```

Under the timing-critical budget (`SC_CAPTURE_FLUSH(timingCriticalTrackActive ? 8 : 64)`) this branch **drops records and returns without ever writing to Serial**. Tier-A is therefore never transmitted while a track is recording, overdubbing, or playing — it can only leave the ring by eviction. Every envelope window emitted between roughly 20 s and 265 s had to survive in a 96 KiB ring until the stop flush; only those from 177 s onward fit. Every window after `PLAYING → STOPPED` (278 s onward) survives, because the budget returns to 64 and the lines go straight out.

Making Tier-A un-droppable also introduced a second effect. A Tier-A record at the head is a barrier: the drop loop breaks on it, and `appendCaptureRecord` then refuses incoming Tier-B/C. The ring stalls until the next Tier-A evicts the head. The result is fragmented coverage with **true holes** — records never stored at all, not stored then evicted:

```
retained seconds, 170–270 s:
177  182-184  202-205  207-208  212  215  217-223  227-228
230  232  236  238-239  243-245  247-253  257-258  262-270
```

Zero records carry a timestamp in 185–202 s, and `RING,overflow` fires only three times (all at stops), confirming loss by refused append rather than eviction.

**Fix direction (not yet implemented):** give the timing-critical path a small, bounded Tier-A **transmit** allowance gated on `serialWriteRoom`, so Tier-A leaves the ring by being sent instead of accumulating as a barrier. This is the option deferred when RC-S0a was chosen. Pair it with counters for refused appends, Tier-A evictions, and Tier-A transmissions so the next run measures the delivery path instead of inferring it.

### S0 status

Partially complete. The capture-phase envelope now exists and is usable; the pre-177 s window and the delivery path are still owed. The dominant term is unambiguous — MIDI service — but the responsible segment inside it is not, so the next stage is S0b, not S1.

---

## 31c. S0b — segment the MIDI service interval (observation only)

**Status:** firmware **implemented**; device-attributed in [`193645`](../../../../captures/session_20260812_193645.log) (§31i) — the PLAYING/OVERDUB `midisvc` term is `usbdev`.

`handleMidiInput` has four sequential segments, and S0 measures only their sum. `clk` already covers the Clock branch and accounts for at most 8.83 ms of a 149 ms call, so the cost is in one of the other three:

| Segment | Owner | Emission |
|---|---|---|
| USB device drain + `dispatchMidiBatch` | `usbMIDI.read()` loop | `DIAG,usbdev` |
| DIN drain + `dispatchMidiBatch` | `MIDIserial.read()` loop | `DIAG,din` |
| USB host stack service | `usbHost.Task()` | `DIAG,hosttask` |
| USB host drain (callbacks into `handleMidiMessage`) | `usbHostMIDI.read()` loop | `DIAG,hostdrain` |
| Clock dispatch within a batch | `ClockManager::onMidiClockPulse` | `DIAG,clk` (S0) |
| Track update within clock dispatch | `TrackManager::updateAllTracks` | `DIAG,tracks` (S0) |

Clock dispatch is nested inside the drain that received the Clock byte (`usbdev`, `din`, or `hostdrain`). Subtract `clk` from that drain when attributing the remainder.

Per-message work reached from `handleMidiMessage` (`sendMidiThru`, `SC_MIDI_IN`) is counted inside the drain that dispatched the message, not as a fifth sequential segment. If a named drain matches `midisvc` and still exceeds `clk`, that nested work is the next split — not this stage.

Constraints, unchanged from S0: `micros()` deltas and comparisons only, accumulate maxima into the existing 5 s window, emit as Tier-A `DIAG,` lines, take no scheduling decision. The phase split matters — report each segment separately for RECORD versus PLAYING/OVERDUB, since S0 already shows the two differ by ~140×.

**Exit criterion:** the 762.5 ms overdub-entry sample ([`145555`](../../../../captures/session_20260812_145555.log)), the recurring 129–149 ms samples, and the [`191356`](../../../../captures/session_20260812_191356.log) 218–221 ms / 110–125 ms overdub samples are each attributed to a named segment. Report RECORD versus PLAYING/OVERDUB separately.

### What S0 already rules out

`midisvc` is measured across three call sites, all in `loop()`, with no nesting, so each sample is one complete call. Two candidates are therefore excluded by the data:

- **Not the RC-C extra MIDI drain.** That call fires only when a track is recording or overdubbing. During RECORD it is active and `midisvc` max is 1.1 ms. In the 268.1–273.1 s window the track had been `STOPPED` since 265.52 s, so the drain was inactive — and `midisvc` max is still 129.5 ms.
- **Not clock dispatch.** The same window reports `clk` 0, `tracks` 0, and `clockrate` 0, meaning no clock pulse was serviced at all.

The cost lives in the USB device drain, the DIN drain, `usbHost.Task()`, or the USB host drain, and it appears only once the loop holds committed content (absent during RECORD, absent again by 278.1 s).

---

## 31d. Regression vs [`9678c3d`](https://github.com/Lytrix/MidiLooper/commit/9678c3d) — display lag and transition feel

User report: at `9678c3d`, RECORD↔OVERDUB switching was smoother and there was no display lag during OVERDUB. Two changes in `6053b01` account for that, one of them measured directly in [`145555`](../../../../captures/session_20260812_145555.log).

### Display lag — new budget bailouts show stale frames

`resolveDisplayNotesLiveCapture` gained a soft budget with three bailouts that did not exist at baseline:

```504:517:src/DisplayManager/DisplayNoteResolveLiveCapture.cpp
    const bool reuseLastValidFrame =
        !cacheCold && budgetExceeded() && liveDisplayCacheCommittedNoteCount_ +
                                                  liveDisplayCacheCaptureNoteCount_ >
                                              0;

    if (reuseLastValidFrame) {
        // Keep liveDisplayNotes as last valid frame; unfinished committed/capture work stays
        // pending via visualCacheDirty / capturePreview.revision.
    } else if (committedLayerChanged) {
        const uint32_t displayBuildStartUs = micros();
        DIAG_COUNTER_INC(DisplayIncrementalUpdate);
        rebuildCommittedLayer();
        if (!budgetExceeded()) {
            replaceCaptureLayer();
```

The budget is `Diagnostics::kDisplayResolveBudgetMicros = 5000`. Measured `DIAG,timing_max,DisplayResolveLiveCapture` in the same session:

| When | Measured | Ratio to budget |
|---|---|---|
| RECORD (215.4 s) | 7 761 µs | 1.6× |
| Overdub 1 entry (223.2 s) | 23 054 µs | 4.6× |
| Overdub 2 entry (251.8 s) | 29 991 µs | 6.0× |
| Overdub 2 (257.0 s) | 30 747 µs | 6.1× |

Resolve is over budget for essentially the whole of both overdub passes. That makes `reuseLastValidFrame` the normal case, so the capture layer is not replaced and the playhead tails are skipped (`!reuseLastValidFrame && (isRecording() || isOverdubbing()) && !budgetExceeded()`). The display holds the previous frame — which is exactly the reported lag.

At `9678c3d` no bailout existed: resolve ran to completion every frame. It cost more, but the frame was current. The bailout converted a cost problem into a correctness-of-freshness problem without reducing the underlying work below budget.

This restates §33's existing rule — *never rebuild everything synchronously and rely on a timeout to make it safe* — with device evidence. A 5 ms budget on an operation that measures 30 ms does not bound anything; it only decides which frames get dropped.

### Transition feel — Clock is no longer dispatched transport-first

At baseline, `isMidiTransport` put `Clock` in the first dispatch pass, so the tick always advanced before channel messages in the same batch. `MidiDispatchOrder::isSequenceTransport` covers only `Start`/`Stop`/`Continue`, leaving Clock in wire order with channel messages. Notes in a batch can now be handled at the previous tick. This adds no measurable CPU — the reorder is three O(count) passes over a ≤128 batch — but it changes grid alignment at the RECORD↔OVERDUB boundary.

### Secondary, conditional

`skipFocusLoadForSlotSession` now paints the OLED during capture where the baseline skipped it, but only while `focusSlotRestoreWork && SlotLoadSession::isActive()`. `PianoRollDraw`'s bounded-window scan reduces cost on long loops and is ruled out as a cause. Overdub-entry invalidation (`startOverdubbing`, `markDisplayCachesStale`, `invalidateLiveDisplayCache`) is unchanged in this diff.

### Consequence for stage order

The display bailouts are the reported user-visible defect and are independent of the `midisvc` term. They are their own root-cause slice, not part of S0b.

### Compose sub-step instrumentation (shipped, observation only)

The existing telemetry could not name the slow step. `DisplayCaptureCompose` measured 30 736 µs of a 30 747 µs resolve, and `DisplayBuild` (33 244 µs) wraps `rebuildCommittedLayer` and `replaceCaptureLayer` together, while `rebuildCommittedLayer` has four distinct outcomes during overdub.

Two slots were declared but never recorded anywhere in the firmware, so their zero values were not evidence: the `DisplayCaptureGather` timing and the `DisplayCaptureFullGather` counter. Both are now wired to the gather branch.

Added, following the existing `DIAG_TIMING_RECORD` / `DIAG_COUNTER_INC` pattern:

| Slot | Covers |
|---|---|
| `DisplayCommittedRebuildTime` | `rebuildCommittedLayer` (timed at the call site — the lambda returns early on the overdub path) |
| `DisplayCaptureReplaceTime` | `replaceCaptureLayer` — full resize + insert of `capturePreview.notes` |
| `DisplayCaptureSyncTime` | `synchronizeCaptureLayer`, nesting the replace sample when the capture mirror is invalid |
| `DisplayCaptureGatherTime` | `rebuildDisplayNotesInWindow` (was dead) |
| `DisplayCommittedWindowFilter` | `filterDisplayNotesByWindowInclusion` branches — full scan of `visualCache.notes` plus a fresh vector |
| `DisplayCommittedFullAssign` | full `assign` of `visualCache.notes` (no paint window) |
| `DisplayCaptureFullGather` | gather branch (was dead) |

Both enums are append-only before `Count`, so existing indices are unchanged; `static_assert`s now bind `kCounterNames` / `kTimingNames` to their enums, and `test_diagnostics` pins the new indices. Native suite 1034/1034.

**Exit criterion:** one ≈100-bar RECORD + 2 OVERDUB run that attributes the 23–31 ms resolve to a named sub-step. The bailout semantics decision — stale frame, resumable, or always-complete — is deferred until that lands.

---

## 31e. Run [`152948`](../../../../captures/session_20260812_152948.log) — sub-step attributed, and a larger finding

Transitions: `RECORDING → STOPPED_RECORDING` 230.08 s · three OVERDUB passes ending 402.12 s · `PLAYING → STOPPED` 405.46 s.

### The resolve cost is the committed layer, and it splits in two

At the 402.1 s snapshot, `DisplayResolveLiveCaptureTime` max is 29 746 µs, `DisplayCommittedRebuildTime` max is 29 728 µs, and `DisplayCaptureGatherTime` max is 29 716 µs. The committed rebuild is 99.9 % of resolve and the gather is 99.96 % of that. `DisplayCaptureReplaceTime` max is 252 µs and `DisplayCaptureSyncTime` max is 3 740 µs — neither is a factor.

Splitting the sums between the 333.8 s and 402.1 s snapshots separates two distinct costs:

| Branch | Calls | Cost each |
|---|---|---|
| `rebuildDisplayNotesInWindow` (gather) | 22 | **25.7 ms** |
| `filterDisplayNotesByWindowInclusion` | 622 | **5.48 ms** |
| `replaceCaptureLayer` | 644 | 0.10 ms |
| `synchronizeCaptureLayer` | ~110 | 0.01 ms |

The branch counters confirm the split exactly: `DisplayCommittedWindowFilter` rose by 622 and `DisplayCaptureFullGather` by 22, together accounting for all 644 committed rebuilds. `DisplayCommittedFullAssign` stayed at 0 for the whole session, so the full-`assign` branch never runs.

So the rare gather produces the 25–30 ms spikes, while the *common* window filter costs 5.48 ms — just over the 5000 µs budget. That is why `DisplayResolveOverBudgetCount` reached 2 223 while the gather ran only 23 times: the sustained over-budget driver is a linear scan of `visualCache.notes` plus a fresh vector allocation, not the gather.

`rebuildCommittedLayer` runs on only 1 730 of 18 592 resolves (9 %), and mean resolve across all frames is 713 µs. The budget problem is confined to that 9 %.

### RC-D — the OLED repaints every loop iteration, ignoring the 30 ms cadence

`DisplayUpdateTotalTime` reports a mean of 13.1 ms over 24 497 samples. `DFRAME` gives the frame period directly, and it is stable across the entire session — before recording, during all three overdubs, and after stop:

```
 10.617  notes=342  frame_us=13272  -> 56.1 fps
 82.400  notes=342  frame_us=13282  -> 71.7 fps
380.075  notes=359  frame_us=12105  -> 76.8 fps
414.804  notes=364  frame_us=12007  -> 78.6 fps
```

A ~14 ms frame period against `LCD::DISPLAY_UPDATE_INTERVAL = 30 ms`, with ~13 ms spent inside the frame. The main loop is roughly 93 % inside `DisplayManager::update()`, from ten seconds after boot onward. This is the cost that S0 measured as ~34 ms idle MSI.

The cadence gate is not being reached, because one call path has no gate. The chain is closed and provable:

1. `DisplayManager::invalidateLiveDisplayCache` and `invalidateNoteEditDisplayCache` call `EditManager::invalidateProjectedNoteEditDisplayCache()` unconditionally — these are general display-cache paths with no note-edit precondition.
2. That setter raises `noteEditDisplayImmediatePaintRequested_` and bumps `noteEditDisplayInvalidateEpoch_`.
3. `shouldForceNoteEditDisplayUpdate()` reports true on either that flag or `paintedEpoch < invalidateEpoch` — again with no note-edit precondition.
4. `maybeUpdateDisplayForNoteEditSelection` in `main.cpp` calls `displayManager.update()` whenever step 3 is true, and is the **only** display call site with no `DISPLAY_UPDATE_INTERVAL` check.
5. The only clearer, `markNoteEditDisplayPainted()`, runs at the end of `DisplayManager::update()` **only when `editManager.isNoteEditActive()`**.

Once step 1 fires outside a note-edit session, nothing can clear the flag, so step 4 repaints on every loop iteration for the rest of the session. Session [`152948`](../../../../captures/session_20260812_152948.log) contains no note-edit markers at all, and the 70 fps behaviour is present from 10 s — before any capture — which is consistent only with the latch being set during boot slot restore.

**Scale:** at the intended 33 fps the same 13 ms frame would consume ~43 % of loop time instead of ~93 %. This dominates every term the admission model was written to bound, including the 129–149 ms `midisvc` samples.

**Owner:** the paint-epoch acknowledgement in `DisplayManager::update`.

**Fix (shipped):** `markNoteEditDisplayPainted()` is now called unconditionally at the end of the normal paint path. The flag records *a repaint is owed*, and `update()` performed one, so the acknowledgement was never the note-edit session's to withhold.

Behaviour during note edit is unchanged — `isNoteEditActive()` was true there, so the guard never fired. Only the non-note-edit case changes, which is the latch. The two consumers of `noteEditDisplayPaintedEpoch()` are both note-edit fader motor gates in `ControlSurfaceManager` (`processPendingSelectDependentMotorSync`, `processPendingGeometryDriverMotorSync`); they capture their required epoch at schedule time from note-edit drivers, so neither is reachable with a pending epoch outside a session.

**Residual — load/save overlay:** the overlay branch returns before the acknowledgement, and correctly so, since it draws `drawLoadSaveView` rather than the note-edit view; acknowledging there would claim a repaint that did not happen. While the overlay is open with the flag raised, the ungated path therefore still repaints every iteration at the cost of `_display.api.display()`. Bounded to a transient user mode, and not present in [`152948`](../../../../captures/session_20260812_152948.log). Correcting it belongs to the raise side or the call-site gate, not the acknowledgement.

### Consequence for stage order

RC-D outranks the resolve budget. The gather spike and the 5.48 ms window filter are real, but they affect 9 % of frames, whereas RC-D doubles the cost of all of them.

---

## 31f. Run [`155132`](../../../../captures/session_20260812_155132.log) — RC-D verified, MIDI service isolated

Transitions: `RECORDING → STOPPED_RECORDING` 272.79 s · `→ PLAYING` 273.08 s · `→ OVERDUBBING` 274.03 s · `→ PLAYING` 310.49 s · `→ OVERDUBBING` 316.50 s · `→ STOPPED` 330.78 s.

### RC-D fix confirmed

`DFRAME` reports **32.7 fps** across the session, against 70–78 fps before the fix. `DisplayUpdateTotalTime` gives 9 485 frames over 310 s at a 14.8 ms mean, so display now consumes **45 %** of loop time instead of ~93 %.

The envelope moved with it:

| Window | Before ([`152948`](../../../../captures/session_20260812_152948.log)) | After |
|---|---|---|
| Idle `msi` max | ~34 ms | **15.3 ms** |
| RECORD `msi` max | — | 19.0–24.5 ms |
| Idle `midisvc` max | ~34 µs | 2 µs |

### RECORD remains clean, and grows slowly

Across the 60 s of recording, `midisvc` max rises monotonically 604 → 628 → 646 → 652 → 661 → 694 → 709 → 771 → 850 → 888 → 905 µs, with `clk` 174–192 µs and `tracks` 153–172 µs flat. A real O(content) trend, but 300 µs over a full minute of capture — not a scheduling concern at this scale.

### The 126–146 ms MIDI service is unchanged, and is not clock dispatch

Every window from 276.9 s to 332.2 s carries a `midisvc` maximum between **126.0 ms and 145.8 ms**, exactly as in [`145555`](../../../../captures/session_20260812_145555.log). Halving display load did not touch it.

Clock dispatch cannot account for it, and the arithmetic is now unambiguous. In the 282.0 s window `midisvc` is 125 967 µs while `clk` is 4 584 µs, so explaining the call as buffered clock would need ~27 pulses drained in one `handleMidiInput`. At the measured `clockrate` of 46–49 pulses/s that is ~570 ms of accumulation, but the largest `msi` gap in the same window is 73.6 ms — about 3.5 pulses, or ~16 ms. At least 110 ms of that call is outside clock dispatch.

The same input traffic during RECORD costs 604–905 µs, so it is not message volume either. The cost is in the segments S0 does not measure: the USB device drain, the DIN drain, `usbHost.Task()`, the USB host drain, or per-message work reached from `handleMidiMessage`. **S0b is now the only open question on the dominant path.**

### Display resolve is under budget by margin, not by design

`DisplayResolveOverBudgetCount` fell from 2 223 to **86**. That is not a fix. `DisplayCommittedWindowFilter` ran 457 times and non-gather rebuilds average **4 595 µs** against the 5 000 µs budget — the same operation as before, now landing just under the threshold instead of just over. `DisplayCaptureGatherTime` still shows a single 26.3 ms sample, and `DisplayResolveLiveCapture` still peaks at 33.8 ms during the second overdub.

The branch cost has not changed; only its position relative to an arbitrary line has. Treat the low over-budget count as fragile.

### RC-E — the visual cache is marked complete while covering a fraction of the loop

This is the "piano roll not redrawn after stopping" report, and the piano roll is in fact being redrawn — there is almost nothing in the cache to draw.

`TICKS_PER_BAR` is 768 and `TICKS_PER_16TH_STEP` is 48, so the 59 136-tick loop is **77 bars** and the detailed paint window is 16 bars = 12 288 ticks.

Reading `DISP,4,…` across the stop:

```
330.913 STOPPED  visual=299  frame=298  wStart=16176  wNotes=298
331.807 PLAYING  visual=299  frame=1    wStart=0      wNotes=1
335.011 STOPPED  visual=299  frame=1    wStart=0      wNotes=1
337.736 PLAYING  visual=299  frame=1    wStart=0      wNotes=1
338.613 STOPPED  visual=299  frame=1    wStart=0      wNotes=1
```

At the overdub stop the window sits at tick 16 176 (bar 21) and holds **298 of the 299** cached notes. When transport restarts the playhead returns to tick 0, the window follows, and bars 0–15 contain **one** note. It never recovers across two further play/stop cycles.

Bars 0–15 are not empty in storage. The deferred `REVT` dump immediately after the stop walks a 16th-note grid from the beginning of the loop — `REVT,0,4,96`, `REVT,48,4,95`, `REVT,96,4,94`, … — so the record pass has roughly 256 notes in the first 16 bars alone.

`visual` holds at exactly 299 for eight seconds spanning two STOPPED periods, during which `processDeferredIdleMaintenance` runs its stopped-branch slice with priority bar 0. A dirty cache would grow. It does not, so **`visualCacheDirty` is false with a cache covering roughly 22 % of the loop** (bars 21–37 of 77). Nothing will ever backfill it.

The note density confirms the split rather than contradicting it: 298 notes in a 12 288-tick window is exactly a 16th-note grid, so the region that *is* cached is complete and the region that is not is absent entirely.

**Not yet attributed.** `rebuildVisualCacheFromPasses` does a full `gatherCommittedEvents` and only then sets `visualCacheDirty = false`, so on its own it cannot produce a partial-clean cache. The `visual` count also collapses at **both** commits — 977 → 368 at the first overdub, 1 218 → 299 at the second — and the second commit ends with *fewer* notes than the first. Two candidates, distinguishable by measurement and not yet separated:

1. the progressive idle slice over-counts during overdub and the post-commit full rebuild is the truth, in which case committed content is being lost at the second commit;
2. the post-commit full rebuild under-covers, and the progressive figure was closer to correct.

Either way the invariant *a cache may only be marked clean when it covers the whole loop* is violated. This is a storage/commit question, not a scheduling one.

### RC-E instrumentation (shipped, observation-only)

A Tier-A `VCACHE` line reports cached-note coverage at every boundary where the cache is rebuilt or declared stale:

```
#CAP,<us>,VCACHE,<phase>,ev,<gathered>,notes,<n>,first,<bar>,last,<bar>,total,<bars>,dsz,<dirtyBarsSize>,dcnt,<dirtyBars>,dirty,<flag>
```

`ev` is the gathered committed event count where the phase performed a gather and `-1` otherwise. Phases: `full` (`rebuildVisualCacheFromPasses`), `slice_clean` and `slice_nodirty` (the two points where `rebuildVisualCacheIdleSlice` clears the dirty flag), `stale` (`markPassDerivedStale`), `stale_all` (`markDisplayCachesStale`).

The readings separate the two candidates:

| Observation | Conclusion |
|---|---|
| `full` shows `ev` collapsing across the second commit | committed content is lost at commit |
| `full` shows `ev` intact but `notes` low and `first`/`last` spanning a fraction of `total` | reconstruction or coverage, not storage |
| `slice_clean` / `slice_nodirty` fires with `first`/`last` spanning a fraction of `total` | cache marked clean while partial — the invariant break |
| `stale` shows `dsz < total` or `dcnt` far below `total` | stale `dirtyBars` limits which bars later slices may revisit |

That last row is the specific asymmetry worth watching: `markPassDerivedStale` raises `visualCacheDirty` but leaves `dirtyBars` exactly as the previous rebuild left it, whereas `markDisplayCachesStale` marks every bar. `rebuildVisualCacheIdleSlice` only re-marks all bars when `dirtyBars.size() < totalBars`, so a same-length-but-mostly-clean `dirtyBars` would confine every subsequent slice to the bars that happened to hold notes at the last full rebuild. The capture-commit path takes the `markPassDerivedStale` route.

Coverage is measured as bounds only (`first`/`last` over note start and end bars) so the commit path allocates nothing.

---

## 31g-2. Run [`162230`](../../../../captures/session_20260812_162230.log) — RC-E root cause proven

Transitions: `RECORDING → STOPPED_RECORDING` 180.96 s · `→ PLAYING` 181.31 s · `→ OVERDUBBING` 183.73 s · `→ PLAYING` 225.68 s · `→ OVERDUBBING` 228.89 s · `→ PLAYING` 244.62 s · `→ STOPPED` 246.85 s. Loop is 84 bars.

### The collapse reproduces at both overdub stops

| Moment | Cached notes | Bars covered |
|---|---|---|
| First overdub commit, 225.614 s (`VCACHE,stale`) | 1 042 | 0–83 of 84 |
| 72 ms later, 225.686 s (`DISP`) | **385** | 1–30 (confirmed at 228.889 s) |
| Second overdub commit, 244.543 s (`VCACHE,stale`) | 1 298 | 1–83 of 84 |
| 248 ms later, 244.791 s (`DISP`) | **304** | — |

The cache then stops changing: 385 holds from 225.686 s to 228.889 s across 3.2 s of PLAYING, and 304 holds from 244.791 s to the end of the log at 253.4 s.

### Root cause — `DisplayManager::refreshViewportAfterOverdubStop`

The composed display frame — the bounded 16-bar detailed window plus tails, i.e. only what was on screen — is adopted wholesale as the loop's entire visual cache:

```cpp
loop.visualCache.setNotes(liveDisplayNotes);
loop.visualCache.dirtyBars.clear();
++loop.visualCache.revision;
loop.visualCacheDirty = false;
```

Every observation follows from those four lines. The post-stop note count is one window's worth (304 against `wNotes` 301; 385 against frame 305; 299 against `wNotes` 298 in [`155132`](../../../../captures/session_20260812_155132.log)). Coverage collapses to a band around the playhead. And because `visualCacheDirty` is set **false** with `dirtyBars` empty, `rebuildVisualCacheIdleSlice` returns on its first line forever after — nothing can ever backfill the rest of the loop.

This is the shipped **RC5c** behaviour from [`long_overdub_rc5_incremental_display_handoff_investigation.md`](../../long_overdub_rc5_incremental_display_handoff_investigation.md), whose intent was to avoid a synchronous full-loop gather on the overdub stop path. That intent is sound. The defect is that the adopt marks the cache **complete** rather than *this window is fresh, the rest is unknown*. It runs on all three overdub stop paths — `stopOverdubbing`, `stopOverdubbingToStopped`, and the in-edit fold.

The record-stop path does not do this. `refreshViewportAfterRecordStop` touches only `liveDisplayNotes`, which is why the cache rebuilt to full coverage after the record (689 notes over bars 0–83 at 183.728 s, growing to 1 042).

### Two earlier hypotheses are now dead

`VCACHE,full` never fires after boot, so `rebuildVisualCacheFromPasses` is not on the commit path at all and `ev` was never sampled. **Committed content is not being lost at commit** — §31g's first candidate is wrong, and the low post-commit counts were never a full-rebuild truth.

The `markPassDerivedStale` asymmetry is real but not the cause: it reported 19 of 84 dirty bars at the first commit and 11 of 84 at the second, and each was immediately followed by `markDisplayCachesStale` restoring all 84. Worth tidying, not load-bearing.

### Instrumentation gap this exposed

None of the four `VCACHE` probes fired on the adopt. It writes `loop.visualCache` directly, bypassing `markPassDerivedStale`, `markDisplayCachesStale`, and both rebuild functions. A probe belongs on the adopt itself and on `invalidateDisplayCaches`.

### Secondary: refill is unreachable beyond ±20 bars while the transport runs

Independent of the adopt, `processDeferredIdleMaintenance` limits the PLAYING/OVERDUBBING slice to `kPlayingVisualCacheNeighborhoodBars = kMaxDetailedWindowBars + 4 = 20` bars from the priority bar, alternating between playhead and loop tail. On this 84-bar loop with the playhead at bar 15 that reaches bars 0–35 and 63–83, leaving **bars 36–62 unreachable** while playing. The dead zone widens with loop length. **Shipped with RC-E fix:** neighborhood cap is dropped when the cache is in a mixed clean/dirty state so idle slices can reach any dirty bar.

### Fix shipped (RC-E)

`refreshViewportAfterOverdubStop` now calls `Loop::adoptComposedDisplayNotesFromViewport`, which copies the composed frame, marks bars touched by adopted notes clean and every other bar dirty, leaves `visualCacheDirty = true` while any bar remains dirty, and emits `VCACHE,adopt_partial`. Native tests: `test_adopt_partial_visual_cache_*` in `test_display_window_utils`. Device verify: re-run record → overdub → overdub → stop; expect `VCACHE,adopt_partial` with `dirty,1` and `visual` growing via `slice_clean` while PLAYING/STOPPED.

---

## 31g-3. Run [`165636`](../../../../captures/session_20260812_165636.log) — RC-E verified; two display defects isolated

Transitions: `RECORDING → STOPPED_RECORDING` 235.22 s · `→ PLAYING` 235.54 s · `→ OVERDUBBING` 236.90 s · `→ PLAYING` 271.57 s · `→ OVERDUBBING` 274.53 s · `→ PLAYING` 287.27 s · `→ STOPPED` 290.41 s. Loop is 81 bars.

### RC-E fix confirmed

| Moment | `VCACHE` | Notes | Bars | Dirty |
|---|---|---|---|---|
| Overdub 1 stop, 271.571 s | `adopt_partial` | 359 | 0–18 | 62 of 81 dirty |
| 575 ms later, 272.146 s | `slice_clean` | **1 352** | 0–80 | clean |
| Overdub 2 stop, 287.273 s | `adopt_partial` | 327 | 12–28 | 64 of 81 dirty |
| 601 ms later, 287.874 s | `slice_clean` | **1 417** | 0–80 | clean |

The adopt now reports the partial coverage it actually has, and idle slices backfill the whole loop in about 0.6 s. The permanent starvation is gone.

### RC-F — committed notes drain out of the piano roll during overdub

`wNotes` in the `DISP` line is `filterDisplayNotesToWindow(frameNotes, …)`, the same filter the detailed pane draws, so it is exactly the painted note count. Across the second overdub:

| Time | Paint window start | Committed painted (`wNotes`) | Capture painted (`frame − wNotes`) |
|---|---|---|---|
| 274.628 s | 9 080 | 295 | 0 |
| 280.098 s | 11 200 | 270 | 36 |
| 282.767 s | 12 224 | 248 | 66 |
| 287.275 s | 13 920 | **213** | 114 |

The overdub notes are being added correctly. What decays is the **committed** layer: as the paint window advances, fewer of the frozen committed notes fall inside it, so the previously recorded pass fades out of the roll.

The committed layer is only rebuilt when `committedWindowStale` fires, and that predicate requires `loop.visualCacheDirty`:

```cpp
const bool committedWindowStale =
    track.isOverdubbing() && havePaintWindow && loop.visualCacheDirty &&
    liveWindowGatherValid_ && !paintWindowInsideGather(...);
```

After the RC-E fix the cache goes **clean** during overdub, and the clean-cache overdub branch of `rebuildCommittedLayer` additionally sets `liveWindowGatherValid_ = false`. Both conditions now fail permanently, so the committed prefix stays pinned to the window position of the last rebuild while the paint window keeps sliding.

**This is a latent defect unmasked by the RC-E fix** — the previously permanent dirty cache was the only thing keeping the predicate alive.

**Not patched.** The obvious change — drop the `visualCacheDirty` term and record the filtered window — makes the predicate fire on nearly every frame, because the recorded gather would equal the paint window and auto-follow moves it continuously. `filterDisplayNotesByWindowInclusion` over ~1 400 cached notes measured 4 595 µs in §31f, so that would put a ~4.6 ms filter on every overdub frame. Sizing a gather window wider than the paint window is the real fix and is a budget decision, not a mechanical patch.

**Fix shipped.** The clean-cache overdub branch of `rebuildCommittedLayer` now filters a window widened by the existing `kWindowedGatherMarginBars = 2` on each side, records it as the gather window, and sets `liveWindowGatherValid_ = true`; `committedWindowStale` drops the `visualCacheDirty` term. The paint window may now slide two bars inside the gather before a rebuild, so the committed layer follows auto-follow at roughly one filter every two bars instead of every frame. This reuses the margin mechanism `DisplayNoteWindowGather` already applies for the same reason. Native tests: `test_paint_window_inside_gather_tolerates_margin_slide`.

### RC-F follow-up — the first fix moved the cost to overdub entry

[`172405`](../../../../captures/session_20260812_172405.log) confirms the fix works — `wNotes` holds at 300–313 across the whole second overdub instead of decaying — but the first overdub got much more expensive:

| Counter, first overdub | [`165636`](../../../../captures/session_20260812_165636.log) (35 s) | [`172405`](../../../../captures/session_20260812_172405.log) (11 s) |
|---|---|---|
| Committed rebuilds | 3 | 35 |
| `DisplayCaptureFullGather` | 1 | 30 |
| Mean gather | — | **27.9 ms** (835 978 µs / 30) |
| `DisplayResolveOverBudgetCount` | 4 | 70 of 241 resolves |
| `DisplayCaptureTails` calls | — | 6 of 241 resolves |

`committedWindowStale` could not fire before because the clean branch set `liveWindowGatherValid_ = false`; making it fire correctly also let it fire during the post-commit dirty window, where every bar is marked dirty and `rebuildCommittedLayer` therefore takes the **full-gather** branch. The dirty window runs `stale_all` 199.564 s → `slice_clean` 201.877 s, so for 2.3 s essentially every frame paid 27.9 ms. An over-budget frame skips `synchronizeCaptureLayer` entirely, which is why played notes stopped landing per frame, and the playhead tails ran on only 6 of 241 frames.

**Fix shipped.** Cache recovery is idle work, so the resolve path no longer gathers:

```cpp
if (loop.visualCacheDirty && canHoldCommittedLayer) {
    // idle owns recovery — do not gather committed content here
    liveDisplayNotes.resize(liveDisplayCacheCommittedNoteCount_);
    liveCommittedLayerHeldForDirtyCache_ = true;
    return;
}
```

`committedWindowStale` now additionally requires `!loop.visualCacheDirty`, and a new `committedLayerCleanCacheReady` term rebuilds once when idle finishes recovering the cache while the layer was held. The held layer is the pre-commit committed content, stale for the 0.6–2.3 s the idle slice needs — which is the trade the RC-E fix made affordable.

### Unattributed — garbled display during the second overdub

Reported at roughly bar 40 of the second overdub (~240 s). Nothing in the capture explains it: single boot header at 8.8 s, MIDI still flowing at 261 s, `AllocatorFailure` 0, `DisplayResolveLiveCapture` max 6 995 µs, `msi` 21 ms. The sparse `DFRAME` lines are **not** missed paints — the frame index advances 750 across the 31.8 s gap, so the panel was being written at 23.6 fps and the missing lines are RC-S0c capture-ring drops. The RC-G density mask is not implicated at that moment because the cache is clean from 215.386 s onward, so the mask path is inactive. Needs a reproduction with the corruption described before it can be chased.

### RC-H — OVERDUBBING→STOPPED handoff preserve (reverted)

[`174843`](../../../../captures/session_20260812_174843.log) showed a freeze at the second overdub stop. The initial diagnosis was that the clean-cache windowed path replaced the composed overdub-stop frame with a narrower 16-bar slice:

```text
191.719740 OVERDUBBING -> STOPPED
191.720643 VCACHE adopt_partial notes=373 first=7 last=27 dirty=1
191.723302 DISP STOPPED frame=373 wStart=7278 wNotes=297
192.374076 VCACHE slice_clean notes=1217 first=0 last=70 dirty=0
192.384208 DISP STOPPED frame=297 visual=1217 wStart=7278 wNotes=297
```

A fix was shipped (`liveOverdubStopHandoffActive_` / `shouldPreserveOverdubStopHandoff`) to keep the composed frame until the track leaves STOPPED.

**Reverted (RC-I).** [`183429`](../../../../captures/session_20260812_183429.log) proved the fix was wrong. The 174843 window content was density-correct (373 notes over 20 bars ≈ 18.6 notes/bar; 297 notes over 16 bars ≈ 18.6 notes/bar). The roll was static because auto-follow is off at STOPPED — expected behaviour, not a defect. RC-H instead pinned the partial adopted frame permanently:

```text
208.439  VCACHE adopt_partial notes=394 first=17 last=37 dirty=1
209.016  VCACHE slice_clean   notes=1105 first=0 last=59 dirty=0
209.601  DFRAME 394  (held through 214.171)
```

Idle completed the visual cache to 1105 notes covering all 60 bars, but the painted frame stayed at 394 notes forever. The `preservedHandoffAuthority` branch already holds the composed frame for the ~0.6 s dirty window after adopt; no extra latch is needed.

### RC-J — post-stop persistence stall while the MIDI clock still streams

The user-visible "hang" at overdub stop is not a display race — it is unthrottled deferred save work opening as soon as the track enters STOPPED. `timingCriticalTrackActive` in `main.cpp` is derived only from `isRecording() || isOverdubbing() || isPlaying()`, so it goes false at STOPPED while the external clock still streams. That opens both the deferred-restore gate (`processDeferredUndoSnapshots`, `processEditAutosave`, `reclaimUnreferencedDisabledPasses`) and `SC_CAPTURE_FLUSH(64)`.

[`183429`](../../../../captures/session_20260812_183429.log) at the third overdub stop:

```text
208.665 -> 211.827  PERS LoopUndoHistory bundle  (3.16 s)
211.841 -> 214.677  PERS SlotMeta bundle         (2.84 s)
209.482  msi=298465  clockrate=37  (47-48 during capture)
```

298 ms of main-loop block and 21% clock-pulse loss over six seconds. [`174843`](../../../../captures/session_20260812_174843.log) shows the same signature (`midisvc=229535`, `rate=37`) — pre-existing, not introduced by RC-H. The gate's missing transport term is already named in the memory-log comment in `loop()`. **Do not patch ad hoc** — this is an admission-model change and belongs behind S0b.

### RC-I telemetry — no other regressions in [`183429`](../../../../captures/session_20260812_183429.log)

- `DisplayResolveOverBudgetCount = 0` for the whole session (max 4.88 ms against the 5000 µs budget)
- `DisplayCommittedWindowFilter = 9`, `DisplayCommittedFullAssign = 0`
- `midisvc` fell from 103–107 ms in [`172405`](../../../../captures/session_20260812_172405.log) to 43–72 ms
- RC-D, RC-F, and the RC-F follow-up all hold

## 31h. Run [`191356`](../../../../captures/session_20260812_191356.log) — post-RC-I baseline (S0b handoff)

Firmware: `752273d` (RC-H reverted). Two overdub clusters on a long loop, then a new RECORD and more overdubs, including overdub-over-overdub. Display did not freeze. This is the **current stability baseline** for S0b.

### Display — freeze closed

Every overdub stop completes the visual cache (`slice_clean` covers the whole loop: 1766–2018 notes in cluster 1, 1024–1482 after the new record). Final `OVERDUBBING → STOPPED` at 1230.063 s:

```text
1230.064  VCACHE adopt_partial notes=483 first=19 last=39 dirty=1
1230.734  VCACHE slice_clean   notes=1482 first=0 last=63 dirty=0
1230.745  DISP STOPPED visual=1482 wNotes=385 wStart=17552
1231.499  DFRAME 385  (idx keeps advancing)
```

385 notes is the stopped 16-bar window of a clean cache, not the RC-H pin. Auto-follow off at STOPPED is expected.

### MIDI — clock rate held; service latency did not

`clockrate` stayed **47–48** (24 PPQN at 120 BPM) through every PLAYING and OVERDUB window. No half-tempo collapse while capturing. RECORD remains clean (`midisvc` 0.3–0.8 ms, `clockrate` 47–48).

Service duration is still far outside the observational 5 ms ceiling. `clk`/`tracks` stay 4–10 ms, so the remainder is unattributed inside `handleMidiInput`:

| Window | `midisvc` max | `clockrate` |
|---|---|---|
| Overdub cluster 1 (335–371 s) | 99–133 ms | 47–48 |
| Longer overdub (374–405 s) | **218–221 ms** | 46–49 |
| Playing after new record (1045–1155 s) | 47–94 ms | 47–49 |
| Later overdubs (1165–1226 s) | 110–125 ms | 47–48 |
| RECORD (935–1035 s) | 0.3–0.8 ms | 47–48 |

PLAYING ↔ OVERDUB edges spike `msi` to **237–433 ms**. ~15 `midisvc` samples per 5 s window exceed 5 ms. A 220 ms call is ~10 clock periods (20.8 ms at 120 BPM): pulses are counted later in the same window, so `clockrate` stays 48 — **catch-up jitter, not dropped clock**.

### RC-J reproduced, still behind S0b

First stop 437.7 s: `clockrate` 48 → 24 → 0, `msi` 467 ms. Final stop 1230 s: 48 → 36 → 0, `msi` 365 ms. Same deferred-save-at-STOPPED signature as [`183429`](../../../../captures/session_20260812_183429.log). Do not patch.

### Residuals that must not jump the S0b queue

- Overdub piano-roll frame skip / rolling window that restores in PLAYING — RC-F follow-up holds the committed layer while `visualCacheDirty`; §31d bailout class. Independent of S0b.
- `DisplayResolveOverBudgetCount` rose to 140 in this longer session (max `DisplayResolveLiveCaptureTime` 9.4 ms). Display completeness is S7.
- RC-S0c: 12 `RING,overflow`; capture still starts mid-session (first transition at 333.8 s). Pair the Tier-A transmit allowance with S0b if RECORD windows would otherwise be lost.

### S0b handoff (new chat)

**Authorized next stage: S0b device re-run** — firmware already segments `MidiHandler::handleMidiInput`. See §31c. **Closed in §31i** ([`193645`](../../../../captures/session_20260812_193645.log)): named drain is `usbdev`.

- **Owner:** `MidiHandler::handleMidiInput` (and the four sequential drains it already runs). `RuntimeTimingEnvelope` emits `DIAG,usbdev` / `din` / `hosttask` / `hostdrain`. Do not add a scheduler or change MIDI service density.
- **Invariant:** S0b takes no scheduling decision. `micros()` deltas into the existing 5 s window; emit Tier-A `DIAG` lines.
- **Ownership / transition change:** NO.
- **Baseline to beat:** this capture. Attribute the 218–221 ms sustained overdub `midisvc` and the 110–125 ms later-overdub samples to a named drain. The 762.5 ms overdub-entry sample from [`145555`](../../../../captures/session_20260812_145555.log) remains in the exit criterion.
- **Already ruled out:** clock dispatch (`clk` ≈ `tracks`, 4–10 ms); RC-C extra MIDI drain (RECORD is 0.3–0.8 ms with that drain active).
- **Do not:** implement `RuntimeWorkBudget`, change service density, patch RC-J, chase display frame-skip, or start S1.

**Exit criterion (updated):** the 762.5 ms overdub-entry sample, the recurring 129–149 ms samples, **and** the [`191356`](../../../../captures/session_20260812_191356.log) 218–221 ms / 110–125 ms overdub samples are each attributed to a named segment, reported separately for RECORD versus PLAYING/OVERDUB.

### RC-G — the overview strip is fed only the detailed window during RECORD

`PianoRollDraw` chooses the overview density source as:

```cpp
const DisplayNoteVec& overviewDensityNotes =
    (!loop.visualCacheDirty && !loop.visualCache.notes.empty())
        ? loop.visualCache.notes
        : ((captureCritical && useBoundedWindow) ? *detailedNotes : notes);
```

On a first record there are no committed passes, so `visualCache` is empty (`DISP` reports `visual=0` for the whole record) and `captureCritical && useBoundedWindow` is true. The overview therefore renders `*detailedNotes` — **only the current 16-bar window**. Everything outside the moving window is blank, which is the reported gap.

During overdub the first branch applies (`visual` 1 296–1 417, clean), so the overview shows the whole loop. That is exactly the reported difference between record and overdub.

The frame itself is complete during record — at 227.643 s `frame=1238` against `loopLen=59408` — so this is purely the overview source choice, not missing data.

**Not patched.** Feeding `notes` to the overview restores the display but reintroduces the O(record-length) per-frame scan that RC-C B removed, and that grows without bound on long records. A density histogram over the strip's ~256 columns is the bounded answer.

**Fix shipped.** `DisplayManager::updateOverviewCaptureDensity` maintains a per-bar band mask — one byte per loop bar, one bit per pitch band, with `kOverviewBandCount = 8` bands of 16 semitones mapping onto the 8 overview strip rows. It folds in only capture-preview notes it has not seen yet, so the per-frame cost is O(new notes); open notes are re-folded each frame, bounded by `openNoteIndices`. The mask is indexed by bar rather than by screen column, so a growing record pass only appends bars and previously accumulated bars stay valid as the loop rescales. `drawOverviewStrip` takes an optional mask and, when present, draws O(loop bars) instead of O(notes). The mask is built only while recording or overdubbing with no usable `visualCache`; every other state keeps the existing full-cache path. Native tests: `test_overview_band_mask_*`, `test_overview_band_for_note_covers_full_midi_range`.

### Remaining findings — status against this run

**S0b is still the dominant path, and clock dispatch is now fully accounted for.** During PLAYING/OVERDUB, `midisvc` runs **82.7–128.9 ms** while `clk` stays at 3.7–9.8 ms. Notably `clk` and `tracks` are near-identical in every window (4 030/3 999, 4 114/4 084, 9 840/9 829), so clock dispatch is almost entirely `updateAllTracks` and is already attributed. That leaves **79–125 ms per call unattributed inside `handleMidiInput`** — unchanged in character from §31f, and still the only open question on the dominant path.

**RECORD remains clean and grows slowly.** `midisvc` 585 → 831 µs across 66 s of recording, `clk` 159–176 µs, `tracks` 143–156 µs, `msi` 17.8–22.7 ms. Consistent with §31f.

**The post-stop stall reproduces.** At 292.127 s `msi` is 131.5 ms with `midisvc` 3.2 ms and `clockrate` falling to 31; at 297.142 s `midisvc` is 131.1 ms with `clk` 0 and `clockrate` 0. Same signature as the 140 ms stall in §31f, still uninvestigated.

**New: a 324 ms record-stop block.** The 237.101 s window reports `msi` **323 851 µs** against `midisvc` 9 883 µs, so roughly 314 ms of that interval is outside MIDI service entirely. This sits on the record-stop commit and deferred-save path and is larger than anything measured during capture.

**RC-S0c is not resolved.** `DIAG` windows run 6.7 → 71.8 s and then jump to 166.9 s — a 95 s hole during RECORD, the same class of gap as the pre-177 s hole in [`145555`](../../../../captures/session_20260812_145555.log). `RING,overflow` still appears 3 times. The Tier-A transmit allowance is still owed.

---

### Display during overdub — the §31d bailout, unchanged

`DIAG,timing_max,DisplayResolveLiveCapture` reads **33 595 µs** at 316.7 s and **33 844 µs** at 321.8 s, both inside the second overdub, against the 5 000 µs budget. At 6.8× over, `reuseLastValidFrame` holds the previous frame and both `replaceCaptureLayer` and the playhead tails are skipped, so notes being played into the overdub are not composed into the frame. This is the behaviour described in §31d and it is worse in the second pass because the committed layer is larger.

### MIDI drift grows with content, and is worse in the second overdub

| Pass | `msi` max | `midisvc` max |
|---|---|---|
| Overdub 1 (276.9–310.5 s) | 289.6 → 51.9 ms | 145.3 → 134.5 ms |
| Overdub 2 (316.5–330.8 s) | 98.5 → 48.9 ms | 141.4 → 145.8 → **223.4 ms** |

`midisvc` rises monotonically within each pass and starts higher in the second. The 223.4 ms sample sits in the window containing the stop. `clk` never exceeds 9.8 ms in any of these windows, so this remains the unattributed `handleMidiInput` term from §31f — **S0b**.

### Separate: a 140 ms post-stop stall

At 337.2 s and 342.2 s, after `PLAYING → STOPPED`, `msi` max is 140.6 ms and 140.2 ms while `midisvc` is 81–126 µs, `clk` is 0, and `clockrate` is 0. Whatever blocks the loop there is not MIDI service and not clock dispatch. `tracks` is 4.1–9.0 ms with `clk` at 0, which is the internal-clock ISR path rather than `onMidiClockPulse`. Not investigated.

---

## 31i. Run [`193645`](../../../../captures/session_20260812_193645.log) — S0b attributed to `usbdev`

Firmware: `d99576c` (S0b drain probes). RECORD then multiple overdubs on a 64-bar loop (`RECS,stop` length 49152). Envelope tags `usbdev` / `din` / `hosttask` / `hostdrain` present. Capture starts at boot (`HDR` 5.770 s).

### Named drain

In every complete window, `usbdev` max equals `midisvc` max within **0–3 µs**. `din` is 1 µs, `hosttask` is 0–1 µs, `hostdrain` is 1–100 µs. Clock dispatch is nested inside `usbdev` (`clk` ≈ `tracks`) and does not explain the remainder.

| Phase | Window | `midisvc` | `usbdev` | `clk` | `din` / `hosttask` / `hostdrain` | `clockrate` |
|---|---|---|---|---|---|---|
| RECORD | 126.1 s | 687 µs | 686 µs | 183 µs | 1 / 1 / 1 | 47 |
| RECORD | 131.1 s | 701 µs | 700 µs | 175 µs | 1 / 1 / 1 | 48 |
| Overdub entry | 141.2 s | 84.1 ms | 84.1 ms | 14.3 ms | 1 / 1 / 81 | 48 |
| PLAYING/OVERDUB | 181.3 s | 108.0 ms | 108.0 ms | 3.5 ms | 1 / 1 / 2 | 47 |
| PLAYING/OVERDUB | 266.6 s | 134.6 ms | 134.6 ms | 4.9 ms | 1 / 1 / 2 | 46 |
| Overdub peak | 276.6 s | **154.0 ms** | **154.0 ms** | 10.4 ms | 1 / 1 / 98 | 48 |
| Later overdub | 296.6 s | 127.2 ms | 127.2 ms | 4.9 ms | 1 / 1 / 2 | 47 |
| Post-stop | 306.7 s | 3 µs | 1 µs | 0 | 1 / 1 / 2 | 0 |

`overCount` for `usbdev` equals `overCount` for `midisvc` in every complete window (same samples).

**RECORD** stays 0.7 ms, almost all `usbdev`, of which ~0.18 ms is `clk`. **PLAYING/OVERDUB** is 78–154 ms, almost all `usbdev`, of which 3.5–14.3 ms is `clk`. The 70–144 ms remainder is USB-device read + `dispatchMidiBatch` / `handleMidiMessage` other than Clock.

The historical 762.5 ms ([`145555`](../../../../captures/session_20260812_145555.log)), 129–149 ms, and 218–221 ms / 110–125 ms ([`191356`](../../../../captures/session_20260812_191356.log)) samples are the same `midisvc` term. This run did not reproduce those exact peaks (max 154 ms) but identifies that term as `usbdev`.

### Ruled out

- **DIN drain** — 1 µs in every window.
- **`usbHost.Task()`** — 0–1 µs.
- **USB host drain** — ≤100 µs; never a millisecond-scale contributor.
- **Clock dispatch** — still 3.5–14.3 ms inside `usbdev`; `clk` ≈ `tracks`.
- **RC-C extra MIDI drain** — RECORD has that drain active and `usbdev` is 0.7 ms.

### RC-J reproduced, still not patched

`PLAYING → STOPPED` at 300.671 s. Next window 301.660 s: `clockrate` 38, `msi` 303 ms, `usbdev`/`midisvc` still 132 ms (window includes the stop). Window 306.667 s: `clockrate` 0, `msi` 273 ms, `midisvc` 3 µs, `usbdev` 1 µs. The post-stop stall is outside MIDI service.

## 31p. Runtime bundle scope finding — [`112104`](../../../../captures/session_20260813_112104.log)

The later overdub-stop investigation distinguishes two costs:

- `ODUB,stop` remains bounded at roughly 9–31 ms.
- `ODUB,stage,begin_capture` separately reaches 8.09 s; that is the overdub source-view fill and is not the stop owner.

The stop-adjacent stall is deferred persistence. The first save begins immediately after the overdub stop, and the new `PERS,bundle` telemetry reports:

```text
PERS,bundle,LoopUndoHistory,16568186,41680,1316,263,1
PERS,bundle,LoopUndoHistory,4246370,39562,1320,264,1
```

These fields are `workType,totalUs,maxSliceUs,sliceCount,undoEntryCount,snapshotCount`. The runtime bundle therefore occupies 4.25–16.57 s in aggregate across 1,316–1,320 slices, while each individual slice remains below 42 ms. `DIAG,msi` reaches 7.23–9.35 s during the same drain. The payload contains 263–264 undo entries but only one snapshot, so the evidence does not support stop-time loop rematerialization as the owner.

### Scope defect

`PersistKey` admission is already intended to be scoped (`LoopId` for `LoopPersist` / `LoopUndoHistory`, slot for metadata), but `beginDeferredRuntimeBundleWrite` opens the shared runtime bundle and `stepDeferredSaveJobUndoStacks` walks every track's global undo stack. A keyed item therefore causes broad workspace serialization. Many overdubs increase the entry count and consequently increase the aggregate drain, but the cost is the broad runtime-bundle payload, not a new materialize operation at overdub stop.

The first capture also exposed an attribution defect: a completed bundle summary can report a later work type than the visible bundle-start marker. Snapshot the work type at bundle start before using it for attribution.

### Proposal: preserve one runtime owner and narrow payload scope

Keep `StorageManager::processDeferredSaveState` / `stepPersistenceWorkItem` as the single runtime persistence owner. Do not add a Track-level saver or a parallel runtime FSM.

1. Keep `LoopPersist` loop-owned and write only the admitted `LoopId`.
2. Implement the deferred DEC-024/B6 direction for `LoopUndoHistory`: persist undo data in a loop-owned unit and serialize only entries for the touched loop. Do not serialize every track's global undo stack for one overdub stop.
3. Keep `SlotMeta` scoped to the touched track/slot. Do not enqueue it for a loop-only overdub mutation unless slot metadata actually changed.
4. Admit global, track, and workspace-footer sections only from their own dirty keys. “Active” and “selected” are routing context, not sufficient save scope; the committed `LoopId` and explicit dirty key are authoritative.
5. Keep full runtime-bundle rewrite/compaction as an explicit cold operation. Skipping clean sections in a replacement file without copy-forward or loop-owned files would lose untouched state.
6. Coalesce repeated `(PersistWorkType, PersistKey)` admissions through the existing queue and preserve the current one-slice scheduler.

The target invariant is:

> An overdub stop persists the touched loop and its touched undo scope under the existing StorageManager owner; it does not walk unrelated tracks, slots, or undo entries.

This is a payload-scope and admission change, not a MIDI service-density change. It requires a design/implementation gate before firmware because the current interim runtime-bundle wire format must change to preserve unrelated persisted state.

### Residuals

- RC-S0c: 13 `RING,overflow`; RECORD envelope missing from 10.4 s (`STOPPED → ARMED`) until 126.1 s; `ARMED → RECORDING` / `RECORDING → STOPPED_RECORDING` absent. Three windows emit only `clockrate` (176.3, 186.3, 231.3 s).
- Next observation split, **not this stage:** inside `usbdev` — `usbMIDI.read()` versus `dispatchMidiBatch` / `handleMidiMessage` (`sendMidiThru`, `SC_MIDI_IN`, channel/button dispatch). Do not start S1 or patch RC-J.

**S0b exit:** met. S0c attributed in §31k (`usbdisp`).

---

## 31j. S0c — split `usbdev` (observation only)

**Status:** firmware **implemented**; device-attributed in [`195240`](../../../../captures/session_20260812_195240.log) (§31k) — PLAYING/OVERDUB `usbdev` is `usbdisp`.

[`193645`](../../../../captures/session_20260812_193645.log) showed `usbdev` = `midisvc` within 0–3 µs. S0c splits that drain without changing service density or taking a scheduling decision.

| Probe | What it measures | How sampled |
|---|---|---|
| `DIAG,usbread` | `usbMIDI.read()` loop | duration of one USB-device drain's read |
| `DIAG,usbdisp` | `dispatchMidiBatch` for that USB batch | duration of one USB-device dispatch |
| `DIAG,usbcap` | `SC_MIDI_IN` for `SOURCE_USB` | **sum** of those calls in that dispatch, then max across calls |
| `DIAG,usbthru` | `sendMidiThru` for `SOURCE_USB` | **sum** of those calls in that dispatch, then max across calls |

`usbdev` remains the outer read+dispatch duration. `clk` remains the max of one `onMidiClockPulse` (nested in `usbdisp`). `usbcap` / `usbthru` are batch sums so they can explain a 78–154 ms call; a per-message max would not.

Remainder of `usbdisp` after `usbcap` + `usbthru` + nested `clk` is channel/button/transport work in `handleMidiMessage`. That remainder is not a fifth sequential drain; name it only if the four probes leave a millisecond-scale gap.

**Exit criterion:** the [`193645`](../../../../captures/session_20260812_193645.log) 78–154 ms PLAYING/OVERDUB `usbdev` samples (and RECORD 0.7 ms) are each attributed to `usbread`, `usbdisp`, `usbcap`, and/or `usbthru`, reported separately for RECORD versus PLAYING/OVERDUB. Do not start S1 or patch RC-J.

---

## 31k. Run [`195240`](../../../../captures/session_20260812_195240.log) — S0c attributed to `usbdisp`

Firmware: `c33a30a` (S0c probes). RECORD (~64 bars, `RECS,stop` length 36864) then multiple overdubs. Envelope tags `usbread` / `usbdisp` / `usbcap` / `usbthru` present. Capture starts at boot (`HDR` ~7.6 s). `clockrate` 47–48 through RECORD and PLAYING/OVERDUB.

### Named sub-segment

In every complete PLAYING/OVERDUB window, `usbdisp` max equals `usbdev` / `midisvc` max within **0–3 µs**. `usbread` is 3–13 µs. `usbcap` is 22–38 µs (batch **sum** of `SC_MIDI_IN`). `usbthru` is 4–6 µs (batch **sum** of `sendMidiThru`). Clock dispatch is nested in `usbdisp` (`clk` ≈ `tracks`, 3.9–18.2 ms) and does not explain the remainder.

| Phase | Window | `midisvc` | `usbdisp` | `usbread` | `usbcap` | `usbthru` | `clk` | `clockrate` |
|---|---|---|---|---|---|---|---|---|
| RECORD | 53.3 s | 327 µs | 324 µs | 2 µs | 26 µs | 5 µs | 173 µs | 47 |
| RECORD | 118.4 s | 614 µs | 612 µs | 3 µs | 28 µs | 6 µs | 175 µs | 47 |
| Overdub entry | 123.4 s | 51.0 ms | 51.0 ms | 9 µs | 34 µs | 5 µs | 18.2 ms | 47 |
| PLAYING/OVERDUB | 153.4 s | 51.1 ms | 51.1 ms | 3 µs | 29 µs | 5 µs | 4.4 ms | 47 |
| PLAYING/OVERDUB | 198.4 s | 57.7 ms | 57.7 ms | 3 µs | 36 µs | 6 µs | 4.9 ms | 47 |
| Later overdub | 233.5 s | **92.8 ms** | **92.8 ms** | 9 µs | 22 µs | 4 µs | 9.2 ms | 47 |
| Stop window | 238.5 s | 88.7 ms | 88.7 ms | 9 µs | 31 µs | 4 µs | 4.8 ms | 47 |

`overCount` for `usbdisp` equals `overCount` for `midisvc` in complete windows (same samples).

**RECORD** stays 0.3–0.6 ms `usbdisp` (two spikes 2.2 ms / 5.5 ms at 58.3 s and 88.3 s). **PLAYING/OVERDUB** is 48–93 ms, almost all `usbdisp`. After subtracting `clk` (max of one pulse), **40–85 ms** of that dispatch is still unattributed inside `handleMidiMessage`.

### Ruled out

- **`usbMIDI.read()`** — 3–13 µs.
- **`SC_MIDI_IN`** — 22–38 µs summed across the USB batch. Not the ring-eviction cost hypothesized from RC-S0c.
- **`sendMidiThru`** — 4–6 µs summed across the USB batch.
- **DIN / `usbHost.Task()` / USB host drain** — 1–89 µs, unchanged from S0b.

### Residuals

- `clk` is the max of one `onMidiClockPulse`, not the sum of clocks in that USB batch. S0d (`usbclk`) is the batch sum. Channel/button/transport remainder is `usbnote` / `usbcc` / `usbtrans` (§31l).
- RC-S0c: 10 `RING,overflow`. RECORD envelope is present from 38 s (better than [`193645`](../../../../captures/session_20260812_193645.log)). `ARMED → RECORDING` absent; `RECORDING → STOPPED_RECORDING` present at 119.582 s.
- Capture ends at 241.5 s, 0.9 s after `PLAYING → STOPPED`; no post-stop `clockrate` 0 window in this log. RC-J not re-measured here. Do not patch.

**S0c exit:** met. S0d firmware: §31l.

---

## 31l. S0d — split `handleMidiMessage` remainder (observation only)

**Status:** firmware **implemented**; device-attributed in [`200452`](../../../../captures/session_20260812_200452.log) (§31m) — PLAYING/OVERDUB `usbdisp` is `usbnote`.

[`195240`](../../../../captures/session_20260812_195240.log) showed `usbdisp` = `usbdev` within 0–3 µs, with `usbcap` / `usbthru` / `usbread` in the tens of microseconds. `clk` is the max of one `onMidiClockPulse`, so it cannot prove or disprove a batch of clock dispatches. S0d sums the remaining `handleMidiMessage` work inside one USB-device dispatch.

| Probe | What it measures | How sampled |
|---|---|---|
| `DIAG,usbclk` | `onMidiClockPulse` for `SOURCE_USB` Clock | **sum** in that dispatch, then max across calls |
| `DIAG,usbnote` | `handleNoteOn` / `handleNoteOff` | **sum** in that dispatch, then max across calls |
| `DIAG,usbcc` | `handleControlChange` / pitch / aftertouch / program change | **sum** in that dispatch, then max across calls |
| `DIAG,usbtrans` | `handleMidiStart` / `handleMidiStop` / `handleMidiContinue` | **sum** in that dispatch, then max across calls |

`clk` remains the per-pulse max. `usbclk` is the batch sum of those pulses in one USB dispatch. If `usbclk` matches `usbdisp`, the 48–93 ms call is many `updateAllTracks` in one batch. If `usbnote` or `usbcc` matches, the cost is channel/button work.

**Exit criterion:** the [`195240`](../../../../captures/session_20260812_195240.log) 48–93 ms PLAYING/OVERDUB `usbdisp` samples (and RECORD 0.3–0.6 ms) are each attributed to `usbclk`, `usbnote`, `usbcc`, and/or `usbtrans`, reported separately for RECORD versus PLAYING/OVERDUB. Do not start S1 or patch RC-J.

**S0d exit:** met. Attribution: §31m.

---

## 31m. Run [`200452`](../../../../captures/session_20260812_200452.log) — S0d attributed to `usbnote`

Firmware: `89cf3b3` (S0d probes). RECORD (`RECS,stop` length 78336) then multiple overdubs. Envelope tags `usbclk` / `usbnote` / `usbcc` / `usbtrans` present. Capture starts at boot (`HDR` ~6.8 s). `clockrate` 47–48 through RECORD and most PLAYING/OVERDUB.

### Named sub-segment

In complete PLAYING/OVERDUB windows, `usbnote` max is **98.7–99.9 %** of `usbdisp` max (`usbdisp − usbnote` is 0.1–2.3 ms except one 7.5 ms outlier at 419.2 s). `overCount` for `usbnote` equals `overCount` for `usbdisp` in most windows (same samples). `usbclk` is 3.2–10.4 ms — the same scale as `clk` / `tracks`, not the 89–300 ms dispatch. `usbcc` is 0. `usbtrans` is 0.

| Phase | Window | `midisvc` | `usbdisp` | `usbnote` | `usbclk` | `usbcc` | `usbtrans` | `clk` | `clockrate` |
|---|---|---|---|---|---|---|---|---|---|
| RECORD | 212.3 s | — | 1.02 ms | 826 µs | 190 µs | 0 | 0 | — | 48 |
| Overdub entry | 217.3 s | 132.8 ms | 132.8 ms | 131.6 ms | 7.1 ms | 0 | 0 | 4.6 ms | 48 |
| PLAYING/OVERDUB | 262.4 s | 92.0 ms | 92.0 ms | 90.8 ms | 3.6 ms | 0 | 0 | 3.6 ms | 47 |
| Later overdub | 357.5 s | 177.3 ms | 177.3 ms | 176.1 ms | 3.5 ms | 0 | 0 | 3.5 ms | 47 |
| Peak | 419.2 s | **669.8 ms** | **669.8 ms** | **662.3 ms** | 10.4 ms | 0 | 0 | 6.2 ms | 42 |
| After overdub stop | 424.2 s | 7.2 ms | 7.2 ms | 101 µs | 7.0 ms | 0 | 0 | 7.0 ms | 51 |
| Stop window | 449.5 s | 291.4 ms | 291.4 ms | 290.3 ms | 6.1 ms | 0 | 0 | 4.2 ms | 40 |

The 424.2 s window is the exception that names `usbclk`: after `OVERDUBBING → PLAYING` at 424.203 s, the peak dispatch is the clock-sum (7.0 of 7.2 ms), not notes. That does not explain the 89–670 ms PLAYING/OVERDUB samples.

**RECORD** stays ~1 ms `usbdisp` (826 µs `usbnote`, 190 µs `usbclk`). **PLAYING/OVERDUB** is 89–300 ms typical, 670 ms peak, almost all `handleNoteOn` / `handleNoteOff`.

### Ruled out

- **Clock batch sum (`usbclk`)** — 3.2–10.4 ms. Not the 89–670 ms dispatch. Closes the S0c gap: `clk` was a per-pulse max; the batch sum is still an order of magnitude below `usbdisp`.
- **CC / pitch / AT / PC (`usbcc`)** — 0 in every window.
- **Start / Stop / Continue (`usbtrans`)** — 0 in every window.
- **`usbread` / `usbcap` / `usbthru`** — still microseconds, unchanged from S0c.

### Residuals

- Next observation split, **not this stage:** inside `accumulatePendingNoteChangesForIncomingNote` — `noteappend`, `notechg`, `noterecon`, `notepair` (§31n). Geometry remainder is `notechg − noterecon − notepair` by subtraction.
- RC-S0c: 11 `RING,overflow`. RECORD envelope missing from 6.9 s until 197.2 s; `ARMED → RECORDING` absent; `RECORDING → STOPPED_RECORDING` present at 214.607 s. Several PLAYING/OVERDUB windows drop `msi` / `midisvc` / `usbdev` while nested USB tags survive.
- Capture ends at 453.4 s, 4.4 s after `OVERDUBBING → STOPPED` at 449.051 s. Stop window `clockrate` 40; no post-stop `clockrate` 0 window. RC-J not re-measured here. Do not patch.

**S0d exit:** met.

---

## 31n. S0e — split overdub note-off path (observation only)

**Status:** **attributed** in [`204221`](../../../../captures/session_20260812_204221.log). Follow-through: [`realtime_incremental_work_overdub_note_change_bugfix.md`](../../realtime_incremental_work_overdub_note_change_bugfix.md).

[`200452`](../../../../captures/session_20260812_200452.log) attributed PLAYING/OVERDUB `usbnote` to `handleNoteOn` / `handleNoteOff`. Analysis showed the cost is per note-off while overdubbing (control window 419.2–424.2 s with no notes: `usbnote` 101 µs). The suspected owner is `Loop::accumulatePendingNoteChangesForIncomingNote`, which calls `NoteUtils::reconstructDisplayNotes` over the full `overdubSourceViewEvents_` on every note-off. S0e splits that path without changing behavior.

| Probe | What it measures | How sampled |
|---|---|---|
| `DIAG,noteappend` | `appendCaptureEventWithResult` in `recordMidiEvents` | **sum** in that USB dispatch, then max across calls |
| `DIAG,notechg` | `accumulatePendingNoteChangesForIncomingNote` | **sum** in that USB dispatch, then max across calls |
| `DIAG,noterecon` | `reconstructDisplayNotes` inside note-change | **sum** in that USB dispatch, then max across calls |
| `DIAG,notepair` | candidate-pair scan over reconstructed notes | **sum** in that USB dispatch, then max across calls |

S0e probes are gated to the active USB-device nested window (`beginUsbDeviceNested` / `commitUsbDeviceNested`) so DIN and USB-host note-offs do not leak into the batch sum. Geometry (`analyzeEditSessionInteractions` + `resolveConstrainedGeometry`) is `notechg − noterecon − notepair` by subtraction. Expected closure: `usbnote` ≈ `noteappend + notechg`.

**Exit criterion:** met in [`204221`](../../../../captures/session_20260812_204221.log). PLAYING/OVERDUB `usbnote` is `notechg`. Split on a 4257-event / 2109-note loop: `noterecon` 177 ms, `notepair` 98 ms, `noteappend` 59 µs. Geometry remainder is small. Follow-through is RC-K1–K3 (§31o), not admission S1.

---

## 31o. S0e follow-through — RC-K1 / RC-K2 / RC-K3

**Status:** firmware **shipped**; device re-measure pending. Plan: [`realtime_incremental_work_overdub_note_change_bugfix.md`](../../realtime_incremental_work_overdub_note_change_bugfix.md).

This is the **targeted bounded-work** branch of §32 (existing mechanisms sufficient). It is **not** admission-model S1.

| RC | Invariant | Change |
|----|-----------|--------|
| **K1** | Reconstruct dedup is O(N log N), not O(N²) | `reconstructNotesImpl` tracks seen `(note, startTick, endTick)` in an ordered set |
| **K2** | Candidate scan does not walk source events per note | Delete `noteIdHasChannel` (DEC-033) |
| **K3** | Source view reconstructed once per overdub pass | `overdubSourceViewNotes_` filled in `establishOverdubSourceView` |

S0e probes stay as the measurement gate. Expected: `noterecon` leaves the note-off path; `notechg` falls under the 5 ms observational soft ceiling.

**Exit criterion:** a grown-loop overdub capture shows `noterecon` ≈ 0 on the note-off path and `notechg` well under 5 ms, versus [`204221`](../../../../captures/session_20260812_204221.log) 177 / 274 ms. Do not start admission S1 or patch RC-J.

---

