# 2026-10-27 — Canonical-base denial reporter (DEC-170) + retained-cache falsification

Owner-approved investigation step ("add device blocker reporter"). This is a **report-only** device
instrument that names the first false term in `stepCanonicalPrefix`, so one device capture settles
whether the ~12 s base stall is a shared-machinery contention, a builder denial, or an identity-restart
loop — before any behavior fix.

## Investigation result (why this reporter exists)

The owner asked to falsify the retained-cache hypothesis before adopting cause-based abandon. Two findings:

1. **Retained cache is falsified.** A native fixture reproducing the device's exact high-level state
   (destination-Playing stop, base build under a live *same-slot* successor) dumps `stepCanonicalPrefix`'s
   gates: `captureAttached=0`, all identities match (passes pointer, revision, generation, length, storage),
   `prefixPhase=Preparing` with the build making progress. There is nothing retained to release, and the base
   builds to completion natively — so "successor capturing" alone does **not** make the base impossible.
2. **The native fixture does not reproduce the stall**, so the device-only cause is still unnamed. The header
   note is the smoking gun: *"`stepCanonicalPrefix` latches `CanonicalPrefixPhase::Denied` and nothing else ever
   clears it"* — the device stall is a sticky `Denied` latch, and the 2,048-grain count is dead time once it latches.

## The reporter (DEC-170, report-only)

- **`LoopPlaybackRuntime::CanonicalPrefixDenial`** — `None, BridgeActive, CaptureAttached, RevisionMismatch,
  GenerationMismatch, InputGenerationMismatch, StepDenied, InputsBusy, ApplyUnavailable`.
- **`stepCanonicalPrefix`** records the denial reason at each non-advancing return; `prefixFirstDenialReason`
  remembers the **original** transition until the build restarts/completes/makes progress, so the row names what
  *first* blocked the base, not the sticky `Denied` it left behind. An identity supersession emits the existing
  `prefix_restart` stage (a restart, not a denial) and clears the record.
- **Device row** (once per denial episode, when the base returns non-`Complete`):
  `#CAP,<us>,DIAG,prefix_denied_first,reason=<name>,interval=<n>,phase=<p>,bridge=<0/1>,attached=<0/1>,genmatch=<0/1>,stormatch=<0/1>,apply=<0/1>,succ=<0/1>`.
  (`succ` is the successor-capturing state; the name strings are FLASH-resident.)

## Bitfield packing (owner's "replace bools with bit values")

The two new report fields plus the phase are packed into one byte, so neither `sizeof(Track)` (384) nor
`sizeof(LoopPlaybackRuntime)` (7,768, I5) grows:

```
CanonicalPrefixPhase canonicalPrefixPhase : 2;         // Idle..Denied
CanonicalPrefixDenial prefixFirstDenialReason : 4;     // None..ApplyUnavailable
bool prefixDeniedFirstReported : 1;
```

A dead `prefixDenialReason` field was eliminated, and a Track member for the reporter was moved into the packed
bitfield (the first attempt grew `trackManager` by 8×4 = +32 B, which the post-link RAM1 guard caught — the
`sizeof(Track)` static_assert is gated in an uncalled `placementPins()` and did not fire, worth noting).

## Verification

- Control fixture `test_destination_playing_base_builds_natively_under_live_successor` (`test_shared_capture_finalization`):
  the base builds past `CanonicalPrefix` natively under a live same-slot successor, and the reporter stays silent
  (`prefixFirstDenialReason == None`, `prefixDeniedFirstReported == false`).
- `test_shared_capture_finalization` 114/114, full native **3,526 / 7 skipped / 3,519 succeeded / 0 failed**.
- `teensy41-capture-serial` **SUCCESS** at RAM1 `variables 96,192` / ITCM `code 390,472 + padding 2,744`
  (both unchanged; diagnostic strings are FLASH, +600 B flash) — no re-base.

## Next (needs one device capture)

The row `prefix_denied_first,reason=<x>` decides the repair:
- `bridge_active` / `capture_attached` / `apply_unavailable` / `inputs_busy` → shared-machinery ownership bug; fix it,
  keep the base (no cause-based abandon).
- `step_denied` (with all identities/resources valid) → the builder genuinely cannot build under a live successor;
  adopt immediate cause-based abandon and delete the 2,048-grain wait.
- `revision_mismatch` / `generation_mismatch` / `input_generation_mismatch` (or a `prefix_restart` flood) →
  supersession/rebind semantics; cause-based abandon would hide a stale-source bug.
