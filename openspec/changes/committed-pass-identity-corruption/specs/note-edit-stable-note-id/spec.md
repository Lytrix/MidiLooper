## ADDED Requirements

### Requirement: Sealed capture-pass rows are immutable

Once a capture pass is sealed/committed, its event-row content SHALL be immutable. Later overdub commits,
canonical/prefix normalization, certified-prefix resume, companion publication, undo/redo, disable/enable and
derived-cache work MAY change pass-level state or create new derived representations, but SHALL NOT add,
remove, move or rewrite a row in that sealed capture pass. Undo/redo/disable SHALL normally change the pass
state, not its original rows.

#### Scenario: Long overdub chain leaves the Record pass rows identical

- **WHEN** a Record pass is sealed and a chain of overdub commits runs over it
- **THEN** the complete semantic Record-pass row vector (event type, tick, NoteId, channel, pitch, velocity)
  after every overdub commit equals the vector captured immediately after the Record seal
- **AND** the first unequal commit names the corrupting transition; a row whose NoteId moves, loses its pair
  or gains a duplicate is visible in the delta (the chain's NoteId 24 witness is one such row, not the oracle)

#### Scenario: The seal itself stores only complete pairs

- **WHEN** a Record take is sealed at a stop whose trailing bar contains a note strictly inside that bar
- **THEN** the sealed pass keeps that bar and stores the note's NoteOn and NoteOff as one pair at their original
  ticks
- **AND** no NoteOn is wrapped into the loop start without its NoteOff

#### Scenario: A covering stop length leaves the captured rows unchanged

- **WHEN** the chosen Record loop length is greater than the maximum captured event tick
- **THEN** the Record-stop projection preserves every captured row (event type, tick, NoteId, channel, pitch,
  velocity)
- **AND** projection acts only on rows genuinely outside the chosen loop

#### Scenario: Pass state changes do not mutate the sealed source rows

- **WHEN** a sealed pass is disabled, re-enabled or its state changes through undo/redo
- **THEN** its original event rows are unchanged
- **AND** only the pass state decides whether those rows participate

#### Scenario: Derived representations preserve the sealed identity and pairing

- **WHEN** a root, resolved view, companion projection or playback window materializes a sealed pass
- **THEN** each corresponding note keeps its birth NoteId and its On/Off pairing from the sealed source
- **AND** a derived representation never becomes the authority for repairing or reshaping the sealed rows

#### Scenario: The display oracle stays strict

- **WHEN** the committed source is read instead of a stale published root
- **THEN** the frame preserves each expected NoteId and geometry exactly once
- **AND** a stale root or a broader source selection is never used to hide a bad pass row
