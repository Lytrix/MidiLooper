# 3.15e — slice 4 execution packet: the wrap and stop hand-offs (exactly once at the seam)

**Change:** `windowed-playback-generation` · **Task:** 3.15e · **Branch:** `feature/unified-rolling-playback-path` ·
**Base:** `b50df105` · **Design:** the
[2026-10-22 addendum](../../ARCHITECTURE-REASSESSMENT.md#2026-10-22-addendum--the-live-overdub-overlay-moves-onto-the-single-window-reader-315e-dec-116-extension),
slice 4 of its queue. **Status: designed, not implemented.** W3 (the pass-commit seam) is deliberately moved here from
slice 3.

## Scope

The live-overlay window now serves while the overdub is live (slice 3, W1). Slice 4 owns what happens at the two
boundaries that change the committed base underneath it:

1. **The wrap pass commit** — `Loop::commitOverdubWrap` seals the live rows into a pass, advances the playback revision
   and re-bases the capture. The window's base identity moves; the candidate retires; the committed reader serves the
   newly committed pass; the echo serves the new live tail.
2. **The deferred stop** — the capture freezes, the stop job commits the final pass, and the live view retires. The
   committed representation serves; the echo is off; the ledger closes.

Plus the **wrap-held continuation**: a NoteOn in the old pass whose NoteOff is in the new pass.

Nothing audible is added to the default image; the echo stays the fallback.

## The contract to pin

**H1 — wrap commit mid-overdub.** On the wrap turn:

- the window's frontier stops at the last fully ordered row; no partial tail is exposed;
- the candidate is retired by the lane's identity rules (`nextGrain() == Denied` on the re-based capture, or the base
  revision mismatch) — retirement precedes any new build;
- the committed reader serves the **new committed pass** exactly once (including the rows that were live before the wrap);
- the echo serves the rows appended **after** the wrap (the new tail) exactly once;
- no row is replayed from the old pass by the echo (its cursor is not rewound), and none is emitted twice at the seam.

**H2 — deferred stop.** When the stop is accepted, the lane releases the borrow and retires the candidate (the stop
boundary already does this, 2b-ii-b). The committed/stop representation serves the final pass; the echo is off; the
ledger closes with no open committed note. No part of the seam may wait on the lane.

**H3 — wrap-held continuation.** A NoteOn inside the old pass with its NoteOff in the new pass is committed in the old
pass. In the new pass the committed content carries the On (from the pass) and the Off at its own tick; the note sounds
once and the ledger closes once. The held pair is never emitted twice (once from the window, once from the echo).

**H4 — route switching at the seam.** The resolver selects the live view on the **base revision + playback generation**,
so the commit stops the binding on the next turn. The echo's cursor is untouched while the window serves; when it resumes,
it must not re-serve rows from the served interval (the tick filter drops past ticks, but the fixture must prove no
duplicate). A re-base resets the capture store; the new tail starts there.

**H5 — ledger.** Committed-origin rows apply the occupy ledger exactly once; overlay rows never do (`221334`/`224719`);
the pass's rows ledger when they become committed content — never twice.

## Fixture problem to solve first (the honest blocker)

The slice-3 W3 attempt drove the production clock from `kLoopTicks+1056` to `+1536` (the shape the 3.15e echo oracle
uses) and the pass **did not commit**: `loop.passes.overdubPasses.size()` stayed unchanged. The wrap requires
`Loop::shouldCommitOverdubWrap`, which needs `overdubWrapArmed_` and a `didPlayheadCrossPhase(prev, current, 0, length)`
frame. In the new suite the frame/cursor setup around the candidate build (or a suppression from the session start) is
not matching the oracle's. **Diagnosed 2026-10-22 (no code change):** the suite's **first** `playMidiEvents` re-anchors
`projectionCycleStartTick` to **1536** (the transport-start projection edge), while the 3.15e echo oracle runs from a
different projection history. `overdubWrapArmed_` is true after leaving the start phase, but the wrap frame's
`prev/current` phases no longer describe the oracle's crossing, so the pass does not commit at `+1536`. Test-only Loop
witnesses (`overdubWrapArmedForTest`, `overdubWrapSuppressNextForTest`) were added for the diagnosis and remain available
to the H-fixtures. **This must be diagnosed and driven honestly before any H-fixture is written** — do not force
`commitOverdubWrap()` from the test, and do not fake the revision. The H-fixtures must first establish a stable
projection **and** the same pre-overdub frame history the oracle has: a probe that primed one committed turn (so the
projection start was 0) and then ran the same 1056→1504→1536 sequence **still** did not commit, so the remaining difference
is the frame/cursor state at the crossing (`lastTickInLoop`/`playheadPhaseTick` and the `didPlayheadCrossPhase` inputs),
not the projection start alone. The next probe should witness those three values on the 1504→1536 frame and compare them
with the echo oracle's exact setup before writing H1. If the production clock path cannot reach the wrap in this harness, the slice is
blocked on a harness fact, not on the design.

## Oracle (write the fixtures before the code; H1/H3 first)

| # | Case | Expected |
|---|---|---|
| **H1** | live view serving, wrap commits the pass | candidate retired; committed reader serves the committed pass once; echo serves the new tail once; no row emitted twice at the seam; ledger closes |
| **H2** | deferred stop while the live view serves | candidate retired, borrow released; final pass served once as committed; echo off; ledger closed; stop admitted |
| **H3** | held NoteOn before the wrap, Off after | On committed in the old pass; new pass serves On (committed) + Off at its tick once; ledger closes once; no duplicate |
| **H4** | route switch at the commit | resolver stops selecting on the revision move; echo cursor not rewound; no old-pass replay |
| **H5** | ledger through the seam | committed rows exactly once, overlay rows never, the sealed pass's rows once as committed |
| **H6** | mutation: echo re-enabled while the window serves | duplicates at the seam (RED) |
| **H7** | mutation: the lane does not retire at the commit | the stale window continues to serve / is selected on the old identity (RED) |

## Verification recipe

- `pio test -e native -f test_overlay_edge` (new H-fixtures), `-f test_shared_capture_finalization` (the echo oracle and
  the 2b-ii-b wrap retirement stay green), `-f test_playback_prewarm` (build/frontier contract).
- Full native, both firmware builds (default pin exact; the lane pin is already `391,912 + 1,304` and must not move
  without a measured record), strict validation, `git diff --check`.
- Mutations H6/H7 plus one ledger mutation.
- Device: **3.15f** (no physical claim here).

## Risks

1. **The wrap trigger in native** (above). This is the gating item.
2. **Cursor/echo hand-off** — the echo's cursor is not advanced while the window serves; the first resumed turn must not
   re-serve a past tick. The tick filter is the guard, but the fixture must prove it.
3. **The stop's finalization** — the deferred stop must never wait on the lane (2b-ii-b's boundary release); slice 4
   re-proves it with the window serving.
4. **Held-note ownership** — the committed pass and the live tail both know the note; only one may emit each row.
5. **Ledger at the seam** — the pass's rows transition from overlay-origin to committed-origin; the applied-once rule
   must not double-count.

## Rollback

Delete the seam assertions and any resolver branch change; the echo and the committed reader are today's behavior. No
data or schema is touched.

## Explicit exclusions

No 2b-iii arbitration, no emit-path adoption beyond what slice 3 landed, no device claim, no DEC change (the emission
rule's DEC stays with the committed cutover).
