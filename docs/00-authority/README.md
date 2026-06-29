# Authority hierarchy

Agents and humans resolve conflicts using this order (highest wins):

```
PROJECT_INTENT.md          ← product identity, scope, philosophy
    ↓
ARCHITECTURE_RULES.md      ← ownership, boundaries, structure
    ↓
OpenSpec                   ← desired behavior, accepted requirements
    openspec/specs/          (archived normative behavior)
    openspec/changes/        (active deltas until /opsx:archive)
    ↓
Current guides             ← docs/Guides/ (behavior as shipped)
    ↓
Historical plans           ← docs/plans/, docs/Refinements/ (context only)
    ↓
Code                       ← implementation; cannot silently redefine architecture
```

**Conflict resolution:** precedence alone is not enough — see [Conflict resolution](#conflict-resolution) below.

---

## What each layer defines

| Layer | Defines | Cannot be… |
|-------|---------|------------|
| **PROJECT_INTENT** | Product identity, scope, litmus tests, key decisions | Overridden by lower layers without deliberate intent update |
| **ARCHITECTURE_RULES** | Ownership, naming, forbidden patterns, extension rules | Bypassed by OpenSpec, plans, or code |
| **OpenSpec** | Desired behavior, SHALL/MUST requirements, task scope | Used to redefine architecture directly (new Manager, ownership move) without reassessment |
| **Guides** | How shipped behavior works today | Authority over OpenSpec specs or architecture |
| **Historical plans** | Design context, handoffs, exports | Treated as normative or override for architecture |
| **Code** | Current implementation | Silent source of truth when docs disagree |

---

## Conflict resolution

When two layers disagree, use this table — **do not guess** or implement a compromise in code.

### Layer vs layer

| Conflict | Resolution |
|----------|------------|
| **OpenSpec vs ARCHITECTURE_RULES** | **Stop.** OpenSpec states *what*; architecture states *who owns how*. If behavior requires a new Manager, ownership move, or forbidden pattern → [architecture reassessment](../ARCHITECTURE_REASSESSMENT.md). **No code until approved.** Then: amend OpenSpec design/tasks **or** extend existing owner per architecture. |
| **OpenSpec vs PROJECT_INTENT** | **Stop.** Spec or feature violates litmus tests → drop or redesign spec; update intent only if user approves product change. |
| **Guide vs OpenSpec (archived spec)** | **OpenSpec wins** for normative behavior. Update guide after implementation, or `/opsx:sync` if spec drifted. |
| **Plan vs OpenSpec / architecture** | **Plan loses** unless promoted into OpenSpec + authority update. |
| **Code vs OpenSpec / architecture** | **Docs win** unless deliberate migration: fix code **or** run reassessment + spec/intent update — never “code is the spec.” |
| **DECISION_LOG vs new proposal** | Logged decision stands. **Supersede** via reassessment + new `DEC-###` entry — do not ignore. |

### Worked example: OpenSpec vs architecture

```text
OpenSpec proposes:     Introduce SaveSessionManager
ARCHITECTURE_RULES:    Persistence owned by StorageManager; avoid new *Manager without review
```

**Wrong:** Implement `SaveSessionManager` because tasks.md says so.  
**Wrong:** Drop the OpenSpec requirement silently.  
**Right:**

1. **STOP** implementation  
2. Run [architecture reassessment](../ARCHITECTURE_REASSESSMENT.md) — problem, alternatives, migration, recommendation  
3. Choose one path with user approval:
   - **A (preferred):** Extend `StorageManager` / existing policy — update OpenSpec design to name owner methods, not a new class  
   - **B:** New Manager — ownership transfer protocol + update ARCHITECTURE_RULES + DECISION_LOG + OpenSpec after approval ([OWNERSHIP_TRANSFER.md](../templates/OWNERSHIP_TRANSFER.md))  
4. Record in [DECISION_LOG.md](../DECISION_LOG.md)  
5. Then implement

### Behavior requires architecture change

If implementing accepted **behavior** (OpenSpec) would violate **structure** (ARCHITECTURE_RULES) or **identity** (PROJECT_INTENT):

```text
STOP implementation
→ Architecture reassessment (problem, alternatives, migration, recommendation)
→ User approval
→ Update authority and/or OpenSpec explicitly
→ DECISION_LOG append
→ Then code
```

**No partial implementation** (stub Manager, TODO ownership) to “unblock” the spec.

### Behavior fits architecture

If OpenSpec fits existing ownership and forbidden patterns:

```text
Proceed
→ DECISION_REVIEW + PREFLIGHT
→ Implement against tasks.md
```

---

## Agent rule (planning)

During planning, before the first edit:

1. **Identify the highest authority source** for each constraint (intent, architecture, spec, guide).  
2. **Scan for conflicts** — especially OpenSpec ↔ ARCHITECTURE_RULES and proposal ↔ DECISION_LOG.  
3. If conflict detected → **pause implementation**; output reassessment; wait for approval.  
4. If no conflict → historical review + preflight → implement.

Do not resolve conflicts by:

- Adding a parallel helper that duplicates an owner  
- Weakening architecture in code without doc update  
- Treating `tasks.md` as permission to invent structure  

---

## Rules (precedence)

| Rule | Meaning |
|------|---------|
| Historical plans never override current architecture | A `docs/plans/*.plan.md` from 2025 does not trump `ARCHITECTURE_RULES` or an archived OpenSpec spec. |
| OpenSpec is authoritative for **behavior** | After `/opsx:archive`, requirements live in `openspec/specs/`. Active work follows `openspec/changes/<name>/tasks.md`. |
| OpenSpec is **not** authority for **structure** alone | New modules and ownership changes require architecture alignment. |
| Runtime code cannot silently redefine architecture | If code and docs disagree, fix code **or** update authority deliberately. |
| This layer does not change firmware | These files govern agent workflow and documentation only. |
| **Progress by default** | Continue implementation unless a [formal reassessment trigger](../ARCHITECTURE_REASSESSMENT.md#formal-triggers) fires — see [decision ladder](ARCHITECTURE_RULES.md#progress-bias-and-decision-ladder) |

### OpenSpec attention (from delivery rules)

- **Active change list** lives in [PROJECT_STATE.md](../runtime/PROJECT_STATE.md) + scope in [CURRENT_WORK.md](../runtime/CURRENT_WORK.md) — not ROADMAP or static docs alone.
- **Historical decisions** — search [DECISION_LOG.md](../DECISION_LOG.md); run [DECISION_REVIEW.md](../templates/DECISION_REVIEW.md) before firmware implementation.
- **Authority conflicts** — reassess before coding; see [Conflict resolution](#conflict-resolution).
- **Implement** via `tasks.md` + `/opsx:apply`; **archive** only after native (+ HITL when required).
- **Cite brownfield docs** in proposals; do not duplicate [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) or phase-3 plans into new specs.
- **Locked order:** persistence/hardening → JamRecorder → M10 → **D13 last** — never start parked `jam-recording` early.
- **Naming:** passes, Capture, editPass, NoteEditSession — see Naming-Vocabulary Cursor rule.

---

## Files in this folder

| File | Role |
|------|------|
| [PROJECT_INTENT.md](PROJECT_INTENT.md) | Canonical project goal and decision log |
| [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md) | Ownership, naming suffixes, forbidden patterns, new-abstraction checklist |
| [DELIVERY_RULES.md](DELIVERY_RULES.md) | How work is tracked, verified, and shipped |

---

## Agent entry points

1. [docs/runtime/PROJECT_STATE.md](../runtime/PROJECT_STATE.md) — execution context (load first)
2. [docs/runtime/CURRENT_WORK.md](../runtime/CURRENT_WORK.md) — implementation scope (**required** before coding)
3. [docs/runtime/ROADMAP.md](../runtime/ROADMAP.md) — future sequencing (optional; not implementation authority)
4. [docs/DECISION_LOG.md](../DECISION_LOG.md) — historical decisions (search + append at closeout)
5. [docs/templates/DECISION_REVIEW.md](../templates/DECISION_REVIEW.md) — mandatory before firmware implementation
6. [docs/AGENT_CONTEXT_MAP.md](../AGENT_CONTEXT_MAP.md) — domain → required docs
7. [docs/templates/PREFLIGHT.md](../templates/PREFLIGHT.md) — planning template (§ Similar Historical Decisions mandatory)
8. [docs/templates/OWNERSHIP_TRANSFER.md](../templates/OWNERSHIP_TRANSFER.md) — when mutable scope moves between modules
9. [docs/ARCHITECTURE_REASSESSMENT.md](../ARCHITECTURE_REASSESSMENT.md) — when to pause for design review **or authority conflict**
10. [docs/templates/SESSION_CLOSEOUT.md](../templates/SESSION_CLOSEOUT.md) — before closing design chats

Cursor rule: `.cursor/rules/Agent-Context-Workflow.mdc`
