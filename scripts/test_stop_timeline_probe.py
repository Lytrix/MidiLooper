import unittest
from hitl_stop_timeline_probe import fresh_capture_lines, emission_evidence_complete, emission_deadlines_met, normal_stop_cleanup_count


class FreshRunEvidenceTests(unittest.TestCase):
    def test_visible_playing_and_transport_recovery_do_not_count_as_normal_cleanup(self):
        row = dict(valid=True, track=4, slot=0, phase=10, finished_phase=1,
                   destination=0, start_us=100)
        timeline = {"states": [{"from_state": 7, "to_state": 5}], "grains": []}
        self.assertEqual(normal_stop_cleanup_count(timeline, 4, 0), 0)
        timeline["grains"] = [row, dict(row), dict(row, start_us=200, destination=1),
                              dict(row, start_us=300, finished_phase=0),
                              dict(row, start_us=400, valid=False), dict(row, start_us=500, track=3)]
        self.assertEqual(normal_stop_cleanup_count(timeline, 4, 0), 1)
        timeline["grains"].append(dict(row, start_us=600))
        self.assertEqual(normal_stop_cleanup_count(timeline, 4, 0), 2)

    def test_emission_gate_rejects_empty_one_sided_and_uncorrelated_traces(self):
        timeline = {"correlation_complete": True, "state_correlation_complete": True, "emissions": []}
        self.assertFalse(emission_evidence_complete(timeline, 4, 0))
        timeline["emissions"] = [{"kind": "on", "valid": True, "track": 4, "slot": 0}]
        self.assertFalse(emission_evidence_complete(timeline, 4, 0))
        timeline["emissions"].append({"kind": "off", "valid": True, "track": 4, "slot": 0})
        self.assertTrue(emission_evidence_complete(timeline, 4, 0))
        self.assertFalse(emission_evidence_complete(timeline, 3, 0))
        self.assertFalse(emission_evidence_complete(timeline, 4, 1))
        timeline["correlation_complete"] = False
        self.assertFalse(emission_evidence_complete(timeline, 4, 0))

    def test_complete_correlation_does_not_imply_deadline_success(self):
        timeline = {"correlation_complete": True, "state_correlation_complete": True,
                    "emissions": [{"kind": kind, "valid": True, "track": 4, "slot": 0,
                                   "lateness_us": 231121} for kind in ("on", "off")]}
        self.assertTrue(emission_evidence_complete(timeline, 4, 0))
        self.assertFalse(emission_deadlines_met(timeline, 4, 0))
        for row in timeline["emissions"]:
            row["lateness_us"] = 0
        self.assertTrue(emission_deadlines_met(timeline, 4, 0))

    def test_excludes_old_events_printed_after_fence(self):
        old = "#CAP,500,DIAG,stop_state,4,0,6,7,0,90,1,80,100,20"
        new = "#CAP,501,DIAG,stop_state,4,0,6,7,0,110,2,100,120,20"
        self.assertEqual(fresh_capture_lines([old, new], 100), [new])

    def test_rebuild_uses_occurrence_time_and_accepts_legacy_rows(self):
        old = "#CAP,500,DIAG,playback_build,snapshot,10,0,768,1,4,0,1,90,4294967295,290,256"
        new = old.replace(",90,", ",110,")
        legacy = "#CAP,502,DIAG,playback_build,snapshot,10,0,768,1,4,0,1"
        self.assertEqual(fresh_capture_lines([old, new, legacy], 100), [new, legacy])

    def test_device_micros_wrap_is_supported(self):
        old = "#CAP,4294967200,ST,Track,OVERDUBBING,OVERDUB_STOPPING"
        new = "#CAP,10,ST,Track,OVERDUB_STOPPING,PLAYING"
        self.assertEqual(fresh_capture_lines([old, new], 4294967250), [new])

    def test_missing_fence_cannot_score_evidence(self):
        self.assertEqual(fresh_capture_lines(["#CAP,10,ST,Track,PLAYING,STOPPED"], None), [])


if __name__ == "__main__":
    unittest.main()
