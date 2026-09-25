#!/usr/bin/env python3
"""Unit tests for the capture-side library-observation classifier (Packet A / tasks 1.2 and 1.4).

The expected states and causes are written explicitly here, mirroring the native suite
`test/test_library_observation/test_library_observation.cpp` (L01-L08); they are not read back from the
implementation.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.library_observation import (  # noqa: E402
    Cause,
    SlotInput,
    classify_slot,
    parse_capture_lines,
    parse_observation_lines,
    summarize,
)


def empty_slot_input() -> SlotInput:
    """A slot observed as having nothing at all: absent payload, no content, no request."""
    return SlotInput(
        track=3,
        slot=2,
        payload="absent",
        offered="not_offered",
        request="none",
        content_observed=True,
        has_committed_content=False,
        selection_observed=True,
        required_for_initial_playback=False,
    )


def loss_causes() -> tuple[str, ...]:
    return (
        Cause.CATALOG_OMISSION,
        Cause.RESTORE_REFUSAL,
        Cause.INTENTIONAL_EVICTION,
        Cause.CONTENT_LOSS,
    )


class ClassifierTests(unittest.TestCase):
    def test_l01_empty_with_no_history_is_empty_state_and_unknown_cause(self) -> None:
        result = classify_slot(empty_slot_input())
        self.assertEqual(("empty",), result.current_state)
        self.assertEqual(Cause.UNKNOWN, result.cause)
        self.assertNotIn(result.cause, loss_causes())

    def test_l02_prior_inventory_without_an_event_is_unknown_not_content_loss(self) -> None:
        slot_input = empty_slot_input()
        slot_input.prior_inventory_known = True
        slot_input.prior_populated = True
        result = classify_slot(slot_input)
        self.assertEqual(("empty",), result.current_state)
        self.assertEqual(Cause.UNKNOWN, result.cause)
        self.assertTrue(result.evidence["prior_inventory_disappearance"])
        self.assertNotEqual(Cause.CONTENT_LOSS, result.cause)
        self.assertNotEqual(Cause.INTENTIONAL_EVICTION, result.cause)

    def test_l03_explicit_eviction_event_records_intentional_eviction(self) -> None:
        slot_input = empty_slot_input()
        slot_input.prior_inventory_known = True
        slot_input.prior_populated = True
        slot_input.explicit_eviction_event = True
        result = classify_slot(slot_input)
        self.assertEqual(Cause.INTENTIONAL_EVICTION, result.cause)
        self.assertTrue(result.evidence["explicit_eviction_event"])

    def test_l04_corrupt_payload_and_refused_restore_is_ambiguous(self) -> None:
        slot_input = empty_slot_input()
        slot_input.payload = "invalid_token"
        slot_input.offered = "offered"
        slot_input.request = "terminal"
        slot_input.terminal = "refused"
        slot_input.request_record_exists = True
        result = classify_slot(slot_input)
        self.assertTrue(result.evidence["payload_validation_failed"])
        self.assertTrue(result.evidence["refused_terminal"])
        self.assertEqual(Cause.AMBIGUOUS, result.cause)
        self.assertNotIn("populated", result.current_state)

    def test_l05_outstanding_request_is_pending_and_asserts_no_terminal(self) -> None:
        for request in ("queued", "running", "waiting"):
            slot_input = empty_slot_input()
            slot_input.request = request
            slot_input.request_wait = "waiting_commit_gate"
            slot_input.request_record_exists = True
            slot_input.terminal = "committed"  # a stale terminal must not be reported for a live request
            result = classify_slot(slot_input)
            self.assertIn("pending", result.current_state)
            self.assertNotIn("empty", result.current_state)
            self.assertEqual("none", result.observed["terminal"])
            self.assertEqual(Cause.NOT_APPLICABLE, result.cause)

    def test_l06_two_consistent_causes_are_ambiguous(self) -> None:
        slot_input = empty_slot_input()
        slot_input.payload = "invalid_token"
        slot_input.offered = "not_offered"
        slot_input.prior_inventory_known = True
        slot_input.prior_populated = True
        result = classify_slot(slot_input)
        self.assertEqual(Cause.AMBIGUOUS, result.cause)
        self.assertTrue(result.evidence["payload_validation_failed"])
        self.assertTrue(result.evidence["catalog_omission_candidate"])

    def test_l07_populated_slot_has_a_nullable_cause(self) -> None:
        slot_input = empty_slot_input()
        slot_input.payload = "present_verified"
        slot_input.offered = "offered"
        result = classify_slot(slot_input)
        self.assertIn("populated", result.current_state)
        self.assertEqual(Cause.NOT_APPLICABLE, result.cause)

    def test_unobserved_fields_are_unknown_not_empty(self) -> None:
        result = classify_slot(SlotInput(track=1, slot=1))
        self.assertEqual((), result.current_state)
        self.assertEqual(Cause.UNKNOWN, result.cause)

    def test_l08_four_counts_and_window_are_separate_outputs(self) -> None:
        verified = empty_slot_input()
        verified.track, verified.slot = 0, 0
        verified.payload = "present_verified"
        verified.offered = "offered"
        verified.required_for_initial_playback = True
        verified.has_committed_content = True
        verified.committed_terminal_in_window = True
        verified.unknown_fields = []

        committed_only = empty_slot_input()
        committed_only.track, committed_only.slot = 1, 0
        committed_only.required_for_initial_playback = True
        committed_only.has_committed_content = True

        absent = empty_slot_input()
        absent.track, absent.slot = 2, 0

        results = [classify_slot(s) for s in (verified, committed_only, absent)]
        summary = summarize(
            results,
            {
                "start_us": 1000,
                "end_us": 2000,
                "line_count": 10,
                "admission_complete": False,
                "every_initial_playback_slot_observed": True,
                "consistent_view": True,
                "unknown_fields": [],
            },
        )
        counts = summary["counts"]
        self.assertEqual(1, counts["payload_inventory"])
        self.assertEqual(2, counts["initial_playback_required"])
        self.assertEqual(1, counts["observed_restore_completions"])
        self.assertEqual(2, counts["committed_at_checkpoint"])
        self.assertFalse(summary["complete"])
        self.assertTrue(summary["incomplete_window"])

    def test_unobserved_quantities_are_none_with_the_field_named(self) -> None:
        # The `s13` captures carry no payload token, content dump or selection index, so those
        # quantities must be `None` with the field named — never a silent zero.
        lines = [
            "#CAP,100,RRQ,0,0,0,0,queued,none,none",
            "#CAP,200,RRQ,0,0,0,0,running,none,none",
            "#CAP,300,RRQ,0,0,0,0,terminal,none,committed",
        ]
        analysis = parse_capture_lines(lines)
        counts = analysis["summary"]["counts"]
        self.assertIsNone(counts["payload_inventory"])
        self.assertIsNone(counts["initial_playback_required"])
        self.assertIsNone(counts["committed_at_checkpoint"])
        self.assertEqual(1, counts["observed_restore_completions"])
        self.assertEqual(1, analysis["n_committed_slots_window_scoped"])
        self.assertIn("payload_token", analysis["summary"]["counts_unknown_fields"])
        # The one offered slot completed its restore, but its content field was not observable offline,
        # so its state stays unknown (`[]`, not `empty`) and its cause stays nullable.
        completed = [s for s in analysis["slots"] if s["track"] == 0 and s["slot"] == 0]
        self.assertEqual(1, len(completed))
        self.assertEqual([], completed[0]["state"])
        self.assertEqual(Cause.NOT_APPLICABLE, completed[0]["cause"])
        self.assertIn("committed_content", completed[0]["unknown_fields"])
        # Every other slot is unknown, with its missing fields named — never `empty`.
        unknown = [s for s in analysis["slots"] if s["state"] == [] and s["cause"] == Cause.UNKNOWN]
        self.assertTrue(unknown)
        self.assertTrue(all(s["unknown_fields"] for s in unknown))

    def test_refused_terminal_is_restore_refusal_with_evidence(self) -> None:
        lines = [
            "#CAP,100,RRQ,1,1,0,0,queued,none,none",
            "#CAP,200,RRQ,1,1,0,0,terminal,none,refused",
        ]
        analysis = parse_capture_lines(lines)
        refused = [s for s in analysis["slots"] if s["track"] == 1 and s["slot"] == 1][0]
        self.assertEqual(Cause.RESTORE_REFUSAL, refused["cause"])
        self.assertTrue(refused["evidence"]["refused_terminal"])

    def test_window_is_the_capture_log_and_n_is_window_scoped(self) -> None:
        lines = [
            "#CAP,500,RRQ,0,0,0,0,terminal,none,committed",
            "#CAP,900,RRQ,0,1,0,0,terminal,none,committed",
        ]
        analysis = parse_capture_lines(lines)
        window = analysis["summary"]["window"]
        self.assertEqual(500, window["start_us"])
        self.assertEqual(900, window["end_us"])
        self.assertEqual(2, analysis["n_committed_slots_window_scoped"])
        self.assertIn("window", analysis["n_committed_slots_definition"])

    def test_incomplete_window_is_reported_incomplete(self) -> None:
        lines = ["#CAP,100,RRQ,0,0,0,0,queued,none,none"]
        analysis = parse_capture_lines(lines)
        self.assertTrue(analysis["summary"]["incomplete_window"])
        self.assertFalse(analysis["summary"]["complete"])


class DeviceObservationTests(unittest.TestCase):
    """The device record family is compact (codes, not names): the capture text cap is 192 bytes and the
    first full-name form was silently truncated on device."""

    def test_compact_slot_and_end_records_decode(self) -> None:
        lines = [
            "#CAP,100,LIBOBS,B,a=100,e=0,s=64",
            # t=0,s=0,state=populated(2),cause=not_applicable(n),payload=present_verified(3),
            # offered=offered(2),request=terminal(5),wait=0,terminal=committed(1),gen=0,epoch=0,
            # content=1,pass=1,root=0,root_current=0,passes=1,highpass=4,len=1920,events=88,req=1,in=0,inv=0
            "#CAP,110,LIBOBS,S,t=0,s=0,st=2,c=n,p=3,o=2,q=5,w=0,m=1,g=0,e=0,k=1,kp=1,kr=0,kc=0,a=1,"
            "h=4,l=1920,v=88,n=1,i=0,x=0",
            "#CAP,120,LIBOBS,S,t=0,s=1,st=1,c=u,p=1,o=1,q=1,w=0,m=0,g=0,e=0,k=0,kp=0,kr=0,kc=0,"
            "a=0,h=0,l=0,v=0,n=0,i=0,x=0",
            "#CAP,200,LIBOBS,E,a=100,b=200,t=64,r=0,s=64,p=36,i=8,c=0,m=8,P=8,R=28,n=1,e=1,v=0,x=1",
        ]
        observation = parse_observation_lines(lines)
        self.assertTrue(observation["complete"])
        self.assertTrue(observation["content_representation_observed"])
        self.assertEqual(
            {
                "payload_inventory": 36,
                "initial_playback_required": 8,
                "observed_restore_completions": 0,
                "committed_at_checkpoint": 8,
                "pass_backed_at_checkpoint": 8,
                "root_backed_at_checkpoint": 28,
            },
            observation["counts"],
        )
        self.assertTrue(observation["window"]["admission_complete"])
        self.assertTrue(observation["window"]["every_initial_playback_slot_observed"])
        self.assertFalse(observation["window"]["consistent_view"])
        first = observation["slots"][0]
        self.assertEqual(["populated"], first["state"])
        self.assertEqual("not_applicable", first["cause"])
        self.assertEqual("present_verified", first["payload"])
        self.assertEqual("committed", first["terminal"])
        self.assertTrue(first["required_for_initial_playback"])
        self.assertTrue(first["has_pass_content"])
        self.assertFalse(first["has_root_content"])
        self.assertTrue(first["content_representation_observed"])
        second = observation["slots"][1]
        self.assertEqual(["empty"], second["state"])
        self.assertEqual("unknown", second["cause"])
        self.assertEqual("absent", second["payload"])

    def test_root_backed_slot_decodes_as_content_without_passes(self) -> None:
        """The LSV1 shape: the loaded count covers it and the representation says *why*."""
        lines = [
            "#CAP,100,LIBOBS,B,a=100,e=0,s=64",
            "#CAP,110,LIBOBS,S,t=2,s=1,st=2,c=n,p=3,o=2,q=5,w=0,m=1,g=0,e=0,k=1,kp=0,kr=1,kc=1,a=0,"
            "h=0,l=0,v=0,n=1,i=0,x=0",
            "#CAP,120,LIBOBS,S,t=3,s=2,st=2,c=n,p=3,o=2,q=5,w=0,m=1,g=0,e=0,k=1,kp=0,kr=1,kc=0,a=0,"
            "h=0,l=0,v=0,n=1,i=0,x=0",
            "#CAP,200,LIBOBS,E,a=100,b=200,t=64,r=0,s=64,p=36,i=8,c=0,m=30,P=8,R=28,n=1,e=1,v=0,x=1",
        ]
        observation = parse_observation_lines(lines)
        current = observation["slots"][0]
        self.assertTrue(current["has_committed_content"])
        self.assertFalse(current["has_pass_content"])
        self.assertTrue(current["has_root_content"])
        self.assertTrue(current["root_is_current"])
        # An installed-but-stale root is still content; currency is reported beside it, not instead.
        stale = observation["slots"][1]
        self.assertTrue(stale["has_committed_content"])
        self.assertTrue(stale["has_root_content"])
        self.assertFalse(stale["root_is_current"])
        # The union is never presented without its two auditable parts beside it, and it can never
        # exceed their sum (a slot carrying both is one loaded slot, not two).
        counts = observation["counts"]
        self.assertLessEqual(
            counts["committed_at_checkpoint"],
            counts["pass_backed_at_checkpoint"] + counts["root_backed_at_checkpoint"],
        )
        self.assertGreaterEqual(
            counts["committed_at_checkpoint"],
            max(counts["pass_backed_at_checkpoint"], counts["root_backed_at_checkpoint"]),
        )

    def test_capture_without_representation_keys_reports_not_observed(self) -> None:
        """A pre-correction capture could not separate the two representations.

        It must not be read as "backed by neither": the representation counts are absent rather than
        zero, and the flag says the field was not observed.
        """
        lines = [
            "#CAP,100,LIBOBS,B,a=100,e=0,s=64",
            "#CAP,110,LIBOBS,S,t=0,s=0,st=2,c=n,p=3,o=2,q=5,w=0,m=1,g=0,e=0,k=1,a=1,h=4,l=1920,"
            "v=88,n=1,i=0,x=0",
            "#CAP,200,LIBOBS,E,a=100,b=200,t=64,r=0,s=64,p=36,i=8,c=0,m=8,n=1,e=1,v=0,x=1",
        ]
        observation = parse_observation_lines(lines)
        self.assertFalse(observation["content_representation_observed"])
        self.assertIsNone(observation["counts"]["pass_backed_at_checkpoint"])
        self.assertIsNone(observation["counts"]["root_backed_at_checkpoint"])
        self.assertEqual(8, observation["counts"]["committed_at_checkpoint"])
        slot = observation["slots"][0]
        self.assertFalse(slot["content_representation_observed"])
        self.assertTrue(slot["has_committed_content"])

    def test_missing_end_record_is_incomplete_with_no_counts(self) -> None:
        lines = ["#CAP,100,LIBOBS,B,a=100,e=0,s=64", "#CAP,110,LIBOBS,S,t=0,s=0,st=2,c=n,p=3,o=2,"
                 "q=5,w=0,m=1,g=0,e=0,k=1,a=0,h=0,l=0,v=0,n=0,i=0,x=0"]
        observation = parse_observation_lines(lines)
        self.assertTrue(observation["incomplete_window"])
        self.assertTrue(all(value is None for value in observation["counts"].values()))

    def test_end_record_with_complete_zero_is_incomplete_but_keeps_counts(self) -> None:
        lines = [
            "#CAP,100,LIBOBS,B,a=100,e=0,s=64",
            "#CAP,200,LIBOBS,E,a=100,b=200,t=64,r=0,s=64,p=36,i=8,c=0,m=8,n=0,e=1,v=0,x=0",
        ]
        observation = parse_observation_lines(lines)
        self.assertTrue(observation["incomplete_window"])
        self.assertEqual(8, observation["counts"]["committed_at_checkpoint"])
        self.assertFalse(observation["window"]["admission_complete"])


if __name__ == "__main__":
    unittest.main()
