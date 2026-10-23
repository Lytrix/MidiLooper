# Slice 3.14c — the undo edge requests currency, and the owner rebuilds from the pass inventory

**Change:** `windowed-playback-generation` · **Task:** 3.14 (**fourth increment; the task stays open**) ·
**Date:** 2026-10-20 · **Branch:** `feature/unified-rolling-playback-path` · **Baseline:** `49ac25e8`
**Contract:** DEC-108 §13(g) — request through the existing immutable publication owner, no inline
enumeration/publication; DEC-073 — currency is coverage; **DEC-150 — a rebuild request enumerates the passes
from an empty parent**.
**Follows:** [`G0-INVESTIGATION.md`](G0-INVESTIGATION.md) (the fork this increment resolves) and
[`…-s314c-refresh-source-verification`](../2026-10-02-s314c-refresh-source-verification/EVIDENCE.md) (why the
edge cannot enumerate).

---

## What landed

| Piece | Change |
|---|---|
| `Loop::requestCommittedPassesRebuild()` | New request, same publication lane and coverage stamping as `requestCommittedPassesRepublish`, but it sets `PendingSnapshotPublishBox::rebuildFromPassInventory`. |
| `resolvePendingParent` | A rebuild request resolves an **empty** parent (the candidate branch still wins, so later fragments build on earlier work). The published root stays selected and audible until the replacement publishes. |
| `TrackUndo::requestPlaybackSourceCurrency(loop, rebuild)` | The edge's request, skipped only when the root is already current. A publication already in flight is **replaced** — its enumeration is older — so the last edge before the idle lane runs publishes. |
| `applyPublishedSnapshotSelect` | Calls it once the entry is fully applied; **edit-pass-carrying** entries use the rebuild shape, entries that only changed committed passes keep the incremental republish the record/overdub path has always used. |
| `adoptNewInventoryRowsInto` + `queueSessionExitSnapshot` | When a stop's session-exit queue adopts a rebuild box (undo, then stop), the box adopts the inventory identities it does not track yet before stamping coverage. `queueAllCommittedPassesInto` stays a **literal** enumeration: a duplicate pass id is an inventory row, not an identity. |

No new firmware state outside the heap-resident pending box (one `bool`), no scheduler, no second publisher.

## Why the shape, not just the call

The G0 investigation measured that the *call* alone is not enough: an edit-pass undo leaves the committed
passes unchanged, so the incremental republish diffs to nothing against the published root, the start returns
false, and the empty-candidate path clears the box (`publishedSnapshotCoveredRevision()` stayed at **1** while
`playbackRevision` reached **4**, over 20 000 owner turns). A companion change can also only add or update, so
the root could not retract the overlay a disabled batch no longer contributes. The rebuild form answers both:
the candidate *is* the pass inventory plus the **active** overlays.

## Oracle, as measured

The 3.14 fixture drives the production entry point (`TrackUndo::undoForLoop`) on `NoteEditPassClosed` batches of
a 64-bar slot whose root the owner built. The served window (`requestPlaybackWindow`'s own scratch) is compared
with `Loop::copyEffectiveCommittedEventsInRange` for the interval `Track::playbackWindowInterval` names — never a
hand-written expectation.

| Case | Assertion (fixture) | Measured |
|---|---|---|
| setup | `pre-undo: 4 committed events + 3 overlay note pairs are effective` / `the served window carries the overlay the root published` | **10** effective events, served |
| S1 the request exists | `the undo edge requests currency through the publication owner` | `hasPendingSnapshotPublish() == true`, `pendingSnapshotFromLoadedPasses() == false` |
| S1 stale still refused | `a lagging root is named, never served as current -- even while a request is pending` | `SourceNotCurrent`, no maintenance owed |
| S1 the lane names the debt | `a stale window is named as owing, not silently replaced by silence` | readiness leaves `Ready` |
| S2 the last edge wins | `the second edge re-requests the inventory it enumerated` / `the owner's own lane makes the replacement current` | the second undo replaces the first request; one publication after it lands **current** |
| S2 content and retraction | `the served window carries exactly the post-undo effective content` / `6 … 4 committed events + the one batch still active` / `the two undone batches' overlay rows are **retracted**` | 10 → **6** events, equal to the oracle, with the still-active batch present |

## Red-proofs (sources restored byte-identically, `shasum -a 256 -c` OK)

| # | Mutation | Failure |
|---|---|---|
| M1 | request removed at the edge | `the undo edge requests currency through the publication owner` |
| M2 | a pending publication suppresses the request (the coalescing form this slice first shipped) | `the owner's own lane makes the replacement current` — the older candidate publishes behind the live revision |
| M3 | `requestCommittedPassesRebuild()` downgraded to the incremental form | `Expected 6 Was 10 … the served window carries exactly the post-undo effective content` — nothing publishes and the undone overlay stays served (the G0 finding) |

## Gates

| Gate | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **221 / 221** (baseline verified at 221 on the stashed tree; a *failing* run reports 222 because the harness counts the aborted case) |
| `pio test -e native` | **3338 / 7 skipped / 3331 succeeded** |
| `pio test -e native -f test_note_edit_entry_boundary` | **104 / 104** — the literal-inventory contract (`{11, 21, 24, 24}`) is untouched; the first form of the adoption change broke it and was replaced |
| `pio run -e teensy41-capture-serial` | **SUCCESS**; RAM1 `variables 96,192`, `code 392,216`, `padding 1,000` (1,160 B above the 840 floor) — identical to the pre-slice line; FLASH code 1,168,584 |
| `openspec validate windowed-playback-generation --strict` | valid |
| Device | none claimed, none needed: the owner, the request, the edge and the adoption path are native-verifiable, and no device-visible behaviour is asserted here |

## Review

One focused independent review (publication/lifecycle change) found two blockers, both fixed and re-measured:

1. **The coalescing guard suppressed a request the record/overdub path always made** — with a pending
   publication (the ordinary stop → undo sequence) the undone pass could stay represented and nothing would ask
   again. Fixed by removing that guard; the replacement rule is now the tested claim (red-proof M2).
2. **A session-exit queue could stamp coverage on a rebuild box it did not re-enumerate** — the candidate is
   built from an empty parent, so a pass re-enabled in between could be omitted while the watermark advanced.
   Fixed by `adoptNewInventoryRowsInto` before the stamp; the first attempt (making the shared enumeration
   idempotent) collapsed the pinned duplicate-row inventory and was replaced.

Also tightened from the same review: the entry's shape discriminator now includes committed-pass entries that
carry edit passes (their companion overlay must be retracted too), and the fixture compares the **served** window
instead of a ready view whose count happened to be zero (it proved nothing before).

## Named limits (recorded, not hidden)

- A change that arrives with **no** requesting edge can still publish an intermediate root at the coverage it
  enumerated — it never stamps a newer revision current (DEC-073), and the next edge requests again.
- The rebuild costs one full pass enumeration per edit-pass undo/redo (the work a load/restore publication already
  pays), on an idle lane bounded at `kSnapshotIdleConstructionUnitsPerTurn = 2`. It is not on any MIDI hot path.
- `adoptNewInventoryRowsInto` is **not** fixture-covered: the interleaving needs a live stop immediately after an
  undo. Its absence would be caught by the coverage rule (a current root missing a pass), not by a test.
- **Task 3.14 stays open**: its remaining terms (off-clock selective reconciliation coverage across the
  between-grains matrix, 3.7b's fallback-removal gate, and the device leg of the audible handoff) are unchanged
  by this increment.
