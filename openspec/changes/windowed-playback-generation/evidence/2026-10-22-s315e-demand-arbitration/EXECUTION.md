# 3.15e — migration slice 2a: completion record

**Packet:** [`EXECUTION-PACKET.md`](EXECUTION-PACKET.md) · **Status:** **native verified**, no production include, no
audible change, no DEC yet (slice 3 owns the emission rule).

## Landed

`include/Utils/PlaybackDemandArbitration.h` — a pure owner-level resolver over the three demands that can owe the one
`ready` role, with a contractual class order (Preview > Overlay > Committed), identity-before-class refusal, exactly one
owner, a named fate for every other claimant, a named role conflict, a named role-capacity refusal that says the
compatibility route is authoritative, and no stored state at all.

New suite `test/test_playback_demand_arbitration` (8 cases) covers the four owner constraints directly: deterministic
priority (also independent of the caller's claim order), explainable losers (deferred / superseded / stale / role
conflict), identity beating class for both overlay and preview, and the single-owner + no-extra-state rule.

## Gates

| Gate | Command | Result |
|---|---|---|
| Focused | `pio test -e native -f test_playback_demand_arbitration` | **8 / 8** |
| Full native | `pio test -e native` | **3 394 / 7 skipped / 3 387 succeeded / 0 failed** (baseline 3 386 / 7 / 3 379) |
| Default build | `pio run -e teensy41-capture-serial` | SUCCESS, `code 391 688 + padding 1 528` — unchanged |
| Lane build | `pio run -e teensy41-capture-serial-window-qualification` | SUCCESS, `code 391 848 + padding 1 368` — unchanged |
| Production include | `grep -rn PlaybackDemandArbitration src include` | only the header itself: no firmware TU includes it |
| Spec / whitespace | `openspec validate --strict`, `git diff --check` | valid / clean |

## Mutations (each restored byte-identically, header `sha256 7dce9cfaed44fad3…`)

| # | Mutation | Result |
|---|---|---|
| M1 | class order reversed (Committed > Overlay > Preview) | **6 failures**, each naming its contract case |
| M2 | `identityCurrent` ignored by the validity test | exactly the two stale-identity cases fail |
| M3 | role-conflict branch disabled | exactly the two-complete-candidates case fails (`Expected 5 Was 2`, `RefusedRoleConflict` refused) |

## Owed (slice 2b)

Wire the **claim providers** and the role-retirement calls: the committed requester, the NOTE_EDIT preview lane and the
overlay lane each build their claim from existing owner state (preview revision; slice-1 `overlayCaptureGeneration()`;
committed request identity), the resolver decides who may build/adopt this turn, and the loser returns its named outcome
without recording a second complete candidate. That slice must re-check the pins and the `ready` contention under a
NOTE_EDIT session that starts and exits while an overdub stays live. Slice 3 then defines the clock edge, the echo
removal and the ledger rule (DEC).

## 2b-i — the per-class identity providers (2026-10-22)

`include/Utils/PlaybackDemandClaims.h` turns each owner's two existing facts into the claim the arbiter consumes, and it
is where the "identity beats role labels" rule is actually true or false:

| Class | Live / bound pair | Current only when |
|---|---|---|
| Committed | `playbackRevision` + Track playback generation | both are present and both equal the bound pair |
| Preview | NOTE_EDIT session preview revision | the live session revision is non-zero and equals the bound revision |
| Overlay | live overdub capture content generation (slice 1's `overlayCaptureGeneration()`) | the live generation is non-zero and equals the bound one — a wrap's pass commit advances it |

Every comparison is within one class, and **absent is never current** (`live == 0`), so "no root yet", "no session" and
"no live capture" cannot accidentally match a zero bound value and claim a boundary.

Four new cases (12/12 in the suite): the per-class rules including the absent case; a wrap pass commit making the
overlay's own claim stale so the **committed** successor wins despite the class order; the converse (an advanced committed
root stale against a valid overlay); and the three-view pressure case — three valid demands, one `ready` role, exactly one
owner, the role's fate decided by name, two losers holding nothing.

Two mutations, each killing exactly the intended case and restored byte-identically (`sha256 5bb3a810eb7c079b…`):
dropping the non-zero guard (the absent-currency case fails), and dropping the generation half of the committed
comparison (an advanced generation reads as current).

Gates: focused **12/12**; full native **3 398 / 7 skipped / 3 391 succeeded / 0 failed**; both images byte-identical at
the pins (`391,688 + 1,528` / `391,848 + 1,368`) because no firmware TU includes either header.

### Still owed in slice 2

1. **2b-ii — the overlay lane.** A per-overdub request identity and an owner turn that builds slice 1's continuation
   window for the active overdub generation (admission, role, retire on pass commit). Nothing emits from it yet.
2. **2b-iii — the wiring.** Those lanes call the resolver with their own claim plus the other two providers; a loser
   returns a named `PlaybackViewRequestResult`/turn outcome, and the winner executes `supersedeReady`/`retireStaleReady`
   against `viewSet->lifecycle` (the retirement that slice 1 proved is needed). The committed lane must be deferred only
   once the overlay lane can actually take the boundary, otherwise convergence stalls.
