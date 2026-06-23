# Handover — naming drift cleanup and scoped EditPass model

**Date:** 2026-06-23  
**Branch:** `refactor/timeline-data-model`  
**Status:** planning complete; implementation not started.

This handoff is for continuing the naming cleanup in a new chat without re-opening the full discussion.

---

## Current State

Two tracks are now separated:

1. **Naming drift remediation** — code/docs rename cleanup, no behavior change.
2. **Scoped EditPass model** — real OpenSpec data-model change for future note / control-change / audio edit rows.

Do **not** combine them into one implementation pass.

---

## Track 1 — Naming Drift Remediation

**Plan:** `/Users/eelkejager/.cursor/plans/naming_drift_remediation_248456bf.plan.md`

This is ordinary code cleanup, not a new OpenSpec change.

### Locked Vocabulary

| Concept | Use |
|---------|-----|
| Active capture pass merge | **merge** |
| Full pass replay | **materialize** |
| Fast RAM / malloc heap | **internal heap** in code, **fast RAM** as doc synonym |
| External RAM / PSRAM pool | **external memory pool** in code, **external RAM** as doc synonym |
| Serial capture tokens | frozen serial capture tokens (`#CAP`, `PERS`, `REVT`, etc.) |

### Do First

1. Extend `.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc`.
2. Add README glossary notes under loop storage vocabulary:
   - memory tiers: fast RAM ↔ internal heap; external RAM ↔ external memory pool
   - merge vs materialize
   - current edit vocabulary vs future scoped EditPass OpenSpec

### Firmware Rename Order

Run `pio test -e native` after each logical group.

| Current | Target |
|---------|--------|
| `flattenActiveCapturePasses` | `mergeActiveCapturePasses` |
| `appendFlattenedChunkIds` | `appendChunkRefEvents` |
| `materializeToFlat` | `materializeToEventVector` |
| `editFlat_` | `passesMaterializedStore_` or equivalent approved cache name |
| `discardEditFlatMaterialization` | `discardPassesMaterializedCache` |
| `buildLiveEventView` | `mergeMaterializedPassesWithCapture` or final approved action+scope name |

### Memory Rename Order

| Current | Target |
|---------|--------|
| `ExtMemAllocator` | `InternalHeapFirstAllocator` |
| `PsramFirstAllocator` | `ExternalMemoryFirstAllocator` |
| `isInPsram` | `isInExternalMemoryPool` |
| `isPsramAvailable` | `isExternalMemoryPoolAvailable` |
| `getPsram*Bytes` | `getExternalMemoryPool*Bytes` |
| `getFreeHeap` | `getInternalHeapFreeBytes` |
| `RAM2_SAFETY_FLOOR_BYTES` | `INTERNAL_HEAP_SAFETY_FLOOR_BYTES` |
| `hasRam2HeadroomForNonCriticalWork` | `hasInternalHeapHeadroomForNonCriticalWork` |

Do **not** rename Teensy platform API symbols such as `extmem_malloc`, `extmem_free`, `EXTMEM_PSRAM_START`, or `external_psram_size`; keep them behind platform shims/comments.

### Docs After Code

Update only active README-linked docs:

- `README.md`
- `docs/README.md`
- `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`
- `docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md`
- `docs/plans/record_overdub_memory_display_timeline_enhancement.md`

Do not sweep historical OpenSpec archives or old handoff plans for wording-only drift.

---

## Track 2 — Scoped EditPass Model

**OpenSpec:** `openspec/changes/scoped-edit-pass-model/`  
**Validation:** `openspec validate scoped-edit-pass-model` passed.  
**Artifacts complete:** `proposal.md`, `design.md`, `specs/timeline-passes/spec.md`, `tasks.md`.

This is **not** part of the flat/memory naming cleanup. Apply later with `/opsx:apply` when ready to change the edit pass data model.

### Locked Decisions

Future storage keeps one ordered `editPasses[]` array on `LoopPasses`. Each row is a scoped **EditPass**:

```cpp
struct EditPass {
  PassId id;
  EditSessionType sessionType;   // Note | ControlChange | future Audio
  uint8_t editPassIndex;         // batch index scoped by sessionType
  EditPassState state;
  EditActionType actionType;     // Create | Update | Delete
  EditPropertyType propertyType; // None for Create/Delete
};
```

### Vocabulary

| Layer | Name | Meaning |
|-------|------|---------|
| Scope | `EditSessionType` | note vs control-change vs future audio |
| Action | `EditActionType` | CRUD-style `Create`, `Update`, `Delete` |
| Property | `EditPropertyType` | stored editable field: pitch, length, tick, value |
| Target | `NoteRef`, future `ControlChangeRef` | stable object identity |

Use **Property**, not **Parameter**. In this repo, `parameter` already means controller/action config (`withParameter(...)`).

### Important Boundaries

- Current `EditChange` / `EditChangeType` is shipped legacy note-edit storage.
- Do not extend `EditChange` with CC fields.
- Do not introduce a broad standalone `Edit` container.
- Do not split record/overdub capture: CC is already captured in the same time-ordered MIDI stream as notes.
- Undo/redo is separate from stored edit actions:
  - undo disables pass rows
  - redo re-enables pass rows
  - undo does not append `EditActionType::Delete`

### Open Questions Captured In Design

- Whether note move stores `StartTick` + `EndTick` or a scoped note-range payload.
- Whether `EditPropertyType::Length` stores user-facing length or maps directly to `EndTick`.
- Exact SD strategy: **`scoped-edit-pass-payload`** — **STORAGE_VERSION** **5**, reject v1–v4, no
  migration.

---

## Recommended New Chat Prompt

Use this to continue:

```text
Continue from docs/plans/naming_drift_scoped_edit_pass_handoff.md.

Do Track 1 only: naming drift remediation. Do not apply scoped-edit-pass-model yet.

Start with rules + README glossary, then rename flat/flatten/materialize identifiers, then memory naming.
Run pio test -e native after each firmware rename group.
```

If you want to work on the OpenSpec instead:

```text
Continue from docs/plans/naming_drift_scoped_edit_pass_handoff.md.

Apply openspec/changes/scoped-edit-pass-model with /opsx:apply.
Do not include flat/memory naming cleanup in that change.
```

