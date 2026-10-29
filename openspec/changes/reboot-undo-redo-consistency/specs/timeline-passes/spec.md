## ADDED Requirements

### Requirement: Restored global undo and redo update visible committed content

Global undo and redo after reboot SHALL display the resulting committed notes through the normal display update without requiring a transport stop/restart or completion of a replacement snapshot when a complete current display representation for the requested interval is available. An authoritative empty result SHALL clear removed notes. Unavailable replacement content SHALL NOT be interpreted as an empty result.

#### Scenario: Undo after reboot has a ready display representation

- **GIVEN** a restored loop contains a record pass and an overdub and its notes have been displayed
- **WHEN** global undo excludes the overdub and a complete current representation covers the visible interval
- **THEN** the next completed normal display update SHALL show the surviving notes without the excluded overdub
- **AND** snapshot rebuilding SHALL NOT force retention of the pre-undo frame

#### Scenario: Redo restores the visible overdub

- **WHEN** global redo re-includes the restored overdub and current display content is available
- **THEN** the next completed normal display update SHALL include its notes exactly once
- **AND** unrelated notes and the selected slot SHALL remain unchanged

#### Scenario: Undo removes the last visible note

- **WHEN** undo yields a complete current representation with no notes in the visible interval
- **THEN** the next completed normal display update SHALL paint that interval empty
- **AND** SHALL NOT retain deleted notes because the replacement result is empty

#### Scenario: Publication is pending with no qualified replacement

- **WHEN** a replacement display representation is unavailable or does not cover the requested interval
- **THEN** the existing coherent-frame fallback SHALL remain in effect
- **AND** no partial or stale result SHALL be presented as current content

### Requirement: Restored global undo and redo preserve playable saved content

Restored loop undo/redo SHALL preserve the stored content required by the surviving and redoable passes. Repeated undo/redo SHALL NOT permanently silence a nonempty slot through source turnover. The behavior SHALL apply with or without a restored snapshot, and SHALL NOT require re-recording the loop.

#### Scenario: Repeated undo and redo of a restored overdub

- **GIVEN** a saved record pass and overdub have been loaded and playback serves both
- **WHEN** the overdub is undone and redone repeatedly while playback runs
- **THEN** the record pass SHALL remain playable
- **AND** each redo SHALL restore the overdub exactly once through the ordinary playback handoff
- **AND** saved pass content SHALL remain intact

#### Scenario: Restored-slot recording safety

- **GIVEN** a loaded slot contains accepted saved content
- **WHEN** ordinary playback, undo/redo and a subsequent Record or overdub operation use the slot
- **THEN** source maintenance SHALL NOT erase its pass payloads
- **AND** explicit user-requested replacement recording SHALL retain its existing replacement semantics

#### Scenario: Other slots keep their content

- **WHEN** a restored slot undergoes undo/redo or its source preparation is temporarily refused
- **THEN** unrelated sounding slots SHALL retain their content and playback
- **AND** no saved-slot clear, reload or transport restart SHALL be required as a repair
