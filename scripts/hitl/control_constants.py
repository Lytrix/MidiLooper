"""Shared MIDI control notes, timing grids, and HITL threshold constants."""

from __future__ import annotations

TRACK_SELECT_NOTE_BASE = 60
LOOP_SELECT_NOTE_BASE = 50
RECORD_BUTTON_NOTE = 36
PLAY_STOP_BUTTON_NOTE = 40
GLOBAL_TRANSPORT_NOTE = 39
CONTROL_CHANNEL_1BASED = 16

# Must match Config::TICKS_PER_BAR / Config::TICKS_PER_16TH_STEP in include/Globals.h.
TICKS_PER_BAR = 768
TICKS_PER_16TH_STEP = 48
TICKS_PER_8TH_STEP = TICKS_PER_16TH_STEP * 2
MIDI_CLOCKS_PER_BAR = 96
MIDI_CLOCKS_PER_BEAT = 24
RECORD_GRID_STEP_CLOCKS = 6  # 16th notes at 24 PPQN
OVERDUB_GRID_STEP_CLOCKS = 12  # 8th notes at 24 PPQN
TICKS_PER_BEAT = TICKS_PER_BAR // 4
DEFAULT_RECORD_NOTE_SPAN_MIN_RATIO = 0.9
DEFAULT_LONG_RUN_BAR_THRESHOLD = 48
DEFAULT_RECORD_STOP_RAM2_FLOOR_BYTES = 0
DEFAULT_RECORD_STOP_RAM2_WARN_BYTES = 12 * 1024

# Edit / DROID controls (MidiConfig::Transport / LengthEdit)
EDIT_BUTTON_NOTE = 38
LENGTH_EDIT_NOTE = 35
EDIT_BUTTON_DEBOUNCE_MS = 350  # firmware double-tap window before deferred short fires
DISPLAY_SETTLE_MS = 800
FADER_SELECT_SETTLE_MS = 650
NOTE_SELECTION_GRACE_MS = 750
# ControlSurfaceManager::SELECT_DEPENDENT_SETTLE_MS after dependent motor send.
SELECT_DEPENDENT_SETTLE_MS = 150
# Host quiet window after fader move (motor echo / select_apply storm).
DEPENDENT_FADER_QUIET_MS = 120
COARSE_MOVE_MIN_SETTLE_MS = 80
# DROID motor sweep fallback when serial capture is unavailable.
FADER_MOTOR_SETTLE_MS = 500
# Max wait for firmware POSITION/LENGTH EDIT log after coarse fader2 send.
COARSE_EDIT_LOG_TIMEOUT_MS = 2000
# Host pacing between edit actions when serial capture is active (stress / HITL).
EDIT_ACTION_PAUSE_MS = 500
# Firmware FEEDBACK_IGNORE_PERIOD on coarse outbound (ControlSurfaceManager).
FEEDBACK_IGNORE_PERIOD_MS = 1500
# Per-edit grace after fine CC / pitch fader (firmware "note selection disabled for 750ms").
FINE_FADER_SETTLE_MS = 800

# Canonical scoped edit-pass undo serial markers (TrackUndo post-exit global undo).
SCOPED_EDIT_PASS_UNDONE = "Scoped edit pass undone"
SCOPED_EDIT_PASS_REDONE = "Scoped edit pass redone"
