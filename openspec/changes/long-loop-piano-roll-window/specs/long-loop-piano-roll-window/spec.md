## ADDED Requirements

### Requirement: Long loops cap detailed piano-roll window at 16 bars
When loop length exceeds 16 bars, the detailed piano-roll layer SHALL render at most a 16-bar tick window mapped onto the piano-roll width. When loop length is 16 bars or less, the detailed layer SHALL render the full loop (current behavior).

#### Scenario: 64-bar loop shows readable detailed window
- **WHEN** a loop with length 64 bars is displayed during playback
- **THEN** the detailed piano roll maps at most 16 bars of tick range onto the piano-roll width
- **AND** notes outside the detailed window are not drawn in the detailed layer

#### Scenario: Short loop unchanged
- **WHEN** loop length is 16 bars or less
- **THEN** the detailed piano roll maps the full loop length onto the piano-roll width
- **AND** the overview strip MAY be hidden or collapsed

### Requirement: Detailed window starts at loop start until user moves it
Until LOOP_EDIT navigation changes it, the detailed window SHALL start at tick 0 (bars 0–15 for a 64-bar loop). The detailed window SHALL NOT auto-follow the playhead during playback.

#### Scenario: Default window at loop start
- **WHEN** a loop longer than 16 bars is first displayed
- **THEN** `detailedWindowStartTick` is 0
- **AND** the detailed window covers bars 0 through 15 (or fewer if loop is between 17 and 16 bars wide after cap logic)

#### Scenario: Playhead outside detailed window shows on overview
- **WHEN** playback position is outside the detailed window tick range
- **THEN** the playhead cursor is not drawn in the detailed piano-roll layer
- **AND** the playhead position is indicated on the overview strip

### Requirement: Overview strip shows full-loop context
Loops longer than 16 bars SHALL display a compact full-width overview strip with binary grouped note presence and a marker for the detailed window region.

The overview strip SHALL use dynamic grouping (1, 2, 4, 8, 16, 32, or 64 bars per segment) so the full loop fits the display width. Each segment SHALL indicate **has notes** or **no notes** only (v1). The strip SHALL mark the start and end of the detailed piano-roll window and SHALL show the full-loop playhead position.

#### Scenario: Overview strip covers full loop
- **WHEN** a 64-bar loop is displayed
- **THEN** the overview strip spans the full piano-roll width
- **AND** each segment indicates whether any note intersects that segment's tick span
- **AND** a window box or start/end markers show which region maps to the detailed layer

#### Scenario: Grouping scales with loop length
- **WHEN** loop length varies from 32 to 64 bars
- **THEN** the overview strip selects a grouping from {1, 2, 4, 8, 16, 32, 64} bars per segment so all segments fit the display width

### Requirement: LOOP_EDIT navigates detailed window
In **LOOP_EDIT** mode, the user SHALL be able to move the detailed window along the loop and resize its length from 1 to 16 bars. Controls SHALL be inactive outside LOOP_EDIT mode.

#### Scenario: Move window in LOOP_EDIT
- **WHEN** the user is in LOOP_EDIT on a 64-bar loop and moves the window control to bars 16–31
- **THEN** `detailedWindowStartTick` equals 16 × `BAR_TICKS`
- **AND** the detailed layer and overview window box update to that region

#### Scenario: Resize window in LOOP_EDIT
- **WHEN** the user resizes the detailed window to 8 bars in LOOP_EDIT
- **THEN** `detailedWindowBars` equals 8
- **AND** the overview window box width reflects 8 bars
- **AND** `detailedWindowStartTick` is clamped so the window fits within loop length

### Requirement: Display capture reports window state for long loops
When loop length exceeds 16 bars, firmware SHALL emit capture markers reporting detailed window start tick, window width in bars, and the count of notes drawn in the detailed layer, so HITL verification can assert bounded-window behavior without full-frame dumps.

#### Scenario: DISP reports window on 64-bar loop
- **WHEN** a 64-bar loop is playing and display capture is enabled
- **THEN** serial output includes window start tick, window bars (≤ 16), and window note count
- **AND** HITL verification can assert `windowBars ≤ 16` and `windowNoteCount > 0` when the loop has notes in the visible window
