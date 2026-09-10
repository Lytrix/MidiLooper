# NOTE_EDIT stacked move + deselect drops the note from display

**Status:** FROZEN — RC1 shipped (device PASS)  
**Kind:** bugfix  
**Evidence (fail):** [`session_20260909_232309.log`](../../captures/session_20260909_232309.log)  
**Evidence (pass):** [`session_20260910_100844.log`](../../captures/session_20260910_100844.log) — move + empty-step; `VCACHE,full notes` stays 42 (no drop). Native `test_keep_committed_mover_in_visual_cache_232309` PASS.  
**Does not reopen:** control-plane RC7/RC8, F1 select display lag ([`note_edit_fader1_select_display_lag_bugfix.md`](note_edit_fader1_select_display_lag_bugfix.md)), compose `STIM-NE-OD-COMPOSE-01` / `02` / `03` / `04`, persist-parked close, piano-roll retain  
**Does not start:** a second display owner, `CaptureAwareGather` `[0, loopLength)`, snapshot publication redesign

## Debugging boundary

```text
F1 / F2 motor travel (ValueOnly notegate hold)
    ← control-plane RC7 — not this

Move note 55 onto the 64@235 / 67@238 sixteenth, then select-away commit
    → commitAllPendingNoteEditActions writes NoteRange 240–289
    → Loop::rebuildVisualCacheFromPasses
    → resolveDisplayNotes(passes) vs snapshot spans
    ← this RC
```

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO if the fix extends `rebuildVisualCacheFromPasses` / `resolveDisplayNotes` / post-commit visual cache after `commitEditAction` |
| State transition change? | NO — select-away already commits |
| Unbounded interval owner? | NO if the slice does not add a full-loop gather on the MIDI interval |

Stop if the fix needs a new snapshot publisher or a second paint owner.

## Capture pin ([`232309`](../../captures/session_20260909_232309.log))

Move of `noteId=8` pitch 55 lands at 240–289 (`EditSessionAction` type=3 at 286.917). Live paint is `DNTE,55,240,240,49,3`. Select-away at 289.524 to `67@238`:

| Fact | Proof |
|---|---|
| Commit wrote the parked mover | `pre-commit row: targetNoteId=8 … NoteRange start=240 end=289`; `canonical=1 apply_owned=1` |
| Store / snapshot spans still have the note | `lcr,srcspan id=8 p=55 s=240 e=289`; `pairrows,at=spans,count=8` |
| Pass flatten and visual cache do not | `pairrows,at=passes,count=7`; `VCACHE,full,ev,16,notes,7` |
| Display follows the cache | `DISP,0,OVERDUBBING,1536,7,7,7,7`; later `DFRAME,7` |
| Empty-step at 144 is after the drop | `select_apply … 144 … empty_step` at 289.703 |

`rebuildVisualCacheFromPasses` after `commitEditAction` is the owner to trace. Do not treat this as a select-inventory miss (`note_edit_reselect_moved_stacked_note_bugfix.md`).

## Invariant

After select-away commit of a Visible mover onto a stacked sixteenth, visual cache and `DISP` keep that note at the committed start. Snapshot spans and pass flatten must agree on membership for that `NoteId`.

## Shipped (RC1)

`commitAllPendingNoteEditActions` calls `Loop::keepCommittedMoverInVisualCache` after `rebuildVisualCacheFromPasses` and `retireSupersededPitchDisplayNote`. Missing `NoteId` is copied from `publishedSnapshot()` (else `unpublishedSnapshot()`). Not a second display owner.

Native: `test_keep_committed_mover_in_visual_cache_232309`. Host: `test_vcache_drop_after_deselect_fails`. HITL verifier: `moved_note_kept_after_deselect` on `note_edit_reselect_stacked`.

Device [`100844`](../../captures/session_20260910_100844.log): `noteId=13` commit `294–486` then `empty_step` at 192; every `VCACHE,full` stays `notes,42`. `keep_committed_mover` did not need to insert (rebuild already kept the id).

## Misattribution guard

Do not reopen F1 one-resolve or control-plane RC3/RC7/RC8 for a cache membership drop. Do not add a second display owner or full-loop gather on the MIDI interval.
