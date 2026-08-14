## 1. Stage 0 — gates

- [x] 1.1 Write architecture plan and DEC-035
- [x] 1.2 Create GitHub Task #33 and Bug #32; point CURRENT_WORK at Layer A
- [x] 1.3 Propose this OpenSpec change (proposal, design, specs, tasks)
- [x] 1.4 Complete PREFLIGHT.md and DECISION_REVIEW.md in this folder (required before Stage 2 firmware)

## 2. Stage 1 — content sufficiency (native only)

- [x] 2.1 Inventory every `UndoEntry` kind and grouping field (`editPassIds`, `noteEditPassIndex`, companions, geometry)
- [x] 2.2 Native fixtures: A→B→C; A→B→C→undo→undo→D; one edit → multiple records; mixed record/overdub/edit; multi-slot
- [x] 2.3 Prefix holds; undo-unit-id gap retracted (session `editPassIds` are E:-only; reboot flatten already exists)
- [x] 2.4 Keep writing today's bundle undo stack; do not delete `UndoStacks`

## 2b. Stage 1b — LoopGeometry content record

- [x] 2b.1 Add `LoopGeometry` next to `recordPass` / `editPasses` with `LoopGeometry.id` from `nextPassId_++`
- [x] 2b.2 Persist additive `GEO1` tail; old cards without the tail still load
- [x] 2b.3 `commitPendingLoopGeometry` appends a `LoopGeometry` record; in-session GUS `LoopBoundaryChange` unchanged
- [x] 2b.4 `deriveContentUndoUnits` emits `LoopBoundaryChange` from `passes.loopGeometries`
- [x] 2b.5 Native: `test_loop_content_history` + `test_storage_loop_io` geometry fixtures; `pio test -e native`

## 3. Stage 2 — load-time editing state

- [x] 3.1 On Loop load, derive tip, undo units, undo-step count, effective records; redo empty
- [x] 3.2 Fill in-session `GlobalUndoStack` from that derivation (GUS remains runtime owner)
- [x] 3.3 Display `U:nn` after load/reboot uses the derived count
- [x] 3.4 Native: walk-to-empty; serialize + reload identity
- [x] 3.5 `pio test -e native`

## 4. Stage 3 — delete persisted UndoStacks

- [x] 4.1 Confirm Stage 2 reboot `U:nn` matches tip depth before any persist deletion (`024428` undo 17 then 16; `025322` exit bake rows=1)
- [x] 4.2 Footer writes empty GUS headers (no live-stack walk); leftover `UndoStacks` stage writes empty only
- [x] 4.3 `admitLoopUndoHistory` is a no-op; `LoopUndoHistory` is not runtime-bundle work
- [x] 4.4 Legacy bundle-undo read kept; load still rebuilds GUS from Loop content
- [x] 4.5 `pio test -e native` (1137/1137)
- [ ] 4.6 Device (`teensy41-capture-serial`, upload only on confirmation): no UndoStacks-scale `PERS,bundle`; post-stop gap bounded by remaining `LoopPersist`

## 5. Closeout

- [x] 5.1 Update CURRENT_WORK, PROJECT_STATE, DELIVERABLE_TRACKING, LOOP_MIDI / deferred-persist guides
- [ ] 5.2 Do not start Stage 3b, Layer B, or Layer D in this change
