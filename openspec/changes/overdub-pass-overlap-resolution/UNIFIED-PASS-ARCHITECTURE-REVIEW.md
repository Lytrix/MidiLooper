# Architecture review — Unify overdub and note-edit passes

**Date:** 2026-08-12  
**Change:** `overdub-pass-overlap-resolution`  
**Status:** Review complete — **user pin required** before any Phase 2 firmware  
**Trigger:** User refinement (pause C→A) + formal triggers (persistent state model / undo semantics / OpenSpec vs timeline-passes two-family rule)

Related: [design.md](design.md) §20, [PREFLIGHT.md](PREFLIGHT.md) (C→A — **paused**), [DEC-031](../../../docs/DECISION_LOG.md) (parked for seal path), [DEC-032](../../../docs/DECISION_LOG.md).

---

## Reason triggered

| Trigger | Why |
|---------|-----|
| Persistent state model | Proposal to replace dual committed containers with one canonical pass |
| Undo semantics | One undo kind for any input mechanism |
| OpenSpec vs architecture | Active `timeline-passes` requires two storage families; unification would supersede |
| User hard stop | Do not implement pending-buffer → OverdubPass+EditPass until this review pins direction |

---

## User hypothesis (refinement)

> Live MIDI overdub and note editing are two **input mechanisms** that produce the same canonical note-change operations (Add / Shorten / Hide). The input mechanism should not determine the resulting pass representation. `OverdubPass` and `EditPass` are candidates for removal/unification.

Desired flow:

```text
input mechanism (MIDI | note editor)
        │
        ▼
canonical geometry (shared)
        │
        ▼
Add / Shorten / Hide
        │
        ▼
pending session changes
        │
        ▼
one committed timeline pass
        │
        ▼
one undo
```

`overdubSourceView`, multi-wrap evaluate-on-insert, and shared geometry remain in force.

---

## Current architecture (code)

```text
recordPass / overdubPasses[]     → chunk-backed MIDI layers (CapturePassState, mergeSequence)
editPasses[]                     → Create/Update/Delete delta rows (EditActionType, targetNoteId)

materialize:
  appendActiveCapturePassesToFlat
        │
        ▼
  applyActiveEditPasses*
```

| Layer | Vocabulary | Owner path |
|-------|------------|------------|
| Live geometry | `EditSessionActionType` Shorten/Hide/… | `resolveConstrainedGeometry` / NOTE_EDIT apply |
| Stored capture | MIDI in `committedChunkIds` | `commitCapturePass` / `commitPendingCapturePass` |
| Stored edit | `EditActionType` Create/Update/Delete | `Loop::saveNoteEditPass` |
| Undo | `OverdubPassAdded` (`passId`) vs `NoteEditPassClosed` (`editPassIds`) | `TrackUndo` |
| Persistence | Capture headers+chunks vs EPT3 edit tail | `StorageLoopIo` |

Normative: [`openspec/specs/timeline-passes/spec.md`](../../specs/timeline-passes/spec.md) — two storage families; do not merge record+overdub into one capture row family.  
Naming: [`docs/Authority/NAMING.md`](../../../docs/Authority/NAMING.md) — distinct pass ends-with + undo kinds.

---

## Evidence: SEMANTIC vs HISTORICAL

### Must remain distinct unless OpenSpec + persistence migration supersede (semantic / storage)

| Distinction | Proof |
|-------------|--------|
| Chunk layer vs delta overlay | `OverdubPass.committedChunkIds` vs `EditPass` action fields (`LoopPasses.h`, `EditPass.h`) |
| Two-phase materialize | `LoopPasses::materializeToEventVector` — capture then edit |
| Admission tiers | Chunk pool reserve vs `canHeapAdmitEditPass` |
| Playback fast path | No active edits → chunk window; active edits → full materialize (`gatherCommittedEvents`) |
| Undo identity | One `passId` vs many `editPassIds` |
| SD wire | Capture stream vs EPT3 marker `0x45505433` |
| Lifecycle | Capture seal vs NOTE_EDIT `saveNoteEditPass` / session close |

### Historical / can converge without merging storage types

| Item | Proof |
|------|--------|
| Capture-only overlap heuristics | `isDuplicateCaptureEvent`, `shouldRestoreCommittedOverlapOnOverdubStop` |
| Dual live languages mapping to same edit rows | `EditSessionActionType` → `recordApplyOwnedEditPassRow` → Delete/Update+Length |
| Overdub Adds as chunks vs NOTE_EDIT Adds as Create rows | Different seal paths; both can mean “Add” musically |
| Naming that sounds like “input mechanism” | Product overdub vs NOTE_EDIT UI |

---

## Answer to the required OpenSpec question

> Are `OverdubPass` and `EditPass` true domain concepts that must remain distinct, or are they historical representations of two different input mechanisms that should converge into one canonical pass/change model?

### Evidence-based answer (storage)

**They are true domain storage concepts today** — two committed families with different payloads, materialize phases, admission, reclaim, undo apply, and SD wire. Treating them as mere input labels is **not supported** by the codebase or archived `timeline-passes`.

### Evidence-based answer (resolution)

**Add / Shorten / Hide as shared resolution outcomes across input mechanisms is supported** and already the direction of this OpenSpec + DEC-031 geometry layer. That does **not** by itself collapse the two storage families.

### Hypothesis status

| Claim | Status |
|-------|--------|
| Share one geometry authority for MIDI and note-edit | **Validated** — pursue |
| `overdubSourceView` + multi-wrap evaluate-on-insert | **Validated** — keep |
| Exact duplicate is one geometry outcome, not separate authority | **Validated** — keep |
| Remove/unify OverdubPass and EditPass as one committed type **now** | **Not validated** — high formal-trigger cost; contradicts current specs/DEC-031 seal pin |
| Long-term unified committed change model | **Open** — requires explicit supersession of `timeline-passes` + migration design |

---

## Migration options

| Option | Meaning | Disruption | SD existing sets |
|--------|---------|------------|------------------|
| **G1 — Geometry unify only** | Shared `resolveConstrainedGeometry`; keep dual storage; Phase 2 resumes as pending ops → dual seal (C→A) | Low | Load unchanged |
| **G2 — Unified pending + dual seal** | One session pending-action model; seal still dual; one undo step | Low–med | Load unchanged |
| **U1 — Unified committed pass type** | One storage type for Add/Shorten/Hide from any input | High | New format + legacy dual-reader or convert-on-load |
| **U2 — Encode overdub Adds as EditPass Create only** | Collapse Adds into edit heap rows; drop overdub chunk role for Adds | Med–high | Possible without new pass type; admission/scale unknown |

**DEC-031 C→A** = G2 at the seal boundary. This refinement **pauses G2 firmware** until the user pins G1/G2 vs U1/U2.

---

## Formal triggers if choosing U1

- Persistent state model change  
- Storage schema / EPT3 + capture wire  
- Undo kind collapse or reshaping  
- OpenSpec conflict with `timeline-passes` two-family rule  
- Materialize + reclaim rewrite  
- Ownership of seal paths (capture commit vs `saveNoteEditPass`)

---

## Recommendation (for user pin)

1. **Adopt the refinement’s process model as normative for resolution:**
   - input mechanism ≠ pass type  
   - shared geometry → Add/Shorten/Hide  
   - session pending changes → commit → undo  
   - `overdubSourceView` remains  
2. **Do not remove OverdubPass/EditPass in this change** until a dedicated migration OpenSpec supersedes `timeline-passes` and pins U1 (or U2) with load converters.  
3. **Immediate Phase 2 choice** (pick one):

| Pin | Effect |
|-----|--------|
| **G2 (resume DEC-031)** | Smallest path to device `183525` relief + shared geometry; dual storage transitional |
| **G1 only** | Geometry+lookup first; delay seal companions |
| **Park all Phase 2 seal; open U1 change** | No overdub overlap encode until unified storage designed |

Default engineering recommendation from code evidence: **G2 for this change** (musical authority = geometry; storage families remain transitional), and schedule **U1** as a separate OpenSpec after Phase 2/3 green — *unless* the user prioritizes storage unification over near-term wrap/display recovery.

---

## Pins still required from user

1. **Resolution model:** Confirm shared Add/Shorten/Hide geometry across MIDI and note-edit (**recommended YES**).  
2. **Storage model for this change:** G2 (dual seal) vs park for U1.  
3. If U1: approve formal reassessment scope (schema, undo, materialize) as a **new** OpenSpec phase/change — not silent Phase 2 continuation.  
4. Restore authority: when overdub source view exists, capture restore/remove is not authoritative (unchanged from PREFLIGHT Q4).

---

## Approval

| Field | Value |
|-------|-------|
| Approval required | **YES** — hard stop |
| Firmware | **Blocked** until pin |
| Phase 1 (`overdubSourceView`) | Remains shipped / valid |
