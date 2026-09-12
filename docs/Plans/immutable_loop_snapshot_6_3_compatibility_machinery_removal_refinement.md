# OpenSpec 6.3 — Compatibility machinery removal

**Status:** **in progress** (6.3.1 SHIPPED 2026-09-12)  
**OpenSpec:** `immutable-loop-snapshot-passes` task **6.3**  
**Depends on:** [6.2 plan](immutable_loop_snapshot_6_2_compatibility_retirement_refinement.md) (**SHIPPED**), [5.7 plan](immutable_loop_snapshot_5_7_persistence_work_queue_stall_hardening_refinement.md) (**SHIPPED**)  
**Regression baseline:** HITL `base` PASS [`012147`](../../captures/session_20260912_012147.log)

## North star (task 6.3)

Remove **compatibility effective-state derivation** (`passesMaterializedStore_`,
`LoopPasses::materializeToEventVector`, `gatherCommittedEvents*`, LCR maintained
history) after all normal consumers read **published `LoopSnapshot` content only**.
Session-exit publication remains **idle-stepped** per [DEC-045](../DECISION_LOG.md#dec-045--overdub-snapshot-publish-is-idle-stepped); 6.3 does not
reintroduce synchronous `tryPublishSessionExitSnapshot` on the MIDI stop path.

## Architecture gate (task 6.3 — parent)

| Question | Answer |
|---|---|
| **Owner module** | `LoopMaterialization`, `LoopSnapshotPublish`, `LoopContentResolution`, per-consumer owners from 6.1 inventory |
| **Primary invariant** | No normal consumer reaches pass replay or maintained LCR effective-state; snapshot miss → hold-empty or snapshot-native path |
| **Ownership change?** | NO — delete compat machinery only |
| **State transition change?** | NO — publication queue unchanged |
| **Execution bound change?** | NO — removal only |
| **Behavior-preserving?** | NO during session-exit gap (hold-empty replaces C1-a compat); YES after root publishes |
| **Reuse** | Extend snapshot-first owners; delete compat APIs when last reader gone |
| **Phase scope** | One machinery slice per commit |

### Architecture checkpoint (parent)

1. **Ownership?** NO  
2. **State transitions?** NO  
3. **Unbounded interval owner?** NO

## Stage table

| Stage | Scope | Status |
|---|---|---|
| **6.3.1** | Remove C1-a session-exit compat bridges (B1/B3/B4/B2/B5); hold-empty during `isPlaybackSessionExitPublicationGap` | **SHIPPED** |
| **6.3.2** | Post-record visual continuity during deferred publication gap (no C1-a restore) | **SHIPPED** — [plan](immutable_loop_snapshot_6_3_2_post_record_visual_continuity_refinement.md); deferred record-start preview polish in same plan |
| **6.3.3** | Remove `passesMaterializedStore_`, `materializeEditViewFromPasses`, `midiEvents()` consumer paths | **SHIPPED** |
| **6.3.4** | Remove `gatherCommittedEvents*` APIs and `LoopMaterialization` pass-replay gather | pending |
| **6.3.5** | Remove capture `copyCommittedPassReplayEvents` / construction replay; snapshot-only capture source | pending |
| **6.3.6** | Remove `materializeToEventVector` and LCR maintained-history construction | pending |
| **6.3.7** | Delete `isPlaybackSessionExitPublicationGap` if unused; host gate finalization; docs + OpenSpec 6.4/6.5 handoff | pending |

## 6.3.1 — C1-a hold-empty (SHIPPED)

**Decision:** Retire 6.2 C1-a narrow compat (`gatherCommittedEvents*` /
`materializeEditViewFromPasses` on `isPlaybackSessionExitPublicationGap`). Consumers
hold empty until idle `stepPendingSnapshotPublish` completes (DEC-045 unchanged).

### Files

- `TrackPlaybackWindowBuild.cpp` — remove session-exit gather branches
- `LoopVisualCache.cpp` — remove session-exit gather branches
- `DisplayColdHelpers.cpp`, `DisplayNoteResolveCommitted.cpp` — remove session-exit gather
- `CommittedMidiScratch.cpp` — gap no longer `FromPasses`
- `TrackDeferredMaintenance.cpp` — remove gap `materializeEditViewFromPasses`
- `LoopCapture.cpp` — gap no longer triggers `copyCommittedPassReplayEvents` in narrow compat
- Host gates: `test_b1_*`, `test_b3_b4_*`, `test_b2_b5_b6_*`, `test_compatibility_consumer_inventory.py`

### Verification (6.3.1)

| Gate | Status |
|---|---|
| Host C1-a retirement gates | **PASS** (B1/B3/B4/B2/B5 + inventory 12 tests) |
| Native `pio test -e native` | **1829/1834** (+`test_committed_scratch_hold_empty_during_session_exit_gap`) |
| RAM1 `teensy41-capture-serial` | **6880** locals |
| HITL `base` | **PASS** [`013611`](../../captures/session_20260912_013611.log) / report [`013652`](../../captures/host_midi_automation_baseline_20260912_013652.json) |

## Explicit non-goals (6.3 parent)

- Changing DEC-045 idle publication scheduling
- OpenSpec archive / spec sync (6.5)
- Playback gather LCR consume ([`playback_gather_lcr_consume_enhancement.md`](playback_gather_lcr_consume_enhancement.md) — separate work identity)

## Pre-implementation review

### Ready

- 6.2 B1–B8 consumer compat retired; `_6_2_BLOCKER_SITES` empty
- 5.7 HITL persist sign-off PASS [`012147`](../../captures/session_20260912_012147.log)
- DEC-045 documents idle-stepped session-exit publish (hold-empty is the intended 6.3 gap policy)

### Proceed?

**YES** — start **6.3.1** (C1-a hold-empty); machinery deletion in **6.3.2–6.3.5** after host gates shrink.

## 6.3.3 — `passesMaterializedStore_` removal (SHIPPED)

**Decision:** Delete lazy `passesMaterializedStore_` / `materializeEditViewFromPasses` /
`Loop::midiEvents()` / `Track::getMidiEvents()`. `copyEffectiveCommittedEvents` delegates to
`copyCommittedPassReplayEvents` (direct pass replay, no cache). `CommittedMidiScratch` holds
empty on snapshot miss (C4 stale-root pass fallback retired with C1-a).

### Files

- `include/Loop.h`, `src/Loop.cpp`, `src/Loop/LoopMaterialization.cpp` — remove store + accessors
- `include/Track.h` — remove `getMidiEvents()`; note-cache paths use `copyEffectiveCommittedEvents`
- `src/Utils/CommittedMidiScratch.cpp` — snapshot-only refresh
- `src/EditManager/NoteEditSessionLifecycle.cpp`, `src/TrackUndo.cpp`,
  `src/TrackManager/TrackManagerMemoryPressure.cpp` — drop discard hooks
- Host gates: `test_compatibility_consumer_inventory.py`, `test_b2_b5_b6_narrow_session_exit_compat_gate.py`
- Native: `test_effective_event_store`, `test_loop_take_survival`, `test_overdub_source_view`,
  `test_note_edit_session_undo`

### Verification (6.3.3)

| Gate | Status |
|---|---|
| Host `test_compatibility_consumer_inventory.py` + `test_b2_b5_b6_narrow_session_exit_compat_gate.py` | **PASS** |
| Native `pio test -e native` | **1830/1835** |
| RAM1 `teensy41-capture-serial` | **6880** locals |
| HITL `base` | **PASS** [`022843`](../../captures/session_20260912_022843.log) / [`022928`](../../captures/host_midi_automation_baseline_20260912_022928.json); flake FAIL [`022633`](../../captures/session_20260912_022633.log) (`already_pending` wedge) |

### OpenSpec 4b.3 (documented, not closed)

HITL `base` PASS [`020309`](../../captures/session_20260912_020309.log): attribution holds
(`ODUB,stop,display` 4.1/9.3 ms; post-OD PLAYING `midi_gap` ~44 ms from `late_on` +
`track_order`). Bound not met — keep open.
