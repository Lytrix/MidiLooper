# Long overdub / record display freeze — layered display invalidation

**Status:** In progress — Stages 1 / 1a / 1b implemented; Stage 3 manual gate pending  
**Priority:** P0  
**Behavior:** Behavior-preserving  
**Ownership change:** No  
**State-transition change:** No  
**Evidence:** [`captures/session_20260810_234809.log`](../../captures/session_20260810_234809.log)  
**OpenSpec:** [`runtime-derived-representation-heap`](../../openspec/changes/runtime-derived-representation-heap/)  
**Related:** [`multi_track_playback_pressure_closure_refinement.md`](multi_track_playback_pressure_closure_refinement.md)

---

## 1. Problem

The long-overdub capture demonstrates an OLED display freeze while MIDI transport continues normally.

Evidence from [`session_20260810_234809.log`](../../captures/session_20260810_234809.log):

- 118-bar loop on track 0, slot 4 (`length=90624` ticks = 118 bars).
- Second overdub lasts approximately **170 seconds**.
- Transport continues advancing throughout the apparent freeze.
- No `#CAP,DFRAME` after approximately **62 seconds**.
- MIDI/debug input continues.
- Only **29** `#CAP,MO` lines — not the previous background-slot MO flood.
- Three `RING,overflow` events near overdub stop.
- Display state resumes at capture stop.

Evidence indicates **main-loop starvation in display-derived work**, not a frozen transport or playback clock.

Memory/storage-pressure findings (`Capture append failed`, `non-canonical storage`), boot deferred-save gating (clear blocked until slot restore drains), and continuous `SAVE,pending` / `save_state` failures are **Stage 5** (separate commit).

---

## 2. Root cause

**Primary owner:** [`resolveDisplayNotesLiveCapture`](../../src/DisplayManager/DisplayNoteResolveLiveCapture.cpp)

During active capture/overdub, `eventsAdded` participates in global `needsFullLiveRebuild`, escalating a **capture-layer change** into **whole-display reconstruction**:

```text
capture event
    ↓
eventsAdded participates in needsFullLiveRebuild
    ↓
overdub resolver selects full committed+capture reconstruction
    ↓
gatherCommittedEventsWithCapture()
    ↓
increasing O(N) display work as the loop grows
    ↓
cooperative main-loop budget exceeded
    ↓
OLED/deferred work starved
```

The defect is **not** simply that `eventsAdded` exists. The architectural defect:

> A change to the live capture layer can escalate into reconstruction of the committed + capture display representation.

Populated `visualCache` worsens this — current `useWindowedCommitLayer` excludes a populated cache:

```cpp
const bool useWindowedCommitLayer =
    loop.visualCache.notes.empty() &&
    loop.hasCommittedPasses() &&
    shouldAvoidFullVisualRebuild(loop, liveLoopLength);
```

A populated visual cache should be an **asset** for cheap composition.

The failure mode is **O(N) work × every frame**, not O(N) storage. A 118-bar overdub with hundreds or thousands of display notes in an incremental vector is acceptable if the hot path does not repeatedly copy or rebuild that vector every frame.

---

## 3. Architectural target — three bounded operations

Not toward windowed capture **storage**. Toward three **bounded operations**:

| Operation | Policy | Status |
|-----------|--------|--------|
| **1. Capture append** | Incremental (note-on append amortized O(1); note-off lookup / dirty-span marking are not strictly O(1)) | **Exists** — `appendCaptureEvent` → `applyCaptureEventToPreview` |
| **2. Capture-layer recomposition** | Revision-gated **and delta/layer bounded** — no full-preview copy when continuous input makes every frame observe a new revision | **Design checkpoint 0a → Stage 1** |
| **3. Playhead / tail update** | Per frame, **bounded** — no full capture-store copy | **Fix** — Stage 1 hot-path audit |

```text
                CAPTURE
                   │
          ┌────────┴────────┐
          │                 │
       append          display frame
          │                 │
          ▼                 ▼
 capturePreview        tails / window
 incremental            bounded
          │                 │
          └───────┬─────────┘
                  ▼
             composition
                  ▼
                 OLED
```

### Canonical storage → display (no new layer)

```text
                  CANONICAL STORAGE
                         │
          ┌──────────────┴──────────────┐
          │                             │
     committed                       capture
       passes                         store
          │                             │
          ▼                             ▼
   visualCache                  capturePreview
          │                         incremental
          │                             │
          └─────────────┬───────────────┘
                        │
                  display compose
                        │
              ┌─────────┴─────────┐
              │                   │
        visible committed    active capture
            material             material
              │                   │
              └─────────┬─────────┘
                        │
                    tails / frame
                        │
                        ▼
                       OLED
```

Do **not** introduce another canonical or derived storage layer to fix this bug.

### capturePreview: session-bounded, not window-bounded

`capturePreview` is **incrementally maintained** but **session-bounded** (holds all capture-session display notes), not window-bounded. This is **acceptable for this fix** provided:

- capture-layer recomposition is revision-gated (not per frame)
- per-frame tail work is bounded (no full-store copy)

**Window-bounded capture projection** (`capturePreviewWindow` or similar) is **optional future optimization** — only if profiling shows composing/copying the full preview exceeds the display budget. **Not part of this fix.**

### Approach ranking (audit-validated)

| Approach | Decision |
|----------|----------|
| Full committed + capture rebuild | ❌ Fix |
| Remove `eventsAdded` only | ❌ Too shallow |
| `visualCache` + incremental `capturePreview` + tails | ✅ Correct architecture |
| Add windowed capture representation immediately | ⚠️ Premature |
| Window capture projection if profiling requires | ✅ Future optimization |
| Split canonical committed storage into finer ownership | ❌ Unnecessary |

**Stage 1 scope:** The capture layer is **already incrementally maintained**. Stage 1 is a **display invalidation / composition correction**, not a new capture-preview architecture. Implementation must **additionally eliminate full-capture copying / reconstruction from per-frame tail and nominal incremental paths.**

---

## 4. Architecture gate

### Owner

- Primary: `resolveDisplayNotesLiveCapture`
- Secondary: Stage 4 playback-window build

### Invariant

> **Capture events update the capture layer only. During active overdub, capture-event arrival must not invalidate a valid committed `visualCache`.**

Independent committed-layer changes remain allowed:

- committed/visual revision changes
- explicit invalidation
- window/context changes
- other events that genuinely alter the committed representation

`captureRevisionChanged` causes **capture-layer recomposition only**; it does **not** invalidate the committed layer when the committed representation remains valid.

### Required invariants (Stage 1)

| ID | Invariant |
|----|-----------|
| **R1** | `captureRevisionChanged` → capture-layer recomposition only; **not** committed-layer invalidation when committed representation is valid |
| **R2** | `captureDisplayRevision` incremented **exactly once** per successful live capture append (single owner — remove duplicate increment in `recordMidiEvents` / `appendCaptureNoteOffAtPhase`) |
| **R3** | The nominal capture-revision path must **not** reconstruct the full display representation **or copy all `capturePreview.notes`**. Continuous MIDI can change the revision before every display frame, so revision gating alone is insufficient. |
| **R4** | `applyCapturePlayheadTails` (and overdub tail path) must **not** call `copySortedCaptureEvents` over the entire active capture every display frame. The replacement must preserve channel-aware wrap-held detection and preferred wrap-head note-off pairing. `capturePreview` alone does not currently contain that information. |

### Ownership / transitions — **NO change**

Do not modify capture ownership, overdub ownership, recording/overdub FSM, capture commit semantics, stop handling, or record → overdub transitions.

### Playback wrap — **NO touch**

Do not modify `playMidiEventsForSlot` or `projectionCycleStartTick`.

---

## 5. Correct fix (summary)

**Layer-specific invalidation** — not merely removing `eventsAdded` from one boolean.

Three bounded operations (§3) replace the prohibited global-rebuild path.

**Prohibited:**

```text
capture event → global display invalidation → full committed+capture reconstruction → main-loop starvation
```

---

## 6. Display layers

| Layer | Source | Invalidation / update |
|-------|--------|------------------------|
| **Committed** | `loop.visualCache.notes` | Committed/visual revision, explicit invalidate, window/context change — **not** capture-event arrival alone |
| **Capture** | `loop.capturePreview.notes` | Incremental via `applyCaptureEventToPreview` on append; **session-bounded** storage (not window-bounded — see §3) |
| **Tails** | `applyCapturePlayheadTails` / `applyRecordingPreviewOpenTails` | Per frame — **bounded**; no full `capture.store` copy each frame (R4) |

**Composition:** visible committed material + active capture material + tails/frame — layer-specific revisions determine which layer requires work.

---

## 7. Implementation order

Numbering is for **this bugfix**; does not replace M6 cross-references.

1. **Stage 0** — Instrumentation
2. **Stage 0a** — Pre-code design checkpoint: bounded preview composition + wrap-correct bounded tails
3. **Stage 1** — Layered display invalidation
4. **Stage 1a** — Overdub + populated `visualCache`
5. **Stage 1b** — Long live-record path
6. **Stage 1c** — M6 display/LED audit
7. **Stage 2** — Native regression tests
8. **Stage 3** — Manual 118-bar HITL verification
9. **Stage 4** — Playback window / M6 Phase 1
10. **Stage 5** — Memory / persistence pressure (separate commit): append failures, storage canonicalization, boot clear vs deferred save + slot-restore queue

Display starvation is fixed and verified **before** playback-derived representation and memory changes.

---

## 7a. Model handoff — when to stop and switch models

Use this table at **stage boundaries**. Do not start the next stage’s primary work until the handoff row says to switch.

| Stage | Primary model | Stop / switch trigger |
|-------|---------------|----------------------|
| **0** Instrumentation | Composer 2.5 | **Stop after** counters/timings land + `teensy41-capture-serial` builds + `pio test -e native` passes |
| **0a** Design checkpoint | **GPT 5.6 Sol** | **Start Sol here** — do **not** start Stage 1 firmware until 0a documents Decision A (composition) and Decision B (tails) in this plan |
| **1 / 1a** Core invalidation | GPT 5.6 Sol | **Stop after** layered invalidation + overdub/`visualCache` path ships; hand off mechanical follow-ups |
| **1b / 1c** Long record + M6 audit | Composer 2.5 | Implement after 0a design is pinned; Sol reviews layer-behavior assertions |
| **2** Native tests | Composer 2.5 | Fixtures + `pio test -e native`; Sol reviews invalidation semantics |
| **3** Manual 118-bar HITL | Composer 2.5 | Run capture / build/upload; **Sol** analyzes logs vs R1–R4 and pre-fix Stage 0 baselines |
| **4** Playback window (M6 Phase 1) | GPT 5.6 Sol | MIDI correctness — not Composer-only |
| **5** Memory / persistence | GPT 5.6 Sol | Separate commit — 5a append/canonicalization; 5b boot clear + deferred save; not display-fix acceptance |

**Hard gate:** Stage 0 complete → switch to **GPT 5.6 Sol** for Stage **0a** only. Stage 1 firmware remains blocked until 0a output is recorded in §9 checkpoint output.

---

## 8. Stage 0 — Instrumentation

`PERF_TELEMETRY` / `Diagnostics::Counter` (RAM1-safe):

- `DisplayCaptureFullGather` (target: **0** during steady long-loop overdub)
- `DisplayResolveOverBudgetCount` (resolve elapsed > `kDisplayResolveBudgetMicros` = 5000 µs)
- `DisplayCaptureEventsAdded`
- Timing slots with sum/count + `#CAP,DIAG,timing_max,…` via `architectureTimingMax`:
  - `DisplayResolveLiveCaptureTime` (full `resolveDisplayNotesLiveCapture`)
  - `DisplayCaptureGatherTime`
  - `DisplayCaptureComposeTime` (`rebuildLiveDisplayNotes`)
  - `DisplayCaptureTailsTime`
  - `DisplayUpdateTotalTime` (`DisplayManager::update`)

Establish a **pre-fix baseline** before Stage 1 for:

- complete `resolveDisplayNotesLiveCapture`
- committed+capture gather
- capture-layer composition/copy
- tail preparation
- total display update

Existing `DisplayBuild` sum/count remains for full-rebuild branch timing. Reuse existing `#CAP,DFRAME` for successful-frame cadence.

**Shipped (Stage 0):** enum extensions in `Diagnostics.h`, `architectureTimingMax` in `DebugSessionCapture`, touch points in `DisplayNoteResolveLiveCapture.cpp` and `DisplayManager.cpp`.

### Session evidence — `session_20260811_004608.log` (Stage 0 build)

| Symptom | Log anchor | Assessment |
|---------|------------|------------|
| Display freeze during long record | `#CAP` DFRAME last at **86.77s**; gap **259s** until **346.06s**; `#CAP,80163820,RING,overflow` at **80.16s** | Same starvation class as §2 — `resolveDisplayNotesLiveCapture` during `RECORDING` on 118-bar slot |
| “Hang at bar 55” | Musical bar 55 ≈ **197s** session time during long record; overdub at **362s** (`tick 1421` = bar 1.85) has **zero** post-362s `DFRAME` | On-screen bar 55 is playhead position while display already dead since ~87s — not a separate overdub-only defect |
| Post-record play, notes not redrawn | `Playback started` **329.633s**; `DISP,4,STOPPED,92928,…,1935` notes at **345.051s**; DFRAME resumes only after transport stop **345.05s** | Data present (`1935` display notes) but `DisplayManager::update()` not completing paint — PLAYING path still blocked (`shouldDeferHeavyDisplayRebuild` + long-loop windowed gather) |
| Record stop “>1 bar” | Stop button **328.904s** → `Recording stopped` **329.618s** (0.7s wall); `length=92928` `tick=92872` | Firmware stop path is fast; perceived delay is likely frozen UI (no state/note redraw until transport stop) or bar-quantized loop length (`RecordStopLength::quantizeTransportRecordLength`) |
| Boot clear blocked | `Queuing boot playback loop slot restore 8 pending`; `SAVE,pending` from **7.3s**; `Clear aborted: could not complete deferred save first` (11s, 17s, 32s, 47s); `PERS,…,already_pending` / `sync_drain_already_pending` | **Stage 5** — `MidiButtonActions::handleClearTrack` + `StorageManager::hasDeferredSaveWork()` / boot slot-restore queue; also keeps `shouldDeferHeavyDisplayRebuild()` true and worsens display deferral |

Stage 0 `DIAG,counter` / `DIAG,timing` lines are absent from this capture (likely ring pressure / snapshot not emitted during starvation). `PERF,playback_stop` shows `save_state fail=5`, `display[s=213…` (truncated).

---

## 9. Stage 0a — Pre-code design checkpoint (required)

**Status:** Complete — 2026-08-11. Stage 1 may proceed within the gate and file scope below.

### Decision A — capture-layer composition without full-preview copying

Continuous MIDI can make `captureRevisionChanged` true on every 30 ms display frame. Therefore “once per revision” does not by itself remove O(N)-per-frame work.

**Selected: synchronize capture deltas into the existing partitioned `liveDisplayNotes` vector.**

Reuse the established committed/capture boundary:

```text
liveDisplayNotes
├── [0, liveDisplayCacheCommittedNoteCount_)     committed
└── [liveDisplayCacheCommittedNoteCount_, base)  capturePreview mirror
    └── temporary per-frame tail/head segments after base
```

On a nominal capture revision:

1. append only new `capturePreview.notes` suffix rows;
2. apply only preview-note indices changed by note-off / wrap metadata updates;
3. retain the committed prefix unless its own revision, context, or display window changed;
4. remove prior temporary tail/head rows and reapply only active open-note tails.

`CapturePreview` remains the Loop-owned capture display representation. Add session-bounded change bookkeeping to that existing type; do not infer changed rows by rescanning the whole preview. A cold preview rebuild may replace the capture suffix once and must carry an explicit replacement signal.

**Rejected:**

- Separate committed/capture draw APIs: `drawPianoRoll`, `drawNoteInfo`, and capture telemetry all consume one `DisplayNoteVec`; changing all paint/read APIs is broader than this correction.
- Revision gating alone: continuous MIDI changes the revision every frame and still permits a full preview copy every frame.
- Dirty-bar-only synchronization: `capturePreview.dirtyBars` does not identify exact changed note rows and long held notes can dirty an unbounded bar span.

### Decision B — bounded tails with exact wrap semantics

The current tail path scans/copies the full capture event stream because `isWrapHeldOpenNote` and `findPreferredWrapHeadOffTick` require channel-aware event history. `capturePreview` currently lacks channel and preferred wrap-pairing information.

**Selected: extend `CapturePreview` with append-maintained, channel-aware open-note and wrap-pair metadata.**

Use sidecar state aligned with capture preview note indices; do **not** add channel to the general `DisplayNote` type. The sidecar records only display-tail inputs:

- preview note index;
- channel + pitch identity;
- open/closed state;
- wrap-held state;
- preferred head-off presence/tick.

`applyCaptureEventToPreview` updates the affected channel/pitch state on successful append. Normal append work must not scan/copy the full capture session; cold `rebuildCapturePreviewFromStore` may reconstruct all sidecar state. `applyCapturePlayheadTails` consumes the precomputed sidecar and remains proportional to active open notes per frame.

Native parity fixtures must prove the metadata path produces the same tail/head segments as the existing event-history path for:

- head-off at tick 0;
- same pitch on different channels;
- intervening later tail note-on;
- synthetic loop-end off before head-off;
- live playhead crossing loop start;
- multiple simultaneous open notes.

**Rejected:**

- Store-native scan without copy: removes allocation but remains O(session events × open notes) per frame.
- `Track::pendingNotes` / playback active-note state: wrong owner and does not contain preferred wrap pairing.
- `liveDisplayCacheOpenNotes`: pitch-only merged-buffer state and not a capture-preview authority.

### Post-record PLAYING handoff

The same capture shows no completed display frame from long RECORDING through 15 seconds of post-stop PLAYING. `refreshViewportAfterRecordStop` clears `liveDisplayNotes` while the committed `visualCache` is dirty/empty; continuous deferred-save work then removes the warm-cache fallback.

Stage 1b must preserve the last valid live-capture frame across the record-stop display handoff and let `resolveDisplayNotesCommitted` replace it with the bounded committed window. Do **not** add a synchronous full or 16-bar representation rebuild to `Track::stopRecording`; reuse stale-while-revalidate and the existing windowed resolver.

### Checkpoint output

| Question | Decision |
|----------|----------|
| **Owner module** | `Loop` owns `CapturePreview`; `DisplayManager::resolveDisplayNotesLiveCapture` owns composition; `DisplayManager::resolveDisplayNotesCommitted` owns post-stop paint |
| **Primary invariant** | Capture arrival changes only the capture suffix; committed display data remains reusable; per-frame work is independent of capture-session length |
| **Ownership change?** | **NO** |
| **State transition change?** | **NO** — no capture, stop, transport, or persistence FSM edits |
| **Behavior-preserving?** | **YES** — same note/tail/head pixels and wrap pairing |
| **Reuse decision** | **YES** — extend `CapturePreview`, `liveDisplayCacheCommittedNoteCount_`, `applyCaptureEventToPreview`, and stale-while-revalidate |
| **Phase scope** | Stage 1 / 1a / 1b display correction only |

**Exact firmware scope:**

- [`include/VisualCache.h`](../../include/VisualCache.h) — capture preview change + tail sidecars;
- [`include/DisplayManager.h`](../../include/DisplayManager.h) — capture sync cursors / temporary-tail boundary;
- [`include/LoopInternal.h`](../../include/LoopInternal.h) and [`include/DisplayManagerInternal.h`](../../include/DisplayManagerInternal.h) — existing-owner helper signatures only;
- [`LoopInternalColdHelpers.cpp`](../../src/Loop/LoopInternalColdHelpers.cpp) and [`LoopCapture.cpp`](../../src/Loop/LoopCapture.cpp) — incremental preview metadata + cold replacement;
- [`DisplayNoteResolveLiveCapture.cpp`](../../src/DisplayManager/DisplayNoteResolveLiveCapture.cpp) — delta compose and metadata tails;
- [`DisplayCacheLifecycle.cpp`](../../src/DisplayManager/DisplayCacheLifecycle.cpp) and [`DisplayNoteResolveCommitted.cpp`](../../src/DisplayManager/DisplayNoteResolveCommitted.cpp) — bounded post-record stale handoff;
- [`TrackCaptureInput.cpp`](../../src/Track/TrackCaptureInput.cpp) — remove duplicate display revision bumps only;
- Stage 2 native capture-preview / tail parity / post-stop display fixtures.

**Stage 0 baseline:** the capture contains no `DIAG,counter` / `DIAG,timing` snapshot because the capture ring overflowed during starvation. The usable pre-fix baseline is the 259-second `DFRAME` gap, zero post-record PLAYING frames, and `RING,overflow`; post-fix acceptance additionally requires `DisplayCaptureFullGather == 0` during steady capture and bounded timing maxima.

### Bugfix architecture checkpoint

1. **Ownership:** no change. `Loop` continues to create/store/update capture preview data; `DisplayManager` continues to compose and draw it.
2. **State transitions:** no change. The correction changes derived-representation invalidation and incremental synchronization only.

No architecture reassessment or new `DEC-###` is required. This completes the existing DEC-016 / M6 display policy rather than introducing a new runtime model.

---

## 10. Stage 1 — Layered display invalidation (P0)

**File:** [`DisplayNoteResolveLiveCapture.cpp`](../../src/DisplayManager/DisplayNoteResolveLiveCapture.cpp)

Replace global `eventsAdded` invalidation with layer-specific triggers:

```text
capture event           → capture layer update (append path — already exists)
capture revision        → capture-layer recomposition only (once per revision)
committed revision      → committed layer refresh
explicit visual invalidate → committed layer refresh
window/context change   → appropriate layer refresh
playhead movement       → tails / per-frame update (bounded — R4)
```

Do **not** implement as `remove eventsAdded` alone.

### Stage 1 — single-owner capture revision (R2)

`captureDisplayRevision` must be incremented **exactly once** for each successful live capture append. Remove the duplicate increment in [`TrackCaptureInput.cpp`](../../src/Track/TrackCaptureInput.cpp) (`recordMidiEvents`, `appendCaptureNoteOffAtPhase`) — owner remains [`Loop::appendCaptureEvent`](../../src/Loop/LoopCapture.cpp).

### Stage 1 — hot-path audit requirements

| Hot path | Requirement |
|----------|-------------|
| Nominal incremental branch | Must not full-reconstruct display **or copy all preview notes** when each frame observes a capture revision (R3) |
| Overdub tails | Must not copy/scan the entire store every frame, while preserving channel-aware wrap pairing selected in Stage 0a (R4) |
| Full rebuild branch | No `gatherCommittedEventsWithCapture` on capture-event arrival when `visualCache` valid |

---

## 11. Stage 1a — Overdub with populated visual cache (P0)

When `track.isOverdubbing()` and committed visual cache is valid, compose:

```text
loop.visualCache + loop.capturePreview + playhead tails
```

New capture events must:

- update `capturePreview`
- trigger capture-layer recomposition where required
- **not** invalidate committed visual cache
- **not** invoke `gatherCommittedEventsWithCapture()` merely because an event was added

**Hot-path invariant:**

```text
capture event → capture preview changes → committed revision unchanged → no committed-layer rebuild
```

---

## 12. Cold committed layer / long loop

When committed visual cache is cold and long loop requires committed rebuild: use `rebuildDisplayNotesInWindow` where appropriate. Do not reconstruct full committed pass stack on capture arrival.

---

## 13. Stage 1b — Long live record (P0)

For `track.isRecording()`: prefer `capturePreview + syncDetailedPaintWindow + playhead tails`.

Avoid repeated `copySortedCaptureEvents + reconstructDisplayNotes` per captured event.

> Long live recording must not perform O(N) full display reconstruction for every newly captured note/event.

Recording and overdubbing remain **separately testable** (composition differs).

---

## 14. Stage 1c — M6 display / LED audit

Audit [`DisplayNoteResolveCommitted.cpp`](../../src/DisplayManager/DisplayNoteResolveCommitted.cpp) and [`DisplayNoteResolve.cpp`](../../src/DisplayManager/DisplayNoteResolve.cpp) for `captureActive()` PLAYING paths using `mergeMaterializedPassesWithCapture` — replace with chunk merge + capture layer + revision-gated buffer where supported.

Audit [`MidiLedManager.cpp`](../../src/MidiLedManager.cpp): when `visualCacheDirty`, use `mergeActiveCapturePasses` not full materialize.

No ownership or transport behavior changes.

### Stage 1 implementation result — 2026-08-11

- `resolveDisplayNotesLiveCapture` keeps a reusable committed prefix and delta-syncs only new or changed `capturePreview` rows.
- `CapturePreview` maintains channel-aware note state and active-note indices; playhead tails iterate active notes without copying or scanning the capture store.
- `Loop::appendCaptureEvent` is the sole owner of `captureDisplayRevision` increments.
- `refreshViewportAfterRecordStop` preserves the last live frame until the committed window replaces it.
- Stage 1c audit found no remaining per-frame full capture gather: committed display capture paths dispatch through `resolveDisplayNotesLiveCapture`; the LED merge remains bar-rate and uses chunk-reference `gatherCommittedEventsWithCapture`.
- Native suite passed; capture-preview parity fixtures cover channel separation, preferred later tails, tick-zero heads, loop-end synthetic close followed by head-off, and simultaneous opens.
- `teensy41-capture-serial` build passed. The 118-bar manual display/latency gate remains required.

---

## 15. Stage 2 — Native regression tests

```bash
pio test -e native
```

**Populated-cache overdub:** given valid committed `visualCache` + active overdub + **many** capture events, assert:

- capture preview changes
- committed visual layer remains valid
- committed visual revision does not change
- committed layer is not rebuilt
- full committed+capture gather is not invoked
- no full `capturePreview.notes` copy occurs on each capture revision
- wrap-held and preferred wrap-head pairing remain identical without full-store per-frame copy

Verify **layer behavior**, not merely absence of one function call. Do not satisfy test by replacing `gatherCommittedEventsWithCapture()` with equivalent full reconstruction under another helper.

---

## 16. Stage 3 — Manual HITL verification

1. Record ~118 bars on slot 4 → stop/commit → play → overdub ≥45 bars → continue past prior freeze interval.

Expected: OLED updates, playhead/window advances, buttons responsive, transport normal, no sustained display-induced starvation.

---

## 17. Verification telemetry

During active record/overdub:

- `#CAP,DFRAME` ~once per second
- Playhead/window past ~42-bar point
- `DisplayCaptureFullGather == 0` during steady overdub
- `DisplayResolveMaxUs` / `DisplayResolveOverBudgetCount` within budget
- No sustained `RING,overflow` from display path
- Button responsiveness regression from `66ef4f7` remains intact

---

## 18. Stage 4 — Playback window / M6 Phase 1 (P0)

**File:** [`TrackPlaybackWindowBuild.cpp`](../../src/Track/TrackPlaybackWindowBuild.cpp)

Isolated from display fix. Capture-active: chunk merge + capture layer (not `mergeMaterializedPassesWithCapture` on hot path). Non-capture background PLAYING: `gatherPublishedFlatForDerivedView`.

Playback MIDI correct before send. **No** `playMidiEventsForSlot` / `projectionCycleStartTick` changes.

---

## 19. Stage 5 — Memory / persistence pressure (P1, separate commit)

Does **not** block display-fix acceptance (Stages 1–3). Display recovery does **not** prove persistence resolved.

### 5a — Capture / storage integrity

From prior captures (`Capture append failed`, `non-canonical storage`):

- Chunk-pool headroom under long open record
- Layered storage growth
- Append failure handling
- Canonicalization at overdub stop

### 5b — Boot deferred save + clear gating

Evidence: [`session_20260811_004608.log`](../../captures/session_20260811_004608.log)

| Finding | Log / code anchor |
|---------|-------------------|
| Boot queues 8 slot restores before interactive ready | `[StorageManager] Queuing boot playback loop slot restore 8 pending` — [`CurrentSetBootLoad.cpp`](../../src/StorageManager/CurrentSetBootLoad.cpp) |
| `SAVE,pending` from early boot | `#CAP,7292325,SAVE,pending,0` and recurring `already_pending` |
| Clear aborted while deferred work outstanding | `Clear waiting for deferred save completion` / `Clear aborted: could not complete deferred save first` — [`MidiButtonActions::handleClearTrack`](../../src/MidiButtonActions.cpp) |
| Sync drain cannot preempt pending writer | `PERS,…,sync_drain_already_pending` — [`SyncDrainSaveState.cpp`](../../src/StorageManager/SyncDrainSaveState.cpp) |
| `save_state` failures under pressure | `PERF,playback_stop,…,save_state[s=5,…,fail=5]` |
| Display deferral coupling | [`shouldDeferHeavyDisplayRebuild()`](../../src/DisplayManager/DisplayColdHelpers.cpp) keys off `hasDeferredSaveWork()` — continuous pending save extends PLAYING display deferral after record stop |

**Investigate / fix options** (design in Stage 5 — not Stage 1):

- Boot UX: clear allowed before background slot restore completes, or faster `bootInteractiveReady` without blocking clear on unrelated tracks
- Clear policy: slot clear without full workspace sync drain when only selected slot mutates; or bounded wait with user-visible “save draining” vs hard abort
- Writer queue: `already_pending` / `sync_drain_already_pending` interaction when boot restore + user clear overlap
- Whether boot restore should admit persistence without parking `SAVE,pending` across the whole session

**Primary files:** `StorageManager/*` (boot load, deferred save, sync drain), `MidiButtonActions.cpp`, `LoadLoopJob.cpp`, `DisplayColdHelpers.cpp` (deferral coupling — document or relax only if persistence gating fixed).

### Stage 5 acceptance (persistence track)

- Clear selected slot succeeds within bounded time after boot without requiring all 8 background restores to finish first (or explicit product policy documented if intentional)
- No sustained `SAVE,pending` + `save_state fail` through normal record/stop/play
- Append/canonicalization findings from 5a addressed or explicitly deferred with log anchor

---

## 20. Acceptance criteria

**Functional:** long record/overdub update display; playhead/window progresses; buttons responsive; transport and playback MIDI correct.

**Architectural:** incremental capture layer; valid committed cache not invalidated by capture arrival during overdub; independent committed revisions may still invalidate committed layer; capture revision does not auto-trigger committed rebuild; no full-stack gather on capture arrival; ownership unchanged.

**Performance:** `DisplayCaptureFullGather == 0` steady overdub; no equivalent full reconstruction; no full-preview copy on the nominal capture-revision path (R3); tails without per-frame full-store copy/scan and with unchanged wrap pairing (R4); resolve within budget; no sustained display-induced `RING,overflow`.

**Storage / persistence:** Stage 5 separate commit — append/canonicalization (5a) + boot clear vs deferred save (5b); not implicitly closed by display fix.

---

## 21. Out of scope

Capture/overdub FSM; record-button → overdub; capture ownership; commit/stop semantics; playback-wrap; `playMidiEventsForSlot`; `projectionCycleStartTick`; reverted wrap fix; `#CAP,DIAG,heap`; memory/persistence findings as part of display fix (Stage 5); **window-bounded capture projection** (`capturePreviewWindow` — future optimization only).

---

## 22. Final architecture contract

> During active overdub, capture-event arrival updates only the live capture representation. It must not invalidate or reconstruct an otherwise valid committed visual representation.

```text
ACTIVE OVERDUB

capture event → applyCaptureEventToPreview → capture layer changes
    ├─► committed visualCache remains valid
    ▼
display composition (visualCache + capturePreview + playhead tails) → OLED
```

---

## capturePreview audit (2026-08-11)

Audit of whether `capturePreview` is already incrementally maintained and window-bounded — informs Stages 1 / 1a / 1b.

### Incremental maintenance — **yes on append hot path**

| Path | Behavior |
|------|----------|
| [`Loop::appendCaptureEvent`](../../src/Loop/LoopCapture.cpp) | After successful `capture.store.append`, calls `applyCaptureEventToPreview(capturePreview, evt, …)` and `++captureDisplayRevision` |
| [`Track::recordMidiEvents`](../../src/Track/TrackCaptureInput.cpp) | All live record/overdub MIDI → `appendCaptureEvent` |
| [`applyCaptureEventToPreview`](../../src/Loop/LoopInternalColdHelpers.cpp) | Note-on: `push_back` display note + `markPreviewSpan`; note-off: reverse scan for open preview note (pitch only, `endTick == startTick`) and set `endTick` |

`capturePreview` is **already wired** on the capture append hot path. Architecture docs ([`DerivedViews.md`](../../docs/Authority/Architecture/DerivedViews.md), M6 plan) correctly describe incremental preview maintenance.

### Window-bounded capture projection — **future optimization (out of scope)**

| Aspect | Finding |
|--------|---------|
| `capturePreview.notes` | Session-bounded: all capture-session display notes — O(session notes) **storage** is acceptable |
| `capturePreview.dirtyBars` | Updated but **no consumer** for display windowing |
| Display compose | Full-vector copy on **recomposition** — acceptable if revision-gated, not per frame |
| Windowing today | **Committed** layer only (`syncDetailedPaintWindow`, `rebuildDisplayNotesInWindow`, `visualCache` bar-slices) |

Do **not** claim `capturePreview` is windowed. Do **not** add `capturePreviewWindow` in this fix unless profiling proves full-preview compose exceeds display budget.

**Implication:** Stage 1 is display invalidation/composition policy. Capture-layer recomposition must be **revision-aware and delta/layer bounded**; revision gating alone is insufficient under continuous input. Per-frame tail work must preserve wrap semantics without a full-store copy/scan (R3, R4).

### Full-rebuild preview paths (cold / edge)

| Path | Cost |
|------|------|
| [`rebuildCapturePreviewFromStore`](../../src/Loop/LoopInternalColdHelpers.cpp) | `copyEventsTo` + full `reconstructDisplayNotes` — used by [`removeOpenCaptureNoteOn`](../../src/Loop/LoopCapture.cpp) (overlap restore on overdub stop) |
| Display recording fallback | If `capturePreview` empty but `capture.store` non-empty → `copySortedCaptureEvents` + `reconstructDisplayNotes` ([`DisplayNoteResolveLiveCapture.cpp`](../../src/DisplayManager/DisplayNoteResolveLiveCapture.cpp)) |

These are cold/edge paths — not steady overdub per-event hot path, but long-record fallback can hit O(N) if preview and store diverge.

### Revision signals — **duplicate bump defect**

Display gating uses `loop.captureDisplayRevision`, **not** `capturePreview.revision` (`capturePreview.revision` is bumped but **never read**).

`captureDisplayRevision` is incremented **twice** per successful live MIDI append:

1. inside `appendCaptureEvent` ([`LoopCapture.cpp`](../../src/Loop/LoopCapture.cpp))
2. again in `recordMidiEvents` / `appendCaptureNoteOffAtPhase` ([`TrackCaptureInput.cpp`](../../src/Track/TrackCaptureInput.cpp))

This amplifies `captureRevisionChanged` in `needsFullLiveRebuild` and should be corrected in Stage 1 (single owner for display revision bump on append).

### Display resolver still ignores incremental preview (root defect)

Even with incremental `capturePreview`, [`resolveDisplayNotesLiveCapture`](../../src/DisplayManager/DisplayNoteResolveLiveCapture.cpp) currently:

- treats `eventsAdded` and `captureRevisionChanged` as **global** `needsFullLiveRebuild` triggers
- on overdub full rebuild with populated `visualCache`, calls `gatherCommittedEventsWithCapture` before composing preview
- calls `rebuildLiveDisplayNotes()` even in the `else` (“incremental”) branch
- on overdub with open preview notes, calls `copySortedCaptureEvents` every frame for `applyCapturePlayheadTails`

**Conclusion:** `capturePreview` incremental maintenance **exists and is sufficient** for the capture layer. The freeze fix is **display invalidation and compose policy** — not new capture-preview architecture. Stage 1 must: (1) single-owner `captureDisplayRevision` (R2); (2) `captureRevisionChanged` → capture-layer recomposition only (R1); (3) no full `rebuildLiveDisplayNotes` on nominal incremental path (R3); (4) no per-frame `copySortedCaptureEvents` for tails (R4). Stage 1b ensures recording uses preview authoritatively on hot path.

### Test coverage gap

No native test asserts `applyCaptureEventToPreview` incremental behavior or layer-specific invalidation. Stage 2 tests should cover preview updates + committed stability across **many** events.

---

## Pre-implementation review

### Ready

- Log evidence and root-cause chain documented
- M6 Phase 2 scope pre-approved
- Architecture gate: derived-display only

### Resolved

| Topic | Decision |
|-------|----------|
| Fix shape | Display invalidation/composition correction — not new capture-preview architecture |
| Three bounded ops | Append (exists); recomposition once/revision; bounded tails/frame |
| `captureRevisionChanged` | Capture-layer recomposition only (R1) — not committed invalidation |
| Revision owner | Single `captureDisplayRevision` bump per append (R2) |
| Hot-path audit | No full reconstruction or preview copy on incremental path (R3); no per-frame store copy/scan for tails, with exact wrap behavior (R4) |
| `capturePreview` | Incremental, session-bounded — acceptable; window projection = future only |
| Populated `visualCache` | Enables cheap committed path |
| Composition | Delta-sync capture suffix into partitioned `liveDisplayNotes`; exact changed-row sidecar; no dual-vector paint API |
| Tails | Append-maintained channel/wrap sidecar on `CapturePreview`; per-frame work proportional to open notes |
| Post-record paint | Preserve last valid live frame; replace through bounded committed window; no stop-path rebuild |
| Approach ranking | visualCache + capturePreview + tails ✅; windowed capture ⚠️ premature |
| Stage numbering | 0 instrumentation → 0a design checkpoint → 1/1a/1b/1c display → 2 native → 3 manual → 4 playback → 5 memory |
| Storage / persistence | Stage 5 separate commit — 5a append/canonicalization; 5b boot clear + deferred save |

### Proceed?

**YES for Stage 1 firmware.**

- Decision A and Decision B are selected above.
- Architecture checkpoint: ownership **NO**, state transitions **NO**.
- Stage 1 must stay within the listed files and behavior-preserving contracts.
- Stop and reopen design only if native parity proves append-maintained metadata cannot reproduce the existing wrap pairing contract without a full capture-session scan.
