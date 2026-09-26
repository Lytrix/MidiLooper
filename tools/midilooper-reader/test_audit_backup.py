import struct
import unittest
import audit_backup as a


def epoch_file(body=b'payloadSAVE'):
    return struct.pack('<IHI', 7, 1, a.firmware_crc(body, 256, False)) + body


def revision_file(payload=b'', chunks=0):
    header = bytearray(128)
    header[:8] = b'REVPK02\0'
    struct.pack_into('<HHHHI', header, 8, 1, 128, 2, 3, 7)
    struct.pack_into('<H', header, 28, chunks)
    struct.pack_into('<I', header, 32, len(payload))
    struct.pack_into('<I', header, 36, a.crc(header))
    return bytes(header) + payload + struct.pack('<III', 0x53564f4b,
        a.firmware_crc(payload, 512, True), 140+len(payload))


class AuditTests(unittest.TestCase):
    def test_epoch_corruption_and_truncation(self):
        good = epoch_file()
        self.assertEqual(a.epoch(good)['epoch'], 7)
        for bad in (good[:-1], good[:12], good[:-5]+b'xSAVE'):
            with self.assertRaises(ValueError): a.epoch(bad)

    def test_firmware_crc_is_not_standard_crc(self):
        self.assertNotEqual(a.firmware_crc(b'123456789', 256, False), a.crc(b'123456789'))
        self.assertEqual(a.firmware_crc(b'123456789', 256, False), 0xd202d277)

    def test_revision_checksum_and_boundaries(self):
        good = revision_file(struct.pack('<BBBBI', 1, 0, 0, 0, 3)+b'abc', 1)
        self.assertEqual(a.revision(good)['set_id'], 3)
        for bad in (good[:-1], good[:135]+b'X'+good[136:], good+b'X'):
            with self.assertRaises(ValueError): a.revision(bad)

    def test_valid_checksum_does_not_hide_truncated_chunk(self):
        bad = revision_file(struct.pack('<BBBBI', 1, 0, 0, 0, 9)+b'abc', 1)
        with self.assertRaisesRegex(ValueError, 'truncated chunk'): a.revision(bad)

    def test_chunk_count(self):
        with self.assertRaisesRegex(ValueError, 'chunk count'): a.revision(revision_file(b'', 1))

    def test_metadata_is_not_content_proof(self):
        body = struct.pack('<IIII', 1, 0, 1536, 0)
        self.assertEqual(a.slot_metadata(body)['loop_length_ticks'], 1536)
        self.assertIn('prefix only', a.slot_metadata(body)['format'])

    def test_lsv1_section_boundary(self):
        body = b'LSV1'+struct.pack('<HH', 1, 0)+b'\x0cLoopMetadata'+struct.pack('<I', 32)+b'\0'*32
        self.assertEqual(a.slot_metadata(body)['loop_length_ticks'], 0)
        with self.assertRaises(ValueError): a.slot_metadata(body[:-1])


if __name__ == '__main__':
    unittest.main()
