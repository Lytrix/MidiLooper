"""Offline structural audit. Never opens a device or modifies the input copy.

Layouts: CurrentWorkspaceStorage.cpp, RevisionPackedBlob.cpp,
StorageLoopIoBodyFormatV1.cpp. Checksums are evidence of integrity, NOT loadability.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib


def require(condition, message):
    if not condition:
        raise ValueError(message)


def crc(data):
    return zlib.crc32(data) & 0xffffffff


def firmware_crc(data, grain, seed_first):
    # PersistenceSchema::crc32Continue complements the prior result before update.
    value = 0
    for pos in range(0, len(data), grain):
        seed = 0 if pos == 0 and seed_first else value ^ 0xffffffff
        value = zlib.crc32(data[pos:pos+grain], seed) & 0xffffffff
    return value


def epoch(data):
    require(len(data) >= 14, 'truncated epoch file')
    generation, schema, checksum = struct.unpack_from('<IHI', data)
    require(schema >> 8 == 0, 'unsupported schema major')
    require(data[-4:] == b'SAVE', 'missing SAVE footer')
    require(firmware_crc(data[10:], 256, False) == checksum, 'firmware epoch body CRC mismatch')
    return {'epoch': generation, 'schema': schema}


def slot_metadata(body):
    if body.startswith(b'LSV1'):
        require(len(body) >= 8, 'truncated LSV1 header')
        require(struct.unpack_from('<H', body, 4)[0] == 1, 'unknown LSV1 version')
        pos, sections = 8, {}
        while pos < len(body):
            n = body[pos]; pos += 1
            require(0 < n <= 64 and pos + n + 4 <= len(body), 'invalid section name')
            name = body[pos:pos+n].decode('ascii'); pos += n
            size = struct.unpack_from('<I', body, pos)[0]; pos += 4
            require(pos + size <= len(body), 'truncated section')
            require(name not in sections, 'duplicate section')
            sections[name] = body[pos:pos+size]; pos += size
        require('LoopMetadata' in sections and len(sections['LoopMetadata']) == 32,
                'missing or malformed metadata')
        body = sections['LoopMetadata']
        kind = 'LSV1'
    else:
        kind = 'raw (metadata prefix only)'
    require(len(body) >= 16, 'truncated loop metadata')
    loop_id, start, length, loop_start = struct.unpack_from('<IIII', body)
    return {'format': kind, 'loop_id': loop_id, 'loop_length_ticks': length}


def revision(data):
    require(len(data) >= 140, 'truncated revision')
    require(data[:8] == b'REVPK02\0', 'revision magic')
    schema, size, rev, set_id, source_epoch = struct.unpack_from('<HHHHI', data, 8)
    require(size == 128 and schema >> 8 == 0, 'revision header format')
    count = struct.unpack_from('<H', data, 28)[0]
    payload_size, checksum = struct.unpack_from('<II', data, 32)
    header = bytearray(data[:128]); header[36:40] = b'\0'*4
    require(crc(header) == checksum, 'revision header CRC mismatch')
    require(len(data) == 128 + payload_size + 12, 'revision size mismatch')
    token, payload_crc, file_size = struct.unpack_from('<III', data, len(data)-12)
    require(token == 0x53564f4b and file_size == len(data), 'revision footer mismatch')
    payload = data[128:-12]
    require(firmware_crc(payload, 512, True) == payload_crc, 'firmware revision payload CRC mismatch')
    pos, chunks, slots = 0, 0, []
    while pos < len(payload):
        require(pos + 8 <= len(payload), 'truncated chunk header')
        kind, track, slot, _, length = struct.unpack_from('<BBBBI', payload, pos)
        pos += 8
        require(pos + length <= len(payload), 'truncated chunk body')
        if kind == 5:
            require(track < 8 and slot < 8, 'invalid slot identity')
            require((track, slot) not in [(s['track'], s['slot']) for s in slots], 'duplicate slot chunk')
            slots.append({'track': track, 'slot': slot, **slot_metadata(payload[pos:pos+length])})
        pos += length; chunks += 1
    require(chunks == count, 'chunk count mismatch')
    return {'set_id': set_id, 'revision_id': rev, 'source_epoch': source_epoch,
            'created_unix': struct.unpack_from('<Q', data, 20)[0],
            'slot_chunks': len(slots), 'nonzero_length_slots': sum(s['loop_length_ticks'] > 0 for s in slots),
            'slots': slots}


def audit(root):
    manifest = json.loads((root / 'manifest.json').read_text())
    require(manifest['status'] == 'complete', 'incomplete download')
    report = {'scope': 'structural integrity only; no restore or content-equivalence verdict', 'files': []}
    for entry in manifest['files']:
        local = (root / entry['local_path']).resolve()
        require(local.is_relative_to(root.resolve()), 'manifest path escapes backup')
        data = local.read_bytes()
        require(len(data) == entry['bytes'] and hashlib.sha256(data).hexdigest() == entry['sha256']
                and f'{crc(data):08x}' == entry['crc32'], 'transfer integrity: ' + entry['path'])
        path = entry['path']
        row = {'path': path, 'local_path': entry['local_path'], 'bytes': len(data)}
        try:
            if data.startswith(b'REVPK02\0'):
                row['kind'] = 'revision'
                row.update(revision(data))
            elif '/slots/loop_' in path or 'runtime.bundle.bin' in path:
                row['kind'] = 'epoch_container'
                row.update(epoch(data))
                if '/slots/loop_' in path:
                    row.update(slot_metadata(data[10:-4]))
            elif path.endswith('/workspace.bin'):
                row['kind'] = 'workspace'
                require(len(data) == 77, 'workspace size mismatch')
                require(crc(data[:-4] + b'\0'*4) == struct.unpack_from('<I', data, 73)[0], 'workspace CRC mismatch')
                row['epoch'] = struct.unpack_from('<I', data, 2)[0]
            else:
                row['status'] = 'not_inspected'
            row.setdefault('status', 'structural_checks_pass')
        except (ValueError, struct.error, UnicodeError) as exc:
            row.update(status='rejected', reason=str(exc))
        report['files'].append(row)
    report['transfer_verified_entries'] = len(report['files'])
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('backup', type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.backup), indent=2))
