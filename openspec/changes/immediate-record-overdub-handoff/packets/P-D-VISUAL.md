# P-D — bounded visual continuation

G2 execution packet for [CONTRACTS.md](../CONTRACTS.md) C5.1–C5.5.
Index: [packets](README.md). Template: [EXECUTION_PACKET](../../../../docs/Templates/EXECUTION_PACKET.md).

## Identity and scope

- **Packet / task IDs:** P-D. Implements C5 — reachable bounded visual continuation. OpenSpec spec:
  `realtime-visual-cache` — *Real-time visual reconstruction has bounded indivisible work* and
  *Deferred visual work preserves exact content and coherent presentation*. **DEC-159** (visual half
  of the direction); [execution bounds](../../../../docs/Authority/Architecture/ExecutionBounds.md) rows
  5, 6, 7, 8, 9, 10.
- **Status:** specified; fixture demonstrated (named RED). **Step 1 attempted and reverted** — the
  continuation must be bounded first (C5.2a); nothing is implemented or measured.
- **Owner and path:** `Loop::rebuildVisualCacheIdleSlice` (visual cache) decides what to serve;
  `LoopContentResolution`'s prepared-index session (`sDeviceGateSession`) is the continuation and is
  stepped as `deviceGateRunOneSlice`; `Track::processDeferredIdleMaintenance` is the service entry that
  must run the producer grain **before** the slice so the slice can find a prepared window;
  `DisplayManager` remains the presentation owner.
- **Primary files:** `src/Loop/LoopVisualCache.cpp` (attempt order, deferral token),
  `src/Track/TrackDeferredMaintenance.cpp` (producer wakeup and one-grain ordering),
  `src/LoopContentResolution.cpp` + `include/LoopContentResolution.h` (session qualification and the
  dirty-window policy), `docs/Authority/Architecture/ExecutionBounds.md` (row reclassification, C5.4).
  Harness: `test/test_immediate_visual_cache_probe` (existing bound RED),
  `test/test_live_capture_display_boundary` (controls).
- **Preconditions already present:** the prepared-index session with phases
  `Idle|IndexCommit|Window|RebuildPrepare|RebuildSpans|RebuildCheckpoints|State|Done`, per-phase cursors,
  `kDeviceGateEventsPerSlice = 8`, the stamped result (`preparedIndexKept`,
  `preparedPlaybackRevision`), and the public gates `preparedWindowReady(playbackRevision)` /
  `tryResolvePreparedWindow(...)` — already called by the slice. **Prerequisite in this packet:** a
  production-qualified wakeup (C5.3). **External blockers:** none for native work; device timing is a
  separate gate.
- **Mutation (C5.1–C5.4):**
  1. fix the attempt order so a source miss cannot select the whole-pass `resolveDisplayNotes(passes, …)`
     route, and make the deferral visible as `slice_deferred:<reason>`;
  2. make the prepared-index producer reachable from the derived-view branch: one phase step per turn,
     admitted for the owning track (not only the selected one), with the dirty-window case in
     `deviceGateDirtyPolicy` replacing the clean-cache precondition — named predicates, not deleted checks;
  3. keep `preparedWindowReady` keyed by `playbackRevision` and `deviceGateMatchesLoop` keyed by
     `(trackIndex, loopIndex)`; a request for another loop is a qualified reset, never a second index;
  4. reclassify rows 5 and 8 and add a row for the new producer grain in the same commit.
- **Non-goals:** no second prepared index or reader, no budget increase, no bars-per-slice reduction
  passed off as a bound, no µs budget treated as a structural bound, no display-authority change, no
  capture-custody change.
- **Commit boundary — two independently buildable commits:**
  1. **Reachability + ordering** (producer admitted and stepped, slice still falling through to the old
     fallback when no window is ready). No class promotion yet; rows unchanged.
  2. **Attempt-order switch + row reclassification**, which removes the whole-pass route and updates the
     inventory in the same commit (bounds rule 3).

## Step 1 amendment (2026-10-25) — BLOCKED on bounding the continuation

Step 1 was implemented, built at unchanged RAM1 pins, and then **reverted** before shipping: the
prepared-index session is not live-lane-safe. Eleven of its steps are `indivisible = true` and
content-sized (two whole-index sorts plus whole-vector copies), and `kDeviceGateSliceBudgetUs` is never
read, so it is documentation rather than a bound. Promoting it to the derived-view lane would put
whole-pass sorting on the MIDI lane — the defect this packet exists to remove, and what C5.4 forbids.

[CONTRACTS C5.2a](../CONTRACTS.md) therefore re-scopes step 1: **bound the continuation's indivisible
steps first**, then enable it. Evidence and the two forward options:
[producer bound](../evidence/PD-PRODUCER-BOUND.md). Step 2 (attempt order + row reclassification) is
unchanged and still cannot run before a live-lane-safe producer exists.

## Oracle fixed before behaviour edits

| Case | Initial state | Ordered steps | Expected after each step |
|---|---|---|---|
| `P-D-1` `test_desired_bound_source_miss_must_not_resolve_whole_passes` | background track: 33 bars, 17 passes, 1,584 input events, no root, no prepared index; selected track Recording | one real-time slice with `fallbackGatherEventBudget`, then drain turns | the slice does **not** report `PassesResolve` for the miss; it either serves a prepared window or defers with a named reason; the previous complete frame stays authoritative |
| `P-D-2` eventual progress | as P-D-1 | drain with the producer admitted | the exact independent inventory is reached (every identity once, exact pitch/velocity/start/end and cardinality), coherent at every observed frame |
| `P-D-3` per-turn unit | as P-D-1 | one producer turn | at most one phase step and at most `kDeviceGateEventsPerSlice` (8) appended index/span rows |
| `P-D-4` cancellation | producer mid-flight | bump `playbackRevision`, or change `loopLengthTicks`, `loopId`, active slot or `playbackGeneration` | partial candidate discarded; previous complete frame unchanged; no mixed publication |
| `P-D-5` no deadlock | producer cannot be admitted this turn | one slice turn | the refusal is visible (`slice_deferred:<reason>`) and no whole-pass work runs |

- **Completion / exactly-once:** publication stays a single `++visualCache.revision` edge; a repeated
  turn after completion changes nothing.
- **Preserved invariants:** `builtFromRevision`/`builtFromSnapshotIdentity` stay gated by
  `visualCacheGatherCoversPlaybackRevision`; the existing held-frame and moving-window controls stay
  GREEN; capture input is not delayed by visual work.
- **Counterexample that must fail:** the current bound RED — "one-bar budget=1 still selects whole-pass
  `resolveDisplayNotes`; bounded producer absent".

## Controls and coverage

- **Seams reused:** the probe's real `Loop`/`Track`/content/edit/display closure with native clock,
  allocator and physical-display boundaries; `CommittedChunkIdTestHelpers.h` for ownership transfer.
  No copied resolver.
- **Production paths asserted:** `rebuildVisualCacheIdleSlice`, `tryResolvePreparedWindow`,
  `deviceGateRunOneSlice`, the derived-view maintenance branch, `adoptPartialVisualCacheNotes` revision edge.
- **Cases beyond the oracle table:** dense 33-bar fixture with 8 pairs/bar Record and 16 committed
  overdubs; length shortens id1000 to [12,40); Delete hides id1001; revision replacement between slices
  (33 identities removed) reaching the 758-note inventory; overlay rejection; a clean-cache track as a
  no-op control.
- **Independent expected content:** constructed from the seeded identities and explicit endpoints, not
  from another resolution algorithm. Native work counts are counts, not timings.
- **Numbers:** `kDeviceGateEventsPerSlice = 8`; `fallbackGatherEventBudget` 96/192 as already passed by
  the callers. No µs threshold is asserted.
- **Untestable here:** device input-service gap and output lateness — a separate authorised stimulus.

## Verification recipe

```sh
pio test -e native -f test_immediate_visual_cache_probe
IMMEDIATE_VISUAL_CASE=bound pio test -e native -f test_immediate_visual_cache_probe
pio test -e native -f test_live_capture_display_boundary
pio test -e native                     # expect 3,454 total / 7 skipped / 0 failed
pio run -e teensy41-capture-serial     # build only; no upload
```

- **Pass criteria:** `IMMEDIATE_VISUAL_CASE=bound` exits with the named assertion GREEN only after step 2;
  the unknown-selector exit-2 behaviour is preserved; full native 0 failed; shipping build succeeds and
  the RAM1 delta of the producer is attributed (the session already exists; only its reachability changes).
- **Device:** NOT RUN. Recording-start contention, input-service gap and output lateness need a
  registered stimulus and separate authorization.
- **Evidence path:** `evidence/PD-VISUAL-EXECUTION.md`, plus the bounds-inventory diff in the same commit.

## Completion record

- Oracle/contract unchanged: pending
- Fixture implemented and discriminating assertion demonstrated: **PASS** for `P-D-1` —
  `IMMEDIATE_VISUAL_CASE=bound pio test -e native -f test_immediate_visual_cache_probe` exits1 with
  `EXPECTED RED: one-bar budget=1 still selects whole-pass resolveDisplayNotes; bounded producer
  absent`, after the exact first-bar content check passes
  ([fixture receipt](../evidence/PACKET-FIXTURES.md)). `P-D-2..P-D-5` need the producer to be
  reachable, so they are not yet expressible.
- Native verification: NOT RUN
- Build/RAM1 verification: NOT RUN
- Device verification: NOT RUN (out of scope)
- Required reviews and durable docs: NOT RUN (ExecutionBounds rows 5, 8 + new row are part of step 2)
- Task IDs eligible for checkoff after all of the above: none alone; P-D is the implementation half of
  P1 task 1.6's contract, and 2.2 still requires P-C
