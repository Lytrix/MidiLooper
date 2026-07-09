# Boot load sequence

Agent-oriented map of cold-boot ordering: SD workspace load, deferred slot restore, USB Host (DROID), and display/LED refresh. Read before changing `main.cpp` setup, `StorageManager::loadState`, `beginUsbHost`, or boot-time LED side effects.

**Related:** [`DEFERRED_RUNTIME_PERSISTENCE.md`](DEFERRED_RUNTIME_PERSISTENCE.md) (SD layout, SAVE tokens), [`RUNTIME_STORAGE_AND_PERSISTENCE.md`](RUNTIME_STORAGE_AND_PERSISTENCE.md) (persistence invariants), [`DROID_MOTORFADER_PITCHBEND.md`](DROID_MOTORFADER_PITCHBEND.md) (USB host LED pacing).

---

## Why ordering matters

Teensy 4.1 shares the SDIO bus between the built-in SD slot and USB Host. Cold boot with DROID already connected previously hung when:

1. `usbHost.begin()` ran while **deferred loop slot restore** was still reading SD.
2. `clearLeds()` / `forceLedUpdate()` flooded ch15 USB Host MIDI during those SDIO reads.
3. USB enumeration could not progress because `usbHost.Task()` was not pumped during long SD slices.

Disconnect/reconnect worked because enumeration then ran in `loop()` with no SDIO contention. Fix: defer USB Host and DROID LED refresh until **all** queued slot restores finish.

---

## Phase sequence

```mermaid
sequenceDiagram
  participant Setup as setup()
  participant SD as StorageManager
  participant Loop as loop()
  participant USB as MidiHandler

  Setup->>SD: loadState (sync bundle + manifest scan)
  SD-->>Setup: queue deferred slot restores
  Note over Setup: BOOT,load,ok — USB host NOT started
  Setup->>Setup: finishBootSetup, LOOP_EDIT to USB device MIDI

  loop->>SD: processDeferredLoopSlotRestore (one slot per idle slice)
  Note over Loop: DISP updates; no DROID LED outbound yet

  Loop->>USB: beginUsbHost when restore queue empty
  Note over USB: BOOT,usb_host,begin
  USB->>USB: Task() poll up to 300 ms (cold-plug enumerate)
  Loop->>Loop: clearLeds, LOOP_EDIT note 100 flash, onBootSlotLoadComplete
```

### `setup()` (blocking, no USB Host)

| Step | Owner | Notes |
|------|-------|-------|
| Pool + loop RAM alloc | `MemoryPool`, `TrackManager` | Before USB Host heap use |
| `midiHandler.setup()` | DIN MIDI only | `BOOT,usb_host,deferred` |
| Early OLED + "Loading…" | `DisplayManager::beginBootOled` | Minimal SPI; full UI later |
| `looper.setup()` → `loadState` | `StorageManager` | Bundle read, manifest scan, queue restores |
| `finishBootSetup()` | `DisplayManager` | Startup splash |
| `sendEditSessionChange(Loop)` | `EditManager` | PC + fader feedback to **USB device** MIDI only |

`bootLoadInProgress_` suppresses `forceLedUpdate` during `loadState` only. It is cleared before `setup()` returns.

### `loop()` (cooperative)

| Step | Gate | Notes |
|------|------|-------|
| Deferred slot restore | `!timingCriticalTrackActive` | One slot per call; repaint OLED after each |
| Deferred undo hydrate | same | Reads undo bodies from bundle |
| **USB Host begin** | `!hasPendingLoopSlotRestore()` | `bootSlotLoadRefreshPending` gate in `main.cpp` |
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
| `usb_host,begin` | **After last deferred slot restore** | Must not appear before restore lines |
| `USB Host MIDI device connected!` | Enumeration complete | Usually within 300 ms of `begin` |

**Cold-boot + DROID connected (regression):** [`session_20260709_183527.log`](../../captures/session_20260709_183527.log) — `BOOT,usb_host,begin` at line 173 **after** deferred restore `0/0`–`4/0`; continuous `DFRAME` afterward.

**Earlier failure pattern:** `BOOT,usb_host,begin` immediately after `BOOT,load,ok`, LED flood interleaved with `Deferred restore loop slot`, then hang or sparse `DFRAME` ([`session_20260709_182947.log`](../../captures/session_20260709_182947.log) before fix).

---

## USB Host rules (do not regress)

1. **Do not** call `usbHost.begin()` in `setup()` before deferred slot restore completes.
2. **Do not** call `clearLeds()` / `forceLedUpdate()` from `setup()` — run from the boot-slot-complete gate (or `onBootSlotLoadComplete`).
3. **Do** pump `usbHost.Task()` after `begin()` for up to 300 ms when DROID is cold-plugged at power-on (`MidiHandler::beginUsbHost`).
4. **Do** keep ch15 LED outbound queued and paced (`MidiConfig::DroidUsbHost`) — see DROID guide.

---

## Memory stabilization

After a long loop load, `stabilizeBootMemoryAfterLoad()` may clear undo stacks when internal heap is below reserve (loop data intact). See [`DEFERRED_RUNTIME_PERSISTENCE.md`](DEFERRED_RUNTIME_PERSISTENCE.md) § Boot after long save.

---

## Key files

| File | Role |
|------|------|
| `src/main.cpp` | Setup order; `bootSlotLoadRefreshPending` gate |
| `src/StorageManager.cpp` | `loadState`, `processDeferredLoopSlotRestore` |
| `src/MidiHandler.cpp` | `beginUsbHost`, enumeration poll |
| `src/TrackManager.cpp` | `beginBootLoad` / `onBootSlotLoadComplete` |
| `include/Utils/BootTelemetry.h` | `BOOT,*` milestones |

**History:** `50ad01b` deferred USB Host until after sync `loadState`; follow-up defers until **deferred** slot restore queue drains (SDIO + enumeration isolation).
