# Reject same-pitch NoteOn while pending

**Status:** RC1 shipped — device PASS [`212509`](../../captures/session_20260909_212509.log) / [`212544`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_212544.json) `10/10` including `pending_same_pitch_note_on_not_loop_end_tail`. `PENDON,reject,49,5,1598,1701`. No `1869` tail.  
**Kind:** bugfix  
**Evidence:** [`session_20260909_202226.log`](../../captures/session_20260909_202226.log) USB `48` On@435, On@520, Off@529; persisted paint [`session_20260909_203112.log`](../../captures/session_20260909_203112.log) `DNTE,48,435,435,1869`  
**Does not reopen:** replace-on-retrigger (synthetic Off), `finishOpenNotes` loop-end tails, RC1 mover `80–2303`, compose `STIM-NE-OD-COMPOSE-01` / `02` / `03` / `04`

## Debugging boundary

```text
HITL emit_immediate_first_step + gate==step
    ← host overlap of first two same-pitch gates; do not edit compose 01–04
Track::noteOn pendingNotes replace + recordMidiEvents(NoteOn)
    ← this plan (reject the second On)
LIFO Off pairs the later On; first On stays in capture
    ← trust once the second On is not appended
foldLiveCaptureIntoNoteEditSession → reconstructDisplayNotes finishOpenNotes
    ← do not change; orphan On must not exist
```

## Problem

`Track::noteOn` keeps one `pendingNotes[{note, channel}]` slot. A second live NoteOn **replaces** that slot and is appended. The next NoteOff closes the **second** On (LIFO). The first On stays in `capture.store` with no Off.

In-edit fold then calls `mergeCaptureNotesAsAdded` → `reconstructDisplayNotes(..., finishOpenNotes=true)`. That unpaired On becomes a loop-end tail.

`202226` second live stream (after the move), pitch 48, loop 2304:

| USB | Tick |
|---|---|
| NoteOn | 435 |
| NoteOn | 520 |
| NoteOff | 529 |

`203112` select at `01:03:02:03` (tick 435) paints `DNTE,48,435,435,1869`. `1869 = 2304 − 435`. That length was never gated.

HITL pitch is the selected seed (48). The host overlap is `stream_pattern_for_bars` with `emit_immediate_first_step=True` and `gate_clocks == step_clocks`: first On at phase clock 1 (Off due at 13), second On at clock 12. Same pattern on the first live stream (`1591` then `1690`). A keyboard can send the same On-On-Off.

## Chosen policy (not replace-on-retrigger)

Deferred **replace-on-retrigger** would append a synthetic Off for the first On and **keep** the second On. That stays deferred ([`MIDI_PROTOCOL_GUIDE.md`](../Guides/MIDI_PROTOCOL_GUIDE.md), [`loop_pass_snapshot_effective_state_refinement.md`](loop_pass_snapshot_effective_state_refinement.md)).

This plan does the opposite, as requested: **drop the second same-pitch NoteOn immediately**. The first On stays in `pendingNotes`. The next NoteOff closes it. No synthetic Off. No later `finishOpenNotes` / `closeOpenTails` repair.

Equal-tick Off-then-On is unchanged: Off erases the slot, then On is a new pending note.

## Architecture checkpoint (RC1)

| Question | RC1 |
|---|---|
| Ownership change? | NO — extend `Track::noteOn` before `pendingNotes` assign and `recordMidiEvents` |
| State transition change? | NO — RECORDING / OVERDUBBING unchanged; capture admission only |
| Unbounded interval owner? | NO — one `pendingNotes.find` |

Do **not** add a second pending slot. Do **not** insert events on the MIDI interval path. Do **not** change `finalizePendingNotes`, `LoopStopFinalize`, or `reconstructDisplayNotes`.

MIDI thru (if already sent before `Track::noteOn`) stays as-is. Reject is capture-only.

## RC1 — reject NoteOn when the key is already pending

**Owner:** `Track::noteOn` in [`TrackCaptureInput.cpp`](../../src/Track/TrackCaptureInput.cpp)

Today:

```text
pendingNotes[{note, channel}] = pending;  // replaces first
recordMidiEvents(NoteOn, ...);
```

Required:

```text
if pendingNotes has {note, channel}:
    do not replace the slot
    do not recordMidiEvents(NoteOn)
    return
else:
    existing snapshot + assign + recordMidiEvents
```

`Track::noteOff` already no-ops when the key is missing. After reject, the physical Off still finds the first slot and appends Off against that On.

**Invariant:** While `{note, channel}` is in `pendingNotes`, a later NoteOn on that key is not appended and does not change `startNoteTick`. The next NoteOff pairs with the first On.

**Native fixture (`202226` second-stream slice):** admission and pairing, not only absence of `On@520`:

```text
events: NoteOn 48 @435, NoteOff 48 @529  (must not contain NoteOn 48 @520)
pending after NoteOn@520: {48, channel} exists, startNoteTick == 435
after NoteOff@529: {48, channel} absent
reconstructed: start 435, end 529, length 94
```

Companion in the same suite: `On@435`, `Off@529`, `On@529` admits the second On.

Replace `test_retrigger_pending_note_slot_replaces_first` with `test_retrigger_pending_note_rejects_second_same_pitch`. Update the `Track::noteOn` comment. The rejected NoteOn is not recorded in capture; it is not a MIDI-input rejection.

**Observability:** `SC_PENDING_SAME_PITCH_NOTE_ON_REJECT` on the reject path only (pitch, channel, pendingStartTick, rejectedTick). Accept path has no diagnostics work.

**HITL:** do not edit compose `01`/`02`/`03`/`04`. Verifier `pending_same_pitch_note_on_not_loop_end_tail` fails `203112`-style `DNTE,48,435,435,1869`. Device compose PASS [`212509`](../../captures/session_20260909_212509.log).

**Shipped:**
- `Track::noteOn` rejects a second same-pitch On while pending
- `SC_PENDING_SAME_PITCH_NOTE_ON_REJECT` on reject only
- Native: `test_retrigger_pending_note_rejects_second_same_pitch`, `test_retrigger_pending_note_admits_on_after_off_same_tick`
- `pio test -e native` **1811 succeeded, 1 skipped**
- RAM1 locals **7008**
- Device compose [`212509`](../../captures/session_20260909_212509.log) / [`212544`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_212544.json) **10/10**; `PENDON,reject,49,5,1598,1701`

## Out of scope

| Item | Why |
|---|---|
| Synthetic Off + keep second On | Deferred replace-on-retrigger |
| HITL `stream_pattern_for_bars` Off-before-On | Separate host fix; do not change compose stimuli to hide this |
| `finishOpenNotes` / `closeOpenTails` | Repair after the orphan exists |
| RC1 mover Length `80–2303` | Different owner (`shouldRejectMoverPreCommitRow`) |

## Docs after RC1

- [`MIDI_PROTOCOL_GUIDE.md`](../Guides/MIDI_PROTOCOL_GUIDE.md) — same-pitch pending NoteOn is rejected; replace-on-retrigger stays deferred
- [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) — one line under capture admission
- [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) / [`PROJECT_STATE.md`](../Runtime/PROJECT_STATE.md)
