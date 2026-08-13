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
| Representation | `Loop::visualCache.notes`, `capturePreview.notes` (from `projectDisplayNotes` / merge path) |
| Interval | `DetailedWindowContext` / `DisplayWindowUtils` `TickInterval` on **post-projection** notes |

---

## Owner

**`DisplayManager`** owns draw orchestration. **`Loop`** owns display representation build (`rebuildVisualCacheFromPasses`, `visualCache.revision`).

Display **does not** mutate passes or session store.

---

## Build policy (target)

| Phase | Policy |
|-------|--------|
| Stopped / NOTE_EDIT | Rebuild display representation when revision stale; filter by window on read. NOTE_EDIT paint committed base is `visualCache.notes` (`getVisualNotesForSlot`); `NoteEditCurrentState` overlays this-session Hide / Shorten / move / add only. At NOTE_EDIT open, missing current-state rows are filled as Visible from that same list, and unedited Existing Visible rows take the visual-cache span (rematerialize off-pairing does not keep a longer end). Paint does not add rematerialize-only Visible rows that are not in the committed base, except this-session Added. A Visible row that matches a committed display note by span still paints when the cache row has no NoteId. Hidden / Deleted / this-session geometry are not overwritten. NOTE_EDIT overlap evaluate and leave-restore use that same display span for unedited Existing rows — not rematerialize pairing. Select / focus rebuild set `focus.last` from the painted DisplayNote and Visible current-state span, not per-select rematerialize. Macro commit does not emit Length or NoteRange to `loopLength` for an overlap participant that `visualCache.notes` does not paint (or paints with `endTick == startTick`). |
| PLAYING | **Defer** full representation rebuild — use last good representation + playhead overlay; schedule rebuild in `Track::processDeferredIdleMaintenance` |
| Live record | Incremental `capturePreview` — not full-loop flatten per frame |

**Anti-pattern (64-bar regression):** `ensureVisualCacheBuilt()` on every PLAYING frame or from `MidiLedManager` bar-LED path.

---

## Two-step display (required)

1. **Representation** — `IntervalProjection::projectDisplayNotes` → `DisplayNote` list (head/tail, open tails).
2. **Interval** — viewport filter via `TickInterval::intersects` on that list.

Window move should adjust step 2 only when representation revision unchanged.

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
