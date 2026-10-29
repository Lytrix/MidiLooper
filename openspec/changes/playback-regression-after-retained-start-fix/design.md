## Context

Owner request: treat the silent-playback defect as a **regression introduced at the DEC-191 image boundary**,
not as evidence that the playback architecture is invalid. Freeze the display/boot packet, stop expanding it,
and answer one question first:

> What changed between the last image with working playback and the first image with the 7.5 Hz fix that
> caused existing playback-source servicing to stop?

Baseline: dirty `feature/unified-rolling-playback-path` tree. Preserve every pre-existing edit. Installed
image and card state must be pinned by hash and read-only inventory, never inferred from source comments.

Boundary evidence: [BOUNDARY-2026-10-29.md](evidence/BOUNDARY-2026-10-29.md). Parked, unauthorized
architecture: [`docs/Plans/future_root_independent_playback.md`](../../../docs/Plans/future_root_independent_playback.md).

## Goals / Non-Goals

**Goal:** name the first missing producer/opportunity between the last-good and first-bad images, repair only
that boundary, and prove playback returns to last-good behavior while the 7.5 Hz retry storm stays fixed.

**Non-goal:** any window-first, root-independent, any-slot, continuous-coverage or standby-pool work; any new
source lifetime, working-set, persistence or scheduling contract; any restore/re-record/SD repair as a
workaround. If the narrow repair is impossible because last-good playback depended on an unsafe accident,
**stop and ask** before reopening the parked architecture.

## G0 — boundary, owners, and the two forks

VERIFIED (evidence file): `852b15f7…` / `session_20261029_004321.log` has 92 `playback_read` rows and 41/41
painted seconds with note-ons; `0dbf08c1…` / `session_20261029_011304.log` has 0 `playback_read` rows and 68/72
seconds with `out=0`. The symptom persisted on `3e79bd1b…` (idle-lane settlement removed) and, per the 01:30
owner report, on `c06f2e43…` (all DEC-191 production call sites reverted).

Fork 1 — **code path.** A DEC-191 delta removed a playback/source opportunity. Weakened by the reported
persistence after revert, but not falsified: a rebuild is not guaranteed to equal the pre-DEC-191 tree.

Fork 2 — **card state.** A broken run rewrote the slot body, and every later image inherits it. The 01:55
entry records a deferred save writing a passes-only body with no root (paint 384 of 672 passes), but the
causal claim that orphan release caused it was later **withdrawn**. Treat it as a hypothesis, not a premise.
Complication to respect: DEC-192's recovery has since rebuilt a root on the device, so the live card may
already be repaired and would show "playback works" on **both** images. A pre-regression backup exists
(`captures/sd-backup-20261029_001835/`, 00:18, before the 01:12 flash) and is the only trustworthy
pre-regression body.

Owners (unchanged, do not extend): `main.cpp` deferred dispatch; `Track::processDeferredIdleMaintenance` and
the Snapshot-slot opportunity; `Loop` committed passes, its private publication candidate and the publisher;
the Track retained-interaction carrier and `startOverdubbing`. MIDI consumes the existing merged/window
reader. Storage keeps its existing save/load ownership.

## DEC-191 behavioral deltas — the complete candidate set

Only these changed in the boundary window. Each is a candidate mutation, not a suspect to assume.

| # | Delta | Where | Playback-relevant surface |
|---|---|---|---|
| D1 | Cheap `overdubStartPreflight(uint8_t&)` refusal before the costly body | `TrackOverdubLifecycle.cpp` | Runs on the Start path; if it runs on every turn it consumes the turn |
| D2 | Refusal-keyed re-drive park/wake (claim state, live reserved owner, ring occupancy) | `OverdubStartGate.h`, carrier | Changes how often the Start path executes per main-loop turn |
| D3 | Session-exit orphan settlement (`Reserved → Empty`) from the **Start claim path** | `LoopSnapshotPublish.cpp`, `TrackOverdubLifecycle.cpp` | Can settle a claim a pending stop still needs |
| D4 | Same settlement called unconditionally from the **deferred idle lane** | `TrackDeferredMaintenance.cpp` | Occupies/alters the idle-lane turn that also serves playback-source maintenance |
| D5 | Deferred-owner opportunity/order changes implied by D3/D4 | idle lane | Any additional consumer changes who gets the turn |

D4 was already reverted once (01:35) because it released a claim while a deferred **Record** stop still owed
publication; that is precisely a "no root published → silence" shape. Whether that released claim was the
first-bad cause is **UNPROVEN** and is task 3, not an assumption.

## Oracles fixed before any production edit

**Oracle A — playback restored.** Fixture: seed the same passes-only committed slot shape, Start through the
production admission path, then drive the **ordinary** production owner turns (deferred load/display, Snapshot,
idle maintenance, rolling/request maintenance, MIDI tick) with an **advancing** clock — no frozen-clock rescue,
no direct publication stepping, no injected ready window, and **with the DEC-192 recovery request disabled**
so the fixture cannot be satisfied by the historical repair. Assert: the first due MIDI event appears at its
declared deadline with exact NoteId/tick/pitch/channel/velocity, then a second interval continues. RED must be
observed on the first-bad behavior before the repair; GREEN after.

**Oracle B — 7.5 Hz storm stays fixed.** Fixture: hold an unadmittable retained Start at an unchanged
refusal tuple for N owner turns (production carrier, not the pure decision table). Assert:
`overdubStartBodyEntryCount()` does not advance, the exact request is retained, and playback on other sounding
slots keeps emitting. The existing pure `OverdubStartGate` test is necessary but insufficient and must not be
used to re-enable call sites by itself.

**Oracle C — scope guard.** The change's diff names one boundary. A patch that instead adds a new reader,
window producer, standby pool, or rootless emitter fails review even if A passes.

## Diagnostic ladder (stop at the first branch that answers the question)

1. **Read-only card inventory.** Dump the current slot's passes/root/pass-edit inventory and compare with the
   00:18 backup. Distinguishes "body was damaged and is now repaired" from "body was never damaged". No SD
   write. If the current body matches the pre-regression shape, Fork 2 loses most of its weight.
2. **Image A/B on a controlled body.** Last-good vs first-bad on a body known to be pre-regression (backup
   copy on a quarantined slot, or a fresh scratch slot recorded once for this test). Requires explicit
   permission for any SD write and one upload authorization per image. Outcome: playback differs → code fork;
   playback identical → data fork (and the defect is a save/publication parity defect, not DEC-191).
3. **Minimal mutation matrix (code fork only).** Re-enable exactly one delta at a time on the pre-regression
   tree: D4 alone, D3 alone, D1+D2 alone, then combinations. The smallest mutation that switches playback off
   names the repair boundary. Each run uses Oracle A and records `playback_read`/`out=` counts.
4. **Name the lost producer.** For the winning mutation, trace which producer no longer runs (root
   publication request, source admission, or the reader's qualification) and its refusing predicate.
5. **Repair only that boundary**, reusing the existing owner. No new state.
6. **Oracle B** on the repaired tree; then Oracle A; then focused native, full native, build, scoped review.
7. **Device A/B**: same slot, same gesture, both oracles, physical USB-to-Mac output.

## Execution packet — first implementation slice (task 2 only)

- **Status:** specified, NOT RUN. **Owners:** existing. **Files (read-only first):**
  `test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp`,
  `test/test_continuous_capture/test_continuous_capture.cpp`, `src/Track/TrackDeferredMaintenance.cpp`.
- **Preconditions:** seeded committed passes-only slot, selected and enabled, current length, no pending
  publisher, deterministic clock, production dispatch order, DEC-192 recovery request compiled out or
  guarded off. Record the exact patch that disables it so the fixture cannot silently depend on it.
- **Bound:** one bounded Snapshot/idle turn per opportunity; no gather or allocation on the MIDI tick; no
  frozen clock; no time injected to "rescue" readiness. Count turns explicitly.
- **Evidence to record:** first failing assertion, turn count at failure, emitted-pair inventory, and the
  same fixture on the reverted/mutated tree inverted.
- **Stop condition:** if no mutation reproduces the lost producer natively, record NON-REPRODUCTION and go to
  the device A/B (task 5) instead of inventing a fix.

## Risks and decision rule

- **Fixing the symptom with a workaround** (restore, re-record, hydrate at boot, whole-root rebuild at Start)
  is rejected; it hides the regression and touches user data.
- **Mutation re-enablement left in the tree** is a defect. Every bisect patch is scratch and must be removed
  or clearly converted into the reviewed repair.
- **A/B on a repaired card** produces a false negative; pin the body, not just the image.
- **Decision rule:** only if the last-good image's playback is proven to depend on an unsafe or accidental
  behavior that cannot legitimately be restored does this change stop and hand back to
  [`future_root_independent_playback`](../../../docs/Plans/future_root_independent_playback.md), with explicit
  owner authorization. It does not do so by inference.

## Review requirement

One focused independent lifecycle review using [`docs/Agents/reviewer.md`](../../../docs/Agents/reviewer.md)
after the RED names the boundary and before the repair is accepted; fix concrete findings and retest. No broad
review loop.
