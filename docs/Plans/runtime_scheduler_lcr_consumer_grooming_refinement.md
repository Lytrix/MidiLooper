# Runtime scheduler — LCR consumer grooming

**Status:** Active — Slice 4d NOTE_EDIT idle paint consumes stale
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
| STOPPED idle | not MIDI-sensitive | Slice 4c: always `rebuildVisualCacheIdleSlice` |
| `EditManager::openNoteEditSession` | user action | own hydrate contract — see NOTE_EDIT below |
| `DisplayManager::refreshViewportAfterOverdubStop` | MIDI-sensitive | Slice 4: keep/adopt only; idle fills |
| Display committed resolve | paint path | Slice 4b: no `ensureVisualCacheBuilt`; last-resort gather remains |
| NOTE_EDIT session-idle `resolveDisplayNotes` | paint path | Slice 4d: consume stale/empty; idle fills |
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

### `shouldRestoreCommittedOverlapOnOverdubStop` — Slice 3 closed

`passes.materializeToEventVector` of the whole loop answered “is this pitch sounding” on the overdub-stop path. DEC-031 G2 already made `overdubSourceView` authoritative; `finalizePendingNotes` skipped the predicate when the view existed. `beginCapture(Overdub)` always establishes the view, so the flatten was unreachable on production stop.

Slice 3 removes the flatten. The function returns false. Do not read `visualCache.notes` here. Do not call LCR from stop.

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

Slice 2 skip **exercised** in [`134955`](../../captures/session_20260816_134955.log) (same boot, micros 382 s+). 88 idle-slice `6a` lines (`win,proj,tot` — no `oracle`). No `VCACHE,full`. Coverage stays `first,0,last,63`.

| Event | `slice_clean` notes |
|-------|--------------------:|
| [`134329`](../../captures/session_20260816_134329.log) last append-path / [`134701`](../../captures/session_20260816_134701.log) LCR `hist` | 1493 |
| First prepared full rebuild @ 397.221 s | 1476 |
| Next prepared rebuild | 1469 |
| After overdub stop | 1473, then 1556 |

Interior slices: `ev=176` `notes=86` `tot≈9.6 ms`. One wrap-edge slice: `notes=500` `tot=39.6 ms`. PLAYING `clockrate` 47–48.

1493 → 1476 is **not** Slice 2 note-loss. [`134955`](../../captures/session_20260816_134955.log) `MIDI: Undo` kind=1 `Overdub undone @ tick 1344` at 396.299 s, then the 6a burst and `slice_clean` 1476 at 397.221 s. Second undo at 397.747 s (`@ tick 1904`) then `stale` 1476 → `slice_clean` 1469. Overdub stops later add notes (1469 → 1556). **Device PASS [`135551`](../../captures/session_20260816_135551.log)** (same boot, no `Undo` / `undone`). Overdub stop dirties the 64-bar cache; idle-slice `6a` (`win,proj,tot`) rebuilds; count rises; coverage stays `0–63`. PLAYING `clockrate` 47–48. No `VCACHE,full`.

| Event | Notes |
|-------|------:|
| Pre-stop `stale` | 1592 |
| After 2× `6a` `slice_clean` | 1603 |
| Next pre-stop `stale` | 1603 |
| After 5× `6a` `slice_clean` | 1618 |

No undo between dirty and clean. Prepared consume added the overdub; it did not drop the loop.

STOPPED 64-bar count 1639 → 1456 in [`134329`](../../captures/session_20260816_134329.log) (8.765–30.994 s) is on the gather+append path (before LCR idx). Not a Slice 2 result. [`133314`](../../captures/session_20260816_133314.log) same loop started at 1643 and stayed ~1630+ until overdub.

### Slice 3 — retire restore flatten on overdub stop

**After Slice 2.** Investigate `shouldRestoreCommittedOverlapOnOverdubStop`, then remove the flatten if a note list already owns the question.

**Investigation (code):**

| Fact | Proof |
|------|-------|
| Predicate used whole-loop `materializeToEventVector` + reconstruct | `shouldRestoreCommittedOverlapOnOverdubStop` before this slice |
| Caller already skips when the view exists | `Track::finalizePendingNotes`: `!hasOverdubSourceView()` |
| Production overdub always has the view | `beginCapture(Overdub)` → `establishOverdubSourceView` always sets `overdubSourceViewEstablished_` |
| View still live at finalize | `finalizePendingNotes` runs before `commitCapturePass` → `clearOverdubSourceView` |
| Source-view notes cannot replace the fallback | No view ⇒ notes empty |
| `visualCache.notes` is not overlap authority | DEC-031 G2; display cache may be dirty / wrap-incomplete |

**Firmware:** `shouldRestoreCommittedOverlapOnOverdubStop` returns false and does not materialize. Missing-view sessions keep the Add and synthesize NoteOff (same as G2 when the view exists).

**Native:** `test_overdub_begin_makes_restore_flatten_unreachable` — `beginCapture(Overdub)` establishes the view with `committedEventsFullMaterializeCount() == 0`.

This does not cut PLAYING `clockrate` or OLED paint. It removes the last whole-loop flatten from the restore function so a missing-view stop cannot stall.

**Device PASS [`140841`](../../captures/session_20260816_140841.log).** Boot `scan,start` → `done`. Four overdub stops; every `Stop finalize pending` has `overlap_restore=0`. Enter→display 7.0–8.8 ms; seal stage 1.9–3.3 ms. PLAYING `clockrate` 47. No `VCACHE,full`. Cache after stops: 1469→1479→1530→1543→1608, coverage `0–63`. One undo at 65.973 s after the last stop (`Overdub undone`).

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

## Pre-implementation review (Slice 3)

### Ready
- Owner is `shouldRestoreCommittedOverlapOnOverdubStop`. Caller already G2-gated.

### Resolved
| Topic | Decision |
|-------|----------|
| Source-view sufficient? | Yes for established sessions — those already skip this function |
| Replace with `visualCache.notes`? | No |
| Firmware | Return false; no `materializeToEventVector` |

### Open before coding
None.

### Proceed?
YES

### Slice 4 — overdub-stop viewport must not full-rebuild (first `ensureVisualCacheBuilt` / `rebuildVisualCacheFromPasses` caller)

**After Slice 3.** One caller only.

`DisplayManager::refreshViewportAfterOverdubStop` called `rebuildVisualCacheFromPasses` on short loops (`loopLength ≤ 16` bars). Long loops already kept the loop-wide list or adopted the composed frame. PLAYING idle already runs `rebuildVisualCacheIdleSlice`.

[`020910`](../../captures/session_20260814_020910.log) / [`021959`](../../captures/session_20260814_021959.log) stale paint was **adopt_partial** replacing the loop-wide list with the viewport. Keep existing `visualCache.notes` when non-empty.

**Firmware:** remove the short-loop `rebuildVisualCacheFromPasses` branch. Same keep/adopt/clear path for every loop length. Do not call `ensureVisualCacheBuilt` here. Do not touch NOTE_EDIT open.

**Native:** `test_short_loop_stale_keeps_notes_for_overdub_stop_handoff` — 4-bar `markDisplayCachesStale` keeps notes.

**Device PASS [`141425`](../../captures/session_20260816_141425.log)** — 4-bar loop (`first,0,last,3,total,4`). The only `VCACHE,full` is boot @ 6.967 s (89 notes). Three overdub stops: `overlap_restore=0`; cache stays dirty with the prior count then idle `slice_clean` rises (89→119, 119→120, 139→141). No `VCACHE,full` on stop. Where `ODUB,stop,display` is present, enter→display is 8.8 ms. PLAYING `clockrate` 47. `RING,overflow` dropped some stop CAP lines; INFO still has `Overdub stopped` at 28.028 / 36.413 / 49.825 s. [`140841`](../../captures/session_20260816_140841.log) was 64-bar (already skipped).

## Pre-implementation review (Slice 4)

### Ready
- Owner is `DisplayManager::refreshViewportAfterOverdubStop`. Idle slice already fills PLAYING dirty bars.

### Resolved
| Topic | Decision |
|-------|----------|
| Caller | Short-loop stop rebuild only |
| Keep vs adopt | Keep when notes non-empty |
| NOTE_EDIT | Out of scope |

### Open before coding
None.

### Proceed?
YES

### Slice 4b — committed display resolve must not call `ensureVisualCacheBuilt`

**After Slice 4.** One caller only.

`DisplayManager::resolveDisplayNotesCommitted` called `ensureVisualCacheBuilt` on short loops when `deferVisualRebuild` was false (STOPPED, other-slot overdub) and again on empty dirty cache under undo/save pressure ([`234050`](../../captures/session_20260717_234050.log)). Overdub focus paint is `resolveDisplayNotesLiveCapture` — not this function. PLAYING already deferred.

**Firmware:** remove both `ensureVisualCacheBuilt` calls. Paint uses authoritative cache, incremental/handoff, or the existing last-resort gather (later slice). Idle owns rebuild.

**Native:** `test_committed_display_visual_cache_authoritative` — dirty + notes is not authoritative.

**Device PASS [`142100`](../../captures/session_20260816_142100.log)** — 4-bar loop. The only `VCACHE,full` is boot @ 6.271 s (141 notes). None after PLAYING starts @ 26.342 s. Two overdub stops: enter→display 10.0 ms and 8.6 ms; `overlap_restore=0`; idle `slice_clean` 129→133 and 151→152. PLAYING/OVERDUB `clockrate` 47. STOPPED undos before play used `slice_clean` (139/120/119), not `VCACHE,full`.

## Pre-implementation review (Slice 4b)

### Ready
- Owner is `DisplayManager::resolveDisplayNotesCommitted`.

### Resolved
| Topic | Decision |
|-------|----------|
| Both ensure sites | Remove |
| Last-resort gather | Leave |
| NOTE_EDIT | Out of scope |

### Open before coding
None.

### Proceed?
YES

### Slice 4c — STOPPED idle must not call `ensureVisualCacheBuilt`

**After Slice 4b.** One caller only.

`Track::processDeferredIdleMaintenance` called `ensureVisualCacheBuilt` on short loops when not under save/hydrate pressure. Long loops and heavy defer already used `rebuildVisualCacheIdleSlice`. [`142100`](../../captures/session_20260816_142100.log) STOPPED undos already `slice_clean` under save pressure.

**Firmware:** STOPPED dirty visual cache always calls `rebuildVisualCacheIdleSlice` (2 bars if deferred save, else 4). Do not call `ensureVisualCacheBuilt`. Leave `getVisualNotesForSlot`, NOTE_EDIT `resolveDisplayNotes`, and `prewarmSelectedDisplayVisualCache`.

**Native:** `test_short_loop_idle_slice_cleans_without_full_rebuild` — 4-bar stale → one 4-bar slice → clean, note kept.

**Device PASS [`142548`](../../captures/session_20260816_142548.log)** — 4-bar loop. Zero `VCACHE,full` (boot load used `slice_clean`, not `full`). Seven STOPPED undos: stale then `slice_clean` 152→151→133→133→129→122→119→89. After empty overdub stop @ 29.268 s: stale 89 → `slice_clean` 90. Later stops: 90→109, 109→111. PLAYING/OVERDUB `clockrate` 47–48.

## Pre-implementation review (Slice 4c)

### Ready
- Owner is `Track::processDeferredIdleMaintenance`. Slice function already exists.

### Resolved
| Topic | Decision |
|-------|----------|
| Short-loop STOPPED | Always slice |
| Grain | Unchanged (2 / 4 bars) |
| NOTE_EDIT / `getVisualNotesForSlot` | Out of scope |

### Open before coding
None.

### Proceed?
YES

### Slice 4d — NOTE_EDIT session-idle paint must not call `ensureVisualCacheBuilt`

**After Slice 4c.** One caller only.

`DisplayManager::resolveDisplayNotes` called `ensureVisualCacheBuilt` when session type is Note and the session is not active. `sendEditSessionChange(Note)` sets the type before `openNoteEditSession`. Overdub-stop paint in that window is MIDI-sensitive.

**Firmware:** assign existing `visualCache.notes` (stale or empty). Do not call `ensureVisualCacheBuilt`. Leave `getVisualNotesForSlot` (NOTE_EDIT projection / Slice 5) and `prewarmSelectedDisplayVisualCache` (no production callers). Long-loop window gather unchanged (4e).

**Native:** `test_note_edit_idle_paint_consumes_stale_visual_cache` — 4-bar stale keeps notes without `ensureVisualCacheBuilt`.

**Device [`143144`](../../captures/session_20260816_143144.log) — 4d gate not isolatable; NOTE_EDIT session FAIL on other owners.** Do not treat as 4d PASS. Do not start 4e.

Open `VCACHE,full` @ 7.813 s is `openNoteEditSession` (Slice 5). Mid-session `VCACHE,full` @ 48.744 s and 83.710 s follow select `VCACHE,stale` — `getVisualNotesForSlot` (left in 4d).

Sluggish select/pitch is not 4d:
- Select: `UNDO_WARM,warm,complete` 128–451 ms on each note change (`focus_snap` 68–272 ms).
- Pitch/move: `GEOM_APPLY,resolve` n=209, median 84 ms, max 162 ms.

Exit did save rows (`saved=3` @ 88.786 s, `saved=1` @ 110.985 s). Display after first exit is stale 111 then `slice_clean` 112. Commits already report the painted note missing: `M24@120 missing in recon` / `M24@1656 missing in recon` on replay_flat, session_store, and loop_materialized. That is NOTE_EDIT commit/recon, not session-idle paint.

## Pre-implementation review (Slice 4d)

### Ready
- Owner is `DisplayManager::resolveDisplayNotes` session-idle branch.

### Resolved
| Topic | Decision |
|-------|----------|
| `getVisualNotesForSlot` | Leave — NOTE_EDIT projection |
| `prewarmSelectedDisplayVisualCache` | Leave — no production callers |
| Idle paint | Consume stale/empty |

### Open before coding
None.

### Proceed?
YES

### Later (not authorized)

4e. Display fallback gather in `resolveDisplayNotesCommitted` / `rebuildDisplayNotesInWindow`.
4f. `getVisualNotesForSlot` / NOTE_EDIT projection — Slice 5 hydrate, not this audit.
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
