# Capture display lag + record-stop playhead bugfix

**Status:** Shipped (P0)  
**Evidence:** [`session_20260713_150620.log`](../captures/session_20260713_150620.log)

## Problems

1. **Lag during overdub** — `resolveDisplayNotes` called `rebuildLiveDisplayNotes()` every display frame on the warm cache path, triggering `ensureVisualCacheBuilt()` whenever `visualCacheDirty`.
2. **Playhead on bar 2 after record stop** — detailed viewport not recentered after record-stop rewind.
3. **Skipped notes on first playback cycle** — `reanchorPlaybackProjection` clobbered `lastTickInLoop` set by `stopRecording` with `UINT32_MAX`.

## Fixes

| Area | Change |
|------|--------|
| `DisplayManager::resolveDisplayNotes` | Warm path: strip playhead tail extensions via `liveDisplayCacheCommittedEnd`; full rebuild only on `needsFullLiveRebuild` |
| `Track::stopRecording` | `invalidateLiveDisplayCache()` + `centerDetailedWindowOnPlayhead` after `startPlaying` |
| `Track::reanchorPlaybackProjection` | When `preserveLoopPhaseOrigin`, keep record-stop `lastTickInLoop` instead of `UINT32_MAX` |

## Verification

- `pio test -e native`
- Manual: 2-bar record → stop → playhead visible in bar 1; overdub UI responsive between MIDI events
