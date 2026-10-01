#!/usr/bin/env python3
"""S4f.14's continuation leg on the S4b.8 switch runner: opt-in, appended, and never raised.

The wire is read from the port the runner is handed -- one entry per `send`, in order, with the real send
timestamp -- so the assertions below name the actual (type, note, channel) sequence rather than a paraphrase
of it. With `--continue-on-switched-track` off the bytes are the switch presses and nothing else; with it on,
two `RECORD_BUTTON_NOTE` (36) presses on `CONTROL_CHANNEL_1BASED` follow the track+slot selects, separated by
`--continue-hold-ms`. The optional note burst (`--continue-midi-channel`, defaulting in the CLI to the switch
track's own channel) is content of the destination session's own: it plays inside that hold, between the two
Record/Overdub presses, and is truncated -- never the hold extended -- when the hold cannot fit it. The runner
owns only *what it pressed and played*: S4f.14's discriminators (which slot's finalization retired, which
destination the continuation's grain went to) are device-row facts and are deliberately not asserted here.
"""
from __future__ import annotations

import argparse
import sys
import time
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import hitl_s4b8_slot_switch_during_finalization as runner  # noqa: E402
from hitl.control_constants import (  # noqa: E402
    CONTROL_CHANNEL_1BASED,
    LOOP_SELECT_NOTE_BASE,
    RECORD_BUTTON_NOTE,
    TRACK_SELECT_NOTE_BASE,
)

CONTROL_CHANNEL = CONTROL_CHANNEL_1BASED - 1
TRACK_INDEX = 4                                  # `--switch-track-number 5` -> `TRACK_SELECT_NOTE_BASE + 4`
TRACK_NOTE = TRACK_SELECT_NOTE_BASE + TRACK_INDEX
DESTINATION_SLOT = 3                             # `--switch-loop-slot 3` -> `LOOP_SELECT_NOTE_BASE + 2`
DESTINATION_NOTE = LOOP_SELECT_NOTE_BASE + DESTINATION_SLOT - 1
PRESS_MS = 120                                   # the runner's `press_ms`, i.e. the hold of each short press
BURST_CHANNEL_1BASED = 5                         # the destination track's own channel (track N -> channel N)
BURST_CHANNEL = BURST_CHANNEL_1BASED - 1
BURST_FIRST_NOTE = 60
BURST_NOTES = 8
BURST_SPACING_MS = 150.0


def wire(message) -> tuple:
    """The three fields every assertion here is about: the message kind, the note, and the channel."""
    return (message.type, message.note, message.channel)


class RecordingOutput:
    """The runner's only output port: it records what went out, and can refuse a note on demand."""

    def __init__(self, *, fail_note=None, fail_after=0):
        self.messages: list[object] = []
        self.at: list[float] = []
        self._fail_note = fail_note
        self._fail_after = fail_after
        self._note_ons = 0

    def send(self, message) -> None:
        if (self._fail_note is not None and message.type == "note_on"
                and message.note == self._fail_note and message.velocity > 0):
            self._note_ons += 1
            if self._note_ons > self._fail_after:
                raise RuntimeError(f"fake port refused note {message.note}")
        self.at.append(time.monotonic())
        self.messages.append(message)

    @property
    def sequence(self) -> list[tuple]:
        return [wire(m) for m in self.messages]

    def note_ons(self, note: int) -> list[float]:
        return [t for t, m in zip(self.at, self.messages) if m.type == "note_on" and m.note == note]


class RecordingClock:
    """A `time` stand-in for the runner module: every requested sleep is recorded, optionally real."""

    def __init__(self, *, real: bool = False):
        self.sleeps: list[float] = []
        self.real = real

    def sleep(self, seconds: float) -> None:
        self.sleeps.append(seconds)
        if self.real:
            time.sleep(seconds)

    def monotonic_ns(self) -> int:
        return time.monotonic_ns()

    def monotonic(self) -> float:
        return time.monotonic()

    def time(self) -> float:
        return time.time()


class ContinuationTests(unittest.TestCase):
    def _interrupt(self, *, real_clock=False, fail_note=None, fail_after=0, **kw):
        """Fire the intervention once and return (port, output, events, clock).

        `_interrupt` runs under a recording clock; that it returns at all is the "never raised" half of the
        error case, and the recorded sleeps make the spacing assertions independent of wall-clock jitter.
        """
        output = RecordingOutput(fail_note=fail_note, fail_after=fail_after)
        events: list[dict] = []
        clock = RecordingClock(real=real_clock)
        options = dict(delay_s=0.0, channel=3, target_track_index=TRACK_INDEX,
                       target_slots=[DESTINATION_SLOT])
        options.update(kw)
        port = runner.SlotSwitchInterventionPort(output, events.append, **options)
        with mock.patch.object(runner, "time", clock):
            port._interrupt()
        return port, output, events, clock

    @staticmethod
    def _burst(output) -> list[tuple]:
        """The burst's (kind, note) pairs in wire order, read by the channel it plays on."""
        return [(m.type, m.note) for m in output.messages if m.channel == BURST_CHANNEL]

    @staticmethod
    def _burst_note_ons(output) -> list[int]:
        return [note for kind, note in ContinuationTests._burst(output) if kind == "note_on"]

    @staticmethod
    def _only_burst_event(events) -> dict:
        bursts = [e for e in events if e["event"] == "continue_note_burst_sent"]
        if len(bursts) != 1:  # one event per burst, never one per note
            raise AssertionError(f"expected exactly one continue_note_burst_sent, got {bursts}")
        # `event` is the name this helper selected by; everything else is the event's own fields.
        return {key: value for key, value in bursts[0].items() if key not in {"host_ns", "event"}}

    @staticmethod
    def _record_press_gap(output) -> float:
        """Seconds between the continuation's start press and its stop press, as sent."""
        first, second = output.note_ons(RECORD_BUTTON_NOTE)
        return second - first


    def test_the_flag_off_gesture_is_the_switch_presses_and_nothing_more(self):
        """The S4b.8/S4b.9 legs' wire bytes must not change: no record press, no extra sleep."""
        _port, output, events, clock = self._interrupt(phase_wait_ms=500)
        self.assertEqual([("note_on", TRACK_NOTE, CONTROL_CHANNEL), ("note_off", TRACK_NOTE, CONTROL_CHANNEL),
                          ("note_on", DESTINATION_NOTE, CONTROL_CHANNEL),
                          ("note_off", DESTINATION_NOTE, CONTROL_CHANNEL)], output.sequence)
        self.assertEqual(["switch_track_select_sent", "switch_slot_select_sent"],
                         [e["event"] for e in events])
        # The only sleep in the gesture is the track -> slot phase wait; nothing was appended after the slot.
        self.assertEqual([0.5], clock.sleeps)
        self.assertNotIn(RECORD_BUTTON_NOTE, [note for _kind, note, _channel in output.sequence])

    def test_the_flag_off_slot_only_gesture_stays_a_single_press(self):
        _port, output, events, clock = self._interrupt(target_track_index=None)
        self.assertEqual([("note_on", DESTINATION_NOTE, CONTROL_CHANNEL),
                          ("note_off", DESTINATION_NOTE, CONTROL_CHANNEL)], output.sequence)
        self.assertEqual(["switch_slot_select_sent"], [e["event"] for e in events])
        self.assertEqual([], clock.sleeps)  # no track press, so no phase wait either

    def test_the_continuation_starts_then_stops_after_the_selects(self):
        port, output, events, clock = self._interrupt(continue_on_switched_track=True, continue_hold_ms=750)
        self.assertEqual([("note_on", TRACK_NOTE, CONTROL_CHANNEL), ("note_off", TRACK_NOTE, CONTROL_CHANNEL),
                          ("note_on", DESTINATION_NOTE, CONTROL_CHANNEL),
                          ("note_off", DESTINATION_NOTE, CONTROL_CHANNEL),
                          ("note_on", RECORD_BUTTON_NOTE, CONTROL_CHANNEL),
                          ("note_off", RECORD_BUTTON_NOTE, CONTROL_CHANNEL),
                          ("note_on", RECORD_BUTTON_NOTE, CONTROL_CHANNEL),
                          ("note_off", RECORD_BUTTON_NOTE, CONTROL_CHANNEL)], output.sequence)
        self.assertEqual(["switch_track_select_sent", "switch_slot_select_sent",
                          "continue_session_start_sent", "continue_session_stop_sent"],
                         [e["event"] for e in events])
        # phase wait before the continuation, then the hold between its start and its stop press.
        self.assertEqual([0.5, 0.5, 0.75], clock.sleeps)
        # No channel was named, so there is no burst and no burst event: the continuation is the two presses.
        self.assertEqual([], self._burst(output))
        self.assertTrue(port.sent)
        self.assertIsNone(port.error)
        self.assertNotIn("continue_note_burst_sent", [e["event"] for e in events])

    def test_the_hold_is_really_waited_between_the_two_record_presses(self):
        """A recorded number is not a wait: with real sleeps the gap between the two presses is the hold."""
        hold_ms = 250
        _port, output, _events, clock = self._interrupt(
            real_clock=True, phase_wait_ms=50, continue_on_switched_track=True, continue_hold_ms=hold_ms)
        self.assertEqual([0.05, 0.05, hold_ms / 1000.0], clock.sleeps)
        first, second = output.note_ons(RECORD_BUTTON_NOTE)
        # The gap is the hold plus the short press that precedes it (`press_ms` is real in `_send_short_press`).
        self.assertGreaterEqual(second - first, hold_ms / 1000.0)
        self.assertLessEqual(second - first, hold_ms / 1000.0 + (PRESS_MS + 250) / 1000.0)


    def test_the_flag_off_gesture_sends_no_burst_even_with_a_channel_named(self):
        """The burst hangs off the continuation, so naming a channel alone changes nothing."""
        _port, output, events, clock = self._interrupt(continue_midi_channel=BURST_CHANNEL_1BASED)
        self.assertEqual([("note_on", TRACK_NOTE, CONTROL_CHANNEL), ("note_off", TRACK_NOTE, CONTROL_CHANNEL),
                          ("note_on", DESTINATION_NOTE, CONTROL_CHANNEL),
                          ("note_off", DESTINATION_NOTE, CONTROL_CHANNEL)], output.sequence)
        self.assertEqual(["switch_track_select_sent", "switch_slot_select_sent"], [e["event"] for e in events])
        self.assertEqual([0.5], clock.sleeps)

    def test_the_burst_plays_inside_the_hold_between_the_two_record_presses(self):
        _port, output, events, _clock = self._interrupt(
            continue_on_switched_track=True, continue_hold_ms=4000,
            continue_midi_channel=BURST_CHANNEL_1BASED, continue_note_spacing_ms=BURST_SPACING_MS)
        burst_pairs = [pair for note in range(BURST_FIRST_NOTE, BURST_FIRST_NOTE + BURST_NOTES)
                       for pair in (("note_on", note), ("note_off", note))]
        self.assertEqual([("note_on", TRACK_NOTE, CONTROL_CHANNEL), ("note_off", TRACK_NOTE, CONTROL_CHANNEL),
                          ("note_on", DESTINATION_NOTE, CONTROL_CHANNEL),
                          ("note_off", DESTINATION_NOTE, CONTROL_CHANNEL),
                          ("note_on", RECORD_BUTTON_NOTE, CONTROL_CHANNEL),
                          ("note_off", RECORD_BUTTON_NOTE, CONTROL_CHANNEL)]
                         + [(kind, note, BURST_CHANNEL) for kind, note in burst_pairs]
                         + [("note_on", RECORD_BUTTON_NOTE, CONTROL_CHANNEL),
                            ("note_off", RECORD_BUTTON_NOTE, CONTROL_CHANNEL)], output.sequence)
        # Strictly inside the two Record/Overdub presses: the burst is the session's content, not the gesture.
        record_presses = [index for index, (_kind, note, channel) in enumerate(output.sequence)
                          if note == RECORD_BUTTON_NOTE and channel == CONTROL_CHANNEL and _kind == "note_on"]
        burst = [index for index, (_kind, _note, channel) in enumerate(output.sequence)
                 if channel == BURST_CHANNEL]
        self.assertEqual(2, len(record_presses))
        self.assertLess(record_presses[0], burst[0])
        self.assertGreater(record_presses[1], burst[-1])
        # Ascending from 60, one short pair per note, at the baseline's own content velocity.
        self.assertEqual(list(range(BURST_FIRST_NOTE, BURST_FIRST_NOTE + BURST_NOTES)),
                         self._burst_note_ons(output))
        self.assertEqual([98, 0] * BURST_NOTES,
                         [m.velocity for m in output.messages if m.channel == BURST_CHANNEL])
        self.assertEqual({"channel_1based": BURST_CHANNEL_1BASED, "notes_sent": BURST_NOTES,
                          "requested_notes": BURST_NOTES, "first_note": BURST_FIRST_NOTE,
                          "last_note": BURST_FIRST_NOTE + BURST_NOTES - 1,
                          "spacing_ms": BURST_SPACING_MS, "truncated": False},
                         self._only_burst_event(events))
        self.assertEqual(["switch_track_select_sent", "switch_slot_select_sent", "continue_session_start_sent",
                          "continue_note_burst_sent", "continue_session_stop_sent"],
                         [e["event"] for e in events])

    def test_a_short_hold_truncates_the_burst_instead_of_moving_the_stop_press(self):
        hold_ms = 600
        port, output, events, _clock = self._interrupt(
            real_clock=True, phase_wait_ms=50, continue_on_switched_track=True, continue_hold_ms=hold_ms,
            continue_midi_channel=BURST_CHANNEL_1BASED, continue_note_spacing_ms=BURST_SPACING_MS)
        # Four notes fit `notes * press_ms + (notes - 1) * (spacing - press_ms)` into a 600 ms hold.
        self.assertEqual(4, port._continue_burst_plan())
        self.assertEqual([60, 61, 62, 63], self._burst_note_ons(output))
        burst = self._only_burst_event(events)
        self.assertEqual(4, burst["notes_sent"])
        self.assertEqual(60, burst["first_note"])
        self.assertEqual(63, burst["last_note"])
        self.assertTrue(burst["truncated"])
        # The stop press keeps its hold distance from the start press: 120 ms start-press gate + the 600 ms hold.
        gap = self._record_press_gap(output)
        self.assertGreaterEqual(gap, (PRESS_MS + hold_ms) / 1000.0)
        self.assertLessEqual(gap, (PRESS_MS + hold_ms + 250) / 1000.0)

    def test_a_hold_too_short_for_one_note_sends_no_notes_and_still_stops_on_time(self):
        hold_ms = 100
        port, output, events, _clock = self._interrupt(
            real_clock=True, phase_wait_ms=50, continue_on_switched_track=True, continue_hold_ms=hold_ms,
            continue_midi_channel=BURST_CHANNEL_1BASED, continue_note_spacing_ms=BURST_SPACING_MS)
        self.assertEqual(0, port._continue_burst_plan())
        self.assertEqual([], self._burst(output))
        self.assertEqual([("note_on", RECORD_BUTTON_NOTE, CONTROL_CHANNEL),
                          ("note_off", RECORD_BUTTON_NOTE, CONTROL_CHANNEL),
                          ("note_on", RECORD_BUTTON_NOTE, CONTROL_CHANNEL),
                          ("note_off", RECORD_BUTTON_NOTE, CONTROL_CHANNEL)], output.sequence[4:])
        # Requested but nothing fitted: absence is named, not passed off as a sent note.
        self.assertEqual({"channel_1based": BURST_CHANNEL_1BASED, "notes_sent": 0, "requested_notes": BURST_NOTES,
                          "first_note": None, "last_note": None, "spacing_ms": BURST_SPACING_MS,
                          "truncated": True}, self._only_burst_event(events))
        gap = self._record_press_gap(output)
        self.assertGreaterEqual(gap, (PRESS_MS + hold_ms) / 1000.0)
        self.assertLessEqual(gap, (PRESS_MS + hold_ms + 250) / 1000.0)

    def test_a_burst_channel_without_a_switch_track_resolves_to_no_burst(self):
        """The same-track switch has no destination track to play on, so the channel stays absent there."""
        config = argparse.Namespace(continue_midi_channel=None, switch_track_number=None)
        self.assertIsNone(runner.resolve_continue_midi_channel(config))
        _port, output, events, _clock = self._interrupt(
            continue_on_switched_track=True, continue_hold_ms=750,
            continue_midi_channel=runner.resolve_continue_midi_channel(config))
        self.assertEqual([], self._burst(output))
        self.assertEqual(["switch_track_select_sent", "switch_slot_select_sent", "continue_session_start_sent",
                          "continue_session_stop_sent"], [e["event"] for e in events])
    def test_no_switch_press_means_no_continuation(self):
        """Nothing selected the destination, so a record press would land on an arbitrary track."""
        port, output, events, _clock = self._interrupt(continue_on_switched_track=True,
                                                       target_track_index=None, target_slots=[])
        self.assertEqual([], output.messages)
        self.assertEqual([], events)
        self.assertEqual(0, port.switch_presses_sent)
        # `sent` keeps its existing meaning ("the intervention ran without error"), which is exactly why the
        # continuation is gated on the press count instead: nothing here selected a destination.
        self.assertTrue(port.sent)

    def test_a_refusing_port_records_the_error_on_either_press_and_never_raises(self):
        for fail_after, expected in ((0, "start"), (1, "stop")):
            with self.subTest(failing_press=expected):
                port, _output, events, _clock = self._interrupt(
                    continue_on_switched_track=True, continue_hold_ms=750,
                    fail_note=RECORD_BUTTON_NOTE, fail_after=fail_after)
                names = [e["event"] for e in events]
                self.assertEqual("continue_session_error", names[-1])
                self.assertEqual(port.error, events[-1]["detail"])
                self.assertIsNotNone(port.error)
                self.assertIn("refused note 36", port.error)
                # The switch half stays its own verdict: the continuation failure is not relabelled.
                self.assertNotIn("switch_intervention_error", names)
                self.assertTrue(port.sent)
                self.assertNotIn("continue_session_stop_sent", names)
                self.assertEqual(fail_after == 1, "continue_session_start_sent" in names)


class ChannelDefaultTests(unittest.TestCase):
    """`--continue-midi-channel` omitted: the switch track's own channel is the burst's channel."""

    def test_the_switch_tracks_own_channel_is_the_default(self):
        config = argparse.Namespace(continue_midi_channel=None, switch_track_number=5)
        self.assertEqual(5, runner.resolve_continue_midi_channel(config))

    def test_an_explicit_channel_wins_over_the_switch_track(self):
        config = argparse.Namespace(continue_midi_channel=2, switch_track_number=5)
        self.assertEqual(2, runner.resolve_continue_midi_channel(config))

    def test_a_same_track_switch_resolves_to_no_channel(self):
        config = argparse.Namespace(continue_midi_channel=None, switch_track_number=None)
        self.assertIsNone(runner.resolve_continue_midi_channel(config))


if __name__ == "__main__":
    unittest.main()
