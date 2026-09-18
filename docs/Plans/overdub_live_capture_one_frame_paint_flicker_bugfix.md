# Overdub live-capture one-frame paint flicker

**Status:** CLOSED 2026-09-18 — **folded into the memory-pressure work**, not fixed in place. No fix
landed and none is planned under this identity.
**Closed by:** user decision 2026-09-18 ("close this bug, we have a bigger plan to solve the memory
heap issue"). Successor owner is
[`runtime-working-set-ownership`](../../openspec/changes/runtime-working-set-ownership/tasks.md)
(`CURRENT_WORK.md` § Now implementing), the working-set/heap-pressure change that owns PSRAM lease
sizing, snapshot-publication admission and the resident-store scaling law.
**Branch:** base is `feature/resumable-midi-boundary-playback-maintenance` (`4aacd18e`); this was a
base-branch bug, **not** a `runtime-code-tidyup` regression. The closure is a docs-only commit on
`bugfix/overdub-live-capture-paint-flicker`.

## Why this closes without a fix

The symptom was narrowed to a real, measured one-frame compose dip, and the same capture shows the
paint path's own source state — the published snapshot / visual cache — being held back by the memory
gate for the whole run. Those two facts make this a *symptom of the heap state*, and the successor
change is the owner that can actually make the source state advance. Re-measuring a paint-path RC
against a publication path that is being denied admission every turn would be measuring the denial,
not the paint.

Evidence for that reading, from the same reference capture the OPEN pass used
([`captures/tidyup-base-track6-slot1-r2/session_20260918_221953.log`](../../captures/tidyup-base-track6-slot1-r2/session_20260918_221953.log)):

1. **The dip is real, and the existing on-change `DISP` telemetry already catches it** — the OPEN pass
   assumed it did not. `DISP` fires on *any* change of `frameNotes`, so a one-frame dip appears as a
   **decrease** row. Scanning the whole capture, exactly two decreases exist: `23.001 s`
   (`RECORDING`, 32 → 0 — the ordinary clear) and **`49.972 s`: `OVERDUBBING`, `frameNotes` 75 → 74,
   restored to 75 at `50.019 s`**. That is the blink: one painted frame with one row fewer, inside the
   overdub window, immediately after the `49.946 s` MIDI event (`MI U` + `COORD abs 109`).
   Because `DISP` is emitted at `Transfer` with `resolvedPaintNotes()` — the same vector `Draw`
   consumed — the row was lost in the **compose**, not in the draw.
2. **The publication gate refuses admission for the whole window.** The same capture carries **52**
   `#MEM_DENIED,op=snapshot_reserve,reason=heap_low,need=40960B,heap_free=68KB,pool_refused=0/0B`,
   continuously reported (1 s report window) from **27.64 s to 79.06 s**, i.e. through the entire
   overdub. `kSnapshotPublicationReserveBytes` (40,960) on top of the 32 KB `kOperationHeapFloorBytes`
   needs 73,728 B against 68 KB free. DEC-075 deliberately makes a denial non-fatal (the previous root
   stays selected), so the committed source the paint composes from cannot advance while the heap sits
   where the arm left it.

Consequently the invariant this work owed is recorded here and handed over, rather than asserted by a
landed fix:

> **Invariant (handed to `runtime-working-set-ownership`):** a note present in the composed paint set
> before and after a note-on/note-off must be painted in every frame in between. A one-frame compose
> dip is a defect whatever its cause.

The concrete next step for the successor, once the lease/publication pressure is addressed: re-run the
same fixture and scan for `DISP` **decreases** inside the overdub window (a two-line awk away — no new
firmware telemetry is required for detection). If the dip survives a healthy publication gate, it is a
genuine paint-path RC and needs its own plan; the draw-source compose in
`DisplayManager::resolveDisplayNotesByDrawSource` plus
`Loop::applyPendingNoteChangesToDisplayNotes` is where to look, with the
`captureDisplayRevision`-keyed committed rebuild and the pending Hidden/Shorten application as the two
candidate frames.

## Historical record (OPEN pass, 2026-09-18)

## Symptom

During OVERDUB on the selected slot, the piano roll flashes for one frame around each incoming note
(observed on both note-on and note-off). The blink is one display frame, not a content change: the note
set loses a row for a single painted frame and recovers on the next.

## Evidence

| Anchor | What it shows |
|---|---|
| User observation, tidy-up firmware (`d3fec28c`, hex `21b4d320…`) | Blink present on track 6 slot 1 while overdubbing. |
| User observation, **base** firmware built from `4aacd18e` (hex `7bbc5e63…`) | **Blink still present.** The regression is therefore pre-existing on the feature branch, not introduced by P1–P6. |
| `captures/tidyup-base-track6-slot1-r2/session_20260918_221953.log` | Overdub window 31.55 s → 45.35 s, 52 note events, `overall_ok: true`, `overflow_free=True`. Exactly **one** `VCACHE,stale_all` in the whole window, so the blink is not a per-note full visual-cache invalidation. |
| Same log, `DFRAME` family | `SC_DFRAME` fires every 30th frame (`DisplayManager.cpp` `dframeCounter % 30U`), so *that* family cannot catch a one-frame dip. **Superseded 2026-09-18:** the on-change `DISP` family can and does — see the closure section (`49.972 s`, 75 → 74 → 75). |
| Same log, `DISP` decrease scan (added at closure) | Exactly two decreases in the whole run: `23.001 s` clear (32 → 0) and **`49.972 s` `OVERDUBBING` 75 → 74 → 75 at `50.019 s`** — one painted frame, one row fewer, right after a MIDI event. `DISP` is emitted at `Transfer` from `resolvedPaintNotes()`, the same vector `Draw` used, so the lose-and-recover is in the **compose**. |
| Same log, `#MEM_DENIED` | **52** `op=snapshot_reserve,reason=heap_low,need=40960B,heap_free=68KB,pool_refused=0/0B` rows, continuously reported (1 s window) from `27.64 s` to `79.06 s`: the DEC-075 publication gate refuses every turn across the entire overdub, so the published root the paint composes from cannot advance. Handed to `runtime-working-set-ownership`. |

**Tidy-up exoneration (already measured).** The whole `runtime-code-tidyup` diff on the live-capture
display path is behavior-preserving: `activeNoteEntriesMatch` is field-identical to all four originals;
the `Phase::Trim` `pop_back()` was unreachable because `begin` already resizes `rows` to `targetCount`;
`Loop::liveRecordCanApplyCompletedTicks`' removed guards all fell through to `true`;
`RuntimePlaybackViewBuildGrain` → `Grain` keeps every comparison by enumerator and the state machine is
text-identical; the `2048`/`768u`/closure-flag/cycle-mask literals map to identical values; `barCount`
matches `Config::TICKS_PER_BAR`; the `ceilGrains` sites all have the `capacity == 0 → UINT64_MAX` guard
before the ceiling term; P5's three splits left RAM1 `code` and `_itcm_block_count` unchanged, so no
function crossed ITCM/FLASH.

## Debugging boundary

```
MIDI note-on/off -> Track/Loop capture + pending-note changes   <- trust (tidy-up-neutral, not touched)
                 -> DisplayManager paint pipeline                <- current investigation
                      frame phases (Resolve/Draw/Transfer)
                      resolveDisplayNotes compose (committed + capture + pending)
                      draw-source selection / pending representation
```

Do **not** revisit the capture, snapshot or companion content owners for this symptom: the tidy-up audit
above and the byte-identical P3 differential fixtures already bound them. **Amended at closure
2026-09-18:** the *content* owners stay bound, but the snapshot **publication admission gate** is no
longer excluded — the closure evidence above makes the gated (never-advancing) published root the
hand-off pointer. The boundary is therefore "capture/companion content and the tidy-up diff are
exonerated; publication-gate pressure is owned by `runtime-working-set-ownership`."

## Ruled out so far (this pass)

1. **Frame-phase machine race.** `DisplayManager::update()` runs exactly one phase per call
   (`displayFramePhaseToRun` → `advanceDisplayFramePhase`), and `shouldClearResolvedPaintNotesNow` only
   clears `liveDisplayNotes` when the phase is `Idle`; with a frame in progress the clear is deferred to
   `applyCoalescedPaintInvalidation()` at `frameComplete`, which is followed by a `Resolve` before any
   `Draw`. So a mid-frame `invalidateLiveDisplayCache(false)` cannot blank the frame that is about to be
   drawn through this path.
2. **Per-note visual-cache full invalidation.** One `stale_all` in the entire overdub window.
3. **Tidy-up P1–P6** (see exoneration above).
4. **The dip is not a draw-only artifact** (added at closure): `DISP` is sampled at `Transfer` from
   `resolvedPaintNotes()`, the same vector `Draw` read, and it recorded the dip — so the row was missing
   from the **compose**, which is what makes it the successor's business rather than a rendering quirk.

## Next steps (in order) — superseded at closure 2026-09-18

Kept for provenance. Step 1's per-frame instrumentation was not built: the on-change `DISP` family
already exposes the dip (a decrease scan needs no firmware change), so the denser-telemetry step is
withdrawn with the closure. Steps 2–4 transfer to `runtime-working-set-ownership` as the re-measure
recipe in the closure section.

1. **Instrument the compose per frame** (capture-gated, `SESSION_CAPTURE`), not every 30th frame: log the
   sizes and identity of `liveDisplayNotes`, `liveDisplayPendingPaintNotes_`, `resolvedPaintUsesPending_`,
   the draw-source kind, and the capture/pending overlay counts. This is the observability the current
   `DFRAME` cadence lacks.
2. **Reproduce with a registered stimulus.** The base preset (`STIM-BASE-2-2-01@v1`) already overdubs
   notes; use `host_midi_hitl.py run --preset base --track 6 --loop-slot 1 --midi-channel 6`. If a denser
   note cadence is needed, fork a **temporary** stimulus variant (`--stimulus-temp-suffix`) rather than
   editing a canonical stimulus.
3. **Name the RC** from the first frame where the painted note set loses a row, then write the invariant
   ("a note present in the compose before and after a note-off must be painted in every frame") and open
   the GitHub Bug.
4. **Fix one root cause**, with a native fixture from the captured slice; HITL only for what the host
   cannot cover. Re-run `pio test -e native` before sharing the commit.

## Non-goals

- Do not change the committed-content owners (snapshot, passes, companion) for this symptom.
- Do not widen `SESSION_CAPTURE` gating; denser paint telemetry would have been diagnostic only.
- Do not fold this into `runtime-code-tidyup` (PR #42): that change is behavior-preserving and this is a
  pre-existing base-branch defect.

## Closeout requirements

Superseded at closure: none of these are owed, because no fix landed and no GitHub Bug was opened. If
the dip survives the successor's publication/lease repair and re-measurement, this plan is re-opened
with the same invariant and this closeout list.

Native suite; HITL evidence on the selected slot with the blink cleared; update `CURRENT_WORK.md` /
`PROJECT_STATE.md`; `DECISION_LOG` only if the fix changes a documented rule.
