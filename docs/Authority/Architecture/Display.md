# Runtime architecture — display consumer

**Parent:** [RuntimeArchitecture.md](RuntimeArchitecture.md)

**Naming:** [NAMING.md](../NAMING.md) — Display read paths; avoid new `*View` types.

---

## Runtime request

```
Derived Display Representation  +  Display interval  →  Visible pixels / CAP telemetry
```

| Input | Today |
|-------|-------|
| Representation | Sealed committed source (`LoopSnapshot` when complete, else bounded prepared/committed) plus `capturePreview.notes`. `visualCache.notes` is a reuse/handoff seed only — it does not decide whether committed notes exist. |
| Interval | `DetailedWindowContext` / `DisplayWindowUtils` `TickInterval` on **post-projection** notes |

Display is the piano-roll consumer. It does **not** feed `playMidiEvents`. Idle fills dirty bars (2–4) into `visualCache`; OLED filters the paint window. Sixteen-step and bar LED pads read the same `visualCache.notes` list (plus `capture.store` while overdubbing); they do not paint OLED. See [DerivedViews.md](DerivedViews.md) § Consumers.

---

## Owner

**`DisplayManager`** owns draw orchestration. **`Loop`** owns display representation build (`rebuildVisualCacheFromPasses`, `visualCache.revision`).

Display **does not** mutate passes or session store.

---

## Build policy (target)

| Phase | Policy |
|-------|--------|
| Stopped / NOTE_EDIT | Keep `visualCache.notes` on STOPPED track/slot switch (`markDisplayCachesStale`). STOPPED committed display finalization uses `LoopContentResolution::resolveDisplayNotes`, so the first frame reuses effective geometry instead of finalizing stale cache content. Idle fills dirty bars with a budgeted gather and deferred wrap-append. Rebuild display representation when revision stale; filter by window on read. **Target compose (DEC-037 Editor amendment):** same as overdub — committed `visualCache.notes` + this-session overlay. Overlay is neighborhood `NoteEditCurrentState` rows (mover + participants), not a full-loop `EditSession.store` and not a current-state row per cache note. Today: `projectedNoteEditDisplayNotes` uses the current `LoopSnapshot` as committed base when `visualCache` is dirty and that root is current (`ensureNoteEditDisplayProjectionCachesBuilt`, 4.4d); otherwise the Runtime Request consumes `getVisualNotesForSlot` / VisualCache (may be stale — dirty bars are idle maintenance debt). Rematerialize-when-dirty on the request is forbidden (RC-A). A missing current-state row is not Hidden (paint the committed-base note). Unedited Existing Visible rows take the visual-cache span. Paint does not add rematerialize-only Visible rows except this-session Added. Overlap evaluate / leave-restore / `focus.last` use the painted DisplayNote span. Macro commit does not emit Length or NoteRange to `loopLength` for an overlap participant that `visualCache.notes` does not paint (or paints with `endTick == startTick`). Rematerialize-when-dirty for paint is not the architecture. |
| PLAYING / OVERDUBBING | **Defer** full representation rebuild. Overdub paint composes from the complete sealed committed source (`LoopSnapshot` when complete, else bounded prepared/committed plus uncovered `sealedCapturePreview`) plus `capturePreview`. `visualCache.notes` may reuse that frame only when it does not predate the last seal and covers the requested window. Overdub wrap marks affected bars via `markAffectedDisplayCacheRanges` and keeps `visualCache.notes`; it does not `markDisplayCachesStale` / clear notes. Schedule cache fill in `Track::processDeferredIdleMaintenance`. |
| Live record | Incremental `capturePreview` — not full-loop flatten per frame |

**Anti-pattern (64-bar regression):** `ensureVisualCacheBuilt()` on every PLAYING frame or from `MidiLedManager` bar-LED path.

---

## Two-step display (required)

1. **Representation** — `IntervalProjection::projectDisplayNotes` → `DisplayNote` list (head/tail, open tails).
2. **Interval** — viewport filter via `TickInterval::intersects` on that list.

Window move should adjust step 2 only when representation revision unchanged.

## Overdub-stop handoff

`Track::stopOverdubbing` publishes the sealed overdub pass before changing to
`PLAYING` and queues `queueSessionExitSnapshot`. `DisplayManager::refreshViewportAfterOverdubStop`
holds the last OVERDUBBING composed `liveDisplayNotes` vector. It does not
pass-gather or snapshot-gather on the stop stack. The first new committed paint
uses `LoopContentResolution::resolveDisplayNotes(*snapshot)` only after
`publishedSnapshotIsCurrent()`. Occupy/pending geometry on the held frame
(`225234`) is accepted until that successor is current.

The invariant is observable in `#CAP,DISP`: the first `OVERDUBBING → PLAYING`
frame must have the same frame and effective counts as the settled frame.
HITL [`session_20260907_230846.log`](../../captures/session_20260907_230846.log)
passed this invariant (`98`/`98` on the first and settled PLAYING frames).

---

## Telemetry

`#CAP DISP` / `SC_DISP` report bounded-window fields for HITL — interval metadata, not a second representation.

Future long-loop overview strip: binary presence bins per segment (cheap), not full note list per frame — see `openspec/changes/long-loop-piano-roll-window/`.

---

## Related

- [DerivedViews.md](DerivedViews.md) — display representation owner
- [IntervalProjection.md](IntervalProjection.md) — wrap before viewport filter
- [Guides/control-surface/Display.md](../../Guides/control-surface/Display.md) — control surface
- [plans/long_loop_piano_roll_overview_enhancement.md](../../Plans/long_loop_piano_roll_overview_enhancement.md) — window UX
