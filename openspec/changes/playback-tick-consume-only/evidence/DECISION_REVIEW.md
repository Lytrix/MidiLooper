# Historical decision review — playback-tick-consume-only

From [`docs/Templates/DECISION_REVIEW.md`](../../../../docs/Templates/DECISION_REVIEW.md), 2026-11-01. Task 0.2.

## Searched

- [x] `docs/DECISION_LOG.md`
- [x] `openspec/specs/windowed-playback-generation/spec.md` (main spec)
- [x] Active changes `runtime-profiling-layer` (DEC-239/241 evidence)

**Terms:** windowed lane, route selection, merged fallback / merged catch-all, paged delivery, bridge, readiness,
rolling, DEC-116, DEC-193, DEC-197, DEC-241.

## Relevant findings

| Source | What it says | Bearing on this change |
|---|---|---|
| Main spec, "Active playback is served from an immutable window" | One rolling request/build/adopt/emit path owned by `Track::stepPlaybackViewMaintenance`; merged/bridge/paged branches are *named temporary migration fallbacks with a removal trigger* | Phases 1–3 measure conformance; phase 6 is the removal-trigger evidence |
| Main spec, "One off-clock owner acquires, replaces and publishes windows" | The transport tick and clock path only read readiness; no allocation, build or block | The tick contract this change makes measurable and, in phase 5, universal |
| **DEC-116** — one rolling playback path per sounding slot | Lists the merged fallback among the separate content decisions to be retired | Same direction; no conflict |
| **DEC-193** — playback investigated as a decision-boundary regression before any window-first architecture (2026-10-29) | **Parked** the window-first material (`docs/Plans/future_root_independent_playback.md`) as unauthorized-not-withdrawn; reopening needs explicit owner approval | **Phases 1–3 are report-only measurement and a native fixture — compatible.** Phase 4 (repair) may change routing/admission toward the windowed lane: its design note (task 4.1) must state whether it reopens parked DEC-193 material and, if so, carry the owner's explicit approval |
| **DEC-197** — the merged fallback serves a short loop as one whole-loop window, not a frozen two-bar slice | Long loops keep the 2-bar moving window | Explains the seam-driven builds measured on the 20-bar chain |
| **DEC-241** — the internal-clock ISR tick is consume-only | Owed + containment, `PlaybackTickContext::gState` (RAM2) | Reused: phase 1's counters live beside it; the miss count is its `misses` |

## Existing reusable pattern

- Counters: `PlaybackTickContext::gState` (RAM2, cleared by `initAtBoot()`).
- Row emission: `RuntimeTimingTelemetry::emitWindow` (5 s family) → an aggregated sink with a flash-resident format, as
  `DebugSessionCapture::envelopeInstrRow` does; format literal in the carved `StopDiagRows.cpp`.
- Tier-A: a `PROGMEM` named prefix in `isTierATextLine` (parsed by `check_marker_contract.py`; accessor-function prefixes
  are not).
- Route naming: the existing route codes in `reportFirstBarRouteForSlot` (1 bridge, 2 view, 3 paged, 4 merged).

## Reuse decision

**YES** — no new owner. Telemetry extends DEC-241's state and the 5 s family; no new architecture before phase 3.

**Migration required:** NO.
