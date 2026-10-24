# 2.1 prepared pair — increment 1 evidence (production path removed)

Date: 2026-10-24
Branch: `feature/unified-rolling-playback-path`
Base: `bf004313` (1.x closeout + 2.1 packet)

## Scope finding (corrects the packet's first read)

The prepared pair is **not** the launch/switch's window source and it is **not** a full-loop shortcut for the demoted
route. Three facts pin its real reach:

- `Track::preparePlaybackLaunchForSlot` accepts only **demoted** content (`launch_not_demoted_content` otherwise), and
  a demoted loop never builds a full-loop window, so `publishPreparedPlaybackPairFromWorkingWindow` (which requires
  `isFullLoopMergedPlaybackWindow`) can never publish there.
- `Track::requestCommittedPlaybackWindowForSlot` deliberately refuses **undemoted/full-take** slots: "Whole-take
  (undemoted) slots keep their existing route; the windowed lane's production scope is the demoted shape (the 1.4
  scope split)."
- Therefore the pair's only production route is the **full-take legacy switch/active** route: after the switch commits,
  the clock path's consume miss requests the pair and `TrackManager::servicePendingPreparedPlaybackPair` publishes it.

Consequences for 2.1:

- The supported (demoted) launch/switch routes already serve the unified window; the pair is unreachable there.
- Removing the pair leaves the full-take legacy route on the **raw merged fallback** (2.5's family, still compiled and
  reachable). That is the correct 2.1 end state; extending the window lane to full-take would cross the 1.4 scope split
  and belongs to 2.5, not 2.1.
- The earlier "whole-loop window extension for the launch/switch full-take target" idea was rejected for that reason.

## RED

Test: `test_2_1_the_full_take_switch_does_not_publish_a_prepared_pair`
(`test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp`).

Fixture: slot 0 active with content, slot 1 committed at 768 ticks (full take), both roots published, transport
playing, pending switch to slot 1; then `servicePendingSlotProvision`, `updateAllTracks` across the switch boundary, and
`TrackManager::servicePendingPreparedPlaybackPair` each turn.

Run before the edit:

```
test_2_1_the_full_take_switch_does_not_publish_a_prepared_pair: active=1 pair=1 merged=2 order=2 len=768 req=0  [FAILED]
```

`pair=1` is the production pair published on the full-take switch route; the test's `pair=0` assertion is the RED.

## Edit (increment 1)

- `src/Track/TrackPlaybackWindowBuild.cpp` (`Track::ensurePlaybackMergedEventsForSlot`): removed the
  `publishPreparedPlaybackPairFromWorkingWindow(slotIndex)` call.
- `src/Track/TrackPlaybackHotPath.cpp`: removed the `resolvePreparedPairConsume` block; `livePair` stays `nullptr` and
  `pairHeld` stays `false`, so the route falls through to `buildMergedPlaybackFallbackCold`.
- Tests retired because their subject (the production publication/consume) is gone:
  - `test_prepared_pair_publishes_off_clock_and_the_consume_decision_never_builds`
  - `test_source_change_requests_a_replacement_and_holds_the_last_coherent_pair`
  - `test_published_pair_emits_the_synchronous_oracle_trace_after_a_source_change`
  - `test_seal_admission_signal_requests_the_pair_replacement`
  - `test_gathered_pair_matches_the_synchronous_path_across_a_wrap_and_a_loop_edge_note`
  - `test_gathered_pair_holds_then_replaces_atomically_when_the_root_changes_mid_window`
  - `test_gathered_pair_waits_for_the_record_stop_root_then_publishes`
  - `test_selected_prepared_pair_has_a_real_reader_witness` (`test_playback_read_witness`)

## GREEN

| Gate | Result |
|---|---|
| 2.1 RED test after edit | PASSED (`pair=0`, target active) |
| Focused (`continuous_capture`, `read_witness`, `prewarm`, `rolling_record_retirement`, `runtime_working_set_ownership`) | **607/607** |
| Full native | **3437 cases, 7 skipped, 3430 passed** |
| Shipping build | RAM1 **96,192** / ITCM **392,040 + 1,176 = 393,216** / locals **34,880**; floor 840, 336 B headroom |
| Qualification build | same RAM1/ITCM/locals |
| Shipping HEX | `15103188953f919d` |
| Qualification HEX | `8e4216c797e95225` |
| `git diff --check` | clean |

The demoted launch/switch tests (3.7e, 3.15) are inside the focused set and stay green: the supported window route is
unaffected.

## Not checked / increment 2 (still 2.1)

- Delete the now-dead `PreparedPlaybackPair`, the runtime fields/`preparedPairStorage`/API, `resolvePreparedPairConsume`,
  `publishPreparedPlaybackPairFromWorkingWindow`, the owner (`TrackManager`/`main`/`StorageManager`), the hot-path dead
  variables/branches, the post-reload clear and the metrics row.
- Re-point/delete the three remaining direct-seed pair tests (`test_prepared_pair_stream_matches_synchronous_oracle_plain_playback`,
  `test_prepared_pair_preserves_same_tick_off_before_on_order`, `test_prepared_pair_anchor_is_part_of_its_identity`),
  the prewarm/whole-take storage assertions, and the increment-1 test's pair reference.
- Device requalification (qualified launch/switch gesture on shipping + cold control), cold parity, rollback record.
- 2.1 remains **unchecked** until those land.

## Rollback

- Prior shipping image: `09a5ae69…` (stable-demand shipping).
- Cold control: `e7e4b4fc…`.
