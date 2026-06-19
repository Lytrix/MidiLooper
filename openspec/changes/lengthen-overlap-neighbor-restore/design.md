# Design — overlap neighbor restore (lengthened moving note)

**Change:** `lengthen-overlap-neighbor-restore`  
**Status:** Evidence + AC only — implementation in [note-edit-modification-session](../note-edit-modification-session/design.md); **Track A/B fix** signed off in [change-length-commit-rematerialize](../change-length-commit-rematerialize/) (capture `203729`).  
**Bug spec:** [BUG.md](./BUG.md)  
**Parent:** [note-edit-modification-session/design.md](../note-edit-modification-session/design.md) (A1 + B1 + pre-commit resolve)  
**Vocabulary:** moving note + **overlap note** / **overlapNotes** (legacy folder name; do not add **neighbor**, **ledger**, or **deletedNotes** as new implementation terms).

---

## Context

User report matches serial trace `host_midi_automation_edit_baseline_20260619_144458`:

```text
Move M0 (lengthened) over P0 (inner neighbor)
  → contained delete (hidden in store); shared release @ mover start
Pitch M0 60→67
  → restore P0 from overlapNotes / legacy deletedNotes scratch (NOT Take)
Move past / return home
```

The **reappear on pitch** behavior is **by design today** in `NoteEditManager` (restore neighbor notes whose pitch conflicts with new mover pitch). The bug is **downstream**: neighbors are not **re-tracked** when hidden again, and **rematerialize** does not match live RAM — consistent with **no Take/session baseline** for hidden neighbors.

---

## Goals

1. **One neighbor ledger** for the active note-edit move session: hide, restore-on-uncover, restore-on-pitch-lane-change — without losing geometry.
2. **Baseline fidelity:** neighbor gate (start/end ticks) MUST match **recorded fixture** after round-trip, recoverable without ad-hoc full flatten.
3. **Take boundary:** Record Take chunks **read-only**; baseline = materialized view at session open + EditChanges (m8-edit path).
4. **Rematerialize parity:** `applyEdits` / session store replay MUST match live overlap session end state.

## Non-Goals

- Mutating Take storage on restore.
- Replacing entire `note-move-pitch-overlap-flaky` engine in one PR (coordinate or land snapshot contract first).

---

## Decision 1 — Neighbor restore sources (reject Take write, allow Take read)

| Source | Role today | Target role |
|--------|------------|-------------|
| `movingNote.deletedNotes` | Scratch hide/restore | **Active session ledger** — authoritative while `movingNote.active` |
| Pitch handler one-off restore | Clears ledger entry on lane change | **Re-classify** neighbor: **visible** but still **inner** if under span |
| Record Take | Unread on restore | **Read-only baseline** for `NoteRef` lookup when ledger entry missing or corrupt |
| `applyEdits` replay | Post-commit rematerialize | MUST use same overlap rules or replay EditChanges that encode neighbor state |

**User hypothesis (“not restoring from recording take”):** **Confirmed partial** — pitch restore @ 65.818s uses `deletedNotes` only. **Not confirmed** that Take *should* be the primary restore path during live edit; preferred fix is **session baseline snapshot** (see Decision 2) with optional Take read for missing neighbors.

---

## Decision 2 — Session baseline snapshot

**Locked in parent** — baseline at **fader-1 select** from `NoteEditSession.store`; single
**neighborLedger** on **NoteEditFocus** (replaces `deletedNotes` / scratch vectors).

Pre-commit resolve (**B1**): materialize **only** impacted **OverlapNote** entries in
**overlapNotes** before **saveEdit**; **EditChange** list includes **only** notes that differ
from baseline.

---

## Decision 3 — Shared-release @ mover start (tick 496 case)

When moving note note-on lands on inner neighbor note-off tick:

- **Do not** steal inner note-off for LIFO pairing across different pitches (A@67 off @496 vs M0@60 on @496).
- Classify in shared overlap engine: **boundary co-release** — inner neighbor keeps off; mover gets distinct on.

Investigation task: add native test with A@403–496, M0 lengthened move start→496.

---

## Decision 4 — Pitch vs move restore symmetry

From flaky design clarification — **restore pass after every geometry change** (position **and** pitch):

```text
applyGeometryChange (position | pitch)
  → restoreNeighborsNoLongerOverlapping()
  → applyOverlaps()
  → update movingNote.last* + hiddenNeighbors / inner tracking
```

Pitch path MUST NOT be the only step that restores `deletedNotes` without re-evaluating **inner** neighbors on the next position move.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **Yes** — neighbor baseline + hidden/inner tracking |
| State transition change? | **Yes** — pitch restore no longer drops inner-span tracking |
| Take write? | **No** |
| Hot-path full flatten? | **No** — baseline from snapshot or `readStore()` window |

Coordinate with **m8-edit** NoteEditSession so baseline is O(n notes) at select, not full loop flatten.

---

## Validation

| Layer | Criterion |
|-------|-----------|
| Native | New test: lengthen → move over P0 → pitch → move back; P0 gate intact |
| Native | Existing `test_lengthen_after_move_keeps_p0_fixture_gate` + rematerialize tests green |
| HITL | `host_midi_automation_edit_baseline` overlap round-trip: `m0_home_ok`, native parity snapshots |
| Serial | No restore-from-scratch without baseline copy log after contained delete |

---

## Sequencing with `note-move-pitch-overlap-flaky`

1. Land **session snapshot + sessionEnd on length edit** (flaky tasks 1–2).
2. Add **hiddenNeighbors / pitchVisibleInnerNeighbors** (this change).
3. Route pitch through shared `applyPitchChange` (flaky task 2) with new ledger rules.
4. HITL + native matrix.

---

## Open questions

- Whether pitch-visible inner neighbors should render dimmed on display (UX — TBD, out of scope).
