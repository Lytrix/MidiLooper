# Display seal handoff — capture retention

**Status:** Firmware in tree, host gates passed. **Parked** — [`204043`](../../captures/session_20260901_204043.log) did not exercise pitch-64 holds. Reopen only on the recapture gate below.
**Date:** 2026-09-01
**Kind:** bugfix
**Parent:** `.cursor/plans/snapshot-first_bar_painter_7a96d434.plan.md` (Stage B/C landed)
**Evidence:** [`150650`](../../captures/session_20260901_150650.log)

**Debugging boundary:**

```text
wrap snapshot publication stamping (OpenSpec 3.5/3.6)
    ← closed 2026-09-01, root_rev tracks playback_rev every wrap in 150650
keepShortestEndAtPitchStart arbitration
    ← not the owner: no slice_keep in 150650, cache predates the notes
capture → committed tier handoff at seal
    ← this slice
```

**Does not authorize:** changes to `keepShortestEndAtPitchStart`, synchronous snapshot
publication at seal, a wrap-time visual-cache splice, or a new `DrawSourcePath` value.

---

## Problem

In [`150650`](../../captures/session_20260901_150650.log) the STOPPED frame dump has 34 rows
against 34 effective rows, 32 identical. The two held pitch-64 rows differ:

| Source | s0 | s768 |
|---|---|---|
| effective | len 624 | len 624 |
| frame | len 40 | len 48 |

The MIDI input settles ground truth: NoteOn storage 0 at `52367592` with NoteOff storage 624 at
`53987500`, and NoteOn storage 768 at `54367054` with NoteOff storage 1392 at `55988203`. Both
holds are 624 ticks, so effective is right and the frame paints the previous pass.

Neither committed tier carried the sealed pass when the frame was painted:

| Event | Timestamp |
|---|---|
| last hold NoteOff | `55988203` |
| pass sealed, `playback_rev` 17 | `56135261` |
| **frame dump** | **`56215949`** |
| `root_rev` 17 published | `56288500` |
| `VCACHE,slice_clean` covering the pass | `56311186` |

The visual cache's previous rebuild was `VCACHE,slice_clean` at `52023305`, four seconds before
those notes existed. A cache built before the content cannot contain it, so arbitration is not
the owner — and there is no `VCACHE,slice_keep` anywhere in the capture, so the smaller-slice
guard never fired either. `DIAG,draw_src,diff` is empty: the old compose path paints the same
stale rows, because it also prefers a clean `visualCache`.

The seam is the seal itself. `Loop::commitPendingCapturePass` clears `capturePreview`, so the
rows leave the Capture tier at the instant the pass is sealed, while the Snapshot tier still
needs its 150 to 220 ms to publish and the VisualCache tier needs its next idle slice. For that
window the painter can only show the previous pass. This is the reported "flashes one frame of
old state", and at a stop the final frame can land inside the window and stay there.

## Invariant

```
capture-derived rows stay in the painted frame until the committed representation the
    painter would select proves it covers the revision they were sealed into
same pitch+start: the retained sealed row supersedes the lagging committed-tier row
retained rows join the committed prefix, so pending note changes apply to them
release is driven by selected-tier coverage, never by a frame count or timer
retention is copied once per seal and never rebuilt per frame
```

## Ownership

```text
capturePreview
    ↓ seal
sealedCapturePreview
    ↓ selected committed representation proves coverage
released

Loop owns both.
DisplayManager only consumes them.
VisualCache reports coverage; it does not own retention.
```

`DisplayManager` must not keep a private backup of either vector. A display-side copy that
outlives the frame is the fourth tier this slice exists to avoid.

## Release predicate

The question is not whether *some* tier reached revision `R`. It is whether the committed
representation `resolveDisplayNotesByDrawSource` **will select** covers `R`. Those differ: a
current Snapshot at `R` is useless if the painter reads a VisualCache still at `R-1`.

The selection and the coverage proof are the same predicate, which makes this exact:

| `publishedSnapshotIsCurrent()` | Representation the painter selects | Coverage proof |
|---|---|---|
| true or false | Snapshot prefix while capture/session is active; VisualCache after stop | `visualCache.builtFromRevision >= R` and `!visualCacheDirty` only. Snap-current is not membership (`012714` wrap 2: `snap=1` dropped `sealed=47`, frame 148→129). |

```text
release iff !visualCacheDirty and visualCache.builtFromRevision >= R
```

Amended by [`overdub_paint_sealed_survives_current_snapshot_bugfix.md`](overdub_paint_sealed_survives_current_snapshot_bugfix.md).

## Supersede contract

The helper is deliberately narrow. It is **not** a generic "newer vector wins" primitive, which
could override a more authoritative companion-transformed row:

```text
retained sealed capture row
    replaces stale committed row
    iff same pitch + same start
otherwise append
```

It applies only to the retained sealed rows against the committed prefix, and only for the
duration of the seam. Wrap-held rows (`endTick < startTick`) are left alone.

`keepShortestEndAtPitchStart` must **not** appear in this path. `150650` establishes that
arbitration was not involved.

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — `Loop` already owns `capturePreview` and keeps capture-derived rows longer. Nothing moves to `DisplayManager`. |
| State transition change? | NO — no capture or track transition is added, removed or reordered. Only the lifetime of a display buffer across the existing seal changes. |
| Unbounded interval owner? | NO — retention copies an existing bounded vector once per seal. The paint union is bounded by the retained row count, with no gather and no reconstruct. |
| Reuse | YES — extend `Loop::commitPendingCapturePass`, `DisplayManager::resolveDisplayNotesByDrawSource`, `VisualCache`, `DisplayWindowUtils` |

Retention is a **lifetime refinement of the existing Capture tier**, not a fourth tier.
`DrawSourcePath` is unchanged.

## Implementation

| Owner | Change |
|---|---|
| `Loop::commitPendingCapturePass` | Before `capturePreview.clear()`, retain the preview rows and stamp the revision they were sealed into |
| `Loop` | `sealedCapturePreviewNotes_` + `sealedCapturePreviewRevision_`, beside `capturePreview` |
| `Loop::releaseSealedCapturePreviewIfCovered` | Drop the retained rows once the representation the painter would select covers the sealed revision, per the predicate above |
| `VisualCache` | `builtFromRevision`, stamped on full rebuild and when the last dirty bar clears, so the cache tier can report what it carries |
| `DisplayManager::resolveDisplayNotesByDrawSource` | Union retained rows after snapshot/visual. [`003558`](../../captures/session_20260902_003558.log) keeps them **after** `committedDisplayEnd` so occupy Hide cannot drop wrap-1 paint — [`overdub_paint_sealed_prefix_hide_bugfix.md`](overdub_paint_sealed_prefix_hide_bugfix.md). Suffix upsert still supersedes at pitch+start. |
| `DisplayWindowUtils::upsertDisplayNoteAtPitchStart` | Supersede helper, header-only so it is natively testable |

## Residual not covered by this slice

Occupancy that **shortens** a note at a *different* start is still shown with pre-seal geometry
during the seam, because supersede keys on pitch+start. The reported symptom is same-start
hide/replace, which this slice covers. Track separately if a capture shows it.

## Tests

- `test_display_window_utils`: `upsertDisplayNoteAtPitchStart` replaces at equal pitch+start,
  appends otherwise, leaves wrap-held rows alone, and does not reorder unrelated rows
- **Ordering case, reproducing the `150650` timestamps** — the critical negative test:

```text
seal
  ↓
frame                         ← MUST show the new pass from retained rows
  ↓
snapshot publication
  ↓
cache clean
  ↓
release                       ← MUST now leave exactly the committed representation
```

- Release predicate: a current Snapshot at `R` releases while the cache is still at `R-1`; a
  stale Snapshot does **not** release until `visualCache.builtFromRevision >= R`
- Retention is copied once per seal: repeated frames during the seam must not re-copy or
  re-derive the retained vector
- `pio run -e teensy41-capture-serial` RAM1 locals >= 4000, then `pio test -e native`
- HITL: overdub the same pitch across passes, confirm no stale frame at wrap or at stop

## Parked — reopen if recaptured

Not Now implementing after [`204043`](../../captures/session_20260901_204043.log) (no pitch-64 holds).

| Gate | Reopen when a future capture shows |
|------|-------------------------------------|
| Held-note retention | After seal, a held pitch (pitch 64 in [`150650`](../../captures/session_20260901_150650.log)) paints 40/48 on the frame while MIDI / effective is ~624 (`s0 len40` / `s768 len48` vs Off 624 / 1392) |

Do not treat a capture without those holds as closeout or as a reason to patch `keepShortestEndAtPitchStart`.
