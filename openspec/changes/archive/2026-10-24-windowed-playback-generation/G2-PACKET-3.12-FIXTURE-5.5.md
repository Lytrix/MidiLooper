# G2 execution packet — task 3.12 fixture 5.5 (legacy paged content)

## Identity and scope

- **Change/task:** `windowed-playback-generation`, task 3.12, fixture 5.5 portion only.
- **Owner:** existing `WindowPagedStream` producer/consumer primitives (`include/WindowPagedStream.h`) over `CommittedEventRange::appendWindowBudgeted` and committed chunk lists.
- **Slice scope:** add a focused native fixture proving bounded ring storage, exact-once output, ordering, wrap interval behavior, same-tick over-capacity content, and a note closure crossing a page boundary.
- **Non-goals:** no changes to production playback wiring, `Track::recordMidiEvents`, event/page capacities, MIDI behavior, persistence format, or service scheduling. No device timing or production-consumer claim.
- **Preconditions:** 3.12a accounting, 3.12b `WindowPagedStream`, and 3.12c MIDI-edge gate exist. Existing 700-event ring fixture is green but does not cover same-tick saturation, wrap-window order, or cross-page note closure.
- **Dependency boundary:** production consumption of the paged stream remains tied to task 3.7's fallback migration. This fixture does not close that dependency.

## Oracle fixed before fixture changes

1. **Same-tick density beyond L:** produce/consume an over-capacity interval with many events sharing a tick. Assert every event appears exactly once, equal-tick source order is preserved, no event is truncated, and the ring high-water mark never exceeds `kWindowPageCount`.
2. **Wrap window:** request a linear-modular W-wide interval crossing loop end using the existing range convention. The independent oracle is the source list filtered by the exact half-open modular predicate, preserving canonical source order (ascending stored tick order); playback phase projection is downstream. Assert every matching event is emitted exactly once.
3. **Closure across pages:** place a NoteOn before a page boundary and its NoteOff after it, with intervening same-tick/dense events forcing page reuse. Assert both rows preserve identity/order and are emitted once, with no consumer wait or tick reassignment.
4. **Backpressure/stale revision:** a full ring returns `RingFull`; consuming a page permits reuse; a foreign revision returns `StaleRevision` on produce and consume. The existing 3.12b fixture already covers stale revision; this slice explicitly pins `RingFull` and reuse.

The independent oracle is the source sequence filtered by the requested interval. Storage bound is the fixed three-page ring; throughput/deadline is not inferred from this bound.

## Validation

- Focused: `pio test -e native -f test_window_paged_legacy`.
- Existing stream regression: `pio test -e native -f test_runtime_working_set_ownership`.
- Broad: `pio test -e native`; `git diff --check`; `openspec validate windowed-playback-generation --strict`.
- No firmware build/upload or HITL claim for a test-only fixture.
- Evidence: [`evidence/2026-10-05-s312-fixture-55/EVIDENCE.md`](evidence/2026-10-05-s312-fixture-55/EVIDENCE.md).

## Completion record

- Fixture implemented; M-RING red-proof (do not recycle drained page) failed all three cases to terminate, then production header restored and confirmed diff-clean.
- `pio test -e native -f test_window_paged_legacy`: **3/3 passed**. Full `pio test -e native`: **2810 cases / 7 skipped / 2803 succeeded / 0 failed**.
- `openspec validate windowed-playback-generation --strict`: valid; `git diff --check`: clean.
- Device timing and production consumer: **NOT RUN / NOT APPLICABLE to this fixture-only slice**; task 3.12 remains open for timing and 3.7 consumer integration.
