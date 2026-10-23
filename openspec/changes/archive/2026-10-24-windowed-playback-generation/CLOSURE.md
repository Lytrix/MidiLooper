# Scope closure — windowed-playback-generation (2026-10-24)

## What this change closes on

`windowed-playback-generation` establishes and qualifies the unified rolling reader for the declared supported-route
scope under `PLAYBACK_WINDOW_ROLLING_ENABLED=1`. Shipping-default activation and removal of compatibility emitters are
intentionally deferred. Legacy emitters remain available behind the disabled shipping-default feature gate as
differential/fallback infrastructure and are **not claimed removed** by this change.

The qualification/supported-route scope is:

| Route | Evidence | Claim |
|---|---|---|
| Base record, both overdub passes, post-stop undo/redo, display, persistence | base matrix 4/4 (qualification + default, host 120 + internal) | device PASS |
| Live + frozen Record / stop return | rolling Record campaign (growth-empty, held, matched; aggregate PASS, all four terminals) | device PASS |
| NOTE_EDIT (short loop, route + audibility) | runs 24/25 on `cbd4703d…` | device PASS |
| Overlay edge | `overlay_edge ok=ff` on device; native W1/W8 | device selection observed |
| Dense / windowed | qualification `ok=True` (leg/lane/snapshot; readiness 29/29; 0 overruns); default content leg PASS | device PASS, two content-pipeline terms deferred |
| Internal / external clock | base legs | device PASS |
| RAM1 / PSRAM / RAM2 | pins exact; RAM2 147,456 B; PSRAM 3.97–4.02 MB | measured |
| Switch / inbound slot | mixed leg drove; switch gesture confirmed | gesture observed; mixed oracle accepted as setup-limited |
| 3.15a oracle | 8 routes at the production edge + stop return and overdub/wrap discharged natively; first Record bar, global undo/redo, clock/seek accepted on the device campaign | owner scope decision |
| 3.15b one owner | five source kinds, live first-Record `current`, one-bar later-occurrence handoff, release/expiry | mechanism; affected-tick bound and service budget transferred to 6.3/6.5 |
| 3.15c edge reader | removal-of-reader REDs, owner-turn/adoption, NOTE_EDIT cutover | mechanism; short-loop Record/overdub deferred |
| 3.15d NOTE_EDIT | short/long route delegated to 3.15f | closed |

## Explicitly not claimed

- **Shipping-default activation** (enabling `PLAYBACK_WINDOW_ROLLING_ENABLED` on `teensy41-capture-serial`) and the
  resulting device requalification of every route on that image.
- **Removal of the compatibility emitters** (fixed/bridge, first-Record/Record-return, prepared pair, paged emission
  selection, merged fallback + overdub echo). 3.7b is a real architectural destination, deferred — **not waived**.
- Deferred routes as future features: simultaneous-track Record, layered/jam, short-loop Record/overdub.
- `ExecutionBounds.md` row 2 reclassification (belongs with the removal change).
- The dense content-pipeline terms and the mixed oracle's A-retirement terms (recorded setup limits).

## 3.7b — deferred, not waived

> **Deferred — product activation/removal change.** Removing the compatibility emitters requires enabling
> `PLAYBACK_WINDOW_ROLLING_ENABLED` in the shipping image. This is an activation boundary rather than residual
> implementation cleanup and will be performed under a separate change with full device requalification.

The separate change is **`shipping-rolling-activation`** (thesis: *Activate unified rolling playback in the shipping
image and retire compatibility emission routes*). It begins with rolling enabled **without removing anything**, proves
parity on the shipping configuration, and only then removes the five families one at a time, each with its own RED/GREEN
and device gate and a rollback story (the flag restores the legacy path until the family is removed).

## Transferred residuals (not success claims)

| Task | Residual | Carried by |
|---|---|---|
| 2.2 | derived-deadline fixture (short + long loop; oracle is the derived deadline) | `shipping-rolling-activation` verification track |
| 2.4 | commit-edge mutation (allocation/inline build) in the S4b.7 shape | same |
| 2.8 | red-proof accounting for 2.2/2.9 | same |
| 2.9 | retained-source hold/release matrix; `retainPlaybackSourceForSlot` still has no production caller | same |
| 4.1 | content-level undo parity + parity-consumer decision | same |
| 4.2 | content-divergence oracle + repair choice (provenance vs bounded load reconciliation) | same |
| 4.5 | device closure-ordering/last-resort leg; `serviceAdmittedDeadlineMisses` boundary; displayed provenance is RWO S4e | activation device parity |
| 5.5 | 6.3 sweeps of ring/reserve, device leg, two named criterion residuals | activation device parity |
| 6.3 | device sweep leg | activation device parity |
| 6.5 | sustained multi-track sounding contention; DIN/external clock | activation device parity |

The current change does **not** claim these as passed. The activation change's verification track owns them, and its
activation step is gated on their closure where they are prerequisites.

## Authority

This closure is an owner scope decision recorded on 2026-10-24. It relies on the change's own spec clause that legacy
paths MAY remain temporarily behind differential verification until every supported route passes its own
cutover/removal gate; this change closes the supported-route gate under `ROLLING=1` and hands the shipping activation
and removal gate to the separate change.
