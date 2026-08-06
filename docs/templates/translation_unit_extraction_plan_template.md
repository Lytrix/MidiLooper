# {Module} translation-unit extraction

**Kind:** refinement  
**Branch:** `refactor/{module}` (from `dev`)  
**Naming authority:** [NAMING.md](../00-authority/NAMING.md)  
**Workflow:** [Mechanical-TU-Split-Workflow.mdc](../../.cursor/rules/Mechanical-TU-Split-Workflow.mdc)  
**Legacy API retirement:** [legacy_api_retirement_tu_extraction_refinement.md](../plans/legacy_api_retirement_tu_extraction_refinement.md)

---

## One-line goal

Shrink [`src/{Root}.cpp`](../../src/{Root}.cpp) by moving method bodies into **domain-owned modules** under [`src/{Folder}/`](../../src/{Folder}/), behavior-preserving, **no ownership or lifecycle changes**.

**Success criteria:** domain ownership primary — not LOC targets. Use LOC bands as review heuristics only.

---

## Phase 0 — prior splits inventory (mandatory)

| Sibling refactor | Shipped pattern to reuse |
|------------------|--------------------------|
| [Track TU plan](../../.cursor/plans/track_tu_split.plan.md) | `src/Track/`, `Track*TestDeps.cpp` |
| [EditManager TU plan](../../.cursor/plans/editmanager_tu_split.plan.md) | `src/EditManager/`, `EditManagerInternal.h` |
| [Loop TU plan](../../.cursor/plans/loop_tu_split.plan.md) | `src/Loop/`, `LoopInternal.h`, `LoopCaptureTestDeps.cpp` |

List **already-extracted** symbols/files in this domain that the plan must account for (do not re-plan work that shipped).

---

## Phase 0 — naming pass (mandatory)

Symbols/files to **strengthen** in the same PR as moves ([NAMING.md](../00-authority/NAMING.md) § Migration policy — TU extraction):

| Current | Proposed | Rationale |
|---------|----------|-----------|
| | | |

---

## Phase 0 — cross-domain placement table (mandatory)

| Symbol / file | Owner module | Rationale | Rename (if misleading) |
|---------------|--------------|-----------|------------------------|
| | | | |

Flag rows where the **name** suggests the wrong domain (e.g. `LoadLoop*` on StorageManager code).

---

## Primary architectural modules

```text
src/{Root}.cpp                 coordinator (target band ~150–250 if class-owned; may be 0 if free functions)
src/{Folder}/{ModuleA}.cpp     architectural question A
src/{Folder}/{ModuleB}.cpp     architectural question B
include/{Module}Internal.h     shared cold helpers / template decls (if needed)
```

| Module | Architectural question |
|--------|------------------------|
| | |

**LOC heuristic (review only):** &lt;200 → possible over-slice; &gt;800 → split candidate if domain supports it.

---

## Rules (every phase)

1. **Behavior-preserving** — no ownership or state transition changes.
2. **Architecture checkpoint** — ownership **NO**, transition **NO** unless phase explicitly approved.
3. **Per phase:** `git commit` + `pio run -e teensy41-capture-serial` (mandatory).
4. **Per batch (low/med):** `pio test -e native` once at end.
5. **Naming in-scope** — touch-and-strengthen in same commit as move; **no legacy wrapper/alias removal** during extraction ([legacy_api_retirement_tu_extraction_refinement.md](../plans/legacy_api_retirement_tu_extraction_refinement.md)).
6. **Risk tier** per phase: `low` | `medium` | `high` — high splits sessions; firmware + native per slice.

---

## Phase N — {title} ({risk})

**Risk:** low | medium | high

| Symbol | Role |
|--------|------|
| | |

**PR title:** `refactor({module}): Phase N …`

---

## Phase LR — Legacy Retirement (optional, after Phase 10)

**Risk:** low — API cleanup only; no ownership or behaviour change.

List wrappers/aliases to remove (must meet [retirement criteria](../plans/legacy_api_retirement_tu_extraction_refinement.md#retirement-criteria)):

| Legacy symbol | Replacement | Callers migrated? |
|---------------|-------------|-------------------|
| | | |

**PR title:** `refactor({module}): Legacy Retirement — remove obsolete wrappers`

---

## Per-phase checklist (copy into PR)

```markdown
## Architecture gate
- Owner: …
- Ownership / transition change: NO
- Risk: …

## Verification
- [ ] pio run -e teensy41-capture-serial (this phase)
- [ ] pio test -e native (batch end for low/med)
- [ ] LOC note recorded
- [ ] HITL (if high / display / commit path)
```

---

## Native test include policy

Update `*TestDeps.cpp` or per-suite `#include` paths when symbols move. Many suites `#include` root `.cpp` directly.
