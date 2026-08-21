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

---

## 11. `load_frame` child attribution (Stage O / R1C)

The residual above is measured only where the transport-idle block runs, and three of that block's four calls were absent from the §5 inventory entirely. Instrumented, same observation-only class as §10:

| Tag | Owner |
|---|---|
| `lframe_job` | `DeferredJobScheduler::runFrame` (budgeted by `LoadLoopBudget`) |
| `lframe_undo` | `StorageManager::processDeferredUndoSnapshots` |
| `lframe_autosave` | `StorageManager::processEditAutosave` |
| `lframe_reclaim` | `TrackManager::reclaimUnreferencedDisabledPasses` |
| `lframe_enqueue` | `StorageManager::enqueueRemainingLoopSlotRestoresFromSd` |

The first four run only under `allowDeferredSlotRestore && !timingCriticalTrackActive && !SlotLoadSession::isActive()`, which is the same condition that separates the 7.6–8.0 ms tail residual from the 0.3 ms active-transport residual. `DIAG,lframe_` is Tier-A. Window accumulators rather than the child one-shot, because the one-shot cannot fire below 50 ms.

**§5 inventory correction:** `processDeferredUndoSnapshots`, `processEditAutosave`, and `enqueueRemainingLoopSlotRestoresFromSd` were missing from the 20-owner inventory. They run inside the `load_frame` span on every transport-idle turn and their bound is not yet established — treat the inventory as **23 owners** pending the next capture, and do not classify these three until measured.

### RAM1 constraint: emit tags must be `PSTR`

Adding these five tags first cost **4096 bytes** of RAM1 stack headroom (4640 → **544 free**) for 40 bytes of accumulator state. The linker map shows why: `.bss` carries `.dmabuffers` input sections with two `*fill*` runs of 3392 bytes, and ordinary string literals become `.rodata`, which the Teensy 4 linker places in DTCM on the `_VectorsRam` 1 KB align edge. Ten added tag literals crossed that edge.

`recordMidiLedHelperRem` already documents this and already uses `PSTR` for exactly this reason. Routing the tags in `emitWindow` through a `RT_TAG` macro (`PSTR` on device, plain literal for host tests) returned RAM1 to **4640 free** — the §10 display tags included, so the net RAM1 cost of both telemetry slices is now **zero**.

**Rule for any further telemetry:** new emit tags go through `RT_TAG`. A plain literal is not a free addition here, and the RAM1 figure is alignment-sensitive, so a 40-byte change can swing headroom by 4 KB in either direction. Measure `pio run -e teensy41-capture-serial` RAM1 before and after.

Native **1406/1406**.

### Measured results — [`100726`](../../captures/session_20260821_100726.log)

18 windows. `load_frame` now reconciles against `disp_total` plus the five children to within **0.0–0.3 ms** in every window where the transport-idle block ran. (Windows t=42 and t=47 reconcile to a small negative because each tag reports a per-window maximum from a possibly different frame; maxima are not additive within a window. This is a property of the accumulator, not a missing child.)

| Tag | Owner | Max | Over 5 ms | Classification |
|---|---|---|---|---|
| `lframe_reclaim` | `reclaimUnreferencedDisabledPasses` | **8.2 ms** | **513** | **unbounded — the residual owner** |
| `lframe_job` | `DeferredJobScheduler::runFrame` | **25.7 ms** | 237 | budgeted but **overruns its budget** |
| `lframe_undo` | `processDeferredUndoSnapshots` | **52.1 ms** | 7 | **unbounded**, rare |
| `lframe_autosave` | `processEditAutosave` | 0.3 ms | 0 | empirically bounded, negligible |
| `lframe_enqueue` | `enqueueRemainingLoopSlotRestoresFromSd` | 0.0 ms | 0 | negligible |

**The 7.6–8.0 ms residual is `TrackManager::reclaimUnreferencedDisabledPasses`.** At t=118 `load_frame` 34.8 ms minus `disp_total` 26.3 ms is 8.5 ms against `lframe_reclaim` 8.2 ms; at t=52 the 7.0 ms gap is 6.8 ms reclaim plus 0.2 ms autosave. It is present at 6.4–8.2 ms in every transport-idle turn and crosses the soft ceiling 513 times — the most *frequent* over-ceiling owner found so far, as opposed to the largest.

Two owners neither the inventory nor §10 predicted:

- **`processDeferredUndoSnapshots` peaks at 52.1 ms** — comparable to the worst persistence step (56.5 ms) and on a path with no budget at all.
- **`DeferredJobScheduler::runFrame` peaks at 25.7 ms** despite being budgeted by `LoadLoopBudget`. The budget exists and is exceeded, which is the same "budget correct, steps too coarse" shape as L7 rather than a missing budget.

This capture also contains a second multi-second committed resolve: `load_frame` 22 659.8 ms with `disp_total` 22 406.5 ms at t=34, confirming the §10 finding is reproducible and not specific to one boot.

**Revised §5 classification.** Of the three owners missing from the inventory, one is negligible (`processEditAutosave`), one is trivial (`enqueueRemainingLoopSlotRestoresFromSd`), and one is unbounded (`processDeferredUndoSnapshots`). Adding `reclaimUnreferencedDisabledPasses` and `DeferredJobScheduler::runFrame` as separately measured owners, the count of unbounded or over-budget owners rises from 11 to **14**.

**Stage O is now closed for the `load_frame` span.** Every child is named and measured; nothing above 0.3 ms is unattributed.

## 12. The multi-second resolve is not the gather tail (Stage O)

§10 and §11 both measured a multi-second `disp_res_cmt` but neither established *which* call inside it runs. Before designing L1 or L3 around it, that had to be settled, because L3 targets the `advanceFallbackGatherSession` tail and the attribution was an assumption.

**It is not the gather tail.** In [`100726`](../../captures/session_20260821_100726.log) the 22.4 s span runs from 12.562 s to 34.959 s between two *consecutive* capture lines — it emits **zero** log lines. Every `advanceFallbackGatherSession` edit phase emits `DIAG,lcr,vch,gather_edit`; those lines appear at 10.2 s, 11.3 s, 36.1 s, 37.2 s, 70.6 s and 71.6 s (0.60–1.11 s each) and **none** falls inside the gap. The window's own split confirms the containment: `disp_total` 22 406.5 ms, `disp_resolve` 22 386.8 ms, `disp_res_cmt` 22 386.8 ms, against `disp_draw` 8.7 ms and `disp_xfer` 4.5 ms.

So the largest single gap in the system sits in a path that emits nothing. Reading `resolveDisplayNotesCommitted`, this is structural rather than accidental: two heavy paths carry only `DIAG_TIMING_RECORD(DisplayBuild, …)`, which surfaces solely through `emitArchitectureMetricsSnapshot` — established in §3 as never emitting in steady state. Both are therefore invisible in every capture taken so far:

1. `resolveWindowedDisplayNotes` → `rebuildDisplayNotesInWindow` — reached when `visualCache.notes` is empty, which is the cold-boot state. The 68-bar loop in this window (`loopLength` 52224, 1123 notes) exceeds `kMaxDetailedWindowBars`, so `syncDetailedPaintWindow` succeeds and this is the path the boot commit takes.
2. The unwindowed committed tail block in `resolveDisplayNotesCommitted` — reached when `syncDetailedPaintWindow` returns false.

**Structural defect found by reading, independent of timing.** `rebuildDisplayNotesInWindow` receives `windowStart`/`windowLength` and passes them to `gatherCommittedEventsInWindow`, but then calls `mutLoop.appendOverdubPassDisplayNotes(outNotes)` — which takes no window argument. The windowed resolver calls a **full-loop** append and only discards the out-of-window rows afterwards, in the `std::remove_if` that follows. This is the C3 / L4 owner family reached from the *windowed* path, meaning the window bound does not actually bound this phase. The same call appears in the unwindowed tail block.

### Instrumentation shipped for this measurement

Four sub-phases in `rebuildDisplayNotesInWindow` (`gather`, `recon`, `append`, `filter`) and three in the committed tail block (`gather`, `recon`, `tails`), each emitting one threshold-gated line at ≥ 10 ms with the event count, note count and window geometry:

- `DIAG,disp_win,us=…,gather=…,recon=…,append=…,filter=…,ev=…,notes=…,ws=…,wl=…`
- `DIAG,disp_full,us=…,gather=…,recon=…,tails=…,ev=…,notes=…,len=…`

Threshold-gating rather than accumulators is deliberate: the bounded `visualCache`-filter path in `resolveWindowedDisplayNotes` returns before reaching either site, so any crossing is already the anomaly, and no new `.bss` is introduced. Both tags match the existing `DIAG,disp_` Tier-A prefix, so `CaptureLineTier` needed no change and these lines cannot be dropped under ring pressure.

The `PSTR` rule from §11 applied again and was not optional: with plain format-string literals RAM1 headroom fell **4640 → 544 bytes**, the same alignment edge, restored to **4640** by routing both through `DISP_DIAG_FMT`. Net RAM1 cost of this slice is **zero**. Native **1406/1406**.

`append` is the phase to watch. If it dominates `disp_win`, the multi-second resolve is the non-windowed `appendOverdubPassDisplayNotes` call and L4 — not L1 or L3 — is the owner-boundary fix that removes the largest gap in the system.

### Measured results — [`102119`](../../captures/session_20260821_102119.log)

**`append` is not the owner. The windowed gather is.** One `disp_win` crossing in the whole capture, at boot commit:

```
DIAG,disp_win,us=26727786,gather=26368771,recon=44867,append=313376,filter=772,ev=2093,notes=1094,ws=0,wl=13824
```

| Phase | Owner | µs | Share |
|---|---|---|---|
| `gather` | `Loop::gatherCommittedEventsInWindow` | **26 368 771** | **98.66 %** |
| `append` | `appendOverdubPassDisplayNotes` | 313 376 | 1.17 % |
| `recon` | `NoteUtils::reconstructDisplayNotes` | 44 867 | 0.17 % |
| `filter` | window `remove_if` | 772 | 0.00 % |

`DIAG,disp_full` never crossed 10 ms, so the unwindowed committed tail block is **not** implicated — the cost is entirely on the windowed path. Corroborating window values: `loop_rem,boot_commit` 26 990 771 µs, `load_frame` 27 004 850 µs, `midi_gap` 27 011 815 µs, gap spanning 9.165 s → 35.897 s. Third consecutive boot to reproduce a 22–27 s resolve.

**Correction to §12.** The prediction that `append` would dominate was wrong. It is real, non-windowed, and worth 313 ms, but it is 1.2 % of the span. **L4 is not the fix for the largest gap in the system.** The owner is `Loop::gatherCommittedEventsInWindow` → `copyEffectiveCommittedEventsInRange` → `CommittedEventRange::appendWindowBudgeted`.

**What reading the code rules out.** Every step in that traversal is linear or O(1), so the span is not explained by algorithmic shape:

- `LoopEventStore::chunkTickSpan` and `appendChunkRefEvent` both index `pool_[id]` directly after a bounds and `poolUsed_` check — no chunk-id search, so the traversal is **not** quadratic in chunk count.
- `DisplayWindowUtils::filterMidiEventsToWindow` is a single pass over the chunk's own events with `reserve` up front — it does not scan outside its chunk.
- `chunkEvents` / `windowEvents` are hoisted above the loop, so `clear()` retains capacity and per-chunk reallocation does not recur.
- The `std::sort` in `copyEffectiveCommittedEventsInRange` covers 2093 events and cannot account for seconds.
- `includeActiveEditRows` defaults false on the display call, so `applyNoteEditPassSequence` — the §10 gather-tail owner — does not run here at all. This is a second, independent confirmation that the multi-second resolve is not the edit tail.

26.4 s across 2093 emitted events is 12.6 ms per emitted event. No linear reading of this code produces that, so the cause is not visible in the call graph and must be measured.

### Instrumentation shipped for the traversal decomposition

`CommittedEventRange::appendWindowBudgeted` now emits one threshold-gated line at ≥ 10 ms, at **every** exit (both budget-exhausted returns and the completion return) so budgeted callers cannot traverse silently:

```
DIAG,cer_win,us=…,span=…,copy=…,filter=…,lists=…,chunks=…,nospan=…,outside=…,copied=…,evcopy=…,evout=…
```

This separates the three per-chunk calls (`span` = `chunkTickSpan` plus the intersect test, `copy` = `appendChunkRefEvent`, `filter` = `filterMidiEventsToWindow`) and reports the traversal shape: lists collected, chunks visited, chunks rejected for missing span, chunks rejected as outside the window, chunks copied, events copied pre-filter, events emitted. Those counts decide between the remaining candidates — a chunk population far larger than the emitted event count, versus a per-event memory cost — without further guessing.

`DIAG,cer_` added to the `CaptureLineTier` Tier-A prefixes with coverage in `test_capture_line_tier`, alongside the `disp_win` / `disp_full` / `disp_res_cmt` / `lframe_reclaim` cases that had no test. Format strings route through `CER_DIAG_FMT`; RAM1 unchanged at **4640** bytes. Native **1406/1406**.

### Measured results — [`103826`](../../captures/session_20260821_103826.log)

**The traversal is not the owner either.** 29 `cer_win` lines, and the largest in the entire capture is **62 213 µs**. The boot resolve in the same capture:

```
DIAG,disp_win,us=34951148,gather=34568381,recon=44730,append=337210,filter=827,ev=2087,notes=1079,ws=0,wl=13824
```

`gather` is **34 568 381 µs**. The `cer_win` line before it is at 13.518 s and the next is at 48.715 s, so across the entire 34.6 s gather `appendWindowBudgeted` **never crossed the 10 ms threshold once**. The traversal is therefore not where the time goes.

Traversal shape in steady state, for the record: `lists` **105–106**, `chunks` 48–115, `nospan` **0** in every line, `outside` 29–97, `copied` 15–85, `evcopy` 778–5436, `evout` 507–4590, with `span` 1.1–3.7 ms, `copy` 1.7–7.9 ms, `filter` 0.9–3.3 ms. The three sub-phases account for the whole `us` in every line, so the traversal is internally consistent and bounded at tens of milliseconds. Its real problem is `lists=106` — 106 active committed passes walked per paint — not the per-chunk work.

**Where the 34.6 s must be.** `gather` calls exactly one of two entry points, and `cer_win` excludes the traversal inside both. The remaining unmeasured code is:

- `copyEffectiveCommittedEventsInRange` siblings — `collectActiveCommittedChunkLists` and the `std::sort` over the emitted events. Reading both: the collect is a linear pass over `passes.overdubPasses` plus a sort of 106 pointers, and the event sort covers 2087 entries. Neither is seconds.
- `gatherCommittedEventsInWindowWithCapture`'s capture tail, which `rebuildDisplayNotesInWindow` reaches because it passes `includeActiveCapture = true`. This path was never read in §12 and is **unwindowed**: `ensureCaptureEventsSorted()` then `capture.store.copyEventsTo(captureFlat)` copies the **entire** capture store before `filterMidiEventsToWindow` reduces it to the window.

**Correction to §12 and its measured results.** Two owner attributions have now been withdrawn — first `appendOverdubPassDisplayNotes` (1.2 %), then `CommittedEventRange::appendWindowBudgeted` (≤ 62 ms against 34.6 s). Both were narrowed by measurement rather than reasoning, and in both cases reading the call graph had suggested the wrong answer. The remaining candidate set is small enough that the next capture closes it.

### Instrumentation shipped for full `gather` attribution

- `DIAG,cer_range,us=…,collect=…,append=…,sort=…,lists=…,evout=…` — splits `copyEffectiveCommittedEventsInRange` into collect, traversal, and event sort.
- `DIAG,cer_cap,us=…,committed=…,sort=…,copy=…,filter=…,merge=…,capflat=…,capwin=…,evout=…` — splits the capture tail into the committed gather, `ensureCaptureEventsSorted`, `copyEventsTo`, window filter, and `std::merge`, emitted at **every** early return so a tail that exits before the merge still reports.
- `cap=0|1` added to `disp_win` to record which entry point `gather` took, removing the last ambiguity about which of the two paths ran.

Both new tags fall under the existing `DIAG,cer_` Tier-A prefix. `capflat` is the decisive count: it reports how many events `copyEventsTo` moved before the window filter, which is what distinguishes an unwindowed full-store copy from a bounded one. RAM1 unchanged at **4640** bytes. Native **1406/1406**.

Note this slice edits `LoopMaterialization.cpp`, which carries uncommitted in-progress work; the instrumentation is additive and `#if`-gated, but the file is shared with that refactor.

### Measured results — [`104301`](../../captures/session_20260821_104301.log)

```
DIAG,disp_win,us=38017312,gather=37624998,recon=44405,append=347068,filter=840,ev=2120,notes=1091,ws=0,wl=13824,cap=0
```

`cap=0` — the capture tail was **not** taken, so `gatherCommittedEventsInWindowWithCapture` is eliminated and `DIAG,cer_cap` is correctly absent. `gather` is **37 624 998 µs**. Yet only two `cer_range` lines exist, the larger being 83 904 µs (`collect` 168, `append` 62 694, `sort` 21 040, `lists` 112, `evout` 4734), and the largest `cer_win` is 62 362 µs.

**The owner is `applyNoteEditPassSequence`.** The `cer_range` emit was placed after the `std::sort` but *before* the `includeActiveEditRows` block, so it excluded the edit projection. The display path calls the three-argument `gatherCommittedEventsInWindow`, and the in-flight refactor defaults `includeActiveEditRows` to **`true`** — so every windowed committed gather on the paint path runs the full note-edit projection. With `collect`, `append`, `sort` and the capture tail all measured and bounded, the edit block is the only remaining statement in the call.

**Correction: §12's elimination of the gather tail was wrong.** §12 stated that `includeActiveEditRows` defaults false on the display call and used that as independent confirmation that the multi-second resolve was not the `advanceFallbackGatherSession` tail. The default is `true`. Both paths converge on the *same* owner: `applyNoteEditPassSequence`, which §10 measured as the unbudgeted `gather_edit` phase at 0.60–1.11 s. The original L3 attribution was right; three intermediate attributions (`appendOverdubPassDisplayNotes`, `appendWindowBudgeted`, the capture tail) were not.

**Why it reaches tens of seconds.** The display path passes a `SessionMidiEventVec`, so it enters the overload that copies the entire vector into a `MidiEventVec` scratch, applies the rows, and copies back. Inside, each planned row runs `applyNoteEditPassWithLookupCache` over the whole event vector, and `collectPresentNoteIds` / `containsPresentNoteId` add linear scans per row. Cost therefore grows with row count × event count, with vector mutation inside the inner apply. §10 measured 1.11 s at `rows` 1534 / `ev` 518; this window carries 2120 events, and `rows=` on the next capture pins the multiplier.

### Instrumentation correction shipped

`cer_range` now brackets the edit block and reports `edit=` and `rows=`:

```
DIAG,cer_range,us=…,collect=…,append=…,sort=…,edit=…,lists=…,rows=…,evout=…
```

Every microsecond of `gather` is now attributed on both entry points. RAM1 unchanged at **4640** bytes. Native **1406/1406**.

### Bound problems now confirmed on the paint path

1. **Unbudgeted edit projection per paint.** `applyNoteEditPassSequence` runs inside the windowed display gather because `includeActiveEditRows` defaults true, and it is bounded by neither the paint window nor an event budget.
2. **`lists` is 105–112.** Every windowed gather walks over a hundred active committed passes. This does not cause the multi-second span but is unbounded in pass count.
3. **The edit projection is not windowed.** It is applied to the gathered window output, but its per-row work scans the whole vector rather than the window.

### Confirmed — [`105032`](../../captures/session_20260821_105032.log)

```
DIAG,disp_win,us=38010808,gather=37613411,...,ev=2192,notes=1104,ws=0,wl=13824,cap=0
DIAG,cer_range,us=37613373,collect=169,append=61659,sort=21113,edit=37530431,lists=108,rows=1694,evout=2192
```

`edit` is **37 530 431 µs = 99.78 %** of the range call, and `cer_range` reconciles with `disp_win`'s `gather` to within **38 µs**. `applyNoteEditPassSequence` is the confirmed owner of the multi-second committed resolve. Every intermediate attribution is now closed out and the §10 / L3 owner stands.

**Scale.** 37.53 s over `rows` 1694 is **22.15 ms per row**; against the 2192-event window that is **10.1 µs per row-event pair**, roughly 6000 cycles — consistent with per-row vector mutation in `applyNoteEditPassWithLookupCache` plus the per-row `containsPresentNoteId` linear scan, executing on `SessionMidiEventVec` in external memory after the scratch copy in and out.

**This is not boot-only.** All 30 `cer_range` lines show `edit` dominating:

| Phase | Steady-state range across 30 windows |
|---|---|
| `edit` | **1.9 ms – 367 ms** (boot: 37 530 ms) |
| `append` (traversal) | 7.7 – 61.7 ms |
| `sort` | 0.1 – 21.1 ms |
| `collect` | ≤ 0.2 ms |

The edit projection is the **top display cost continuously**, not merely a boot spike; boot is simply the window carrying 2192 events instead of the typical 135–330.

**Two unbounded counts, both monotonic.** `rows` is **1694 and constant** for the whole session — active note-edit rows are never pruned or collapsed for projection purposes, so every paint replays all of them. `lists` climbs **108 → 115** across the same session, so active committed passes accumulate. Neither count is bounded by the paint window.

## 13. Why the row population is 1694 (Stage O)

The row count is the multiplier on the confirmed owner, so it decides whether the fix belongs in the projection or upstream in edit-pass lifecycle. Two facts are already established from capture:

- `rows=1694` is present in the **first** `cer_range` at 7.05 s, before any user input in the session, and never changes. The rows therefore arrive with loaded loop state.
- `overlap_hold` in the same session reports `shorten=0` and `hide=0` with `add` 2–35, so this session's overlap evaluation did not create them.

Expected scale from the user: ~4 notes/bar over 68 bars is ~272 notes, and "around the hundreds" of rows. 1694 is ~6.2× the note count.

### Code-path findings (read-only trace)

`EditPassState` has only **`Active`** and **`Disabled`** — there is no sealed or baked state, and edit rows are never folded into committed chunk content. Every production append goes through `Loop::saveNoteEditPass`, from three triggers: NOTE_EDIT commits (`EditManager::commitEditAction`, `bakeNoteEditSessionStoreToPasses`), overdub overlap sealing (`Loop::sealPendingNoteChangesToEditPasses`), and SD restore (`readPersistedEditsTail`). Edit rows **are** persisted with their state in the v6 loop body under the `EPT3` tail and restored as `Active`, which is consistent with 1694 rows existing 7 s after boot with no user input.

Two structural properties explain how the population can exceed the note count:

1. **`sealPendingNoteChangesToEditPasses` runs per seal event** — every overdub loop wrap (`Track::commitOverdubWrapAtSessionStart`) and every overdub stop — and assigns **new** row ids without disabling prior companion rows for the same `targetNoteId`. Pending Hide/Shorten upserts per note *within* one window, so a single window cannot multiply; successive seal generations can.
2. **`planContiguousCollapsedRows` cannot fold companions.** It only merges contiguous runs of `Update` rows whose `propertyType` is `Pitch` or `Velocity` with a matching `targetNoteId`. Overdub companions are `Delete` (Hide) and `Update` + `Length` (Shorten), so collapse is a no-op for them. `applyNoteEditPassSequence` still visits every planned row even when `containsPresentNoteId` skips the apply, so stale no-op rows cost a full iteration each.

The only edit-row reclaim, `Loop::reclaimUnreferencedDisabledEditPasses`, erases **`Disabled`** rows not pinned by undo. Active rows are never pruned and `saveNoteEditPass` admission is bounded only by `Config::HEAP_RESERVE_BYTES` — there is no maximum row count.

### Instrumentation shipped to close it

Reading gives a consistent account but not proof of composition, so `DIAG,cer_rows` reports the actual population whenever `cer_range` emits (computed only on emit, so no steady-state cost):

```
DIAG,cer_rows,active=…,disabled=…,comp=…,ne=…,del=…,crt=…,ulen=…,upit=…,uvel=…,urng=…
```

`comp` counts active rows at `kOverdubCompanionEditPassIndex` (255) versus `ne` for NOTE_EDIT indices, and the action/property split separates what `planContiguousCollapsedRows` *can* fold (`upit`, `uvel`) from what it cannot (`del`, `ulen`, `urng`, `crt`). This distinguishes the two candidate causes directly: a `comp`-dominated population with high `del` + `ulen` confirms cumulative overdub companion sealing, whereas an `ne`-dominated one points at NOTE_EDIT commit accumulation instead. `disabled` also shows whether reclaim is keeping up. Tag falls under the existing `DIAG,cer_` Tier-A prefix. RAM1 unchanged at **4640** bytes. Native **1406/1406**.

### Confirmed composition — [`110842`](../../captures/session_20260821_110842.log)

All 29 `cer_rows` lines are byte-identical:

```
DIAG,cer_rows,active=1694,disabled=0,comp=1694,ne=0,del=1378,crt=0,ulen=316,upit=0,uvel=0,urng=0
```

| Field | Value | Meaning |
|---|---|---|
| `comp` / `ne` | **1694 / 0** | **Every** row is an overdub overlap companion; zero NOTE_EDIT rows |
| `del` | **1378** | Hide companions (`Delete`) |
| `ulen` | **316** | Shorten companions (`Update` + `Length`) |
| `upit` + `uvel` | **0** | Rows `planContiguousCollapsedRows` is able to fold |
| `crt` / `urng` | 0 / 0 | No creates, no NOTE_EDIT range moves |
| `disabled` | **0** | No companion row has ever been disabled |

**Cause confirmed: cumulative overdub companion sealing.** The NOTE_EDIT hypothesis is eliminated outright by `ne=0`.

**Correction.** §13 said `planContiguousCollapsedRows` "runs every call but cannot reduce a population dominated by" companions. The measured population is not merely dominated by them — it is **100 %** of them, and the count of collapse-eligible rows is exactly **0**. Collapse is structurally incapable of helping here, not merely ineffective.

`disabled=0` also shows supersession never happens: sealing a newer Hide/Shorten for a note does not disable the older row for the same `targetNoteId`, and no undo disabled any of them. Against ~272 notes, `del=1378` is ~5.1 Hide rows per note — roughly five seal generations retained in full.

**The cost is superlinear in window size, independent of row count.** Two boot windows with `rows` fixed at 1694:

| Capture | `evout` | `edit` |
|---|---|---|
| [`105032`](../../captures/session_20260821_105032.log) | 2192 | 37 530 431 µs |
| [`110842`](../../captures/session_20260821_110842.log) | 2339 | 45 393 105 µs |

A **6.7 %** increase in events produced a **21 %** increase in time. Bounding the row population alone therefore does not fix the growth shape — the per-row apply over the whole event vector has to be addressed too.

**Durability consequence.** These rows are persisted `Active` in the v6 `EPT3` tail, so existing saved loops already carry them. A projection-side fix changes paint cost but does not shrink stored state; shrinking that needs supersession-disable at seal time or a migration.

## 14. The cost is 219 Shorten applies, not 1694 rows (Stage O)

Decision from the design question: fix the **paint-path cost** first, via **last-wins dedupe at projection** — no persistence change, no state-transition change.

### Measured — [`111733`](../../captures/session_20260821_111733.log)

```
DIAG,cer_edit,us=48755056,plan=405,collect=3860,guard=1586,del=10519044,ulen=38071317,other=0,
              planned=1694,exec=1350,skip=344,ndel=1131,nlen=219,ev=2483,present=1370
DIAG,cer_copy,us=48777653,copyin=18413,core=48755116,copyout=4124,ev=2483
```

| Phase | Total | Calls | Per call |
|---|---|---|---|
| `ulen` — `applyChangeLengthById` | **38 071 317 µs (78.1 %)** | **219** | **173.8 ms** |
| `del` — `applyDeleteNoteById` | 10 519 044 µs (21.6 %) | 1131 | 9.3 ms |
| `plan` + `collect` + `guard` | 5 851 µs | — | negligible |
| `copyin` + `copyout` | 22 537 µs | — | negligible |

**Corrections to §13's reasoning.** Two claims made from reading were wrong:

1. "Duplicate Hide rows are already skipped by the guard, so dedupe won't help." The guard skips only **344** of 1694 rows; **1350 execute**. Row skipping is not the dominant behavior.
2. The cost is not spread across the population. **219 Shorten applies carry 78 % of a 49-second stall.** Row count is not the cost driver — per-apply cost is.

**Mechanism.** `applyChangeLengthById` has two branches. When `newEnd < refEnd` it delegates to `shortenNoteEndById` and returns. When `newEnd > refEnd` it calls **`NoteUtils::reconstructNotes(events, loopLength, false)`** — a full reconstruction over the entire event vector — then scans every reconstructed note for same-pitch overlap. 173.8 ms per call at 2483 events (≈70 µs per event) is consistent with that reconstruction being quadratic in event count, and the per-call cost is superlinear across windows: **2.73 ms** at `ev=227`, **9.98 ms** at `ev=808`, **173.8 ms** at `ev=2483` — tripling events made each call 17× slower.

**Why a Shorten companion would ever lengthen.** Companion rows for one note accumulate across seal generations with different end ticks and are replayed in stored order. Once a note has been shortened to a newer, shorter end, a later row carrying an **older, longer** end satisfies `newEnd > refEnd` and takes the reconstruct branch. This is the same ordering property that makes **last-wins dedupe** the correct fix: keeping only the final Length row per `targetNoteId` removes the out-of-order lengthen, so the reconstruct branch is never entered from companion replay. Dedupe is therefore effective for a different reason than row-count reduction.

### Instrumentation shipped to validate the fix before implementing it

`cer_edit` now reports which branch runs, plus the reconstruction cost in isolation:

```
…,lgth=<lengthen-branch calls>,shrt=<shorten-branch calls>,noop=<newEnd==refEnd>,recon=<µs in reconstructNotes>
```

This is the pre-implementation check for the chosen dedupe: if `lgth` accounts for the 219 Shorten applies and `recon` for the bulk of `ulen`, then last-wins dedupe eliminates the dominant cost. If instead `shrt` dominates, the cost is inside `shortenNoteEndById` and dedupe would **not** fix it — in which case the fix has to target the apply itself. RAM1 unchanged at **4640** bytes. Native **1406/1406**.

### Confirmed — [`112151`](../../captures/session_20260821_112151.log)

```
DIAG,cer_edit,us=51131076,del=10669888,ulen=40293621,recon=<clipped>,ndel=1131,nlen=219,lgth=70,shr…
```

| Owner | Cost | Share | Calls |
|---|---|---|---|
| `ulen` — lengthen branch | **40 293 621 µs** | **78.8 %** | `lgth=70` |
| `del` — `applyDeleteNoteById` | 10 669 888 µs | 20.9 % | `ndel=1131` |

The boot line clipped at `recon`, but every untruncated smaller-window line puts `recon` at **93–99 % of `ulen`** (`ulen=33506/recon=33142`, `ulen=25454/recon=25092`, `ulen=27780/recon=26398`). So **70 lengthen calls**, each paying one full `NoteUtils::reconstructNotes`, are essentially the whole 40.3 s — ≈576 ms per call at `ev=2513`.

`shrt` and `noop` carry no measurable cost. The pre-implementation check in the previous section resolves to: **the cost is the reconstruct, not the apply**.

---

## 15. Fix shipped — pitch-scoped reconstruction, not row dedupe (Stage O)

### Last-wins dedupe was implemented and rejected on evidence

Dedupe of superseded `Length` rows (keep the newest per `targetNoteId`, barrier at `Create` / `Delete` / `NoteRange`) does remove the out-of-order lengthen: with one row per note the note is at committed length when the row applies, so `newEnd < refEnd` always holds. It was implemented and it **failed `test_sequence_phasea_randomized_equivalence_against_baseline`** (scenario 5, index 6) — the planner's strict parity guard against naive row-by-row replay.

The failure is real, not a test artifact. The lengthen branch has **side effects on other notes** (`notesToDelete`, `notesToShorten`). Dropping an intermediate `Length` row also drops the overlap side effects it applied to same-pitch neighbours, so replay output changes in the general case. Whether those side effects are replay artifacts of stale companion rows or intended semantics is an **open behavior decision**, not something to settle by weakening the parity test. Dedupe is parked pending that decision.

### What shipped instead

`applyChangeLengthById`'s lengthen branch only ever inspects reconstructed notes matching the edited note's own pitch — every other reconstructed note is discarded by its `note.note != onEvt` filter. `reconstructNotesForPitch` now reconstructs **only that pitch**.

Exactly equivalent, verified against the code rather than assumed:

| Stage | Why pitch-local |
|---|---|
| Pairing | `appendCanonicalSpansFromMidiRange` keys on `activeNoteStacks[pitch]`; `tryPairWrappedTailOn` / `noteIdAtOnTick` match pitch + channel |
| Projection | `IntervalProjection::projectDisplayNotes` projects each span in isolation — no cross-span reference |
| Dedupe | `dedupeProjectedDisplayNoteVec` keys on `(note, startTick, endTick)`; filtering preserves relative index order |

This also removes the superlinearity rather than just scaling it down: `tryPairWrappedTailOn` rescans the whole event vector from inside the per-event loop, so the discarded pitches were a quadratic term.

**Not** the `del` owner. `ndel=1131` is 1131 *distinct* notes deleted — the `presentNoteIds` guard already skips repeats — so **10.67 s (20.9 %) survives this fix** and is the next lever. Each call is 9.4 ms for what is a find-on / find-off / erase pair over 2513 events; batching the deletions into one sweep instead of 1131 O(n) erases is the obvious shape, but it reorders apply semantics and needs the same behavior decision as dedupe.

Native **1406/1406**, `test_edit_apply` 51/51 including the randomized parity guard. RAM1 unchanged at **4640** bytes.

---

## 16. The reconstruct was 4 % of it — `applyDeleteNoteById` owns the rest (Stage O)

### Measured after the pitch-scoped reconstruct — [`113813`](../../captures/session_20260821_113813.log)

```
#CAP,66383954,DIAG,cer_edit,us=54056103,del=11560194,ulen=42316496,recon=1562250,
              ndel=1131,nlen=219,lgth=70,shrt=126,noop=8,ev=2660
```

The fix worked on its target: per-lengthen-call reconstruct went **576 ms → 22 ms** at boot, and **9.98 ms → 1.66 ms** at `ev≈800`. Total `recon` is now **1.56 s**.

**But `ulen` stayed at 42.3 s**, so 40.7 s of it was never in `reconstructNotes`. §14's inference that `recon ≈ ulen` was drawn from small untruncated windows and does not hold at boot — in this capture `recon` is only **18 %** of `ulen` even at `ev=773`.

### Where the 40.7 s is, by elimination from measured numbers

| Step | Derivation | Result |
|---|---|---|
| One `applyDeleteNoteById` | `del=11 560 194 / ndel=1131` | **10.2 ms** |
| `findNoteOffForOnIndex` upper bound | dominates that call | **≤ 10 ms** |
| All 126 shorten calls | two of those each | **~2.5 s** — cannot explain 40.7 s |
| Remaining owner | lengthen branch's `notesToDelete` loop calls `applyDeleteNoteById` per swallowed same-pitch note | ~580 ms per call ≈ **57 nested deletes** |

So `applyDeleteNoteById` owns roughly **52 s of the 54 s** — the 10.7 s of top-level `del` rows and the 40.7 s of nested deletes are the same primitive.

### Attempted fix — stop sizing the pairing scratch to the whole vector

> **Superseded by the next section.** The hypothesis below (allocation size is the cost) was
> **disproved on device.** Kept for the reasoning trail; do not treat it as a shipped win.

`findNoteOffForOnIndex` built `walkOrder` with `reserve(events.size())` (a 21 KB heap block at `ev=2660`) and `offPaired(events.size(), 0)` (a full-length zero fill), both on the **global** heap rather than `MidiEventVec`'s `InternalHeapFirstAllocator`. Both only ever cover the walked channel + pitch, so they were resized to that subset with the paired flags indexed by position in an ascending `pitchIndices` list.

Behavior-preserving: `pitchIndices` holds exactly the events the old filters admitted, in ascending order, so the wrap-off scan is the same traversal — same order, same `break`, same `untaggedWrapOffCount`. That property still holds in the shipped one-pass version.

**Still out of scope and unmeasured:** `findWrapHeadOffForOnIndex` also runs per delete, with an inner `isPreferredWrapTailForHeadOff` full scan gated by `isHeadTailWrappedPair`, and `findNoteOnByIdCached` is always linear because the lookup cache is cleared after every delete. Neither was touched.

Native **1406/1406**, including the randomized planner parity guard and `test_noteutils_reconstruct`.

### Per-call tuning is not the lever — measured neutral

The scratch-sizing change in §16 was committed before device validation. That was wrong: it **regressed** per-delete cost by 11 %, because the counting pass added to size the allocation exactly cost more than the two allocations it saved.

Boot totals are **not** comparable across these captures — the edit-row population differs per session (`ndel` 1131 → 1216, `nlen` 219 → 316). Only windows matching on both `ev` and `ndel` are valid:

| Version | `ev=250`, `ndel=175` | Per delete |
|---|---|---|
| [`113813`](../../captures/session_20260821_113813.log) — two passes (baseline) | `del=9092` | 51.95 µs |
| [`114927`](../../captures/session_20260821_114927.log) — three passes | `del=10085` | 57.63 µs |
| [`115535`](../../captures/session_20260821_115535.log) — one pass | `del=9060` | **51.77 µs** |

One pass recovered the regression and gained nothing beyond it. **Pass count and allocation size are not what makes a delete cost ~11 ms at boot**, and nothing measured so far explains what does. The one-pass shape is kept because it is strictly less work, not because it is faster.

The §15 reconstruct fix continues to hold: `recon=1 734 568` over `lgth=83` is **21 ms** per lengthen call versus 576 ms before. `ulen` is still 52.9 s, so ~638 ms per lengthen call remains the nested delete loop.

### Behavior-preserving options are exhausted

Making each delete cheaper does not work. Every remaining lever removes delete *calls* and changes replay output:

| Lever | Blocker |
|---|---|
| Batch the lengthen branch's nested deletes into one sweep | Same-pitch LIFO pairing shifts as each note is erased, so it is not equivalent as written — though using the reconstruction's own pairing for all of them is arguably more self-consistent than re-deriving it after every erase |
| Revive the parked last-wins dedupe (§15) | Fails the randomized planner parity guard |

Both reduce to one **open decision**: should replaying a stale companion row apply overlap side effects to its neighbours, or is only the newest generation's geometry real? Not resolvable from the code. Nothing further should be implemented on this path until it is answered.

---

## 17. Stage O closeout — what is proven, what is not

### Shipped

| Commit | Change | Result |
|---|---|---|
| `7d8c513` | `reconstructNotesForPitch` — lengthen branch reconstructs only the edited note's pitch | **576 ms → 21 ms** per lengthen call. Real, exactly behavior-preserving, holds across three captures |
| `210d387` | Pairing scratch sized to the walked pitch, via a counting pass | **Regressed 11 %** per delete. Committed before device validation — the process error of this stage |
| `cc891c7` | Same without the counting pass — one full pass instead of two | **Neutral** (51.77 vs 51.95 µs). Kept because it is strictly less work, not because it is faster |

Native **1406/1406** throughout, including the randomized planner parity guard.

### Proven

- The lengthen branch's full-loop reconstruct was real, and pitch-scoping removed it. Reconstruction is pitch-local end to end (`activeNoteStacks[pitch]`, span-local projection, `(note, startTick, endTick)` dedupe), so the candidate set is unchanged.
- `applyDeleteNoteById` owns the bulk of the replay — both the top-level `del` rows and the lengthen branch's nested deletes are the same primitive.
- Per-call scanning and allocation inside that primitive are **not** the cost. Two, three, and one full passes over the event vector measured 51.95, 57.63, and 51.77 µs per delete at a matched window.

### Not explained

**A single `applyDeleteNoteById` costs ~11 ms at boot sizes and nothing measured accounts for it.** It is not the scanning removed in `cc891c7`, and no operation count derivable from `findNoteOnByIdCached` + `findNoteOffForOnIndex` + `findWrapHeadOffForOnIndex` + `eraseEventIndices` approaches that figure at `ev≈2700`. Per-delete cost also scales far worse than the event count (~170 µs at `ev≈800`, ~11 ms at `ev≈2700` — 80× for 3.5×), which no counted term explains either.

Candidate explanations, **none measured, none to be acted on before instrumenting**:

- `MidiEventVec` is `InternalHeapFirstAllocator` and spills to external memory when the internal heap is exhausted, so boot-size vectors may be scanned in PSRAM while `ev≈800` ones are not.
- `findWrapHeadOffForOnIndex`'s inner `isPreferredWrapTailForHeadOff` full scan, gated by `isHeadTailWrappedPair`, is quadratic whenever the gate opens. Never instrumented.
- `findNoteOnByIdCached` degrades to a linear scan on every call, because the lookup cache is cleared after each delete.

### Method corrections to carry forward

1. **Do not infer a ratio measured in small windows onto the boot window.** §14 concluded `recon ≈ ulen` from untruncated small lines; at boot `recon` is 4 % of it, and 18 % even at `ev=773`. That single inference sent the first fix at 4 % of the problem.
2. **Do not compare boot totals across captures.** The edit-row population differs per session (`ndel` 1131 → 1216, `nlen` 219 → 316). Only windows matching on **both** `ev` and `ndel` are comparable — the `ev=250, ndel=175` window is the reliable anchor.
3. **Do not commit a performance change before device validation.** `210d387` put a regression in HEAD; it was fixed forward per [Multi-Stage-Bugfix-Workflow](../../.cursor/rules/Multi-Stage-Bugfix-Workflow.mdc), not amended.
4. **A truncated capture line is not evidence.** `kMaxCaptureTextBytes` is 192; the boot `cer_edit` line clipped at `recon` and the missing field was the one that mattered.

### Two candidate directions — neither started

**A. Remove the delete calls.** Both options change replay output and share one open decision: should replaying a stale companion row apply overlap side effects to its neighbours, or is only the newest generation's geometry real?

| Lever | Blocker |
|---|---|
| Batch the nested deletes into one sweep | Same-pitch LIFO pairing shifts as each note is erased |
| Revive the parked last-wins dedupe (§15) | Fails the randomized planner parity guard |

**B. Question the replay itself.** Boot replays **1216 deletes and 316 length rows** to paint a loop the user recorded four notes per bar into (§13 established row accumulation comes from cumulative overdub companion sealing without supersession disable). Making that replay faster may be the wrong frame; not replaying it at boot is the alternative. This path does not depend on the decision in A.

## 18. Incremental effective companion projection experiment

The native experiment is implemented in
`test/test_edit_apply/test_edit_apply.cpp`:

- `R(history)` uses the current ordered `applyNoteEditPassSequence()`.
- `I(history)` starts from the initial capture events and applies each newly
  sealed companion with `applyNoteEditPass()`.
- Both states are compared after every generation, including exact MIDI event
  identity/order/ticks and reconstructed display-note geometry.
- Undo/redo is modeled by moving through retained generation states while
  re-evaluating the corresponding historical prefix.

The deterministic matrix covers single and repeated Hide, increasing and
decreasing Shorten/Lengthen, Hide/Shorten ordering, wrap notes, and multiple
overdub generations. The randomized fixture covers repeated targets, same-
pitch overlaps, different-pitch notes, wrap notes, and undo/redo transitions.

Native result: **1408/1408**, including the two new experiment cases.

The affected-geometry assertions establish:

- Shorten changes only the target note.
- Hide removes only the target note.
- Lengthen can affect multiple same-pitch overlap notes, including more than
  one neighboring row; a different-pitch note is unchanged.
- Shorten after Hide is a no-op.
- Hide after Shorten applies the target deletion to the already-shortened
  state.

This proves that ordered companion application is compositional: applying a
new companion to the existing effective event state produces the same result
as replaying the complete history for the tested sequences. It does **not**
prove that historical rows may be discarded, nor does it establish the
smallest persistent representation. The retained state must still contain
enough same-pitch and wrap-aware geometry for the next companion delta.

No production firmware, EditPass state, ordering, undo, persistence, NoteId,
scheduler, or playback-state-machine behavior was changed.

## 19. Reduced-state candidate (minimal geometry) divergence experiment

The second native experiment tests a stricter candidate:

- `J(history)` retains only per-note effective geometry
  (`NoteId`, pitch, velocity, start tick, end tick) as reconstructed
  `DisplayNote` rows.
- For each newly sealed companion, `J` rematerializes a transient event vector
  from that reduced state, applies `applyNoteEditPass()`, then reconstructs
  reduced geometry again.
- The oracle compares `J` against `R(history)` (ordered
  `applyNoteEditPassSequence()`) at every generation and undo/redo prefix.

Coverage is implemented in `test/test_edit_apply/test_edit_apply.cpp`:

- `test_incremental_companion_projection_reduced_state_minimal_geometry_diverges`
- `test_incremental_companion_projection_reduced_state_randomized_divergence`

Native result: **1410/1410**.

Observed behavior:

- The reduced geometry candidate diverges from `R(history)` at generation 1 in
  deterministic and randomized histories (single Hide is sufficient).
- The reduced-state affected-geometry checks also show wrap-coupled mutations:
  target-note Hide/Shorten/Lengthen operations can change
  `kCompanionWrapNoteId` geometry in this replay contract.

Conclusion:

- Minimal per-`NoteId` geometry is **not** sufficient to preserve current
  ordered replay semantics bit-for-bit.
- Any bounded projection that replaces full historical replay must retain
  additional same-pitch and wrap-aware pairing context (or explicitly redefine
  semantics with a separate effective-state contract).

No production firmware, `EditPass` semantics/order, undo, persistence, NoteId
identity, scheduler, or playback-state-machine behavior was changed.

## 31. Implementation gate decision

The investigation now has a complete proof boundary for the effective-state
representation used by companion replay.

### Proven minimum equivalent state

Across deterministic, randomized, wrap, and undo/redo-prefix oracles, exact
parity with ordered `applyNoteEditPassSequence()` requires:

- paired note spans,
- explicit full `eventOrder` tokens,
- stable `noteId`,
- `pitch`, `channel`, `velocity`,
- `startTick`, `endTick`,
- state refresh after every applied companion delta.

All attempted reductions outside that set are disproven by deterministic tests
in `test/test_edit_apply/test_edit_apply.cpp`.

### Representation boundary decision

- Keep companion `EditPass` rows as canonical history/persistence/undo inputs.
- Build and maintain a separate effective paired state for projection consumers.
- Update that state incrementally per newly sealed companion delta.

### First implementation scope (owner-local)

Apply this only to committed/projection consumers that currently replay the full
active edit-row set:

- `Loop::materializeToEventVector` in `src/Loop/LoopMaterialization.cpp`
- fallback projection paths in `src/Loop/LoopVisualCache.cpp`
- related projection call sites that currently invoke
  `applyNoteEditPassSequence()` on full active companion history

Do not change:

- companion row semantics/order,
- undo and persistence behavior,
- scheduler or playback state machine.

### Decision

IMPLEMENTATION JUSTIFIED: introduce a projection-only paired effective companion
state (with full `eventOrder` + note metadata + stable IDs) and consume it in
projection/materialization paths instead of replaying full companion history on
every projection call.

## 33. Boot title stall — remove committed display resolve from `boot_commit`

This slice is independent of the companion-projection cache. The regression is
present on the pre-addition path after `b4a8a70` and is owned by the boot
load-frame gate, not by `applyNoteEditPassSequence` itself.

### Capture anchors

| Capture | Owner | Numbers |
|---|---|---|
| [`115535`](../../captures/session_20260821_115535.log) | `loop_rem,boot_commit` | `67510398` us; `disp_win=67240538`; `cer_range=66596898`; `rows=1876` |
| [`111733`](../../captures/session_20260821_111733.log) | same family | 49 s-class `boot_commit` |

`BOOT,scan,done` arrives quickly. Title remains because `runDeferredLoadAndDisplayFrame`
calls `displayManager.finishBootSetup()` then immediately `displayManager.update()`.
After `bootSetupComplete_` is true, `DisplayManager::update()` leaves the title
early-return and runs `resolveDisplayNotesCommitted` → `rebuildDisplayNotesInWindow`
→ `copyEffectiveCommittedEventsInRange` + `applyNoteEditPassSequence`.

### Fix shipped

In `runDeferredLoadAndDisplayFrame`:

- `boot_commit` still runs `finishBootSetup`, `beginUsbHost`, LED refresh, and
  `onBootSlotLoadComplete`.
- It no longer calls `displayManager.update()` and no longer stamps
  `lastDisplayUpdate`.
- First-commit `Track::ensurePlaybackMergedEventsForSlot` is deferred while
  `bootSlotLoadRefreshPending` and runs on the first post-boot load-frame turn.
- Post-boot same-frame prewarm is unchanged (000659).

Display projection, companion history, persistence, and undo are unchanged.

### Host gates

- `pio test -e native -f test_runtime_timing_telemetry` PASS
- `pio test -e native -f test_committed_event_range` PASS
- `pio test -e native -f test_edit_apply` PASS
- `pio test -e native` PASS (`1425/1425`)
- `pio run -e teensy41-capture-serial` PASS (`RAM1` code `425740` / locals `4640`)

### Device capture

[`145953`](../../captures/session_20260821_145953.log) after the `boot_commit` display removal:

| Marker | Time / duration |
|---|---|
| `BOOT,scan,done` / setup done | ~8.2 s |
| `loop_rem,idle_gather` | **136084342** us (9.5 s → 145.7 s) |
| `DisplayManager: Boot setup complete` | 147.283 s |
| `loop_rem,boot_commit` | **242233** us (no longer the title owner) |
| first `disp_win` after USB | 136820430 us (`gather=136222448`) |

Title drain was blocked by the first STOPPED idle-maintenance turn: `runDeferredIdleMaintenance` ran before `setBootTitleLoadDrain(true)` in the load frame, so `rebuildVisualCacheIdleSlice` paid the full companion gather while OSTINATIX was still up.

Follow-up from [`151555`](../../captures/session_20260821_151555.log):

- Idle-drain skip reached `Boot setup complete` at 8.6 s.
- `finishBootSetup()` OLED blank left a black panel because `boot_commit` no longer painted.
- Restored `displayManager.update()` in `boot_commit`.
- Removed post-`b4a8a70` display/load-frame attribution telemetry (`disp_*`, `lframe_*`, `disp_win`, `disp_full`). Companion projection and idle-drain skip remain.

Success criteria: title drain reaches `Boot setup complete` without multi-minute `idle_gather`, and the main piano roll paints instead of staying black.

## 32. Companion-only projection cache in window consumers

This stage implements the first owner-local production slice for the companion
projection boundary in `Loop` projection consumers.

### Scope implemented

- Added a projection-only companion cache in `Loop` keyed by `playbackRevision`.
- Added a companion-only fast-path gate:
  - active note rows must all be overdub companions
    (`editPassIndex == kOverdubCompanionEditPassIndex`)
  - if any non-companion note row is active, the path falls back to existing
    ordered replay behavior.
- `Loop::copyEffectiveCommittedEventsInRange` now attempts this fast path
  before legacy chunk-window + replay.
- `Loop::advanceFallbackGatherSession` now attempts the same fast path before
  budgeted traversal/replay.

### Behavior contract preserved in this stage

- Companion/non-companion semantics and row ordering are unchanged.
- Undo/persistence/state-machine behavior is unchanged.
- When companion-only gate does not hold, replay remains exactly on the legacy
  path.

### Added native coverage

In `test/test_effective_event_store/test_effective_event_store.cpp`:

- `test_companion_projection_window_matches_legacy_replay`
- `test_companion_projection_falls_back_with_non_companion_note_rows`

Both tests assert the new path produces the same window result as the legacy
window+replay reference for the covered fixtures.

### Validation

- `pio test -e native -f test_effective_event_store` PASS (9/9)
- `pio test -e native -f test_edit_apply` PASS (68/68)
- `pio run -e teensy41-capture-serial` PASS
  - RAM1: `variables:93664, code:425740, padding:244, free:4640`

### Notes

- The first implementation uses a projection-only effective event cache for the
  companion-only gate and keeps the legacy replay fallback for all mixed-row
  windows.

## 30. Final remaining slices batch: start/end tick requirements + final full native gate

This batch completed the remaining straightforward context-necessity slices in
one pass and then ran the requested single full native sweep.

### Added tests (`test/test_edit_apply/test_edit_apply.cpp`)

- `test_incremental_companion_projection_start_tick_ablation_requirement`
- `test_incremental_companion_projection_end_tick_ablation_requirement`
- `test_incremental_companion_projection_pitch_ablation_requirement`
- `test_incremental_companion_projection_same_tick_order_divergence_boundary`

### Proven in this batch

1. Start-tick ablation diverges immediately (generation `0`) on mixed-metadata
   fixtures.
2. End-tick ablation diverges immediately (generation `0`) on mixed-metadata
   fixtures.
3. Pitch ablation diverges immediately (generation `0`) on mixed-metadata
   fixtures.
4. Same-tick-only ordering diverges with a deterministic boundary on seeded
   randomized histories.
5. The same mismatching histories continue to pass under full paired state.

### Validation

- Focused gate during iteration: `pio test -e native -f test_edit_apply`
  (suite expanded to 68 cases and stayed green).
- Final full native sweep after all slices were in place:
  **1423/1423** passed.

### Net conclusion after all slices

The smallest currently proven-equivalent effective representation for companion
delta replay remains:

- paired note spans,
- explicit full `eventOrder` tokens,
- stable `noteId`,
- `pitch`,
- `channel`,
- `velocity`,
- `startTick` and `endTick`,
- refreshed after each applied companion delta.

No production firmware, `EditPass` semantics/order, undo, persistence, NoteId
identity, scheduler, or playback-state-machine behavior was changed.

## 29. Pitch ablation requirement (mixed-metadata fixture)

This stage tests whether pitch can be removed from the paired candidate while
keeping all other proven-required context.

Candidate:

- `PitchAblatedCompanionState` keeps paired spans + `eventOrder` + stable
  `noteId` + channel + velocity, but normalizes pitch in candidate state.

Coverage in `test/test_edit_apply/test_edit_apply.cpp`:

- `test_incremental_companion_projection_pitch_ablation_requirement`

Fixture details:

- Uses `makeCompanionProjectionMixedChannelVelocityInitialEvents` (mixed pitch,
  channel, and velocity metadata) and companion `Length`/`Delete` operations.

Proven assertions:

1. Full paired state remains equivalent on the same fixture/history.
2. Pitch-ablated candidate diverges at generation `0`.

Native status for this stage:

- Focused gate: `pio test -e native -f test_edit_apply` passed.

Conclusion:

- Pitch is required context for exact replay parity in the paired candidate.
- Removing pitch from stored effective state breaks equivalence immediately on
  mixed-metadata fixtures.

No production firmware, `EditPass` semantics/order, undo, persistence, NoteId
identity, scheduler, or playback-state-machine behavior was changed.

## 28. Same-tick-order ablation (tick buckets + intra-tick order only)

This stage tests whether full explicit `eventOrder` can be reduced to:

- tick-bucket ordering (ticks ascending), plus
- explicit order only within each tick bucket.

Candidate:

- `SameTickOrderCompanionState` keeps paired spans and per-tick token order but
  does not preserve any additional cross-tick ordering context.

Coverage in `test/test_edit_apply/test_edit_apply.cpp`:

- `test_incremental_companion_projection_same_tick_order_divergence_boundary`

Proven assertions:

1. Seeded randomized histories contain deterministic divergence under this
   same-tick-only ordering representation.
2. The same mismatching history remains equivalent under full paired state.
3. Divergence has a deterministic boundary (`prefix N-1` equivalent,
   `prefix N` divergent).

Native status for this stage:

- Focused gate: `pio test -e native -f test_edit_apply` passed.

Conclusion:

- Preserving only same-tick ordering is not sufficient; replay parity still
  requires full explicit event-order tokens.

No production firmware, `EditPass` semantics/order, undo, persistence, NoteId
identity, scheduler, or playback-state-machine behavior was changed.

## 27. Split-order ablation (separate On/Off order streams)

This stage tests a lighter ordering representation than full `eventOrder`:

- `SplitOrderCompanionState` stores paired spans plus two order vectors:
  - `onOrder` for NoteOn ordering
  - `offOrder` for NoteOff ordering
- Materialization merges these two ordered streams by tick.

Coverage in `test/test_edit_apply/test_edit_apply.cpp`:

- `test_incremental_companion_projection_split_order_divergence_boundary`

Proven assertions:

1. Seeded randomized histories contain deterministic divergence under split
   On/Off ordering.
2. The same mismatching history remains equivalent under full paired state.
3. Divergence has a deterministic boundary (`prefix N-1` equivalent,
   `prefix N` divergent).

Native status for this stage:

- Focused gate: `pio test -e native -f test_edit_apply` passed.

Conclusion:

- Splitting order into independent On/Off streams is not sufficient to preserve
  full replay semantics.
- Full explicit event-order tokens are still required in the smallest
  proven-equivalent candidate.

No production firmware, `EditPass` semantics/order, undo, persistence, NoteId
identity, scheduler, or playback-state-machine behavior was changed.

## 26. Derived-key ordering ablation (rich deterministic key vs stored order)

This stage strengthens the ordering test by replacing stored `eventOrder`
tokens with a richer deterministic event sort key during materialization.

Candidate:

- `DerivedKeyOrderedCompanionState` keeps paired spans + stable note IDs +
  channel + velocity, but derives order via:
  `tick`, then event type (On before Off), then `pitch`, `channel`,
  `velocity`, `noteId`.

Coverage in `test/test_edit_apply/test_edit_apply.cpp`:

- `test_incremental_companion_projection_derived_key_order_divergence_boundary`

Proven assertions:

1. Seeded randomized histories still contain deterministic divergence under this
   richer derived-order policy.
2. The same mismatching history remains equivalent under full paired state.
3. The divergence has a deterministic boundary (`prefix N-1` equivalent,
   `prefix N` divergent).

Native result: **1418/1418**.

Conclusion:

- Even a rich deterministic key is not a complete substitute for explicit
  stored order tokens in the current replay contract.
- `eventOrder` remains required context in the smallest proven-equivalent
  companion representation.

No production firmware, `EditPass` semantics/order, undo, persistence, NoteId
identity, scheduler, or playback-state-machine behavior was changed.

## 25. NoteId ablation (renumbered identity vs stable identity)

This stage tests whether stable `noteId` identity can be removed from the
paired candidate while retaining all other proven-required context.

Candidate:

- `NoteIdAblatedCompanionState` keeps paired spans + `eventOrder` + channel +
  velocity, but renumbers note IDs after each applied delta.

Coverage in `test/test_edit_apply/test_edit_apply.cpp`:

- `test_incremental_companion_projection_note_id_ablation_divergence_boundary`

Proven assertions:

1. Seeded randomized histories contain deterministic divergence when note IDs
   are renumbered after deltas.
2. The same mismatching history remains equivalent under full paired state.
3. The divergence has a deterministic boundary (`prefix N-1` equivalent,
   `prefix N` divergent).

Native result: **1417/1417**.

Conclusion:

- Stable note identity is required context for exact companion replay parity.
- Removing or rewriting note IDs in the effective paired representation breaks
  equivalence even when event-order, channel, and velocity are retained.

No production firmware, `EditPass` semantics/order, undo, persistence, NoteId
identity, scheduler, or playback-state-machine behavior was changed.

## 24. Tick-ordered materialization ablation (derived order vs stored order)

This stage tests whether explicit `eventOrder` tokens can be removed and
replaced by deterministic tick-sorting rules derived from paired spans.

Candidate family:

- `TickOrderedCompanionState` (keeps paired spans, drops stored order tokens)
- Materialization policy A: same-tick **On before Off**
- Materialization policy B: same-tick **Off before On**

Coverage in `test/test_edit_apply/test_edit_apply.cpp`:

- `test_incremental_companion_projection_tick_ordered_materialize_divergence_boundary`

Proven assertions:

1. At least one deterministic tick-order policy diverges from
   `R(history)` on seeded randomized histories.
2. The same mismatching history still passes under full paired state
   (`eventOrder` retained).
3. For the first mismatching policy/history pair, divergence has a deterministic
   boundary (`prefix N-1` equivalent when `N>0`; `prefix N` divergent).

Native result: **1416/1416**.

Conclusion:

- Derived tick ordering is not a complete replacement for stored event-order
  tokens under the current replay contract.
- Explicit `eventOrder` context remains part of the smallest proven-equivalent
  companion representation.

No production firmware, `EditPass` semantics/order, undo, persistence, NoteId
identity, scheduler, or playback-state-machine behavior was changed.

## 23. Context ablation: channel and velocity requirements

To continue shrinking the paired candidate, this stage ablates channel and
velocity context independently while keeping paired spans + event-order tokens.

Coverage in `test/test_edit_apply/test_edit_apply.cpp`:

- `test_incremental_companion_projection_context_ablation_channel_velocity_requirements`

Fixture details:

- Mixed initial events (`makeCompanionProjectionMixedChannelVelocityInitialEvents`)
  with heterogeneous channel/velocity values.
- Companion operations include both `Length` and `Delete` rows.

Proven assertions:

1. Full paired state remains equivalent on this fixture.
2. Dropping channel context diverges at generation `0`.
3. Dropping velocity context diverges at generation `0`.
4. Dropping both diverges at generation `0`.

Native result: **1415/1415**.

Conclusion:

- Channel and velocity are both required context for exact replay parity when
  fixtures include heterogeneous note metadata.
- The current smallest proven-equivalent candidate remains:
  paired note spans + event-order tokens + channel + velocity, refreshed after
  each applied companion delta.

No production firmware, `EditPass` semantics/order, undo, persistence, NoteId
identity, scheduler, or playback-state-machine behavior was changed.

## 22. Span-only ablation (drop explicit event ordering context)

The next reduction stage removes explicit event-order context from the paired
candidate while preserving paired note spans:

- Candidate `L(history)` keeps only `CompanionEventSpan` entries and
  rematerializes as contiguous On/Off pairs per span order.
- Oracle comparison remains against `R(history)` (ordered
  `applyNoteEditPassSequence()`).

Coverage in `test/test_edit_apply/test_edit_apply.cpp`:

- `test_incremental_companion_projection_span_only_randomized_divergence_boundary`

This test proves three facts in one fixture:

1. A fixed-seed randomized history exists where span-only diverges.
2. The same history still passes with the paired state (`eventOrder` retained).
3. The divergence has a deterministic boundary (`prefix N-1` equivalent,
   `prefix N` divergent for the same history).

Native result: **1414/1414**.

Conclusion:

- Explicit event-order context remains required for parity under the current
  companion replay contract.
- The smallest proven-equivalent candidate in this investigation remains the
  paired state that refreshes both note spans and event-order tokens after each
  delta.

No production firmware, `EditPass` semantics/order, undo, persistence, NoteId
identity, scheduler, or playback-state-machine behavior was changed.

## 20. Paired companion state candidate (adds note on/off pairing context)

The third native experiment adds context beyond minimal display geometry:

- `K(history)` stores paired note spans derived from note events
  (`noteId`, channel, pitch, velocity, start tick, end tick) and preserves
  note-event ordering.
- For each newly sealed companion, `K` rematerializes note events from this
  paired state, applies `applyNoteEditPass()`, and re-extracts paired spans.
- The oracle compares `K` against `R(history)` (ordered
  `applyNoteEditPassSequence()`) at every generation and undo/redo prefix.

Coverage in `test/test_edit_apply/test_edit_apply.cpp`:

- `test_incremental_companion_projection_paired_state_operation_matrix`
- `test_incremental_companion_projection_paired_state_randomized_divergence`

Native result: **1413/1413**.

Observed behavior:

- The paired candidate (`CompanionEventSpan` + `eventOrder`) matches `R(history)`
  on the deterministic operation matrix.
- After correcting `PairedCompanionState::applyCompanionDelta` to refresh both
  `spans` and `eventOrder` from rematerialized events, the paired candidate also
  matches the seeded randomized histories.

Conclusion:

- The previously reported randomized divergence was caused by stale
  `eventOrder` in the test candidate state, not by a semantic mismatch in
  ordered companion replay.
- With refreshed ordering state, this paired representation preserves
  `R(history)` equivalence for the current deterministic + randomized + undo/redo
  oracle coverage.

No production firmware, `EditPass` semantics/order, undo, persistence, NoteId
identity, scheduler, or playback-state-machine behavior was changed.

## 21. Deterministic scenario-51 regression fixture

The fixed-seed scenario fixture is retained as a regression guard for the
paired-state ordering correction:

- `test_incremental_companion_projection_paired_state_scenario51_generation13_equivalent`
- Seed: `0xA11CE`, scenario index: `51`
- Boundary assertions:
  - prefix through generation `12`: equivalent
  - prefix through generation `13`: equivalent

Implementation detail:

- Shared helper `makeCompanionRandomizedHistoryScenario(...)` now builds one
  deterministic scenario from the seeded RNG stream so all companion randomized
  tests consume identical scenario generation logic.

Native result: **1413/1413**.

Conclusion:

- The fixed scenario now proves that generation-13 parity holds after the
  ordering-state refresh fix.
- This fixture guards against reintroducing stale ordering state in the paired
  candidate while preserving deterministic reproducibility.

No production firmware, `EditPass` semantics/order, undo, persistence, NoteId
identity, scheduler, or playback-state-machine behavior was changed.
