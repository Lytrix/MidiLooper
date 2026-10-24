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
- [x] 1.2 **Close the transferred verification residuals** (from `windowed-playback-generation/CLOSURE.md`), each with
  its own RED→GREEN where a fixture is owed: 2.2 derived-deadline fixture (short + long loop), 2.4 commit-edge
  allocation/inline-build mutation, 2.9 retained-source hold/release matrix (+ the `retainPlaybackSourceForSlot`
  consumer decision), 4.1 content-level undo parity + consumer decision, 4.2 content-divergence oracle + repair choice,
  and the 2.8 consolidated red-proof accounting. No residual is checked by declaration alone.
  **Progress 2026-10-24 — all six residuals landed; 1.2 checked.**
  [`evidence/2026-10-24-verification-residuals/PROGRESS.md`](evidence/2026-10-24-verification-residuals/PROGRESS.md):
  the derived-deadline short/long/tempo fixture, the S4b.7 edge-capacity assertion (both mutations: 28 red, restored),
  the retained-source paused-build/replacement matrix (test-driven; no production retainer consumer by decision), and
  the exact-root undo identity (parity remains an oracle; the 4.2 load path is its first consumer). 4.2's design is
  [`evidence/2026-10-24-4.2-design/DESIGN.md`](evidence/2026-10-24-4.2-design/DESIGN.md) **rev 2**: the code-backed
  coverage source (LSV1 carries a passes snapshot + the published root, **no coverage field**; the domain is the whole
  loop), a pre-publication comparison of the two staged snapshots (no live-root read), the canonical equivalence tuple
  (tick/type/channel/pitch-or-controller/data/velocity-or-value/pair closure; IDs only when guaranteed; no raw
  structs), the passes-only reconstruction **control as a prerequisite**, transactional slot-level failure (old slot
  wholly authoritative until atomic commit), and a bounded streaming comparison (no pre-emptive bound change). Repair
  **B** stands; no LSV1 provenance. Remaining: 4.2 implementation in the design's order (control → divergent RED →
  pre-publication parity → bounded reconcile → transactional failure → device) and the 2.8 accounting.
  **4.2 step 2 landed 2026-10-24:** the staged pitch-pair+note mutation RED (the old loader published the divergent
  root) is GREEN under the pre-publication **note-level** guard; the divergent body applies neither root nor passes, and
  a fresh slot has no current. Storage **57/57**, full native **3,437 / 7 / 3,430**, both pins exact. The old
  load-commit fixture was a synthetic passes/root pair and now writes a coherent production body. **Not 4.2 complete:**
  velocity/channel/event-tuple/closure, the bounded streaming walk and the device leg remain. **The existing-slot
  transactional proof landed:** a coherent load followed by a fully parsed divergent body leaves the same root object,
  pass and parity, with no diverged pitch. **Event-tuple slice landed:** the passes-derived free projection
  (`copyEffectiveCommittedEventsFromPasses`) and an order-preserving canonical event compare (tick/type/channel/
  pitch-or-cc/velocity-or-value) reject a velocity divergence the note guard cannot see; disabling the event compare
  fails exactly that case. **Closure edge:** a coherent wrap note (On@1500/Off@50) round-trips current; the note-level
  compare false-rejected it (different wrap-span representation), so the note guard is retired and the canonical event
  tuple is the sole pre-publication authority. **Same-tick canonical compare landed:** per-tick groups are compared as
  multisets (in-place insertion sort, no global sort); a coherent same-tick body round-trips and a same-tick velocity
  divergence is refused. **Streaming walk landed:** `RootEventStream` yields the staged root membership one reference
  at a time and the passes stream is walked in lockstep; only the current tick's group is buffered. A dense coherent
  body (512+ pairs plus a same-tick group) round-trips. Storage 62/62; full native 3,442/7/3,435; pins 391,944+1,272 /
  392,184+1,032. The **per-turn device bound** and the device leg remain (the walk is O(events) with per-tick buffers;
  the bound is a device measurement, not a claim). **Device-leg packet drafted:**
  [`evidence/2026-10-24-4.2-device-leg/PACKET.md`](evidence/2026-10-24-4.2-device-leg/PACKET.md) — scratch slot 8/5,
  production-written coherent/diverged bodies with a positive bite, host-side file injection (or a test-only chunked
  receive fallback), a temporary `LOAD,parity` row for the named outcome and `walk_us`, and the load-turn/rollback
  checklist. Execution awaits the scratch-slot raw-body write authorization and the injection choice.
  **4.2 device leg PASS 2026-10-24 (bench track 8 / slot 5 = firmware 7/4).** With the device-written mixed-seed body
  (34 events), the guard reports `t=7,s=4,result=ok,events=34,walk_us=120–125`; the **same body uploaded through the
  seam** is also `ok`; a **one-byte root-velocity patch** of that body (tick 320 note 64, 100→90; passes byte-identical)
  is **`result=divergent,events=34,walk_us=61`**, the slot has **no current** (`content_identity=0`, `root_identity=0`),
  and `CAPTURE_CONTENT` is empty — the diverged root is never served. The original was restored and re-verified
  (`ok`, identities back). An **existing** device body on track 8 slot 4 also reports `result=divergent,events=26` —
  independent evidence, not only a fixture. Walk share is 10% of `LoadLoopBudget::FocusRestoreUs = 1,200 µs`; no
  apply-overshoot row. **Host/device layout finding:** `sizeof(MidiEvent)` is 32 B native vs 20 B device (64-bit vs
  32-bit union), so host-written LSV1 bodies are not device-valid; the leg uses the device-written body and a one-byte
  patch. The seam/`LOAD_PARITY_ROW` row and the temporary qualification RAM1 re-base were removed; clean qualification
  HEX `8c7d546d…`; full native 3,443 / 7 / 3,436; storage 63/63. Evidence:
  [`evidence/2026-10-24-4.2-device-leg/EVIDENCE.md`](evidence/2026-10-24-4.2-device-leg/EVIDENCE.md). **4.2 is
  complete. **2.8 landed:** [`evidence/2026-10-24-verification-residuals/RED-PROOF-ACCOUNTING.md`](evidence/2026-10-24-verification-residuals/RED-PROOF-ACCOUNTING.md)
  consolidates every residual's discriminating RED, exact failures, byte-identical restore and GREEN
  gate: 2.2 (28 red), 2.4 (28 red), 2.9 (paused-build hold clause removed → 1 red), 4.1 (root re-derived
  in the undo select → 1 red, exact-root assertion), 4.2 (compare disabled → 5 red), plus the device
  capstone. Storage 63/63; RWO 241/241; full native 3,443/7/3,436.**
- [x] 1.3 **Activate without removing anything.** Set `PLAYBACK_WINDOW_ROLLING_ENABLED=1` on the shipping env; keep
  every compatibility branch compiled and reachable as the named fallback. Build both images; native gates; RAM1 at the
  approved pins. No route is removed and no branch is deleted.
  **Landed 2026-10-24:** the shipping env carries `PLAYBACK_WINDOW_ROLLING_ENABLED=1` plus the explicit marker
  `PLAYBACK_WINDOW_ROLLING_SHIPPING_ACTIVATED=1` (not the qualification instrumentation flag); the activation guard
  accepts either named marker and still rejects any other capture build. Two files changed, **no branch/route
  removed**. Both builds at `96,192 / 392,184 + 1,032 / 34,880` (floor 840); full native 3,443/7/3,436; storage 63/63;
  RWO 241/241. Shipping HEX `2fab59ab…`. Evidence:
  [`evidence/2026-10-24-1.3-activation/ACTIVATION.md`](evidence/2026-10-24-1.3-activation/ACTIVATION.md). Rollback
  remains `ROLLING=0` (cold `dab405b3…`); 1.4 is the device parity.
- [ ] 1.4 **Prove shipping parity on device.** Flash the 1.3 image; boot/drain; run the registered route matrix and
  compare ordered MIDI + ledger to the 1.1 cold reference; record named missed deadlines/refusals, ring integrity,
  PSRAM/RAM2 high-water and the exact HEX. A fallback pass is not a parity pass.
  **Finding 2026-10-24 — base route is NOT a parity pass yet.** The shipping image (`2fab59ab…`, `ROLLING=1`
  without qualification receipts) passes the base content/ledger verifier (record 32/32, overdub 16/16, second 8/8,
  loop 1536, RAM2 floor 147,456, display undo/redo), but the **serving route is not named**: shipping reports
  `win_readiness,4,0,not_servable` during the record and the **bridge** handoff rows at both overdub stops, while the
  cold reference reports `ready` and `fallback`. Timing differs materially (`max_pers` 75.7/76.1 ms vs 37.2/40.8;
  second overdub 44.3 s vs 37.2 s; host messages 1,397 vs 842; display snapshots 3 vs 5). Either the 2-bar base is a
  whole-take route outside the lane's supported window scope, or the compatibility bridge served it (the task's
  “fallback pass is not a parity pass”). **Campaign paused** for the scope decision: run the parity on the
  qualification-receipt image to name the route, and record the base scope explicitly (or wire the 3.7b demand).
  Evidence: [`evidence/2026-10-24-1.4-parity/FINDING.md`](evidence/2026-10-24-1.4-parity/FINDING.md).
  **Production-demand slice + dense re-run 2026-10-24.** `Track::requestActiveCommittedPlaybackWindow` now asks for
  the **active demoted** slot on the control-owner turn (whole-take slots request nothing; legacy paths intact). RED
  proof: the request removed fails the new seam test (`Expected 12 Was 4`); RWO 242/242, full native 3,444/7/3,437,
  both builds at the line; shipping HEX `225a12ea…`. The dense 24-bar re-run **demands and serves** the lane
  (410 gap-work turns, 25 readiness receipts, 24 source-qualified, 394 `build_complete`, 423/423 opportunities at
  permitted 64; display leg `ok=True`) but the verifier fails on **one overrun (121 > 64)**; the missing
  `snap_gate` rows are **qualification-only instrumentation** (the shipping image cannot emit them), so that term
  belongs to the qualification-receipt parity image. **1.4 remains unchecked:** next is the leading demand (record stop / slot activation),
  then the snapshot-gate gap, then the full 1.4 matrix. **Record-stop lead landed 2026-10-24** (slot-specific demand
  at the finalize, before `startPlaying`); the dense re-run still shows one overrun (`worst=118`), while the
  pre-demand qualification dense had 0 — so the breach is the committed demand's **service budget**, not the lead.
  **Stable-demand hold landed 2026-10-24:** the per-turn re-centring was the cause; holding one committed demand
  while the playhead is inside its coverage (and skipping while the view build is active) gives the
  qualification dense **`ok=True`, overrun_rows=0**, 429/429, build_complete 392, leg+snapshot ok. Native RED
  (coverage 9216→9280), RWO 242/242, full native 3,444/7/3,437. Qualification HEX `6762b8e6…`, shipping
  `09a5ae69…`. Remaining 1.4: shipping dense from a clean STOPPED boot (or accept the qualification receipt image as the dense
  parity instrument), switch cold+shipping, 1.5 rollback. Shipping base content/ledger PASS on the demand image;
  the shipping dense capture was blocked by the attach/display leg (transport running / 25 boot rows), not the lane.
- [ ] 1.5 **Rollback drill.** Return `ROLLING=0`, build/flash, and confirm the legacy route restores exactly (the 1.1
  HEX); record the exact rollback pair. This is the accepted rollback until the last family is removed.
  **Drill run 2026-10-24 (not checked):** current-source cold control `e7e4b4fc…` built/flashed; the legacy image
  boots and records the base (32/32, loop 1536), but the full base run is `INCONCLUSIVE_CAPTURE` (overdubs 0/0)
  because the bench set/transport state blocks the capture. Rollback pair: shipping `09a5ae69…` ↔ cold
  `e7e4b4fc…`; the 1.1 `dab405b3…` artifact is not preserved. A clean-bench run is owed before removals. Evidence:
  [`evidence/2026-10-24-1.5-rollback/ROLLBACK.md`](evidence/2026-10-24-1.5-rollback/ROLLBACK.md).

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
