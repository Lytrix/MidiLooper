#!/usr/bin/env python3
"""Static gate for the fixed-memory content engine: no heap or growing containers in include/ContentEngine or
src/ContentEngine (fixed-memory-engine invariant I2). Comments are ignored. Exit 1 and list offending lines."""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DIRS = [ROOT / "include" / "ContentEngine", ROOT / "src" / "ContentEngine"]
FORBIDDEN = re.compile(
    r"\bmalloc\b|\bcalloc\b|\brealloc\b|\bextmem_malloc\b|\bnew\b|\bdelete\b|"
    r"std::(vector|map|unordered_map|deque|list|set|shared_ptr|unique_ptr|make_shared|make_unique|string|function)\b|"
    r"#include\s*<(vector|map|unordered_map|deque|list|set|memory|string|functional)>"
)


def strip_comments(line: str) -> str:
    return line.split("//", 1)[0]


def main() -> int:
    bad = []
    for d in DIRS:
        if not d.exists():
            continue
        for path in sorted(d.rglob("*")):
            if path.suffix not in {".h", ".hpp", ".cpp"}:
                continue
            for n, line in enumerate(path.read_text().splitlines(), 1):
                code = strip_comments(line)
                if FORBIDDEN.search(code):
                    bad.append(f"{path.relative_to(ROOT)}:{n}: {line.strip()}")
    if bad:
        print("content engine heap gate FAILED:")
        print("\n".join(bad))
        return 1
    print("content engine heap gate: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
