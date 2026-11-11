#!/usr/bin/env python3
"""Code hygiene report (docs/Authority/CODE_HYGIENE.md): per firmware file, its size, its comment lines, the history
references its comments carry (decision IDs, owner quotes, stage / task IDs) and its functions over the length
threshold, and the line count of each folder AGENTS.md (CODE_HYGIENE.md § 4). A report, not a gate: it exits 0 unless
--strict is given, then 1 when a folder AGENTS.md is over its cap (a file over the size threshold is reported; with
--strict-size it fails too).

  scripts/code_hygiene_report.py                 # every file under include/ and src/
  scripts/code_hygiene_report.py --top 15        # the 15 largest
  scripts/code_hygiene_report.py path/to/file.h  # named files
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILE_LINES = 1000      # review threshold (CODE_HYGIENE.md § 2)
FUNCTION_LINES = 60    # review threshold
NOTES_LINES = 120      # cap of a folder AGENTS.md (CODE_HYGIENE.md § 4)

DECISION = re.compile(r"\bDEC-\d+")
OWNER = re.compile(r"\bowner\b[^)\n]{0,40}\b20\d\d-\d\d-\d\d", re.I)
STAGE = re.compile(r"\b(E\d+(?:\.\d+[a-z]?)+(?:-\d+[a-z]?)*|slice \d+[a-z]?)\b")
FUNCTION_START = re.compile(r"^\s*(?:template\s*<.*>\s*)?[\w:<>,~*&\s]+\([^;]*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?\{\s*$")


def comment_text(line: str) -> str:
    i = line.find("//")
    return line[i:] if i >= 0 else ""


def long_functions(lines: list[str]) -> list[tuple[int, int, str]]:
    """Functions whose body runs over FUNCTION_LINES (heuristic: a signature line ending in '{', its braces matched)."""
    found = []
    i = 0
    while i < len(lines):
        if FUNCTION_START.match(lines[i]) and not lines[i].lstrip().startswith(("if", "for", "while", "switch", "else")):
            depth = 0
            for j in range(i, len(lines)):
                code = lines[j].split("//", 1)[0]
                depth += code.count("{") - code.count("}")
                if depth <= 0 and j > i:
                    length = j - i + 1
                    if length > FUNCTION_LINES:
                        name = re.sub(r"\s+", " ", lines[i].strip())[:70]
                        found.append((i + 1, length, name))
                    i = j
                    break
        i += 1
    return found


def report(path: Path) -> dict:
    text = path.read_text(errors="replace")
    lines = text.split("\n")
    comments = [comment_text(l) for l in lines]
    joined = "\n".join(comments)
    return {
        "path": path.relative_to(ROOT).as_posix(),
        "lines": len(lines),
        "comment_lines": sum(1 for c in comments if c),
        "decisions": len(DECISION.findall(joined)),
        "owner": len(OWNER.findall(joined)),
        "stages": len(STAGE.findall(joined)),
        "long_functions": long_functions(lines),
    }


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*")
    ap.add_argument("--top", type=int, default=0, help="only the N largest files")
    ap.add_argument("--functions", action="store_true", help="list the long functions")
    ap.add_argument("--strict", action="store_true", help="exit 1 when a folder AGENTS.md is over its cap")
    ap.add_argument("--strict-size", action="store_true", help="also exit 1 when a file is over the size threshold")
    args = ap.parse_args(argv)
    paths = [Path(f).resolve() for f in args.files] if args.files else sorted(
        [*ROOT.glob("include/**/*.h"), *ROOT.glob("src/**/*.cpp"), *ROOT.glob("src/**/*.h")])
    rows = sorted((report(p) for p in paths), key=lambda r: -r["lines"])
    if args.top:
        rows = rows[: args.top]
    print(f"{'file':58} {'lines':>6} {'cmt':>5} {'DEC':>4} {'owner':>5} {'stage':>5} {'fn>' + str(FUNCTION_LINES):>6}")
    over = 0
    for r in rows:
        flag = " *" if r["lines"] > FILE_LINES else ""
        over += 1 if flag else 0
        print(f"{r['path']:58} {r['lines']:>6} {r['comment_lines']:>5} {r['decisions']:>4} {r['owner']:>5} "
              f"{r['stages']:>5} {len(r['long_functions']):>6}{flag}")
        if args.functions:
            for line, length, name in r["long_functions"]:
                print(f"    {line:>5}  {length:>4} lines  {name}")
    total = {k: sum(r[k] for r in rows) for k in ("lines", "comment_lines", "decisions", "owner", "stages")}
    print(f"{'total':58} {total['lines']:>6} {total['comment_lines']:>5} {total['decisions']:>4} {total['owner']:>5} "
          f"{total['stages']:>5} {sum(len(r['long_functions']) for r in rows):>6}")
    print(f"* over {FILE_LINES} lines: {over} file(s). History references belong in the decision log (CODE_HYGIENE.md § 1).")
    notes = sorted([*ROOT.glob("include/**/AGENTS.md"), *ROOT.glob("src/**/AGENTS.md")])
    notes_over = 0
    if notes:
        print(f"\nfolder notes (cap {NOTES_LINES} lines)")
        for n in notes:
            count = len(n.read_text(errors="replace").rstrip("\n").split("\n"))
            flag = " OVER CAP" if count > NOTES_LINES else ""
            notes_over += 1 if flag else 0
            print(f"{n.relative_to(ROOT).as_posix():58} {count:>6}{flag}")
    failed = (args.strict or args.strict_size) and notes_over
    failed = failed or (args.strict_size and over)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
