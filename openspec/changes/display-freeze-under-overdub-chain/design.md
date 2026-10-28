## Context

See [proposal](proposal.md) for scope and [investigation](INVESTIGATION.md) for retained captures.
Baseline is the dirty `feature/unified-rolling-playback-path` tree, not just Git HEAD. Preserve all pre-existing edits. Installed-image claims must use the recorded HEX hash; the current source already contains a later 4-grain/600-us burst variant and `STOP_GRAIN_BURST_DISABLED`, whereas the captured qualification image is described as 8 grains/1500 us. Neither comment is evidence of what was flashed.

## Goals / Non-Goals

Restore display progress under the same admitted chain while retaining stop throughput. Separate frame service from content freshness and source qualification. No new owner, source lifetime, queue, lease, persistence representation or MIDI timing guarantee. Scope excludes restore diagnosis and speculative boot repair.

## G0 — evidence and implementation trace

### VERIFIED paths and owners

- `main.cpp::loop` dispatches the existing deferred owners. `runDeferredLoadAndDisplayFrame` admits one active child using `activeLoadDisplayContinuation.select`; `finishAdmittedDisplayPaintTurn` calls `DisplayManager::update`, and advances `lastDisplayUpdate` only when no paint phase remains.
- `runDeferredIdleMaintenance` dispatches Snapshot separately, services canonical-prefix priority before the other realtime per-track maintenance, and invokes `Track::processDeferredIdleMaintenance` for eligible tracks. Therefore direct Track stepping alone does not establish main-loop fairness.
- The Track pending-stop branch handles the RecordPublished/Snapshot exception first, otherwise services the stop worker; its VisualCache opportunity can also advance the existing qualified source. It then returns. No change may accidentally remove that source opportunity.
- Stop bursting is overdub-only. The current branch preserves Record's one-grain separation. Existing burst tests in `test_shared_capture_finalization` prove queue progress but do not execute the display pipeline.
- `DisplayManager::emitDisplayTurnReceipt` suppresses unchanged fresh-viewport receipts. `disp_window` is also transition telemetry. `DFRAME` is sampled, not a receipt for every physical transfer. None alone proves per-frame latency.
- `test_live_capture_display_boundary` already exercises actual resolve/submission/completion, busy transfer, live inputs after resolve, pending successor stops, moving windows, retained-source undo/redo, geometry and focus invalidation.

### Lifetime and interaction map

Track owns stop descriptors and retirement. Loop owns passes, snapshots, qualified source candidates and overlays; DisplayManager owns compose/framebuffer phases. DMA owns the active transfer's bytes until completion. This change does not alter those lifetimes. Stop commit remains exactly once; persistence stays on StorageManager's admitted deferred path. Undo/clear/focus invalidation continues to reject stale source keys. `activeLoopIndex` remains sole active-slot authority.

### Resources and timing

No production container or content-sized allocation is authorized. Existing display source work remains the 8-unit producer described in [ExecutionBounds](../../../docs/Authority/Architecture/ExecutionBounds.md#publication-gap-display-source-continuation). Resolve remains empirically bounded and transfer fixed-size; a between-grain microsecond check is not a bound on the next grain. No tempo/count envelope expansion: use current Config and ClockManager setters. Test-only observation seams have no target cost; any required capture observation must be fixed-size, gated, RAM1-priced and registered before firmware edits.

### Historical decision review

Searched `docs/DECISION_LOG.md` for `burst`, `display`, `DEC-045`, `DEC-177`, `DEC-178`; read the applicable display/execution authority and existing DMA/load specs. Reuse: YES, existing display phases, source continuation and deferred dispatch. DEC-045 publication exclusions and DEC-177 prefix reuse remain intact. DEC-178 burst throughput is preserved, not rolled back as a workaround. No migration or new abstraction. See [Display authority](../../../docs/Authority/Architecture/Display.md).

### Verdict and unresolved evidence

G0 PASS for an observation/fixture slice: paths and ownership are identified. Exact regression cause remains UNKNOWN; the 16-second transition gap is not proof. G1 fixes the allowed architecture (same owners, same lifetime and qualification). A production repair packet is BLOCKED until the fixture names a failing boundary and the exact minimal mutation is recorded. CLI artifact readiness is not firmware-readiness approval.

## Decisions

1. **Use a two-slice sequence.** First establish a production-path regression and real boundary observations; then pin and implement the smallest repair to existing dispatch/admission. Do not choose a new lifetime model to explain missing telemetry.
2. **Observe service and freshness independently.** Count actual update phases/submission/completion using existing native display/DMA seams; check explicit seeded NoteIds in the rendered frame. The existing harness submits after three healthy admitted updates; assert those phase transitions and a separate controlled DMA completion, rather than expecting each update to finish a frame. Distinguish a moving playhead from a stationary content revision.
3. **Use the same trace for stop and display.** Extend the 22-overdub/20-owner-turn fixture shape, but run production owner ordering and paint opportunities; no 4000-turn rescue inside the measured chain. After the measurement window, bounded teardown is separate. Enforce the existing queue cap of 4 and exact final capture-pass count. Logical progress must occur within each granted display opportunity, or remain pending solely for a demonstrably busy DMA phase that resumes when released. This is not a wall-clock guarantee.
4. **Do not infer a repair from reduced burst constants.** A/B controls isolate only the burst mutation on the same dirty-tree snapshot and fixed input trace. Existing source exclusions predate the burst and are not a demonstrated regression by themselves. If a source-lifetime or publication-policy change is needed, return to design rather than bypass qualification.
5. **Keep physical qualification separate.** A same-scenario hardware run measures actual completed-frame age, content freshness, stop acceptance and MIDI service. Register numeric hardware thresholds and stimulus before the run; no invented bound from a sampled row. Native green alone cannot close the user-visible defect.

## Execution packet A — regression observation and fixture

- **Status:** specified; NOT RUN. Owners: existing main dispatch, Track maintenance and DisplayManager phases. Files: `src/main.cpp`, `src/Track/TrackDeferredMaintenance.cpp` (read-only initially), `test/test_live_capture_display_boundary/test_live_capture_display_boundary.cpp`, `test/test_shared_capture_finalization/test_shared_capture_finalization.cpp`, existing `test_midi_service_drain` harness. Add only minimal test observation/time/I/O seams if missing; do not copy domain state machines.
- **Preconditions:** seeded committed Record with explicit NoteIds, selected active slot, admitted successor overdubs, deterministic clock and controllable transfer completion. Include a long loop and a current-root control. Record full initial source/generation and dirty diff.
- **Oracle:** for each start, input pair, stop and owner rotation, retain the accepted request until its existing owner consumes it; capture queue depth, stop phase, source readiness, paint phase and submitted/completed generation. After due display opportunities assert actual phase progress; after DMA release assert pending newest frame completes. Assert original Record and qualifying closed overdub identities in the served interval exactly once. Final drain asserts 23 capture passes for the 22-overdub fixture plus its Record.
- **Counterexamples:** unchanged viewport with healthy frames must not fail; due update denied while no DMA fault must fail; frozen content despite healthy submissions must fail independently; disabling bursts must still trip the original throughput oracle where applicable.
- **Controls:** single stop, Record head, active DMA, live input after resolve, viewport wrap, selected/background track contention, focus/slot replacement, transport Stop, undo/redo and geometry invalidation. Reuse existing passing controls where they already cover these transitions.
- **Outcomes:** due/ongoing display = retained continuation; DMA busy = pending newest frame; completed = observed transfer completion, not transition text; stale source = refused for composition with previous coherent frame retained; accepted stop = consumed exactly once or visibly pending. Absence of a diagnostic is UNKNOWN, never completion.
- **Evidence:** `evidence/REGRESSION.md` records case names, exact commands, dirty-tree snapshot, A/B difference and first failed assertion. If the native trace does not reproduce, record that limit; do not call a passing control a reproducer. Use a narrowly scoped capture observation only after the authoring recipe, not a generic watchdog framework.
- **Commit boundary:** fixture/observability only on the working branch; no commit unless requested. No production policy mutation.

## Execution packet B — minimal repair and qualification

**Status: IMPLEMENTED (native + build green; not flashed).** Packet A named the predicates; DEC-179 records the
contract. Landed slices:

- **B1 — restore applies saved content.** `applyLoadedLoopSlotBodyCommit` no longer returns silently on parity
  divergence: a populated slot keeps its previous generation (`KeptExisting`), an empty slot applies the body's
  passes without the unverified root (`AppliedPassesOnly`, parity stays `NoCheckpoint`) and marks display caches
  stale. `commitLoadLoopJobPublish` maps the outcome to the terminal (`Superseded` for kept-existing) and writes
  one human row per case, so a restore can no longer report `committed` with nothing applied.
- **B2 — selected slot paints first.** The stopped-lane focus predicate accepts a root-only restored slot
  (`publishedSnapshotIdentity()`), and the realtime idle lane visits the selected track before the background walk.
- **B3 — painted content tracks pass growth.** `Loop::commitCapturePass` marks affected display cache ranges for
  every committed capture pass, not only `OverdubWrap`.

Discriminating fixtures: `test_4_2_a_parity_mismatch_into_an_empty_slot_restores_the_saved_passes`,
`test_painted_notes_follow_committed_passes_on_a_stationary_viewport` (fails before B3), plus the retained
populated-slot control `test_4_2_a_failed_parity_leaves_the_existing_slot_generation_intact`.

Original gate text follows for the record: before firmware mutation, fill into `evidence/REGRESSION.md`: exact
existing owner and refusing predicate, stepwise failing→passing oracle, minimal mutation, complete work-unit
bound and failure/cancellation effects. Preserve packet A's tests and the existing throughput oracle unchanged. No mutation to source lifetime, publication permission, stop admission or persistence is pre-authorized by this packet.

Allowed repair boundary: correct the demonstrated eligibility/continuation omission in the existing display/deferred paths, preserving both due paint/source opportunities and bounded stop service. If the evidence requires a different contract, stop and update design plus focused red-team review before writing implementation tasks for that change.

Validation after a repair: focused display, stop, service-drain and DMA suites; full `pio test -e native`; `pio run -e teensy41-capture-serial`; unchanged RAM1 budget and one focused independent lifecycle review. Then authorized same-scenario device evaluation with image hash and boot proof. Existing registered stimulus must be listed/reused, never edited in place. No automatic upload or SD manipulation. Update display/execution authority only where behavior actually changed, and append/supersede a decision only if its policy changed.

## Risks / Trade-offs

- Transition sampling mistaken for freeze → observe phase/completion separately; corrected investigation inference.
- Source is stale while physical frames continue → explicit content oracle, not just counters.
- Fixture bypasses orchestration → execute production dispatch order; success stubs cannot establish scheduling acceptance.
- Current source differs from flashed image → pin source diff plus HEX identity before every comparison.
- Fairness fix resurrects stop backlog → keep original throughput assertions and exact-content checks.
- Observation perturbs RAM1/timing → prefer host seams, gate fixed-size target observations and price them before build.

## Migration Plan

No SD or data migration. Build first; flash only with authorization. Retain the prior known image/hash for authorized rollback; do not revert shared source edits. Archive only after both device display and stop acceptance, never on artifact/native status alone.

## Terminology

No new domain noun. Frame service, qualified source, Capture, pass, slot and deferred owner retain their existing meanings.
