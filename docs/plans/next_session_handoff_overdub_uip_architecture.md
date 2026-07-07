# Next session handoff — 64-bar regression plan, runtime architecture

**Kind:** handoff  
**Date:** 2026-07-07 (updated)  
**Branch:** `derived-note-overlap-logic` (ahead of `origin` by 3 commits)

**Start here.** Load with [`docs/runtime/CURRENT_WORK.md`](../runtime/CURRENT_WORK.md) and [`docs/runtime/PROJECT_STATE.md`](../runtime/PROJECT_STATE.md).

**Active plan:** [`64bar_regression_commit_analysis_enhancement.md`](64bar_regression_commit_analysis_enhancement.md) (full analysis + bisect anchors).

---

## Executive summary

64+64 HITL **regressed** after June 23 PASS (`58d6c08`). Direct hot-path patches (uncommitted) are **partial Phase A only** — playback still full-materializes on first PLAYING tick (**H6**). **Do not** re-gate UIP 5.5 until 64+64 passes.

**Strategy:** save-bypass → commit bisect → Phase A→C runtime invariants → `validate-64x64` → UIP 5.5.

**Architecture authority:** [DEC-016](../DECISION_LOG.md), [`docs/00-authority/Architecture/`](../00-authority/Architecture/).

---

## Work order (locked)

| Step | Todo ID | What to do |
|------|---------|------------|
| 0 | `save-bypass-gate` | Flash `teensy41-capture-bypass`, run 64+64 HITL — isolate deferred save (H4) |
| 1 | `bisect-anchors` | `git stash` uncommitted firmware; HITL at `58d6c08`, `f946d82`, `4e83ac1`, `ecb3b8a` |
| 2 | `phase-a-invariants` | Playback low-cost view; LED bar probe; REVT `!isPlaying()`; display stale-while-revalidate |
| 3 | `phase-b-derived-views` | One materialize per `playbackRevision`; display notes from flat |
| 4 | `phase-c-partial-display` | Bar-slice / window-first reconstruct in `processDeferredIdleMaintenance` |
| 5 | `validate-64x64` | 64+64 HITL PASS — compare to `captures/host_midi_automation_baseline_20260623_112324.json` |
| 6 | `uip-5.5-hitl` | **Blocked until step 5** — `long_loop_display_window`, 152335, audition, queued slot |

`architecture-review` is **completed** (DEC-016 + Architecture docs).

---

## Step 0 — save bypass (start here)

```bash
pio run -e teensy41-capture-bypass -t upload   # ask user; PROGRAM MODE if needed

# Terminal 1 — serial capture
.venv/bin/python scripts/capture_session.py --port /dev/cu.usbmodem154944801

# Terminal 2 — same gate as FAIL artifact 20260707_032321
.venv/bin/python scripts/host_midi_hitl.py run --preset base \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 6 --midi-channel 6 --loop-slot 8 \
  --record-bars 64 --overdub-bars 64 --second-overdub-bars 0 \
  --verify-serial-log captures/session_<timestamp>.log
```

| Outcome | Next |
|---------|------|
| PASS (overdub ST seen) | Save is a contributor — Phase A + revisit save slice budget |
| FAIL (same ~12-line stall, 0 ODUB) | Proceed to bisect — H4 not sole cause |

---

## Step 1 — bisect anchors

```bash
git stash push -m "partial phase-a wip"
git checkout <sha>
pio run -e teensy41-capture-serial -t upload
# run same 64+64 HITL as Step 0
git checkout derived-note-overlap-logic
git stash pop
```

| SHA | Expect | Tests hypothesis |
|-----|--------|------------------|
| `58d6c08` | PASS | Baseline O1 |
| `f946d82` | FAIL? | H1+H2+H5 (full materialize visual cache, REVT on PLAYING) |
| `4e83ac1` | FAIL? | H3 (LED → `ensureVisualCacheBuilt`) |
| `ecb3b8a` | FAIL? | H1 playback (`mergeMaterializedPassesWithCapture`) |

---

## Phase A checklist (after bisect)

Uncommitted WIP covers **display/LED defer only**. Phase A is **not done** until playback is fixed.

| Invariant | Status (uncommitted) | Target |
|-----------|---------------------|--------|
| Display defer on PLAYING | Partial | `DisplayManager` — no sync `ensureVisualCacheBuilt` |
| Stale-while-revalidate | **Missing** | Show last `visualCache.notes` while dirty — not empty |
| LED low-cost query | Partial | Bar-local probe — no display rebuild (`MidiLedManager`) |
| REVT off PLAYING | **Wrong direction** | Gate `!isPlaying()` (today: slice 16 during PLAYING) |
| Playback low-cost view | **Missing** | `ensurePlaybackWindowBuilt` — chunk-ref merge or fresh store once per revision |
| Remaining hot calls | Open | `TrackManager.cpp:83`, `Track.h:278` `ensureVisualCacheBuilt` |

Key blocker (H6):

```177:186:src/Track.cpp
    if (loop.visualCacheDirty) {
      loop.ensurePassesMaterializedStore();
      ...
    }
    const MidiEventVec& published = loop.midiEvents();
    ...
  } else {
    loop.mergeMaterializedPassesWithCapture(runtime.primaryWindow.mergedEvents);
```

Reference at PASS commit: `mergeActiveCapturePasses` in `ensurePlaybackWindowBuilt` and `rebuildVisualCacheFromPasses` — see plan § Historical context.

---

## Phase B and C (after Phase A PASS on native)

**Phase B:** Seed `passesMaterializedStore` once at stop/idle; consumers read flat / store — one build per `playbackRevision`.

**Phase C:** Incremental `reconstructDisplayNotes` via `VisualCache.dirtyBars`; window-first (~16 bars) before full loop.

---

## Step 5 — validate-64x64 pass criteria

Compare to June PASS artifact `20260623_112324`:

- `PLAYING → OVERDUBBING` ≥ 1
- `PERS,result,...,ok`
- Inbound MIDI after PLAYING (not 0)
- Optional: `#CAP,ODUB,stage` after Phase A telemetry lands

---

## Step 6 — UIP 5.5 (only after step 5)

| Scenario | Preset |
|----------|--------|
| Long loop display window | `--preset long_loop_display_window` |
| Move across boundary | `edit_minimal` / manual 152335 |
| Playback audition | NOTE_EDIT + transport play |
| Queued slot start | Short-press slot during play |

OpenSpec: `openspec/changes/unified-interval-projection/tasks.md` § 5.5.

**Phase 6** (overlap resume) blocked until 5.5 PASS.

---

## Git state

### Committed on branch

| Commit | Focus |
|--------|--------|
| `b1260ce` | extMem routing |
| `432da4d` | NOTE_EDIT fader hot path |
| `d05e736` | Boot/play OLED freeze fix |
| `d690fda` | Bracket tick after `loopStartTick` |
| `6f77914` | Boot display partial revert — read diff before touching boot |

### Uncommitted firmware (stash before bisect)

`DisplayManager.cpp`, `Track.cpp`, `MidiLedManager.cpp`, `Loop.cpp`, `TrackManager.cpp`, `DebugSessionCapture.*`, HITL scripts — partial Phase A; **insufficient for validate-64x64**.

### Do not commit

`Archive.zip`

---

## Architecture quick reference

```
Capture Storage → Derived Representations → Interval Projection → Runtime Request
```

- **Entry:** [`RuntimeArchitecture.md`](../00-authority/Architecture/RuntimeArchitecture.md)
- **Owner table:** [`DerivedViews.md`](../00-authority/Architecture/DerivedViews.md)
- **Investigation:** [`overdub_start_64bar_playing_window_regression_bugfix.md`](overdub_start_64bar_playing_window_regression_bugfix.md)
- **Partial fixes log:** [`overdub_start_playing_window_hot_path_refinement.md`](overdub_start_playing_window_hot_path_refinement.md)

---

## Validated vs not

| Validated | Not validated |
|-----------|---------------|
| Native 472/472 | 64+64 HITL (FAIL `20260707_032321`) |
| NOTE_EDIT faders (`session_20260706_113243.log`) | UIP 5.5 matrix |
| Boot/play display (`session_20260706_220537.log`) | Bisect anchors |
| | Save-bypass gate |

---

## Agent reminders

1. Default build: `teensy41-capture-serial`; bypass env for Step 0 only.
2. Ask before Teensy upload.
3. `pio test -e native` before push (not required for HITL-only session).
4. `kNoteEditFaderFeedbackEnabled = false` — see [`FADER_STATE_SYSTEM.md`](../Guides/FADER_STATE_SYSTEM.md).
5. Do not start `edit-session-action-geometry` firmware until UIP Phase 6.

---

## Related docs

| Doc | Topic |
|-----|--------|
| [64bar_regression_commit_analysis_enhancement.md](64bar_regression_commit_analysis_enhancement.md) | Full bisect analysis + hypotheses |
| [unified_interval_projection_enhancement.md](unified_interval_projection_enhancement.md) | UIP phases |
| [derived_note_overlap_logic_handoff.md](derived_note_overlap_logic_handoff.md) | After UIP Phase 6 |
