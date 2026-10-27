# HITL test recipe — device allocation census by owner class (task 4.20)

Filled from [`docs/Templates/HITL_TEST_RECIPE.md`](../../../../docs/Templates/HITL_TEST_RECIPE.md), traps checked against
[`docs/Agents/hitl_authoring_traps.md`](../../../../docs/Agents/hitl_authoring_traps.md).
Change: [`2026-10-27-DEVICE-DISPLAY-OK-OVERDUB-HEAP-DENIED.md`](2026-10-27-DEVICE-DISPLAY-OK-OVERDUB-HEAP-DENIED.md) §17–§18.
**Status: instrument landed and built; not flashed, not run.**

| Field | Fill in |
|---|---|
| Acceptance criterion (one sentence) | Across one long session (boot → record → several overdubs → the reported `#MEM_DENIED,op=overdub_start` press), the census must show whether internal-heap loss (`heap`/`hmin`) tracks the persistent content classes (`cap`, `edit`, `ib`/`eb`/`ub`) — the answer decides whether storage policy is even the right lever. |
| Reused stimulus / flow | **No new stimulus.** For the managed part, the authoritative entry point `scripts/hitl_stop_interruption.py` (`--record-bars 24 --overdub-bars 24 --normal-stop`), which already clears its named scratch slot and drives `scripts/hitl/actions/`. For the rest, the census needs **no gesture choreography at all**: it self-reports every 2 s, so an ordinary owner session from boot through several short overdubs reproduces the reported device shape and fills the row series. Nothing new is registered, and nothing is hand-rolled. |
| New stimulus? | No — and deliberately so: the owner's directive is a *measurement* of the existing failure shape, not a new reproducer. |
| Gestures used | Only `scripts/hitl/actions/` (`buttons.py`, `slots.py`, `transport.py`, `observation.py`) through the wrapper, plus the owner's own presses during the interactive part. `control_constants.py` supplies every constant (record 36, play/stop 40, control channel 16). |
| Expected markers | `DIAG,alloc_census` — new, Tier-A, **≤1 row / 2 s** (`kAllocationCensusIntervalMs`), emitted from `DebugSessionCapture::allocationCensusRow` via the `main.cpp` diagnostics turn. Fields: `heap=` internal-heap free now, `hmin=` its `min_ever` watermark, `chunks=` free pool chunks, `cap=`/`edit=`/`undo=` cardinality, `ib=` internal-first resident bytes, `eb=`/`ub=` the edit and undo shares of `ib`, `xb=` external-first resident bytes. The existing `DIAG,reclaim` and `DIAG,pressure` rows supply the pin counts and the pressure class at the same instants. |
| Marker tier test | `test_capture_ring` — pinned by both boundaries (`#CAP,…,DIAG,alloc_census,…` true, `…,alloc_censusx,…` false). |
| `check_marker_contract.py` output | `OK   DIAG,alloc_census,: emitted=src/Utils/DebugSessionCapture.cpp:1408 tier-a-prefix='DIAG,alloc_census,' (18/31 bytes)` |
| RAM1 impact | `variables 96,192` **unchanged** (no DTCM snap; the row's literals are `SC_FMT`/`PSTR` in flash, and the census walk is `TRACK_COLD_MEM`), ITCM `code 390,008 → 390,216` (+208 B), `padding 3,208 → 3,000` (floor 840, headroom 2,160). No re-base. |
| Native test that would fail without the change | `test_shared_capture_finalization` → `test_4_20_allocation_census_matches_the_owners_it_reports` (cardinality follows the owners; `eb`/`ub`/`xb` are exactly the owners' capacities × element size; `ib` is exactly the sum of its three parts). `test_4_20_settled_overdub_heap_plateau_measurement_128_cycles` supplies the host baseline the device series is compared against. |
| Boot-census / smoke check | After the flash: attach only, confirm the boot banner, `boot_reset,crash=0`, and that at least one `DIAG,alloc_census` row arrives with `cap=1` (the empty-slot baseline). Then `!LIB_OBSERVE` for the content census. Absence must be classified, not assumed: `python3 scripts/hitl_absence_check.py <session.log> 'DIAG,alloc_census,'`. |
| Full scenario entry point | Managed part: `.venv/bin/python scripts/hitl_stop_interruption.py --track-number <T> --loop-slot <S> --record-bars 24 --overdub-bars 24 --normal-stop --out-dir captures/s4.20-census-<date>` (bounded by `HITL_MANAGED_RUN_TIMEOUT_S`). **The wrapper clears the named scratch slot** — `<T>`/`<S>` need the owner's authorization in the same session. Interactive part: the owner's own presses on the same authorized slot; no tool drives the device. |
| Verdict rule | Read the row series, not the exit code. Normalise every row to **bytes per retained pass** (`ib` and `xb` divided by `cap`). **Classes explain the decline** if `ib` rises while `heap`/`hmin` fall, with `ib`'s growth attributable to `eb` or `ub` in the same series — then the next decision is whether old committed generations may be compacted after their undo/reference lifetime expires. **Classes do not explain it** if `ib` is flat (or `eb`/`ub` flat) while `heap` falls — then, and only then, the retained-pin/`editPins` and fragmentation line is the next step. **Neither** if `RING,stats` reports Tier-A loss: that run is not evidence. |
| Evidence to record | The `captures/<run>/session.log` tail (or a `scripts/parse_*.py` helper — never a whole log), the `metadata.json`/`invocation.json` pair the managed run writes, the first and last `DIAG,alloc_census` rows quoted verbatim, and the `DIAG,reclaim` rows at the reported denial instant (`editPins=`, `passesReclaimed=`). |

## Two owner gates before this can run

1. **Flash**: `teensy41-capture-serial` builds green at the pinned RAM1 line above; programming a board needs the owner's explicit go-ahead, and a macOS upload must use `teensy_loader_cli` **2.3+** per
   [`docs/Guides/TEENSY_FIRMWARE_UPLOAD.md`](../../../../docs/Guides/TEENSY_FIRMWARE_UPLOAD.md).
2. **Destructive slot clear**: the wrapper clears the named scratch slot on the card. Per `Destructive-SD-Operations.mdc`
   the owner must name an authorized track/slot in the same session; the firmware has no restore command.

Order of work: native fixtures (done) → build + RAM1 line (done) → **ask** → smoke check → long session → row series read and recorded in §18.

## Addendum — `!HEAP_PROBE`, the allocatability discriminator (§21)

The census answers *what is resident*; it cannot answer *what is still allocatable*, because
`getInternalHeapFreeBytes()` is `&_heap_end - __brkval` — the arena's untouched tail, while freed blocks are reusable
inside newlib's own free list below it. So before this run is read as a leak verdict, take one on-demand reading:

1. Stop the transport (the command is stopped-only by design — it must not run while a deferred stop owns the turn).
2. Send `!HEAP_PROBE`. Expect a plain `HEAP_PROBE,tail=<B>,want=16384,malloc=<0|1>,pool=<0|1>` line and a Tier-A
   `#CAP,<us>,DIAG,heap_probe,…` row. `want` is the exact margin `OverdubStart` requires (16 KB).
3. Record `tail` next to the last `DIAG,alloc_census` `heap=` — they should agree (same metric, same instant).

**Decision rule:** `malloc=1` while `tail` is below the 32 KB floor + 16 KB margin (48 KB) means the admission test
is refusing on arena high-water with the block available → RED the *metric*. `malloc=0` means the tail told the truth
→ the false-denial hypothesis is dead and the allocation-event census is next. Either way the 32 KB floor itself is
not in question, and no policy change ships on one reading without its own contract.
