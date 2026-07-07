# Decision log

Persistent record of **accepted architectural and implementation decisions**. Not a changelog, roadmap, or meeting notes.

**Agents:** run [DECISION_REVIEW.md](templates/DECISION_REVIEW.md) before implementation; append new decisions at [session closeout](templates/SESSION_CLOSEOUT.md).

| Rule | Meaning |
|------|---------|
| Append only | Never delete; supersede with a new entry (`Status: Superseded`) |
| Search before design | `rg` this file + active OpenSpec + relevant `docs/plans/` |
| Challenge via reassessment | Redesign is allowed through [ARCHITECTURE_REASSESSMENT.md](ARCHITECTURE_REASSESSMENT.md) + new log entry |

## Index

| ID | Date | Topic | Status |
|----|------|-------|--------|
| [DEC-016](#dec-016-runtime-architecture-four-layer-model) | 2026-07-07 | Runtime architecture four-layer model | Accepted |
| [DEC-015](#dec-015-interval-projection-stage-1-stage-2-split) | 2026-07-05 | IntervalProjection module + Stage 1/2 split | Accepted |
| [DEC-014](#dec-014-dual-normalization-boundaries-micro-vs-macro) | 2026-07-03 | Dual normalize micro/macro | Accepted |
| [DEC-013](#dec-013-linear-loop-tick-validate-vs-normalize) | 2026-07-03 | Linear loop tick validate vs normalize | Accepted |
| [DEC-012](#dec-012-storagesession-persistence-state-model) | 2026-06-29 | StorageSession persistence state | Accepted |
| [DEC-011](#dec-011-bias-toward-progress) | 2026-06-29 | Bias toward progress | Accepted |
| [DEC-010](#dec-010-ownership-evolution-protocol) | 2026-06-29 | Ownership evolution protocol | Accepted |
| [DEC-009](#dec-009-runtime-state-vs-roadmap-split) | 2026-06-29 | Runtime state vs roadmap | Accepted |
| [DEC-008](#dec-008-authority-conflict-resolution) | 2026-06-29 | Authority conflict resolution | Accepted |
| [DEC-007](#dec-007-historical-decision-reuse-enforcement) | 2026-06-29 | Historical decision reuse | Accepted |
| [DEC-006](#dec-006-agent-context-harness-vs-chat-history) | 2026-06-29 | Agent context / chat history | Accepted |
| [DEC-005](#dec-005-gpio-base-module-vs-droid-only-actions) | 2026-06-29 | GPIO base vs DROID actions | Accepted |
| [DEC-004](#dec-004-recordoverdub-stop-validation) | 2026-06-29 | Record/overdub stop validation | Accepted |
| [DEC-003](#dec-003-d13-jam-recording-ordering) | 2026-06-29 | D13 jam recording order | Accepted |
| [DEC-002](#dec-002-set-revision-vs-flat-savedset-snapshot) | 2026-06-29 | Set revision vs flat SavedSet | Accepted |
| [DEC-001](#dec-001-loadsave-overlay-confirm-control) | 2026-06-29 | Load/save overlay confirm | Accepted |

---

<!-- Append new entries below (newest first). Next ID: DEC-017 -->

## DEC-016 — Runtime architecture four-layer model

**Date:** 2026-07-07  
**Owner:** `docs/00-authority/Architecture/` (conceptual); brownfield build owners per layer (`Loop`, `Track`, `EditManager`, `DisplayManager`, `IntervalProjection`)  
**Status:** Accepted

**Context:** The 64-bar PLAYING-window regression investigation showed display, playback, and LED paths each deciding when to rebuild timeline data. UIP (DEC-015) unified interval math but not representation ownership, revision chains, or consumer scheduling. Investigation detail must not live in permanent architecture docs.

**Decision:**

1. **Four layers** (orthogonal): **Capture Storage** → **Derived Representations** → **Interval Projection** → **Runtime Request** (representation × `TickInterval` → consumer result).
2. **Derived representations** are not consumer-owned caches. Each documents: owner, dependencies, revision, invalidation, build policy, consumers. Prefer the term *derived representation* in new docs; reserve *cache* for true memoization (e.g. COW materialized flat).
3. **Intervals are consumer-agnostic** — one `TickInterval` (e.g. bars 8–24); playback, display, edit, and LED interpret the same range. Interval projection remains in `IntervalProjection` (DEC-015); it does not own storage or representations.
4. **Revision chain:** storage mutation → event representation → downstream display/playback representations. Consumers validate staleness; they do not call peer rebuild APIs on hot paths (e.g. `ensureVisualCacheBuilt` from `MidiLedManager` during PLAYING).
5. **Scheduling:** document **responsibilities** (owner, policy, defer vs immediate). A dedicated scheduler is optional implementation — not an architectural requirement.
6. **NOTE_EDIT exception:** `NoteEditSession.store` is a live overlay on passes during edit (Tier-2 playback audition via `sessionMidiEvents()` / `sessionPreviewRevision_`) — not folded into a single loop event representation.
7. **Doc split:** permanent model in `docs/00-authority/Architecture/`; regression bisect/evidence in `docs/plans/*_bugfix.md`; concrete patches in `docs/plans/*_refinement.md`.

**Consequences:**

- Agents load [RuntimeArchitecture.md](00-authority/Architecture/RuntimeArchitecture.md) for display/playback/LED read paths before changing rebuild behavior.
- PLAYING/overdub hot-path work defers full display representation rebuild (see [overdub_start_playing_window_hot_path_refinement.md](plans/overdub_start_playing_window_hot_path_refinement.md)).
- Future features (clip launch, multi-window, LTS) add a representation, an interval source, or a consumer — not parallel cache systems.
- Code rename (`VisualCache` → representation naming) is deferred; docs use architecture terms with brownfield mapping table in [DerivedViews.md](00-authority/Architecture/DerivedViews.md).

**References:** DEC-015, [unified-interval-projection/design.md](../openspec/changes/unified-interval-projection/design.md), [overdub_start_64bar_playing_window_regression_bugfix.md](plans/overdub_start_64bar_playing_window_regression_bugfix.md).

---

## DEC-015 — IntervalProjection Stage 1 / Stage 2 split

**Date:** 2026-07-05  
**Owner:** `IntervalProjection` (`include/Utils/IntervalProjection.h`, `src/Utils/IntervalProjection.cpp`)  
**Status:** Accepted

**Context:** Display, playback, and edit each duplicated loop-wrap math. `edit-session-action-geometry` would add a third path (`normalizeWrapToLinear`).

**Decision:**

1. **Module home:** `IntervalProjection` in `Utils/` — single engine for all wrap/linearization consumers.
2. **Stage 1 (`generateEquivalentIntervals`):** Pure math — bounded ±k·`loopLength` shifts from canonical `TickInterval` spans; k bounds derived from `ProjectionContext.window` intersection; no `ProjectionType` policy.
3. **Stage 2 (`selectProjectedInterval` / `selectProjectedIntervalsForDisplay`):** Consumer selection only (Playback / Display / Edit / reserved Timeline).
4. **Phase helpers** (`tickPhaseInLoop`, `noteRelativeTick`, `noteStorageTick`, projection-cycle helpers) centralized in `IntervalProjection`; `TickPhase.h` and `SelectNavigation` thin-wrap for brownfield call sites.
5. **`window` is `TickInterval` input frame (D23)** — never `ProjectedNoteInterval`; projection preserves `noteId` across k copies (D8).

**Consequences:** Phases 2–5 migrate consumers to supply `ProjectionContext` only; overlap pipeline blocked until Phase 5 HITL. Native gate: `test_interval_projection`.

**References:** `openspec/changes/unified-interval-projection/`, DEC-013 linear storage.

---

## DEC-014 — Dual normalization boundaries (micro vs macro)

**Date:** 2026-07-03  
**Owner:** `LoopTickNormalize`, NOTE_EDIT commit path  
**Status:** Accepted

### Problem

Single `normalizeWindow` at every boundary blurred live-interaction consistency with persistent canonical commit.

### Decision

- **Micro:** `normalizeWindow` on **edit closure set** at `publishDependentFaderLatch` — local geometric consistency for faders/projection; not sole persistent canonical authority.
- **Macro:** `normalizeAll` at `commitAllPendingNoteEditActions` — full-store canonical invariants, undo snapshots, pass readers. MUST NOT be skipped.
- **Closure set:** seed modified `NoteId`s → paired on/off, overlap participants, wrap interactors; no UI window/selection as scope.
- **Playback during edit:** Tier 2 `sessionMidiEvents()` — verification only, no merge overlay.
- **Set window:** drives F1/F2 range; full-loop window → wrap at fader extremes; partial-window slide deferred.

### Consequences

- Phase 2 wires closure-set computation + dual hooks before HITL 152335.
- OpenSpec: `linear-loop-tick-storage`, `note-edit-modification-session`, `note-edit-fader-feedback`.

---


## DEC-013 — Linear loop tick validate vs normalize

**Date:** 2026-07-03  
**Owner:** `LoopTickNormalize`, `LoopEventValidation` (`include/Utils/`)  
**Status:** Accepted

### Problem

Loop MIDI storage mixed modulo ticks, wrap-pair geometry, and projection — causing edit move cutoff and loop-stretch display inflation.

### Decision

- **Invariants** — pure boolean predicates in `LoopEventValidation`; MUST NOT mutate storage or call normalize.
- **Conversion rules** — pure transforms in `LoopTickNormalize`; ONLY place legacy wrap-pair / synth-off shapes become canonical linear spans.
- **Normalize timing** — boundary-based only (not mid-pipeline, not on SD load). See DEC-014 for micro/macro split.

### Consequences

- `validateAndCleanupMidiEvents` logs canonical failures and performs orphan removal only (no synth loop-end insert on idle).
- OpenSpec: `openspec/changes/linear-loop-tick-storage/`.

---

## DEC-012 — StorageSession persistence state model

**Date:** 2026-06-29  
**Owner:** `StorageManager` (persistence)  
**Status:** Accepted

### Problem

Revision persistence and overlay work (~30 commits) left ~80 anonymous statics in `StorageManager.cpp`. Overlay coordination used imperative `PersistencePhase` assignments (8 sites) alongside bool FSM flags. Sprint naming (**pipeline**, **prompt**, **saveThenLoad**, **LoadRequestGate**) reads as prose and blurs the display/backend split.

### Decision

1. Introduce **`StorageSession`** struct — RAM aggregate of storage **jobs**, owned exclusively by **`StorageManager`** (DEC-008).
2. Jobs: `currentWorkspaceSave`, `revisionCommit`, `revisionLoad`, `setBrowserNavigation`, `bootRecovery`.
3. Revision load lifecycle: **requested → held** (`heldForWorkspaceDirty`) **→ dispatched** (`pending` / `inProgress` / `stage`). Flag **`loadAfterRevisionCommit`** for commit-then-load path. Remove **`revisionLoadPipelineActive`** when derived phase lands (Tier 2).
4. APIs use **request / confirm / cancel / dispatch**; rename away from `*DirtyPrompt*` / `*Pipeline*` / `*Staged*` on backend surfaces.
5. Drop **`LoadRequestGate`**; use `RevisionLoadPolicy::shouldHoldRevisionLoadRequest(bool workspaceDirty)`.
6. **Derived** coordination phase (Tier 2): `AwaitingRevisionCommit`, `RevisionLoadActive`, `RevisionCommitActive` — not stored on session.
7. Display overlay names stay on **`DisplayManager`**; no `StorageOverlaySession` or `*Intent*` struct names.
8. Implementation tiers: 0 snapshot/tests → 2 derived phase → 1 struct migration → 3 TU split before `transport.bin` / `global.bin`.

### Rationale

Matches **EditSession + EditManager** pattern (session struct + coordinator) without a new Manager. Reuses established **request**, **pending**, **inProgress**, **stage**, **job**, **deferred** vocabulary. Keeps front/back naming split.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| `SaveSessionManager` / `StorageHandler` | DEC-008; Handler suffix is event routing, not FSM ownership |
| `SaveSession` / `PersistenceSession` namespace as owner | Ownership ambiguity; collides with rejected Manager name or new top-level noun |
| `StorageOverlaySession` | **Overlay** implies display scope; backend is load/save jobs + navigation |
| `*Intent*` subgroup | New metaphor; not in repo suffix vocabulary |
| `LoadRequestGate` / **Gate** suffix | Only used in 3.7 sprint; queue vocabulary (**held**, **requested**) already fits deferred save |
| Keep imperative `PersistencePhase` only | Desync risk across 8 assignment sites; tested derivation is safer |
| Move drill navigation to `DisplayManager` now | Would be ownership transfer; defer — `StorageManager` already mutates `NavigationState` |

### Affected modules

`StorageManager`, `StorageSession` (new), `RevisionLoadPolicy`, `SetBrowserOverlayPolicy`, `DisplayManager` (getters), `MidiButtonActions`, native tests

### Constraints created

- No parallel persistence owner (DEC-008).
- Tier 0–2 must not reshape deferred/revision FSM **step logic** during active overlay tasks except naming/migration agreed in handoff.
- HITL/serial wire strings may keep legacy names until explicitly migrated.

### Related OpenSpec

`set-revision-persistence`, `load-save-overlay-display-regression`

### Migration notes

Handoff: [`docs/plans/storage_session_state_refactor_handoff.md`](plans/storage_session_state_refactor_handoff.md). Builder starts Tier 0.

---

## DEC-011 — Bias toward progress

**Date:** 2026-06-29  
**Owner:** Process (`ARCHITECTURE_REASSESSMENT.md`, agent workflow)  
**Status:** Accepted

### Problem

Governance risked becoming a bottleneck — agents stopped for full preflight/reassessment on normal extension work (new methods, display updates, bug fixes).

### Decision

**Default: continue implementation.** Decision ladder: extend owner → reuse extension point → follow DECISION_LOG → reassess only on [formal triggers](../ARCHITECTURE_REASSESSMENT.md#formal-triggers) → else implement. Max one reassessment per session; if rejected, return to implementation. Lightweight inline preflight for non-trigger work; full PREFLIGHT only when triggered.

### Rationale

Governance exists to detect **structural** change, not to approve every edit.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Full preflight always | Too heavy; slows bug fixes and display work |
| No triggers / trust agents | Loses structural guardrails |
| Stop on “feels unclear” | Indefinite planning loops |

### Affected modules

Agent workflow, PREFLIGHT modes, ARCHITECTURE_REASSESSMENT trigger list

### Constraints created

>3 files alone is not a trigger — must cross ownership. Explicit “not triggers” list documented.

### Related OpenSpec

N/A

### Migration notes

Supersedes implicit “always stop for 3+ files” interpretation.

---

## DEC-010 — Ownership evolution protocol

**Date:** 2026-06-29  
**Owner:** Process (`docs/00-authority/ARCHITECTURE_RULES.md`)  
**Status:** Accepted

### Problem

Strict ownership rules risked freezing architecture. Agents responded with permanent adapters, duplicated state, and shadow Managers instead of explicit transfer.

### Decision

**Ownership transfer protocol** in ARCHITECTURE_RULES: reassessment + migration plan + documented compat layer + removal schedule + approval + DECISION_LOG. Temporary delegation/adapters allowed; parallel permanent ownership forbidden.

### Rationale

Architecture must evolve without drift or workaround layers that never leave.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Never allow ownership change | Forces shadow owners and duplicated fields |
| Allow transfer without removal schedule | “Temporary” compat becomes permanent |
| Adapters without reassessment | Same as shadow ownership |

### Affected modules

All modules; template `OWNERSHIP_TRANSFER.md`; reviewer/builder/architect roles

### Constraints created

Compat layers require creation date, removal condition, maximum lifetime. Search owner + extension points before transfer.

### Related OpenSpec

N/A

### Migration notes

Example path: surface-agnostic Actions (DEC-005) should use this protocol when moving input ownership off `MidiButtonActions` only.

---

## DEC-009 — Runtime state vs roadmap split

**Date:** 2026-06-29  
**Owner:** Process (`docs/runtime/`)  
**Status:** Accepted

### Problem

`PROJECT_STATE.md` mixed active work, future milestones, and next actions. Agents could implement roadmap items (D13, JamRecorder) thinking they were current scope.

### Decision

Split `docs/runtime/` into **PROJECT_STATE** (execution context), **CURRENT_WORK** (now / not now / completion), **ROADMAP** (future only, never implementation authority). Agents load PROJECT_STATE + CURRENT_WORK before planning; ROADMAP optional.

### Rationale

Operational state must be separable from sequencing information to prevent speculative scaffolding.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Single PROJECT_STATE file with sections | Agents still skimmed roadmap sections as tasks |
| ROADMAP in DELIVERABLE_TRACKING only | Session start does not load that file by default |

### Affected modules

Agent workflow, session closeout, PREFLIGHT loaded docs

### Constraints created

No future milestones in PROJECT_STATE. Implementation must appear in CURRENT_WORK § Now implementing.

### Related OpenSpec

N/A

### Migration notes

Post-M7 sequence moved from PROJECT_STATE to ROADMAP.md.

---

## DEC-008 — Authority conflict resolution

**Date:** 2026-06-29  
**Owner:** Process (`docs/00-authority/README.md`)  
**Status:** Accepted

### Problem

Authority ordering defined precedence but not conflict handling. Example: OpenSpec proposes `SaveSessionManager` while ARCHITECTURE_RULES assigns persistence to `StorageManager` and discourages new Managers — agents had no deterministic resolution.

### Decision

Explicit conflict matrix in `00-authority/README.md`. OpenSpec owns **behavior**; ARCHITECTURE_RULES owns **structure**. On conflict: **STOP**, architecture reassessment, user approval, update docs, then code. Prefer extending existing owner over new Manager.

### Rationale

Prevents architecture drift, endless replanning, and partial “stub” implementations that bypass ownership.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| OpenSpec always wins | Would redefine architecture through tasks without review |
| Architecture always wins | Would make specs unimplementable without reassessment path |
| Implement and fix later | Partial implementations become production debt |

### Affected modules

Agent workflow, OpenSpec proposals, PREFLIGHT authority conflict check

### Constraints created

No code on authority conflict until reassessment approved. No stub Managers to unblock specs.

### Related OpenSpec

N/A — applies to all changes

### Migration notes

Worked example: `SaveSessionManager` → extend `StorageManager` or approved new owner + DECISION_LOG.

---

## DEC-007 — Historical decision reuse enforcement

**Date:** 2026-06-29  
**Owner:** Process (`docs/DECISION_LOG.md`, agent workflow)  
**Status:** Accepted

### Problem

Agents recreated rejected abstractions, helpers, and ownership patterns because historical decisions were not mandatory reading before implementation.

### Decision

Mandatory **DECISION_REVIEW** + PREFLIGHT § Similar Historical Decisions before firmware edits. Structured **DECISION_LOG** at `docs/DECISION_LOG.md`. Reviewer rejects duplicate abstractions and ignored DEC-### entries.

### Rationale

Planning protected architecture but did not force explicit reuse/challenge of prior conclusions as project history grows.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Rely on agent memory across chats | Unreliable; tabs close |
| Changelog-style decision doc | Conflates shipped code with architectural choices |
| Block all redesign | Legitimate overturn via reassessment + supersede entry required instead |

### Affected modules

Documentation and `.cursor/rules/Agent-Context-Workflow.mdc` only

### Constraints created

New architecture decisions MUST be logged. Supersede — never delete. Challenge via ARCHITECTURE_REASSESSMENT.

### Related OpenSpec

N/A

### Migration notes

`docs/runtime/DECISION_LOG.md` redirects here. Entries DEC-001–DEC-006 migrated to structured format.

---

## DEC-001 — Load/save overlay confirm control

**Date:** 2026-06-29  
**Owner:** `SetBrowserOverlayPolicy`, `MidiButtonActions`, `GpioButtonManager`  
**Status:** Accepted

### Problem

Load/save overlay needs a single confirm gesture without conflicting with record, scroll, or note-edit bindings.

### Decision

Overlay confirm = **Edit short (note 38)** or **encoder short** only.

### Rationale

Scroll uses REC/PLAY and track buttons; NOTELEN is suppressed or bound to note-edit semantics elsewhere. Edit short and encoder are already the user-facing “commit row” affordance in overlay docs.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| NOTELEN short for confirm | Conflicts with note start/end and delete/create in edit; overlay suppresses NOTELEN for other roles |
| REC/PLAY short for confirm | Used for list scroll down in overlay |

### Affected modules

`SetBrowserOverlayPolicy`, `MidiButtonActions`, `GpioButtonManager`, README overlay table

### Constraints created

No new confirm binding without surface-agnostic Actions refactor for base module.

### Related OpenSpec

`set-revision-persistence` (set-browser-overlay)

### Migration notes

Reopen when base-module-only confirm needed without encoder — extend Actions layer first, do not add a third confirm note without reassessment.

---

## DEC-002 — Set revision vs flat SavedSet snapshot

**Date:** 2026-06-29  
**Owner:** `StorageManager`  
**Status:** Accepted

### Problem

Need durable Sets on SD without disrupting live **Current** workspace during performance.

### Decision

**Current** = mutable auto-saved workspace. **Save** = append immutable **Set revision** (REVPK) via deferred FSM. Current unchanged after Save.

### Rationale

Performers keep playing from Current; revisions are catalog snapshots for recall. Flat replace-on-save breaks flow.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Flat SavedSet as primary model | Superseded by revision catalog (`currentset-savedset-storage-layout` parked) |
| Save clears Current | Violates “music keeps running” intent |

### Affected modules

`StorageManager`, `SetRevisionCatalog`, `SavedSetCatalog`, overlay browser

### Constraints created

No silent revert to flat SavedSet-only layout. Schema changes require OpenSpec + migration notes.

### Related OpenSpec

`set-revision-persistence`, `workspace-session-persistence` (SUPERSEDED.md for old M3/M4)

### Migration notes

Parked: `currentset-savedset-storage-layout`. Reopen only via new OpenSpec change if revision model fails field validation.

---

## DEC-003 — D13 jam recording ordering

**Date:** 2026-06-29  
**Owner:** Timeline / Phase 3 (not yet implemented)  
**Status:** Accepted

### Problem

Phase 3 jam capture was scoped before timeline prerequisites existed, causing architectural drift.

### Decision

Ship order: **JamRecorder (JamAction) → M10 → D13 arrangement capture last**. Pool-budget and M8 edit precede jam capture.

### Rationale

Capture depends on stable passes[], undo, and slot infrastructure already shipped; D13 OpenSpec started too early.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Early `jam-recording` OpenSpec | Parked — prerequisites missing |
| Implement capture from `phase-3-multi-loop.md` alone | Slots yes, capture no; R1–R5 unresolved |

### Affected modules

Future: `Track`, `TrackManager`, jam state fields (read-only until JamRecorder)

### Constraints created

No D13 wiring in production paths until JamRecorder + M10 scoped. See DELIVERY_RULES hard guards.

### Related OpenSpec

Parked: `archive/20260617-parked-jam-recording-d13/`

### Migration notes

Reopen when `jam-recorder` change proposed and M10 design exists.

---

## DEC-004 — Record/overdub stop validation

**Date:** 2026-06-29  
**Owner:** `Loop`, `Track`, `LoopStopFinalize`  
**Status:** Accepted

### Problem

Long loops cannot afford full-loop validate or SD I/O on record/overdub stop.

### Decision

Stop path = `finalizeLoopAtStop` + wrap window only. Full validate via deferred idle maintenance. Persistence via `requestDeferredSaveState`.

### Rationale

Documented in LOOP_MIDI_STORAGE guide and archived pool-budget / memory-headroom specs; HITL and native tests depend on bounded stop path.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Full `validateAndCleanupMidiEvents` on stop | Latency and memory on long loops |
| SD write on stop hot path | Violates deferred save architecture |

### Affected modules

`Loop.cpp`, `Track.cpp`, `StorageManager`, `main.cpp` idle drain

### Constraints created

Hot path: no full flatten/validate on stop. No new stop-side persistence without reassessment.

### Related OpenSpec

`openspec/specs/timeline-epochs/`, `long-record-memory-headroom/`, `storage-loop-io/`

### Migration notes

Normative — challenge only via architecture reassessment and spec amendment.

---

## DEC-005 — GPIO base module vs DROID-only actions

**Date:** 2026-06-29  
**Owner:** Input / `MidiButtonActions` (future: shared Actions)  
**Status:** Accepted

### Problem

DROID development left `ButtonManager` dormant while actions live only in MIDI paths.

### Decision

DROID is extension only. Encoder + 4 GPIO must remain capable in principle. Revive GPIO via **shared Actions layer**, not parallel implementations.

### Rationale

PROJECT_INTENT decisions 1–4; base module is the product hypothesis.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| DROID-only core workflow | Violates intent litmus “base module alone” |
| Wire `ButtonManager` without shared Actions | Duplicates `MidiButtonActions` |

### Affected modules

`ButtonManager` (dormant), `MidiButtonActions`, `GpioButtonManager`

### Constraints created

No new MIDI-only action paths for core record/undo/nav. New input surfaces must map to existing Actions.

### Related OpenSpec

`note-edit-session-undo-gpio` (archived GPIO geometry)

### Migration notes

Reopen when surface-agnostic Actions refactor is explicitly scheduled.

---

## DEC-006 — Agent context harness vs chat history

**Date:** 2026-06-29  
**Owner:** Process (`docs/00-authority/`, `docs/runtime/`)  
**Status:** Accepted

### Problem

Design exclusions and rejected alternatives were lost when chat tabs closed, causing repeated debates.

### Decision

Durable memory in `PROJECT_STATE.md`, this `DECISION_LOG.md`, OpenSpec, and mandatory historical review before implementation.

### Rationale

Chat threads are not searchable and agents do not load them by default.

### Alternatives considered

| Alternative | Rejected because |
|-------------|------------------|
| Rely on open chat tabs | Not portable across sessions |
| Full transcript archive in docs | Noise; extract decisions only |

### Affected modules

Documentation and agent workflow only (no firmware)

### Constraints created

New architecture decisions MUST be recorded here. Session closeout when alternatives discussed.

### Related OpenSpec

N/A

### Migration notes

Supersedes informal “keep the tab open” workflow.

---
