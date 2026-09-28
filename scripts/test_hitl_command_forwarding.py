#!/usr/bin/env python3
"""Host tests for the S4f.18 / response-driven HITL task-2.3 command forwarding.

One serial owner (the capture session) forwards *allowlisted*, fenced, read-only
requests; the host decodes the receipt and the response window exactly once. These
tests pin the guard rails without a device: allowlist refusal, one outstanding
request, nonce mismatch, missing fences, coalesced lines, duplicates, delayed
delivery and a stale (pre-reset) window.

Run: .venv/bin/python scripts/test_hitl_command_forwarding.py
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import capture_session  # noqa: E402

# The firmware accepts a `!TRACE_FENCE` token only when it is exactly 32 characters
# (learned from a device run where 8-character tokens were silently ignored), so every
# fixture uses tokens in that shape.
START = "a" * 32
END = "b" * 32
from hitl.managed_capture import ManagedCaptureSession  # noqa: E402
from hitl.serial.protocol import (  # noqa: E402
    collect_library_observation,
    fenced_rows,
    fenced_window,
    fence_token,
    format_command_request,
    parse_command_receipt,
)


class FakeStdin:
    def __init__(self):
        self.written = []
        self.fail = False

    def write(self, text):
        if self.fail:
            raise BrokenPipeError("closed")
        self.written.append(text)

    def flush(self):
        pass


class FakeProcess:
    def __init__(self, stdin=None, returncode=None):
        self.stdin = stdin
        self._returncode = returncode

    def poll(self):
        return self._returncode


def make_session(stdin=None, returncode=None):
    session = ManagedCaptureSession(out_dir=Path("/tmp/hitl-command-forwarding-test"))
    session._process = FakeProcess(stdin, returncode)
    return session


class RequestParsingTests(unittest.TestCase):
    def test_a_well_formed_request_is_two_fence_tokens_then_command(self):
        self.assertEqual((START, END, "!LIB_OBSERVE"),
                         capture_session.parse_command_request(f"{START} {END} !LIB_OBSERVE\n"))

    def test_fence_tokens_must_be_32_hex_like_the_firmware(self):
        self.assertTrue(capture_session.is_fence_token(START))
        self.assertFalse(capture_session.is_fence_token("abc"))
        self.assertFalse(capture_session.is_fence_token("a" * 31))
        self.assertFalse(capture_session.is_fence_token("z" * 32))

    def test_malformed_requests_are_refused(self):
        bad = ("", "abc\n", "!LIB_OBSERVE\n", f"{START} !LIB_OBSERVE\n",
               f"{START} {END}\n", f"{START} {END} LIBOBS\n", f"{START} {END} !\n",
               f"{START} {START} !LIB_OBSERVE\n", f"abc {END} !LIB_OBSERVE\n")
        for line in bad:
            self.assertIsNone(capture_session.parse_command_request(line), line)

    def test_the_allowlist_is_read_only(self):
        self.assertIn("!LIB_OBSERVE", capture_session.ALLOWED_HITL_COMMANDS)
        self.assertIn("!STOP_STATUS", capture_session.ALLOWED_HITL_COMMANDS)
        self.assertIn("!LIB_OBSERVE_STATUS", capture_session.ALLOWED_HITL_COMMANDS)
        for mutating in ("!CLEAR", "!QUARANTINE_WORKSPACE", "!REV_NUKE_SETS", "!RESTORE_WORKSPACE 1 2"):
            self.assertNotIn(mutating, capture_session.ALLOWED_HITL_COMMANDS)

    def test_request_formatting_is_one_line(self):
        self.assertEqual(f"{START} {END} !LIB_OBSERVE\n",
                         format_command_request("!LIB_OBSERVE", START, END))


class ReceiptTests(unittest.TestCase):
    def test_accepted_and_refused_receipts_are_distinct(self):
        self.assertEqual((START, "!LIB_OBSERVE", True),
                         parse_command_receipt(f"[capture] command accepted nonce={START} command=!LIB_OBSERVE"))
        self.assertEqual((START, "!CLEAR", False),
                         parse_command_receipt(f"[capture] command refused nonce={START} command=!CLEAR"))

    def test_non_receipts_and_nonce_less_lines_are_ignored(self):
        self.assertIsNone(parse_command_receipt("[capture] boot settle complete"))
        self.assertIsNone(parse_command_receipt("[capture] command accepted command=!LIB_OBSERVE"))


class FencedWindowTests(unittest.TestCase):
    def rows(self, *extra):
        return ["#CAP,1,LIBOBS,B,a=1,e=0,s=64", *extra]

    def test_a_complete_window_returns_only_the_rows_between_its_fences(self):
        lines = ["noise",
                 f"#CAP,10,TRACE_FENCE,{START}",
                 "#CAP,11,LIBOBS,S,t=0,s=0",
                 "#CAP,12,LIBOBS,S,t=1,s=0",
                 f"#CAP,13,TRACE_FENCE,{END}",
                 "noise"]
        self.assertEqual(["#CAP,11,LIBOBS,S,t=0,s=0", "#CAP,12,LIBOBS,S,t=1,s=0"],
                         fenced_rows(lines, START, END))

    def test_a_missing_end_fence_is_loss_not_an_empty_observation(self):
        self.assertIsNone(fenced_rows([f"#CAP,10,TRACE_FENCE,{START}", "#CAP,11,LIBOBS,S,t=0,s=0"],
                                      START, END))

    def test_a_missing_start_fence_is_loss(self):
        self.assertIsNone(fenced_rows([f"#CAP,13,TRACE_FENCE,{END}"], START, END))

    def test_a_stale_or_other_nonce_does_not_open_this_window(self):
        old_start, old_end = "c" * 32, "d" * 32
        lines = [f"#CAP,5,TRACE_FENCE,{old_start}", "#CAP,6,LIBOBS,S,t=9,s=0",
                 f"#CAP,7,TRACE_FENCE,{old_end}",
                 f"#CAP,8,TRACE_FENCE,{START}", "#CAP,9,LIBOBS,S,t=0,s=0",
                 f"#CAP,10,TRACE_FENCE,{END}"]
        self.assertEqual(["#CAP,9,LIBOBS,S,t=0,s=0"], fenced_rows(lines, START, END))
        self.assertIsNone(fenced_rows(lines, old_start, "e" * 32))

    def test_a_duplicate_start_fence_cannot_extend_an_earlier_window(self):
        lines = [f"#CAP,1,TRACE_FENCE,{START}", "#CAP,2,LIBOBS,S,t=0,s=0",
                 f"#CAP,3,TRACE_FENCE,{START}", "#CAP,4,LIBOBS,S,t=1,s=0",
                 f"#CAP,5,TRACE_FENCE,{END}"]
        self.assertEqual(["#CAP,4,LIBOBS,S,t=1,s=0"], fenced_rows(lines, START, END))

    def test_a_coalesced_line_never_completes_a_window(self):
        # A dropped newline merges the fence and the row: the token no longer matches,
        # so the window is reported lost rather than containing a partial observation.
        lines = [f"#CAP,1,TRACE_FENCE,{START}",
                 f"#CAP,2,LIBOBS,S,t=0,s=0#CAP,3,TRACE_FENCE,{END}"]
        self.assertIsNone(fenced_rows(lines, START, END))

    def test_a_delayed_window_is_returned_once_it_arrives(self):
        partial = [f"#CAP,1,TRACE_FENCE,{START}", "#CAP,2,LIBOBS,S,t=0,s=0"]
        self.assertIsNone(fenced_rows(partial, START, END))
        complete = partial + [f"#CAP,3,TRACE_FENCE,{END}"]
        self.assertEqual(["#CAP,2,LIBOBS,S,t=0,s=0"], fenced_rows(complete, START, END))

    def test_fence_token_reads_the_marker_payload(self):
        self.assertEqual("d" * 32, fence_token(f"#CAP,13,TRACE_FENCE,{'d' * 32}"))
        self.assertIsNone(fence_token("#CAP,13,LIBOBS,S,t=0,s=0"))


class ObservationFamilyTests(unittest.TestCase):
    def family(self, *rows):
        return [f"#CAP,{i},{row}" for i, row in enumerate(rows)]

    def test_a_family_is_complete_only_with_its_terminal_row(self):
        open_rows = self.family("LIBOBS,B,a=1,e=0,s=64", "LIBOBS,S,t=0,s=0,st=2")
        window = collect_library_observation(open_rows, 0)
        self.assertIsNotNone(window)
        self.assertFalse(window.closed)
        closed_rows = open_rows + self.family("LIBOBS,E,a=1,b=1,t=1,r=0,s=1")
        window = collect_library_observation(closed_rows, 0)
        self.assertTrue(window.closed)
        self.assertEqual(1, len(window.slots))

    def test_no_family_yet_is_none_not_an_empty_census(self):
        self.assertIsNone(collect_library_observation(["#CAP,1,DISP,0,PLAYING"], 0))

    def test_a_family_before_the_request_is_ignored(self):
        lines = self.family("LIBOBS,B,a=0,e=0,s=0", "LIBOBS,E,a=0,b=0,t=0,r=0,s=0")
        lines += self.family("LIBOBS,B,a=1,e=0,s=64", "LIBOBS,E,a=1,b=1,t=1,r=0,s=1")
        window = collect_library_observation(lines, 2)
        self.assertTrue(window.closed)
        self.assertEqual(0, len(window.slots))

    def test_fenced_window_reports_its_indices(self):
        lines = [f"#CAP,1,TRACE_FENCE,{START}", "#CAP,2,MID", f"#CAP,3,TRACE_FENCE,{END}", "#CAP,4,AFTER"]
        self.assertEqual((0, 2, ["#CAP,2,MID"]), fenced_window(lines, START, END))
        self.assertIsNone(fenced_window(lines, "c" * 32, END))


class ManagedSenderTests(unittest.TestCase):
    def test_one_outstanding_request_is_enforced_and_released(self):
        stdin = FakeStdin()
        session = make_session(stdin)
        accepted, detail = session.send_command("!LIB_OBSERVE", nonce=START, end_nonce=END)
        self.assertTrue(accepted, detail)
        self.assertEqual(f"{START} {END} !LIB_OBSERVE\n", stdin.written[0])
        self.assertEqual(START, session.pending_command_nonce)
        refused, detail = session.send_command("!LIB_OBSERVE", nonce="c" * 32, end_nonce="d" * 32)
        self.assertFalse(refused)
        self.assertEqual("one outstanding request", detail)
        self.assertEqual(1, len(stdin.written))
        session.complete_command("c" * 32)  # a different nonce must not release the slot
        self.assertEqual(START, session.pending_command_nonce)
        session.complete_command(START)
        accepted, _ = session.send_command("!LIB_OBSERVE", nonce="e" * 32, end_nonce="f" * 32)
        self.assertTrue(accepted)

    def test_no_running_session_refuses_rather_than_writing(self):
        session = make_session(FakeStdin(), returncode=1)
        accepted, detail = session.send_command("!LIB_OBSERVE", nonce=START, end_nonce=END)
        self.assertFalse(accepted)
        self.assertEqual("capture session not running", detail)
        idle = ManagedCaptureSession(out_dir=Path("/tmp/hitl-command-forwarding-test"))
        accepted, detail = idle.send_command("!LIB_OBSERVE", nonce=START, end_nonce=END)
        self.assertFalse(accepted)
        self.assertEqual("capture session not running", detail)

    def test_a_closed_pipe_refuses_and_does_not_hold_the_slot(self):
        stdin = FakeStdin()
        stdin.fail = True
        session = make_session(stdin)
        accepted, detail = session.send_command("!LIB_OBSERVE", nonce=START, end_nonce=END)
        self.assertFalse(accepted)
        self.assertIn("write failed", detail)
        self.assertIsNone(session.pending_command_nonce)

    def test_fenced_response_times_out_without_releasing_evidence(self):
        session = make_session(FakeStdin())
        session._session_log = None
        self.assertIsNone(session.fenced_response(START, END, timeout_s=0.01, poll_s=0.005))
        self.assertIsNone(session.pending_command_nonce)


if __name__ == "__main__":
    unittest.main(verbosity=2)
