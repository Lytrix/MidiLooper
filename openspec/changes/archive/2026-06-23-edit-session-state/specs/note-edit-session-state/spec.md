## MODIFIED Requirements

### Requirement: Edit session and edit pass type enums

Live edit RAM SHALL use **`EditSession`** with **`EditSessionType`**: **Loop**, **Note**,
**ControlChange**.

Stored **editPass** rows SHALL use **`EditPassType`**: **Note**, **ControlChange**, **Audio**
(renamed from **`EditSessionType`** on **EditPass**).

**Loop** SHALL appear only on **`EditSessionType`**, not **`EditPassType`**.

#### Scenario: Note session maps to note pass on commit

- **WHEN** **`EditSession::sessionType`** is **Note** and **saveNoteEditPass** commits
- **THEN** appended rows have **`passType = Note`**

#### Scenario: Loop session does not append edit pass

- **WHEN** **`sessionType`** is **Loop**
- **THEN** **saveNoteEditPass** is not invoked for loop chrome alone

#### Scenario: Unified session owner

- **WHEN** note edit opens
- **THEN** **`sessionType`**, **`store`**, and note branch state live on one **`EditSession`**
