#!/usr/bin/env python3
"""Offline aggregation for independently captured rolling-Record pass bundles.

This tool never opens device ports. Each input directory must be a completed managed run.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.verify.rolling_record import aggregate_rolling_record_bundles


def _load_run_bundle(run_dir: Path) -> dict:
    paths = {
        "report": run_dir / "rolling-record.json",
        "metadata": run_dir / "metadata.json",
        "shutdown": run_dir / "shutdown-result.json",
        "midi_cleanup": run_dir / "midi-cleanup-result.json",
    }
    result = {"run_dir": str(run_dir.resolve())}
    for key, path in paths.items():
        if not path.is_file():
            raise ValueError(f"run_artifact_missing:{path.name}")
        try:
            result[key] = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise ValueError(f"run_artifact_unreadable:{path.name}:{exc}") from exc
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_dirs", nargs="+", type=Path,
                        help="Completed managed run directories; never pass a captured command.json")
    parser.add_argument("--out-dir", required=True, type=Path,
                        help="New output directory for rolling-record-aggregate.json")
    args = parser.parse_args(argv)

    out_dir = args.out_dir
    report_path = out_dir / "rolling-record-aggregate.json"
    if report_path.exists():
        print(f"[rolling-record-aggregate] refusing to overwrite {report_path}", flush=True)
        return 2

    bundles = []
    load_issues = []
    for run_dir in args.run_dirs:
        try:
            bundles.append(_load_run_bundle(run_dir))
        except ValueError as exc:
            load_issues.append(f"{run_dir}:{exc}")
    if load_issues:
        report = dict(ok=False, verdict="INCOMPLETE_REQUIRED_STOP_COVERAGE",
                      full_recipe_complete=False, issues=load_issues)
    else:
        report = aggregate_rolling_record_bundles(bundles)
    report["input_run_dirs"] = [str(path.resolve()) for path in args.run_dirs]
    out_dir.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"[rolling-record-aggregate] {report['verdict']}: {report_path}", flush=True)
    if report.get("issues"):
        for issue in report["issues"]:
            print(f"[rolling-record-aggregate] {issue}", flush=True)
    return 0 if report.get("ok") is True and report.get("full_recipe_complete") is True else 2


if __name__ == "__main__":
    raise SystemExit(main())
