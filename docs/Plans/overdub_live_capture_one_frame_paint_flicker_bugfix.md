# Overdub live-capture one-frame paint flicker

**Status:** OPEN — investigation started 2026-09-18. No root cause confirmed yet; no fix landed.
**Work identity:** `CURRENT_WORK.md` § Now implementing. GitHub Bug: to be opened once the owner and
invariant are confirmed ([GITHUB_WORK_TRACKING.md](../Authority/GITHUB_WORK_TRACKING.md) § When a capture
becomes a Bug).
**Branch:** base is `feature/resumable-midi-boundary-playback-maintenance` (`4aacd18e`); this is a
base-branch bug, **not** a `runtime-code-tidyup` regression.

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
| Same log, `DFRAME` family | `SC_DFRAME` fires every 30th frame (`DisplayManager.cpp` `dframeCounter % 30U`), so the existing telemetry cannot catch a one-frame dip. Denser sampling is the first diagnostic step. |

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
above and the byte-identical P3 differential fixtures already bound them.

## Ruled out so far (this pass)

1. **Frame-phase machine race.** `DisplayManager::update()` runs exactly one phase per call
   (`displayFramePhaseToRun` → `advanceDisplayFramePhase`), and `shouldClearResolvedPaintNotesNow` only
   clears `liveDisplayNotes` when the phase is `Idle`; with a frame in progress the clear is deferred to
   `applyCoalescedPaintInvalidation()` at `frameComplete`, which is followed by a `Resolve` before any
   `Draw`. So a mid-frame `invalidateLiveDisplayCache(false)` cannot blank the frame that is about to be
   drawn through this path.
2. **Per-note visual-cache full invalidation.** One `stale_all` in the entire overdub window.
3. **Tidy-up P1–P6** (see exoneration above).

## Next steps (in order)

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
- Do not widen `SESSION_CAPTURE` gating; the denser paint telemetry above is diagnostic and must be
  reverted or gated before this work closes.
- Do not fold this into `runtime-code-tidyup` (PR #42): that change is behavior-preserving and this is a
  pre-existing base-branch defect.

## Closeout requirements

Native suite; HITL evidence on the selected slot with the blink cleared; update `CURRENT_WORK.md` /
`PROJECT_STATE.md`; `DECISION_LOG` only if the fix changes a documented rule.
