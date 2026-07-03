## ADDED Requirements

### Requirement: Projection layer is derived only

Playback, display, reconstruction, and editor visualization SHALL be derived projections from canonical storage. The projection layer SHALL produce temporary objects only and SHALL NOT write back into canonical storage.

#### Scenario: Wrapped display segments

- **WHEN** canonical storage has `NoteOn.tick = 1400`, `NoteOff.tick = 1550`, `loopLength = 1536`
- **THEN** projection produces tail segment starting at `1400` through loop end and head segment from `0` through wrapped head end (per `DisplayNote` inclusive end convention in `NoteUtils`)
- **AND** canonical storage remains unchanged

#### Scenario: No write-back from display end

- **WHEN** projection computes a wrapped display end tick
- **THEN** no MidiEvent tick in canonical storage is modified as a side effect

### Requirement: Loop shorten hides notes in projection only

When loop length is shorter than a note's `NoteOn.tick`, projection (playback and display) SHALL omit that note. Canonical storage SHALL retain the note events so they reappear in projection if loop length increases again.

#### Scenario: Shortened loop hides note

- **WHEN** `loopLength` decreases from 3072 to 1536 and a note has `NoteOn.tick = 2000`
- **THEN** projection does not emit that note for playback or piano roll
- **AND** canonical storage still contains the NoteOn and NoteOff events

#### Scenario: Lengthened loop restores note

- **WHEN** `loopLength` increases from 1536 back to 3072
- **THEN** the note at `NoteOn.tick = 2000` appears in projection again without a new capture or edit

### Requirement: Playback ordering from projected ticks

Playback SHALL order note events by `event.tick % loopLength` (playback order index). Linear storage ticks beyond `loopLength` SHALL fire at the correct wrapped position in the loop.

#### Scenario: Linear off fires at loop head

- **WHEN** canonical storage has `NoteOff.tick = 1536` and `loopLength = 1536`
- **THEN** playback fires the NoteOff at wrapped tick `0` in loop order
- **AND** the note does not stick on

### Requirement: NOTE_EDIT playback uses session store (verification)

During NOTE_EDIT, `ensurePlaybackWindowBuilt` SHALL use `editManager.sessionMidiEvents()` when `sessionPreviewRevision_` is active (shipped Tier 2). This **replaces** the playback source with the session store; there is no materialized canonical underlay to merge or suppress.

Phase 2 task §2.4 is **verification only** — confirm HITL that transaction geometry is audible during edit. No new per-span overlay implementation is required.

#### Scenario: Session store drives playback during edit

- **WHEN** NOTE_EDIT is active and the user moves a note before macro commit
- **THEN** `mergedEvents` in the playback window equals the session store
- **AND** playback reflects transaction-state geometry without reading materialized loop passes

#### Scenario: Playback after macro commit

- **WHEN** `commitAllPendingNoteEditActions` completes with `normalizeAll`
- **THEN** session store is canonical and playback uses that store on subsequent loops

### Requirement: Piano roll and editor window clipping

Long-loop piano-roll window filtering and NOTE_EDIT window probes SHALL use projection-layer wrap semantics. Notes whose linear off extends past the window end SHALL still appear when a projected head segment intersects the window.

#### Scenario: Head segment visible in window

- **WHEN** a note has linear off past the window end but a projected head segment intersects the window
- **THEN** the head segment is included in the filtered display list
- **AND** canonical storage is not modified

### Requirement: materializeWrapSegments boundary

Phase 1 MAY alias `NoteUtils::reconstructNotes`, playback order rebuild, session-store playback, and `DisplayWindowUtils` as the projection boundary. All new code SHALL treat projection as read-only with respect to canonical storage.

#### Scenario: reconstructNotes is projection

- **WHEN** `reconstructNotes` is called on canonical storage
- **THEN** it returns DisplayNote vectors only
- **AND** the input MidiEvent store is unchanged
