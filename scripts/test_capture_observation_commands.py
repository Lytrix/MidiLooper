"""Safety and wire contract for scoped read-only capture observations."""
import io
import unittest
from unittest.mock import patch
from scripts import capture_session as capture

START = "a" * 32
END = "b" * 32


class SerialSink:
    is_open = True

    def __init__(self):
        self.writes = []
        self.flushes = 0

    def write(self, data):
        self.writes.append(data)

    def flush(self):
        self.flushes += 1


class ObservationCommandsTest(unittest.TestCase):
    def service(self, request):
        sink = SerialSink()
        stream = io.StringIO(request)
        with patch.object(capture.sys, "stdin", stream), patch.object(
            capture.select, "select", return_value=([stream], [], [])
        ), patch("sys.stdout", io.StringIO()):
            capture._service_command_requests(sink, enabled=True)
        return sink

    def test_valid_scoped_parse_and_wire(self):
        for command in ("!SLOT_BYTES 7 3 0", "!SLOT_BYTES 0 7 4294967295", "!CAPTURE_CONTENT_SLOT 3"):
            with self.subTest(command=command):
                request = f"{START} {END} {command}\n"
                self.assertEqual(capture.parse_command_request(request), (START, END, command))
                sink = self.service(request)
                self.assertEqual(sink.writes, [f"!TRACE_FENCE {START}\n".encode(), f"{command}\n".encode(), f"!TRACE_FENCE {END}\n".encode()])
                self.assertEqual(sink.flushes, 1)

    def test_existing_argument_free_probes(self):
        for command in capture.ALLOWED_HITL_COMMANDS:
            with self.subTest(command=command):
                self.assertEqual(len(self.service(f"{START} {END} {command}\n").writes), 3)

    def test_refused_commands_never_write(self):
        commands = ("!REBOOT", "!QUARANTINE_WORKSPACE", "!UNKNOWN", "!STOP_STATUS 0",
                    "!SLOT_BYTES", "!SLOT_BYTES 7 3", "!SLOT_BYTES 8 3 0",
                    "!SLOT_BYTES 7 8 0", "!SLOT_BYTES -1 3 0", "!SLOT_BYTES 7 3 -1",
                    "!SLOT_BYTES 7 3 4294967296", "!SLOT_BYTES 7 3 +1",
                    "!SLOT_BYTES 7 3 1x", "!SLOT_BYTES ７ 3 0", "!SLOT_BYTES 7 3 0 1",
                    "!CAPTURE_CONTENT_SLOT", "!CAPTURE_CONTENT_SLOT 8", "!CAPTURE_CONTENT_SLOT 3 0",
                    "!CAPTURE_CONTENT_SLOT 3\x00", "!STOP_STATUS\r!REBOOT",
                    "!STOP_STATUS\t")
        for command in commands:
            with self.subTest(command=repr(command)):
                self.assertEqual(self.service(f"{START} {END} {command}\n").writes, [])

    def test_invalid_fences_never_write(self):
        for start, end in ((START, START), ("a" * 31, END), ("A" * 32, END), (START, "z" * 32)):
            self.assertIsNone(capture.parse_command_request(f"{start} {end} !SLOT_BYTES 7 3 0\n"))
            self.assertEqual(self.service(f"{start} {end} !SLOT_BYTES 7 3 0\n").writes, [])

    def test_embedded_newline_rejected_by_parser(self):
        self.assertIsNone(capture.parse_command_request(f"{START} {END} !STOP_STATUS\n!REBOOT\n"))


if __name__ == "__main__":
    unittest.main()
