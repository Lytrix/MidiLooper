# Architecture review — Unify overdub and note-edit passes

**Date:** 2026-08-12  
**Change:** `overdub-pass-overlap-resolution`  
**Status:** **G2 approved** (DEC-032) — Phase 2 may resume  
**Trigger:** User refinement + formal triggers for storage unification (U1 deferred)

Related: [design.md](design.md) §19–20, [PREFLIGHT.md](PREFLIGHT.md) (transitional dual-seal encoding), DEC-031 / DEC-032.

---

## User pin (2026-08-12)

**G2 — Unified pending action model with existing dual storage/seal representations.**

| Concern | Decision |
|---------|----------|
| Resolution / geometry | **Unified now** — MIDI and NOTE_EDIT share Add/Shorten/Hide semantics |
| Pending session delta | Session-scoped logical change set (not a pass type) |
| Persistent storage | **Transitional dual** — capture chunks for Add; edit rows for Shorten/Hide |
| U1 / U2 | **Out of scope** — separate migration OpenSpec if ever pursued |
| Semantic model | Not `OverdubPass + linked EditPass` — that is encoding only |
| Phase 2 firmware | **Unblocked** under G2 |

> Unify the semantics of change, not prematurely unify the persistence representation of change.

---

## Resolution vs storage

**Resolution unification and storage unification are separate architectural concerns.**

MIDI overdub and NOTE_EDIT share canonical note geometry and produce the same logical Add/Shorten/Hide operations. During a session these form one pending logical change set. Commit may encode into existing capture and edit storage families. That dual representation is transitional implementation architecture, not a second overlap engine.

---

## Code finding (unchanged)

`OverdubPass` / `EditPass` remain real **storage families** today (chunks vs Create/Update/Delete; two-phase materialize; separate undo/persistence wire). G2 keeps them for this change. Shared geometry across input mechanisms is the semantic goal of Phase 2.

---

## Phase 2 target (G2)

```text
stable overdubSourceView
          │
          ▼
per-note candidate lookup
          │
          ▼
shared canonical geometry
          │
          ▼
pending logical Add/Shorten/Hide
          │
          ▼
stop overdub
          │
          ▼
existing dual storage seal
          │
          ▼
one undo
```

Do not: new semantic OverdubPass abstraction; `lastSeenTick` authority; merge persistent storage families; overlap resolve in persistence.

---

## U1/U2 deferred

| Option | Status |
|--------|--------|
| U1 unified committed pass | Separate OpenSpec + schema/undo/materialize reassessment |
| U2 Adds-only via edit Create | Separate reassessment (heap/chunk scale) |
| G1 geometry-only | Not chosen |

---

## Historical options table (reference)

| Option | Meaning | Status |
|--------|---------|--------|
| G1 | Geometry + lookup; delay seal | Not chosen |
| **G2** | Pending logical delta + dual seal | **Approved** |
| U1 | Single committed pass type | Deferred |
| U2 | Adds via edit rows only | Deferred |
