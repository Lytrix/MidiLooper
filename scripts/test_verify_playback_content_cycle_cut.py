"""The playback verifier's cycle cut when the loop has no unique event.

A fully periodic loop (an ascending pattern, a CC sweep) has no signature that occurs exactly once in `effective`,
which is what the shipped cut anchors on. These tests pin the event-count fallback: it must cut the same way the
anchor does when the capture is clean, report a dropped event rather than absorb it, and decline when the capture is
shorter than one loop.
"""
from __future__ import annotations

import importlib.util
import io
import json
import pathlib
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / ".agents/skills/hitl-content-verification/scripts/verify_playback_content.py"
CHANNEL = 8
NOTE_ON, NOTE_OFF = 144, 128
PITCHES = (60, 61, 62)
REPEATS = 3


def _load_verifier():
    spec = importlib.util.spec_from_file_location("hitl_verify_playback_content", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _effective_rows():
    """A periodic loop: every signature occurs REPEATS times, so none is unique."""
    rows, tick = [], 0
    for _ in range(REPEATS):
        for pitch in PITCHES:
            rows.append((tick, NOTE_ON, CHANNEL, pitch, 98, pitch - 59))
            rows.append((tick + 24, NOTE_OFF, CHANNEL, pitch, 0, pitch - 59))
            tick += 48
    return rows


def _dump(rows, length):
    lines = [f"CAPTURE_CONTENT,begin,track=8,slot=3,length={length}",
             f"CAPTURE_CONTENT,rows,record,0,{len(rows)}",
             f"CAPTURE_CONTENT,rows,effective,0,{len(rows)}"]
    for kind in ("record", "effective"):
        lines += [f"CAPTURE_CONTENT,event,{kind},0,{','.join(str(v) for v in row)}" for row in rows]
    lines.append("CAPTURE_CONTENT,end")
    return "\n".join(lines) + "\n"


def _playback(rows, *, drop=None, prefix=0):
    """Host-side messages for the served stream, one `host_ns` apart from 1000."""
    messages = []
    for _ in range(prefix):
        messages.append(("note_on", 99, 1))
    kept = 0
    for row in rows:
        if drop is not None and kept == drop:
            kept += 1
            continue
        kept += 1
        _tick, status, _channel, pitch, velocity, _nid = row
        if status == NOTE_ON:
            messages.append(("note_on", pitch, velocity))
        else:
            messages.append(("note_off", pitch, 0))
    return messages


def _write_capture(directory: pathlib.Path, rows, *, cycles=2, drop=None, prefix=0, served_rows=None):
    source = rows if served_rows is None else served_rows
    served = _playback(source, drop=drop, prefix=prefix) * cycles
    if drop is not None:
        served = _playback(source, drop=drop) * cycles
    lines = []
    host_ns = 10_000
    for index, (kind, note, velocity) in enumerate(served):
        lines.append(json.dumps({"host_ns": host_ns + index,
                                 "message": {"type": kind, "channel": CHANNEL - 1,
                                             "note": note, "velocity": velocity}}))
    (directory / "midi-output.jsonl").write_text(("\n".join(lines) + "\n") if lines else "")
    (directory / "serial.log").write_text(_dump(rows, 768))
    (directory / "actions.jsonl").write_text(
        json.dumps({"event": "start", "host_ns": 9_999}) + "\n"
        + json.dumps({"event": "stop", "host_ns": host_ns + len(served) + 1}) + "\n")


def _run(directory: pathlib.Path) -> dict:
    verifier = _load_verifier()
    argv = sys.argv
    sys.argv = ["verify_playback_content.py", str(directory)]
    try:
        with redirect_stdout(io.StringIO()):
            verifier.main()
    finally:
        sys.argv = argv
    return json.loads((directory / "content-review.json").read_text())


class PeriodicFixtureTests(unittest.TestCase):
    def test_a_clean_periodic_capture_passes_on_two_cycles(self) -> None:
        rows = _effective_rows()
        with tempfile.TemporaryDirectory() as tmp:
            directory = pathlib.Path(tmp)
            _write_capture(directory, rows)
            result = _run(directory)
        self.assertEqual("PASS", result["result"], result)
        self.assertEqual("event_count", result["cycle_cut"])
        self.assertIsNone(result["anchor"])
        self.assertEqual(2, len(result["cycles"]))
        for cycle in result["cycles"]:
            self.assertTrue(cycle["inventory_equal"], cycle)
            self.assertTrue(cycle["exact_order"], cycle)

    def test_a_dropped_event_is_reported_not_absorbed(self) -> None:
        rows = _effective_rows()
        with tempfile.TemporaryDirectory() as tmp:
            directory = pathlib.Path(tmp)
            _write_capture(directory, rows, drop=1)
            result = _run(directory)
        self.assertEqual("FAIL", result["result"], result)
        self.assertTrue(result["cycles"])
        self.assertFalse(result["cycles"][0]["inventory_equal"], result["cycles"][0])
        self.assertGreater(result["cycles"][0]["unexplained_missing_count"], 0)

    def test_a_capture_shorter_than_one_loop_declines(self) -> None:
        rows = _effective_rows()
        with tempfile.TemporaryDirectory() as tmp:
            directory = pathlib.Path(tmp)
            _write_capture(directory, rows, cycles=0)
            result = _run(directory)
        self.assertEqual("INCONCLUSIVE_CAPTURE", result["result"], result)
        self.assertIn("fewer than one loop", result["reason"])


class TerminalBoundaryOffTests(unittest.TestCase):
    """A loop stored with a terminal `Off` at `length - 1` does not serve it, so its served period is one shorter."""

    def _with_boundary(self):
        rows = _effective_rows()
        boundary = (767, NOTE_OFF, CHANNEL, rows[-1][3], 0, 0)
        return rows, rows + [boundary]

    def test_the_unserved_boundary_off_does_not_shift_the_cut(self) -> None:
        rows, dumped = self._with_boundary()
        with tempfile.TemporaryDirectory() as tmp:
            directory = pathlib.Path(tmp)
            _write_capture(directory, dumped, served_rows=rows)
            result = _run(directory)
        self.assertEqual("PASS", result["result"], result)
        self.assertEqual("event_count", result["cycle_cut"])
        self.assertEqual(1, result["cycle_trimmed"])
        self.assertEqual(len(rows), result["cycle_period_events"])
        for cycle in result["cycles"]:
            self.assertTrue(cycle["inventory_equal"], cycle)
            self.assertTrue(cycle["exact_order"], cycle)

    def test_a_served_boundary_off_keeps_the_strict_cut(self) -> None:
        _rows, dumped = self._with_boundary()
        with tempfile.TemporaryDirectory() as tmp:
            directory = pathlib.Path(tmp)
            _write_capture(directory, dumped)
            result = _run(directory)
        self.assertEqual("PASS", result["result"], result)
        self.assertIsNone(result["cycle_trimmed"])
        self.assertEqual(len(dumped), result["cycle_period_events"])

    def test_a_dropped_event_is_not_rescued_by_the_trim(self) -> None:
        rows, dumped = self._with_boundary()
        with tempfile.TemporaryDirectory() as tmp:
            directory = pathlib.Path(tmp)
            _write_capture(directory, dumped, served_rows=rows, drop=1)
            result = _run(directory)
        self.assertEqual("FAIL", result["result"], result)
        self.assertGreater(result["cycles"][0]["unexplained_missing_count"], 0)


class PhaseAlignmentTests(unittest.TestCase):
    def test_the_cut_starts_at_the_loops_own_first_event(self) -> None:
        verifier = _load_verifier()
        rows = _effective_rows()
        served = _playback(rows, prefix=2)           # two stray events before the loop starts
        expected = __import__("collections").Counter(
            (row[1], row[2], row[3], 0 if row[1] == NOTE_OFF else row[4]) for row in rows)
        actual = []
        for kind, note, velocity in served:
            actual.append((NOTE_ON if kind == "note_on" else NOTE_OFF, CHANNEL, note,
                           velocity if kind == "note_on" else 0))
        first_event = (NOTE_ON, CHANNEL, rows[0][3], rows[0][4])
        windows, offset, difference = verifier.event_count_windows(
            actual, expected, len(rows), first_event)
        self.assertEqual(2, offset)
        self.assertEqual(0, difference)
        self.assertEqual((2, 2 + len(rows)), windows[0])


if __name__ == "__main__":
    unittest.main()
