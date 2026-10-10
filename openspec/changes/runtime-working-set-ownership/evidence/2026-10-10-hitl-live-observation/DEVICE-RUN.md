# Authorized track8/slot4 device qualification attempt — FAIL

## Authorization and pin

Owner approved upload and registered stacked-reselect rerun, overwriting only operator track8/slot4 (firmware7/3). Preflight `captures/hitl-live-repair-upload-pre-01/shutdown-result.json` verified two idempotent Stop/fenced STOPPED/persistence-idle cycles, complete/2/ok; no latch. `pio run -e teensy41-capture-serial -t upload` SUCCESS,14.253s; before/after HEX SHA256 exactly `c2dee3be8b2f1c54b4855678331b6c65704d3931cc1a55eca7a6b31229562c88`. RAM1 variables96192/code392360/padding856 >=840 unchanged. No extra upload/reset/retry or other-slot clear/record gesture, SD rename/delete/quarantine, canonical stimulus edit or Git delivery.

Authoritative invocation:

```sh
.venv/bin/python scripts/host_midi_hitl.py run --preset note_edit_reselect_stacked --track-number 8 --loop-slot 4 --midi-channel 8 --no-boot-reset --capture-boot-wait 30 --out-dir captures/hitl-live-observation-t8s4-01
```

Upload itself owns the fresh boot; no second reset. Metadata pins commit `067be7f3d7bf0500a49da589c02d78c0904f9cda` + dirty tree and exact image. Capture `session_20261010_185711.log` carries `DIAG,boot_reset,crash=0` at1.720004s. Registered stimulus reused, unmodified. Process exit1.

## What worked

- Intended recording has8 On/8 Off/0 unmatched/0 out-of-range: no empty setup take attribution. Record `RECA,3,0`28.360701s, `RECS,stop,3,2023,0,2023,2304,0`33.248489s.
- Live observation/framing actually works under global transport. All three windows replay through the real fenced/status parser:
  -51.788163s: track7/slot3/STOPPED/transport0;
  -52.298034s: track7/slot3/STOPPED/transport0;
  -52.920859s: track7/slot3/PLAYING/transport1.
  Global start completed52.507s. Both fences are present for each reply. No second clock-driven transport toggle was sent.
- Managed final `shutdown-result.json`: status complete, drains2, oktrue; fresh two Stop/drain cycles completed before capture close, then existing final stopped heap observation. No incomplete-shutdown latch.

## Remaining failures — not smoothed

- Seed pacing/grid: stream reports device clock learned247.2BPM/177pulses despite CLI120 and direct device feedback120.6. Actual length2304 vs intended1536;8 notes, first tick271, last1600, min/max deltas96/288,6 bad deltas vs budget2, sequence mismatches6. First note348.689ms after Record entry,17 estimated clocks vs12 criterion. Device-clock-reader/preamble/pacing attribution remains unresolved, not a proven firmware clock defect. Verifier's `final_length_ok=true` only means its raw2023 rounds consistently to2304; it does NOT establish the intended two-bar fixture.
- Base verdict INCONCLUSIVE_CAPTURE/EVIDENCE_MISSING on `persistence_stage_missing:result`. Full capture later contains PERS result47.080576s/SAVE completed47.094934s/SAVE idle47.913490s, after the base report/close-drain window. This is a result-observation boundary, not evidence persistence never finished. No verifier rule was weakened.
- Live guard still returns SETUP_INCOMPLETE although a valid fresh PLAYING/transport1 reply is in the final log. Its per-request timeout is0.5s; delayed log availability/deadline handling is the leading explanation. Host command/reply arrival timestamps were not independently captured, so precise causal timing is UNKNOWN. Firmware command/framing and parser replay are VERIFIED; online observation did not qualify.
- No NOTE_EDIT selection/reselect/content/timing acceptance. No repeated overwrite to chase the same failure. Preserve this take and compare a controlled clock/response-deadline fixture before another device leg.

## Evidence integrity and terminal state

Fenced stats37.015214s: attempts2134/ok2134/tx1749/shed30/tierA_evict0/overflow0/crit_rej0/wipe0. This is a sampled preterminal read, not a full-window loss guarantee. Later persistence completion and canonical shutdown are present. Hardware task stays unchecked; no firmware/host behavior edits or native rerun in this upload/rerun session. Local verification from the preceding slice remains100 focused/3034 native passed plus7 skipped and same host-baseline failures.
