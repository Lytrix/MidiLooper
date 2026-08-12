# Long record onset display freeze

**Status:** RC-A fix shipped — **interim device PASS** [`013747`](../../captures/session_20260812_013747.log); full 101-bar re-verify pending  
**Priority:** P0  
**Evidence:** [`session_20260812_012342.log`](../../captures/session_20260812_012342.log)  
**Parent:** [`long_overdub_display_freeze_bugfix.md`](long_overdub_display_freeze_bugfix.md) (Stage 1 layered capture — trust unless broken)

---

## Debugging boundary

```
frozen: wrap duplicate / G2 overdubSourceView OLED PASS 010000
frozen: Stage 5a pool_alloc proof (abandoned 2026-08-12)
trust unless broken: Stage 1 layered live capture (eventsAdded diagnostic only)
→ current: runDeferredLoadAndDisplayFrame OLED gating during capture + CAP telemetry class
```

---

## Interim verification (`013747` — post RC-A fix)

| Signal | `012342` (fail) | `013747` (pass) |
|--------|-----------------|-----------------|
| User OLED | Frozen at overdub bar 2 | **Live** through record + overdub |
| `#CAP,DISP` during capture | 4 total; no mid-session `wStart` advance | **24** lines; `wStart` **1060→1112** during `OVERDUBBING` |
| Record shape | 101 bars (`77568`) | ~58 bars after truncate (`44544`); prior slot had 101-bar content |
| Overdub passes | 1 | **4** (`undo_entries` 20→22) |
| `#CAP,DFRAME` on serial | 224 s gap (misleading — RC-B) | 165 s gap during transport (same telemetry class; **not** fail criterion) |
| Append pressure | 0× | 0× |

**Pass criteria met (interim):** OLED advanced; `#CAP,DISP` shows rolling window during overdub. **Pending:** user retry at full **101-bar** record + same-notes overdub shape matching `012342`.

---

## Evidence (`012342` — pre-fix)

| Signal | Finding |
|--------|---------|
| Loop | 101 bars (`length=77568`) |
| Last `#CAP,DFRAME` | `1724936553` — **before** `Recording started @ tick 0` (1725.161 s) |
| `#CAP,DFRAME` gap | ~224 s until post-stop flush — user saw freeze at overdub bar 2; onset aligns with **RECORDING start**, not overdub entry |
| Transport | Overdub tick 712→8058; stop OK — not MCU halt |
| Chunk pressure | 0× `append,deny`; `loop_chunks=14` at stop — not Stage 5a class |
| `#CAP` during gap | No `#CAP` on serial during RECORD/PLAY/OVERDUB (timing-critical flush policy — see RC-B below) |
| Overdub start | `PERF,overdub_start` max 247 ms — secondary; not onset |

---

## RC-A — OLED skipped during capture + focus slot load (primary)

**Owner:** `runDeferredLoadAndDisplayFrame` in [`main.cpp`](../../src/main.cpp)

When `focusSlotRestoreWork && SlotLoadSession::isActive()`, both display branches set `skipFocusLoad` and **skip** `displayManager.update()` entirely.

`captureActive` blocks **background** slot restore (`allowDeferredSlotRestore`) but **not** focus-slot `DeferredJobScheduler::runFrame`. Focus load can stay active across RECORDING/OVERDUBBING while OLED is suppressed.

**Onset correlation:** last `DFRAME` while `ARMED` (`captureActive == false`); silence begins when `TRACK_RECORDING` sets `captureActive == true` with focus load session still active.

**Invariant after fix:** OLED must refresh every `DISPLAY_UPDATE_INTERVAL` while any track is recording or overdubbing, even if focus `SlotLoadSession` is active.

**Fix (shipped in tree):** gate `skipFocusLoad` with `!captureActive`:

```cpp
const bool skipFocusLoad =
    skipDisplayAfterFocusCommit ||
    (focusSlotRestoreWork && SlotLoadSession::isActive() && !captureActive);
```

Architecture checkpoint: **ownership NO**, **transitions NO** — scheduling-only change in existing owner.

---

## RC-B — `#CAP,DFRAME` silence during transport (telemetry class)

**Owner:** `DebugSessionCapture::flushCaptureBuffer` — [`DebugSessionCapture.cpp`](../../src/Utils/DebugSessionCapture.cpp)

During RECORDING/PLAYING/OVERDUBBING, `SC_CAPTURE_FLUSH(8)` **drops** ring records without USB `Serial` write. Missing `DFRAME` in capture logs during transport is **not** sufficient proof that `DisplayManager::update` stopped — verify OLED behavior separately.

Post-stop `RING,overflow` + bulk `SEVT` flush is expected drain after transport idle.

---

## RC-C — promoted (timing integrity; not OLED-only)

**Status:** Architecture investigation + **review amendments** applied — implementation gated on user decisions.  
**Evidence:** [`session_20260812_104104.log`](../../captures/session_20260812_104104.log) — external sequencer stable; looper BPM 56–258 at record tail; **heard notes out of time**; PLAYING after stop stable. DisplayResolve max ≈281 ms; DisplayUpdate max ≈317 ms; `DisplayFullRebuild=4`.

**Architecture authority:** [`realtime_incremental_work_capture_overdub_architecture.md`](realtime_incremental_work_capture_overdub_architecture.md)

Narrow RC-C: MIDI Clock FIFO with notes; eliminate full-loop visual reconstruction on RECORD/OVERDUB paths; **incremental/windowed materialization** (extend `rebuildVisualCacheIdleSlice`); last-valid-frame is **safety only**. OVERDUB work must be delta/window proportional. Broader persistence/overview/reclaim follow-up in that doc.

---

## Verification

### Device (required)

1. Flash `teensy41-capture-serial` with RC-A fix.
2. Reproduce shape: long RECORD (101-bar class) on loaded slot with undo history; PLAY; overdub same notes.
3. **Pass:** OLED playhead/notes advance through RECORD and overdub; no bar-2 freeze.
4. **Pass:** `#CAP,DISP` or periodic `#CAP,DFRAME` after transport stops (during transport DFRAME may stay sparse — RC-B).

### Parse (post-capture)

```bash
SESSION=captures/session_YYYYMMDD_HHMMSS.log
rg 'DFRAME|DISP,|Recording started|Overdub session|ST,Track' "$SESSION"
rg 'append,deny|Capture append failed' "$SESSION"   # expect 0 for this bug class
```

### Native

```bash
pio test -e native
```

---

## Acceptance

- [x] RC named with owner + invariant
- [x] Minimal fix in `main.cpp` (RC-A)
- [x] Device interim PASS — [`013747`](../../captures/session_20260812_013747.log) (58-bar record, 4 overdubs; user confirmed OLED live)
- [ ] Full **101-bar** re-run vs `012342` shape (user retry pending)
- [x] `pio test -e native` — **1016/1016** (2026-08-12)
