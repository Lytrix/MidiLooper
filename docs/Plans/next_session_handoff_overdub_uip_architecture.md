# Next session handoff — Phase A playback (runtime redesign)

**Kind:** handoff  
**Date:** 2026-07-07  
**Branch:** `derived-note-overlap-logic` @ `d635296`  
**Authority:** [DEC-016](../DECISION_LOG.md#dec-016-runtime-architecture-four-layer-model), [DEC-017](../DECISION_LOG.md#dec-017-skip-long-hitl-gates-implement-runtime-redesign)

Load [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) and [`RuntimeArchitecture.md`](../Authority/Architecture/RuntimeArchitecture.md).

---

## Next implementation step (one session)

**Finish Phase A item 1: playback low-cost derived event view before MIDI send (H6).**

Architecture checkpoint: **scheduling/cost only** — no overdub state-machine or ownership changes. Playback must be correct before `playMidiEvents` sends; display/LED may lag.

---

## Already shipped (`d635296`)

| Invariant | Status |
|-----------|--------|
| Display defer `ensureVisualCacheBuilt` on PLAYING + STOPPED_RECORDING | Done — `DisplayManager.cpp` `deferVisualRebuild` |
| Idle visual cache rebuild | Done — `Track::processDeferredIdleMaintenance` |
| Idle `passesMaterializedStore` seed | Done — `Track::processDeferredIdleMaintenance` |
| Playback chunk-ref merge | Done — `ensurePlaybackWindowBuilt` → `mergeActiveCapturePasses` |
| Visual cache chunk-ref merge (no edit passes) | Done — `Loop::rebuildVisualCacheFromPasses` |
| LED skip when `visualCacheDirty` | Done — `MidiLedManager.cpp` |
| REVT gated `!isPlaying()` | Done — `Track::processDeferredIdleMaintenance` |
| Stop tail `pre_state_advance` telemetry | Done — `Track::stopRecording` |
| ODUB stage telemetry | Done |

---

## Step 1 — Playback window (do this first)

**Problem:** First PLAYING tick after record stop calls full materialize via `midiEvents()` / `mergeMaterializedPassesWithCapture`.

**File:** `src/Track.cpp` — `ensurePlaybackWindowBuilt` (anonymous namespace, ~line 160)

**Current hot path (non–NOTE_EDIT, no live capture):**

```cpp
if (loop.visualCacheDirty) {
  loop.ensurePassesMaterializedStore();  // full materialize
}
const MidiEventVec& published = loop.midiEvents();
runtime.primaryWindow.mergedEvents.assign(published.begin(), published.end());
```

**Target (DEC-016 / June PASS reference at `58d6c08`):**

```cpp
loop.mergeActiveCapturePasses(runtime.primaryWindow.mergedEvents);
```

Rules:

1. When `!noteEditPreview && !loop.captureActive()` and `builtFromRevision != playbackRevision`, use **`mergeActiveCapturePasses`** (chunk-ref merge) — not `mergeMaterializedPassesWithCapture` / `ensurePassesMaterializedStore` on the path to first MIDI send.
2. **NOTE_EDIT Tier 2** unchanged — `sessionMidiEvents()` when `noteEditPreview`.
3. **Live capture** (`captureActive()`) may still use `mergeMaterializedPassesWithCapture` until Phase B defines store-once policy.
4. If edit passes require full `passes.materializeToEventVector`, materialize **once per `playbackRevision`** in idle maintenance (seed `passesMaterializedStore_`), not synchronously in `ensurePlaybackWindowBuilt` on PLAYING entry.

**Reference:** `git show 58d6c08:src/Track.cpp` (lines ~131–138).

**After edit:** `pio test -e native` (expect 472/472). Optional: 16-bar record + play smoke on hardware — no long capture.

---

## Step 2 — Display stale-while-revalidate (same session if step 1 is small)

**Problem:** When `deferVisualRebuild` is true and `visualCacheDirty`, display returns **empty** notes.

**File:** `src/DisplayManager.cpp` ~lines 682–686

**Change:** If `!needsLiveMergeForDisplay` and `visualCacheDirty`, still assign `loop.visualCache.notes` when non-empty (stale OK). Only return empty when cache was never built.

**Invariant:** Window move filters interval only; full rebuild runs in idle maintenance (`Track.cpp` ~740–744).

---

## Step 3 — Remaining Phase A (follow-up session)

| Item | File | Status |
|------|------|--------|
| REVT during PLAYING | `Track.cpp` `processDeferredIdleMaintenance` | Done — `!isPlaying()` gate |
| `prewarmSelectedDisplayVisualCache` | `TrackManager.cpp:83` | Done — no-op on PLAYING / STOPPED_RECORDING |
| `getVisualNotesForSlot` | `Track.h:278` | Done — stale read on PLAYING paths |
| Display live-merge materialize | `DisplayManager.cpp:579,690` | Live capture/overdub only; published PLAYING uses stale-while-revalidate |

---

## Step 4 — Phase B (shipped 2026-07-07)

| Deliverable | Implementation |
|-------------|----------------|
| Seed `passesMaterializedStore` once per revision | `Track::processDeferredIdleMaintenance` — store before visual cache; PLAYING + STOPPED_RECORDING |
| Display reconstruct from flat | `Loop::rebuildVisualCacheFromPasses` → `gatherPublishedFlatForDerivedView` reads `midiEvents()` when fresh |
| Playback reads seeded flat | `ensurePlaybackWindowBuilt` uses `midiEvents()` when `isPassesMaterializedStoreFresh()` |

Native **472/472** PASS.

---

## Step 5 — Phase C (shipped 2026-07-07)

| Deliverable | Implementation |
|-------------|----------------|
| Bar-slice idle rebuild | `Loop::rebuildVisualCacheIdleSlice` — 2–4 bars/slice, playhead-priority, chunk merge only |
| No materialize on PLAYING | `ensurePassesMaterializedStore` + full `ensureVisualCacheBuilt` only when transport fully idle |
| Window-first display read | `DisplayManager::resolveDisplayNotes` — provisional 16-bar chunk merge when cache empty on long loops |
| `dirtyBars` on stale | `markDisplayCachesStale` marks all bars dirty; slices clear per bar |

Native **472/472** PASS.

---

## Step 6 — Follow-up

---

## Acceptance (this step)

- [x] `ensurePlaybackWindowBuilt` uses low-cost merge for published playback (no sync full materialize on PLAYING entry for normal record→play)
- [x] `pio test -e native` PASS (472/472)
- [ ] Manual: boot v6 workspace, 16-bar record → play → stop (no OLED freeze)
- [ ] **No** new `captures/` HITL runs (DEC-017)

Long-loop / 64-bar overdub re-check only after Phase A–C, if user requests.

---

## Key files

| Concern | Path |
|---------|------|
| Playback window | `src/Track.cpp` `ensurePlaybackWindowBuilt`, `playMidiEvents` |
| Chunk-ref merge | `src/Loop.cpp` `mergeActiveCapturePasses` |
| Full materialize (avoid on hot path) | `Loop::materializeEditViewFromPasses`, `mergeMaterializedPassesWithCapture` |
| Display read | `src/DisplayManager.cpp` `resolveDisplayNotes` |
| Owner table | `docs/Authority/Architecture/DerivedViews.md` |

---

## Parked (do not start)

- Commit bisect / save-bypass / `validate-64x64` HITL (DEC-017)
- UIP 5.5 HITL, Phase 6 overlap
- Flash pre–Jul-6 SHAs without SD reset (storage v4 vs v6)

---

## Uncommitted docs (same session arc)

If committing process docs: `DECISION_LOG.md` (DEC-017), `CURRENT_WORK.md`, `PROJECT_STATE.md`, plan frontmatter — not required for firmware step 1.

---

## Agent prompt (copy to next chat)

> Implement Phase A step 1 from `docs/Plans/next_session_handoff_overdub_uip_architecture.md`: fix `ensurePlaybackWindowBuilt` to use `mergeActiveCapturePasses` instead of full materialize on PLAYING entry after record stop. Then stale-while-revalidate in `DisplayManager` if time. Run `pio test -e native`. No long HITL captures.
