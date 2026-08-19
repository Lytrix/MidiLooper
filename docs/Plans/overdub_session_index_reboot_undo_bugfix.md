# Overdub session index reboot undo — bugfix

**Status:** **HITL PASS** [`003854`](../../captures/session_20260820_003854.log)
**Date:** 2026-08-20  
**Decision:** [DEC-038](../DECISION_LOG.md#dec-038-overdub-wrap-commit-and-session-undo) amendment 2026-08-20; [DEC-035](../DECISION_LOG.md#dec-035-loop-persists-content-only) content metadata  
**Approved:** Approach A — `overdubSessionIndex` follows `editPassIndex` grouping

---

## Problem

Live overdub stop pushes one `OverdubPassAdded` with `passIds` = session wraps (DEC-038.2). After reboot, `rebuildSlotFromLoopContent` → `deriveContentUndoUnits` emits one unit per `OverdubPass`. Sidebar **U:** grows with wrap count.

DEC-035 Stage 3 no longer persists `GlobalUndoStack`. Wrap-session grouping lived only on in-session `overdubSessionPassIds_`.

## Invariant

One user overdub session is one global undo unit before and after reboot. Cards without `OSI1` stay one unit per pass (`overdubSessionIndex == 0`).

## Owners

| Piece | Owner |
|-------|--------|
| Allocate / stamp | `Loop::openOverdubSession`, `Loop::sealCapture`, `Loop::commitPendingCapturePass` |
| Derive | `deriveContentUndoUnits` / `buildContentUndoEntries` |
| Persist | `StorageLoopIo` additive `OSI1` tail after `GEO1` |

No new Manager. Undo kind stays `OverdubPassAdded`.

## Field

`OverdubPass.overdubSessionIndex` (`uint8_t`). Same role as `EditPass.editPassIndex`.

| Value | Meaning |
|-------|---------|
| `0` (`kUngroupedOverdubSessionIndex`) | Legacy / missing tail — do not group |
| `1..255` | Wraps that share this index are one undo unit |

`openOverdubSession` assigns `nextOverdubSessionIndex_` (starts at 1). Load recomputes next as `max(existing)+1` (or 1). Do not persist the allocator.

## Persist

Additive `OSI1` after `GEO1` (same peek probe as geometry). Payload: count + (`passId`, `overdubSessionIndex`) per overdub pass. No capture-header layout change.

## Tests

- `test_loop_content_history`: same index → one unit `passIds`; index `0` → one unit each; two indexes → two units
- `test_storage_loop_io`: `OSI1` roundtrip; missing tail loads `0`
- `test_overdub_source_view`: two wraps in one session share a non-zero index; next session gets a new index

## Does not

- Persist GUS / `LoopUndoHistory`
- Group consecutive overdubs without a shared index
- Change live `pushOverdubSessionOnStop`
- Add a `session-id` noun

---

## Pre-implementation review

### Ready

- Live stop already writes `passIds` via `pushOverdubSessionOnStop`
- `deriveContentUndoUnits` already groups note-edit rows by `editPassIndex` and companions by `255`
- `GEO1` peek/read/skip is the persist pattern
- `deepCloneOverdubPass` copies the struct

### Resolved

| Topic | Decision |
|-------|----------|
| Grouping key | `overdubSessionIndex` (user 2026-08-20) |
| Ungrouped sentinel | `0` so legacy cards stay per-pass |
| Persist | `OSI1` tail, not pass-header insert |
| Allocator | Recompute on load; not a snapshot header field |

### Open before coding

None.

### Proceed?

YES

---

## HITL PASS [`003854`](../../captures/session_20260820_003854.log)

Track 6, 4-bar loop (3072). Record stop `DISP` **14**. Overdub open `undo_entries=179`. Three wrap commits (38.816 / 46.827 / 54.836) then stop (61.576). Stopped `DISP` **112**.

Reboot. First undo: `Undo (entries=2)` `kind=1` `Overdub undone`, `undo_count=179`. `DISP` **112 → 14**. Redo `kind=1` restores **112**.

Four sealed wraps would have been four `OverdubPassAdded` units without `overdubSessionIndex`. After reboot they are one unit.
