---
name: Pass vocabulary consistency
overview: "Four pass kinds; two storage families. editPass + EditPassKind (NoteEdit | ControlChange). OpenSpec timeline-pass-model locked."
todos:
  - id: openspec-vocab-locked
    content: Four pass kinds + editPass/EditPassKind in timeline-pass-model
    status: completed
  - id: apply-timeline-pass
    content: Run /opsx:apply on timeline-pass-model
    status: pending
isProject: false
---

# Locked — four pass kinds, two families

| Kind (prose) | Storage |
|--------------|---------|
| recordPass | `passes.recordPass` (0–1) |
| overdubPass | `passes.overdubPasses[]` |
| noteEditPass | `editPass` row, `EditPassKind::NoteEdit` |
| controlChangeEditPass | `editPass` row, `EditPassKind::ControlChange` (stub) |

Not used: single capturePass+kind, four separate edit arrays, committed on passes.

OpenSpec: [`openspec/changes/timeline-pass-model/`](../../openspec/changes/timeline-pass-model/)
