## ADDED Requirements

### Requirement: Set list sorted by latest revision time

The overlay Set list SHALL sort Sets by **latest revision `createdUnix`** (newest first).

#### Scenario: Recently revised Set appears first

- **WHEN** Set `S0002` received revision `v0005` today and Set `S0003` last revised yesterday
- **THEN** `S0002` appears above `S0003` in the list

### Requirement: Subtitle is optional human-facing name in right panel

`set.bin` `subtitle` (max 48 chars) SHALL support user-edited and auto-generated labels. The right
**Details** panel SHALL show subtitle when non-empty. When never set, the UI SHALL omit the subtitle
line (no placeholder). Auto-fill date SHALL run only on **first Save** for a new Set.

**Encoder long press** on the subtitle field in **Details** SHALL enter subtitle edit mode.

#### Scenario: No subtitle hides line

- **WHEN** Set `S0004` has empty `subtitle`
- **THEN** the right panel does not show a subtitle row

#### Scenario: Encoder long press edits subtitle

- **WHEN** the user focuses subtitle in **Details** and long-presses the encoder button
- **THEN** subtitle edit mode opens (max 48 chars)
- **AND** epoch advance marks workspace dirty when persisted to Current scope

### Requirement: Overlay explicit mode state machine

The load/save UI SHALL use explicit overlay modes — not a single boolean:

| Mode | Purpose |
|------|---------|
| `ROOT` | Workspace Set browser or loop overlay root list |
| `DIRTY_PROMPT` | "Save Current?" modal |
| `REVISION_HISTORY` | Revision list for one Set |
| `LOOP_PICK` | Drilled Set loop list (from Favorites/Browse) |
| `MINIMAL_LOADING` | Spinner during deferred SD load/commit |

Only one mode SHALL be active. Transitions SHALL preserve stack context (e.g. `LOOP_PICK` remembers
`ROOT` for back navigation).

#### Scenario: Dirty prompt replaces root temporarily

- **WHEN** the user initiates Set load while dirty
- **THEN** mode becomes `DIRTY_PROMPT` until Yes, No, or Cancel

### Requirement: Save Current dirty prompt modal

The system SHALL enter `DIRTY_PROMPT` when Set or revision load is requested and workspace is dirty
(`lastCommittedEpoch != currentEpoch`), presenting rows:

- **Yes** — Save + Load (save-then-load pipeline)
- **No** — Discard + Load (single press)
- **Cancel** — return to prior overlay mode without load

**Encoder short press** SHALL confirm the focused row. Loop import SHALL NOT trigger this prompt (see
`revision-load`).

#### Scenario: Cancel returns to Set list

- **WHEN** `DIRTY_PROMPT` is shown and the user selects **Cancel**
- **THEN** mode returns to `ROOT` and no load runs

### Requirement: Drill-down back navigation

The overlay SHALL support **long press** on the context row (Set header or back affordance) from
`LOOP_PICK` or `REVISION_HISTORY` to return **one level** to the preserved parent mode without
exiting the overlay entirely.

#### Scenario: Long press backs out of drilled loop list

- **WHEN** the user drilled into `S0005` loop list from Browse
- **AND** long-presses the back/context control
- **THEN** mode returns to `ROOT` loop or workspace list with scroll position preserved

### Requirement: Revision history navigation

- **Short press** on a Set row SHALL load the **latest** validated revision (subject to dirty-load rules).
- **Double press** on a Set row SHALL toggle **favorite** in `set.bin` (see `set-revision-catalog`).
- **Long press** on a Set row SHALL open `REVISION_HISTORY` (newest first).
- **Single press** on a revision row SHALL load that revision (subject to dirty-load rules).
- **Second long press** on the same Set row, or **double press** Play/Stop while overlay is open,
  SHALL **exit** the overlay **without** performing a load.

#### Scenario: Exit overlay from history without loading

- **WHEN** revision history for `S0003` is open
- **AND** the user double-presses Play/Stop
- **THEN** the overlay closes and no revision is loaded

### Requirement: Save row exits overlay immediately

The **Save** action row SHALL queue `commitRevision` and **exit** the overlay immediately (commit
continues in background with minimal display).

#### Scenario: Save does not wait on overlay

- **WHEN** the user confirms **Save**
- **THEN** the overlay closes before commit completes

### Requirement: Load paths keep overlay in minimal mode until complete

Dirty-load **Yes**, dirty-load **No**, and **clean** load SHALL keep the overlay open in **minimal
mode** (spinner only) until deferred load reaches 100%, then exit. This differs from the **Save**
row, which exits immediately.

#### Scenario: Clean load minimal until done

- **WHEN** the user loads a clean Current into Set `S0002`
- **THEN** the overlay shows minimal mode until load completes
- **THEN** the overlay exits and full piano-roll display resumes

### Requirement: Loop overlay clear and import rows

The load/save overlay SHALL support **loop-management mode** when opened from Record or loop-slot
long-press. In that mode the left action list SHALL include at minimum:

- **Clear track** or **Clear slot** (scope matches entry gesture) — **initially selected**.
- **Loops** list below **Clear** (same overlay; see `slot-loop-import` spec) — not a separate
  **Import loop** row.

Slot entry uses **slot mode** (single target). Record entry uses **track mode** (multi-select wrapper
over the same component).

#### Scenario: Clear is first selected row on slot entry

- **WHEN** loop-management overlay opens from loop slot 5 long-press
- **THEN** **Clear slot 5** is the highlighted / selected row
- **AND** the **Loops** list is reachable by scroll below **Clear**

### Requirement: Encoder scrolls overlay list focus

The system SHALL route **encoder rotation** (GPIO encoder; MIDI encoder mapping **deferred v2**) to
overlay list navigation when any overlay mode is active. One detent SHALL move focus to the next or
previous **actionable** row using the same navigation model as list-scroll buttons.

Encoder scroll SHALL **skip** non-actionable rows (e.g. `---TRACK N---` headers in the loop overlay).
The **Details** panel SHALL update to preview the newly focused row.

#### Scenario: Encoder scroll skips track header

- **WHEN** the loop overlay is open and focus is on **Clear track**
- **AND** the user rotates the encoder one step down
- **THEN** focus moves to the first loop row under `---TRACK 1---`
- **AND** the track header is not focused

#### Scenario: Encoder scroll in Set browser

- **WHEN** the workspace Set browser is open
- **AND** the user rotates the encoder
- **THEN** `DisplayManager` list selection moves among **Save**, **Current**, and Set rows
- **AND** the right preview updates for the focused row

### Requirement: Encoder button uses same overlay press logic

The system SHALL route the **encoder button** (GPIO `BUTTON_ENCODER`; MIDI `NOTE_EDIT_MODE` mapping
**deferred v2**) through the **same short / double / long press detection and row actions** as MIDI
overlay buttons when any overlay mode is active. Note-edit cycle, pitch-edit hold, and exit SHALL be
**disabled** while overlay owns input.

| Press | Focused row | Action |
|-------|-------------|--------|
| Short | **Clear** | Execute clear (slot or track) + exit |
| Short | Set row (workspace) | Load latest revision (dirty rules apply) |
| Double | Set row (workspace) | Toggle favorite (no load) |
| Short | Loop row (slot mode) | Add into target slot + exit |
| Short | Loop row (track mode) | Add all selected loops + exit |
| Double | Loop row (slot mode) | Overwrite target slot + exit |
| Double | Loop row (track mode) | Overwrite all selected loops + exit |
| Long | Loop row (track mode) | Toggle loop row selection (no exit) |
| Long | Other rows / no row | Exit overlay without action |

Double-press and long-press timing SHALL reuse the existing MIDI button debounce windows unless a
native test proves overlay-specific tuning is required.

#### Scenario: Encoder short press confirms Clear track

- **WHEN** the loop overlay is open in track mode with **Clear track** focused
- **AND** the user short-presses the encoder button
- **THEN** the active track is cleared
- **AND** the overlay exits

#### Scenario: Encoder long press toggles loop selection in track mode

- **WHEN** track mode has `LOOP_02_01` focused
- **AND** the user long-presses the encoder button
- **THEN** `LOOP_02_01` selection toggles
- **AND** the overlay stays open

#### Scenario: Encoder button does not cycle note edit in overlay

- **WHEN** the loop overlay or Set browser is active
- **AND** the user short-presses the encoder button
- **THEN** note-edit mode cycle does **not** run
- **AND** the focused overlay row action runs instead

#### Scenario: GPIO and MIDI encoder share overlay routing

- **WHEN** the Set browser is active
- **AND** the user rotates the GPIO encoder
- **THEN** overlay list selection updates

### Requirement: Overlay is input-modal only

While any overlay mode is active, the overlay SHALL suspend **input** for:

- record and overdub arm actions
- loop slot selection gestures
- note-edit gestures

The overlay SHALL **NOT** suspend:

- playback scheduling
- MIDI clock generation
- deferred persistence FSM (`processDeferredSaveState`)

Display rendering MAY continue. Avoid global UI freeze or stopping transport.

#### Scenario: Playback continues during overlay

- **WHEN** the Set browser is open during **PLAYING**
- **THEN** playback and clock continue
- **AND** deferred Current epoch writes MAY still advance in idle slices

### Requirement: Overlay suspends Record and slot input

While overlay is active, Record (36) and loop slot (50–57) MIDI **input** SHALL route to overlay
actions only — not global record, undo, or overdub handlers.
#### Scenario: Slot double does not global undo in overlay

- **WHEN** the loop overlay is open
- **AND** the user double-presses a loop slot button
- **THEN** global slot undo does **not** run
- **AND** overlay row action applies if mapped

### Requirement: Minimal display during SD jobs

During revision commit, revision load, or save-then-load pipeline, the display SHALL skip full
piano-roll redraw and show only the save-status spinner.

#### Scenario: Piano roll suppressed during load pipeline

- **WHEN** a deferred load is in progress
- **THEN** `DisplayManager` skips heavy note draw paths until load completes
