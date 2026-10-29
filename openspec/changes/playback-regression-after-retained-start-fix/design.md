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

Fork 2 — **card state.** *Measured 2026-10-29 and rejected for the rootless property.* A broken run was
suspected of rewriting the slot body so every later image inherits it, but the pre-regression boot restores the
same slot **rootless** (`root_rev,0`) and still serves playback from the pass resolution, while the first-bad
boot never builds a root at all. The persisted shape is not the differentiator. The 00:18 backup cannot serve
as the pre-regression body either: its `loop_07_03.bin` is a 78-byte stub and no slot body in it is the 31-bar
shape. A save/publication parity defect of a different kind is not excluded, but it is not this regression and
is out of scope. Full readings: [BOUNDARY-2026-10-29.md](evidence/BOUNDARY-2026-10-29.md#card-inventory--2026-10-29-read-only-no-upload-no-sd-write).

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

1. ~~**Read-only card inventory.**~~ **DONE 2026-10-29.** The slot is restored rootless on the
   pre-regression image too, which still plays from the pass resolution; the first-bad boot never builds a
   root. Fork 2 (card state) does not explain the regression. Readings in
   [BOUNDARY-2026-10-29.md](evidence/BOUNDARY-2026-10-29.md#card-inventory--2026-10-29-read-only-no-upload-no-sd-write).
2. ~~**Delta review against the tree.**~~ **DONE 2026-10-29.** No DEC-191 production call site remains; D1/D2 are
   low, D3 is a narrow claim-lifetime change, D4 is the only delta with a demonstrated mechanism (`011304`), and
   the playback lane is *not* gated by the retained latch. Two different failure signatures exist. See
   [delta review](evidence/2026-10-29-DELTA-REVIEW.md).
3. **D4 single mutation.** Restore only the unconditional idle-lane settlement on a scratch worktree and check
   whether the `pub=1`/`hold_gap`/`playback_read 0` signature returns with a decoy Record stop. Cheap and
   falsifiable; NON-REPRODUCTION is a valid result.
4. **Sticky-invalidation probe (native).** Rootless passes slot, Start, transport stop/start: is a *new* playback
   request issued after the invalidated one (`TrackPlaybackWindowBuild.cpp:203` makes rejection sticky for the
   interval)? If not, that is the producer boundary for the persistent silence.
5. **Two-state reconstruction — only if steps 3 and 4 name nothing.** `reconstruction-control` (no DEC-191
   behavior) vs `reconstruction-dec191` (same tree plus DEC-191 behavior), unrelated edits identical and the
   DEC-192 recovery guarded off. Name them as reconstructions, never as `0dbf08c1`/`3e79bd1b`. If it toggles,
   split D1+D2 / D3 / D4.
6. **Name the lost producer.** For the winning experiment, trace which producer no longer runs (publication
   request, source admission, or the reader's qualification) and its refusing predicate.
7. **Repair only that boundary**, reusing the existing owner. No new state.
8. **Oracle B** on the repaired tree; then Oracle A; then focused native, full native, build, scoped review.
9. **Device A/B**: same slot, same gesture, both oracles, physical USB-to-Mac output. Record the HEX per
   [TEENSY_FIRMWARE_UPLOAD](../../../docs/Guides/TEENSY_FIRMWARE_UPLOAD.md#procedure) step 8.

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
