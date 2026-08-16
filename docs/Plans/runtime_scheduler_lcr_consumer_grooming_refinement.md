# Runtime scheduler — LCR consumer grooming

**Status:** Active — Slice 2 idle one-source (wrap-held edge append documented)  
**Date:** 2026-08-16  
**Kind:** refinement  
**Evidence:** [`114736`](../../captures/session_20260816_114736.log) (LED Stage 1 PASS); [`132439`](../../captures/session_20260816_132439.log) (Slice 1 boot reset); [`133314`](../../captures/session_20260816_133314.log) (Slice 1 attribution)  
**Parent:** [`post_undo_led_lookup_resumable_source_refinement.md`](post_undo_led_lookup_resumable_source_refinement.md)  
**Scheduling contract:** [`runtime_scheduling_admission_model_architecture.md`](runtime_scheduling_admission_model_architecture.md)  
**Owner-boundary roadmap:** [`runtime_scheduling_owner_boundary_admission_refinement.md`](runtime_scheduling_owner_boundary_admission_refinement.md) (R1C remaining-owner inventory)  
**LCR architecture:** [`loop_event_sourced_resolution_architecture.md`](loop_event_sourced_resolution_architecture.md)  
**Authority:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype) (do not wire resolution onto MIDI/display until three gates); DEC-016 derived representations

LED Stage 1 is the architectural precedent. Do not reopen it unless Stage 2 is demonstrated necessary.

This file is **not** a license to implement the whole inventory. Authorized slices are listed under [Approved slices](#approved-slices). Later rows stay investigation until a slice is opened.

## Consumer rule

A MIDI-sensitive consumer must never turn “derived content is dirty/empty” into “rebuild derived content now.”

```text
producer / idle owner
        ↓
prepare notes incrementally
        ↓
consumer
        ↓
read whatever prepared/stale notes exist
        ↓
never synchronously flatten because they are dirty
```

LED Stage 1 demonstrated this: after the BAR gather left, `midi_led_*` rem disappeared and the remaining PLAYING gaps became `idle_maint` + `load_frame`. That is the next class of work, not the same gather moved.

DEC-037 still forbids calling `tryResolvePreparedWindow` / `resolveWindow` / `resolveState` from `handleMidiInput`, BAR `updateLeds`, `startOverdubbing`, `stopOverdubbing`, or `commitLoadLoopJobPublish`. Idle may consume prepared LCR. MIDI-sensitive and paint-critical paths may not.

Long work stays in an existing owner that can stop and resume across `loop()` turns. No new Session, no new Manager.

## Derived-work stalkers

Three distinct classes. Fixes are not interchangeable.

```text
A. Flatten-on-demand
   consumer asks for dirty content
       → full materialize/gather

B. Non-resumable derived work
   legitimate preparation
       → happens in one loop() turn

C. Duplicate derivation
   prepared/derived content already exists
       → another owner reconstructs the same content anyway
```

| Stalker | Correct response |
|---------|------------------|
| **A** Flatten-on-demand | Consume stale/prepared representation |
| **B** Non-resumable work | Make the existing owner resumable |
| **C** Duplicate derivation | Remove the second derivation |

LED Stage 1 eliminated **A** on the BAR path. `appendOverdubPassDisplayNotes` after a prepared LCR window is **C**. Bundled `load_frame` / OLED paint is currently **B** (the bundle hides which sub-operation owns the µs).

Do not turn every new timing line into a bespoke optimization. Name the stalker first.

## What [`114736`](../../captures/session_20260816_114736.log) shows

Rem one-shots (≥50 ms):

| Span | Count | Range | Owner in this capture |
|------|------:|-------|------------------------|
| `idle_maint` | 67 | 50–122 ms | `Track::processDeferredIdleMaintenance` → `Loop::rebuildVisualCacheIdleSlice` |
| `load_frame` | 17 | 59–802 ms | `runDeferredLoadAndDisplayFrame` |

`persist_save` max is 10 ms. No `midi_led_*`, no `VCACHE,full`, no `DIAG,lcr,6a`. The only LCR line is `lcr,skip,restore` at 6.303 s, so this session never had a prepared LCR window. Idle used `gatherCommittedEventsInWindow` + `reconstructDisplayNotes`.

| Window | `midi_gap` | Composition |
|--------|------------|-------------|
| Boot 10.554 s | 835 ms | `load_frame` 802 ms + `idle_maint` 122 ms |
| PLAYING 5 s | 110–127 ms | `idle_maint` 46–55 ms + `load_frame` 60–74 ms |

Every PLAYING `load_frame` rem sits immediately after a `DISP`. At 69.413 s, `DFRAME` paint is 63.3 ms and `load_frame` is 64.1 ms. That is OLED paint, not LoadLoopJob hydrate. Boot 802 ms is a different class ([`032803`](../../captures/session_20260816_032803.log) 915 ms).

Treat those as two measurements until child spans prove otherwise.

## What [`133314`](../../captures/session_20260816_133314.log) shows

Boot: `BOOT,scan,start` → `t0` … `t7` → `done` → `load,ok`. RAM1 PSTR fix holds.

Child rem (50 ms one-shot; no 5 s child windows):

| Span | Samples | Duration | Parent `load_frame` |
|------|--------:|----------|---------------------|
| `boot_commit` | 1 | 784.3 ms @ 6.798 s | 791.5 ms |
| `display_frame` | 8 | 50.7–72.3 ms | parent +0.6–7.0 ms |
| `load_job` | 0 | never ≥50 ms | — |
| `first_commit` | 0 | never ≥50 ms | — |

PLAYING `display_frame` sits on the `DISP` immediately before it (592 notes @ 21.054 s → 72.3 ms; 528 notes @ 58.572 s → 67.4 ms). Parent is the same turn.

PLAYING 5 s windows with `clockrate` 47–48:

| `midi_gap` | Composition |
|------------|-------------|
| 111–118 ms | `idle_maint` 48–52 ms + `display_frame` 67–72 ms |
| 48–54 ms | `idle_maint` 48–54 ms only (no paint rem that window) |

[`114736`](../../captures/session_20260816_114736.log) still holds. Boot 792 ms is `boot_commit`, not `load_job`. PLAYING 67–72 ms is OLED paint, not LoadLoopJob. Do not start a `LoadLoopJob` firmware change from these lines.

## Already on the consumer-rule path

| Consumer | Source today | Resume | Stalker closed |
|----------|--------------|--------|----------------|
| LED presence | `visualCache.notes` if non-empty (Stage 1) | idle `slice_clean` | **A** on BAR |
| Display when cache covers the window | filter `visualCache.notes` | idle slice | **A** when covered |
| Overdub entry | prepared LCR window, else copy `visualCache.notes` | consume-only | **A** on entry |
| Idle visual cache | `tryResolvePreparedWindow`, else window gather | 2–4 bars/slice | sliced; **C** still open |
| LoadLoopJob read/parse | SD bytes / parse state | `DeferredJobScheduler::runFrame` | read/parse already **B**-sliced |
| Persist | work-queue item | budgeted | — |

## Inventory (not an implementation queue)

### `runDeferredLoadAndDisplayFrame` — bundle, not one owner

`load_frame` times the whole function. Child work:

```text
load_frame
├── display_frame
│   └── DisplayManager::update()
├── load_job
│   └── DeferredJobScheduler::runFrame()
├── first_commit
│   └── Track::ensurePlaybackMergedEventsForSlot()
└── boot_commit
    └── finishBootSetup / USB host
```

| Mode | Bundled rem | Child ([`133314`](../../captures/session_20260816_133314.log)) |
|------|-------------|-------------------------------------|
| PLAYING | 67–73 ms | `display_frame` — OLED paint (528–592 notes in this capture) |
| Boot | 792 ms | `boot_commit` 784 ms; `load_job` / `first_commit` never ≥50 ms |

Read/parse is already resumable. These pieces are not:

- **`commitLoadLoopJobPublish`** — one turn: `applySnapshotToLoop` → `adoptPersistedSnapshot` → `markDisplayCachesStale` → `TrackUndo::rebuildSlotFromLoopContent`. No note gather, but it cannot yield (**B**).
- **First-commit `Track::ensurePlaybackMergedEventsForSlot`** — `ensurePlaybackMergedMidiEventsBuilt` still `gatherCommittedEvents*` (full loop if ≤16 bars, 2-bar window if longer). That is 6.3, parked.
- **`DisplayManager::update`** — already reads `visualCache.notes` when covered. PLAYING 60–74 ms is paint, not flatten (**B**). Do not start optimizing `LoadLoopJob` from a 70 ms `load_frame` line.

Child spans proven in [`133314`](../../captures/session_20260816_133314.log). Keep parent `load_frame`. Do not optimize `LoadLoopJob` from a PLAYING `display_frame` line.

### `rebuildVisualCacheIdleSlice` + `appendOverdubPassDisplayNotes` — duplicate derivation

Idle is already the resume owner. In [`114736`](../../captures/session_20260816_114736.log) it is 50–80 ms (122 ms at boot) because LCR was not prepared.

When a window **is** prepared, today’s path is:

```text
tryResolvePreparedWindow
        ↓
reconstructDisplayNotes
        ↓
appendOverdubPassDisplayNotes
        ↓
walk every active overdub pass
        ↓
reconstruct again
```

`Loop::appendOverdubPassDisplayNotes` copies chunks, applies all edit rows, and reconstructs on every slice even after LCR supplied the window. That is **C**.

Desired path:

```text
rebuildVisualCacheIdleSlice
    ├── prepared LCR window? → resolve notes → visualCache
    │     └── wrap-edge slice (bar 0 or last bar) may still append
    └── no → existing window gather + append
```

**Native delta (must document before skip):**

| Fixture | LCR-only vs LCR+append |
|---------|------------------------|
| Linear overdub (record 60 + overdub 72) | Match |
| Wrap-held + later same-pitch body ([`021218`](../../captures/session_20260816_021218.log) / `test_visual_cache_keeps_overdub_wrap_held_without_stretching_record`) | LCR-only omits `12@2976` and `12@0`. Append keeps them. Record spans stay 672–768 and 2400–2500 |

Merged reconstruct cannot take `overdubPassWrapPairing` — that flag is one overdub pass only ([`015618`](../../captures/session_20260816_015618.log) stretched record). So a prepared interior slice uses LCR only. A prepared slice that keeps bar 0 or the last bar still calls `appendOverdubPassDisplayNotes` for that pairing gap. Unprepared gather still appends every slice.

**Invariant (first behavioral slice):** for a prepared interior window, `rebuildVisualCacheIdleSlice` has exactly one content-resolution source. It must not walk overdub passes again for bars that cannot hold the wrap-held pairing gap.

LCR stays an idle consumer. Do not pull it onto MIDI or display input. Grain (2–4 bars) is a later **B** bound, not this slice.

### `ensureVisualCacheBuilt` — caller-by-caller, not a global delete

`gatherCommittedEvents` + full reconstruct. Callers do not share one latency contract.

| Caller | Context | Desired behavior |
|--------|---------|------------------|
| PLAYING | MIDI-sensitive | **never** synchronous rebuild |
| STOPPED idle | not MIDI-sensitive | may stay synchronous if a capture proves it harmless |
| `EditManager::openNoteEditSession` | user action | own hydrate contract — see NOTE_EDIT below |
| `DisplayManager::refreshViewportAfterOverdubStop` | MIDI-sensitive | must not hydrate synchronously |
| Display committed resolve | paint path | stale/empty rather than gather |
| `TrackManager::prewarmSelectedDisplayVisualCache` | not PLAYING, short loop | audit; do not assume idle-slice is enough |
| `Loop::seedRecordPassFromStore` | capture setup | separate contract |

Direction is mark-stale → existing resumable owner → slices. Audit each caller before replacing it. Do not delete `ensureVisualCacheBuilt` in one pass.

### Display fallback gather — flatten-on-demand on paint

`DisplayManager::resolveWindowedDisplayNotes` filters `visualCache.notes` when the window is covered. If not, `rebuildDisplayNotesInWindow` still `gatherCommittedEventsInWindow*` + reconstruct on the paint path (**A**).

`resolveCommittedDisplayNotes` has a last-resort full `gatherCommittedEvents` + reconstruct when the cache is dirty/empty and the window path cannot run (**A**).

Same consumer rule as LED: if notes are empty, paint stale/empty and let idle fill. Not the first slice.

### NOTE_EDIT hydrate — separate session design

`EditManager::openNoteEditSession` builds three representations:

```text
openNoteEditSession
    → rebuildVisualCacheFromPasses
    → rematerializeEditView
    → midiEvents()
        → ensureEffectiveEventStoreCurrent
        → materializeToEventVector
```

Later, `EditManager::materializedLoopEventsForNoteEditFocus` materializes again on revision change. Session undo rematerializes on each undo.

Do **not** fold this into the idle-slice or `ensureVisualCacheBuilt` audit. NOTE_EDIT needs authoritative session content, not display stale-while-revalidate.

Later design (not this file’s next slice):

```text
NOTE_EDIT open
    ↓
create edit-hydrate session
    ↓
consume prepared content where possible
    ↓
resume across loop()
    ↓
editable session becomes ready
```

Not `resolveWindow` on the button path. Not in [`114736`](../../captures/session_20260816_114736.log).

### `shouldRestoreCommittedOverlapOnOverdubStop` — investigate before replace

`passes.materializeToEventVector` of the whole loop to answer “is this pitch sounding” on the overdub-stop path (**A** or **B**, stop-path). Overdub source view and `visualCache.notes` already hold a note list.

The question is not “can I find the pitch in `visualCache.notes`?” It is: **does the existing source-view representation contain precisely the temporal/sounding information this predicate needs at overdub stop?**

If yes, replace the flatten with a note-list read. That removes a synchronous whole-loop derivation without a new resumable subsystem. If no, leave it and record the missing field. Investigate after the idle **C** slice, before any global `ensureVisualCacheBuilt` change.

### Playback merge — parked 6.3

`ensurePlaybackMergedMidiEventsBuilt` still gathers (2-bar window on long loops, full gather on short). Called from clock/slot launch and from load-frame first-commit prewarm. Do not start this.

### Cold / idle-only leftovers

| Function | Flatten | Already deferred? |
|----------|---------|-------------------|
| `Track::processDeferredStoredMidiVerification` | first slice `mergeActiveCapturePasses`, then reconstruct for `DNTE` | Yes — idle, one-shot per boot |
| `Loop::liveEventCount` | full `materializeToEventVector` for a count | Only if something calls `Track::getMidiEventCount` |
| LCR device gate | own sliced build | STOPPED only; skipped during PLAYING |

Do not fold these into the PLAYING scheduler pass.

## Approved slices

### Slice 0 — LED Stage 1 (done)

Precedent for the consumer rule. Stage 2 rejected. Do not touch `MidiLedManager` lookup unless a later capture shows one-bar LED lag that product rejects.

### Slice 1 — split `load_frame` telemetry (device PASS [`133314`](../../captures/session_20260816_133314.log))

**Measurement only. Zero behavior change.**

Child `loop_rem` from `runDeferredLoadAndDisplayFrame` via `RuntimeTimingTelemetry::recordLoadFrameChildRem` (`FLASHMEM` + `PSTR`). Parent `load_frame` rem + 5 s window kept.

| Span | Owner |
|------|--------|
| `display_frame` | cadence `DisplayManager::update()` (not the boot paint) |
| `load_job` | `DeferredJobScheduler::runFrame()` |
| `first_commit` | `Track::ensurePlaybackMergedEventsForSlot()` |
| `boot_commit` | `finishBootSetup` / USB host / boot OLED |

**5 s child windows withdrawn.** Plan originally added child maxes so work under 50 ms still shows. Those tags and Snapshot fields grew `.rodata` / DTCM. Child attribution is `loop_rem` only (50 ms one-shot). Parent `load_frame` 5 s window remains.

**Device FAIL [`132439`](../../captures/session_20260816_132439.log):** title → `BOOT,load,start` → `BOOT,scan,start` → reset. Same class as LoopPersist finalize ([`persist_loop_slot_finalize_slice_bugfix.md`](persist_loop_slot_finalize_slice_bugfix.md)): hung at `scan,start` with no `scan,t0`. Cause: Slice 1 child span literals in `.rodata` (DTCM) plus four 5 s accumulators. LED rem already documents this — `recordMidiLedHelperRem` uses `PSTR` so strings stay in `.progmem`. After the `PSTR` helper and withdrawn 5 s child windows: RAM1 variables **91808**, code **425612**, padding **372**, locals **6496** (crashing Slice 1 build was **2368**).

Device gate **PASS** [`133314`](../../captures/session_20260816_133314.log): boot past `scan,start`; PLAYING 67–72 ms is `display_frame`; boot 792 ms is `boot_commit` 784 ms. Do not start a `LoadLoopJob` firmware change from a PLAYING `display_frame` line. Remaining boot **B** work is later (not Slice 2).

## Pre-implementation review (Slice 1)

### Ready
- Owner is `runDeferredLoadAndDisplayFrame`. Parent rem + 5 s window already exist.

### Resolved
| Topic | Decision |
|-------|----------|
| Behavior | Zero change — timers only |
| Parent rem | Keep `load_frame` |
| Boot OLED `update()` | Inside `boot_commit`, not a second `display_frame` |
| Undo/autosave/reclaim | Unattributed inside parent |
| 5 s child windows | Withdrawn — RAM1 / `.rodata` at [`132439`](../../captures/session_20260816_132439.log). Child spans are `PSTR` `loop_rem` only |

### Open before coding
None.

### Proceed?
YES

### Slice 2 — one resolution source on idle visual-cache rebuild (first behavioral)

**After Slice 1.** [`114736`](../../captures/session_20260816_114736.log) still holds in [`133314`](../../captures/session_20260816_133314.log).

When `tryResolvePreparedWindow` succeeds, `rebuildVisualCacheIdleSlice` must not call `appendOverdubPassDisplayNotes` as a second reconstruction of an **interior** window.

Owner: `Loop::rebuildVisualCacheIdleSlice`. LCR remains idle-only.

**Native (landed):** `test_prepared_linear_overdub_matches_lcr_plus_append` match. `test_prepared_wrap_held_overdub_lcr_append_delta` documents the wrap-held gap. Idle rebuild keeps wrap-held notes (`test_idle_slice_prepared_keeps_wrap_held`) and linear notes (`test_idle_slice_prepared_linear_matches_lcr_only`).

Device: no `VCACHE` note-loss vs a same-loop capture that used the append path. Wrap-edge append stays until LCR pairing owns wrap-held overdub heads.

**Device [`134329`](../../captures/session_20260816_134329.log) + [`134701`](../../captures/session_20260816_134701.log).** [`134329`](../../captures/session_20260816_134329.log) cut off during LCR idx. [`134701`](../../captures/session_20260816_134701.log) is the same boot (micros continue at 86.7 s).

6A.1 compare at 109.362 s (`Track::` deferred 6a, not idle-slice 6a): `match=1` `miss=0` `extra=0` `pmatch=1` `gmatch=0` — same as [`025651`](../../captures/session_20260816_025651.log). Window `ev=117` `notes=52`. LCR `hist=1493` equals [`134329`](../../captures/session_20260816_134329.log) last 64-bar `slice_clean` 1493. After that, `idle_maint` is 285 µs (cache already clean). No `VCACHE` in [`134701`](../../captures/session_20260816_134701.log). No idle-slice `6a` (`win,proj,tot` without `oracle`). The interior skip did not run — visual cache was not dirty after prepare.

Slice 2 device gate still needs a dirty idle slice after LCR is prepared (overdub stop / `stale` while `preparedWindowReady`).

STOPPED 64-bar count 1639 → 1456 in [`134329`](../../captures/session_20260816_134329.log) (8.765–30.994 s) is on the gather+append path (before LCR idx). Not a Slice 2 result. [`133314`](../../captures/session_20260816_133314.log) same loop started at 1643 and stayed ~1630+ until overdub.

## Pre-implementation review (Slice 2)

### Ready
- Owner is `Loop::rebuildVisualCacheIdleSlice`. Display `appendOverdubPassDisplayNotes` is out of scope.

### Resolved
| Topic | Decision |
|-------|----------|
| Linear prepared window | Skip append |
| Wrap-held delta | Documented — LCR-only drops tail/head; append stays on bar 0 / last bar |
| Merged wrap pairing | Forbidden (`015618`) |
| Unprepared gather | Still appends |

### Open before coding
None.

### Proceed?
YES

### Later (not authorized)

3. Investigate `shouldRestoreCommittedOverlapOnOverdubStop` — semantic dependency, then replace if the source-view list is sufficient.
4. Audit `ensureVisualCacheBuilt` callers one at a time. Do not globally delete.
5. NOTE_EDIT hydrate — own session design.
6. Remaining `load_frame` / boot **B** work — only after Slice 1 names the child. Boot 800–900 ms is a different class from PLAYING 60–70 ms paint.

Each firmware slice: one owner, one invariant, one stalker class, native test, `pio test -e native`. Do not bundle.

## Architecture checkpoint (per slice, before firmware)

1. Ownership change? Extending the named owner: **NO**. New Session / Manager / LCR call on a transport or BAR path: **YES** — stop.
2. State transition change? Telemetry-only, or removing a second idle reconstruct: **NO**. Deferring NOTE_EDIT open or overdub stop until hydrate finishes: **YES** — design session.

Reuse: extend the existing owner. Do not add `tryResolvePreparedWindow` on MIDI-sensitive paths.

## Does not start

- 6.3 long-loop playback gather
- Re-arm PLAYING drain
- Interval reservation
- `tryResolvePreparedWindow` from `MidiLedManager`, `handleMidiInput`, `commitLoadLoopJobPublish`, `startOverdubbing`, or `stopOverdubbing`
- Deleting `materializeToEventVector`
- Global deletion of `ensureVisualCacheBuilt`
- Folding NOTE_EDIT open into Slice 2 or the visual-cache audit
- Visual-cache idle grain change (2–4 bars) until the idle notes source is single
- LoopPersist CRC
- Optimizing `LoadLoopJob` from an unsplit PLAYING `load_frame` line

## Consumer-path pattern

```text
                    derived content
                         │
          ┌──────────────┼──────────────┐
          ↓              ↓              ↓
       display          LED          overdub
          │              │              │
       idle slice     BAR path       prepared consume
          │              │              │
       bounded        Stage 1        bounded/
       rebuild        no gather      prepared
```

LED Stage 1 is the template: the consumer reads notes that already exist; resume stays on `rebuildVisualCacheIdleSlice`. `load_frame` is the next named span, but it is a bundle — split the timer before changing hydrate or paint.
