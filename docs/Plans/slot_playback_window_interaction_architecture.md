# Playback Window / Slot Interaction Model (Naming + Architecture)

**Status:** Authoritative architecture input for OpenSpec change [`slot-performance-interaction`](../../openspec/changes/slot-performance-interaction/).  
**Principle:** Name the thing on which an action operates, not the action itself.

---

## Core design insight

The system converges on one central concept:

> A **movable window over a Loop** that defines the active musical subset.

**PlaybackWindow** is a shared **domain** object consumed by playback, display, step editor, encoder/fader scaling, quantisation, and future derived-loop extraction — not a UI-only construct.

---

## Record button vs slot buttons (locked 2026-07-06)

**All capture and history gestures live on Record/Overdub (note 36) only:**

| Record button | Action |
|---------------|--------|
| Short | Arm / record / overdub / play (existing `TOGGLE_RECORD`) |
| Double | Undo (`UNDO`) |
| Triple | Redo (`REDO`) |
| Long | Clear selected slot (`CLEAR_TRACK`) |

**Slot buttons (notes 50–57) are performance, LoopTriggerSequence chain entry, and management only.** They SHALL NOT arm, record, overdub, undo, or redo (except LTS-local undo inside **`LoopTriggerSequence Edit Mode`** per [`unified-interval-projection`](../../openspec/changes/unified-interval-projection/)).

- Empty slot short press → **select slot only** (no record arm)
- Capture while switching slots → `finalizeCaptureAndSelectSlot` when Record-driven capture is active and user selects another slot via slot press (focus change only)

---

## Slot button behaviour (locked 2026-07-06)

| Gesture | Quantisation | Non-selected (filled) | Selected (filled) | Empty slot |
|---------|--------------|----------------------|-------------------|------------|
| **Short** | `LoopEnd` | Select + full-loop PlaybackWindow + queue **launch** | Queue **mute/unmute** | **Select only** |
| **Double** | `NextGrid` | Select + queue **launch** (earlier commit) | Queue **restart** from loop start | Select only |
| **Long press** | — | **`LoopTriggerSequence` chain** — see UIP Phase 7 | Same | Same |
| **Triple** | — | Open **slot management overlay** | Open overlay | Open overlay |
| **Very long (~4s)** | — | **Delete slot** (+ global undo checkpoint) | Delete slot | No-op |

### Long press → LoopTriggerSequence chain ([`unified-interval-projection`](../../openspec/changes/unified-interval-projection/) Phase 7)

Long press on loop slots SHALL **not** open the management overlay and SHALL **not** use legacy multi-hold enabled-set layering (`beginSlotLayerHold`).

| Context | Long-press behaviour |
|---------|---------------------|
| Play / Loop Edit / Note Edit | **Build phase entry:** long-press slot A, then short-press slot B → `LoopTriggerSequence Edit Mode` build phase (idle timeout default 2 bars) |
| **`triggerSequencePlay` active** | Long-press slot → **cue** a different sequence; switch after idle timeout |
| **Build phase active** | Long-press first slot in pending list → exit build and discard pending chain |

Implementation owner: **`LoopTriggerSequenceManager`**; `MidiButtonActions` dispatches mode-aware. Full behaviour ships with UIP Phase 7; this change reserves the gesture map and removes conflicting paths (multi-hold, immediate clear).

**Very long (~4s)** remains **delete slot** + global undo — unchanged.

---

## Multi-slot playback vs multi-slot overdub (locked 2026-07-13)

| Capability | Status | Notes |
|------------|--------|--------|
| **Layered multi-slot playback** | **Shipped (legacy hold)** | `beginSlotLayerHold` / `pendingMultiSlotCommit` — multiple enabled slots audible via `playMidiEvents` + `playMidiEventsForSlot`. Removed from slot buttons when `slot-performance-interaction` gesture remap ships; until then it is the only multi-slot playback UX. |
| **Loop-end chain (long press, non-selected)** | **Shipped** | Single-slot switch at loop boundary — not simultaneous layers. |
| **Multi-slot overdub** | **Explicitly out of scope** | Too complex. Overdub stays on Record button, **active capture slot only**. |

---

## Piano roll: playing vs selected (locked 2026-07-13)

When **preview/selected** ≠ **playing**, or when legacy layered playback is active:

- **Primary notes:** selected/preview slot — normal brightness
- **Reference overlay:** other audible enabled slots — **dimmed** MIDI
- **Tiling** vs playing slot length `L_play`:
  - Shorter other loop → repeat pattern as dimmed
  - Longer other loop → show `L_play`-sized repeated segments semi-dimmed (not full long loop scroll)

Owner: `DisplayManager` composes layers; extends split-focus (preview cursor flash + playing LEDs). See OpenSpec **D23**.

---

## Quantisation split

| Action | Quantisation | Why |
|--------|--------------|-----|
| Launch, mute, unmute | **`LoopEnd`** | Clip-launcher: switch at loop boundary |
| Restart (double selected) | **`NextGrid`** | Start realignment earlier (16th grid) |
| Launch (double other) | **`NextGrid`** | Same earlier commit for slot switch |

Record punch-in keeps existing record queue paths (`NextGrid` / bar quantise) on **Record button only**.

---

## Type layers

| Layer | Types | Role |
|-------|-------|------|
| **UI** | `DetailedWindowContext` | Viewport zoom/scroll; aligns with PlaybackWindow but remains display-specific |
| **Domain** | `PlaybackWindow`, `Slot`, `Loop` | Musical working region + persistence |
| **Core** | `TickRange` / `TickInterval` | `start` + `end` or `start` + `length` primitive |
| **Engine** | **`PlaybackMergedMidiEvents`** | Precomputed merged events for real-time playback (rename from today's `PlaybackWindow` in `include/PlaybackWindow.h`) |

### Naming: `PlaybackMergedMidiEvents` (not `PlaybackMergeMidiEvents`)

Repo convention uses **past participle** for derived buffers (`mergedEvents`, `mergeMaterializedPassesWithCapture`). The type name SHALL be **`PlaybackMergedMidiEvents`**; invalidation **`invalidatePlaybackMergedMidiEvents()`**.

### PlaybackWindow (domain)

```cpp
struct PlaybackWindow {
    uint32_t start;
    uint32_t length;
};

enum class PlaybackWindowMode : uint8_t {
    FullLoop,
    Window
};
```

---

## Global undo integration

Destructive slot operations SHALL use the **global undo stack** (`TrackUndo` / `handleUndo` routing), consistent with Record long-press clear:

| Operation | Undo checkpoint before mutate |
|-----------|------------------------------|
| Very-long delete slot | `pushClearTrackSnapshot` (or slot-scoped equivalent) |
| Overlay replace slot | Clear/replace checkpoint per DEC-001 |
| Overlay clear row | Same as today’s clear-slot undo path |

Slot gestures SHALL NOT push undo; Record button and overlay confirm rows do.

---

## PlaybackWindow lifecycle

### Transient window (bar / 16th)

HOLD_ONE / HOLD_TWO / bar-step gestures → transient `PlaybackWindow` on selected slot; no pass mutation.

### Persisting window (destination slots)

| Destination | Gesture | Action |
|-------------|---------|--------|
| **Empty slot** | Short while transient window active | Persist window to slot; select slot |
| **Filled slot** | Triple → overlay → replace confirm | Replace with undo checkpoint |

---

## Implementation order (locked)

1. **Rename** `PlaybackWindow` merge cache → `PlaybackMergedMidiEvents`; `invalidatePlaybackWindow` → `invalidatePlaybackMergedMidiEvents`
2. Phase 0: note-offs + LOOP_EDIT resync
3. `SlotActionQueue` with quantisation split (`LoopEnd` vs `NextGrid`)
4. Gesture remap (strip record/overdub/undo/redo from slots)
5. Long → `LoopTriggerSequenceManager` dispatch; remove multi-hold on loop slots; triple → overlay
6. PlaybackWindow domain struct migration (follow-up)

---

## Related artifacts

- OpenSpec: [`openspec/changes/slot-performance-interaction/`](../../openspec/changes/slot-performance-interaction/)
- Impact review: prior session analysis (`MidiButtonActions`, `TrackManager`, `SlotStateMachine`, …)
- Control surface: [`docs/Guides/control-surface/Loops.md`](../Guides/control-surface/Loops.md) (rewrite on implement)
