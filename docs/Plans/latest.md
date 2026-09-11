---
name: OpenSpec 5.1 Undo
overview: "OpenSpec task 5.1: TrackUndo and clear-slot retain/select before/after published LoopSnapshot identities, with deferred after-attach for session-exit publish, while keeping pass Active/Disabled and ClearSlot PersistedLoopSnapshot restore until §6."
todos:
  - id: stage-0-docs
    content: Write 5.1 plan to docs/Plans + latest.md, Architect webhook, CURRENT_WORK Now implementing
    status: in_progress
  - id: stage-1-schema
    content: Add beforePublishedSnapshot/afterPublishedSnapshot + select/capture helpers in TrackUndo
    status: pending
  - id: stage-2-push-apply
    content: Capture at push + select on undo/redo/clear; keep pass toggles and ClearSlot clone
    status: pending
  - id: stage-3-deferred-attach
    content: Attach after by originating UndoEntryId on session-exit publish only (never tip inference)
    status: pending
  - id: stage-4-native
    content: Native fixtures for sync select, deferred attach to exact entry, LIFO/redo clear, ClearSlot
    status: pending
  - id: stage-5-gates
    content: RAM1 capture-serial, native suite, HITL base; check off tasks.md 5.1 + runtime docs
    status: pending
isProject: false
---

# OpenSpec 5.1 — snapshot-identity undo select

**Status:** **SHIPPED** (2026-09-11). Native **1841/1**; RAM1 **6880**; HITL `base`
PASS [`150709`](../../captures/session_20260911_150709.log).
**Change:** `immutable-loop-snapshot-passes` task **5.1**  
**Phase gate:** Phase 4 (Undo, load, and persistence) — first slice only  
**Decisions (pinned):** Q1=1 coexistence; Q2=1 deferred after-attach  
**Architect review:** APPROVED WITH TIGHTENING (exact-originating-entry attach; before-capture boundary; identity-vs-compatibility contract)

## Pins (required before Stage 1)

### Pin A — Deferred attach targets the originating GUS entry

Deferred after-attach must target **the exact GUS entry created by the originating session-exit push**. It must **never** infer the target solely from current cursor/tip + `loopId`/`slotIndex`.

Reuse the existing runtime entry identity: [`UndoEntry::id`](include/GlobalUndoStack.h) (`UndoEntryId`). No new persistent ID and no wire-format field.

At the originating push (when `afterPublishedSnapshot` is left empty for deferred publish), record that entry’s `id` on the pending session-exit publish context (extend [`PendingSnapshotPublishBox`](src/Loop/LoopSnapshotBoxes.h) with a runtime-only `UndoEntryId`, or an equivalent Loop-owned pending-attach token already tied to that queue). On session-exit `storePublishedSnapshot`, `TrackUndo::attachAfterPublishedSnapshot(...)` looks up **that id** in the live stack and fills `afterPublishedSnapshot` only on that entry.

```text
Overdub A stops → push entry A (id=A), after empty → queue publish(token=A)
operation B     → push entry B → tip changes
publish A done  → attach after onto entry id=A (not tip B)
```

No-op when the id is missing, the entry was dropped by `dropRedoBranch`, kind mismatch, or `afterPublishedSnapshot` already set.

### Pin B — `beforePublishedSnapshot` capture boundary

`beforePublishedSnapshot` is the **currently published root immediately before the operation represented by the GUS entry changes the live branch** — the published root the operation started from — not “whatever `publishedSnapshot()` contains at some later point.”

```text
capture beforePublishedSnapshot
        ↓
existing pass mutation / commit
        ↓
publish, if synchronous
        ↓
push GUS entry (after = published, or empty if deferred)
```

Overdub (deferred after):

```text
capture before = current published parent
        ↓
existing overdub stop mutation
        ↓
push GUS entry (after empty, record entry id for attach)
        ↓
session-exit publish
        ↓
attach after to exact originating entry id
```

### Pin C — Identity select vs compatibility restore

**Identity selection establishes the published immutable root; existing pass/ClearSlot restoration remains in place for compatibility and behavior preservation.**

Implementers must not treat pass/ClearSlot restore as redundant, remove it, or reorder it away because identity select exists. Both run: select published root **and** keep today’s Active/Disabled / `restorePassesSnapshot` paths.

## Architecture gate (Phase 4 / task 5.1)

| Question | Answer |
|---|---|
| **Owner module** | `TrackUndo` selects; `Loop::storePublishedSnapshot` remains publish owner; ClearSlot keeps `sharePassesSnapshot` / `restorePassesSnapshot` |
| **Primary invariant** | Live-branch GUS entries retain before/after **published** `LoopSnapshot` roots (before = start root, Pin B); undo/redo/clear **select** those roots while pass/ClearSlot restore remain (Pin C); deferred after attaches by originating `UndoEntryId` only (Pin A); LIFO + `dropRedoBranch` unchanged; stack stays runtime-only (no new wire fields) |
| **Ownership change?** | NO |
| **State transition change?** | YES — snapshot selection on undo/redo/clear (Phase 0 / Phase 4 approved) |
| **Execution bound change?** | NO — cold-path select + ref attach only |
| **Behavior-preserving?** | YES — pass Active/Disabled and ClearSlot deep-clone remain |
| **Reuse** | YES — extend `UndoEntry`, `push*` / `applyUndoEntry` / `applyRedoEntry`, and session-exit publish path |
| **Phase scope** | Task **5.1 only** — not 5.2–5.6 storage/load, not E:/O: provisional select, not §6 retirement |

## Invariant

```text
beforePublishedSnapshot = published root the operation started from (Pin B).
GUS entry retains ImmutableLoopSnapshotRef before/after roots.
Undo/redo: identity select establishes published immutable root;
           pass/ClearSlot restore remains for compatibility (Pin C).
New push → dropRedoBranch (unchanged)
Deferred after attaches to originating UndoEntryId only (Pin A).
```

```mermaid
sequenceDiagram
  participant Push as TrackUndo_push
  participant Loop as Loop_publish
  participant GUS as GlobalUndoStack
  participant Undo as TrackUndo_undo

  Push->>GUS: before equals start published root
  Note over Push,GUS: after empty; record UndoEntryId on pending publish
  Loop->>Loop: storePublishedSnapshot successor
  Loop->>GUS: attach after by originating UndoEntryId
  Undo->>Loop: selectPublishedSnapshot before
  Note over Undo: pass or ClearSlot restore still runs
```

## Naming (new fields)

Do **not** reuse `beforeSnapshot` / `afterSnapshot` (`LoopSnapshotRef` = `PersistedLoopSnapshot` DTO).

Add on [`UndoEntry`](include/GlobalUndoStack.h):

- `ImmutableLoopSnapshotRef beforePublishedSnapshot`
- `ImmutableLoopSnapshotRef afterPublishedSnapshot`

These are the named-root retains from design.md. Identity for freshness remains `ref.get()` / `publishedSnapshotIdentity()`.

## Stages

### Stage 0 — Plan + runtime scope (docs only)

- Write plan to `docs/Plans/immutable_loop_snapshot_5_1_undo_identity_select_refinement.md` and `docs/Plans/latest.md`; Architect webhook.
- Update [`CURRENT_WORK.md`](docs/Runtime/CURRENT_WORK.md) § Now implementing to 5.1.

### Stage 1 — Schema + select helper

- Include [`LoopSnapshot.h`](include/LoopSnapshot.h) from [`GlobalUndoStack.h`](include/GlobalUndoStack.h); add the two fields.
- File-local helpers in [`TrackUndo.cpp`](src/TrackUndo.cpp):
  - `capturePublishedSnapshot(loop)` → `loop.publishedSnapshot()` when present
  - `selectPublishedSnapshot(loop, ref)` → `loop.storePublishedSnapshot(ref)` (or `clearPublishedSnapshot()` when ref empty) then existing invalidate/refresh already used after pass-state undo
- **Do not** change STK1–STK3 / runtime.bundle undo wire layout (5.2+). New refs are RAM-only; load leaves them empty (pass path still restores content).

### Stage 2 — Capture at push + select on apply (sync cases)

Extend existing push/apply in [`TrackUndo.cpp`](src/TrackUndo.cpp); keep all pass disable/enable and ClearSlot clone logic (Pin C).

Capture `beforePublishedSnapshot` at the Pin B boundary (published root the operation started from), **before** the mutation that the GUS entry represents changes the live branch. Call sites that today mutate then push must capture before first (e.g. record/overdub stop commit path in [`TrackCaptureStopCommit.cpp`](src/Track/TrackCaptureStopCommit.cpp); NOTE_EDIT in [`NoteEditSessionLifecycle.cpp`](src/EditManager/NoteEditSessionLifecycle.cpp)).

| Kind | before | after at push |
|---|---|---|
| `RecordPassAdded` | published root immediately before record-stop live-branch change; empty if none (first record) | `publishedSnapshot()` if already the post-op current root; else empty + deferred attach id |
| `OverdubPassAdded` | published parent immediately before overdub-stop live-branch change | empty + record `UndoEntryId` on pending session-exit publish |
| `NoteEditPassClosed` / CC | parent immediately before `tryPublishNoteEditSnapshot` | published after successful publish |
| `ClearSlot` | published root immediately before clear + existing `beforeSnapshot` DTO | empty until undo captures post-clear published (usually empty) |
| `LoopBoundaryChange` | leave published fields empty in 5.1 |

NOTE_EDIT: capture `parent = loop.publishedSnapshot()` **before** `tryPublishNoteEditSnapshot`; pass into `pushNoteEditPassClosed`.

On `applyUndoEntry` / `applyRedoEntry`: run existing pass/ClearSlot restore **and** `selectPublishedSnapshot` (Pin C). If the chosen published ref is empty, do not invent a root (compatibility path remains authoritative for content).

### Stage 3 — Deferred after-attach (session-exit publish)

In [`LoopSnapshotPublish.cpp`](src/Loop/LoopSnapshotPublish.cpp), only on the **session-exit** `storePublishedSnapshot(box.unpublishedCandidate)` path (not wrap provisional publish), call:

`TrackUndo::attachAfterPublishedSnapshot(track, loop, publishedRef, originatingEntryId)`

Behavior (Pin A):

- Look up GUS entry by **`originatingEntryId`** (`UndoEntry::id` recorded at push on the pending publish box).
- If found, `afterPublishedSnapshot` empty, and kind is session-exit Record/Overdub, set after to the just-published ref.
- No-op if id missing, entry gone (e.g. redo-branch drop), wrong kind, or after already set.
- **Never** choose the target by current cursor/tip + loop/slot alone.
- Wrap-only `storePublishedSnapshot(..., wrapTargetRevision)` does **not** attach.

Idempotent when after was already filled at push (sync record path).

### Stage 4 — Native tests

Extend existing suites (no new test architecture):

- Record → overdub publish drain → undo selects **before** identity; redo selects **after**; second commit after undo clears redo (`dropRedoBranch`).
- NOTE_EDIT close: before/after refs set; undo selects before via `storePublishedSnapshot` while pass disable still applies.
- ClearSlot: `beforePublishedSnapshot` retained; undo selects it **and** `restorePassesSnapshot`.
- Deferred exact-entry: push overdub A (`after` empty) → push another GUS entry B → complete A’s session-exit publish → **entry A** (by id) has `afterPublishedSnapshot == published`, entry B unchanged.
- Keep existing Active/Disabled assertions.

### Stage 5 — Device gates + closeout

- `pio run -e teensy41-capture-serial` — RAM1 locals ≥ 4000.
- `pio test -e native` + HITL `base` (undo/redo already in preset) in parallel after upload per HITL-Implementation-Evaluation.
- Check off task **5.1** in [`tasks.md`](openspec/changes/immutable-loop-snapshot-passes/tasks.md); update `CURRENT_WORK` / `PROJECT_STATE` / brief ARCHITECTURE-REVIEW Phase 4 note that 5.1 shipped, 5.2+ remain.

## Explicit non-goals

- 5.2 storage format approval / 5.3–5.6 load migration
- Removing pass toggles or ClearSlot `PersistedLoopSnapshot` (§6)
- E:/O: session undo selecting provisional identities (separate from GUS 5.1)
- Changing GUS footer serialization
- New Manager / scheduler / `RuntimeWorkBudget::admit`
