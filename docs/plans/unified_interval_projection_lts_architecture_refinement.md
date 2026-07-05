# LoopTriggerSequence architecture refinement

**Kind:** refinement (OpenSpec `unified-interval-projection` Phase 7)  
**Status:** Integrated into design + spec (2026-07-05)

## Principle

| Layer | Responsibility |
|-------|----------------|
| **LoopTriggerSequence** | **What** plays |
| **IntervalProjection** | **Where** in working time |
| **Track** | **How** playback runs |

## Pipeline

```
Capture → stored TriggerEvents → playback coordination (Manager)
  → ProjectionContext → IntervalProjection → Track → MIDI
```

## Key decisions

1. **Start / End events** — loop-level MIDI analog; no duration field
2. **Capture / storage / playback** — separate stages (mirrors MIDI)
3. **Immutable snapshots** on **Start** (`PlaybackTarget`, offsets, length, repeatCount)
4. **No wrap math in LTS** — Manager produces `ProjectionContext`; UIP owns projection
5. **One Start + End per engage** — loop wraps do not emit extra Start events
6. **`triggerSequenceTick`** — sequence timeline (like **`currentTick`** for **`midiEvents`**); capture in record/overdub; playhead during **`triggerSequencePlay`**; edit without stop
7. **`projectionCycleStartTick`** — integration point on Track
8. **`PlaybackTarget`** — v1 Loop; Phase 8 LoopSlice; future clip/scene reserved
9. **Manager coordinates, Track performs**
10. **Edit without stop** — sequence storage edits never require stopping transport or playback

## Types (Phase 7)

```cpp
enum class TriggerEventType { Start, End };

struct TriggerEvent {
  TriggerEventType type;
  int32_t tick;
  TriggerPlaybackSnapshot snapshot;  // full on Start
};
```

See [`openspec/changes/unified-interval-projection/design.md`](../../openspec/changes/unified-interval-projection/design.md) D14, D17–D19.
