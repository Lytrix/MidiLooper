# 3.7a (I1) — the retained-source window consumer: rootless serving, the index-bounded lookup, and the measured ITCM finding

Slice evidence for the increment the packet in this directory authorizes
([`EXECUTION.md`](EXECUTION.md), written before the edit). Task **3.7a stays open**: this is increment **I1** of four
(I2 the deferred lane turn, I3 the 3.12/3.13 consumer halves, I4 differential parity + timing admission).

## What landed

**One new primitive, one requester branch, one vocabulary change — no new owner, no new scheduler, no root.**

`Track::gatherRetainedSourceWindowForSlot` (`src/Track/TrackPlaybackWindowBuild.cpp`, declared in `include/Track.h`)
answers an interval from the **retained revision's pinned bodies**, and it is the source for a slot with **no published
root** (DEC-108 §10):

1. **Currency first, by name.** No retained source → `NoRetainedSource`; a retained revision that is not the one asked
   for → `RevisionMismatch` (nothing read). The one-pass-late *allowance* stays the lag rule's consumer (task 3.6b).
2. **The index answers before any body is touched.** `queryRetainedIntervalForSlot` reports the interval's covered and
   skipped bars; **all bars skipped is a served empty window** (`indexAnsweredEmpty`, `inspectedChunks == 0`) rather
   than a scan, which is the lookup bound the index exists for (W-2.10's shape). A loop outside
   `kRetainedIndexMaxBars`, or an interval with no published index, refuses by name instead of presenting a scan as
   window-bounded.
3. **One bounded slice per call** through the existing resumable primitive
   (`CommittedEventRange::appendWindowBudgeted`, the one `WindowPagedStream` produces pages with, sliced at
   `kWindowPageCapacity`), so a caller can resume across deferred turns and the inspected work is the slice's own.
4. The pinned passes are the revision's **membership** (read from the pin, not from the live states — a pass a later
   revision disabled is exactly the lag the retention serves). Pins whose bodies are unreachable while the revision
   **owns** bodies (`ownedChunks` non-empty) refuse (`PinnedBodiesUnavailable`): an empty window there would be served
   as silence for content the revision demonstrably has.

`Track::requestPlaybackWindow` takes that path **only where the published-root enumeration cannot answer at all**
(`tryGatherPublishedSnapshotEventsInWindow` → `false`): the stale-root `Overlay` case and every root-backed case keep
their existing behaviour, and a slot with no retained revision keeps the named `SourceUnavailable` (3.6c's fixture
re-pointed, not deleted). The merged-window fallback is untouched and still serves such a slot in the meantime.

`Track::playbackWindowReadinessForSlot` names the **build** owner (`window_build_owner`) instead of `published_source`
for a rootless slot whose retained source is the **current** revision, because that is now a window a build can gather;
a rootless slot with no retained revision, or with a lagging one, still owes the source. That readiness/vocabulary
change is one line of the same contract, and it is red-proved on its own.

## Recorded, not smoothed over

- **The ITCM cost of `std::sort`, measured.** The first device build **failed the RAM1 guard**: `code 392,856 +
  padding 360` (baseline `391,576 + 1,640`), i.e. **+1,280 B**, with `variables` byte-identical. The cause was not the
  gather's own body: `nm` showed libstdc++'s introsort instantiations for this TU's comparator
  (`__introsort_loop`, `__insertion_sort`, `__unguarded_linear_insert`, their `.constprop` copies and the long-branch
  **veneers** from this flash-resident TU) landing in ITCM. Sorted **by hand** (bounded insertion sort over the
  window, whose event count is bounded by `kWindowAdmissionLimit`) the same slice costs **+192 B**
  (`code 391,768 + padding 1,448`, 608 B above the floor). The ordering bound for a per-turn caller is named as I2's.
- **One mutation is a non-red-proof, recorded as such.** MR-3.7a-B (remove the gather's own source-revision gate)
  left **147/147 green**: the *index*'s revision authority already refuses that request, so the gather's gate is
  redundant in every reachable state. The refusal itself is red-proved by **MR-3.7a-B'**, which removes both
  authorities and fails 3.6c's own fixture at line 4976 (`RevisionMismatch` → `Indexed`) **and** this slice's W-37a-3
  at line 5105 (`RevisionMismatch` → `Served`).
- **A retained revision with Active note-edit rows is refused** (`PinnedEditRowsUnsupported`), not served. The
  fallback's own effective-content path applies each committed list *separately* and then merges, and says why (a
  merged-then-apply pass lets a LIFO delete pair a hidden On with another pass's Off), so applying them flat here would
  be a *different* answer. Per-layer resolution is I2's; the refusal is a named residual, not silence.
- **What the differential oracle can and cannot say.** W-37a-1 compares the retained answer against
  `Loop::copyEffectiveCommittedEventsInRange` (the merged fallback's own windowed effective gather) at the same
  revision, pairwise by tick. The fixture's window has two events at distinct ticks, so this says **nothing** about
  same-tick order — that parity stays task 4.3's gate, and the sort here is not stable.
- **The requester's admission needs a real handoff.** `requestPlaybackWindow` at a playhead *at* the interval's start
  is refused (`CannotCompleteBeforeHandoff`, measured: the first fixture draft used playhead 0 and got result 8), so
  the fixture drives playhead 500 — inside the first bar, with the handoff a lap away. Recorded because a reader would
  otherwise assume the interval-start request is servable.

## Verification

| Check | Result |
|---|---|
| focused `pio test -e native -f test_runtime_working_set_ownership` | **146/146** (144 baseline + the two new fixtures) |
| full `pio test -e native` | **2,817 cases / 7 skipped / 2,810 succeeded / 0 failed** (baseline 2,815 / 2,808) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** — `variables 96,192` (byte-identical) / `code 391,768 + padding 1,448` / locals 34,880; floor 840, **608 B above it** |
| `openspec validate windowed-playback-generation --strict` | valid |
| `git diff --check` | clean |
| `git diff --stat -- src include` | `include/Track.h` +50, `src/Track/TrackPlaybackWindowBuild.cpp` +226/-11 |

Red-proofs, each run against the **final** revision and each restored byte-identically
(`src/Track/TrackPlaybackWindowBuild.cpp` sha256 `f13fbc9bda0e51a0…`, `include/Track.h` `e4a923a6363ed323…`,
the test file `1bb2fd3b73ffd95b…`; `grep -c "if (false)"` = 0 afterwards):

| ID | Mutation | Observed |
|---|---|---|
| MR-3.7a-A | the index's empty-interval answer removed (`if (coveredBars == 0)` → `if (false)`) | 1 failed / 145 — this slice's fixture at `indexAnsweredEmpty` (line 5087) |
| MR-3.7a-B | the gather's own source-revision gate removed | **147/147 green — a non-red-proof** (the index refuses first) |
| MR-3.7a-B' | both revision authorities removed | 2 failed — 3.6c's fixture (line 4976) and this slice's W-37a-3 (line 5105) |
| MR-3.7a-C | the requester's retained-source branch made dead | 1 failed / 145 — this slice's fixture at `Admitted` (line 5057) |
| MR-3.7a-D | readiness keeps `published_source` for a retained-backed rootless slot | 1 failed / 145 — this slice's fixture at `OwesBuild` (line 5022) |

## Fixtures

`test_retained_source_window_serves_a_rootless_interval_and_the_index_bounds_the_lookup` — W-37a-1 (rootless serving
end-to-end through the production requester **and** the production turn, `origin == Committed` with a **null** root
identity, differential parity against the fallback's effective gather), W-37a-2 (the index answers an empty interval at
zero inspected chunks; the fallback agrees it is empty), W-37a-3 (the un-indexed revision refused by name with nothing
inspected; the loop's revision advancing makes the requester refuse and the retained revision still answerable to a
caller that asks for it; readiness becomes `OwesCurrency` against the stale served window), W-37a-5 (the holder pair:
`published_source` → `window_build_owner` on one slot once the current revision is retained).

`test_the_retained_source_gather_refuses_by_name_where_it_must` — W-37a-4 (rootless without retention keeps
`SourceUnavailable` / `NoRetainedSource`), W-37a-6 (513 bars → `BarsUnsupported`), the pinned-Active-edit-row refusal,
and the owned-bodies-unreachable refusal (the pass set moves on while the retention still owns the bodies).

## Not claimed

No production **lane** service for a rootless demand yet (that is I2: `committedPlaybackRetryCanProgress` /
`stepCommittedPlaybackRetry` still require a current published root, so the deferred turn serves none of this); no
3.12 paged consumer and no 3.13 preview hooks (I3); no differential parity over edited content, long windows or
same-tick order (I4/4.3); no timing envelope, no device run, no `ExecutionBounds.md` row change, no fallback removal
(3.7b). Task **3.7a, 3.12, 3.13, 3.14 and 4.5 all stay open**.
