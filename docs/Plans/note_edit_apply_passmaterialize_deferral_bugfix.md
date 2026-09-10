# NOTE_EDIT apply must not force PassMaterialize (bugfix)

**Status:** RC-A **SHIPPED** (2026-09-10). Compose HITL PASS 14/14
[`142119`](../../captures/note_edit_overdub_compose_20260910_142119_serial.log)
(`resolve` 8123 µs, `reconstruct` 2837 µs). Native PASS.  
**Kind:** Phase 1 runtime-policy / ownership-boundary bugfix. **Not** a scheduler.  
**Parent / prior:** [`geom_apply_reconstruct_cost_bugfix.md`](geom_apply_reconstruct_cost_bugfix.md) (RC1 preview≠committed-base key — necessary, insufficient).  
**Evidence:** [`124645`](../../captures/note_edit_overdub_compose_20260910_124645_serial.log), [`135418`](../../captures/note_edit_overdub_compose_20260910_135418_serial.log).

## Device evidence (shipped)

| Gate | Result |
|---|---|
| F1 (no PassMaterialize / reconstruct class) | PASS — max reconstruct **2837** µs, resolve **8123** µs; no wrap coincidentally between apply/reconstruct on this capture; selector cannot return PassMaterialize |
| F2 | PASS — compose paint held (machine 14/14; live notes stay on roll) |
| F3 | PASS — same-pitch hold + move verified by HITL cases |
| F4 | PASS — no scheduler added |
| F5 | PASS — native selector tests |

## What this RC is (and is not)

| This RC | Not this RC |
|---|---|
| Establish: Runtime Request must never promote stale representation into content-sized reconstruction | A runtime scheduler |
| Remove PassMaterialize from NOTE_EDIT committed-base **selection** | Admission / `RuntimeWorkBudget::admit` |
| Make stale VisualCache + dirty debt a **legal** request-side state | Teaching GEOM_APPLY “which representation is correct forever” |
| Leave catch-up with existing idle maintenance | New `*Scheduler` / deferred-apply FSM |

Later phases (out of scope here):

1. **Phase 1 (this)** — invariant: request consumes available representation; never force rematerialize.
2. **Phase 2** — make deferred debt explicit on the existing idle owner (dirty bars / snapshot catch-up).
3. **Phase 3** — only then harden admission/budgeting **on that deferred owner**, not inside GEOM_APPLY.

A scheduler’s job is **when** expensive representation work may run — not which base GEOM_APPLY picks.

## Problem

In-edit overdub `GEOM_APPLY` still hits ~30 ms `reconstruct` when an **overdub wrap** lands between apply and `finalReconstructAndSelect`:

1. Wrap → `commitCapturePass` advances `playbackRevision`.
2. `publishedSnapshotIsCurrent()` becomes false.
3. `selectNoteEditCommittedBaseKind` returns **`PassMaterialize`**.
4. Apply path runs `passes.materializeToEventVector` + `reconstructDisplayNotes` on the MIDI/apply cadence.

```text
❌ today:
snapshot stale → PassMaterialize → 30 ms rebuild → GEOM_APPLY

✅ target:
wrap → revision changes → snapshot not current
  → apply proceeds from existing VisualCache (may be stale)
  → dirty regions remain dirty (maintenance debt)
  → idle maintenance catches them up
```

| Authority | Rule |
|---|---|
| [RuntimeArchitecture.md](../Authority/Architecture/RuntimeArchitecture.md) | Stale representation must not trigger a content-sized full-loop rebuild on the request |
| [Display.md](../Authority/Architecture/Display.md) | PLAYING/OVERDUBBING **defers** full rebuild; wrap keeps `visualCache.notes`; idle fills dirty bars |
| Control-plane RC2 | `GEOM_APPLY` is notes × NOTE_EDIT interval — not loop rematerialize |

RC1 (preview out of committed-base key) fixes preview-bump reproject. It does **not** stop wrap-stale → PassMaterialize.

## Invariant (wording that must hold)

> **VisualCache is the bounded runtime representation available to the request; dirty state records what maintenance still owes.**

It is **not**: “VisualCache is the fully correct committed representation.”

Stale VisualCache is a **legal runtime state** for GEOM_APPLY / NOTE_EDIT paint. F2 is the architectural proof of that legality, not only a UI regression check.

```text
Runtime Request consumes an already-available representation;
it may not create a content-sized representation.
  ├─ apply / edit geometry
  ├─ playback
  └─ capture / overdub
        → if representation available: consume it
        → if not: do NOT rebuild; record/retain debt
        → deferred idle owner catches up

Deferred maintenance (content-sized, existing idle owner)
  ├─ snapshot construction / publish catch-up
  ├─ dirty-bar regeneration
  └─ representation catch-up
```

Hard constraints (implementation): change only selector/projection for RC-A; never invoke PassMaterialize/`materializeToEventVector` from stale-snapshot GEOM_APPLY; no scheduler/admission/FSM/new owner; no wrap/publish/idle transition edits; F2/F3 fail → stop and report (never restore PassMaterialize); 10/20 ms diagnostic only.

## Code proof — wrap retains usable notes

[`Loop::markAffectedDisplayCacheRanges`](../../src/Loop/LoopVisualCache.cpp) (called from `Track::commitOverdubWrap` after seal):

- Sets `visualCacheDirty = true`.
- Marks affected bars dirty (`markBarDirty` / `markTickSpanDirty`).
- Iterates `visualCache.notes` for companion targets — **does not clear** the vector.
- Emits `stale_range`.

Early exits call [`markDisplayCachesStale`](../../src/Loop/LoopVisualCache.cpp): marks **all** bars dirty; **does not** `visualCache.notes.clear()`.

Wholesale clear is only [`invalidateDisplayCaches`](../../src/Loop/LoopVisualCache.cpp) — not on the normal wrap path.

**Invariant for RC-A:** wrap → revision changes → cache dirty → **notes remain usable** → dirty bars identify incomplete portions → request must not assume cache == fully current.

## Debugging boundary

```
RC1 preview key / bump without full invalidate     ← trust when no wrap
wrap → !publishedSnapshotIsCurrent
  → selectNoteEditCommittedBaseKind PassMaterialize  ← remove from result space
  → VisualCache + dirty debt → idle catch-up         ← this RC
```

Do not reopen resolver analyze/setup. Do not implement a scheduler. Do not treat µs ceilings as architectural permission.

## RC-A — Phase 1 only

### Selector result space

[`selectNoteEditCommittedBaseKind`](../../include/Utils/DisplayWindowUtils.h) valid results become **only**:

| Condition | Result |
|---|---|
| dirty ∧ current | **Snapshot** |
| otherwise | **VisualCache** |

**Remove `PassMaterialize` from this selector’s valid result space** — not “unreachable dormant arm.” A latent `case PassMaterialize:` in the runtime-request path invites resurrection.

| Today | After |
|---|---|
| dirty ∧ ¬current → PassMaterialize | dirty ∧ ¬current → **VisualCache** |

If some **other** owner still needs full pass rematerialize (commit/oracle), that stays a **different explicit call site** — not a GEOM_APPLY / NOTE_EDIT display-base option.

### Switch in `ensureNoteEditDisplayProjectionCachesBuilt`

- Keep Snapshot and VisualCache arms.
- **Delete** the PassMaterialize arm from this function’s switch (and enum use from this selector). Enum value may remain only if other TUs still need the name for docs/tests; this selector must not return it.
- Null fallthrough to `getVisualNotesForSlot` stays.

### Framing

Do **not** document this as “VisualCache is correct committed content.” Document: request consumes available bounded representation; dirty records debt for idle.

If F2 fails (empty visual / blank paint), stop — follow-up last-published-snapshot handoff (`004351` family). **Do not** restore PassMaterialize in the same commit.

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | **NO** — same owners; removes illegal work from the request |
| State-transition change? | **NO** — wrap/publish/idle unchanged |
| Unbounded MIDI-interval owner? | **NO** — removes content-sized materialize from apply |

## Explicit non-goals

- Runtime scheduler, interval reservation, `RuntimeWorkBudget::admit`
- Turning 10 ms / 20 ms into scheduler or “acceptable operation” budgets
- Forcing snapshot publish on the wrap/apply stack
- Deleting `materializeToEventVector` globally (other owners may still call it explicitly)
- OLED STOPPED `resolveDisplayNotes(loop.passes)` fallback in this RC
- Phase 2/3 idle admission hardening

## Falsification

Ship only if all pass. Any fail falsifies RC-A.

### F1 — Request must not run content-sized reconstruction (primary / architectural)

On `note_edit_overdub_compose` / STIM-NE-OD-COMPOSE-02, any OVERDUBBING `GEOM_APPLY` with `Overdub wrap committed` between `phase,apply` and `phase,reconstruct`:

| Signal | Pass | Fail (falsifies) |
|---|---|---|
| PassMaterialize / `materializeToEventVector` on that reconstruct | **Absent** | Present |
| `DIAG,lcr,pairrows,at=passes` in that window | Absent | Present |
| Reconstruct character | Bounded / non-content-sized (reproject / select class) | Full-loop flatten class (~30 ms historically) |

**Timing is diagnostic, not architectural permission.** Existing HITL ceilings (`resolve ≤ 20000`, `reconstruct ≤ 10000`) remain useful **falsification measurements** so the gate still fails the known bad capture. They must **not** be read as: “9.5 ms content-sized rebuild on apply is allowed.” A content-sized rebuild on GEOM_APPLY falsifies even if it fits under the ceiling.

### F2 — Stale VisualCache is a legal runtime state (architectural)

After the same wrap+apply, before idle completes dirty bars:

| Signal | Pass | Fail |
|---|---|---|
| Committed-side paint / `DNTE` | Non-empty; prior notes remain (may lag sealed wrap bars) | Empty / collapse to ~0 committed notes |
| `DISP` / `draw_src` frame count | Stays in prior band (± live overlay) | Collapse that only recovers after a long stall |

F2 proves stale VisualCache is legal for the request — not that the cache is fully current.

### F3 — Edit identity / geometry still resolve (architectural correctness)

Elevate above “compose stimulus only.” Prove:

```text
committed snapshot revision changed
  → old visual representation retained
  → edit identity / geometry still resolves
```

not:

```text
snapshot changed → old cache retained → edit refers to non-corresponding objects → no-op
```

| Signal | Pass | Fail |
|---|---|---|
| Same-pitch hold | Hide or Shorten logged | Hold miss with no harness fault |
| Move apply after wrap | `EditSessionAction` type=3 (or expected geometry) applies | Silent no-op / missing mover apply |

### F4 — Idle owns catch-up (no scheduler smuggled in)

| Signal | Pass | Fail |
|---|---|---|
| After wrap | `snapshot_idle` / visual dirty-bar fill continues on existing idle owner | Apply path full-flattens to “catch up” |
| New types | None | `*Scheduler`, admit wrapper, deferred-apply FSM for this paint |

### F5 — Native selector contract

| Fixture | Pass | Fail |
|---|---|---|
| dirty ∧ ¬current | `VisualCache` | `PassMaterialize` or any third kind from this selector |
| dirty ∧ current | `Snapshot` | otherwise |
| ¬dirty | `VisualCache` | `PassMaterialize` |

Update [`test_select_note_edit_committed_base_kind_capture_vs_playing`](../../test/test_display_window_utils/test_display_window_utils.cpp). Assert the selector’s result space is only Snapshot | VisualCache.

## Observability (minimal)

Prefer existing CAPs. Optional `SESSION_CAPTURE` on rebuild:

`#CAP,…,GEOM_APPLY,base_kind,<Snapshot|VisualCache>`

Falsify if `PassMaterialize` (or materialize-class work) appears on in-edit overdub apply after this RC.

## Tests

1. Native: F5 selector + RC1 projection reuse still green.
2. Device: `--preset note_edit_overdub_compose` only.
3. Capture-serial RAM1 locals ≥ 4000; upload+HITL without asking.
4. Full `pio test -e native` in parallel with HITL.

## Doc closeout (on ship)

- Mark shipped with capture ids + F1–F5 (note F1/F2/F3 as architectural).
- Update [`geom_apply_reconstruct_cost_bugfix.md`](geom_apply_reconstruct_cost_bugfix.md): RC1 insufficient alone; RC-A closes wrap-forced flatten.
- Update [`Display.md`](../Authority/Architecture/Display.md) NOTE_EDIT sentence: dirty ∧ ¬current uses VisualCache handoff (stale legal); rematerialize-when-dirty on the request is forbidden.
- [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) / 4.5 when compose PASS.

## Sequencing

| Stage | Role |
|---|---|
| RC1 preview key | In tree |
| **RC-A Phase 1** | This commit — ownership boundary only |
| Phase 2 idle debt clarity | Later, existing idle owner |
| Phase 3 admission on idle | Later — not GEOM_APPLY |
| Last-published-snapshot NOTE_EDIT handoff | Only if F2 fails |

## Pre-implementation review

### Ready

- Selector + switch owners confirmed.
- Wrap note retention verified in `markAffectedDisplayCacheRanges` / `markDisplayCachesStale` (code).

### Resolved (review 2026-09-10)

| Topic | Decision |
|---|---|
| Framing | Stale VisualCache = legal request state + dirty debt; not “correct committed base” |
| PassMaterialize in selector | **Removed from valid result space** (no dormant arm) |
| 10/20 ms | Falsification measurements only — not budgets / permission |
| Wrap cache | Notes retained; dirty ranges marked — verified |
| Scheduler | **Out of scope** — do not implement |

### Open before coding

None for RC-A. F2 fail → stop for last-snapshot handoff design; never resurrect PassMaterialize on this selector.
