# Display freeze during an overdub chain, first-frame delay after upload, MIDI timing drift

**Date:** 2026-10-28
**Baseline:** the current working tree (DEC-165…178, uncommitted). The owner's decision: **keep the code** — the
start/stop liveness issue is solved — and treat these three symptoms as a new investigation **on top of** it.
**Status:** evidence collection. No code change yet.

## 2026-10-28 planning correction — transition gaps are not frame gaps

The prior interpretation below is retained as the investigation's original hypothesis, **not verified attribution**.
`emitDisplayTurnReceipt` in `src/DisplayManager/DisplayNoteWindowGather.cpp` suppresses repeated fresh frames when
track/slot, retained state and published viewport do not change; divergent requests are sampled. Consequently,
the 15.8–16.0 s spacing below does not prove frames were not served. `DFRAME` is also sampled and cannot by itself
establish each transfer's completion time. The owner's visible freeze remains a real reported symptom; its exact
boundary remains UNKNOWN.

Current source additionally contains a later reduced-burst variant (4 grains/600 us plus remaining-time check),
not the 8-grain/1500-us behavior described for the qualification image. Pin the dirty source and HEX separately;
do not turn comments into flashed-image evidence or overwrite these pre-existing changes.

The [proposal](proposal.md), [spec](specs/overdub-chain-display-progress/spec.md), [design](design.md) and
[fixture tasks](tasks.md) now separate actual phase/submission/completion, content freshness and stop throughput.
Production repair is gated on a discriminating fixture; a smaller burst alone is not an accepted fix.

## 2026-10-28 Apply evidence — transfers continue, native trace blocked elsewhere

The existing DMA census now directly falsifies the inferred total frame hole: in `162431`, completed DMA
frames increase **1480→1627 between 108.434 and 122.870 s**, and **1637→1809 between 123.877 and 139.111 s**,
with no timeout/blocking fallback at those endpoints. `reportDisplayDmaTransport` reads the completed-frame
counter. This does not prove note/playhead freshness; investigate stale content separately from transfer service.

Combined one-bar/24-bar native traces complete **26/27 frames** with expected note identities before an
accepted 16th Stop remains pending beyond regular turns. The prescribed 22-stop trace is not completed.
Disabling only the burst reaches queue5 much earlier (7th/5th Stop), while content checks still pass.
This does not reproduce the device display symptom and is not grounds for removing the working burst.
Two failing chain characterizations remain explicit; see [execution evidence](evidence/REGRESSION.md).

## Reported symptoms
1. After a firmware upload the board "often hangs on the first frame".
2. After ~4–5 overdubs the display freezes.
3. MIDI timing is perceived as deteriorating.

## Evidence already in hand

### `captures/session_20261028_162431.log` (image `fbab9dcc…`, DEC-177 + DEC-178)
- 23 overdubs / 23 stops, **0** stop retentions, 0 refused starts, 0 heap denials, `cap=22`, `edit=19` — the liveness
  work is intact.
- `disp_turn`: **1133 fresh, 6 ret (`hold_gap`)**, plus **175 `win_gap` rows**.
- Display service **holes of 15.8–16.0 s** while overdubs continue: e.g. last frame at t=107.6 s (`n=423`), next at
  t=123.6 s (`n=464`). Content grew by 41 notes across the hole; the frames simply were not served.
- `disp_window` state rows keep arriving during the hole (`st=PLAYING`/`OVERDUBBING`), so the *state* lane is alive; it is
  the frame/source lane that is gapped.

### `captures/session_20261028_162119.log` (boot after upload)
- First `disp_turn` at **13.5 s** (`n=0`), first `DFRAME` at 15.1 s, first frame with content at **22.5 s**
  (`n=84`, `st=ret, why=handoff_seed`). 19 `win_gap` rows in the first 25 s.
- So "hangs on the first frame" is measurable: first paint is 13–22 s after boot, with the display holding a
  handoff seed frame in between.

### Mid-chain publication (the shared suspect)
- The display resolves notes from the published root / qualified window. In a chain, publication is exactly the lane the
  deferred stop FIFO excludes: `processDeferredIdleMaintenance` returns from the pending-stop branch, and
  `allowSnapshotConstructionStart` refuses while an overdub is live with a published root (DEC-045).
- DEC-178 makes each backlogged stop turn longer (up to 4 grains / 600 µs, plus a grain), which can only reduce the
  turns other lanes see. The reported "worse after the latest upload" is consistent with that, but is **not proven**.

## Hypotheses (ordered, each falsifiable)
- **H1 — frame lane starved by the stop/publication exclusion, aggravated by the burst.** A chain keeps a stop job
  pending almost continuously; the display's qualified source is gapped (`win_gap`) and frames are served only at chain
  boundaries (~16 s). *Test:* a `disp_turn` gap watchdog naming the blocking predicate; and a host fixture asserting a
  bounded frame cadence while a chain runs with a live successor.
- **H2 — first paint waits for restored-content publication.** 13–22 s of `win_gap`/handoff seed at boot. *Test:* boot
  timeline with the same watchdog, and the first-paint gate's own row.
- **H3 — MIDI drift from longer owner turns.** Not measurable from the current rows alone: `track_update` max is 3,847 µs
  on this image vs 3,890 µs on `155453`, i.e. **no measured regression yet**; the perception may be display-related
  (frames lagging the audio) rather than clock jitter. *Test:* edge-to-edge playback jitter oracle across the chain, and
  a worst-turn duration row from the burst.

## First actions
1. Add a report-only gap row for the display frame lane naming *what* blocked the last frame (source gap, hold, handoff,
   publication) — the DEC-166 pattern: a lane that cannot say why is indistinguishable from one nobody serviced.
2. Host fixture: 20-overdub chain, successor live, `kOwnerTurnsPerPress` model; assert frame servings per second do not
   collapse (today the equivalent device shape gives one frame per ~16 s).
3. Only then decide between (a) making the burst yield to a publication/display backlog, (b) un-gapping the display source
   during a chain, or (c) both.
