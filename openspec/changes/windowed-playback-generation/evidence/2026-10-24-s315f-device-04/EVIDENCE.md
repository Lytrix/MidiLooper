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

**Overlay finding (not a pass).** The qualification base shows two `playback_build,overlay` rows (rev 7 and rev 9) and
the lane's `OverlayRejected`/base-rejection rows, but the `playback_read` stream is `origin=0` (committed) on every
overdub turn; the only `origin=2` read is the **record** overlay at 29.45 s. So the overdub overlay candidate was
**built but not served** by the edge in this run — the echo fallback served. The native W1/W8 prove the edge under
controlled conditions; the device's non-selection is unexplained and needs a RAM1-neutral observation before this route
can be claimed.

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
