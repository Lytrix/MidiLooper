# Display draw-source reuse and wrap-seal overruns

**Status:** Host shipped — device recapture required  
**Date:** 2026-09-03  
**Kind:** bugfix  
**Evidence:** [`151552`](../../captures/session_20260902_151552.log)  
**Does not replace:** DEC-052 occupy predicate consumption, [#41](https://github.com/Lytrix/MidiLooper/issues/41) Stage 1 visual-cache slices  
**Does not authorize:** STOPPED `usbdisp` / `PERS`; wrap-seal redesign or off-clock `commitCapturePass`; `#41` Stage 2 snapshot fill of `visualCache`; `refreshVisualCacheAfterPassStateChange` on record stop

---

## Debugging boundary

```text
Track::commitOverdubWrap / DIAG,wrap.seal     ← RC-W (child timings only)
selectDrawSourceReuseKind + gatherDrawSourceBars + record-stop handoff
                                              ← RC-D1 / RC-D2 (implement)
visualCache idle slice / #41 Stage 2          ← do not start
moving wrap seal off the clock                ← design session, not this slice
```

Reuse is reuse of a **derived gather result**. It does not make `liveDisplayNotes` a new committed source owner.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — display stays `DisplayManager::resolveDisplayNotesByDrawSource`; wrap sealer stays `Track::commitOverdubWrap` |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** for display (LoadAndDisplay remainder). Wrap seal already lives on the clock |

Reuse: extend `selectDrawSourceReuseKind`, `resolveDisplayNotesByDrawSource`, `shouldGatherPublishedSnapshotForDrawSource`, `refreshViewportAfterRecordStop`. No new Manager. Prefer existing `DrawSourceReuseKind` values (Hit / SlideOneBar / Rebuild). Overlay-only reuse is a branch in `resolveDisplayNotesByDrawSource` (Hit + overlay-key mismatch).

## Layer contract (pin)

`gatherDrawSourceBars` already splits the frame:

1. **Committed source** — snapshot **or** `appendNotesIntersectingBar(visualCache)` for `[firstBar, lastBar]`. Then `committedEnd = out.size()`.
2. **Sealed overlay** — `shouldUnionSealedCapturePreviewForDrawSource` then upsert from `committedEnd` only. Sealed does **not** rewrite `[0, committedEnd)`.
3. **Preview overlay** — capture-active preview rows (open holds unique-by-id; closed rows upsert into the suffix).
4. **Pending transforms** — `applyPendingDrawSourceTransforms` after gather.
5. **Playhead tails** — `applyCapturePlayheadTails` after gather, paint-only.

**Sealed is a transient overlay, not part of the reusable committed prefix.** Overlay-only invalidation includes sealed-key changes. The reusable prefix is `[0, liveDisplayCacheCommittedNoteCount_)` from the last committed-source walk.

## Intended pipeline

```text
                 committed authority
                         │
                 draw-source predicate
                         │
                  selected source
                         │
                         ▼
               committed gathered bars
                         │
                 ┌───────┴────────┐
                 │                │
          content change     transient change
                 │                │
             Rebuild       reuse committed bars
                 │                │
                 └───────┬────────┘
                         ▼
              sealed + preview + pending
                    re-union
                         │
                         ▼
              playhead-dependent tails
                         │
                         ▼
                       draw
```

---

## RC-W — OD1 wrap `seal=91059` (measure only)

**Owner:** `Track::catchUpOverdubWrapPlaybackLedger` times `maybeCommitOverdubWrap`; work is `Track::commitOverdubWrap`.

[`151552`](../../captures/session_20260902_151552.log) L8562: `DIAG,wrap total=104656 seal=91059 pass_apply=1582 src_append=12015`. `sealUs` is the whole `maybeCommitOverdubWrap` span.

**This slice answers only:** which child owns the 91 ms?

Measure `commitCapturePass` / `sealPendingNoteChangesToEditPasses` / `beginCapture` as `commit` / `seal_pend` / `begin` on the existing `DIAG,wrap` line. Keep `pass_apply` / `src_append`. Do **not** redesign seal, defer `commitCapturePass`, or move seal off the clock.

---

## RC-D1 — per-bar redraw + record-stop empty frame

**Owner:** `selectDrawSourceReuseKind` + `refreshViewportAfterRecordStop` + inactive branch of `shouldGatherPublishedSnapshotForDrawSource`.

### Playhead-only Hit

Today, `windowStart` unchanged + `playheadBar` changed → **Rebuild**. `gatherDrawSourceBars` does `(void)playheadBar`. On a 16-bar full-loop window that is a full gather every bar crossing.

**Invariant:** a playhead-only Hit reuses the committed gathered source; all playhead-dependent tails/overlays continue to be applied after the gather.

### Record-stop continuity (two authorities, one seed)

[`151552`](../../captures/session_20260902_151552.log): last RECORDING composed frame is dropped after `stale_all` and clamp-to-empty. Inactive predicate `return !visualCacheNonempty` drops a current snapshot as soon as the first idle slice makes cache nonempty.

**Precedence when both exist:**

```text
current published snapshot selected by draw-source predicate
        ↓
use snapshot

otherwise
        ↓
retain last RECORDING composed prefix (handoff seed only)
```

The last RECORDING composed prefix is a record-stop continuity seed only; it does not become a new committed source representation or alter subsequent draw-source authority. Idle `rebuildVisualCacheIdleSlice` stays the fill owner. Do **not** call `refreshVisualCacheAfterPassStateChange` on the record-stop MIDI path.

### Inactive predicate truth table

Amends the closed-session row that treated any nonempty visual as authority, including dirty+current. Stale snapshot still must not win ([`092344`](../../captures/session_20260902_092344.log)).

- Session **closed**, snapshot **current**, visual **nonempty**, **clean** → visual (existing `010217`)
- Session **closed**, snapshot **current**, visual **nonempty**, **dirty** → **snapshot**
- Session **closed**, snapshot **current**, visual **empty** → **snapshot**; if no current snapshot, continuity seed
- Session **closed**, snapshot **stale**, visual **nonempty**, **clean** → visual
- Session **closed**, snapshot **stale**, visual **nonempty**, **dirty** → continuity seed, **not** stale snapshot

Active-session rows stay as they are (current → snapshot; stale+dirty/empty → last snapshot; stale+clean nonempty → visual).

Native: `test_display_window_utils` playhead-only Hit + the closed-session table above. Device: first PLAYING `DISP` after record equals last RECORDING frame; no `frame=0`.

---

## RC-D2 — OVERDUBBING resolve 32 ms (overlay-only reuse)

**Owner:** `resolveDisplayNotesByDrawSource` / `gatherDrawSourceBars`.

[`151552`](../../captures/session_20260902_151552.log) L12845–L12890: `snap=1 visual=251 preview=135 sealed=177 frame=646`, `DFRAME resolveUs=32204`.

### A. Committed-source invalidation → existing full gather

Snapshot identity / revision / current flag; visual-cache revision when visual is the selected source; window start / length; pitch range, slot, track; `playbackRevision` / `captureDisplayRevision`.

### B. Overlay-only invalidation → reuse committed prefix

`capturePreview.revision`, `pendingChangeGeneration`, sealed overlay keys (`sealedPreviewRevision` / `sealedPreviewCount` / `sealedCompanionOverlay`).

**Invariant:** a preview/pending/sealed-only invalidation may reuse the previously gathered committed bars. It must not cause any walk of snapshot/visual committed notes. Only the changed transient layers are re-evaluated against `[0, committedEnd)`.

Stop forcing `captureActive && SlideOneBar` → Rebuild once overlay-only reuse exists. `appendDisplayNoteUniqueById` stays as-is. If overlay union against a large prefix is still ≥ 8 ms, that is a **new measured RC**, not an index in this slice.

### Fixture contract

- committed source walk = 0
- sealed/preview walk = yes when those layers changed
- pending transform = yes when pending changed
- final union matches the full-gather oracle

Device acceptance: OVERDUBBING `DFRAME` `resolveUs` **under 8 ms** on a dense 16-bar pass when the window covers the loop and only preview/playhead/pending moved.

---

## Explicitly not this slice

- STOPPED `usbdisp` / `PERS`
- Redesign / defer / off-clock wrap seal
- `#41` Stage 2 snapshot fill of `visualCache`
- `refreshVisualCacheAfterPassStateChange` on record stop
- Record-start `idle_maint` 99 ms, `begin_capture` 10 ms
- New uniqueness index
- Promoting `liveDisplayNotes` or the RECORDING compose prefix to committed-source authority

## Pre-implementation review

### Ready

- `selectDrawSourceReuseKind` / `shouldGatherPublishedSnapshotForDrawSource` in `DisplayWindowUtils.h`
- `resolveDisplayNotesByDrawSource` / `gatherDrawSourceBars` in `DisplayNoteWindowGather.cpp`
- `refreshViewportAfterRecordStop` in `DisplayCacheLifecycle.cpp`
- Host twin `gatherDrawSourceDisplayNotes` in `test_loop_take_survival`
- `Track::commitOverdubWrap` / `overdubWrapTick`

### Resolved

| Topic | Decision |
|-------|----------|
| Playhead-only | Hit when committed keys match and `windowStart` is unchanged |
| Overlay-only | Hit kind + resolve re-applies overlays; no new `DrawSourceReuseKind` |
| Record-stop | Current+dirty → snapshot; otherwise retain composed handoff seed |
| Slide + capture | Remove force Rebuild; slide committed prefix then re-apply overlays |
| RC-W fields | `commit` / `seal_pend` / `begin` on existing `DIAG,wrap` |

### Open before coding

None.

## Tests

- `test_display_window_utils`: playhead-only Hit; closed-session predicate table; committed vs overlay key split; `shouldRetainRecordStopHandoffSeed`
- `test_loop_take_survival`: overlay-only reuse walk counts + full-gather oracle
