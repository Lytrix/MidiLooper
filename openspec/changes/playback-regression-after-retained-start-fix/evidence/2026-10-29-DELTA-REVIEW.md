# DEC-191 delta review against the tree — 2026-10-29

Read-only. Sources: the checkpoint tree (`a3d56f07`) and `HEAD` (`2edb1f2e`, pre-DEC-191) compared with
`git show`, plus the 2026-10-29 captures. No build, upload, firmware edit or device action.

Question: which DEC-191 delta could plausibly suppress the old rootless playback producer?

## What is actually in the tree

No DEC-191 production call site remains. Only the helpers and their tests:

- `Track::overdubStartPreflight` — `TrackOverdubLifecycle.cpp:65`, called only from a test.
- `overdubStartRetryDecision` — `include/Utils/OverdubStartGate.h`, called only from a test.
- `Track::settleOrphanedSessionExitReservation` / `Loop::releaseOrphanedSessionExitReservation` — definitions
  at `TrackOverdubLifecycle.cpp:4503` and `LoopSnapshotPublish.cpp:2084`, no production caller.
- `TrackDeferredMaintenance.cpp:1065` keeps the D4 post-mortem: the settlement ran here unconditionally and the
  device regressed in `session_20261029_011304.log` — a deferred Record stop parked at `CanonicalPrefix` with
  `pub=1`, `playback_read` zero and the panel holding `why=hold_gap`.

## Structure that was already there before DEC-191

Verified identical at `HEAD` (`2edb1f2e`):

- `serviceAcceptedInteractionTurn()` is `if (!hasAcceptedUnexecutedInteraction() || hasPendingDeferredOverdubStop()) return false; (void)driveAcceptedInteraction(); return true;`
  The drive's own result is discarded, and `driveAcceptedInteraction` returns `false` on "ordinary admission
  refused: retain the exact request" (`TrackOverdubLifecycle.cpp:454`). **A refused Start therefore consumes the
  idle turn every turn.**
- `processDeferredIdleMaintenance` starts with `if (serviceAcceptedInteractionTurn()) return;` (line 1220), i.e.
  the early return sits above the idle lane's later branches.
- `hasAcceptedUnexecutedInteraction()` gates `processDeferredCanonicalPrefix` (874), the overdub overlay lane
  (7132), `hasRealtimeDeferredIdleWork` (154) and `TrackManagerSlotMatrix.cpp:45`.

**But the playback view lane is a different owner.** `main.cpp:571` iterates every track unconditionally,
calls `requestActiveCommittedPlaybackWindow` for playing tracks and picks one holder round-robin through
`playbackWindowServiceOffer`. Neither the latch nor a pending stop is consulted there. So a retained Start
**does not** starve the playback source lane; it starves canonical-prefix and overlay work. That rules out the
tempting "turn consumption is the playback cause" repair.

## Per-delta verdict

| Delta | Eligibility/order | Claim/obligation lifetime | Early return before the producer | Verdict |
|---|---|---|---|---|
| D1 preflight | no — inside `startOverdubbing`, same `false` return | no | no (Start path only) | **LOW** |
| D2 retry park/wake | no new gate; **lengthens** the latch's lifetime | no | no (does not reach the playback lane) | **LOW for playback**; the 7.5 Hz fix |
| D3 orphan settlement (Start path) | no | **yes** — `Reserved → Empty` | only while a Start claims the slot | **MEDIUM** |
| D4 orphan settlement (idle lane) | adds one consumer at the top of the idle lane | **yes** — releases a claim a pending stop still owes | only inside the idle lane | **HIGH for 011304** |
| D5 order | no separate change found; D4 is the order delta | — | — | covered by D4 |

## The two signatures do not match

- `011304` (D4 present): `pub=1` at `CanonicalPrefix`, `playback_read` 0, `root_rev` 0, `hold_gap`, idle lane
  ~9 ms — **no** retained Start, **no** 7.5 Hz storm. This is D4's claim-release mechanism.
- `011932` (D4 removed) and `012142`: **no** pending stop, **no** retained Start, `playback_read` 0, and
  `playback_build,pending_position_invalidated` at a stopped/transport boundary. This is a *different* refusal,
  and `TrackPlaybackWindowBuild.cpp:203` makes it **sticky for the interval**
  (`if (same && request.positionInvalidated) return;`). No DEC-191 delta touches this code.

Predictive consequence: a two-state reconstruction (`reconstruction-control` vs `reconstruction-dec191`) is
likely to **not** toggle playback, because the persistent silence is not delta-shaped. It would cost a session
and return "no difference".

## Recommended order (replaces the reconstruction-first plan)

1. **D4 single mutation** on a scratch worktree: restore only the unconditional idle-lane settlement, rootless
   slot, decoy Record stop, and check whether the `pub=1`/`hold_gap` signature returns. Cheap and falsifiable.
2. **Sticky-invalidation probe** (native, no device): rootless passes slot, Start, transport stop/start, assert
   a *new* playback request is issued after the invalidated one. If it is not, that is the named producer
   boundary for the persistent silence and the repair target, independent of DEC-191.
3. Only if 1 and 2 both fail to name a boundary: build the reconstruction pair.

Do not treat "retained Start consumes the idle turn" as the playback repair: it cannot reach the playback lane.
It may still deserve its own fix for canonical-prefix work, under its own evidence.
