# Overdub source view — wrap-held tail paired with the wrong head Off

**Status:** Open — host pin next. Do not patch reconstruct wrap pairing from the 4-event sketch below.  
**Date:** 2026-09-02  
**Kind:** bugfix  
**Parent:** [`overdub_wrap_companion_and_pairing_bugfix.md`](overdub_wrap_companion_and_pairing_bugfix.md) (RC2b parked; this is a new recapture gate, not `2296→672`)  
**Evidence:** [`013709`](../../captures/session_20260902_013709.log) occupy `pitch=39,as=3024,ae=448,hs=288`; storage `WRAP,3024,64`; effective `DNTE,39,288,288,2784`  
**Does not authorize:** Gate 5A order change; equal-tick consume ([`overdub_reconstruct_equal_tick_pair_not_open_bugfix.md`](overdub_reconstruct_equal_tick_pair_not_open_bugfix.md) is a different RC)

---

## Debugging boundary

```
storage WRAP 3024→64 / SEVT N@3024 F@64
    ← trust
equal-tick N+F@776 ch13
    ← closed 932a58d6
source view as=3024 ae=448 at hs=288
    ← this RC
effective 39@288 len 2784
    ← same Off@448 stolen from On@288
```

## Problem

At 66.079s occupy read source view `39 @ 3024→448` while the deferred WRAP dump is `3024→64`. Hold 288 is inside that phantom wrap-held span, so occupy resolves against the wrong row. Effective reconstruct then leaves `39@288` open to loop end (2784).

A 4-event host reconstruct (`Off@64`, `On@288`, `Off@448`, `On@3024`) does **not** emit `3024→448`. It leaves `3024→3071` because `tryPairWrappedTailOn` for `Off@64` is blocked by intervening `On@288` (existing wrap-pair-blocked rule). The device row is therefore not that isolated pairing. Next pin must use the committed source-view merge / `collapseSplitHeadTailToWrapHeld` / companion materialize path with the capture’s full pitch-39 SEVT set.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** if the fix stays in `rebuildOverdubSourceView` / `collapseSplitHeadTailToWrapHeld` / companion materialize |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** if the pin stays on the existing source-view rebuild |

Stop and reopen a design session if the pin requires changing `tryPairWrappedTailOn` globally.
