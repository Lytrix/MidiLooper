# 2.1 prepared pair — increment 2 evidence (family deleted)

Date: 2026-10-24
Branch: `feature/unified-rolling-playback-path`
Base: `90c8a94f` (increment 1: production publication/consume removed)

## What was deleted

- `include/TrackPlaybackRuntime.h`: `PreparedPlaybackPair`, `preparedPair`, `preparedPairStorage`,
  `preparedPairRequestPending`/source/tick/consumed-generation, the five pair counters, `prewarmPreparedPlaybackPair`,
  `preparedPairIsCurrent`, `requestPreparedPlaybackPair`, `publishPreparedPlaybackPair`, `clearPreparedPlaybackPair`,
  `notePreparedPairHoldTick`, `resetPreparedPairCounters`, and `ownsInFlightPreparedHandover`.
- `src/Track/TrackPlaybackRuntime.cpp`: the same definitions, plus the pair storage reservation in `prewarmViews`
  (the whole-take release keeps the view-set release).
- `include/TrackInternal.h`: `PreparedPairConsumeDecision` + `resolvePreparedPairConsume`.
- `src/Track/TrackPlaybackWindowBuild.cpp`: `resolvePreparedPairConsume`,
  `publishPreparedPlaybackPairFromWorkingWindow`, `servicePendingPreparedPlaybackPair`(+Slot),
  `hasPendingPreparedPlaybackPairRequest`, `preparedPairMetrics`, `resetPreparedPairMetrics`, the post-reload clear
  and the read-witness pair branch; the ownership guard that read `ownsInFlightPreparedHandover`.
- `include/Track.h` + `include/TrackManager.h`: the service/predicate/metrics declarations and `PreparedPairMetrics`.
- `src/TrackManager.cpp`, `src/main.cpp`, `src/StorageManager.cpp`: the owner calls and functions.
- `src/Track/TrackPlaybackHotPath.cpp`: the dead `livePair`/`pairHeld` paths in the stream context and order-dirty
  pointer; the route is now `paged → bridge → fixed view → merged fallback`.

Tests:

- Deleted the remaining pair-own tests and harness: `test_prepared_pair_stream_matches_synchronous_oracle_plain_playback`,
  `test_differential_harness_detects_a_perturbed_pair`, `test_seal_admitted_into_pair_requires_atomic_replacement`,
  `test_prepared_pair_preserves_same_tick_off_before_on_order`, `test_prepared_pair_anchor_is_part_of_its_identity`,
  and the `seedPreparedPair`/`consumePairOverRange`/`consumePublishedPairOverRange`/
  `assertPairMatchesSynchronousPathFromAnchor` helpers.
- Re-pointed the surviving long-loop window test to consume the **merged working window**
  (`consumeMergedWindowOverRange`) against its independent reference order; restored the shared
  `differentialTracesEqual`, `buildFixtureRoot` and `DifferentialRefStream` helpers that had lived inside the deleted
  region.
- Re-pointed the S3.2 / occupancy owner tests: the in-flight prepared handover is no longer an owner term.
- Re-pointed the 2.1 route test to `test_2_1_the_full_take_switch_serves_the_merged_fallback` (active target, no
  window demand, merged window built).
- Removed the pair-storage assertions from the prewarm tests, the read-witness clear, and the rolling-retirement
  `pair=` message field.

`sizeof` re-baseline (compiler measured): `LoopPlaybackRuntime` **7,768 B target / 9,896 B host** (was 7,920 / 10,120).

## Gates

| Gate | Result |
|---|---|
| Full native | **3432 cases, 7 skipped, 3425 passed** |
| Shipping build | RAM1 **96,192** / ITCM **392,040 + 1,176** (393,216, floor 840) / locals **34,880**; FLASH 1,197,696 |
| Qualification build | same RAM1/ITCM/locals; FLASH 1,202,888 |
| Shipping HEX | `e5a26dd68ecf9a46` |
| Qualification HEX | `9b0e1d3f10054699` |
| `git diff --check` | clean |

RAM1/ITCM/locals are unchanged because the runtime (with its former pair storage) is externally allocated; FLASH
shrank by 2,136 B on shipping. The runtime descriptor shrank 152 B target / 224 B host.

## Still open for 2.1 (device gate)

- Qualified launch/switch gesture on shipping `e5a26dd6…` plus the cold control, cold parity comparison, and the
  rollback record. The bench is on the 25-bar campaign tree (`151444`); the mixed-oracle finalization limitation is
  carried, not waived. 2.1 stays **unchecked** until that leg lands.

## Rollback

- Prior shipping: `15103188953f919d` (increment 1) / `09a5ae69…` (stable-demand shipping).
- Cold control: `e7e4b4fc…`.
