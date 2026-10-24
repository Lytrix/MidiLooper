#!/usr/bin/env python3
"""4.2 device leg: read back / upload one scratch-slot body over the HITL serial.

The upload protocol is the qualification-only `LOAD_PARITY_ROW` seam in the firmware:

    !BODY_BEGIN <track> <slot> <bytes>
    !BODY_CHUNK <hex>            (<= 16 bytes per line, 48-byte firmware line buffer)
    !BODY_END
    !BODY_ABORT
    !BODY_RESTORE <track> <slot>

`!BODY_END` appends the `SAVE` token and renames the temp file over the slot path. The first
upload parks the original file as `<slot>.legbak`; later uploads keep that backup.

**Evidence artifact only.** The `!BODY_*` commands belong to the temporary `LOAD_PARITY_ROW` seam, which was
removed at 4.2 closeout. On the clean image only `download`/`probe`/`capture` (production `!SLOT_BYTES`) work.

Readback uses the existing read-only `!SLOT_BYTES <track> <slot> <offset>` command.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import time
from pathlib import Path

import serial

DEFAULT_PORT = "/dev/cu.usbmodem154944801"
CHUNK_BYTES = 16


class DeviceLegError(RuntimeError):
    pass


class Link:
    def __init__(self, port: str, timeout_s: float = 10.0) -> None:
        self.port = port
        self.timeout_s = timeout_s
        self.ser = serial.Serial(port, 115200, timeout=0.2, write_timeout=2.0)

    def close(self) -> None:
        self.ser.close()

    def __enter__(self) -> "Link":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def send(self, line: str) -> None:
        self.ser.write((line + "\n").encode("ascii"))
        self.ser.flush()

    def read_line(self, deadline: float) -> str | None:
        buffered = bytearray()
        while time.monotonic() < deadline:
            chunk = self.ser.read(1)
            if not chunk:
                continue
            if chunk == b"\n":
                return buffered.decode("ascii", "replace").rstrip("\r")
            buffered.extend(chunk)
            if len(buffered) > 4096:
                buffered.clear()
        return None

    def request(self, line: str, marker: str, timeout_s: float | None = None,
                attempts: int = 1) -> tuple[str, list[str]]:
        deadline = time.monotonic() + (timeout_s if timeout_s is not None else self.timeout_s)
        seen: list[str] = []
        for _ in range(attempts):
            self.send(line)
            while time.monotonic() < deadline:
                response = self.read_line(deadline)
                if response is None:
                    break
                seen.append(response)
                if marker in response:
                    return response, seen
        raise DeviceLegError(f"no response containing {marker!r} for {line!r}; tail={seen[-6:]}")


def download_slot(link: Link, track: int, slot: int, out_path: Path) -> dict:
    data = bytearray()
    identity = None
    while True:
        offset = len(data)
        response, _ = link.request(f"!SLOT_BYTES {track} {slot} {offset}",
                                   f"SLOT_BYTES,{track},{slot},{offset},", timeout_s=5.0)
        fields = response.split(",")
        if len(fields) != 12:
            raise DeviceLegError(f"malformed SLOT_BYTES: {response!r}")
        status = fields[4]
        if status == "busy":
            time.sleep(0.1)
            continue
        if status != "ok":
            raise DeviceLegError(f"SLOT_BYTES status={status}")
        current = tuple(int(v) for v in fields[5:10])
        count = int(fields[10])
        chunk = bytes.fromhex(fields[11])
        if identity is None:
            identity = current
        elif identity != current:
            raise DeviceLegError("slot payload changed during readback")
        data.extend(chunk)
        if len(data) == current[0]:
            break
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(bytes(data))
    return {
        "track": track,
        "slot": slot,
        "path": str(out_path),
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "identity_fields": {
            "size": identity[0], "body_offset": identity[1], "set_epoch": identity[2],
            "content_identity": identity[3], "root_identity": identity[4],
        },
        "lsv1": data[identity[1]:identity[1] + 4] == b"LSV1",
    }


def upload_body(link: Link, track: int, slot: int, body_path: Path, attempts: int = 30) -> dict:
    body = body_path.read_bytes()
    response = None
    for _ in range(attempts):
        try:
            response, _ = link.request(f"!BODY_BEGIN {track} {slot} {len(body)}",
                                       "BODY,begin,", timeout_s=5.0)
        except DeviceLegError:
            time.sleep(0.5)
            continue
        if ",busy" in response:
            time.sleep(0.5)
            continue
        break
    if response is None or "BODY,begin,ok" not in response:
        raise DeviceLegError(f"upload begin refused: {response!r}")
    for offset in range(0, len(body), CHUNK_BYTES):
        chunk = body[offset:offset + CHUNK_BYTES]
        response, _ = link.request(f"!BODY_CHUNK {chunk.hex()}", "BODY,chunk,", timeout_s=5.0)
        if "BODY,chunk,ok" not in response:
            raise DeviceLegError(f"upload chunk refused at {offset}: {response!r}")
    response, _ = link.request("!BODY_END", "BODY,end,", timeout_s=10.0)
    if "BODY,end,ok" not in response:
        raise DeviceLegError(f"upload end refused: {response!r}")
    return {"track": track, "slot": slot, "path": str(body_path), "bytes": len(body),
            "sha256": hashlib.sha256(body).hexdigest()}


def probe(link: Link, track: int, slot: int) -> dict:
    response, _ = link.request(f"!SLOT_BYTES {track} {slot} 0",
                               f"SLOT_BYTES,{track},{slot},0,", timeout_s=5.0)
    fields = response.split(",")
    return {
        "status": fields[4],
        "size": int(fields[5]),
        "body_offset": int(fields[6]),
        "set_epoch": int(fields[7]),
        "content_identity": int(fields[8]),
        "root_identity": int(fields[9]),
    }


def reboot_and_capture(link: Link, seconds: float, port: str) -> list[str]:
    link.send("!REBOOT")
    time.sleep(0.5)
    link.close()
    deadline = time.monotonic() + seconds
    lines: list[str] = []
    while time.monotonic() < deadline:
        try:
            with Link(port) as capture:
                while time.monotonic() < deadline:
                    line = capture.read_line(deadline)
                    if line is not None:
                        lines.append(line)
        except (serial.SerialException, OSError):
            time.sleep(0.5)
    return lines


def capture_only(port: str, seconds: float, grep: str, out_path: Path | None) -> dict:
    lines: list[str] = []
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            with Link(port) as link:
                while time.monotonic() < deadline:
                    line = link.read_line(deadline)
                    if line is not None:
                        lines.append(line)
        except (serial.SerialException, OSError):
            time.sleep(0.5)
    if out_path is not None:
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    hits = [line for line in lines if grep in line]
    return {"captured_lines": len(lines), "grep": grep, "hits": hits,
            "log": str(out_path) if out_path else None}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=DEFAULT_PORT)
    sub = parser.add_subparsers(dest="command", required=True)

    p_download = sub.add_parser("download")
    p_download.add_argument("--track", type=int, default=7)  # 1-based bench track 8
    p_download.add_argument("--slot", type=int, default=4)  # 1-based bench slot 5
    p_download.add_argument("--out", required=True)

    p_upload = sub.add_parser("upload")
    p_upload.add_argument("--track", type=int, default=7)  # 1-based bench track 8
    p_upload.add_argument("--slot", type=int, default=4)  # 1-based bench slot 5
    p_upload.add_argument("--body", required=True)

    p_probe = sub.add_parser("probe")
    p_probe.add_argument("--track", type=int, default=7)  # 1-based bench track 8
    p_probe.add_argument("--slot", type=int, default=4)  # 1-based bench slot 5

    p_restore = sub.add_parser("restore")
    p_restore.add_argument("--track", type=int, default=7)  # 1-based bench track 8
    p_restore.add_argument("--slot", type=int, default=4)  # 1-based bench slot 5

    p_reboot = sub.add_parser("reboot")
    p_reboot.add_argument("--seconds", type=float, default=20.0)
    p_reboot.add_argument("--grep", default="LOAD,parity")
    p_reboot.add_argument("--out")

    p_capture = sub.add_parser("capture")
    p_capture.add_argument("--seconds", type=float, default=40.0)
    p_capture.add_argument("--grep", default="LOAD,parity")
    p_capture.add_argument("--out")

    args = parser.parse_args()
    result: dict = {"command": args.command}

    if args.command == "reboot":
        with Link(args.port) as link:
            lines = reboot_and_capture(link, args.seconds, args.port)
        if args.out:
            Path(args.out).parent.mkdir(parents=True, exist_ok=True)
            Path(args.out).write_text("\n".join(lines) + "\n", encoding="utf-8")
        hits = [line for line in lines if args.grep in line]
        result.update({"captured_lines": len(lines), "grep": args.grep, "hits": hits,
                       "log": args.out})
        print(json.dumps(result, indent=2))
        return 0

    if args.command == "capture":
        result.update(capture_only(args.port, args.seconds, args.grep,
                                   Path(args.out) if args.out else None))
        print(json.dumps(result, indent=2))
        return 0

    with Link(args.port) as link:
        if args.command == "download":
            result.update(download_slot(link, args.track, args.slot, Path(args.out)))
        elif args.command == "upload":
            result.update(upload_body(link, args.track, args.slot, Path(args.body)))
        elif args.command == "probe":
            result.update(probe(link, args.track, args.slot))
        elif args.command == "restore":
            response, _ = link.request(f"!BODY_RESTORE {args.track} {args.slot}",
                                       "BODY,restore,", timeout_s=10.0)
            result.update({"response": response})
        else:
            raise SystemExit(f"unknown command {args.command}")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
