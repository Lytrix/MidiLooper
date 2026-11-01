# 2026-11-01 — DEC-241 verification: the internal-clock ISR tick no longer builds

Commit `b260d4d0`. Native: RED→GREEN in `test_runtime_profiler_sites`, two mutations caught, full native
**3,827 / 8 skipped / 3,819**. RAM1 variables unchanged (state is `DMAMEM`, cleared at boot); default ITCM code
391,928 → 391,944 (padding 1,272); profile padding 936.

## Device

Owner chain, fresh boot, Record ≈ 14–17 s, then the capture held **45 s** after the rig so the host-clock-lost
internal-clock interval is recorded (`MIDI clock lost, switching to internal at 120.1 BPM`). Split by clock source with
the `DIAG,midi_gap` / `DIAG,late_on` 5 s rows.

| Run | Image | Chain valid | external midi_gap med / max | internal midi_gap med / max | internal late_on med / max | `PlaybackWindow_build` from ISR |
|---|---|---|---|---|---|---|
| chain-78 | **pre-fix** profile `f50738f38989e129a7608287d16d90075dd494f7af66588247c9b85fb981745b` (`session_20261101_025133.log` parent) | yes | 63.5 / 210.9 ms | 18.5 / **143.2 ms** | **225 / 277 ms** | 55–57 (earlier runs) |
| chain-77 | **fixed** profile `9dbb5a2abeb9b9738a69f02e3684f8e381d6137f337f7942fb7d26f0b3c354fc` (`session_20261101_024006.log`) | yes | 30.4 / 180.7 ms | **11.0 / 12.8 ms** | **126 / 128 ms** | **0** |

Fixed-image read-back (`session_20261101_024350.log`): `PlaybackWindow_build calls=114 ref=0` (main-lane builds rose
from 74 to 114: the owner now builds what the ISR owed), `TickMergedFallback_build ref=184,393` (cheap consume checks
from the ISR), `isr_last=138:25`.

**Reading.**
- Internal clock: the 143 ms gap is gone and lateness halved. A flat ~126 ms `late_on` remains in every internal
  window; it was present before (225–277 ms), so it is a separate, pre-existing internal-clock lateness — **open**.
- External clock: unchanged by this slice (the main-loop tick still builds); the differences are run-to-run variance
  between two single runs and are not claimed.
- Each window move under the internal clock now costs a contained tick until the owner builds; the normal-case
  "qualified source before internal takeover" layer (prefetch the next window off-clock) is the next step.

Board left on the fixed default image `f27cf58c03162c6332ff4001403cd341186361294c283439c9bde58623ddd251`
(boot proven: `boot_reset,crash=0`, `BOOT,load,ok`).
