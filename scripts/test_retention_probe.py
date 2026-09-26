#!/usr/bin/env python3
"""Host tests for the S4f.3 retention oracle (`hitl/retention_probe.py`).

The oracle must accept a **complete ordered chain** and refuse to pass when any link is missing or reordered.
These tests run without a device: they feed synthetic logs shaped like the device's real `#CAP` records and
metadata/report JSON, and assert the verdict, the reason and the exit code for each mutation.

Two of these are regressions for false passes the owner reproduced on the first oracle:
`test_a_start_before_capacity_release_is_not_a_pass` and
`test_a_generic_denial_does_not_substitute_for_the_identified_one`.

Run: .venv/bin/python scripts/test_retention_probe.py
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from hitl.retention_probe import analyse  # noqa: E402

BOOT = "BOOT,usb_host,deferred"
TEMPO = "[91.823] [DEBUG] [MIDI] Sent tempo CC feedback: 120.0 BPM -> CC=60"
METADATA = {"git_commit": "abc123def456", "firmware_hex_sha256": "f00dbabe"}
BASE_REPORT = {"hitl_report": {"capture_integrity": "COMPLETE", "capture_complete": True,
                               "setup_transitions_confirmed": True, "issues": []}}

OPEN_US = 100_000_000
WINDOW_US = 1_335_389
RELEASE_US = OPEN_US + WINDOW_US
DENIAL_US = OPEN_US + 20_000
# The successor seal must follow the release for a well-ordered run.
START_US = RELEASE_US + 100_000
SEAL_US = START_US + 50_000
STOP_US = START_US + 80_000
TAIL_US = STOP_US + 600_000  # > the 500 ms follow-up floor


def chain_log(*, retained: bool = True, start_us: int = START_US, seal_events: int = 2,
              release_us: int = RELEASE_US, extra: list[str] | None = None,
              denial: str | None = None, bind_evidence: bool = True,
              dup_start: bool = False, tail_us: int = TAIL_US,
              seed: bool = True, window_us: int | None = OPEN_US) -> str:
    """A synthetic capture log for the full chain; each keyword removes or moves one link."""
    denial_body = denial if denial is not None else f"DIAG,reentry,deny,16400,7,1,0"
    lines: list[str] = []
    if seed:
        lines.append(f"#CAP,{OPEN_US - 5000},RECS,stop,0,13930,13663,1803,1536,0")
    if window_us is not None:
        lines.append(f"#CAP,{window_us},DIAG,reentry_window_open,0,16387,1536")
    lines.append(f"#CAP,{DENIAL_US},{denial_body}")
    lines.append(f"#CAP,{DENIAL_US},ODUB,stop,admission,0,0,4294967295,4294967295,1,0,"
                 "successor_stop_denied_capacity")
    if retained:
        # A human log row: no #CAP timestamp, ordered by position between the denial and the release.
        lines.append("[INFO] [LOOP] Track 0 loop 3: switch and record start retained "
                     "(stop not yet admitted)")
    lines.append(f"#CAP,{release_us},DIAG,canonical_release,0,4242,1536,8")
    if bind_evidence:
        # The identity-grade binding record: same slot and interval as the denial.
        lines.append(f"#CAP,{release_us + 5_000},DIAG,reentry,bound,17000,7,1,0")
        lines.append(f"#CAP,{release_us + 10_000},DIAG,reentry_window_open,1,17000,1536")
        lines.append(f"#CAP,{release_us + 20_000},DIAG,stop_phase,0,1,2,4242,9,0,5,0,0,"
                     f"{release_us + 20_000},{release_us + 20_000},0")
    lines.append(f"#CAP,{start_us},ST,Track,ARMED,RECORDING")
    lines.append(f"#CAP,{start_us},RECA,2,17000")
    lines.append(f"#CAP,{SEAL_US},RECS,stage,seal,866,858,4294967295,4294967295,{seal_events},0,ok")
    lines.append(f"#CAP,{STOP_US},RECS,stop,2,13930,13663,1803,1536,0")
    if dup_start:
        lines.append(f"#CAP,{STOP_US + 1000},RECA,2,18000")
    lines.append(f"#CAP,{tail_us},DIAG,reentry,admit,20000,8,2,0")
    lines.extend(extra or [])
    return "\n".join([BOOT, TEMPO] + lines) + "\n"


def content_dump(*, slot: int = 2, notes: tuple[int, ...] = (67,), expected_slot: int | None = None) -> str:
    """A `!CAPTURE_CONTENT` dump as dump_and_capture.py records it in serial.log."""
    out = [f"CAPTURE_CONTENT,begin,track=1,slot={slot},length=1536"]
    out.append(f"CAPTURE_CONTENT,rows,record,0,{2 * len(notes)}")
    for n in notes:
        for tick, etype, vel in ((10, 144, 100), (40, 128, 0)):
            out.append(f"CAPTURE_CONTENT,event,record,0,{tick},{etype},1,{n},{vel},7")
    out.append("CAPTURE_CONTENT,end")
    return "\n".join(out) + "\n"


REVIEW_OK = {"result": "PASS", "storage_matches_effective": True,
             "cycles": [{"inventory_equal": True}, {"inventory_equal": True}]}


def run(text: str, **kw):
    kw.setdefault("slot_a", 0)
    kw.setdefault("slot_b", 1)
    kw.setdefault("slot_c", 2)
    kw.setdefault("metadata", METADATA)
    kw.setdefault("base_report", BASE_REPORT)
    kw.setdefault("accept_event_count_only", False)
    return analyse(text, **kw)


def by_name(result) -> dict:
    return {c.name: c for c in result.checks}


class RetentionProbeChainTests(unittest.TestCase):
    def test_conceded_links_yield_partial_never_a_pass(self):
        """A conceded link is not full acceptance: an automated consumer must not close the gate on it."""
        result = run(chain_log(), accept_slot_scoped_bind=True, accept_event_count_only=True)
        self.assertEqual("RETENTION_PARTIAL", result.verdict,
                         [(c.name, c.status, c.detail) for c in result.checks])
        self.assertEqual(5, result.exit_code)
        self.assertNotEqual(0, result.exit_code)
        self.assertGreaterEqual(len(result.concessions), 1, result.concessions)
        for name in ("L1_finalizer_outstanding", "L2_denial_identified_for_b", "L3_request_retained_for_c",
                     "L4_capacity_released", "L5_same_b_stop_bound", "L6_successor_started_once",
                     "L7_successor_recorded_expected_midi", "L8_bounded_followup_clean"):
            self.assertEqual("ok", by_name(result)[name].status, name)

    def test_the_same_chain_without_concessions_is_insufficient_evidence_not_a_pass(self):
        result = run(chain_log())
        self.assertEqual("INSUFFICIENT_EVIDENCE", result.verdict)
        self.assertEqual(4, result.exit_code)
        # The default chain now carries the identity-grade bind record, so only L7 lacks evidence:
        # event count cannot show the expected notes.
        self.assertEqual("ok", by_name(result)["L5_same_b_stop_bound"].status)
        self.assertEqual("unknown", by_name(result)["L7_successor_recorded_expected_midi"].status)

    # --- the two reproduced false passes -------------------------------------------------------------
    def test_a_start_before_capacity_release_is_not_a_pass(self):
        """Regression: C's start before A released capacity used to pass."""
        result = run(chain_log(start_us=OPEN_US + 200_000), accept_slot_scoped_bind=True,
                     accept_event_count_only=True)
        self.assertEqual("RETENTION_FAIL", result.verdict, result.verdict)
        self.assertEqual("fail", by_name(result)["L6_successor_started_once"].status)
        self.assertIn("before capacity was released",
                      by_name(result)["L6_successor_started_once"].detail)

    def test_a_generic_denial_does_not_substitute_for_the_identified_one(self):
        """Regression: the slot-agnostic stage record used to satisfy the denial link."""
        text = chain_log()
        text = "\n".join(l for l in text.splitlines() if "DIAG,reentry,deny" not in l) + "\n"
        result = run(text, accept_slot_scoped_bind=True, accept_event_count_only=True)
        self.assertEqual("INSUFFICIENT_EVIDENCE", result.verdict)
        self.assertEqual(4, result.exit_code)
        self.assertEqual("unknown", by_name(result)["L2_denial_identified_for_b"].status)
        self.assertIn("cannot establish", by_name(result)["L2_denial_identified_for_b"].detail)

    # --- the timely human denial row now carries its own identity ------------------------------------
    def _human_only_denial_log(self, human_row: str) -> str:
        """The machine denial rows removed, leaving the denial's timely human row as the only trace.

        This is the measured shedding case: the deferred `#CAP` rows were absent from a completed run while
        the behaviour had happened, so what the human row can and cannot establish matters.
        """
        text = chain_log(extra=[human_row])
        return "\n".join(l for l in text.splitlines()
                         if "DIAG,reentry,deny" not in l
                         and "successor_stop_denied_capacity" not in l) + "\n"

    def test_the_human_denial_names_its_slot_and_interval_but_is_not_a_device_time_anchor(self):
        """Identity without a device timestamp: reported as insufficient, never promoted to a pass.

        L5 orders the bind against `denial.us` in DEVICE micros. The human row's timestamp is host
        milliseconds, so accepting it as the L2 pass would put two time bases into that comparison.
        """
        text = self._human_only_denial_log(
            "[1885.0] [DEBUG] [TRACK] Overdub stop refused: capacity unavailable slot=1 interval=7")
        result = run(text, accept_slot_scoped_bind=True, accept_event_count_only=True)
        check = by_name(result)["L2_denial_identified_for_b"]
        self.assertEqual("unknown", check.status, check.detail)
        self.assertIn("interval 7", check.detail)
        self.assertIn("device-timestamped", check.detail)
        self.assertEqual("INSUFFICIENT_EVIDENCE", result.verdict)
        self.assertNotEqual(0, result.exit_code)

    def test_a_human_denial_naming_another_slot_fails_rather_than_staying_unknown(self):
        """The logging fix makes a wrong-slot denial detectable, which previously looked like a gap."""
        text = self._human_only_denial_log(
            "[1885.0] [DEBUG] [TRACK] Overdub stop refused: capacity unavailable slot=5 interval=7")
        result = run(text, accept_slot_scoped_bind=True, accept_event_count_only=True)
        check = by_name(result)["L2_denial_identified_for_b"]
        self.assertEqual("fail", check.status, check.detail)
        self.assertIn("slot 5", check.detail)
        self.assertIn("not the one refused", check.detail)

    def test_a_pre_identity_human_denial_stays_unknown(self):
        """An older firmware emits the row without the fields; that must not read as a mismatch."""
        text = self._human_only_denial_log(
            "[1885.0] [DEBUG] [TRACK] Overdub stop refused: capacity unavailable")
        result = run(text, accept_slot_scoped_bind=True, accept_event_count_only=True)
        check = by_name(result)["L2_denial_identified_for_b"]
        self.assertEqual("unknown", check.status, check.detail)
        self.assertIn("pre-identity firmware", check.detail)

    # --- S4.1: the working-set lease release is reported by name, not inferred ----------------------
    def test_a_ws_release_row_is_reported_by_name_and_is_not_a_gate(self):
        """Reported, not gating: `canonical_release` is the occupancy, `ws_release` is the lease."""
        text = chain_log(extra=["#CAP,60000000,DIAG,ws_release,4,40,released"])
        result = run(text, accept_slot_scoped_bind=True, accept_event_count_only=True)
        joined = " ".join(result.limitations)
        self.assertIn("IS observed by name", joined)
        self.assertIn("[4]", joined)
        self.assertEqual(["60000000: DIAG,ws_release,4,40,released"],
                         result.evidence["working_set_lease_release"])
        # It must not change the verdict: the row is evidence about a different resource than L4's.
        self.assertEqual("RETENTION_PARTIAL", result.verdict)

    def test_refused_lease_releases_are_reported_not_discarded(self):
        text = chain_log(extra=["#CAP,60000000,DIAG,ws_release,4,40,refused_busy"])
        result = run(text, accept_slot_scoped_bind=True, accept_event_count_only=True)
        joined = " ".join(result.limitations)
        self.assertIn("refusals reported rather than discarded", joined)
        self.assertIn("slot 4 (refused_busy)", joined)

    def test_an_absent_ws_release_row_reads_as_not_exercised_not_as_a_failure(self):
        """A same-track A/B/C sequence excludes its own track from the take-start sweep, so the row can
        legitimately be absent -- the report must say so rather than leave it inferred."""
        result = run(chain_log(), accept_slot_scoped_bind=True, accept_event_count_only=True)
        joined = " ".join(result.limitations)
        self.assertIn("NOT observed by name", joined)
        self.assertIn("did not exercise it", joined)
        self.assertEqual([], result.evidence["working_set_lease_release"])

    # --- one negative per remaining link ------------------------------------------------------------
    def test_no_denial_at_all_is_not_exercised(self):
        text = chain_log()
        text = "\n".join(l for l in text.splitlines()
                         if "DIAG,reentry,deny" not in l
                         and "successor_stop_denied_capacity" not in l) + "\n"
        result = run(text)
        self.assertEqual("RETENTION_NOT_EXERCISED", result.verdict)
        self.assertEqual("fail", by_name(result)["L2_denial_identified_for_b"].status)

    def test_a_setup_failure_is_not_masked_by_an_unstaged_or_unidentified_denial(self):
        """A run that is not evidence must never be reported as a behavioural result."""
        no_denial = "\n".join(l for l in chain_log().splitlines()
                              if "DIAG,reentry,deny" not in l
                              and "successor_stop_denied_capacity" not in l) + "\n"
        # Staged-with-no-denial plus a failed setup must be SETUP_INCOMPLETE, not NOT_EXERCISED.
        no_boot = "\n".join(no_denial.splitlines()[1:]) + "\n"
        result = run(no_boot)
        self.assertEqual("SETUP_INCOMPLETE", result.verdict,
                         [(c.name, c.status) for c in result.checks if c.status != "ok"])
        # ...and generic-denial-only plus a failed setup must be SETUP_INCOMPLETE, not INSUFFICIENT_EVIDENCE.
        generic_only = "\n".join(l for l in chain_log().splitlines()
                                 if "DIAG,reentry,deny" not in l) + "\n"
        result = run("\n".join(generic_only.splitlines()[1:]) + "\n")
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)

    def test_a_missing_retained_request_row_is_insufficient_evidence(self):
        result = run(chain_log(retained=False), accept_slot_scoped_bind=True,
                     accept_event_count_only=True)
        self.assertEqual("INSUFFICIENT_EVIDENCE", result.verdict)
        self.assertEqual("unknown", by_name(result)["L3_request_retained_for_c"].status)
        self.assertIn("must name both the track and the slot",
                      by_name(result)["L3_request_retained_for_c"].detail)

    def test_a_start_that_was_not_held_fails_retention(self):
        # The retained row exists, but a start also fired before the release.
        text = chain_log(extra=[f"#CAP,{OPEN_US + 150_000},RECA,2,16500"])
        result = run(text, accept_slot_scoped_bind=True, accept_event_count_only=True)
        self.assertEqual("RETENTION_FAIL", result.verdict)
        self.assertEqual("fail", by_name(result)["L3_request_retained_for_c"].status)

    def test_a_missing_bind_link_is_insufficient_evidence(self):
        result = run(chain_log(bind_evidence=False), accept_event_count_only=True)
        self.assertEqual("INSUFFICIENT_EVIDENCE", result.verdict)
        self.assertEqual("unknown", by_name(result)["L5_same_b_stop_bound"].status)
        self.assertTrue(any("captureInterval_" in l for l in result.limitations))

    def test_two_successor_starts_fail_exactly_once(self):
        result = run(chain_log(dup_start=True), accept_slot_scoped_bind=True,
                     accept_event_count_only=True)
        self.assertEqual("RETENTION_FAIL", result.verdict)
        self.assertIn("more than once", by_name(result)["L6_successor_started_once"].detail)

    def test_a_start_on_the_wrong_slot_is_not_a_pass(self):
        text = chain_log().replace("RECA,2,17000", "RECA,1,17000")
        result = run(text, accept_slot_scoped_bind=True, accept_event_count_only=True)
        self.assertEqual("RETENTION_FAIL", result.verdict)
        self.assertIn("never executed", by_name(result)["L6_successor_started_once"].detail)

    def test_a_seal_with_no_events_fails_the_recording_link(self):
        result = run(chain_log(seal_events=0), accept_slot_scoped_bind=True,
                     accept_event_count_only=True)
        self.assertEqual("RETENTION_FAIL", result.verdict)
        self.assertIn("eventCount=0", by_name(result)["L7_successor_recorded_expected_midi"].detail)

    def test_an_unexpected_event_count_fails_the_recording_link(self):
        result = run(chain_log(seal_events=5), expect_successor_events=2,
                     accept_slot_scoped_bind=True, accept_event_count_only=True)
        self.assertEqual("RETENTION_FAIL", result.verdict)
        self.assertIn("expected 2", by_name(result)["L7_successor_recorded_expected_midi"].detail)

    def test_a_content_review_alone_is_parity_only_not_note_level(self):
        """A review proves storage<->effective parity; it cannot say *which* notes were recorded."""
        result = run(chain_log(), content_review=REVIEW_OK, accept_slot_scoped_bind=True)
        self.assertEqual("unknown", by_name(result)["L7_successor_recorded_expected_midi"].status)
        self.assertIn("parity is not note-level",
                      by_name(result)["L7_successor_recorded_expected_midi"].detail)

    def test_the_complete_unconceded_chain_passes(self):
        """The acceptance target: every link on its own evidence, no concession, exit 0."""
        result = run(chain_log(), content_dump=content_dump(), content_review=REVIEW_OK,
                     expect_successor_notes=(67,))
        self.assertEqual("RETENTION_PASS", result.verdict,
                         [(c.name, c.status, c.detail) for c in result.checks if c.status != "ok"])
        self.assertEqual(0, result.exit_code)
        self.assertEqual([], result.concessions)
        self.assertEqual("ok", by_name(result)["L5_same_b_stop_bound"].status)
        self.assertIn("contains the expected note", by_name(result)["L7_successor_recorded_expected_midi"].detail)

    def test_a_dump_for_the_wrong_slot_fails_the_recording_link(self):
        result = run(chain_log(), content_dump=content_dump(slot=1), content_review=REVIEW_OK,
                     expect_successor_notes=(67,))
        self.assertEqual("RETENTION_FAIL", result.verdict)
        self.assertIn("not the successor slot",
                      by_name(result)["L7_successor_recorded_expected_midi"].detail)

    def test_a_dump_without_the_expected_note_fails_the_recording_link(self):
        result = run(chain_log(), content_dump=content_dump(notes=(60, 62)), content_review=REVIEW_OK,
                     expect_successor_notes=(67,))
        self.assertEqual("RETENTION_FAIL", result.verdict)
        self.assertIn("does not contain the expected note",
                      by_name(result)["L7_successor_recorded_expected_midi"].detail)

    def test_a_dump_without_a_review_is_not_a_full_pass(self):
        result = run(chain_log(), content_dump=content_dump(), expect_successor_notes=(67,))
        self.assertEqual("INSUFFICIENT_EVIDENCE", result.verdict)
        self.assertIn("parity", by_name(result)["L7_successor_recorded_expected_midi"].detail)

    def test_the_identity_bind_record_removes_the_L5_concession(self):
        result = run(chain_log(), content_dump=content_dump(), content_review=REVIEW_OK,
                     expect_successor_notes=(67,))
        self.assertEqual([], result.concessions)
        self.assertEqual("ok", by_name(result)["L5_same_b_stop_bound"].status)

    def test_a_bind_before_the_denial_fails_and_one_between_denial_and_release_passes(self):
        """The bind is ordered against the DENIAL, not against the release *record*.

        Measured on device: `canonical_release` is the first maintenance turn at which the resources were
        observed free, and it landed 25 ms AFTER B had already bound -- so requiring a bind to follow the
        release record inverts the timing the fix documents.
        """
        from test_retention_probe import OPEN_US, RELEASE_US, chain_log
        # A bind after the denial but before the release record is legitimate.
        text = chain_log().replace(f"#CAP,{RELEASE_US + 5_000},DIAG,reentry,bound,17000,7,1,0",
                                   f"#CAP,{OPEN_US + 30_000},DIAG,reentry,bound,17000,7,1,0")
        result = run(text, content_dump=content_dump(), content_review=REVIEW_OK,
                     expect_successor_notes=(67,))
        self.assertEqual("ok", by_name(result)["L5_same_b_stop_bound"].status,
                         by_name(result)["L5_same_b_stop_bound"].detail)
        # A bind at or before the denial is still a contradiction.
        text = chain_log().replace(f"#CAP,{RELEASE_US + 5_000},DIAG,reentry,bound,17000,7,1,0",
                                   f"#CAP,{OPEN_US + 1_000},DIAG,reentry,bound,17000,7,1,0")
        result = run(text, content_dump=content_dump(), content_review=REVIEW_OK,
                     expect_successor_notes=(67,))
        self.assertEqual("fail", by_name(result)["L5_same_b_stop_bound"].status)
        self.assertIn("without a denied prerequisite", by_name(result)["L5_same_b_stop_bound"].detail)

    def test_a_slot_only_bind_record_is_a_weaker_link(self):
        text = "\n".join(l for l in chain_log().splitlines() if "DIAG,reentry,bound" not in l) + "\n"
        result = run(text, content_dump=content_dump(), content_review=REVIEW_OK,
                     expect_successor_notes=(67,))
        self.assertEqual("INSUFFICIENT_EVIDENCE", result.verdict)
        self.assertIn("identity-grade", by_name(result)["L5_same_b_stop_bound"].detail)

    def test_a_short_followup_fails_the_duplicate_bound(self):
        result = run(chain_log(tail_us=STOP_US + 1000), accept_slot_scoped_bind=True,
                     accept_event_count_only=True)
        self.assertEqual("RETENTION_FAIL", result.verdict)
        self.assertIn("follow-up is too short", by_name(result)["L8_bounded_followup_clean"].detail)

    def test_a_reissued_denial_after_the_release_fails_the_duplicate_bound(self):
        extra = [f"#CAP,{STOP_US + 300_000},DIAG,reentry,deny,20000,8,1,0"]
        result = run(chain_log(extra=extra), accept_slot_scoped_bind=True,
                     accept_event_count_only=True)
        self.assertEqual("RETENTION_FAIL", result.verdict)
        self.assertIn("re-executed", by_name(result)["L8_bounded_followup_clean"].detail)

    def test_an_activation_invariant_violation_fails_loudly(self):
        text = chain_log(extra=["[INFO] [TRACK] Track 0: record activation invariant violated"])
        result = run(text, accept_slot_scoped_bind=True, accept_event_count_only=True)
        self.assertEqual("RETENTION_FAIL", result.verdict)
        self.assertEqual("fail", by_name(result)["activation_invariant_held"].status)

    # --- observation integrity ---------------------------------------------------------------------
    def test_a_missing_boot_banner_is_a_setup_failure(self):
        text = "\n".join(chain_log().splitlines()[1:]) + "\n"
        result = run(text, accept_slot_scoped_bind=True, accept_event_count_only=True)
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertEqual("fail", by_name(result)["fresh_boot"].status)

    def test_missing_build_identity_is_a_setup_failure(self):
        result = run(chain_log(), metadata={"git_commit": None, "firmware_hex_sha256": None},
                     accept_slot_scoped_bind=True, accept_event_count_only=True)
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertEqual("fail", by_name(result)["identity_pinned"].status)

    def test_capture_ring_overflow_is_a_setup_failure(self):
        text = chain_log(extra=["#CAP,999,RING,overflow,512,3"])
        result = run(text, accept_slot_scoped_bind=True, accept_event_count_only=True)
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertEqual("fail", by_name(result)["capture_integrity"].status)

    def test_a_truncated_log_is_a_setup_failure(self):
        result = run(chain_log().rstrip("\n"), accept_slot_scoped_bind=True,
                     accept_event_count_only=True)
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertEqual("fail", by_name(result)["observation_tail_complete"].status)

    def test_an_incomplete_base_report_capture_is_a_setup_failure(self):
        report = {"hitl_report": {"capture_integrity": "COMPLETE", "capture_complete": False,
                                 "setup_transitions_confirmed": True, "issues": []}}
        result = run(chain_log(), base_report=report, accept_slot_scoped_bind=True,
                     accept_event_count_only=True)
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertEqual("fail", by_name(result)["observation_tail_complete"].status)

    def test_unconfirmed_setup_transitions_are_a_setup_failure(self):
        report = {"hitl_report": {"capture_integrity": "COMPLETE", "capture_complete": True,
                                 "setup_transitions_confirmed": False, "issues": []}}
        result = run(chain_log(), base_report=report, accept_slot_scoped_bind=True,
                     accept_event_count_only=True)
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertEqual("fail", by_name(result)["setup_transitions_confirmed"].status)

    def test_an_unexplained_base_report_issue_blocks_a_pass(self):
        """Out-of-range notes are a report verdict, not a proven generator artefact."""
        report = {"hitl_report": {"capture_integrity": "COMPLETE", "capture_complete": True,
                                 "setup_transitions_confirmed": True,
                                 "issues": ["overdub_out_of_range_notes"]}}
        result = run(chain_log(), base_report=report, accept_slot_scoped_bind=True,
                     accept_event_count_only=True)
        self.assertEqual("INSUFFICIENT_EVIDENCE", result.verdict)
        self.assertEqual("unknown", by_name(result)["reported_issues_resolved"].status)
        self.assertIn("operator", by_name(result)["reported_issues_resolved"].detail)

    def test_an_allowlisted_issue_is_recorded_as_a_claim_not_as_evidence(self):
        report = {"hitl_report": {"capture_integrity": "COMPLETE", "capture_complete": True,
                                 "setup_transitions_confirmed": True,
                                 "issues": ["overdub_out_of_range_notes"]}}
        result = run(chain_log(), base_report=report, allow_setup_issues=("overdub_out_of_range_notes",),
                     accept_slot_scoped_bind=True, accept_event_count_only=True)
        self.assertEqual("RETENTION_PARTIAL", result.verdict)
        self.assertEqual(5, result.exit_code)
        self.assertIn("operator claim", by_name(result)["reported_issues_resolved"].detail)

    def test_a_missing_tempo_readback_is_a_setup_failure(self):
        text = "\n".join(l for l in chain_log().splitlines() if "tempo CC feedback" not in l) + "\n"
        result = run(text, accept_slot_scoped_bind=True, accept_event_count_only=True)
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertEqual("fail", by_name(result)["tempo_verified"].status)

    def test_a_narrow_window_is_an_exposure_setup_failure(self):
        result = run(chain_log(release_us=OPEN_US + 40_000), accept_slot_scoped_bind=True,
                     accept_event_count_only=True)
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertEqual("fail", by_name(result)["capacity_denied_window_wide_enough"].status)

    def test_no_finalizer_window_cannot_stage_the_state(self):
        result = run(chain_log(window_us=None), accept_slot_scoped_bind=True,
                     accept_event_count_only=True)
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertEqual("fail", by_name(result)["L1_finalizer_outstanding"].status)

    def test_an_empty_log_is_a_setup_failure(self):
        result = run("")
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertEqual(["telemetry_present"], [c.name for c in result.checks if c.status == "fail"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
