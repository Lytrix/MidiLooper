# Boot load sequence

Agent-oriented map of cold-boot ordering: SD workspace load, deferred slot restore, USB Host (DROID), and display/LED refresh. Read before changing `main.cpp` setup, `StorageManager::loadState`, `beginUsbHost`, or boot-time LED side effects.

**Related:** [`DEFERRED_RUNTIME_PERSISTENCE.md`](DEFERRED_RUNTIME_PERSISTENCE.md) (SD layout, SAVE tokens), [`RUNTIME_STORAGE_AND_PERSISTENCE.md`](RUNTIME_STORAGE_AND_PERSISTENCE.md) (persistence invariants), [`DROID_MOTORFADER_PITCHBEND.md`](DROID_MOTORFADER_PITCHBEND.md) (USB host LED pacing).

---

## Why ordering matters

Teensy 4.1 shares the SDIO bus between the built-in SD slot and USB Host. Cold boot with DROID already connected previously hung when:

1. `usbHost.begin()` ran while **deferred loop slot restore** was still reading SD.
2. `clearLeds()` / `forceLedUpdate()` flooded ch15 USB Host MIDI during those SDIO reads.
3. USB enumeration could not progress because `usbHost.Task()` was not pumped during long SD slices.

Disconnect/reconnect worked because enumeration then ran in `loop()` with no SDIO contention. Fix: defer USB Host and DROID LED refresh until **audible** queued slot restores finish (non-audible SD slots stay HEADER_READY until explicitly requested).

---

## Phase sequence

```mermaid
sequenceDiagram
  participant Setup as setup()
  participant SD as StorageManager
  participant Loop as loop()
  participant USB as MidiHandler

  Setup->>SD: loadState (sync bundle + manifest scan)
  SD-->>Setup: queue audible slot restores only
  Note over Setup: BOOT,load,ok - USB host NOT started; title stays
  Setup->>Setup: LOOP_EDIT to USB device MIDI (no finishBootSetup yet)

  loop->>SD: while-drain audible processDeferredLoopSlotRestore
  Note over Loop: OSTINATIX title; audible Commit; no piano roll yet

  Loop->>Loop: finishBootSetup when restore queue empty
  Loop->>USB: beginUsbHost
  Note over USB: BOOT,usb_host,begin
  USB->>USB: Task() poll up to 300 ms (cold-plug enumerate)
  Loop->>Loop: clearLeds, LOOP_EDIT note 100 flash, onBootSlotLoadComplete
  Note over Loop: first piano-roll resolve is the next DisplayManager::update cadence, not boot_commit
```

### `setup()` (blocking, no USB Host)

| Step | Owner | Notes |
|------|-------|-------|
| Pool + loop RAM alloc | `MemoryPool`, `TrackManager` | Before USB Host heap use |
| `midiHandler.setup()` | DIN MIDI only | `BOOT,usb_host,deferred` |
| Early OLED + OSTINATIX title | `DisplayManager::beginBootOled` / `drawBootScreen` | Title held until boot playback drain |
| `looper.setup()` → `loadState` | `StorageManager` | Bundle read, manifest scan, hydrate metadata all payloads, **enqueue boot playback set** (`isBootPlaybackSlot` = per-track union of file selected + file active) |
| `sendEditSessionChange(Loop)` | `EditManager` | PC + fader feedback to **USB device** MIDI only |

`bootLoadInProgress_` suppresses `forceLedUpdate` during `loadState` only. It is cleared before `setup()` returns.

### `loop()` (deferred job steps)

| Step | Gate | Notes |
|------|-------|-------|
| Boot slot restore | `bootSlotLoadRefreshPending` && idle && turn free | **Boot playback set only** — `DeferredJobScheduler::runFrame(BootTitleRestoreUs)`; demote/park **disabled** until title clears. `setBootTitleLoadDrain(true)` still prevents LoadLoopJob park during drain; idle visual slices run. |
| Idle maintenance | focus track first while title drain is armed | `161804`: 4-bar `idle_gather` builds the 68-bar focus cache (`slice_clean` 2638 notes at 10.9 s) before `boot_commit`. Do not call `prewarmOverdubSourceSpanCache` in that `slice_clean` turn (`160519`). After `boot_commit`, a later clean-cache STOPPED turn prewarms the source-span cache. |
| Post-boot slot restore | after ready + holdoff; PLAYING allows focus/`LoadLoopJob` only | `FocusRestoreUs` / `BackgroundRestoreUs`; demote parks one in-flight job |
| Deferred undo hydrate | same | Reads undo bodies from bundle |
| **finishBootSetup + USB Host** | `bootInteractiveReady()` and focus visual cache clean | `161804`: USB 11.484 s, first `DISP` visual=2638, `boot_commit=288557`. Do not call `invalidateForSlotChange` from `onBootSlotLoadComplete`. |
| First-commit playback prewarm | same load-frame as first slot commit | `154353` `first_commit=128693` during boot. Deferring it until after the first piano roll hung `160519`. |
| DROID LED refresh | after `beginUsbHost` | `clearLeds`, note 100 flash, `onBootSlotLoadComplete` |
| Mid-pass persistence | idle transport | `PERS,mid_pass` during/after restore |

---

## Boot telemetry (`BOOT,*` / `#CAP,BOOT,*`)

Emitted via `emitBootMilestone` (`include/Utils/BootTelemetry.h`). Use capture-serial (`teensy41-capture-serial`) for HITL.

| Milestone | When | Pass criterion |
|-----------|------|----------------|
| `usb_host,deferred` | After `midiHandler.setup()` | Always first |
| `load,start` | `StorageManager::loadState` entry | |
| `ram1,<bytes>` | After loop alloc / post-load | Internal heap free |
| `scan,start` … `scan,done` | 8×8 manifest exists-only scan | `t0`–`t7` without hang |
| `load,ok` | Workspace bundle parsed | Before USB host |
| `heap,<bytes>` | End of `setup()` | |
| `usb_host,begin` | **After audible deferred slot restore** | Must not appear before audible restore lines |

**Cold-boot + DROID connected (regression):** [`session_20260709_183527.log`](../../captures/session_20260709_183527.log) — `BOOT,usb_host,begin` at line 173 **after** deferred restore `0/0`–`4/0`; continuous `DFRAME` afterward.

**Earlier failure pattern:** `BOOT,usb_host,begin` immediately after `BOOT,load,ok`, LED flood interleaved with `Deferred restore loop slot`, then hang or sparse `DFRAME` ([`session_20260709_182947.log`](../../captures/session_20260709_182947.log) before fix).

**Audible-first baseline to beat:** [`session_20260718_020628.log`](../../captures/session_20260718_020628.log) — ~10.5 s `load_ok` → `usb_host,begin` for 26-slot full drain.

---

## USB Host rules (do not regress)

1. **Do not** call `usbHost.begin()` in `setup()` before audible deferred slot restore completes.
2. **Do not** call `clearLeds()` / `forceLedUpdate()` from `setup()` — run from the boot-slot-complete gate (or `onBootSlotLoadComplete`).
3. **Do** pump `usbHost.Task()` after `begin()` for up to 300 ms when DROID is cold-plugged at power-on (`MidiHandler::beginUsbHost`).
4. **Do** keep ch15 LED outbound queued and paced (`MidiConfig::DroidUsbHost`) — see DROID guide.
5. **Do not** clear the boot title (`finishBootSetup`) until `bootInteractiveReady()` — piano roll only after audible Commit.
6. Arm `setBootTitleLoadDrain(true)` in `setup()` so the first `loop()` turn cannot run STOPPED idle gather before title clear ([`145953`](../../captures/session_20260821_145953.log) `idle_gather=136084342`). Do not blank the OLED in `finishBootSetup()` — [`151555`](../../captures/session_20260821_151555.log) stayed black until the first piano-roll paint.

---

## Memory stabilization

After a long loop load, `stabilizeBootMemoryAfterLoad()` may clear undo stacks when internal heap is below reserve (loop data intact). See [`DEFERRED_RUNTIME_PERSISTENCE.md`](DEFERRED_RUNTIME_PERSISTENCE.md) § Boot after long save.

---

## Key files

| File | Role |
|------|------|
| `src/main.cpp` | Setup order; `bootSlotLoadRefreshPending` gate; load frame before display |
| `src/StorageManager.cpp` | `stepSubmittedLoadJobs` (via `DeferredJobScheduler::runFrame`), `LoadLoopJob` active+parked, `bootInteractiveReady` |
| `include/LoadLoopBudget.h` | Focus / Background / BootTitle µs budgets |
| `include/Utils/BootLoopSlotRestore.h` | `isBootPlaybackSlot` / boot restore priority |
| `src/MidiHandler.cpp` | `beginUsbHost`, enumeration poll |
| `src/TrackManager.cpp` | `beginBootLoad` / `onBootSlotLoadComplete` |
| `src/DisplayManager.cpp` | `drawBootScreen` / `finishBootSetup` title gate |
| `include/Utils/BootTelemetry.h` | `BOOT,*` milestones |

**History:** `50ad01b` deferred USB Host until after sync `loadState`; follow-up defers until deferred slot restore queue drains (SDIO + enumeration isolation). Full-set drain under title (`af1227c`) superseded for interactive ready by **boot playback set** enqueue (`feature/deferred-lazy-load`, OpenSpec `unified-commit-lazy-slot-load` Phase 2). That set is **union(selected, active)** per track (stopped remaps `active := selected` in `loadTransportSlotIndices`, and enables the playing slot so `updateAllTracks` can emit MO). Other payloads stay HEADER_READY until select/focus / Phase 4.
