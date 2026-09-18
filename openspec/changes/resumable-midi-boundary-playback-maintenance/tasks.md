## Resumable MIDI boundary playback maintenance — task checklist

Implementation detail, open-item status and the session journal were relocated to
[`docs/Runtime/CURRENT_WORK.md`](../../../docs/Runtime/CURRENT_WORK.md#resumable-midi-boundary-playback-maintenance--relocated-task-journal)
by `runtime-code-tidyup` task 7.4; each line cites its own `Task N.N` entry there. This file is the
checklist only. Checkbox state is unchanged by the relocation.

## 1. Decision and baseline gates

- [x] 1.1 Complete `docs/Templates/DECISION_REVIEW.md`, the OpenSpec preflight [detail][t-1-1]
- [x] 1.2 Capture an overflow-free active-transport baseline that separates each `playbackOrderDirty` cause and every `load_frame` child [detail][t-1-2]
- [x] 1.3 Add the affected `ExecutionBounds.md` rows and a capture-gate note for order-build and load-frame grains [detail][t-1-3]

## 2. Resumable playback-order successor

- [x] 2.1 Add fixed, external-memory-first playback views containing events, order, source/projection identity, cycle-qualified `EmissionCursor`, cursor anchor [detail][t-2-1]
- [x] 2.2 Implement phase/index fill and bottom-up merge ordering as independently bounded, resumable grains, including canonical equal-phase Off-before-On ordering [detail][t-2-2]
- [x] 2.3 Implement continuous queued capture in reviewable slices: (a) add a failing reproducer through Button A, TrackManager [detail][t-2-3]
- [ ] 2.4 Change active `playCommittedLoopMidi()` order handling from synchronous full rebuild to request/consume behavior while retaining the complete in-place builder… [detail][t-2-4]
- [ ] 2.5 Differential-test the published/ready/build/bridge path against the complete oracle across record stop, phase-zero overdub-wrap admission, an append just before wrap [detail][t-2-5]

## 3. Resumable load/display-frame orchestration

- [x] 3.1 Add fixed continuation state to the `main.cpp` load/display owner that selects exactly one expensive active-transport child per eligible turn [detail][t-3-1]
- [x] 3.2 Retain the existing resolve/draw/transfer display state machine and DMA polling, but make its one admitted phase an explicitly selected load/display child [detail][t-3-2]
- [ ] 3.3 Keep `DeferredJobScheduler::runFrame(budgetUs)` as the existing load-job child, but expose its next phase and isolate its turn from other active-transport children [detail][t-3-3]
- [ ] 3.4 Replace synchronous first-commit playback prewarm in a timing-critical load frame with bounded prewarm grains [detail][t-3-4]
- [x] 3.5 Preserve post-overdub and `OVERDUB_STOPPING` MIDI polls and persistence non-stacking around the selected child [detail][t-3-5]
- [x] 3.6 Fix PLAYING display starvation: require actual pending restore work for active load admission/retention, prioritize admitted due display frames between load slices [detail][t-3-6]

## 4. Attribution and capture gates

- [ ] 4.1 Extend capture-only telemetry with owner, grain kind, input quantum, capture/staging/frozen generation, NoteReservation allocation/closure/rejection, stop-admission state [detail][t-4-1]
- [x] 4.2 Add a host-side timing report that scores RECORDING, PLAYING, OVERDUBBING, and `OVERDUB_STOPPING` intervals [detail][t-4-2]

## 5. Verification and closeout

- [x] 5.1 Run the focused native suites for playback ordering, MIDI service drain, telemetry, capture tiers, slot load, and new continuation fixtures [detail][t-5-1]
- [x] 5.2 Build the capture-serial firmware and verify RAM1/firmware-size limits remain within the project gate. [detail][t-5-2]
- [ ] 5.3 Run overflow-free HITL record, overdub, wrap, canonical-versus-pending-audible admission, normal `OVERDUB_STOPPING`, transport Stop during `OVERDUB_STOPPING` [detail][t-5-3]
- [ ] 5.4 Remove the direct active-transport full order rebuild and compound load-frame compatibility paths only after the previous gates pass [detail][t-5-4]

Order for this change: **2.5 next** (see the relocated open-item status).

[t-1-1]: ../../../docs/Runtime/CURRENT_WORK.md#task-11
[t-1-2]: ../../../docs/Runtime/CURRENT_WORK.md#task-12
[t-1-3]: ../../../docs/Runtime/CURRENT_WORK.md#task-13
[t-2-1]: ../../../docs/Runtime/CURRENT_WORK.md#task-21
[t-2-2]: ../../../docs/Runtime/CURRENT_WORK.md#task-22
[t-2-3]: ../../../docs/Runtime/CURRENT_WORK.md#task-23
[t-2-4]: ../../../docs/Runtime/CURRENT_WORK.md#task-24
[t-2-5]: ../../../docs/Runtime/CURRENT_WORK.md#task-25
[t-3-1]: ../../../docs/Runtime/CURRENT_WORK.md#task-31
[t-3-2]: ../../../docs/Runtime/CURRENT_WORK.md#task-32
[t-3-3]: ../../../docs/Runtime/CURRENT_WORK.md#task-33
[t-3-4]: ../../../docs/Runtime/CURRENT_WORK.md#task-34
[t-3-5]: ../../../docs/Runtime/CURRENT_WORK.md#task-35
[t-3-6]: ../../../docs/Runtime/CURRENT_WORK.md#task-36
[t-4-1]: ../../../docs/Runtime/CURRENT_WORK.md#task-41
[t-4-2]: ../../../docs/Runtime/CURRENT_WORK.md#task-42
[t-5-1]: ../../../docs/Runtime/CURRENT_WORK.md#task-51
[t-5-2]: ../../../docs/Runtime/CURRENT_WORK.md#task-52
[t-5-3]: ../../../docs/Runtime/CURRENT_WORK.md#task-53
[t-5-4]: ../../../docs/Runtime/CURRENT_WORK.md#task-54
