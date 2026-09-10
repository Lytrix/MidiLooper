# Immutable loop snapshot — OpenSpec 4.5 cross-gate verify

**Status:** Implementing — `base` + occupy PASS; compose blocked on resolve latency  
**OpenSpec:** `immutable-loop-snapshot-passes` task `4.5`  
**Parent:** 4.4 closed
([`immutable_loop_snapshot_4_4_closeout_refinement.md`](immutable_loop_snapshot_4_4_closeout_refinement.md)).  
**Does not start:** 4.6 freshness migration, hydrate 4b–5, raise `N`, §5
undo/load format, §6 compatibility retirement. No new HITL stimulus.

## Gate log (2026-09-10)

| Gate | Result |
|---|---|
| HITL `base` (harness ARMED→play→RECORDING) | PASS — [`122812`](../../captures/session_20260910_122812.log) / report [`122857`](../../captures/host_midi_automation_baseline_20260910_122857.json); first-note 7/10 clocks |
| HITL `overdub_occupy_paint` | PASS — [`123445`](../../captures/session_20260910_123445.log) / occupy report [`123557`](../../captures/host_midi_hitl_occupy_paint_20260910_123557.json) |
| HITL `note_edit_overdub_compose` (`02`) | FAIL 13/14 — [`124645`](../../captures/note_edit_overdub_compose_20260910_124645_serial.log): same-pitch Hold/Move OK (`EditSessionAction` type=2/3); **`GEOM_APPLY,resolve` 61442 µs > 20000 µs** (`overdub_apply_reuses_committed_base`) |

## Gate log (2026-09-09)

| Gate | Result |
|---|---|
| RAM1 `teensy41-capture-serial` | PASS — locals **7008** |
| `pio test -e native` (full) | 1487 succeeded / 1 skipped; 9 suites ERRORED (sconsign/SIGKILL infra) |
| Native ERRORED remount | PASS — all 10 suites / 341 cases (`test_loop_take_survival`, `test_sd_load_adopt`, …) |
| HITL `base` attempt 1–6 | FAIL / blocked — superseded by 2026-09-10 harness PASS |

**OpenSpec 4.5 not checked off** — compose blocked on in-edit overdub resolve latency (firmware gate), not on seed arm/select after harness fixes.

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — verify only |
| State-transition change? | NO |
| Unbounded MIDI-interval owner? | NO |

## Invariant

```text
Existing native and HITL gates still PASS after current-root consumers
read LoopSnapshot as the normal source (4.4 closed). Pass rematerialize
remains fallback/oracle only.
```

## Verification matrix (existing gates only)

| 4.5 concern | Gate | Pass criteria |
|---|---|---|
| Record + overdub + clock/transitions + post-stop undo/redo | HITL `--preset base` / `STIM-BASE-2-2-01` | HITL-Test-Flow baseline criteria |
| Wrap + same-`NoteId` Hide/Shorten + undo/redo | HITL `--preset overdub_occupy_paint` / `STIM-OCCUPY-PAINT-01` | occupy scenario verify PASS |
| Active-capture separation (NOTE_EDIT + live Capture) | HITL `--preset note_edit_overdub_compose` / `STIM-NE-OD-COMPOSE-02` | compose verify checklist PASS |
| Short loop | Covered by `base` (2-bar) | same as base |
| Long loop | Native `test_loop_take_survival` / visual-cache snapshot fixtures (no registered long-loop HITL in `registry.py`) | focused + full native PASS |
| Slot / load adopt | Native `test_sd_load_adopt` | suite PASS |
| Same-`NoteId` geometry versioning / wrap journal | Native wrap-journal fixtures in `test_loop_take_survival` / `test_pending_note_change` | suite PASS |
| Old-snapshot stability / latest-selected version | Native published-snapshot / stale-root fixtures; device serial `root_rev` / `playback_rev` on HITL captures | fixtures PASS; HITL serial shows current root after drain |
| RAM1 floor | `pio run -e teensy41-capture-serial` | locals ≥ 4000 |
| Full host suite | `pio test -e native` | all succeeded (1 skipped OK) |

## Stages

### Stage 1 — plan + matrix (this doc)

### Stage 2 — firmware build (RAM1)

### Stage 3 — upload; parallel native + HITL `base`

### Stage 4 — HITL `overdub_occupy_paint` then `note_edit_overdub_compose` (stimulus `02`)

### Stage 5 — record results; check off OpenSpec 4.5 only if all mapped gates PASS

## Out of scope

- New stimuli or presets  
- Snapshot-identity undo attachment (§5.1)  
- `legacyMidiEventsFromCommitted` freshness (4.6)  
- Closing 4b.3 post-stop PLAYING gap  
