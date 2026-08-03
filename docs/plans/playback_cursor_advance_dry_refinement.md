# Playback cursor advance DRY

Behavior-preserving DRY of `Track::playMidiEvents` / `playMidiEventsForSlot`.

## Architecture

### Orchestrator — `Track::playCommittedLoopMidi`

Owns merge build, playback order rebuild, wrap handling, and tick frame. Public entry points are thin guards only.

| `PlaybackMidiTarget` | Wrap | Capture stream | Jam / de-dupe |
|--------------------|------|----------------|---------------|
| `ActiveSlot` | advances `projectionCycleStartTick` + both indices | yes | yes |
| `LayeredSlot` | resets `nextEventIndex` only | no | no |

**Invariant:** wrap handling completes before `advancePlaybackCursor` runs.

### Cursor advance — `advancePlaybackCursor` ([`PlaybackCursorAdvance.h`](../../include/Utils/PlaybackCursorAdvance.h))

Advances the committed playback cursor while emitting every event crossed by the playback interval. Mutates only `PlaybackCursorAdvanceState` (`cursor`, optional `playbackOrderDirty`). Send via `PlaybackSendFn` callback.

Must **not** write: `projectionCycleStartTick`, `captureNextEventIndex`, `lastTickInLoop`, or rebuild playback order.

### Predicates — `IntervalProjection`

- `isPlaybackCatchUpWindow` — `<=` semantics (not `isPlaybackAtLoopStart`'s `<`)
- `didPlaybackEventCross` — event crossed playhead this tick

## Verification

- `pio test -e native` (predicate + `test_playback_cursor_advance` suites)
- `pio run -e teensy41-capture-serial`
- `rg 'projectionCycleStartTick\s*=' src/` — layered slot path has no writer
