# Display frame phase-split and always-windowed paint

**Status:** Stage 3 host shipped (2026-09-01). Device [`233642`](../../captures/session_20260901_233642.log) scored. Stage 4 stays closed.  
**Date:** 2026-09-01  
**Kind:** refinement  
**Evidence:** [`220427`](../../captures/session_20260901_220427.log) (`midi_gap` ≈ `load_frame`, overdub `DFRAME` 48.5 ms / 509 notes, `clockrate` 47–48)  
**Does not replace:** [`runtime_scheduling_owner_boundary_inventory_investigation.md`](runtime_scheduling_owner_boundary_inventory_investigation.md) C1, [`ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md) rows 3–4, [`oled_dma_partial_frame_bugfix.md`](oled_dma_partial_frame_bugfix.md), [`consumer_window_budget_ownership_architecture.md`](consumer_window_budget_ownership_architecture.md)  
**Does not authorize:** interval reservation, `RuntimeWorkBudget::admit`, new `handleMidiInput()` sites, OLED DMA yield / partial GRAM, a `WindowManager`, shrinking playback gather or overdub source/hold to the display window

---

## Debugging boundary

```
trust: ClockManager 24-pulse BPM; clockrate 47–48 = clocks not lost
trust: midi_gap ≈ load_frame (remainder 350–400 µs) in 220427
trust: DFRAME = whole DisplayManager::update (resolve + draw + OLED)
→ current: DisplayManager::update is one non-preemptible LoadAndDisplay unit
→ current: loops ≤ 16 bars take a full-loop control-flow path (boolean / boundedThreshold)
```

Do not reopen capture-serial volume gates (`SESSION_CAPTURE_SNAPSHOT_IDLE` / `BPM` / `DRAW_SRC`). Do not treat `DISP` as optional HITL evidence.

---

## Problem

[`220427`](../../captures/session_20260901_220427.log) after those gates: displayed BPM swings because `ClockManager::onMidiClockPulse` writes `setBpmFloat` from a 24-pulse window, and one `load_frame` of 27–55 ms sits in that window. `clockrate` stays 47–48.

`DisplayManager::update()` is one call: clear buffer → `resolveDisplayNotes` → draw → `_display.api.display()`. During OVERDUBBING, `MidiServiceDrain::aroundDeferredLoadFrame` is false, so MIDI is serviced only after the whole frame.

On loops **≤ 16 bars** (`kMaxDetailedWindowBars`), `syncDetailedPaintWindow` returns **false** and writes start 0 / length = loop. Callers treat that boolean as “not a bounded/moving detailed window.” `resolveDisplayNotesByDrawSource` and `drawPianoRoll` then walk every note (509 in `220427`). Loops **> 16 bars** already use a moving detailed window. Two control-flow paths, same owner.

---

## North star

One display-paint path: **always an explicit `[start, length)` window**, then **reuse a last-valid resolved note set**, then **one LoadAndDisplay phase per turn**, then a **pinned row-3 yield policy** if a single resolve turn is still tens of milliseconds.

`kMaxDetailedWindowBars` remains the **display detailed-paint horizon** only. It does not govern playback gather, overdub source/hold, or visual-cache idle neighborhood ([`consumer_window_budget_ownership_architecture.md`](consumer_window_budget_ownership_architecture.md)).

A display window **may cover the entire loop**. **Windowed** means every detailed consumer receives an explicit `[windowStart, windowStart + windowLength)` in ticks. It does **not** mean the window is smaller than the loop.

---

## Architecture checkpoint (all stages)

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — `DisplayManager` paints; `main.cpp` `runDeferredLoadAndDisplayFrame` admits the turn; `Loop` still owns `visualCache` / snapshot / capture preview |
| State transition change? | **NO** for Stages 1–3 (paint-phase cursor is DisplayManager frame progress, not a Track/Loop mode). Stage 4 **yes if** an incomplete resolve becomes a user-visible stale-frame policy — pin before coding |
| Unbounded interval owner? | **NO** — split or reduce existing row 3/4 work. Do not add interval owners. Do not add `handleMidiInput()` sites ([ExecutionBounds.md](../Authority/Architecture/ExecutionBounds.md)) |

Reuse: extend `DisplayManager::update`, `syncDetailedPaintWindow`, `resolveDisplayNotesByDrawSource`, `drawPianoRoll`. No new Manager.

---

## Stage 0 — static inventory (gate, not a firmware stage)

Required before the first Stage 1 production edit. Re-run `rg` in the Stage 1 commit and paste any leftover hits into the stage closeout.

### `syncDetailedPaintWindow` boolean — current meaning

**Today `false` is a loop-size / jam classifier**, not “window calculation failed.” On `false` the function still writes `outWindowStart = 0`, `outWindowLength = loopLength`.

| Caller | Uses return as | `false` means today |
|--------|----------------|---------------------|
| `resolveDisplayNotesByDrawSource` | ignores bool; always uses out params | n/a (always gathers `firstBar..lastBar`) |
| `resolveDisplayNotes` NOTE_EDIT idle | `if (true) resolveWindowedDisplayNotes` | fall through to `visualCache` assign / empty |
| `resolveDisplayNotesCommitted` (3 sites) | `if (true) resolveWindowedDisplayNotes` | skip windowed gather; keep compose / full cache |
| `resolveDisplayNotesLiveCapture` | `havePaintWindow = loopLen > 16 bars && slot ok && sync…` | live-capture stays on full-loop capture layer |
| `DisplayColdHelpers` comment | documents that sync “cannot run below 16 bars” | stale comment; must die in Stage 1 |

Jam and `displaySlot >= kDisplaySlotCount` remain the only **non-detailed** cases: no piano-roll window (jam length / invalid slot).

### `boundedThreshold` / `kMaxDetailedWindowBars` — display vs other consumers

| Site | Role after Stage 1 |
|------|-------------------|
| `syncDetailedPaintWindow` | Horizon cap only: `min(requestedBars * TICKS_PER_BAR, loopLength)`. Not a boolean classifier |
| `PianoRollDraw` (`useBoundedWindow`, `shouldAutoFollowDetailedWindow`, two helpers) | Must consume the **window struct**, not `loopLength > boundedThreshold` |
| `DisplayCacheLifecycle` (two early-returns) | Today: skip centering `detailedWindowStartTick_` on short loops. Stage 1: still **do not auto-follow** when the window covers the loop; do **not** treat “short loop” as “no window”. Recenter only when `windowLength < loopLength` |
| `DisplayNoteResolve.cpp` `SC_DISP` vs `SC_DISP_WINDOW` | Telemetry only. Window fields describe geometry; they do not imply the window is smaller than the loop. Runtime correctness does not depend on the tag name |
| `DisplayNoteResolveCommitted` `loopLength > boundedThreshold` gates | Remove as loop-size classifiers; use `syncDetailedPaintWindow` success = “use this window” |
| `DisplayNoteResolveLiveCapture` `liveLoopLength > …` before sync | Same |
| `Loop::shouldAvoidFullVisualRebuild` | **Keep.** Long-loop visual-cache rebuild policy, not a display-window predicate. Update the `DisplayColdHelpers` comment that ties it to “sync cannot run below 16 bars” |
| `shouldAdoptComposedNotesOverVisualCache` (`loopBars <= 16`) | **Keep.** Adopt policy for short-loop compose vs cache (`000013` / `185931`). Not a paint-window classifier |
| `Track::processDeferredIdleMaintenance` neighborhood (`+ 4` bars) | **Do not change** |
| `LoopContentResolution` / `Loop.h` experiment comments | **Do not change** |

Stage 1 invariant:

> **`syncDetailedPaintWindow` returns true when the out-params are the detailed-window representation this consumer must use. False means jam or invalid slot only. No consumer may infer `loopLength > kMaxDetailedWindowBars` from the boolean.**

A leftover `if (syncDetailedPaintWindow(...))` that still means “long loop” is a Stage 1 fail.

### Tick / bar coverage (existing contract — do not invent a new one)

`DisplayWindowUtils` already copies events in the half-open tick window `[start, start + length)` (`include/Utils/DisplayWindowUtils.h`). `noteIntersectsWindow` / `previewNoteIntersectsBar` decide bar membership.

**Invariant:** window coverage is half-open `[windowStart, windowStart + windowLength)` in ticks. Bar iteration includes every bar that **intersects** that interval, including a partial terminal bar when `loopLength` is not a multiple of `TICKS_PER_BAR`. `requestedBars` is an integer display horizon; `outWindowLength = min(requestedBars * TICKS_PER_BAR, loopLength)` is tick-length, not “only whole bars.” Point at `noteIntersectsWindow` — do not add a second inclusion rule.

---

## What Stage 1 does *not* change

Always-windowed means **one code path**, not a smaller piano roll on a 4-bar loop.

| Loop length | Window after Stage 1 |
|-------------|----------------------|
| Window length would be ≥ `loopLength` | start 0, length = `loopLength` (same pixels as today) |
| Window length < `loopLength` | Existing moving window, `min(detailedWindowBars_[slot], 16)` bars, auto-follow unchanged |

Overview strip stays a **full-loop density** consumer from `visualCache` (or the incremental capture-density mask). Do not feed the overview from the detailed window only ([`overview_strip_window_gather_notes_bugfix.md`](overview_strip_window_gather_notes_bugfix.md)).

Jam stays full jam length. NOTE_EDIT still resolves from `EditManager` projection; Stage 1 windows **draw** through the same window struct (window = loop when the loop fits in the horizon).

Do not change idle visual-cache neighborhood, playback gather, or overdub source/hold.

---

## Stage sequence

Implement **one stage**, verify, commit, then start the next. Do not bundle. Stage 0 inventory is a gate on Stage 1, not its own commit.

| Stage | Invariant | Owner symbols |
|-------|-----------|----------------|
| **1** Always-windowed paint path | Every non-jam detailed resolve/draw uses an explicit window from `syncDetailedPaintWindow`. The boolean is not a loop-size classifier. Short-loop window **covers** the loop. | `syncDetailedPaintWindow`, draw-source / committed / live-capture / `drawPianoRoll` / `DisplayCacheLifecycle`, telemetry window fields |
| **2** Dirty-bar / revision reuse | Reuse the last **successfully resolved** draw-source note set when every mutation key is unchanged. A partial gather must not replace that set. | `resolveDisplayNotesByDrawSource` |
| **3** Phase-split `DisplayManager::update` | Interval gates a **new** frame. A **pending phase** admits the next LoadAndDisplay turn. Transfer is the only OLED-visible phase. `midi_gap` follows the longest **phase**, not resolve+draw+transfer. | `DisplayManager::update`, `runDeferredLoadAndDisplayFrame` |
| **4** Row 3 yield policy | Pin incomplete-resolve policy first. A partial candidate never replaces last valid resolved state. | draw-source gather; `kDisplayResolveBudgetMicros` only if policy A |

C1 in the inventory doc is Stage 3. The 5 ms resolve number stays measurement until Stage 4.

---

## Stage 1 — always-windowed path

### Change

`syncDetailedPaintWindow` today returns false when `loopLength <= boundedThreshold` (and still fills a full-loop window). After Stage 1:

- Jam or invalid slot: return **false**; out-params = jam/full length as today.
- Every other in-slot loop: write `[windowStart, windowLength)` and return **true**.
- `windowLength = min(requestedBars * TICKS_PER_BAR, loopLength)`.
- If `windowLength == loopLength`: `windowStart = 0` (covers the loop; auto-follow is a no-op).
- If `windowLength < loopLength`: existing auto-follow + clamp.

Replace every `useBoundedWindow = loopLength > boundedThreshold` **display-paint** test with “use the window from `syncDetailedPaintWindow`” (or the same out-params already computed this frame). `DisplayCacheLifecycle` recenters `detailedWindowStartTick_` only when the window is **smaller** than the loop.

`emitDisplayCaptureSnapshot`: always include window geometry fields when a detailed window exists. Tag name (`SC_DISP` vs `SC_DISP_WINDOW`) must not drive runtime. Fields describe the selected window; they do not imply the window is smaller than the loop.

Update the `shouldAvoidFullVisualRebuild` comment in `DisplayColdHelpers.cpp` so it no longer claims sync cannot run below 16 bars.

### Tests after Stage 1

**Host**

- `pio run -e teensy41-capture-serial` — RAM1 locals **≥ 4000**
- `pio test -e native`
- Inventory closeout: no remaining display-paint caller uses the boolean or `boundedThreshold` as `loopLength > 16 bars`
- Fixture: 4-bar loop → true, start 0, length = loop
- Fixture: 32-bar loop → true, length = 16 bars, auto-follow still centers
- Fixture: **note identity set** on a short loop (including wrap-crossing and a partial last bar if `loopLength` is not bar-aligned) equals the pre-Stage-1 full-loop gather. Compare `NoteId` + pitch + start/end, not count alone
- Fixture: long-loop window identity set unchanged vs pre-Stage-1 windowed path

**Device** (`teensy41-capture-serial`, ask before upload)

Same HITL shape as [`220427`](../../captures/session_20260901_220427.log): record + overdub on a **4-bar** loop, then a **>16-bar** loop (or existing 32/64-bar HITL).

| Gate | Pass |
|------|------|
| Explicit window | Every play/overdub detailed path has `[windowStart, windowLength)` |
| 4-bar coverage | Window covers the loop; frame **identity set** same class as `220427` (not merely similar pixel count) |
| >16-bar | Detailed roll ≤ 16 bars; overview still full loop |
| Neighborhood | Playback / overdub source / idle visual-cache windows unchanged |
| OLED | No blank/partial GRAM |
| HITL | `DISP` present; `Overdub undone` / `Overdub redone` for `OverdubPassAdded` |

**Not a Stage 1 pass criterion:** `load_frame` / BPM. A 4-bar 509-note loop still paints 509 notes.

**Host closeout:** RAM1 locals **6880**. Native `test_display_window_utils` Stage 1 fixtures PASS. Full native 1596 passed; one pre-existing failure `test_same_pass_playhead_tails_keep_closed_bar_four_end` (expected 2976, was 2296) — not this slice.

Inventory leftover `kMaxDetailedWindowBars` in display files (horizon cap only, not a loop-size boolean):

| Site | Role |
|------|------|
| `syncDetailedPaintWindow` jam/invalid out-params | Full-length fallback when there is no detailed window |
| `syncDetailedPaintWindow` / `resolveDetailedWindow` / `centerDetailedWindowOnPlayhead` / `DisplayCacheLifecycle` | `min(requestedBars, kMax)` horizon clamp |
| `rebuildOverdubCommittedDisplayLayer` `maxCommittedGatherLength` | Gather horizon cap, not a paint-window classifier |

No remaining display-paint caller uses the boolean or `boundedThreshold` as `loopLength > 16 bars`.

**Device closeout:** [`230114`](../../captures/session_20260901_230114.log) 20-bar overdub — all `DISP` windowed; PLAYING/OVERDUBBING `wbars=16`, `wstart` auto-follows `0…3072`. [`230655`](../../captures/session_20260901_230655.log) 4-bar overdub — sealed `loop=3072`; every PLAYING/OVERDUBBING/STOPPED `3072` row is `wstart=0` `wbars=4` and `frame==wnotes`; `Overdub undone` ×2 and `Overdub redone` ×2. Zero-note `DFRAME`s are empty-slot frames before record start, not a mid-phrase blank.

Commit Stage 1 before Stage 2. Device HITL is closed. Do not start Stage 2 until the user asks.

---

## Stage 2 — dirty-bar / revision reuse

### Resolved state vs painted state

Do **not** say “return the last painted vector.”

| Name | Meaning |
|------|---------|
| **Last valid resolved draw-source set** | Last gather that finished the current window. This is what a cache hit returns. Stage 4 policy A keeps this set while a candidate is in progress |
| **In-progress resolve candidate** | Only if Stage 4 policy A. Must not overwrite the last valid set until the candidate is complete |
| **CPU framebuffer** | Stage 3 phase 1 only |
| **Transferred framebuffer** | Stage 3 phase 2 / OLED GRAM |

On a cache hit, reuse the last **successfully resolved** draw-source note set. Stage 3 decides when that set is eligible to draw.

### Cache key

**Invariant:** every mutation that can change the draw-source note set must advance one of the generation keys in this cache. Listing today’s fields is not enough; any new mutation path must bump a key or Stage 2 is wrong.

Keys that exist today and must be included if they can change the set:

- display slot + track
- paint `windowStart` / `windowLength`
- `loop.playbackRevision`
- `loop.captureDisplayRevision`
- sealed-preview / pending-change generation already used by `applyPendingNoteChangesToDisplayNotes`

**Playhead bar:** `resolveDisplayNotesByDrawSource` **does** change gather content by playhead bar today: `barIsLiveCapture` selects `DrawSourcePath::Capture` vs snapshot/cache for that bar. Until that is removed or moved to draw-only, playhead **bar** (not tick) stays in the gather cache key. Do **not** drop the playhead-bar key in Stage 2 to save rebuilds — that would be a gather-semantics change, not a cache optimization.

Playhead **tick** stays out of the gather key. `shouldApplyLiveCapturePlayheadTails` is applied on top of the resolved set each resolve (or each draw if Stage 3 keeps tails in draw). Tails must not force a content rebuild.

On miss: incremental reuse is permitted **only** when revisions match, window **length is unchanged**, and `windowStart` differs from the previous window by **exactly one bar** in the expected auto-follow direction. Then drop the leaving bar’s rows and append the entering bar. Any other window geometry change (including a two-bar jump such as `[4, 20)` → `[8, 24)`), revision jump, or slot change performs a **full-window rebuild**.

Do not invent a new cache owner. Use existing `DisplayManager` frame fields (`liveDisplayNotes` as the last-valid set until Stage 4 needs a second buffer). Stage 3 phase 0 must either leave that last-valid set untouched or **replace it atomically** with a newly completed set — never `clear` then append in place.

### Tests after Stage 2

**Host**

- RAM1 locals ≥ 4000, then `pio test -e native`
- Cache hit: committed prefix identity unchanged; no full-window rebuild
- Contiguous one-bar slide (same length, start ± one bar): only the entering/leaving bars change
- Any other window geometry change: full-window rebuild
- Playhead tick move inside the same bar: no content rebuild; tails may change
- Every known mutation (pass publish, pending change, sealed preview, capture revision) misses the cache
- Overdub-style continuous revision: no stale `NoteId` left from a previous generation

**Device** (same 4-bar overdub recapture as Stage 1)

| Gate | Pass |
|------|------|
| Steady PLAYING | `DFRAME` / `load_frame` drop vs Stage 1 when the playhead stays in one bar and revisions are quiet |
| Overdub | Live capture bar and tails update; no frozen hold |
| `midi_gap` | Still ≈ `load_frame` until Stage 3 |
| Notes | Identity set does not collapse vs Stage 1 on the same phrase |

If Stage 2 does not move `DFRAME` on a 4-bar loop, the remaining cost is **draw**. Record that and continue to Stage 3. Do not add a second gather owner.

**Host closeout:** RAM1 locals **6880**. Native Stage 2 reuse fixtures PASS. Full native 1603 passed; one pre-existing failure `test_same_pass_playhead_tails_keep_closed_bar_four_end` (expected 2976, was 2296) — not this slice.

Commit Stage 2 before Stage 3. Device HITL is the same 4-bar overdub shape as Stage 1.

**Device closeout:** [`232353`](../../captures/session_20260901_232353.log) is the 4-bar recapture (`3072`). Ignore the leading 176 leftover `DFRAME`s (`432` notes, ~8.8 ms) from the previous 24-bar boot. After `ARMED → RECORDING`: every sealed PLAYING/OVERDUBBING/STOPPED `3072` row is `wstart=0` `wbars=4` and `frame==wnotes` (171/171). Overdub frames move (`68→168`, 86 unique). Quiet PLAYING after OD2 `DFRAME` p50 **5174 µs** vs Stage 1 [`230655`](../../captures/session_20260901_230655.log) **6368 µs**. OVERDUB 2 p50 **5443 µs** vs Stage 1 **7891 µs**. `load_frame` is the 5 s max of `runDeferredLoadAndDisplayFrame` and did not drop (p50 **11339** vs **8468**). `midi_gap` on this 4-bar shape already disagreed with `load_frame` in Stage 1 (p50 |diff| **40.7 ms**); Stage 2 is the same class (p50 **36.7 ms**), with two OVERDUB samples at remainder **481 / 495 µs**. After OD2 identity is 163 notes vs Stage 1 168; undo/redo lands 152 vs 160 — no collapse. `Overdub undone` ×1 and `Overdub redone` ×1. Remaining 4-bar cost is draw. Do not start Stage 3 until the user asks.

**Earlier 24-bar evidence [`231735`](../../captures/session_20260901_231735.log):** after overdub stop, quiet PLAYING `DFRAME` is **8.7–9.0 ms** (431 notes, `wstart` held). Auto-follow slides stay **~41 ms**. Stage 1 20-bar [`230114`](../../captures/session_20260901_230114.log) PLAYING-after-overdub `DFRAME` p50 was **28–33 ms** with no 9 ms cluster.

---

## Stage 3 — phase-split `DisplayManager::update`

**Do not implement until the admission/cadence contract below is treated as pinned.** It is an owner-boundary rule, not a new scheduler.

### Phases

```text
phase 0  resolve   → last-valid or newly completed resolved draw-source set
phase 1  draw      → fill_buffer + drawTrackStatus / drawPianoRoll / sidebar / info / note info
                     entire CPU framebuffer prepared; no GRAM / SPI / DMA
phase 2  transfer  → _display.api.display() only
```

### Admission and cadence

```text
no pending frame
    ↓ DISPLAY_UPDATE_INTERVAL due
phase 0 resolve
    ↓ pending phase (continuation)
phase 1 draw
    ↓ pending phase (continuation)
phase 2 transfer
    ↓ complete
lastDisplayUpdate = now
    ↓ interval from completion
next phase 0
```

**A pending display phase is itself the admission reason for the next LoadAndDisplay turn. `DISPLAY_UPDATE_INTERVAL` gates the start of a new frame, not continuation of an already-started frame.**

`runDeferredLoadAndDisplayFrame` still calls `displayManager.update()` at most once per admitted LoadAndDisplay turn. When a phase is pending, that call is allowed even if the interval has not elapsed. When no phase is pending, the interval gate stands.

The pending-phase flag is **DisplayManager frame progress** consumed by the existing LoadAndDisplay owner. It is not a second scheduler, pending queue, or extra admission class.

Existing `afterDeferredDisplay` / `aroundDeferredLoadFrame` polls stay as they are. **No new `handleMidiInput()` calls.**

Do not split `SSD1322_API::display()`. Row 4 stays one blocking transfer (~5 ms floor).

### Framebuffer and resolve-state invariants

- **Only phase 2** makes a framebuffer visible to the OLED.
- **Only phase 1** mutates the CPU framebuffer. Phase 1 prepares the **entire** buffer before return. No display transfer, DMA kick, or GRAM write in phase 1.
- **Only phase 0** mutates resolved display-note state (and tails if they stay on resolve). Phase 0 either leaves the last-valid set untouched or replaces it **atomically** with a completed set — never clear-then-append in place.
- Neither phase 0 nor phase 1 may change the currently displayed GRAM.

### Invalidation coalescing

Invalidations that arrive while a frame is in progress are **coalesced into the next frame**. They do not restart phase 0, do not abandon a half-drawn CPU buffer in a way that phase 2 would push, and do not enqueue extra display turns. Required under OVERDUBBING, where revisions arrive continuously.

### NOTE_EDIT

No force-complete exception. `shouldForceNoteEditDisplayUpdate` may **request** a frame or give a pending frame priority. Completion still uses the same three phases. Do not restore a synchronous critical section for NOTE_EDIT.

### Telemetry (lock before coding)

| Tag | Meaning after Stage 3 |
|-----|------------------------|
| `load_frame` / child rem 0 | One admitted `DisplayManager::update()` = **one phase** |
| `DFRAME` | Wall time from frame-start resolve through transfer completion (`≈ resolve + draw + transfer + scheduling gaps`) |
| `DFRAME` phase fields | `resolve_us`, `draw_us`, `transfer_us` on the complete-frame line |

**Do not interpret `DFRAME` as a MIDI-blocking interval after Stage 3.** The MIDI-relevant metric is **max phase `load_frame`**.

Telemetry must compile out or return immediately when capture is off, and must not change admission or phase order. If phase fields do not fit the existing rem budget, omit the extra children and keep `load_frame` = phase + `DFRAME` = complete frame — do not drop the contract to “maybe add children.”

`maybeEmitDisplayCaptureOnChange` / `SC_DFRAME` run only when the **complete** frame exists (after phase 2). Do not emit a resolve-only `DISP` as a paint.

Capture-active “never skip OLED” still means: do not skip the LoadAndDisplay **turn**. A turn may be a single phase.

### Tests after Stage 3

**Host**

- RAM1 locals ≥ 4000, then `pio test -e native`
- **Frame-state admission** (not call-count): three admitted `update()` turns complete exactly one frame: resolve → draw → transfer. After transfer, `lastDisplayUpdate` advances; a new resolve is admitted only when the interval gate for the next frame is due.
- Executable sequences:
  1. no pending frame + interval not due → no phase
  2. interval due → resolve only
  3. pending → draw
  4. pending → transfer
  5. transfer complete → no immediate new resolve if interval not due
  6. interval due → next resolve
- Invalidation during draw → current frame finishes; next frame sees coalesced state
- NOTE_EDIT request uses the same three phases (no single-call complete)

**Device**

| Gate | Pass |
|------|------|
| MIDI | `max(midi_gap)` follows the **longest indivisible phase**, not resolve+draw+transfer. This is the Stage 3 win. It does **not** require 5–10 ms if resolve or draw is still 30–40 ms |
| `load_frame` | One phase per dump sample class; no longer equals pre-split `DFRAME` |
| `DFRAME` | Complete-frame wall time; not used as the MIDI bound |
| OLED | Continuous refresh; no first-row-only GRAM |
| `clockrate` | 47–48 |
| 4b.3 | Score post-overdub PLAYING first `midi_gap` as in [`213738`](../../captures/session_20260901_213738.log). Do not close 4b.3 unless the OpenSpec bound is met |

**Host closeout:** RAM1 locals **6880**. Native Stage 3 admission fixtures PASS. Full native 1606 passed; one pre-existing failure `test_same_pass_playhead_tails_keep_closed_bar_four_end` (expected 2976, was 2296) — not this slice.

Commit Stage 3 before Stage 4.

**Device evidence [`233642`](../../captures/session_20260901_233642.log):** 4-bar HITL (`3072`), Stage 3 `DFRAME` lines have resolve/draw/transfer. After `ARMED → RECORDING`: every sealed PLAYING/OVERDUBBING/STOPPED `3072` row is `wstart=0` `wbars=4` and `frame==wnotes` (196/196). Overdub frames move (`64→152`, 83 unique). `Overdub undone` ×2 and `Overdub redone` ×2. Transfer is **4495–4497 µs** on every sampled frame (full GRAM). `clockrate` is **47** after the first record-start dump (`13`). `DFRAME` wall p50 **12844 µs** (complete frame + scheduling gaps); phase-sum p50 **5169 µs**; longest phase is transfer **4496 µs**. After the first RECORDING dump (`243593`), `load_frame` is **4667–5384 µs** and no longer equals `DFRAME` wall. `midi_gap` 5 s-max dropped vs Stage 2 [`232353`](../../captures/session_20260901_232353.log) (p50 **14.4 ms** vs **45.2 ms**) but still often sits near the complete-frame span, not the 4.5 ms transfer floor; first PLAYING gap after OD2 is **6930 µs**, a later PLAYING dump is **25771 µs**. 4b.3 stays open. Stage 4 stays closed.

**Follow-up (not Stage 4):** OD2 committed bars in [`233642`](../../captures/session_20260901_233642.log) stayed wrong until stop because VisualCache idle reconstruct ran mid-OVERDUB (`visual` 102→99, stop `adopt_partial` 146). `DISP` frame still moved (`116→159`). DEC-045 amendment restore: `allowVisualCacheReconstructSlice`. Paint handoff while that gate holds: [`overdub_paint_capture_handoff_bugfix.md`](overdub_paint_capture_handoff_bugfix.md) ([`234726`](../../captures/session_20260901_234726.log)).

---

## Stage 4 — row 3 yield policy (pin first)

[`ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md) row 3: resolve is **Unbounded**, 5000 µs measurement only. Cadence ≠ computation.

**Do not implement until the user pins one policy:**

| Policy | Incomplete resolve |
|--------|-------------------|
| A | Keep last valid resolved set; resume gather next resolve phase. Cap: one bar (or one dirty-bar page) per resolve turn |
| B | Always finish the window gather in this resolve phase (Stage 3 only; 5000 µs stays measurement) |
| C | Always finish; Stage 1 window + Stage 2 reuse are the bound (no timer) |

A local timer between bars does not make an indivisible sort/apply structurally bounded.

Policy A two-state model (names may map to existing `DisplayManager` fields):

```text
lastValidResolvedDisplayNotes     ← last complete gather; this is what draw/transfer may use
pendingResolvedDisplayNotes       ← in-progress candidate
```

**A partial candidate must never replace the last valid resolved state.**

### Tests after Stage 4 (only after pin)

**Host:** RAM1 ≥ 4000; resume fixture (A) or “no early return” fixture (B/C).

**Device:** resolve-phase `load_frame` / `midi_gap` vs the pinned cap; no missing notes after the window is complete; HITL `DISP` still matches the phrase.

If policy B or C is pinned, Stage 4 is **doc + inventory note only** (row 3 class unchanged unless a structural cap exists).

---

## Shared verification commands

After every firmware stage:

```bash
pio run -e teensy41-capture-serial          # RAM1 locals >= 4000 before native
pio test -e native
```

Upload only after the user confirms (`teensy41-capture-serial`). Recapture with managed HITL + serial (`HITL-Test-Flow`).

Score each recapture:

| Field | Why |
|-------|-----|
| `DIAG,clockrate` | 47–48 = clocks present |
| `DIAG,midi_gap` + `DIAG,load_frame` | Gap owner; after Stage 3, max **phase** |
| `DFRAME` | Complete-frame wall time (not MIDI-blocking after Stage 3) |
| Window geometry on `DISP` | Selected `[start, length)`, not “smaller than loop” |
| `ST,Track` | Phase attribution |
| Implied 24-pulse BPM | Context only; Stage 3 does not claim 5–10 ms |

Keep `SESSION_CAPTURE_BPM=0` on the default env. Do not re-enable `draw_src` for these gates.

---

## Exclusions

- OLED DMA / partial transfer ([`oled_dma_partial_frame_bugfix.md`](oled_dma_partial_frame_bugfix.md))
- Extra MIDI poll sites; extending `aroundDeferredLoadFrame` to OVERDUBBING
- Changing `DISPLAY_UPDATE_INTERVAL` as a substitute for a smaller step
- Playback window, `kOverdubSourceWindowBars`, occupy/hold, snapshot construction
- Changing `Loop::shouldAvoidFullVisualRebuild` or idle neighborhood to follow the paint boolean
- Undo paint (RC5b) and OD1-stop `stale_range` visual shrink
- Closing OpenSpec 4b.3 or 3.6 from Stage 1–2, or from Stage 3 unless the bound is met
- Gating `DISP`, `ST`, `VCACHE`, `ODUB,stop,*`, `DIAG,clockrate`
- NOTE_EDIT force-complete of all three phases in one `update()`

---

## Pre-implementation review (Stage 1)

### Ready
- Owner: `DisplayWindowUtils::resolveDetailedPaintWindow` + `DisplayManager::syncDetailedPaintWindow` and existing paint consumers
- Geometry is host-testable without `Track`
- Short-loop draw keeps full-loop mapping (`movingWindow` only when `windowLength < loopLength`); overview strip only when moving
- `Loop::shouldAvoidFullVisualRebuild`, `shouldAdoptComposedNotesOverVisualCache`, idle `+4` neighborhood, playback gather, and overdub source/hold stay unchanged

### Resolved
| Topic | Decision |
|-------|----------|
| Boolean meaning | `true` = use these out-params; `false` = jam or invalid slot only |
| Covering window | start 0, length = `loopLength`; auto-follow / recenter are no-ops |
| Draw coordinates | Window-relative ticks and overview only when the window is smaller than the loop |
| Telemetry | `SC_DISP_WINDOW` whenever a detailed window exists; tag name does not drive runtime |

### Open before coding
1. Windowed gather of a covering window must keep the same `NoteId` + pitch + start/end set as the pre-Stage-1 full-loop path, including wrap-crossing and a partial last bar
2. Live-capture `havePaintWindow` becomes sync-only; covering-window capture-layer identity matches today's full assign
3. Last-resort committed rebuild stays behind jam/invalid only; short loops take `resolveWindowedDisplayNotes` with window = loop

### Proceed?
YES — implementer pins only. Stages 2–4 stay closed.

## Pre-implementation review (Stage 2)

### Ready
- Owner: `DisplayManager::resolveDisplayNotesByDrawSource`
- Reuse policy is host-testable as `selectDrawSourceReuseKind` / `DrawSourceGatherKey`
- Last-valid set stays `liveDisplayNotes`; rebuild/slide writes a candidate then `swap`
- Playhead bar stays in the key; tails stay off the gather key

### Resolved
| Topic | Decision |
|-------|----------|
| Cache owner | Existing `liveDisplayNotes` + `liveDrawSourceKey_` |
| One-bar slide | Keep notes that still intersect the new window, then append the entering bar |
| Mutation keys | Include capture-preview / visual-cache / snapshot-current / sealed-preview / pending fingerprint so occupy and live capture miss |

### Open before coding
1. Re-applying pending transforms after a slide must stay idempotent on already-transformed rows
2. Device `DFRAME` drop on a 4-bar loop is optional; if it does not move, remaining cost is draw

### Proceed?
YES — implementer pins only. Stages 3–4 stay closed.

## Pre-implementation review (Stage 3)

### Ready
- Owner: `DisplayManager::update` + `runDeferredLoadAndDisplayFrame`
- Phase/admission helpers are host-testable in `DisplayWindowUtils`
- Last-valid notes stay `liveDisplayNotes` / `liveDisplayPendingPaintNotes_` across draw and transfer
- Existing `afterDeferredDisplay` / `aroundDeferredLoadFrame` polls stay; no new `handleMidiInput()`

### Resolved
| Topic | Decision |
|-------|----------|
| Admission | Pending phase or NOTE_EDIT request or interval due |
| `lastDisplayUpdate` | Advances only when `update()` returns with no pending phase (complete or boot skip) |
| NOTE_EDIT prefix | One phase per `maybeUpdateDisplayForNoteEditSelection`; continuation stays LoadAndDisplay |
| Overlay | Same three phases; phase 0 stores `paintLoadSaveActive_` so draw/transfer finish that frame |
| Invalidation | Dirty keys immediately; do not clear resolved notes until transfer completes |
| Telemetry | `load_frame` child 0 = one phase; `DFRAME` after transfer with resolve/draw/transfer µs |

### Open before coding
1. Device `midi_gap` follows the longest phase; 4-bar draw may still be ~5 ms
2. Do not close 4b.3 unless the OpenSpec bound is met

### Proceed?
YES — implementer pins only. Stage 4 stays closed.

---

## Docs closeout (per stage commit)

- This plan: mark the stage **shipped** + capture id  
- [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) / [`PROJECT_STATE.md`](../Runtime/PROJECT_STATE.md)  
- [`ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md) only if Stage 4 reclassifies row 3  
- [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) only if `DFRAME` phase fields change the capture contract

No `DEC-###` unless Stage 4 policy A is pinned (incomplete resolve is then a display completeness rule).
