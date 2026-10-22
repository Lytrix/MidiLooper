# 3.15e — slice 3 execution packet: edge adoption, echo suppression and the provenance ledger

**Change:** `windowed-playback-generation` · **Task:** 3.15e (single-reader migration for the overdub overlay) ·
**Branch:** `feature/unified-rolling-playback-path` · **Code base:** `8e58f78a` (docs base `8c941afc`) · **Design:**
[2026-10-22 addendum](../../ARCHITECTURE-REASSESSMENT.md#2026-10-22-addendum--the-live-overdub-overlay-moves-onto-the-single-window-reader-315e-dec-116-extension),
slice 3 of its queue. **Status: designed, not implemented.** No code, no build, no device leg in this packet.

## The owner's decision this packet records

**2b-iii (three-way role arbitration) is deferred.** It was attempted and withdrawn on 2026-10-22: the only committed
demand reachable during an overdub is DEC-136's private Record-return obligation, and ordinary convergence is cancelled at
overdub start, so the priority contract currently has **no production consumer**. The Record-return and launch routes stay
untouched. Slice 3 must therefore adopt the overlay window **without** asking the three-way resolver: it uses the
lifecycle's own one-successor rule and never defers, retires or clobbers another owner's `ready` candidate. Arbitration is
reopened only if this slice exposes a **real competing production claim** (see *Risks*).

## What is already true (preconditions, each from a landed slice)

| Fact | Where |
|---|---|
| One build identity carries a committed base (from borrowed `RuntimeCompanionPasses`, with per-row provenance) and an append-stable live overlay, ordered off-clock | 3.15e slice 1; `RuntimePlaybackViewFill::beginLiveContinuation` / `RuntimePlaybackViewBuild::beginLiveOverlayContinuation` |
| Overlay rows carry `kLiveOverlayPassOrigin` in `passOrigins` and `kLiveOverlayCanonicalRank`; committed rows keep their own pass index; the window reports `passOriginsReady` with `canonicalRanksReady == false` | slice 1 |
| The lane's owner turn builds, extends and retires that candidate, and releases the borrowed companion instance as soon as the base is copied; a stop/wrap boundary retires it first | 2b-ii-b step 2 (commit `8e58f78a`) |
| The stopped oracle refuses to build underneath a base-current candidate and retires a stale one inside the call | 2b-ii-b step 2 |
| The live overlay is still served by the **second walk** (`ActiveCaptureOverdub` over `loop.capture.store`) after the committed walk, and its turn runs `ensureCaptureEventsSorted()`; the echo is emit-only | `src/Track/TrackPlaybackHotPath.cpp` (the `TRACK_OVERDUBBING` block) |
| The 3.15e emission oracle (`test_3_15e_a_live_overdub_is_served_exactly_once_through_the_wrap_and_the_pass_handoff`) pins today's exact stream through the wrap and the real stop | `test_shared_capture_finalization` |
| The occupy ledger is committed-only (`221334`/`224719`); the echo never applies it | `playbackCursorAdvanceSendCapture` vs `playbackCursorAdvanceSend` |
| Nothing consumes the built overlay window yet; the `ready` role is normally free during an overdub because ordinary convergence is cancelled at overdub start | 2b-iii withdrawal evidence |

## Contract for slice 3

**C1 — role admission without arbitration.** A completed overlay candidate is published as the slot's `ready` successor
(`RuntimePlaybackViewLifecycle::publishCompletedBuildAsReadySuccessor`) **only when the role is free** and the candidate is
still current for its demand (`candidateBaseCurrent`, unchanged from 2b-ii-b). If `ready` already holds a complete view
of any origin — a Record-return or launch successor, the preview — the lane keeps its candidate in `build` and returns a
**named** outcome; the edge then keeps the echo. It never retires, replaces or ages another owner's `ready` candidate, and
it never asks the three-way resolver. This is the one-successor rule the lifecycle already enforces, applied by the lane
that owns the candidate.

**C2 — edge binding.** `Track::resolveQualificationPlaybackSourceForTurn` (already `FLASHMEM`, one call from the ITCM
resolver) gains an overdub branch, after the NOTE_EDIT preview check and before the first-take Record branch: bind the
slot's overlay window when it is

- in the `ready` or `current` role (the edge consumes only an adopted or adoptable window),
- `published && complete && sourceFilled && passOriginsReady`,
- `source` matches the live committed base and carries the live capture generation for the turn,
- `coverage` contains the current phase and its loop length equals the loop's,
- the demand is still owed (`overdubOverlayDemandForSlot`).

On a match the resolver sets `rollingViewSelectedOut = true` (the same flag the preview lane uses) so the Record-lane
ownership logic is unchanged, and the committed walk serves the window. No new owner, no new queue.

**C3 — adoption boundary.** The window is adopted through the existing atomic handoff at a **boundary where the echo has
not already served the same rows**. The safe rule for this slice:

- adopt at the window's interval start (whole-loop windows: phase 0 of the next pass; long-loop windows: the interval's
  `startPhase`), never mid-interval after the echo has emitted rows from the same region;
- carry the window's own `cursorSeed` like every other adopted window, so the first served row is the one after the
  boundary;
- if the boundary has already passed when the candidate completes, **wait for the next one**; the echo keeps serving the
  remainder of this pass.

This rule is deliberately conservative. It is the one behavior the fixtures must pin before any implementation claim; a
mid-interval adoption that can duplicate an echoed row is a rejected option, not an optimization.

**C4 — echo suppression while the window serves.** On a turn where C2 selected the overlay window, the
`ActiveCaptureOverdub` walk **and** its `ensureCaptureEventsSorted()` do not run. The committed walk (which now walks the
window) is the only traversal; the overlay rows are already in that window, in canonical order. When the window is not
selected (no candidate, role occupied, identity stale, default image), today's echo walk and sort run **unchanged**. The
echo stays the named compatibility fallback until 3.7b removes the emitter.

**C5 — provenance-based ledger.** The window route applies the occupy ledger **only to committed-origin rows**:
`view.passOrigins[storageIndex].pass != kLiveOverlayPassOrigin` → ledger and send; `== kLiveOverlayPassOrigin` → send
only. This preserves `221334`/`224719` (occupy reads committed entries only) and makes the clause's "exactly once"
true in the strong sense: each committed note is ledgered once, each overlay row sounds once, and the pass that later
contains it is ledgered when it becomes committed content — never twice. The echo's emit-only path is untouched.

**Gating probe — how the emission callback learns a row's origin: PASSED.** Native probe
`test_3_15e_slice3_probe_row_origin_is_visible_at_emission_time` (`test_playback_prewarm`) drives the **real**
`advancePlaybackCursor` over a real slice-1 overlay window with a probe stream whose `eventAt` records
`view.passOrigins[view.storage.order[cursor]].pass` into the send context. Measured: **5 rows, 5 `eventAt` calls** (one
traversal, no second pass), **2 committed / 3 overlay** emissions, the origin observed by the send callback is the row
being sent (asserted against `storage.order`), and **0 overlay rows ledgered** while both committed rows are. The
`consume`-after-send ordering is therefore not usable, and no reordering is needed.

**Production delta this fixes (no new function, no traversal):** extend `FixedPlaybackStreamCtx` with
`const uint16_t* passOrigins` and `uint16_t* originOut`; in `fixedPlaybackStreamEventAt`, when `passOrigins != nullptr`,
write `*originOut = passOrigins[storage->order[cursor]]` before returning the row. The window route passes a send context
that reads `originOut`; every other fixed route leaves the pointer null and pays one null check per row. The alternative —
a dedicated `makeOverlayPlaybackStream` with its own `eventAt` — avoids the check on other routes at the cost of one more
flash function target; either is compatible with the probe, and the implementation slice must pick one with the measured
edge cost. Reordering `consume` before send (a shared semantics change for every route) and duplicating the advance loop
(two ordering/emission rules) are **rejected**. The probe is native-only; the ITCM/flash placement of the modified
`eventAt` (already `TRACK_INTERNAL_MEM`, flash) is verified at the implementation slice's build gate, not claimed here.

**C6 — hand-offs.** A wrap pass commit or a deferred stop invalidates the candidate exactly as 2b-ii-b already does
(`nextGrain() == Denied` → retire). If the edge is serving the retired window when the identity moves, the resolver's C2
match fails on the next turn and the echo serves the remainder of the pass. The full wrap-held continuation (a NoteOn
before the wrap whose Off is after it, served once through the hand-off) is **slice 4**, not this packet; this slice must
not duplicate a row at the seam, and the slice-4 fixtures own the positive exactly-once proof there.

**C7 — default image.** `PLAYBACK_WINDOW_ROLLING_ENABLED == 0` compiles C1–C6 out; the default image's behavior and both
RAM1 pins must be unchanged by construction (`MAIN` branch only in the qualification build). The lane image keeps the echo
for every state C1–C2 do not select.

## Oracle (write the fixtures before the code)

| # | Case | Expected |
|---|---|---|
| **W1** | live overdub with one note before the wrap, canonical base ready, `ready` free, candidate complete | the candidate is published as `ready`; the edge binds it and adopts at the next interval boundary; from adoption the **committed walk** serves base + overlay once each in canonical order with **no echo row** on those turns (`ActiveCaptureOverdub` not entered, sort count 0); the stream equals the 2026-10-22 oracle's post-adoption segment |
| **W2** | `ready` already holds a complete non-overlay successor (a Record-return/launch/preview shape) | the overlay candidate stays in `build`, the lane returns its **named** deferral, the echo serves, the foreign successor is untouched (complete, same source) |
| **W3** | pass commit (wrap) while the window serves | the candidate is retired on the lane's next turn; the edge falls back to the echo for the rest of the pass; the committed reader serves the newly committed pass once; no row is emitted twice at the seam |
| **W4** | ledger | every committed-origin row applies the ledger exactly once; every overlay row is sent with **no** ledger application (`openCount` unchanged by overlay rows); after the pass is committed, its rows ledger once as committed content |
| **W5** | mutation: re-enable the echo walk while the window is selected | duplicates the overlay rows and fails W1/W3 (RED) |
| **W6** | mutation: force the overlay branch off | the echo stream returns; the existing 3.15e oracle stays green on the fallback (the echo is a real fallback, not dead code) |
| **W7** | capacity/identity refusal (candidate stale, role occupied, coverage mismatch) | the named refusal is reported; echo serves; no partial window is adopted |

## Verification recipe

- Focused: `pio test -e native -f test_shared_capture_finalization` (new W-cases + the 3.15e oracle twice: window route and
  forced-fallback route), `-f test_windowed_preview_integration` (role occupancy by the preview shape),
  `-f test_playback_prewarm` (the fill/build contract and the origin probe stay green).
- Full native (`pio test -e native`), both firmware builds (**default pins must stay byte-identical**; lane pins must not
  move without a measured re-base), `openspec validate windowed-playback-generation --strict`, `git diff --check`.
- Mutations: W5, W6 and one ledger mutation (apply the ledger to overlay rows → W4 RED).
- Device: **not this slice** (3.15f); the qualification image's route comparison consumes this evidence later.

## Risks / open questions (each must be answered in the slice's execution record)

1. **Row origin at emission (C5's mechanism) — probe PASSED, see above.** The data flow is proven on the real walk; the
   remaining choice is the production shape (shared `eventAt` + null check, or a dedicated overlay stream) and its measured
   edge cost. The ledger split no longer waits on feasibility.
2. **Adoption phase vs the echo prefix (C3).** The next-boundary rule is safe but may leave a full pass on the echo after
   completion. A mid-interval rule is only acceptable with a proof that the window's cursor seed excludes every echoed
   row; do not infer it.
3. **Role occupancy by DEC-136.** The Record-return successor can hold `ready` while an overdub is live. C1 must leave it
   alone (no retirement, no deferral of its lane). If a future change wants the overlay to own `ready` in that state, that
   is a real competing claim and reopens 2b-iii — not this slice.
4. **Long-loop coverage.** For a demoted/long loop the window covers the committed route's own interval; C2/C3 must use
   that interval's `startPhase`, not phase 0, and the echo's phase offsets must agree. The wrapped-coverage fixture is a
   required case.
5. **RAM1/ITCM.** The resolver branch is `FLASHMEM`; the origin hook (if added) must be data-only (a pointer/context), no
   new ITCM function and no per-row call on the hot path beyond the existing send. Both pins stay the approved values.
6. **No silent change to DEC-136.** The return obligation's preparation, retirement and stop admission are untouched by
   this slice; W2 is the guard.

## Explicit exclusions and rollback

- No three-way arbitration, no committed deferral, no change to launch/Record-return/stop routes.
- No echo removal, no emitter change, no pass-hand-off ownership (slice 4), no device claim (3.15f), no DEC yet (the DEC
  lands with the emission rule when it ships).
- Rollback: delete the resolver branch and the echo-skip condition; the echo walk and sort are still present and are
  today's behavior. No data or schema is touched.

## Definition of done for this slice

W1–W7 green through production entry points, the 3.15e oracle green on **both** routes, focused + full native + both
builds at the pins, strict valid, diff clean, the gating mechanism probe recorded, and an explicit statement of which
hand-off cases are slice 4's. A checkbox does **not** follow (3.15e stays open until the emitter/parity work).
