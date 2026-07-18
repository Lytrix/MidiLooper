## MODIFIED Requirements

### Requirement: Loop passes owner

The system SHALL store capture and note-edit history on a loop through **`passes[]`**
(**LoopPasses**). **LoopPasses** SHALL contain:

- zero or one **recordPass**,
- zero or more **overdubPass** entries ordered by `mergeSequence`,
- zero or more **editPass** entries in **`editPasses[]`**, each tagged with **editPassIndex**.

A pass SHALL be described as **pending** only while it is **not** yet in **`passes[]`**. The system
SHALL NOT use **committed** as a pass state label, container name, or pass undo kind suffix.

Architectural **Commit** (making immutable loop state runtime-visible) and APIs such as
`hasCommittedPasses` refer to **committed loop state** / presence of canonical pass content — not a
`PassState::Committed` label on rows in **`passes[]`**.

#### Scenario: Loop exposes passes not takes array

- **WHEN** firmware loads a loop with capture and edit history
- **THEN** `Loop` accesses history through **`passes`**
- **AND** there is no `takes[]` / **Take** type in product code

#### Scenario: Committed-pass presence is not a pass state enum

- **WHEN** a loop has rows in **`passes[]`** after Commit
- **THEN** committed-pass presence APIs may report true
- **AND** pass rows still use existing Active/Disabled (or equivalent) state labels
- **AND** there is no `PassState::Committed` enum value required by this change

## ADDED Requirements

### Requirement: Publish vocabulary retired for committed-truth APIs

Product code SHALL use Commit/Committed action+scope names for APIs that describe canonical
runtime-visible pass content (formerly Publish/Published). Guides and active OpenSpec prose for
this concern SHALL prefer **committed passes** / **committed loop state** over “published events”
for pass-level meaning. **Events** remains appropriate only for MIDI gather/range helpers.

#### Scenario: Rename replaces hasPublishedEvents

- **WHEN** the rename pass for this change is complete
- **THEN** `hasPublishedEvents` is not a public Loop API
- **AND** the successor committed-passes presence API is used at former call sites
