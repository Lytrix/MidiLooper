## Why

An overdub Stop retained behind unavailable finalization resources currently leaves the input interval open. The owner has authorized a separate stop-edge slice: close input on the press, preserve that musical boundary through deferred completion, and allow a successor overdub while its predecessor is still finalizing.

## What Changes

- Separate immediate input closure from deferred finalization; no post-Stop input may extend the stopped interval.
- Preserve the first Stop's loop-phase boundary, identity, pending notes, reservations and visible preview until exactly-once finalization.
- Use the existing interval-owned stop FIFO, capture staging and custody for successor overlap; a busy canonical finalizer alone must not block successor capture.
- Preserve ordinary admitted-stop behavior, transport-stop priority, stale-identity rejection, undo and persistence ownership.
- Explicitly amend the contrary live-recording/boundary-rederivation clauses of DEC-067 and DEC-078, rather than silently changing their meaning.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `capture-state-guards`: immediate overdub input closure, immutable Stop phase, isolated successor admission and exactly-once deferred completion.

## Impact

Primary owners: `Track` stop/start lifecycle, `Loop` capture/session data, existing serialized `LoopPlaybackRuntime` finalizer. Relevant files are `src/Track/TrackOverdubLifecycle.cpp`, `include/Track.h`, manager admission/state callers, and the existing `test/test_shared_capture_finalization` harness. Display reads must distinguish live tails from stopped preview without dropping content. No SD format change, second canonical finalizer or new Manager.

Brownfield references: [capture-state guards](../../specs/capture-state-guards/spec.md), [storage/capture guide](../../../docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md), [delivery overview](../../../docs/DELIVERABLE_TRACKING.md). Scope is separately declared in [CURRENT_WORK](../../../docs/Runtime/CURRENT_WORK.md); unrelated display/window and profiler changes remain separate.

## Evidence and non-goals

**VERIFIED:** the capacity branch retains a Stop while leaving `OVERDUBBING`; deferred drive supplies its current tick, not the latch's stored phase, to `job.phaseTick`. **VERIFIED correction:** `captures/session_20261030_221346.log` shows `OVERDUBBING -> OVERDUB_STOPPING -> PLAYING` at 127.931 s and a successor start at 128.916 s, not the originally described rollback. The general code defect is not proof that this particular capture exercised it.

**UNKNOWN:** whether this state-machine defect explains the reported temporary extension of already-closed notes. That visual symptom remains a separate acceptance claim, not assumed fixed by this slice. Board identity `e80c8e00…` is owner-reported; no programming proof has been acquired in this session.

Non-goals: host-rig changes, circular display-window repair, overlap-resolution changes, throughput tuning, unbounded pending intervals, storage migration, physical-SD deletion/quarantine, unrelated dirty-tree cleanup. Product intent is settled; implementation readiness must resolve the finite physical-exhaustion and continuation details before firmware edits.
