# Tasks — overdub stop-edge closure

One firmware slice, not part of display-window or profiler implementation. Contract, ownership,
pre/postconditions, invariants, non-goals, interleavings and commit boundary are in
[design.md](design.md#execution-packet--one-stop-edge-implementation-slice). All checkboxes describe
implementation/verification work; planning artifact existence does not complete them.

## 1. Fixture-first transition oracle

- [x] 1.1 Add the capacity-wait principal fixture in `test_shared_capture_finalization` using actual Stop, controlled ticks, resource refusal/recovery and ordinary deferred turns; verify pre-fix failure at the immediate non-Overdubbing/no-post-Stop-row assertion, and record the full failing trace in `evidence/EXECUTION.md`.
- [x] 1.2 Add its successor-overlap legs through manager and direct Track Start, proving no retained-input reset under physical cut exhaustion, then A handoff and B capture before A retires; verify pre-fix discriminating failure and pin independent A/B event, NoteId, reservation and publication expectations.
- [x] 1.3 Add the uncontended admitted-stop principal control with exactly one seal/publication/undo unit and duplicate-drain assertions; run the existing implementation and record its result without inventing a RED if it already passes.
- [x] 1.4 Add focused controls for transport Stop with/without a queued predecessor, slot-switch-before-binding, real maintenance transfer-horizon recovery, empty/populated canonical-output waits, stale identity and request supersession; verify the discriminating failures or reused passing cases are named and registered.
- [x] 1.5 Add the stopped-preview wrap/control fixture with an already-closed note, held note and pending Shorten/Hide; verify the expected geometry is independent of moving playhead and successor geometry stays live.

## 2. Implement the one stop-edge slice

- [x] 2.1 Implement design decision 1 in Track: immediate input-state closure, immutable first phase, matching closed-interval retry and exactly-once interval handoff; verify the principal closure fixture passes and late NoteOn/Off/CC cannot extend it.
- [x] 2.2 Separate physical descriptor/cut admission from canonical worker binding per decision 2; remove eager readiness bypass and replenish only empty unowned output on ordinary deferred turns; verify empty-output recovery, populated-output preservation, and one active canonical finalizer.
- [x] 2.3 Implement decision 3 before any Start prewarm/reset/claim: hand off retained Stop before successor admission, preserve its reservation ownership and retain distinct Start continuation on genuine physical exhaustion; verify manager/direct overlap legs start B before A retires and never replace A storage or latch.
- [x] 2.4 Implement decision 4's slot-qualified switch guard, closed-interval transfer-horizon eligibility, retained-stop transport dispatch/retarget and held-note preservation across output silencing; verify ordinary-turn recovery and all transport/switch/stale-identity controls without injected success or changed closure phase.
- [x] 2.5 Implement decision 5 through the existing display-source boundary, retaining preview/pending transforms and its phase until job handoff; verify the preview fixture and affected existing display-boundary suites, without a blanket live-tail freeze.
- [x] 2.6 Update the affected `ExecutionBounds.md` inventory and durable capture guide to describe actual waiting/worker paths; verify classification does not promote timing guarantees, no SD/undo schema changes exist, and no unrelated dirty hunk is included in the slice diff.

## 3. Verification and closeout

- [x] 3.1 Run focused native suites, then full `pio test -e native` in background alongside `pio run -e teensy41-capture-serial`; record exact commands, required case presence, totals/skips/errored suites and unchanged target RAM1 budget in `evidence/EXECUTION.md`.
- [x] 3.2 Verify the implemented slice against `evidence/REVIEW.md`'s concrete findings and `docs/Agents/reviewer.md`; record the focused disposition and retest concrete repairs without a broad review loop or weakening assertions.
- [ ] 3.3 Pin the reused registered stimulus, unchanged 20-bar chain reproduction and authorized device target in a HITL recipe; after a successful target build retain/hash the HEX, upload and independently verify fresh crash-free/load-ok boot; run the authorized content gate while native continues and record row-level Stop/Start/content verdict plus capture-loss limits. Leave this task open if target authorization or hardware is unavailable.
- [x] 3.4 Update `PROJECT_STATE.md`, `CURRENT_WORK.md` and shipped tracking only to the evidenced completion level; verify every required native/build/device gate passed before marking the behavioral slice complete or archiving, and keep the already-closed-note visual symptom explicitly unproven unless reproduced and resolved.
