# Slice 2 parity gate — measured RED and the blocker it names

**Date:** 2026-10-29
**Change:** `realtime-idle-owner-admission-bound`
**Suite:** `test/test_overdub_source_window_parity` (cases RIAO-2.A–E plus characterizations X/X2/Y)
**Status:** RED by design. Not a pass, not a product claim.

## What was built before this run

- `include/Utils/MidiServiceDrain.h` — `kOwnerWorkBudgetUs` (25 ms) vs `kIdleMaintBreachThresholdUs`
  (50 ms), the `IdleOwnerChild` registry with declared maxima and finer grains, `admitIdleOwnerChild`,
  `IdleOwnerTurnBudget` (reserve-before-dispatch, charges the declared maximum), and
  `idleOwnerSourceViewCovers` (the coverage rule from correction 1).
- `test/test_realtime_idle_owner_admission` — **10/10 PASS**. Packet `RIAO-1` cases A–F, including "a
  child that cannot fit is not dispatched", "a finer grain takes the remaining deadline", the
  aggregate-turn bound over all permutations, and the coverage rule.
- `test/test_overdub_source_window_parity` — the D3/D3a gates, borrowing the live-capture display
  boundary closure so it runs production `LoopContentResolution` over production `Loop` state.

## Result (focused run, 9 cases / 5 pass / 3 fail / 1 discriminating pair)

```
A  ordinary window matches whole loop       FAILED  ws=1536 expected=2 windowed=0
B  wrap window keeps the wrap note          PASSED  (only with a loop-aware pad)
C  same-pitch twins keep identity           FAILED  record pass twin must survive
D  assembled cache matches whole loop       FAILED  Expected 8 Was 6
E  boundary crosser appears exactly once    PASSED
X  chunk span is append-order endpoints     PASSED  (characterization)
X2 a pass ending near its own start         PASSED  (characterization)
Y  mid-loop window skips that chunk         PASSED  (recorded defect)
```

## Finding 1 — the pad must be loop-aware (already absorbed)

With the visual-cache slice's clamped pad (`startBar > padBars ? startBar - padBars : 0`) the wrap
window returned **0 of 3** notes: a wrap note whose note-on lies outside the window can never be
paired. With a modulo pad, case B passes. Recorded in `design.md` D3 as a consequence.

## Finding 2 — the blocker: a chunk's advertised span is not a bound on its content

`CommittedEventRange::inWindow` admits a chunk from its `[firstTick, lastTick]`, treating
`firstTick > lastTick` as a wrap span. `LoopEventStore::appendToTailChunk` sets `firstTick` from the
**first appended** event and `lastTick` from the **last appended** event (`src/LoopEventStore.cpp`,
lines 647–650) — append order, not content extent. `chunkTickSpan` publishes the same pair.

A capture pass stops being tick-monotonic at its **first loop wrap**, so the pair stops being a bound.
Three shapes were measured directly:

| shape | first/last appended | advertised span | sound? |
|---|---|---|---|
| monotonic | 1000 / 2100 | `[1000, 2100]` | yes |
| ends after the wrap | 1000 / 100 | wrap `[1000,6144)+[0,100]` | yes — over-covers |
| **starts and ends near the loop top** | **96 / 100** | **`[96, 100]`** | **no** — content reaches 6044 |

The third shape is the failing one, and it is ordinary: *start an overdub at the top of the loop, stop
just after the next top*. Measured consequence (`RIAO-2.Y`): `inWindow` over `[1800, 2600)` returns
**0 events** from a chunk that holds a note at 2400, while `CommittedEventRange::full` returns all of
them. The content is present; the admission hides it.

The parity fixture reproduces exactly this: its record pass appends `(100,300) … (6044,100)`, so
`firstTick == lastTick == 100`, and the mid-loop window returns **0 notes**. A, C and D are all this
one cause (D is A assembled over four windows).

## Why this stopped the slice

Design D3 forbids refreshing the source view from a windowed derive that can under-report content, and
correction 1 forbids shipping an image where the view is deferred against stale coverage. Wiring
`rebuildOverdubSourceView` to `resolveDisplayNotesInWindow` today would trade a MIDI stall for a
**wrong overlap source** — the failure mode is a missed overlap, i.e. a corrupted take, not a slow
frame.

**No production file outside the header contract has been changed.**

## Candidate fixes, and why none was applied here

1. **Min/max span** (`firstTick = min`, `lastTick = max` in `appendToTailChunk`). Sound, three lines,
   but a chunk containing a wrap pair then advertises almost the whole loop and is copied into every
   window — it moves the cost problem rather than solving it, and that cost lands on the display path
   that also uses `inWindow`.
2. **Split the chunk at the wrap** (seal when the appended tick decreases). Keeps spans tight; adds
   one sealed chunk per wrap per pass, and changes chunk cardinality, which persistence
   (`admitSealedChunk`), memory and every consumer can observe.
3. **Admit by bar mask** — the store already keeps `captureBarMask` per 32-event group; admit a chunk
   when any group's mask intersects the window. Tightest, but a storage-layer API change.

All three are storage-owned. Per this change's own design rule, that is its own slice with its own
packet; choosing between them is a scope decision, not an implementation detail.

## Not claimed

- No device run and no firmware claim for the parity work.
- `A/C/D` are attributed to Finding 2 by direct characterization (`X2`, `Y`), not by inspection alone.
- Whether real device takes hit the failing shape is **argued, not measured**: the shape is ordinary,
  but no device capture has been decoded for it yet. The owner capture that started this change
  (`session_20261029_190227.log`) shows `cer_win,span=17,copy=3574,evcopy=998` — a 17-chunk window
  copying 998 events — which is consistent with over-copying, not with under-admission; that capture's
  take may simply not hit the shape.
