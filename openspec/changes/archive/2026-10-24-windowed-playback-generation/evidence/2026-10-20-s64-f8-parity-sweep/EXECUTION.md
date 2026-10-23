# F-8 parity sweep at five demoted lengths (2026-10-20)

**Task:** 4.4 — **F-8** length-independent root residency: a residency statement per slot (windows resident, root
demoted) **plus** content parity for the demoted state, swept over loop lengths
(`proposal.md` F-8; residency census 2026-10-19).

**Device and image.** Registered target **track 8 / slot 4** (owner-authorized). The sweep's **first leg built and
flashed the committed lane image** (`5df573a2`: the `DEC-147` second form with its row diagnostics in permanent form and
the temporary probes removed) with `--build-upload`; HEX sha256
`7532e6add1c6356014cd73e44cc82f7d859375a15f5ef475eaf0cfcf3080b87b`, recorded in
`captures/i5-s64-f8-parity-23bar-02/metadata.json`. Every later leg and both content-pipeline reads ran on that flashed
image (`--no-boot-reset`). Its only difference from the image that produced the `win_ready → win_serving` chain the same
day (`a475aca2…`, which still carried the temporary probes) is the probe removal; no firmware line changed after the
flash.

**Method.** One registered preset leg per length (`windowed_content_parity --track 8 --loop-slot 4 --midi-channel 8
--no-boot-reset --record-bars N`, with `--stop-press-advance-clocks 33` for 23 bars) records the take and writes the
lane rows; the **existing content pipeline** then reads that slot
(`dump_and_capture.py --select-track 8 --dump-slot 4` + `verify_playback_content.py`). F-7/F-8 extend that pipeline;
no second oracle was built.

**Why 23 bars needed the compensation.** `--record-bars 23` first landed `18 432` (one bar long) — the preset's stop
press lands past the requested bar and the device quantises up. Advancing the press by 33 clocks (the documented
one-bar compensation at 16 ticks/clock) landed `17 664`, which is the length this sweep reports for 23 bars.

## Parity (content pipeline, `content-review.json`)

| bars | loop length (ticks) | run | cycles | rows/cycle | `inventory_equal` | `exact_order` | verdict |
|---|---|---|---|---|---|---|---|
| 23 | 17 664 | `captures/i5-s64-f8-parity-23bar-03-playback` | 3 | 1 104 | true ×3 | true ×3 | **PASS** |
| 24 | 18 432 | `captures/i5-s64-f8-parity-24bar-01` (2026-10-19) | 3 | 1 147 | true ×3 | true ×3 | **PASS** |
| 25 | 19 200 | `captures/i5-s64-f7-playback-01` (the F-7 demoted run) | 3 | 1 152 | true ×3 | true ×3 | **PASS** |
| 26 | 19 968 | `captures/i5-s64-f8-parity-26bar-02-playback` | 3 | 1 245 | true ×3 | true ×3 | **PASS** |
| 32 | 24 576 | `captures/i5-s64-f8-parity-32bar-01` (2026-10-19) | 3 | 1 536 | true ×3 | true ×3 | **PASS** |

Every cycle reports `missing=0`, `unexplained_missing=0`, `extra=0`; `capacity_path_exercised=false` (this is a parity
check, not a capacity re-stress — the same figure the earlier 4.4 evidence reports). `max_bar_refs` is reported per run
(49 at 24 and 26 bars, 48 at 25, 51 at 23 and 32).

## Residency (lane rows, same lengths)

`av=1` is "above the 12 288-tick / 16-bar demotion bound" and `sv=1` is "a complete, published current window":
together they are F-8's statement.

| bars | length | row | `av` | `sv` | note |
|---|---|---|---|---|---|
| 24 | 18 432 | `win_track_not_playing` ×5 | 1 | 1 | residency holds while stopped |
| 25 | 19 200 | `win_track_not_playing` ×3 | 1 | 1 | **`al=0`** — demand refused by name |
| 26 | 19 968 | `win_track_not_playing` ×3 | 1 | 1 | `al=1` |
| 32 | 24 576 | census + 2026-10-20 chain runs | 1 | 1 | `al=1` |

The 25-bar case is the census's finding reproduced: `19 200 % 1 536 = 768`, so the **demand entry** refuses by name
(`win_window_ineligible`) while the slot's window is resident and serving — the alignment term gates the demand, not
the residency.

## What this closes and what it does not

* **Closes F-8's sweep:** residency **and** parity now stand at the same five demoted lengths (23, 24, 25, 26, 32).
* **Not claimed:** the retained-source figure (3.6a) and 6.3's wider sweeps stay with their owners; `oracle_complete`
  is untouched (4.5/6.4), and no capacity re-stress is implied (`capacity_path_exercised=false`).
