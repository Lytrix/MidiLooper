# Display visual-cache snapshot reuse

**Status:** Stage 1 device PASS [`202745`](../../captures/session_20260831_202745.log). Stage 1d wrap-tick visual merge closed (`d35d64a7`). Stage 1e idle-slice current fill in tree ([`display_visual_cache_idle_slice_current_fill_bugfix.md`](display_visual_cache_idle_slice_current_fill_bugfix.md)). Stage 1f STOPPED paint current fill device FAIL [`220337`](../../captures/session_20260831_220337.log) and [`221925`](../../captures/session_20260831_221925.log) (**128 / 137**). Stage 1g wrap dirty span in tree ([`display_visual_cache_wrap_dirty_span_bugfix.md`](display_visual_cache_wrap_dirty_span_bugfix.md)); [`221925`](../../captures/session_20260831_221925.log) wrap `dcnt` inconclusive. Stage 1h held-note wrap pair shipped (`4238b422`). Stage 1i wrap/stop first paint shipped (`42b00cde`). Stage 1j stop handoff playhead tail native **1526/1526** ([`display_visual_cache_stop_handoff_playhead_tail_bugfix.md`](display_visual_cache_stop_handoff_playhead_tail_bugfix.md)). Stage 1k wrap compose hold grow-vs-compose in tree ([`display_visual_cache_wrap_compose_hold_bugfix.md`](display_visual_cache_wrap_compose_hold_bugfix.md)). Stage 1l Off@0 leftover tail pair in tree. Stage 1b same-start shortest end in tree. Stage 1m stop compose adopt in tree ([`display_visual_cache_stop_compose_adopt_bugfix.md`](display_visual_cache_stop_compose_adopt_bugfix.md)). Stage 2 unblocked, not started. GitHub [#41](https://github.com/Lytrix/MidiLooper/issues/41).  
**Date:** 2026-08-31  
**Kind:** refinement (Stage 1 is a wrap-stale bugfix; Stage 2 is 4b.1b snapshot fill)  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41) — one work item, two stages. Work: [Work](https://github.com/users/Lytrix/projects/1)  
**Parent inventory:** [`playback_window_build_owner_boundary_refinement.md`](playback_window_build_owner_boundary_refinement.md) § 4b.1b now points here.  
**Evidence:** [`192422`](../../captures/session_20260831_192422.log) wrap `VCACHE,stale,notes=255,dsz=0,dcnt=0`; `loop_rem,idle_maint,50450` + `lcr,src,why=idle,from=passes`.

**Does not start:** all-track idle fan-out, `T4 track_update`, N / pages knobs, OpenSpec `3.5` / `3.6` checkoff, LCR as a second `visualCache`, `overdubSourceView` consume authority, cooperative `reconstructDisplayNotes`.

---

## One work item

[#41](https://github.com/Lytrix/MidiLooper/issues/41) is the capability: `visualCache` stays the piano-roll list; published `LoopSnapshotNote` membership becomes the committed fill source.

| Stage | Invariant | Status |
|---|---|---|
| **1 — wrap-stale** | After wrap commit (and the following `beginCapture(Overdub)`), `visualCache.notes` stay. `dirtyBars` is the affected bar set, never empty-then-all while notes exist. After wrap + `appendLastCommittedOverdubPassToSourceView`, `overdubSourceSpanCacheReady()` is true so idle does not `from=passes`. Sealed wrap-pass notes merge into `visualCache`; overdub paint assigns the grown cache instead of the pre-wrap hold. | Device PASS [`202745`](../../captures/session_20260831_202745.log). Three wraps; no wrap `dsz=0` while `notes>0`; only `lcr,src` is `why=open,from=visual`. First wrap `DISP` `183,111` → next frame `184,180`. Second wrap first paint `249,246` (not a hold of 180). |
| **1b — wrap-pair length / same-start** | Visual-cache merge of a sealed wrap pass uses merged reconstruct (wrap pairing off). `appendOverdubPassWrapPairedNotes` does not add a longer sibling of an existing pitch+start. `dedupeProjectedDisplayNoteVec` unique-keys pitch+start (shortest end). Idle splice skips an existing pitch+start. | In tree. Device FAIL [`231953`](../../captures/session_20260831_231953.log): reconstruct 33 same-start stacks. Sibling: [`display_visual_cache_same_start_shortest_end_bugfix.md`](display_visual_cache_same_start_shortest_end_bugfix.md). |
| **1c — wrap-held display** | Visual-cache wrap append (`appendOverdubPassDisplayNotes`) keeps wrap-held heads/tails only. Interior wrap-paired body notes do not enter `visualCache`, including NOTE_EDIT open rebuild. Source view keeps full wrap pairing. | Device FAIL [`205138`](../../captures/session_20260831_205138.log): wrap 2 cache `205→271` then slice `260`. Isolated wrap-pass merge still lengthens until STOPPED. |
| **1d — no wrap-tick visual merge** | `appendLastCommittedOverdubPassToSourceView` does not merge the sealed pass into `visualCache`. Idle slice is the fill. Wrap-tick note count stays at the pre-wrap list. | Shipped `d35d64a7`. Device FAIL [`205917`](../../captures/session_20260831_205917.log): idle Overlay fill left longs in cache after wrap 2. |
| **1e — idle-slice current fill** | Idle accepts current-root Snapshot only. Overlay is discarded; fill continues to prepared LCR or budgeted `gatherCommittedEventsInWindow`. Overlay never clears dirty bars. First eligible neighborhood is current committed content. | Native **1518/1518**. RAM1 locals **6944**. Device recapture required. Sibling: [`display_visual_cache_idle_slice_current_fill_bugfix.md`](display_visual_cache_idle_slice_current_fill_bugfix.md). |
| **1f — STOPPED paint current fill** | Last-resort `resolveDisplayNotesCommitted` accepts current-root Snapshot only. Overlay is discarded; gather continues to `gatherCommittedEvents`. First STOPPED `DISP` frame is current committed content, not Overlay reconstruct. | Native **1519/1519**. RAM1 locals **6944**. Device FAIL [`220337`](../../captures/session_20260831_220337.log): first STOPPED **276 / 344**; later PLAYING **334 / 334**. Sibling: [`display_visual_cache_stopped_paint_current_fill_bugfix.md`](display_visual_cache_stopped_paint_current_fill_bugfix.md). |
| **1g — wrap dirty span** | Wrap-held `endTick < startTick` dirties the On bar and Off bar only. Inclusive wrap fill must not mark interior bars the wrap did not change. Linear pass notes still dirty the bars they occupy. | Native **1521/1521**. RAM1 locals **6944**. Device [`221925`](../../captures/session_20260831_221925.log): 4-bar loop, wrap `dcnt=4` (linear 39/45/51 in every bar — not a wrap-interior fail). Wrap 1–2 `slice_clean`. Wrap 3 no fill; STOPPED **128 / 137**; `DNTE` 39@48/432 **144+256**. Sibling: [`display_visual_cache_wrap_dirty_span_bugfix.md`](display_visual_cache_wrap_dirty_span_bugfix.md). |
| **1h — held-note wrap pair** | `Off@0` wrap-splits a tail-window On only. A mid-loop On with a linear Off is never drawn to `loopLength`. Overdub held tails still close at playhead. | Native **1523/1523**. RAM1 locals **6944**. Sibling: [`display_visual_cache_held_note_wrap_pair_bugfix.md`](display_visual_cache_held_note_wrap_pair_bugfix.md). |
| **1i — wrap/stop first paint** | First wrap OVERDUBBING `DISP` holds the pre-wrap committed count while cache is dirty. First STOPPED selected-slot `DISP` holds dirty nonempty `visualCache` when handoff is gone. | Native **1525/1525**. Device FAIL [`230400`](../../captures/session_20260831_230400.log): wrap 1 `105→66` (cache 63). Hold is committed-prefix only; sealed capture is gone. Sibling: [`display_visual_cache_wrap_stop_first_paint_bugfix.md`](display_visual_cache_wrap_stop_first_paint_bugfix.md). |
| **1j — stop handoff playhead tail** | Live overdub may paint open notes to `currentTick`. `liveDisplayNotes` stays at capture-preview lengths. STOPPED RC5a handoff does not freeze playhead-extended ends. | Device [`231953`](../../captures/session_20260831_231953.log): first STOPPED **160 / 148**; frame dump 160; no frame note ends at stop **1168**. Sibling: [`display_visual_cache_stop_handoff_playhead_tail_bugfix.md`](display_visual_cache_stop_handoff_playhead_tail_bugfix.md). |
| **1k — wrap compose hold** | First wrap OVERDUBBING `DISP` keeps the last composed `liveDisplayNotes` while cache is dirty. Idle may grow cache past `max(committedCount, composed size)`, not the pre-wrap prefix. | Grow-vs-compose hole in tree. Device wrap 1 recapture required after [`232910`](../../captures/session_20260831_232910.log) `89>63` dropped compose 104. Sibling: [`display_visual_cache_wrap_compose_hold_bugfix.md`](display_visual_cache_wrap_compose_hold_bugfix.md). |
| **1l — Off@0 leftover tail pair** | Leftover `F@0` wrap-splits a leftover tail-window On (`exclusiveEndForLoopOff(L-1)`) before `finishCanonicalSpansOpenNotes`. Mid-loop linear pairs stay linear (1h). Do not call `tryPairWrappedTailOn` for leftover. | In tree. Device [`231953`](../../captures/session_20260831_231953.log): leftover-to-`loopLength` on 39/59. Sibling: [`display_visual_cache_off_zero_tail_pair_bugfix.md`](display_visual_cache_off_zero_tail_pair_bugfix.md). |
| **1m — stop compose adopt** | Short-loop compose larger than `visualCache` is adopted onto that loop before hop-clear, on wrap-hold, and on RC5a. Full-loop idle must not replace a larger cache (`slice_keep`). | In tree. Device [`000013`](../../captures/session_20260901_000013.log): hop-clear 141→31; Prepared 95→80. Sibling: [`display_visual_cache_stop_compose_adopt_bugfix.md`](display_visual_cache_stop_compose_adopt_bugfix.md). |
| **2 — snapshot fill** | A VisualCache slice whose dirty bars are covered by the published snapshot copies those `LoopSnapshotNote`s. No `materializeToEventVector` / `reconstructDisplayNotes` / `from=passes` on that slice. | **2.1 in tree** (copy when current). **2.0 load enqueue** not started. Plan: [`display_visual_cache_snapshot_fill_refinement.md`](display_visual_cache_snapshot_fill_refinement.md). Former 4b.1b. |

Stage 2 first would copy snapshot notes, then wrap would wipe them again.

---

## Stage 1 root cause

Wrap `commitCapturePass(OverdubWrap)` / `commitStopFinalizeFromStore` call `notifyCommittedContentChanged` → `Loop::markPassDerivedStale`. After `slice_clean`, `dirtyBars` is empty, so stale emits `dsz=0`. Idle then `markAllVisualCacheBarsDirty`. Reconstruct runs with `allowOverdubWrapAppend = false`, so wrap-crossing overdub notes disappear until `append_resume`. `playbackRevision++` invalidates the source-span cache; the same VisualCache turn does `rebuildOverdubSourceView("idle")` `from=passes`.

Overdub **button** start already must not `markDisplayCachesStale` (DEC-036 3b). Stop already uses `markAffectedDisplayCacheRanges`. Wrap does not.

**Architecture checkpoint:** ownership NO, musical transitions NO, Row-22 unchanged. Reuse `markAffectedDisplayCacheRanges`.

---

## Stage 1 change

- Wrap publish marks affected bars via `markAffectedDisplayCacheRanges`, not `markPassDerivedStale`.
- `markPassDerivedStale`: nonempty notes + empty `dirtyBars` must not emit a zero-size dirty set.
- Idle: empty `dirtyBars` + nonempty notes must not mean `markAllVisualCacheBarsDirty`.
- Do not `visualCache.notes.clear()` / `invalidateDisplayCaches` on wrap or overdub start.
- `appendLastCommittedOverdubPassToSourceView` merges the wrap-pass notes into `overdubSourceSpanCacheNotes_` and restamps `valid` / `playbackRevision` / `fullLoop`. O(pass) only — not a full-loop span-cache reconstruct on the wrap tick.
- Same pass notes merge into `visualCache.notes` by merged-reconstruct geometry (wrap pairing off). Overdub paint assigns a grown cache (`shouldAssignGrownVisualCacheOverOverdubHold`) instead of holding the pre-wrap record-only layer.
- `appendOverdubPassWrapPairedNotes` skips a candidate when pitch+start is already present, so wrap-edge idle cannot add a longer sibling of a body note (`202745` DNTE 39/45/51 at 208, 256, and 304).

Native: wrap a clean 16-bar cache; note count holds; `dirtyBars` is the wrap-pass span. After wrap-pair restamp fail, append makes `overdubSourceSpanCacheReady()` true and keeps the record note. Record-stop `markDisplayCachesStale` unchanged.

Device PASS [`202745`](../../captures/session_20260831_202745.log): no wrap `VCACHE,stale` with `dsz=0` while `notes>0`; no wrap `lcr,src,why=idle,from=passes`; first overdub pass stays on the paint frame after wrap.

---

## Stage 2 change

Idle already gathers snapshot **events** then `reconstructDisplayNotes`. `LoopSnapshotNote` is already the display span. Copy membership for dirty bars. `tryGatherPublishedSnapshotEventsInWindow` is fallback when the published root does not cover those bars. Live capture and unpublished wrap journals stay an overlay.

Native: one dirty bar after a published root copies snapshot notes; reconstruct path is not taken.

---

## Sequence

1. Stage 1 wrap-stale firmware + native + RAM1 + commit — done (`46f3b1bf`).
2. Span-cache restamp (`0f675b85`) + wrap paint hold (`f11b982d`). Device PASS [`202745`](../../captures/session_20260831_202745.log).
3. Stage 1b wrap-pair length — device FAIL [`204132`](../../captures/session_20260831_204132.log).
4. Stage 1c wrap-held display — device FAIL [`205138`](../../captures/session_20260831_205138.log).
5. Stage 1d no wrap-tick visual merge — shipped `d35d64a7`. Device FAIL [`205917`](../../captures/session_20260831_205917.log).
6. Stage 1e idle-slice current fill — in tree.
7. Stage 1f STOPPED paint current fill — device FAIL [`220337`](../../captures/session_20260831_220337.log).
8. Stage 1g wrap dirty span — in tree.
9. Stage 1h held-note wrap pair — shipped `4238b422`.
10. Stage 1i wrap/stop first paint — shipped `42b00cde`.
11. Stage 1j stop handoff playhead tail — native **1526/1526**. Device recapture required after full `DNTE`/`WRAP`/frame dump.
12. Stage 1k wrap compose hold — grow compares to composed hold. Device wrap 1 recapture required ([`232910`](../../captures/session_20260831_232910.log) `104→92`).
13. Stage 1l Off@0 leftover tail pair — in tree.
14. Stage 1b same-start shortest end — in tree.
15. Stage 1m stop compose adopt — in tree.
16. Stage 2.1 membership copy — in tree. Do not start Stage 2.0 load enqueue without a confirmed load-transition. Do not check off OpenSpec `3.5` / `3.6`.

## Pre-implementation review

### Ready
- Wrap publish: `Loop::commitCapturePass(OverdubWrap)` → `notifyCommittedContentChanged` → `markPassDerivedStale`.
- Track wrap: `Track::commitOverdubWrapAtSessionStart` seals companions after publish.
- Stop already calls `markAffectedDisplayCacheRanges` after companion seal.
- Idle expand: `rebuildVisualCacheIdleSlice` when `dirtyBars.size() < totalBars`.
- `beginCapture(Overdub)` does not clear `visualCache.notes`.

### Resolved
| Topic | Decision |
|---|---|
| Wrap dirty mark | Reuse `markAffectedDisplayCacheRanges` at wrap publish and after companion seal |
| Empty dirty set | Do not emit `dsz=0`; idle does not `markAllVisualCacheBarsDirty` |
| Record-stop | `markDisplayCachesStale` unchanged |
| Wrap idle `from=passes` | Restamp existing span cache in `appendLastCommittedOverdubPassToSourceView`; do not reconstruct on the wrap tick |
| Wrap paint hold | Merge sealed pass into `visualCache`; assign grown cache over the pre-wrap hold |
| Wrap-pair length | Visual-cache merge uses wrap pairing off; wrap-pair append skips existing pitch+start |
| Wrap-held display | `appendOverdubPassDisplayNotes` is wrap-held only; source view keeps full wrap pairing |
| Idle Overlay | Playback Overlay stays valid; visual-cache idle discards it and fills from current committed |
| STOPPED paint Overlay | Last-resort committed paint discards Overlay and uses `gatherCommittedEvents`; `rebuildDisplayNotesInWindow` Overlay contract stays |
| Wrap dirty span | Wrap-held spans dirty On/Off bars only; linear spans stay inclusive |
| Stage 2 | Not this session |

### Open before coding
None.

### Proceed?
YES
