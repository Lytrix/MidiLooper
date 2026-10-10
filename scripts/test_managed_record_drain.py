"""Same-owner end-of-run drain: real framing/parser and a controlled firmware endpoint."""
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from capture_session import parse_command_request
from hitl.managed_capture import ManagedCaptureSession
from hitl.serial.protocol import record_evidence_stats, stopped_capture_queue_idle
from hitl.legacy_record_baseline import _managed_record_evidence_cleanup


class Device:
    def __init__(self, path, *, answer_delay_s: float = 0.0, clock=None):
        self.path = path
        path.write_text("")
        self.commands = []
        self.stopped = False
        self.ignore_stop = False
        self.pending = 0
        self.enqueued = 130
        self.tx = 0
        self.evicted = 0
        self.rejected = 0
        self.drop_log = False
        self.no_fence = False
        self.regress = False
        self.flush_after_stats_fence = False
        self.latch_stats = False
        # Rows the ring counted but this log can never receive: shed before the managed
        # log opened (`captures/live-current-root-entry-t8s4-14` holds `RID entered=7,
        # enq=7,tx=6` for the rest of that boot) and, separately, rows that keep entering
        # while the retrieval drains (an in-session loss, which must still fail closed).
        self.lost_before_session: dict[str, int] = {}
        self.grow_shortfall = 0
        self.in_session_lost = 0
        self.shed_total = 0
        self.answer_delay_s = answer_delay_s
        self.clock = clock

    def stop(self):
        self.commands.append("MIDI_STOP")
        self.stopped = not self.ignore_stop

    def _family_rows(self):
        """The reply's counted families, including the shortfalls this fixture models."""
        extra = self.in_session_lost

        def counts(family, enqueued, transmitted):
            enqueued += self.lost_before_session.get(family, 0) + extra
            return enqueued, transmitted

        rid, rid_tx = counts("RID", 0, 0)
        reca, reca_tx = counts("RECA", 0, 0)
        recs, recs_tx = counts("RECS", self.enqueued, self.tx)
        return [f"#CAP,1,RID,stats,entered={rid},enq={rid},rej=0,tx={rid_tx},evict=0",
                f"#CAP,1,RING,record_stats,RECA,entered={reca},enq={reca},rej=0,tx={reca_tx},evict=0",
                f"#CAP,1,RING,record_stats,RECS,entered={recs},enq={recs},rej={self.rejected},"
                f"tx={recs_tx},evict={self.evicted}",
                f"#CAP,1,RING,stats,attempts=0,ok=0,flush=0,tx={self.tx},shed={self.shed_total},"
                f"tierA_evict=0,overflow=0,crit_rej=0,wipe=0,wipe_bytes=0"]

    def write(self, request):
        start, end, command = parse_command_request(request)
        self.commands.append(command)
        rows = [f"#CAP,1,TRACE_FENCE,{start}"]
        if self.stopped:
            if command == "!STOP_STATUS":
                rows.append(f"#STOP_STATUS,us=1,state=STOPPED,pending={self.pending},queue={self.pending}/2")
            elif command == "!CAP_IDENTITY_STATS":
                self.in_session_lost += self.grow_shortfall
                rows += self._family_rows()
                if self.latch_stats:
                    rows.append("#CAP,1,RING,record_stats,DIAG,latch,entered=0,enq=0,rej=0,tx=0,evict=0")
            elif command == "!TRACE_DRAIN":
                if self.pending:
                    self.pending = 0
                    self.enqueued += 1  # a final publication row appears during cleanup
                count = min(64, self.enqueued - self.tx)
                self.tx += count
                if not self.drop_log:
                    rows += ["#CAP,1,RECS,stage,publish,0,0,0,0,0,0,ok"] * count
                if self.regress:
                    self.enqueued = self.tx = 0
        if not self.no_fence:
            rows.append(f"#CAP,2,TRACE_FENCE,{end}")
        payload = "\n".join(rows) + "\n"
        if self.answer_delay_s and self.clock is not None:
            # A device whose reply lands later than the request that asked for it.
            self.clock.defer(self.clock.now + self.answer_delay_s, payload, self.path)
            return
        with self.path.open("a") as log:
            log.write(payload)
            if command == "!CAP_IDENTITY_STATS" and self.flush_after_stats_fence:
                self.flush_after_stats_fence = False
                log.write("#CAP,3,RECS,stage,publish,0,0,0,0,0,0,ok\n")
                self.tx += 1

    def flush(self):
        pass


class FakeClock:
    """Deterministic stand-in for the drain's own clock, so a late reply is reproducible.

    The drain waits for a fenced answer with its own polling sleeps, so advancing this clock
    inside `sleep` is what makes "the device answers after the retrieval's budget ran out"
    repeatable instead of a rare race.
    """

    def __init__(self):
        self.now = 0.0
        self.due = []

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += max(seconds, 0.0)
        self.deliver()

    def defer(self, when, payload, path):
        self.due.append((when, payload, path))
        self.due.sort(key=lambda item: item[0])

    def deliver(self):
        ready = [item for item in self.due if item[0] <= self.now]
        self.due = [item for item in self.due if item[0] > self.now]
        for _when, payload, path in ready:
            with path.open("a") as log:
                log.write(payload)


class DrainTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.device = Device(root / "session.log")
        self.session = ManagedCaptureSession(out_dir=root)
        self.session._session_log = self.device.path
        self.session._process = SimpleNamespace(stdin=self.device, poll=lambda: None)

    def drain(self, **kwargs):
        return self.session.drain_record_evidence(self.device.stop, timeout_s=.2, **kwargs)

    def test_stop_then_multiple_fenced_drains_before_completion(self):
        result = self.drain()
        self.assertTrue(result["complete"], result)
        self.assertEqual(3, result["drains"])
        self.assertEqual("MIDI_STOP", self.device.commands[0])
        self.assertEqual(130, result["log_counts"]["RECS"])
        self.assertEqual(130, result["families"]["RECS"]["tx"])
        self.assertIsNone(self.session.pending_command_nonce)

    def test_async_flush_after_stats_fence_belongs_to_next_snapshot(self):
        self.device.flush_after_stats_fence = True
        result = self.drain()
        self.assertTrue(result["complete"], result)
        self.assertEqual(130, result["log_counts"]["RECS"])

    def test_prior_boot_transmissions_do_not_require_a_reboot(self):
        self.device.enqueued = 135
        self.device.tx = 5  # from before this managed log opened
        result = self.drain()
        self.assertTrue(result["complete"], result)
        self.assertEqual(130, result["log_counts"]["RECS"])
        self.assertEqual(135, result["families"]["RECS"]["tx"])
        self.assertEqual(5, result["before"]["RECS"]["tx"])

    def test_waits_for_finalizer_even_if_existing_rows_were_drained(self):
        self.device.enqueued = 0
        self.device.pending = 1
        result = self.drain()
        self.assertTrue(result["complete"], result)
        self.assertEqual(1, result["log_counts"]["RECS"])
        self.assertEqual(1, result["drains"])

    def test_fence_alone_does_not_prove_transport_stopped(self):
        self.device.ignore_stop = True
        result = self.drain()
        self.assertFalse(result["complete"])
        self.assertFalse(result["stopped_confirmed"])
        self.assertNotIn("!TRACE_DRAIN", self.device.commands)

    def test_transmission_counter_does_not_prove_log_receipt(self):
        self.device.drop_log = True
        result = self.drain()
        self.assertFalse(result["complete"])
        self.assertIn("do not match", result["reason"])

    def test_a_reported_family_without_log_rows_does_not_break_the_reconciliation(self):
        """Task 2.2's latch family is *reported*, not counted in the log, and must not break the drain.

        Regression: the reconciliation iterated every reported family against a row counter that only sees
        `RECA`/`RECS`/`RID`, so a real baseline run carrying `RING,record_stats,DIAG,latch,…` died with
        `KeyError: 'DIAG,latch'` and reported `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)` on an otherwise clean
        run (`captures/session_20261001_092818.log`). The family is now named as reported-not-reconciled
        instead of being dropped, so a reader can see which families carry a row cross-check.
        """
        self.device.latch_stats = True
        result = self.drain()
        self.assertTrue(result["complete"], result)
        self.assertEqual(["RECA", "RECS", "RID"], result["reconciled_families"])
        self.assertEqual(["DIAG,latch"], result["unreconciled_families"])
        self.assertEqual(130, result["log_counts"]["RECS"])
        self.assertEqual(130, result["families"]["RECS"]["tx"])
        self.assertEqual({"entered": 0, "enq": 0, "rej": 0, "tx": 0, "evict": 0},
                         result["families"]["DIAG,latch"])

    def test_a_reported_family_does_not_weaken_the_row_reconciliation(self):
        """The extra family is reported, not a licence: rows missing from the log still fail, with or without it."""
        for latch in (False, True):
            with self.subTest(latch=latch):
                root = Path(self.temp.name) / f"drop-{latch}"
                root.mkdir()
                device = Device(root / "session.log")
                device.drop_log = True
                device.latch_stats = latch
                session = ManagedCaptureSession(out_dir=root)
                session._session_log = device.path
                session._process = SimpleNamespace(stdin=device, poll=lambda: None)
                result = session.drain_record_evidence(device.stop, timeout_s=.2)
                self.assertFalse(result["complete"], result)
                self.assertIn("do not match", result["reason"])

    def test_lost_rows_do_not_become_a_successful_drain(self):
        for field in ("evicted", "rejected"):
            with self.subTest(field=field):
                setattr(self.device, field, 1)
                result = self.drain()
                self.assertFalse(result["complete"])
                self.assertIn("rejected or evicted", result["reason"])
                setattr(self.device, field, 0)

    def test_missing_fence_fails_and_releases_request_slot(self):
        self.device.no_fence = True
        result = self.drain()
        self.assertFalse(result["complete"])
        self.assertIn("fence missing", result["reason"])
        self.assertIsNone(self.session.pending_command_nonce)

    def test_a_row_shed_before_this_session_does_not_drain_until_the_deadline(self):
        """Regression: a boot-scoped shortfall the log can never receive must not chase the deadline.

        `captures/live-current-root-entry-t8s4-14` reported `RID entered=7,enq=7,tx=6` and the same for
        `RECA` -- one Record row that entered the ring and was shed before this managed log opened. The
        counters span the boot, so requiring every family to balance (`tx == enq`) demanded drain rounds
        until the retrieval deadline: `drains=20`, the leg reported `INCONCLUSIVE_CAPTURE
        (SERIAL_INCOMPLETE)` for evidence it had already received, and the run's own take rows were in the
        log the whole time. The retrieval now converges when the *shortfall* stops shrinking, and names the
        frozen remainder instead of treating a lost row as a pending transmission.
        """
        self.device.lost_before_session = {"RID": 1, "RECA": 1}
        self.device.shed_total = 901  # the ring's own explanation for a row that never left it
        result = self.drain()
        self.assertTrue(result["complete"], result)
        self.assertEqual({"RID": 1, "RECA": 1}, result["stalled_families"])
        self.assertIn("never left the ring", result["reason"])
        self.assertEqual(901, result["ring_loss"]["shed"])
        self.assertEqual(130, result["log_counts"]["RECS"])
        self.assertLessEqual(result["drains"], 6, result)

    def test_a_shortfall_that_keeps_growing_is_not_a_successful_drain(self):
        """The converse guard: rows entering the ring while the retrieval drains are still a failure."""
        self.device.grow_shortfall = 1  # a Record row enters on every reply and never transmits
        result = self.drain()
        self.assertFalse(result["complete"], result)
        self.assertNotIn("queued Record rows drained into this session log", result["reason"])

    def test_a_request_is_not_issued_without_budget_for_its_own_answer(self):
        """A fenced request needs room for its reply: an unanswerable last request is not a lost fence.

        The failing request in `captures/live-current-root-entry-t8s4-14` was issued as the deadline
        expired, so its window was never polled; the fences arrived 3 ms later
        (`#CAP,2253438046,TRACE_FENCE,2cc9...` / `...8b09...`). The retrieval now reports the pending
        deadline instead of claiming a fence that was lost.
        """
        clock = FakeClock()
        root = Path(self.temp.name) / "late"
        root.mkdir()
        device = Device(root / "session.log", answer_delay_s=0.16, clock=clock)
        session = ManagedCaptureSession(out_dir=root)
        session._session_log = device.path
        session._process = SimpleNamespace(stdin=device, poll=lambda: None)
        with patch("hitl.managed_capture.time", SimpleNamespace(monotonic=clock.monotonic,
                                                               sleep=clock.sleep)):
            result = session.drain_record_evidence(device.stop, timeout_s=0.45)
        self.assertFalse(result["complete"], result)
        self.assertIn("drain deadline", result["reason"])
        self.assertNotIn("fence missing", result["reason"])
        self.assertNotIn("!TRACE_DRAIN", device.commands)

    def test_counter_reset_is_not_empty_ring_success(self):
        self.device.regress = True
        result = self.drain()
        self.assertFalse(result["complete"])
        self.assertIn("regressed", result["reason"])

    def test_outstanding_request_is_not_stolen(self):
        self.session._pending_command_nonce = "other-request"
        result = self.drain()
        self.assertFalse(result["complete"])
        self.assertIn("one outstanding", result["reason"])
        self.assertEqual("other-request", self.session.pending_command_nonce)

    def test_saturated_counters_cannot_prove_delivery(self):
        self.device.enqueued = 0xFFFFFFFF
        result = self.drain()
        self.assertFalse(result["complete"])
        self.assertIn("saturated", result["reason"])

    def test_cleanup_runs_on_leg_exception_before_midi_port_closes(self):
        events = []
        class Port:
            def __enter__(self): return self
            def __exit__(self, *exc): events.append("port_closed")
            def send(self, message):
                self_device.stop()
                events.append(message.type)
        self_device = self.device
        result = {}
        with patch("hitl.managed_capture.active_managed_capture", return_value=self.session):
            with self.assertRaisesRegex(RuntimeError, "leg failed"):
                with Port() as port, _managed_record_evidence_cleanup(port, result):
                    raise RuntimeError("leg failed")
        self.assertTrue(result["complete"], result)
        self.assertEqual(["stop", "port_closed"], events)

    def test_cleanup_failure_preserves_the_original_leg_exception(self):
        session = SimpleNamespace(drain_record_evidence=lambda send: (_ for _ in ()).throw(OSError("pipe closed")))
        result = {}
        with patch("hitl.managed_capture.active_managed_capture", return_value=session):
            with self.assertRaisesRegex(RuntimeError, "original leg failure"):
                with _managed_record_evidence_cleanup(None, result):
                    raise RuntimeError("original leg failure")
        self.assertFalse(result["complete"])
        self.assertIn("pipe closed", result["reason"])

    def test_nonmanaged_run_does_not_change_transport(self):
        result = {}
        with patch("hitl.managed_capture.active_managed_capture", return_value=None):
            with _managed_record_evidence_cleanup(None, result):
                pass
        self.assertEqual({}, result)

    def test_parser_refuses_partial_or_duplicate_stats_and_pending_queue(self):
        self.assertIsNone(record_evidence_stats(["#CAP,1,RID,stats,entered=0,enq=0,rej=0,tx=0,evict=0"]))
        self.assertFalse(stopped_capture_queue_idle(["#STOP_STATUS,us=1,pending=0,queue=1/2"]))
        self.assertFalse(stopped_capture_queue_idle([]))

    def test_parser_carries_every_counted_family_not_only_the_record_trio(self):
        """Task 2.2: a family that is silently dropped cannot have its loss detected.

        The reply now carries `DIAG,latch` alongside the record trio (measured on the flashed image:
        `captures/base_20260930_164240_serial.log`). The trio must still be complete for the reply to count --
        that is what the managed retrieval reconciles against the log -- but the extra family is *reported*
        rather than ignored, so the retrieval's refusal/displacement gate covers it.
        """
        reply = [
            "#CAP,1,RID,stats,entered=1,enq=1,rej=0,tx=1,evict=0",
            "#CAP,1,RING,record_stats,RECA,entered=1,enq=1,rej=0,tx=1,evict=0",
            "#CAP,1,RING,record_stats,RECS,entered=7,enq=7,rej=0,tx=7,evict=0",
            "#CAP,1,RING,record_stats,DIAG,latch,entered=0,enq=0,rej=0,tx=0,evict=0",
        ]
        stats = record_evidence_stats(reply)
        self.assertIsNotNone(stats)
        self.assertEqual({"RID", "RECA", "RECS", "DIAG,latch"}, set(stats))
        self.assertEqual({"entered": 0, "enq": 0, "rej": 0, "tx": 0, "evict": 0}, stats["DIAG,latch"])
        # A reply that holds the extra family but misses a record family is still incomplete.
        self.assertIsNone(record_evidence_stats([row for row in reply if ",RECS," not in row]))
        # A duplicate family is refused, extra or not.
        self.assertIsNone(record_evidence_stats(reply + [reply[-1]]))
        # A malformed counter is refused rather than read as zero.
        self.assertIsNone(record_evidence_stats(reply[:-1] + [
            "#CAP,1,RING,record_stats,DIAG,latch,entered=,enq=0,rej=0,tx=0,evict=0"]))


class AttachStreamGateTests(unittest.TestCase):
    """A capture that attached to a silent device must not run the measured sequence."""

    def test_silent_attach_is_refused(self) -> None:
        from hitl.managed_capture import attach_stream_is_usable

        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "silent.log"
            log.write_text("\n".join(f"line {i}" for i in range(15)), encoding="utf-8")
            usable, detail = attach_stream_is_usable(log)
        self.assertFalse(usable)
        self.assertIn("15 line", detail)

    def test_stale_reply_is_not_readiness(self) -> None:
        """A reply that precedes this attach is a backlog, not evidence of a live device."""
        from hitl import managed_capture as mc

        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "stale.log"
            log.write_text("\n".join(f"line {i}" for i in range(60)), encoding="utf-8")

            class FakeSession:
                session_log = log

                def send_command(self, command, *, nonce, end_nonce):
                    return True, "accepted"

                def fenced_response(self, nonce, end_nonce, *, timeout_s):
                    return ["#STOP_STATUS,us=1,state=STOPPED,pending=0,queue=0/4"]

            fake = FakeSession()
            # The reply's fences are not in the log at all -> not fresh.
            with patch.object(mc, "active_managed_capture", lambda: fake):
                ready, reason = mc.establish_attach_readiness(timeout_s=0.2)
        self.assertFalse(ready)
        self.assertIn("not present", reason)

    def test_fresh_stopped_reply_is_readiness(self) -> None:
        from hitl import managed_capture as mc

        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "fresh.log"
            log.write_text("\n".join(f"line {i}" for i in range(60)), encoding="utf-8")
            state = {"nonce": "", "end": "", "baseline": 0}

            class FakeSession:
                session_log = log

                def send_command(self, command, *, nonce, end_nonce):
                    state["nonce"], state["end"] = nonce, end_nonce
                    with log.open("a", encoding="utf-8") as fh:
                        fh.write(f"#CAP,1,TRACE_FENCE,{nonce}\n")
                        fh.write("#HEAP_STATS,transport=0,tracks=0,storage=0\n")
                        fh.write(f"#CAP,2,TRACE_FENCE,{end_nonce}\n")
                    return True, "accepted"

                def fenced_response(self, nonce, end_nonce, *, timeout_s):
                    return ["#HEAP_STATS,transport=0,tracks=0,storage=0"]

            fake = FakeSession()
            with patch.object(mc, "active_managed_capture", lambda: fake):
                ready, reason = mc.establish_attach_readiness(timeout_s=1.0)
        self.assertTrue(ready, reason)

    def test_idle_device_attach_is_not_judged_by_line_count(self) -> None:
        """An attaching run joins a running, idle device that emits nothing until spoken to."""
        from hitl.managed_capture import attach_stream_is_usable

        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "idle.log"
            log.write_text("\n".join(f"line {i}" for i in range(21)), encoding="utf-8")
            usable, detail = attach_stream_is_usable(log, require_stream=False)
        self.assertTrue(usable, detail)
        self.assertIn("attaching", detail)

    def test_healthy_attach_is_accepted(self) -> None:
        from hitl.managed_capture import attach_stream_is_usable

        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "healthy.log"
            rows = ["BOOT,load,ok"] + [f"#CAP,{i},ST,Track,PLAYING,PLAYING" for i in range(60)]
            log.write_text("\n".join(rows), encoding="utf-8")
            usable, detail = attach_stream_is_usable(log)
        self.assertTrue(usable, detail)


if __name__ == "__main__":
    unittest.main()
