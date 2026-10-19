"""Fail-closed Record-only seed qualification, without hardware."""
from __future__ import annotations

import copy
import os
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hitl.baseline_loop_inventory import (
    base_report_record_seed_ok, base_report_usable_for_note_edit_sweep,
)
from hitl.control_constants import TICKS_PER_BAR, TICKS_PER_16TH_STEP
from host_midi_automation_edit_baseline import EDIT_RECORD_FIXTURE


def complete_seed_report(serial_path: Path) -> dict:
    count = len(EDIT_RECORD_FIXTURE)
    lines = ["#CAP,100,RECA,0,0", "#CAP,200,RECS,stop,3,0,0,0,1536,1536"]
    lines += [f"#CAP,{201+i},REVT,{n.step*TICKS_PER_16TH_STEP},8,{n.pitch}"
              for i, n in enumerate(EDIT_RECORD_FIXTURE)]
    serial_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return {
        "config": {"record_only": True, "edit_record_fixture": True, "record_bars": 2,
                   "midi_channel": 8},
        "serial_log_path": str(serial_path), "overall_ok": False,
        "hitl_report": {"capture_complete": True, "capture_integrity": "COMPLETE",
                        "stimulus_valid": True},
        "per_track_stats": [{"record_notes_sent": count, "record_clock_pulses_seen": 177}],
        "assertions": {
            "serial_capture_enabled": True, "serial_error": "", "verification_line_count": 12,
            "abort_reason": "", "timed_out": False, "heartbeat_lost": False,
            "precondition_failures": [], "phase_note_failures": [], "phase_activation_failures": [],
            "transition_checks": [
                {"from": "ARMED", "to": "RECORDING", "ok": True},
                {"from": "RECORDING", "to": "STOPPED_RECORDING", "ok": True},
                {"from": "STOPPED_RECORDING", "to": "PLAYING", "ok": False}],
            "record_evidence_drain": {"complete": True, "stopped_confirmed": True,
                "persistence_shutdown": {"status": "complete", "drains": 2, "ok": True}},
            "serial_verification": {
                "cap_sparse": False,
                "boundaries": {"record_start_ts": 100, "record_stop_ts": 200},
                "record_phase": {"phase_missing": False, "note_on_count": count,
                    "note_off_count": count, "unmatched_open_notes": 0,
                    "out_of_range_count": 0, "sequence_mismatch_count": 6},
                "record_loop_length": {"recs_missing": False,
                    "actual_final_length": 2*TICKS_PER_BAR, "final_length_ok": True},
                "persistence_handoff": {"result_ok": True, "first_missing_stage": None,
                    "result": {"timestamp": 300, "outcome": "ok"}},
            },
        },
    }


class SeedQualificationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=os.environ.get('PI_SCRATCH_DIR'))
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / "seed.log"
        self.report = complete_seed_report(self.path)

    def test_complete_seed_with_irrelevant_playing_diagnostic(self):
        self.assertTrue(base_report_record_seed_ok(self.report))
        self.assertTrue(base_report_usable_for_note_edit_sweep(self.report))

    def test_run09_shape(self):
        self.report['hitl_report'].update(capture_complete=False, capture_integrity='INCOMPLETE')
        self.report['assertions']['abort_reason'] = '!TRACE_DRAIN: response fence missing'
        self.report['assertions']['record_evidence_drain']['complete'] = False
        self.assertFalse(base_report_record_seed_ok(self.report))

    def test_extra_bar_even_when_legacy_length_ok(self):
        self.report['assertions']['serial_verification']['record_loop_length']['actual_final_length'] = 3*TICKS_PER_BAR
        self.assertFalse(base_report_record_seed_ok(self.report))

    def test_missing_mandatory_metadata(self):
        for chain in [('hitl_report',), ('assertions', 'record_evidence_drain'),
                      ('assertions', 'serial_verification'), ('config', 'record_bars'),
                      ('assertions', 'abort_reason'), ('assertions', 'timed_out'),
                      ('assertions', 'transition_checks')]:
            with self.subTest(chain=chain):
                report = copy.deepcopy(self.report)
                owner = report
                for key in chain[:-1]: owner = owner[key]
                del owner[chain[-1]]
                self.assertFalse(base_report_record_seed_ok(report))

    def test_host_only_notes_sent(self):
        self.report['assertions']['serial_verification']['record_phase'] = {}
        self.assertFalse(base_report_record_seed_ok(self.report))

    def test_incomplete_drain(self):
        self.report['assertions']['record_evidence_drain']['complete'] = False
        self.assertFalse(base_report_record_seed_ok(self.report))

    def test_cleanup_not_done(self):
        shutdown = self.report['assertions']['record_evidence_drain']['persistence_shutdown']
        for key, value in [('status', 'pending'), ('drains', 1), ('ok', False)]:
            with self.subTest(key=key):
                original = shutdown[key]; shutdown[key] = value
                self.assertFalse(base_report_record_seed_ok(self.report)); shutdown[key] = original

    def test_missing_cleanup(self):
        del self.report['assertions']['record_evidence_drain']['persistence_shutdown']
        self.assertFalse(base_report_record_seed_ok(self.report))

    def test_old_save_result(self):
        self.report['assertions']['serial_verification']['persistence_handoff']['result']['timestamp'] = 99
        self.assertFalse(base_report_record_seed_ok(self.report))

    def test_incomplete_serial_inventory(self):
        phase = self.report['assertions']['serial_verification']['record_phase']
        for key, value in [('note_on_count', 7), ('note_off_count', 7),
                           ('unmatched_open_notes', 1), ('phase_missing', True)]:
            with self.subTest(key=key):
                original = phase[key]; phase[key] = value
                self.assertFalse(base_report_record_seed_ok(self.report)); phase[key] = original

    def test_wrong_fixture_pitch(self):
        self.path.write_text(self.path.read_text().replace(',8,67', ',8,68'))
        self.assertFalse(base_report_record_seed_ok(self.report))

    def test_missing_stored_inventory(self):
        self.path.write_text('#CAP,100,RECA,0,0\n')
        self.assertFalse(base_report_record_seed_ok(self.report))

    def test_no_serial_file(self):
        self.path.unlink()
        self.assertFalse(base_report_record_seed_ok(self.report))

    def test_overall_pass_cannot_bypass_record_qualification(self):
        self.report['overall_ok'] = True
        self.report['hitl_report']['capture_complete'] = False
        self.assertFalse(base_report_usable_for_note_edit_sweep(self.report))

    def test_cached_pass_cannot_bypass(self):
        from hitl.scenarios.edit_minimal import _resolve_base_seed
        self.report['hitl_report']['capture_complete'] = False
        ctx = SimpleNamespace(base_preset_passed=True, base_report=self.report, base_serial_log_path=self.path)
        with patch('hitl.baseline_loop_inventory.latest_base_report', return_value=self.report):
            with self.assertRaises(RuntimeError):
                _resolve_base_seed(Path(self.tmp.name), ctx=ctx)

    def test_base_refuses_incomplete_even_with_exit_zero(self):
        from hitl.scenarios.base import run_base_scenario
        self.report['overall_ok'] = True
        self.report['hitl_report']['capture_complete'] = False
        args = SimpleNamespace(preset='note_edit_reselect_stacked', legacy_args=[], out_dir=Path(self.tmp.name))
        with patch('host_midi_automation_baseline.run', return_value=0), \
             patch('hitl.baseline_loop_inventory.latest_base_report', return_value=self.report), \
             patch('hitl.stimuli.stimulus_for_base_preset', return_value=None):
            self.assertNotEqual(run_base_scenario(args), 0)

    def test_ingress_off_pitch_mismatch_despite_balanced_report(self):
        with self.path.open('a') as serial:
            for i, note in enumerate(EDIT_RECORD_FIXTURE):
                serial.write(f'#CAP,{101+i*2},MI,U,144,8,{note.pitch},100\n')
                serial.write(f'#CAP,{102+i*2},MI,U,128,8,{note.pitch},0\n')
        self.assertTrue(base_report_record_seed_ok(self.report))
        self.path.write_text(self.path.read_text().replace('MI,U,128,8,67,0', 'MI,U,128,8,68,0'))
        self.assertFalse(base_report_record_seed_ok(self.report))


    def test_serial_length_cannot_be_overridden_by_report(self):
        self.path.write_text(self.path.read_text().replace('1536,1536', '2304,2304'))
        self.assertFalse(base_report_record_seed_ok(self.report))

    def test_predecessor_content_cannot_qualify_current_seed(self):
        self.report['assertions']['serial_verification']['boundaries'].update(
            record_start_ts=250, record_stop_ts=275)
        self.assertFalse(base_report_record_seed_ok(self.report))

    def test_valid_cached_seed_is_requalified(self):
        from hitl.scenarios.edit_minimal import _resolve_base_seed
        ctx = SimpleNamespace(base_preset_passed=True, base_report=self.report,
                              base_serial_log_path=self.path)
        lines, _layout, path = _resolve_base_seed(Path(self.tmp.name), ctx=ctx)
        self.assertEqual(path, self.path)
        self.assertTrue(lines)

    def test_record_only_follow_log_is_qualified(self):
        self.report['config']['follow_serial_log'] = self.report.pop('serial_log_path')
        self.assertTrue(base_report_record_seed_ok(self.report))

    def test_full_legacy_overall_pass_preserved(self):
        self.assertTrue(base_report_usable_for_note_edit_sweep({'overall_ok': True, 'config': {}}))


if __name__ == '__main__':
    unittest.main()
