## Context

The capture ring is a bounded per-transport-session evidence buffer with a deliberate no-drain policy
during `PLAYING`/`OVERDUBBING`. RC-MEM-7 made Tier-A displacement non-fatal and observable; this change
makes the stream small enough that a 15–20 minute session does not displace anything at all, and makes
the host refuse to call such a run clean when it did.

Ownership (respecting `docs/Authority/ARCHITECTURE_RULES.md`, no ownership moves):

- `include/Utils/CaptureLineTier.h` — **classification**. Header-only so the host can test it without
  Arduino. Owns tag parsing, tier membership, and (new) family + behaviour class.
- `src/Utils/DebugSessionCapture.cpp` / `include/Utils/DebugSessionCapture.h` — **the ring**, admission,
  budgets, eviction, notices and the coverage summary. Owns all of it; nothing else writes the ring.
- `src/Utils/MemoryMonitor.cpp` — keeps `[Memory] ext` **direct to serial**, unchanged.
- The harness (`scripts/hitl/…`) — **verdicts**. Consumes the coverage summary; owns `PASS`/`DEGRADED`.

## Options considered

| # | Option | Verdict |
|---|--------|---------|
| A | Tier-aware degradation only (split Tier-A vs critical loss, advisory) | **Landed** (RC-MEM-7). Fixes survivability, not completeness. |
| B | Profile-selected, per-family **byte-budgeted** capture with bounded bursts and semantic behaviour | **This change.** |
| C | Uniform divisor on all Tier-A | **Rejected.** Thins `DISP`/`VCACHE` as hard as UI rows; the 5.12 display regression was pinned by exactly one `DISP` + one `VCACHE` row per second, which a divisor would have hidden. Also loses drop attribution. |
| D | Rotate families at full rate | **Rejected.** Coverage gaps land exactly on the failure window; a wrap/stop bug needs the wrap/stop second. |
| E | One always-on minimal profile, no per-bug-type selection | **Rejected.** No single set serves both a publication/memory bug and a persistence-drain bug. |
| F | Enlarge the ring | **Deprioritized.** 4 MiB buys ~2 min at the observed rate and spends recovered PSRAM headroom. |
| G | Direct-to-serial for low-rate high-value rows | **Kept as-is** (already the pattern for some denial rows). Not a budget mechanism; `[Memory] ext` relies on it. |
| H | Offload the ring to SD through the deferred/sliced persistence lane | **Deferred, not rejected.** The only option that scales to hours and the only one that survives a hang/reset (today a reset destroys the ring, which is why a reported hang produced a 0-byte session log). It extends the sink, not the stream: at ~30–35 KB/s it would push 36–42 MB per 20 minutes of constant deferred I/O, and a bug that inflates logging 10x becomes a heavier storage workload rather than a bounded one. Do it when reset-survivability, unattended soaks, or never-reaching-`STOPPED` bugs become requirements. |
| I | Compact binary ring records | **Deferred until after B**, and blocked on the gap below. |

## The primitive

Each family gets a **token bucket in bytes** plus a semantic rule, not a row cadence:

```
family:
    bytes_per_second
    burst_bytes
    behaviour: edge | sample | stream
```

Why bytes: rows range ~40–190 B, so `1 row/s` is not a space bound, and the goal is a **duration
guarantee**. Why the semantic rule on top: a byte budget alone would happily spend the whole family's
budget on the first second of a burst and then drop the stop cluster.

Why bursts must be **bounded**: the guarantee is only mathematical if the worst case is bounded, i.e.

```
sum(burst_family) <= (Tier-A capacity after reserves) * safety_factor
```

A family may be quiet for ten seconds and then spend accumulated budget around a transition — that is
the point — but it may not accumulate without limit.

### Behaviour classes

- **edge** — transport/state transitions, admission and denial, stop/commit edges. Never cadence-
  sampled. Subject only to a hard pathological-rate guard (which must *report*, not silently drop).
- **sample** — memory, display, coverage. **Coalesced**, not sampled-by-drop: ten samples in a second
  become one row carrying latest/min/max of that second, which is strictly more informative than
  keeping an arbitrary member.
- **stream** — per-tick/per-window telemetry. Byte-budgeted hard, with anomaly preference (a row
  flagged as an outlier is admitted before routine rows of the same family).

### Family assignment is per **full tag prefix**

`DIAG,…` is heterogeneous: `DIAG,stop_phase,` is a stop edge and `DIAG,midi_gap,` is a timing stream,
and both are Tier-A today. A family map keyed on the top-level token would merge them and apply the
wrong behaviour to the stop edges. The map is therefore an explicit, ordered list of full tag prefixes
with a fallback family for unrecognised Tier-A tags.

## Budget arithmetic

Ring 1 MiB, and the guarantee must hold *including* the critical reserve:

| target | no reserve | with 64 KiB critical reserve |
|---|---|---|
| 15 min (900 s) | 1,165 B/s | **1,092 B/s** |
| 20 min (1,200 s) | 873 B/s | **819 B/s** |

So a 20-minute guarantee needs **core + profile <= ~820 B/s**; with a 200–250 B/s core the profile gets
**570–620 B/s**. At 15 minutes the profile gets ~840 B/s. The reserve costs ~4% of the budget and is
therefore part of the arithmetic, not added afterwards.

Two consequences to keep honest:

- **The reserve is not borrowable.** The ring is currently a single FIFO; a physical reserve partitions
  it, and idle critical space cannot backstop Tier-A without giving up the guarantee. Partitioning is a
  real change to the head/tail eviction arithmetic.
- **Critical protection today is a rule, not space.** It works because `incomingMayDisplace` refuses to
  let non-critical displace critical. Under a pathological burst that is more fragile than a reserve.

## Hot-path and memory constraints

- **No allocation, no blocking** on the producer path. Budget accounting is O(1) integer work per row;
  the token bucket is a timestamp plus a byte counter.
- **Formatting cost is real.** Rows are built with `vsnprintf` into a 192-byte buffer before insertion,
  so a future binary encoding (Option I) also removes work from the timing path — one more reason I
  follows B.
- **RAM1 is tight**: ~35 KB free locals, but only ~870 B of padding before `code + padding` crosses into
  another 32 KB FlexRAM bank. The budget table is PROGMEM/FLASH-resident; per-family runtime state is a
  small array (~30 families x ~16 B). Every task must re-read the RAM1 line, and the instrumentation
  stays session-capture-gated.
- **Pressure has a second effect.** The ring exposes a ¾-capacity pressure signal that is consulted to
  *skip* optional instrumentation. Keeping the ring deliberately below pressure therefore also makes the
  rest of the capture more reliable — a budget is not only a duration guarantee.

## Interaction with RC-MEM-7

RC-MEM-7's `RING,tier_a_evicted,<count>,first=<us>` and its advisory stay exactly as they are. They
become the **regression signal**: with budgets working, a 20-minute run should report **zero** Tier-A
evictions, and any non-zero count means a family's budget or cadence is wrong. `RING,overflow` remains
critical-only and remains fatal.

## Verdict semantics (host)

The verifier already separates fatal `issues` from non-fatal `advisory_issues`. The rule this change
adds:

> **Never emit an unconditional pass when a family the assertion requires was thinned below that
> assertion's evidence contract.**

Each assertion declares required families and a minimum coverage (e.g. `complete`, or `>= 1 Hz`), the
report exposes per-family coverage, and a shortfall yields `DEGRADED` for that assertion — a hard state,
not a footnote, so a thinned run cannot be mistaken for a clean one.

## Deferred: Option I's blocking gap

Option I looks additive because the ring already supports typed binary records
(`CaptureRecordType` + `CaptureRecordHeader{type, payloadLen}` + `recordTotalBytes`). It is not: the tier
test is `headRecordIsTierAText()`, so binary records are treated as ordinary diagnostics and shed first.
Compacting the high-volume families into binary would therefore make them the eviction fodder unless the
tier notion is extended to record types. That extension is a prerequisite task for I, not a detail.

## Verification plan

- **B1 (native):** family classification (including the `DIAG,*` split), core/profile selection, byte
  counters, per-family drop/coverage reporting, and the contract that an over-budget family can never
  drop critical or edge evidence. Budget arithmetic unit-tested against the 15/20-minute targets.
- **B2 (native + device):** token buckets with bounded bursts, edge/sample/stream policies, coalescing,
  critical reserve; a **mathematical proof obligation** that `sum(burst) <= capacity * safety`, and a
  device soak that holds 20 minutes with `tier_a_evicted = 0`.
- **Host:** per-family coverage parsed into assertion-level verdicts; a fixture where a required family
  is thinned must produce `DEGRADED`, never `PASS`.
- **HITL:** one run per profile on a shape that exercises that profile's bug class, plus a regression
  replay of a past bug per profile where one exists.
