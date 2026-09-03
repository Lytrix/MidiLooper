# Display draw-source reuse and wrap-seal overruns

**Status:** RC-D1/D2/W host shipped (`a92595d9`). RC-D3 device **PASS**. RC-S device **PASS** on [`122730`](../../captures/session_20260903_122730.log) (`030e1a08`). D2 `resolveUs` 8 ms still open.  
**Date:** 2026-09-03  
**Kind:** bugfix  
**Evidence:** [`151552`](../../captures/session_20260902_151552.log), [`095648`](../../captures/session_20260903_095648.log), [`101721`](../../captures/session_20260903_101721.log), [`115237`](../../captures/session_20260903_115237.log), [`120321`](../../captures/session_20260903_120321.log), [`122730`](../../captures/session_20260903_122730.log)  
**Does not replace:** DEC-052 occupy predicate consumption, [#41](https://github.com/Lytrix/MidiLooper/issues/41) Stage 1 visual-cache slices, [`overdub_paint_sealed_survives_current_snapshot_bugfix.md`](overdub_paint_sealed_survives_current_snapshot_bugfix.md) (`012714`)  
**Does not authorize:** STOPPED `usbdisp` / `PERS`; wrap-seal redesign or off-clock `commitCapturePass`; `#41` Stage 2 snapshot fill of `visualCache`; `refreshVisualCacheAfterPassStateChange` on record stop; RC-D4 / `liveDisplayNotes` authority

---

## Debugging boundary

```text
Track::commitOverdubWrap / DIAG,wrap.seal     ← RC-W (measure only; shipped)
selectDrawSourceReuseKind + gather + handoff  ← RC-D1 / RC-D2 (host shipped)
ReuseCommittedDrawSource overlays             ← RC-D3 (device PASS 115237 / 120321)
retainCapturePreviewAtSeal retain+merge       ← RC-S (device PASS 122730)
DFRAME resolveUs 8 ms                         ← RC-D2 device gate; still open (122730)
live hidden vs stop DIAG,oh hide              ← reassessed; different counters; not a reuse owner
visualCache idle / #41 Stage 2                ← do not start
moving wrap seal off the clock                ← still a design session
RC-D4 / liveDisplayNotes authority            ← do not introduce
```

Reuse is an optimization over a **derived gather result**. It does not make `liveDisplayNotes` a new committed source owner.

`DrawSourceReuseKind::ReuseCommittedDrawSource` means the **committed gathered source** remains valid for this frame. It does **not** mean the complete resolved display result remains valid. The shipped no-op (reuse the resolved frame) is too strong; RC-D3 is that correction.

**Prefix immutability (D3 shipped):** overlay and pending-transform application operates on a frame-local copy. It never mutates the reusable committed prefix.

**RC-S:** `retainCapturePreviewAtSeal` retains all currently uncovered sealed rows, then merges this pass's closed preview. It must not discard previously retained sealed rows merely because a new pass is being sealed. Release remains exclusively `releaseSealedCapturePreviewIfCovered`. Do not move wrap seal off the clock, fill `visualCache` on the clock, or add a reuse owner.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — display stays `DisplayManager::resolveDisplayNotesByDrawSource`; wrap sealer stays `Track::commitOverdubWrap` |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** for display (LoadAndDisplay remainder). Wrap seal already lives on the clock |

Reuse: extend `selectDrawSourceReuseKind`, `resolveDisplayNotesByDrawSource`, `shouldGatherPublishedSnapshotForDrawSource`, `refreshViewportAfterRecordStop`. No new Manager. Committed-source decision values: `ReuseCommittedDrawSource` / `SlideCommittedDrawSource` / `RebuildCommittedDrawSource`. Overlay work stays `applyDrawSourceOverlays` / `applyPendingDrawSourceTransforms` / `applyCapturePlayheadTails`. RC-S: extend `Loop::retainCapturePreviewAtSeal` only.

## Layer contract (pin)

`gatherDrawSourceBars` already splits the frame:

1. **Committed source** — snapshot **or** `appendNotesIntersectingBar(visualCache)` for `[firstBar, lastBar]`. Then `committedEnd = out.size()`.
2. **Sealed overlay** — `shouldUnionSealedCapturePreviewForDrawSource` then upsert from `committedEnd` only. Sealed does **not** rewrite `[0, committedEnd)`.
3. **Preview overlay** — capture-active preview rows (open holds unique-by-id; closed rows upsert into the suffix).
4. **Pending transforms** — `applyPendingDrawSourceTransforms` after gather.
5. **Playhead tails** — `applyCapturePlayheadTails` after gather, paint-only.

**Sealed is a transient overlay, not part of the reusable committed prefix.** Overlay-only invalidation includes sealed-key changes. The reusable prefix is the last **untransformed** committed-source walk, not the post-Hide contents of `liveDisplayNotes`.

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

### Playhead-only `ReuseCommittedDrawSource`

Today, `windowStart` unchanged + `playheadBar` changed → **Rebuild**. `gatherDrawSourceBars` does `(void)playheadBar`. On a 16-bar full-loop window that is a full gather every bar crossing.

**Invariant:** `ReuseCommittedDrawSource` reuses the committed gathered source. It does not freeze the resolved frame. Tails always apply. Overlays/pending apply on a frame-local copy of that prefix.

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

Native: `test_display_window_utils` playhead-only `ReuseCommittedDrawSource` + the closed-session table above. Device occupy-paint [`095648`](../../captures/session_20260903_095648.log) / [`094740`](../../captures/session_20260903_094740.log): last RECORDING `frameNotes=4` → first PLAYING `frameNotes=4`. [`151552`](../../captures/session_20260902_151552.log) was `251 → 0`. Not a 16-bar / 251-note recapture.

---

## RC-D2 — OVERDUBBING resolve 32 ms (overlay-only reuse)

**Owner:** `resolveDisplayNotesByDrawSource` / `gatherDrawSourceBars`.

[`151552`](../../captures/session_20260902_151552.log) L12845–L12890: `snap=1 visual=251 preview=135 sealed=177 frame=646`, `DFRAME resolveUs=32204`.

### A. Committed-source invalidation → existing full gather

Snapshot identity / revision / current flag; visual-cache revision when visual is the selected source; window start / length; pitch range, slot, track; `playbackRevision` / `captureDisplayRevision`.

### B. Overlay-only invalidation → reuse committed prefix

`capturePreview.revision`, `pendingChangeGeneration`, sealed overlay keys (`sealedPreviewRevision` / `sealedPreviewCount` / `sealedCompanionOverlay`).

**Invariant:** a preview/pending/sealed-only invalidation may reuse the previously gathered committed bars. It must not cause any walk of snapshot/visual committed notes. Only the changed transient layers are re-evaluated against `[0, committedEnd)`.

Stop forcing `captureActive && SlideCommittedDrawSource` → `RebuildCommittedDrawSource` once overlay-only reuse exists. `appendDisplayNoteUniqueById` stays as-is. If overlay union against a large prefix is still ≥ 8 ms, that is a **new measured RC**, not an index in this slice.

### Fixture contract

- committed source walk = 0
- sealed/preview walk = yes when those layers changed
- pending transform = yes when pending changed
- final union matches the full-gather oracle

Device acceptance: OVERDUBBING `DFRAME` `resolveUs` **under 8 ms** on a dense 16-bar pass when the window covers the loop and only preview/playhead/pending moved. [`101721`](../../captures/session_20260903_101721.log) still **15–33 ms** — gate open.

---

## Device recapture [`115237`](../../captures/session_20260903_115237.log)

One overdub session only: `PLAYING → OVERDUBBING` (L2636) → `OVERDUBBING → STOPPED` (L11047). Three wraps (L4358 / L7314 / L10576). No second `PLAYING → OVERDUBBING`. `DRAW_SRC` is on (`DIAG,draw_src,layers`). `hidden` climbs `0 → 1` at L3811, `2` at L4080, before wrap 1 (L4358). `visual` stays **127** the whole OVERDUBBING.

`DISP` `frameNotes` **grows during each lap**, before wrap:

| Lap | `frameNotes` |
|-----|----------------|
| Before wrap 1 | **127 → 199** |
| Wrap 1 → wrap 2 | **200 → 323** |
| Wrap 2 → wrap 3 | starts **246**, then **246 → 323** |
| Wrap 3 → stop | **265–268** |

Wrap **drops** the live preview and replaces it with the sealed count (323 → 246 at wrap 2; 312 → 265 at wrap 3). That is seal handoff, not “notes only appear at wrap.”

Split the user-visible “only updates at wrap” report:

| What | Serial | Action |
|------|--------|--------|
| Notes played this pass | `frameNotes` climbs every few hundred ms ([`115237`](../../captures/session_20260903_115237.log), [`101721`](../../captures/session_20260903_101721.log)) | No new source-selection work |
| Occupy Hide/Shorten of existing source notes | [`101721`](../../captures/session_20260903_101721.log) `hidden=0` until wrap/stop; [`115237`](../../captures/session_20260903_115237.log) / [`120321`](../../captures/session_20260903_120321.log) `hidden` climbs before wrap 1 | **RC-D3 PASS** |
| `visualCache` count | stays 126/127 until post-stop idle adopt | `#41` Stage 2 — do not start |
| Wrap 2+ preview vs sealed | [`120321`](../../captures/session_20260903_120321.log) wrap 2: preview **91 → 1**, sealed **78 → 85**, frame **323 → 242**. [`122730`](../../captures/session_20260903_122730.log) wrap 2: sealed **63 → 159**, frame **316 → 311** | **RC-S PASS** |

## Device recapture [`120321`](../../captures/session_20260903_120321.log)

One OVERDUBBING session (L2669 → L10134). Two wraps (L4543 / L7564). Loop **8 bars** (`BAR,6144,8`). `DRAW_SRC` on.

| Marker | Value |
|--------|--------|
| OD open layers | `visual=126,preview=0,sealed=0,snap=1,frame=126,hidden=0` (L2732) |
| Last before wrap 1 | `preview=81,sealed=0,snap=1,frame=201,hidden=5` (L4518) |
| First `hidden>0` | L3516 — before wrap 1 (L4543). D3 gate **PASS** |
| First after wrap 1 | `preview=4,sealed=78,snap=0,frame=202,hidden=5` (L4575) |
| First `snap=1` after wrap 1 | `preview=26,sealed=78,frame=298` (L5294) |
| Last before wrap 2 | `preview=91,sealed=78,snap=1,frame=323,hidden=5` (L7497) |
| First after wrap 2 | `preview=1,sealed=85,snap=0,frame=242,hidden=5` (L7592) |
| After wrap 2 `snap=1` | one row only, L9896, `frame=326` — then stop |
| Stop | `DIAG,oh` hide=38 shorten=3 add=66 (L10132); `VCACHE,adopt_partial` notes=269 (L10136); first STOPPED `visual=269,frame=358` then `292` |

`drawPianoRoll` paints the composed `notes` argument when the window covers the loop. Serial `frame` climbs during every lap. The rows that *stay* after wrap 2 are record `visual` 126 plus the new sealed 85. Stop adopt is when `visual` leaves 126. RC-S below.

## Device recapture [`122730`](../../captures/session_20260903_122730.log)

One OVERDUBBING session (L2751 → L11526). Three wraps (L4490 / L7712 / L11341). Loop **8 bars** (`DISP` 6144). `DRAW_SRC` on. Firmware `030e1a08`.

| Marker | Value |
|--------|--------|
| OD open layers | `visual=127,preview=0,sealed=0,snap=1,frame=127,hidden=0` (L2818) |
| First `hidden>0` | L3032 `hidden=1` — before wrap 1 (L4490) |
| Last before wrap 1 | `preview=66,sealed=0,snap=1,frame=189,hidden=4` (L4465) |
| First after wrap 1 | `preview=4,sealed=63,snap=0,frame=190,hidden=4` (L4520) |
| Last before wrap 2 | `preview=106,sealed=63,snap=1,frame=316,hidden=4` (L7692) |
| First after wrap 2 | `preview=2,sealed=159,snap=0,frame=311,hidden=4` (L7765) |
| Last before wrap 3 | `preview=97,sealed=159,snap=1,frame=375,hidden=4` (L11321) |
| First after wrap 3 | `preview=2,sealed=217,snap=0,frame=389,hidden=4` (L11378) |
| Stop | `DIAG,oh` hide=1 shorten=0 add=2 (L11524); `VCACHE,adopt_partial` notes=341 (L11528); first STOPPED `DISP` visual=341 frame=559 then 341 |

Wrap 2 sealed **63 → 159** (159 > lap-2 preview 106), so sealed is not “this lap only.” Frame **316 → 311**, not `visual + new-sealed` (127 + 96 + 2 = 225). Wrap 3 sealed **159 → 217**, frame **375 → 389**. `visual` stays 127 for the whole OVERDUBBING session. RC-S device **PASS**.

---

## RC-D3 — occupy must paint on reuse, not only at wrap

**Owner:** `resolveDisplayNotesByDrawSource` `ReuseCommittedDrawSource` branch.

[`101721`](../../captures/session_20260903_101721.log): `preview`/`frame` grow during OVERDUBBING; `visual` stays 127; `hidden=0` for the whole pass including both wraps; stop `DIAG,oh` hide=53 shorten=4; first PLAYING 351 / adopt 274. [`151552`](../../captures/session_20260902_151552.log) `hidden` climbed `1 → 8` during the first lap before wrap.

Shipped `a92595d9` reuse + overlay-key match left `liveDisplayNotes` unchanged and only ran `applyCapturePlayheadTails`. That treated reuse as “don't rebuild anything,” so pending Hide/Shorten waited until wrap/stop rebuilt the committed source.

### Proposed solution (host in tree)

Do **not** wait for wrap seal to make occupy visible. Do **not** fill `visualCache` on the clock. Complete the existing layer contract:

```text
ReuseCommittedDrawSource
    → copy liveDisplayCommittedPrefix_   (untransformed [0, committedEnd))
    → applyDrawSourceOverlays            (sealed + preview)
    → applyPendingDrawSourceTransforms   (Hide / Shorten)
    → applyCapturePlayheadTails
    → draw
    → prefix unchanged
```

`liveDisplayCommittedPrefix_` is the last untransformed committed walk. Overlay/pending apply only on a frame-local candidate that is swapped into `liveDisplayNotes`. Hide never mutates the reusable prefix.

No new `DrawSourceReuseKind`. No new cache owner. Wrap seal stays on the clock.

Host fixtures:

- `test_draw_source_reuse_reapplies_pending_without_mutating_prefix` — walk 0; Hide on the frame copy; stored prefix un-hidden; second reuse idempotent (`test_loop_take_survival` stubs apply, so Hide is the suite’s prefix drop).
- `test_pending_hide_does_not_mutate_copied_committed_prefix` — real `applyPendingNoteChangesToDisplayNotes` on a copy.

Device gate **PASS**: [`115237`](../../captures/session_20260903_115237.log) `hidden` 0→2 before wrap 1; [`120321`](../../captures/session_20260903_120321.log) `hidden` 0→5 before wrap 1. Live `hidden` max is not the stop `DIAG,oh` hide count (120321 hide=38). That remainder and the wrap-2 preview/sealed drop are not a second reuse owner. **Pause D3** — no RC-D4, no `#41` Stage 2.

---

## RC-S — wrap-2 sealed replace drops uncovered laps

**Owner:** `Loop::retainCapturePreviewAtSeal` (same call site in `commitCapturePass`). Display still consumes sealed via `applyDrawSourceOverlays` / `shouldUnionSealedCapturePreviewForDrawSource`.

Host `030e1a08`. Device **PASS** [`122730`](../../captures/session_20260903_122730.log).

### Root cause

Two facts, both in code.

**1. Sealed is the only live home for the just-sealed lap until visual covers it.**

`Loop::sealedCapturePreviewIsCovered` is visual-revision only (`012714`: a current snapshot is not membership). [`120321`](../../captures/session_20260903_120321.log) `visual` stays 126, so sealed must keep every uncovered lap. That is the 150650 / `012714` contract: retain closed preview at seal; release only on coverage.

**2. The next wrap throws that overlay away.**

Before this slice, `Loop::retainCapturePreviewAtSeal` did `sealedCapturePreviewNotes_.clear()` then copied **this pass’s closed** `capturePreview` rows (open holds skipped). Called from `commitCapturePass` immediately before `capturePreview.clear()`.

```text
wrap 1: preview 81 closed → sealed 78     (record snapshot/visual 126 still the prefix)
wrap 2: clear sealed 78; copy lap-2 closed → sealed 85
        lap-1 notes are not in visual (126) and not in last snapshot membership
        frame = 126 + 85 + open preview 1 → 242
```

The comment at the call site says keep the rows until a committed tier proves coverage. `clear()` on wrap 2+ violates that. `012714` stopped *releasing* sealed when `snap=1`; it did not stop *replacing* sealed on the next wrap.

This is not a wrap-seal clock move and not `#41` Stage 2. Wrap-1 snapshot publication (`snap=1` at L5294) still left `sealed=78` in the frame, which is the `012714` “current snapshot can omit the just-sealed lap” case. Replacing sealed is the hole in that contract.

```text
lap-1 preview → wrap 1 retain → sealed 78
snap-current still omits lap 1 ─┐
visual 126 ─────────────────────┼→ frame 323 → 242
sealed 78 → wrap 2 clear+copy ──┤
lap-2 preview 91 ───────────────┘→ sealed 85 (lap 2 only)
                                   → stop adopt visual 269
```

### Solution

**Do not** redesign `Track::commitOverdubWrap`, defer `commitCapturePass`, fill `visualCache` on the clock, or add a new reuse kind.

**Change:** retain all currently uncovered sealed rows, then merge this pass’s closed preview. Do not `clear()` first.

- Skip open holds (unchanged).
- Identity is the existing overlay rule `DisplayWindowUtils::upsertDisplayNoteAtPitchStart` (same as gather’s `upsertDisplayNoteAtPitchStartFrom` with suffix begin 0): replace that row’s geometry only when pitch+start match and the incoming row is linear; otherwise append. Wrap-held (`end < start`) always appends. This is not a new “latest lap discards” rule — a newer lap does not drop a distinct sealed row merely by existing.
- If preview is empty, do not clear existing sealed; still set `sealedCapturePreviewRevision_ = playbackRevision`.
- Set `sealedCapturePreviewRevision_ = playbackRevision` so coverage still waits for visual to consume the **latest** seal.
- `releaseSealedCapturePreviewIfCovered` still drops the whole overlay when visual covers.

**Invariant:**

```text
retainCapturePreviewAtSeal never removes an existing sealed row merely
because a new pass is being sealed.

Existing sealed rows may be removed only by
releaseSealedCapturePreviewIfCovered.
```

`retainCapturePreviewAtSeal` may merge new closed preview rows into existing sealed rows, but it must not discard previously retained sealed rows merely because a new pass is being sealed; release remains exclusively governed by `releaseSealedCapturePreviewIfCovered`.

Consequence of that invariant: after wrap 2, sealed still contains uncovered lap-1 rows plus lap-2 closed rows (pitch+start identity updates only).

### Architecture checkpoint (RC-S)

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** |
| State transition change? | **NO** — same seal call, same coverage release |
| Unbounded interval owner? | **NO** — same wrap-clock walk; merge adds the existing sealed length, not a new MIDI-interval owner |

Reuse: **YES** — extend `retainCapturePreviewAtSeal`.

### Sequence (accepted)

1. **RC-S** — retain+merge. Host `030e1a08`. Device **PASS** [`122730`](../../captures/session_20260903_122730.log).
2. **Reassess live `hidden` vs stop `DIAG,oh` hide** — **not a reuse owner.** `hidden` counts `visualCache` rows with no pitch+start in the drawn frame (`DisplayNoteResolve.cpp`). `DIAG,oh hide` is the occupy overlap-hold Hide tally (`overlapHold`). [`122730`](../../captures/session_20260903_122730.log) live `hidden` max=4 (L3732, before wrap 1) vs stop hide=1. [`120321`](../../captures/session_20260903_120321.log) was 5 vs 38. Different counters; do not start RC-D4.
3. **RC-D2 device 8 ms** — still open. [`122730`](../../captures/session_20260903_122730.log) OVERDUBBING `DFRAME resolveUs` min 2149 / median 8724 / max 15285 (16 of 31 frames ≥ 8000). Do not start an index.
4. **`#41` Stage 2 / off-clock wrap seal** — still out.

Not a substitute for RC-S: making wrap-journal snapshots include the just-sealed lap. `012714` already showed `snap=1` can omit that lap; 120321 still had `sealed=78` while `snap=1`.

### Host / device gates

- Native: `test_retain_at_seal_merges_uncovered_laps` — two wrap seals, visual left behind `sealedCapturePreviewRevision_`; after wrap 2 sealed is not wrap-2-only; same pitch+start updates geometry only. Existing `test_seal_retains_capture_rows_until_snapshot_covers_revision` stays.
- Device **PASS** [`122730`](../../captures/session_20260903_122730.log): wrap 2 `sealed` 63→159, `frame` 316→311. Pause — no `#41` Stage 2, no RC-D4.

---

## Explicitly not this slice

- STOPPED `usbdisp` / `PERS`
- Redesign / defer / off-clock wrap seal (`Track::commitOverdubWrap` stays; RC-S is retain merge only)
- `#41` Stage 2 snapshot fill of `visualCache`
- `refreshVisualCacheAfterPassStateChange` on record stop
- Record-start `idle_maint` 99 ms, `begin_capture` 10 ms
- New uniqueness index
- Promoting `liveDisplayNotes` or the RECORDING compose prefix to committed-source authority
- RC-D4 / new reuse cache owner
- Making wrap-journal snapshots include the just-sealed lap as a substitute for RC-S
- Mutating the reusable committed prefix in place when applying Hide/Shorten

## Pre-implementation review

### Ready

- `selectDrawSourceReuseKind` / `shouldGatherPublishedSnapshotForDrawSource` in `DisplayWindowUtils.h`
- `resolveDisplayNotesByDrawSource` / `gatherDrawSourceBars` in `DisplayNoteWindowGather.cpp`
- `refreshViewportAfterRecordStop` in `DisplayCacheLifecycle.cpp`
- Host twin `gatherDrawSourceDisplayNotes` in `test_loop_take_survival`
- `Track::commitOverdubWrap` / `overdubWrapTick`
- RC-S: `Loop::retainCapturePreviewAtSeal` / `sealedCapturePreviewIsCovered` in `LoopCapture.cpp`
- RC-S: `DisplayWindowUtils::upsertDisplayNoteAtPitchStart` (gather uses the suffix variant)
- RC-S: `test_seal_retains_capture_rows_until_snapshot_covers_revision` / `test_retain_at_seal_merges_uncovered_laps`

### Resolved

| Topic | Decision |
|-------|----------|
| Playhead-only | `ReuseCommittedDrawSource` when committed keys match and `windowStart` is unchanged |
| Overlay-only | Same reuse kind; resolve re-applies overlays; no new `DrawSourceReuseKind` |
| Record-stop | Current+dirty → snapshot; otherwise retain composed handoff seed |
| Slide + capture | Remove force Rebuild; slide committed prefix then re-apply overlays |
| RC-W fields | `commit` / `seal_pend` / `begin` on existing `DIAG,wrap` |
| Reuse meaning | Committed prefix valid; not resolved-frame valid |
| D3 apply | Frame-local copy; stored prefix never mutated |
| Prefix store | `liveDisplayCommittedPrefix_` on DisplayManager |
| [`115237`](../../captures/session_20260903_115237.log) | Preview `frameNotes` update before wrap; occupy-until-wrap was D3, not a second session |
| [`120321`](../../captures/session_20260903_120321.log) wrap 2 | `clear()` in `retainCapturePreviewAtSeal` drops uncovered lap-1 sealed |
| RC-S owner | Extend `retainCapturePreviewAtSeal`; do not move wrap seal off the clock |
| RC-S apply | Retain uncovered sealed, then merge closed preview; existing pitch+start upsert only |
| Snapshot completeness | Not a substitute — `012714` / 120321 `snap=1` still needed `sealed=78` |
| [`122730`](../../captures/session_20260903_122730.log) | RC-S device PASS; wrap 2 sealed 63→159; hidden vs oh hide are different counters |
| D2 8 ms after RC-S | [`122730`](../../captures/session_20260903_122730.log) resolveUs median 8724, max 15285 — still open |

### Open after RC-S device

1. RC-D2 `resolveUs` 8 ms — measured, not started.
2. `#41` Stage 2 / off-clock wrap seal stay out.

## Tests

- `test_display_window_utils`: playhead-only `ReuseCommittedDrawSource`; closed-session predicate table; committed vs overlay key split; `shouldRetainRecordStopHandoffSeed`
- `test_loop_take_survival`: overlay-only reuse walk counts + prefix-immutability reuse pin
- `test_pending_note_change`: Hide on a frame-local copy does not mutate the stored prefix
- RC-S: `test_retain_at_seal_merges_uncovered_laps`; keep `test_seal_retains_capture_rows_until_snapshot_covers_revision`
- Full native **1634/1636** — pre-existing `test_same_pass_playhead_tails_keep_closed_bar_four_end` only
