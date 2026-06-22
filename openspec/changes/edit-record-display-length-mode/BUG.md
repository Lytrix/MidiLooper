# BUG — Record display, edit refresh lag, length-mode leak

**Change:** `edit-record-display-length-mode`  
**Status:** OpenSpec proposal (2026-06-19) — implement after `/opsx:apply`  
**Evidence:** Manual observation during edit-baseline HITL; serial captures `host_midi_automation_edit_baseline_20260619_033940*`

**Related:** Overlap store fix in `NoteMovementUtils` (`note-move-pitch-overlap-flaky`) — **separate** from this display/length-mode track.

---

## Reported symptoms (user)

1. **Record:** ARM then RECORD — **current tick / loop length grows** to 2 bars on display, but **notes only appear after record stops**.
2. **Edit HITL:** Scenario runs **without transport playing** (STOPPED) — expected by script today; user flags as confusing vs performance workflow.
3. **Display lag:** Note **movements** and **length** changes commit in store but **LEN / piano roll refresh too late** to trust on hardware.
4. **Length mode leak:** After **long M0 → overlap → pitch → move**, **fader 1** reselect from the lengthened mover **stretches P0** (pitch 60, bar 1 beat 3 / fixture step 12) toward **loop end** — user suspects **length edit still active** when fader 2 fires.

---

## Expected behavior

| Phase | Expected |
|-------|----------|
| RECORD (transport running) | Growing loop length **and** fixture note gates visible on piano roll as REVT/capture events arrive; open note tails follow playhead. |
| RECORD (transport stopped, growing capture) | Same live preview via `capturePreview` / open-note tails (`DisplayManager::resolvePlayheadInLoop` stopped-record path). |
| NOTE_EDIT after move/pitch/length | Sidebar **LEN** and `#CAP DNTE` match reconstructed note within **≤1 display frame** after `invalidateCaches()`. |
| Length mode OFF | Fader 2/3 control **note START** only; fader 1 reselect must **not** apply LENGTH EDIT to another note (P0 gate unchanged). |
| After overlap contained-delete | P0 absent from store until restore; **no** stretch to loop end on fader 1 select while length mode off. |

---

## Actual behavior (observed / inferred)

| Symptom | Evidence |
|---------|----------|
| Notes invisible until record stop | User report; investigate `resolveDisplayNotes` live path vs `capturePreview.revision` / `isLiveRecordingDisplay`. |
| Edit runs STOPPED | Script: `_stop_transport_if_running` before edit (`host_midi_automation_edit_baseline.py` ~4249). **By design** for HITL — document, do not treat as record bug. |
| LEN lag | `COARSE_EDIT_READY_MS` (1200 ms) + `DISPLAY_SETTLE_MS` (800 ms) in HITL; firmware may not emit DNTE until next select refresh after `invalidateCaches()`. |
| P0 → loop end on fader 1 | Hypothesis: `lengthEditingMode == true` when `sendCoarseFaderPosition` runs after reselect → fader 2 END move on wrong note; or stale pitchbend triggers `handleCoarseFaderMovement` in LENGTH EDIT branch (`NoteEditManager.cpp` ~1179). |

---

## Repro (manual)

1. Flash `teensy41-capture-serial`, run edit baseline or manual equivalent.
2. Clear → ARM → RECORD 2-bar fixture **with transport running** — watch piano roll during record.
3. Enter NOTE_EDIT, lengthen M0 to step 14, run overlap move over P0, pitch M0 60→67, move past A, return home.
4. **Fader 1** reselect M0 (or adjacent slot) — watch P0 length on OLED / serial `#CAP DNTE` for pitch 60 @ step 12.

**Pass:** P0 stays ~2-step gate (~96 ticks). **Fail:** P0 length → loop end or `Active note at loop end` in reconstruction.

---

## Serial pass criteria (HITL additions)

After overlap + pitch restore (existing gates pass):

- After fader 1 reselect on M0 home: `Length editing mode DISABLED` in recent serial (if length was toggled off in scenario).
- No `LENGTH EDIT:` log line referencing P0 start tick (~584) unless length mode explicitly on.
- P0 gate check passes (`require_p0_record_gate`) **after** fader 1 reselect post-overlap round-trip.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership of `lengthEditingMode`? | **Yes** — `NoteEditManager`; must not leak across fader 1 select / mode toggle. |
| Ownership of live record display? | **Yes** — `DisplayManager` + `Loop::capturePreview`; may need revision bump on capture insert. |
| State transition change? | **Maybe** — length mode off must reset fader 2 routing before `scheduleOtherFaderUpdates(FADER_SELECT)`. |

Design session required before patch if fader state machine ownership changes beyond length-mode flag + display revision.

---

## Patch history

| Date | Result |
|------|--------|
| 2026-06-19 | OpenSpec `edit-record-display-length-mode` opened from HITL/manual reports. Overlap LIFO fix landed separately in `NoteMovementUtils`. |
| 2026-06-22 | **Reopened.** Spike on `015537` vs `015318` serial: D1 confirmed via `#CAP DISP` (`frame=0` for 191/191 RECORD lines in `015537`; `190/191` with `frame>0` in `015318`). Root causes documented below. HITL `live_record_display` gate added (warn-only until D1 fix lands). **RC1 shipped:** `resolveDisplayNotes` live-record branch first; `exitEditMode` resets `MAIN_MODE_LOOP_EDIT`. |

---

## Spike 2026-06-22 — D1 root causes (confirmed / likely)

### RC1 — `NOTE_EDIT` main mode steals live record path (confirmed code + serial correlation)

`DisplayManager::resolveDisplayNotes` checks **`MAIN_MODE_NOTE_EDIT` before** `isLiveRecordingDisplay`:

```345:356:src/DisplayManager.cpp
    if (noteEditManager.getCurrentMainEditMode() == NoteEditManager::MAIN_MODE_NOTE_EDIT) {
        invalidateLiveDisplayCache();
        ...
        return track.getCachedNotes();
    }
```

During RECORD, `resolveDisplayLoopLength` still grows the piano-roll window via the live-capture branch, but notes come from **stale/empty `getCachedNotes()`** → loop grows, no bars.

`EditManager::exitEditMode` does **not** call `sendMainEditModeChange(LOOP_EDIT)` — main mode can stay **NOTE_EDIT** after a prior edit HITL run until reboot.

**Evidence:** `015318` (record at 41s, edit at 48s) had `frame>0` during RECORD (LOOP_EDIT). `015537` (record at 180s) had `frame==0` for every RECORD `DISP` line — consistent with NOTE_EDIT left active from the prior run on the same powered Teensy.

**Fix direction:** Prioritize `isLiveRecordingDisplay` over NOTE_EDIT cached path; reset main mode to LOOP_EDIT on `exitEditMode`.

### RC2 — Open-note tails vs growing `loopLength` (likely contributor)

`NoteUtils::findOpenNoteOns` skips events with `evt.tick >= loopLength`. During growing capture, `liveLoopLength = currentTick - startLoopTick` can be **shorter than the first note-on tick** until the playhead passes the note — open-note detection returns empty and `applyLiveOpenTails` adds nothing.

When `capturePreview.notes` is also empty (`useM4Sources == false`), `reconstructNotes` on a lone note-on returns empty → `frameNotes==0`.

When `capturePreview` is populated (`applyCaptureEventToPreview` on each insert), `frame>0` — explains `015318` behavior.

**Fix direction:** Prefer `capturePreview.notes` during RECORD; bump `captureDisplayRevision` in `Loop::appendCaptureEvent`; widen open-note scan for growing capture length.

### RC3 — HITL record prelude ARM / second press (related, not display-only)

First record press often lands **ARMED** (`!sequencerRunning` after script stops transport). Second press either:

- starts transport + RECORDING (`handleArmedRecordPress` + `!isTransportRunning`), or
- **cancels arm** if transport already running.

Long ARMED window: no capture events, no notes (expected). Script should ensure Teensy transport running before first record press, not send a blind second record press.

**Fix direction:** HITL — wait for `Transport started` on device; if `ARMED`, send transport start not second record.

### RC4 — `captureDisplayRevision` not bumped in `appendCaptureEvent` (design gap)

Revision is bumped in `Track::recordMidiEvents` when `isRecording() && !isPlaying()`, and in `beginCapture`, but **not** in `Loop::appendCaptureEvent` itself. Display cache relies on `liveEventCount` delta; loop-length-only updates can skip preview rebuild in the `else` branch until the final recording block runs.

**Fix direction:** Bump `captureDisplayRevision` on every successful `appendCaptureEvent` (today `Track::recordMidiEvents` bumps only when `isRecording() && !isPlaying()` — transport-running record skips the bump; `eventsAdded` usually still rebuilds, but NOTE_EDIT branch prevents reaching this path).
