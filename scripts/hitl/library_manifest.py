#!/usr/bin/env python3
"""restored-library-stability Packet B / task 2.2 — the complete per-slot manifest comparator.

Task **2.2 — complete-manifest stability** requires the *whole* expected manifest to be compared on
each of three consecutive boots, and any unexplained difference — **including in a slot that is not
part of the fixture** — to be a failure. This module is the pure, read-only half: it turns the device
observation records (`!LIB_OBSERVE` → `LIBOBS` frames, decoded by
``scripts/hitl/library_observation.py``) into a per-slot manifest, records a baseline as the expected
manifest, and compares every other boot against it in full.

Three distinctions keep the comparison honest (design D4/D6):

* **required fields** — payload presence/token, committed content identity and counts, and the
  initial playback-required flag. A difference here is a **manifest difference**: the library did not
  reproduce. This is the task-2.2 verdict.
* **lifecycle fields** — the observed request record and classifier outputs. A difference here is also
  reported (and fails by default): a settled observation of the same library reproduces its request
  records too. It is kept separate only so the reader can see *what* differed.
* **window-scoped field** — ``committed_terminal_in_window`` and the observed restore-completion
  count. D4 makes these explicitly window-scoped, so they are reported and **never** fail the gate.

No device access, no upload, no SD write. A capture that is not a fresh boot or whose observation
frame is incomplete is **ineligible** (reported, not compared): an incomplete window is never presented
as the settled library state.
"""
from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable, Sequence

from hitl.library_observation import parse_observation_lines

__all__ = [
    "REQUIRED_FIELDS",
    "LIFECYCLE_FIELDS",
    "WINDOW_SCOPED_FIELDS",
    "REQUIRED_COUNTS",
    "WINDOW_SCOPED_COUNTS",
    "MANIFEST_FINGERPRINT_FIELDS",
    "BootManifest",
    "load_boot_manifest",
    "compare_manifests",
    "stability_report",
    "format_report",
    "manifest_fingerprint",
]

#: Task 2.2's manifest core: payload presence/token, committed content identity and counts, and the
#: initial playback-required set. A difference in any of these in any slot is a failure.
REQUIRED_FIELDS = (
    "payload",
    "has_committed_content",
    "committed_pass_count",
    "highest_committed_pass_id",
    "loop_length_ticks",
    "committed_event_count",
    "required_for_initial_playback",
)

#: The observed request/classifier record. Compared, and failing by default, but reported separately.
LIFECYCLE_FIELDS = (
    "offered",
    "cause",
    "state_mask",
    "request",
    "wait",
    "terminal",
    "generation",
    "epoch",
    "invalidated",
)

#: Explicitly window-scoped by design D4: reported, never a failure.
WINDOW_SCOPED_FIELDS = ("committed_terminal_in_window",)

#: The fields that make up the whole-manifest fingerprint. The window-scoped field is excluded: D4 makes
#: it window-scoped, so it must not change the library's identity.
MANIFEST_FINGERPRINT_FIELDS = REQUIRED_FIELDS + LIFECYCLE_FIELDS

#: The three settled D4 quantities. ``observed_restore_completions`` is window-scoped and is excluded.
REQUIRED_COUNTS = ("payload_inventory", "initial_playback_required", "committed_at_checkpoint")
WINDOW_SCOPED_COUNTS = ("observed_restore_completions",)

BOOT_RESET_RE = re.compile(r"#CAP,(\d+),DIAG,boot_reset,")


@dataclass
class BootManifest:
    """One boot's decoded observation: identity, completeness, counts and the per-slot manifest."""

    capture_dir: str
    fresh_boot: bool
    boot_banner: bool
    boot_reset_us: int | None
    complete: bool
    incomplete_window: bool
    window: dict[str, Any]
    counts: dict[str, Any]
    slots: dict[tuple[int, int], dict[str, Any]] = field(default_factory=dict)
    ineligible_reason: str | None = None

    @property
    def eligible(self) -> bool:
        return self.ineligible_reason is None

    def manifest_of(self, fields: Sequence[str]) -> dict[tuple[int, int], tuple[Any, ...]]:
        return {
            key: tuple(slot.get(name) for name in fields)
            for key, slot in self.slots.items()
        }

    def as_dict(self) -> dict[str, Any]:
        return {
            "capture_dir": self.capture_dir,
            "fresh_boot": self.fresh_boot,
            "boot_banner": self.boot_banner,
            "boot_reset_us": self.boot_reset_us,
            "complete": self.complete,
            "incomplete_window": self.incomplete_window,
            "eligible": self.eligible,
            "ineligible_reason": self.ineligible_reason,
            "window": self.window,
            "counts": self.counts,
            "slot_count": len(self.slots),
            "manifest_sha256": manifest_fingerprint(self),
        }


def manifest_fingerprint(boot: BootManifest) -> str:
    """A stable SHA-256 over the whole per-slot manifest (window-scoped fields excluded).

    Two boots with the same fingerprint have identical **observed field values** for every slot, so the
    fingerprint is a compact identity for the complete-manifest comparison. It is an identity over the
    observation only: it does not establish anything about the payload bytes on SD, and it is not a
    statement that nothing was written.
    """
    digest = hashlib.sha256()
    for key in sorted(boot.slots):
        record = boot.slots[key]
        digest.update(f"{key[0]},{key[1]}".encode())
        for name in MANIFEST_FINGERPRINT_FIELDS:
            digest.update(b"\x00")
            digest.update(str(record.get(name)).encode())
    return digest.hexdigest()


def _observation_from_dir(directory: Path) -> tuple[dict[str, Any], list[str]]:
    """Read one capture directory's `LIBOBS` records plus its raw log lines. Read-only."""
    census_path = directory / "census.json"
    log_path = directory / "boot-census.log"
    if census_path.exists():
        census = json.loads(census_path.read_text(encoding="utf-8"))
    else:
        census = {}
    lines: list[str] = []
    if log_path.exists():
        lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    observation = census.get("library_observation")
    if observation is None and lines:
        observation = parse_observation_lines(lines)
    return {"census": census, "observation": observation or {}, "lines": lines}, lines


def load_boot_manifest(directory: Path) -> BootManifest:
    """Decode one capture directory into a `BootManifest`. Performs no device action."""
    payload, lines = _observation_from_dir(directory)
    census = payload["census"]
    observation = payload["observation"]

    boot_reset_us: int | None = None
    for line in lines:
        match = BOOT_RESET_RE.search(line)
        if match:
            boot_reset_us = int(match.group(1))
            break

    boot_banner = bool(census.get("boot_banner")) or boot_reset_us is not None
    complete = bool(observation.get("complete"))
    incomplete_window = bool(observation.get("incomplete_window", not complete))
    window = observation.get("window") or {}
    counts = observation.get("counts") or {}
    slots: dict[tuple[int, int], dict[str, Any]] = {}
    for record in observation.get("slots") or []:
        slots[(int(record["track"]), int(record["slot"]))] = record

    manifest = BootManifest(
        capture_dir=str(directory),
        fresh_boot=boot_reset_us is not None,
        boot_banner=boot_banner,
        boot_reset_us=boot_reset_us,
        complete=complete,
        incomplete_window=incomplete_window,
        window=window,
        counts=counts,
        slots=slots,
    )
    if not observation:
        manifest.ineligible_reason = "no library observation was requested or recorded"
    elif not complete:
        manifest.ineligible_reason = (
            "the observation window is incomplete; its counts and records are not the settled "
            "library state"
        )
    elif not manifest.fresh_boot:
        manifest.ineligible_reason = (
            "the capture did not observe a boot (`boot_reset` absent); it is a mid-session "
            "observation, not a boot"
        )
    elif not manifest.boot_banner:
        manifest.ineligible_reason = "no boot banner was captured"
    elif not slots:
        manifest.ineligible_reason = "the observation recorded no per-slot records"
    elif int(window.get("slots_observed", 0)) != len(slots):
        manifest.ineligible_reason = (
            f"slots_observed={window.get('slots_observed')} but {len(slots)} slot records were read"
        )
    elif int(window.get("restarts", 0)) != 0:
        manifest.ineligible_reason = (
            f"the observation restarted {window.get('restarts')} time(s); no single consistent view"
        )
    return manifest


def _slot_differences(
    expected: dict[tuple[int, int], dict[str, Any]],
    observed: dict[tuple[int, int], dict[str, Any]],
    fields: Sequence[str],
) -> list[dict[str, Any]]:
    differences: list[dict[str, Any]] = []
    for key in sorted(set(expected) | set(observed)):
        want = expected.get(key)
        got = observed.get(key)
        if want is None or got is None:
            missing_in = "observed" if got is None else "expected"
            extra_in = "observed" if want is None else "expected"
            detail = {name: (want or {}).get(name) for name in fields}
            other = {name: (got or {}).get(name) for name in fields}
            differences.append({
                "track": key[0],
                "slot": key[1],
                "kind": "slot_present_in_only_one_boot",
                "missing_in": missing_in if want is None else extra_in,
                "field_differences": {},
                "expected": detail,
                "observed": other,
            })
            continue
        field_differences = {
            name: {"expected": want.get(name), "observed": got.get(name)}
            for name in fields
            if want.get(name) != got.get(name)
        }
        if field_differences:
            differences.append({
                "track": key[0],
                "slot": key[1],
                "kind": "field_mismatch",
                "field_differences": field_differences,
            })
    return differences


def compare_manifests(
    expected: BootManifest,
    observed: BootManifest,
    *,
    authorized_changes: Iterable[tuple[int, int]] = (),
) -> dict[str, Any]:
    """Compare one boot's manifest against the recorded expected manifest, in full.

    ``authorized_changes`` names the fixture slots whose recorded change is expected (the Packet B
    fixture); a difference in any other slot — including a slot outside the approved range — is
    unexplained and fails.
    """
    authorized = {(int(t), int(s)) for t, s in authorized_changes}
    required = _slot_differences(expected.slots, observed.slots, REQUIRED_FIELDS)
    lifecycle = _slot_differences(expected.slots, observed.slots, LIFECYCLE_FIELDS)
    window_scoped = _slot_differences(expected.slots, observed.slots, WINDOW_SCOPED_FIELDS)

    count_differences = {
        name: {"expected": expected.counts.get(name), "observed": observed.counts.get(name)}
        for name in REQUIRED_COUNTS
        if expected.counts.get(name) != observed.counts.get(name)
    }
    window_count_differences = {
        name: {"expected": expected.counts.get(name), "observed": observed.counts.get(name)}
        for name in WINDOW_SCOPED_COUNTS
        if expected.counts.get(name) != observed.counts.get(name)
    }

    def unexplained(differences: list[dict[str, Any]]) -> list[dict[str, Any]]:
        return [d for d in differences if (d["track"], d["slot"]) not in authorized]

    unexplained_required = unexplained(required)
    unexplained_lifecycle = unexplained(lifecycle)
    failed = bool(unexplained_required or unexplained_lifecycle or count_differences)
    return {
        "observed_capture": observed.capture_dir,
        "expected_capture": expected.capture_dir,
        "authorized_changes": sorted(f"{t},{s}" for t, s in authorized),
        "manifest_differences": required,
        "unexplained_manifest_differences": unexplained_required,
        "lifecycle_differences": lifecycle,
        "unexplained_lifecycle_differences": unexplained_lifecycle,
        "window_scoped_differences": window_scoped,
        "count_differences": count_differences,
        "window_scoped_count_differences": window_count_differences,
        "pass": not failed,
        "verdict": "pass" if not failed else "fail",
    }


def stability_report(
    directories: Sequence[Path],
    *,
    expected_dir: Path | None = None,
    authorized_changes: Iterable[tuple[int, int]] = (),
    required_boots: int = 3,
) -> dict[str, Any]:
    """Load every boot, record the expected manifest, and compare each boot against it in full."""
    boots = [load_boot_manifest(Path(directory).resolve()) for directory in directories]
    expected_index = 0
    if expected_dir is not None:
        expected_path = Path(expected_dir).resolve()
        for index, boot in enumerate(boots):
            if Path(boot.capture_dir) == expected_path:
                expected_index = index
                break
        else:
            boots.insert(0, load_boot_manifest(expected_path))
            expected_index = 0

    expected = boots[expected_index]
    # Only eligible boots are compared. An ineligible capture (incomplete window, mid-session attach)
    # is reported as ineligible and never presented as a settled observation to compare.
    comparisons = [
        compare_manifests(expected, boot, authorized_changes=authorized_changes)
        for index, boot in enumerate(boots)
        if index != expected_index and boot.eligible and expected.eligible
    ]
    eligible = [boot for boot in boots if boot.eligible]
    observed_eligible = [boot for boot in boots if boot.eligible and boot is not expected]
    distinct_boots = len({boot.boot_reset_us for boot in eligible if boot.boot_reset_us is not None})
    # Every eligible capture observed its own `boot_reset`, so it is its own boot. Reusing one boot
    # twice (a second mid-session observation) is ineligible above rather than counted here. The gate
    # counts the *observed* boots compared against the recorded expected manifest.
    enough = len(observed_eligible) >= required_boots
    all_pass = all(comparison["pass"] for comparison in comparisons)
    return {
        "task": "2.2 — complete-manifest stability",
        "required_boots": required_boots,
        "expected_manifest_from": expected.capture_dir,
        "expected_manifest_eligible": expected.eligible,
        "expected_manifest_sha256": manifest_fingerprint(expected),
        "boots": [boot.as_dict() for boot in boots],
        "eligible_boots": len(eligible),
        "observed_eligible_boots": len(observed_eligible),
        "distinct_boot_resets": distinct_boots,
        "enough_boots": enough,
        "comparisons": comparisons,
        "all_match": all_pass,
        "verdict": "pass" if (enough and all_pass and expected.eligible) else "fail",
        "ineligible_boots": [
            {"capture_dir": boot.capture_dir, "reason": boot.ineligible_reason}
            for boot in boots
            if not boot.eligible
        ],
    }


def format_report(report: dict[str, Any]) -> str:
    """A compact human-readable rendering of a stability report."""
    lines = [
        f"[manifest] task {report['task']}",
        f"[manifest] expected manifest recorded from: {report['expected_manifest_from']} "
        f"(sha256 {report['expected_manifest_sha256'][:16]}…)",
        f"[manifest] eligible boots: {report['eligible_boots']} total, "
        f"{report['observed_eligible_boots']} observed of required {report['required_boots']} "
        f"(distinct boot resets: {report['distinct_boot_resets']})",
    ]
    for boot in report["boots"]:
        state = "eligible" if boot["eligible"] else f"INELIGIBLE ({boot['ineligible_reason']})"
        counts = boot["counts"]
        lines.append(
            f"[manifest]   {Path(boot['capture_dir']).name}: {state} "
            f"banner={boot['boot_banner']} boot_reset_us={boot['boot_reset_us']} "
            f"complete={boot['complete']} slots={boot['slot_count']} "
            f"payload={counts.get('payload_inventory')} "
            f"initial_playback={counts.get('initial_playback_required')} "
            f"committed={counts.get('committed_at_checkpoint')} "
            f"completions(window)={counts.get('observed_restore_completions')} "
            f"sha256={boot['manifest_sha256'][:16]}…"
        )
    for comparison in report["comparisons"]:
        name = Path(comparison["observed_capture"]).name
        if comparison["pass"]:
            lines.append(f"[manifest]   {name}: MANIFEST MATCH (every slot, in full)")
        else:
            lines.append(f"[manifest]   {name}: MANIFEST DIFFERENCE — FAIL")
        for difference in comparison["unexplained_manifest_differences"]:
            lines.append(
                f"[manifest]      t{difference['track']}s{difference['slot']} {difference['kind']}: "
                f"{difference.get('field_differences') or difference.get('expected')}"
            )
        for difference in comparison["unexplained_lifecycle_differences"]:
            lines.append(
                f"[manifest]      (lifecycle) t{difference['track']}s{difference['slot']} "
                f"{difference.get('field_differences')}"
            )
        if comparison["count_differences"]:
            lines.append(f"[manifest]      counts: {comparison['count_differences']}")
        if comparison["window_scoped_count_differences"]:
            lines.append(
                "[manifest]      window-scoped (not a failure): "
                f"{comparison['window_scoped_count_differences']}"
            )
    lines.append(f"[manifest] verdict: {report['verdict'].upper()}")
    return "\n".join(lines)
