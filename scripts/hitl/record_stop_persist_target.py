#!/usr/bin/env python3
"""S1.3 save/restore-survival — correlate one recording's slot with its persistence target.

The growth fixture could not tell "the payload was lost" from "the payload was never written for this
slot": the device's record-stop persistence telemetry named an *adjacent* slot (`slot:5:0`) while the take
had recorded on `(5,1)`. This module is the read-only correlator for that chain. It reads the firmware's
`RID` record family (`DebugSessionCapture::recSlotIdentity`), which emits one bounded line at record start
and one at the persistence admission, both carrying the same take id:

  `#CAP,<us>,RID,start,take=<id>,t=<track>,ss=<startSlot>,sp=<stopSlot>,cs=<commitSlot>,pt=<persistTrack>,ps=<persistSlot>,r=<reason>,w=<work>,chg=<slotChanged>`
  `#CAP,<us>,RID,admit,take=<id>,t=<track>,ss=<startSlot>,sp=<stopSlot>,cs=<commitSlot>,pt=<persistTrack>,ps=<persistSlot>,r=<reason>,w=<work>,chg=<slotChanged>`

**The verdict is a stop condition, not a device action.** A mismatch means: do **not** reboot the fixture
and do not treat the boot observation as evidence about payload survival — the fixture first has to write
the slot it means to grow. No device access, no SD access, no writes here.

Caveat this module states rather than hides: the `RID`/`PERS` telemetry covers the **deferred persistence
worker**. Four other paths can write a current-set slot payload (revision load/commit, boot recovery,
saved-set copy, and the persisted-set writer) and emit no `PERS` work record. So a *missing* `LoopPersist`
line is not proof that no payload write happened. Adjacent-slot work without take correlation is not
proof of a wrong-target admission. Stored event parity must be verified separately before reboot,
followed by restored event parity; file existence or size alone does not establish either.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Any, Iterable, Mapping, Sequence

__all__ = [
    "RID_FIELDS",
    "SlotIdentityChain",
    "parse_rid_lines",
    "correlate_takes",
    "PERS_WORK_RE",
    "parse_persist_admissions",
    "format_verdict",
]

#: The fields the `RID` record carries, in emission order (`recSlotIdentity`).
RID_FIELDS = (
    "take",
    "t",
    "ss",
    "sp",
    "cs",
    "pt",
    "ps",
    "r",
    "w",
    "chg",
)

RID_RE = re.compile(
    r"#CAP,(?P<us>\d+),RID,(?P<event>start|admit),(?P<fields>[^\r\n]*)"
)
KV_RE = re.compile(r"([A-Za-z_]+)=([^,]*)")
#: The deferred-persistence worker's per-work-item line: `PERS,work,<us>,0,0,<Type>,<key>,<phase>,<ok>`.
PERS_WORK_RE = re.compile(
    r"#CAP,(?P<us>\d+),PERS,work,(?P<dur>\d+),0,0,(?P<type>[A-Za-z]+),(?P<key>[^,]*),(?P<phase>[a-z_]+),(?P<result>[a-z]+)"
)


@dataclass
class SlotIdentityChain:
    """One take's slot identity, across the boundaries the fixture has to correlate."""

    take: int
    start_us: int | None = None
    admit_us: int | None = None
    start_slot: int | None = None
    stop_slot: int | None = None
    commit_slot: int | None = None
    persist_track: int | None = None
    persist_slot: int | None = None
    track_index: int | None = None
    reason: str | None = None
    work: str | None = None
    slot_changed: bool | None = None

    @property
    def observed_admission(self) -> bool:
        return self.admit_us is not None

    @property
    def boundaries(self) -> dict[str, Any]:
        """The correlation the owner asked for, boundary by boundary."""
        return {
            "recorded_slot_at_capture_start": self.start_slot,
            "recorded_slot_at_stop": self.stop_slot,
            "slot_passed_to_finalize_commit_side_effects": self.commit_slot,
            "admitted_persist_track": self.persist_track,
            "admitted_persist_slot": self.persist_slot,
        }

    def verdict(self) -> dict[str, Any]:
        """`pass` only when every observed boundary names the recorded slot.

        An absent admission is reported as `no_admission_observed`, never as a pass: the redirect this
        module exists to catch is exactly an admission that names some other slot.
        """
        problems: list[str] = []
        if self.start_slot is None:
            problems.append("record_start_identity_missing")
        if not self.observed_admission:
            problems.append("persist_admission_not_observed")
        if self.start_slot is not None and self.stop_slot is not None and self.stop_slot != self.start_slot:
            problems.append("stop_slot_differs_from_start_slot")
        if self.commit_slot is not None and self.stop_slot is not None and self.commit_slot != self.stop_slot:
            problems.append("commit_slot_differs_from_stop_slot")
        if (
            self.persist_slot is not None
            and self.stop_slot is not None
            and self.persist_slot != self.stop_slot
        ):
            problems.append("persist_slot_differs_from_recorded_slot")
        # The firmware's own `chg` flag is reported even when the value comparison already caught the
        # divergence: it is the device's independent statement that the target moved, and silencing it
        # would hide a case where the two sources disagree.
        if self.slot_changed is True:
            problems.append("firmware_reports_slot_changed")
        return {
            "take": self.take,
            "boundaries": self.boundaries,
            "problems": problems,
            "pass": not problems,
            "verdict": "pass" if not problems else "fail",
            "note": (
                "a pass here is slot-identity correlation only; it is not proof that the payload bytes "
                "were written (the RID/PERS telemetry covers the deferred worker)"
            ),
        }


def _int_or_none(fields: Mapping[str, str], key: str) -> int | None:
    raw = fields.get(key)
    if raw is None or raw == "":
        return None
    try:
        return int(raw)
    except ValueError:
        return None


def parse_rid_lines(lines: Iterable[str]) -> dict[int, SlotIdentityChain]:
    """Decode the `RID` record family into one chain per take id. Read-only."""
    chains: dict[int, SlotIdentityChain] = {}
    for line in lines:
        match = RID_RE.search(line)
        if not match:
            continue
        fields = {m.group(1): m.group(2) for m in KV_RE.finditer(match.group("fields"))}
        take = _int_or_none(fields, "take")
        if take is None:
            continue
        chain = chains.setdefault(take, SlotIdentityChain(take=take))
        us = int(match.group("us"))
        if match.group("event") == "start":
            chain.start_us = us
            chain.start_slot = _int_or_none(fields, "ss")
            chain.track_index = _int_or_none(fields, "t")
        else:
            chain.admit_us = us
            chain.start_slot = _int_or_none(fields, "ss")
            chain.stop_slot = _int_or_none(fields, "sp")
            chain.commit_slot = _int_or_none(fields, "cs")
            chain.persist_track = _int_or_none(fields, "pt")
            chain.persist_slot = _int_or_none(fields, "ps")
            chain.track_index = _int_or_none(fields, "t")
            chain.reason = fields.get("r")
            chain.work = fields.get("w")
            changed = _int_or_none(fields, "chg")
            chain.slot_changed = None if changed is None else bool(changed)
    return chains


def correlate_takes(
    lines: Iterable[str],
    *,
    expected_slot: int | None = None,
    expected_track: int | None = None,
) -> dict[str, Any]:
    """The fixture's gate: correlate every take, and name the first boundary that differs.

    ``expected_slot``/``expected_track`` are the zero-based coordinates the run pinned **before**
    recording. When they are supplied, a take that names different coordinates is itself the finding.
    """
    materialized = list(lines)
    chains = parse_rid_lines(materialized)
    verdicts = []
    for take in sorted(chains):
        chain = chains[take]
        verdict = chain.verdict()
        if expected_slot is not None and chain.start_slot is not None and chain.start_slot != expected_slot:
            verdict["problems"].append("record_start_slot_differs_from_pinned_target")
        if expected_track is not None and chain.track_index is not None and chain.track_index != expected_track:
            verdict["problems"].append("record_track_differs_from_pinned_target")
        verdict["pass"] = not verdict["problems"]
        verdict["verdict"] = "pass" if verdict["pass"] else "fail"
        verdicts.append(verdict)
    return {
        "takes": verdicts,
        "take_count": len(verdicts),
        "expected_track": expected_track,
        "expected_slot": expected_slot,
        "pass": bool(verdicts) and all(v["pass"] for v in verdicts),
        "verdict": "pass" if (verdicts and all(v["pass"] for v in verdicts)) else "fail",
        "reason": (
            "no RID record was captured: the fixture cannot correlate a take, so it must not reboot"
            if not verdicts
            else "all observed boundaries name the recorded slot"
            if all(v["pass"] for v in verdicts)
            else "a boundary names a different slot; stop before reboot and trace the change"
        ),
    }


def parse_persist_admissions(lines: Iterable[str]) -> list[dict[str, Any]]:
    """The deferred worker's work items, so an admitted `LoopPersist` key can be checked directly."""
    rows = []
    for line in lines:
        match = PERS_WORK_RE.search(line)
        if not match:
            continue
        rows.append(
            {
                "us": int(match.group("us")),
                "type": match.group("type"),
                "key": match.group("key"),
                "phase": match.group("phase"),
                "result": match.group("result"),
            }
        )
    return rows


def format_verdict(report: Mapping[str, Any]) -> str:
    lines = [
        f"[rid] takes={report['take_count']} pinned track={report['expected_track']} "
        f"slot={report['expected_slot']} -> {report['verdict'].upper()}",
    ]
    for verdict in report["takes"]:
        lines.append(f"[rid]   take {verdict['take']}: {verdict['verdict'].upper()}")
        for name, value in verdict["boundaries"].items():
            lines.append(f"[rid]     {name} = {value}")
        for problem in verdict["problems"]:
            lines.append(f"[rid]     PROBLEM {problem}")
    lines.append(f"[rid] {report['reason']}")
    return "\n".join(lines)
