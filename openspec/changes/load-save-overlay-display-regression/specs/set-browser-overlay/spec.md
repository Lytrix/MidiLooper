## MODIFIED Requirements

### Requirement: Load paths keep overlay in minimal mode until complete

Dirty-load **Yes**, dirty-load **No**, and **clean** load SHALL keep the overlay open in **minimal
mode** (spinner / save+load status only) until deferred load reaches 100%, then **exit the overlay
automatically** and resume full piano-roll display. This differs from the **Save** row, which exits
immediately while commit continues in background.

When load completes while the overlay is open, the system SHALL call `exitLoadSaveMode`, consume
display refresh pending, and invalidate live and per-loop visual caches on the next full display
update.

#### Scenario: Clean load minimal until done then auto-exit

- **WHEN** the user loads a clean Current into Set `S0002`
- **THEN** the overlay shows minimal mode until load completes
- **THEN** the overlay exits without user gesture
- **AND** full piano-roll display resumes on the next frame

#### Scenario: Save-then-load minimal covers commit queue window

- **WHEN** the user confirms dirty-load **Yes**
- **AND** revision commit is pending before the FSM dispatches
- **THEN** the overlay remains in minimal mode
- **AND** does not redraw the ROOT set list with SD reads

#### Scenario: Re-open overlay during minimal load does not reset pipeline navigation

- **WHEN** a load pipeline is active and minimal mode is showing
- **AND** the user toggles the overlay closed then open again
- **THEN** minimal mode is restored
- **AND** overlay navigation reset does not clear staged load targets

### Requirement: Minimal display during SD jobs

During revision commit, revision load, or save-then-load pipeline **while the load/save overlay is
open**, the display SHALL skip full piano-roll redraw and show only minimal overlay content plus
save/load status indicators.

Overlay catalog SD reads SHALL be deferred per `overlay-persistence-display-gate` while persistence
jobs are active.

#### Scenario: Piano roll suppressed during load pipeline with overlay open

- **WHEN** a deferred load is in progress and the overlay is open
- **THEN** `DisplayManager` draws minimal overlay only until load completes
- **AND** does not perform overlay catalog SD reads during the pipeline
