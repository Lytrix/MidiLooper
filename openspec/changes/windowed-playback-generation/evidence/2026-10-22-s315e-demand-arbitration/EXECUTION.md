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
