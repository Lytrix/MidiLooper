#!/usr/bin/env python3
"""Host tests for `hitl.device_shutdown` -- the canonical stop/drain protocol and its latch.

The two decisive fixtures are the **real logs from the 2026-09-26 workspace-load failure**, preserved under
`openspec/changes/runtime-working-set-ownership/evidence/2026-09-26-workspace-loss/`:

* `clear-run-session.log` -- the run that ended with a workspace save in flight. The existing idle owner
  reads it as **active / not idle**, so a verified drain must never report it complete. This is the regression.
* `retention-run-session.log` -- a run that finished properly. The same owner reads it as idle, so the check
  must not false-positive on a good finish.

Using the incident's own bytes rather than a synthetic marker is deliberate: a hand-made "busy" line would
only prove the test agrees with itself.

Run: .venv/bin/python scripts/test_device_shutdown.py
"""
import ast
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

from hitl.device_shutdown import (  # noqa: E402
    COMPLETE,
    INCOMPLETE_DISCONNECTED,
    INCOMPLETE_TIMEOUT,
    NOT_ATTEMPTED,
    DeviceSession,
    clear_latch,
    latch_reason,
    mark_incomplete,
    refuse_reset_if_latched,
    shutdown_device,
)

_REPO = Path(__file__).resolve().parent.parent
_EV = _REPO / "openspec/changes/runtime-working-set-ownership/evidence/2026-09-26-workspace-loss"
INCIDENT_LOG = _EV / "clear-run-session.log"
CLEAN_LOG = _EV / "retention-run-session.log"
DRIVER = _REPO / "scripts/hitl_retention_handoff.py"


def _noop() -> None:
    pass


class ShutdownProtocolTests(unittest.TestCase):
    def setUp(self):
        for p in (INCIDENT_LOG, CLEAN_LOG):
            if not p.exists():
                self.skipTest(f"preserved evidence missing: {p}")

    def test_the_incident_log_is_never_reported_complete(self):
        """The regression: a log that ends mid-save must not pass a verified drain."""
        result = shutdown_device(capture_log=INCIDENT_LOG, send_transport_stop=_noop,
                                 drain_timeout_s=0.2, quiet_s=0.0)
        self.assertEqual(INCOMPLETE_TIMEOUT, result.status)
        self.assertFalse(result.ok)
        self.assertIn("may still be writing", result.detail)

    def test_a_clean_finish_is_reported_complete(self):
        result = shutdown_device(capture_log=CLEAN_LOG, send_transport_stop=_noop,
                                 drain_timeout_s=5.0, quiet_s=0.0)
        self.assertEqual(COMPLETE, result.status, result.detail)
        self.assertTrue(result.ok)
        self.assertEqual(2, result.drains, "the canonical protocol is two stop+drain cycles")

    def test_a_press_that_raises_is_a_disconnection_not_a_pass(self):
        def boom():
            raise OSError("serial port closed")

        result = shutdown_device(capture_log=CLEAN_LOG, send_transport_stop=boom,
                                 drain_timeout_s=5.0, quiet_s=0.0)
        self.assertEqual(INCOMPLETE_DISCONNECTED, result.status)
        self.assertFalse(result.ok)
        self.assertIn("transport stop press failed", result.detail)

    def test_a_reported_disconnection_is_incomplete(self):
        result = shutdown_device(capture_log=CLEAN_LOG, send_transport_stop=_noop,
                                 drain_timeout_s=5.0, quiet_s=0.0, is_connected=lambda: False)
        self.assertEqual(INCOMPLETE_DISCONNECTED, result.status)

    def test_a_fixed_sleep_could_not_distinguish_these_two_logs(self):
        """Both logs are the same size class and both are 'quiet' by wall clock; only the verified
        predicate separates them, which is why a sleep is insufficient."""
        result_clean = shutdown_device(capture_log=CLEAN_LOG, send_transport_stop=_noop,
                                       drain_timeout_s=0.2, quiet_s=0.0)
        result_incident = shutdown_device(capture_log=INCIDENT_LOG, send_transport_stop=_noop,
                                          drain_timeout_s=0.2, quiet_s=0.0)
        self.assertNotEqual(result_clean.status, result_incident.status)


class LatchTests(unittest.TestCase):
    def setUp(self):
        self.latch = Path(self._testMethodName + ".latch.json")
        self.latch.unlink(missing_ok=True)
        self.addCleanup(lambda: self.latch.unlink(missing_ok=True))

    def test_an_incomplete_shutdown_is_latched_and_refuses_a_reset(self):
        mark_incomplete(type("R", (), {"as_dict": lambda self_: {"status": INCOMPLETE_TIMEOUT,
                                                                 "drains": 0,
                                                                 "detail": "save in flight"}})(),
                        context="unit", latch_path=self.latch)
        self.assertIn("incomplete_timeout", latch_reason(self.latch))
        refusal = refuse_reset_if_latched(tool="test-tool", latch_path=self.latch)
        self.assertIsNotNone(refusal, "an incomplete shutdown must block the follow-up reset")
        self.assertIn("refusing to upload/reset", refusal)
        self.assertIn("2026-09-26", refusal)

    def test_clearing_the_latch_allows_a_reset(self):
        mark_incomplete(type("R", (), {"as_dict": lambda self_: {"status": INCOMPLETE_TIMEOUT,
                                                                 "drains": 0,
                                                                 "detail": "save in flight"}})(),
                        context="unit", latch_path=self.latch)
        clear_latch(self.latch)
        self.assertIsNone(latch_reason(self.latch))
        self.assertIsNone(refuse_reset_if_latched(tool="test-tool", latch_path=self.latch))

    def test_no_latch_means_no_refusal(self):
        self.assertIsNone(refuse_reset_if_latched(tool="test-tool", latch_path=self.latch))


class EveryExitDrainsTests(unittest.TestCase):
    """`DeviceSession` must drain on normal return, early return, and exception alike.

    This is the structural guarantee: a per-exit call can be forgotten when a new return is added, a
    `__exit__` cannot.
    """

    def setUp(self):
        if not CLEAN_LOG.exists():
            self.skipTest("preserved evidence missing")
        self.latch = Path(self._testMethodName + ".latch.json")
        self.latch.unlink(missing_ok=True)
        self.addCleanup(lambda: self.latch.unlink(missing_ok=True))
        self.events = []
        self.drained = []

    def _session(self, *, log=CLEAN_LOG, timeout=5.0):
        def stop():
            self.drained.append(True)

        return DeviceSession(capture_log_getter=lambda: log, send_transport_stop=stop,
                             out_dir=None, events=self.events, drain_timeout_s=timeout,
                             latch_path=self.latch)

    def test_normal_exit_drains(self):
        with self._session() as session:
            pass
        self.assertEqual(COMPLETE, session.result.status)
        self.assertEqual(2, len(self.drained), "the canonical protocol is two stop+drain cycles")

    def test_early_return_drains(self):
        def body():
            with self._session() as session:
                return session  # an early return, exactly like the --prepare-only path
        session = body()
        self.assertEqual(COMPLETE, session.result.status)
        self.assertEqual(2, len(self.drained), "an early return must still drain")

    def test_an_exception_still_drains_and_propagates(self):
        with self.assertRaises(RuntimeError):
            with self._session():
                raise RuntimeError("mid-body failure")
        self.assertEqual(2, len(self.drained), "an exception must still drain")
        self.assertFalse(self.latch.exists(), "a completed drain must not latch")

    def test_an_incomplete_exit_latches_and_is_recorded(self):
        with self._session(log=INCIDENT_LOG, timeout=0.2):
            pass
        self.assertTrue(self.latch.exists(), "an incomplete shutdown must latch")
        self.assertTrue(any(e.get("event") == "device_shutdown" for e in self.events))
        self.assertIn("incomplete_timeout", latch_reason(self.latch))

    def test_no_session_log_is_not_reported_as_an_incomplete_shutdown(self):
        with self._session(log=None) as session:
            pass
        self.assertEqual(NOT_ATTEMPTED, session.result.status)
        self.assertFalse(self.latch.exists())


class ResetGateTests(unittest.TestCase):
    """The consumers of the latch must actually honour it, not just define it.

    `hitl_boot_census` is the tool whose reset landed on top of the interrupted save on 2026-09-26, so its
    refusal is asserted directly: with an incomplete shutdown latched, `main` must return 4 **without**
    invoking the upload at all.
    """

    def test_the_census_refuses_to_reset_when_a_shutdown_was_incomplete(self):
        import hitl_boot_census as census

        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp) / "census-out"
            with mock.patch("hitl.device_shutdown.refuse_reset_if_latched",
                            return_value="refusing to upload/reset the device (test)") as gate, \
                 mock.patch.object(census.subprocess, "run",
                                   side_effect=AssertionError("the census must not reset when latched")), \
                 mock.patch.object(sys, "argv", ["hitl_boot_census.py", "--out-dir", str(out_dir)]):
                rc = census.main()
        self.assertEqual(4, rc)
        gate.assert_called_once()


class DriverStructureTests(unittest.TestCase):
    """Every `return` after device interaction in the driver must sit inside a `DeviceSession`.

    Checked structurally rather than by enumerating exits, so a *new* early return added later cannot quietly
    reintroduce the 2026-09-26 defect.
    """

    def test_every_return_inside_the_capture_session_is_inside_a_device_session(self):
        tree = ast.parse(DRIVER.read_text())
        offenders = []

        # NOTE: the visitor must traverse function bodies. An earlier cut returned early from
        # `visit_FunctionDef`, so `main`'s body was never examined and the test passed even against a planted
        # bypassing `return` -- a false green caught by red-proving it.
        class Visitor(ast.NodeVisitor):
            def __init__(self):
                self.capture_depth = 0
                self.device_depth = 0

            def _is_with(self, node, needle):
                for item in node.items:
                    ctx = item.context_expr
                    name = ast.unparse(ctx)
                    if needle in name:
                        return True
                return False

            def visit_With(self, node):
                capture = self._is_with(node, "ManagedCaptureSession")
                device = self._is_with(node, "DeviceSession")
                self.capture_depth += 1 if capture else 0
                self.device_depth += 1 if device else 0
                for child in node.body:
                    self.visit(child)
                self.capture_depth -= 1 if capture else 0
                self.device_depth -= 1 if device else 0

            def visit_Return(self, node):
                if self.capture_depth > 0 and self.device_depth == 0:
                    offenders.append(node.lineno)

        Visitor().visit(tree)
        self.assertEqual([], offenders,
                         f"driver returns outside a DeviceSession: lines {offenders}")
        self.assertIn("DeviceSession", DRIVER.read_text(),
                      "the driver must use the shared shutdown owner")


if __name__ == "__main__":
    unittest.main(verbosity=2)
