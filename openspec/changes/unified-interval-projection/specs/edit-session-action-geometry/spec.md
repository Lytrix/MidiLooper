## MODIFIED Requirements

### Requirement: EditSessionAction geometry pipeline

Live geometry edits (`EditSessionType::Note` first; Loop and ControlChange later) SHALL use a declarative pipeline:

1. **Transaction baseline** — immutable **`baselineMap`** per **edit driver**; full loop in this change
2. **Edited geometry** — **`EditorSelection`** plus one **linear `NoteBaseline` causing span** per selected note this tick (`focus.last` for **`primaryNote`**; see design § Edited geometry)
3. **Edit projection** — **`projectEditIntervalsForAnalysis`** (Edit **`ProjectionContext`**); post-projection intervals before analysis; replaces **`normalizeWrapToLinear`**
4. **Edit session geometry orchestrator** — determine **changed causing notes** and **eligible overlap pairs**; MUST NOT delegate selection or latch logic to analyze
5. **`analyzeEditSessionInteractions`** — pure geometry facts for supplied causing notes; **positive interaction graph** only (omit non-overlapping pairs; no **None** enum)
6. **`groupEditSessionInteractionsByTarget`** — group interactions **per target `NoteId`**; ephemeral; MUST NOT persist across ticks
7. **`resolveConstrainedGeometry`** — for each **constrained geometry target note** (see Resolver scope): baseline + per-target interactions → **`ConstrainedNoteGeometry`**; MUST NOT know **`EditSessionActionType`**
8. **`buildEditSessionActions`** — **edit session action builder**: inputs **`constrainedGeometry`**, **`editedGeometry`**, **`transactionBaseline`**, **`liveStore`**; compute minimal ordered **`EditSessionActions`**; **omit actions that would not change live store**; MUST NOT mutate live store; MUST NOT inspect **`EditSessionInteraction`** directly
9. **`applyEditSessionActions`** — **edit session action apply**; the ONLY subsystem that mutates **live store** for live geometry (includes **boundary split** sub-step, D10); **`normalizeWindow`** is separate (edit closure only)
10. **`normalizeWindow`** on edit closure — geometry tick path boundary (existing DEC-014)
11. **Read-only projection** — MUST NOT write back to storage

The system MUST NOT use a restore-first prelude or persistent overlap scratch as the primary model. The system MUST NOT maintain a persistent **`ConstraintRegistry`** between geometry ticks. The system MUST NOT reintroduce **`ResolutionPolicy`** as a separate pipeline stage.

**Dependency:** Steps 1–11 require **`unified-interval-projection`** Edit + Display + Playback migration (Phases 1–5) before firmware implementation begins.

#### Scenario: Geometry parity classify fixtures

- **WHEN** native parity fixtures run (internal swallow, head-on overlap, tail overlap, external cover, no overlap)
- **THEN** **`InteractionType`** matches the locked classify order after **Edit projection**

#### Scenario: Wrapped target before analyze

- **GIVEN** target **A** is a wrapped display note
- **WHEN** analyze runs
- **THEN** **Edit projection** has produced linear working **`baselineSpan`** for **A** before **`InteractionType`** is classified
- **AND** **`normalizeWrapToLinear`** is not called

## REMOVED Requirements

### Requirement: EditSessionInteraction wraps attribute

**Reason:** Edit projection normalizes working coordinates; shorten math uses projected `causingSpan` and `baselineSpan` only.

**Migration:** Remove `wraps` from `EditSessionInteraction`; update **`computeShortenedEndTick`** to use projected spans. Reintroduce only if Phase 2 native fixtures prove a residual edge case (design D6 fallback).

## MODIFIED Requirements

### Requirement: EditSessionInteraction structure

**`EditSessionInteraction`** SHALL carry **`InteractionType`**, **`causingNoteId`**, **`targetNoteId`**, **`baselineSpan`**, and **`causingSpan`** in post-projection working coordinates. It SHALL NOT carry **`wraps`**, **`causingSelected`**, **`targetSelected`**, or **`InteractionType::None`**.

#### Scenario: No redundant selection or wrap fields

- **WHEN** **`EditSessionInteraction`** is defined after UIP + overlap sync
- **THEN** it does not include **`wraps`**
- **AND** selection membership is derived at orchestrator time only
