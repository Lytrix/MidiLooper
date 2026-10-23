# Task 3.12 / fixture 5.5 — bounded paged legacy stream characterization

**Date:** 2026-10-05  
**Scope:** test-only native fixture; no production code behavior changed.  
**Baseline:** working tree after task 3.12a/b/c and `WindowPagedStream` existed.  
**Contract:** DEC-108 §14; `tasks.md` task 3.12 and fixture 5.5; packet `G2-PACKET-3.12-FIXTURE-5.5.md`.

## Fixture

New suite `test/test_window_paged_legacy` exercises the existing `WindowPagedStream` and `CommittedEventRange` primitives:

1. 2,400 same-tick NoteOns across two ordered source lists (over the provisional 2,048-event L): every event appears once, equal-tick source-list order is preserved, ring high-water is three pages, and drained pages are reused.
2. A modular wrap-crossing 200-tick interval: selected events equal the independently listed half-open source-window result in the source's canonical ascending tick order.
3. A NoteOn at tick 0 and its NoteOff at tick 200 separated by 700 same-tick events: the pair survives multiple pages and page reuse exactly once.
4. The first case explicitly fills all three ring slots, observes `RingFull`, drains a page, and verifies production can resume.

**Red proof:** temporarily removing the drained-page `page->published = false` release made all three tests fail to terminate within the bounded test loop. The original header line was restored; `git diff -- include/WindowPagedStream.h` was empty afterward.

## Verification

- `pio test -e native -f test_window_paged_legacy`: **3/3 passed**.
- `pio test -e native`: **2810 cases, 7 skipped, 2803 succeeded, 0 failed**.
- `openspec validate windowed-playback-generation --strict`: valid.
- `git diff --check`: clean.
- Firmware build / upload / hardware timing: **not run**; this increment changes tests and docs only.

## Limits / not claimed

This proves storage-bounded ring behavior and event ordering for these native shapes. It does **not** prove a device timing envelope, no-tick-delay on hardware, or production consumption by the playback lane. The paged path has no production consumer until the task 3.7 migration; task 3.12 remains open on those integration/timing obligations.
