# Historical decision review

**Mandatory before firmware implementation** when a [formal trigger](../ARCHITECTURE_REASSESSMENT.md#formal-triggers) fired. For normal extension work: quick `rg` on DECISION_LOG, then implement per [decision ladder](../00-authority/ARCHITECTURE_RULES.md#progress-bias-and-decision-ladder).

---

## Historical review

### Searched

Run search (e.g. `rg`) across:

- [ ] [`docs/DECISION_LOG.md`](../DECISION_LOG.md)
- [ ] Active OpenSpec — `openspec/changes/<name>/` per [PROJECT_STATE.md](../runtime/PROJECT_STATE.md)
- [ ] `openspec/specs/` for archived normative behavior
- [ ] `docs/plans/` — handoffs and `*_enhancement.md` only (not every `.plan.md` export)

**Search terms used:** _list keywords (module names, feature nouns, rejected pattern names)_

### Relevant findings

_List DEC-### IDs, OpenSpec paths, or plan links — or "none found"._

—

### Existing reusable pattern

_Extension point, owner method, Actions handler, policy class, or spec requirement to extend — or "none"._

—

### Reuse decision

**YES** / **NO**

### If NO

**Reason:** _Why extension point or prior decision does not apply (must cite evidence, not preference)._

**New constraints introduced:** _What future work must respect if this path proceeds._

**Migration required:** **YES** / **NO**

_If YES: SD format, data migration, test matrix, HITL — one line each._

---

## Challenge path

Historical decisions are **not immutable**. To overturn a logged decision:

1. Run [ARCHITECTURE_REASSESSMENT.md](../ARCHITECTURE_REASSESSMENT.md)
2. Add new `DECISION_LOG` entry with `Status: Superseded` on the old ID (reference in Rationale)
3. OpenSpec proposal if normative behavior changes

---

## Reviewer gate

Implementation must be **rejected** if this review was skipped or shows:

- Equivalent abstraction already exists
- Equivalent helper already exists
- Ownership duplicated
- Historical decision ignored without supersede entry
- Migration path missing when `Migration required: YES`

See [agents/reviewer.md](../agents/reviewer.md).
