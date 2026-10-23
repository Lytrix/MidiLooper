# I5 — owner opportunity and input-dispatch timing reassessment

**Status: design review draft; no firmware approval or service PASS.** Follows [state/resource/service feasibility](STATE-RESOURCE-SERVICE.md). User requested continuing the next service/adoption design after the current bound failed. The newly verified input-batch dependency prevents solving the unchanged deadline with main-loop owner urgency alone.

## Trigger and current architecture

Formal trigger: **timing model changes**, specifically clock pulse handling / updateAllTracks delivery if preparation checkpoints are inserted before further queued clocks. See docs/ARCHITECTURE_REASSESSMENT.md §Formal triggers. Introducing persistent input continuation would also require an explicit state/lifetime contract under IMPLEMENTATION_READINESS. This draft does not adopt that change or create a new scheduler/Manager.

Existing owners remain: MidiHandler routes input, ClockManager advances ticks and calls TrackManager, Track owns window preparation/publication, main chooses off-clock deferred opportunities. Current main polls handleMidiInput before its selected deferred owner; the batch itself runs clocks and playback edges synchronously.

MidiHandler uses128-message batches normally or32 in post-overdub drain, independently on USB-device and DIN. USB-host similarly drains callbacks up to that cap. dispatchMidiBatch first applies MidiDispatchOrder::planDispatchOrder: Start/Stop/Continue move ahead within the batch, while Clock/channel relative FIFO is retained. Each routed Clock can invoke ClockManager::onMidiClockPulse, advancing8ticks and calling updateAllTracks (except the first pulse after Start). Internal clock independently invokes updateAllTracks from its timer path; input segmentation alone would not bound that path.

## New verified counterexample

A legal externally aligned trace starts at736 during established external transport: NoteOff, Clock, Clock, Clock, Clock. The actual production dispatch-order helper preserves indexes0..4. Arithmetic using Config::TICKS_PER_CLOCK gives736→744→752→760→768. The five messages fit both current32/128 caps. No main preparation opportunity exists between these dispatches, because dispatchMidiBatch does not yield to main.

Scratch witness `i5-clock-batch-witness.cpp` invokes the actual order helper and checks this arithmetic; it does **not** link full MidiHandler/ClockManager or establish live content acceptance. It is a source-backed structural counterexample, not a device loss or cadence measurement. Witness exits0 with `BATCH COUNTEREXAMPLE PASS`; log `i5-clock-batch-witness.log`.

Fresh `pio test -e native -f test_midi_dispatch_order -f test_midi_service_drain`:50/50 succeeded, exit0. Existing FIFO, transport-first and owner fairness tests remain green. No production/test source was changed. Log `i5-owner-design-focused.log`.

## Alternatives and recommendation

1. **More urgent main-loop playback turns only:** preserves current input protocol, but cannot insert work into a poll that already crosses the adoption boundary. Rejected as a sufficient solution, though useful as one part of a composed design.
2. **EDF replacing across-track round-robin:** would not solve the within-poll gap and conflicts with the current normative cross-track rotation requirement. Not proposed; preserve round-robin across tracks and earliest owing slot within a track.
3. **Build in a Clock callback or Capture ingress handler:** no late batching gap, but violates the no-build timing-edge contract and couples input/clock routing to publication. Rejected.
4. **Smaller read cap with no retained batch protocol:** reduces nominal batching but changes transport-first grouping. A NoteOn before a later Start must not be silently accepted under the wrong epoch because the poll boundary changed. Per-route caps also do not bound aggregate clocks or internal timer advancement. Not sufficient as an unexamined constant change.
5. **Preferred design direction: transport-preserving resumable dispatch plus main-owned checkpoints and bounded owner arbitration.** Retain the already-decided transport-first/Clock-channel order for a qualified batch; dispatch bounded segments, return to main, and offer one named preparation/source/competitor grain before consuming more relevant clocks. Existing owners retain their responsibilities; no inline callback build, new Manager, duplicate event authority or clock-message drop/reorder.

Option5 is only a direction: no approved segment length, complete-message quantum, extra-opportunity cap, continuation layout or numerical bound exists. No guarantee follows from calling it resumable. It may still fail under the actual workload or internal-clock interference.

## Contract that must be pinned before executable tasks

- **Continuation owner/lifetime:** where unread/planned route messages and dispatch indexes live, source eligibility, coherent epoch handling, single consume/exactly-once behavior, cancellation on transport/source transition, and total bounded resident footprint. Do not assume stack batch arrays can persist after return or introduce an unbounded queue. Price all routes and RAM1/PSRAM allocation/constructor costs.
- **Transport grouping:** preserve the established batch's Start/Stop/Continue-first behavior and Clock/channel FIFO while segmented; no second read/drain may overtake retained messages. Host callbacks require a coherent equivalent protocol, not merely USB-device logic.
- **Clock and input health:** clock bytes are neither dropped nor reordered; physical ingress backlog/residence and signed arrival/stamping remain measured. Limiting consumed clocks must not manufacture a readiness PASS by delaying musical time, skewing incoming-note timestamps or moving clock latency outside its declared limits.
- **Preparation checkpoints:** executed by main/existing deferred owners after input segment completion, never inside a Capture append/clock dispatch. Each opportunity performs one declared grain with no hidden stacking; source prerequisites and Track window grants are separately charged.
- **Fairness/competitor bounds:** retain cross-track round-robin and earliest slot within track. Bound consecutive deferrals of finalization/Snapshot/persistence/display/load obligations. Mode-wide priority, arbitrary ready flags and indefinite debt are not a contract.
- **Internal clock:** timer tick/playback may interrupt owner work, so segmenting external dispatch is not a proof for internal operation. Need clock/source revalidation and bounded indivisible owner execution in that path too; do not hold IRQs or freeze transport to make it fit.
- **Deadline:** unchanged legal adoption at768 after the late accepted input; no mid-traversal swap or oracle weakening. Demonstrate all source/fill/order/delta-merge/copy-back/publication/competitor work before that boundary. Old-version one-pass containment is a named failure of readiness, not success.

## Migration impact and review boundary

Likely files if a complete contract is later approved: existing MidiHandler dispatch/batch code, main's cooperative opportunity sites, existing MidiServiceDrain owner policy and Track request/builder path. ClockManager/TrackManager timing contracts must be traced but not silently modified. No SD schema, persistence owner or undo representation change is proposed.

Validation must start with controllable actual input-dispatch→owner→ClockManager/TrackManager fixtures, not only the helper/arithmetic witness. Cover Note-before-Start across continuation, repeated Clock/channel mixes, all physical routes, epoch/source transition mid-batch, late On/Off, ready/refusal/recovery, all admitted holders and competitor starvation. Frozen continuation bytes and consumed indexes must be independent asserted outcomes. Include actual resource pricing, native/build/RAM1 and one focused lifecycle/oracle review before registered hardware qualification.

Rollback retains current verified input dispatch and compatibility playback with default rolling OFF; neither is credited as new-reader readiness. No continuation or scheduler state is introduced in this draft, and no existing constant/spec threshold changed.

## Required decision

**Approval required before production code:** yes, for a complete clock-delivery/input-continuation timing contract, not merely this outline. User continuation authorizes developing the review but does not make unpriced state or an unmeasured bound accepted.

Recommendation is option5 for the next design work, with confidence high that main-only urgency is insufficient and moderate that a transport-preserving continuation can be made feasible. G1 — architecture closure remains open; G2 — execution packet cannot be written as ready until the continuation/resource/interleaving and numerical proof obligations above close. HITL is still not the first missing gate.

**Owner-approved contract development follow-up:** [I5 — concrete timing draft](TIMING-CONTRACT.md) now fixes one-route continuation ownership/order, candidate1064B continuation+16B mandatory-normal state, proposed urgent/ordinary alternation and conditional2*H*G checkpoint bound.19,531 design-model traces and599focused native cases pass. Physical-time reference/WCET, actual source/continuation implementation and production fixtures remain missing; user approved developing the contract, not firmware activation or a new budget. One checkpoint per input message is explicitly insufficient, and extra turns must end at independent clock/input deadlines rather than wait for readiness.
