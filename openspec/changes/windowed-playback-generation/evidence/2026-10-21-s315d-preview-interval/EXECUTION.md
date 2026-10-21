# 3.15d slice 1 — interval-qualified preview composition (completion record)

Contract and oracle: [`EXECUTION-PACKET.md`](EXECUTION-PACKET.md). This record covers **slice 1 only**; 3.15d, 2.12 and
3.7b stay open.

## What changed

- `Track::requestPreviewWindow` now states its coverage with the **committed route's own policy**
  (`playbackWindowInterval(loopLength, shouldAvoidFullVisualRebuild(loopLength), phase, coverage)`) instead of
  whole-loop coverage at phase 0. A short loop's interval is its whole loop, so the two loop lengths answer through one
  function rather than two rules.
- The empty-source refusal became "absent composition only": `sourceCount != 0 && source == nullptr` is the invalid
  shape, matching `RuntimePlaybackViewFill::begin`'s treatment of a zero-count source as a **complete filled empty
  source**. An empty *effective* interval is a valid preview of zero events.
- `Track::servicePreviewWindowForNoteEditSession` dropped the demoted-length gate and the empty-projection refusal, and
  now requests the **interval composition** instead of the whole-loop projection.
- New `Track::composePreviewIntervalSourceForSlot(slot, coverage, out)` composes the interval source in two shapes:
  a **current published root** (gather the committed interval, then apply the session's identity-qualified delta) or a
  **rematerialized session** (no current root: the session store already holds the loop's effective content, so the
  interval filter is the composition).
- The delta rule moved out of `ensurePlaybackMergedMidiEventsBuilt` into a file-local
  `applyNoteEditDeltaToCommittedWindow` that both readers call, so the merged NOTE_EDIT fallback and the preview lane
  cannot drift while the switch (3.7b) is still ahead. Behaviour is unchanged for the merged path: the same filter by
  overridden `noteId`, the same overlay append for rows whose presence ∉ {Hidden, Deleted}, and the interval's
  `contains` is the old `winStart .. winEnd` predicate for a non-wrapping window.
- The composition buffer is the slot runtime's **resident** scratch (`LoopPlaybackRuntime::previewSourceEvents`), written
  only after the service's busy guard, so a live build's source is never overwritten; it is released with the retained
  gather's scratch (`discardRetainedGather`). The recorded I7 rule forbids sharing `retainedGatherEvents`, so this is a
  second buffer, not an alias.

Not changed: the merged NOTE_EDIT fallback's behaviour, the audible route, `fixedViewEligible`, edge adoption during
NOTE_EDIT (slice 2), the display consumer (RWO S4e), save/storage, DEC-136 admission constants, DEC-147/DEC-155
positioning.

## RED before production mutation

Production files (`src/`, `include/`) were stashed, the updated tests kept, and the focused suite re-run
(`pio test -e native -f test_windowed_preview_integration`). Failures, by test name and assertion message:

| Case | Pre-fix failure |
|---|---|
| P1 `test_3_15d_the_preview_is_the_committed_interval_composed_with_the_session_delta` | `Expected 6253 Was 0` — the preview's interval was the whole loop (start 0), not the committed route's `[6253, 7789)`; content was the delta alone |
| P2 `test_3_15d_an_empty_effective_interval_is_a_complete_silent_preview` | `Expected 0 Was 2` — the empty composition was refused, so the previous preview kept serving (stale retention) |
| P3 `test_3_15d_a_short_loop_requests_its_whole_loop_interval` | `Expected 3072 Was 0` — the demoted gate meant no window served a short loop at all |
| P4 `test_3_15d_the_interval_preview_never_mixes_revisions` | `Expected 14 Was 4` — the served content was the intermediate delta, not the newest composition |
| P5 `test_3_15d_a_preview_window_is_never_committed_currency` | **passes pre-fix** — it re-asserts a currency invariant this slice must preserve, not a new behaviour; recorded as a preserved clause, not a discriminator |
| existing `test_the_session_owns_the_preview_source_and_its_revision_is_the_lane_currency` | `Expected 6253 Was 0` — its interval assertion moved to the committed-route contract (documented contract change, not a regression) |
| existing `test_a_mutation_between_build_grains_never_publishes_a_mixed_or_stale_preview` | `Expected 14 Was 4` — same contract change, measured on the served stream |
| existing `test_a_selected_track_change_cancels_the_departing_tracks_preview_by_its_own_lifecycle`, `test_the_session_exit_owes_the_committed_handoff_and_never_re_requests_a_preview` | the direct `requestPreviewWindow` call was refused (the old whole-loop request shape against an interval-qualified source) |

The production change was then restored (`git stash pop`, tree byte-identical to the working state) and the same suite
went 16/16.

Two contract-updated suites needed their fixtures moved to the new contract, not weakened:

- `test_runtime_working_set_ownership`: `test_preview_window_is_a_distinct_source_and_never_committed_currency` now
  expects `Admitted` for an empty composition and asserts the adopted window carries the new revision with zero events;
  it also asserts the two-bar interval length. `test_3_7a_i3_the_capture_admission_counts_committed_windows_only`
  places its preview source inside the two-bar interval the request serves (the fill retains only the coverage).
- `test_windowed_preview_integration`: the restored-shape fixtures place the playhead at the first committed note's
  onset (`kContentPhase = 7021`, interval `[6253, 7789)`) so "the window is the session's content" is not a comparison
  against zero events. `publishSessionPreviewAt`/`publishSessionPreview` take the phase the requester reads, so the
  fixture's source filter and the window's interval are one geometry.

## GREEN

| Command | Result |
|---|---|
| `pio test -e native -f test_windowed_preview_integration` | 16 cases, 16 succeeded |
| `pio test -e native -f test_runtime_working_set_ownership` | 230 cases, 230 succeeded |
| `pio test -e native` | **3 357 cases: 7 skipped, 3 350 succeeded, 0 failed** (3:19) |
| `pio run -e teensy41-capture-serial` | SUCCESS; `variables 96 192 / code 392 216 + padding 1 000` |
| `pio run -e teensy41-capture-serial-window-qualification` | SUCCESS; `variables 96 192 / code 392 376 + padding 840` |
| `openspec validate windowed-playback-generation --strict` | valid |
| `git diff --check` | clean |

Baseline for the full suite at `555354a4` was 3 352 / 7 / 3 345; this slice adds five cases and none regress.

### RAM1 and the runtime size pin

`LoopPlaybackRuntime` is `extmem`-allocated per slot, so the new vector header is external state, not a Track field or a
RAM1 variable. The compiler-measured pins moved **7 896 → 7 904** (target, +8) and **10 080 → 10 104** (host, +24), both
recorded in `TrackPlaybackRuntime.cpp`.

The shared delta rule is marked `TRACK_COLD_MEM`: as an ITCM-resident function it cost **400 B** of the pinned ITCM
budget (`code 392 616 + padding 600`, below the reviewed 840 floor). Placing it in flash returns both environments to
their approved figures exactly (`392 216 + 1 000` and `392 376 + 840`). Both callers are rebuild-time paths, and the
merged builder is called on content change, not per tick.

## Not claimed

- **3.15d** is not checked off: slice 2 (clock-edge adoption of a `Preview`-origin fixed view during NOTE_EDIT plus
  per-boundary interval cadence) and slice 3 (short/long parity through the single reader, lifecycle through that
  reader, 2.12's save clause) are owed.
- **2.12** is not checked off.
- **Audible route:** unchanged. The merged NOTE_EDIT fallback still serves playback; the preview lane remains a
  write-only publisher for the display until RWO S4e.
- **Device:** not run — a device run cannot judge a lane the audible route does not consume. 3.15f owns the route's
  device parity.
- `win_blocked_by_request` / `launch_bound_owned` device reads remain unobtainable through production gestures.
