# 6.5 — the hardware claims read off the DEC-140/141 campaign's own rows (2026-10-17)

**Disposition: three of 6.5's four hardware claims are now *measured* on the device; the fourth is still NOT RUN, and
nothing is checked off.** 6.5 stays open — this packet replaces "not run" with numbers and a limit, not with a verdict.
No firmware, instrument, limit or checkoff change; the rows were already being emitted and the parser that reads them
was already shipped.

## Identity

| | |
|---|---|
| Image | `32f6501d2051bc38af046c204d2e38e6773987f2cc3ee36714ada7069e13acaa` (qualification env) |
| Build env | `teensy41-capture-serial-window-qualification` (rolling lane enabled) |
| Source | git `26125cebb08980342ba56ed2b77e5397783ac8e5`, `git_dirty: true` — the DEC-140/DEC-141 tree |
| Preset / target | `rolling_record`, track 8 / slot 4, host clock **120 BPM**, 192 PPQN, 768 ticks/bar |
| Campaign | `captures/i5-slot45-diagnosis-01/campaign-01/` — aggregate `verdict=PASS`, `full_recipe_complete=true` |
| Legs read | `growth-empty` (8 bars recorded live), `held`, `matched` |

The recipe PASS is what makes this reading legitimate rather than opportunistic: the lane had a **demand** under a
running transport in every leg, and the device image and host-verifier fingerprint are the same for all three passes.

## What was read, and from where

No new instrumentation. Two existing emitter families carry 6.5's hardware quantities:

- `#CAP,<us>,DIAG,win_gap,<maxDelta>,<opportunities>,<phase>,<running>,<discontinuities>,<eligibleMax>,<eligibleTurns>,
  <progressTurns>,<deniedTurns>,<maxWorkerUs>,<epoch>,<saturatedGaps>,<permitted>,<overruns>,<overrunMax>,3` —
  `DebugSessionCapture::windowServiceGapImmediate`, fed by `PlaybackLaneMeasurement::observe` on **every** lane
  dispatch, emitted on the first dispatch, every 256th, on `BuildComplete` and on `GrainDenied`.
- `[Memory] ext,<us>,phase=…,live=<KB>,high=<KB>,…,heap_free=<KB>,heap_min=<KB>,…` — `MemoryMonitor`, once per second.

Reproduce (the field positions are pinned by
`scripts/test_windowed_content_parity_verify.py::test_gap_row_reads_the_service_and_worker_fields_six_point_five_claims`):

```bash
PYTHONPATH=scripts .venv/bin/python - <<'EOF'
from pathlib import Path
from hitl.verify.windowed_lane import gap_rows, overrun_rows
ROOT = Path("captures/i5-slot45-diagnosis-01/campaign-01")
for leg in ("growth-empty", "held", "matched"):
    lines = (ROOT / leg / "rolling-record-serial.log").read_text(errors="replace").splitlines()
    rows, ov = gap_rows(lines), overrun_rows(lines)
    run = [r for r in rows if r["running"] == "1"]
    print(leg, len(rows), len(run), (run[-1]["us"] - run[0]["us"]) / 1e6,
          max(int(r["max_worker_us"]) for r in run),
          max(int(r["eligible_predecessor_max"]) for r in run),
          max(int(r["max_delta_ticks"]) for r in run),
          sorted({r["permitted"] for r in rows}),
          sum(int(r["overruns"]) for r in rows), len(ov),
          sum(int(r["saturated_gaps"]) for r in rows),
          sum(int(r["denied_turns"]) for r in rows))
EOF
```

## Claim 1 — grain duration: **measured**

The preparation grain's own duration is the block's worst **worker** span (`maxWorkerUs`), i.e. the longest single
lane turn observed in that reporting block, over every dispatch the block contains.

| Leg | Blocks (running / total) | Running span | Worst worker span |
|---|---|---|---|
| `growth-empty` | 54 / 75 | 52.8 s | **595 µs** |
| `held` | 16 / 19 | 15.5 s | 448 µs |
| `matched` | 9 / 17 | 6.4 s | 467 µs |

Campaign worst: **595 µs**, in `growth-empty`. The five worst running blocks there read 445 / 459 / 464 / 464 / **595** µs.

**Not claimed:** this is a **maximum observed**, not a WCET or an owner/IRQ-return bound. The worst block sits 2.49 s
away in device time from the nearest accepted persistence completion (44.1 ms), so the 595 µs is **not attributed** to
persistence contention — no correlation is visible and none is inferred.

## Claim 2 — maximum service gaps: **measured, and no named exceedance**

The declaration travels in the row itself: `permittedAdvanceTicks = 64` ticks (166.7 ms at 120 BPM / 192 PPQN,
`Track::kPlaybackViewPermittedAdvancePerTurn`). The gap that 6.5 is about is the one whose predecessor was *eligible*
(`eligiblePredecessorMax`), because an idle gap is not a scheduling failure.

| Leg | Worst eligible-predecessor gap | Worst gap (any) | Overruns | `win_gap_overrun` rows | Saturated |
|---|---|---|---|---|---|
| `growth-empty` | **40 ticks** (104.2 ms) | 56 ticks (145.8 ms) | 0 | 0 | 0 |
| `held` | 16 ticks (41.7 ms) | 32 ticks (83.3 ms) | 0 | 0 | 0 |
| `matched` | 16 ticks (41.7 ms) | 16 ticks (41.7 ms) | 0 | 0 | 0 |

So on this image the worst eligible gap is **40 of the permitted 64 ticks (62.5 % of the bound)**, and the lane's own
named exceedance (`win_gap_overrun`) never fired. `denied_turns = 1` per leg; discontinuities 8 / 3 / 3 are the era
changes and tick re-anchors across the legs' transport restarts (epochs 1–11, 11–15, 16–20).

**Not claimed:** a gap below the bound is not a deadline proof for the *musical* path, and `maxDeltaTicks` includes
non-eligible gaps, which is why the two columns differ.

## Claim 3 — memory high-water marks: **recorded for this image and shape**

| Leg | External pool high-water | Live at leg start → end | Internal heap free (min) | Endpoint `#HEAP_STATS` |
|---|---|---|---|---|
| `growth-empty` | 3,771 KB | 2,337 → 3,766 KB | 100 KB | arena 233,920 B, used_chunk 211,904, largest 21,448 |
| `held` | 4,097 KB | 3,766 → 4,092 KB | 100 KB | arena 258,496 B, used_chunk 227,568, largest 30,104 |
| `matched` | 4,097 KB | 4,092 → 4,092 KB | 100 KB | arena 258,496 B, used_chunk 227,632, largest 30,104 |

Campaign high-water: **4,097 KB (4,195,328 B) of the 8 MB external pool**, with the internal heap never below
**100 KB free** (`heap_min` 100 KB in every row, matching the `PERS,result` heap fields of 102,400 B).

**Named limits, not smoothed:** `high` is the pool's **boot-lifetime** high-water and is monotonic, so the per-leg
values are *cumulative* — the `held`/`matched` 4,097 KB cannot be attributed to those legs alone. The row's cadence is
1 s, so a transient peak shorter than the cadence would be missed. And 6.5 asks for high-water marks **with boundary
content parity**; the parity half is F-7 (task 4.3), which is not landed, so this column is a memory reading only.

## Claim 4 — sustained multi-track playback with capture/persistence contention, external clock: **still NOT RUN**

- **Multi-track:** the whole campaign ran one track (8) with one slot (4) against a `--record-bars 8` take. Persistence
  contention *is* present inside the running span (`growth-empty`: 6 of 9 accepted saves complete while the transport
  runs, 44–163 ms each, all `ok`) and capture contention is the live 8-bar Record itself — but a **second** sounding
- **Multi-track:** the whole campaign ran one track (8) with one slot (4) against a `--record-bars 8` take. Persistence
  contention *is* present inside the running transport — every accepted save of every leg completes while it runs
  (`growth-empty` 9 saves at 44.1–126.8 ms, `held` 5 at 44.1–162.7 ms, `matched` 3 at 44.3–47.2 ms, all `ok`) — and
  capture contention is the live 8-bar Record itself. But a **second** sounding track competing for the same lane owner
  and drain turns is not exercised by this recipe at all.
- **External clock:** the preset drives the host clock at 120 BPM. A DIN/external-clock leg, with its own epochs and
  arrival jitter, has not run under the rolling lane.

So of 6.5's hardware half, two claims are measured and one is recorded for this image, and the contention/external-clock
claim remains open — it needs its own registered pass, not an extrapolation from this campaign.

## What this packet does not change

- **No firmware line, no instrument, no limit.** The rows and the parser already existed; the only code change is that
  the shipped parser now exposes the fields it was already splitting (`scripts/hitl/verify/windowed_lane.py`), pinned by
  one new host test.
- **6.5's native half is untouched.** The **interval A/B revision trace** is still only partial (shared-view fixtures,
  no rolling-chain trace), which is the other named gap in 6.5.
- **No device action:** the campaign was already run and accepted; the board is on the pinned default image. No upload,
  no SD action, no Git delivery from this packet.

## Gates

Host only; no firmware or native C++ source touched, so the native suite was not re-run (its last recorded state for this
tree is 3,303 cases / 7 skipped / 3,296 succeeded / 0 failed).

- `scripts/test_windowed_content_parity_verify.py` **51/51** (one new case). **Red-proved** by mutation
  (`"max_worker_us": fields[13]` → `fields[12]`), which fails **exactly** that case and nothing else, file restored
  byte-identically (`sha256 3407bf17…`).
- `scripts/test_envelope_timings.py` **56/56** (the other host module that parses `win_gap` rows directly).
- Full host discover (`scripts/test_*.py`) **1 841 tests / 7 failures / 1 error**. The failing names are the repo's known
  group and **none is in a module this change touches**: `test_compatibility_consumer_inventory`
  (`test_materialize_to_event_vector_only_in_machinery_owners`, `test_session_exit_gap_predicate_only_in_visual_hold_owners`,
  each seen twice because discover also loads the file as `__main__`), `test_6_3_machinery_closeout_gate.test_all_6_3_host_gates_pass`,
  `test_layered_verifiers.test_record_overdub_capture_verifier_uses_base_report_config`,
  `test_foundation_runner.test_verify_only_record_overdub_writes_report`,
  `test_accepted_sequence_scenario.test_the_existing_presets_and_scenarios_are_unchanged`,
  `test_occupy_paint_serial_verify.test_pass_when_od1_stop_uses_odub_enter_without_st`, and one error
  (`test_edit_session_state_serial_verify.test_enter_fails_when_cycle_before_select`). An earlier session recorded **3**
  errors on this tree; the error count is environment-dependent and was **not** investigated here — nothing in it names
  this change's modules.
- `openspec validate windowed-playback-generation --strict` valid; `git diff --check` clean.

## Next

1. A registered pass that drives **two sounding tracks** plus a save while the lane serves (claim 4, multi-track), and a
   pass on an **external/DIN clock** — both under the existing 6.4 registration rules, not by editing the canonical
   stimulus.
2. The **interval A/B revision trace** on the rolling chain (6.5's native remainder).
3. Nothing here checks off 6.4 or 3.15f: the recipe covers the rolling **Record** route only, and 3.15f still needs
   overdub, NOTE_EDIT, switch, undo/redo and both clock sources compared against their own oracles.
