"""Bounded host SD readback. RAM content and SAVE token alone never authorize a reboot."""
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess
import time
import struct
import zlib


class ReadbackError(RuntimeError):
    pass


def persisted_epoch_crc(body):
    """Match CurrentWorkspaceStorage's 256-byte file CRC walk exactly.

    PersistenceSchema::crc32Continue complements its input before the standard
    CRC update. This existing wire convention is not zlib.crc32(whole_body).
    Changing the firmware convention/migrating files is outside this observer.
    """
    crc = 0
    for offset in range(0, len(body), 256):
        crc = zlib.crc32(body[offset:offset + 256], crc ^ 0xffffffff)
    return crc


def download_slot(session, track, slot, *, timeout_s=30, monotonic=time.monotonic, sleep=time.sleep):
    deadline = monotonic() + timeout_s
    data = bytearray()
    identity = None
    while monotonic() < deadline:
        offset = len(data)
        marker = f"SLOT_BYTES,{track},{slot},{offset},"
        lines, answered = session.request_line(f"!SLOT_BYTES {track} {slot} {offset}",
                                               marker=marker, timeout_s=min(2, max(.01, deadline-monotonic())))
        if not answered:
            raise ReadbackError("payload_page_unanswered")
        line = next(l[l.index(marker):] for l in lines if marker in l)
        fields = line.split(',')
        if len(fields) != 12:
            raise ReadbackError("payload_page_malformed")
        if fields[4] == 'busy':
            sleep(.1)
            continue
        if fields[4] != 'ok':
            raise ReadbackError('payload_' + fields[4])
        try:
            current = tuple(int(v) for v in fields[5:10])  # size, body offset, epoch, content, root
            count = int(fields[10])
            chunk = bytes.fromhex(fields[11])
        except ValueError as exc:
            raise ReadbackError('payload_page_malformed') from exc
        size, body_offset, *_ = current
        if not (4 < size <= 2*1024*1024 and 0 <= body_offset < size-4
                and 0 < count <= 64 and count == len(chunk) and offset+count <= size):
            raise ReadbackError('payload_page_bounds')
        if identity is None:
            identity = current
        elif identity != current:
            raise ReadbackError('payload_changed_during_read')
        data.extend(chunk)
        if len(data) == size:
            if data[-4:] != b'SAVE':
                raise ReadbackError('payload_bad_token')
            return bytes(data), identity
    raise ReadbackError('payload_read_deadline')


def ram_effective(lines, track, slot):
    begin = None
    rows = []
    ended = False
    for line in lines:
        if 'CAPTURE_CONTENT,' not in line:
            continue
        parts = line[line.index('CAPTURE_CONTENT,'):].split(',')
        if parts[1] == 'begin':
            begin = dict(p.split('=') for p in parts[2:])
        elif parts[1] == 'event' and parts[2] == 'effective':
            rows.append(tuple(int(p) for p in parts[4:10]))
        elif parts[1] == 'end':
            ended = True
    if not ended or not begin or int(begin['track']) != track+1 or int(begin['slot']) != slot:
        raise ReadbackError('ram_dump_identity_or_completion')
    return int(begin['length']), rows


def verify_saved_slot(session, track, slot, ram_lines, decoder, out_dir):
    first, identity = download_slot(session, track, slot)
    second, identity2 = download_slot(session, track, slot)
    if first != second or identity != identity2:
        raise ReadbackError('payload_changed_between_reads')
    if identity[1] == 10:
        stored_crc = struct.unpack_from('<I', first, 6)[0]
        if persisted_epoch_crc(first[10:]) != stored_crc:
            raise ReadbackError('payload_epoch_crc_mismatch')
    elif identity[1] != 0:
        raise ReadbackError('payload_unknown_envelope')
    out_dir = Path(out_dir)
    (out_dir/'slot-payload.bin').write_bytes(first)
    body_path = out_dir/'slot-body.bin'
    body_path.write_bytes(first[identity[1]:-4])
    decoded_process = subprocess.run([str(Path(decoder).resolve()), '--decode-slot-body', str(body_path.resolve())],
                                    capture_output=True, text=True, timeout=30)
    (out_dir/'slot-decoder.log').write_text(decoded_process.stdout + decoded_process.stderr)
    if decoded_process.returncode:
        raise ReadbackError('payload_decode_failed')
    decoded = next((json.loads(l.removeprefix('SLOT_DECODE,')) for l in decoded_process.stdout.splitlines()
                    if l.startswith('SLOT_DECODE,')), None)
    if decoded is None:
        raise ReadbackError('payload_decoder_result_missing')
    length, expected = ram_effective(ram_lines, track, slot)
    actual = [tuple(r) for r in decoded['events']]
    if not expected or decoded['length'] != length or Counter(actual) != Counter(expected):
        raise ReadbackError('stored_events_differ_from_recorded_ram')
    result = dict(stored_payload_verified=True, event_count=len(actual), length=length,
                  format=decoded['format'], sha256=hashlib.sha256(first).hexdigest(),
                  identity=list(identity), identical_full_reads=2, events=decoded['events'])
    (out_dir/'stored-payload-verification.json').write_text(json.dumps(result, indent=2))
    return result
