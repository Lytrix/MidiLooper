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

## Recommended next step

Run the parity on the **qualification receipt image** (same `ROLLING=1` lane, with
`PLAYBACK_WINDOW_ROLLING_QUALIFICATION=1`) to obtain a **named serving route** for the base and the dense/long
routes, then compare the content to the cold reference; use the shipping image for a final content/behavior
smoke only. If the base is intended as a whole-take route, record that scope decision explicitly; if not, wire
the production window demand (3.7b) before claiming 1.4.
