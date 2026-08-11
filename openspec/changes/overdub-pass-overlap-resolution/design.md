# OpenSpec Refinement — Overdub Source View, Pass Delta, and Overlap Resolution

## Decision

The overdub overlap model is now refined around the existing **pass + edit-pass architecture**.

The important distinction is:

* an overdub session may span multiple loop wraps;
* every newly inserted note must be evaluated during the session;
* evaluation is against **one stable canonical source view** for that overdub session;
* the source material is immutable from the perspective of the overdub operation;
* the resulting `overdubPass` contains the **complete delta** required to transform that source state into the overdub result;
* that delta includes both newly added notes and changes to existing material such as **shorten** and **remove**;
* the overdub session currently produces one `overdubPass` / one undo unit when stopped.

This is closer to the existing `editPass` model than to a generic "deduplication cache".

---

# 1. Source View Lifetime

## Decision: establish the source view at overdub start

At:

```text
Loop::beginCapture(Overdub)
TrackUndo::beginOverdubSession
```

the system establishes the canonical materialized source state against which the overdub session will be resolved.

Preferred semantic name:

**`overdubSourceView`**

Do **not** call this a "freeze".

The loop is not frozen and committed geometry is not generally frozen by `beginCapture(Overdub)` today.

The source view is a **stable semantic view for the lifetime of one overdub session**.

```text
begin overdub
      │
      ▼
establish overdubSourceView
      │
      │ stable for entire session
      ▼
┌─────────────────────────────┐
│ overdub session              │
│                             │
│ new note → resolve          │
│ new note → resolve          │
│ loop wrap                   │
│ new note → resolve          │
│ loop wrap                   │
│ new note → resolve          │
└─────────────────────────────┘
      │
      ▼
stop overdub
      │
      ▼
commit overdubPass
```

### Stability invariant

Every note inserted during the same overdub session must be evaluated against the same semantic source state.

The result must not depend on unrelated materialization changes occurring later in the session.

Therefore:

> **The overdub source view is established once for an overdub session and remains semantically stable until that session ends.**

This does **not** require a large standalone copy of all loop events. The implementation may use existing materialized/chunk/window infrastructure.

---

# 2. What the Source View Represents

## Decision: define the semantic view, not the physical data structure

The OpenSpec must require a **materialize-aware canonical note view**.

It must include the effective result of the existing pass history relevant to the current canonical loop state, including `editPasses`.

It must therefore **not** simply use a bare:

```text
CommittedEventRange
```

when edits affect the canonical result.

Conceptually:

```text
canonical pass history
        │
        ├── recordPass
        ├── applicable overdubPasses
        └── editPasses
              │
              ▼
      canonical materialized state
              │
              ▼
       overdubSourceView
```

The important point is that `overdubSourceView` represents **the canonical musical state at the overdub-session boundary**.

It is a stable view of that state, not a new lifecycle state for the loop.

### Physical representation remains open

The implementation may use:

**A. Materialized `MidiEvent` representation**

* closest to the current materialization path;
* can reuse existing event materialization;
* spans can be reconstructed.

**B. Reconstructed note-span representation**

* directly matches `NoteGeometryResolver`;
* efficient for overlap decisions;
* avoids repeated event-to-span reconstruction.

**C. Chunk/window-backed representation with span access**

* potentially best fit for bounded memory;
* can preserve immutable source access;
* may avoid copying the entire loop.

**D. Combination**

Possible if profiling demonstrates a need.

The OpenSpec should **not prescribe one of these representations**.

The requirement is:

> The source view must provide materialize-aware immutable note geometry with efficient access to the candidate region required for overlap resolution.

Prefer existing chunk/window infrastructure before introducing a new global index or cache.

---

# 3. Ownership

## Decision: Loop owns canonical source access; Track owns overdub lifecycle

There is a distinction between **canonical loop data ownership** and **overdub session lifecycle**.

### Track

Owns the overdub session lifecycle:

```text
beginOverdubSession()
...
end / commit overdub
```

### Loop

Owns access to the canonical materialized loop state and therefore provides the source view required by the overdub session.

Conceptually:

```text
Track
  │
  │ begin overdub
  ▼
Loop
  │
  ├── establish overdubSourceView
  │
  ▼
stable canonical source
```

Do not introduce a new top-level manager merely to own this.

The exact field/class placement should be determined during implementation after auditing existing materialization and capture ownership.

### Naming

Preferred:

```text
overdubSourceView
```

Possible alternative if implementation clearly creates a snapshot object:

```text
overdubSourceSnapshot
```

Avoid:

```text
freezeOverdubSourceGeometry
freezePass
FrozenPass
FrozenGeometry
```

These introduce a domain concept that does not currently exist.

---

# 4. Pass Model

The phrase **"previous materialized passes"** should not be used as the semantic definition.

The relevant relationship is:

```text
canonical source state
        │
        │ overlap resolution
        ▼
new overdubPass
```

The source state is treated as immutable by the overdub operation.

The new overdub pass records the delta required to produce the resulting canonical state.

This mirrors the general edit-pass model.

---

# 5. `overdubPass` Is a Complete Delta

An `overdubPass` is **not merely a container for newly recorded notes**.

It must be capable of representing:

1. newly added notes;
2. shortening of existing source notes;
3. removal/hiding of existing source notes;
4. any other overlap consequences required by the canonical note-edit geometry rules.

Conceptually:

```text
overdubPass
├── additions
│   └── newly recorded notes
│
├── shortenings
│   └── changes to source notes
│
└── removals
    └── source notes made inactive/removed
```

The exact representation should reuse the existing pass/edit mechanisms wherever possible.

### Source immutability

If the source contains:

```text
A ─────────────────────
```

and the overdub inserts:

```text
       B ────
```

the source remains unchanged:

```text
A ─────────────────────
```

The overdub pass contains the transformation:

```text
Shorten(A, ...)
Add(B)
```

The source is therefore never destructively edited.

---

# 6. Overlap Examples

## 6.1 Short note overlapping long note

Source:

```text
A ─────────────────────
```

Incoming overdub note:

```text
       B ────
```

Resulting overdub delta:

```text
Shorten(A, ...)
Add(B)
```

The exact shortening must be determined by the canonical note-overlap geometry.

---

## 6.2 Long note covering multiple short notes

Source:

```text
A ──
    B ──
         C ──
```

Incoming overdub note:

```text
X ───────────────────────
```

Resulting overdub delta may be:

```text
Remove(A)
Remove(B)
Remove(C)
Add(X)
```

according to the canonical overlap rules.

The existing source pass remains unchanged.

---

## 6.3 Exact duplicate

If the incoming note exactly duplicates source material, it is an overlap case whose resulting action is determined by the same canonical overlap semantics.

It must **not** be implemented as a separate capture-only semantic policy.

This is important for `183525`: duplicate detection is only one outcome of the broader overlap-resolution process.

---

# 7. Canonical Overlap Authority

The overdub path should reuse the existing note-edit geometry flow rather than create a second overlap engine.

The intended authority remains:

```text
NoteGeometryResolver
       │
       ▼
constrained geometry
       │
       ▼
EditSessionAction / equivalent canonical actions
       │
       ▼
pass delta
```

The required invariant is:

> **Given the same source note geometry and the same incoming note geometry, overdub overlap resolution must make the same geometric decision as the canonical note-edit flow.**

This includes:

* exact duplicate;
* partial overlap;
* shortening;
* covering multiple notes;
* removal;
* minimum-length handling;
* wrap-equivalent geometry.

---

# 8. Minimum Note Length

Minimum note length remains part of the canonical geometry decision.

Current code uses:

```text
Config::noteMinLengthTicks
noteMinLengthRemoveEnabled
```

with the current default documented as:

```text
DEFAULT_NOTE_MIN_LENGTH_TICKS = 12
```

The product discussion has also referred to:

* 32nd-note minimum existing-note length;
* optionally 16th-note minimum when configured globally.

These must be reconciled during OpenSpec design.

The overdub path must **not** introduce an independent overdub-specific minimum-length rule.

The effective global minimum-length policy should be shared with note editing.

---

# 9. Incremental Evaluation During Overdub

A single overdub session may span multiple loop wraps.

The overlap-evaluation boundary is **each newly inserted note**, not overdub stop.

```text
start overdub
   │
   ├── wrap 1
   │    └── new note → evaluate
   │
   ├── wrap 2
   │    └── new note → evaluate
   │
   ├── wrap 3
   │    └── new note → evaluate
   │
   └── stop overdub
        └── commit one overdubPass / undo
```

Every newly inserted note that can overlap existing material must therefore enter the canonical overlap flow.

A loop wrap must not suppress subsequent evaluations.

### Important semantic distinction

The following are separate boundaries:

| Boundary          | Meaning                               |
| ----------------- | ------------------------------------- |
| Loop wrap         | Changes capture tick/phase context    |
| New inserted note | Triggers overlap evaluation           |
| Overdub stop      | Ends the overdub session              |
| Pass commit       | Stores the resulting overdub delta    |
| Undo              | Reverts the logical overdub operation |

A loop wrap does **not** currently create a new pass or undo.

---

# 10. Multi-Wrap Requirement

Consider:

```text
loop:
|---------------------------|

overdub session:

wrap 1:
    A

wrap 2:
    A

wrap 3:
    A
```

Each newly inserted `A` must be evaluated.

The implementation must not assume that evaluation in wrap 1 makes the corresponding phase irrelevant in wrap 2 or wrap 3.

It must not reduce the semantic search to:

* current wrap only;
* most recent event;
* most recent `(channel, note, type)` event;
* events appended since the previous wrap.

Candidate lookup may be optimized, but the semantic source domain must remain the stable `overdubSourceView`.

---

# 11. Undo Relationship

The current undo boundary is the overdub recording session:

```text
initial record
  └── recordPass + initial undo

overdub session #1
  ├── wrap 1
  ├── wrap 2
  ├── wrap 3
  └── stop
       └── overdubPass + one undo

overdub session #2
  ├── wrap 1
  └── stop
       └── overdubPass + another undo
```

The overdub pass therefore owns the complete delta produced during that session.

That includes:

```text
Add
Shorten
Remove
```

not just additions.

Undoing the overdub pass should therefore restore the previous canonical materialized state by removing/reversing the overdub pass delta.

Any future change to per-wrap undo granularity is a separate pass/undo design decision and is not part of this OpenSpec.

---

# 12. Why `lastSeenTick` Is Not Valid

The previously proposed:

```text
(channel, note, type) → lastSeenTick
```

must not become semantic authority.

It cannot correctly represent:

* long-vs-short overlap;
* partial overlap;
* one new note covering multiple source notes;
* shortening;
* removal;
* source geometry;
* wrap-equivalent geometry;
* the complete overdub-pass delta.

It also risks conflating events that belong to different semantic passes.

Candidate indexes may be introduced for performance, but they must remain an implementation of the source-view query rather than a replacement for its semantics.

---

# 13. Candidate Lookup

The intended runtime flow is:

```text
new incoming note
       │
       ▼
overdubSourceView
       │
       ▼
bounded chunk/window candidate lookup
       │
       ▼
candidate note spans
       │
       ▼
canonical NoteGeometryResolver
       │
       ▼
canonical actions
       │
       ▼
overdubPass delta
```

The optimization target is therefore:

> **Efficient candidate discovery for repeated per-note overlap evaluation.**

It is not merely:

> "Make duplicate detection faster."

The implementation should first audit existing chunk/window/materialization infrastructure.

Do not prescribe a new index structure in the OpenSpec unless implementation analysis shows that existing mechanisms cannot satisfy the required bounded lookup.

---

# 14. `183525` Reframing

The capture still establishes the immediate performance bug.

Current logic effectively assumes:

```text
reverse capture-store walk
    └── break when evt.tick < lo
```

This is only valid when reverse traversal is monotonic in tick.

After loop wrap, append order can be:

```text
... high loop ticks
0
1
2
3
```

so the assumption is invalid.

The result is an unnecessarily broad scan for each incoming note, producing the observed duplicate storm and display starvation.

The fix should therefore be framed as:

> **Provide efficient, wrap-safe candidate lookup into the stable overdub source view for repeated per-note overlap evaluation.**

Do not implement a semantic `lastSeenTick` shortcut.

Do not weaken the candidate domain to make the scan cheaper.

---

# 15. Phase 1

Phase 1 should establish the architecture before attempting the complete `183525` fix.

### Phase 1 scope

* establish `overdubSourceView` at overdub start;
* verify it is materialize-aware;
* verify it includes the effective `editPasses` result;
* verify it remains stable across multiple loop wraps;
* verify source material is not mutated;
* add native tests for source selection and candidate lookup;
* establish parity with canonical note-edit geometry where host-testable.

### Later phase

Then:

* connect per-note overdub overlap resolution;
* implement efficient candidate lookup;
* remove the invalid reverse-tick early-out;
* route overlap decisions through canonical note geometry;
* encode Add / Shorten / Remove into the new overdub pass;
* verify `183525` on hardware;
* throttle duplicate diagnostics separately.

### Interim safety

If CAP/RING flooding prevents useful observation of `DFRAME`, a behavior-preserving deny-log throttle may be implemented separately.

It must not change overlap semantics.

---

# 16. Persistence Boundary

Persistence is not responsible for overlap resolution.

The persistence layer may persist the appropriate sealed capture material and/or the resulting committed pass according to its existing contract.

It must not:

* resolve note overlaps;
* shorten notes;
* remove notes;
* invent overdub state during load;
* re-run geometry resolution during save/recovery.

The separation remains:

```text
runtime capture
      │
      ▼
overlap resolution
      │
      ▼
committed overdubPass
      │
      ▼
persistence
```

---

# 17. Updated Architecture Invariants

| Question                                                   | Decision                 |
| ---------------------------------------------------------- | ------------------------ |
| Is the loop itself frozen at overdub start?                | **NO**                   |
| Is there a stable overdub source view?                     | **YES**                  |
| Is it established at overdub start?                        | **YES**                  |
| Does it remain semantically stable throughout the session? | **YES**                  |
| Is it materialize-aware?                                   | **YES**                  |
| Does it include effective `editPasses`?                    | **YES**                  |
| Is source material mutated directly by overdub resolution? | **NO**                   |
| Does `overdubPass` contain only new notes?                 | **NO**                   |
| Can `overdubPass` contain shorten/remove changes?          | **YES**                  |
| Is every newly inserted overlapping note evaluated?        | **YES**                  |
| Can one overdub span multiple wraps?                       | **YES**                  |
| Does a loop wrap create an undo?                           | **NO, current behavior** |
| Does overdub stop create the overdub pass/undo?            | **YES**                  |
| Is `lastSeenTick` semantic authority?                      | **NO**                   |
| Is candidate lookup optimized?                             | **YES**                  |
| Is the source representation prescribed?                   | **NO**                   |
| Should existing chunk/window infrastructure be preferred?  | **YES**                  |
| Does persistence perform overlap resolution?               | **NO**                   |
| Is a new top-level manager required?                       | **NO**                   |

---

# 18. Normative Core

The OpenSpec should ultimately establish these requirements:

1. **Each overdub session establishes one stable, materialize-aware `overdubSourceView` at overdub start.**
2. **The source view remains semantically stable for the lifetime of that overdub session.**
3. **Every newly inserted note that can overlap existing material is evaluated during the session.**
4. **Every evaluation queries the same source view, including across loop wraps.**
5. **Overlap decisions reuse the canonical note-edit geometry semantics.**
6. **The source material is never destructively modified by the overdub operation.**
7. **The resulting `overdubPass` records the complete delta required to transform the source state into the overdub result, including additions, shortenings, and removals.**
8. **The current overdub session remains one logical `overdubPass` / undo operation regardless of the number of loop wraps.**
9. **Candidate lookup may use chunk/window/index optimizations, but optimization must not reduce the semantic candidate domain.**
10. **Persistence does not perform overlap resolution.**
11. **The physical representation of `overdubSourceView` is an implementation decision, provided the semantic contract is preserved.**

This should replace the earlier "materialized all previous passes" wording and should be the basis for the OpenSpec design phase.

---

# 19. Open Q4 — Encode pin (C → A) + PREFLIGHT

**User decision (2026-08-12):** Option **1 — C → A**.

Full audit: [`PREFLIGHT.md`](PREFLIGHT.md). Decision log: **DEC-031**.

| Topic | Pin |
|-------|-----|
| Mid-session | Pending overdub-operation **buffer** (session state on `Loop`, not a pass); no mid-session `EditPass` writes |
| Seal Adds | Existing `OverdubPass` chunks |
| Seal Shorten/Hide | Existing `EditPass` rows via `Loop::saveNoteEditPass` |
| Extend `OverdubPass` struct? | **NO** for v1 |
| Logical undo | One `OverdubPassAdded` with `passId` + companion `editPassIds` (field already on `UndoEntry`) |
| Grouping identity | `OverdubPass.id` — no new overdub-op identifier |
| Commit order | Publish OverdubPass → save EditPass rows → push undo |
| `shouldRestoreCommittedOverlapOnOverdubStop` | **Not authoritative** when `overdubSourceView` established; do not `removeOpenCaptureNoteOn` on that path |
| Persistence | No loop SD schema bump; GUS must round-trip `editPassIds` on `OverdubPassAdded` |

### Phase 2 firmware slices (after this pin)

**PAUSED (2026-08-12).** Do not implement C→A pending-buffer → OverdubPass+EditPass until the unified-pass architecture pin lands.

See §20 and [`UNIFIED-PASS-ARCHITECTURE-REVIEW.md`](UNIFIED-PASS-ARCHITECTURE-REVIEW.md).

---

# 20. Architecture refinement — unify input mechanisms vs pass storage

## Decision (process)

**Stop Phase 2 C→A firmware** before introducing a pending overdub buffer that seals into `OverdubPass` + `EditPass` as if that dual seal were the final architecture.

## Hypothesis (user)

Live MIDI overdub and note editing are two **input mechanisms** that produce the same canonical operations:

```text
Add / Shorten / Hide
```

The pass should represent **what changed**. The interaction mechanism represents **how** the change was produced. These should not require separate pass types long-term.

```text
input (MIDI | note editor)
        │
        ▼
canonical geometry
        │
        ▼
Add / Shorten / Hide
        │
        ▼
pending session changes
        │
        ▼
committed canonical pass
        │
        ▼
undo
```

Session ≠ pass. `overdubSourceView` remains a session baseline. Multi-wrap evaluate-on-insert remains. Capture-only duplicate / restore heuristics must not be a second overlap engine.

## Code review finding (storage)

Audit ([`UNIFIED-PASS-ARCHITECTURE-REVIEW.md`](UNIFIED-PASS-ARCHITECTURE-REVIEW.md)):

- **`OverdubPass` / `EditPass` are true storage families today** (chunks vs Create/Update/Delete; two-phase materialize; separate undo kinds; capture wire vs EPT3).
- **Shared Add/Shorten/Hide geometry across input mechanisms is validated** and remains the resolution goal.
- **Removing both types in this change is not validated** without superseding `timeline-passes` and designing SD/undo migration.

## Required user pin before Phase 2 resumes

| Option | Meaning |
|--------|---------|
| **G2** | Resume DEC-031 transitional dual seal (geometry unified; storage dual) |
| **G1** | Geometry + source-view lookup only; delay seal |
| **U1** | Park seal; open unified committed-pass OpenSpec (schema/undo/materialize) |

Until pinned: no pending-overdub-buffer firmware, no `OverdubPassAdded.editPassIds` companion seal, no mid-session EditPass writes.
