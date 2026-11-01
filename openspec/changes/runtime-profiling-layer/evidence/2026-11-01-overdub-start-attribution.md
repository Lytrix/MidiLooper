# 2026-11-01 — Where the 66 ms overdub-start edge goes (profiler attribution)

**Question.** Slice 4 measured `OverdubStart_body` (whole `Track::startOverdubbing`) at **66 ms avg / 159 ms max per
edge**, 12 edges per owner chain. Which part of the start owns it?

**Method.**
- Profile env only, read-only scopes, no behaviour change. Child identities are appended (append-only numbering):
  - 15–21, the heavy calls inside `startOverdubbing`: handoff, staging, workingSet, memAdmit, interval, capture,
    undo.
  - 22–24, inside the capture step: openSession, beginCapture, bindPredecessor.
  - All children use a HOT 250 µs ceiling.
- Owner chain on the DEC-240 tree, fresh boot each run, Record at about 13 s.
- Stats read back with `scripts/hitl_profile_dump.py --attach` after the rig exits.

| Run | Image | Capture | Read-back |
|---|---|---|---|
| chain-73 | `11fc1187c336f0acb9716603011779523ad7e5c320e6177262eaa1efba18c814` | `session_20261101_015008.log` | `session_20261101_015236.log`, `session_20261101_015319.log` |
| chain-74 | `3abddecf17c27a2b3e5048fc561d8b46d17332b2fafc9a4752933a619fec4b24` | `session_20261101_015444.log` | `session_20261101_015717.log` + one more |

Both runs are valid: 12 overdubs, 0 `order_deny`, 0 `preparation_denied`.

## Result (chain-74, 12 edges)

| Identity | Code | calls | avg µs | max µs |
|---|---|---|---|---|
| `OverdubStart_body` | whole `Track::startOverdubbing` | 12 | **67,448** | **163,839** |
| `OverdubStart_capture` | `openOverdubSession` + `beginCapture` + `bindPrivatePredecessorSourceForOverdub` | 12 | **59,493** | 86,937 |
| ↳ `OverdubStart_beginCapture` | `Loop::beginCapture(CapturePhase::Overdub, …)` | 12 | **45,009** | **71,707** (last call: 26) |
| ↳ `OverdubStart_openSession` | `Loop::openOverdubSession(…)` | 12 | **14,420** | 15,496 (every call ≈ 14 ms) |
| ↳ `OverdubStart_bindPredecessor` | `bindPrivatePredecessorSourceForOverdub` | 12 | 20 | 24 |
| `OverdubStart_staging` | `prewarmCaptureStaging` + `frozenCaptureCompanions.ensureCapacity` (2 scopes per edge) | 24 | 3,746 | **89,510** (once) |
| `OverdubStart_workingSet` | `admitOverdubStartWorkingSet` | 12 | 102 | 186 |
| `OverdubStart_interval` | `claimOverdubSessionExitObligation` + `beginCaptureInterval` | 24 | 31 | 45 |
| `OverdubStart_memAdmit` / `_undo` / `_handoff` | — | 12 each | 5–14 | ≤ 22 |

The body's self time is about 0.3 ms, so the children account for the edge. **About 88 % of it is two `Loop`
calls doing content-sized work on the edge.**

1. **`Loop::beginCapture` → `establishOverdubSourceView(playheadPhaseTick)` (≈ 45 ms avg, 72 ms max).**
   - It runs up to 8 coverage grains (`serviceOverdubCoverageTurn(kGrainEventsPerTurn)`), then
     `rebuildOverdubSourceView(playheadPhaseTick, "open")`, a full source-view rebuild.
   - It is skipped when the view is already established, which is why the last edge cost 26 µs.
2. **`Loop::openOverdubSession` → `seedOverdubCoverageClosureFromCommittedContent()` (≈ 14 ms, every edge).**
   - It gathers all committed events into a `SessionMidiEventVec` (`gatherConstructionCommittedEvents`, an
     allocating copy) and pairs every note to find the longest span.
   - The answer depends only on committed content, yet it is recomputed at each session open.
3. **`OverdubStart_staging`, once 89.5 ms.** On the edge that has to allocate, the capture-staging reserve
   (`prewarmCaptureStaging`) is a single external allocation. The other 23 calls are cheap.

## Repair directions (owner decision; nothing changed)

- **Seed (14 ms): make it per-revision, not per-edge.**
  - Cache the seeded closure (max note span) keyed by the committed content identity (`playbackRevision` or the
    published snapshot identity).
  - Compute it off-edge, from the idle owner or at commit, next to `adoptPrewarmedOverdubCoverage` (which already
    exists for a prewarmed coverage).
  - The edge adopts the cached value, or falls back to today's path when it is stale.
  - Low risk: it is a pure function of committed content.
- **Source view (45 ms): prepare before the edge, adopt on the edge (the DEC-231 pattern).**
  - The open-window coverage grains and `rebuildOverdubSourceView(…, "open")` depend on the playhead phase, so the
    edge cannot adopt a fully prepared view blindly.
  - Two options:
    - (a) Bound the edge: run at most one coverage grain and defer the rest to the existing deferred coverage turn,
      if the source view tolerates owed coverage at open. This needs the `rebuildOverdubSourceView` contract checked.
    - (b) Prepare the view off-edge for the predicted window, and re-target cheaply at the edge.
  - Higher risk: overlap/ledger semantics apply (`docs/Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md`).
- **Staging (89 ms once):** reserve capture staging off-edge (boot/load or at slot selection), as DEC-065 did for
  Record. It is a single allocation, so the change is small, but the reserve has a memory cost.

## Second finding: a playback-window build reached from interrupt context

The IPSR guard (red-team R6) counted **`isr_refused=16`** in chain-74. The per-identity refusal count added in this
run names the owner: **`PlaybackWindow_build ref=16`**.
- The scope sits after the reuse return, so these were real merged-window builds entered with `IPSR ≠ 0`.
- With the external host clock the IntervalTimer path (`ClockManager::updateInternalClock`) returns early, so it is
  not that.
- The candidate is the overlay playback stream (`makeOverlayPlaybackStream` → `ensurePlaybackMergedMidiEventsBuilt`,
  `TrackPlaybackWindowBuild.cpp`), if MIDI output is serviced from a deadline interrupt.
- **Unproven.** A content-sized build in an ISR would be a direct MIDI-timing hazard and a re-entrancy risk. It
  needs a trace: who calls the overlay stream from handler mode.
