## Context

See [proposal](proposal.md) for motivation and [investigation](INVESTIGATION.md) for call paths, resource lifetimes, measured device observations and classifications. This is an existing-owner extension, not an implementation-ready claim merely because artifacts exist. User explicitly approved pre-admitted per-interval sealed-range custody inside Track with a measured cap and no new Manager/schema.

The installed queued-only image74392e15 remains unchanged. This proposal replaces its publication dependency only when the immediate Start **and early Stop** contracts are implemented and qualified. Existing capture guards, canonical Commit, single rolling playback and snapshot/persistence authority remain binding.

## Goals / Non-Goals

**Goals:**

- Seal Record A, immediately admit B into independent live Capture, seal B at its stop, and admit/seal C while A finalizes; retain exact accepted coordinates and independent source identity.
- Reuse the ordinary live/frozen admission model, pre-admitted capacity and serialized workers; prepare source-dependent work without blocking input.
- Preserve successor content/geometry/undo through late A publication or empty result.
- Remove content-sized visual fallback work from real-time service intervals using a reachable existing-owner continuation.

**Non-Goals:**

No new Manager/scheduler, SD format, second scheduled reader, cloned synchronous legacy stop, arbitrary budget increase, whole-take derived buffer, silent cap/feature reduction, or automatic device-content authorization. Faster normalization alone is insufficient. The pre-existing viewport-truncation issue is not bundled unless needed for source/presentation correctness.

## Decisions

### 1. Separate sealed custody from canonical worker ownership

Track remains owner of interval identity and sealed-source custody, TrackManager of actual admission. Extend externally allocated active-role storage, not the eight-way Track value or per-recorded-slot derived allocations. Each accepted sealed interval needs a stable generation-qualified custody receipt; one canonical worker/output remains serial. The interaction FIFO and a presentation reader reference custody independently so job retirement never invalidates an extent still being served (preserve DEC-142).

An ordinary stop boundary consumes pre-owned capacity and seals without copying/scanning event bodies. Source/directory/tail capacity must be proven before the supported capture overlap is admitted. Added metadata cardinality and retained body overlap are measured, never inferred from FIFO depth. The minimum acceptance trace contains A and two early stopped overdubs B/C; indefinite command bursts are not silently claimed.

**Rejected:** remove `hasPendingDeferredOverdubStop`, reuse `presentations[slot].rawRange`, or assign `captureStopOwner` to B while A uses it. Those violate verified custody. Also reject retaining Stop while appending beyond the requested endpoint: it moves the problem, not the boundary.

**Required before firmware:** exact stable receipt representation, fixed cap, reference/release protocol, target byte/reserve price and exhaustion behavior consistent with acceptance authority. The user approved the direction, not an unmeasured numeric envelope. Firmware tasks for this mechanism are intentionally withheld until this contract is concrete. [Contracts](CONTRACTS.md) C1 — independent sealed custody and early Stop fixes the receipt, obligation mask, release protocol and denial tokens; C2 — total reserves and pre-admitted cap fixes the charge sites and the derivation, while the cap value itself stays BLOCKED on target measurement.

### 2. Keep live ingress independent of private canonical publication

A stopped Record source is immutable raw Capture, not a committed pass or root. Reuse the same logical playback owner with source-qualified complete windows; never expose private canonical prefixes as current Commit. Candidate source work belongs under existing Loop/runtime preparation owners and should run ahead during Record where possible.

Input timestamps/identities cannot wait for source reconstruction. Pending same-pitch/held/Hide/Shorten decisions must carry causal identity and complete before their audible/canonical effect is required. The accepted B/C trace must compare against a sequential canonical oracle; merely accepting unrelated pitches is not sufficient.

**Rejected:** empty committed-pass fallback, stale snapshot, uncovered display cache, whole-loop source reconstruction on Start, or an unbounded queue of unresolved transforms.

**Required before firmware:** select and prove the exact bounded source recipe and its service/affected-tick deadline, plus supersession when A publishes. This is not delegated to Apply as a helper decision. [Contracts](CONTRACTS.md) C3 — private-source preparation and deadline fixes the distinct non-committed origin, the 32-unit cursor, the single reachable wakeup, the semantic deadline and the supersession rule.

### 3. Finalization publishes predecessor-local receipts only

Record normalization and pass transfer stay on existing deferred workers. Late A publication may advance committed base but cannot reset B/C producer state, clear pending overlap changes, erase successor undo entries or reset live geometry. Record undo attachment preserves causal operation order; empty A must leave accepted B/C valid rather than calling a destructive shared reset. StorageManager persists through existing admission/drain and committed checkpoint rules.

The exact successor-safe predicates and undo/geometry receipt protocol require an explicit architecture decision if lifetime/undo semantics change. No new undo kind, changed routing or early private root authority is silently introduced. The old queued-only Start latch remains a recovery/insufficient-resource carrier, not success for supported immediate operation. [Contracts](CONTRACTS.md) C4 — successor-safe undo and empty cleanup fixes the predicates, the scoped undo erasure and the persistence responsibility, and C4.6 names the decision that must be ratified before production code.

### 4. Bound the visual fallback at the actual indivisible work boundary

`Loop::rebuildVisualCacheIdleSlice` remains the visual-cache owner; DisplayManager remains presentation owner. Its snapshot/prepared miss must not execute whole-pass `resolveDisplayNotes` in one real-time turn. Use existing resumable source preparation where it supplies exact pass-local pairing/edits, or extend that same owner with a persistent bounded continuation. Include scan, pairing, transform, compose/append and retirement cost. Never lower bars-per-slice and assume the whole-pass branch is bounded.

Retain a complete qualified frame/interval until the replacement is complete. A waiting state is valid only if the existing production scheduler runs a named producer; withholding display work while no other owner produces its prerequisite is a deadlock, not safe deferral. Background tracks are part of the fixture, because the capture does not identify the slow visual caller.

**Required before firmware:** concrete continuation/source producer, stale cancellation, per-turn bound/cap and eventual-progress proof. Do not promote a child/owner bound based on native wall-time or a checked-after-call microsecond budget. [Contracts](CONTRACTS.md) C5 — reachable bounded visual continuation fixes the producer (the existing prepared-index session), its wakeup, its declared per-turn unit, its cancellation keys and the forbidden whole-pass route; the device service envelope stays a separate measurement gate.

### 5. Freeze the resource/timing contract before claim or checkoff

Use Config counts and actual ClockManager setters, not roadmap targets. Initial fixtures use the observed32/33-bar long-loop class with held notes, same-pitch changes and background contention; short-loop controls prevent a route split. Parameter sweeps cover accepted density, loop length, active holders and retirement overlap. Pricing includes live input, sealed A/B/C, presented/retiring sources, worker scratch, roots/undo, visual/persistence and recovery reserve with shared bodies charged once.

Define numeric command-to-ingress, command-to-stop, input-service gap, output lateness and allowed service gaps in a readiness packet before device qualification. No value is invented from one later warm cycle. Gesture-recognition latency is measured separately. Unset bounds are NOT PASS.

## Readiness and execution packets

### P1 — custody and source proof (test/measurement only)

Owner: existing Track/Loop/runtime test harnesses. Preconditions: current installed/source evidence and approved existing-owner custody direction. Mutation: test-only fixture control at clock, allocator and I/O boundaries; scratch measurement probes, no firmware behavior. Primary fixtures: shared finalization, continuous capture and live display boundary.

Oracle: clear→Record A32/33bars→seal A→withhold canonical publication→Start B→accept a distinctive same-pitch/held pair→Stop B→input after stop cannot enter B→Start/Stop C→run workers→exact A/B/C inventory, geometry, undo/save and stable raw identities. Busy/stale/clear/load/slot/focus/Stop/clock-change/empty branches are controlled. Existing production should fail specifically on publication-gated Start and early Stop/custody, not compilation or manufactured success.

Outputs: exact before/during/after owner map, stable-lifetime receipt contract and target/host bytes; producer/wakeup recipe for private source and visual misses; numeric supported envelope/service oracle; concrete G2 packets for production slices. If a required capacity cannot be admitted, return the measured conflict rather than narrow the trace or lift guards. This packet can be applied as tests/measurement before firmware readiness.

### Production packet generation boundary

After P1 resolves architecture-changing unknowns and the required reassessment/DEC update is ratified, generate separate concrete packets for (a) sealed custody/admission including early Stop, (b) qualified source and immediate input handoff, (c) successor-safe finalization/undo/persistence, and (d) bounded visual reconstruction. Each must name exact mutation/preconditions/outcomes, bound and fields, cancellation/retirement, independent oracle, commands and commit boundary. [Contracts](CONTRACTS.md) now fixes those contents — (a) = C1 + C2, (b) = C3, (c) = C4, (d) = C5 — and [the price annex](evidence/CONTRACT-PRICE.md) measures the C2 metadata tier, so packet generation is gated only on the C2.4 backfill terms and the C4.6 ratification, not on further design. These production tasks are not generated speculatively now.

## Risks / Trade-offs

- [Single range or premature receipt reuse] → per-interval stable custody proof including presentation retirement; no raw pointer lifetime assumptions.
- [Memory grows with accepted overlap] → pre-admitted measured active-role cap and reserve; do not price only112-byte host metadata or duplicate event bodies.
- [Input accepted but overlap source late/wrong] → causal receipts, source-qualified windows and affected-tick proof before handoff claim.
- [Late A cleanup destroys B/C] → specific raw/content/history/geometry assertions at each continuation and all-filtered A control.
- [Visual wait has no producer] → production owner rotation test and eventual exact completion, not only retained-frame silence.
- [Native timing mistaken for hardware proof] → declared device measurements with intact evidence and matched first/later-cycle workload.

## Architecture reassessment and migration

Triggers: existing single-slot sealed lifetime is insufficient for same-slot early Stop; successor-safe undo/empty-result semantics and real-time indivisible visual work need explicit contracts. Current owner/extension points are established above. User chose existing Track custody extension; no ownership transfer or schema migration is proposed. Preserve ordinary stop/source/currency safety until the replacement proof passes.

At production application, ratify the concrete custody/undo/bound decision, then amend DEC-158 and the active shipping delta's queued-only scenario to reference the new behavior. Keep the old image74392e15 as a hashed rollback artifact and retain safety/recovery paths until qualified; do not reclassify queued-only behavior as immediate. Native, shipping build, RAM1/PSRAM/pool and registered device gates precede checkoff/archive. No upload/content operation follows from proposal approval.

## Readiness verdict

Artifacts define the requested behavior, a bounded verification packet and now a concrete contract for
each previously unresolvable term: [contracts](CONTRACTS.md) C1 — independent sealed custody and early
Stop, C2 — total reserves and pre-admitted cap, C3 — private-source preparation and deadline,
C4 — successor-safe undo and empty cleanup, C5 — reachable bounded visual continuation. **Readiness passes
at contract level; it is not a firmware claim.** The cap is derived
([`K = 5`, `R_rec = 5 chunks`](evidence/CAP-DERIVATION.md)), the undo/lifetime semantics are ratified by
[DEC-160](../../../docs/DECISION_LOG.md), and all four production packets exist
([P-A](packets/P-A-CUSTODY.md), [P-B](packets/P-B-SOURCE.md), [P-C](packets/P-C-FINALIZATION.md),
[P-D](packets/P-D-VISUAL.md)) with their named REDs demonstrated. C2.4 remains a first-slice pre-flight
that cannot change `K`. Nothing is implemented: no acceptance profile has moved, and native, build and
device verification for every packet remain NOT RUN. These are explicit readiness conditions, not deferred
'Open Questions' inside implementation; a contract is not a measurement.
