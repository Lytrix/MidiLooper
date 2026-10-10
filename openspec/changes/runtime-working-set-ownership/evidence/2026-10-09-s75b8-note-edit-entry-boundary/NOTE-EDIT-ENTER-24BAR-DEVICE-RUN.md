# NOTE_EDIT enter on a recorded-then-playing take — device run (record intent 24 bars)

Owner-requested validation of the **S7.5b.8b — bounded NOTE_EDIT entry** enter gesture in the shape
"record a long take, let it play, then enter NOTE_EDIT". Owner authorized, in the same session,
clearing/overwriting **only** operator track 8 / slot 4 (firmware indices 7/3) and no other slot, no
SD rename/delete/quarantine, no upload and no board reset.

## Pin and preconditions

| Item | Value |
|---|---|
| Git | `067be7f3d7bf0500a49da589c02d78c0904f9cda` + dirty tree (`metadata.json`) |
| Image | `teensy41-capture-serial`, HEX SHA-256 `c2dee3be8b2f1c54b4855678331b6c65704d3931cc1a55eca7a6b31229562c88` — **the image already on the board**, not re-uploaded |
| Tree match | no `src/`/`include/`/`scripts/` file newer than the 2026-10-10 18:58 upload, so the flashed image is the current tree |
| Preconditions | `/dev/cu.usbmodem154944801` present, MIDI in/out `Teensy MIDI` present, no `.device-shutdown-incomplete.json` latch, no stray capture process |
| Boot | `--no-boot-reset` (attach without reset), `--capture-boot-wait 30` |

No preflight Stop/drain was run before the leg: its only purpose is to protect a *reset*, and this run
performs none. The managed capture owns attach readiness and the canonical terminal teardown.

## Invocation

```sh
HITL_MANAGED_RUN_TIMEOUT_S=900 .venv/bin/python scripts/host_midi_hitl.py run \
  --preset note_edit_reselect_stacked \
  --track-number 8 --loop-slot 4 --midi-channel 8 \
  --record-bars 24 \
  --no-boot-reset --capture-boot-wait 30 \
  --out-dir captures/note-edit-enter-24bar-t8s4-01
```

Outcome: exit 1 (scenario `note_edit_reselect_stacked` failed; see below). No canonical stimulus was
edited; the registered `STIM-NE-RESELECT-STACKED-01` was reused unmodified.

## What validated — the enter gesture

Device rows from `captures/note-edit-enter-24bar-t8s4-01/session_20261010_194956.log` (host-time
prefixes; `#CAP` rows carry device microseconds):

```
[150.256] [DEBUG] [BTN] Button press triggered: Ch15 Note38 Type0
[150.256] [INFO] Button press: Edit Mode (short)
[150.256] [INFO] #CAP,NOTE_EDIT_OPEN,sync,visual_notes,8,session_events,0,current_state,0
[150.256] [INFO] #CAP,NOTE_EDIT_OPEN,q3,current=1,root_rev=1,visual_ids=8,store_ids=0,shared=0,only_visual=8,...
[150.259] [DEBUG] [MIDI] Note selection changed: -1 -> 2
[150.259] [DEBUG] EditSelectNoteState::onEnter at tick 360
[150.260] [INFO] EditSelectNoteState: Found and selected note 2 at tick 360
[150.260] [INFO] MIDI Encoder: Entered SELECT mode (bracket=360)
[150.271] [INFO] [MIDI] Edit session: NOTE_EDIT (Program 1, Note 0 trigger)
[150.271] [INFO] NOTE_EDIT session fader sync: outbound coordinator
```

- Both enter markers are present for **this** press (`#CAP,NOTE_EDIT_OPEN,…` and
  `Edit session: NOTE_EDIT (Program 1, Note 0 trigger)`), so the enter is not being satisfied by a row
  left over from the composed preset's base leg.
- The entry opened on a **current, published root** (`current=1`, `root_rev=1`) with **8 visual notes**
  and produced a first selection at tick 360 — i.e. selectable inventory was ready at the entry.
- The target was **playing** when the gesture ran: `#CAP,149235881,ST,Track,STOPPED,PLAYING` (~1.0 s
  before the press).
- Device view of the loop at the entry: `loopLength=2304, navLength=2304, numSteps=48`.

Offline replay of the leg's own verifier over that capture (host-only, `--verify-only`), report
`captures/note-edit-enter-24bar-t8s4-01/verify-01/host_midi_hitl_verify_20261010_195226.json`:

| Check | Result |
|---|---|
| `note_edit_entered` | **true** |
| `playing_motor_value_only` | ok — `playing_motor_sync_judged=2` (was **0** on the 2-bar runs), 0 short, 0 over-gap |
| `value_only_notegate_held` | ok — judged 2 |
| `select_apply_highlight_follows` | ok — `select_apply_note_changed=1`, `dnte_count=83` |
| `moved_note_kept_after_deselect` | ok — 8 visual notes kept |
| `selected_note_moved` | present (`move_index=9118`) — not in the issue list, so it passes here (it failed on the 2-bar runs) |
| `stacked_reselect_after_deselect` | **FAIL** — `dest_tick=336`, `dest_note_idxs=[]` |
| `base` leg | PASS (verifier) |

## What failed, named

1. **`stacked_reselect_after_deselect` has nothing to judge: the reselect gesture was never sent.**
   The scenario aborted at its own deselect wait (`[note-edit-reselect-stacked] empty-step select_apply
   missing` after F1 deselect attempts 1/3–3/3), so the follow-up reselect onto the destination
   sixteenth never ran; hence `dest_note_idxs=[]`. The device rows for that deselect **do exist in the
   same log** — `#DBG select_apply selected_tick=0 note_idx=-1 slot=0 apply=1 reason=empty_step`
   (log line 11177, device ≈164.867 s) and the matching `apply=0` re-evaluation (line 11522, ≈168.480 s)
   — and the offline verifier finds them (`empty_select_index=11176`). So this is a **host-side live
   wait/latency** outcome for those presses, not a firmware contradiction, and it is the same class as
   the earlier `Timed out waiting for stable select` readings.
2. **Stacked-index mismatch on the source select.** The device applied
   `#DBG select_apply selected_tick=552 note_idx=3 slot=2 … apply=1 reason=note_changed` while the
   host's fixture layout expected `note_idx=2` at that tick (and the device's own motor-sync row says
   `note_idx=2 bracket_tick=552`). Tick agreement is exact (552 = sixteenth 11); only the
   host-expected note index differs by one. Recorded, not attributed.
3. **The take is not 24 bars.** The record intent was 24 bars, but the device committed a **3-bar** loop:
   `#CAP,128635719,RECS,stop,3,373,0,18805,2304,0` and
   `[128.635] [DEBUG] [TRACK] Recording stopped @ tick 373 (recStart=0 length=2304)`.
   The base leg's own verifier reports `record_loop_length_mismatch`
   (`expected_final_length 18432`, `actual_final_length 2304`, `raw_length 18805`), and the base report
   is `FAIL (FIRMWARE_CONTRADICTION)` with the composed preset continuing because the loop
   materialized. 8 On / 8 Off / 0 unmatched were recorded with the fixture's spacing (REVT deltas
   96–288). This is the same host/device pacing class already recorded for
   `captures/hitl-live-observation-t8s4-01` (2304 ticks against an intended 1536): **the long-loop
   variant of the "record, play, then enter NOTE_EDIT" shape was not exercised in this run**, because
   the device loop was 3 bars (2,304 ticks), not 24 bars.

## Terminal state and limits

- Managed `shutdown-result.json`: `status complete, drains 2, ok true` ("2 stop+drain cycle(s) reached
  verified persistence idle"); **no** incomplete-shutdown latch.
- Ring: 3 `RING,shed,1` rows, **no** `RING,overflow`, no critical rejection/eviction rows.
- The failed take (8 fixture notes, `length=2304`) is left on track 8 / slot 4; nothing else was cleared
  or written, no SD rename/delete/quarantine, no upload/reset, no canonical stimulus edit.
- Not claimed: no S7.5b.8b/8c/8d checkoff, no timing/envelope bound, no long-loop (24-bar) entry
  validation, no reselect/move selection PASS, no content-parity verdict. The `#CAP` family in this
  capture is the default image's; the windowed-lane rows remain qualification-image-only.
- Next bounded candidates (in order): (a) a true long-loop take — resolve the record length/pacing
  blocker (2304 vs 18432) before re-running this shape, otherwise the run can only ever exercise a
  3-bar loop; (b) why the live deselect wait expires although the apply row is emitted
  (press→row latency vs the 2 s window); (c) the host↔device stacked-index convention at the source
  sixteenth.

## OpenSpec closure status

**Closed by this run:** the S7.5b.8b live-entry HITL finding *"the stated criterion is never exercised"*
(`HARDWARE-CLEAR-DIAGNOSIS.md`, `SEED-TOGGLE-REPAIR.md`, `LIVE-ENTRY-HITL-RECIPE.md`) — NOTE_EDIT entry is now
measured on hardware on a settled, published current root while the target is playing, with the enter rows and
the leg's own `note_edit_entered` check green.

**Not closed by this run — recorded in `tasks.md`, not smoothed:**

- **S7.5b.8b stays unchecked.** Its criteria are the production interleavings plus focused/full native and
  capture-build/RAM1 verification (explicitly: *hardware duration qualification remains S7.5b.8d*); none of
  those is supplied or replaced by this run, and the leg as a whole still exits 1.
- **S7.5b.8d stays unchecked.** No unit duration, composed MIDI input gap, capture timestamp error, per-route
  output lateness/queue residence, source/window-owner gap, feedback completion or RAM1/PSRAM/LoopPool
  high-water was measured, and the loop length is 3 bars, so the "both loop lengths" condition is not
  established either.
- **The parent S7.5b.8 and S7.5 stay unchecked**; no 3.10 — bounded service or S7.5 — combined continuity
  handoff follows, and no timing/envelope claim is made from correlation or an overflow-free log.
