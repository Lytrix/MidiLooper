## Context

`DisplayManager::drawPianoRoll` currently maps the full `loopLength` onto `pianoRollWidth()`:

```788:816:src/DisplayManager.cpp
    const uint32_t loopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
    ...
    drawGridLines(jamLength, pianoRollY0, pianoRollY1);
    drawAllNotes(track, displaySlot, currentTick, 0, jamLength, minPitch, maxPitch, notes);
```

For a 64-bar loop this compresses 64 bars into the same horizontal pixels as 2 bars, making individual notes unreadable. The display path may also reconstruct or hold more `DisplayNote` entries than the detailed layer needs.

Display scaling was scoped in `record-stop-64-bar-crash` §6 but parked when the RAM2 root cause was identified. This change implements that display scope as a follow-on after `long-record-memory-headroom`.

**Locked product decisions (from plan):**
- Detailed window is **fixed at loop start** (tick 0, bars 0–15) until the user moves it in LOOP_EDIT (M2); no auto-follow playhead.
- M1 is display-only; M2 adds LOOP_EDIT move/resize controls.
- Overview strip v1 is binary **has notes** / **no notes** with dynamic grouping (1/2/4/8/16/32/64 bars).

Primary files: `DisplayManager.cpp`, `DisplayManager.h`, `LoopEditManager.cpp`, `DebugSessionCapture.h`.

## Goals / Non-Goals

**Goals:**
- Readable piano roll for loops > 16 bars via a capped detailed window.
- Full-loop context via overview strip (presence bins + window box + playhead on strip).
- LOOP_EDIT navigation to move and resize the window (1–16 bars).
- HITL-verifiable `#CAP DISP` / `#CAP OVW` markers for 32/64-bar loops.
- No full-loop per-frame note reconstruction for the detailed layer.

**Non-goals:**
- Storage, capture, persistence, or RAM2 policy (prerequisite change).
- NOTE_EDIT display changes.
- Jam-mode arrangement navigation.
- Overview velocity/density shading in v1.

## Decisions

### Decision 1: Detailed window state on DisplayManager (per active slot)
Store `detailedWindowStartTick` (default `0`) and `detailedWindowBars` (default `16`, capped) per display slot on `DisplayManager`. When `loopLengthTicks ≤ 16 * BAR_TICKS`, use full-loop rendering (today's behavior) and hide or collapse the overview strip.

**Rationale:** Window is a display concern, not loop storage. Per-slot state lets each of the 8 slots remember its own view when the user switches slots (open question 3 resolved toward per-slot).

**Alternatives considered:**
- Global single window for the track: rejected; switching slots would show the wrong region.
- Store on `Loop`: rejected; window is UI navigation, not loop geometry.

### Decision 2: Filter notes on read; no new display module type
Filter `DisplayNote` list to the window tick range (wrap-aware) in `DisplayManager` before `drawAllNotes`. Reuse `visualCache` / live display buffer sources; do not add a new top-level display noun.

**Rationale:** Matches repo naming rules (filter on existing types, no `*View` module).

### Decision 3: Overview strip placement and content
Place the strip in the gap below the piano roll (`y ≤ 31`) if space permits; otherwise reuse the lowest piano-roll row. Segments use dynamic `barsPerSegment` so the full loop fits the display width. Each segment is filled when any note intersects that tick span. Draw a hollow box or start/end markers for the detailed window and a playhead tick on the strip at full-loop position.

**Rationale:** Carries forward parked Decision 6 from `record-stop-64-bar-crash`. Binary presence is cheap and sufficient for v1 navigation.

### Decision 4: Fixed window + playhead on overview (tradeoff accepted)
When the detailed window stays at bars 0–15, the playhead cursor may leave the detailed piano roll during playback of later bars. M1 draws playhead position on the overview strip. M2 lets the user move the window in LOOP_EDIT.

**Rationale:** User explicitly chose fixed-at-start over auto-follow playhead.

### Decision 5: M2 control map (TBD before M2 implement)
Proposed default (resolve in open questions before M2):
- **Fader 3** — move window start along loop (bar-quantized).
- **Hold-turn encoder** — resize window length (1–16 bars).

**Alternatives:** swap fader/encoder roles; use bar-step buttons for coarse move.

### Decision 6: DISP capture extension
When `loopLen > 16 * BAR_TICKS`, extend `#CAP DISP` with `windowStartTick`, `windowBars`, `windowNoteCount`. If the line grows too wide, add `#CAP OVW` for overview-specific fields.

**Rationale:** Reuses existing HITL DISP parsing infrastructure from `edit-record-display-length-mode`.

## Risks / Trade-offs

- **[Risk] Playhead invisible in detailed roll for bars > 16 until user moves window** → **Mitigation:** playhead on overview strip in M1; M2 LOOP_EDIT move.
- **[Risk] Overview strip steals vertical space** → **Mitigation:** prefer gap below piano roll; fall back to one piano-roll row; confirm on hardware before M1 lands.
- **[Risk] Window filter breaks live-record display (D1)** → **Mitigation:** during growing capture, cap window at min(16 bars, current live length); run D1 HITL gates.
- **[Risk] Per-frame bin computation for overview** → **Mitigation:** compute presence bins once per loop revision / visual-cache invalidation, not every frame.

## Migration Plan

1. Land M1: window state, note filter, overview strip, DISP/OVW markers, native tests.
2. HITL: 32/64-bar display gates; confirm no D1 `frameNotes == 0` regression.
3. Land M2: LOOP_EDIT fader/encoder wiring, move + resize.
4. HITL: scripted LOOP_EDIT window move on 64-bar loop.
5. `pio test -e native`; update plan doc cross-link if needed.

## Open Questions

- Strip vertical placement on hardware: gap below `y = 31` vs steal one piano-roll row?
- M2 control map: fader 3 = move vs length; encoder role; document in `MidiMapping` comments.
- Live record/overdub: window caps at min(16 bars, current capture length) — confirm on device.
