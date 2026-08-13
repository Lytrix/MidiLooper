## 1. Stage 0 — gates

- [x] 1.1 Write architecture plan and DEC-035
- [x] 1.2 Create GitHub Task #33 and Bug #32; point CURRENT_WORK at Layer A
- [x] 1.3 Propose this OpenSpec change (proposal, design, specs, tasks)
- [x] 1.4 Complete PREFLIGHT.md and DECISION_REVIEW.md in this folder (required before Stage 2 firmware)

## 2. Stage 1 — content sufficiency (native only)

- [x] 2.1 Inventory every `UndoEntry` kind and grouping field (`editPassIds`, `noteEditPassIndex`, companions, geometry)
- [x] 2.2 Native fixtures: A→B→C; A→B→C→undo→undo→D; one edit → multiple records; mixed record/overdub/edit; multi-slot
- [x] 2.3 Prefix holds; named gaps: loop-lifetime undo-unit id; geometry content revision
- [x] 2.4 Keep writing today's bundle undo stack; do not delete `UndoStacks`

## 3. Stage 2 — load-time editing state

- [ ] 3.1 On Loop load, derive tip, undo units, undo-step count, effective records; redo empty
- [ ] 3.2 Fill in-session `GlobalUndoStack` from that derivation (GUS remains runtime owner)
- [ ] 3.3 Display `U:nn` after load/reboot uses the derived count
- [ ] 3.4 Native: walk-to-empty; serialize + reload identity
- [ ] 3.5 `pio test -e native`

## 4. Stage 3 — delete persisted UndoStacks

- [ ] 4.1 Confirm Stage 2 reboot `U:nn` matches tip depth before any persist deletion
- [ ] 4.2 Remove `DeferredSaveStage::UndoStacks` / `stepDeferredSaveJobUndoStacks`
- [ ] 4.3 Remove `LoopUndoHistory` from `isRuntimeBundleWorkType` and `admitLoopUndoHistory` call sites
- [ ] 4.4 Keep legacy bundle-undo read only if existing cards require it
- [ ] 4.5 `pio test -e native`
- [ ] 4.6 Device (`teensy41-capture-serial`, upload only on confirmation): no UndoStacks-scale `PERS,bundle`; post-stop gap bounded by remaining `LoopPersist`

## 5. Closeout

- [ ] 5.1 Update CURRENT_WORK, PROJECT_STATE, DELIVERABLE_TRACKING, LOOP_MIDI / deferred-persist guides
- [ ] 5.2 Do not start Stage 3b, Layer B, or Layer D in this change
