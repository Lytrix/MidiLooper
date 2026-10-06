# Task 3.7a — I4 integrated parity and timing admission

G2 packet for the remaining I4 increment of OpenSpec task 3.7a — differential parity and admission behavior for the retained-source/page path. Authority: `IMPLEMENTATION_READINESS.md` §G2, task 3.7a, DEC-108 §§13–14, DEC-114, and the existing 3.7a packet at `evidence/2026-10-06-s37a-production-consumer/EXECUTION.md`.

**Status: native/evidence verified (test/evidence-only closeout); no production policy change or device qualification.**

## Identity and scope

- **Increment:** task 3.7a / I4 — integrated production-path parity and timing-admission outcomes.

- **Owner/path:** `Track::stepPlaybackViewMaintenance` advances the off-clock producer; `Track::consumePagedPlaybackWindowForSlot` owns page consumption; `Track::playCommittedLoopMidi` selects an admitted page stream or the retained merged fallback. `Track` owns per-slot playback runtime and page/source lifetimes; `Loop` remains effective-content and checkpoint owner.
- **Primary files:** `test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp`; existing I4/I5/I8 evidence and this packet. Production files are not expected to change.
- **Preconditions:** I1–I3 and I3c emission selection exist; production tick driver, Track MIDI sink, page producer/consumer and root-vs-retained comparison fixtures exist; fallback remains present.
- **External limit:** task 3.10's service envelope remains NOT QUALIFIED and the lane-enabled qualification image fails its RAM1 guard. Native observations do not establish physical timing or supported production enablement.
- **Non-goals:** no gate/policy enablement, capacity or service-constant adjustment, no 3.7b, 3.14 or 4.5, no HITL/upload, no row reclassification.
- **Commit boundary:** test/evidence-only increment, independently buildable; fallback and production gates unchanged.

## Oracle and existing fixture mapping

Use the existing fixtures rather than duplicating their established production-path coverage. Together they form independent links in the parity chain: the clock path's paged output matches `Loop::copyEffectiveCommittedEventsInRange` for one pinned revision, and the retained-source gather matches the current published-root window/fallback enumeration for the same revision and interval rules. Admission is evaluated separately from the content oracle.

| Case | Fixture and drive | Required observation | Counterexample |
|---|---|---|---|
| **I4-PAGE-OUTPUT** | `test_3_7a_i5_a_rootless_demoted_slot_is_served_from_the_ring_pass_exact`; real `Track::playMidiEvents`, off-clock page turn, production MIDI sink and wire recorder. | A complete pass's emitted `(tick, type, channel, note, velocity)` equals the independent effective-content answer, exactly once; the event interval exceeds one page, crosses a page/pass boundary and respects source revision. | A dropped/duplicated/reordered event or an unadmitted page delivery selected for emission fails. |
| **I4-FALLBACK-PARITY** | `test_3_7a_i8_the_two_lanes_serve_one_revision_identically`, `test_3_7a_i8_the_two_lanes_agree_at_every_window_across_the_loop`, `test_3_7a_i8_a_revision_only_one_lane_holds_is_refused_by_name`, and `test_3_7a_i8_an_edited_revision_is_resolved_the_same_way_by_both_lanes` (`W-37a-27`…`W-37a-30`), plus `test_3_7a_i5_a_rootless_demoted_slot_is_served_from_the_ring_pass_exact`. | Published-root/fallback window enumeration equals the retained-source answer for the same current revision; the production paged output independently equals its retained-source effective-content answer. Do not overclaim event-ID or untested cross-layer tie semantics. | Removing a source branch, using the wrong revision/interval, or differing wire content must fail its owned comparison. |
| **I4-ADMITTED-RATE** | `test_3_7a_i4_the_paged_delivery_names_a_halved_service_rate`, with one producer turn per declared opportunity in the passing control. | Existing admission state and pass accounting distinguish a delivery that remains within the declared cadence from one that falls behind; no production gate is changed. | Selecting a merely bound/unproven or stale delivery must fail existing admission assertions. |
| **I4-LATE-SERVICE** | Same rate fixture, with producer service intentionally halved. | The missed service is named as dry/unstraddled; page content is not represented as a complete successful pass. This is a native logical-rate result, not a device deadline measurement. | If the reduced service rate silently appears complete/admissible, the fixture fails. |

## Controls and coverage

- Reuse `rollingTestTick`, `Track::playMidiEvents`, existing `Track` MIDI sink, page APIs and root/retained parity fixtures. No copied state machine or on-path success stub.
- Preserve `(slot, playbackGeneration, playbackRevision, interval)` identity, exact page-boundary/wrap behavior and the served/fallback path.
- The existing I4 rate fixture uses `drivePagedEmissionFrame`; I5 supplies the production tick/emission leg. If running these fixtures reveals that the rate assertion is not connected to the production selection outcome, pause and add a separately packeted fixture rather than claiming that link.
- The existing numeric cadence is exercised only as the declared model. The support envelope remains open under tasks 3.10/6.3/6.5.

## Verification recipe

- Focused: `pio test -e native -f test_runtime_working_set_ownership`; confirm the I4 rate, I5 production emission and W-37a-27…30 parity cases are registered and pass.
- Broad: `pio test -e native`; report skipped suites/cases separately.
- Build/RAM1: `pio run -e teensy41-capture-serial`; report actual guard line. No qualification build is required for this test/evidence increment; no upload.
- Hygiene: `openspec validate windowed-playback-generation --strict`, `git diff --check`, and verify `git diff --stat -- src include` is empty.
- **Device: NOT RUN.** No hardware timing result is inferred from native logical stepping.
- Baseline: `0d1144d9` plus the pre-existing `docs/Runtime/CURRENT_WORK.md` scope note. Evidence remains in this directory.

## Completion record

- Oracle/contract: unchanged. Native logical admission proof only; timing/service envelope remains unqualified for production support.
- Existing fixtures verified: `test_3_7a_i4_the_paged_delivery_names_a_halved_service_rate`, `test_3_7a_i5_a_rootless_demoted_slot_is_served_from_the_ring_pass_exact`, and `W-37a-27`..`W-37a-30` are registered and passing. No new fixture or mutation was added/run in this closeout; prior mutations are recorded in their increment evidence.
- Focused: `pio test -e native -f test_runtime_working_set_ownership` — **176/176 passed**.
- Full native: `pio test -e native` — **2,855 total / 7 skipped / 2,848 succeeded / 0 failed**.
- Build: `pio run -e teensy41-capture-serial` — **SUCCESS**; RAM1 `variables 96,192`, `code 392,248 + padding 968`, locals `34,880`, **128 B** above 840 B floor.
- Hygiene: `openspec validate windowed-playback-generation --strict` valid; `git diff --check` clean; `git diff --stat -- src include test` empty.
- Device: **NOT RUN**; no upload or hardware timing/content claim.
- Task 3.7a: checked complete with fallback retained. 3.10/6.3/6.5, 3.14, 4.5 and 3.7b remain open; none checked off here.
