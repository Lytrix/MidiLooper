# 3.15e — migration slice 2a: the arbitration contract

**Change:** `windowed-playback-generation` · **Task:** 3.15e · **Branch:** `feature/unified-rolling-playback-path` ·
**Base:** `cd0b77d7` · Design:
[2026-10-22 addendum](../../ARCHITECTURE-REASSESSMENT.md#2026-10-22-addendum--the-live-overdub-overlay-moves-onto-the-single-window-reader-315e-dec-116-extension).

## What this slice settles

Three owners can owe a window on one slot and `kViewCount == 3` gives **one** `ready` role:

| Class | Role | Priority |
|---|---|---|
| NOTE_EDIT preview | explicit session-local presentation override | **1** |
| Live overdub overlay | temporary continuity reader for the active overdub generation | **2** |
| Committed successor | ordinary convergence (subsumed by the overlay's base while an overlay is owed) | **3** |

The contract, written as a pure resolver (`include/Utils/PlaybackDemandArbitration.h`):

1. **Identity before priority.** A claim whose currency does not match its owner's live state (stale overlay capture
   generation, stale preview revision, stale committed root) is `RefusedStaleIdentity` **whatever its class**, and a stale
   claim holding `ready` asks for that role to be retired (`retireStaleReady`), never adopted.
2. **One owner, contractually.** Among valid claims the class order above picks the owner; every other valid claim is
   `DeferredToHigherPriority`. The rule reads the class a claim *declares*, not its position in the caller's array, so
   reordering claims cannot change the winner (proved by fixture).
3. **A losing `ready` candidate always has a name.** A valid candidate of a lower class is superseded
   (`supersedeReady`); two owners claiming the same role is a **named inconsistency** (`roleConflict`,
   `RefusedRoleConflict`) — never a silently resolved race.
4. **No role, no silent echo.** With room exhausted the winner is `RefusedNoRoleCapacity` and
   `compatibilityAuthoritative` says the echo carries the slot, so a caller cannot report silence as success.
5. **No hidden fourth state.** The resolver is pure: no window, no queue, no runtime field. A loser keeps its own existing
   obligation and re-asks on its own turn, which is what makes "at most one complete unpublished candidate" structural
   rather than conventional.

## What this slice deliberately does not do

- **No lane wiring.** The three *claim providers* (committed requester, preview lane, overlay lane) and the role-retirement
  calls are **slice 2b**. Nothing in production includes this header yet, so both images are byte-identical.
- **No new demand record.** A per-slot three-way demand table would grow the pinned runtime (`7 904` target) and is exactly
  the "hidden extra state" the contract forbids; slice 2b derives each claim from its existing owner (preview revision,
  overlay generation from slice 1's `overlayCaptureGeneration()`, committed request identity).
- **No audible change.** Slice 3 still owns what the clock edge binds and the echo's removal.

## Oracle

| # | Case | Expected |
|---|---|---|
| 1 | committed complete `ready`, overlay becomes owed | overlay owns; committed `DeferredToHigherPriority`; `supersedeReady` |
| 2 | overlay holds `ready`, NOTE_EDIT starts | preview owns; overlay deferred; `supersedeReady` |
| 3 | NOTE_EDIT exits, overdub still live | overlay owns; no supersede; committed deferred |
| 4 | overlay generation stale, committed owed | committed owns; overlay `RefusedStaleIdentity`; `retireStaleReady`, no supersede |
| 5 | preview revision stale, overlay owed | overlay owns; preview refused |
| 6 | two complete `ready` claimants | one owner; the other `RefusedRoleConflict`; `roleConflict` |
| 7 | role exhaustion | `RefusedNoRoleCapacity`; `compatibilityAuthoritative`; no owner |
| 8 | all three owed, and the same facts in another array order | preview > overlay > committed, each named; identical resolution |

## Verification

`pio test -e native -f test_playback_demand_arbitration` (8/8), `pio test -e native`, both firmware builds (unchanged
pins), three mutations (class order reversed → 6 contract failures; identity ignored → exactly the two stale cases;
role-conflict branch removed → exactly the two-candidate case), `openspec validate --strict`, `git diff --check`.
No device leg (3.15f).
