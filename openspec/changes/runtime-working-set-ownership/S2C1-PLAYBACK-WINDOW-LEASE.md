# S2.1 — the playback window lease capacity, derived from the window (not the 2048-event ceiling)

**Status:** derivation only. No behaviour edit and no `src/`/`include/` change — `IMPLEMENTATION_CONTRACT.md`
§4 keeps the authority change in phase F, and this change's own rule is that S2.1 *derives* what the lease
charges. What is landed here: the measured composition of the `P` class on the **target ABI**, the closed form
`P(capacity)`, and the window-derived capacity rule that replaces the global `kEventsPerView = 2048`.

**Where this comes from (cited, not invented).** This is the promotion of **RC-MEM-2's outstanding item** —
*"per-slot bundles are sized by the representable ceiling, not by the window"*
(`openspec/changes/immutable-loop-snapshot-passes/tasks.md`, superseded as strategy, absorbed here) — whose
measured statement was *"an armed slot reserves ~1.16 MB whether or not the loop has content for it"* (that
figure is the **whole arm bundle**, which is wider than the view set of §2), with the
device evidence that a 24-bar loop never publishes a fixed view at all and playback takes the **2-bar merged
window** instead. The class is already declared in the lease vocabulary: `WorkingSetRole::PlaybackWindow` —
*"the term multiplied by N; must stay small and length-independent"* (`include/Utils/WorkingSetLease.h`). What
S4c.1 ([`S4C1-READINESS-CURRENCY-CONTRACT.md`](S4C1-READINESS-CURRENCY-CONTRACT.md)) added is the target the
capacity must serve: an **interval/window with a deadline**, whose identity is `(slot, revision, interval)`.

## 1. The measured composition of one holder

`P` is one `RuntimePlaybackViewSet` (`include/RuntimePlaybackView.h`). Its per-event arrays are `events`
(`MidiEvent`), `order` (u16), `phases` (u32), `mergeScratch` (u16), `retained` (u8), `passOrigins`
(`PlaybackPassEventOrigin`) and `canonicalRanks` (u16); capture staging omits the last two. The code already
computes the total — `RuntimePlaybackViewSet::fixedExternalBytes()` — so the derivation is about its **inputs**.

**The inputs were measured, not inferred.** A host build is the wrong ABI here: `MidiEvent` holds
`const uint8_t* sysexData` (`include/MidiEvent.h`), so `sizeof(MidiEvent)` is **32 B on x86-64 and 20 B on
cortex-m7**, and a derivation taken from the host would inflate every figure by **34 %** — the host value is
recorded in the evidence file as the trap it is. The sizes below come from cross-compiling the real headers with
the project's own toolchain (`arm-none-eabi-c++`, `-mcpu=cortex-m7 -mfloat-abi=hard -mfpu=fpv5-d16`, the Teensy
framework MIDI library), reading each value out of the compiler:

The probe, the exact commands and the raw compiler output are kept in
[`evidence/2026-10-01-s2c1-abi-measurement/`](evidence/2026-10-01-s2c1-abi-measurement/EVIDENCE.md), so these
numbers are re-derivable rather than trusted.

| input | device (cortex-m7) | host (x86-64) | consequence if the host value were used |
|---|---|---|---|
| `sizeof(MidiEvent)` | **20** | 32 | +12 B/event ⇒ **+34 %** on every `P` figure |
| `sizeof(PlaybackPassEventOrigin)` | **4** | 4 | — |
| `sizeof(PlaybackBridgeRecord)` | **28** | 40 | +12 B/bridge record |
| `sizeof(FixedPlaybackEventStorage)` | 28 | 56 | (container; not in the byte sums) |
| view event bytes (7 arrays) | **35** | 47 | `35` is the design's own figure (see below) |
| staging event bytes (5 arrays) | **29** | 41 | staging is 29 B/event, not the view's 35 |

**Four independent cross-checks, all exact, verify the arithmetic** (each is an existing authority statement
about the same structures):

| cross-check | source | derived here |
|---|---|---|
| `35 B/event` — `MidiEvent 20 + order 2 + phase 4 + mergeScratch 2 + retained 1 + passOrigin 4 + canonicalRank 2` | `design.md` §Sizing | 35 B/event |
| views + bases = **350 KB** (3 rolling views + 2 bridge bases) | `ExecutionBounds.md` tag-2 reconciliation | 358,400 B = **350.0 KiB** |
| bridges = **360 KB** | same | 368,640 B = **360.0 KiB** |
| `captureStagings[2]` = `FixedPlaybackEventStorage(2048)` **59,392 + 128 reservations = 61,952 each** | `ExecutionBounds.md` resident-store table | 59,392 + 128 × 20 = **61,952 B** each |
| `canonicalPassInputs` = **147,456 B at C=2048** (a sibling class's prewarm) | `ExecutionBounds.md`, `sizeof(MidiEvent)=20` | 2048 × (2·20 + 2·8 + 8 + 4 + 2·2) = **147,456 B** |

(`ExecutionBounds.md` states `sizeof(MidiEvent)=20` on device in its own text; the cross-checks above are what
makes the *rest* of my table trustworthy rather than merely consistent.)

## 2. `P` as a function of capacity

With `c` the event capacity, `r` the bridge-record capacity and `v`/`b` the view and bridge counts:

```
P_windowed(c)  = Views(3)·c·35  +  Stagings(2)·(c·29 + 128·20)
P_wholetake(c) = P_windowed(c) + Bases(2)·c·35 + Bridges(2)·[ r·(28 + 29 + 8 + 6 + 1) + c·(12 + 4 + 2) ]
```

At **today's ceiling** (`c = r = 2048`), on device:

| mode | bytes | KiB | note |
|---|---|---|---|
| **windowed-only** (long loop: bridges, bases and the prepared pair released — RC-MEM-2, `fullTakePlaybackViewAdmissible()` false) | **338,944** | **331** | the shape a 24-bar loop has |
| **whole-take reserved** (short loop) | **850,944** | **831** | 210 views + 140 bases + 360 bridges + 121 staging |

The sums are corroborated by recorded figures: `ExecutionBounds`' `prewarmViews` = **1,240 KB** per arm (its own
790 + 450 tag reconciliation, the 790 being a whole-take bundle plus the companion structures tagged with it),
together with the four exact cross-checks of §1. **One coincidence to name rather than conflate:** `design.md`
Revision 11's *"~325 KB per loop, ~2.6 MB at today's N = 8"* is the **`N × whole-root`** term — the published
snapshot root, a *different* structure from this one. The magnitudes are close (331 KiB here; `8 × 331 KiB =
2.59 MiB`), which is precisely why they must not be treated as the same term: the root is S2.3's canonical-phase
subject, and what this document measures is the **playback view set**.

## 3. The derivation: capacity comes from the window

The ceiling is not derived from anything — it is a global maximum the arrays were sized to so that "the worst
case a user can add in one interval never has to grow". A windowed authority has a smaller thing to serve:
**the events the resolved window may hold**, `capacity ≈ eventsPerBar × windowBars`. `design.md` supplies the
two densities (typical ~72 events/bar; representable ceiling 4,096 refs/bar) and evaluates what the derived
capacity costs:

| window | capacity at typical density | `P_windowed` | capacity at the ceiling | `P_windowed` |
|---|---|---|---|---|
| 1/4 bar | 18 events | 8,054 B (7.9 KiB) — **42×** smaller | 1,024 | 172,032 B (168 KiB) — 2× smaller |
| 1 bar | 72 | 16,856 B (16.5 KiB) — **20×** smaller | 4,096 | 672,768 B (657 KiB) — **2.0× larger** |
| 2 bars | 144 | **28,592 B (27.9 KiB) — ~12× smaller** | 8,192 | 1,340,416 B (1.28 MiB) — **4.0× larger** |

Three consequences the capacity rule has to carry:

1. **The window changes the term by its own ratio, and the effect is large.** A 2-bar window at typical density
   is **28.6 KB** against today's 331 KiB — a **~12×** reduction of the term the budget multiplies by `N` (a
   1-bar window is 20× smaller). That is what makes the design's `N × P` affordable instead of merely paid.
2. **Length independence becomes a property of the formula, not of the loop.** `P` names `capacity`, and
   capacity names the *window*; nothing in it references the loop length, so `design.md` row 2's "64 bars ≈ 4
   bars" holds by construction rather than by hoping the reservation stays small.
3. **The window alone cannot bound it.** At the representable ceiling a 2-bar window needs **8,192** events —
   *above* today's 2048 — so the ceiling density would make `P` **worse**. The capacity is therefore
   `min(declared product limit, windowEvents)`: the window sets the *shape*, and a declared limit sets the
   *size*. Choosing that limit is a product decision (a single interval then admits at most that many captured
   events), not a measurement.

## 4. What the lease must declare (the four S4c.1 interfaces, now with numbers)

1. **Window identity** — `(slot, revision, interval)` unchanged; it is what keys the lease.
2. **The `P`-class accounting** — the derivation above is that figure: **331 KiB per holder** at the current
   ceiling, `P_windowed(c)` thereafter, and the whole-take bundle (a further 500 KiB, 831 KiB total) **only
   when `fullTakePlaybackViewAdmissible()`**, which is RC-MEM-2's landed half and must not regress when the
   capacity is reshaped.
3. **The readiness predicate and lag bound** — S4c.1 AC-3/AC-5; the capacity must be able to serve the interval
   the predicate is asked about, which is the requirement that keeps a *derived* capacity honest.
4. **The off-clock owner** — S4c.1 AC-6. A capacity change is an admission change, so it belongs at the cold
   capture-admission boundary that already calls `prewarmViews`, never on the MIDI or clock edge.

## 5. Owed, named (not claimed)

* **The product limit** — the value in `min(limit, windowEvents)`. Its own record said *"this is the decision to
  take"*; it is a user-facing admission limit (how much one interval may capture) and belongs to phase F or an
  approved product decision, not to a derivation. Nothing here presupposes a value.
* **`N` itself** — S2.3/S2.4 derive it against the envelope; `design.md` keeps it **provisional, bounded by
  phase F**. This document supplies the per-holder term they multiply.
* **C3(a)'s saturation claim, per slot** — `capacity = min(limit, committedEvents + intervalHorizon)` assumes a
  long take already needs the full horizon. That is a *measurement* (the per-slot `committedEvents` at the arm),
  and it belongs to **S2.2/S2.3**, together with the other three classes (`C`, `D`, `I`), which must not be sized
  by the playback target.
* **No code was changed, so no red-proof applies.** The formula this derivation sizes is pinned by
  `test/test_playback_prewarm/test_playback_prewarm.cpp`
  (`test_runtime_view_set_prewarm_has_fixed_capacity_and_no_growth` asserts `fixedExternalBytes()` against the
  same expression and asserts a full buffer refuses to grow), and `fixedExternalBytes()` recomputes from the
  `sizeof`s, so a capacity edit cannot silently change the figure without that fixture seeing it.
