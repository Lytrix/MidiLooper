## Why

`SESSION_CAPTURE` diag rows (`#CAP,<micros>,<tag>,…`) are buffered in a **1 MiB PSRAM ring** that is
drained to USB serial **only when transport is idle**, by deliberate policy:

> `PLAYING`/`OVERDUBBING` never flushes USB serial: capture must retain the full active-transport
> evidence burst until `STOPPED`. […] reserve this fixed PSRAM budget rather than weakening Tier-A
> evidence. Raised 512 KiB -> 1 MiB (2026-09-17) after two captures aborted on `RING,overflow`.

The ring is therefore a **bounded per-transport-session evidence buffer, not a log**. Tier-A has since
outgrown it. Measured in a 24-bar record + two-overdub run (`captures/rcemem7-tier-degrade`):
**92,655 Tier-A records were evicted over ~162 s of active transport**, i.e. Tier-A is produced at
**~30–35 KB/s** and the 1 MiB ring covers **~30 seconds**. The requirement is **15–20 minutes**.

`immutable-loop-snapshot-passes` RC-MEM-7 already made that overflow *non-fatal*: Tier-A displacement
is reported as a degradation (`#CAP,<us>,RING,tier_a_evicted,<count>,first=<us>`) and the host raises an
advisory, while only critical loss stays fatal. That fixed *survivability*. It did not fix
*completeness*: a long run is now honest about being incomplete, but the evidence needed to explain it
is still gone.

This change bounds the evidence stream so a 15–20 minute session fits the existing ring **intact**,
and makes the host refuse to call such a run clean when a family it needed was thinned.

Brownfield context:
[`docs/Guides/HITL_CONTENT_VERIFICATION.md`](../../../docs/Guides/HITL_CONTENT_VERIFICATION.md),
[`docs/Runtime/CURRENT_WORK.md`](../../../docs/Runtime/CURRENT_WORK.md),
[`docs/DELIVERABLE_TRACKING.md`](../../../docs/DELIVERABLE_TRACKING.md),
`openspec/specs/hitl-automation/spec.md` (verdict composition),
`openspec/specs/long-record-memory-headroom/spec.md` (PSRAM-first budget discipline).

## What Changes

- **Tier-A families.** Every Tier-A row is classified into a **family** by its **full tag prefix** (not
  its top-level token: `DIAG,stop_phase,` is a stop edge and `DIAG,midi_gap,` is a timing stream, and
  both are Tier-A today).
- **Behaviour classes.** Each family is `edge`, `sample`, or `stream`:
  - `edge` — state transitions, admission/denial, stop/commit edges. **Never cadence-sampled**; every
    occurrence is recorded subject to a hard pathological-rate guard.
  - `sample` — memory, display, coverage state. **Coalesced** at its configured cadence, keeping
    latest/min/max of the interval rather than the first row.
  - `stream` — per-tick/per-window telemetry. Byte-budgeted aggressively, with anomaly preference.
- **Per-family byte budgets, not row cadence.** Each family gets `bytes_per_second`, `burst_bytes` and
  a semantic rule. Bytes are the currency because rows range ~40–190 B, so a row cap cannot be a
  duration guarantee. Bursts make a family able to spend accumulated budget around a transition; they
  are **bounded**, so the duration guarantee stays mathematical.
- **Always-on core (~200–250 B/s).** Transport/state transitions; record/overdub start, stop, admission
  and denial; resource-denial events; critical stop/commit markers; the capture profile header;
  per-family coverage/drop summary; ring degradation summary. Event-driven, not sampled.
- **Profiles selected at runtime.** `memory`, `persistence`, `capture-stop`, `content-parity`,
  `display`, `midi-timing`, plus `all` (legacy). Compiled default, runtime selection over the existing
  serial command surface, and **recorded in the session header** so every trace is self-describing.
- **A physical critical reserve (~64 KiB)** inside the ring, never sampled, so a pathological Tier-A
  burst cannot starve critical evidence by exhausting the ring before eviction logic runs.
- **Per-family coverage and drop counters** reported in the `RING,` summary and consumed by the host.
- **Host verdict with a hard degraded state.** Every assertion declares the families it requires; a
  required family thinned below that contract yields `DEGRADED`, never an unconditional `PASS`.

**Non-goals:**

- **The SD sink (Option H).** Deferred: it extends the *sink*, not the *stream*. Its real justification
  is reset-survivability, unattended soaks, and bugs that never reach `STOPPED` — not capacity. B first.
- **Compact binary ring records (Option I).** Deferred until B is measured, and blocked on one gap: the
  tier test is `headRecordIsTierAText()`, so binary records have no Tier-A protection today and would
  become the eviction fodder.
- **Enlarging the ring.** 4 MiB buys ~2 minutes at the current rate and spends PSRAM headroom that
  `playback-runtime-prewarm` / the lazy chunk pool just recovered.
- **Changing the drain policy.** No flushing during `PLAYING`/`OVERDUBBING`.
- **Reclassifying any line out of Tier-A** to save bytes. This change selects *which* families are
  collected; it does not demote evidence.
- **Moving `[Memory] ext` into the ring.** It is deliberately direct-to-serial so it survives a device
  death; a ring copy would be separate and compact if wanted.

## Capabilities

### New Capabilities

- `capture-tier-a-budget-profiles`: profile-selected, per-family byte-budgeted capture with bounded
  bursts and semantic edge/sample/stream behaviour, a guaranteed critical reserve, and a host verdict
  that cannot report an unconditional pass over degraded evidence.

### Related Capabilities (referenced, not redefined)

- `hitl-automation`: verdict composition and advisory-vs-issue separation are the host side this
  capability feeds.
- `long-record-memory-headroom`: PSRAM-first discipline for length-scaling buffers; the ring is one.
- `playback-runtime-prewarm`: owns the PSRAM accounting this change must not disturb.

## Impact

| Area | Primary files |
|------|---------------|
| Tier/family classification | `include/Utils/CaptureLineTier.h` (header-only, host-testable) |
| Ring budgets, reserve, coverage | `src/Utils/DebugSessionCapture.cpp`, `include/Utils/DebugSessionCapture.h` |
| Profile command + session header | `src/Utils/DebugSessionCapture.cpp`, `src/main.cpp` (command surface) |
| Host verdict | `scripts/hitl/verify/midi_boundary_timing.py`, `scripts/hitl/legacy_record_baseline.py` |
| Tests | `test/test_capture_line_tier` (classification/budget math), new ring-budget fixtures |
| Docs | `docs/Guides/HITL_CONTENT_VERIFICATION.md` (profile + coverage semantics) |

**Apply order.** Independent of the display work in `immutable-loop-snapshot-passes` 5.12. Builds
directly on RC-MEM-7 (degradation + advisory, landed). Sits before Option I and Option H.

**Resource note.** The budget table is FLASH/PROGMEM-resident; per-family RAM state is small (~30
families x ~16 B). RAM1 has ~35 KB free locals but only ~870 B of padding before `code + padding`
crosses into another 32 KB FlexRAM bank, so the RAM1 line must be re-read on every task.

## Open decisions (TBD)

1. **Exact per-profile family lists and byte numbers.** The families and profiles below come from the
   bug classes actually hit in one session; the numbers need one measured run each.
2. **Critical reserve size.** 64 KiB is the working figure; it should come from observed worst-case
   critical volume, and it costs ~4% of the budget (see `design.md`).
3. **Whether core carries a compact in-ring memory row** in addition to the direct-serial
   `[Memory] ext`, or relies on the direct row alone.
4. **Profile command spelling** (`!CAP_PROFILE <name>`) and whether `all` stays the compiled default
   until a device soak proves the budgets.
5. **Whether a degraded run should also fail closed** (refuse to emit any verdict above `DEGRADED`)
   for content-parity assertions specifically, or only mark them.
