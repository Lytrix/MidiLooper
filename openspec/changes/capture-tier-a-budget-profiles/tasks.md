# Tasks — capture-tier-a-budget-profiles

Scope: `docs/Runtime/CURRENT_WORK.md` § Now implementing. Before firmware edits: run
[`docs/Templates/DECISION_REVIEW.md`](../../../docs/Templates/DECISION_REVIEW.md), the preflight, and
search [`docs/DECISION_LOG.md`](../../../docs/DECISION_LOG.md) (RC-MEM-7 is the direct predecessor).
Each stage is a reviewable PR; a stage is only complete when its gates below pass.

Guiding constraint for every task: **no allocation and no blocking on the MIDI/timing path**, and the
budget table is FLASH/PROGMEM-resident. Re-read the RAM1 line (`locals free`, `padding`) after each
stage: ~870 B of padding is all that separates `code + padding` from another 32 KB FlexRAM bank.

## B1 — classification, core/profile selection, counters, degraded verdict

- [ ] B1.1 **Family classifier** in `include/Utils/CaptureLineTier.h` (header-only, host-testable):
      an ordered list of full tag prefixes to family, a fallback family for unrecognised Tier-A tags,
      and a behaviour class per family (`edge` / `sample` / `stream`). Must split `DIAG,*` correctly —
      `DIAG,stop_phase,` is stop-edge, `DIAG,midi_gap,` is MIDI-timing — and every currently-Tier-A tag
      must map to exactly one family, with a unit test enumerating the existing Tier-A list so a newly
      added tag cannot silently land in the fallback.
- [ ] B1.2 **Family table as data**: profile -> family -> `{bytes_per_second, burst_bytes, behaviour}`,
      PROGMEM-resident, with the six profiles (`memory`, `persistence`, `capture-stop`, `content-parity`,
      `display`, `midi-timing`) plus `all`. Numbers start as the working figures in `design.md` and are
      revised by B2's measurements.
- [ ] B1.3 **Core set** with its own budget (~200–250 B/s), event-driven: transport/state transitions,
      record/overdub start/stop/admission/denial, resource-denial events, critical stop/commit markers,
      profile header, coverage summary, ring-degradation summary.
- [ ] B1.4 **Profile selection**: serial command (spelling per proposal TBD 4), compiled default, and the
      profile + per-family budgets written into the session header (`HDR` row) so a trace is
      self-describing. Automatic pressure handling may only *add* emergency evidence, never substitute a
      profile (asserted).
- [ ] B1.5 **Per-family byte counters and coverage**: `complete` / `sampled` / `budget_limited` /
      `excluded_by_profile`, plus drop counts, emitted in the `RING,` summary; enforced at admission so
      budget accounting stays O(1) and allocation-free.
- [ ] B1.6 **Budget arithmetic unit tests**: for each profile, `core + sum(families)` multiplied by the
      configured duration must fit the Tier-A capacity after the critical reserve; a profile that cannot
      must be *rejected*, not silently thinned (15 min and 20 min targets as fixtures).
- [ ] B1.7 **Host verdict**: parse the coverage summary in `scripts/hitl/verify/midi_boundary_timing.py`
      and expose per-family coverage; teach `scripts/hitl/legacy_record_baseline.py` assertions to declare
      required families, and make a shortfall resolve to `DEGRADED` — never an unconditional pass. Fixture:
      a thinned required family must not produce `PASS`.
- [ ] B1.8 Keep RC-MEM-7's `RING,tier_a_evicted` advisory intact and re-document it as the regression
      signal (zero expected for a 20-minute run). `RING,overflow` stays critical-only and fatal.
- [ ] B1 gates: `pio test -e native` (full), `test_capture_line_tier` extended, harness
      `test_capture_transitions.py` + `test_baseline_canonical_args.py`, `teensy41-capture-serial` RAM1
      line recorded in the task.

## B2 — token buckets, behaviour policies, critical reserve, the guarantee

- [ ] B2.1 **Token buckets**: bytes/second plus a bounded burst per family; burst capped so
      `sum(burst) <= Tier-A capacity * safety_factor`, asserted in a native test rather than by
      inspection.
- [ ] B2.2 **Edge policy**: never cadence-sampled; a hard pathological-rate guard that *reports* when it
      engages (it must not silently drop edge evidence).
- [ ] B2.3 **Sample policy with coalescing**: one row per interval carrying latest/min/max plus the
      observation count, replacing drop-the-rest sampling.
- [ ] B2.4 **Stream policy with anomaly preference**: anomalies admitted ahead of routine rows in the
      same family when the interval budget is exhausted.
- [ ] B2.5 **Critical physical reserve** (initial 64 KiB): partition the ring so budgeted Tier-A traffic
      cannot consume it, and update the head/tail eviction arithmetic accordingly. The reserve is a
      partition of the existing 1 MiB, not an addition. Re-derive the budget table at the final size
      (64 KiB costs ~4%: a 20-minute target needs core + profile <= ~820 B/s).
- [ ] B2.6 **Mathematical guarantee**: document and unit-test the bound that a profile's worst case
      over the configured duration fits the ring, including bursts and the reserve.
- [ ] B2.7 **Device soak**: 20 minutes of active transport on a real shape with a real profile, requiring
      `tier_a_evicted = 0`, a coverage summary naming every family's state, and no `FATAL`, no
      `#MEM_EXT_REFUSED` and a live device throughout. Record the RAM1 line and the PSRAM `live` curve.
- [ ] B2.8 **Per-profile HITL**: one run per profile on a shape that exercises that profile's bug class
      (memory/publication, persistence drain, capture stop/commit, content parity, display/draw-source,
      MIDI timing), with the resulting coverage summary and verdict recorded in
      [`docs/Guides/HITL_CONTENT_VERIFICATION.md`](../../../docs/Guides/HITL_CONTENT_VERIFICATION.md).
- [ ] B2 gates: B1 gates plus the soak, and a before/after of the ring's Tier-A eviction count on the
      24-bar two-overdub shape (previously `capture_tier_a_evicted:92655`).

## I — compact binary ring records (deferred until B is measured)

- [ ] I.1 **Prerequisite, not a detail**: extend the tier notion to record types. Today the tier test is
      `headRecordIsTierAText()`, so binary records are shed first — compacting high-volume families into
      binary without this would make them the eviction fodder. Add the tier classification for
      `CaptureRecordType` and a native test that a binary Tier-A record is protected like a text one.
- [ ] I.2 Predefined fixed-cost binary schemas per high-volume family (timestamp delta, family id, event
      id, small payload), serialized with no allocation, decoded host-side. Also removes the `vsnprintf`
      formatting cost from the producer path.
- [ ] I.3 Measure the actual ratio on a device run and re-derive whether it buys duration headroom or
      budget headroom; only then decide the per-family budgets again.

## H — deferred SD sink (only on a concrete requirement)

- [ ] H.1 Trigger check, recorded in this file: implement only when reset-survivability, unattended
      multi-hour soak testing, or a bug that never reaches `STOPPED` becomes a real requirement. SD
      extends the sink; it does not bound the stream, which is what B does.
- [ ] H.2 Design pass through the deferred/sliced storage lane: a new work-item type beside the loop
      persist path, written in bounded idle slices, never on a MIDI edge; the ring stays as staging in
      front of it.
- [ ] H.3 File policy and consent: name, size cap, rotation and delete policy for a diagnostic file on a
      card that holds the user's workspace — explicit user approval required, per
      `.cursor/rules/Destructive-SD-Operations.mdc`.

## Closeout (all stages)

- [ ] Update `docs/Runtime/PROJECT_STATE.md` and `docs/Runtime/CURRENT_WORK.md`.
- [ ] Update `docs/DELIVERABLE_TRACKING.md`; append a `DEC-###` entry (new ownership? budget/verdict
      policy is architecture-adjacent and needs the decision record).
- [ ] Update `docs/Guides/HITL_CONTENT_VERIFICATION.md` with profile selection, coverage semantics and the
      degraded-verdict rule.
- [ ] Archive only after the B1 + B2 gates pass and the soak is recorded; I and H may remain open.
