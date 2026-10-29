## Why

Playback that worked on the image immediately before the DEC-191 retained-Start repair stopped working on
the first image after it. Three later images — including one with every DEC-191 production call site
reverted — kept the same symptom, so the regression is pinned to a **small image boundary**, not to a
general invalidity of the existing playback path.

The active packet [`display-freeze-under-overdub-chain`](../display-freeze-under-overdub-chain/proposal.md)
has since accumulated a replacement playback architecture (window-first, root-independent, any-slot,
continuous coverage) that is far larger than this regression. That material is parked in
[`docs/Plans/future_root_independent_playback.md`](../../../docs/Plans/future_root_independent_playback.md)
and is **not** authorized by this change.

## What Changes

- Pin the last-good and first-bad images, sessions and the measured playback difference between them.
- Resolve the one remaining fork before any code change. **Resolved 2026-10-29:** the card-state explanation
  does not hold (the slot restores rootless on both sides and the pre-regression image still plays from passes),
  so the code path is the live hypothesis.
- If it is code, bisect only the DEC-191 behavioral deltas (cheap preflight, retry park/wake, session-exit
  orphan settlement, idle-lane orphan settlement, deferred-owner opportunity order) to the smallest
  mutation that switches playback off.
- Repair only the named missing producer/opportunity inside the existing owners.
- Prove two things and nothing more: **playback behaves like the last-good image**, and **an unadmittable
  retained Start does not re-enter the ~109 ms preparation body every main-loop turn**.

## Capabilities

### New Capabilities

- `playback-servicing-regression`: the last-good playback servicing is restored for the pinned slot shape,
  the retained-Start refusal stays cheap, and the repair does not grow into a new playback architecture.

### Modified Capabilities

None. `windowed-playback-generation` and the historical `overdub-chain-display-progress` requirement about
rootless whole-root recovery are **not** changed here; DEC-192's recovery stays as it is (eventual
recovery only) until this change names the actual missing producer.

## Impact

Read-only investigation first. Expected production files if a code repair is proven: the existing Track /
Loop owner that owned the lost opportunity in the last-good image (`src/Track/TrackDeferredMaintenance.cpp`,
`src/Track/TrackSlotPool.cpp`, `src/Track/TrackOverdubLifecycle.cpp`, `src/Loop/LoopSnapshotPublish.cpp`),
plus the native harness that already links them (`test/test_runtime_working_set_ownership`). No new Manager,
queue, lease, persistence format, source-lifetime contract, or SD layout.

Device work is bounded to authorized flash/A-B on the pinned images and one non-destructive, registered
playback stimulus on the owner's saved set (`scripts/hitl_authoring_traps` recipe first). No re-record,
slot removal, restore, or SD mutation without explicit permission in the same session.

See [design](design.md), [boundary evidence](evidence/BOUNDARY-2026-10-29.md) and
[DEC-193](../../../docs/DECISION_LOG.md#dec-193--playback-is-investigated-as-a-decision-boundary-regression-before-any-window-first-architecture).

## Evidence and limits

- VERIFIED (owner-visible A/B): the last-good image (`852b15f7…`) emits playback rows; the first-bad image
  (`0dbf08c1…`) records `playback_read` = 0 with note-on counts at 0 in 68 of 72 sampled rows.
- VERIFIED: the symptom survived removing the idle-lane orphan settlement (`3e79bd1b…`) and, per the
  recorded owner report, the full DEC-191 call-site revert (`c06f2e43…`).
- VERIFIED: the card-state fork is measured and rejected for the rootless property. The pre-regression boot
  restores the slot rootless (`root_rev,0`) and still plays from the pass resolution; the first-bad boot never
  builds a root. The persisted shape is not the differentiator.
- VERIFIED: the 00:18 backup is **not** a body reference for the 31-bar slot (78-byte stub, no 23808-tick
  body). It cannot support a byte-level pre/post body diff.
- VERIFIED: the live device has 16 committed slots and 8 required for initial playback; 7 carry a root only
  because DEC-192 recovery rebuilt them after boot.
- UNKNOWN: which DEC-191 delta is load-bearing. The next experiment is a two-state reconstruction
  (`reconstruction-control` vs `reconstruction-dec191`), not historical images.
- No new timing, memory, or physical-output guarantee is claimed by the repair itself.

## Non-goals

No window-first / root-independent playback implementation, no any-slot standby pool, no continuous
coverage redesign, no lifetime or working-set contract change, no restore/malformed-slot repair, no
re-record, no SD deletion or rename, no bulk boot hydration, no MIDI-tick gather, no automatic upload.
A failure of the narrow repair is a **stop-and-ask** outcome, not permission to proceed with the parked
architecture.
