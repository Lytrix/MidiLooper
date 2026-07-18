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
    usb_begin_line: int | None = None
    usb_begin_cap: int | None = None
    first_restore_cap: int | None = None
    last_restore_cap: int | None = None
    last_restore_line: int | None = None
    restore_count = 0
    mid_pass_during_restore = 0
    tier0_sync = False
    audible_sync = False
    in_restore_window = False

    for i, line in enumerate(lines):
        if "Queuing loop slot restore" in line:
            m = re.search(r"restore (\d+) pending", line)
            if m:
                queue_count = int(m.group(1))

        if "Boot tier0" in line:
            tier0_sync = True
        if "Boot audible" in line:
            audible_sync = True
            tier0_sync = True  # audible set supersedes tier-0-only logs

        if "BOOT,load,ok" in line or "#CAP,BOOT,load,ok" in line:
            load_ok = True

        if "Deferred restore loop slot" in line:
            restore_count += 1
            in_restore_window = True
            last_restore_line = i
            cap = _next_cap(lines, i)
            if cap is not None:
                if first_restore_cap is None:
                    first_restore_cap = cap
                last_restore_cap = cap

        # Count mid_pass across the full deferred drain (Phase 3: USB may begin mid-drain).
        if in_restore_window and ",PERS,mid_pass," in line:
            mid_pass_during_restore += 1

        if "BOOT,usb_host,begin" in line or "#CAP,BOOT,usb_host,begin" in line:
            usb_begin = True
            usb_begin_line = i
            usb_begin_cap = _line_cap(line) or _next_cap(lines, i)

        if last_restore_line is not None and i > last_restore_line and restore_count > 0:
            # Keep window open until we have seen the last restore so far; finalized after loop.
            pass

    # Close restore window after last deferred restore line for mid_pass accounting.
    # Re-scan mid_pass between first and last restore lines (inclusive neighborhood).
    if restore_count > 0:
        first_i = next(
            (i for i, l in enumerate(lines) if "Deferred restore loop slot" in l), None
        )
        last_i = None
        for i, l in enumerate(lines):
            if "Deferred restore loop slot" in l:
                last_i = i
        mid_pass_during_restore = 0
        if first_i is not None and last_i is not None:
            for i in range(first_i, last_i + 1):
                if ",PERS,mid_pass," in lines[i]:
                    mid_pass_during_restore += 1
            # Also count mid_pass shortly after last restore (session teardown).
            for i in range(last_i + 1, min(last_i + 20, len(lines))):
                if ",PERS,mid_pass," in lines[i]:
                    mid_pass_during_restore += 1

    early_usb = False
    if usb_begin and last_restore_line is not None and usb_begin_line is not None:
        early_usb = usb_begin_line < last_restore_line
    elif usb_begin and restore_count == 0:
        early_usb = True  # tier-0 only / empty background

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
        "tier0_sync": tier0_sync,
        "audible_sync": audible_sync,
        "early_usb": early_usb,
        "usb_begin_cap": usb_begin_cap,
        "last_restore_cap": last_restore_cap,
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
        default=None,
        help="Optional: fail if first-to-last deferred restore span exceeds this (seconds)",
    )
    parser.add_argument(
        "--require-early-usb",
        action="store_true",
        default=False,
        help="Phase 3 legacy: require BOOT,usb_host,begin before the last deferred restore",
    )
    parser.add_argument(
        "--no-require-early-usb",
        action="store_true",
        help="Deprecated alias: drain-before-USB is the default",
    )
    parser.add_argument(
        "--require-drain-before-usb",
        action="store_true",
        default=True,
        help="Require full deferred drain before BOOT,usb_host,begin (default on)",
    )
    parser.add_argument(
        "--no-require-drain-before-usb",
        action="store_true",
        help="Disable drain-before-USB gate",
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
    print(f"Boot tier0/audible sync log seen: {result['tier0_sync']}")
    print(f"Boot audible set log seen: {result.get('audible_sync', False)}")
    print(f"deferred restore lines: {result['restore_count']}")
    if result["total_restore_s"] is not None:
        print(f"restore span (first->last): {result['total_restore_s']:.2f}s")
    print(f"PERS,mid_pass during restore: {result['mid_pass_during_restore']}")
    print(f"early USB (before last deferred restore): {result['early_usb']}")

    ok = True
    require_early_usb = args.require_early_usb and not args.no_require_early_usb
    require_drain_before_usb = (
        args.require_drain_before_usb
        and not args.no_require_drain_before_usb
        and not require_early_usb
    )

    if result["queue_count"] and result["restore_count"] == 0 and not result["usb_begin"]:
        print(
            "FAIL: boot hung or capture truncated before first deferred restore "
            "(check CrashReport / keep capture_session running through usb_host,begin)"
        )
        ok = False
    if result["mid_pass_during_restore"] > 0:
        print("FAIL: mid_pass ran during deferred boot restore (redundant SD writes)")
        ok = False
    if (
        args.max_restore_s is not None
        and result["total_restore_s"] is not None
        and result["total_restore_s"] > args.max_restore_s
    ):
        print(f"FAIL: restore span > {args.max_restore_s}s")
        ok = False
    if require_early_usb and result["restore_count"] > 0 and result["usb_begin"]:
        if not result["early_usb"]:
            print(
                "FAIL: Phase 3 expects BOOT,usb_host,begin before the last deferred restore "
                "(tier-0 interactive while background queue drains)"
            )
            ok = False
    if require_drain_before_usb and result["restore_count"] > 0 and result["usb_begin"]:
        if result["early_usb"]:
            print(
                "FAIL: expects full deferred drain before BOOT,usb_host,begin "
                "(title until load complete; no early USB)"
            )
            ok = False
        if result.get("audible_sync"):
            print(
                "FAIL: Boot audible sync log present — audible early path should be reverted"
            )
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
