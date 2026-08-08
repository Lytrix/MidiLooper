# Fader dependent outbound regression bugfix

**Status:** Implemented 2026-06-30  
**OpenSpec:** `openspec/changes/note-edit-fader-feedback-regression/`  
**Golden reference:** commit `cc21df9`

## Problem

F1 slot change no longer drives F2–F4 motors reliably. Phase 6–7 outbound state machine (`NoteSelectDependent` coalesce + quiet dedupe) replaced the working cc21df9 deferred select-dependent path.

## Root causes (RC29–RC32)

| RC | Issue |
|----|-------|
| RC29 | Fine outbound used stale `referenceStep`; coarse used live anchor |
| RC30 | Coalescing state machine starved select-dependent F2–F4 delivery |
| RC31 | Motor triggers fired when send helpers no-oped |
| RC32 | Processor deferred scheduling disabled without equivalent in `NoteEditManager` |

## Fix (shipped)

1. **Restore cc21df9 select path** — `scheduleDependentFadersFromSelect()` on F1 slot change; `processSelectDependentFaderRefresh()` sends direct F2–F4 burst after F1 quiet (400 ms), bypassing `requestFaderOutbound` coalesce.
2. **Unify fine anchor** — `positionFineCcFromAnchorTick()` from live note start tick (same source as coarse).
3. **Sync anchors on geometry** — `commitBracketTickFromGeometry` → `syncAnchorFromBracketTick`.
4. **Always refresh on slot change** — `refreshAnchorFromLiveSelection` even when `applyNoteSelect` returns false (D36).
5. **Quiet starvation belt** — `shouldSkipQuietDependentRefresh` does not dedupe when pending select-dependent refresh or coalesced `NoteSelectDependent` is queued.
6. **D34 send honesty** — send helpers return `bool`; outbound pipeline skips triggers on no-op.

## Kept from Phase 7

- D36 `commitBracketTickFromGeometry`
- D37 `sendFader1BracketFeedbackFromGeometry` on geometry drivers

## Verification

- `pio test -e native` — fine anchor helpers + existing outbound plan tests
- Capture: F1 slot → F2/F3/F4 within **500 ms** of last `SCHED_SELECT_DEP`; fine CC at off-grid ticks
- Compare against cc21df9 behavior checklist

## RC33–RC35 (2026-06-30)

| RC | Fix |
|----|-----|
| RC33 | 200 ms trailing debounce on slot change |
| RC34 | Select-kind outbound uses inventory note, not `focus.last` |
| RC35 | Quiet refresh deduped when select-dependent debounce pending |

## Out of scope

- Full revert to cc21df9 (session state, outbound machine removal)
- D32/D33/D35
