#!/usr/bin/env python3
"""Guard for `docs/Authority/NAMING.md` — the claim-scoped naming authority.

NAMING.md decides **names and meanings** and cites every behavior, ownership and bound statement instead of
restating it. This guard enforces the mechanical half of that rule, so the document cannot drift back into a
shadow specification that outranks OpenSpec by accident:

  BOUND-CLASS  NAMING.md must not define a bound class (they are defined in
               `Architecture/ExecutionBounds.md` § Bound classes; a pointer row is allowed)
  DUPLICATE    two table rows must not restate the same definition text
  CITATION     a contract sentence (SHALL / MUST, "is the contract", "never torn", a µs policy value)
               must carry a markdown link — the owning document is named on the line
  SECTION      every `§ Heading` citation resolves: in the linked document, or in NAMING.md itself

`--audit` adds a non-failing report of terms defined in more than one section (the slow drift measure; the
bound-class rows and the LoopSnapshot restatement were both of this shape).

Usage:
  python3 scripts/check_naming_doc.py                 # check the default document
  python3 scripts/check_naming_doc.py --audit         # + duplicate-label report
  python3 scripts/check_naming_doc.py --path <file.md>
Exit status: 0 all pass, 1 any finding, 2 usage/parse error.
"""

from __future__ import annotations

import argparse
import difflib
import re
import sys
from pathlib import Path
from typing import Dict, List, NamedTuple, Optional, Sequence, Tuple

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DOC = ROOT / "docs" / "Authority" / "NAMING.md"

# Names that only `Architecture/ExecutionBounds.md` may define (NAMING may point at them).
BOUND_CLASS_NAMES = (
    "structurally bounded",
    "empirically bounded",
    "unbounded owner",
    "bounded class",
    "unbounded",
)

CONTRACT_MARKERS = (
    re.compile(r"\b(SHALL|MUST)\b"),
    re.compile(r"\bis the contract\b", re.IGNORECASE),
    re.compile(r"\bnever (torn|held silent)\b", re.IGNORECASE),
    re.compile(r"\b\d[\d,]*\s*µs\b"),
)

INDEX_SECTION = "Term index (alphabetical)"

# A definition comparison needs some substance before near-equality means anything.
MIN_DEFINITION_TOKENS = 8
DEFINITION_SIMILARITY = 0.90


class Finding(NamedTuple):
    rule: str
    line: int
    detail: str

    def render(self) -> str:
        return f"{self.rule}: line {self.line}: {self.detail}"


class Row(NamedTuple):
    line: int
    section: str
    cells: Tuple[str, ...]


def _strip_inline(text: str) -> str:
    """Remove markdown emphasis/code and link syntax, keeping link and code labels."""
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = text.replace("`", "").replace("**", "").replace("__", "")
    text = re.sub(r"(?<!\w)\*(?!\s)", "", text)
    return re.sub(r"\s+", " ", text).strip()


def _normalize_definition(text: str) -> str:
    text = _strip_inline(text).lower()
    text = re.sub(r"[^a-z0-9 ]+", " ", text)
    return re.sub(r"\s+", " ", text).strip()


def _split_row(line: str) -> Optional[Tuple[str, ...]]:
    """Split a markdown table row, honouring escaped pipes; None when the line is not a row."""
    stripped = line.strip()
    if not stripped.startswith("|") or not stripped.endswith("|"):
        return None
    body = stripped[1:-1]
    cells: List[str] = []
    current: List[str] = []
    index = 0
    while index < len(body):
        char = body[index]
        if char == "\\" and index + 1 < len(body) and body[index + 1] == "|":
            current.append("|")
            index += 2
            continue
        if char == "|":
            cells.append("".join(current).strip())
            current = []
            index += 1
            continue
        current.append(char)
        index += 1
    cells.append("".join(current).strip())
    return tuple(cells)


def _is_separator_row(cells: Sequence[str]) -> bool:
    return all(re.fullmatch(r":?-{2,}:?", cell.replace(" ", "")) for cell in cells if cell != "")


def _iter_rows(text: str) -> List[Row]:
    rows: List[Row] = []
    section = ""
    in_fence = False
    for number, line in enumerate(text.splitlines(), start=1):
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        heading = re.match(r"^#{2,4}\s+(.*)$", line)
        if heading:
            section = _strip_inline(heading.group(1)).strip()
            continue
        cells = _split_row(line)
        if cells is None:
            continue
        if _is_separator_row(cells):
            # The row above a separator is a column header, not a definition.
            if rows and rows[-1].line == number - 1:
                rows.pop()
            continue
        rows.append(Row(number, section, cells))
    return rows


def _display(path: Path) -> str:
    """Repo-relative when inside the repository, absolute otherwise (synthetic docs in tests)."""
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)

def _heading_texts(text: str) -> List[str]:
    return [_normalize_definition(m.group(1)) for m in re.finditer(r"^#{1,6}\s+(.*)$", text, re.MULTILINE)]


def _looks_like_pointer(cell: str) -> bool:
    return "](" in cell or cell.strip().startswith("§")


def _rule_bound_classes(rows: Sequence[Row]) -> List[Finding]:
    findings: List[Finding] = []
    for row in rows:
        if len(row.cells) < 2:
            continue
        label = _normalize_definition(row.cells[0])
        if label not in BOUND_CLASS_NAMES:
            continue
        if any(_looks_like_pointer(cell) for cell in row.cells[1:]):
            continue
        findings.append(
            Finding(
                "BOUND-CLASS",
                row.line,
                f"`{_strip_inline(row.cells[0])}` is defined here; bound classes are defined in "
                "docs/Authority/Architecture/ExecutionBounds.md § Bound classes — keep only a pointer",
            )
        )
    return findings


def _definition_text(row: Row) -> str:
    candidates = [_strip_inline(cell) for cell in row.cells[1:]]
    candidates = [c for c in candidates if c]
    if not candidates:
        return ""
    return max(candidates, key=len)


def _rule_duplicate_definitions(rows: Sequence[Row]) -> List[Finding]:
    findings: List[Finding] = []
    seen: List[Tuple[Row, str]] = []
    for row in rows:
        if row.section == INDEX_SECTION:
            continue
        text = _normalize_definition(_definition_text(row))
        if len(text.split()) < MIN_DEFINITION_TOKENS:
            continue
        for earlier, earlier_text in seen:
            if earlier.section == row.section:
                continue
            if difflib.SequenceMatcher(None, earlier_text, text).ratio() >= DEFINITION_SIMILARITY:
                findings.append(
                    Finding(
                        "DUPLICATE",
                        row.line,
                        f"definition repeats line {earlier.line} (§ {earlier.section} / § {row.section}); "
                        "keep one definition and cite it",
                    )
                )
                break
        seen.append((row, text))
    return findings


def _rule_citation(text: str) -> List[Finding]:
    findings: List[Finding] = []
    in_fence = False
    for number, line in enumerate(text.splitlines(), start=1):
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        if not any(marker.search(line) for marker in CONTRACT_MARKERS):
            continue
        if "](" in line:
            continue
        findings.append(
            Finding(
                "CITATION",
                number,
                "contract sentence without a citation; name the owning document with a link on this line "
                "(NAMING.md § Scope of this authority → Citation rule)",
            )
        )
    return findings


def _rule_section_citations(text: str, path: Path) -> List[Finding]:
    findings: List[Finding] = []
    own_headings = _heading_texts(text)
    cache: Dict[Path, List[str]] = {}
    in_fence = False
    for number, line in enumerate(text.splitlines(), start=1):
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        cited = re.findall(r"§\s*([A-Z][^|§\n]*)", line)
        if not cited:
            continue
        targets = [
            target
            for target in re.findall(r"\[[^\]]*\]\(([^)]+)\)", line)
            if target.endswith(".md") and "://" not in target and not target.startswith("#")
        ]
        for raw in cited:
            candidates = _citation_candidates(raw)
            if not candidates:
                continue
            target_files: List[Path] = []
            target_missing = False
            for target in targets:
                resolved = (path.parent / target.split("#")[0]).resolve()
                if not resolved.exists():
                    findings.append(Finding("SECTION", number, f"citation target does not exist: {target}"))
                    target_missing = True
                    continue
                target_files.append(resolved)
            # A `§ Heading` resolves in a document linked on this line, or in this document itself.
            if path not in target_files:
                target_files.append(path)
            if target_missing:
                continue
            if not any(
                _citation_resolves(candidate, target_files, path, own_headings, cache, exact=len(candidate.split()) == 1)
                for candidate in candidates
            ):
                names = ", ".join(_display(f) for f in target_files)
                findings.append(Finding("SECTION", number, f"§ {raw.strip()} not found in {names}"))
    return findings


def _citation_candidates(raw: str) -> List[str]:
    """Progressively shorter prefixes of a `§` citation: the longest one that resolves wins."""
    text = raw.split(" — ")[0].split(".")[0]
    words = _normalize_definition(text).split()
    return [" ".join(words[:count]) for count in range(len(words), 0, -1)]


def _citation_resolves(
    wanted: str,
    files: Sequence[Path],
    path: Path,
    own_headings: Sequence[str],
    cache: Dict[Path, List[str]],
    exact: bool = False,
) -> bool:
    for candidate in files:
        if candidate == path:
            headings = own_headings
        else:
            if candidate not in cache:
                cache[candidate] = _heading_texts(candidate.read_text(encoding="utf-8"))
            headings = cache[candidate]
        for heading in headings:
            if heading == wanted:
                return True
            if not exact and (heading.startswith(wanted) or wanted in heading):
                return True
    return False


def audit_duplicate_labels(text: str) -> List[Tuple[str, List[Tuple[int, str]]]]:
    """Terms whose label is defined as a table row in more than one section (report only)."""
    seen: Dict[str, List[Tuple[int, str]]] = {}
    for row in _iter_rows(text):
        if row.section == INDEX_SECTION or len(row.cells) < 2:
            continue
        label = _strip_inline(row.cells[0])
        if not label:
            continue
        seen.setdefault(label, []).append((row.line, row.section))
    return sorted(((label, hits) for label, hits in seen.items() if len(hits) > 1), key=lambda item: item[0].lower())


def check_text(text: str, path: Path) -> List[Finding]:
    rows = _iter_rows(text)
    findings: List[Finding] = []
    findings.extend(_rule_bound_classes(rows))
    findings.extend(_rule_duplicate_definitions(rows))
    findings.extend(_rule_citation(text))
    findings.extend(_rule_section_citations(text, path))
    return sorted(findings, key=lambda finding: (finding.line, finding.rule))


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Guard docs/Authority/NAMING.md against scope drift.")
    parser.add_argument("--path", default=str(DEFAULT_DOC), help="document to check")
    parser.add_argument("--audit", action="store_true", help="also report terms defined in several sections")
    args = parser.parse_args(argv)

    path = Path(args.path)
    if not path.is_absolute():
        path = (Path.cwd() / path).resolve()
    if not path.is_file():
        print(f"FAIL  not a file: {path}", file=sys.stderr)
        return 2

    text = path.read_text(encoding="utf-8")
    findings = check_text(text, path)

    if args.audit:
        duplicates = audit_duplicate_labels(text)
        print(f"audit: {len(duplicates)} label(s) defined in more than one section")
        for label, hits in duplicates:
            where = ", ".join(f"line {line} (§ {section})" for line, section in hits)
            print(f"  {label}: {where}")

    for finding in findings:
        print(finding.render())
    print(f"{_display(path)}: {len(findings)} finding(s)")
    return 0 if not findings else 1


if __name__ == "__main__":
    raise SystemExit(main())
