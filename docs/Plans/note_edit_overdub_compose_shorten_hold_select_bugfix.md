# Compose Shorten hold vs reopen select

**Status:** RC1 + RC2 implemented; HITL-04 added  
**Kind:** bugfix  
**Evidence:** [`session_20260909_134549.log`](../../captures/session_20260909_134549.log) (HITL report [`135013`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_135013.json))  
**Does not reopen:** compose Stage 1/2 firmware, `STIM-NE-OD-COMPOSE-01` / `02` / `03`, first-note loop-end display (parked)

HITL machine still PASS: Shorten `EditSessionAction` fired. These rows are after that action, and after the later human NOTE_EDIT enter.

## Debugging boundary

```text
STIM-NE-OD-COMPOSE-03 host recipe
    ← shipped; do not edit 01/02/03 to chase this
EditSessionAction type=1 Shorten noteId=58 2102–2111
    ← trust the action line
LCR ledger / WRAP / DNTE for id=58 after Shorten
    ← RC1
in-edit overdub fold + NOTE_EDIT reopen (current=0)
    ← RC2
exit / re-enter / select / move
    ← STIM-NE-OD-COMPOSE-04
```

## Architecture checkpoint

| Question | RC1 | RC2 |
|---|---|---|
| Ownership change? | NO — extend `applyPendingHideAndShortenToNotes` via `Loop::applyNoteEditCurrentStateToOverdubSourceView` | NO — extend `NoteEditCurrentState` + `openNoteEditSession` + `findSlotIndexForSelection` |
| State transition change? | NO | NO |
| Unbounded interval owner? | NO — NoteId walk of the existing source-view vector | NO — open-boundary seal + slot lookup |

## RC1 — Shorten holds on source view and rematerialize

At 244.993 s Shorten wrote `noteId=58 start=2102 end=2111`. Still OVERDUBBING, LCR source view at 247.563 s kept `58 2102–2212`.

**Invariant:** After NOTE_EDIT Shorten/Hide apply, `overdubSourceViewNotes_` matches the overlay for that NoteId. Rematerialize-open `ensureVisibleRowsForDisplayNotes` must not replace a shorter store span with a longer same-start visual end.

**Shipped:**
- `NoteGeometryResolver::resolve` calls `Loop::applyNoteEditCurrentStateToOverdubSourceView` after `applyEditSessionActions`
- `ensureVisibleRowsForDisplayNotes` keeps the rematerialize Shorten when visual is longer (`134549`)
- Native: `test_note_edit_shorten_clips_overdub_source_view_134549`, `test_ensure_does_not_lengthen_rematerialize_shorten_134549`

## RC2 — reopen select keeps painted notes

Every human NOTE_EDIT after fold (`current=0`, `session_events=255`) selected index 59 at tick 1616, then F1 walked empty sixteenths. Rematerialize `upsertRow` marks every row Active overlap; off-grid notes at 1616 were not targeted by sixteenth pitchbend.

**Invariant:** Rematerialize-open current state is the new committed baseline (not an in-progress overlap). A sixteenth-step fader tick selects an off-grid note in that step.

**Shipped:**
- `NoteEditCurrentState::sealRematerializeBaseline` after rematerialize `ensureVisibleRowsForDisplayNotes`
- `findSlotIndexForSelection` prefers a note in the same sixteenth over an empty grid placeholder
- Native: `test_rematerialize_seal_keeps_painted_notes_selectable_134549`, `test_find_slot_prefers_off_grid_note_in_same_sixteenth_134549`

## HITL-04

`STIM-NE-OD-COMPOSE-04` v1. Preset `note_edit_overdub_compose_last_bar_reopen`. Same last-bar Shorten as `03`, then short-press exit to LOOP_EDIT, short-press re-enter, re-select the seed on the original seed layout, and F2 move. Do not edit `01` / `02` / `03`.

NOTE_EDIT close arms `LoopEditManager::armLoopFaderFeedbackIgnore` before the session type flips to LOOP_EDIT so inbound faders cannot apply loop start/length from the previous NOTE_EDIT positions.
