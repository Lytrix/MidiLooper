# Design — unified-interval-projection

**Date:** 2026-07-05 (refined)  
**Status:** Design session — blocks `edit-session-action-geometry` until full-stack migration ships  
**Handoff:** [`docs/plans/unified_interval_projection_enhancement.md`](../../docs/plans/unified_interval_projection_enhancement.md)

---

## Context

Canonical loop MIDI storage is **linear** (`NoteOff.tick >= NoteOn.tick`, may exceed `loopLength`) per `linear-loop-tick-storage`. Loop boundaries exist only in **projection** — temporary derived intervals for playback, display, and edit analysis.

Today wrap math is duplicated:

| Location | Role |
|----------|------|
| `NoteUtils::reconstructNotes` | Display head/tail segments |
| `DisplayWindowUtils` | Viewport intersection with modulo |
| `Track::ensurePlaybackWindowBuilt` | Playback order via `% loopLength` |
| `NoteEditFocus` | `resolveLinearNoteSpanForOverlap`, `isInflatedDisplaySpan` |
| `NoteMovementUtils` | Ad-hoc unwrap, display-wrap skip |
| `edit-session-action-geometry` (planned) | `normalizeWrapToLinear` before analyze |

**Core principle:** Storage is global; projection is local. Changing viewport, playback window, or edit window must not require changing stored notes or downstream algorithms — only **`ProjectionContext`**.

---

## Goals / Non-Goals

**Goals:**

- Single mathematical model: generate equivalent intervals → select best for context
- All consumers (Playback, Display, Edit v1) share one implementation
- Overlap analyze, constraint resolution, overlay generation receive **normalized projected intervals** — never ask "does this wrap?"
- Full-stack migration before overlap pipeline resumes

**Non-Goals:**

- Timeline/clip editing (`ProjectionType::Timeline` stub only)
- Storage format changes
- Capture hot-stop boundary materialization
- Overlap pipeline (`edit-session-action-geometry`) in this change

---

## Architecture

```
Canonical Storage (MidiEvent pairs / NoteBaseline — linear ticks)
        │
        ▼
ProjectionContext (per consumer)
        │
        ▼
generateEquivalentIntervals()     ← pure math, no policy
        │
        ▼
selectProjectedInterval()         ← consumer policy only
        │
        ▼
ProjectedNoteInterval(s)          ← consumer input
        │
        ├── Playback → ensurePlaybackWindowBuilt
        ├── Display  → projectDisplayNotes() → DisplayNote list
        └── Edit     → analyzeEditSessionInteractions inputs
```

**Ownership:**

| Layer | Owner | Mutates storage? |
|-------|-------|------------------|
| Canonical storage | `EditSession.store`, loop passes, SD | Yes — via apply/normalize only |
| Interval projection | `IntervalProjection` (`Utils/`) | **No** |
| Playback consumer | `Track.cpp` | No (reads projection) |
| Display consumer | `IntervalProjection::projectDisplayNotes`, thin `NoteUtils` wrapper | No |
| Edit consumer | Overlap orchestrator (future) / `NoteEditManager` | No |
| Live geometry apply | `applyEditSessionActions` (overlap change, later) | Yes |

---

## Data structures

```cpp
// Shared primitive — interval is primary; length is derived (coordinate model §3–§4)
struct TickInterval {
  int32_t start;
  int32_t end;
  int32_t length() const { return end - start; }
  bool intersects(const TickInterval& other) const {
    return start < other.end && end > other.start;
  }
};

enum class ProjectionType : uint8_t {
  Playback,
  Display,
  Edit,
  Timeline,  // reserved — long-loop viewport / future clip window
};

// ProjectionContext = coordinate space + consumer extensions (coordinate model §2)
struct ProjectionContext {
  uint32_t loopLength;             // period (duration) — NOT an interval; used for ±k·L shifts
  TickInterval window;             // inclusive working coordinate window [start, end)
  ProjectionType type;
  // Playback extensions
  int32_t originTick = 0;
  int32_t projectionCycleStartTick = 0;
  bool useQueuedStart = false;
  int32_t queuedStartTick = 0;
  // Loop geometry (playback + edit + display)
  int32_t loopStartTick = 0;       // persisted loop startpoint (Loop.loopStartTick)
  // Edit extensions
  int32_t selectedTick = 0;        // runtime select/bracket tick (NOTE_EDIT)
};

struct CanonicalNoteSpan {
  NoteId noteId;
  TickInterval interval;  // storage linear on → off
  uint8_t pitch;
  uint8_t velocity;
};

struct ProjectedNoteInterval {
  NoteId noteId;          // identity invariant — same note across all k copies (coordinate model §8)
  TickInterval interval;  // working coordinates for this context
  uint8_t pitch;
};
```

**API (action + scope):**

```cpp
// Pure — bounded k shifts by loopLength; deterministic order
std::vector<ProjectedNoteInterval> generateEquivalentIntervals(
    const CanonicalNoteSpan& span,
    uint32_t loopLength,
    const ProjectionContext& context);

// Policy — consumer-specific selection (Stage 2); may return one or many (Display)
ProjectedNoteInterval selectProjectedInterval(
    span<const ProjectedNoteInterval> candidates,
    const ProjectionContext& context);

std::vector<ProjectedNoteInterval> selectProjectedIntervalsForDisplay(
    span<const ProjectedNoteInterval> candidates,
    const ProjectionContext& context);

// Batch helper
std::vector<ProjectedNoteInterval> projectNoteIntervals(
    span<const CanonicalNoteSpan> spans,
    const ProjectionContext& context);

// Edit-specific context builder (orchestrator)
ProjectionContext buildEditProjectionContext(
    const EditorSelection& selection,
    uint32_t loopLength,
    TickInterval analysisWindow);

std::vector<ProjectedNoteInterval> projectEditIntervalsForAnalysis(
    span<const CanonicalNoteSpan> spans,
    const ProjectionContext& context);

// Display — projection (interval selection) + rendering (head/tail split) (coordinate model §7)
NoteUtils::DisplayNoteVec projectDisplayNotes(
    span<const CanonicalNoteSpan> spans,
    const ProjectionContext& context,
    uint32_t playheadTick = UINT32_MAX);  // live capture open-tail extension (rendering stage)
```

**Module home:** `include/Utils/IntervalProjection.h`, `src/Utils/IntervalProjection.cpp`

---

## Tick vocabulary (D15)

| Tick | Owner | Role |
|------|-------|------|
| **`currentTick`** | `ClockManager` | One global monotonic transport tick (positive only) |
| **`startLoopTick`** | `Loop` (brownfield) | Record/phase origin in global space; maps to `projectionCycleStartTick` during migration |
| **`loopStartTick`** | `Loop` (persisted) | Stored loop startpoint in projected interval `[0, L)`; defaults from record/overdub; editable via LOOP_EDIT Fader 1 after first bar-quantized commit |
| **`selectedTick`** | NOTE_EDIT session runtime | Selection/bracket tick inside projected interval; drives Fader 1 select, Fader 2/3 dependent geometry, 16th-step logic (today: `bracketTick`) |
| **`projectionCycleStartTick`** | Track runtime | Rolling global cycle origin; `+= loopLength` on each wrap (D13); one per track |
| **`queuedStartTick`** | Track runtime | One-shot queued restart/scrub; applied at configurable grid commit; then normal wrap (replaces brownfield `jamTick` target semantics) |
| **`triggerSequenceTick`** | **`LoopTriggerSequenceManager`** | Capture timeline for **`TriggerEvent`** record/overdub |

**Two concepts on `Loop` today:** `startLoopTick` (global phase anchor) and `loopStartTick` (in-loop startpoint). UIP keeps **`loopStartTick`** as the persisted loop geometry field. Do **not** rename it to `selectedLoopTick`. **`selectedTick`** is separate runtime selection state.

**Track / Loop / Slot ownership:**

| Owner | Stores |
|-------|--------|
| **ClockManager** | `currentTick` |
| **Track** | Effective playback tick, **`projectionCycleStartTick`**, **`queuedStartTick`**, pending grid commit |
| **Loop** | Canonical storage, `loopLength`, **`loopStartTick`**, slice metadata |
| **Slot** | Thin initializer adapter — no tick runtime |
| **`LoopTriggerSequenceManager`** | **`TriggerEvent`** list, **`triggerSequenceTick`**, LTS Edit Mode state, local undo |

### Playback position vocabulary — not one `PlaybackCursor` (D21)

Brownfield **`PlaybackCursor`** is **not** the global playhead. It is a per-slot merge-cache struct inside **`LoopPlaybackRuntime`**, used only from **`Track::playMidiEvents`** / **`playMidiEventsForSlot`**. Other playback positions use the ticks above and **`Loop.nextEventIndex`**.

| Concern | Brownfield owner | After UIP |
|---------|------------------|-----------|
| Global transport | **`currentTick`** | unchanged |
| Phase in loop / wrap | **`startLoopTick`** + **`tickPhaseInLoop`** | **`projectionCycleStartTick`** + **`IntervalProjection`** helpers (D13) |
| Next MIDI event to send | **`Loop.nextEventIndex`** | unchanged (scan index on **`Loop`**) |
| Last phase sent | **`Loop.lastTickInLoop`** | unchanged; **remove duplicate on `PlaybackCursor`** |
| Merged events cache | **`PlaybackWindow.primaryWindow`** | unchanged; build path may call projection |
| Playback order sort | **`rebuildPlaybackOrder`** / **`playbackSortPhase`** | **`IntervalProjection`** playback policy (Phase 4) |
| Per-slot runtime cache | **`PlaybackCursor`** + **`loopHeadWindow`** | **removed** — see D21 |
| **`PlaybackWindow` dead fields** | `windowStartBar`, unused `effectiveWindowBars` | **removed** — D22 |
| Display viewport filter | **`DisplayWindowUtils`** `% loopLength` | **`TickInterval`** intersection after projection — D22 task 3.3 |
| OLED viewport state | **`DetailedWindowContext`** start+length | **`TickInterval`** — D22 |
| LTS sequence position | — | **`triggerSequenceTick`** on Manager (Phase 7) |

---

## Display: projection vs rendering (coordinate model §7)

| Stage | Responsibility | Owner |
|-------|----------------|-------|
| **Projection** | Select interval(s) intersecting `window` — typically one equivalent copy per canonical span | `selectProjectedIntervalsForDisplay` / batch pipeline |
| **Rendering** | Split wrapped intervals into drawable head/tail segments (e.g. `-60→120` → `-60→0` + `0→120`); live capture open-tail | **`projectDisplayNotes()`** adapter only |

Playback and Edit do **not** use head/tail rendering — they consume projected intervals directly.

---

## Display segments vs viewport window

| Concept | Meaning |
|---------|---------|
| **Head/tail segments** | One stored note crossing the loop boundary → Display adapter may emit 1–2 `DisplayNote` rows (tail at loop end + head from tick 0). Core engine returns **one** `ProjectedNoteInterval`; split is in **`projectDisplayNotes()`**. |
| **Long-loop window** | Viewport filter for loops >16 bars (`DisplayWindowUtils`, future `Timeline`). Runs **after** reconstruction — not the same as head/tail split. |

---

## Decisions

### D1 — Storage remains canonical

**Decision:** UIP projects **from** existing `MidiEvent` pairs and `NoteBaseline`. No new persistent `startTick/endTick` storage struct. Invariants 1–7 from `linear-loop-tick-storage` unchanged.

### D2 — Two-stage pipeline: mathematics then consumer policy

**Decision:** Treat Stage 1 and Stage 2 as **independent concepts** throughout spec and code.

| Stage | Function | Knows about |
|-------|----------|-------------|
| **Stage 1 — equivalent interval generation** | `generateEquivalentIntervals` | Canonical span, `loopLength`, `window` bounds for k — **pure math only** |
| **Stage 2 — consumer projection policy** | `selectProjectedInterval` / `selectProjectedIntervalsForDisplay` | `ProjectionType`, playhead, selection, viewport |

Stage 1 MUST NOT branch on playback, editing, display, overlap, or rendering. Stage 2 MUST NOT reimplement ±k·L shift math.

**Example:** Storage `900 → 1080`, loop `960`. Equivalent intervals include `-60→120`, `900→1080`, `1860→2040`, … Generator produces a **bounded** deterministic set (k derived from `window` extent — see algorithm section).

**Alternative rejected:** Each consumer reimplements shift-by-loopLength — current brownfield duplication.

### D3 — ProjectionContext represents the coordinate space

**Decision:** `ProjectionContext` is the **coordinate system** in which projection occurs — not a miscellaneous parameter bag.

**Common core (every consumer):**

```cpp
ProjectionContext {
  loopLength;   // period — duration, not an interval (see D3b)
  window;       // TickInterval { start, end }
  type;
}
```

**Consumer extensions** (same struct; populated per consumer):

| Consumer | Additional fields |
|----------|-------------------|
| **Playback** | `originTick`, `projectionCycleStartTick`, `queuedStartTick`, `loopStartTick` |
| **Edit** | `originTick`, `selectedTick`, `loopStartTick` |
| **Display** | `loopStartTick` (today); no extra fields required v1 |
| **Timeline** | Reserved — viewport-nearest policy (coordinate model §9) |

No hard-coded loop wrap inside subsystems — only context construction differs.

| Consumer | `ProjectionType` | Context construction |
|----------|------------------|----------------------|
| Playback | `Playback` | `PlaybackWindow` preload; `originTick` from global `currentTick` or `getEffectivePlaybackTick` (D12) |
| Display | `Display` | Piano-roll viewport as `TickInterval` from `DisplayWindowUtils` |
| Edit | `Edit` | `EditorSelection.primaryNote` linear start as `originTick`; v1 `window` = full loop |
| Timeline | `Timeline` | Reserved — `long-loop-piano-roll-window` M2 |

### D3b — TickInterval primitive; loopLength stays a length

**Decision:** Introduce **`TickInterval`** `{ start, end }` as the shared primitive. **`length()`** is computed (`end - start`) — interval is primary.

- **Windows, spans, viewports, selection ranges** → `TickInterval`
- **`loopLength`** → duration (periodicity for `candidate + k × loopLength`) — **not** wrapped in `TickInterval`

Overlap test uses interval intersection directly:

```
candidate.interval.start < window.end
&& candidate.interval.end   > window.start
```

**Alternative rejected:** `windowStart` + `windowLength` as primary coordinates — redundant with interval model and repeats `end = start + length` at call sites.

See **D23** — **`window`** shares **`TickInterval`** geometry with projected spans but is never typed as **`ProjectedNoteInterval`**.

### D4 — Selection policy per ProjectionType

| `ProjectionType` | Rule |
|------------------|------|
| **Playback** | Select one interval intersecting `window`; prefer interval containing `originTick` (playhead) |
| **Display** | Select **every** candidate interval intersecting `window` (typically one per span); **rendering** (head/tail split) is a separate stage in **`projectDisplayNotes()`** |
| **Edit** | Select one contiguous interval closest to `originTick` (primary edited note) |
| **Timeline** | Reserved — interval nearest editor viewport (coordinate model §9); no storage or generator changes |

**Display adapter:** `NoteUtils::reconstructNotes` becomes thin delegate to **`projectDisplayNotes()`**. Live capture open-tail: extend projected interval end to playhead tick in working coordinates (display only; storage unchanged at stop).

### D5 — Edit projection replaces normalizeWrapToLinear

**Decision:** `edit-session-action-geometry` pipeline step 3 becomes **`projectEditIntervalsForAnalysis`** (Edit projection). Overlap **`analyzeEditSessionInteractions`** receives post-projection intervals only.

**Retired:** `normalizeWrapToLinear` as a separate function name — absorbed into Edit projection.

### D6 — wraps attribute retirement

**Decision:** **`EditSessionInteraction.wraps`** is **retired**. Edit projection fully normalizes working coordinates; resolver **`computeShortenedEndTick`** uses projected `causingSpan` / `baselineSpan` only. No conditional gate.

### D7 — No consumer-local wrap math after migration

**Decision:** Post-migration grep gate: no independent `% loopLength` wrap classification in `NoteUtils`, `DisplayWindowUtils`, `Track`, `NoteEditFocus`, `NoteMovementUtils`, `MidiLedManager`, `SelectNavigation` except inside **`IntervalProjection.cpp`** helpers.

**Centralized helpers (examples):** `tickPhaseInLoop`, `noteRelativeTick`, `noteStorageTick`, LED/display phase mapping, playback sort phase. Scheduling grid checks (e.g. `currentTick % gridTicks == 0` in `SlotStateMachine`) remain in state machine — not note projection.

### D8 — Projection never writes storage; preserves note identity

**Decision:** Inherits `loop-wrap-projection`. DisplayNotes, projected intervals, playback window events are temporary. Mutation authority remains `applyEditSessionActions` (overlap) and normalize boundaries (DEC-014).

**Identity invariant (coordinate model §8):** Projection changes **coordinates only** — never creates duplicate logical notes. Every `ProjectedNoteInterval` for canonical Note #42 carries `noteId == 42` regardless of k shift. Required for edit, overlap, undo/redo, selection, and note ownership.

### D9 — normalizeWindow / normalizeAll unchanged

**Decision:** Canonicalization at transaction boundaries is separate from projection. Geometry tick path: project → analyze → apply → (edit closure) `normalizeWindow`. Macro: `normalizeAll` at commit.

### D10 — Blocks edit-session-action-geometry

**Decision:** Overlap pipeline Phases 1–4 do **not** start until UIP Phases 1–5 complete + HITL pass. Phase 6 syncs overlap OpenSpec deltas only.

### D11 — Future viewport editing

**Decision:** Editing a segment of a larger loop (e.g. bars 3–4 of 8) uses the same pipeline with `ProjectionContext.window` scoped to the visible segment (`{ start: bar3Tick, end: bar5Tick }`). No edit algorithm changes — only context.

### D12 — Global time in ProjectionContext

**Decision:** **`ProjectionContext.originTick`** is **global transport space** (`ClockManager.currentTick` or track effective playback tick) — not clip-local beat 0. **`loopStartTick`** is the persisted loop startpoint (`Loop.loopStartTick`) for deriving projected ranges. **`selectedTick`** is runtime selection/bracket (NOTE_EDIT; today `EditorSelection.bracketTick`).

**Reset/playhead-at-zero:** `originTick == currentTick == projectionCycleStartTick`.

### D13 — Rolling projection cycle start

**Decision:** On each loop wrap detection: **`projectionCycleStartTick += loopLength`** where `loopLength` is **`Loop.loopLengthTicks` at wrap time**. Phase: `(currentTick - projectionCycleStartTick) % loopLength`.

**Mid-cycle length change:** Match brownfield — `projectionCycleStartTick` stays fixed until wrap; only `% loopLength` uses new length (`setLoopLengthWithWrapping`, LOOP_EDIT Fader 2).

### D14 — Queued start (slot button, bar press, 16th step)

**Decision:** **`queuedStartTick`** is a one-shot queued restart/scrub applied at the next **configurable grid tick** (default 16th; global config, not hard-coded only in `SlotStateMachine`). Bar-select and slot-trigger queued starts are **mutually exclusive** — **last one set wins** at grid commit. Pending slot switch: **one per track**; new press **replaces** pending target and **resets** `queuedAtTick` (last press wins).

**Per-track runtime (4.7 resolved):** **`projectionCycleStartTick`** is **one per Track** — not per Loop, not per slot. Cross-track jamming: each track keeps its own rolling cycle; switching to another track leaves other tracks' cycles unchanged; the track being jammed can be modified.

**Slot commit (both):** At grid `T_commit`: **`projectionCycleStartTick = T_commit`** on that track **and** apply **`queuedStartTick`** once (default = committing loop's **`loopStartTick`**, unless bar/16th queued another start).

**Slot is thin (no tick logic):** Slots are triggers only — play from **`loopStartTick`**, load/switch loop, double-press to load. Slot does **not** own projection runtime or capture.

**LoopTriggerSequence (D14 — architecture):**

### LTS principle — three layers of responsibility

| Layer | Decides | Owner |
|-------|---------|-------|
| **LoopTriggerSequence** | **What** should play | **`LoopTriggerSequenceManager`** |
| **IntervalProjection** | **Where** notes exist in working time | **`IntervalProjection`** (shared) |
| **Track** | **How** playback runs | **`Track`** |

```
User performance
  → Trigger capture
  → stored TriggerEvents
  → trigger playback (Manager)
  → ProjectionContext (Manager → Track)
  → IntervalProjection
  → Track playback
  → MIDI output
```

**Capture, storage, playback are separate** (mirrors MIDI record architecture):

| Stage | Operates on | Rule |
|-------|-------------|------|
| **Capture** | Live gestures → **`TriggerEvent`** rows on **`triggerSequenceTick`** | Record/overdub only |
| **Storage** | Committed **`LoopTriggerSequence`** vector | Edit/build changes stored events only |
| **Playback** | Snapshotted **`TriggerEvent`** data → **`ProjectionContext`** | Never reads live slot **`Loop`** metadata |

Playback SHALL NOT depend on how events were originally captured.

### TriggerEvent — loop-level MIDI analog (explicit Start / End)

**TriggerEvents** are the loop-level equivalent of **`MidiEvent`** NoteOn/NoteOff — explicit events, **no duration field**.

```text
MIDI:           NoteOn … NoteOff
TriggerSequence: Start … End
```

**Storage:** ordered **`TriggerEvent`** vector on **`triggerSequenceTick`** timeline:

```cpp
enum class TriggerEventType : uint8_t { Start, End };

enum class PlaybackTargetType : uint8_t {
  Loop,       // v1
  LoopSlice,  // Phase 8
  // Clip, Scene, TimelineRegion — future (type reserved; not implemented)
};

struct PlaybackTarget {
  PlaybackTargetType type;
  LoopId loopId;  // v1 Loop target
};

struct TriggerPlaybackSnapshot {
  PlaybackTarget target;
  uint32_t loopLengthTicks;
  uint32_t loopStartTickSnap;
  int32_t playbackOffsetTick;  // queued/start offset at capture
  uint16_t repeatCount;        // 0 = until End; >0 = cap wraps (D17)
};

struct TriggerEvent {
  TriggerEventType type;           // Start | End
  int32_t tick;                    // position on triggerSequenceTick timeline
  TriggerPlaybackSnapshot snapshot; // required on Start; default on End
};

struct LoopTriggerSequence {
  std::vector<TriggerEvent> events;  // ordered; immutable snapshots at commit
};
```

**Engage invariant:** one **Start** + one **End** per continuous engagement. **No extra Start events on loop wrap** — loop boundaries are **`IntervalProjection`**, not trigger recording (D18).

**Overdub:** append **End** for open engage before next **Start**.

### LTS depends on IntervalProjection — not a playback engine

**`LoopTriggerSequenceManager`** SHALL NOT implement wrap or loop projection math. It produces **playback intent** → **`ProjectionContext`** → existing **`IntervalProjection`** → **`Track`** playback.

Integration point on **`Track`**: **`projectionCycleStartTick`** + snapshotted engage data from **Start** event → build **`ProjectionContext`** → project loop **`midiEvents`**.

```
TriggerEvent (Start)
  → projectionCycleStartTick + ProjectionContext
  → IntervalProjection
  → Track playback
```

### triggerSequenceTick ownership (D18)

**`LoopTriggerSequenceManager`** owns **`triggerSequenceTick`** — the sequence timeline (same role as **`currentTick`** for loop **`midiEvents`** capture).

| Mode | Capture | **`triggerSequenceTick`** |
|------|---------|---------------------------|
| **`triggerSequenceRecord`** / **overdub** | **Active** — append **Start**/**End** at **`triggerSequenceTick`** (derived from **`currentTick`** relative to capture origin) | Advances with transport |
| **Build / edit** | **Inactive** — mutate stored **`TriggerEvent.tick`** directly; no live capture append | Advances during **`triggerSequencePlay`** if sequence is playing; not used to append events |
| **`triggerSequencePlay`** | Off unless overdub | Advances with transport — sequence playhead position |

**Edit without stop (D20):** The system SHALL **never require** stopping transport, **`triggerSequencePlay`**, or Note Edit to modify a stored **`LoopTriggerSequence`**. Build, local undo, and **`TriggerEvent`** tick edits MAY run while playback continues.

**Apply rule (v1):** Edits to events at **`tick`** values **after** current **`triggerSequenceTick`** during play take effect when playback reaches them. Edits at or before **`triggerSequenceTick`** take effect at the next **Start**/**End** boundary.

### Manager vs Track ownership (D19)

| **`LoopTriggerSequenceManager`** | **`Track`** |
|-----------------------------------|-------------|
| Records / edits / sequences **`TriggerEvent`**s | Owns playback state, transport, active loop slot |
| Owns **`triggerSequenceTick`**, LTS Edit Mode, local undo | Owns **`projectionCycleStartTick`**, **`queuedStartTick`** |
| **Requests** playback state changes | **Performs** playback via **`IntervalProjection`** |
| Builds playback intent + **`ProjectionContext`** from snapshotted **Start** | Executes projected loop **`midiEvents`** |

Manager **coordinates**; Track **performs**. No duplicated transport or wrap logic on Manager.

**Core types summary** — see structs above. **`PlaybackTarget`** keeps **`TriggerEvent`** independent of future target kinds (slices, clips, scenes).

**Modes / UX state:**

| Mode | Entry | Role |
|------|-------|------|
| **`LoopTriggerSequence Edit Mode`** | **LoopTriggerSequence row** button or track REC/PLAY while entering LTS workflow; **overrides** Play / Loop Edit / Note Edit | Build, record, overdub, or cue sequences; display/faders **follow** **`projectedInterval`** so Note Edit can continue during **`triggerSequencePlay`** |
| **`triggerSequenceRecord`** | LTS row + slot arm/record lifecycle (below) | Capture **`TriggerEvent`** rows on **`triggerSequenceTick`**; arm at **next bar** |
| **`triggerSequenceOverdub`** | 4th short-press on slot (same as midiLoop model) | Append pass; close prior pair with **`endTick`** |
| **`triggerSequencePlay`** | 3rd short-press or commit idle | Manager drives **Start**/**End** playback intent → **`ProjectionContext`** → **`Track`** |

Retire separate **`triggerSequenceCreate`** / **`triggerSequenceEdit`** mode names — use **build phase** inside **`LoopTriggerSequence Edit Mode`** (local undo during idle). Sequence **overlay UI** TBD (may need dedicated edit substate later).

**LoopTriggerSequence row (8 buttons, ex-Jams):** primary entry for record/overdub/play/recall per preset slot on that row.

**Slot row gestures in LTS Edit Mode** (empty slot — same lifecycle as midiLoop):

| Press | Action |
|-------|--------|
| 1st short | Arm trigger record |
| 2nd short | Start **`triggerSequenceRecord`** (next bar) |
| 3rd short | **`triggerSequencePlay`** |
| 4th short | **`triggerSequenceOverdub`** |
| Long | Delete |
| Double / triple | Undo / redo (dedicated trigger-sequence undo stack) |

**During `triggerSequencePlay`:**

| Gesture | Action |
|---------|--------|
| Short press slot | **Exit** sequence play → normal slot behavior |
| Long press slot | **Cue** new sequence while current plays; switch after **idle timeout** (global config, default 2 bars) |

**Bar/16th region jam → slot metadata:** When bar/16th jam region is active, **short press empty slot** writes jam region as that slot's **loop metadata** (`jamTick` → **`loopStartTick`** snapshot). **Long press filled slot** overwrites metadata. **Cue slot** concept TBD later.

**Midi capture routing:**

| Context | Track REC/PLAY | LTS row (8) |
|---------|----------------|-------------|
| Play / Loop Edit / Note Edit | **`midiEvents`** record/overdub (existing) | Enter **LTS Edit Mode** |
| **LTS Edit Mode** | **Fallback** → Loop Edit for **`midiEvents`** capture (wrong-state escape) | **`triggerSequenceRecord`** / **overdub** only |

**Playback:** Manager interprets **Start**/**End** events, updates **`projectionCycleStartTick`**, builds **`ProjectionContext`** from snapshotted **Start** payload — **`Track`** plays via **`IntervalProjection`**. No wrap math in Manager.

**Ownership (no "Engine" suffix; no new Processor/Handler in Phase 7):**

| Owner | Role |
|-------|------|
| **`LoopTriggerSequenceManager`** | Capture, storage edit, sequence playback coordination, **`triggerSequenceTick`**, LTS Edit Mode, local undo |
| **`Track`** | Playback execution, transport, **`projectionCycleStartTick`**, **`IntervalProjection`** requests |
| **`IntervalProjection`** | Wrap/projection math (shared — LTS does not duplicate) |
| **`MidiButtonActions`** | Mode-aware dispatch to Manager |
| **`MidiButtonProcessor`** | Unchanged — gesture detection |
| **Initializer adapters** | Slot, 16th, bar jam — pluggable |

Optional later: thin row **Handler** only if LTS MIDI map cannot use **`MidiButtonProcessor`** (input routing only).

**Persist / recall:** Save/load **`LoopTriggerSequence`** presets; **LoopTriggerSequence row** recall. Rename **Jams** → **LoopTriggerSequence** in docs/MIDI map.

**Phase split:** Phases 1–5 (IntervalProjection) → Phase 7 (**`LoopTriggerSequence`** + SD persist) → Phase 8 (slice metadata on **Loop**) → Phase 6 overlap unblock unchanged before 7.

### D17 — Continuous engage and repeatCount (resolved)

**Decision:** Between **Start** and **End** on one engagement:

1. **Default:** Loop repeats via **`IntervalProjection`** at snapshotted **`loopLengthTicks`** until **End** tick — **no additional Start events on wrap**.
2. **Optional:** **`repeatCount`** on **Start** snapshot — **0** = until **End**; **>0** = cap full wraps within engage.

**Step transition:** **End** (stop target A) → next **Start** (engage target B). Not duration-based spans.

### D18 — LTS capture / storage / playback separation

**Decision:** Three independent stages (mirrors MIDI). Editing operates on stored **`TriggerEvent`** vector only. Playback uses snapshotted **Start** payloads only — never live **`Loop`** metadata.

### D20 — Edit sequence without stopping playback

**Decision:** **`LoopTriggerSequence`** storage edits (build phase, local undo, **`TriggerEvent.tick`** changes, append/remove **Start**/**End**) SHALL NOT require stopping transport or **`triggerSequencePlay`**. Mirrors Note Edit continuing during sequence play via **`projectedInterval`**.

**Not required:** exit sequence play before edit; stop transport before build; pause before overdub append (overdub is explicit capture mode, not a stop-for-edit gate).

### D19 — LTS is a ProjectionContext producer, not a playback engine

**Decision:** **`LoopTriggerSequenceManager`** coordinates **what** plays and sets **`projectionCycleStartTick`** / **`ProjectionContext`**. **`Track`** + **`IntervalProjection`** decide **where** and **how**. Manager SHALL NOT implement `% loopLength` or equivalent-interval generation.

**Not in scope Phase 7:** Cue slot; sequence overlay UI; **`PlaybackTargetType`** beyond **Loop** (Phase 8 **LoopSlice** only).

**Not in scope Phase 7:** Cue slot; sequence overlay UI; **`PlaybackTargetType`** beyond **Loop** (Phase 8 **LoopSlice** only).

### D21 — Retire `PlaybackCursor` (Phase 4 playback refactor)

**Decision:** **Remove** [`include/PlaybackCursor.h`](../../include/PlaybackCursor.h) and **`LoopPlaybackRuntime::cursor`**. Do **not** extend **`PlaybackCursor`** for UIP, LTS, or display playhead. Playback position vocabulary stays on existing ticks (D15 table) + **`Loop.nextEventIndex`**.

**Brownfield audit (why remove, not refactor in place):**

| `PlaybackCursor` field | Status | Action |
|----------------------|--------|--------|
| `mergeCursorIndex` | Written only; mirrors **`Loop.nextEventIndex`**; never read | **Delete** — **`nextEventIndex`** remains on **`Loop`** |
| `lastTickInLoop` | Duplicates **`Loop.lastTickInLoop`** | **Delete** — keep **`Loop.lastTickInLoop`** only |
| `windowStartBar`, `effectiveWindowBars` | Never read; window lives on **`PlaybackWindow`** | **Delete** |
| `loopHeadReady`, `loopHeadRevision` | Never used outside reset | **Delete** |
| `cachedLoopRevision`, `cachedTrackGeneration` | Used by **`isStale`** / **`syncRevision`** | **Move** to **`LoopPlaybackRuntime`** as two fields (or fold into **`primaryWindow.builtFromRevision`** policy) |

| Other | Status | Action |
|-------|--------|--------|
| **`loopHeadWindow`** | Cleared in reset; never read | **Delete** with cursor |
| **`primaryWindow`** | Active — **`mergedEvents`**, **`builtFromRevision`** | **Keep** on **`LoopPlaybackRuntime`** |
| **`ActiveNoteLedger`** | Active | **Keep** |

**Phase 4 refactor steps (`Track.cpp`):**

1. Add **`Track.projectionCycleStartTick`** (per D13); migrate wrap detect and phase off **`startLoopTick`**-only paths where projection owns wrap.
2. Replace **`tickPhaseInLoop(currentTick, loop.startLoopTick, …)`** in playback send with **`IntervalProjection`** phase helper using **`ProjectionContext`** (Playback type).
3. Replace **`rebuildPlaybackOrder`** / **`playbackSortPhase`** modulo sort with **`playbackEventPhase`** + precomputed sort phases (D24) — not allocating **`projectNoteIntervals`** in comparators.
4. Inline stale-cache check currently on **`runtime.cursor.isStale`** → **`LoopPlaybackRuntime`** (no **`PlaybackCursor`** type).
5. Delete **`#include "PlaybackCursor.h"`**; update **`TrackPlaybackRuntime.h`**; fix **`test_playback_prewarm`** if it includes cursor headers only.
6. Grep gate: no **`PlaybackCursor`** identifier remains.

**Explicit non-goals:** Do not rename **`PlaybackCursor`** to **`PlaybackTarget`** / sequence types. LTS **`triggerSequenceTick`** stays on **`LoopTriggerSequenceManager`**.

### D22 — Related window/cache cleanup (Phase 3–4)

Same class of brownfield debt as **`PlaybackCursor`**: duplicate window fields, unused buffers, or local `% loopLength` intersection logic. Address during UIP migration — **not** a second playback engine.

| Artifact | Role today | Phase | Action |
|----------|------------|-------|--------|
| **`LoopPlaybackRuntime::loopHeadWindow`** | Never read | **4** | **Delete** (with D21) |
| **`PlaybackWindow.windowStartBar`** | Never read/written in `Track.cpp` | **4** | **Delete** field |
| **`PlaybackWindow.effectiveWindowBars`** | Set to `PLAYBACK_WINDOW_MAX_BARS`; never read for send logic | **4** | **Delete** or defer until real preload window ships |
| **`PlaybackWindow.mergedEvents`** + **`builtFromRevision`** | Active merge cache | **4** | **Keep** — may rename struct (e.g. **`LoopMergeCache`**) but not merged with display viewport |
| **`DisplayWindowUtils`** | Piano-roll viewport filter; own `normalizeTick` / `% loopLength` | **3** | **Refactor** — filter **after** **`projectDisplayNotes()`**; intersection via **`TickInterval`** + projection helpers (task 3.3); not deleted |
| **`DetailedWindowContext`** (`windowStartTick`, `windowLengthTicks`) | OLED detailed viewport state | **3** | **Migrate** to **`TickInterval`**; UI-only — not playback merge cache |
| **`playbackSortPhase`** (`Track.cpp`) | Local modulo sort key | **4** | **Replace** with **`IntervalProjection`** playback ordering (task 4.11) |
| **`tickPhaseInLoop`** (`TickPhase.h`) | Shared phase helper | **1** | **Centralize** into **`IntervalProjection`**; callers thin-wrap (task 1.4) — not removed |
| **Jam region** (`jamStartTick`, `jamLength`) | Display/playback sub-region | **4** / **8** | **Map** to **`queuedStartTick`** / slice metadata — not a window struct; separate from **`PlaybackWindow`** |
| **`long-loop-piano-roll-window`** OpenSpec | Future **`ProjectionType::Timeline`** | **post-UIP** | Uses same **`TickInterval`** + **`ProjectionContext.window`** — out of Phase 4 scope |

**Do not merge:** **`PlaybackWindow.mergedEvents`** (MIDI send hot path) and **`DetailedWindowContext`** (OLED viewport) serve different consumers — same **`TickInterval`** shape, different owners.

### D23 — Window is `TickInterval` frame; never `ProjectedNoteInterval`

**Decision:** **`ProjectionContext.window`** is a **`TickInterval`** coordinate frame (input). **`ProjectedNoteInterval`** is a projected **note span** (output). Unify **geometry** via **`TickInterval`**; do **not** type windows as **`ProjectedNoteInterval`**.

| Concept | Type | Pipeline role |
|---------|------|---------------|
| Viewport / preload / analysis region | **`TickInterval`** on **`ProjectionContext.window`** | **Input** — defines k bounds and intersection predicate |
| Canonical storage span | **`TickInterval`** on **`CanonicalNoteSpan.interval`** | **Input** — source note on→off |
| Shifted note in working coordinates | **`ProjectedNoteInterval`** | **Output** — carries **`noteId`** + pitch |
| Loop period | **`uint32_t loopLength`** | **Parameter** — shift stride; not an interval (see D3b) |

**Rules:**

1. **`window`** SHALL NOT be modeled as **`ProjectedNoteInterval`** — no fake **`noteId`** or pitch on viewports.
2. **`window`** SHALL NOT pass through **`generateEquivalentIntervals`** — only **`CanonicalNoteSpan`** candidates are k-shifted; **`window`** filters them.
3. Intersection SHALL use **`TickInterval::intersects`** (or equivalent) between **`window`** and **`ProjectedNoteInterval.interval`** — not a second projection pass on the window.
4. Display long-loop viewport clip (task 3.3) and sub-window edit (D11) both use **`TickInterval`** — the former MAY run after head/tail **rendering**; neither promotes the viewport to **`ProjectedNoteInterval`**.

**Alternative rejected:** Single type for frame and note output — conflates coordinate space with domain objects and breaks the identity invariant (D8): every **`ProjectedNoteInterval`** must refer to exactly one stored note.

### D24 — Playback hot-path projection (Phase 4 guardrail)

**Decision:** Playback sort keys and per-tick send phase use **`IntervalProjection::playbackEventPhase`** — a scalar, non-allocating k-scan over the full-loop playback window `{0, loopLength}`. Batch/analysis paths continue to use **`generateEquivalentIntervals`** → **`selectProjectedInterval`**.

| API | Use | Heap |
|-----|-----|------|
| **`playbackEventPhase(storageTick, loopLength)`** | **`rebuildPlaybackOrder`**, **`playMidiEvents`** send gate | **No** |
| **`projectPlaybackEventPhase(storageTick, context)`** | Tests, contextful call sites | Thin wrapper → **`playbackEventPhase`** |
| **`projectNoteIntervals`** / **`generateEquivalentIntervals`** | Edit, display, overlap analysis | Allowed |

**Rules:**

1. **`rebuildPlaybackOrder`** SHALL precompute each event's phase, then sort — never call allocating projection inside a **`std::sort`** comparator (Teensy heap exhaustion on sustained playback).
2. **`playMidiEvents`** SHALL use **`playbackEventPhase`** for event-tick phase vs **`lastTickInLoop`** — not **`projectNoteIntervals`** or **`generateEquivalentIntervals`** per event.
3. **`generateEquivalentIntervals`** SHALL NOT run on the MIDI send hot path.

**`projectionCycleStartTick` alignment (extends D13):** Beyond wrap **`+= loopLength`**, sync when playhead and **`lastTickInLoop`** are established together:

| Event | Rule |
|-------|------|
| Fresh **`startPlaying`** (`preserveLoopPhaseOrigin=false`) | **`projectionCycleStartTick = currentTick`** |
| **`startPlaying`** with **`preserveLoopPhaseOrigin=true`** | **`projectionCycleStartTick = currentTick - phase`** where `phase = tickPhaseInLoop(currentTick, loop.startLoopTick, loopLength)` |
| Record stop after truncation rewind | **`projectionCycleStartTick = playbackTick - lastTickInLoop`** |
| Queued start grid commit (D14) | unchanged — **`projectionCycleStartTick = commitTick - startPhase`** |

**Regression (2026-07-05):** HITL base preset (2 overdubs) rebooted on 2nd run when the sort comparator called **`projectPlaybackEventPhase`** (which allocated **`std::vector`**) and record-stop omitted **`projectionCycleStartTick`** sync after truncation rewind.

### D15 — loopStartTick vs selectedTick (naming)

**Decision:** Keep **`loopStartTick`** as persisted loop startpoint. **`selectedTick`** is runtime selection inside the projected interval (Fader 1/2/3, bracket, 16th buttons). **`SelectNavigation::noteRelativeTick` / `noteStorageTick`** transform between storage and projected coordinates using `loopStartTick`.

### D16 — projectDisplayNotes owned by IntervalProjection

**Decision:** Display head/tail split and live capture open-tail extension live in **`IntervalProjection::projectDisplayNotes()`**. Consumers delegate; engine owns display projection policy.

### D12 legacy note — brownfield jamTick

Brownfield **`Track.jamTick`** / **`jamPlaybackActive`** map to target **`queuedStartTick`** semantics during Phase 4 migration. Does not alter canonical storage.

**Contrast (prior research):** Ableton Clip View local time is **not** the MidiLooper authority model.

---

## Equivalent interval generation (normative)

Given canonical span interval `[S, E)` (linear storage ticks) and loop period `L`:

1. Derive **k bounds from `window`** — not fixed constants such as `for (k = -2; k <= 2)`:
   - Find minimum and maximum integer `k` such that shifted interval `[S + k·L, E + k·L)` intersects `[window.start, window.end)` using `TickInterval::intersects`.
2. Emit each candidate as `ProjectedNoteInterval` preserving **`noteId`** and `pitch`.
3. Order candidates deterministically (ascending k).

Generator MUST NOT branch on `ProjectionType` except to read `window` and `loopLength` for k bounds.

**Intersection test (normative):**

```
candidate.start < window.end && candidate.end > window.start
```

---

## Mapping from brownfield

| Today | After UIP |
|-------|-----------|
| `NoteUtils::reconstructNotes` wrap split | `projectDisplayNotes()`; thin wrapper preserves API |
| `DisplayWindowUtils::noteIntersectsWindow` | Display projection + intersection |
| `ensurePlaybackWindowBuilt` `% loopLength` | `projectNoteIntervals` + Playback **`ProjectionContext`** |
| `PlaybackCursor` / duplicate `lastTickInLoop` | **`Loop.lastTickInLoop`** + **`Loop.nextEventIndex`** only |
| `rebuildPlaybackOrder` / `playbackSortPhase` | Projection-backed playback ordering |
| `normalizeWrapToLinear` (planned) | `projectEditIntervalsForAnalysis` |
| `resolveLinearNoteSpanForOverlap` | Edit projection or thin wrapper |
| `isInflatedDisplaySpan` | Display projection reject path |
| `NoteMovementUtils` display-wrap skip | Removed when overlap wires (after UIP) |

---

## Retire list (post migration)

- Duplicate linear-span helpers in `NoteEditFocus` (absorbed or thin-wrap)
- **`PlaybackCursor.h`** and **`loopHeadWindow`** (D21 — unused / duplicate)
- Display-wrap skip branches in `NoteMovementUtils`
- Consumer-local modulo wrap for overlap/display/playback
- `normalizeWrapToLinear` identifier in overlap OpenSpec tasks

---

## Test strategy

| Suite | Focus |
|-------|-------|
| `test_interval_projection` | Equivalent generation (900→1080 @ L=960); bounded k; selection per type |
| `test_noteutils_reconstruct` | Display projection parity with current reconstruct |
| `test_display_window_utils` | Viewport filter on projected intervals |
| `test_note_edit_focus` | Edit projection parity with linear-span fixtures |
| HITL | 152335 move-across-boundary; long-loop display; NOTE_EDIT playback audition |

---

## Risks

| Risk | Mitigation |
|------|------------|
| Behavioral regression in display/playback | Native parity tests before consumer swap; HITL Phase 5 |
| Performance (generate per note per tick) | Bounded k; closure-scoped batch in edit path |
| Overlap blocked too long | Phase 6 explicit sync; overlap OpenSpec delta pre-written |
| Display multi-segment vs single interval | Core single interval + `projectDisplayNotes()` adapter; Edit always single contiguous |

---

## Prior research (appendix — math reuse only)

Third-party sequencers validate the **generate → select** split; none ship one shared engine. This change **does not** adopt clip-local time as authority. Reuse **algorithms only**:

| Source | Reuse in v1 |
|--------|-------------|
| **Magda-core** `fmod` phase | `generateEquivalentIntervals`; Edit select closest to `originTick` |
| **JUCE / cp3.io** | Playback buffer wrap + mid-block loop cross |
| **Tracktion** `LoopingMidiNode` ranges | Context builder shape (`window` + `loopLength` + offset) |
| **Ardour / Helio** | Overlap OpenSpec only — not projection |

**External fixtures (Phase 1):** Magda `900→1080` @ `L=960`; cp3.io buffer crosses loop end; Ardour-style boundary (interval intersection, not region membership).

**In-repo parity (not external research):** `test_note_edit_focus`, `test_noteutils_reconstruct` after consumer wire.

Detail + links: [`docs/plans/unified_interval_projection_sequencer_prior_research_refinement.md`](../../docs/plans/unified_interval_projection_sequencer_prior_research_refinement.md).

---

## References

- [`linear-loop-tick-storage`](../linear-loop-tick-storage/design.md)
- [`edit-session-action-geometry`](../edit-session-action-geometry/design.md) D20
- [`docs/Guides/NOTE_WRAPPING_LOGIC.md`](../../docs/Guides/NOTE_WRAPPING_LOGIC.md)
- [`docs/plans/note_edit_geometry_wrap_regression_bugfix.md`](../../docs/plans/note_edit_geometry_wrap_regression_bugfix.md)
- [`long-loop-piano-roll-window`](../long-loop-piano-roll-window/proposal.md)
- [`docs/plans/unified_interval_projection_sequencer_prior_research_refinement.md`](../../docs/plans/unified_interval_projection_sequencer_prior_research_refinement.md)
- [`docs/plans/dual-tick_view_override_architecture_856310b1.plan.md`](../../docs/plans/dual-tick_view_override_architecture_856310b1.plan.md) — D12 global / jam phase hook
