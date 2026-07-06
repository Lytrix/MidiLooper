#!/usr/bin/env python3
"""Audit vector/MidiEventVec declarations for external-memory routing.

Reports std::vector and MidiEventVec uses that do not use ExternalMemoryFirstAllocator
or SessionMidiEventVec. Input for Phase 4 PSRAM routing audit — no firmware changes.
"""
from __future__ import annotations

import argparse
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SKIP_DIRS = {
    ".git",
    ".pio",
    ".venv",
    "captures",
    "openspec/changes/archive",
}

ROUTED_MARKERS = (
    "ExternalMemoryFirstAllocator",
    "SessionMidiEventVec",
    "InternalHeapFirstAllocator",  # intentional internal-first (hot/small)
    "ChunkIdList",
    "BarIndexVec",
)

VECTOR_PATTERNS = (
    re.compile(r"\bstd::vector\s*<"),
    re.compile(r"\bMidiEventVec\b"),
    re.compile(r"\bSessionMidiEventVec\b"),
    re.compile(r"\bstd::unordered_map\s*<"),
    re.compile(r"\bstd::deque\s*<"),
)


@dataclass
class Hit:
    path: Path
    line_no: int
    text: str
    routed: bool


def should_skip(path: Path) -> bool:
    rel = path.relative_to(ROOT).as_posix()
    for skip in SKIP_DIRS:
        if rel.startswith(skip):
            return True
    if "/test/" in f"/{rel}/" and "mock" in rel:
        return False
    return False


def scan_file(path: Path) -> list[Hit]:
    hits: list[Hit] = []
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return hits
    for line_no, line in enumerate(lines, start=1):
        if not any(p.search(line) for p in VECTOR_PATTERNS):
            continue
        if line.strip().startswith("//"):
            continue
        routed = any(marker in line for marker in ROUTED_MARKERS)
        hits.append(Hit(path=path, line_no=line_no, text=line.strip(), routed=routed))
    return hits


def collect_hits(paths: list[Path]) -> list[Hit]:
    all_hits: list[Hit] = []
    for path in paths:
        if should_skip(path):
            continue
        if path.suffix not in {".h", ".hpp", ".cpp", ".cc"}:
            continue
        all_hits.extend(scan_file(path))
    return all_hits


def discover_source_files() -> list[Path]:
    result = subprocess.run(
        ["rg", "--files", "-g", "*.h", "-g", "*.hpp", "-g", "*.cpp", "-g", "*.cc",
         "include", "src", "test"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode not in (0, 1):
        return sorted(ROOT.rglob("*.cpp"))
    return [ROOT / line for line in result.stdout.splitlines() if line.strip()]


def summarize(hits: list[Hit], show_routed: bool) -> str:
    unrouted = [h for h in hits if not h.routed]
    routed = [h for h in hits if h.routed]
    lines = [
        f"total vector/map hits: {len(hits)}",
        f"routed (allocator alias or marker): {len(routed)}",
        f"unrouted (review candidates): {len(unrouted)}",
        "",
    ]
    if show_routed and routed:
        lines.append("=== routed ===")
        for hit in routed:
            rel = hit.path.relative_to(ROOT)
            lines.append(f"{rel}:{hit.line_no}: {hit.text}")
        lines.append("")
    lines.append("=== unrouted (review) ===")
    if not unrouted:
        lines.append("(none)")
    else:
        by_file: dict[Path, list[Hit]] = {}
        for hit in unrouted:
            by_file.setdefault(hit.path, []).append(hit)
        for path in sorted(by_file, key=lambda p: p.as_posix()):
            rel = path.relative_to(ROOT)
            lines.append(f"\n{rel}")
            for hit in by_file[path]:
                lines.append(f"  {hit.line_no}: {hit.text}")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--show-routed",
        action="store_true",
        help="Include routed lines in output",
    )
    parser.add_argument(
        "--paths",
        nargs="*",
        type=Path,
        help="Optional files or directories to scan (default: include/, src/, test/)",
    )
    args = parser.parse_args()

    if args.paths:
        files: list[Path] = []
        for raw in args.paths:
            path = raw if raw.is_absolute() else ROOT / raw
            if path.is_dir():
                files.extend(path.rglob("*.h"))
                files.extend(path.rglob("*.hpp"))
                files.extend(path.rglob("*.cpp"))
            elif path.is_file():
                files.append(path)
    else:
        files = discover_source_files()

    hits = collect_hits(files)
    print(summarize(hits, show_routed=args.show_routed))


if __name__ == "__main__":
    main()
