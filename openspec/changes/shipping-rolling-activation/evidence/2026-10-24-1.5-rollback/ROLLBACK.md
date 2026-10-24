# 1.5 rollback drill — cold image (2026-10-24)

**Pair**
- Shipping activation (rolling + demand + stable hold): `09a5ae6911eabfac5f7ddd682467cfd4d3910dc35fddf7b981b3a571caab8f40`.
- Rollback (`teensy41-capture-serial-cold-control`, `ROLLING=0`, current source with the demand/guard
  compiled out): `e7e4b4fc4934d833d1b60fd09a31268b9413683783c94426f78573582633478e`
  (1287168-byte HEX, 15.8%). Built SUCCESS; flashed and booted.

**What the cold image did**
- The legacy base route recorded: **record on/off 32/32**, final loop **1536/1536** (raw 1528), display
  transport stop ok, `HEAP_STATS` observed (RAM2 floor **131,072 B** on this set; the 1.1 cold floor was
  147,456 on its set).
- The full base run ended `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)` — the overdub/second-overdub phases
  did not run (`0/0`), because the bench set/transport state blocked the capture (the same harness/state
  limit as the shipping dense display leg). The legacy **route** is confirmed by the record; the **full
  base parity** is not re-claimed on this state.

**Gap**
- The 1.1 cold artifact `dab405b3…` is **not preserved**; the rollback is the current-source `ROLLING=0`
  build above, whose HEX differs by build stamp and by the deferred residuals (the 4.2 guard/demand code
  compiles out under `ROLLING=0`). A clean-bench run is required to re-state the full base parity and to
  close the rollback drill; do not start the family removals before that.
