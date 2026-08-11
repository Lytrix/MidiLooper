# RC4f — Record pass tail missing after overdub loop wrap

**Status:** Fix shipped — awaiting verification capture  
**Evidence:** [`session_20260811_121918.log`](../../captures/session_20260811_121918.log)  
**Parent:** [`long_overdub_rolling_window_overdub_bugfix.md`](long_overdub_rolling_window_overdub_bugfix.md)

## Problem

After loop wrap during long overdub, the last segment of the sealed record pass disappears from
the piano roll until overdub stop. Post-stop DISP shows full `visual=2283` vs partial counts
during overdub.

## Root cause

Two compounding issues in the overdub committed display path:

1. **Stale window gather after visualCache completes** — idle slices can finish building
   `visualCache` (`visualCacheDirty=false`) while `resolveDisplayNotesLiveCapture` still holds
   a ~18-bar window gather in `liveDisplayNotes`. `committedLayerChanged` did not detect that
   transition, so auto-follow after wrap kept filtering the stale gather instead of the full
   cache until overdub stop forced a rebuild.

2. **Partial visualCache on overview** — overview minimap used `visualCache.notes` whenever
   non-empty, even when `visualCacheDirty` (sparse idle slices). Tail bars not yet backfilled
   showed as gaps.

3. **Wrap-aware slice removal** — `removeDisplayNotesOverlappingBars` used linear bar ranges and
   mishandled wrap-spanning display notes when rebuilding head neighborhoods after wrap.

## Fix

| Area | Change |
|------|--------|
| `resolveDisplayNotesLiveCapture` | Track `liveDisplayCommittedFromWindowGather_`; promote to full `visualCache` when idle finishes (`shouldPromoteOverdubCommittedToFullVisualCache`) |
| `PianoRollDraw` | Overview minimap uses `visualCache` only when `!visualCacheDirty` |
| `LoopVisualCache` | `removeDisplayNotesOverlappingBars` uses `noteIntersectsWindow` (wrap-aware) |

## Acceptance

- [x] 80-bar record + overdub: after one loop wrap, record tail visible without overdub stop
- [x] After overdub stop while PLAYING: rolling window follows playhead (not blank until track stop)
- [ ] `pio test -e native`
- [ ] `pio run -e teensy41-capture-serial`

## RC4g — Rolling window after overdub stop (`session_20260811_124133`)

**Cause:** `resolveDisplayNotesCommitted` returned window-gathered notes (~16 bars) while
`drawPianoRoll` auto-follow moved the paint window — second filter left the roll empty until
track stop forced a full rebuild.

**Fix:** When `deferVisualRebuild` (PLAYING / stopped-recording), return `visualCache.notes`
(full or partial) and let `drawPianoRoll` own the rolling window filter. Overview minimap
uses partial `visualCache` density again (committed-layer promote handles overdub tail).

## RC4h — Gradual chunk reveal during overdub (`session_20260811_165148`)

**Cause:** Narrow window gather during overdub.

**Fix (revised):** Gather full committed span (`0..loopLength`) when `visualCacheDirty`; `drawPianoRoll` filters rolling window. Do not assign stale dirty `visualCache` (170314 regression).

## RC4i — Overview minimap + record-stop preserve (`session_20260811_170314`)

**Cause:** Partial dirty `visualCache` painted minimap; `invalidateNoteEditDisplayCache` cleared preserved
`liveDisplayNotes` inside `invalidateLiveDisplayCache(true)` after record/overdub stop.

**Fix:** Overview uses fully built `visualCache` only when `!visualCacheDirty`; else full `notes` from
resolve. `invalidateLiveDisplayCache` invalidates note-edit projection without clearing preserved frame
notes.

## RC5 — Incremental display handoff (shipped, device verify)

**Plan:** [`long_overdub_rc5_incremental_display_handoff_investigation.md`](long_overdub_rc5_incremental_display_handoff_investigation.md)

RC5a preserve composed frame / RC5b dirty-cache gate / RC5c promote-adopt into `visualCache` /
RC5d window filter on PLAYING. Full-loop gather retained as recovery only — not default on
`playbackRevision` bump.
