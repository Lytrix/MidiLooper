# Design — slot-performance-interaction

**Date:** 2026-07-06  
**Status:** Ready for implementation  
**Architecture:** [`docs/plans/slot_playback_window_interaction_architecture.md`](../../../docs/plans/slot_playback_window_interaction_architecture.md)

---

## Context

Three interaction categories:

| Category | Entry | Examples |
|----------|-------|----------|
| **Performance** | Short, double tap | Launch, mute, restart |
| **LoopTriggerSequence** | Long press on loop slots | Chain build / cue — [`unified-interval-projection`](../unified-interval-projection/) Phase 7 |
| **Editing** | Record button, edit mode, faders | Arm, record, overdub, undo, redo — **not** on slot buttons (except LTS-local undo in LTS Edit Mode) |
| **Management** | Triple press, very long hold, overlay | Load, save, replace, delete |

`slot-selection-focus` (shipped) owns **selected vs active** focus lifecycle. UIP Phase 4 owns **projection cycle** and **queued start**. This change owns **performance action scheduling** and **silence side effects**.

### PlaybackWindow (domain)

> Name the thing on which an action operates.

| Layer | Type | Role |
|-------|------|------|
| Domain | `PlaybackWindow` `{start, length}` | Active musical subset over a `Loop` |
| Domain | `Slot` | `LoopId` + persisted window metadata |
| Core | `TickInterval` / `TickRange` | Projection primitive (`start`, `end`) |
| UI | `DetailedWindowContext` | Display viewport; follows domain window |
| Engine | `PlaybackMergedMidiEvents` | Merged event cache (rename from today's `PlaybackWindow.h`) |

**PlaybackWindow** drives: what is heard, what is edited, what is projected for display. Bar/16th gestures create a **transient** window overlay; slot metadata holds the **persisted** window.

---

## Goals / Non-Goals

**Goals:**

- One `SlotActionQueue` per track for performance commits at loop boundary
- Note-offs on any transition that stops audible MIDI from a slot
- LOOP_EDIT `loopStartTick` change resyncs playback via existing queued-start API
- Gesture map aligned with clip-launcher UX; management via overlay
- LED: selected solid, queued pulse, muted dim

**Non-Goals:**

- Interval projection engine changes
- Jam capture (D13)
- Scene launch implementation
- Per-slot track transport state machine duplicate

---

## Decisions

### D1 — SlotActionQueue owns performance commits

**Decision:** Extend `SlotStateMachine` (or colocated `SlotActionQueue` on `TrackManager`) with pending action + `queuedAtTick`. `TrackManager::updateAllTracks` commits when `shouldCommitSlotAction(track, currentTick)` — same loop-boundary rule as `SlotQuantization::LoopEnd` (phase in active loop wraps to 0).

**Action types (v1):**

| Type | Enqueue from | Commit side effects |
|------|--------------|---------------------|
| `LaunchSlot` | Short on non-selected filled slot | `LoopEnd` | `setSelectedSlotIndex(No)`; full-loop PlaybackWindow; enable; `queuePlaybackStartAtGrid`; note-offs if replacing audible set |
| `LaunchSlot` | Double on non-selected filled slot | `NextGrid` | Same side effects; earlier grid commit |
| `RestartSlot` | Double on selected filled slot | `NextGrid` | `queuePlaybackStartAtGrid(loopStartTick)`; `resetPlaybackStateForSlot`; note-offs |
| `MuteSlot` | Short on selected when unmuted | `LoopEnd` | `slotMuted=true`; `sendAllNotesOff` |
| `UnmuteSlot` | Short on selected when muted | `LoopEnd` | `slotMuted=false`; `resetPlaybackStateForSlot` |

**Ownership:** `TrackManager` enqueues/commits; `MidiButtonActions` only enqueues. Mirrors track state transition pattern.

**Alternatives:** Keep ad-hoc `toggleSlotMuted` + `requestSlotSwitch` — rejected; duplicated timing and missing note-offs.

### D2 — Quantisation: LoopEnd vs NextGrid

**Decision:**

| Action | Quantisation |
|--------|--------------|
| Short launch, short mute/unmute | **`SlotQuantization::LoopEnd`** |
| Double restart (selected), double launch (other) | **`SlotQuantization::NextGrid`** |

Record punch-in / arm queue on **Record button (36)** keeps existing paths — not on slot buttons.

**Rationale:** Loop boundary for clip switch; 16th grid for earlier restart alignment. Locked 2026-07-06.

### D3 — Note-offs on silence

**Decision:** `Track::silenceAudibleNotes()` wraps `sendAllNotesOff()` + clear per-slot `ActiveNoteLedger` on `TrackPlaybackRuntime`. Called from queue commit paths that mute, disable, delete, or relaunch with replacement.

**Rationale:** All slots on a track share `midiChannel`; CC123 on output channels is correct and idempotent ([`exclude_led_channels_from_all_notes_off`](../../../docs/plans/exclude_led_channels_from_all_notes_off_1fca1ecd.plan.md)).

**LOOP_EDIT boundary:** `applyLoopStartTick` / `setLoopStartTick` while playing SHALL call silence + queued restart (D4).

### D4 — LOOP_EDIT playback resync

**Decision:** When `loopStartTick` or loop length changes on the **selected** slot while `TRACK_PLAYING` or `TRACK_OVERDUBBING`:

1. `silenceAudibleNotes`
2. `queuePlaybackStartAtGrid(newLoopStartTick, currentTick)`
3. Commit at next loop boundary (or immediately if transport stopped)
4. `resetPlaybackStateForSlot` + `invalidatePlaybackMergedMidiEvents` (after Phase −1 rename)

Uses UIP `commitQueuedPlaybackStart` — no parallel projection math.

**Touch points:** `LoopEditManager::applyLoopStartTick`, `applyLoopLength*`, `BarStepButtonHandler` HOLD_ONE 16th path.

### D5 — Slot buttons exclude capture and history

**Decision:** Notes 50–57 SHALL NOT invoke arm, record, overdub, undo, or redo. Those remain on **Record/Overdub (36)** per [`MidiButtonConfig.cpp`](../../../src/Utils/MidiButtonConfig.cpp) (`TOGGLE_RECORD`, `UNDO`, `REDO`, `CLEAR_TRACK`).

Empty slot short press → **select only** (no record arm). Strip record/overdub fallbacks from `handleToggleRecordForSlot`.

### D6 — Gesture map (BREAKING)

**Decision:**

| Gesture | Slot behaviour |
|---------|----------------|
| Short | Performance: launch / mute / select empty |
| Double | Performance: launch (other) / restart (selected) |
| Long press | **`LoopTriggerSequence` chain** — dispatch to `LoopTriggerSequenceManager` |
| Triple | **Slot management overlay** |
| Very long (~4s) | Delete slot |

Remove slot mappings: `OVERDUB_FOR_SLOT`, `REDO_FOR_SLOT`, `CLEAR_TRACK_FOR_SLOT`, `UNDO_FOR_SLOT`, and **`beginSlotLayerHold`** multi-hold on loop slot buttons.

Undo/redo/clear/record: **Record button only** (global undo stack for destructive slot ops).

### D8 — Long press routes to LoopTriggerSequence chain

**Decision:** Long press on loop slots SHALL start or continue the **`LoopTriggerSequence`** chain workflow per [`unified-interval-projection`](../unified-interval-projection/) Phase 7:

| Context | Behaviour |
|---------|-----------|
| Play / Loop Edit / Note Edit | Long-press slot A + short-press slot B → **build phase** (`LoopTriggerSequence Edit Mode`) |
| **`triggerSequencePlay` active** | Long-press slot → **cue** different sequence; switch after idle timeout (default 2 bars) |
| **Build phase active** | Long-press first slot in pending list → exit build, discard pending chain |

**Owner:** `LoopTriggerSequenceManager`; `MidiButtonActions` mode-aware dispatch. Full implementation may ship in UIP Phase 7; this change removes conflicting handlers (`beginSlotLayerHold`, immediate clear on long release).

**Supersedes:** legacy multi-hold enabled-set layering on loop slot long press.

### D9 — Management: triple overlay and very-long delete

**Decision:**

- Triple → open slot overlay (`set-revision-persistence` shell)
- Very long (~4000ms) → delete slot with undo checkpoint + silence

Long hold does **not** open overlay — reserved for **`LoopTriggerSequence`** chain entry.

### D10 — Global undo on destructive slot ops

**Decision:** Very-long delete and overlay replace/clear SHALL push checkpoints on the **global undo stack** (`TrackUndo`, same routing as `handleUndo` / Record long clear) before mutating slot data.

### D11 — PlaybackWindow domain object

**Decision:** **PlaybackWindow** `{start, length}` + `PlaybackWindowMode` (`FullLoop` | `Window`) is the shared musical working region across playback, editing, and display projection.

- **Persisted:** per-slot metadata (`loopStartTick`, `loopLengthTicks`) — may differ across slots sharing the same `LoopId`
- **Transient:** bar/16th HOLD gestures set runtime window (today `setJam` / `jamPlaybackActive`; migrate to `PlaybackWindow` on `Track`)
- **Release** → return to selected slot's persisted window
- **Persist to slot:** empty slot short press; filled slot **triple → overlay** replace confirm
- **Display:** `DetailedWindowContext` remains UI-layer; aligns via `IntervalProjection` / `ProjectionContext.window`

Jam **capture** scoped to window remains parked (D13).

### D12 — Engine cache rename (Phase −1, first)

**Decision:** Rename merge cache [`include/PlaybackWindow.h`](../../../include/PlaybackWindow.h) → **`PlaybackMergedMidiEvents`**. Rename **`Track::invalidatePlaybackWindow` → `invalidatePlaybackMergedMidiEvents`**.

### D13 — Short press other slot resets window

**Decision:** When launching a non-selected slot, **PlaybackWindow** for the destination SHALL reset to that slot's persisted metadata (default: full loop). Active transient window on the prior slot does not carry over.

### D14 — Window bounds

**Decision:** PlaybackWindow SHALL clamp to `[0, loopLengthTicks)`. Wrap behaviour follows existing interval projection rules — no free movement outside loop storage period in v1.

### D15 — Single active window (v1)

**Decision:** One active **PlaybackWindow** per selected slot at a time. Multi-window stacking / clip-follow deferred.

### D16 — Layered playback phase (follow-up)

**Decision:** v1 may default single-slot launch. `playMidiEventsForSlot` phase gap — fix in follow-up if layered playback in scope.

### D17 — Interaction with slot-selection-focus

**Decision:** Performance enqueue runs **after** `setSelectedSlotIndex(..., SyncPlayback::No)` for other-slot launch (UI immediate). Active switch + queued start at action commit — replaces direct `toggleSlotMuted` and `requestSlotSwitch` in `handleToggleRecordForSlot`.

---

## Call flow

```
Short other slot (playing)
  → setSelectedSlotIndex(track, slot, SyncPlayback::No)  // slot-selection-focus
  → SlotActionQueue.enqueue(LaunchSlot, slot)
  → LED pending pulse
  → [loop boundary]
  → commit: silence, enable, queuePlaybackStartAtGrid, commitQueuedPlaybackStart
```

```
Short selected slot (playing)
  → SlotActionQueue.enqueue(MuteSlot | UnmuteSlot, LoopEnd)
  → [loop boundary]
  → commit: slotMuted toggle + silence or resetPlaybackStateForSlot
```

```
Double selected slot (playing)
  → SlotActionQueue.enqueue(RestartSlot, NextGrid)
  → [next 16th grid]
  → commit: note-offs, queuePlaybackStartAtGrid, resetPlaybackStateForSlot
```

---

## Risks / mitigations

| Risk | Mitigation |
|------|------------|
| Quantised mute feels laggy | LED pending pulse; only performance actions quantised — editing stays immediate |
| Overlay not stable | Gate Phase 2 on `load-save-overlay-display-regression` |
| HITL gesture tests break | New scenarios; update slot double expectations |

---

## Verification

- `pio test -e native` — queue commit timing; projection after loop-start resync
- HITL: launch at boundary, mute note-offs, restart alignment, LOOP_EDIT start move
- Manual: slot-selection-focus §8 with new gestures
