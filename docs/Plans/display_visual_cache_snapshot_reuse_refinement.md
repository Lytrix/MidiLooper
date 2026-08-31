# Display visual-cache snapshot reuse

**Status:** Stage 1 device PASS [`202745`](../../captures/session_20260831_202745.log). Stage 1c device FAIL [`205138`](../../captures/session_20260831_205138.log). Stage 1d no wrap-tick visual merge in tree. Stage 2 unblocked, not started. GitHub [#41](https://github.com/Lytrix/MidiLooper/issues/41).  
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
| **1b — wrap-pair length** | Visual-cache merge of a sealed wrap pass uses merged reconstruct (wrap pairing off). `appendOverdubPassWrapPairedNotes` does not add a longer sibling of an existing pitch+start. Wrap-held heads/tails still append. NOTE_EDIT reconstruct already used wrap pairing off; that paint must not be the only path that drops the extras. | Device FAIL [`204132`](../../captures/session_20260831_204132.log): second wrap occupy already has `192–592`; STOPPED DNTE is short (`192/16`, `336/112`); NOTE_EDIT open `rebuildVisualCacheFromPasses` then shows 291 notes and `presence=0`. |
| **1c — wrap-held display** | Visual-cache wrap append (`appendOverdubPassDisplayNotes`) keeps wrap-held heads/tails only. Interior wrap-paired body notes do not enter `visualCache`, including NOTE_EDIT open rebuild. Source view keeps full wrap pairing. | Device FAIL [`205138`](../../captures/session_20260831_205138.log): wrap 2 cache `205→271` then slice `260`. Isolated wrap-pass merge still lengthens until STOPPED. |
| **1d — no wrap-tick visual merge** | `appendLastCommittedOverdubPassToSourceView` does not merge the sealed pass into `visualCache`. Idle slice is the fill. Wrap-tick note count stays at the pre-wrap list. | In tree. Device recapture required. |
| **2 — snapshot fill** | A VisualCache slice whose dirty bars are covered by the published snapshot copies those `LoopSnapshotNote`s. No `materializeToEventVector` / `reconstructDisplayNotes` / `from=passes` on that slice. | Unblocked. Not started. This is former 4b.1b. |

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
5. Stage 1d no wrap-tick visual merge — in tree. Do not start Stage 2 until CURRENT_WORK says so. Do not check off OpenSpec `3.5` / `3.6`.

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
| Stage 2 | Not this session |

### Open before coding
None.

### Proceed?
YES
