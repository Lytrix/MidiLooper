# Tasks — shipping-rolling-activation

Staging rule: **activation without removal first, parity on the shipping configuration, then one family at a time.**
Every removal slice keeps a rollback until that family is gone; the `ROLLING` flag is the rollback for activation and
for every family not yet removed.

## 1. Baseline, verification residuals, activation

- [x] 1.1 **Freeze the cold shipping baseline.** Build `teensy41-capture-serial` with `PLAYBACK_WINDOW_ROLLING_ENABLED=0`
  (the legacy reference), record the exact HEX and a route matrix: base record, live/frozen Record, overdub, NOTE_EDIT,
  switch, undo/redo, dense/restored, internal/external clock, persistence/display; RAM1/PSRAM/RAM2. This is the cold
  comparison for 1.4 and every removal slice.
  **Landed 2026-10-24:** baseline [`evidence/2026-10-24-cold-baseline/BASELINE.md`](evidence/2026-10-24-cold-baseline/BASELINE.md)
  — `dab405b3…`, code 391,752 + padding 1,464, cold route matrix (base host/internal PASS, dense default leg PASS,
  NOTE_EDIT default content pair; no cold switch run named as a gap).
- [ ] 1.2 **Close the transferred verification residuals** (from `windowed-playback-generation/CLOSURE.md`), each with
  its own RED→GREEN where a fixture is owed: 2.2 derived-deadline fixture (short + long loop), 2.4 commit-edge
  allocation/inline-build mutation, 2.9 retained-source hold/release matrix (+ the `retainPlaybackSourceForSlot`
  consumer decision), 4.1 content-level undo parity + consumer decision, 4.2 content-divergence oracle + repair choice,
  and the 2.8 consolidated red-proof accounting. No residual is checked by declaration alone.
  **Progress 2026-10-24 — 2.2 and 2.4 landed and red-proved; 2.9/4.1/4.2/2.8 remain, so 1.2 stays unchecked.**
  [`evidence/2026-10-24-verification-residuals/PROGRESS.md`](evidence/2026-10-24-verification-residuals/PROGRESS.md):
  the derived-deadline short/long/tempo fixture and the S4b.7 edge-capacity assertion are in; both mutations turned 28
  tests red and were restored byte-identically. 2.9 needs the retainer-consumer decision; 4.1 the parity-consumer
  decision; 4.2 the content-divergence oracle/repair choice.
- [ ] 1.3 **Activate without removing anything.** Set `PLAYBACK_WINDOW_ROLLING_ENABLED=1` on the shipping env; keep
  every compatibility branch compiled and reachable as the named fallback. Build both images; native gates; RAM1 at the
  approved pins. No route is removed and no branch is deleted.
- [ ] 1.4 **Prove shipping parity on device.** Flash the 1.3 image; boot/drain; run the registered route matrix and
  compare ordered MIDI + ledger to the 1.1 cold reference; record named missed deadlines/refusals, ring integrity,
  PSRAM/RAM2 high-water and the exact HEX. A fallback pass is not a parity pass.
- [ ] 1.5 **Rollback drill.** Return `ROLLING=0`, build/flash, and confirm the legacy route restores exactly (the 1.1
  HEX); record the exact rollback pair. This is the accepted rollback until the last family is removed.

## 2. Family removals (one at a time; each keeps the cold parity oracle)

Each slice: remove one family, prove no supported route reaches it (native RED→GREEN on the route's own oracle),
device-requalify the affected routes, re-run the cold parity comparison, and record the rollback (prior HEX). Do not
remove two families in one slice.

- [ ] 2.1 **Prepared pair.** Remove `resolvePreparedPairConsume` / `PreparedPlaybackPair` publication and clearing /
  `TrackManager::servicePendingPreparedPlaybackPair`; prove the launch/switch routes serve from the unified reader.
- [ ] 2.2 **Fixed/bridge.** Remove `resolveFixedPlaybackSourceForTurn`, the `FixedPlaybackBridgeStorage` selection and
  `boundLaunchView`; prove the committed routes serve from the window reader.
- [ ] 2.3 **First-Record / Record-return.** Remove `adoptFirstRecordingBarReturnWindow` and
  `adoptOrServiceOnwardRecordWindow`; prove the Record/stop-return routes serve from the ordinary owner.
- [ ] 2.4 **Paged emission selection.** Remove the legacy `pagedEmission` selection while retaining the paginated
  storage as the dense-content delivery; prove the restored/dense routes serve through the one reader.
- [ ] 2.5 **Merged fallback + overdub echo.** Remove `buildMergedPlaybackFallbackCold`,
  `ensurePlaybackMergedMidiEventsBuilt`, `Track::ensurePlaybackMergedEventsForIdle` and the `ActiveCaptureOverdub` walk;
  prove exact-once overdub/wrap/stop and the dense routes without the fallback. **This is the last family.**
- [ ] 2.6 **Absorb the transferred device/product-wide legs as removal gates:** 4.5 closure ordering/last resort and
  the `serviceAdmittedDeadlineMisses` boundary; 5.5 paged-envelope 6.3 sweeps (ring 3×128/reserve 128) and the two
  criterion residuals; 6.3 device sweeps; 6.5 sustained multi-track sounding and DIN/external clock. No removal slice
  may claim these without evidence, and none may convert a missing leg into a pass.

## 3. Finalization

- [ ] 3.1 **Reclassify `ExecutionBounds.md` row 2** (merged/projection owner) with the evidence from 1.4/2.x; retire the
  legacy tests and any doc that describes a removed emitter as authoritative; update the spec/design/delivery docs.
- [ ] 3.2 **Final qualification and closeout.** Full native, both builds at the pins (or a deliberate re-base), the
  shipping-image route matrix and cold-reference comparison, strict validation, and the change's own evidence packet.

## Not in this change

- New playback features or a second reader.
- Promoted deferred routes (simultaneous-track Record, layered/jam, short-loop Record/overdub) unless the owner opens
  them explicitly.
- RWO S4e displayed provenance and S7.5 continuity acceptance.

## Evidence

- The reader change's closure and transfer table: `../archive/2026-10-24-windowed-playback-generation/CLOSURE.md`.
- The qualification campaign: `../archive/2026-10-24-windowed-playback-generation/evidence/2026-10-24-s315f-device-04/EVIDENCE.md`.
- The deferred 3.7b wording: `../archive/2026-10-24-windowed-playback-generation/tasks.md` (3.7b note).
