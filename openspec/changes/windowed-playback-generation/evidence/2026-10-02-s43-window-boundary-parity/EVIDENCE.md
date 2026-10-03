# Slice 4.3 — content parity across a window boundary (F-7)

**Change:** `windowed-playback-generation` · **Task:** 4.3 (**landed; the task stays open on its device leg and on the
closure obligation the measurement names**) · **Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Increment 4.3
**Contract:** the spec's *Content parity across a window boundary* — "The content served by successive windows SHALL
concatenate to the same events as the whole-loop path. No event SHALL be lost, duplicated or reordered at a window
boundary", scenario *No tear at a boundary* — "**THEN** the events served across the boundary equal those the
whole-loop path serves **AND** no note-on is left unclosed by the transition".

---

## What landed

| File | Change |
|---|---|
| `include/Track.h`, `src/Track/TrackPlaybackWindowBuild.cpp` | `Track::playbackWindowEventAtForSlot(slot, cursor, out)` — **the served window's ordered event stream**, rendered exactly as the emission path renders it (`storage.events[storage.order[cursor]]`). Read-only, O(1), allocation-free; it reads the window that is **served** (complete, published, valid interval), never a prepared successor. No new state. |
| `test/test_runtime_working_set_ownership/…` | `test_windowed_stream_concatenates_to_the_whole_loop_stream_across_a_boundary` — 32 successive windows over a 64-bar demoted loop, concatenated and compared **row for row** with the whole-loop stream, including the note whose closure crosses a boundary; plus the shared helper `publishBoundaryRootForSlot` (boundary-sensitive root content) and `readServedWindowStream`. |

**Why this is the missing piece rather than a convenience:** the lane exposed only identity, coverage, a count and a
presence predicate — there was **no way to read what a window actually holds**. Nothing in production consumes a
window's events (the emission path still reads bridge/pair/oracle/merged; `stepPlaybackViewMaintenance` always returns
`Idle` in production because nothing requests a window), and no native fixture read a built window's content. So the
spec's parity clause had no observable on this side at all, and the emission switch (3.7) would have needed this door.

**Why the reference is the existing instrument, not a second one:** the whole-loop stream comes from
`LoopContentResolution::resolveWindow(snapshot, 0, loopLengthTicks, out)` — the project's existing content resolver at
whole-loop width, the same primitive the content-verification pipeline's `effective` inventory dump is built on. The
device leg of that pipeline (with the **windowed** stimulus profile) is task **6.4**, and the native/hardware split is
task **6.5**; both are owed, and named below.

## The oracle, case by case

| Case | Observed |
|---|---|
| the fixture's root | built with three notes: one inside the first two-bar tile, one whose **NoteOn is in tile 0 and NoteOff in tile 1** (the boundary closure), one in the last tile — installed as a **current** root (`publishedSnapshotIsCurrent`) |
| the reference | `resolveWindow(root, 0, 64 bars)` = **6 rows** (three notes, on and off) |
| the interval policy | for every tile, `playbackWindowInterval(64 bars, demoted, centre) == {start, 2 bars}` — the policy the parity rides on, asserted per tile |
| the tiles | **32** windows requested, each built to completion, and the served interval asserted equal to the requested tile |
| the two publication paths, measured | the **first** window is installed as the **served** one (nothing to straddle); every **replacement** publishes as the ready successor and the consumer-side edge adopts it — asserted as such, rather than papered over |
| **row parity** | the concatenation equals the whole-loop stream row for row and in order (tick, type, channel, note): nothing lost, nothing duplicated, nothing reordered at any of the 31 boundaries |
| the closure across the boundary | the boundary note's ON is served by **tile 0** and its OFF by **tile 1** — measured, and it is *not* synthesized at the edge |
| the closure clause | the concatenation balances (equal NoteOn and NoteOff counts), so **no note-on is left unclosed by the transition** — the spec's clause read at the concatenation, which is where it is a property of the lane rather than of a consumer |

## The closure obligation the measurement names

Because a window's stream is a half-open tick filter, a note sounding across a boundary legitimately ends its served
window **unclosed** (its NoteOn is in tile N) and begins the next window **unmatched** (its NoteOff is in tile N+1).
The concatenation is therefore balanced while **either window alone is not** — so "no note-on is left unclosed by the
transition" is satisfied by the *sequence* and is an obligation on the **consumer**: the emission switch must not treat
a window's end as a closure, and the ledger's obligation set (4.5's `ClosureSettlement`, 3.14's `NotePresenceSet`) is
what discharges it. This is recorded as the measurement it is, not as a defect: nothing here is wrong, but the
transition now has a **named owner** instead of an assumption. It is owed to **3.7** (the emission switch) with 4.5's
closure handling.

**Also measured, and owed for the same reason:** the interval policy is *centred* on the playhead, so consecutive
requests at *arbitrary* phases produce **overlapping** windows (the fixture's tiles are the centre sample, which tiles
exactly). A consumer that plays consecutive windows therefore must not double-serve an overlap; that too is 3.7's
obligation, and it is why this slice's parity claim is stated for the tiling sample rather than for every phase pair.

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **66/66** (was 65; +1 fixture) |
| `pio test -e native -f test_storage_loop_io` | **52/52** (unchanged) |
| `pio test -e native -f test_playback_prewarm` (cited pin) | **190/190** (unchanged) |
| `pio test -e native -f test_midi_service_drain` | **38/38** (unchanged) |
| `pio test -e native` (full) | **2698 cases: 7 skipped, 2691 succeeded, 0 failed** (baseline 2697 → +1) |
| `pio run -e teensy41-capture-serial` (clean rebuild, 82.4 s) | **SUCCESS** — `variables 96,192` **unchanged** / `code 392,328 + padding 888` / locals 34,880, floor 840, **48 B headroom**, byte-identical to the eight previous slices (one cold accessor plus a test file: no ITCM growth, no pin moved) |

## Red-proofs

Four mutations, each applied to a **named** file at an anchor whose **occurrence count** was asserted before the run,
each restored **byte-identically** (`shasum -a 256 -c`):

| # | Mutation | Observed failure |
|---|---|---|
| M58 | the interval policy loses its centring (`phase - length/2` → `phase`) | the fixture's per-tile policy assertion: `Expected 1536 Was 2304` (plus six other window fixtures) |
| M64 | the window span (`kWindowBars = 2` → `1`) | `Expected 0 Was 384` (plus two others) |
| M60 | the accessor reads the **ready successor's** storage instead of the served window's | the served-window read fails (`Expected TRUE Was FALSE`): a successor is not readable through this door |
| M61 | the accessor drops its bounds check | the fixture's out-of-range refusal fails (`Expected FALSE Was TRUE`) |

**The bound of these red-proofs, stated rather than glossed:** the mutations that break the lane's output are caught by
the fixture's **interval and coverage pins** before the row-for-row comparison runs, so the row comparison is the claim
those pins protect rather than a separately red-proved assertion. What the comparison *would* catch — a wrong or
reordered row — is the thing no honest mutation of this slice can produce without also breaking the pins; that is
recorded as the fixture's reach, not claimed as coverage it does not have.

## Not claimed

- **No production consumer.** Nothing reads a window's events for playback today; this slice makes the stream
  *readable* so the parity proof (and, later, 3.7's switch) can read it. `stepPlaybackViewMaintenance` still returns
  `Idle` in production, and `ExecutionBounds.md` row 27 still classifies active windowed playback as **not admitted**.
- **No device / HITL evidence.** The pipeline the spec names is the HITL content verification
  (`docs/Guides/HITL_CONTENT_VERIFICATION.md`), whose **windowed profile** is task 6.4 and whose device legs are 6.5's
  — neither exists, and active windowed playback is denied until row 27's bounds are measured. So F-7's **device leg
  is owed and gated**, and this slice claims a native parity proof only.
- **The loop-wrap boundary is not in this fixture.** The content is boundary-sensitive for an interval edge and for the
  ends of the loop's content, but a wrap note (whose NoteOff precedes its NoteOn in tick order) is not exercised here;
  `5.5` names "loop wrap and cross-page note closures" for the paged path and this is recorded rather than implied.
- **Task 4.3 stays open** on: its device leg (6.4/6.5), the closure obligation at the transition (3.7 with 4.5), and
  the overlap rule for consecutive arbitrary-phase windows (3.7).
