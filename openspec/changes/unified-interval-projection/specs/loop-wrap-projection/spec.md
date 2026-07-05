## MODIFIED Requirements

### Requirement: Projection layer is derived only

Playback, display, reconstruction, and editor visualization SHALL be derived projections from canonical storage via the **`IntervalProjection`** engine (`generateEquivalentIntervals`, `selectProjectedInterval`, `projectNoteIntervals`, **`projectDisplayNotes`**). The projection layer SHALL produce temporary objects only and SHALL NOT write back into canonical storage.

#### Scenario: Wrapped display segments (head/tail — boundary split)

- **WHEN** canonical storage has `NoteOn.tick = 1400`, `NoteOff.tick = 1550`, `loopLength = 1536`
- **THEN** **`projectDisplayNotes()`** produces tail segment starting at `1400` through loop end and head segment from `0` through wrapped head end (per `DisplayNote` inclusive end convention in `NoteUtils`)
- **AND** canonical storage remains unchanged
- **AND** this is **not** the same as long-loop viewport window filtering (>16 bars)

#### Scenario: Long-loop viewport filter is separate

- **WHEN** loop length exceeds 16 bars and a bounded piano-roll window is active
- **THEN** viewport filtering uses `DisplayWindowUtils` on reconstructed notes
- **AND** head/tail boundary split is handled by `projectDisplayNotes()` before window filter

#### Scenario: No write-back from display end

- **WHEN** projection computes a wrapped display end tick
- **THEN** no MidiEvent tick in canonical storage is modified as a side effect

### Requirement: Playback ordering from projected ticks

Playback SHALL order note events using **Playback projection** over canonical storage. Linear storage ticks beyond `loopLength` SHALL fire at the correct wrapped position in the loop via projected interval selection — not ad-hoc consumer-local `% loopLength` ordering after migration.

#### Scenario: Linear off fires at loop head

- **WHEN** canonical storage has `NoteOff.tick = 1536` and `loopLength = 1536`
- **THEN** playback fires the NoteOff at wrapped tick `0` in loop order
- **AND** the note does not stick on

#### Scenario: Playback order precomputes phases before sort

- **GIVEN** merged playback events need reordering for wrap-correct send
- **WHEN** `rebuildPlaybackOrder` runs
- **THEN** each event's sort key is computed once via **`playbackEventPhase`**
- **AND** the sort comparator does not allocate or call **`generateEquivalentIntervals`**

### Requirement: Playback hot path is non-allocating

Playback sort and per-tick MIDI send SHALL use **`IntervalProjection::playbackEventPhase`** (scalar k-scan over `{0, loopLength}`). The playback hot path SHALL NOT call **`generateEquivalentIntervals`**, **`projectNoteIntervals`**, or allocate containers inside **`std::sort`** comparators or the per-tick send loop. **`projectPlaybackEventPhase`** MAY wrap **`playbackEventPhase`** for tests and contextful call sites outside the hot path.

### Requirement: Piano roll and editor window clipping

Long-loop piano-roll window filtering and NOTE_EDIT window probes SHALL use **Display projection** wrap semantics. Notes whose linear off extends past the window end SHALL still appear when a projected head segment intersects the window.

#### Scenario: Head segment visible in window

- **WHEN** a note has linear off past the window end but a projected head segment intersects the window
- **THEN** the head segment is included in the filtered display list
- **AND** canonical storage is not modified

### Requirement: materializeWrapSegments boundary

The projection boundary SHALL be implemented by **`IntervalProjection`** (`include/Utils/IntervalProjection.h`). `NoteUtils::reconstructNotes`, playback window rebuild, session-store playback, and `DisplayWindowUtils` SHALL delegate wrap mathematics to that engine. **`projectDisplayNotes()`** owns Display head/tail adapter logic.

#### Scenario: reconstructNotes delegates to IntervalProjection

- **WHEN** `reconstructNotes` is called on canonical storage
- **THEN** it uses Display projection internally
- **AND** returns `DisplayNote` vectors only with unchanged input store

#### Scenario: NOTE_EDIT Tier 2 playback unchanged

- **WHEN** NOTE_EDIT is active
- **THEN** `ensurePlaybackWindowBuilt` still uses `sessionMidiEvents()` as sole playback source
- **AND** Playback projection applies to event ordering within that window without merging materialized passes
