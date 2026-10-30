# Stored-MIDI verification bound — 2026-10-30

## Host implementation

- The effective-notes phase now derives **one bar per owner turn** through
  `Loop::resolveEffectiveDisplayNotesInWindow` (the one-bar padded source window) and merges with
  `LoopContentResolution::upsertResolvedDisplayNotes`, instead of the whole-pass
  `resolveDisplayNotes`. The former `copyEffectiveCommittedEvents` copy was removed: its vector was
  cleared after the phase without being read by the resolve or the SEVT rows.
- The SEVT row emission is capped at **4 notes per turn while timing-critical** (64/16 otherwise), so
  a 54-row burst no longer runs in one turn.
- `stored_midi_verification` / `sevt_dump` are reclassified from unbounded to **4 ms max / 1 ms fine
  grain**; the production gate is the header-only `boundedIdleOwnerChildMayRun` (no registry-array
  ODR use, so no DTCM snap). The 2.3 denial remains the fallback if the fine grain cannot fit.

## Base capture-build probe

Image `teensy41-capture-serial-no-grain`, HEX
`sha256 bbf88845712e1024fa5acb3f1996e8ff0c768d598ad7c724e0e304b089dec5b0`, retained in
`captures/bbf88845/`. Capture: `captures/riao-dnte4-base/session_20261030_111327.log` (base preset,
2-bar record + overdubs).

- Two verification dumps completed (`drain_emit_start` -> `drain_complete` for the record and overdub
  queues): `dnte,src,effective,count,54` and `dnte,src,frame,count,54` each time; 332 `DNTE` rows total.
  The effective count matches the frame count, which is the dump's own parity check.
- **No `loop_rem,idle_maint` row during either dump.** The periodic `DIAG,idle_maint` max stays at or
  below **1 ms** across the dump turns; the earlier 54-row burst measured 44 ms in one turn. The one
  `loop_rem,idle_maint` row is 130,873 us at 8.8 s with a load job active (boot lane, separate).
- Full native **3706 / 7 skipped / 3699 / 0 failed** (parallel run); no-grain build SUCCESS at RAM1
  `96,192`, ITCM `391,880 + 1,336`.

## Under-transport limitation

The "completing under transport" condition is **unreachable in the current transport model**, not
merely unbuilt:

- `transportFullyIdle = !isAudiblePlaybackActive() && !isRecording() && !isStoppedRecording()`.
  `Track::isAudiblePlaybackActive()` is `trackState == TRACK_PLAYING || TRACK_OVERDUBBING ||
  TRACK_OVERDUB_STOPPING`; muting a slot does **not** change `trackState`.
- The only per-track stop is the **global** transport stop. The loop-button short press toggles slot
  mute (long clears/switches, double starts overdub); the track-button short selects, double mutes,
  long solos. There is no per-track stop gesture.
- `timingCriticalTrackActive` requires some track audible; the global stop makes every track
  inaudible. A populated target track therefore cannot be both fully idle and attended by a
  timing-critical aggregate.

The mixed-leg run (`captures/riao-mixed-71b/`, DROVE_LEG) queued the verification twice but produced no
`dnte` rows while its same-track A/B interval played. The mute probe
(`captures/riao-under-transport/`) short-pressed the playing slot after the second overdub stop; the
slot muted but the track state stayed PLAYING, and the dump still ran only after the final stop
(76.0 s vs stop 73.6 s). The gate's timing-critical branch is therefore dead for this child in the
current control surface; the **2.3 denial is the effective behavior**, and the bounded derive/4-row
SEVT slice is exercised on the stopped lane (base probe above). This is recorded as a design limit,
not a missing run: an under-transport run cannot be constructed without a new per-track stop feature.
