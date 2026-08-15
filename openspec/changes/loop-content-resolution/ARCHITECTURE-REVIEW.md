# Architecture review — loop content resolution

**Change:** `loop-content-resolution` (DEC-037)  
**Updated:** 2026-08-14  
**Plan:** [`loop_event_sourced_resolution_architecture.md`](../../../docs/Plans/loop_event_sourced_resolution_architecture.md)

**Related:** DEC-016; DEC-035 Layers C–D; DEC-036 D1 withdrawn / 3b PASS; overlap DEC-031/032.

**Orthogonal:** persisted D3 checkpoint write; D4 `LoadLoopJob` publication; GUS Stage 3b; overlay picker.

---

## North star

Do not optimize `materializeToEventVector` again. Prove whether indexed, checkpointed `LoopContentResolution` can make materialization unnecessary on the normal path.

---

## Per-phase gates

### Phase 0 — Canonical fixture (native)

| Question | Answer |
|----------|--------|
| **Owner module** | Test fixture under `test/test_loop_content_resolution/` |
| **Primary invariant** | One growing fixture (64/128 bars, 45+ passes, wrap, overlaps, edits) with history/window/candidate/ops/µs counters |
| **Ownership change?** | NO |
| **State transition change?** | NO |
| **Behavior-preserving?** | YES — tests only |
| **Reuse** | YES — existing `LoopPasses` / `EditPass` builders from native tests |
| **Phase scope** | Fixture + counters only |

### Phase 1–5 — Native resolveWindow / resolveState

| Question | Answer |
|----------|--------|
| **Owner module** | `LoopContentResolution` |
| **Primary invariant** | Active pass set + edit history → deterministic `resolveState` / `resolveWindow`; `resolveNotes` is a derived consumer |
| **Ownership change?** | **YES** — new derivation owner (DEC-037). Not wired to firmware consumers |
| **State transition change?** | NO |
| **Behavior-preserving?** | YES for audible firmware (unused). Native equivalence vs materialize+reconstruct |
| **Reuse** | YES — `LoopPasses`, `EditPass`, DEC-031/032 overlap. NO — materialize as the resolver |
| **Phase scope** | Native module + tests; no `handleMidiInput` |

### Phase 6–8 — Index + checkpoints

| Question | Answer |
|----------|--------|
| **Owner module** | `LoopContentResolution` indexes and in-RAM checkpoints |
| **Primary invariant** | Candidate find does not walk every pass; `resolveState` replay ≤ `checkpointIntervalTicks` |
| **Ownership change?** | NO further — same owner |
| **State transition change?** | NO |
| **Behavior-preserving?** | YES for firmware |
| **Reuse** | `CommittedEventRange` contract is **not** sufficient if it still walks pass lists |
| **Phase scope** | Native; persist D3 **not** in this phase |

### Phase 9 — Device three-part gate

| Question | Answer |
|----------|--------|
| **Owner module** | `LoopContentResolution::StateCheckpoints` + idle `DeviceGateSession` (measurement only) |
| **Primary invariant** | Checkpoint is a jump point, not a copy of the resolved loop. Worst-case latency on `035414` class: no multi-second MIDI/OLED stall; no `VCACHE,full` on the normal path; no full materialize after commit |
| **Ownership change?** | NO — same owner; density is a measured parameter. Production swap is a **later** approved slice |
| **State transition change?** | NO |
| **Behavior-preserving?** | YES until swap. Native 1-bar vs device 8/16-bar stride MUST agree |
| **Reuse** | YES — extend `StateCheckpoints` / `checkpointIntervalTicks`. Keep 3b visual-cache overdub copy |
| **Phase scope** | Sparse `soundingAt`, split `prepareRebuildSpans`, >63-bar measure ([`151450`](../../../captures/session_20260815_151450.log) 139 bars); IndexCommit / pairing / reconstruct / project / RebuildSpans / RebuildPrepare batch `kDeviceGateEventsPerSlice`; 5.15–5.18 **FROZEN**; **5.1 PASS** [`173842`](../../../captures/session_20260815_173842.log); **5.2 PASS** [`180624`](../../../captures/session_20260815_180624.log); **no** delete of materialize; arm cap stays off |

---

### Phase 6 — Production swap (consume-only; 6A / 6B / 6C)

| Question | Answer |
|----------|--------|
| **Owner module** | Idle `DeviceGateSession` produces prepared LCR state. **6A:** `Loop::rebuildVisualCacheIdleSlice` consumes. **6B:** `Track::finalizeCommitSideEffects` + `Loop::markAffectedDisplayCacheRanges`; `refreshViewportAfterOverdubStop` must not discard a nonempty loop-wide cache. `establishOverdubSourceView` is 6C only. Not a new overdub owner |
| **Primary invariant** | Overdub start/stop MUST NOT synchronously construct, sort, checkpoint, or resolve LCR state. Consume already-prepared derived state only. LCR is not a replacement for `overdubSourceView` |
| **Ownership change?** | NO for record/overdub FSM. 6A display gather and 6B invalidation stay on existing owners |
| **State transition change?** | NO |
| **Behavior-preserving?** | YES for overdub FSM. Keep 3b `visualCache.notes` copy. Dirty-cache `resolveWindow` / `ensure*` on `startOverdubbing` is **forbidden** |
| **Reuse** | YES — idle gate already owns LCR construction; 3b copy stays fallback |
| **Phase scope** | **6A PASS** [`185931`](../../../captures/session_20260815_185931.log) `match=1`. **6B PASS** [`192334`](../../../captures/session_20260815_192334.log) `stale_range` `dcnt` 15/5/5. **6C native** (`tryResolvePreparedWindow` → `overdubSourceView`; 3b fallback). Consume-when-ready only |

### Phase 6D — Incremental overdub-query index (investigation; 6D.1 first)

| Question | Answer |
|----------|--------|
| **Owner module** | `LoopContentResolution::TickIndex`. Commit publishes the pass via `Loop::commitPendingCapturePass`. 6B already marks affected display bars. Consume stays `tryResolvePreparedWindow` |
| **Primary invariant** | After a committed `OverdubPass`, the index required for a subsequent overdub query (`capturePasses` + `tickEvents` + stamp) is updated incrementally within a bounded budget. Not all of LCR |
| **Ownership change?** | NO — same derivation owner |
| **State transition change?** | NO — 6D.1 does not touch production. Later firmware placement is not this phase |
| **Behavior-preserving?** | YES — native tests only |
| **Reuse** | YES — `beginCapturePass` + `indexCapturePassEventRange` already append. Do not use full `commitCapturePass` (it also pairs/`byNoteId`-uniques). Do not incrementally maintain `openOnByPitch`. Do not repeat DEC-036 D1 |
| **Phase scope** | **6D.1 native only:** measure `index_order_us` vs `history_events` and vs `commit_delta_events` at ≥2 history sizes. Full sort that scales with history fails even if < 50 ms. Production untouched. Plan [`loop_content_resolution_incremental_commit_maintenance_refinement.md`](../../../docs/Plans/loop_content_resolution_incremental_commit_maintenance_refinement.md). **A rejected. B rejected.** |

---

**Approval:** APPROVE design gate — native Phase 0–5 may proceed. Firmware consumer wiring requires Phase 9 gate + Stage 6 consume-only invariant + explicit implement request (start with **6A**, not overdub). **6D.1** is native measurement only; production stays untouched until the mutation contract is established. Do not treat 6D as “LCR is always live.”
