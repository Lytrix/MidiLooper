# Overdub-start PLAYING-window hot path — refinement

**Kind:** refinement  
**Date:** 2026-07-07  
**Investigation:** [overdub_start_64bar_playing_window_regression_bugfix.md](overdub_start_64bar_playing_window_regression_bugfix.md)  
**Architecture:** [RuntimeArchitecture.md](../Authority/Architecture/RuntimeArchitecture.md), [Display.md](../Authority/Architecture/Display.md)

Implementation-only. No new architectural nouns — applies existing runtime request / build-policy model.

---

## Goal

Keep PLAYING and overdub arm off full-loop **display representation** rebuild; align with deferred scheduling responsibilities in [DerivedViews.md](../Authority/Architecture/DerivedViews.md).

---

## Changes (shipped 2026-07-07, branch)

| # | Change | Files (typical) |
|---|--------|-----------------|
| 1 | Defer visual cache off PLAYING display path; rebuild in `Track::processDeferredIdleMaintenance` | `DisplayManager.cpp`, `Track.cpp` |
| 2 | Single materialize — `materializeEditViewFromPasses`; playback seeds from `midiEvents()` via `ensurePassesMaterializedStore()`; skip redundant overdub-entry display merge | `Loop.cpp`, `Track.cpp` |
| 3 | Bound `#CAP REVT` slice: 8 deferred save, 16 PLAYING, 64 otherwise | `DebugSessionCapture` / emit sites |
| 4 | `#CAP,ODUB,stage` in `startOverdubbing` | `Track.cpp`, `TrackManager.cpp` |
| 5 | `MidiLedManager` — skip `ensureVisualCacheBuilt` when `visualCacheDirty` | `MidiLedManager.cpp` |

Native: **472/472** PASS. Build: `teensy41-capture-serial`.

---

## Follow-up (architecture-aligned)

| Item | Rationale |
|------|-----------|
| Bar-dirty incremental display rebuild | Window move without full-loop `projectDisplayNotes` |
| Central rebuild schedule on `Loop` | Single owner for display representation policy |
| LED path reads bar-presence summary | Avoid coupling LEDs to full `DisplayNote` list |

Not required for 64-bar overdub HITL re-gate.

---

## Verification

Same as investigation plan — 64-bar record + overdub with serial capture. Mark investigation plan **archived** when PASS.
