# 3.15d slice 2 — completion record (native)

Packet: [`EXECUTION-PACKET.md`](EXECUTION-PACKET.md). Superseded draft: [`CONTRACT-DRAFT.md`](CONTRACT-DRAFT.md).
Slice 1: [`../2026-10-21-s315d-preview-interval/EXECUTION.md`](../2026-10-21-s315d-preview-interval/EXECUTION.md).

Branch `feature/unified-rolling-playback-path`, base `3002bc8f` (docs-only closeout of slice 1).

## Order actually executed

The owner's ordering was: native emission harness first, and **no audible-route change until the harness can RED the
merged↔preview boundary**; then settle `LedgerBoundarySummary` semantics with a crossing-note fixture; then the packet;
then the smallest seam the REDs prove.

1. Harness (F1, S1, S2a, S2b, S3, S4, S5 + helpers) written against the unmodified production resolver.
2. `LedgerBoundarySummary` semantics settled first, by fixture: **exact over the live ledger**; empty means *empty
   ledger*, never *unconstrained*. Consumers are the bridge selection and the stopped-oracle publication only — the
   preview route has no ledger admission gate, so the draft's hazard 2 is answered by *not* gating adoption on it
   (gating would refuse every adoption once the playhead moved, since the summary is captured at build time).
3. Seam implemented, then the whole production diff **stashed** to prove the harness REDs without it, then re-applied.
4. One focused independent review of the seam diff (read-only, no builds). It found a **BLOCKER** (the exit left the
   frame `Serving` on a `Preview` view, which the committed reader rejects into `Blocked` — a state that never
   re-requests), plus cadence, admission and yield defects. Everything it named was verified against the code and
   fixed; the admission finding changed the cadence's shape (below).
5. Targeted mutations to prove the fixtures have teeth; the one that did not bite is recorded as such.

## What the review changed

| Finding | Verdict | Fix |
|---|---|---|
| Exit leaves the frame `Serving` on a `Preview` view → committed lane `Blocked`, `previewHandoffOwed` never discharged | **confirmed BLOCKER** | `cancelPreviewWindowForSlot` ends a `Preview`-bound frame (`Idle` + one cursor reset); the served window stays readable, so the exit is still a boundary. Pinned by **S6** |
| Seam could end a frame another lane owns (committed/capture) | **confirmed, my own finding** | the seam only acts on a frame bound to its own `Preview` view; anything else returns to the resolver |
| Seam never re-checked the bound storage instance | confirmed | serving requires `published` and `frame.boundStorageGeneration == current.storageGeneration` (the committed reader's own terms) |
| Admission lead was `loopLength − 768`, not the real one bar (the interval's centre was passed in as "now") | **confirmed** | `requestPreviewWindow` now takes `playheadPhase` and `intervalPhase` separately; the lead is measured from the playhead. **Measured consequence:** the one-boundary-ahead cadence is refused (`CannotCompleteBeforeHandoff`, 2 304-tick requirement vs a 768-tick lead), so the cadence was withdrawn in favour of re-covering from the playhead |
| Cadence retired the successor that was due for adoption at that very boundary (half-open coverage) | confirmed | retirement is judged by the successor's own interval vs the playhead, and a covering successor is never retired |
| `servedThisRevision` was identity, not coverage (a passed window blocked re-covering) | confirmed | "serving" now means `coverage.contains(playhead)` at the session revision |
| Retirement used `ClearOrLoad` (identity invalidation) per cadence step, tearing the served frame and sibling slots' frames | confirmed | `retireSupersededReady` (no identity change); `ClearOrLoad` only as the fallback when role accounting refuses |
| A slot switch mid-session kept the departing slot's `Preview` frame | confirmed | `EditManager::beforeSelectedSlotChange` cancels the departing slot's preview (no handoff owed). Pinned by **S7** |
| Cadence ran during jam playback, preparing windows the edge never adopts | confirmed | jam early-out in the requester |

## RED evidence

The whole production diff was **stashed** against the harness as it stood before the review round (7 fixtures). The
harness cannot be stashed this way any more: it now calls `requestPreviewWindow` with the playhead and the interval phase
as separate arguments, so the pre-diff header does not compile. The three fixtures that changed or were added after that
run therefore carry their own targeted mutations (C, D, E below) instead of a second stash run.

```
test_..._note_edit_serves_the_preview_interval_at_the_clock_edge  FAILED  the clock edge binds the preview window inside its interval
test_..._the_preview_to_merged_seam_carries_a_crossing_note_once   FAILED  the preview serves inside its interval
test_..._the_cadence_keeps_the_seam_contiguous (now S2b)           FAILED  the cadence's successor serves the next interval, not a gap
test_..._adoption_closes_the_identity_the_session_removed          FAILED  the interval's adoption is the turn's route
test_..._the_lane_keeps_the_next_interval_prepared (now S5)        FAILED  the lane keeps the interval that reaches two bars past the playhead prepared
============ 5 failed / the rest succeeded ============
```

`F1` (ledger exactness) and `S4` (stale asymmetry) are **green pre-fix by design**: they pin invariants the slice must
preserve, not behaviour it introduces. `main.cpp`'s owner-visit call and the served-frame guard in
`serviceRollingPlaybackWindow` are part of the same diff and therefore part of the same RED. `S6`/`S7` pin yields that
only exist once the seam serves at all, so their REDs are the mutations below, not the stash.

## Mutation runs (teeth)

| Mutation | Result | Reading |
|---|---|---|
| **B** — drop `reconcileDeletedNoteEditPreviewSoundings` from the adoption | `S3` **RED** only (`Expected 1 Was 0. adoption closes the removed identity: exactly one NoteOff, no re-send`) | the closure path is the fixture's discriminator |
| **C** — disable the exit yield in `cancelPreviewWindowForSlot` | `S6` **RED** only (`and ends the frame that claimed it…`) | the yield is what keeps the committed lane alive after a session |
| **D** — disable the depart hook's preview cancel | `S7` **RED** only (`the departing slot's preview source yields`) | the depart hook is what yields a slot the session never visits again |
| **E** — admission given the true playhead for a one-boundary-ahead cadence | `S2b` + `S5` **RED**, all adoption fixtures green | the successor-ahead cadence is inadmissible under the declared envelope — this is the measurement that withdrew it (and the truthful lead is what stayed) |
| **A** — drop the yield-time `loop.nextEventIndex = 0` | **no** fixture changes | recorded honestly: the crossing predicate (`prevTickInLoop`-qualified) plus the merged rebuild's own reanchor already cover every exercised shape, so the reset is retained as an invariant guard rather than presented as proven necessity. No fixture was invented to make it bite. |

## Harness discoveries that changed the fixtures (not the production code)

- The requester and the edge must read the **same** tick: `readyEmissionPlaying(track, slot, startTick)` publishes a
  `nativePlaybackDeadlineClock` of `{startTick, epoch 1}`, without which the requester computed phase 0.
- A preview request at **phase 0** is refused: the demoted interval would start at the playhead. Every fixture opens its
  session at a nonzero phase (500 / 600).
- The merged NOTE_EDIT rebuild reanchors the cursor each turn, which is why mutation A cannot be observed from the
  stream.
- `S5`'s refusal pin is the only fixture that calls `requestPreviewWindow` directly (it asserts a *contract refusal*, not
  a route), and it clears the retained demand afterwards so no later fixture inherits it.

## Size / budget

| Build | Result |
|---|---|
| `teensy41-capture-serial` | `variables 96 192 / code 392 216 + padding 1 000 / locals 34 880` — pins unchanged |
| `teensy41-capture-serial-window-qualification` | `392 376 + padding 840` — the approved floor, unchanged |

`sizeof(LoopPlaybackRuntime)` is untouched: the seam reuses `runtime.rolling`'s existing phase/bound fields and
`runtime.previewSource*`, adding **no** field. First draft of the seam (two lambdas) grew ITCM by 272 B and failed the
budget; the lambda-free, `FLASHMEM` shape restored it exactly. Every addition in this round lives in a `FLASHMEM` /
`TRACK_COLD_MEM` function, so the pins did not move.

## Gates

| Check | Status | Evidence |
|---|---|---|
| Focused native | **PASS** | `test_windowed_preview_integration` 25/25 (16 existing + 9 new) |
| Full native | **PASS** | `pio test -e native` → 3 366 cases / 7 skipped / 3 359 succeeded / 0 failed; slice-1 baseline 3 357 / 7 / 3 350 (+9) |
| Both builds + pins | **PASS** | see the table above |
| Strict OpenSpec / `git diff --check` | **PASS** | `openspec validate windowed-playback-generation --strict` valid; `git diff --check` clean |
| Device | NOT RUN | seam is qualification-image only; audible parity is 3.15f. Board keeps slice 1's image (`75d47454`), transport stopped |
| 3.15d checkoff | NOT ELIGIBLE | slice 3 owed: short/long parity through the single reader, lifecycle (mutation/undo/redo/re-focus/exit) beyond the yields pinned here, 2.12's save clause re-read against the interval source |
