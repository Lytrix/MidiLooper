# lazy-slot-hydration — restored-library inventory and loss attribution

## ADDED Requirements

### Requirement: Library state is reported as observed state, separate from an attributed cause

The system SHALL report, per `(track, slot)`, the **observed state** of the restored library from recorded
evidence, and SHALL keep that observation separate from any **attributed cause**. Observation SHALL NOT be
collapsed into a single-cause classification.

The observed state SHALL carry at least these independent fields:

- **payload** — absent, present-with-invalid-token, or present-and-verified;
- **enumeration** — whether the slot was offered to the boot/background restore path;
- **request** — none, queued, running, waiting (with its named reason), or terminal (with its named terminal),
  carrying the full request identity `(track, slot, requestGeneration, setEpoch)`;
- **content** — whether the slot holds committed content, with a content inventory;
- **selection** — whether the slot is required for initial playback.

An attributed cause, when recorded, SHALL be **nullable** and SHALL be one of: `empty/not applicable`,
`pending`, `catalog omission`, `restore refusal`, `intentional eviction`, `content loss`, `ambiguous`, or
`unknown`. A cause SHALL NOT be recorded without the evidence field(s) that support it and a stated precedence
rule.

The following SHALL hold:

- a slot with no payload and no outstanding request SHALL be reported `empty/not applicable` and SHALL NOT be
  reported as loss;
- when two or more causes are equally consistent with the evidence, the report SHALL record **all** consistent
  causes and the attribution `ambiguous` — it SHALL NOT choose one;
- a payload that fails validation *and* produced a refused restore SHALL record both facts rather than being
  forced into a single cause;
- a single snapshot SHALL NOT be reported as proof of a **historical** eviction or deletion; that SHALL require
  a prior inventory or an explicit event record, otherwise the attribution is `unknown`;
- a request that has not terminated SHALL be reported `pending`, not as a loss or a refusal.

The report SHALL record its own **completeness**: the observation window's start and end, whether the window
covered every slot required for initial playback, whether the background admission scan completed, and whether
every slot was observed within one consistent view or across turns.

#### Scenario: A slot that was never populated is not reported as lost

- **WHEN** a slot has no payload file and no outstanding restore request
- **THEN** its observed state records no payload and no request
- **AND** its attribution is `empty/not applicable`, not `catalog omission`, `restore refusal` or `content loss`

#### Scenario: A corrupt payload is not forced into one cause

- **WHEN** a slot's payload file is present but fails token or body validation
- **AND** the restore for that slot reached a `refused` terminal
- **THEN** the report records the validation failure and the refused terminal as separate facts
- **AND** the attribution records the consistent cause(s) with `ambiguous` where they cannot be separated

#### Scenario: A historical deletion is not claimed from one snapshot

- **WHEN** a slot's payload is absent and no earlier inventory or explicit event record exists
- **THEN** the attribution is `unknown`
- **AND** the report does not claim `intentional eviction` or `content loss` from the single snapshot

#### Scenario: An unfinished request is pending, not a loss

- **WHEN** a slot's restore request is queued, running, or waiting at the observation
- **THEN** the observed request state and its named wait are recorded
- **AND** the attribution is `pending`, and no terminal is asserted for that identity

#### Scenario: Two consistent causes are recorded, not resolved by preference

- **WHEN** the evidence is consistent with both `restore refusal` and `content loss`
- **THEN** both are recorded with their supporting fields
- **AND** the attribution is `ambiguous`

#### Scenario: An incomplete observation is marked incomplete

- **WHEN** the observation window ended before the background admission scan completed, or before every
  initial-playback slot was observed
- **THEN** the report states that the window is incomplete
- **AND** no count from that window is presented as the settled library state

### Requirement: The library inventory is obtainable on demand and is not gated on a settled system

The system SHALL provide a **bounded, on-demand** diagnostic snapshot of the library inventory that does NOT
depend on the settled census conditions (an empty restore queue, no live `SlotLoadSession`, and both restore
job instances inactive). The settled post-restore census checkpoint SHALL be retained unchanged for settled
memory measurements, and the on-demand snapshot SHALL NOT replace, relax or be reported as that measurement.

Collection SHALL be **incremental and bounded per invocation**: SD reads and committed-content traversal SHALL
be bounded and accounted for, and a snapshot over a large library SHALL NOT perform an unbounded traversal in
one turn.

Snapshot consistency SHALL be explicit: slots observed on different turns SHALL be marked as such, and a
snapshot assembled across turns SHALL NOT be presented as a single consistent instant.

#### Scenario: A permanently waiting request still yields a diagnostic

- **WHEN** a restore request is stuck waiting and the settled census checkpoint therefore never fires
- **THEN** the on-demand diagnostic snapshot is still obtainable
- **AND** it names the waiting request's state, its named wait, and the slot's observed payload and content state

#### Scenario: Collection over a large library is bounded

- **WHEN** the diagnostic is requested for a library larger than one turn's budget
- **THEN** it collects incrementally within the declared per-invocation bound
- **AND** the partial result is marked as covering only the slots it observed, with the observation turns
  recorded
