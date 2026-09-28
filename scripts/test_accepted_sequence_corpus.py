"""Historical device failures remain non-passes without inventing missing identity."""
import hashlib
import json
from pathlib import Path
import unittest

from hitl.accepted_sequence_probe import analyse
from test_accepted_sequence import BASE_REPORT, METADATA, chain_log

FIXTURES = Path(__file__).parent / 'hitl' / 'fixtures' / 'accepted_sequence'


class AcceptedSequenceCorpus(unittest.TestCase):
    def test_attributed_device_excerpts_do_not_pass(self):
        for item in json.loads((FIXTURES / 'manifest.json').read_text()):
            with self.subTest(source=item['source']):
                data = (FIXTURES / item['file']).read_bytes()
                self.assertEqual(item['excerpt_sha256'], hashlib.sha256(data).hexdigest())
                self.assertEqual(len(item['source_lines']), len(data.splitlines()))
                result = analyse(data.decode(), leg=item['leg'], slot_a=item['slot_a'],
                                 slot_b=item['slot_b'], metadata=item['metadata'],
                                 base_report=item['base_report'])
                self.assertEqual(item['expected'], result.verdict)
                self.assertNotEqual(0, result.exit_code)

    def test_record_telemetry_cannot_prove_overdub_even_with_claimed_metadata(self):
        # A seed's record events must never stand in for live overdub intervals.
        result = analyse(chain_log(), leg='od', slot_a=4, slot_b=5,
                         metadata=METADATA, base_report=BASE_REPORT)
        self.assertNotEqual(0, result.exit_code)
        self.assertTrue(any(c.name == 'overdub_interval_identity' and c.status == 'unknown'
                            for c in result.checks))

    def test_contaminated_or_incomplete_synthetic_chains_do_not_pass(self):
        cases = {
            'wrong_slot': chain_log().replace('RECA,4,', 'RECA,6,'),
            'duplicate_stop': chain_log(stops_b=2),
            # These legacy rows carry no track, interval or run identity. They cannot
            # establish a current same-track sequence, even if a boot line exists.
            'wrong_track_unattributable': chain_log(),
            'stale_run_unattributable': chain_log(),
            'missing_boot': chain_log(boot=False),
        }
        for name, text in cases.items():
            with self.subTest(case=name):
                result = analyse(text, leg='rec', slot_a=4, slot_b=5,
                                 metadata=METADATA, base_report=BASE_REPORT)
                self.assertNotEqual(0, result.exit_code)
        result = analyse(chain_log(), leg='rec', slot_a=4, slot_b=5)
        self.assertNotEqual(0, result.exit_code)


if __name__ == '__main__':
    unittest.main()
