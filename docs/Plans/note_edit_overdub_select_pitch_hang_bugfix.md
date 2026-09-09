# NOTE_EDIT select/pitch during overdub hangs

**Status:** shipped — device PASS [`152705`](../../captures/session_20260909_152705.log) / HITL-04 [`152745`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_152745.json)
**Kind:** bugfix  
**Evidence:** [`session_20260909_145634.log`](../../captures/session_20260909_145634.log)

## Debugging boundary

```text
NOTE_EDIT + OVERDUBBING select / pitch
    ← this RC
Compose Shorten hold / reopen select (RC1/RC2)
    ← not this
```

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — extend `ensureNoteEditDisplayProjectionCachesBuilt` and existing `ValueOnly` send |
| State transition change? | NO |
| Unbounded interval owner? | NO — removes full-loop gather and `delay()` from the fader path |

## Root cause (`145634`)

Every F1 note change during overdub rebuilt the NOTE_EDIT paint cache, including full-loop `rebuildDisplayNotesInWindow` (`CaptureAwareGather`). Focus rebuild to selection-changed was **56–80 ms**. After select idle, `select_motor_sync sequence=parallel_timed_burst` ran `delay()` (~250 ms). USB Host MIDI then hit batch cap 128. Wall-clock gaps after select/pitch were **700–1105 ms**.

## Invariant

Fingerprint / selected-note paint-cache changes reuse the committed base. While overdubbing, dependent/F1 motor updates send values without the blocking timed burst.

## Device proof

[`152705`](../../captures/session_20260909_152705.log): `select_motor_sync sequence=value_only`, `geometry_motor_sync sequence=value_only`. No `#CAP,RING,overflow`. No USB Host MIDI batch-cap. HITL `--preset note_edit_overdub_compose_last_bar_reopen` machine **PASS** 4/4. Native **1804 succeeded, 1 skipped**. RAM1 locals **7008**.
