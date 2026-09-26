#!/usr/bin/env python3
"""Copy only /MidiLooper from the standalone USB reader; never uploads or resets."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zlib


class TransferError(RuntimeError):
    pass


class SerialLines:
    """Read USB in blocks; pyserial's byte-at-a-time readline can starve Teensy TX."""

    def __init__(self, serial_port):
        self.port = serial_port
        self.buffer = bytearray()

    def __call__(self):
        while True:
            end = self.buffer.find(b'\n')
            if end >= 0:
                line = bytes(self.buffer[:end + 1])
                del self.buffer[:end + 1]
                return line
            if len(self.buffer) > 2048:
                raise TransferError('Oversized protocol line')
            data = self.port.read(max(1, min(self.port.in_waiting, 65536)))
            if not data:
                raise TransferError('USB read timed out or disconnected')
            self.buffer.extend(data)


def safe_path(encoded: str) -> Path:
    try:
        name = bytes.fromhex(encoded).decode('utf-8')
    except (ValueError, UnicodeError) as exc:
        raise TransferError('Invalid filename encoding') from exc
    parts = name.split('/')
    if (not name or PurePosixPath(name).is_absolute() or
            any(p in ('', '.', '..') for p in parts) or '\\' in name or
            any(ord(c) < 32 or ord(c) == 127 for c in name)):
        raise TransferError('Unsafe relative filename')
    return Path(*parts)


def receive(readline, output: Path, max_bytes: int = 1024**3, preserve_duplicates: bool = False) -> dict:
    """Strict streaming decoder. Completion requires file CRCs and matching final totals.

    Output must be new: no pre-existing paths/symlinks can redirect or overwrite files.
    Partial files and manifest stay marked incomplete after any failure.
    """
    output.mkdir(parents=True, exist_ok=False)
    root = output / 'MidiLooper'
    root.mkdir()
    manifest = {'status': 'incomplete', 'source': '/MidiLooper', 'files': [], 'bytes': 0,
                'duplicate_paths': []}
    manifest_path = output / 'manifest.json'

    def save():
        manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')

    def line():
        raw = readline()
        if not raw or not raw.endswith(b'\n') or len(raw) > 2048:
            raise TransferError('Transfer timed out, disconnected, or sent an incomplete line')
        try:
            text = raw.decode('ascii').strip()
        except UnicodeError as exc:
            raise TransferError('Non-protocol data from device') from exc
        if text.startswith('ERROR '):
            raise TransferError(text)
        return text.split()

    save()
    try:
        if line() != ['BEGIN', 'MLREADER2']:
            raise TransferError('Missing reader transfer header')
        seen = set()
        while True:
            row = line()
            if len(row) == 3 and row[0] == 'DONE':
                if [int(row[1]), int(row[2])] != [len(manifest['files']), manifest['bytes']]:
                    raise TransferError('Final file/byte totals disagree')
                manifest['status'] = 'complete'
                save()
                return manifest
            if not row or row[0] not in ('DIR', 'FILE') or len(row) != (2 if row[0] == 'DIR' else 3):
                raise TransferError('Unexpected transfer record')
            rel = safe_path(row[1])
            duplicate = rel in seen
            if duplicate and (row[0] == 'DIR' or not preserve_duplicates):
                raise TransferError(f'Duplicate path: {rel}')
            seen.add(rel)
            path = root / rel
            if not path.parent.is_dir():
                raise TransferError('Parent directory was not declared')
            if row[0] == 'DIR':
                path.mkdir(exist_ok=False)
                continue
            if len(manifest['files']) >= 100000:
                raise TransferError('Too many entries; possible directory corruption')
            if duplicate:
                # Preserve ambiguous on-card entries separately, never overwrite the first.
                manifest['duplicate_paths'].append(rel.as_posix())
                extra = output / 'duplicate_entries'
                extra.mkdir(exist_ok=True)
                path = extra / f'{len(manifest["files"]):06d}.bin'
            length = int(row[2])
            if length < 0 or manifest['bytes'] + length > max_bytes:
                raise TransferError('Download exceeds the configured byte limit')
            crc = 0
            sha = hashlib.sha256()
            received = 0
            # The outer manifest remains incomplete until every file and DONE validates.
            with path.open('xb') as file:
                while True:
                    data_row = line()
                    if len(data_row) == 2 and data_row[0] == 'END':
                        if received != length or int(data_row[1], 16) != crc:
                            raise TransferError(
                                f'{rel}: received {received}/{length} bytes; '
                                f'CRC {crc:08x}, device {data_row[1]}')
                        break
                    if len(data_row) != 2 or data_row[0] != 'DATA':
                        raise TransferError('Expected file data or checksum')
                    data = bytes.fromhex(data_row[1])
                    received += len(data)
                    if not data or len(data) > 256 or received > length:
                        raise TransferError('Invalid data chunk length')
                    file.write(data)
                    sha.update(data)
                    crc = zlib.crc32(data, crc)
            manifest['files'].append({'path': rel.as_posix(), 'bytes': length,
                                      'local_path': path.relative_to(output).as_posix(),
                                      'sha256': sha.hexdigest(), 'crc32': f'{crc:08x}'})
            manifest['bytes'] += length
            save()
            print(f'{rel}: {length:,} bytes')
    except Exception as exc:
        manifest['error'] = str(exc)
        save()
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='USB serial port running the standalone reader')
    parser.add_argument('--output', type=Path, required=True, help='New local backup directory')
    parser.add_argument('--max-mib', type=int, default=1024, help='Download limit, default 1024 MiB')
    parser.add_argument('--preserve-duplicate-files', action='store_true',
                        help='Preserve duplicate directory entries under duplicate_entries; record the ambiguity')
    parser.add_argument('--reader-installed', action='store_true', required=True,
                        help='Confirm approved standalone reader firmware is installed; never use on looper firmware')
    args = parser.parse_args()
    if args.output.exists() or args.max_mib <= 0:
        parser.error('Output must not exist, and --max-mib must be positive')
    import serial
    # No 134-baud reset, upload, transport gesture, or incident-latch manipulation.
    # Opening the port is only permitted after the reader was deliberately installed.
    ser = serial.Serial(port=None, baudrate=115200, timeout=10, write_timeout=10)
    # Teensy Serial's connection predicate requires DTR. This is normal 115200-baud
    # reader communication, not the bootloader's 134-baud reboot request.
    ser.dtr = True
    ser.rts = False
    ser.port = args.port
    try:
        ser.open()
        ser.reset_input_buffer()
        lines = SerialLines(ser)
        ser.write(b'MLREADER_HELLO\n')
        if lines() != b'MLREADER2 READ_ONLY /MidiLooper\r\n':
            raise TransferError('Wrong firmware or missing handshake; export was not requested')
        ser.write(b'MLREADER_EXPORT\n')
        def acknowledged_line():
            raw = lines()
            ser.write(b'OK\n')
            return raw
        result = receive(acknowledged_line, args.output, args.max_mib * 1024**2,
                         args.preserve_duplicate_files)
        print(f"Complete: {len(result['files'])} files, {result['bytes']:,} bytes -> {args.output}")
        if result['duplicate_paths']:
            print(f"Warning: {len(result['duplicate_paths'])} duplicate file paths preserved separately; see manifest")
    except (TransferError, OSError, ValueError, serial.SerialException) as exc:
        parser.exit(1, f'Reader failed: {exc}\n')
    finally:
        ser.close()


if __name__ == '__main__':
    main()
