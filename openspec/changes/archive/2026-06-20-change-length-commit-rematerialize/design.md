# Design — ChangeLength commit rematerialize

**Change:** `change-length-commit-rematerialize`  
**Status:** Proposed (2026-06-19)  
**Evidence:** [BUG.md](./BUG.md)  
**Parent:** [note-edit-modification-session/design.md](../note-edit-modification-session/design.md) (B1 pre-commit + overlap engine — **done**)

---

## Context

**NoteEditSession** overlap work (focus, **overlapNotes**, **movingNoteRange**, `applyNoteEditChange`) is implemented and passes **`pio test -e native`**.

HITL capture **`195830`** shows:

1. Live session keeps lengthened M0 for moves (correct scratch behavior).
2. After **ChangeLength** **saveEdit**, reconstruction inventory still shows short M0.
3. `_verify_change_length_store_rebuild` native parity fails at three checkpoints.
4. `_verify_long_over_short_pitch_restore` reports `m0_home_ok` false; inner A/P0 mostly pass.

So the bug is **not** “overlap engine missing” — it is **persisted edit replay vs live session store** on the Teensy path after length commit.

---

## Goals / Non-Goals

**Goals:**

- Close **Track A**: post-commit inventory matches **ChangeLength** `newEndTick` on device.
- Close **Track B**: overlap round-trip **M0 home** when Track A is green (re-HITL before extra overlap edits).
- Document sign-off: which parent/child bug AC rows this change closes.
- Keep native suite green; add host test only if it reproduces the device gap.

**Non-Goals:**

- Track **C** insert/reorder — parent **7.4**.
- Track **D** P0 `1535` peak — parent **0.1** (investigation only unless blocking Track A).
- AC5/AC6 short-B segment — separate follow-up if still red after A+B.
- Re-architecting **NoteEditSession** or overlap classification.

---

## Decisions

### D1 — Fix order: Track A before Track B

**Choice:** Do not patch M0-home overlap logic until `_verify_change_length_store_rebuild` passes at post-commit snapshot.

**Rationale:** Serial shows short M0 in store after commit; home logic reads wrong geometry. **TBD-1** in proposal: confirm B auto-closes after A.

**Alternative rejected:** Patch move-back restore first — treats symptom while store remains wrong.

### D2 — Investigation surface (primary files)

| Area | File | Question |
|------|------|----------|
| Commit boundary | `EditManager.cpp` | `commitAllPendingNoteEditActions`, `commitEditAction`, `loopMidiEventsFromTakesAndEdits` reload |
| Length UI path | `NoteEditManager.cpp` | Length-mode off → pending **ChangeLength** → commit trigger |
| Replay | `EditApply.cpp` | `ChangeLength` / **NoteRef** + `shortenNoteEnd` / note-off pairing |
| Loop materialize | `Loop.cpp` | `saveEdit`, `rematerializeEditView`, `applyEditsToFlat` |
| Focus baseline | `NoteEditFocus.cpp` | **NoteRef** at **commitBaseline** coords in `buildPreCommitEditChanges` |

### D3 — Debug protocol (HITL)

1. Capture serial during canonical edit baseline run (`teensy41-capture-serial`).
2. Grep: `Edit committed ChangeLength`, next `Final note: pitch=60`, `#CAP` / REVT reconstruction lines.
3. Run baseline script with `--verify-serial-log` on captured log.
4. Sign-off when Track A issues empty; re-run for Track B.

### D4 — Native test policy

- Keep **`test_lengthen_commit_rematerialize_hitl_fixture`** green.
- Add device-parity test **only if** a host-only repro is found (e.g. channel byte, store reload order).
- Do not duplicate overlap round-trip in native if HITL remains the gate for B.

### D5 — Child bug folder relationship

| Folder | Role after this change |
|--------|------------------------|
| `lengthen-overlap-neighbor-restore` | HITL AC checklist — link here for sign-off |
| `note-move-pitch-overlap-flaky` | AC4–AC7 reference — AC4/B tied to Track B; AC7 = Track C |

---

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Fix only display cache, not **Edits** replay | Gate on `_verify_change_length_store_rebuild` native parity, not move_display alone |
| Double `loadFromFlat` masks bug | Compare inventory after first vs second reload in serial |
| Host test cannot reproduce | Rely on HITL + targeted serial logging in firmware |
| Track B still fails after A | Open sub-task in this change (overlap home) — do not expand to Track C |

---

## Sign-off matrix

| Gate | Verifier / AC | Required for archive |
|------|---------------|----------------------|
| Track A | `_verify_change_length_store_rebuild` all issues empty | **Yes** |
| Track B | `_verify_long_over_short_pitch_restore` `m0_home_ok` | **Yes** |
| Track B | `_verify_split_overlap_note_round_trip` mover at home | **Yes** |
| Parent 7.1 | Full edit serial `ok: true` | **Stretch** — may still fail Track C |
| Parent 0.1 | P0 `1535` | **No** (Track D) |
| Parent 7.4 | insert/reorder | **No** (Track C) |

**Archive this change** when Track A + B gates pass and native **105/105** holds. Full edit baseline green is parent **7.1** (may require Track C follow-up change).

---

## Open Questions

1. ~~Does **`test_lengthen_commit_rematerialize_hitl_fixture`** use the same **NoteRef** channel as track 5 HITL?~~ **Closed (§0.3):** channel **5** matches; fixture ticks differ (`8/104` vs `40/136`) and native omits second pitch-60 @616.
2. ~~Is length commit going through **`commitAllPendingNoteEditActions`**?~~ **Closed (§0.2):** yes — serial lines 6020–6022.
3. After Track A fix, does **`inner_a_contained_at_home`** pass without further edits? **Open** on `203729` (A visible via split verifier; log-based contained-at-home check still false).

---

## Migration Plan

1. `/opsx:apply` this change's **tasks.md** (investigation → fix → HITL).
2. On sign-off: update parent [note-edit-modification-session/tasks.md](../note-edit-modification-session/tasks.md) **7.1** partial complete; link patch history in child BUG.md files.
3. `/opsx:archive` this change when sign-off matrix **Yes** rows pass.
