# First-record playback admission — deferred launch rejected

**User decision:** Deferred first-record launch is rejected. The looper must remain synchronized and return to playback at the existing final-bar boundary. Prepare playback during recording, with bar 1 established at record start and maintained ready for that return. The recommendation below is retained only as rejected review history; it is not implementation authority. See the current design and normative scenarios.

## Problem and evidence

The 2026-09-15 09:52 HITL capture proves that the first snapshot reaches playback at phase 375 with `lastTickInLoop == UINT32_MAX`. The runtime treats this as fresh-start catch-up and sends earlier notes in a burst; nested send time reaches 144.102 ms. See `FOCUSED-STOP-HITL.md`, “Device-confirmed fresh-origin catch-up”.

`Track::stopRecording()` commits the capture, calls `startPlaying(playbackTick, true)`, and requests deferred snapshot work. `startPlaying()` resets the playback cursor to the fresh-origin sentinel. `playCommittedLoopMidi()` returns on an empty source before advancing interval bookkeeping. These three behaviors expose PLAYING without a usable committed source. Cursor advancement alone would exchange the burst for omitted notes and would not provide held-note boundary semantics.

## Recommended explicit contract extension

Extend the existing deferred-admission model to the first record-stop. This changes the time at which the first recorded take becomes audible; it is not currently an accepted exception to record-stop playback parity.

1. A first-record stop closes input at the requested tick and freezes the accepted take, including all required terminal NoteOffs and final loop geometry. Preserve all accepted events. Keep the existing `STOPPED_RECORDING` state while preparing playback; expose it as preparation in the UI rather than reporting PLAYING.
2. Prepare the initial immutable playback view, ordering, cursor anchor, and ledger boundary in bounded owner turns between MIDI services. Existing tracks continue playing. No synchronous full gather/sort or allocation is added to the MIDI tick. Use existing runtime/view and continuation ownership, not another scheduler.
3. After a complete source is ready, launch this track at phase zero on the next boundary of its original loop grid. Do not rewind the global clock. If ready too late for a boundary, leave the take pending until the following boundary; never catch up old notes in a burst or silently skip the beginning of the take.
4. Select source, projection origin, initial cursor, and empty playback ledger atomically at launch. Record-time held notes are closed/silenced under the stop policy; the new playback cycle emits its own canonical NoteOn/Off sequence, including canonical wrap closures.
5. Transport Stop always cancels the pending audible launch and selects STOPPED. Preserve the frozen take and complete canonical recovery under STOPPED policy. Reject a new recording/overdub command while first-record preparation owns that take; never clear it as an apparent successful commit.
6. Bind undo/persistence publication to the successfully prepared canonical take, using the existing command-epoch and ownership-transfer rules. The preparation interval must not be falsely treated as idle for load, persistence, or DMA scheduling.
7. Establish the required first-record work/capacity bounds separately. The provisional 6,240 overdub grains are not automatically a reservation for this path. If the take cannot be prepared under active policy, preserve it with an explicit STOPPED recovery outcome rather than silently truncating it or running unbounded work.

## Implementation and validation after approval

- Extend the normative scenarios and task scope for first-record preparation and grid-qualified initial launch; update the ownership/decision review for this extension.
- Reuse `STOPPED_RECORDING` plus a runtime-owned continuation, frozen source identity, command epoch, and requested destination. Audit every state predicate and control transition before making the state persistent across turns.
- Add differential/native fixtures for the confirmed phase-375/sentinel case, preparation crossing multiple loop boundaries, held notes, empty take, same-phase Off/On, geometry truncation, unavailable capacity, and Stop before publication or launch. Require the canonical first complete audible cycle exactly once.
- Telemetry must retain stop request, preparation grains, source readiness, selected launch boundary, actual first emission, and Stop cancellation on the device clock. Score preparation latency separately from MIDI-boundary blocking.
- Run the authorized track-5/slot-1 fresh-record HITL followed by normal overdub stop and transport-Stop interruption. Keep tasks 2.3–2.5, 3.3–3.4, 4.1, 5.3 and 5.4 open until their complete gates pass.

## Alternative: retain immediate first-record playback

Preserve the existing launch time by preparing an audible source before the record-stop command can arrive. This requires first-record input reservations and a rolling candidate that handles unknown final length, truncation, ordering, and held-note closure without post-stop unbounded work. Its capacity/rejection policy must be specified for first recording; the approved overdub policy cannot silently be applied to it. This is a larger extension than the deferred first-record admission above.

## Review status

No firmware changes implement this proposal. The current runtime still has the verified first-record source-admission defect. Approval is requested specifically for the preparation delay and phase-zero launch on the original loop grid, including transport-Stop precedence and preserved-take recovery.
