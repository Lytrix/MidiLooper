#!/usr/bin/env python3
"""Tiered native test runner (FAST / AFFECTED / FULL; see .cursor/rules/Testing-Workflow.mdc).

  scripts/test_native.py affected [--base REF] [--dry-run]   # diff -> suites + sentinels
  scripts/test_native.py full [-j N]                          # every native suite, sharded
  scripts/test_native.py suites test_a test_b [-j N]          # named suites (FAST)

Selection (``affected``) is mechanical:

* a change under ``test/<suite>/`` selects that suite;
* a firmware file (``src/``, ``include/``) selects the suites whose own test files name it (through their harnesses and
  ``test_support``) plus its ``closures`` in the map; only an **unmapped** firmware file falls back to every suite whose
  whole compile closure reaches it (exact, but for a widely included file that is most of the suite);
* ``scripts/test_map.json`` adds closures (``closures``), marks paths that need FULL now (``full``) and paths that need
  no native gate (``no_gate``); the ``sentinels`` always run with AFFECTED.

Parallelism: every native suite links to the same ``<build_dir>/native/program``, so two ``pio test`` processes must
never share a build directory. Each shard gets its own ``PLATFORMIO_BUILD_DIR`` (``<default build dir>-shard<k>``), and a
suite keeps its shard across runs (``shards.json`` there), so incremental builds stay warm.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import configparser
import fnmatch
import json
import os
import re
import subprocess
import sys
import time
import zlib
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAP_PATH = ROOT / "scripts" / "test_map.json"
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
# The per-suite banner is printed for every suite, including SKIPPED ones (the summary table omits those).
SUITE_LINE_RE = re.compile(r"^-+ native:(test_\S+) \[(PASSED|FAILED|ERRORED|SKIPPED)\] Took ([\d.]+) seconds", re.M)
CASES_RE = re.compile(r"=+\s*(\d+) test cases:\s*(.*?) in ")


# ---------------------------------------------------------------------------------------------------------------------
# Selection (pure; unit-tested by scripts/test_test_native.py)

def glob_match(path: str, pattern: str) -> bool:
    """fnmatch with '**' crossing directories and a bare '*.ext' matching at any depth."""
    if fnmatch.fnmatchcase(path, pattern):
        return True
    if "/" not in pattern and fnmatch.fnmatchcase(path.rsplit("/", 1)[-1], pattern):
        return True
    if "**" in pattern:
        return fnmatch.fnmatchcase(path, pattern.replace("**/", "*").replace("**", "*"))
    return False


def any_match(path: str, patterns) -> bool:
    return any(glob_match(path, p) for p in patterns)


@dataclass
class Selection:
    suites: set = field(default_factory=set)
    full: bool = False
    reasons: dict = field(default_factory=dict)  # suite or "FULL" -> [why]
    no_gate: list = field(default_factory=list)
    unmapped: list = field(default_factory=list)  # changed non-test paths that selected nothing

    def add(self, suite: str, why: str) -> None:
        self.suites.add(suite)
        self.reasons.setdefault(suite, []).append(why)


def select(changed, test_map, closure_index, all_suites, direct_index=None) -> Selection:
    """`direct_index`: path -> suites whose own test files reach it (test files transitively, firmware files they include
    by name). `closure_index`: path -> suites whose full compile closure reaches it (the unmapped fallback)."""
    direct_index = closure_index if direct_index is None else direct_index
    sel = Selection()
    for path in sorted(set(changed)):
        if any_match(path, test_map.get("no_gate", [])):
            sel.no_gate.append(path)
            continue
        if any_match(path, test_map.get("full", {}).get("paths", [])):
            sel.full = True
            sel.reasons.setdefault("FULL", []).append(path)
            continue
        parts = path.split("/")
        if len(parts) >= 3 and parts[0] == "test" and parts[1] in all_suites:
            sel.add(parts[1], path)
            # Another suite's harness may include this file (e.g. a shared header in a suite directory).
            for suite in direct_index.get(path, ()):
                sel.add(suite, f"includes {path}")
            continue
        before = len(sel.suites)
        for suite in direct_index.get(path, ()):
            sel.add(suite, f"includes {path}")
        mapped = False
        for rule in test_map.get("closures", []):
            if any_match(path, rule.get("paths", [])):
                mapped = True
                for suite in rule.get("suites", []):
                    sel.add(suite, f"closure for {path}")
        if not mapped and len(sel.suites) == before:
            # Unmapped firmware: fall back to every suite that compiles it (exact, possibly broad).
            for suite in closure_index.get(path, ()):
                sel.add(suite, f"compiles {path}")
        if len(sel.suites) == before and path.startswith(("src/", "include/", "test/")):
            sel.unmapped.append(path)
    if sel.suites or sel.full:
        for suite in test_map.get("sentinels", {}).get("suites", []):
            sel.add(suite, "sentinel")
    sel.suites &= set(all_suites)  # native-ignored suites (test_support, ...) never run here
    return sel


# ---------------------------------------------------------------------------------------------------------------------
# Repository facts

def native_suites(root: Path = ROOT) -> list:
    cfg = configparser.ConfigParser(interpolation=None, strict=False)
    cfg.read(root / "platformio.ini")
    ignore = set(cfg.get("env:native", "test_ignore", fallback="").split())
    return sorted(p.name for p in (root / "test").iterdir()
                  if p.is_dir() and p.name.startswith("test_") and p.name not in ignore)


def _includes(path: Path) -> list:
    try:
        text = path.read_text(errors="replace")
    except OSError:
        return []
    return INCLUDE_RE.findall(text)


def _resolve(include: str, base: Path, root: Path):
    for candidate in (base.parent / include, root / "include" / include, root / "src" / include):
        try:
            resolved = candidate.resolve()
        except OSError:
            continue
        if resolved.is_file() and root in resolved.parents:
            return resolved
    return None


def closure_indexes(root: Path = ROOT, suites=None):
    """(direct, deep): repo-relative path -> suites. `direct` follows includes only through files under `test/` other than
    `test_support` (a suite reaches its own files, other suites' harnesses it includes, and the firmware files those
    name); `deep` follows every include, i.e. the suite's whole compile closure."""
    suites = suites or native_suites(root)
    test_dir = root / "test"
    direct, deep = {}, {}
    for suite in suites:
        seen, near = set(), set()
        stack = [(p.resolve(), True) for p in (test_dir / suite).glob("*") if p.suffix in (".cpp", ".c", ".h", ".hpp")]
        while stack:
            path, from_test = stack.pop()
            if from_test:
                near.add(path)
            if path in seen:
                continue
            seen.add(path)
            # `test_support` compiles most of the firmware for most suites; reaching a file only through it is the deep
            # (compile) closure, not a suite's own concern.
            in_test = test_dir in path.parents and (test_dir / "test_support") not in path.parents
            for inc in _includes(path):
                resolved = _resolve(inc, path, root)
                if resolved is not None:
                    stack.append((resolved, in_test))
        for path in seen:
            deep.setdefault(str(path.relative_to(root)), set()).add(suite)
        for path in near:
            direct.setdefault(str(path.relative_to(root)), set()).add(suite)
    return direct, deep


def changed_files(base: str) -> list:
    def git(*args):
        return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, check=True).stdout.split("\n")
    files = set(git("diff", "--name-only", f"{base}...HEAD")) | set(git("diff", "--name-only", "HEAD"))
    files |= set(git("ls-files", "--others", "--exclude-standard"))
    return sorted(f for f in files if f)


# ---------------------------------------------------------------------------------------------------------------------
# Running

def default_build_dir() -> Path:
    cfg = configparser.ConfigParser(interpolation=None, strict=False)
    cfg.read(ROOT / "platformio.ini")
    return Path(os.path.expanduser(cfg.get("platformio", "build_dir", fallback=str(ROOT / ".pio" / "build"))))


def assign_shards(suites, jobs: int, state_path: Path, durations: dict) -> dict:
    """Sticky assignment (warm incremental builds); new suites go to the lightest shard by known duration."""
    try:
        sticky = json.loads(state_path.read_text())
    except (OSError, ValueError):
        sticky = {}
    if sticky.get("jobs") != jobs:
        sticky = {"jobs": jobs, "shard": {}}
    load = [0.0] * jobs
    for suite, shard in sticky["shard"].items():
        if suite in suites and 0 <= shard < jobs:
            load[shard] += durations.get(suite, 5.0)
    for suite in sorted(suites, key=lambda s: -durations.get(s, 5.0)):
        if suite not in sticky["shard"] or not 0 <= sticky["shard"][suite] < jobs:
            shard = min(range(jobs), key=lambda k: load[k])
            sticky["shard"][suite] = shard
            load[shard] += durations.get(suite, 5.0)
    state_path.parent.mkdir(parents=True, exist_ok=True)
    state_path.write_text(json.dumps(sticky, indent=1, sort_keys=True))
    return {s: sticky["shard"][s] for s in suites}


def run_shard(k: int, suites: list, build_root: Path, log_dir: Path) -> dict:
    env = dict(os.environ, PLATFORMIO_BUILD_DIR=f"{build_root}-shard{k}")
    cmd = ["pio", "test", "-e", "native"] + [a for s in suites for a in ("-f", s)]
    log = log_dir / f"shard{k}.log"
    start = time.monotonic()
    with log.open("w") as out:
        rc = subprocess.run(cmd, cwd=ROOT, env=env, stdout=out, stderr=subprocess.STDOUT).returncode
    text = log.read_text(errors="replace")
    return {"shard": k, "rc": rc, "seconds": time.monotonic() - start, "log": str(log), "text": text}


def parse_results(texts) -> dict:
    suites, cases, skipped, failed = {}, 0, 0, 0
    for text in texts:
        for name, status, seconds in SUITE_LINE_RE.findall(text):
            suites[name] = (status, float(seconds))
        for total, detail in CASES_RE.findall(text):
            cases += int(total)
            skipped += sum(int(n) for n in re.findall(r"(\d+) skipped", detail))
            failed += sum(int(n) for n in re.findall(r"(\d+) failed", detail))
    return {"suites": suites, "cases": cases, "skipped": skipped, "failed": failed}


def run(suites, jobs: int, label: str) -> int:
    suites = sorted(set(suites))
    if not suites:
        print(f"[{label}] no native suites selected")
        return 0
    build_root = default_build_dir()
    state = Path(f"{build_root}-shards")
    durations = {}
    try:
        durations = json.loads((state / "durations.json").read_text())
    except (OSError, ValueError):
        pass
    jobs = max(1, min(jobs, len(suites)))
    shard_of = assign_shards(suites, jobs, state / "shards.json", durations)
    groups = {}
    for suite, shard in shard_of.items():
        groups.setdefault(shard, []).append(suite)
    log_dir = state / "logs" / time.strftime("%Y%m%d_%H%M%S")
    log_dir.mkdir(parents=True, exist_ok=True)
    print(f"[{label}] {len(suites)} suites in {len(groups)} shards, logs {log_dir}")
    start = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(groups)) as pool:
        results = list(pool.map(lambda kv: run_shard(kv[0], kv[1], build_root, log_dir), sorted(groups.items())))
    wall = time.monotonic() - start
    parsed = parse_results(r["text"] for r in results)
    for name, (status, seconds) in parsed["suites"].items():
        durations[name] = seconds
    (state / "durations.json").write_text(json.dumps(durations, indent=1, sort_keys=True))
    bad = sorted(n for n, (st, _) in parsed["suites"].items() if st in ("FAILED", "ERRORED"))
    missing = sorted(set(suites) - set(parsed["suites"]))
    for name in bad:
        print(f"  {parsed['suites'][name][0]}: {name}")
    for name in missing:
        print(f"  NO RESULT: {name}")
    passed = parsed["cases"] - parsed["skipped"] - parsed["failed"]
    print(f"[{label}] {parsed['cases']} cases: {parsed['skipped']} skipped, {passed} passed, {parsed['failed']} failed; "
          f"{len(parsed['suites'])}/{len(suites)} suites reported; wall {wall:.0f}s "
          f"(suite time {sum(t for _, t in parsed['suites'].values()):.0f}s)")
    # A suite that failed, errored or never reported (a shard that died) fails the gate; pio's own rc is not needed.
    return 1 if bad or missing else 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    aff = sub.add_parser("affected")
    aff.add_argument("--base", default="HEAD", help="diff base (committed changes base...HEAD plus the working tree)")
    aff.add_argument("--dry-run", action="store_true")
    full = sub.add_parser("full")
    named = sub.add_parser("suites")
    named.add_argument("names", nargs="+")
    for p in (aff, full, named):
        p.add_argument("-j", "--jobs", type=int, default=max(1, min(6, (os.cpu_count() or 2) - 2)))
    args = parser.parse_args(argv)
    all_suites = native_suites()
    if args.mode == "full":
        return run(all_suites, args.jobs, "FULL")
    if args.mode == "suites":
        unknown = sorted(set(args.names) - set(all_suites))
        if unknown:
            print("unknown or native-ignored suites: " + ", ".join(unknown))
            return 2
        return run(args.names, args.jobs, "FAST")
    test_map = json.loads(MAP_PATH.read_text())
    direct, deep = closure_indexes(ROOT, all_suites)
    sel = select(changed_files(args.base), test_map, deep, all_suites, direct)
    if sel.no_gate:
        print(f"[AFFECTED] no native gate for {len(sel.no_gate)} path(s)")
    if sel.unmapped:
        print("[AFFECTED] changed paths no suite reaches: " + ", ".join(sel.unmapped))
    if sel.full:
        print("[AFFECTED] FULL required by: " + ", ".join(sel.reasons["FULL"]))
    for suite in sorted(sel.suites):
        print(f"  {suite}: {'; '.join(sorted(set(sel.reasons[suite]))[:3])}")
    if args.dry_run:
        return 0
    if sel.full:
        return run(all_suites, args.jobs, "FULL")
    return run(sel.suites, args.jobs, "AFFECTED")


if __name__ == "__main__":
    sys.exit(main())
