## Context

NOTE_EDIT currently treats `EditSession.store` as the live MIDI event buffer and uses `baselineMap`, `NoteEditFocus`, `overlapNotes`, `changedOverlapNoteIds`, display ordering, and live-store scans to reconstruct current editable note geometry.

That model violates the ownership intent for stable `NoteId` editing: selection is `NoteId`-based, but current geometry is not owned by a single `NoteId`-keyed state. The same-pitch overlap captures show the failure mode:

- `session_20260807_021939`: the first moved same-pitch note is applied as `noteId=17` at current start 1392; later display ids reorder while the second moved note applies as `noteId=25`.
- `session_20260807_021022`: stale committed-baseline geometry for note 17 at start 3600 caused overlap restore/shorten/hide actions after note 17 had moved to 1392.

The current stopgap excludes session-moved targets and skips overlap actions for them. That prevents stale-baseline damage, but it also prevents correct current-span overlap edits against the first moved note.

Authority checked before this design:

- `docs/00-authority/ARCHITECTURE_RULES.md`
- `docs/00-authority/NAMING.md`
- `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`
- `docs/Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md`
- `openspec/specs/edit-session-action-geometry/spec.md`
- `openspec/specs/note-edit-modification-session/spec.md`
- `openspec/specs/note-edit-session-undo/spec.md`

Reference plan: `docs/plans/note_edit_session_current_state_refinement.md`.

## Goals / Non-Goals

**Goals:**

- Add one NOTE_EDIT current editable state owner inside `EditSession`, keyed by stable `NoteId`.
- Keep `baselineMap` as committed transaction baseline only.
- Make `EditSession.store` the canonical, deterministic, intentionally lossy event projection of current state.
- Keep `EditorSelection` as identity/bracket only; display order and `selectedIdx` remain derived.
- Migrate readers and writers in a staged path with parity checks.
- Derive `editPass` commit rows from current state compared to committed baseline.
- Restore current state and projected store together for session undo/redo.

**Non-Goals:**

- No new top-level Manager.
- No return to `selectedIdx` as identity.
- No SD format change.
- No JamRecorder, scene, D13 arrangement capture, or persistence/overlay feature scope.
- No full-loop validate on capture stop or NOTE_EDIT hot paths.

## Decisions

### Decision: Current editable note state is owned by `EditSession`

Add a current-state owner under existing `EditSession`; do not create a parallel Manager.

Proposed names:

- `NoteEditCurrentState`: collection/owner concept.
- `NoteEditCurrentNoteState`: one row keyed by `NoteId`.
- `NoteEditPresenceType`: row visibility in current state.

The row stores only non-derived authority:

```cpp
enum class NoteEditPresenceType : uint8_t {
  Visible,
  Hidden,
  Deleted,
  Added,
};

struct NoteEditCurrentNoteState {
  NoteId noteId = kInvalidNoteId;
  NoteBaseline committedSpan{};
  NoteBaseline currentSpan{};
  NoteEditPresenceType presence = NoteEditPresenceType::Visible;
};
```

Rationale:

- `State` matches `docs/00-authority/NAMING.md` for mutable ownership.
- `GeometryIndex` was rejected because it reads as a cache and narrows future note edit scope.
- `committedSpan`, `currentSpan`, and `presence` are sufficient; moved, resized, pitch-changed, hidden, restored, and delete/create diffs are derived.

Alternatives considered:

- Keep `EditSession.store` authoritative and add a derived index. Rejected because hidden/deleted/current geometry would still be reconstructed from projected events and baseline heuristics.
- Add a separate Manager. Rejected by ownership rules; `EditSession` already owns NOTE_EDIT RAM scope.

### Decision: `EditSession.store` is canonical event projection

During NOTE_EDIT, `EditSession.store` becomes the canonical event projection of `NoteEditCurrentState`.

Projection direction:

```text
NoteEditCurrentState
        |
        v
project current rows to MIDI events
        |
        v
EditSession.store
```

Projection is intentionally lossy:

- `Visible` rows project to note-on/note-off event pairs.
- `Added` rows project to note-on/note-off event pairs.
- `Hidden` rows do not project.
- `Deleted` rows do not project.

Reverse construction from projected events is allowed only at boundaries: session open/reopen, workspace rematerialize, legacy undo/redo migration, and debug/parity probes.

Rationale:

- Playback preview and existing MIDI-event APIs still need an event representation.
- Hidden/deleted rows cannot be encoded safely in projected MIDI without creating a second state channel.

### Decision: Split access before behavior changes

Before changing geometry behavior, split current NOTE_EDIT store access into:

- Read/projection access for display, preview, serialization, and parity.
- Owner mutation APIs for add, delete, move, length, pitch, hide, shorten, and restore.
- Temporary compatibility writes with named removal triggers.

Rationale:

- Current code exposes mutable projected events through `sessionMidiEvents()` and `track.editAwareMidiEvents()`.
- Ownership transfer is unsafe until direct projected-store writers are converted or isolated.

### Decision: Migrate readers before flipping commit authority

Reader migration order:

1. Display/selectable inventory and selected-index derivation.
2. Focus rebuild and driver validation.
3. Geometry resolver and interaction analysis.
4. Action builder.
5. Commit/bake diff builders.
6. Undo/redo landing.

Rationale:

- The original failure is a reader problem: stale baseline/current geometry was read from the wrong source before actions were built.
- Keeping store-diff builders as parity during migration gives a measurable safety net.

### Decision: Commit rows come from current state

`commitAllPendingNoteEditActions` and note edit pass bake paths must derive rows from current state compared to committed baseline.

`Loop::saveNoteEditPass` remains the publication API and `LoopPasses` remains the canonical committed timeline.

Rationale:

- Hidden/deleted rows may not exist in projected events.
- Current state has the complete row needed to emit update/delete/create rows.

### Decision: Undo restores current state and projection as one logical snapshot

Session undo entries must restore current state and projected store together. Restoring one without the other is invalid.

Rationale:

- `EditSession.store` is a projection. If undo restores only projected events, hidden/deleted row state is lost.
- If undo restores only current state, preview/display/event APIs can observe stale projection.

### Decision: Memory follows NOTE_EDIT external-memory-first routing

Current-state row collections must use the same external-memory-first routing as `NoteEditFocus` maps and session undo structures.

Rationale:

- Current state can scale with loop note count.
- Hot fader and geometry paths must not walk the external memory pool for diagnostics.

## Risks / Trade-offs

- **Risk:** Duplicated ownership during migration → **Mitigation:** compatibility gate: no production NOTE_EDIT path mutates projected store directly except the projection owner; all exceptions need removal triggers.
- **Risk:** Commit output diverges from legacy store diff → **Mitigation:** keep store-diff builders as parity checks until fixtures cover hidden, deleted, added, moved, pitch, and length rows.
- **Risk:** Undo entry size grows with current-state rows → **Mitigation:** use external-memory-first containers and prove scoped/delta snapshots before relying on trimmed snapshots.
- **Risk:** Display order regressions during reader migration → **Mitigation:** selection remains `NoteId` + bracket; add selection reorder fixtures before removing event projection compatibility.
- **Risk:** Projection loses hidden/deleted rows by design → **Mitigation:** current state is the only authority for hidden/deleted rows; debug visibility must dump current state, not encode rows in MIDI projection.

## Migration Plan

1. Record ownership transfer in `docs/DECISION_LOG.md` before firmware implementation.
2. Add current-state types and read-only build/verify helpers.
3. Split API usage into projection reads, owner mutation calls, and temporary compatibility writes.
4. Build current state from `EditSession.store` at session open/reopen and verify visible-row projection parity.
5. Migrate display, selection, and focus readers to current state.
6. Migrate resolver, interaction, action builder, and apply paths to current state.
7. Convert direct add/delete/move/length/pitch projected-store writes to current-state mutations.
8. Convert dependent fader closure normalization into a projection-owner operation.
9. Migrate undo/redo snapshots to restore current state and projected store together.
10. Migrate commit/bake row generation to current state diffs; keep legacy store-diff parity.
11. Remove `sessionMovedNoteSpans`, session-moved skip guards, and remaining live-store geometry authority.
12. Run `pio test -e native`, firmware build with `teensy41-capture-serial`, and user-approved HITL edit retest.

Rollback strategy:

- Each migration phase keeps projection/store parity assertions until the next phase passes native tests.
- If a phase regresses, retain the current projection compatibility path and do not remove legacy readers/writers for that phase.

## Open Questions

- Final code identifier approval: `NoteEditCurrentState`, `NoteEditCurrentNoteState`, `NoteEditPresenceType`.
- Session undo payload shape: full current-state snapshot versus scoped/delta rows.
- Whether the ownership transfer should ship in one OpenSpec phase or be split into read-only verification, reader migration, writer migration, and commit/undo phases.
