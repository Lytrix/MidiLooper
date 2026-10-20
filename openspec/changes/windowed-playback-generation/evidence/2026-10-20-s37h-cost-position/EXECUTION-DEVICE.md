# 3.7h — the successor demand is positioned by the cost it will be charged (device evidence)

Slice: `windowed-playback-generation` task **3.7h — the rolling selector's position pays for the window it asks for**.
Branch `feature/unified-rolling-playback-path`, HEAD before this slice `651d8ae7` (3.7g).

## Scope and transition oracle

| Item | Value |
|---|---|
| Scope | `Track::serviceRollingPlaybackWindow`'s selector position (the `lookahead` basis) and the window price it uses: new `CoverageServiceContract::deriveWindowGrainsFrom` (fills + two bounded sorts + rank pass + the pinned release) and file-local `boundRollingWindowWork` (the densest span-aligned window, from page `count`s only). The admission site now prices the counted window with the **same** function. No resolver branch, no envelope constant, no DEC-136 admission bound touched. |
| Transition oracle | A successor demand whose derived cost fits inside one loop must be positioned by that cost and reach `Ready` → `Serving` at its boundary with **no** `win_repositioned`/`handoff_unreachable`; where the cost cannot fit, today's declared position and DEC-147's bounded refusal stand. |
| Observable (device) | The lane's own receipts (`DIAG,win_frozen_readiness,t,s,complete_ready|adopted,…`) and the demand's exit rows (`win_requested`, `win_repositioned`, `win_blocked_by_budget`). |
| Out of scope | 3.7d's terminal `win_blocked_by_request` (still needs a route whose demand can finish), and the `Blocked → Idle` recovery after a transport stop. |

## RED → GREEN (native)

Three cases in `test/test_runtime_working_set_ownership` (slot 0, `forceSetState(TRACK_PLAYING)`, clock 200):

| Case | Fixture | RED | GREEN |
|---|---|---|---|
| `test_3_7h_a_dense_span_is_positioned_by_its_cost_and_reaches_serving` | 32 bars, 8 notes/bar | `the position pays the span's own derived cost` (declared 4 352 ticks) | positioned by the price, `Ready`, then `Serving` at its own boundary after six driven ticks-to-boundary |
| `test_3_7h_a_span_whose_cost_exceeds_the_loop_keeps_the_declared_position` | 18 bars, 15 notes/bar | — (pins the guard: the declared position and `win_repositioned` stand where the price cannot fit) | passes |
| `test_3_7h_a_cheap_span_keeps_the_short_lookahead_and_is_served` | 24 bars, 1 note/bar | — (no regression on the sparse shape) | passes |

Fixture notes for a future reader: the boundary predicate (`rollingCandidateReadyAtBoundary`) requires the playhead to
**arrive** (`lastTickInLoop`), so the case drives `rollingTestTick` up to the frame's own `deadlineTick` instead of
jumping the clock; the slot must be an **enabled** one (a disabled slot never reaches the emitter, so `lastTickInLoop`
never advances); and the drain retires the qualification's `coverageSource` before the boundary, because a candidate
that still owns it is refused.

Gates run: focused suite **230/230 succeeded**, full native **3352 cases: 7 skipped, 3345 succeeded**, both builds
SUCCESS, `teensy41-capture-serial-window-qualification` RAM1 `variables 96 192 / ITCM 393 216 / locals 34 880`
(padding 840 ≥ floor).

## Device read (`captures/i5-s37h-device-32bar-01`)

`windowed_content_parity`, `--clock-source host --tempo-bpm 90`, track 8 / slot 4 (firmware `t=7 s=3`), 32 bars,
boot reset. Record stop at **126.439 s**; the take then plays from its own windows. Image: `firmware.hex` sha256
`4d0c53c2…` (built from `651d8ae7` + this slice's working tree, i.e. the content of `c23cb1e1`; the run's own
`metadata.json` records `git_dirty: true` for that reason).

### A/B against the same take on the previous image

| Row (per run, same 32-bar take) | `i5-s37f-device-32bar-03` (device clock, 3.7f image) | `i5-s37g-host-clock-03` (host clock, 3.7f image) | **`i5-s37h-device-32bar-01` (host clock, this slice)** |
|---|---|---|---|
| `build_drop handoff_unreachable` (receipt family, not rate-limited) | **12** | **13** | **0** |
| `adopted,e2_boundary` (receipt family) | 12 | 5 | 6 |
| `complete_ready,clock_edge` (receipt family) | 14 | 8 | 6 |
| `win_repositioned` (gate row) | 12 | 13 | 0 |
| `win_blocked_by_budget` (gate row) | 1 | 1 | 0 |
| `win_serving` (gate row) | 1 | 1 | 0 |

The two families are read separately on purpose: the `build_drop` receipts are emitted by the owner on every refused
build and are not rate-limited, so their count is comparable across runs; the `win_*` gate rows pass through the
session ring's rate limiter and eviction, so a **zero** there is "not retained", not "did not happen" (see the
`tier_a_evicted=1` note below).

The prior image's rows are the defect 3.7h names: the demand was positioned at the *declared* basis
(`7 × 8 × 64 + 768 = 4 352` ticks, ~5.7 bars) while the admission charged the span's own derived work, so every
attempt ended `handoff_unreachable` and the traversal was re-positioned until the budget answered. The new image opens
no refused handoff at all.

### The take serves across consecutive boundaries

Six adoptions, one bar apart, each preceded by a completion ~65 ms after the previous boundary
(`complete_ready clock_edge` → `adopted e2_boundary`):

```
126.458 s adopted     (record stop 126.439 s, the take's first served window)
129.107 s adopted
131.783 s adopted
134.444 s adopted
137.117 s adopted
139.777 s adopted
```

with `build_drop build_admitted` → `published_as_ready` at 139.81–139.87 s for the next span, and **no**
`handoff_unreachable` receipt anywhere in the run. (The 21 `completed_unpublished` drops are the record-time and
stop-seam discards that the prior runs also show — 26 and 32 — not refusals.)

### What the host-clock run does **not** claim

- **No `win_gate` `win_ready`/`win_serving` row for this take.** The run's ring carried `tier_a_evicted=1` (the
  verifier's pre-existing `capture_evidence_ring_intact` gate), and the transition rows were evicted; the receipts
  above are the same events from the finer `win_frozen_readiness` family.
- **No transport-scoped verdict.** Every verifier transport check reads `under_transport=0` because this run's log
  carries no `Transport started` line: that marker was logged only by the **button** path
  (`ClockManager::toggleTransport`), never by the external-clock path (`onMidiStart`/`onMidiStop`), so a host-clock run
  has no transport intervals at all. **Fixed in the same slice** — see the `-04` run below.
- **No switch leg in this run.** The leg ended the capture at the record stop ("neither a switch leg nor a drain belongs
  here") because the transport was reported stopped, so `switch_target_span_alignment` had no target rows to read. This
  is a leg state, not a host-clock rule: the `-04` run on the same preset and take kept the transport and ran the switch
  leg normally.
- The frame goes `win_blocked` at 142.46 s, **after** the transport stops (the host teardown).

## Second device run — `captures/i5-s37h-device-32bar-02` (device clock, transport-scoped)

Same preset, same 32-bar take, same image, `--clock-source device` (the preset presses play, so the transport markers
exist). Record stop **115.209 s**. This run supplies the gate-row-grade and transport-scoped verdicts the host-clock run
could not:

| Verifier check | Result |
|---|---|
| `readiness_receipts_under_transport` | **ok** — `receipts=31 under_transport=31`, `source_qualified=51 of 350` frozen receipts (`first=adopt_wait/e2_boundary`) |
| `windowed_service_coverage_under_transport` | **ok** — `blocks=576 under_transport=557 with_opportunities=557 permitted=64` |
| `no_windowed_overrun_under_transport` | **ok** — `overrun_rows=0` |
| `switch_target_span_alignment` | **ok** — `target t=7 s=4 span-aligned len=18432` |
| `window_boundaries_crossed_under_transport` | **ok** — `build_complete_under_transport=513` |
| `capture_evidence_ring_intact` | **fail** — `tier_a_evicted=1` (pre-existing on this image) |
| `window_demand_observed` | **ok** — 133 gate rows |

**The 32-bar take serves the whole traversal:** **12 consecutive one-bar boundary adoptions** (116.54 → 145.84 s,
every 2.665 s = one bar at 120 BPM), and the frame blocks at 148.52 s only when the switch leg selects the other slot.

**The cost-does-not-fit case is preserved, on the right slot.** All 12 `handoff_unreachable` receipts and all 12
`win_repositioned`/`win_retry_ordinal` gate rows are on **`t=7 s=4` — the 24-bar switch target** (`len=18432`), whose
loop cannot hold the priced lead by construction; it ends in `win_blocked_by_budget` exactly as DEC-147 describes,
while the 32-bar take on `s=3` opens none. That is the contract's second clause working on device, not a regression:
the slot with `len=18432` and the slot with `len=24576` get different answers from the same price.

## The host-clock run re-judged from its own capture (no rerun, no firmware change)

`Transport started`/`Transport stopped` come only from the **button** path (`ClockManager::toggleTransport`). The
external-clock path that actually runs the transport in the host-clock leg — `ClockManager::onMidiStart` /
`onMidiStop` — logs its **clock** edges instead, and both are in `-01`:

| Edge | Line in `session_20261020_222418.log` | Time |
|---|---|---|
| open | `External MIDI clock detected` (INT→EXT, emitted by `onMidiStart`'s `actuallyTransition`) | **40.744 s** |
| close | `MIDI clock lost, switching to internal at 90.0 BPM` (EXT→INT) | **184.079 s** |

Re-judging the existing capture with that interval, the same five checks give the same picture as the device-clock run:

| Verifier check | `-01` with the external-clock interval |
|---|---|
| `readiness_receipts_under_transport` | **ok** — `receipts=32 under_transport=32`, `source_qualified=18 of 188` frozen receipts, first `adopt_wait/e2_boundary` |
| `windowed_service_coverage_under_transport` | **ok** — `blocks=565 under_transport=555 with_opportunities=555 permitted=64` |
| `window_boundaries_crossed_under_transport` | **ok** — `build_complete_under_transport=511`, `loop_len=768`, revisions `0,1` |
| `no_windowed_overrun_under_transport` | **fail** — one row (see below) |
| `switch_target_span_alignment` | not judgeable — a host-clock run has no switch leg by design (`-02` covers it) |
| `capture_evidence_ring_intact` | **fail** — `tier_a_evicted=1`, pre-existing |

**The interval is not a heuristic — the device agrees with it turn by turn.** The 555 `win_gap` rows that report
`running=1` form one continuous run from **42.08 s to 183.31 s**, and **no** service turn between the first readiness
receipt (44.92 s) and the last source-qualified frozen receipt (180.29 s) reports the transport stopped. The receipts
themselves span 126.46 → 139.87 s, i.e. the take's whole playback.

**Two named limits remain, and neither is a 3.7h regression:**

- `no_windowed_overrun_under_transport`: one row at **42.08 s** — `permitted=64 overruns=1 overrun_max=72`, the record
  leg's **first** service turn. The identical row is in `-03` on the previous image and in **neither** device-clock run:
  it is a host-clock-leg artifact (a ~2 s stall at record start measured as a 72-tick service gap), not the playback
  view advancing past its bound. The device-clock run `-02` is the overrun authority.
- `switch_target_span_alignment`: no switch leg in host mode, by design.

So the missing button markers are a **measurement** detail of the host-clock leg, recoverable from the capture the leg
already writes; no firmware slice is needed to read it. If a future host-clock leg should score transport-scoped gates
automatically, the two-line durable fix is to log the transport edges from `onMidiStart`/`onMidiStop` too.

## Transport markers on the external-clock path — `captures/i5-s37h-host-clock-04`

The durable fix is two lines and changes no behaviour: `ClockManager::onMidiStart` and `onMidiStop` now write the same
`Transport started` / `Transport stopped` markers the button path writes — after the tick-0 alignment and the track
fan-out in the one, after the stop fan-out in the other, i.e. exactly where `toggleTransport` writes them. The verifier
therefore reads the edge the firmware itself acts on (`sequencerRunning`), not a proxy derived from clock-source
transitions. No DEC: no schema, tier, ownership or lifecycle change, and the message text is the existing one, so every
host parser that looks for these lines sees a real edge instead of a button press. Both builds SUCCESS with RAM1
unchanged (default `variables 96 192 / code 392 216 / padding 1 000`; qualification `96 192 / 392 376 / 840` — the floor,
0 B of ITCM); full native **3352 / 7 skipped / 3345 succeeded**.

Same preset, same 32-bar take, same host clock at 90 BPM, image `eace974f…` (this slice's qualification build; the run's
`metadata.json` records `git_dirty: true` because the commit followed the run). Record stop **113.689 s**, `RECS,stop` →
**24 576 = 32 × 768**.

| Check | `-04` (host clock, 90 BPM) |
|---|---|
| `readiness_receipts_under_transport` | **ok** — 30/30 under transport, 32 source-qualified of 261 |
| `windowed_service_coverage_under_transport` | **ok** — 546 opportunities |
| `no_windowed_overrun_under_transport` | **ok** — 0 rows |
| `switch_target_span_alignment` | **ok** — `t=7 s=4 len=18432` |
| `window_boundaries_crossed_under_transport` | **ok** — 490 completions |
| `capture_evidence_ring_intact` | **fail** — `RING,tier_a_evicted,41,first=192.35 s` (pre-existing ring budget) |

Markers: `Transport started` **7.940 s** (the boot/clear leg's play press) → `Transport stopped` **342.618 s**
(teardown) — one interval, with the take's playback (116.32 → 126.99 s) inside it. Unlike `-01` this run kept the
transport through the record stop, so the switch leg ran: the take adopts **5** consecutive one-bar boundaries, and all
**13** `handoff_unreachable`/`win_repositioned` rows are on `t=7 s=4` — the 24-bar switch target (`len=18432`) whose loop
cannot hold the priced lead — ending in `win_blocked_by_budget`. The same contract split the device-clock run shows, now
readable on the known clock.

The ring evictions begin at 192.35 s, after the take and the switch, during the 170 s settle; every row the five green
checks read precedes them. The `no_windowed_overrun_under_transport` row that `-01` carried at 42.08 s did not recur
here, which is consistent with it being a record-start stall rather than a view advance.

## Verifier result (all three runs)

`captures/i5-s37h-device-32bar-01/…json` (host clock, known 90 BPM, **before** the marker fix): `window_demand_observed`
**ok** and `capture_evidence_ring_intact` **fail** (`tier_a_evicted=1`) as reported by the run; re-judged above from the
same capture, three of the five transport-scoped checks are **ok**, `switch_target_span_alignment` is absent because the
leg ran no switch, and the overrun row is the named host-clock artifact.

`captures/i5-s37h-host-clock-04/…json` (host clock, known 90 BPM, **with** the marker fix): all five transport-scoped
checks **ok**, `window_demand_observed` **ok**, `capture_evidence_ring_intact` **fail** (`tier_a_evicted`, pre-existing).

`captures/i5-s37h-device-32bar-02/…json` (device clock): five of six lane checks **ok** (table above), with the same
pre-existing ring gate the only failure.
