# Slice 3.10 (increment 3) — the wiring: the scan routes through the policy

**Change:** `windowed-playback-generation` · **Task:** 3.10 (**stays open; its discriminating fixture is owed and named
below**) · **Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Increment 3.10 (increment 3)
**Predecessors:** [increment 1](../2026-10-02-s310-holder-service-fairness/EVIDENCE.md) ·
[increment 2](../2026-10-02-s310b-shared-handoff-lead/EVIDENCE.md)

---

## What landed

| File | Change |
|---|---|
| `include/Track.h` | `Track::stepPlaybackViewMaintenance(uint32_t currentPhase = 0, MidiServiceDrain::PlaybackHolderCursor* cursor = nullptr)` — the owner turn now takes the playhead's phase and the caller's rotation cursor. |
| `src/Track/TrackPlaybackWindowBuild.cpp` | The scan **builds the holder set** (each owing slot with its urgency) and lets `selectPlaybackHolder` choose; the grain served is unchanged. |
| `src/main.cpp` | The one production call site passes the phase (`clockManager.getCurrentTick()`) and the rotation cursor. |

**The urgency is derived, not stored.** An in-flight build already carries its interval: `RuntimePlaybackViewFill::begin`
sets `destination.coverage` (`src/Track/RuntimePlaybackViewFill.cpp:69`), so the holder's handoff is
`set->views[set->lifecycle.build].coverage.startPhase` and its urgency is `handoffLeadTicks(currentPhase, startPhase,
loop.loopLengthTicks)` — the same quantity the admission judges. No new field, no size-pin move.

**`canProgress` is structurally true for an owing holder today**, and that is stated rather than faked: a begun build's
grains are bounded work, and the refusals live at *request* time (`CannotCompleteBeforeHandoff`, `SourceNotCurrent`). The
policy's blocked path is for a holder whose source goes stale mid-build.

## The RAM1 guard did its job twice, and the placement rule answered it

The first wiring put the rotation in an **8-cursor array** in `main.cpp` → the build **FAILED** the approved budget
(`variables 96,224` against 96,192: +32 B of DTCM). Shrinking to a **single byte** still cost +32 B, because one new byte
of BSS moves the TU's alignment. The fix is DEC-108 §13b's own placement rule — an additional counter is
`DMAMEM`/`FLASHMEM` — so the cursor is `DMAMEM` (RAM2, where 362 KB are free) and the approved line is untouched. The
stricter form of the cursor was rejected **by gates, not by preference**, and the pin was not re-based.

## Verification

| Check | Result |
|---|---|
| `pio run -e teensy41-capture-serial` | **SUCCESS** — `variables 96,192` **unchanged** / `code 392,328 + padding 888` / locals 34,880, **48 B headroom** (the guard's own check passed) |
| `pio test -e native -f test_runtime_working_set_ownership` | **67/67** (every existing owner-turn fixture, now through the policy-routed scan) |
| `pio test -e native -f test_midi_service_drain` | **39/39** |
| `pio test -e native` (full) | **2700 cases: 7 skipped, 2693 succeeded, 0 failed** |

## Not claimed — the owed fixture, named

- **The wiring has no discriminating fixture yet.** Its new behaviour is only observable with **more than one owing
  holder on one track** (today every fixture has exactly one), and the existing fixtures cannot tell the policy-routed
  scan from the old ascending scan — which is precisely the RWO-recorded trap ("dropping the window build once left
  every fixture green"). **The fixture is owed**: two slots on one track, both owing, with the more urgent one at the
  higher slot index and one grain from publication, asserting that the single turn publishes the **urgent** slot and not
  slot 0 (which the old scan would have served), plus that a persistent cursor interleaves them. Until it lands, the
  rotation and the deadline order are **proved as a policy** (increments 1–2) and **wired but not proved end-to-end**.
- **Nothing in production owes a window today**, so this wiring changes no *reachable* behaviour: with no owing holder
  both the old and the new scan return `Idle`. That is why the slice is safe to land before the fixture — and equally
  why the fixture, not a device run, is what will prove it.
- The other open items are unchanged: the **starvation half**, the **envelope derivation** from grain costs and the worst
  permitted service gap, and **one declared grain per turn** as a uniform invariant.
