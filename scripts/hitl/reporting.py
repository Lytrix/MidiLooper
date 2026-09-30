"""JSON report output for layered HITL runs, and the sealed bundle a run leaves behind.

Two different documents live here, and the difference matters:

* `write_scenario_report` writes a **progress** report for one layered scenario. It is rewritten as the run
  advances (that is deliberate: a run killed mid-scenario must still leave what it knew), so it is *not*
  evidence of a finished run.
* `build_report_bundle` / `write_report_bundle` produce the **sealed bundle**: one versioned, digest-pinned
  record per run, written **once**, whose verdict is derived from its own evidence by a rule that a reader can
  re-run later with no device attached. `verify_report_bundle` is that reader.

The rule the bundle's verdict comes from is the spec's, not each caller's: every recorded predicate must be ok
**and** the teardown must have succeeded, because "shutdown failure SHALL prevent overall success". A caller
that reports success over a failed predicate or an incomplete drain gets `passed=False` with the inconsistency
named, instead of a bundle that claims more than its evidence.
"""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Iterable, Optional

from hitl.context import ScenarioContext
from hitl.types import ScenarioResult
from hitl.verify.result import VerificationResult

#: The bundle schema's version. A reader refuses a version it does not know rather than guessing at a shape.
BUNDLE_SCHEMA_VERSION = 2

#: The file name a sealed bundle is written under inside a run's output directory.
BUNDLE_NAME = "run-bundle.json"

#: Every section a bundle carries, in order. `digest` is computed over all of it except itself.
BUNDLE_SECTIONS = (
    "schema_version", "run_id", "kind", "status", "claims_success", "passed", "failures", "identity",
    "authorization", "actions", "observations", "predicates", "inventories", "timing", "content", "teardown",
    "evidence", "notes", "digest",
)


class BundleInvalid(RuntimeError):
    """A bundle that cannot be trusted: unknown version, edited content, or a verdict its evidence denies."""


@dataclass(frozen=True)
class BundleVerdict:
    """What verifying a bundle found."""

    ok: bool
    passed: bool
    schema_version: int
    digest: str
    recorded_digest: str
    failures: tuple[dict, ...] = ()
    notes: tuple[str, ...] = ()

    def as_dict(self) -> dict:
        return {
            "ok": self.ok,
            "passed": self.passed,
            "schema_version": self.schema_version,
            "digest": self.digest,
            "recorded_digest": self.recorded_digest,
            "failures": [dict(failure) for failure in self.failures],
            "notes": list(self.notes),
        }


def write_scenario_report(
    *,
    scenario_id: str,
    result: Optional[ScenarioResult],
    verification: VerificationResult,
    scenario_ctx: ScenarioContext,
    out_dir: Path,
) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    path = out_dir / f"host_midi_hitl_{scenario_id}_{stamp}.json"
    payload = {
        "schema_version": 1,
        "scenario_id": scenario_id,
        "track_number": scenario_ctx.action.config.track_number,
        "loop_slot": scenario_ctx.action.config.loop_slot,
        "midi_channel": scenario_ctx.action.config.midi_channel,
        "passed": verification.ok,
        "verification": _verification_to_dict(verification),
        "observations": {} if result is None else result.observations,
        "markers": [] if result is None else result.markers,
        "artifacts": {
            key: str(value) for key, value in ({} if result is None else result.artifacts).items()
        },
        "out_dir": str(scenario_ctx.out_dir),
    }
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    return path


def exit_code_from_verification(verification: VerificationResult) -> int:
    return 0 if verification.ok else 2


def _verification_to_dict(verification: VerificationResult) -> dict[str, object]:
    return {
        "passed": verification.passed,
        "ok": verification.ok,
        "warnings": list(verification.warnings),
        "failures": [
            {
                "check": failure.check,
                "expected": failure.expected,
                "observed": failure.observed,
                "serial_line": failure.serial_line,
                "known_good_bundle": failure.known_good_bundle,
                "suggestion": failure.suggestion,
            }
            for failure in verification.failures
        ],
        "metrics": verification.metrics,
        "attachments": verification.attachments,
    }


# --- the sealed bundle ---------------------------------------------------------------------------------


def _canonical(payload: dict) -> str:
    """The bundle's bytes for digesting: every section but `digest`, sorted, without incidental whitespace."""
    body = {key: value for key, value in payload.items() if key != "digest"}
    return json.dumps(body, sort_keys=True, separators=(",", ":"), default=str)


def bundle_digest(payload: dict) -> str:
    """The sha256 that pins a bundle's content, including its own verdict and failures."""
    return hashlib.sha256(_canonical(payload).encode("utf-8")).hexdigest()


def derive_verdict(bundle: dict) -> tuple[bool, list[dict]]:
    """Whether a bundle's *own evidence* is a pass, and why not. The one rule a replay re-runs.

    Every recorded predicate must be ok, a teardown result must be present and ok, and a caller's success claim
    must be backed by both -- otherwise the claim itself is recorded as a failure, so the bundle names the
    inconsistency instead of carrying it.
    """
    failures: list[dict] = []
    for predicate in bundle.get("predicates") or ():
        if predicate.get("ok"):
            continue
        failures.append({
            "check": predicate.get("check") or "predicate",
            "expected": predicate.get("expected", "ok"),
            "observed": predicate.get("observed") or predicate.get("detail") or "failed",
        })
    teardown = bundle.get("teardown")
    if not teardown:
        failures.append({"check": "teardown", "expected": "a teardown result",
                         "observed": "none recorded"})
    elif not teardown.get("ok"):
        failures.append({"check": "teardown", "expected": "ok",
                         "observed": teardown.get("status") or teardown.get("detail") or "not ok"})
    if bundle.get("claims_success") and failures:
        failures.append({
            "check": "bundle_verdict",
            "expected": "a success claim backed by its own evidence",
            "observed": f"{bundle.get('status')!r} with {len(failures)} failing check(s)",
        })
    return (not failures), failures


def build_report_bundle(
    *,
    run_id: str,
    kind: str,
    identity: dict,
    status: str,
    claims_success: bool,
    predicates: Iterable[dict] = (),
    teardown: Optional[dict] = None,
    authorization: Optional[dict] = None,
    actions: Iterable[dict] = (),
    observations: Iterable[dict] = (),
    inventories: Optional[dict] = None,
    timing: Optional[dict] = None,
    content: Optional[dict] = None,
    evidence: Optional[dict] = None,
    notes: Iterable[str] = (),
) -> dict:
    """Assemble a versioned bundle and decide its verdict in one place (see `derive_verdict`)."""
    bundle = {
        "schema_version": BUNDLE_SCHEMA_VERSION,
        "run_id": run_id,
        "kind": kind,
        "status": status,
        "claims_success": bool(claims_success),
        "identity": dict(identity),
        "authorization": authorization,
        "actions": [dict(action) for action in actions],
        "observations": [dict(observation) for observation in observations],
        "predicates": [dict(predicate) for predicate in predicates],
        "inventories": inventories,
        "timing": timing,
        "content": content,
        "teardown": dict(teardown) if teardown is not None else None,
        "evidence": evidence,
        "notes": [str(note) for note in notes],
    }
    passed, failures = derive_verdict(bundle)
    bundle["passed"] = passed
    bundle["failures"] = failures
    bundle["digest"] = bundle_digest(bundle)
    return bundle


def write_report_bundle(out_dir: Path, bundle: dict, *, name: str = BUNDLE_NAME) -> Path:
    """Seal `bundle` into `out_dir`, **once**.

    Written to a temporary file and renamed, so a reader never sees half a bundle, and refused if the file
    already exists: a run's evidence is written once, and a second write means the caller is about to lose the
    first (a rerun gets its own output directory, or its own run id). The version and the digest are checked
    here rather than trusted, so a caller cannot seal a bundle this schema does not describe.
    """
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    path = out / name
    if path.exists():
        raise BundleInvalid(f"{path} already exists; a bundle is sealed once")
    if bundle.get("schema_version") != BUNDLE_SCHEMA_VERSION:
        raise BundleInvalid(
            f"refusing to seal schema {bundle.get('schema_version')!r}; this writer knows "
            f"{BUNDLE_SCHEMA_VERSION}")
    if bundle.get("digest") != bundle_digest(bundle):
        raise BundleInvalid("refusing to seal a bundle whose digest does not match its content")
    missing = [section for section in BUNDLE_SECTIONS if section not in bundle]
    if missing:
        raise BundleInvalid(f"refusing to seal a bundle missing {missing}")
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(bundle, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)
    return path


def load_report_bundle(path: Path) -> dict:
    """Read a sealed bundle, refusing a version this reader does not know."""
    path = Path(path)
    try:
        bundle = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise BundleInvalid(f"{path} is not a readable bundle: {exc}") from exc
    if not isinstance(bundle, dict):
        raise BundleInvalid(f"{path} is not a bundle object")
    version = bundle.get("schema_version")
    if version != BUNDLE_SCHEMA_VERSION:
        raise BundleInvalid(f"{path} is schema {version!r}; this reader knows {BUNDLE_SCHEMA_VERSION}")
    return bundle


def verify_report_bundle(path: Path) -> BundleVerdict:
    """Re-derive a sealed bundle's verdict, with no device attached.

    Three checks. The version must be one this reader knows. The content must still hash to the recorded
    digest, so nothing was edited in place. And the *recorded* verdict must be the one the same rule derives
    from the same recorded predicates and teardown -- which is what makes the replay meaningful: a bundle whose
    predicates were flipped (and whose digest was recomputed to match) is still caught, because the derivation
    is re-run rather than read.
    """
    bundle = load_report_bundle(path)
    recomputed = bundle_digest(bundle)
    recorded = str(bundle.get("digest") or "")
    failures: list[dict] = []
    notes: list[str] = []
    if recomputed != recorded:
        failures.append({"check": "bundle_digest", "expected": recorded, "observed": recomputed})
        notes.append("the bundle's content does not hash to its recorded digest")
    passed, derived = derive_verdict(bundle)
    if bool(bundle.get("passed")) != passed:
        failures.append({
            "check": "bundle_verdict",
            "expected": f"passed={bool(bundle.get('passed'))} as recorded",
            "observed": f"re-deriving from the same evidence gives passed={passed}",
        })
    failures.extend(derived)
    return BundleVerdict(
        ok=recomputed == recorded and bool(bundle.get("passed")) == passed,
        passed=passed and bool(bundle.get("passed")),
        schema_version=int(bundle.get("schema_version") or 0),
        digest=recomputed,
        recorded_digest=recorded,
        failures=tuple(failures),
        notes=tuple(notes),
    )
