## Why

`windowed-playback-generation` established and qualified the unified rolling reader for the supported-route scope under
`PLAYBACK_WINDOW_ROLLING_ENABLED=1` (the qualification image). The **shipping** image still builds with
`PLAYBACK_WINDOW_ROLLING_ENABLED=0`, so the legacy emitters remain the production path and the compatibility branches
cannot be removed: activation and removal are inseparable. Trying to absorb that into the reader change would make the
shipping image's behavior change, delete five serving mechanisms at once, require device requalification of every route
on the new image, and remove the easy rollback (`ROLLING=0` restores the old emitter until a family is removed).

This change is the explicit activation boundary: enable the rolling reader on the shipping image **without removing
anything**, prove parity against the cold legacy reference, then retire the compatibility families one at a time with
their own RED/GREEN, device gate and rollback.

## What Changes

- **Enable `PLAYBACK_WINDOW_ROLLING_ENABLED=1` on `teensy41-capture-serial` with no removals.** All compatibility
  branches stay compiled and reachable as the named fallback until their own removal task passes.
- **Prove shipping parity** for every supported route against the cold reference (the same source built with
  `ROLLING=0`): independent ordered MIDI + ledger, the registered content profile, boot/drain/ring integrity, RAM1 and
  PSRAM/RAM2 high-water.
- **Remove the five compatibility families one at a time**, each a separate slice with its own RED→GREEN, native and
  device evidence, and a rollback that still exists until that family is gone:
  1. prepared pair (`resolvePreparedPairConsume` / `PreparedPlaybackPair` publication and clearing);
  2. fixed/bridge (`resolveFixedPlaybackSourceForTurn`, `FixedPlaybackBridgeStorage`, `boundLaunchView`);
  3. first-Record / Record-return (`adoptFirstRecordingBarReturnWindow`, `adoptOrServiceOnwardRecordWindow`);
  4. the paged emission selection (the paginated storage remains the dense-content delivery);
  5. the merged fallback + the overdub echo (`buildMergedPlaybackFallbackCold`,
     `ensurePlaybackMergedMidiEventsBuilt`, `Track::ensurePlaybackMergedEventsForIdle`, the `ActiveCaptureOverdub` walk).
- **Close the transferred verification residuals** from `windowed-playback-generation` (`CLOSURE.md`): 2.2 derived
  deadline, 2.4 commit-edge mutation, 2.8 red-proof accounting, 2.9 retained-source matrix, 4.1 content-level undo
  parity + consumer, 4.2 content-divergence oracle + repair choice, and the device/product-wide legs of 4.5, 5.5, 6.3,
  6.5 (closure ordering/last resort, paged envelope sweeps, device sweeps, multi-track sounding and DIN clock).
- **Reclassify `ExecutionBounds.md` row 2** only after the last family is removed, and retire the legacy tests/docs.

## Impact

- **Shipping behavior:** the shipping image changes from the legacy emitters to the rolling reader; this is the
  product cutover and needs its own device requalification.
- **Routes:** base record, live/frozen Record, overdub, NOTE_EDIT, switch, undo/redo, dense/restored, internal/external
  clock, persistence/display contention and RAM high-water.
- **Rollback:** the flag remains the rollback until each family is removed; after a removal the rollback is the prior
  HEX, which is why the order matters (least-used/least-entangled family first is a candidate, not yet decided).
- **No new reader:** this change activates and retires; it does not change the unified reader's contract.
- **Dependencies:** the `windowed-playback-generation` qualification evidence and its `CLOSURE.md` transfer table; the
  runtime spec's continuity acceptance (RWO S7.5) remains downstream.

## Out of scope

- New playback features or a second reader.
- The deferred routes themselves (simultaneous-track Record, layered/jam, short-loop Record/overdub) unless the owner
  promotes one.
- RWO S4e displayed provenance and S7.5 continuity acceptance.
