#!/usr/bin/env python3
"""Verify deferred boot slot restore timing from capture-serial logs."""

from __future__ import annotations

import argparse
import glob
import re
import sys
from pathlib import Path


def resolve_log_path(raw: str, repo_root: Path) -> Path:
    cleaned = raw.strip()
    if cleaned.startswith("@"):
        cleaned = cleaned[1:]
    path = Path(cleaned)
    if not path.is_absolute():
        path = repo_root / path
    return path


def resolve_log_argument(raw: str | None, follow_current: bool, repo_root: Path) -> Path:
    if follow_current:
        pointer = repo_root / "captures" / ".current_session"
        if not pointer.is_file():
            raise FileNotFoundError(f"current session pointer not found: {pointer}")
        current = pointer.read_text(encoding="utf-8").strip().splitlines()[0].strip()
        return resolve_log_path(current, repo_root)

    if raw is None:
        raise ValueError("log path required unless --follow-current-session is set")

    if any(ch in raw for ch in ("*", "?", "[")):
        matches = sorted(glob.glob(raw))
        if not matches:
            raise FileNotFoundError(f"no log files match pattern: {raw}")
        if len(matches) > 1:
            raise ValueError(
                f"pattern matched {len(matches)} files; pass one path or use "
                f"--follow-current-session (latest: {matches[-1]})"
            )
        return Path(matches[0]).resolve()

    path = resolve_log_path(raw, repo_root)
    if not path.is_file():
        raise FileNotFoundError(f"log not found: {path}")
    return path


def analyze_log(path: Path) -> dict:
    lines = path.read_text(errors="replace").splitlines()
    queue_count = None
    load_ok = False
    usb_begin = False
    first_restore_cap: int | None = None
    last_restore_cap: int | None = None
    restore_count = 0
    mid_pass_during_restore = 0
    in_restore_window = False

    for i, line in enumerate(lines):
        if "Queuing loop slot restore" in line:
            m = re.search(r"restore (\d+) pending", line)
            if m:
                queue_count = int(m.group(1))

        if "BOOT,load,ok" in line or "#CAP,BOOT,load,ok" in line:
            load_ok = True

        if "Deferred restore loop slot" in line:
            restore_count += 1
            in_restore_window = True
            cap = _next_cap(lines, i)
            if cap is not None:
                if first_restore_cap is None:
                    first_restore_cap = cap
                last_restore_cap = cap

        if in_restore_window and ",PERS,mid_pass," in line:
            mid_pass_during_restore += 1

        if "BOOT,usb_host,begin" in line or "#CAP,BOOT,usb_host,begin" in line:
            in_restore_window = False
            usb_begin = True

    total_restore_s = None
    if first_restore_cap is not None and last_restore_cap is not None:
        total_restore_s = (last_restore_cap - first_restore_cap) / 1_000_000

    return {
        "path": str(path),
        "queue_count": queue_count,
        "load_ok": load_ok,
        "usb_begin": usb_begin,
        "restore_count": restore_count,
        "total_restore_s": total_restore_s,
        "mid_pass_during_restore": mid_pass_during_restore,
        "first_restore_cap": first_restore_cap,
    }


def _line_cap(line: str) -> int | None:
    m = re.search(r"#CAP,(\d+)", line)
    return int(m.group(1)) if m else None


def _next_cap(lines: list[str], start: int) -> int | None:
    for j in range(start, min(start + 8, len(lines))):
        cap = _line_cap(lines[j])
        if cap is not None:
            return cap
    return None


def main() -> int:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "log",
        nargs="?",
        help="capture session log path (not a shell glob — use --follow-current-session)",
    )
    parser.add_argument(
        "--follow-current-session",
        action="store_true",
        help="read path from captures/.current_session (written by capture_session.py)",
    )
    parser.add_argument(
        "--max-restore-s",
        type=float,
        default=3.0,
        help="Fail if first-to-last deferred restore span exceeds this (default 3.0)",
    )
    args = parser.parse_args()

    try:
        log_path = resolve_log_argument(args.log, args.follow_current_session, repo_root)
    except (FileNotFoundError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    result = analyze_log(log_path)
    print(f"log: {result['path']}")
    print(f"pending slots queued: {result['queue_count']}")
    print(f"BOOT,load,ok seen: {result['load_ok']}")
    print(f"BOOT,usb_host,begin seen: {result['usb_begin']}")
    print(f"deferred restore lines: {result['restore_count']}")
    if result["total_restore_s"] is not None:
        print(f"restore span (first->last): {result['total_restore_s']:.2f}s")
    print(f"PERS,mid_pass during restore: {result['mid_pass_during_restore']}")

    ok = True
    if result["queue_count"] and result["restore_count"] == 0 and not result["usb_begin"]:
        print(
            "FAIL: boot hung or capture truncated before first deferred restore "
            "(check CrashReport / keep capture_session running through usb_host,begin)"
        )
        ok = False
    if result["mid_pass_during_restore"] > 0:
        print("FAIL: mid_pass ran during deferred boot restore (redundant SD writes)")
        ok = False
    if result["total_restore_s"] is not None and result["total_restore_s"] > args.max_restore_s:
        print(f"FAIL: restore span > {args.max_restore_s}s")
        ok = False
    if result["restore_count"] == 0 and ok:
        print("WARN: no deferred restore lines (log may not include boot)")

    if ok and result["restore_count"] > 0 and result["usb_begin"]:
        print("PASS: boot restore timing checks")
    elif ok and result["restore_count"] == 0:
        print("SKIP: no boot restore data to verify")

    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
