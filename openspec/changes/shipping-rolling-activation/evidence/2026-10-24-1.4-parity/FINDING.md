# 1.4 parity — base-route run finding (2026-10-24)

**Status: NOT a parity pass. 1.4 remains unchecked.** The shipping image (`2fab59ab…`, `ROLLING=1` without the
qualification receipts) ran the base route and its **content/ledger verifier passed**, but the **serving route is
not identified** on this image and the timing/display rows differ from the cold reference. Under the task's own
rule — *a fallback pass is not a parity pass* — this cannot be accepted as activation parity.

## Run

```
scripts/host_midi_hitl.py run --preset base --track 5 --loop-slot 1 --midi-channel 1 \
  --build-env teensy41-capture-serial --out-dir captures/2026-10-24-1.4-base-shipping-host \
  --boot-reset --clock-source host --tempo-bpm 120
```
Image: shipping activation HEX `2fab59ab…` (flashed and booted). Cold reference:
`captures/2026-10-24-s315f-closeout-base-default-host-2` (`dab405b3…`, `ROLLING=0`).

- **Content/ledger parity holds:** record on/off 32/32, overdub 16/16, second overdub 8/8, loop length
  1536/1536, SEVT spans/bars OK, display undo/redo `after_undo=49`/`after_redo=58`, RAM2 floor **147,456 B**
  (identical), `Result: PASS`.
- **Route witness is missing/contradictory on the shipping image.** The shipping run has only three
  `DIAG,win_readiness,4,0,not_servable,not_servable,…` rows (during the record); the cold reference reports
  `ready` (`win_readiness,4,0,ready,ready,7,…`) after the overdub. At both overdub stops the shipping run
  reports the **bridge handoff** rows (`bridge_base,prepared_window`, `bridge_begin,denied`,
  `bridge_ready,context_retained`), while the cold run reports the legacy `ODUB,stop,…,fallback` rows. There is
  no `win_gate`/serve row on either log to name the emitting route directly.
- **Timing/pacing deltas (recorded, not hidden):** `max_pers_work_us` **75,741** vs **37,179**;
  `max_persist_save_us` **76,138** vs **40,780**; second overdub start **44.28 s** vs **37.21 s**;
  host MIDI messages **1,397** vs **842**; serial lines **11,568** vs **14,742**; display snapshots after
  overdub **3** vs **5** (first shipping snapshot `visual_notes=0`); `DISP before undo 64` vs **74**.
  The verifiers accept the run (`ok=True`), but these are not an exact parity of the ordered stimulus/ledger.

## Interpretation

Two readings are possible and this run cannot decide between them:

1. **Expected scope split.** The 2-bar base is a whole-take route; the windowed lane's supported scope is the
   demoted/long routes (the archived 3.15f base qualification ran on the **lane** image with the qualification
   receipts). On this reading the base parity is a **content-only whole-take comparison**, and the lane parity
   must be proven on the dense/long routes (and the switch) with the **qualification receipts**.
2. **Activation gap.** The shipping image's lane is compiled but the base slot's window readiness is
   `not_servable`, so the compatibility **bridge** served the route. That is exactly the fallback the task says
   is not a parity pass; the activation would need the production window demand (the deferred 3.7b boundary) or
   a shipping route witness before 1.4 can pass.

The 1.4 campaign is **paused** at this decision. The cold control env
(`teensy41-capture-serial-cold-control`, `e485f542…`) and the shipping image are built; the switch and dense
routes have not been run.

## Production-demand slice (2026-10-24) — the lane now serves, with a lead gap

Narrow implementation, no removals:

- `Track::requestActiveCommittedPlaybackWindow(phase, clock)` asks for the **active demoted** slot's window; a
  whole-take 2-bar slot requests nothing (`NotServable`), a `Ready` window is left alone, and every refusal keeps
  the bridge/merged fallback reachable.
- The `PlaybackViewMaintenance` owner calls it for each **playing, settled** track before the offers, with the
  turn's qualified clock. Files: `include/Track.h`, `src/Track/TrackPlaybackWindowBuild.cpp`, `src/main.cpp`;
  the native seam test is `test_1_4_production_demand_requests_the_active_demoted_window_and_skips_whole_take`.
- RED proof: removing the request call fails exactly that test (`Expected 12 Was 4`, i.e.
  `DeferredSourceQualification` vs `NotServable`), restored byte-identically (`93b24c6d…`). Clean: RWO
  **242/242**, full native **3,444 / 7 / 3,437**, both builds at `96,192 / 392,184 + 1,032 / 34,880`.
  Shipping HEX **`225a12ea…`** (artifact `captures/device-images/shipping-rolling-demand.hex`).

**Dense 24-bar re-run** (`captures/2026-10-24-1.4-dense-shipping`, same preset/args as the cold reference):
the lane is now **demanded and serving** — `window_demand_observed` (410 gap-work turns), 25 readiness
receipts, 24 source-qualified, `windowed_service_coverage` 423/423 opportunities at permitted 64,
`build_complete_under_transport` **394** (loop_len 768, revisions 0/1). The long-loop display leg is `ok=True`.
The verifier still fails on **two named issues**:

1. `lane: no_windowed_overrun_under_transport: overrun_rows=1 under_transport=1 worst=121` — one service gap
   above the permitted 64, likely the **first interval after the record stop** (the demand is issued on the
   first PLAYING owner turn, so the window can be late for the first handoff).
2. `snapshot: no DIAG,snap_gate rows for record target t=7 s=3` — **expected on this image**: `snap_gate`
   (with `launch_gate`) is a **qualification-only diagnostic** (`include/Utils/DebugSessionCapture.h:30`), and the
   shipping env deliberately does not define `PLAYBACK_WINDOW_ROLLING_QUALIFICATION`. The shipping image therefore
   cannot emit that term; the lane/snapshot content parity must be judged on the **qualification-receipt image**
   (same lane) or with a shipping-safe route receipt. It is an evidence gate, not a functional failure.

**Status:** the shipping image now demonstrably *runs* the rolling route, but 1.4 is still **not a parity
pass** (one named overrun + the missing snapshot-gate rows). The base 2-bar route remains whole-take and its
content/ledger parity stands; the scope split is confirmed by `not_servable`/no demand on the undemoted slot.

## Record-stop lead (2026-10-24) — implemented, overrun NOT fixed

The committed demand is now also issued at the **record-stop finalize** (`finalizeLoopAtStop`, before
`startPlaying`), via the slot-specific `requestCommittedPlaybackWindowForSlot(slot, stopPhase, clock)`. The
active-slot method delegates to it; the owner-turn demand is unchanged. The finalize call runs while the
first-record/frozen view still covers the first bar, so the committed handoff has the first bar's lead.
Builds stay at `96,192 / 392,184 + 1,032 / 34,880`; full native **3,444 / 7 / 3,437**; RWO **242/242**; shipping
HEX **`c1618f14…`**.

**Dense re-run with the lead** (`captures/2026-10-24-1.4-dense-shipping-lead`): the lane still serves (405
gap-work turns, 25 receipts, 24 source-qualified, 390 build_complete) and the **one overrun remains**
(`overrun_rows=1, worst=118`). The **pre-demand qualification dense** reference
(`2026-10-24-s315f-closeout-dense-qual-24bar`) had **0** overruns. So the breach is **not the first-window
lead**: it is a **service-budget gap** introduced by the committed demand (or by the shipping image's
receipt-less lane), most plausibly at the record-stop/handoff turn. 1.4 remains unchecked.

## Recommended next step

1. **Isolate the demand's service cost** — run the same dense route on the **qualification+demand** image
   (receipts on, same lane/build). If the overrun persists there, it is the committed demand's service budget
   (3.10/6.3 scope: the owner turn's one-grain budget/rotation under the combined live+committed demand); if it
   does not, the shipping image's receipt-less lane differs and needs its own timing evidence. Either way, the
   next change is a **bounded service** fix, not another demand.
2. **Snapshot gate term:** it is qualification-only by design; run the dense lane/snapshot content parity on the
   **qualification-receipt image** (same lane) and record the shipping image's `build_complete`/demand rows as the
   behavior proof — or add a shipping-safe route receipt without enabling the qualification instrumentation.
3. **Then** re-run 1.4 (base content/ledger + dense lane + switch with a cold control) before any removal.
