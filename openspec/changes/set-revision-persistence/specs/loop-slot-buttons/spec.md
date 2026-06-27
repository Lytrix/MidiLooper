## ADDED Requirements

### Requirement: Loop slot buttons mirror Record gesture map

Loop slot buttons (notes 50–57, channel 16) SHALL use the **same gesture map** as Record (note 36),
with actions scoped to the pressed slot index:

| Gesture | Record (36) | Loop slot (50–57) |
|---------|-------------|-------------------|
| Short | Arm / record flow for active slot | Arm / record flow for **that** slot |
| Double | Undo | Undo (slot-scoped when applicable) |
| Triple | Redo | Redo (slot-scoped when applicable) |
| Long | Open **track** loop overlay | Open **slot** loop overlay |

Short-press overdub-on-non-empty-slot behavior (today on `TOGGLE_RECORD` /
`TOGGLE_RECORD_FOR_SLOT`) SHALL remain on **short press**, not double press.

#### Scenario: Slot double press is undo outside overlay

- **WHEN** the loop overlay is **not** open
- **AND** the user double-presses loop slot 4
- **THEN** slot-scoped undo runs (replaces prior overdub-on-double behavior)

**Migration note:** Loop slot double-press no longer starts overdub; overdub remains on short press
when slot is non-empty and playing.

### Requirement: Record long press is thin wrapper over slot loop overlay

Record (36) long-press SHALL open the **same loop overlay component** as loop-slot long-press, in
**track mode**. Track mode SHALL NOT duplicate a separate UI; it SHALL only change scope parameters:

- **Clear track** pre-selected (fast clear path unchanged).
- **Multi-select** enabled on loop rows.
- All **non-empty loops on the active track** pre-selected when the loop list is shown.
- Apply copies into **matching slot indices** on the **current track** (e.g. source `LOOP_01_03` →
  current track slot 3).

#### Scenario: Record opens shared overlay in track mode

- **WHEN** the user long-presses Record (36) on track 2
- **THEN** the loop overlay opens with **Clear track** selected
- **AND** the loop list uses track mode with active track 2 slots pre-selected

### Requirement: Long press opens loop overlay with clear pre-selected

**Long press** on Record (36) or a loop slot (50–57) SHALL open the **load/save overlay** in
**loop-management mode**. The overlay SHALL **not** clear immediately on long-press release.

The initially selected row SHALL be:

- **Clear track** when entered from Record long-press (active track).
- **Clear slot** when entered from loop-slot long-press (that slot index).

A **single press** on the pre-selected **Clear** row SHALL execute the clear and **exit** the overlay
(fast path: long press + one click).

The **Loops** list (see `slot-loop-import`) SHALL appear below **Clear** in the same overlay — not a
separate **Import loop** row.

#### Scenario: Fast clear slot

- **WHEN** the user long-presses loop slot 2
- **THEN** the loop overlay opens with **Clear slot 2** as the selected row
- **WHEN** the user single-presses the selected **Clear slot 2** row
- **THEN** slot 2 is cleared
- **AND** the overlay exits

#### Scenario: Fast clear track

- **WHEN** the user long-presses Record (36)
- **THEN** the loop overlay opens with **Clear track** as the selected row
- **WHEN** the user single-presses the selected **Clear track** row
- **THEN** the active track is cleared
- **AND** the overlay exits

#### Scenario: Loop list visible below clear without extra row

- **WHEN** the loop overlay is open from slot or Record long-press
- **THEN** the user MAY scroll from **Clear** into the **Loops** section in the same list
- **AND** no separate **Import loop** menu row is required
