# 3.15f device parity — session 1 (2026-10-24)

**Images.** Qualification `cbd4703d661de0b2b91848ed224c7992c24155dce799ddfe7b0fecde79a273d6` (391,992 + 1,224) and
default `dab405b31f3e18886f1a431e32c287a42a4eed1d2d455dafe79ce04759f90079` (391,752 + 1,464), both from source
`e9628c55…`, loader 2.3, HalfKay → Programming → Booting verified per flash. Stale shutdown latch from 2026-10-21 moved
aside after the successful run-24..27 two-drain shutdowns.

## Base matrix (Session A) — PASS

| leg | dir | verdict | persistence | RAM2 floor | PSRAM occupied peak |
|---|---|---|---|---|---|
| qualification, host 120 | `2026-10-24-s315f-closeout-base-qual-host-2` | **PASS** (`ok=True`, issues=[]) | 11 windows, 40,915/43,161 µs | 147,456 B | 3,988,284 |
| qualification, internal | `2026-10-24-s315f-closeout-base-qual-internal` | **PASS** | 9 windows, 37,523/43,189 µs | 147,456 B | 4,016,100 |
| default, host 120 | `2026-10-24-s315f-closeout-base-default-host-2` | **PASS** | 8 windows, 37,179/40,780 µs | 147,456 B | 3,972,024 |
| default, internal | `2026-10-24-s315f-closeout-base-default-internal` | **PASS** | 8 windows, 39,430/43,253 µs | 147,456 B | (row in session log) |

Covers the 2-bar base record, both overdub passes, post-stop undo/redo and the display verification. The first
qualification-host attempt was **INCONCLUSIVE** on `persistence_active_snapshot_unavailable_during_save` (9 playback
windows, 8 with a read); the immediate rerun passed with 10/10, so that term is intermittent at the capture level, not
a repeatable route failure. The first default-host attempt aborted before a report (post-flash serial race); the rerun
passed.

**Overlay edge — observation and correction.** The first reading (all overdub reads `origin=0`, so "built but not
served") was wrong. A change-only `overlay_edge` observation (RAM1-neutral, FLASHMEM, qualification `9a3157a1…`) in the
base overdub shows the selector's full lifecycle: `ok=00` (no candidate, before the overdub) → `ok=fd` (candidate
exists, `readableEventCount=0`) → `ok=ff` (`rd=98/131/146`, every term: candidate, readable, provenance, no canonical
ranks, Capture origin, revision, generation, coverage) → `ok=df` (the loop revision moved 3→4) → `ok=00` (retired).
So the edge **does** bind the live overlay view on device; the `playback_read` witness logs `origin=0` because it
resolves a source only through the set roles/bridge and the live-view candidate is neither (`observePlaybackReadForTurn`),
a witness limitation rather than a non-selection. Base content (`ok=True`) plus native W1/W8 close the device overlay
selection; the witness's origin field is not a route indicator for this source. The diagnostic is removed after this
run; the final image returns to `cbd4703d…`.

## Dense / windowed (Session B)

- `2026-10-24-s315f-closeout-dense-qual-24bar` — **ok=True, issues=[]**: leg, lane and snapshot all green; readiness
  **29/29** under transport, 369/1,031 source-qualified, 537 blocks / 523 with opportunities, 0 overruns, 476
  build-complete under transport, snapshot root published. `oracle_complete=False`: the two deferred terms
  (`content_parity_across_window_boundary`, `root_demoted_residency_over_loop_lengths`) belong to the content pipeline.
- `2026-10-24-s315f-closeout-dense-default-24bar` — **leg ok=True** (content/display); lane/snapshot terms are not
  applicable on the cold image (no lane rows), exactly the qualification-only exclusion the task states. The composite
  verdict is false only on those terms.

## Scenario attempts (blocked by their own preparation, not route runs)

- `edit_overdub_during_note_edit` (STIM-EDIT-OD-DURING-NE-01): first invocation crashed on a host-tooling import
  shadowing bug (`Path` re-imported inside the function → `UnboundLocalError`); **fixed** in
  `scripts/hitl/scenarios/edit_overdub_during_note_edit.py`. Rerun then failed its **base record seed** precondition
  (`FIRMWARE_CONTRADICTION`: record seed serial On/Off inventory incomplete/mismatched), i.e. the scenario's own fixture,
  not the route.
- `accepted_sequence_mixed` (STIM-BASE-2-MIXED-01, track 8 slots 4/5): requires `--slot-b`; with it, **PREPARATION_FAILED**
  — slot 5 seed finalization release unconfirmed within 10 s (the historical mixed preparation class).
- `rolling_record --rolling-record-pass growth-empty` (track 8 slot 4): **FAIL** `scoped_record_preparation_unconfirmed`
  before the leg; the bench slot still holds the prior dense take, so the scoped preparation cannot confirm an empty
  target.

## State / next

The base and dense legs are recorded. The remaining device rows are: the overlay edge's device selection (needs an
observation of the candidate terms, then a fix or a named refusal), the rolling Record/frozen return (needs a clear bench
slot or an explicit scoped clear), the switch (same preparation class), and layered/jam (no registered stimulus). The
host-tooling `Path` fix is committed with this packet; no firmware line changed in this session.

## Rolling Record campaign — PASS on the final image

After the post-load save settled (the first growth attempt refused the clear by name:
`Clear refused: deferred save temporarily blocked by post_load_holdoff`), the three registered passes ran on the same
final image `cbd4703d…` and the offline aggregate returned **PASS**:

- `2026-10-24-s315f-closeout-rolling-growth-3` — `growth-empty`, both legs CLEARED;
- `2026-10-24-s315f-closeout-rolling-held` — `held` → `corrected_frozen_root_return`;
- `2026-10-24-s315f-closeout-rolling-matched` — `matched` → `matching_physical_reader_preserved`;
- `2026-10-24-s315f-closeout-rolling-aggregate` — `verdict=PASS`, `ok=true`, `full_recipe_complete=true`, `issues=[]`,
  `pass_groups=[growth-empty, held, matched]`, all four stop terminals present.

This covers live Record, the frozen Record return and the stop handoff on the final image; the clear refusal is a
save-settling race, not a route refusal (the same clear succeeded on the retry).

## Switch (mixed) — repeatable harness preparation miss

`accepted_sequence_mixed` (track 8 slots 4/5, `--slot-b 5`) was attempted twice. Both times the B seed **committed**
(`seed_b_committed: confirmed=True`) and the slot-scoped canonical release **was emitted**
(`#CAP,76305948,DIAG,canonical_release,4,1,1536,8` and `canon_rel_interval,4,4294967296,1,1536,8` at 76.306 s, followed by
`PERS,result,…,ok` at 76.949 s), but the flow's post-commit `_await_markers` window did not see it and reported
`slot 5 seed finalization release unconfirmed within 10s`. The scenario never reached the switch leg, so this is a
**host-tooling marker-window race**, not a firmware refusal or a switch result. The route remains device-unqualified.

## Final state

The edge diagnostic was removed; the rebuilt qualification HEX is byte-identical to `cbd4703d…` and is the image left on
the board. Bench slot 8/4 was cleared by the rolling pass (authorized test content); slot 8/5 holds the mixed seed from
the failed preparation. The remaining 3.15f rows are: switch (the harness race above), layered/jam (no registered
stimulus), and the dense content-pipeline terms (deferred by the registered verifier).
