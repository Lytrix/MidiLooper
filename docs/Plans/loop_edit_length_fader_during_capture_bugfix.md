# LOOP_EDIT length fader during capture — bugfix

**Status:** RC1 **SHIPPED** (2026-09-10)  
**Evidence:** [`session_20260910_170743.log`](../../captures/session_20260910_170743.log), [`165541`](../../captures/session_20260910_165541.log)

## Root cause

While `LOOP_EDIT` is active (boot default / HITL leaves it on), pitchbend **ch=14** is the loop-length motor fader. Mid-RECORD/OVERDUB the physical fader still emits pitchbends (≈ −7800 → **4 bars / 3072**). `LoopEditManager::handleLoopLengthInput` applied that and resized the live loop (e.g. 2304→3072), breaking HITL `base` OD2 span checks.

```text
Received pitchbend: ch=14 value=-7804
LOOP EDIT: Changing loop length from 3 bars (2304) to 4 bars (3072)
Pitchbend ch=14 routed to loop length fader (LOOP_EDIT mode)
```

## Fix (RC1)

Ignore loop **length** and **start** fader input while `track.isRecording()` or `track.isOverdubbing()`:

- Predicate: `shouldIgnoreLoopGeometryFaderDuringCapture` in `LoopEditDepartGeometry.h`
- Owners: `LoopEditManager::handleLoopLengthInput` / `handleLoopStartFaderInput`

PLAYING / STOPPED LOOP_EDIT length edits unchanged.

## Gates

| Gate | Result |
|---|---|
| Native predicate | PASS (`test_ignore_loop_geometry_fader_during_capture`) |
| Full `pio test -e native` | **1825 succeeded, 1 skipped** |
| RAM1 locals | **7008** |
| HITL `base` | **PASS** [`171420`](../../captures/session_20260910_171420.log) — ch14 still received; `Loop length input ignored: capture active` |
