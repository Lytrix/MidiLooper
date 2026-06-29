# Session closeout template

Use when ending a chat that involved **design, alternatives, exclusions, or architecture**. Prevents lost rationale when the tab is closed.

**User prompt (optional):** *"Session closeout — update runtime docs."*

---

## Session closeout checklist

- [ ] Update [`PROJECT_STATE.md`](../runtime/PROJECT_STATE.md) and [`CURRENT_WORK.md`](../runtime/CURRENT_WORK.md)
- [ ] **Append** to [`DECISION_LOG.md`](../DECISION_LOG.md) for each new or changed decision (structured `DEC-###` entry)
- [ ] OpenSpec `tasks.md` / `design.md` / `OPEN_QUESTIONS.md` if the change has a folder
- [ ] [`DELIVERABLE_TRACKING.md`](../DELIVERABLE_TRACKING.md) if something shipped

Skip decision-log append only if the session was purely implementation of an already-logged decision with no new exclusions.

---

## Decision log entry (copy per topic)

Add to [`DECISION_LOG.md`](../DECISION_LOG.md) using the full structure in that file:

```markdown
## DEC-### — Short topic title

**Date:** YYYY-MM-DD  
**Owner:** module or role  
**Status:** Accepted | Superseded | Deprecated

### Problem
### Decision
### Rationale
### Alternatives considered
| Alternative | Rejected because |
### Affected modules
### Constraints created
### Related OpenSpec
### Migration notes
```

For **Superseded**: reference prior `DEC-###` in Rationale; do not delete the old entry.

---

## Rejection quality bar

Each rejected alternative needs a **reason a future agent can act on**:

| Weak | Strong |
|------|--------|
| "Too complex" | "Duplicates state already on `StorageManager` dirty flags" |
| "Later" | "Blocked until JamRecorder; D13 parked in archive" |
| "Didn't work" | "HITL 203729: overlap restore failed when …" |

---

## Where else to record

| Depth | Location |
|-------|----------|
| Normative requirement | OpenSpec spec delta → archive to `openspec/specs/` |
| Architecture pause | [ARCHITECTURE_REASSESSMENT.md](../ARCHITECTURE_REASSESSMENT.md) output + DECISION_LOG append |
| Large handoff | `docs/plans/*_handoff.md` (link from decision log) |
| Open product question | OpenSpec `OPEN_QUESTIONS.md` in the change folder |

---

## Example user message to agent

> Session closeout: append decisions to DECISION_LOG and refresh PROJECT_STATE.
