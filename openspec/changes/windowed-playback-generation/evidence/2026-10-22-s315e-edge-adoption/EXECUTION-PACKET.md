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

**C1 — no role admission on this route.** With the live-frontier contract the overlay view is a **live source**, not a
`ready` successor: the lane never publishes it, never adopts it as `current`, and therefore never competes for a role with
the committed, preview or Record-return lanes. DEC-136's `ready` successor, the launch route and the preview lane are
untouched by construction. The candidate lives in the slot's `build` role (or the lane's private candidate slot) and is
identified by its mixed provenance shape + demand identity (2b-ii-b). If a future change wants it to own `ready`, that is a
different contract and reopens 2b-iii.

**C2 — edge binding (live source, no adoption).** `Track::resolveQualificationPlaybackSourceForTurn` (already `FLASHMEM`,
one call from the ITCM resolver) gains an overdub branch, after the NOTE_EDIT preview check and before the first-take Record
branch: bind the slot's overlay **live view** when it is

- the lane's candidate for the live demand (still in the `build` role; never adopted as `current`),
- `passOriginsReady` with the mixed provenance shape, `source` matching the live committed base and carrying the live
  capture generation for the turn,
- `readableEventCount > 0` (a published prefix exists; `complete` is **not** required — a newer tail may still be ordered),
- `coverage` contains the current phase and its loop length equals the loop's,
- the demand is still owed (`overdubOverlayDemandForSlot`).

On a match the resolver sets `rollingViewSelectedOut = true` and a new `overlayViewSelectedOut` (or an equivalent named
result) that the hot path uses to select the overlay stream + provenance send callbacks; the stream's size is
`readableEventCount`, not `storage.eventCount`. No role is taken, no new owner, no new queue.

**C3 — readability boundary, not an adoption boundary.** There is no atomic role handoff for this route: the immutable
thing is the published **frontier**, not the whole view. The edge may read the live view from the first turn its frontier is
non-zero; it never reads a row at or beyond the frontier; when the frontier advances, the existing cursor continues into the
new rows (rows whose ticks already passed are not replayed — they are served by the committed representation at the next
hand-off, which slice 4 owns). The echo is skipped only while this live source is selected; every other state keeps the
echo.

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

**Production delta — landed and measured (C5 slice):** `FixedPlaybackStreamCtx` gained
`const PlaybackPassEventOrigin* passOrigins` and `uint16_t lastOrigin`, and a **separate** adapter
`makeOverlayPlaybackStream` (flash-resident `overlayPlaybackStreamEventAt`) records
`passOrigins[storage->order[cursor]].pass` before returning the row. The shared `fixedPlaybackStreamEventAt` is untouched,
so every other fixed route is byte-identical. The first cut modified the shared adapter instead: the provenance store
grew **ITCM by 64 B** in the lane image (`391,848 + 1,368` → `391,912 + 1,304`); moving the shared adapter to flash
recovered the lane pin but shrank the default image (`391,688` → `391,624`). The dedicated flash adapter restores **both**
pins exactly (`391,688 + 1,528` / `391,848 + 1,368`), with no veneer (the walk reaches it through the stream's pointer).
Reordering `consume` before send (a shared semantics change for every route) and duplicating the advance loop (two
ordering/emission rules) remain **rejected**. Production wiring of the adapter (the send callback and the edge branch) is
C2/C4's, not this increment's.

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

## RESOLVED (owner, 2026-10-22) — the live reader with a frozen ordered frontier

**Decision:** the storage/view may remain append-stable while the **readable prefix is immutable**. The clock edge may
consume only `[0, readableEventCount)`; the owner advances that frontier atomically after Fill/Order completes, never
before, never backwards within one overlay generation, and never exposes the private tail. This replaces "adopt an
immutable complete window" for this route (option 2 of the question below) and preserves both goals: one emission reader
and near-current overdub audibility. It also avoids the `ready`-role question entirely: the overlay view is bound as a
**live source**, never adopted as `current`, so DEC-136's `ready` successor is untouched by construction.

**Landed so far (native, no audible change):**

- `RuntimePlaybackOrderBuilder::extendPrivateOrderPreservingPrefix()` — the delta lifecycle sorts the appended wave within
  itself and concatenates it after the frozen prefix, skipping `PrefixMerge`/`PrefixCopyBack`. Encoded as
  `FrozenDelta*` phases, **not a new field**, because the builder lives inside the pinned `LoopPlaybackRuntime`
  (both static_asserts and both RAM1 pins stay exactly `7 904`/`10 104`, `391,688 + 1,528` / `391,848 + 1,368`).
- `RuntimePlaybackView::readableEventCount` — the published ordered frontier (external view metadata; not the runtime pin).
  It is distinct from `complete`: a live window can be safely readable at N while a newer tail is still being ordered.
- `RuntimePlaybackViewBuild::publishReadableFrontier()` — one monotonic 16-bit store, only when the order completes and only
  for the overlay continuation. The first overlay continuation still merges canonically with the base (the prefix is not
  yet readable); every later wave preserves the prefix.
- Fixtures: `test_3_15e_slice3_prefix_preserving_extension_never_moves_a_published_prefix` (order level, including a
  same-tick Off/On wave and an earlier-tick wave) and
  `test_3_15e_slice3_the_readable_frontier_is_frozen_while_the_tail_is_ordered` (production lane: one owner turn starts the
  extension, the frontier and prefix are unchanged, `complete` is false; after ordering the frontier advances to the new
  extent and the prefix is byte-identical).
- Mutations that discriminate: force the full prefix merge (prefix moves), publish the frontier during the fill (partial
  tail exposed), and stop publishing at order completion (frontier never advances) — each RED exactly its case.

**Edge landed (2026-10-22; native, no audible change to the default image).**

- **C2:** `resolveQualificationPlaybackSourceForTurn` binds the lane's live view when it has a published frontier, the mixed
  provenance shape, a base-identity match and coverage containing the phase; it anchors the shared cursor with a
  logarithmic seek and sets `overlayLiveSelected`.
- **C3:** no adoption; the stream is `makeOverlayPlaybackStream` and its size is `readableCount` (the frontier), so the walk
  never reads the private tail.
- **C4:** while the live view is selected, the `ActiveCaptureOverdub` walk and `ensureCaptureEventsSorted()` do not run.
- **C5:** `playbackCursorAdvanceSendOverlay` / `playbackCursorAdvanceApplyLedgerOverlay` (flash, function-pointer calls)
  apply the ledger only to committed-origin rows.
- **New suite `test_overlay_edge`** (rolling 1, real hot path, captured two-bar world): W1 green — the live row is served
  once, the committed base pair once, the ledger stays closed, and the selection witness proves the route. Mutations that
  discriminate: re-enabling the echo under the window duplicates the live row; applying the ledger to overlay rows opens
  the ledger.
- **Budget:** default image stays `391,688 + 1,528`; the lane image moves **+64 B of ITCM** (`391,848 + 1,368` →
  `391,912 + 1,304`, still 464 B above the approved 840 floor) — the cost of the route selection/locals in the hot path.
  No floor or size pin is re-based; the move is recorded, not absorbed silently.

**Still to implement:** W2 (foreign `ready`/DEC-136 guard), W3 (pass-commit fallback at the seam), W6 (branch-off fallback
proof), W7 (named refusals).

**Fixture-home constraint (found while wiring the edge; the wiring was prototyped and reverted).**
`test_shared_capture_finalization` defines `PLAYBACK_WINDOW_ROLLING_ENABLED 1` **only** when
`UNIFIED_RECORD_RETURN_READER_ORACLE` is defined, and the build never defines that macro — so its default run is the
**echo** gate and the qualification resolver / hot-path overlay branch is compiled out there. The existing 3.15e fixtures
in that suite are correct for the echo path (and the slice-1/2b lane/frontier fixtures call the lane directly, which is
not gated). But W1/W3/W5/W6 must drive the **edge**, so they need a suite that

- compiles with `PLAYBACK_WINDOW_ROLLING_ENABLED == 1`,
- links the real `TrackPlaybackHotPath.cpp` (the resolver + walk + echo block) and the real window build,
- has the captured-overdub setup (a committed two-bar world, an overdub with rows, the canonical prerequisite, and the
  lane's owner turns).

`test_windowed_preview_integration` and `test_runtime_working_set_ownership` satisfy the first two; neither has the
finalization suite's capture-stop harness/setup, so the next unit must either port that setup into one of them or add a
dedicated `test_overlay_edge` suite. The edge prototype (resolver branch, live-view stream size, provenance send/ledger
callbacks, echo skip) is designed in C2–C5 and was reverted uncommitted rather than landed without its oracle. The
frontier and C5 increments stand on their own.

## The question this resolved (kept for the record) — post-adoption appends

The immutable-window contract and a **live** overdub pull in opposite directions, and the fixtures cannot be written
honestly until this is decided:

- `RuntimePlaybackViewFill::step` / `extendLiveCapture` refuse a **published** destination
  (`destination_->published`), and `RuntimePlaybackViewLifecycle::handoff` sets `views[current].published = true` on
  adoption (`src/Track/RuntimePlaybackViewFill.cpp`, `src/Track/RuntimePlaybackView.cpp`).
- So once the overlay window is adopted as `current`, a row the user plays afterwards **cannot enter the served window**.
  The lane's next grain is `Denied` (published) and it retires; the only candidate it can build is for the **next**
  interval, adopted at the next boundary.
- The echo exists precisely to make that row audible **now**. Replacing it with a complete-window reader therefore moves a
  live overdub note by up to one coverage interval (two bars, or the whole loop for a whole-take route) unless a new
  live-read contract is introduced.

Options (an owner decision; all other C1–C5 pieces are independent of it):

1. **Bounded-latency adoption (simplest, immutable contract kept).** Adopt only complete windows at interval boundaries;
   rows appended during the served interval are served by the next interval's window. Latency ≤ one interval; the echo is
   removed for adopted intervals. The 3.15e oracle's post-adoption timing must be rewritten to that contract.
2. **Live window reader with a frozen frontier (closest to today's latency).** The edge serves the `ready` overlay view
   while it is append-stable, emitting only rows below a recorded ordered frontier (`privateOrderedEventCount()` /
   `frozenSourceExtent()` already exist); the lane extends and re-orders off-clock, and the frontier advances when the
   extension completes. This needs an explicit read contract (frozen prefix, atomic frontier, what happens on a pass
   commit) and a proof that a partially ordered tail is never read. It is a new contract, not a tweak.
3. **Committed-base window + echo for the live tail (hybrid).** The window carries the committed base, and the echo keeps
   sounding rows appended since; the tail's ordering can move off-clock. Two emission sources remain, so the "single
   reader" clause is only partially met.

**Recommendation: option 2 if the clause's one-reader requirement is non-negotiable, otherwise option 1 with an explicit
latency contract.** Option 3 should be chosen only if the owner accepts that the live tail keeps its own reader. No
fixture for W1/W3 is written until this is settled; C5 (the provenance mechanism) is independent and has landed.

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
