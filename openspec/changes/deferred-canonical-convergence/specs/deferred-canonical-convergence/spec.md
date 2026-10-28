## Purpose

Provides eventual canonical/companion convergence for a slot whose capture-stop retired raw, so a superseded canonical base never blocks stop completion and the derived representation still reaches the committed state once an authoritative source exists.

## ADDED Requirements

### Requirement: Single slot-level obligation

The system SHALL maintain at most one deferred canonical-convergence obligation per slot. Any committed-state advance while an obligation is owed or building SHALL coalesce into that obligation (target becomes the newest committed generation) rather than queue a second one.

#### Scenario: Coalescing across committed-state advances
- **WHEN** a slot has a pending deferred-convergence obligation and the committed generation advances (a raw stop commit, an undo/redo, an edit, or a wrap)
- **THEN** the slot still owns exactly one obligation, whose target is the newest committed generation

#### Scenario: No obligation stacking
- **WHEN** many committed-state advances occur in sequence while convergence is owed
- **THEN** the slot never accumulates more than one deferred-convergence obligation

### Requirement: Raw commit marks the obligation owed

When a capture-stop retires raw because its canonical derivation was unavailable or became superseded before completion, the system SHALL record a deferred-convergence obligation keyed to the committed generation that stop produced, together with the loop coverage.

#### Scenario: Superseded before bind commits raw and is owed
- **WHEN** a capture-stop reaches its canonical-base decision with its pinned causal source no longer current
- **THEN** the stop SHALL NOT bind the stale source, SHALL commit its immutable raw representation, and the slot SHALL become owed for the committed generation

#### Scenario: Superseded mid-build commits raw and is owed
- **WHEN** a capture-stop begins an inline canonical build against a still-current causal source and the source is superseded before the build completes, so the build returns a terminal denial
- **THEN** the stop SHALL commit its immutable raw representation and the slot SHALL become owed for the committed generation

### Requirement: Build only against an authoritative source

The system SHALL begin deferred convergence only after resolving a fresh authoritative source identity whose snapshot covers the target committed generation, and SHALL bind the builder to that resolved identity.

#### Scenario: Deferred convergence on source availability
- **WHEN** a slot is owed and an authoritative source covering the target committed generation becomes available
- **THEN** the system SHALL resolve that source identity and build the canonical/companion representation through the target generation

#### Scenario: No build without authority
- **WHEN** a slot is owed but no authoritative source covering the target generation exists
- **THEN** the system SHALL leave the obligation owed and SHALL NOT begin building

### Requirement: Source change discards the candidate

If the committed generation or the published source identity changes while deferred convergence is building, the system SHALL discard the in-flight candidate and re-resolve from the new authority; it SHALL NOT publish a candidate built from a superseded source. A terminal denial during the build SHALL also discard the candidate and return the obligation to owed. An ordinary non-terminal `Pending` — even temporarily unchanged — SHALL keep the obligation building.

#### Scenario: Mid-build supersession
- **WHEN** a newer commit or an undo/redo changes the committed generation while a candidate is building
- **THEN** the candidate SHALL be discarded and the obligation SHALL re-resolve to the new authoritative source

#### Scenario: Terminal denial returns to owed
- **WHEN** the deferred build returns a terminal denial or its source becomes invalid
- **THEN** the candidate SHALL be discarded and the obligation SHALL return to owed

#### Scenario: Non-progressing build stays building
- **WHEN** the deferred build returns an ordinary non-terminal pending without advancing
- **THEN** the obligation SHALL remain building and SHALL NOT return to owed

### Requirement: Convergence is independent of interaction completion

Deferred convergence failure or delay SHALL NOT hold the capture-stop FIFO head. The committed raw content SHALL remain authoritative and playable while convergence is owed or building.

#### Scenario: FIFO drains while convergence is owed
- **WHEN** the authoritative source remains unavailable across more stop interactions than the capture-stop ring depth
- **THEN** every stop SHALL retire raw and the FIFO SHALL drain without filling

#### Scenario: Convergence failure is non-blocking
- **WHEN** deferred convergence returns a terminal denial or its bound source becomes invalid
- **THEN** the committed raw content SHALL remain authoritative and the obligation SHALL return to owed without blocking further interaction

### Requirement: Owed is standing demand for service

The `Owed` state itself SHALL make the slot eligible for deferred-convergence service; correctness SHALL NOT depend on catching a publication transition. On a later owner turn the slot SHALL observe an already-ready authoritative source and begin convergence without any further user interaction.

#### Scenario: Authority already ready is observed later
- **WHEN** a slot is owed and the required current snapshot becomes available purely as a result of deferred publication completing
- **THEN** a later owner turn SHALL observe that authority and begin convergence without a new user action or a caught transition

### Requirement: Exclusive canonical-builder ownership

At most one owner SHALL drive a slot's canonical-build machinery at any time; the owners are the inline capture-stop path and deferred convergence. An inline capture-stop SHALL never wait for deferred convergence.

#### Scenario: Inline stop preempts a deferred build
- **WHEN** deferred convergence is building and a new inline capture-stop needs the canonical builder
- **THEN** the deferred candidate SHALL be discarded, the obligation SHALL return to owed (or coalesce), and the inline stop SHALL own the builder without waiting

#### Scenario: Active inline build defers service
- **WHEN** an inline canonical build is active
- **THEN** a deferred owner turn SHALL do nothing and the obligation SHALL remain owed

### Requirement: Inline build satisfies the obligation

A successfully adopted inline canonical result from an authoritative source whose committed revision covers the target committed generation SHALL satisfy the outstanding deferred obligation through that generation, without a later duplicate publication.

#### Scenario: Healthy inline build clears the obligation
- **WHEN** a deferred obligation is owed and a later inline canonical build completes against an authoritative source covering the target generation
- **THEN** the obligation SHALL clear and no duplicate later publication SHALL occur

### Requirement: Slot replacement cancels the obligation

Slot or loop clear, reset, replacement, or load SHALL cancel any outstanding deferred-convergence obligation and invalidate its candidate before replacement content can become authoritative.

#### Scenario: Clear cancels an owed obligation
- **WHEN** a slot with an outstanding obligation is cleared or loaded with new content
- **THEN** the obligation SHALL become idle and the candidate SHALL NOT publish into the new content
