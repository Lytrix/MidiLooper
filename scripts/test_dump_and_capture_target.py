"""The content pipeline's target flags: a named slot is read without a record-toggle gesture.

`!CAPTURE_CONTENT` dumps the selected track's *active* slot, which is fine for the scratch slot the skill
creates content on. A demoted windowed target is not the active slot, so the dump has to name it -- and it must
not be "selected" by pressing its loop button, whose short press is a record toggle on a populated slot. These
tests pin the two halves of that: the request text, and the gesture that moves only the track.
"""
from __future__ import annotations

import importlib.util
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / ".agents/skills/hitl-content-verification/scripts/dump_and_capture.py"
sys.path.insert(0, str(ROOT / "scripts"))

from hitl.actions import slots  # noqa: E402


def _load_recipe():
    spec = importlib.util.spec_from_file_location("hitl_dump_and_capture", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class _FakePort:
    def __init__(self) -> None:
        self.sent: list[object] = []

    def send(self, message) -> None:
        self.sent.append(message)


class DumpCommandTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.recipe = _load_recipe()

    def test_default_dumps_the_active_slot_with_the_unnamed_request(self) -> None:
        self.assertEqual(b"!CAPTURE_CONTENT\n", self.recipe.dump_command(None))

    def test_a_named_slot_is_sent_as_the_firmware_index(self) -> None:
        # The CLI and the action layer are 1-based; the firmware's dump command is 0-based.
        self.assertEqual(b"!CAPTURE_CONTENT_SLOT 3\n", self.recipe.dump_command(4))
        self.assertEqual(b"!CAPTURE_CONTENT_SLOT 0\n", self.recipe.dump_command(1))


class TrackSelectTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.recipe = _load_recipe()

    def test_selecting_a_track_sends_its_canonical_note_on_the_control_channel(self) -> None:
        port = _FakePort()
        slots.press_track(self.recipe._Ctx(port), 8, press_ms=1)
        sent = [(m.type, m.channel, m.note) for m in port.sent]
        self.assertEqual([("note_on", 15, 67), ("note_off", 15, 67)], sent)

    def test_the_selection_never_presses_a_loop_button(self) -> None:
        # A loop button's short press is a record toggle; the demoted target must be read, never re-armed.
        port = _FakePort()
        slots.press_track(self.recipe._Ctx(port), 8, press_ms=1)
        loop_notes = {50 + i for i in range(8)}
        self.assertFalse(loop_notes & {m.note for m in port.sent})


if __name__ == "__main__":
    unittest.main()
