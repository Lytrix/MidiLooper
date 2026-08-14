## Requirements

### Requirement: Effective store is current before overdub

The system SHALL maintain a runtime effective representation of committed loop content that is updated incrementally when content mutates (capture pass commit, undo/redo pass state toggle, edit apply, loop load complete).

The effective store MUST be valid before `beginOverdubSession()` is invoked. Building or merging the full effective representation in response to the overdub button press MUST NOT occur.

#### Scenario: Pass commit updates effective store

- **WHEN** a capture pass is committed or an undo entry toggles pass state
- **THEN** the effective store reflects the new effective prefix without requiring a full pass merge at overdub entry

#### Scenario: Load completes with current effective store

- **WHEN** a loop finishes loading from storage
- **THEN** the effective store is populated before the slot becomes eligible for overdub

---

### Requirement: Overdub entry does not reconstruct display

`beginOverdubSession()` MUST NOT call full-loop `gatherCommittedEvents()` or full-loop `reconstructDisplayNotes()`.

Display-note reconstruction MUST NOT be a prerequisite for entering overdub or beginning capture.

#### Scenario: Overdub opens without full-loop gather

- **WHEN** the user starts overdub on a loop with thousands of committed events
- **THEN** overdub capture becomes active without a full-loop event gather on the MIDI path

#### Scenario: Display may lag briefly

- **WHEN** overdub starts while the visual cache is stale
- **THEN** MIDI capture is active immediately and display MAY catch up asynchronously via idle slice rebuild

---

### Requirement: Range query API

The effective store SHALL expose range-oriented access (`range(startTick, lengthTicks, out)`) suitable for overdub source establishment.

The dependency direction MUST be: layered passes → effective store → tick range → overdub source. Exporting the entire effective vector as the only overdub entry API MUST NOT be the long-term contract.

#### Scenario: Overdub source uses window only

- **WHEN** `establishOverdubSourceView` runs at overdub entry
- **THEN** it queries only the overdub source window (playhead ± configured margin) from the effective store

---

### Requirement: Overdub-open latency bound

Measured `ODUB,begin_capture` duration MUST NOT scale with the number of historical passes, total loop event count, or total display note count.

For the [`035414`](../../../../captures/session_20260814_035414.log) class (68 bars, ~3385 events), `begin_capture` MUST complete in under 50 ms.

#### Scenario: Large loop immediate overdub entry

- **WHEN** overdub is requested on a 68-bar loop with ~3385 committed events
- **THEN** `begin_capture` telemetry is under 50 ms and the first overdub note-on MAY follow within one normal MIDI scheduling window

---

### Requirement: Overlap behavior preserved

Existing overdub overlap resolution (RC-K3 note-off path, `OverlapNoteIdSet` capacity 128) MUST remain behaviorally unchanged.

#### Scenario: Native overlap fixtures

- **WHEN** native overdub source and overlap hold tests run after D1/D2
- **THEN** all fixtures PASS without changing overlap geometry semantics
