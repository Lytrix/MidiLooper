# Tasks — unified-commit-lazy-slot-load

Architecture (frozen): [`docs/plans/unified_publish_pipeline_deferred_lazy_loading_architecture.md`](../../../docs/plans/unified_publish_pipeline_deferred_lazy_loading_architecture.md)  
Review resolutions: [`docs/plans/unified_publish_pipeline_review_resolutions_refinement.md`](../../../docs/plans/unified_publish_pipeline_review_resolutions_refinement.md)  
Rename table: [`docs/plans/unified_publish_pipeline_commit_terminology_refinement.md`](../../../docs/plans/unified_publish_pipeline_commit_terminology_refinement.md)

**Branch:** create from `af1227c` (e.g. `feature/deferred-lazy-load`) before firmware.

## 1. Phase 0 — Freeze and OpenSpec

- [x] 1.1 Freeze architecture docs (parent Status: Frozen)
- [x] 1.2 Open OpenSpec change `unified-commit-lazy-slot-load` (proposal, design, specs, tasks)
- [x] 1.3 Point `CURRENT_WORK.md` / `PROJECT_STATE.md` at this change
- [x] 1.4 Append DEC entry to `docs/DECISION_LOG.md` (Commit-centered lazy slot load)
- [x] 1.5 Freeze rename table rows as Action | Scope/object | Identifier (reject bare `ToCommitted` / new Flat)
- [x] 1.6 Complete DECISION_REVIEW + PREFLIGHT before first firmware edit

## 2. Phase 1 — Publish → Commit rename

- [x] 2.1 Rename committed-truth APIs per terminology table (`hasCommittedPasses`, chunk-id helpers, gather*Events*, `SlotLoadSessionState::Committing`, …)
- [x] 2.2 Update guides / comments that describe published pass presence as committed passes
- [x] 2.3 Update native tests and fixtures for renamed symbols
- [x] 2.4 Grep gate: no remaining public Publish/Published identifiers in the committed-truth family
- [x] 2.5 **Gate:** `pio test -e native` PASS

## 3. Phase 2 — Audible-only boot (existing sync load OK)

- [x] 3.1 Stop auto-enqueue of non-audible SD slots at boot
- [x] 3.2 Sync-commit audible set only (`isAudibleBootSlot`); MVP may keep `loadLoopSlotFromCurrentSetSd`
- [x] 3.3 Redefine `bootInteractiveReady` for audible COMMITTED (explicit flag; never remap via `getActiveLoopIndex()`)
- [x] 3.4 Allow piano roll / interactive UI at COMMITTED without waiting for DERIVED_READY
- [x] 3.5 Update `docs/Guides/BOOT_LOAD.md`
- [x] 3.6 **Gate:** device capture — ready ≪ full-set baseline `020628`; all actives audible on first Play (`session_20260718_165032` — ch2 MO after enable-playing-slot fix)

## 4. Phase 3 — Cooperative SlotLoadSession (incremental)

- [x] 4.1 Extend `SlotLoadSession` cooperative advance (internals); `Publishing` → `Committing` already renamed
- [x] 4.2 Optionally factor `loadLoopSlotFromCurrentSetSd` body into session steps
- [x] 4.3 **Gate:** `pio test -e native`; boot still uses sync audible Commit

## 5. Phase 4 — Deferred on-demand restore

- [x] 5.1 Budget `processDeferredLoopSlotRestore` for explicitly requested slots only (priorities: audible, requested)
- [x] 5.2 Wire select/focus unload → request restore (`prioritizeLoopSlotRestoreForFocus` / equivalent) — queue only, no sync fallback; allow non-enabled focus
- [x] 5.3 MVP: while PLAYING, queue request and defer SD until transport idle (`timingCriticalTrackActive` gate in `main`) — superseded for loads by 6.2
- [x] 5.4 **Gate:** select unloaded slot while stopped → COMMITTED + display; no silent full-set hydrate
- [x] 5.5 Device capture evidence — [`session_20260718_170201.log`](../../../captures/session_20260718_170201.log): post-boot `Deferred restore` `1/0`,`1/2`,`1/3`,`2/1`,`2/2`; `DISP` slot change with notes; user: piano roll + LEDs + play OK

## 6. Phase 5+ — Later (parked until MVP green)

- [ ] 6.1 Derived rebuild off Commit critical path (DERIVED_READY optional polish)
- [x] 6.2 Load while PLAYING interactive slices — focus High while PLAYING; Low when transport idle (`canRunBackgroundLoadLoopNow`); Phase A.6 timed parse + atomic publish; A.7 finalize headroom without PSRAM walk ([`deferred_storage_commit_parse_split_enhancement.md`](../../../docs/plans/deferred_storage_commit_parse_split_enhancement.md)); fail baselines [`220005`](../../../captures/session_20260718_220005.log) / [`223713`](../../../captures/session_20260718_223713.log)
- [x] 6.2-device **Gate:** PASS [`224607`](../../../captures/session_20260718_224607.log) — track switch while PLAYING responsive (BTN 15/15); Low parse suspended during PLAYING; clean stop → idle `LLBG,done`; vs fail [`223713`](../../../captures/session_20260718_223713.log) / smoothness [`190417`](../../../captures/session_20260718_190417.log). **A.7 follow-up in tree:** remove ~295ms `sm_malloc_stats_pool` from pass finalize headroom (re-flash to confirm parse_us drops).
- [x] 6.2b Phase A time-budgeted `LoadLoopJob` — `runDeferredFrame` + `LoadLoopBudget`; demote parks one in-flight job; load before display
- [ ] 6.3 Undo / import / paste documented on Commit contract (as those producers land)

## 7. Closeout

- [x] 7.1 Update `CURRENT_WORK.md`, `PROJECT_STATE.md`, `DELIVERABLE_TRACKING.md`
- [ ] 7.2 Archive change when gates pass (`/opsx:archive`) — 6.2-device PASS on [`224607`](../../../captures/session_20260718_224607.log); confirm A.7 after re-flash then archive
