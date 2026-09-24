# lazy-slot-hydration — restored-library inventory and loss attribution

## ADDED Requirements

### Requirement: A slot that is not COMMITTED after boot is attributable from one recorded inventory

The system SHALL record and report, for the loaded current set, a **per-slot library inventory** that is
sufficient to attribute any slot which is not `COMMITTED` after boot, without a bisect. The inventory SHALL
separate, for each `(track, slot)`:

- the **SD payload** — presence of the slot's payload file and its save-token validity;
- the **boot outcome** — whether the slot was enumerated into the boot restore path and what the restore
  request reached (`queued` / `running` / `waiting` / `terminal`, with the named wait or terminal);
- the **committed content** — whether the slot holds committed content, and an inventory of it;
- the **selection** — whether the slot is part of the boot playback set.

A slot that is not `COMMITTED` after boot SHALL be attributable to exactly one of the following named classes
from that evidence, and the system SHALL NOT report it as lost without distinguishing them:

- **catalog omission** — the payload is present and verifies, but the slot was not enumerated;
- **restore refusal** — the payload was enumerated, but the request did not reach `committed` (a named wait, a
  terminal other than `committed`, or no request for that identity);
- **intentional eviction** — a deliberate, named policy dropped or reused the slot;
- **content loss** — the payload is present but carries no committed content, or its body fails validation.

The inventory SHALL distinguish the **boot playback set** (the slots committed for boot playback) from the
**payload set** (the slots with a valid SD payload), because the two are different quantities and a
non-growing committed count SHALL NOT by itself be read as payload loss.

#### Scenario: A verified payload that does not come back committed is attributed

- **WHEN** a slot's SD payload file is present and its save token verifies
- **AND** the slot is not `COMMITTED` after boot
- **THEN** the recorded inventory names the class (`catalog omission`, `restore refusal`, `intentional
  eviction`, or `content loss`) that the enumerated evidence supports
- **AND** it cites the evidence for that class (enumeration entry, request state/terminal, eviction decision,
  or body/content state)

#### Scenario: A flat or shrinking committed count is not reported as payload loss

- **WHEN** the committed boot-playback count does not grow, or shrinks, across boots
- **THEN** the report states the payload set and the committed boot-playback set **separately**
- **AND** a slot whose payload is present and verifies is not reported as payload loss

#### Scenario: Intentional eviction is reported as a decision, not inferred from the symptom

- **WHEN** a slot is absent from the committed set because a bounded policy dropped or reused it
- **THEN** the report names that policy and the slot it acted on
- **AND** the absence is not reported as a defect or as content loss

#### Scenario: The attribution is available from the session capture alone

- **WHEN** an operator inspects one session-capture artifact for a boot whose committed set differs from the
  expected one
- **THEN** the per-slot inventory and the class per non-committed slot are present in that artifact
- **AND** no additional bisect, re-run, or destructive SD action is required to obtain them

#### Scenario: Absent or invalid payloads are distinguished from refused restores

- **WHEN** a slot has no payload file, or a payload whose token does not verify
- **THEN** the report records the slot as having no valid payload
- **AND** it does not record a `refused` or `unnecessary` restore terminal as the explanation for the absence
