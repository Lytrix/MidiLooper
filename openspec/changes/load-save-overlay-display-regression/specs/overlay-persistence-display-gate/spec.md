## ADDED Requirements

### Requirement: Overlay catalog reads defer while persistence owns SD

While any deferred persistence job (Current epoch save, revision commit, or revision load) is
**queued**, **in progress**, or actively performing **SD I/O** in the current slice, the load/save
overlay SHALL NOT open SD files for catalog listing or metadata preview.

The display layer SHALL use the last valid in-RAM overlay cache when reads are deferred, or omit
detail rows until reads are allowed.

#### Scenario: Set list refresh blocked during revision load

- **WHEN** a revision load job is in progress
- **AND** the load/save overlay is open in minimal mode
- **THEN** `DisplayManager` does not call SD catalog list or metadata read helpers
- **AND** the frame draws minimal loading content only

#### Scenario: Detail panel uses cache during save-then-load

- **WHEN** save-then-load pipeline is active with a valid prior detail cache
- **AND** the user had previewed a Set row before confirming load
- **THEN** the overlay may show cached detail or omit detail
- **AND** no new SD metadata read runs until persistence gate opens

#### Scenario: Catalog refresh runs after pipeline idle

- **WHEN** revision load completes and persistence gate opens
- **AND** the overlay is closed
- **THEN** the next overlay open may refresh the set list from SD

### Requirement: Overlay persistence phase is derived from pipeline ownership

The firmware SHALL expose a single **overlay persistence phase** (`Idle`, load in progress,
save-then-load awaiting commit+load) derived from revision commit/load pipeline state. Overlay
minimal mode resolution SHALL read this phase — not re-derive from five independent booleans at
display time.

#### Scenario: Minimal mode before commit dispatch

- **WHEN** the user confirms dirty-load **Yes**
- **AND** revision commit is pending but not yet in progress
- **THEN** overlay mode is **MINIMAL_LOADING**
- **AND** full ROOT catalog draw path does not run

#### Scenario: Phase returns idle after load complete

- **WHEN** deferred revision load reaches COMPLETE
- **THEN** overlay persistence phase becomes **Idle**

### Requirement: Minimal overlay draw path is SD-free

While overlay mode is **MINIMAL_LOADING**, each display frame SHALL limit draw work to minimal
text, persistence status dots, and sidebar save indicator. It SHALL NOT invoke piano-roll note
resolution or overlay catalog SD reads.

#### Scenario: No piano roll during minimal load

- **WHEN** overlay mode is **MINIMAL_LOADING** during PLAYING
- **THEN** `DisplayManager` does not call `resolveDisplayNotes` or loop visual cache rebuild
- **AND** transport playback continues
