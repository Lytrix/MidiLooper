## ADDED Requirements

### Requirement: Canonical storage unchanged

Interval projection SHALL read canonical linear note spans from storage (`MidiEvent` pairs / `NoteBaseline`) and SHALL NOT introduce alternate persistent storage shapes. Canonical invariants from `linear-loop-tick-storage` (linear `NoteOff.tick >= NoteOn.tick`, may exceed `loopLength`) SHALL remain authoritative.

#### Scenario: Projection does not mutate storage

- **WHEN** `projectNoteIntervals` runs on canonical storage
- **THEN** no `MidiEvent` tick in canonical storage is modified
- **AND** output is temporary projected intervals only

### Requirement: TickInterval primitive

The projection layer SHALL use a shared **`TickInterval`** primitive for all span and window coordinates:

```cpp
struct TickInterval {
  int32_t start;
  int32_t end;
  int32_t length() const;  // end - start; derived, not stored
};
```

Windows, canonical spans, projected intervals, editor windows, display viewports, and future timeline viewports SHALL prefer **`TickInterval`** over independent start/length pairs at API boundaries.

**`loopLength`** SHALL remain a **duration** (loop period for `±k·loopLength` shifts) — not a **`TickInterval`**.

#### Scenario: Overlap uses interval intersection

- **GIVEN** candidate interval `900 → 1080` and window `850 → 950`
- **WHEN** intersection is tested
- **THEN** the test is `candidate.start < window.end && candidate.end > window.start`
- **AND** no `windowStart + windowLength` arithmetic is required at the call site

### Requirement: Window is TickInterval frame not ProjectedNoteInterval

**`ProjectionContext.window`** SHALL be a **`TickInterval`** coordinate frame (pipeline **input**). **`ProjectedNoteInterval`** SHALL represent a projected **note span** (pipeline **output**) with **`noteId`** and pitch.

Implementations SHALL NOT type viewports, preload regions, or analysis windows as **`ProjectedNoteInterval`**. **`window`** SHALL NOT pass through **`generateEquivalentIntervals`** — only **`CanonicalNoteSpan`** inputs are k-shifted; **`window`** bounds k and filters candidates via **`TickInterval`** intersection.

#### Scenario: Viewport is not a projected note

- **GIVEN** a display viewport `{ start: 768, end: 1536 }` on a 32-bar loop
- **WHEN** **`ProjectionContext`** is built for Display
- **THEN** **`window`** is a **`TickInterval`**
- **AND** no **`ProjectedNoteInterval`** is constructed for the viewport
- **AND** note candidates are projected into working coordinates and filtered against **`window`**

#### Scenario: Window does not receive k shifts

- **GIVEN** a **`ProjectionContext`** with **`window`** `{ start: 0, end: 960 }` and loop period `960`
- **WHEN** Stage 1 equivalent-interval generation runs
- **THEN** k shifts apply to **`CanonicalNoteSpan`** intervals only
- **AND** **`window`** remains the fixed intersection predicate

### Requirement: ProjectionContext defines the coordinate space

Every projection consumer SHALL supply a **`ProjectionContext`** representing the coordinate system in which projection occurs.

**Common core (all consumers):**

- `loopLength` — loop period (duration)
- `window` — **`TickInterval`** inclusive working coordinate window
- `ProjectionType` — `Playback`, `Display`, `Edit`, or `Timeline` (reserved)

**Consumer extensions** on the same struct:

| Consumer | Additional fields |
|----------|-------------------|
| **Playback** | `originTick`, `projectionCycleStartTick`, `useQueuedStart`, `queuedStartTick`, `loopStartTick` |
| **Edit** | `originTick`, `selectedTick`, `loopStartTick` |
| **Display** | `loopStartTick` (v1) |

Different subsystems SHALL differ only by context construction — not by independent wrap implementations.

#### Scenario: Playback supplies playback context

- **WHEN** playback builds its preload window
- **THEN** it constructs `ProjectionContext` with `ProjectionType::Playback` and `window` as a **`TickInterval`**
- **AND** does not embed private modulo wrap logic outside `IntervalProjection`

#### Scenario: Edit supplies edit context

- **WHEN** NOTE_EDIT overlap analysis prepares note spans
- **THEN** it constructs `ProjectionContext` with `ProjectionType::Edit` anchored at **`EditorSelection.primaryNote`** linear start
- **AND** v1 analysis `window` MAY span the full loop as `{ start: 0, end: loopLength }`
- **AND** `selectedTick` matches the active select/bracket tick used by Fader 1/2/3 and 16th-step logic

#### Scenario: Reset playhead aligns cycle origin

- **WHEN** transport resets playhead to zero and playback starts
- **THEN** `originTick == currentTick == projectionCycleStartTick`

#### Scenario: Wrap advances cycle start

- **GIVEN** loop wrap at global tick `T` with `loopLength = L` at wrap time
- **WHEN** wrap is detected
- **THEN** `projectionCycleStartTick` increases by `L`

#### Scenario: Length change mid-cycle

- **GIVEN** `loopLength` changes during playback before the next wrap
- **WHEN** projection computes phase
- **THEN** phase uses the new `loopLength` in modulo
- **AND** `projectionCycleStartTick` remains unchanged until the next wrap

#### Scenario: Record stop aligns projection cycle to rewind playhead

- **GIVEN** record stop truncates the loop and rewinds global playhead to `playbackTick`
- **WHEN** `lastTickInLoop` is set from `playbackTick` and `loop.startLoopTick`
- **THEN** `projectionCycleStartTick = playbackTick - lastTickInLoop`
- **AND** playback phase on the first pass after stop matches `lastTickInLoop`

#### Scenario: Preserve-phase startPlaying aligns projection cycle

- **GIVEN** playback resumes with `preserveLoopPhaseOrigin=true` at global tick `T`
- **WHEN** `startPlaying` runs
- **THEN** `projectionCycleStartTick = T - tickPhaseInLoop(T, loop.startLoopTick, loopLength)`
- **AND** wrap detection uses the same rolling cycle as before the pause

### Requirement: Two-stage projection pipeline

The interval projection layer SHALL treat **Stage 1 (mathematics)** and **Stage 2 (consumer policy)** as independent concepts:

1. **`generateEquivalentIntervals`** — deterministic bounded set of mathematically equivalent intervals for one canonical span (shift by ±k·`loopLength`). **Pure math** — no playback, edit, display, overlap, or rendering policy.
2. **`selectProjectedInterval`** / **`selectProjectedIntervalsForDisplay`** — consumer-specific selection from candidates.

**`generateEquivalentIntervals`** SHALL NOT branch on `ProjectionType` beyond reading `window` and `loopLength` for k bounds.

#### Scenario: Equivalent intervals for wrapped storage span

- **GIVEN** canonical storage `900 → 1080` and `loopLength = 960`
- **WHEN** `generateEquivalentIntervals` runs
- **THEN** candidates include `-60 → 120`, `900 → 1080`, and `1860 → 2040` within the k range derived from `window`
- **AND** generation order is deterministic

#### Scenario: Generate stage has no consumer policy

- **WHEN** `generateEquivalentIntervals` runs for any `ProjectionType`
- **THEN** it emits the same candidate set for the same span, loop length, and `window`
- **AND** selection policy runs only in Stage 2

### Requirement: k bounds derived from window not fixed constants

Candidate generation SHALL derive minimum and maximum `k` from the active **`ProjectionContext.window`** such that each shifted interval intersects the window.

Implementations SHALL NOT use fixed small k ranges (e.g. `k ∈ [-2, 2]`) that fail when projection windows exceed a few loop lengths.

#### Scenario: Large window spans multiple loop copies

- **GIVEN** a projection `window` spanning more than two loop lengths
- **WHEN** `generateEquivalentIntervals` runs
- **THEN** k bounds expand to include every candidate intersecting the window
- **AND** no candidate is omitted because of a hard-coded k cap

### Requirement: Selection policy per ProjectionType

**`selectProjectedInterval`** SHALL apply consumer-specific selection rules based on `ProjectionContext.type`:

| `ProjectionType` | Selection rule |
|------------------|----------------|
| **Playback** | Select one interval intersecting `window`; prefer interval containing `originTick` |
| **Display** | Select **every** candidate interval intersecting `window` (typically one per canonical span) |
| **Edit** | Select one contiguous interval closest to `originTick` for overlap analysis |
| **Timeline** | Reserved — interval nearest editor viewport; no generator or storage changes |

#### Scenario: Edit projection single interval

- **GIVEN** a wrapped canonical note and Edit projection context anchored at the primary edited note
- **WHEN** `selectProjectedInterval` runs with `ProjectionType::Edit`
- **THEN** exactly one contiguous projected interval is chosen for overlap analyze input
- **AND** analyze does not receive raw display wrap segments

#### Scenario: Display projection selects intersecting intervals

- **GIVEN** equivalent candidates for one canonical span and a display `window`
- **WHEN** Display Stage 2 selection runs
- **THEN** every candidate intersecting `window` is selected
- **AND** head/tail rendering has not run yet

#### Scenario: Display adapter may split head and tail

- **GIVEN** a projected interval `-60 → 120` crossing the loop boundary
- **WHEN** **`projectDisplayNotes()`** rendering runs
- **THEN** the adapter MAY emit drawable segments `-60 → 0` and `0 → 120`
- **AND** the core generator and Stage 2 selector did not perform this split
- **AND** canonical storage remains unchanged

#### Scenario: Head tail is not long-loop window

- **GIVEN** a loop longer than 16 bars with a bounded piano-roll viewport
- **WHEN** display filtering runs
- **THEN** head/tail boundary split and viewport window filtering are separate stages
- **AND** viewport filtering runs after note reconstruction

### Requirement: Display projection and rendering are separate stages

**`projectDisplayNotes()`** SHALL perform two distinct responsibilities:

1. **Projection** — run generate → **`selectProjectedIntervalsForDisplay`** to choose interval(s) inside `window`
2. **Rendering** — split wrapped intervals into head/tail **`DisplayNote`** rows and apply live capture open-tail extension

Playback and Edit consumers SHALL NOT use the rendering split.

#### Scenario: Live capture open tail extends to playhead

- **GIVEN** canonical capture has an open note-on with no note-off
- **WHEN** Display projection runs during record/overdub preview
- **THEN** the projected interval end MAY extend to the playhead tick in working coordinates
- **AND** canonical storage is unchanged

#### Scenario: reconstructNotes delegates to projectDisplayNotes

- **WHEN** `NoteUtils::reconstructNotes` is refactored
- **THEN** it delegates to **`projectDisplayNotes()`**
- **AND** outward `DisplayNote` API is preserved

### Requirement: Display playhead aligns with playback projection cycle on active slot

When the **displayed loop slot** is the **active playing slot** and transport is **PLAYING** or **OVERDUBBING**, the OLED playhead position and 16th/bar LED tick indicators SHALL derive storage phase from **`projectionCycleStartTick`** via **`tickPhaseInProjectionCycle`**, then map to display coordinates with **`noteRelativeTick(..., loopStartTick, loopLength)`**. This SHALL match the phase gate used in **`playMidiEvents`**.

When the displayed slot is not the active loop, or transport is stopped, storage phase MAY use **`tickPhaseInLoop(currentTick, loop.startLoopTick, loopLength)`** for focus/audition.

#### Scenario: Slot switch commit — playhead matches audible MIDI

- **GIVEN** slot A is playing and the user selects slot B (`SyncPlayback::No`) then playback commits at grid `T_commit`
- **WHEN** `commitQueuedPlaybackStart` re-anchors **`projectionCycleStartTick`** for slot B
- **THEN** the OLED position string and piano-roll playhead for slot B track **`tickPhaseInProjectionCycle`**, not record-time **`startLoopTick`** alone
- **AND** the displayed position matches which stored events are audibly firing

#### Scenario: Stopped slot audition uses record phase anchor

- **GIVEN** transport is stopped and the user selects a filled slot for focus
- **WHEN** the playhead is drawn
- **THEN** storage phase uses **`loop.startLoopTick`**
- **AND** display phase still applies **`loopStartTick`** offset via **`noteRelativeTick`**

### Requirement: NOTE_EDIT selection bracket is display-phase (loopStart-aware)

During NOTE_EDIT, **`EditorSelection.selectedTick`**, draw highlight, and dependent fader feedback SHALL use **display-phase** ticks relative to **`loopStartTick`**. Geometry commit paths SHALL NOT write storage ticks normalized at origin 0 into **`selectedTick`**.

#### Scenario: Move with non-zero loopStart keeps fader 2–4 bound

- **GIVEN** NOTE_EDIT on a loop with **`loopStartTick > 0`**
- **WHEN** the user moves the selected note with coarse/fine faders
- **THEN** **`selectedTick`** remains in display phase
- **AND** faders 2–4 continue to edit pitch/length/start without “No note selected”

### Requirement: Batch projection API

The engine SHALL expose **`projectNoteIntervals`** to project many canonical spans through generate → select in one call. Consumers SHOULD use the batch API rather than reimplementing per-note loops.

#### Scenario: Edit batch projects many spans

- **WHEN** overlap analysis prepares multiple canonical spans for one edit tick
- **THEN** **`projectEditIntervalsForAnalysis`** uses the batch generate → select pipeline
- **AND** consumers do not reimplement per-note wrap loops outside `IntervalProjection`

### Requirement: Edit projection replaces normalizeWrapToLinear

NOTE_EDIT overlap pipeline step 3 SHALL use **`projectEditIntervalsForAnalysis`** (Edit projection) instead of a separate **`normalizeWrapToLinear`** function. Overlap **`analyzeEditSessionInteractions`** SHALL receive only post-projection intervals.

#### Scenario: Wrapped target before analyze uses Edit projection

- **GIVEN** target **A** is a wrapped display note in canonical storage
- **WHEN** analyze runs
- **THEN** Edit projection has produced **`baselineSpan`** and causing spans in working coordinates before **`InteractionType`** is classified
- **AND** no consumer-local wrap linearization runs outside `IntervalProjection`

### Requirement: Queued start at configurable grid

Slot-button restart, bar press, and 16th-step scrub SHALL queue a one-shot **`queuedStartTick`** applied at the next configurable grid tick (default 16th). Bar-select and slot-trigger queued starts SHALL be mutually exclusive — **last one set wins** at grid commit.

#### Scenario: Slot NextGrid commit applies queued start

- **GIVEN** slot B is pressed while slot A plays on the same track
- **WHEN** commit occurs at quantized grid tick `T_commit`
- **THEN** that track's **`projectionCycleStartTick`** becomes `T_commit`
- **AND** **`queuedStartTick`** is applied once for the committing loop (default from slot B's **`loopStartTick`**)
- **AND** slot B is a trigger only — it does not own tick runtime

#### Scenario: Cross-track cycles stay independent

- **GIVEN** track 1 and track 2 are both playing with different projection cycles
- **WHEN** the user jams on track 2 (slot press or queued start)
- **THEN** track 2's **`projectionCycleStartTick`** / queued start updates
- **AND** track 1's projection cycle remains unchanged

#### Scenario: Short press queues single-slot switch

- **GIVEN** the track is playing slot A
- **WHEN** the user short-presses filled slot B
- **THEN** slot B is queued to start at the next 16th grid tick
- **AND** **`projectionCycleStartTick`** and **`queuedStartTick`** update at that commit per D14

#### Scenario: Pending slot replacement

- **GIVEN** slot B is pending and slot C is pressed before commit
- **WHEN** the pending switch is updated
- **THEN** pending target becomes C and B is discarded
- **AND** commit waits for the next grid tick after C's press

### Requirement: TriggerEvent as loop-level Start and End events (Phase 7)

The firmware SHALL store **`LoopTriggerSequence`** as an ordered vector of **`TriggerEvent`** rows — the loop-level equivalent of **`MidiEvent`** NoteOn/NoteOff. **No duration field** — continuous engagement is explicit **Start** + **End** events on the **`triggerSequenceTick`** timeline.

```cpp
enum class TriggerEventType { Start, End };

struct TriggerEvent {
  TriggerEventType type;
  int32_t tick;                    // on triggerSequenceTick timeline
  TriggerPlaybackSnapshot snapshot; // full payload on Start; default on End
};
```

**`TriggerPlaybackSnapshot`** SHALL capture immutable playback data at **Start**: **`PlaybackTarget`**, **`loopLengthTicks`**, **`loopStartTickSnap`**, **`playbackOffsetTick`**, optional **`repeatCount`**.

**Replay** SHALL use stored snapshots only — not live slot **`Loop`** values.

#### Scenario: Start and End mirror NoteOn and NoteOff

- **GIVEN** a recorded engagement
- **WHEN** the sequence is stored
- **THEN** it contains one **Start** event and one **End** event on **`triggerSequenceTick`**
- **AND** no duration or span-length field is stored

#### Scenario: Slot loop length change does not alter committed Start snapshot

- **GIVEN** a committed **Start** with snapshotted **`loopLengthTicks = 960`**
- **WHEN** the user later changes that slot's loop length to 1920
- **THEN** **`triggerSequencePlay`** still uses **`loopLengthTicks = 960`** from the snapshot

#### Scenario: Overdub appends End before next Start

- **GIVEN** **`triggerSequenceOverdub`** with an open engage ( **Start** without matching **End** )
- **WHEN** the user adds the next trigger
- **THEN** an **End** event is appended on **`triggerSequenceTick`**
- **AND** the next **Start** follows

### Requirement: Trigger capture storage and playback separation (Phase 7)

LoopTriggerSequence SHALL divide into three independent stages:

1. **Capture** — live gestures append **`TriggerEvent`** rows during record/overdub
2. **Storage** — committed vector; build/edit mutates stored events only
3. **Playback** — reads snapshotted **Start** payloads only; SHALL NOT depend on original capture path

#### Scenario: Playback ignores live loop metadata

- **GIVEN** a committed sequence and changed slot **`Loop.loopStartTick`**
- **WHEN** **`triggerSequencePlay`** engages a **Start** event
- **THEN** playback uses the **Start** snapshot
- **AND** does not read current slot geometry

### Requirement: LoopTriggerSequence uses IntervalProjection (Phase 7)

**`LoopTriggerSequenceManager`** SHALL NOT implement loop wrap or projection mathematics. It SHALL produce playback intent and **`ProjectionContext`**; **`Track`** SHALL invoke **`IntervalProjection`** for loop **`midiEvents`** playback.

Integration: **Start** event → update **`projectionCycleStartTick`** → build **`ProjectionContext`** → **`IntervalProjection`** → **`Track`** playback.

#### Scenario: Manager does not compute wrap offsets

- **GIVEN** **`triggerSequencePlay`** with a **Start** event spanning multiple loop wraps before **End**
- **WHEN** loop **`midiEvents`** play
- **THEN** wrap behaviour comes from **`IntervalProjection`** only
- **AND** no additional **Start** events are recorded or emitted on wrap

### Requirement: Continuous engage spans loop boundaries (Phase 7)

One continuous engagement SHALL remain one **Start** + one **End** pair regardless of how many loop boundaries playback crosses during the engage.

#### Scenario: Loop wrap does not split engage

- **GIVEN** **Start** at tick 0 and **End** at tick 3840 with snapshotted **`loopLengthTicks = 960`**
- **WHEN** playback runs through four loop wraps
- **THEN** the stored sequence still has exactly one **Start** and one **End**
- **AND** wraps are handled by **`IntervalProjection`**

### Requirement: triggerSequenceTick ownership (Phase 7)

**`LoopTriggerSequenceManager`** SHALL own **`triggerSequenceTick`** — the sequence timeline (analogous to **`currentTick`** for loop **`midiEvents`** capture).

| Mode | Rule |
|------|------|
| **`triggerSequenceRecord`** / **overdub** | Live **capture** appends **Start**/**End** at **`triggerSequenceTick`**; timeline advances with transport |
| **Build / edit** | No live capture append — edits mutate stored **`TriggerEvent.tick`** directly; **`triggerSequenceTick`** MAY still advance during **`triggerSequencePlay`** |
| **`triggerSequencePlay`** | **`triggerSequenceTick`** advances with transport as sequence playhead position |

#### Scenario: Record overdub captures at triggerSequenceTick

- **GIVEN** **`triggerSequenceRecord`** is active and transport is running
- **WHEN** the user triggers a slot engage
- **THEN** a **Start** event is appended with **`tick`** from **`triggerSequenceTick`**
- **AND** **`triggerSequenceTick`** advances with transport

### Requirement: Edit LoopTriggerSequence without stopping playback (Phase 7)

The system SHALL **never require** stopping transport, **`triggerSequencePlay`**, or Note Edit to modify a stored **`LoopTriggerSequence`**. Build-phase edits, local undo, and **`TriggerEvent`** changes SHALL be permitted while sequence playback runs.

Edits SHALL apply without a mandatory stop gate. Events at **`tick`** values after current **`triggerSequenceTick`** during play SHALL take effect when playback reaches them. Edits at or before **`triggerSequenceTick`** SHALL take effect at the next **Start**/**End** boundary.

#### Scenario: Build sequence while triggerSequencePlay runs

- **GIVEN** **`triggerSequencePlay`** is active
- **WHEN** the user enters build phase and adds or removes **Start**/**End** events
- **THEN** playback continues without a required stop
- **AND** stored sequence changes are visible to subsequent playback steps

#### Scenario: Edit pending triggers during idle wait without stopping transport

- **GIVEN** build phase idle wait and transport running
- **WHEN** the user double-presses a slot to undo a pending **Start**
- **THEN** the pending sequence is edited on the local stack
- **AND** transport and sequence playback are not required to stop

#### Scenario: Build edit does not append live capture events

- **GIVEN** LTS Edit Mode build phase with pending events
- **WHEN** the user adds or removes triggers during idle wait
- **THEN** stored event **`tick`** values are edited directly
- **AND** no **Start**/**End** rows are appended from live capture unless **`triggerSequenceRecord`** or **overdub** is active

### Requirement: PlaybackTarget abstraction (Phase 7)

**`TriggerEvent`** SHALL reference a **`PlaybackTarget`** (not a bare slot index). v1 **`PlaybackTargetType::Loop`** only. Phase 8 MAY add **`LoopSlice`**. Future types (clip, scene, timeline region) are reserved — not implemented in Phase 7.

#### Scenario: Start event carries PlaybackTarget snapshot

- **GIVEN** capture from slot 3
- **WHEN** a **Start** event is committed
- **THEN** **`PlaybackTarget`** stores stable **`LoopId`** and snapshot fields
- **AND** replay resolves the target without live slot lookup

### Requirement: LoopTriggerSequenceManager and Track ownership (Phase 7)

**`LoopTriggerSequenceManager`** SHALL record, edit, and sequence **`TriggerEvent`**s and SHALL request playback state changes. **`Track`** SHALL own playback state, transport, **`projectionCycleStartTick`**, and SHALL execute loop playback via **`IntervalProjection`**. The Manager SHALL NOT duplicate transport or projection mathematics.

| **`LoopTriggerSequenceManager`** | **`Track`** |
|----------------------------------|-------------|
| Record, edit, sequence **`TriggerEvent`**s | Own playback state, transport, active loop |
| Own **`triggerSequenceTick`**, LTS Edit Mode | Own **`projectionCycleStartTick`**, execute **`IntervalProjection`** |
| Request playback state changes | Perform playback |

#### Scenario: Manager coordinates Track does not duplicate sequencing

- **GIVEN** **`triggerSequencePlay`** reaches an **End** event
- **WHEN** the next **Start** is due
- **THEN** **`LoopTriggerSequenceManager`** requests the target change
- **AND** **`Track`** applies **`ProjectionContext`** and plays projected **`midiEvents`**

### Requirement: LoopTriggerSequence Edit Mode and capture routing (Phase 7)

The firmware SHALL implement **`LoopTriggerSequence`** playback and capture. The control surface **Jams** row SHALL be renamed **LoopTriggerSequence** row. Retire **`jamLoop`** and **Jams** row naming.

**`LoopTriggerSequence Edit Mode`** SHALL override Play, Loop Edit, and Note Edit when entered from the **LoopTriggerSequence row** or track REC/PLAY during LTS workflow. Display and faders SHALL follow **`projectedInterval`** so Note Edit MAY continue during **`triggerSequencePlay`**.

**Modes within LTS workflow:**

- **Build phase** (inside LTS Edit Mode) — long-press A + short-press B; idle timeout default **2 bars**; local undo during build; REC/PLAY idle → commit at next bar
- **`triggerSequenceRecord`** / **`triggerSequenceOverdub`** — capture on **`triggerSequenceTick`**; arm at **next bar**; timestamps from **`currentTick`**
- **`triggerSequencePlay`** — **Start**/**End** playback via **`ProjectionContext`** + **`IntervalProjection`** on **`Track`**

**Midi capture routing:**

| Context | Track REC/PLAY | LoopTriggerSequence row |
|---------|----------------|---------------------------|
| Play / Loop Edit / Note Edit | **`midiEvents`** record/overdub | Enter LTS Edit Mode |
| LTS Edit Mode | Fallback to Loop Edit for **`midiEvents`** | **`triggerSequenceRecord`** / overdub only |

**Track** owns playback execution and **`projectionCycleStartTick`**. **`LoopTriggerSequenceManager`** owns **`triggerSequenceTick`**, sequencing, and playback coordination. Input gestures dispatch from **`MidiButtonActions`**.

#### Scenario: LoopTriggerSequence row replaces Jams row on control surface

- **GIVEN** the control surface reference layout
- **WHEN** Phase 7 ships
- **THEN** the former **Jams** row is labeled **LoopTriggerSequence** in cheat sheet, row guide, and MIDI config docs
- **AND** each button recalls or targets one saved **`LoopTriggerSequence`** preset

#### Scenario: LTS Edit Mode overrides Note Edit without blocking display

- **GIVEN** an active Note Edit session and **`triggerSequencePlay`** running
- **WHEN** the user adjusts faders
- **THEN** display and fader updates use **`projectedInterval`**
- **AND** note editing MAY continue alongside sequence playback

#### Scenario: Track REC in LTS Edit Mode falls back to Loop Edit for midiEvents

- **GIVEN** **`LoopTriggerSequence Edit Mode`** is active
- **WHEN** the user presses track REC/PLAY expecting **`midiEvents`** capture
- **THEN** state falls back to Loop Edit for loop **`midiEvents`** record/overdub
- **AND** trigger capture remains on the LoopTriggerSequence row only

#### Scenario: Long-press A and short-press B enters build phase

- **GIVEN** the track is in Play, Loop Edit, or Note Edit
- **WHEN** the user long-presses slot A then short-presses slot B
- **THEN** **`LoopTriggerSequence Edit Mode`** begins the build phase
- **AND** idle timeout defaults to **2 bars** (global config)

#### Scenario: Empty slot four-press lifecycle matches midiLoop model

- **GIVEN** an empty slot in LTS Edit Mode
- **WHEN** the user short-presses four times in sequence
- **THEN** presses map to arm → **`triggerSequenceRecord`** → **`triggerSequencePlay`** → **`triggerSequenceOverdub`**
- **AND** each step has dedicated trigger-sequence undo entries

#### Scenario: triggerSequenceRecord arms at next bar

- **GIVEN** the user starts **`triggerSequenceRecord`**
- **WHEN** the next bar grid tick arrives
- **THEN** capture appends **`TriggerEvent`** rows on **`triggerSequenceTick`**
- **AND** row timestamps derive from **`currentTick`**
- **AND** snapshotted **`loopLengthTicks`** and **`loopStartTickSnap`** are stored at capture

#### Scenario: REC/PLAY commits build idle at next bar

- **GIVEN** build phase with pending **`TriggerEvent`** rows and idle wait active
- **WHEN** the user presses REC/PLAY
- **THEN** pending sequence commits at **next bar** and **`triggerSequencePlay`** begins
- **AND** midi loop record does **not** start

#### Scenario: Long-press first slot exits build and discards

- **GIVEN** build phase with a pending sequence
- **WHEN** the user long-presses the first slot in the pending list
- **THEN** LTS Edit Mode build exits and the pending sequence is discarded

#### Scenario: Short press slot during triggerSequencePlay exits sequence

- **GIVEN** **`triggerSequencePlay`** is active
- **WHEN** the user short-presses a slot
- **THEN** sequence play exits and normal slot behavior resumes

#### Scenario: Long press slot during triggerSequencePlay cues new sequence

- **GIVEN** **`triggerSequencePlay`** is active
- **WHEN** the user long-presses a slot to select a different **`LoopTriggerSequence`**
- **THEN** the new sequence is cued while the current sequence continues
- **AND** playback switches after idle timeout (global config, default 2 bars)

#### Scenario: Bar or 16th jam writes slot loop metadata

- **GIVEN** bar/16th region jam is active
- **WHEN** the user short-presses an empty slot
- **THEN** jam region is written as that slot's loop metadata (**`loopStartTick`** snapshot)
- **WHEN** the user long-presses a filled slot
- **THEN** existing slot loop metadata is overwritten

#### Scenario: Midi loop capture stays in EditSession

- **GIVEN** an active **`EditSessionType::Loop`**, **`Note`**, or **`ControlChange`** session outside LTS Edit Mode
- **WHEN** the user records loop **`midiEvents`**
- **THEN** capture uses existing edit/capture rules — separate from **`triggerSequenceRecord`**

### Requirement: Global undo/redo in normal play mode (Phase 7)

When not in LTS Edit Mode **build phase**, global undo/redo SHALL be reachable from track (36/37) and slot double/triple.

#### Scenario: Play mode overdub undo on track or slot

- **GIVEN** normal play (not in LTS build phase)
- **WHEN** the user double-presses REC/PLAY or a slot
- **THEN** the appropriate global undo runs

#### Scenario: Global undo restored after triggerSequencePlay

- **GIVEN** build phase commits to **`triggerSequencePlay`**
- **WHEN** the user double-presses REC/PLAY or a slot
- **THEN** global undo/redo works as in normal play

### Requirement: LTS build phase local undo/redo (Phase 7)

During LTS Edit Mode **build phase**, global undo SHALL be ignored. Slot double/triple SHALL undo/redo **`TriggerEvent`** additions on a local edit stack only.

#### Scenario: Build phase uses local list undo/redo

- **GIVEN** build phase with three pending **`TriggerEvent`** entries
- **WHEN** the user double-presses a slot
- **THEN** the last trigger is undone on the local stack
- **AND** track global undo is not invoked

#### Scenario: Global undo ignored during build phase

- **GIVEN** LTS Edit Mode build phase is active
- **WHEN** the user double-presses REC/PLAY expecting global undo
- **THEN** global undo does **not** run

### Requirement: TriggerEvent Start End step transition (Phase 7)

When **`triggerSequencePlay`** runs a **`LoopTriggerSequence`**, step boundaries SHALL use **End** (stop target A) and the next **Start** (engage target B). **`Track`** SHALL play loop **`midiEvents`** via **`IntervalProjection`** using snapshotted **Start** payload.

**`repeatCount`** on **Start** snapshot: **0** = repeat via projection until **End**; **>0** = cap full wraps within engage.

#### Scenario: Default engage repeats until End

- **GIVEN** **Start** at tick 0 and **End** at tick 3840 with **`repeatCount = 0`** and snapshotted **`loopLengthTicks = 960`**
- **WHEN** **`triggerSequencePlay`** runs the engage
- **THEN** **`IntervalProjection`** wraps at 960-tick intervals until **End**
- **AND** no extra **Start** events appear

#### Scenario: repeatCount caps wraps within engage

- **GIVEN** a **Start** with **`repeatCount = 2`** before its **End**
- **WHEN** **`triggerSequencePlay`** runs the engage
- **THEN** playback performs at most two full wraps via **`IntervalProjection`**
- **AND** playback stops or holds per Phase 7 policy before **End** if repeats exhaust early

#### Scenario: triggerSequencePlay runs A then B using End and Start

- **GIVEN** a committed sequence: engage A (**Start**…**End**) then engage B
- **WHEN** playback reaches A's **End**
- **THEN** target A stops
- **AND** B's **Start** engages with B's snapshotted **`PlaybackTarget`**

#### Scenario: triggerSequencePlay uses snapshotted Start payload not live slot

- **GIVEN** **Start** A with snapshotted **`loopStartTickSnap`**
- **WHEN** **`triggerSequencePlay`** engages A
- **THEN** **`ProjectionContext`** is built from A's snapshot
- **AND** current slot **`Loop.loopStartTick`** is not read

### Requirement: Loop slice metadata (Phase 8)

The firmware SHALL support 16th-granularity projection windows on **Loop** (verse/chorus slices). **`TriggerEvent`** entries MAY reference Loop slice metadata after Phase 8.

#### Scenario: 16th slice window auditions then loops

- **GIVEN** the user holds 16th A and presses 16th B within the current loop
- **WHEN** both buttons are released
- **THEN** playback auditions the temporary 16th-granularity projection window
- **AND** on the next 16th grid tick that window loops
- **AND** the window MAY be persisted as **Loop** slice metadata referenced by **`TriggerEvent`** entries

### Requirement: PlaybackCursor retirement (Phase 4)

The firmware SHALL **remove** **`PlaybackCursor`** and **`LoopPlaybackRuntime::loopHeadWindow`**. Per-slot playback cache SHALL remain on **`LoopPlaybackRuntime.primaryWindow`** and **`Loop.lastTickInLoop`** / **`Loop.nextEventIndex`**. Wrap and phase math SHALL use **`projectionCycleStartTick`** and **`IntervalProjection`** — not **`PlaybackCursor`**.

#### Scenario: PlaybackCursor header deleted after Phase 4

- **WHEN** Phase 4 playback migration completes
- **THEN** **`include/PlaybackCursor.h`** is removed
- **AND** no **`PlaybackCursor`** identifier remains in firmware or tests

#### Scenario: Event scan index stays on Loop

- **GIVEN** **`playMidiEvents`** after migration
- **WHEN** playback advances through merged events
- **THEN** scan position uses **`Loop.nextEventIndex`**
- **AND** not a separate **`mergeCursorIndex`** field

### Requirement: No consumer-local wrap mathematics after migration

After this change ships, playback, display, and edit consumers SHALL NOT implement independent loop-wrap or `% loopLength` classification logic outside `IntervalProjection` helpers.

#### Scenario: Grep gate passes

- **WHEN** migration Phase 5 completes
- **THEN** wrap mathematics for display, playback, edit analysis, LED phase, and select navigation reside in `IntervalProjection` only
- **AND** retired helpers in `NoteEditFocus` / `NoteMovementUtils` are removed or thin-wrap the engine

### Requirement: Projection preserves note identity

Projection SHALL change **coordinates only** — never create new logical notes. Every projected interval for a canonical note SHALL carry the same **`noteId`** across all k-equivalent copies.

#### Scenario: Equivalent copies share noteId

- **GIVEN** canonical Note #42 with storage span `900 → 1080`
- **WHEN** `generateEquivalentIntervals` emits `-60 → 120`, `900 → 1080`, and `1860 → 2040`
- **THEN** every candidate has `noteId == 42`
- **AND** overlap analyze, undo/redo, and selection treat them as one note in different coordinates

### Requirement: Projection never writes back

Interval projection SHALL produce temporary derived objects only. It SHALL NOT write back into canonical storage, **`EditSession.store`**, or committed loop passes.

#### Scenario: DisplayNote is projection output

- **WHEN** Display projection produces `DisplayNote` segments
- **THEN** input canonical event vectors are unchanged

### Requirement: Future viewport editing

The same projection pipeline SHALL support editing a sub-window of a larger loop by changing only **`ProjectionContext.window`**. Edit algorithms SHALL NOT require awareness of full loop extent beyond supplied context.

#### Scenario: Partial loop edit window

- **GIVEN** loop length 8 bars and visible edit window bars 3–4
- **WHEN** Edit projection context uses `window = { start: bar3Tick, end: bar5Tick }`
- **THEN** overlap analysis uses projected intervals inside that window
- **AND** storage ticks remain global linear canonical values
