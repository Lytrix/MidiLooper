import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
import zlib

from read_midilooper import receive, safe_path, TransferError, SerialLines


def stream(data=b'hello\x00\xff', path='current/slot.bin'):
    crc = zlib.crc32(data)
    payload = f'DATA {data.hex()}\n' if data else ''
    return (f'BEGIN MLREADER2\nDIR {"current".encode().hex()}\n'
            f'FILE {path.encode().hex()} {len(data)}\n{payload}'
            f'END {crc:08x}\nDONE 1 {len(data)}\n').encode()


class ReaderTests(unittest.TestCase):
    def test_block_reader_handles_coalesced_and_split_lines(self):
        class Port:
            def __init__(self):
                self.chunks = [b'one\ntw', b'o\nthree\n']
            @property
            def in_waiting(self):
                return len(self.chunks[0]) if self.chunks else 0
            def read(self, n):
                return self.chunks.pop(0) if self.chunks else b''
        lines = SerialLines(Port())
        self.assertEqual([lines(), lines(), lines()], [b'one\n', b'two\n', b'three\n'])
        with self.assertRaises(TransferError):
            lines()

    def run_stream(self, raw):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'backup'
            result = receive(io.BytesIO(raw).readline, out)
            return result, (out / 'MidiLooper/current/slot.bin').read_bytes()

    def test_binary_roundtrip_and_hash(self):
        result, data = self.run_stream(stream())
        self.assertEqual(data, b'hello\x00\xff')
        self.assertEqual(result['status'], 'complete')
        self.assertEqual(result['files'][0]['sha256'], hashlib.sha256(data).hexdigest())

    def test_empty_file(self):
        result, data = self.run_stream(stream(b''))
        self.assertEqual(data, b'')
        self.assertEqual(result['bytes'], 0)

    def test_corrupt_truncated_error_and_totals_fail_closed(self):
        variants = [stream().replace(b'68656c6c6f', b'68656c6c6e'),
                    stream().split(b'DONE')[0], stream().replace(b'DONE 1', b'DONE 2'),
                    b'ERROR sd_unavailable\n', stream().replace(b'END ', b'ERROR ')]
        for raw in variants:
            with self.subTest(raw=raw), tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp) / 'backup'
                with self.assertRaises(TransferError):
                    receive(io.BytesIO(raw).readline, out)
                self.assertEqual(json.loads((out / 'manifest.json').read_text())['status'], 'incomplete')

    def test_path_escape_rejected(self):
        for path in ('../elsewhere', '/elsewhere', 'x/../y', 'x//y', 'x\\y', 'x\x00y'):
            with self.subTest(path=path), self.assertRaises(TransferError):
                safe_path(path.encode().hex())

    def test_existing_destination_and_symlink_not_used(self):
        with tempfile.TemporaryDirectory() as tmp:
            actual = Path(tmp) / 'actual'
            actual.mkdir()
            link = Path(tmp) / 'link'
            link.symlink_to(actual)
            for out in (actual, link):
                with self.assertRaises(FileExistsError):
                    receive(io.BytesIO(stream()).readline, out)
            self.assertEqual(list(actual.iterdir()), [])

    def test_duplicate_file_rejected(self):
        raw = stream().replace(b'DONE 1 7\n', b'FILE 63757272656e742f736c6f742e62696e 0\n')
        with self.assertRaises(TransferError):
            self.run_stream(raw)

    def test_duplicate_file_can_be_preserved_without_overwriting(self):
        raw = stream().replace(b'DONE 1 7\n',
            b'FILE 63757272656e742f736c6f742e62696e 0\nEND 00000000\nDONE 2 7\n')
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'copy'
            result = receive(io.BytesIO(raw).readline, out, preserve_duplicates=True)
            self.assertEqual(result['status'], 'complete')
            self.assertEqual(result['duplicate_paths'], ['current/slot.bin'])
            self.assertEqual((out / 'MidiLooper/current/slot.bin').read_bytes(), b'hello\x00\xff')
            self.assertEqual((out / result['files'][1]['local_path']).read_bytes(), b'')

    def test_byte_limit(self):
        with tempfile.TemporaryDirectory() as tmp, self.assertRaises(TransferError):
            receive(io.BytesIO(stream()).readline, Path(tmp) / 'copy', max_bytes=1)


if __name__ == '__main__':
    unittest.main()
