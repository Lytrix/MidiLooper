#!/usr/bin/env python3
"""Probe a loop slot file for LSV1 LoopSnapshot body format (OpenSpec 5.4.2)."""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

SAVE_TOKEN = b"SAVE"
LSV1_MAGIC = b"LSV1"
# CurrentWorkspaceStorage::kEpochFileHeaderByteSize — epoch wrapper before slot body.
EPOCH_FILE_HEADER_BYTE_SIZE = 10


def find_payload_offset(data: bytes) -> int:
    """Return LSV1 body offset: 0 (bare body) or after epoch header when wrapped."""
    if len(data) < 4:
        return 0
    if data[:4] == LSV1_MAGIC:
        return 0
    if (
        len(data) >= EPOCH_FILE_HEADER_BYTE_SIZE + 4
        and data[EPOCH_FILE_HEADER_BYTE_SIZE : EPOCH_FILE_HEADER_BYTE_SIZE + 4] == LSV1_MAGIC
    ):
        return EPOCH_FILE_HEADER_BYTE_SIZE

    # Defensive: scan for first LSV1 before SAVE trailer (tolerates header drift).
    body_end = len(data) - len(SAVE_TOKEN) if data.endswith(SAVE_TOKEN) else len(data)
    idx = data.find(LSV1_MAGIC, 0, body_end)
    return idx if idx >= 0 else 0


def probe_slot_file(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    if len(data) < 4:
        return {"ok": False, "error": "file_too_short", "size": len(data)}

    if data[-4:] != SAVE_TOKEN:
        return {"ok": False, "error": "missing_save_token", "size": len(data)}

    offset = find_payload_offset(data)
    payload = data[offset:-4]
    if len(payload) < 8:
        return {
            "ok": False,
            "error": "payload_too_short",
            "size": len(data),
            "payload_offset": offset,
        }

    magic = payload[:4]
    if magic != LSV1_MAGIC:
        return {
            "ok": True,
            "format": "legacy",
            "size": len(data),
            "payload_offset": offset,
            "magic": magic.decode("ascii", errors="replace"),
        }

    format_version, flags = struct.unpack_from("<HH", payload, 4)
    return {
        "ok": True,
        "format": "lsv1",
        "size": len(data),
        "payload_offset": offset,
        "format_version": format_version,
        "flags": flags,
        "supported": format_version == 1,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, help="loop_TT_SS.bin from SD (MidiLooper/current/slots/)")
    args = parser.parse_args()

    if not args.path.is_file():
        print(f"error: not a file: {args.path}", file=sys.stderr)
        return 2

    result = probe_slot_file(args.path)
    path = args.path
    if not result.get("ok"):
        print(f"FAIL {path}: {result}")
        return 1

    fmt = result["format"]
    if fmt == "lsv1":
        status = "PASS" if result.get("supported") else "FAIL"
        print(
            f"{status} {path}: LSV1 formatVersion={result['format_version']} "
            f"flags=0x{result['flags']:04x} offset={result['payload_offset']} "
            f"size={result['size']}"
        )
        return 0 if result.get("supported") else 1

    print(
        f"LEGACY {path}: magic={result['magic']} offset={result['payload_offset']} "
        f"size={result['size']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
