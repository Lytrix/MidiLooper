# NOTE_EDIT overlay delta — eliminate full-loop session builds

**Status:** DEC-055 accepted — Stage 1 device PASS (select clone / full-row warm)  
**Kind:** architecture  
**Date:** 2026-09-08  
**Decision:** [DEC-055](../DECISION_LOG.md#dec-055--note-edit-working-set-is-overlay-delta-on-opened-loopsnapshot)  
**Implementation:** [`note_edit_overlay_delta_refinement.md`](note_edit_overlay_delta_refinement.md)  
**Evidence:** [`212850`](../../captures/session_20260908_212850.log) Stage 1 PASS; [`201106`](../../captures/session_20260908_201106.log), [`204458`](../../captures/session_20260908_204458.log) pre-fix  
**Does not authorize:** hydrate Stages 1–5, DEC-054 Stage 2, Option B `clone()` slice, rename `NoteEditCurrentState`, unify live-capture fold, raise `N`, OpenSpec 4.5 / 4.6, `RuntimeWorkBudget`

This session does **not** invent a second consume model. It binds already-approved plans to one north star and adds the missing **E:** restore contract.

Reuse (do not fork):

| Already decided | Where |
|---|---|
| `NoteEditCurrentState` is the mutable NOTE_EDIT overlay | [DEC-029](../DECISION_LOG.md#dec-029-noteeditcurrentstate-owns-note-edit-editable-note-state) |
| Committed content is the published `LoopSnapshot` | [DEC-044](../DECISION_LOG.md#dec-044-immutable-loop-snapshot-model-replaces-maintained-lcr-direction), [NAMING.md](../Authority/NAMING.md) **Analyze** |
| Overlap is the overdub sibling: participants vs selected/mover LinearSpan | [`note_edit_selectedtick_lcr_resolution_architecture.md`](note_edit_selectedtick_lcr_resolution_architecture.md), [`overdub_participant_loop_content_architecture.md`](overdub_participant_loop_content_architecture.md) §5 |
| Select is a `selectedTick` neighborhood, not `resolveState(selectedTick)` | same selected-tick plan |
| Overlay stays neighborhood-sized, like `capturePreview` | selected-tick plan § Overdub display principle |
| `E:00` = no geometry undo; select does not push | [`note-edit-session-undo`](../../openspec/specs/note-edit-session-undo/spec.md) |
| Entries may be a lossless current-state **delta**, sized by edit scope | same spec |
| Open already pins `openedOnCurrentSnapshot` / `openedSnapshotRevision` | `EditSession`, `tryCopyNoteEditOpenedSnapshotEvents` |
| Work path / stage order for consume | [`note_edit_hydrate_enhancement.md`](note_edit_hydrate_enhancement.md) |
| PLAYING select stall is admission, not OLED | [DEC-054](../DECISION_LOG.md#dec-054--note-edit-playing-display-stall-remainder-units), [`204458`](../../captures/session_20260908_204458.log) |

---

## Formal reassessment

| Field | Content |
|---|---|
| **Reason triggered** | **Undo semantics / snapshot shape.** `SessionUndoEntry` would restore E:00 from the opened `LoopSnapshot` and later steps from overlay delta, not a full `NoteEditCurrentState::clone()`. Also **persistent-in-session representation**: current-state rows stop being a second full loop. |
| **Current architecture** | `openNoteEditSession` rematerializes the loop, fills current state from every `visualCache` row, and kind-boundary undo clones every row. `NoteGeometryResolver` walks all NoteOns / display notes. Display already paints from snapshot + overlay on a current root (OpenSpec 4.4h). |
| **Proposed evolution** | **Extend** `EditManager` / `NoteEditCurrentState` / `NoteEditSessionUndo`. Committed Analyze queries consume the selected `LoopSnapshot` (NAMING). No new Manager. No `EditSourceView`. No ledger at `selectedTick`. |
| **Migration impact** | Firmware + native undo/overlap/select fixtures + HITL edit + large-loop device gate. No SD schema. `saveNoteEditPass` / global **U:** unchanged. |
| **Recommendation** | DEC-055 accepted. Stage 1 is select-only (no clone / no full-row warm). First kind-boundary E:00 restore is the next slice. Option B rejected. |
| **Approval required** | Granted 2026-09-08. Stage 1 named in CURRENT_WORK. |

Architecture checkpoint:

| Question | Answer |
|---|---|
| Ownership change? | NO new owner. `EditManager` keeps session, current state, **E:**. `Loop` keeps the published snapshot. `NoteGeometryResolver` keeps overlap Resolution. `DisplayManager` paints. |
| State-transition change? | NO. Open / select / kind / commit / close stay. `EditSession.active` flip is still DEC-054 Stage 2 (separate). |
| Unbounded interval owner? | YES today (`clone()`, rematerialize, full-cache fill). The design removes those complete operations from select / first kind-boundary and from analyze. Each remaining unit needs an `ExecutionBounds.md` row in the same commit that admits it. |

Reuse decision: **YES** — extend the selected-tick / hydrate consume shape and the opened-snapshot identity already on `EditSession`.

---

## North star

> A NOTE_EDIT session never creates a second full-loop representation merely to support editing or undo.

```text
LoopSnapshot
    committed content / E:00 authority
          │
          ▼
NoteEditCurrentState
    only overlay mutations
    mover + participating notes
          │
          ▼
E:01, E:02, ...
    lossless deltas + selection/focus
```

Keep the name `NoteEditCurrentState` (DEC-029). It is overlay state, not a
complete materialized loop.

### Working-set invariant

For a current published `LoopSnapshot`:

- opening an edit session does not construct a full current-state copy;
- select does not enumerate the loop;
- select does not clone current state;
- kind-boundary undo does not clone current state;
- overlap resolution examines only the selected/mover span and indexed
  same-pitch participants;
- undo entries contain snapshot identity plus lossless edit-scope delta;
- no edit operation may publish a second full-loop representation.

Stage 1 enforces the select clone / warm lines only. Later stages take the rest.
`currentState.clone()` on a select or kind-boundary path is a regression.

Large loops make today’s open / select / overlap / undo cost scale with every note. [`201106`](../../captures/session_20260908_201106.log) cloned **137** current-state rows in 91–133 ms. [`204458`](../../captures/session_20260908_204458.log) still pays 34–60 ms per settled select landing because `focus_snap` copies the same full row set.

---

## Full-loop sites to eliminate

These are the builds this session retires. Owners stay; the working set shrinks.

| Site | Today | Target |
|---|---|---|
| `openNoteEditSession` | `rematerializeEditView` + `rebuildVisualCacheFromPasses` + `buildFromSessionStore` of all events + `ensureCurrentStateVisibleRowsFromVisualCache` | Analyze consumes the current published snapshot. Current state starts empty (or mover-only after first select). Hydrate Stage 4a. |
| `NoteGeometryResolver` | `collectEvaluationScopeNoteIds` all NoteOns; `overlayUneditedBaselineMapFromDisplayNotes` every cache row | Same-pitch participants vs selected/mover LinearSpan. Hydrate Stages 2–3. |
| `ensureCurrentStateVisibleRowsFromVisualCache` | Inserts every visible cache note into current state | Remove as an analyze fill. Paint keeps `visualCache` + overlay compose. |
| `processKindBoundaryUndoWarm` / `clone()` | Copies every current-state row so the next kind change can push **E:** | Select does not build. First kind-boundary at **E:00** stores selection + trimmed focus and restores content from `openedSnapshotRevision`. Later **E:** store overlay delta only. |
| `SelectNavigation` | `notes ×` every 16th step of the loop | Neighborhood around `selectedTick` (`tickEvents` / `spanBoundaries`). Hydrate Stage 1. |
| PLAYING audition | Full `sessionMidiEvents()` replace | Playback window + overlay NoteIds. Hydrate Stage 4c. |

Piano-roll `visualCache` remains the idle / LOOP_EDIT committed list. It is not Analyze authority (same split overdub already made).

---

## Mapping (reuse, do not remap `currentTick`)

Do **not** put `ActiveNoteLedger` at `selectedTick`. The ledger is playback state at `currentTick` ([DEC-041](../DECISION_LOG.md#dec-041-occupy-present-at-s-jit-not-full-loop-lcr-mat), [DEC-042](../DECISION_LOG.md#dec-042-same-pitch-active-note-identity-is-a-cardinality-problem-not-an-off-identity-problem)). Occupy reads it. Editor Analyze does not write it.

Do **not** treat `selectedTick` as the overlap origin. That is the withdrawn 16-bar reconstruct.

```text
LoopSnapshot (opened / current root)
    = committed Analyze source          // E:00 content

NoteEditCurrentState
    = this-session overlay only         // mover + remaining participants

Select
    = neighborhood around selectedTick  // not resolveState, not ledger

Overlap
    = same-pitch LinearSpan vs selected/mover span
    = join Shorten/Hide/Add, leave RestoreNote
    = after settle (mover stop + one DisplayManager::update)

E:00
    = openedOnCurrentSnapshot + openedSnapshotRevision
    = undoCount 0

E:01…
    = overlay delta + EditorSelection + trimmed NoteEditFocus
    = not a full row clone, not a new LoopSnapshot
```

Add/remove after settle is the overlay lifecycle table already in the selected-tick plan (MOTION → STOP+find → PARTICIPANT / LEAVER → ACTION RELEASE). Path B (live geometry into the snapshot / LCR index) stays forbidden.

When `openedOnCurrentSnapshot` is false, Analyze miss is today’s legacy compatibility path — not a second authority.

---

## E: contract (this session’s addition)

The selected-tick plan left **E:** routing unchanged. That is why select still clones.

| Step | Restore content | Stores |
|---|---|---|
| **E:00** | `tryCopyNoteEditOpenedSnapshotEvents` + `buildFromSessionStore` (same as open on a current root) | Snapshot identity already on `EditSession` |
| **First kind-boundary** (Select → Move/Pitch/Length/Add at undoCount 0) | Same E:00 rebuild | Selection + trimmed focus + `editPassIdsAtPush`. `hasUndoCurrentState = false` |
| **Later kind-boundary** | E:00 rebuild, then apply stored overlay delta / `editRows` | Only mutated `NoteId` rows since E:00 (or since prior step if that is smaller and lossless) |
| **Live-capture fold** | Existing live-capture `SessionUndoEntry` | Unchanged; those notes are not in the opened snapshot |
| **Select** | Nothing | No push, no warm, no clone |

`applySessionUndoEntry` already has the no-current-state path (`applySessionEditRows` → opened snapshot copy). Firmware today always sets `hasUndoCurrentState` and `clone()`s, which is what `focus_snap` measures.

This matches the spec: entries scale with edit scope or a documented current-state delta, not loop length.

---

## Relationship to DEC-054

DEC-054 Stage 1 coalesced and moved the warm. [`204458`](../../captures/session_20260908_204458.log) still leaves the 1.4–1.5 s `DFRAME` band during back-and-forth select because each >80 ms pause runs a 34–60 ms whole-row `clone()`.

DEC-054 identified and relocated the stall. DEC-055 removes its cause.

- **Option B (slice `clone()`) is rejected.** Do not slice a full copy.
- DEC-054 Stage 2 (deferred open/close) stays a separate admission change.
- Stage 1 drops select warm/clone. The first geometry kind-boundary may still
  cache-miss clone until the E:00 restore slice.

---

## DEC-055 (accepted)

**NOTE_EDIT session working set is overlay delta on the opened LoopSnapshot.**

1. Extend `EditManager`. No new Manager or Analyze owner type.
2. Committed Analyze queries consume the selected `LoopSnapshot`. `NoteEditCurrentState` holds only this-session overlay rows.
3. Overlap and select query shapes stay as the 2026-08-17 selected-tick / overdub-sibling amendment. Do not map `currentTick` → `selectedTick`. Do not reuse `ActiveNoteLedger` for the bracket.
4. **E:00** is the opened snapshot identity. Later **E:** entries are overlay deltas. Select does not clone and does not push.
5. Consume already-prepared snapshot / index on open and fader. Do not construct snapshot or LCR on those stacks. Miss = legacy path only.
6. Execute consume through [`note_edit_hydrate_enhancement.md`](note_edit_hydrate_enhancement.md) stages. Do not start a parallel hydrate numbering from “LCR × interval around `selectedTick`.”
7. Interval reservation, `RuntimeWorkBudget`, raise `N`, OpenSpec 4.5 / 4.6 stay unauthorized.

### Alternatives considered

| Alternative | Rejected because |
|---|---|
| Slice `clone()` (DEC-054 Option B) | Still copies every row; cost grows with loop size |
| `ActiveNoteLedger` at `selectedTick` | Ledger is playhead runtime; DEC-041/042; NAMING forbids ledger for overlap scratch |
| `resolveState(selectedTick)` as Select | Already rejected; not the overlap interval either |
| New `LoopSnapshot` per select / edit | Construction is idle-stepped; more work than a delta; snapshot has no selection/focus |
| Optimistic session flip (DEC-054 F) | Half-open session on faders |
| New `EditSourceView` / Manager | NAMING / DEC-037 |

---

## Execution order

Firmware only when CURRENT_WORK names the stage.

1. **Stage 1 — eliminate select `clone()` / full-row warm** (authorized).
   Select: neighborhood/focus update only. No **E:** push, no current-state
   clone, no `scheduleKindBoundaryUndoWarm`. Do not change first
   kind-boundary restore in this slice.
2. **Stage 2 — first kind-boundary at undoCount 0** uses E:00 snapshot
   restore (`hasUndoCurrentState == false`). Separate capture.
3. **Stop full current-state fill** —
   `ensureCurrentStateVisibleRowsFromVisualCache` is not an analyze fill.
4. **Hydrate 1 → 2 → 3** — select neighborhood query, indexed overlap,
   resolver prepared-hit.
5. **Hydrate 4a → 4b → 4c → 5** — open without rematerialize-to-analyze;
   playback overlay; 16-bar / ~1000-note device gate.

Lossless undo acceptance (later): after arbitrary kind transitions, undo
returns to E:00 without a full current-state clone on the kind-boundary
path. Live-capture fold stays a separate existing entry.

---

## Out of scope

- DEC-054 Stage 2 deferred open/close
- Snapshot-identity global **U:** (OpenSpec remaining)
- Raising snapshot idle `N`, `T_RT_reserve`, OpenSpec 4.5 / 4.6
- Redesign of `materializeToEventVector` / `EditSession.store` in the first consume stages
- Wrap-move persist (parked)
- New domain nouns

---

## Open before firmware

Resolved 2026-09-08: DEC-055 accepted; Stage 1 = eliminate select clone /
full-row warm; `openedOnCurrentSnapshot == false` stays legacy miss for
later E:00 slices; do not rename `NoteEditCurrentState`.

### Proceed?

- YES — Stage 1 only. See the refinement plan.
