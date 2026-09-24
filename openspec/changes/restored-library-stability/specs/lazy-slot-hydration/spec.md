# lazy-slot-hydration — restored-library inventory and loss attribution

## ADDED Requirements

### Requirement: Observed current state is reported separately from a nullable historical cause

The system SHALL report, per `(track, slot)`, an observed **current state** and, separately, a nullable
**historical cause**. The two SHALL NOT be merged into one field, and a current state SHALL NOT be reported as
a cause.

The observed fields per slot SHALL be at least:

- **payload** — absent, present-with-invalid-token, or present-and-verified;
- **enumeration** — whether the slot was offered to the boot/background restore path;
- **request** — none, queued, running, waiting (named reason), or terminal (named terminal), carrying the full
  request identity `(track, slot, requestGeneration, setEpoch)`;
- **content** — whether the slot holds committed content, with a content inventory;
- **selection** — whether the slot is required for initial playback.

The **current state** SHALL be reported as a set of at least: `empty` (no payload, no committed content, no
outstanding request), `populated` (a verified payload and/or committed content), and `pending` (an outstanding
request that has not terminated). A slot MAY be both `populated` and `pending`; the state is not a scalar.

The **historical cause** SHALL be nullable and SHALL be one of: `catalog omission`, `restore refusal`,
`intentional eviction`, `content loss`, `ambiguous`, `unknown`. A cause SHALL NOT be recorded without the
evidence field(s) supporting it and a stated precedence rule.

The following SHALL hold:

- a slot whose current state is `empty` with **no prior inventory and no explicit event** SHALL record the
  cause `unknown`. `empty` SHALL NOT be recorded as a cause, and no loss cause SHALL be recorded in that case;
- "never populated" SHALL be asserted **only** where a prior inventory or an explicit event establishes it;
- a disappearance established by a prior inventory, but with no explicit event, SHALL record the cause
  `unknown` with the prior inventory as its evidence — it SHALL NOT be promoted to `content loss` or
  `intentional eviction` on the strength of the prior inventory alone;
- an explicit eviction or deletion event SHALL be required to record `intentional eviction`;
- when two or more causes are equally consistent with the evidence, the report SHALL record **all** consistent
  causes and the cause `ambiguous` — it SHALL NOT choose one;
- a payload that fails validation *and* produced a refused restore SHALL record both facts rather than being
  forced into a single cause;
- a request that has not terminated SHALL be reported `pending` with no terminal asserted for its identity.

The report SHALL record its own **completeness**: the observation window's start and end, whether the window
covered every slot required for initial playback, whether the background admission scan completed, and whether
every slot was observed within one consistent view or across turns.

#### Scenario: An empty slot with no history is `empty` in state and `unknown` in cause

- **WHEN** a slot has no payload, no committed content, no outstanding request, no prior inventory and no
  recorded event
- **THEN** its current state is `empty`
- **AND** its historical cause is `unknown`
- **AND** neither `empty` nor any of the four loss causes is recorded as its cause

#### Scenario: A prior inventory without an event establishes the disappearance, not the cause

- **WHEN** a prior inventory recorded the slot as populated
- **AND** the slot is now `empty` with no recorded event
- **THEN** the report records the disappearance with the prior inventory as its evidence
- **AND** the cause remains `unknown`, not `content loss` or `intentional eviction`

#### Scenario: An explicit eviction event is required to record intentional eviction

- **WHEN** a bounded policy is observed to have dropped or reused a slot, with the decision recorded
- **THEN** the cause is `intentional eviction`
- **AND** the absence is not reported as a defect or as content loss

#### Scenario: A corrupt payload is not forced into one cause

- **WHEN** a slot's payload file is present but fails token or body validation
- **AND** the restore for that slot reached a `refused` terminal
- **THEN** the report records the validation failure and the refused terminal as separate facts
- **AND** the cause records the consistent cause(s), with `ambiguous` where they cannot be separated

#### Scenario: An unfinished request is pending, not a loss

- **WHEN** a slot's restore request is queued, running, or waiting at the observation
- **THEN** the observed request state and its named wait are recorded
- **AND** the current state includes `pending`, and no terminal is asserted for that identity

#### Scenario: Two consistent causes are recorded, not resolved by preference

- **WHEN** the evidence is consistent with two of the four causes
- **THEN** both are recorded with their supporting fields
- **AND** the cause is `ambiguous`

#### Scenario: An incomplete observation is marked incomplete

- **WHEN** the observation window ended before the background admission scan completed, or before every
  initial-playback slot was observed
- **THEN** the report states that the window is incomplete
- **AND** no count from that window is presented as the settled library state

### Requirement: The on-demand library observation has a bound owner, admission and invalidation contract

The system SHALL provide a **bounded, on-demand** observation of the library inventory that does NOT depend on
the settled census conditions (an empty restore queue, no live `SlotLoadSession`, and both restore job
instances inactive). The settled post-restore census checkpoint SHALL be retained unchanged for settled memory
measurements, and the on-demand observation SHALL NOT replace, relax or be reported as that measurement.

**Owner and service point.** The observation SHALL be owned by the existing persistence/restore owner and
serviced at a **declared existing service point** on the interval/idle turn. It SHALL NOT introduce a new
thread, timer, interrupt, or periodic hot-path cost.

**Incremental collection.** Collection SHALL be incremental and bounded per invocation: SD reads and
committed-content traversal SHALL be bounded and accounted for, and one observation over a large library SHALL
NOT perform an unbounded traversal in one turn. The per-invocation bound SHALL be declared.

**SD-read admission.** Collection SHALL respect the existing SD-session admission — the same gate that
excludes capture and concurrent slot load or persistence write. It SHALL NOT open, read, or hold SD resources
concurrently with a load job, a persistence write, or capture.

**Invalidation.** A slot's observation SHALL be scoped to the identity it was taken under
`(track, slot, requestGeneration, setEpoch)`. If a slot's request generation or the current set epoch changes
while an observation is in progress, the affected slot's observation SHALL be **invalidated or restarted**.
Fields observed under different generations SHALL NOT be combined into one slot record, and a content
reference SHALL NOT be retained across observation slices.

**Permitted degradation.** Fields that are unavailable because admission was denied, or because an
observation was invalidated, SHALL remain explicitly `unknown` in the report. The report SHALL nonetheless be
produced, and a waiting request SHALL still be named.

**Consistency and framing.** Slots observed on different turns SHALL be marked as such; an observation
assembled across turns SHALL NOT be presented as a single consistent instant.

#### Scenario: A permanently waiting request still yields a diagnostic

- **WHEN** a restore request is stuck waiting and the settled census checkpoint therefore never fires
- **THEN** the on-demand observation is still produced
- **AND** it names the waiting request's state, its named wait, and the slot's observed payload and content state

#### Scenario: Collection over a large library is bounded

- **WHEN** the observation is requested for a library larger than one invocation's budget
- **THEN** it collects incrementally within the declared per-invocation bound
- **AND** the partial result is marked as covering only the slots it observed, with the observation turns
  recorded

#### Scenario: A mutation between slices invalidates the affected slot

- **WHEN** a slot's request generation advances (an external reset such as a clear) between two slices of one
  observation
- **THEN** that slot's observation is invalidated or restarted
- **AND** the report does not combine pre-change and post-change fields into one slot record
- **AND** the affected fields are reported `unknown` if they cannot be re-observed within the observation

#### Scenario: A set-epoch change invalidates the whole observation

- **WHEN** the loaded current set's epoch changes while an observation is in progress
- **THEN** the observation is invalidated or restarted under the new epoch
- **AND** no slot record mixes fields from the two epochs

#### Scenario: Denied SD admission degrades fields, not the report

- **WHEN** SD-session admission is denied at a collection slice (capture active, a load job or a persistence
  write holding the session)
- **THEN** the fields that require SD reads are reported `unknown`
- **AND** the report is still produced and still names any waiting request
