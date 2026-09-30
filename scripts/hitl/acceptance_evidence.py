"""The acceptance verdict's *external* evidence: content parity, timing gates and the independent teardown.

Task 5.2 asks the acceptance oracle to require — and to be **vetoed** by — the verifiers that already own the
other halves of the conjunction. This module is the adapter layer, and it is deliberately small: it does **not**
re-implement any check. Each function reads the report its owner already produces and answers the oracle's three
questions in the owners' own words:

* **content parity** — `hitl.library_manifest.compare_manifests`, whose report carries `pass`/`verdict` plus the
  unexplained manifest and lifecycle differences;
* **timing gates** — `hitl.verify.midi_boundary_timing`, through the owner's own
  `midi_boundary_timing_issues(summary)` list, so a new issue the timing owner learns to name is a veto here
  without a second edit;
* **teardown** — `hitl.device_shutdown.ShutdownResult`, whose record (`status`, `drains`, `detail`, `ok`) is the
  independent field design decision 6 keeps out of the acceptance checks and able to veto a pass.

Each returns `(ok, detail)` with `ok` in `{True, False, None}`: `None` is "this evidence was not supplied", which
the oracle grades as `UNKNOWN` -- never as a pass. Every detail string is the owner's own, so a reader can see
which row, difference or issue decided it.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Mapping, Optional

#: The timing owner is imported **lazily** inside `timing_gates`: it pulls the serial-stack modules, and a caller
#: that never supplies a timing summary (a flow, a scenario, or the oracle in a process without pyserial) must not
#: need them importable to grade a capture.
#: How many evidence items are quoted in a check's detail before it is summarised.
_MAX_ITEMS = 3


def _quote(items: list[Any]) -> str:
    """A bounded, readable rendering of an owner's own difference/issue items."""
    shown = [str(item) for item in items[:_MAX_ITEMS]]
    more = "" if len(items) <= _MAX_ITEMS else f" (+{len(items) - _MAX_ITEMS} more)"
    return "; ".join(shown) + more


def load_report(path: Path | str | None) -> Optional[dict]:
    """One verifier's JSON report, or None when no path was given or the file is not there.

    A missing report is *not* an error here: it is the "not supplied" case the oracle reports as an evidence gap,
    because refusing to grade a capture for the absence of an optional input would make every older capture
    ungradable instead of honestly partial.
    """
    if path is None:
        return None
    candidate = Path(path)
    if not candidate.exists():
        return None
    return json.loads(candidate.read_text())


def content_parity(report: Optional[Mapping[str, Any]]) -> tuple[Optional[bool], str]:
    """The manifest comparison's own verdict, as the oracle's `content_parity` check (content owner's words)."""
    if not report:
        return None, "no content-parity report was supplied (the `hitl-content-verification` inventory pipeline's)"
    verdict = str(report.get("verdict", "")).lower()
    passed = report.get("pass")
    differences = list(report.get("unexplained_manifest_differences") or []) \
        + list(report.get("unexplained_lifecycle_differences") or [])
    counts = report.get("count_differences") or {}
    if passed is None and verdict not in ("pass", "fail"):
        return None, "the content-parity report carries neither `pass` nor a `pass`/`fail` verdict"
    ok = bool(passed) if passed is not None else verdict == "pass"
    if ok and not differences and not counts:
        return True, "the manifest comparison passed with no unexplained difference"
    return False, ("the manifest comparison failed: " + _quote(differences or [counts]) if (differences or counts)
                   else "the manifest comparison reported `fail`")


def timing_gates(summary: Optional[Mapping[str, Any]]) -> tuple[Optional[bool], str]:
    """The timing owner's own issue list, as the oracle's `timing_gates` check.

    The issues are produced by `midi_boundary_timing_issues`, not by a copy of its rules: that function is where
    the timing vocabulary lives, and routing through it is what makes a newly-named issue a veto by itself.
    """
    if not summary:
        return None, "no timing summary was supplied (the `midi_boundary_timing` verifier's)"
    try:
        from hitl.verify.midi_boundary_timing import midi_boundary_timing_issues
    except Exception as exc:  # the owner needs the serial stack; a process without it says so
        return None, f"the timing owner could not be consulted in this process ({exc.__class__.__name__}: {exc})"
    issues = midi_boundary_timing_issues(dict(summary))
    if not issues:
        return True, "the timing verifier reported no issue"
    return False, "the timing verifier reported: " + _quote(issues)


def teardown(record: Optional[Mapping[str, Any]]) -> tuple[Optional[bool], str]:
    """The run's independent shutdown result, as the oracle's `teardown` check.

    Design decision 6: "Shutdown result remains an independent field and can veto overall success". The field is
    read as the shutdown owner records it (`hitl.device_shutdown`), so an incomplete drain is a *setup* failure --
    a run that did not drain is not verdict-grade -- rather than an acceptance violation.
    """
    if not record:
        return None, "no teardown result was supplied (the run's own shutdown record)"
    ok = record.get("ok")
    if ok is None:
        return None, (f"the teardown record carries no `ok` (status={record.get('status')!r}), so the independent "
                      "field cannot be read")
    detail = f"shutdown status={record.get('status')!r}"
    if record.get("detail"):
        detail += f": {record['detail']}"
    if record.get("drains") is not None:
        detail += f" (drains={record.get('drains')})"
    return bool(ok), detail
