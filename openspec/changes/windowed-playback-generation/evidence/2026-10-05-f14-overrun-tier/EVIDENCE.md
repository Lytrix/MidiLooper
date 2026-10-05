# F-14 — the named overrun row is Tier-A, and the drain that finally reads the lane under transport

Evidence for `EXECUTION.md` in this directory. This slice's own packet corrects the previous message: **F-13 was
wrong**, the real gap is narrow (**F-14**), and the device run that follows it produces the change's **first positive
observation of the windowed lane serving under transport**.

## F-13 withdrawn, F-14 recorded

The previous slice claimed both `DIAG,win_gap,` and `DIAG,win_gap_overrun,` were missing from the Tier-A classification.
Reading the code first refuted that:

* `src/Utils/DebugSessionCapture.cpp::isTierATextLine` already classifies `DIAG,win_gap,`, and records why it lives there
  rather than in the header: the header list is an inline function, so *"one more literal of that size was measured to
  tip this image's DTCM across an ALIGN boundary (RAM1 guard: `variables` +4,096 B)"*.
* The classification was already pinned by a production-predicate fixture (`test_capture_ring`, line 421).

What survived is narrower and was real: **`DIAG,win_gap_overrun,` is not Tier-A** (its 13th byte is `_`, not `,`, so it
does not share its sibling's prefix). It is the row that reports an **actual exceedance** of the declared permitted
advance — and an exceedance can only be raised while the lane serves, i.e. under transport, the budget-8 regime whose
shed path drops non-Tier-A head records **silently**. The row that reports the exceedance was the one dropped where
exceedances happen.

**The fix** is one entry, beside its sibling and for the same reason:

```cpp
return CaptureLineTier::tagStartsWith(line, "DIAG,win_gap,") ||
       CaptureLineTier::tagStartsWith(line, "DIAG,win_gap_overrun,");
```

**Where the fixture can live — a layering fact found by trying.** A first attempt asserted the new classification in
`test_capture_line_tier`; it failed, and the reason is a property of the suites rather than of the code: that suite asks
`CaptureLineTier::isTierALine`, the **header** predicate, and the lane's gap families are deliberately not in that header.
`scripts/native_test_link.py` does not link the production TU into that suite, while it does for `test_capture_ring`. The
pin therefore lives in the ring suite, which asks the **production** predicate (`isTierATextLine`) that the shed path
itself consults. The misplaced fixture was removed rather than forced, and its removal is recorded here.

| ID | Change | Result |
|---|---|---|
| **M1** | the entry removed | `test_capture_ring` fails at `the named overrun row survives the timing-critical flush` (16 others green) |
| **M2** | the entry widened (prefix without the trailing comma) | fails at `and a longer subtag is not swept in`. **First attempt at M2 was a syntax error** (a dangling `||`) and failed the *build*, which is not a red-proof; it was corrected before counting |

Both restored byte-identically (`src/Utils/DebugSessionCapture.cpp` sha256 `6fbe30f665ee3cad…` after the restore).

## The drain: why the lane's rows were never readable before

The two earlier device runs could not see the playing session at all — the ring is **retained** under transport, and the
harness stopped reading while the transport was still running. The wrapper now ends the leg properly: it reads the leg's
own capture, and when the last transport edge left the transport **running**, it presses the global transport stop and
reads a further 12 s into its own log (`host_midi_hitl_windowed_post_stop_drain_*.log`). Two defects were found and fixed
by the first run that had a drain to do: the serial port is kept in the leg's *arguments* rather than on `args` (the
drain returned early), and the stop press must be gated on the transport actually running, since a toggle would *start*
one.

**The verifier's "under transport" rule was wrong for drained captures, and that mattered.** It read stream *position*;
a retained row is delivered *after* the stop, so position would call it post-stop when it was produced mid-session. Both
row families carry the same clock (`#CAP,<micros>`; `[12.555]` seconds), so the rule is now **device time** — which is
what makes the drain readable at all.

## The device result (run 04, qualification image `e6526831ef6c8bce…`)

From `device-lane-rows-after-drain.txt`:

- **54 `win_gap` blocks, 50 of them with `running=1`**, with `phase` rising 792 → 1682 → 2581 → 3494 → 4414 → 5333 …
  **The lane is dispatched under transport on hardware**, which is the fact the earlier runs could not show and which
  F-12 had guessed the opposite of.
- **Every block `permitted=64`, `overruns=0`** — no exceedance of the declared advance in this leg.
- **Every block `eligible=0, progress=0`** — the lane had **nothing owed**. With the rolling lane open, a plain
  record-and-play still owes no window: the rolling request declines while an edit session is active or a track is not
  playing in the sense it needs, and no launch was owed on this target.
- **0 `win_readiness` rows** in either the leg's log or the drain. That family is Tier-A **and** direct-when-USB-has-room,
  so this is evidence (no receipt was produced) rather than a blind spot — and it agrees with `eligible=0`.
- **0 `win_gap_overrun` rows** — nothing for the newly-protected row to report, which is the honest reading of a `0`.
- 4 996 `playback_build` rows (paths `note_edit`, `snapshot`, `compatibility`) — the legacy merged-window path during
  NOTE_EDIT, which is what the target actually did.

The verifier's verdict on the combined capture:

```
window_demand_observed:                        false  (no eligible/progress turn, no receipt: nothing was owed)
readiness_receipts_under_transport:            false  (receipts=0)
windowed_service_coverage_under_transport:     TRUE   (blocks=54 under_transport=21 with_opportunities=21 permitted=64)
no_windowed_overrun_under_transport:           TRUE   (overrun_rows=0)
window_boundaries_crossed_under_transport:     false  (build_complete_under_transport=0)
```

**What that is worth, and what it is not.** It is the change's first *measurement* of the lane on hardware: the service
turn is reached under transport (50 blocks), at the declared permitted advance, with no exceedance. It is **not** the
envelope: `eligible=0` everywhere means the lane never had work, so nothing about grain duration, service gaps under
load, contention or the receipts has been measured. The profile's oracle is unchanged and still unmet, because the leg
still owes no window — the next step is a leg whose gestures create a demand (a demoted slot switch), which is a driver
change and the owner's call.

## Verification

- `pio test -e native` — **2796 cases / 2789 succeeded / 7 skipped / 0 failed**, with `test_capture_ring` and
  `test_capture_line_tier` both PASSED in that run.
- `test_windowed_content_parity_verify` — **17 tests, OK** (the device-time rule, the port lookup, the stop-press gate).
- Both capture envs build through the RAM1 guard with **byte-identical** lines: `teensy41-capture-serial`
  `code 391,576 + padding 1,640`, and the qualification env `code 392,232 + padding 984` — so the new literal costs
  nothing, exactly as the recorded reason for its placement predicted.
- Device: image `e6526831ef6c8bce…` flashed and driven; capture at `captures/s64-window-leg-04/`.

## Not claimed

- The hardware envelope (grain duration, maximum service gaps, contention, external clock, residency) — `eligible=0` in
  every block means the lane was never given work to do.
- F-7/F-8 or AC-8: no receipt, no publication, no boundary crossing was observed, and the profile's oracle says so.
- Any change to the flush budget, the shed rule, or lane behaviour: the fix is one classification entry.
- That F-14's fix was *needed by* an observation: no device run has yet produced an overrun row. Its justification is the
  regime argument above (an exceedance can only happen under transport), and that is stated as the justification.
