import unittest
from hitl.slot_payload_readback import download_slot, ram_effective, ReadbackError


class ReadbackTests(unittest.TestCase):
    def test_epoch_crc_matches_firmware_continuation_and_256_byte_grains(self):
        from hitl.slot_payload_readback import persisted_epoch_crc
        # Independent bitwise translation of PersistenceSchema::crc32Continue.
        body = bytes(range(256)) * 3 + b'SAVE'
        crc = 0
        for offset in range(0, len(body), 256):
            for byte in body[offset:offset+256]:
                crc ^= byte
                for _ in range(8):
                    crc = (crc >> 1) ^ (0xedb88320 if crc & 1 else 0)
            crc ^= 0xffffffff
        self.assertEqual(persisted_epoch_crc(body), crc)

    def session(self, payload, *, mutate_identity=False, bad_count=False, status='ok'):
        class Session:
            def request_line(self, command, **kwargs):
                _, t, s, off = command.split()
                offset = int(off)
                chunk = payload[offset:offset+64]
                identity = 1 if mutate_identity and offset else 0
                count = len(chunk) + int(bad_count)
                return [f'SLOT_BYTES,{t},{s},{off},{status},{len(payload)},0,0,{identity},0,{count},{chunk.hex()}'], True
        return Session()

    def test_assembles_pages_and_token(self):
        data = bytes(range(120)) + b'SAVE'
        actual, identity = download_slot(self.session(data), 5, 3)
        self.assertEqual(actual, data)
        self.assertEqual(identity[0], len(data))

    def test_rejects_changed_identity(self):
        with self.assertRaisesRegex(ReadbackError, 'changed'):
            download_slot(self.session(b'a'*128+b'SAVE', mutate_identity=True), 5, 3)

    def test_rejects_short_or_bad_token_or_refusal(self):
        for session in [self.session(b'abcdSAVE', bad_count=True), self.session(b'abcdefgh'),
                        self.session(b'abcdSAVE', status='bad_token')]:
            with self.subTest(session=session), self.assertRaises(ReadbackError):
                download_slot(session, 5, 3)

    def test_stopped_ram_identity_uses_channel_and_slot(self):
        lines = ['CAPTURE_CONTENT,begin,track=6,slot=3,length=768',
                 'CAPTURE_CONTENT,event,effective,0,0,144,6,48,100,1', 'CAPTURE_CONTENT,end']
        self.assertEqual(ram_effective(lines,5,3), (768, [(0,144,6,48,100,1)]))
        with self.assertRaises(ReadbackError): ram_effective(lines,5,2)
        with self.assertRaises(ReadbackError): ram_effective(lines[:-1],5,3)

    def test_verification_requires_two_identical_reads_and_exact_event_inventory(self):
        import tempfile
        from unittest.mock import patch
        from types import SimpleNamespace
        from hitl.slot_payload_readback import verify_saved_slot
        ram = ['CAPTURE_CONTENT,begin,track=6,slot=3,length=768',
               'CAPTURE_CONTENT,event,effective,0,0,144,6,48,100,1', 'CAPTURE_CONTENT,end']
        data = b'abcdSAVE'
        identity = (8,0,0,0,0)
        decoded = 'SLOT_DECODE,{"format":"legacy","length":768,"events":[[0,144,6,48,100,1]]}'
        with tempfile.TemporaryDirectory() as output:
            with patch('hitl.slot_payload_readback.download_slot', return_value=(data,identity)), \
                 patch('hitl.slot_payload_readback.subprocess.run', return_value=SimpleNamespace(
                     returncode=0, stdout=decoded, stderr='')):
                result = verify_saved_slot(None,5,3,ram,'decoder',output)
                self.assertTrue(result['stored_payload_verified'])
                self.assertEqual(result['identical_full_reads'],2)
                with self.assertRaises(ReadbackError):
                    verify_saved_slot(None,5,3,[r.replace(',48,',',49,') for r in ram],'decoder',output)
            with patch('hitl.slot_payload_readback.download_slot', side_effect=[(data,identity),(b'efghSAVE',identity)]):
                with self.assertRaisesRegex(ReadbackError,'between_reads'):
                    verify_saved_slot(None,5,3,ram,'decoder',output)

if __name__ == '__main__': unittest.main()
