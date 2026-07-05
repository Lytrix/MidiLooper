## Why

Loop wrapping and linearization are implemented independently in display (`reconstructNotes`), playback (`ensurePlaybackWindowBuilt`), and the planned edit path (`normalizeWrapToLinear` in `edit-session-action-geometry`). Each answers the same question — given canonical linear note storage, what interval representation fits the current working window? — with duplicated math and divergent edge cases (152335, display-wrap skip branches, overlap linear-span helpers).

This change introduces a **single interval projection layer** as the common foundation for playback, display, and editing. Storage stays canonical (DEC-014 / `linear-loop-tick-storage`). **Full-stack migration** (Edit + Display + Playback) must ship before **`edit-session-action-geometry`** overlap pipeline implementation resumes.

Brownfield: [`docs/Guides/NOTE_WRAPPING_LOGIC.md`](../../docs/Guides/NOTE_WRAPPING_LOGIC.md), [`linear-loop-tick-storage`](../linear-loop-tick-storage/proposal.md), [`edit-session-action-geometry`](../edit-session-action-geometry/proposal.md) (blocked).

## What Changes

- **`IntervalProjection` engine** — `generateEquivalentIntervals` (pure math) + `selectProjectedInterval` (consumer policy) + batch `projectNoteIntervals` + **`projectDisplayNotes()`**
- **`ProjectionContext`** — coordinate space: `loopLength` + **`TickInterval` `window`**; consumer extensions (`originTick`, `projectionCycleStartTick`, `loopStartTick`, `selectedTick`, `queuedStartTick`)
- **`TickInterval`** — shared `{ start, end }` primitive; `length()` derived
- **Tick vocabulary** — global `currentTick`; persisted **`loopStartTick`** (loop startpoint); runtime **`selectedTick`** (select/bracket); rolling **`projectionCycleStartTick`**; one-shot **`queuedStartTick`** (slot/bar/16th restart)
- **Edit projection** — replaces planned `normalizeWrapToLinear`; overlap analyze receives post-projection intervals only; **`wraps`** retired
- **Display projection** — `projectDisplayNotes()` owns head/tail adapter; `reconstructNotes` thin delegate; long-loop viewport filter is separate
- **Playback projection** — full rolling `projectionCycleStartTick`; `ensurePlaybackWindowBuilt` via shared engine
- **Queued start** — per-track + bar press at configurable grid (default 16th); mutually exclusive, last wins
- **LoopTriggerSequence (Phase 7)** — explicit **Start**/**End** **`TriggerEvent`** stream; **`PlaybackTarget`**; LTS → **`ProjectionContext`** → UIP; Manager coordinates / Track performs
- **Loop slice metadata (Phase 8)** — 16th-granularity windows on **Loop**; **`TriggerEvent`** may reference slices
- **Modulo centralization** — phase/wrap helpers move into `IntervalProjection`
- **Block** `edit-session-action-geometry` firmware until UIP Phases 1–5 + HITL pass

## Capabilities

### New Capabilities

- **`unified-interval-projection`**: Single projection engine; context-driven interval generation and selection; no write-back to canonical storage

### Modified Capabilities

- **`loop-wrap-projection`** (delta in this change): Implementation path moves to `IntervalProjection` engine; head/tail vs viewport window clarified
- **`edit-session-action-geometry`** (active change delta): Pipeline step 3 = Edit projection; `normalizeWrapToLinear` removed; **`wraps`** retired
- **`note-edit-modification-session`**: Edit inventory and geometry read paths use projection layer only

## Impact

- **New modules:** `include/Utils/IntervalProjection.h`, `src/Utils/IntervalProjection.cpp`
- **Edit:** `NoteEditFocus.cpp`, `NoteEditManager`, `SelectNavigation` (thin-wrap phase helpers)
- **Display:** `NoteUtils.cpp`, `DisplayWindowUtils.cpp`, `DisplayManager.cpp`
- **Playback:** `Track.cpp`, per-slot queued start runtime
- **Tests:** new `test_interval_projection`; extend reconstruct, display window, note edit focus
- **Blocked:** [`edit-session-action-geometry`](../edit-session-action-geometry/) Phases 1–4 until UIP complete

## Non-Goals

- Changing canonical storage format, SD layout, or `NoteId` model
- `edit-session-action-geometry` overlap pipeline implementation (explicitly blocked)
- Timeline / long-loop viewport editing (`ProjectionType::Timeline` deferred)
- Capture hot-stop wrap (`capture-pass-boundary-materialization` — separate OpenSpec)
- Phase 3 capture jams (future OpenSpec)
- Renaming brownfield `Loop.startLoopTick` field in SD (migration separate from UIP)

## Delivery Sequence

| Phase | Work |
|-------|------|
| **0** | OpenSpec sign-off |
| **1** | Core engine + native tests |
| **2** | Edit projection |
| **3** | Display projection (`projectDisplayNotes`) |
| **4** | Playback projection + queued start + rolling cycle |
| **5** | Integration + HITL |
| **6** | Sync overlap OpenSpec; resume derived overlap logic |
| **7** | **`LoopTriggerSequence`** — **`TriggerEvent`** pairs, LTS Edit Mode, SD persist, row rename |
| **8** | Loop slice metadata (16th windows on **Loop**) |

## Relationship to Active Work

| Change | Relationship |
|--------|--------------|
| `linear-loop-tick-storage` | Prerequisite — canonical linear pairs; UIP extends projection only |
| `edit-session-action-geometry` | **Blocked** until UIP full stack |
| `long-loop-piano-roll-window` | Future `Timeline` consumer |
| `note-edit-tick-coordinates-and-audition` | `selectedTick` / `loopStartTick` separation preserved |
