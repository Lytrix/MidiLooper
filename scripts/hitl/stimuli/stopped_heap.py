"""Temporary read-only diagnostic stimulus, run by hitl_boot_census.py.

No notes, start/continue, capture, slot selection or SD commands.
"""
STIMULUS_ID = "STIM-HEAP-STATS-TEMP-01"
COMMANDS = ("!LIB_OBSERVE", "!HEAP_STATS")
CLOCK_REFUSAL = "24 MIDI clock messages at 20 ms spacing; request after first 8; no transport start"
