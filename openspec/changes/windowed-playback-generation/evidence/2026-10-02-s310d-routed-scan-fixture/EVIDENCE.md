# Slice 3.10 (increment 4) — the owed fixture: the routed scan serves the urgent holder

**Change:** `windowed-playback-generation` · **Task:** 3.10 (**stays open** on the starvation half, the envelope
derivation and the grain-uniformity item; the wiring's gate is now **met**) · **Date:** 2026-10-02 · **Branch:**
`feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Increment 3.10 (increment 4)
**Closes:** the gap increment 3 recorded — "the wiring has no discriminating fixture yet".

---

## What landed

One fixture, `test_window_owner_serves_the_most_urgent_holder_and_rotates_between_them`, plus its two helpers
(`publishNotesInBarsRootForSlot`, `prepareOwingWindowHolder`) and the `windowHolderAdvanced` observable. **No
production change.**

**The observable.** A holder's own next grain: `Fill` while the fill still has work, and **progressed** once one
fill grain moved it past that (a small window finishes outright — `Complete` — and the build stops owing; a larger one
stays `Fill` for several turns). "Progressed" is the honest form: the specific post-state is the builder's business,
and the first version of the fixture wrongly asserted `Order` where a one-note window answers `Complete`.

**The cases**, each on its **own slot pair** (a slot that already owes a build answers `BuildInFlight`, and that check
is *per slot*, which is also why two slots on one track can owe at the same time — measured, not assumed):

| Case | Setup | Assertion |
|---|---|---|
| **A — urgency beats index order** | slot 0's interval a whole pass away (window with > one fill grain of work), slot 1's 32 ticks away (one-note window); serve at phase 9200 | slot **1** progressed and slot 0 did **not** — the old ascending scan served slot 0 and left slot 1 untouched |
| **B — the symmetric control** | the roles swapped, on a second slot pair | slot **0** progressed and slot 1 did not: urgency decides, so (A) is not "the higher index always wins" |
| **C — rotation when urgency ties** | both holders the same interval and the same lead, on a third slot pair | two turns with **one persistent cursor** progressed **both** — while the old scan would serve slot 0 twice |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **68/68** (was 67; +1 fixture) |
| `pio test -e native -f test_midi_service_drain` | **39/39** |
| `pio test -e native -f test_storage_loop_io` | **52/52** |
| `pio test -e native -f test_playback_prewarm` (cited pin) | **190/190** |
| `pio test -e native` (full) | **2701 cases: 7 skipped, 2694 succeeded, 0 failed** (baseline 2700 → +1) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** — `variables 96,192` **unchanged** / `code 392,328 + padding 888`, 48 B headroom |

## Red-proofs — the decisive one first

| # | Mutation | Observed failure |
|---|---|---|
| **M76** | the scan goes back to the **pre-3.10 ascending order**, ignoring the policy entirely | case A (`the urgent holder was served, not slot 0`). **This is the proof the increment-3 gap needed:** the fixture does distinguish the routed scan from the old one, so the RWO-recorded "a dropped build left every fixture green" trap is closed for this wiring |
| M77 | the urgency is ignored (`handoffLeadTicks = 0`, so only the tie-break decides) | case A again: the deadline order is load-bearing, not decorative |

Both applied to the named file with the anchor's occurrence count asserted, and both restored **byte-identically**
(`shasum -a 256 -c`). Unity stops a test at its first failure, which is why both mutations report case A rather than
also case C.

## Not claimed

- **Still not reachable in production:** nothing requests windows yet, so the wiring changes no live behaviour; the
  proof is native, and the hardness is that the *policy* is exercised against live holder state for the first time.
- The **starvation half**, the **envelope derivation** from grain costs and the worst permitted service gap, and
  **"one declared grain per turn"** as a uniform invariant remain open — unchanged by this increment.
- The fixture exercises holders **within one track**. Whether the opportunity should rotate across *tracks* (where the
  same starvation argument applies to the `main.cpp` loop that stops at the first track with work) is **not measured
  here** and is recorded as the next question rather than answered by implication.
