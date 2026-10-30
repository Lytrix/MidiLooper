# Task 5.4 — idle-turn child breakdown

## Instrumentation

Image `teensy41-capture-serial-no-grain`, HEX
`sha256 5318f2048f40632c25725a75b81ed1fd080c2c9265fe204ab3307cf1d8ecaf30`, retained in
`captures/5318f204/`. The idle-owner turn records the duration of each major child
(`RuntimeTimingTelemetry::IdleMaintChildScope`) and `main.cpp` emits one near-bar row
(`total >= 24 ms`, one per 100 ms):

```
#CAP,<us>,DIAG,idle_maint,breakdown,total=<us>,child=<us>,residual=<us>,mask=0x<hex>
```

Mask: 0 snapshot, 1 stored verification, 2 source view, 3 visual-cache slice, 4 wrap append,
5 prewarm, 6 deferred record revts. The accumulator lives in the existing telemetry `State`
(EXTMEM on target), so RAM1 stayed `96,192`; ITCM `391,976 + 1,240`.

## Measurement

Heavy base run, 32-bar record + 32-bar overdub, capture
`captures/riao-54/session_20261030_113045.log`; full native **3706 / 7 skipped / 3699 / 0** in
parallel. Four rows exceeded 24 ms, all at boot / record start (no live-overdub turn reached the
24 ms breakdown threshold):

| total us | child us | residual us | mask | dominant children |
|---:|---:|---:|---|---|
| 45,291 | 44,671 | **620** | 0x5a | verification + visual-cache slice + wrap append + record revts |
| 33,938 | 32,270 | **1,668** | 0x48 | visual-cache slice + record revts |
| 30,406 | 30,206 | **200** | 0x10 | wrap append |
| 27,273 | 26,843 | **430** | 0x62 | verification + prewarm + record revts |

The maximum residual is **1,668 us (1.7 ms)**, 3.3 % of the 50 ms bar. The scheduler and the alarm
agree: **no turn approaches the bar on residual alone**; the near-bar turns are child work. The
accepted live-overdub window has no near-bar turn.

## Finding for 5.5 / ExecutionBounds

The 45.3 ms boot/record-start turn is almost entirely admitted child work, and that work exceeds the
declared `visual_cache_slice` (3 ms) and `wrap_append` (4 ms) maxima by an order of magnitude. This is
a **boot/load-lane child-max** gap, not scheduler residual: the visual-cache/wrap-append rows are
admitted as bounded children, but their declared maxima are not enforced at boot. The live-overdub
window does not show it. It is recorded separately so it is not silently counted as a 5.4 pass.
