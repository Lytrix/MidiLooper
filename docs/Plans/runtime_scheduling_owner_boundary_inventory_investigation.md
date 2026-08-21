# Runtime scheduling — owner-boundary inventory and MIDI service interval floor

**Status:** Research / investigation. **Authorizes no firmware.**
**Date:** 2026-08-21
**Kind:** investigation
**Question:** what has to change architecturally so the interval between MIDI service opportunities is bounded at **25–30 ms** instead of the observed 500–2900 ms.

**Contract:** [`runtime_scheduling_admission_model_architecture.md`](runtime_scheduling_admission_model_architecture.md) — this document supplies **Stage R1C** (remaining owner inventory) evidence and **Stage C** ceiling-derivation input. It does not replace the contract and does not start Stage A.
**Roadmap:** [`runtime_scheduling_owner_boundary_admission_refinement.md`](runtime_scheduling_owner_boundary_admission_refinement.md)
**Active scope linkage:** [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) § Overdub lag plan (P1–P4)

**Evidence:** [`084213`](../../captures/session_20260821_084213.log) (best steady state to date), [`085458`](../../captures/session_20260821_085458.log) (regression run), [`082858`](../../captures/session_20260821_082858.log), [`015645`](../../captures/session_20260821_015645.log)

---

## 1. The target has to be restated before it can be engineered

Two different quantities are measured today, and only one of them is what the 25–30 ms goal is about.

| Quantity | Definition | Owner |
|---|---|---|
| **MIDI Input Gap** (`DIAG,midi_gap`) | `handleMidiInput()` **exit** → next **entry** | `RuntimeTimingTelemetry::noteMidiInputEnter` / `noteMidiInputExit` |
| **`handleMidiInput()` duration** (`DIAG,midi_input`) | time spent **inside** `handleMidiInput()` | same owner |

```218:237:src/Utils/RuntimeTimingTelemetry.cpp
void noteMidiInputEnter(uint32_t nowUs) {
  State& s = state();
  if (s.windowStartUs == 0) {
    s.windowStartUs = nowUs;
  }
  if (s.lastInputExitUs != 0) {
    recordSample(s.midiGap, nowUs - s.lastInputExitUs);
  }
  s.inputEnterUs = nowUs;
  s.inputActive = true;
}
```

The interval a MIDI event actually waits is the **sum**. In [`084213`](../../captures/session_20260821_084213.log), transport-running 5 s windows report `midi_gap` maxima of 29–60 ms while `midi_input` maxima in the same windows reach **319 847 µs** and **438 534 µs**. In those two windows the decomposition inside `handleMidiInput()` is `usbdev` 319 844 / 438 530 → `usbdisp` 319 841 / 438 529 → `clk` 15 110 / 438 528 → `tracks` 292 446 / 438 503.

`tracks` brackets `TrackManager::updateAllTracks` in full, and `clk` brackets `ClockManager::onMidiClockPulse` around it:

```202:209:src/ClockManager.cpp
  RUNTIME_TIMING_NOTE_PLAYBACK_SERVICE_ENTER(clockDispatchStartUs,
                                             microsPerTick * Config::TICKS_PER_CLOCK);
#endif
  trackManager.updateAllTracks(currentTick);
  lastMidiClockTime = micros();
#if RUNTIME_TIMING_ENABLED
  RUNTIME_TIMING_NOTE_CLOCK_DISPATCH(lastMidiClockTime - clockDispatchStartUs);
#endif
```

**Consequence:** a target expressed only on `midi_gap` is unachievable as a musical guarantee, because a single clock pulse dispatched inside `handleMidiInput()` has been measured at 438 ms. The engineering target must be:

```text
max(midi_input) + max(midi_gap)  <=  25–30 ms      (interval contract)
late_event_count == 0, max_lateness_us <= 0        (correctness contract, C15)
```

This restatement is the single most important finding in this document. Everything below is scoped to it.

---

## 2. Where the MIDI polls actually are

`loop()` calls `midiHandler.handleMidiInput()` between two and five times per turn. Three of those five call sites are conditional.

| Call site | Gate | Fires in plain PLAYING? |
|---|---|---|
| loop start | unconditional | yes |
| before `runDeferredIdleMaintenance` | `MidiServiceDrain::aroundIdleMaintenance(postOverdubPlayingMidiDrain)` | **no** |
| after `runDeferredIdleMaintenance` | same gate | **no** |
| after `runDeferredLoadAndDisplayFrame` | `MidiServiceDrain::afterDeferredDisplay(captureActive, postOverdubDrain)` | **no** |
| end of turn | unconditional | yes |

```11:23:include/Utils/MidiServiceDrain.h
inline bool aroundIdleMaintenance(bool postOverdubPlayingDrain) {
  return postOverdubPlayingDrain;
}

inline bool afterDeferredDisplay(bool captureActive, bool postOverdubPlayingDrain) {
  return captureActive || postOverdubPlayingDrain;
}
```

So during ordinary PLAYING — no capture, no post-overdub drain window — **the entire loop remainder is one uninterrupted block**: `runLoopPrefixAfterBar` + `SC_CAPTURE_FLUSH` + `runDeferredIdleMaintenance` (× 8 tracks) + `runDeferredLoadAndDisplayFrame` + memory-pressure reclaim + `processDeferredSaveState`. That block *is* `midi_gap`.

Adding a sixth poll is explicitly forbidden by the contract until the Owner-Boundary Gate exits, and correctly so: [`122003`](../../captures/session_20260812_122003.log) already proved that adding work between polls loses clock, and inserting polls into an unbounded owner hides the bound problem rather than solving it.

---

## 3. Measured decomposition

### 3.1 Steady state — [`084213`](../../captures/session_20260821_084213.log)

Consecutive 5 s telemetry windows during PLAYING / OVERDUBBING:

| Window | `midi_gap` max | `idle_maint` max | `load_frame` max | `persist_save` max |
|---|---|---|---|---|
| `100950183` | 30.185 ms | 5.333 ms | 25.400 ms | 1.701 ms |
| `105953216` | 60.017 ms | 4.530 ms | 21.298 ms | 59.953 ms |
| `110973877` | 29.030 ms | 3.272 ms | 21.624 ms | 0.300 ms |
| `115985439` | 47.810 ms | 21.439 ms | 31.621 ms | 14.433 ms |
| `120991030` | 43.532 ms | 20.313 ms | 39.596 ms | 20.653 ms |
| `125994039` | 37.399 ms | 3.826 ms | 32.222 ms | 1.078 ms |
| `130995358` | 60.238 ms | 21.074 ms | 31.606 ms | 60.173 ms |
| `136004136` | 35.550 ms | 3.381 ms | 32.079 ms | 0.274 ms |

Two facts follow directly.

1. **`load_frame` is the floor, not the outlier.** It is 21–40 ms in *every* window, with `overCount` (samples over the 5 000 µs observational ceiling) of 122–150 per window. At `LCD::DISPLAY_UPDATE_INTERVAL = 30 ms` there are ~166 paint opportunities per 5 s window, so essentially **every display frame exceeds 5 ms and the worst frame exceeds 20 ms**.
2. **`persist_save` spikes to ~60 ms** despite `Config::maxPersistenceMicrosActive = 300`. A 300 µs slice budget checked *between* FSM steps cannot bound a 60 ms indivisible step.

Display resolve alone, in the same captures (samples after t = 60 s). Note the exact semantics: `DIAG,timing_max,DisplayResolveLiveCapture` is emitted from `DisplayNoteResolveLiveCapture.cpp` **only when a resolve exceeds `kDisplayResolveBudgetMicros`**, rate-limited to one line per 5 s, carrying the peak over-budget resolve since the previous line. It is therefore a distribution of *worst over-budget resolves*, not a mean over all frames.

| Capture | peak over-budget resolve, mean of samples | max | n |
|---|---|---|---|
| [`084213`](../../captures/session_20260821_084213.log) | 14.837 ms | 21.560 ms | 12 |
| [`085458`](../../captures/session_20260821_085458.log) | 17.116 ms | 23.963 ms | 8 |

**One phase of one display frame already consumes 60–96 % of a 25 ms interval whenever it goes over budget, and it goes over budget often enough to emit on most 5 s windows.**

### 3.2 Outliers — [`085458`](../../captures/session_20260821_085458.log)

| `midi_gap` | Aligned owner |
|---|---|
| 2 893.664 ms | `idle_maint` 2 864.114 ms |
| 2 865.744 ms | `idle_maint` 2 864.092 ms |
| 842.661 ms | `idle_maint` 840.200 ms, preceded by `DIAG,lcr,vch,gather_edit,us=813088` |

`loop_rem` one-shot counts (threshold 50 ms) in the same run: `idle_maint` 66, `idle_gather` 58, `load_frame` 32, `idle_append` 11, `first_commit` 4, `boot_commit` 4, `persist_save` 2.

The regression versus [`084213`](../../captures/session_20260821_084213.log) (active max 79.941 ms, `idle_maint` one-shots 2) is entirely in the visual-cache fallback path. That path is where the 500–2900 ms class lives.

---

## 4. Structural cause classes

Six distinct mechanisms produce the observed intervals. They need different fixes; a scheduler cannot merge them.

### C1 — The display frame is one non-preemptible 20–40 ms unit

`DisplayManager::update()` runs clear-buffer → `resolveDisplayNotes` → five draw calls → `_display.api.display()` with no yield point:

```133:153:src/DisplayManager.cpp
    _display.gfx.fill_buffer(_display.api.getFrameBuffer(), 0);
    ...
    drawTrackStatus(trackManager.getSelectedTrackIndex(), now);
    const DisplayNoteVec& frameNotes = resolveDisplayNotes(selTrack, displaySlot, displayTick);
    ...
    drawPianoRoll(displayTick, selTrack, displaySlot, frameNotes);
    drawSidebar(selTrack, displaySlot);
    drawInfoArea(displayTick, selTrack, displaySlot, now);
    drawNoteInfo(displayTick, selTrack, displaySlot, frameNotes);

   _display.api.display();
```

`Diagnostics::kDisplayResolveBudgetMicros = 5000` is a **measurement**, not a bound — over-budget resolve still completes.

The panel transfer is also fully synchronous and byte-at-a-time. `FRAMEBUFFER_SIZE = DISPLAY_WIDTH * DISPLAY_HEIGHT / 2 = 8192` bytes, and `SSD1322_API::display()` deliberately bypasses DMA:

```380:391:lib/SSD1322_OLED/src/SSD1322_API.cpp
void SSD1322_API::display() {
	SSD1322_API_set_window(0, 63, 0, 63);
#ifdef __IMXRT1062__
	blockingResyncPending_ = false;
	static bool loggedSpiDisplay = false;
	if (!loggedSpiDisplay) {
		loggedSpiDisplay = true;
		Serial.println("SSD1322_API: SPI display (DMA bypass)");
	}
#endif
	SSD1322_API_send_buffer(framebuffer, FRAMEBUFFER_SIZE);
}
```

`SSD1322_HW_DRIVER::SSD1322_HW_SPI_send_array` loops `SPI.transfer()` **per byte** at 20 MHz. Wire time alone is `8192 × 8 / 20e6 = 3.28 ms`; per-byte call overhead adds to that. The existing DMA path (`SSD1322_API_send_buffer_DMA`) is not an escape either — it busy-waits for queue drain with CS held low and no yield, by design, after [`oled_dma_partial_frame_bugfix`](oled_dma_partial_frame_bugfix.md).

### C2 — Budgeted gather with unbudgeted tail phases

`Loop::advanceFallbackGatherSession` resumes traversal across turns on an event budget, but the moment traversal completes it performs sort **and** the full edit-row projection in that same turn:

```475:521:src/Loop/LoopVisualCache.cpp
    if (!fallbackGatherSessionTraversalComplete_) {
      out.clear();
      return false;
    }
    ...
    sortSessionEventsByTick(fallbackGatherSessionEvents_);
    ...
      if (!activeRows.empty()) {
        ...
        applyNoteEditPassSequence(fallbackGatherSessionEvents_, activeRows, loopLengthTicks);
```

`DIAG,lcr,vch,gather_edit` measured **1 083 993 µs** in [`015645`](../../captures/session_20260821_015645.log), **826 242 µs** in [`082858`](../../captures/session_20260821_082858.log) after the Phase A collapse planner, and **813 088 µs** in [`085458`](../../captures/session_20260821_085458.log). `reconstructDisplayNotes` then runs unbudgeted in the caller. The event budget bounds one of three phases.

### C3 — Full-loop, per-pass work in the wrap-append owner

`Loop::appendOverdubPassWrapPairedNotes` ignores the slice window entirely. For **every** active overdub pass it loads **all** of that pass's events, applies **all** active edit rows, reconstructs, and then does a linear geometry-presence scan per produced note:

```539:555:src/Loop/LoopVisualCache.cpp
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state != CapturePassState::Active || pass.committedChunkIds.empty()) {
      continue;
    }
    SessionMidiEventVec overdubEvents;
    LoopEventStore::appendChunkRefEvents(pass.committedChunkIds, overdubEvents);
    if (!activeEdits.empty()) {
      applyNoteEditPassSequence(overdubEvents, activeEdits, loopLengthTicks);
    }
    const NoteUtils::DisplayNoteVec overdubNotes = NoteUtils::reconstructDisplayNotes(
        overdubEvents, loopLengthTicks, false, false, true);
    for (const NoteUtils::DisplayNote& note : overdubNotes) {
      if (!displayNoteGeometryPresent(notes, note)) {
        notes.push_back(note);
      }
    }
  }
```

This is `O(passes × loop_events)` plus `O(produced × cached)` with no cap. It is the `idle_append` 797–949 ms class. The current mitigation only *moves* it: `allowOverdubWrapAppend=false` during active states sets `deferredVisualCacheWrapAppendPending_`, and `applyDeferredVisualCacheWrapAppend` then runs the identical unbounded work in STOPPED idle. Deferral changes *when*, not *how much*.

### C4 — Playback-window rebuild on the clock path

`Track::ensurePlaybackWindowBuilt` performs a merged-playback rebuild that is reachable from `updateAllTracks`, i.e. from inside `handleMidiInput()` clock dispatch. `PlaybackBuildTime` reaches 262 ms in [`084213`](../../captures/session_20260821_084213.log), and [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) records a `DIAG,playback_build=85.417 ms` aligned with a 564 ms gap. This is the mechanism behind the `tracks` = 438 ms window in §1.

### C5 — Budget multiplication across 8 tracks

`Config::NUM_TRACKS = 8`. When any track is PLAYING / RECORDING / OVERDUBBING / STOPPED_RECORDING, `runDeferredIdleMaintenance` calls `Track::processDeferredIdleMaintenance` for **all** tracks in one turn; only the all-STOPPED case is round-robin one-per-turn:

```319:335:src/main.cpp
  if (allTracksStopped) {
    // STOPPED catch-up can run longer; process one track per turn and resume next turn.
    trackManager.getTrack(sStoppedIdleTrackCursor)
        .processDeferredIdleMaintenance(now, postOverdubPlayingMidiDrain);
    ...
    return;
  }
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    trackManager.getTrack(i).processDeferredIdleMaintenance(now, postOverdubPlayingMidiDrain);
  }
```

Every per-track constant is therefore an 8× constant in the active case. The post-overdub drain window already restricts visual slices to the owner track, which is the right shape — but only inside that window.

### C6 — Indivisible persistence and load steps

`PersistenceBudget` and `LoadLoopBudget` check elapsed µs **between** steps. A step larger than the budget overruns it silently. `persist_save` at 59.953 / 60.173 ms against a 300 µs active budget is direct proof. Same structure for `LoadLoopBudget::FocusRestoreUs = 1200` and the `Committing` phase.

---

## 5. Owner bound inventory (Stage R1C)

Classification per contract §3.3: **structural** = code enforces a maximum; **empirical** = measured only; **unbounded** = content-dependent with no cap.

| # | Owner | Where | Measured max | Current bound | Class |
|---|---|---|---|---|---|
| 1 | `TrackManager::updateAllTracks` from clock dispatch | `ClockManager::onMidiClockPulse` | 438.5 ms (`tracks`) | none | **unbounded** |
| 2 | `Track::ensurePlaybackWindowBuilt` | `TrackPlaybackWindowBuild.cpp` | 262.7 ms | none | **unbounded** |
| 3 | `DisplayManager::update` — resolve phase | `resolveDisplayNotes` | 24.0 ms | 5 000 µs *measurement* only | **unbounded** |
| 4 | `DisplayManager::update` — draw + transfer | `_display.api.display()` | ≥ 3.28 ms wire | fixed frame size | **structural** (size), **blocking** |
| 5 | `Loop::advanceFallbackGatherSession` traversal | `LoopVisualCache.cpp` | budgeted | 96 / 192 / 768 events | **structural** |
| 6 | …its sort phase | `sortSessionEventsByTick` | in `gather_edit` window | none | **unbounded** |
| 7 | …its edit-projection phase | `applyNoteEditPassSequence` | 1 083.9 ms | none | **unbounded** |
| 8 | `NoteUtils::reconstructDisplayNotes` in slice | `rebuildVisualCacheIdleSlice` | within `idle_maint` | none | **unbounded** |
| 9 | `Loop::appendOverdubPassWrapPairedNotes` | `LoopVisualCache.cpp` | 949.5 ms | none | **unbounded** |
| 10 | `removeDisplayNotesOverlappingBars` | `rebuildVisualCacheIdleSlice` | not isolated | none | **unbounded** O(V) |
| 11 | `Loop::materializeEditViewFromPasses` | STOPPED idle | 2.2 s class (historical `begin_capture`) | none | **unbounded** |
| 12 | `Loop::prewarmOverdubSourceSpanCache` | STOPPED idle | full materialize | gated, not bounded | **unbounded** |
| 13 | `validateAndCleanupMidiEvents` | deferred, 60 s policy | full merge | none | **unbounded** |
| 14 | `StorageManager::processDeferredSaveState` | `DeferredSaveScheduler` | 60.2 ms | 300 µs / 3 000 µs between steps | **empirical** |
| 15 | `DeferredJobScheduler::runFrame` | `LoadLoopJob` | `boot_commit`/`first_commit` one-shots | 250 / 1 200 / 5 000 µs between steps | **empirical** |
| 16 | `enqueueRemainingLoopSlotRestores` | boot fill | admits 4 entries/turn | 4 entries | **structural** |
| 17 | `reclaimUnreferencedDisabledPasses` | `loop()` at Critical pressure | not isolated | none | **unbounded**, not resumable |
| 18 | `processHitlSerialCommands` | capture builds only | drains available | none | **unbounded** |
| 19 | `processDeferredIdleMaintenance` × 8 | `main.cpp` | 2 864.1 ms | per-track only | **unbounded in aggregate** |
| 20 | `MidiButtonProcessor::processPendingPresses` | inside `handleMidiInput` | transitions run here | none | **unbounded** |

Nine of the twenty are structurally or empirically bounded. Eleven are not. **The eleven unbounded owners are the reason no scheduling policy has held the interval yet** — this is exactly the Owner-Boundary Gate precondition the contract states in §9: *"No operation may be reserved solely because its caller has remaining budget."*

---

## 6. Is 25–30 ms reachable? Floor derivation

Irreducible cost that must fit inside one interval, from measurement:

| Item | Cost | Reducible? |
|---|---|---|
| OLED panel transfer, 8192 B @ 20 MHz | 3.28 ms wire | Not below this at 20 MHz and full-frame. Removable from the *interval* only if the transfer is asynchronous and MIDI is serviced during it. |
| `handleMidiInput()` with no clock pulse due | 0.086–0.094 ms | already small |
| Clock dispatch that only emits due events | not isolated today | requires C4 fix to measure |
| Display resolve | 14.8–24.0 ms today | must become resumable or cache-served; this is the decisive term |
| Draw calls (piano roll + sidebar + info + note info) | not isolated today | **measurement gap — must be instrumented before a ceiling is chosen** |

Two conclusions, both firm:

1. **25–30 ms is not reachable while `DisplayManager::update()` remains one unit.** Resolve alone is 14.8–24.0 ms, transfer adds ≥ 3.28 ms blocking, draw is unmeasured. A single frame plausibly consumes the entire target interval, leaving nothing for idle maintenance, persistence, or load.
2. **25–30 ms is reachable in principle**, because nothing in the irreducible column is inherently large: the frame transfer is 3.28 ms of wire time on a peripheral that supports DMA, and every other large term is content-dependent work that has an incremental formulation.

A candidate allocation, offered as the shape of a Stage C contract rather than as an authorized budget:

| Class | Allocation | Requires |
|---|---|---|
| `handleMidiInput()` incl. clock dispatch | 5 ms | C4 closed (owner #1, #2) |
| one display phase (resolve **or** draw **or** queue-transfer) | 8 ms | C1 split into phases (owner #3, #4) |
| one visual-maintenance unit, one track | 5 ms | C2 + C3 + C5 closed (owners #6–#10, #19) |
| one persistence or load step | 3 ms | C6 step decomposition (owners #14, #15) |
| headroom / ISR overlap | 4 ms | contract §12 G4 |
| **total** | **25 ms** | |

The allocation only becomes a contract once each row's owner is structurally bounded. Assigning the budget first would repeat the rejected `RuntimeWorkBudget::admit(WorkClass)` mistake.

---

## 7. Ordered leverage

Ordered by measured contribution to the interval, not by ease. Each row is an Owner-Boundary Gate item; none of them is interval reservation.

### L1 — Split `DisplayManager::update()` into phases with explicit state

**Owner:** `DisplayManager`. **Invariant:** one `loop()` turn advances at most one display phase; a frame spans turns and the panel is never shown a partially transferred frame.

Highest leverage because it is the only owner present in *every* interval (21–40 ms, ~166 opportunities per 5 s). Natural phase split follows the existing code structure: resolve → draw-into-buffer → transfer. `resolveDisplayNotes` already has a measured bailout point (`kDisplayResolveBudgetMicros`) that can become a real yield.

This crosses design gate **G5 (display freshness)** in the roadmap: stale-frame vs resumable-resolve vs always-complete is a product decision, not an implementation choice. **Stop and decide with the user before firmware.**

### L2 — Make the panel transfer asynchronous

**Owner:** `SSD1322_API`. **Invariant:** the framebuffer is not mutated while a transfer is in flight; the panel never shows a mixed frame.

Removes ≥ 3.28 ms of blocking wire time plus per-byte `SPI.transfer()` overhead from the interval. Two independent sub-problems exist and both are already documented: the byte-at-a-time loop in `SSD1322_HW_SPI_send_array`, and the `waitForOledDmaQueueDrain(1000, false)` busy-wait that the partial-frame bugfix deliberately introduced. Reopening DMA requires re-establishing the CS/GRAM integrity guarantee that [`oled_dma_partial_frame_bugfix.md`](oled_dma_partial_frame_bugfix.md) closed — do not simply re-enable it.

### L3 — Make the visual-cache slice tail resumable

**Owner:** `Loop::advanceFallbackGatherSession` / `rebuildVisualCacheIdleSlice`. **Invariant:** every phase of a fallback slice is budgeted and resumable, not only traversal.

Removes the 500–1080 ms `gather_edit` class. The existing `WindowTraversalCursor` in `CommittedEventRange` is the pattern to extend — sort, edit projection, and reconstruct each need a cursor with the same session-key invalidation semantics already implemented in `fallbackGatherSessionMatches`. Phase A/B work in `applyNoteEditPassSequence` reduced the constant (1 084 ms → 826 ms) but did not change the class; only a cursor does.

Note the known hazard: the in-flight session-window hold attempted during this slice was reverted after startup instability, and [`015645`](../../captures/session_20260821_015645.log) showed **122** consecutive `gather_progress,ev=192,total=192,done=0,resume=0` lines — a restart-churn failure mode where budgeting without a stable session key makes throughput zero. Any cursor work must prove forward progress, not just bounded slices.

### L4 — Window the wrap-append owner

**Owner:** `Loop::appendOverdubPassWrapPairedNotes`. **Invariant:** wrap-pair append cost is proportional to the wrap window and the slice, not to loop length × pass count.

Removes the 797–949 ms `idle_append` class at its source instead of deferring it into STOPPED. Two sub-items: restrict the per-pass event load to the wrap window (the pass already carries `committedChunkIds` with tick spans usable for filtering, as `CommittedEventRange::appendWindowBudgeted` demonstrates), and replace the `displayNoteGeometryPresent` linear scan with the sorted-index membership pattern already used by `mergeDisplayNotesIntoOverdubSourceView`.

### L5 — Take playback-window rebuild off the clock path

**Owner:** `Track::ensurePlaybackWindowBuilt` / `TrackManager::updateAllTracks`. **Invariant:** clock dispatch cost is proportional to events due at that tick.

Removes the 262–438 ms in-service class — the term that makes the interval unbounded regardless of remainder policy. This is the same shape as R1A (`establishOverdubSourceView` off the `begin_capture` path) and shares its design question: what playback does while a window rebuild is incomplete. **State-transition design session required** (architecture checkpoint answers *yes* to transitions), and it overlaps [`playback_gather_lcr_consume_enhancement.md`](playback_gather_lcr_consume_enhancement.md).

### L6 — Make idle maintenance a per-turn single unit

**Owner:** `main.cpp::runDeferredIdleMaintenance`. **Invariant:** one turn advances maintenance for one track.

Removes the 8× multiplier in active states by generalising the two mechanisms that already exist in this function: the all-STOPPED round-robin cursor (`sStoppedIdleTrackCursor`) and the post-overdub drain-window owner-track restriction. This is the cheapest item on the list and the only one that is close to behavior-preserving — but it changes maintenance throughput, so it needs the same debt-classification telemetry (`DIAG,odub_debt`) already in place.

### L7 — Decompose oversized persistence and load steps

**Owner:** `StorageManager` FSM steps, `LoadLoopJob` phases. **Invariant:** no single FSM step exceeds its own slice budget.

Removes the 60 ms `persist_save` spikes. Reuse `PersistenceBudget` / `LoadLoopBudget` — the budgets are correct, the steps are too coarse. Identify which step overruns before touching anything; the current telemetry brackets the whole call, not the step.

---

## 8. What this document does not propose

- **No interval reservation.** Stage A remains unauthorized; it requires the Owner-Boundary Gate to exit, plus OpenSpec, `ARCHITECTURE-REVIEW.md`, and a `DEC-###`.
- **No additional `handleMidiInput()` call sites.** Explicitly forbidden by the contract until Stage R exits, and [`122003`](../../captures/session_20260812_122003.log) is the evidence for why.
- **No new scheduler, Manager, or parallel FSM.** L1–L7 extend existing owners.
- **No move of MIDI clock into an ISR**, and no change to the clock/transport semantics.
- **No ownership transfers.** L5 touches `Track` playback-window state and L1 touches `DisplayManager` frame state; both stay with their current owners.
- **No masking by slice caps.** Per [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) P3, if an owner cause is found, fix the cause first.

---

## 9. Mapping to the existing ladder

| Ladder stage | This document's contribution |
|---|---|
| **O** Observation integrity | Identifies one measurement gap: draw-phase cost inside `DisplayManager::update` is not isolated. `DisplayUpdateTotalTime` / `DisplayBuildTime` did not emit in steady state in either capture. |
| **R1A** source-view transition | L5 is the same class of problem on the clock path; the two share a design question. |
| **R1C** owner inventory | §5 is the inventory: 20 owners, 11 unbounded. |
| **C** contract | §6 supplies the floor derivation and a candidate allocation shape. Not a ceiling until L1–L7 land. |
| **A** interval reservation | Still not authorized. |

New design gates this research adds to the roadmap's list:

- **G9 — display phase split.** Where a frame may yield, and what the panel shows across a phase boundary. Extends G5.
- **G10 — OLED transfer asynchrony.** Re-opening DMA must preserve the partial-frame guarantee from [`oled_dma_partial_frame_bugfix.md`](oled_dma_partial_frame_bugfix.md).
- **G11 — maintenance fairness.** Per-turn single-track maintenance changes debt-drain rate; starvation policy must be stated before the change.

---

## 10. Required next measurements

Before any L-item becomes firmware, three numbers are missing and cheap to obtain:

1. **Draw-phase cost** — instrument `drawPianoRoll` / `drawSidebar` / `drawInfoArea` / `drawNoteInfo` and `_display.api.display()` separately, gated per [Instrumentation-Session-Capture-Gating](../../.cursor/rules/Instrumentation-Session-Capture-Gating.mdc). Without this the L1 phase split has no target.
2. **Committed-path resolve cost in PLAYING** — `DisplayResolveLiveCapture` only covers the live-capture path. The 21–40 ms `load_frame` in non-capture PLAYING is currently unattributed.
3. **The persistence work-item kind and phase at the 60 ms spike.** Already partly attributed: `PersistenceDiagnostics::onSliceCompleted` is called only from the mid-pass and work-item writer branches, and `PERS,diag` in [`084213`](../../captures/session_20260821_084213.log) reports `peakWriterLatencyUs` of **59 885** then **69 284** — matching the `persist_save` spikes of 59.953 / 60.173 ms. With 5 546 `PERS,work` lines for `LoopPersist loop:4` versus 3 `mid_pass`, the overrunning step is a `stepPersistenceWorkItem` slice. What is missing is the **kind + phase inside that step**, because `peakWriterLatencyUs` is a cumulative never-reset peak with no step identity. Revision-commit and revision-load steps are not measured by `onSliceCompleted` at all.

Verification for any subsequent firmware slice is unchanged: `pio test -e native`, `teensy41-capture-serial` build, and the HITL matrix in the roadmap Stage P, scored on both the interval restatement in §1 and the C15 lateness gate.

### Instrumentation shipped for these three measurements

Observation only — no scheduling, ownership, or transition change. Compiles out when `SESSION_CAPTURE` is undefined (the `micros()` locals are inside `#if RUNTIME_TIMING_ENABLED`, not only the macros).

| Measurement | Owner extended | Capture line |
|---|---|---|
| 1 — draw and transfer split | `DisplayManager::update` | `DIAG,disp_draw`, `DIAG,disp_xfer` |
| 1 — frame total for phase-sum reconciliation | `DisplayManager::update` | `DIAG,disp_total` |
| 2 — resolve, all paths | `DisplayManager::update` | `DIAG,disp_resolve` |
| 2 — committed projection only | `DisplayManager::resolveDisplayNotes` tail call | `DIAG,disp_res_cmt` |
| 3 — slice duration and sub-step | `emitPersistenceWorkTelemetry`, `stepPersistenceWorkItem` | `PERS,work` |

Display phases reuse the `RuntimeTimingTelemetry` window accumulators, so each tag reports `maxUs` and `overCount` against `kObservationalSoftCeilingUs` every 5 s alongside `load_frame`. `DIAG,disp_` is Tier-A in `CaptureLineTier::isTierALine`, so the lines survive ring pressure (invariant Q). The `drawPianoRoll` / `drawSidebar` / `drawInfoArea` / `drawNoteInfo` group is measured as one `disp_draw` span together with `fill_buffer` and `drawTrackStatus`; if `disp_draw` turns out to dominate, splitting it further is a follow-up, not a prerequisite for L1.

`PERS,work` previously emitted a hardcoded `0` duration, which is why 5 546 identically shaped lines could not be matched to the 60 ms spike. It now carries the elapsed microseconds of the `stepPersistenceWorkItem` slice that produced it. The three `LoopPersist` emit sites that all reported phase `slice` are now `payload_slice`, `payload_done`, and `finalize_slice`, and `LoopPersist` lines append `write=<DeferredLoopWriteStage>` and `finalize=<DeferredLoopFinalizeStage>`. Host parsing is unaffected: `_PERS_WORK_RE` in `scripts/hitl/persistence_rows.py` reads type, key, phase, and outcome positionally and ignores trailing fields, and `PERSISTENCE_HEAP_STAGES` never included `work`.

**Cost:** RAM1 local headroom 4768 → 4640 bytes; RAM1 code unchanged at 425 276. Native `pio test -e native` **1405/1405** including `test_display_phase_spans_accumulate_independently`.

### Measured results — [`094312`](../../captures/session_20260821_094312.log)

32 telemetry windows, 27 967 lines. All five display tags and the `PERS,work` durations emitted.

#### Display phases

| Tag | Min | Steady max | Worst | `overCount` |
|---|---|---|---|---|
| `disp_xfer` | **4.45 ms** | **4.45 ms** | 4.45 ms | **0** |
| `disp_draw` | 5.01 ms | 10.36 ms | 10.36 ms | 3110 |
| `disp_resolve` | 0.06 ms | 20.18 ms | **30 669.9 ms** | 178 |
| `disp_res_cmt` | 0.05 ms | 11.52 ms | **30 669.9 ms** | 22 |
| `disp_total` | 11.94 ms | 34.55 ms | 30 689.5 ms | 3790 |

Three conclusions, each now measured rather than derived:

1. **The panel transfer is a hard constant.** `disp_xfer` is 4.45 ms in all 31 frames-present windows with zero variance and never crosses the 5 ms soft ceiling. The blocking byte-at-a-time SPI push is not content-dependent.
2. **The draw group is structurally bounded and does not need splitting.** `disp_draw` never exceeds 10.36 ms across the whole capture, including the 30 s window. Measurement 1 therefore closes with a negative result: splitting `drawPianoRoll` / `drawSidebar` / `drawInfoArea` / `drawNoteInfo` into separate tags is **not** a prerequisite for L1.
3. **Resolve is the only unbounded display phase, and the committed path owns the worst case.** This closes measurement 2. `disp_res_cmt` reaches `disp_resolve` exactly in the windows without live capture, so the plain-PLAYING committed projection — not the live-capture path that `DisplayResolveLiveCapture` already covered — is what the 21–40 ms `load_frame` was hiding.

**Revised floor for §6.** `disp_draw + disp_xfer` is **9.5–14.8 ms** of indivisible per-frame cost. Against the 25–30 ms interval restatement in §1 that is **32–59 % of the entire budget for one OLED frame**, before any resolve work. This is now a measured floor.

#### The 30-second event is one committed display resolve

The largest gap in the capture is fully attributed by a single chain:

```text
DIAG,loop_rem,boot_commit,30931754      ← scheduling site (child 3, main.cpp)
DIAG,load_frame,30946447,44             ← owner span
DIAG,disp_total,30689524,1              ← DisplayManager::update
DIAG,disp_resolve,30669888,1            ← resolve phase = 99.94 % of the frame
DIAG,disp_res_cmt,30669883,1            ← resolveDisplayNotesCommitted
DIAG,disp_draw,8783,1                   ← bounded even here
DIAG,disp_xfer,4447,0                   ← constant even here
```

`boot_commit` previously named only the *scheduling site*. The owner is `resolveDisplayNotesCommitted` on a 68-bar loop (`DISP,4,STOPPED,52224`). The unbudgeted `advanceFallbackGatherSession` tail is visible directly in the same capture: `DIAG,lcr,vch,gather_edit` reports **684 472**, **657 773**, **344 657**, **218 114**, and **210 559 µs** with `rows=1683` each time, against `gather_sort` of 2.3–5.4 ms. The edit-projection tail — not the gather or the sort — is the unbounded owner, confirming cause class §4 with numbers.

#### Persistence sub-steps

Every `persist_save` outlier now maps to a named sub-step, closing measurement 3:

| `persist_save` window | Attributed step | Slice |
|---|---|---|
| 32.9 ms @ t=139 | `LoopPersist payload_slice`, `write=CapturePassHeader` | 32 430 µs |
| 20.4 ms @ t=144 | `LoopPersist payload_done` | 19 566 µs |
| 56.8 ms @ t=154 | `LoopPersist done` | 56 544 µs |
| — | `FinalizeWorkspace finalize_slice` (6 slices, mean 7 934 µs) | 28 003 µs |

The budgeted slices behave: `finalize_slice` ran 751 times at a mean of 110 µs, `payload_slice` 293 times at a mean of 362 µs. The overruns are all **terminating** steps — `done`, `payload_done`, and workspace `finalize_slice` — which are indivisible under the current writer. This is the "large indivisible step" class the pre-instrumentation reading predicted, now with names and magnitudes instead of a cumulative `peakWriterLatencyUs`.

#### Open residual (new, not previously visible)

In the tail windows (t=149–194) `load_frame` is 19.6–20.8 ms while `disp_total` is 11.9–12.8 ms, leaving **7.6–8.0 ms** inside `runDeferredLoadAndDisplayFrame` but outside `DisplayManager::update`. At t=68–78 the same residual is 0.3 ms. The `loop_rem` child one-shot only fires above `kLoopRemainderOneShotUs` (50 ms), so no child line names it at this magnitude. This residual is unattributed and is a candidate for the next measurement pass; do not assume a child until it is measured.
