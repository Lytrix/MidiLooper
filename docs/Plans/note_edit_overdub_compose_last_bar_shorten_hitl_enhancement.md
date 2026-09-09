# Compose last-bar beat-4 Shorten HITL

**Status:** Device PASS [`133714`](../../captures/session_20260909_133714.log) / [`133744`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_133744.json)  
**Kind:** enhancement  
**Does not start:** firmware, editing `STIM-NE-OD-COMPOSE-01` or `02`, first-note loop-end display (parked)

Extends the compose pass that already records a wrap live note and remaps selection. After that new note is recorded, move the seed onto it at beat 4 of the last bar and require Shorten.

## Required stimulus

```yaml
required_stimulus:
  scenario: note_edit_overdub_compose_last_bar_shorten
  stimulus: STIM-NE-OD-COMPOSE-03
  version: 1

must_prove:
  - base_recorded
  - committed_od1
  - in_edit_overdub_active
  - live_capture_notes_present
  - selected_note_moved
  - new_note_shortened
```

Do not edit `01` or `02`. Everyday compose overlap stays `02`.

## Sequence

1. Same seed as compose (`STIM-BASE-2-ONE-OD-01`)
2. NOTE_EDIT, in-edit overdub, same-pitch live Capture
3. Wait until a live On sits within one beat of last-bar beat 4
4. Re-select the seed, F2 to that beat immediately
5. Serial: `EditSessionAction` type=1 Shorten

Last-bar beat 4 = `(bars - 1) * 768 + 3 * 192`. On the 2304-tick device loop that is **2112** (sixteenth 44).

## Pre-implementation review

### Ready
- Compose scenario already streams same-pitch live notes and re-selects the seed
- [`132708`](../../captures/session_20260909_132708.log) live ticks include **2118** and wrap **15**

### Resolved
| Topic | Decision |
|---|---|
| New HITL | `STIM-NE-OD-COMPOSE-03` + preset `note_edit_overdub_compose_last_bar_shorten` |
| Mover | Seed, not the newly selected wrap note |
| Proof | Shorten only (Hide is not enough) |

### Open before coding
None.

### Proceed?
YES — shipped. Host tests OK. Device machine PASS 3/3.

Live ticks included **2113**. F2 sixteenth **44** / tick **2112**.
`EditSessionAction` type=1 Shorten `noteId=56` `2100–2111` pitch 48, then
type=3 Move `noteId=57` `2112–2249`. Hide-only is not enough.
