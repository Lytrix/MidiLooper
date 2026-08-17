# Loop length change during overdub

**Status:** Queued — plan + tasks only; **no firmware** until CURRENT_WORK explicitly starts this  
**Date:** 2026-08-17  
**Kind:** enhancement  
**Evidence:** [`140355`](../../captures/session_20260817_140355.log) @ 25.541 s  
**Parent:** [`overdub_lifecycle_representation_authority.md`](overdub_lifecycle_representation_authority.md)  
**Frozen prior:** [`overdub_overlap_hold_display_cache_bugfix.md`](overdub_overlap_hold_display_cache_bugfix.md) (RC11/RC12)  
**OpenSpec:** [`slot-performance-interaction`](../../openspec/changes/slot-performance-interaction/specs/slot-performance-interaction/spec.md) — LOOP_EDIT PlaybackWindow metadata change resyncs playback (tasks 5.x still open)

---

## Invariant (one sentence)

**Changing loop length during overdub is allowed, and every overdub representation (`loop.loopLengthTicks`, source view, live display, LEN / MIDI feedback, playback wrap) must use that same length.**

North star:

```text
loop.loopLengthTicks
    → rebuildOverdubSourceView (if hasOverdubSourceView)
    → live display + visual cache invalidation
    → LEN + MIDI feedback
    → playback resync (per slot-performance-interaction)
```

---

## Architecture checkpoint (before coding)

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — extend `LoopEditManager` + existing `Loop::rebuildOverdubSourceView` |
| **State transition change?** | YES (bounded) — playback re-anchor on length/start settle while PLAYING/OVERDUBBING (already specified in OpenSpec, not shipped) |
| **Forbidden?** | Do not patch `appendOverdubPassDisplayNotes`. Do not make `visualCache` consume authority. Do not reopen RC11/RC12. |

If Stage 2 playback re-anchor is treated as a new transition owner rather than extending `queuePlaybackStartAtGrid` / `commitQueuedPlaybackStart`, stop and design with the user.

---

## Root cause ([`140355`](../../captures/session_20260817_140355.log))

Not an RC12 display defect. LOOP_EDIT stays active during play/overdub (startup policy). Length CC is accepted while overdubbing.

```text
22.897  Overdub session opened (loop 768 ticks / 1 bar)
24.891  First wrap committed @ tick 1296
25.541  CC ch=15 cc=2 value=80 → LOOP EDIT length 768 → 62208 (81 bars)
26.151  Loop geometry settled (undo pushed)
26.893  Second wrap committed
38.465  MIDI length CC feedback finally sent: 81 bars
```

| Symptom | Cause |
|---------|--------|
| Transport/wrap period jumped to 81 bars | `LoopEditManager::applyLoopLengthPreview` sets `loop.loopLengthTicks` immediately |
| Consume geometry still used 1-bar math until next wrap | `overdubSourceViewLoopLengthTicks_` updates only in `Loop::rebuildOverdubSourceView` |
| External length fader / length info stale until ~38 s | `sendCurrentLoopLengthCC` runs on session enter / `onGlobalGeometryRestored`, **not** on preview |
| No playback re-anchor | OpenSpec resync requirement not implemented (`openspec/changes/slot-performance-interaction/tasks.md` 5.x) |

OLED `LEN` in `DisplayManager::drawInfoArea` reads `getLoopLengthForSlot()` and should show 81 once `loop.loopLengthTicks` updates. Stale “length info” in this capture is **MIDI feedback** (and source-view / 16-bar paint window still keyed off the old period until wrap rebuild).

---

## Stage 1 — representation parity on length preview

**Owner:** `LoopEditManager::applyLoopLengthPreview` ([`src/LoopEditManager.cpp`](../../src/LoopEditManager.cpp))

After `loop.loopLengthTicks = newLoopLength` and `track.invalidateCaches()`:

1. If `track.isOverdubbing() && loop.hasOverdubSourceView()`, call `loop.rebuildOverdubSourceView(playheadPhaseTick)` so `overdubSourceViewLoopLengthTicks_` matches the new length. Use the same playhead phase owner as wrap rebuild (`loop.playheadPhaseTick` / `Track` capture phase tick — do not invent a second clock).
2. Invalidate live display cache (`DisplayManager::invalidateLiveDisplayCache` or the existing `track.invalidateCaches` path if that already drops `liveOverdubSourceViewNoteCount_`).
3. Call `sendCurrentLoopLengthCC(track)` after preview. Respect `shouldIgnoreLoopFaderInput` so motor/CC feedback cannot re-enter `handleLoopLengthInput`.

**Pending notes:** default is **preserve** `pendingNoteChanges_` and rebuild source view from LCR (do not force seal-before-rebuild on every fader tick). Native test must prove Hide/Shorten pending still applies after the length change. If rebuild from LCR would drop in-bar pending, stop and ask — do not seal as a workaround.

**Native fixture:** overdub session with source view established → `applyLoopLengthPreview` 768 → 1536 → assert `overdubSourceViewLoopLengthTicks() == 1536` and same-pitch geometry recomputed under the new length. Place in [`test/test_overdub_source_view/`](../../test/test_overdub_source_view/) or a small `test_loop_edit_overdub_geometry`.

**Stage commit:** representation only. No `queuePlaybackStartAtGrid` yet.

---

## Stage 2 — playback resync on geometry settle

**Owner:** `LoopEditManager::commitSettledLoopLength` / `commitPendingLoopGeometry` (settle path, **not** every preview tick)

`LOOP_GEOMETRY_SETTLE_MS` is **600 ms** ([`include/LoopEditManager.h`](../../include/LoopEditManager.h)). That is the commit point for playback resync so a fader sweep does not restart playback on every CC.

Implement the OpenSpec requirement ([`slot-performance-interaction` spec](../../openspec/changes/slot-performance-interaction/specs/slot-performance-interaction/spec.md) — LOOP_EDIT PlaybackWindow metadata change resyncs playback):

1. Silence track output
2. `Track::queuePlaybackStartAtGrid` at the new `loopStartTick`
3. Re-anchor `projectionCycleStartTick` and event indices so heard MIDI matches display playhead

Reuse [`Track::queuePlaybackStartAtGrid`](../../src/Track/TrackTransportControl.cpp) and existing slot-launch silence. Do not add a parallel restart FSM.

**HITL (this stage, not Stage 4 occupied-lane):** change length 1→2 bars during overdub; wrap boundary and playhead agree.

**Stage commit:** playback resync only. Keep Stage 1 native green.

---

## Stage 3 — observability + accidental-input guard (optional)

Not a consume/display RC. Separate product choice.

- Log `#CAP,DIAG,loop_geom,phase=overdub,len=...` on preview when overdubbing
- Optional: debounce / require settle before applying large jumps (81 bars from CC `value=80` in `140355`) — **not** the representation fix

Do not mix this into Stage 1.

---

## Stage 4 — device gate

Repeat occupied-lane scenario on a 1-bar loop **with an intentional** length change 1→2 bars mid-overdub.

Assert:

- `LEN` + MIDI feedback + `DISP` loop-length field + `overdubSourceViewLoopLengthTicks_` all match
- After wrap: `slice_clean notes=N` matches `DIAG,lcr,src,why=wrap,notes=N`
- Standing 1-wrap [`005745`](../../captures/session_20260817_005745.log) still green (no mid-overdub length change in that capture)

---

## Files (expected)

| Stage | Files |
|-------|--------|
| 1 | `src/LoopEditManager.cpp`, `src/Loop/LoopCapture.cpp` (`rebuildOverdubSourceView`), display cache invalidate, native test |
| 2 | `src/LoopEditManager.cpp` settle path, `src/Track/TrackTransportControl.cpp`, `src/TrackManager/` as needed |
| 3 | capture log only |
| 4 | HITL only |

### Out of scope

- NOTE_EDIT display refresh (hydrate / recon investigation)
- Blocking loop-length input during overdub (full support path)
- Changing `appendOverdubPassDisplayNotes` or RC12 display authority

---

## Open before coding

1. Confirm settle debounce (`LOOP_GEOMETRY_SETTLE_MS` = 600) is the playback-resync point (not every preview tick). **Plan default: yes.**
2. Preserve pending vs force seal-before-rebuild. **Plan default: preserve; native must prove it.**
3. Whether Stage 4 must also re-run [`005745`](../../captures/session_20260817_005745.log) with no length change. **Plan default: yes (regression), plus a separate 1→2 bar intentional-change run.**

### Proceed?

**This session:** plan + tasks only. Firmware starts when CURRENT_WORK moves this out of “queued — do not start firmware.”
