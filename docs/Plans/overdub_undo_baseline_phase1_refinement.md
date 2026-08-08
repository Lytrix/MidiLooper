# Overdub undo baseline — Phase 1 refinement

## Problem

32+ bar HITL runs crash at overdub entry when `TrackUndo::pushUndoSnapshot` deep-copies the
full loop (~1500+ events) while the live loop is already in memory. Capture shows snapshot
creation succeeding, then serial silence / reboot.

## Phase 1 change (shipped)

Move the overdub undo baseline from overdub start to record stop; overdub entry opens a
lightweight session marker instead of allocating another full copy.

| Step | Before | After |
|------|--------|-------|
| Record start (empty) | push empty snapshot | unchanged |
| Record stop | no snapshot | `establishRecordStopBaseline` (replace empty snapshot with recorded loop) |
| Overdub start | push full copy | `beginOverdubSession` (hash/count marker, no copy) |
| Overdub stop | — | `endOverdubSession` (close marker) |
| Loaded loop → overdub (no baseline) | push at overdub start | fallback: still push at overdub start |

## Files

- `include/Loop.h` — overdub session fields
- `include/TrackUndo.h`, `src/TrackUndo.cpp` — session + record-stop baseline helpers
- `src/Track.cpp` — call sites in record stop / overdub start / overdub stop

## HITL script

`host_midi_automation_baseline.py` treats serial `Clear ignored — track is empty` as a
successful clear precondition when no `EMPTY` transition is emitted.

## Validation gate

Re-run 32+32 with `--serial-port` and serial heartbeat. Pass criteria unchanged from
`.cursor/rules/HITL-Test-Flow.mdc`; additionally serial must show `Overdub session opened`
(not a second full snapshot) and no heartbeat loss at overdub entry.

## Not in Phase 1

- Bounded delta window (Phase 2)
- Materialize-at-checkpoint (Phase 3)
- Deterministic PSRAM tier map (Phase 1 follow-up task in master plan)
