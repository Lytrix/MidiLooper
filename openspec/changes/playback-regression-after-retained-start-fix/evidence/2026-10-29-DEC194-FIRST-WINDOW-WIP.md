# DEC-194 — bounded rootless first window: work in progress

Owner direction (2026-10-29): rolling/windowed playback over the **authoritative committed passes**, with a whole-loop
root as an **optional acceleration source**, not a precondition for liveness. The measured defect this targets is the
device's ~38 s wait: `session_20261029_032402.log` shows `pending_position_invalidated` → `snapshot_unit,kind=start` →
root only at 58.49 s.

## What landed (production)

`Track::requestCommittedPlaybackWindowForSlot` now establishes the source for a rootless slot that is being asked for
playback:

```cpp
if (!loop.publishedSnapshotIsCurrent() &&
    servingRetainedRevisionForSlot(slot) != loop.playbackRevision) {
  const PlaybackSourceRetainResult retained =
      retainPlaybackSourceForSlot(slot, loop.playbackRevision, playbackGeneration);
  if (retained != Admitted && retained != AlreadyRetained)
    return PlaybackViewRequestResult::SourceUnavailable;
}
```

Why this is the right seam: `retainPlaybackSourceForSlot` pins **passes and committed chunk ids only** — no event
gather, no root, no content copy — and it is idempotent (`AlreadyRetained` refreshes the role without re-owning a
body). It had **no production caller at all** before this slice; the mechanism existed but was never wired.

## What is proven

- Behaviour-neutral for the existing suite: 252 cases, 248 pass (3 failures: this new acceptance RED, the DEC-193
  RED, and the pre-existing frozen-packet characterization).
- `teensy41-capture-serial` SUCCESS, RAM1 `variables 96,192 / code 391,656 / padding 1,560` unchanged.
- The retention call itself cannot grow with loop length: it enumerates passes and chunk ids, not events.

## What is NOT proven — the acceptance fixture is RED

`test_dec194_a_rootless_playing_slot_builds_its_first_window_without_a_root` (device shape: committed passes, no root,
`TRACK_PLAYING`, no fixture-side retention) still fails to reach a serving window.

Observed with the probe from 3.5b:

- the lane reports **`gate_no_role`** (`coverageRoleAvailable` false) during the first turns;
- after the loop the role *is* available again, so the refusal is **transient**, and the demand never recovers from it;
- no serving window is published in 256 turns.

So wiring the source is necessary but not sufficient: the first window is refused at role arbitration while the
prewarmed view set is still settling. That is the next term to name, not a reason to widen the slice.

## Reverted, deliberately

- A candidate change to `requestCommittedPlaybackWindowForSlot`'s `holds()` (treat unqualified/invalidated demands as
  not holding the interval) was tried and **reverted**: it is a plausible lifecycle fix, but it did not turn the
  acceptance fixture green, and an unproven semantic change in the demand lifecycle does not belong in a WIP commit.
  It stays a hypothesis with its own RED to write.
- The noisy per-turn diagnostic asserts used while narrowing the blocker were removed; the probe rows
  (`gate_*`, `dispatch_*`) remain in the production image.

## Next

1. Name **which** `coverageRoleAvailable` term is false on the first turns of a rootless `TRACK_PLAYING` slot, with the
   same one-enum/one-row pattern (`viewBuild.active`, `completedPromisePendingJudgement`, `ownsCoverageSource`,
   `canonicalWindowPhase`, build-role published/complete).
2. Fix only that term (identity-scoped), then re-run the acceptance fixture: `root_rev 0` allowed, serving window
   required, whole-root publication still absent.
3. Then the rolling handoff and the 4-bar vs 40-bar cost-class assertion.
