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
| **Phase scope** | Sparse `soundingAt`, split `prepareRebuildSpans`, >63-bar measure ([`151450`](../../../captures/session_20260815_151450.log) 139 bars); IndexCommit / pairing / reconstruct / project / RebuildSpans batch `kDeviceGateEventsPerSlice`; 5.15 **complete** (`spanBoundaries` append + `sort`); 5.16 phase-budget audit (largest slice vs `DFRAME` gap); **no** delete of materialize; arm cap stays off |

---

**Approval:** APPROVE design gate — native Phase 0–5 may proceed. Firmware consumer wiring requires Phase 9 gate + explicit user approval.
