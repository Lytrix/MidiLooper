#!/usr/bin/env python3
"""Pre-flight for capture markers: run BEFORE building/flashing a HITL test.

For each marker tag (e.g. `DIAG,my_event,`) it answers three questions that otherwise cost a
hardware iteration each:

  EMITTED  does a string literal with this tag exist in src/ or include/?
  TIER-A   does `CaptureLineTier::isTierALine` classify it Tier-A (and not the lossy
           `hold_empty` exception)?  Non-Tier-A rows are shed silently under transport.
  FIT      does the classifying prefix fit the ring's 47-byte head read once the
           `#CAP,<micros>,` header is counted at the 10-digit micros worst case?
           (`headRecordIsTierAText` in src/Utils/DebugSessionCapture.cpp reads 47 bytes.)

The Tier-A prefix list is parsed from include/Utils/CaptureLineTier.h, which stays the single
source of truth; nothing is duplicated here.

Usage:
  python3 scripts/check_marker_contract.py 'DIAG,my_event,' 'RECS,'
  python3 scripts/check_marker_contract.py --audit     # every registered Tier-A prefix: FIT only
Exit status: 0 all pass, 1 any FAIL, 2 usage/parse error.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable, Optional

ROOT = Path(__file__).resolve().parent.parent
TIER_HEADER = ROOT / "include" / "Utils" / "CaptureLineTier.h"
SOURCE_DIRS = (ROOT / "src", ROOT / "include")

# Mirrors `char line[48]` (47 usable bytes) in headRecordIsTierAText.
HEAD_READ_BYTES = 47
# "#CAP," + up to 10 digits (uint32 micros) + ",".
CAP_HEADER_WORST_CASE = 5 + 10 + 1
MAX_PREFIX_LEN = HEAD_READ_BYTES - CAP_HEADER_WORST_CASE

_PREFIX_RE = re.compile(r'strncmp\(\s*tag\s*,\s*"([^"]+)"')
_FN_START = "inline bool isTierALine("


def tier_a_prefixes(text: Optional[str] = None) -> tuple[list[str], list[str]]:
    """(accepted prefixes, excluded prefixes) from isTierALine's body."""
    text = TIER_HEADER.read_text(encoding="utf-8") if text is None else text
    start = text.find(_FN_START)
    if start < 0:
        raise ValueError(f"{_FN_START!r} not found in {TIER_HEADER}")
    body = text[start:]
    accepted: list[str] = []
    excluded: list[str] = []
    for line in body.splitlines():
        stripped = line.strip()
        if stripped.startswith("//"):
            continue
        for prefix in _PREFIX_RE.findall(line):
            # The only early-return-false exception is a `strncmp(...) == 0) { return false;`.
            (excluded if "hold_empty" in prefix else accepted).append(prefix)
    return accepted, excluded


def classify(tag: str, accepted: Iterable[str], excluded: Iterable[str]) -> Optional[str]:
    """Longest accepted prefix matching `tag`, or None. Excluded prefixes win."""
    if any(tag.startswith(p) for p in excluded):
        return None
    matches = [p for p in accepted if tag.startswith(p)]
    return max(matches, key=len) if matches else None


def emitted(tag: str, dirs: Iterable[Path] = SOURCE_DIRS) -> Optional[str]:
    """First `file:line` whose text holds the tag (trailing comma ignored), else None."""
    needle = tag.rstrip(",")
    for base in dirs:
        for path in sorted(base.rglob("*")):
            if path.suffix not in {".cpp", ".h", ".hpp", ".c", ".inc"} or path == TIER_HEADER:
                continue
            try:
                for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                    if needle in line:
                        return f"{path.relative_to(ROOT)}:{number}"
            except OSError:
                continue
    return None


def check_tag(tag: str, accepted: list[str], excluded: list[str]) -> tuple[bool, str]:
    where = emitted(tag)
    prefix = classify(tag, accepted, excluded)
    problems = []
    if where is None:
        problems.append("EMITTED=no (no string literal in src/ or include/; firmware never emits it)")
    if prefix is None:
        problems.append("TIER-A=no (add a prefix in CaptureLineTier.h and a test_capture_line_tier case; "
                        "otherwise it is shed silently under transport)")
    elif len(prefix) > MAX_PREFIX_LEN:
        problems.append(f"FIT=no (prefix {len(prefix)} bytes > {MAX_PREFIX_LEN}; late in a session the "
                        f"47-byte head read truncates it and it is treated as non-Tier-A)")
    if problems:
        return False, f"FAIL {tag}: " + "; ".join(problems)
    return True, f"OK   {tag}: emitted={where} tier-a-prefix={prefix!r} ({len(prefix)}/{MAX_PREFIX_LEN} bytes)"


def audit(accepted: list[str]) -> list[str]:
    return [p for p in accepted if len(p) > MAX_PREFIX_LEN]


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tags", nargs="*", help="marker tag after '#CAP,<micros>,', e.g. 'DIAG,my_event,'")
    parser.add_argument("--audit", action="store_true", help="check the FIT rule for every registered prefix")
    args = parser.parse_args(argv)
    try:
        accepted, excluded = tier_a_prefixes()
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    if not accepted:
        print("error: parsed zero Tier-A prefixes; the header layout changed", file=sys.stderr)
        return 2
    if not args.tags and not args.audit:
        parser.print_usage(sys.stderr)
        return 2
    failed = False
    if args.audit:
        too_long = audit(accepted)
        print(f"audit: {len(accepted)} Tier-A prefixes, limit {MAX_PREFIX_LEN} bytes, {len(too_long)} over")
        for prefix in too_long:
            print(f"FAIL {prefix!r}: {len(prefix)} bytes")
        failed = failed or bool(too_long)
    for tag in args.tags:
        ok, message = check_tag(tag, accepted, excluded)
        print(message)
        failed = failed or not ok
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
