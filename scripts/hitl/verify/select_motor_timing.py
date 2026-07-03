"""HITL timing aligned with `NoteEditFaderMotorTiming.h` (firmware)."""

from __future__ import annotations

# Mirrors kSelectFaderMotorIdleMs (300 ms debounce before dependent motor flush).
SELECT_FADER_MOTOR_IDLE_MS = 300

# Firmware flushes when (now - lastSelectFaderTimeMs) >= 300; host wall-clock anchors
# often observe 299 ms — use a small tolerance below the firmware threshold.
MOTOR_SYNC_MIN_DELAY_S = (SELECT_FADER_MOTOR_IDLE_MS - 10) / 1000.0

# Parallel burst (~249 ms) + margin after debounced flush.
MOTOR_SYNC_MAX_DELAY_S = 0.65

# MO F2/F3/F4 after select_motor_sync sent=1 (parallel_timed_burst).
MOTOR_MO_WINDOW_S = 0.4

# Dwell cluster gap (same as kSelectFaderMotorIdleMs).
SELECT_CLUSTER_GAP_S = SELECT_FADER_MOTOR_IDLE_MS / 1000.0

# ch13 clear ack after notegate MO (DROID NOTE_EDIT motor-ack blocks).
NOTEGATE_ACK_WINDOW_S = 0.75
