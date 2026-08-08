# HITL serial reconciliation enhancement

## Goal

Make record/overdub verification deterministic by always reconciling host assertions with serial-capture results and failing on any mismatch.

## Implemented checks

- Verification is required for every run (`assertions.verification_required = true`).
- Verification source is reported (`serial_port`, `verify_serial_log`, or `none`).
- Serial verification now fails on:
  - record/overdub note-on vs note-off imbalance,
  - record/overdub sequence mismatches,
  - existing boundary/first-note/out-of-range/open-note issues.
- Reconciliation now fails on:
  - host-vs-serial record note-count mismatch,
  - host-vs-serial overdub note-count mismatch,
  - note-off imbalance,
  - sequence mismatch,
  - per-track record/overdub clock mismatch in bar-synced mode (exact `bars * 96` clocks).

## Report fields

- `assertions.assertion_serial_reconciliation.checks`
- `assertions.assertion_serial_reconciliation.track_checks`
- `assertions.assertion_serial_reconciliation.issues`
- `assertions.verification_required`
- `assertions.verification_source`

## Runtime behavior

- `overall_ok` is false whenever reconciliation issues are present.
- Exit code fails when reconciliation fails.
- Console summary prints explicit `RECONCILE ... [ok|fail]` lines per check plus a reconciliation issue list.
