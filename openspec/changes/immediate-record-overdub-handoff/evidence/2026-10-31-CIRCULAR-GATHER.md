# Circular committed display gather — execution packet

Status: fixture implemented + native verified for the crossing-window slice; **two
deliberate REDs open** on the equal-endpoint boundary case (review finding below);
device NOT RUN. Baseline: existing dirty `feature/unified-rolling-playback-path`;
unrelated staged/unstaged work preserved. Continuation:
[repair handoff](../../../docs/Runtime/2026-10-31-121645-repair-handoff.md).

## Scope and contract

Owner: Loop snapshot display projection and DisplayManager's committed-window helper.
Existing contracts: DEC-232 — circular coverage / bar-stable origin; DEC-211 —
committed display base; realtime-visual-cache exact/coherent deferred presentation.
User: “start the repair” after the 121645 regression investigation.

Task 5.1 — circular committed gather. Primary files:
`src/Loop/LoopMaterialization.cpp`, `src/DisplayManager/DisplayColdHelpers.cpp`,
`test/test_live_capture_display_boundary/test_live_capture_display_boundary.cpp`.

Precondition: existing circular interval membership helper and current snapshot
span-index copy. Both callers already gather head/tail but apply a linear final filter.
Mutation: use the existing circular note/window intersection at the final filter.
Postcondition: rootless and current-root paths retain complete identities and original
spans in both halves of a crossing window; excluded middle notes stay excluded.
Preserve current-root authority (including empty), committed/live separation and all
generation/lifetime/capacity semantics. No new state, allocation, owner or timing model.
Non-goals: origin wiring, playback admission, pending Hide/Shorten semantics, blanket
tail freeze, SD/undo changes. Whole-pass fallback cost remains explicitly unqualified;
do not replace it with a clipped event slice that would truncate long notes.

## Transition oracle

Twenty-bar pass with distinctive notes at head, excluded middle and tail; seed via
production pass storage. Query crossing [bar18, bar22) without moving any note.
Rootless then production-published snapshot cases must return head and tail exactly
once with explicit accepted geometry, never the excluded middle. Control a linear
window and full-loop request. Include a note crossing the viewport edge: its full
span, not a one-tick/clipped surrogate, remains authoritative.
Queries are read-only; no pending work or publication is consumed. Publication is
stepped only in the current-root fixture, before querying. No copied domain algorithm.

## Historical review / readiness

Searched DECISION_LOG: circular coverage, committed base, stale root, full-pass display.
DEC-232 explicitly names these linear filters as prerequisites; DEC-211 prohibits
whole-loop fallback as a claimed window bound. Existing owner extension: YES.
No ownership/schema/undo/lifetime change or new synchronous work. G0 discovery locates
the linear filter in both production paths; G1 preserves all existing owners;
this packet pins G2. No formal reassessment trigger.

## Verification

- RED/GREEN: `pio test -e native -f test_live_capture_display_boundary`.
- Broad: `pio test -e native` concurrently with
  `pio run -e teensy41-capture-serial`; exact counts and RAM1 required.
- Focused review of this slice only. Device: build hash and independent boot;
  20-bar reproducer needs explicit scratch-content authorization (none assumed).
- Task stays open until its required gates pass. No broader symptom acceptance.

## Evidence

RED: focused suite exit 1, both new cases fail `Expected 1 Was 0` for missing
head NoteId 900. Existing 49 cases pass; PlatformIO also reports its signal-wrapper
error (52 reported cases vs 51 real cases). Log:
`/private/var/folders/ms/9n65q9vx7gxf9k911t93v34w0000gn/T/opencode/circular-gather-red.log`.

G2 adversarial contract review: PASS with tightening. Alternate current-root and
rootless paths are both covered; no owner/lifetime/queue/undo/persistence mutation,
so stop/start/slot/clock cancellation semantics remain unchanged. The current-root
case constructs its root via production publication, not an injected ready result.
Tightening: retain original long-note geometry and current-empty authority; reuse the
existing in-place interval filter rather than copy a projection algorithm. Its existing
per-note intersection policy is not a newly proven WCET bound. Whole-pass fallback
and source-miss cost remain open and must not be silently labelled repaired.

## Independent scoped review — REQUEST CHANGES (one blocker)

**Equal inclusive endpoints were treated as a whole-loop note.** A production boundary
pair (On at `loopLength - 1`, Off at `0`) is reconstructed to a tail-only row
`[loopLength - 1, loopLength - 1]`; `displayNoteSpanInLoop` and
`noteIntersectsWindow(uint32_t, uint32_t, ...)` then widen `end == start` by one loop,
so the row becomes a member of **every** window. The previous linear filter excluded it.
Smallest repair: distinguish equal (one tick) from less-than (genuine wrap) in the
existing shared helper, preserving geometry. Verification: the boundary pair below, the
equal-endpoint helper case, and the helper's existing sliding-prefix/parity callers.

RED fixtures added (failing at handoff):

- `test_display_window_utils`: `test_equal_inclusive_note_endpoints_are_one_tick_not_a_full_loop`
  — `Expected FALSE Was TRUE` at a middle-window query.
- `test_live_capture_display_boundary`: `test_rootless_boundary_pair_does_not_appear_in_an_unrelated_window`
  — "a boundary pair's one-tick tail must not leak into the middle".

Focused run on those two suites: 185 cases, 2 failed, 181 succeeded. Full native on this
tree is therefore **not** green; the pre-equality baseline was 3,744 / 7 skipped / 3,737.

## Completion record

| Check | Result |
|---|---|
| Fixture implemented, discriminating RED demonstrated | YES — missing head NoteId 900 (`Expected 1 Was 0`), then the equal-endpoint pair |
| Focused native (boundary + window utils + pending note change, pre-equality) | 315 / 315 passed |
| Full native (pre-equality slice) | 3,744 cases / 7 skipped / 3,737 succeeded / 0 failed |
| Target build and RAM1 | SUCCESS — `variables 96,192`, ITCM `393,216` (`code 391,336 + padding 1,880`), `locals 34,880` |
| HEX retained | `fefeb3d2ec35c81ef23aa51d39368199e3b06e3196db718071056a980dab60c4` in `captures/circular-gather-fefeb3d2ec35/` |
| Device / boot | NOT RUN — no serial device present; no boot or content claim |
| Task 5.1 checkoff | **NOT eligible** — two REDs open |
