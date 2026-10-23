# 3.15e — overdub-overlay closure (native)

**Task:** `windowed-playback-generation` 3.15e, overdub-overlay clause. **Date:** 2026-10-24. **Device parity:**
3.15f (not claimed here). **Firmware:** unchanged by this pass (comment/test/doc only); both images at the approved
pins.

## G0 — scope traced

| # | Trace | Result |
|---|---|---|
| 1 | Entry points | `main.cpp:498` per-track `PlaybackViewMaintenance` visit → `Track::serviceOverdubOverlayWindow`; playback edge in `Track::playMidiEvents`/`playCommittedLoopMidi` → `resolveQualificationPlaybackSourceForTurn` |
| 2 | Call path | demand (`overdubOverlayDemandForSlot`) → boundary/retirement → `BaseReady` → `LoopPlaybackRuntime::stepOverdubOverlayBuild` (borrowed `RuntimeCompanionPasses` base + `beginLiveOverlayContinuation`) → candidate → edge binds live source → committed walk only |
| 3 | State transitions | no role/`current` transition: the candidate stays in the slot's build role; stopped/wrap/identity move retires it; the edge re-evaluates per turn |
| 4 | State owners | `LoopPlaybackRuntime` (candidate, borrow, builder phases); `Track` (demand/retention); `EditManager` untouched; no new owner/queue/state |
| 5 | Lifetimes | candidate lives while the demand identity holds; borrowed companion released once the base is copied; retirement ordered before reuse and at stop/wrap |
| 6 | Deferred continuations | one grain per owner turn; the committed canonical owner drives the base (never this lane); 2b-iii arbitration deferred by owner, lane returns the named deferral |
| 7 | Publication/handoff | no publication/adoption on this route; the immutable thing is the published `readableEventCount` frontier of the live source; the pass commit/stop retire and the next turn falls back or rebuilds |
| 8 | Persistence | none on the route; the pass commit owns the snapshot/ledger as before |
| 9 | Undo/redo | none; the overlay is live-only |
| 10 | Display/playback consumers | playback reads `[0, readableEventCount)`; display unchanged |
| 11 | Hot path / bound | `overlayLiveSelected` selects the overlay stream and provenance callbacks; echo + `ensureCaptureEventsSorted()` skipped; owner bound recorded as `ExecutionBounds.md` row 34 |
| 12 | RAM1/PSRAM | no production change; both images exact (`391,752 + 1,464` / `391,992 + 1,224`, floor 840) |
| 13 | Coverage | W1/W2/W3–W7 + the 3.15e E2E oracle; added W8 (64-bar interval) |
| 14 | Alternate paths | echo fallback (W6/W7), NOTE_EDIT preview (takes precedence), first-take Record branch, default image compiles the lane out |

**VERIFIED:** the primary route is the live overlay window; the echo is skipped while it serves; provenance keeps the
ledger committed-only; no `current`/`ready` role or whole-loop materialization; the long loop's window is the two-bar
interval. **ASSUMPTIONS:** none material. **UNKNOWNS:** device timing/route parity (3.15f). **CONFLICTS:** the pre-slice
`Track.h` comment ("the echo stays authoritative") was stale and is corrected. **G0 verdict: PASS** (test/doc closure;
no architecture change).

## What was added

1. `test_the_live_overlay_window_serves_a_long_loop_interval` (`test_overlay_edge`): 64-bar loop, committed note at
   7 021, live overlay note inside the interval. The candidate's coverage is checked as the **two-bar** window (not the
   whole loop), the edge is selected, the live row and the committed note are each served once, and the ledger stays
   closed. This is the task's "short and long lengths" second half (the 2-bar W1 is the first).
2. `ExecutionBounds.md` row 34 — the lane's per-turn bound: fixed checks + at most one builder grain; canonical
   preparation never advanced; named deferral; the underlying build service remains row 27's class.
3. `Track.h` comment corrected: no adoption/role; the edge may serve the candidate and the echo is only the fallback.

## Gates

- `test_overlay_edge` **8/8**; `test_shared_capture_finalization` **95/95**; `test_playback_prewarm` **247/247**;
  `test_windowed_preview_integration` **48/48**; full native **3,433 / 7 skipped / 3,426 succeeded**.
- `teensy41-capture-serial` and `teensy41-capture-serial-window-qualification` SUCCESS at the exact pins.
- Strict OpenSpec validation valid; `git diff --check` clean.

## Not claimed

No device run (3.15f), no 2b-iii role arbitration (deferred by owner; the lane defers by name), and no removal of the
echo emitter (3.7b; the echo remains the named compatibility fallback).
