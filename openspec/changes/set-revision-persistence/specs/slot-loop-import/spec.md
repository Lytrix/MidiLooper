## ADDED Requirements

### Requirement: Unified loop overlay with Loops and Details columns

The loop overlay SHALL present a two-column layout matching the Set load overlay family:

- Left: **Loops** — **Clear**, then scrollable loop rows (and Set browse sections).
- Right: **Details** — preview for the focused loop row.

Slot long-press entry SHALL open **slot mode** (single target slot index). Record long-press entry
SHALL open **track mode** (multi-select, active track scope) as a thin wrapper over the same component.

#### Scenario: Overlay shows Loops and Details headers

- **WHEN** the loop overlay is open
- **THEN** the left column is labeled **Loops** and the right column **Details**
- **AND** **Clear** is the first actionable row

### Requirement: Loop rows grouped by track with skip headers

Loop rows SHALL be grouped under non-selectable **track header** rows (e.g. `---TRACK 1---`). Loop
rows SHALL use labels `LOOP_TT_SS` (track index, slot index within track, e.g. `LOOP_01_03`).

Scroll navigation SHALL **skip** track header rows — focus moves only between actionable rows
(**Clear**, loop rows, Set rows).

#### Scenario: Scroll skips track header

- **WHEN** the user scrolls down from **Clear** into the loop list
- **THEN** focus moves to the first loop row under `---TRACK 1---`
- **AND** the header row is never focused or selected

### Requirement: Slot mode add and overwrite semantics

In **slot mode**, **add** (short press) SHALL:

- **Empty target slot** — clone source loop payload into the target slot.
- **Non-empty target** — append imported MIDI as a new capture layer (merge); preserve existing
  content and undo history; no destructive merge.

**Overwrite** (double press) SHALL replace the target slot payload entirely.

#### Scenario: Slot mode add merges into non-empty slot

- **WHEN** slot mode targets slot 3 which already has MIDI
- **AND** the user short-presses `LOOP_01_05`
- **THEN** imported notes are appended as a new layer in slot 3
- **AND** existing slot content is preserved

### Requirement: Slot mode single target add and overwrite

In **slot mode**, the overlay SHALL target one slot index from the long-press entry gesture.

- **Short press** on a source loop row SHALL **add** that loop into the target slot (preserve existing
  content when the target slot is non-empty and add semantics apply) and **exit** the overlay.
- **Double press** on a source loop row SHALL **overwrite** the target slot and **exit** the overlay.

The overlay SHALL be navigable via **encoder rotation** (scroll focus) and **encoder button**
(short / double / long press) using the same row actions as MIDI — see `set-browser-overlay`
encoder requirements.

#### Scenario: Encoder scroll reaches loop row in slot mode

- **WHEN** slot mode is open with **Clear slot 3** focused
- **AND** the user rotates the encoder down past the track header
- **THEN** focus lands on the first actionable loop row
- **AND** **Details** shows that loop preview

#### Scenario: Slot mode add from current Set loop

- **WHEN** slot mode targets slot 3
- **AND** the user short-presses `LOOP_01_05` (MIDI or encoder button on focused row)
- **THEN** loop payload from track 1 slot 5 is added into current track slot 3 per add semantics
- **AND** the overlay exits

#### Scenario: Slot mode overwrite

- **WHEN** slot mode targets slot 3
- **AND** the user double-presses `LOOP_01_05`
- **THEN** current track slot 3 is overwritten with that loop payload
- **AND** the overlay exits

### Requirement: Track mode pre-selects active track loops in current Set only

In **track mode** (Record long-press), the overlay SHALL **pre-select** all **non-empty** loop rows
belonging to the **active track** in the **current Set** section only. After drill-down into
Favorites or Browse (`LOOP_PICK`), no loops SHALL be pre-selected.

#### Scenario: Active track loops pre-selected at root

- **WHEN** track mode opens on track 2 with loops in slots 1, 2, and 4
- **THEN** `LOOP_02_01`, `LOOP_02_02`, and `LOOP_02_04` are selected
- **AND** other loop rows start unselected

### Requirement: Track mode toggle select apply add and overwrite

In **track mode** the overlay SHALL support multi-select with these gestures:

- **Long press** on a loop row SHALL **toggle** its selection without exiting the overlay.
- **Short press** on a loop row SHALL **add** all **currently selected** loops into matching slot
  indices on the **current track** using the same **add** semantics as slot mode (empty → clone;
  non-empty → append layer).
- **Double press** on a loop row SHALL **overwrite** all **currently selected** loops into matching
  slot indices on the **current track** and **exit** the overlay.

Matching slot index means source `LOOP_TT_SS` copies into slot **SS** on the current track.

#### Scenario: Track mode toggle off one loop

- **WHEN** track mode has `LOOP_02_01` and `LOOP_02_02` pre-selected
- **AND** the user long-presses `LOOP_02_01`
- **THEN** `LOOP_02_01` is deselected and the overlay stays open

#### Scenario: Track mode add all selected

- **WHEN** `LOOP_02_01` and `LOOP_02_04` are selected
- **AND** the user short-presses any selected loop row
- **THEN** both loops are added into slots 1 and 4 on the current track
- **AND** the overlay exits

#### Scenario: Track mode overwrite all selected

- **WHEN** `LOOP_02_02` and `LOOP_02_04` are selected
- **AND** the user double-presses any selected loop row
- **THEN** slots 2 and 4 on the current track are overwritten
- **AND** the overlay exits

### Requirement: Loop list shows all tracks with active track centered

The loop list SHALL show **all tracks × all slots** grouped by track headers. Scroll focus SHALL
auto-center or emphasize the **active track** section when the overlay opens.

#### Scenario: All tracks visible in current Set section

- **WHEN** the loop overlay opens at `ROOT`
- **THEN** loop rows for track 1 through `NUM_TRACKS` are listed under track headers
- **AND** the active track section is visually emphasized

### Requirement: Empty loop rows are visible but not selectable

Empty loop rows (`occupied == 0`) SHALL appear in the list with empty **Details** state. They SHALL
NOT receive scroll focus, multi-select, or short/double apply actions.

#### Scenario: Empty row skipped on scroll

- **WHEN** the user scrolls past `LOOP_01_02` which is empty
- **THEN** focus skips to the next non-empty actionable loop row

### Requirement: Current Set loops listed first for minimum clicks

The loop list SHALL show **all loops in the current Set** grouped by track as the **first scroll
section**, above Favorites and Browse. Data SHALL come from **Current** / latest derived revision.

#### Scenario: Empty loop row visible but not selectable for apply

- **WHEN** `LOOP_01_02` has no payload
- **THEN** the row MAY show empty state in **Details**
- **AND** scroll focus SHALL NOT land on that row

### Requirement: Favorites and browse Sets below current Set loops

Below the current-Set loop section, the overlay SHALL show:

1. **Favorites** — scrollable favorite Sets (`set.bin` `favorite` flag).
2. **Browse Sets** — scrollable full Set list (same sort rules as workspace overlay).

A **short press** on a Set row SHALL enter `LOOP_PICK` — that Set's loop list grouped by track (latest
validated revision), not a full workspace load.

#### Scenario: Short press favorite Set opens grouped loop list

- **WHEN** the user short-presses favorite Set `S0002` in the loop overlay
- **THEN** mode becomes `LOOP_PICK` with track-grouped loops for `S0002` latest revision
- **AND** the full workspace is **not** loaded into Current

### Requirement: Optional revision pick on drilled Set

The overlay SHALL default to the **latest validated revision** when drilling into a Set. The system
SHALL support optional long-press on a Set row to pick a specific `v####` before loop selection.

#### Scenario: Optional older revision for import

- **WHEN** the user long-presses Set `S0002` in drill-down
- **THEN** revision history for `S0002` is available
- **AND** selecting `v0002` updates the grouped loop list source

### Requirement: Import mutates Current only on successful read

Loop copy SHALL apply to Current and RAM only after a successful read from the source revision.
Failed reads SHALL not mark the Set dirty or write SD.

#### Scenario: Failed import leaves Current unchanged

- **WHEN** source revision blob fails validation during loop copy
- **THEN** the target slot(s) on the current track are unchanged
- **AND** no deferred save is queued

### Requirement: Import marks dirty via epoch advance

Loop copy SHALL advance `currentEpoch` when content is successfully applied (via normal deferred
save completion) — not from overlay navigation alone.

#### Scenario: Browse and cancel stays clean

- **WHEN** the user opens loop overlay and browses Sets without applying a loop
- **THEN** `currentEpoch == lastCommittedEpoch` (unchanged)

#### Scenario: Successful apply marks uncommitted

- **WHEN** loop copy completes and deferred save finishes the new epoch
- **THEN** `currentEpoch > lastCommittedEpoch`
