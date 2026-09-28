#!/usr/bin/env python3
"""restored-library-stability Packet A — the pure library-observation classifier and its capture mirror.

The **firmware** classifier (`include/StorageManager/LibraryObservation.h`,
`src/StorageManager/LibraryObservation.cpp`) is normative: `design.md` D3/D4 and the
`lazy-slot-hydration` spec delta § *Observed current state is reported separately from a nullable
historical cause*. This module mirrors it for the **read-only** analysis of the recorded captures,
which do not carry the payload/content fields the device observation reports.

Two rules make the mirror honest:

* observation is separate from attribution — an unobserved field never becomes a loss claim;
* a field the captures cannot supply is reported ``unknown`` **with the missing field named**
  (``unknown_fields``), never inferred from a neighbouring field.

The four D4 quantities are reported per boot. ``payload_inventory`` and ``committed_at_checkpoint`` are
``None`` offline because the captures record no payload token or committed-content dump; the analysis
says so instead of printing a zero.

**Content representation (S1.3 observability correction).** A slot's loaded content has two forms and
the device reports them separately: **pass-backed** (``Loop::passes``) and **valid published-root-backed**
(``Loop::publishedSnapshotIsCurrent()``). ``committed_at_checkpoint`` is their **union** — a slot carrying
both is one loaded slot — and ``pass_backed_at_checkpoint`` / ``root_backed_at_checkpoint`` are reported
beside it so the union is always auditable. A capture written before the correction has no
representation keys; ``content_representation_observed`` is then ``False`` and both representation
counts are ``None``, because that wire could not separate them.
"""
from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable, Optional, Sequence

__all__ = [
    "Cause",
    "SlotInput",
    "SlotResult",
    "classify_slot",
    "summarize",
    "parse_capture_lines",
    "parse_observation_lines",
    "analyze_capture_file",
    "analyze_capture_dir",
    "STATE_EMPTY",
    "STATE_POPULATED",
    "STATE_PENDING",
]

STATE_EMPTY = 1 << 0
STATE_POPULATED = 1 << 1
STATE_PENDING = 1 << 2

#: The request lifecycle as observed, mirroring the firmware enum.
REQUEST_STATES = ("unknown", "none", "queued", "running", "waiting", "terminal")
TERMINALS = ("none", "committed", "unnecessary", "refused", "superseded")
PAYLOADS = ("unknown", "absent", "invalid_token", "present_verified")
OFFERED = ("unknown", "not_offered", "offered")

RRQ_RE = re.compile(
    r"RRQ,(?P<track>\d+),(?P<slot>\d+),(?P<generation>\d+),(?P<epoch>\d+),"
    r"(?P<state>queued|running|waiting|terminal),(?P<wait>[a-z_]+),(?P<terminal>[a-z_]+)"
)
CAP_MICROS_RE = re.compile(r"#CAP,(\d+),")


class Cause:
    """The nullable historical cause, mirroring the firmware enum (``not_applicable`` is the null)."""

    NOT_APPLICABLE = "not_applicable"
    CATALOG_OMISSION = "catalog_omission"
    RESTORE_REFUSAL = "restore_refusal"
    INTENTIONAL_EVICTION = "intentional_eviction"
    CONTENT_LOSS = "content_loss"
    AMBIGUOUS = "ambiguous"
    UNKNOWN = "unknown"


@dataclass
class SlotInput:
    """One slot's observation snapshot, mirroring ``LibraryObservation::SlotInput``."""

    track: int = 0
    slot: int = 0
    payload: str = "unknown"
    offered: str = "unknown"
    request: str = "unknown"
    request_wait: str = "none"
    terminal: str = "none"
    request_record_exists: bool = False
    request_generation: int = 0
    set_epoch: int = 0
    content_observed: bool = False
    has_committed_content: bool = False
    committed_pass_count: int = 0
    highest_committed_pass_id: int = 0
    loop_length_ticks: int = 0
    committed_event_count: int = 0
    selection_observed: bool = False
    required_for_initial_playback: bool = False
    committed_terminal_in_window: bool = False
    prior_inventory_known: bool = False
    prior_populated: bool = False
    explicit_eviction_event: bool = False
    unknown_fields: list[str] = field(default_factory=list)
    evidence_notes: list[str] = field(default_factory=list)

    def as_dict(self) -> dict[str, Any]:
        return {
            "track": self.track,
            "slot": self.slot,
            "payload": self.payload,
            "offered": self.offered,
            "request": self.request,
            "request_wait": self.request_wait,
            "terminal": self.terminal,
            "request_generation": self.request_generation,
            "set_epoch": self.set_epoch,
            "content_observed": self.content_observed,
            "has_committed_content": self.has_committed_content,
            "selection_observed": self.selection_observed,
            "required_for_initial_playback": self.required_for_initial_playback,
            "committed_terminal_in_window": self.committed_terminal_in_window,
        }


@dataclass
class SlotResult:
    """The classifier's two separate outputs plus the observed fields and the evidence."""

    track: int = 0
    slot: int = 0
    current_state: tuple[str, ...] = ()
    cause: str = Cause.NOT_APPLICABLE
    evidence: dict[str, bool] = field(default_factory=dict)
    unknown_fields: list[str] = field(default_factory=list)
    evidence_notes: list[str] = field(default_factory=list)
    observed: dict[str, Any] = field(default_factory=dict)

    def as_dict(self) -> dict[str, Any]:
        return {
            "track": self.track,
            "slot": self.slot,
            "state": list(self.current_state),
            "state_names": "+".join(self.current_state) if self.current_state else "unknown",
            "cause": self.cause,
            "evidence": self.evidence,
            "evidence_notes": self.evidence_notes,
            "unknown_fields": self.unknown_fields,
            "observed": self.observed,
        }


def _request_is_outstanding(request: str) -> bool:
    return request in ("queued", "running", "waiting")


def classify_slot(slot_input: SlotInput) -> SlotResult:
    """Mirror ``LibraryObservation::classifySlot`` exactly.

    The current state is a **set** over ``empty`` / ``populated`` / ``pending``; ``empty`` is derived
    only from *observed* absence. The historical cause is nullable (``not_applicable``) and a cause is
    never recorded without its evidence.
    """
    result = SlotResult(track=slot_input.track, slot=slot_input.slot)
    outstanding = _request_is_outstanding(slot_input.request)
    # A request that has not terminated never asserts a terminal.
    terminal = "none" if outstanding else slot_input.terminal
    result.observed = {
        "payload": slot_input.payload,
        "offered": slot_input.offered,
        "request": slot_input.request,
        "request_wait": slot_input.request_wait if outstanding else "none",
        "terminal": terminal,
        "request_generation": slot_input.request_generation,
        "set_epoch": slot_input.set_epoch,
        "has_committed_content": slot_input.content_observed
        and slot_input.has_committed_content,
        "required_for_initial_playback": slot_input.selection_observed
        and slot_input.required_for_initial_playback,
        "committed_terminal_in_window": slot_input.committed_terminal_in_window,
    }
    result.unknown_fields = list(slot_input.unknown_fields)

    populated = slot_input.payload == "present_verified" or (
        slot_input.content_observed and slot_input.has_committed_content
    )
    pending = outstanding
    # A **completed** restore whose content field could not be observed (SD admission denied) is a
    # positive completion, not a loss.
    committed_but_content_unobserved = (
        not slot_input.content_observed and terminal == "committed"
    )
    # `empty` requires the absence to be **observed**: no payload, no committed content, no outstanding
    # request. If the payload or content field is `unknown`, absence is not established and the state set
    # stays empty (unknown) rather than claiming `empty`.
    absence_established = (
        slot_input.payload == "absent"
        and slot_input.content_observed
        and not slot_input.has_committed_content
    )
    states: list[str] = []
    if populated:
        states.append("populated")
    if pending:
        states.append("pending")
    if not populated and not pending and absence_established:
        states.append("empty")
    result.current_state = tuple(states)

    evidence = {
        "explicit_eviction_event": slot_input.explicit_eviction_event,
        "payload_validation_failed": slot_input.payload == "invalid_token",
        "refused_terminal": terminal == "refused",
        "prior_inventory_disappearance": slot_input.prior_inventory_known
        and slot_input.prior_populated,
    }
    # `catalog omission` means a payload exists that the enumeration did not offer. It is not inferred
    # from a prior inventory alone.
    evidence["catalog_omission_candidate"] = slot_input.offered == "not_offered" and slot_input.payload in (
        "present_verified",
        "invalid_token",
    )
    result.evidence = evidence

    if evidence["explicit_eviction_event"]:
        result.cause = Cause.INTENTIONAL_EVICTION
        return result
    if populated or pending or committed_but_content_unobserved:
        result.cause = Cause.NOT_APPLICABLE
        return result
    consistent = sum(
        1
        for key in (
            "payload_validation_failed",
            "refused_terminal",
            "catalog_omission_candidate",
        )
        if evidence[key]
    )
    if consistent >= 2:
        result.cause = Cause.AMBIGUOUS
    elif evidence["payload_validation_failed"]:
        result.cause = Cause.CONTENT_LOSS
    elif evidence["refused_terminal"]:
        result.cause = Cause.RESTORE_REFUSAL
    elif evidence["catalog_omission_candidate"]:
        result.cause = Cause.CATALOG_OMISSION
    else:
        result.cause = Cause.UNKNOWN
        if slot_input.unknown_fields:
            result.evidence_notes.append(
                "cause is unknown because these fields were not observed: "
                + ", ".join(slot_input.unknown_fields)
            )
        if evidence["prior_inventory_disappearance"]:
            result.evidence_notes.append(
                "a prior inventory establishes the disappearance, not its cause"
            )
    return result


def summarize(results: Sequence[SlotResult], window: dict[str, Any]) -> dict[str, Any]:
    """The four D4 quantities and the window framing, as separate outputs.

    A quantity whose inputs were not observed is reported as ``None`` with its missing field named; it
    is never coerced to ``0``.
    """
    payload_inventory = 0
    payload_observed = False
    initial_playback = 0
    selection_observed = False
    restore_completions = 0
    committed_at_checkpoint = 0
    content_observed = False
    for result in results:
        observed = result.observed
        if observed["payload"] != "unknown":
            payload_observed = True
        if observed["payload"] == "present_verified":
            payload_inventory += 1
        if observed["required_for_initial_playback"]:
            initial_playback += 1
        if "initial_playback_selection" not in result.unknown_fields:
            selection_observed = True
        if observed["committed_terminal_in_window"]:
            restore_completions += 1
        if observed["has_committed_content"]:
            committed_at_checkpoint += 1
        if "committed_content" not in result.unknown_fields:
            content_observed = True

    unknown_quantities: list[str] = []
    if not payload_observed:
        payload_inventory_value: int | None = None
        unknown_quantities.append("payload_token")
    else:
        payload_inventory_value = payload_inventory
    if not selection_observed:
        initial_playback_value: int | None = None
        unknown_quantities.append("initial_playback_selection")
    else:
        initial_playback_value = initial_playback
    if not content_observed:
        committed_value: int | None = None
        unknown_quantities.append("committed_content")
    else:
        committed_value = committed_at_checkpoint

    admission_complete = window.get("admission_complete")
    every_initial = window.get("every_initial_playback_slot_observed")
    complete = bool(admission_complete) and bool(every_initial)
    return {
        "counts": {
            "payload_inventory": payload_inventory_value,
            "initial_playback_required": initial_playback_value,
            "observed_restore_completions": restore_completions,
            "committed_at_checkpoint": committed_value,
        },
        "counts_unknown_fields": unknown_quantities,
        "window": {
            "start_us": window.get("start_us"),
            "end_us": window.get("end_us"),
            "line_count": window.get("line_count"),
            "admission_complete": admission_complete,
            "every_initial_playback_slot_observed": every_initial,
            "consistent_view": window.get("consistent_view"),
            "unknown_fields": window.get("unknown_fields", []),
        },
        "complete": complete,
        "incomplete_window": not complete,
    }


def _blank_input(track: int, slot: int) -> SlotInput:
    return SlotInput(
        track=track,
        slot=slot,
        payload="unknown",
        # The captures carry no payload token, committed-content dump, boot-enumeration record or
        # active/selected index, so those fields are reported unknown **by name**.
        unknown_fields=["payload_token", "committed_content", "initial_playback_selection"],
    )


def parse_capture_lines(
    lines: Iterable[str],
    *,
    track_count: int = 8,
    slots_per_track: int = 8,
) -> dict[str, Any]:
    """Classify one capture window from its log lines. Performs no device action and no writes.

    The window is the log itself: ``start_us``/``end_us`` are the first and last ``#CAP`` timestamps.
    The restore-completion count is therefore **window-scoped** — exactly the correction D4 requires.
    """
    first_us: int | None = None
    last_us: int | None = None
    line_count = 0
    seen: dict[tuple[int, int], dict[str, Any]] = {}
    for line in lines:
        stamp = CAP_MICROS_RE.search(line)
        if stamp:
            micros = int(stamp.group(1))
            if first_us is None:
                first_us = micros
            last_us = micros
            line_count += 1
        match = RRQ_RE.search(line)
        if not match:
            continue
        if stamp is None:
            continue
        track = int(match.group("track"))
        slot = int(match.group("slot"))
        record = seen.setdefault(
            (track, slot),
            {
                "state": match.group("state"),
                "wait": match.group("wait"),
                "terminal": match.group("terminal"),
                "generation": int(match.group("generation")),
                "epoch": int(match.group("epoch")),
                "committed_in_window": False,
                "transitions": 0,
            },
        )
        record["state"] = match.group("state")
        record["wait"] = match.group("wait")
        record["terminal"] = match.group("terminal")
        record["generation"] = int(match.group("generation"))
        record["epoch"] = int(match.group("epoch"))
        record["transitions"] += 1
        if match.group("terminal") == "committed":
            record["committed_in_window"] = True

    results: list[SlotResult] = []
    for track in range(track_count):
        for slot in range(slots_per_track):
            record = seen.get((track, slot))
            if record is None:
                # No request record in this window. Everything else is unobserved offline, so the
                # state set stays empty (`unknown`), not `empty`.
                slot_input = _blank_input(track, slot)
                slot_input.offered = "unknown"
                results.append(classify_slot(slot_input))
                continue
            slot_input = _blank_input(track, slot)
            slot_input.request_record_exists = True
            slot_input.offered = "offered"
            slot_input.unknown_fields = ["payload_token", "committed_content", "initial_playback_selection"]
            slot_input.request = record["state"]
            slot_input.request_wait = record["wait"]
            slot_input.terminal = record["terminal"]
            slot_input.request_generation = record["generation"]
            slot_input.set_epoch = record["epoch"]
            slot_input.committed_terminal_in_window = record["committed_in_window"]
            result = classify_slot(slot_input)
            result.evidence_notes.append(
                f"{record['transitions']} RRQ transition(s) for generation {record['generation']} "
                f"epoch {record['epoch']} in this window"
            )
            results.append(result)

    window = {
        "start_us": first_us,
        "end_us": last_us,
        "line_count": line_count,
        # The firmware's background admission scan emits no completion record and the captures record
        # no active/selected index, so both framing terms are unknown offline rather than assumed.
        "admission_complete": None,
        "every_initial_playback_slot_observed": None,
        "consistent_view": None,
        "unknown_fields": [
            "background_admission_complete",
            "initial_playback_selection",
        ],
    }
    summary = summarize(results, window)
    return {
        "slots": [result.as_dict() for result in results],
        "requests_seen": len(seen),
        "summary": summary,
        # `N` is explicitly window-scoped: distinct (track, slot) whose `committed` terminal was
        # observed **within this capture window**, background admission included. It is neither the
        # boot playback set nor the library size.
        "n_committed_slots_window_scoped": summary["counts"]["observed_restore_completions"],
        "n_committed_slots_definition": (
            "distinct (track, slot) with an RRQ terminal=committed line observed within this capture "
            "window; background admission included; not the boot playback set and not the library size"
        ),
    }


def analyze_capture_file(path: Path) -> dict[str, Any]:
    """Analyze one capture log file. Read-only."""
    text = path.read_text(encoding="utf-8", errors="replace")
    analysis = parse_capture_lines(text.splitlines())
    analysis["capture"] = str(path)
    return analysis


# ---------------------------------------------------------------------------------------------------
# The **device** observation's own records (`restored-library-stability` 1.3). Unlike the capture
# mirror above, these carry every observed field, so the classifier already ran on the device; the host
# only reads the records back and reports them.
# ---------------------------------------------------------------------------------------------------

LIBOBS_RE = re.compile(r"LIBOBS,(?P<kind>[BSE]),(?P<fields>.*)$")
# Uppercase is allowed because the end record's representation counts are `P=`/`R=`; the slot record's
# representation flags are the lowercase `kp`/`kr`.
KV_RE = re.compile(r"([A-Za-z_]+)=([^,]*)")


def _fields(payload: str) -> dict[str, str]:
    return {match.group(1): match.group(2) for match in KV_RE.finditer(payload)}


# The device emits wire **codes**, not names: the capture text cap is 192 bytes and a full-name record
# was silently truncated on device (2026-09-24). These are the reverse of
# `LibraryObservation::causeCode` / `payloadCode` / `offeredCode` / `requestCode` / `terminalCode` and of
# the `StateBit` mask.
CAUSE_CODES = {
    "n": "not_applicable",
    "c": "catalog_omission",
    "r": "restore_refusal",
    "i": "intentional_eviction",
    "l": "content_loss",
    "a": "ambiguous",
    "u": "unknown",
}
PAYLOAD_CODES = {0: "unknown", 1: "absent", 2: "invalid_token", 3: "present_verified"}
OFFERED_CODES = {0: "unknown", 1: "not_offered", 2: "offered"}
REQUEST_CODES = {0: "unknown", 1: "none", 2: "queued", 3: "running", 4: "waiting", 5: "terminal"}
TERMINAL_CODES = {0: "none", 1: "committed", 2: "unnecessary", 3: "refused", 4: "superseded"}


def state_names_from_mask(mask: int) -> list[str]:
    names: list[str] = []
    if mask & 1:
        names.append("empty")
    if mask & 2:
        names.append("populated")
    if mask & 4:
        names.append("pending")
    return names


def slot_content_state(record: dict[str, Any]) -> Optional[bool]:
    """Tri-state content classification for one observed slot.

    ``True`` — populated by **either** representation: loaded committed content
    (`k`) or a verified SD payload (`p=3`, which the firmware treats as populated
    even with no loaded content).
    ``False`` — **confirmed** empty: the committed-content field was observed
    absent *and* the SD payload was observed absent.
    ``None`` — evidence unavailable (either field was not observed, or the payload
    token is unknown/invalid). Absence that was not observed must never be read as
    empty, and a slot that cannot be classified must be cleared, not skipped.
    """
    content_observed = bool(record.get("content_field_observed"))
    payload_observed = bool(record.get("payload_field_observed"))
    if (content_observed and record.get("has_committed_content")) or \
            record.get("payload") == "present_verified":
        return True
    if content_observed and not record.get("has_committed_content") and \
            payload_observed and record.get("payload") == "absent":
        return False
    return None


def observation_window(lines: Iterable[str]):
    """``(occupancy, device_complete)`` for the first observed family, else ``(None, None)``.

    Occupancy classifies each slot's union content from **all** the evidence it
    carries: loaded committed content (`has_committed_content`) and the verified SD
    payload token. An SD-only slot (verified payload, `k=0`) is populated, not
    empty; a slot is empty only when both representations were observed absent;
    anything else is unknown (``None``). A family whose terminal row never arrived
    returns ``(None, None)``: a lost window is not an empty library.

    ``device_complete`` is the device's own settle flag (`LIBOBS,E,…,x=`). A sweep that
    spans more than one service turn reports ``False`` even though every slot row is
    present and individually re-checked before emission — measured on device, where the
    64-slot sweep reports ``x=0``. Callers that only need per-slot content (target
    preparation) use the occupancy; a *settled* census must additionally require the flag.
    """
    observation = parse_observation_lines(lines)
    if not observation.get("window"):
        return None, None
    occupancy = {
        (int(record["track"]), int(record["slot"])): slot_content_state(record)
        for record in observation["slots"]
    }
    return occupancy, bool(observation.get("complete"))


def occupied_slots(lines: Iterable[str]) -> Optional[dict[tuple[int, int], Optional[bool]]]:
    """Per-slot content occupancy for this window, or None when the window was lost.

    Each value is tri-state: True populated, False confirmed empty, None unknown.
    """
    occupancy, _ = observation_window(lines)
    return occupancy


def parse_observation_lines(lines: Iterable[str]) -> dict[str, Any]:
    """Read back the device's on-demand library observation (`!LIB_OBSERVE`) from capture lines.

    Returns the per-slot records (decoded), the four D4 quantities, the window framing and the
    completeness flag. An observation whose `end` record never arrived — or that arrived with
    `complete=0` — is reported as **incomplete**, and its counts are not presented as the settled
    library state.
    """
    begin: dict[str, Any] | None = None
    end: dict[str, Any] | None = None
    slots: list[dict[str, Any]] = []
    for line in lines:
        match = LIBOBS_RE.search(line)
        if not match:
            continue
        fields = _fields(match.group("fields"))
        kind = match.group("kind")
        if kind == "S":
            mask = int(fields.get("st", "0"))
            # The two content representations are decoded when the capture carries them. A capture
            # written before the representation correction has neither key; it is reported as
            # **not observed** rather than as two false flags, so no reader can mistake "the old wire
            # could not separate them" for "this slot is backed by neither".
            representation_observed = "kp" in fields or "kr" in fields
            slots.append(
                {
                    "track": int(fields.get("t", "0")),
                    "slot": int(fields.get("s", "0")),
                    "state_mask": mask,
                    "state": state_names_from_mask(mask),
                    "cause": CAUSE_CODES.get(fields.get("c", "u"), "unknown"),
                    "payload": PAYLOAD_CODES.get(int(fields.get("p", "0")), "unknown"),
                    "offered": OFFERED_CODES.get(int(fields.get("o", "0")), "unknown"),
                    "request": REQUEST_CODES.get(int(fields.get("q", "0")), "unknown"),
                    "wait": int(fields.get("w", "0")),
                    "terminal": TERMINAL_CODES.get(int(fields.get("m", "0")), "none"),
                    "generation": int(fields.get("g", "0")),
                    "epoch": int(fields.get("e", "0")),
                    # `has_committed_content` is the **union** content signal (pass-backed, published
                    # root-backed, or both); the two representations are reported beside it. `kc` says
                    # whether the installed root currently covers the live committed-pass state — an
                    # installed-but-stale root is still content (playback reads it through the
                    # stale-root overlay), so it never defines `has_root_content`.
                    "has_committed_content": fields.get("k", "0") == "1",
                    "has_pass_content": fields.get("kp", "0") == "1",
                    "has_root_content": fields.get("kr", "0") == "1",
                    "root_is_current": fields.get("kc", "0") == "1",
                    "content_representation_observed": representation_observed,
                    # Field *presence*: a missing `k`/`p` is unavailable evidence, not an observed
                    # absence. `slot_content_state` needs both to prove emptiness.
                    "content_field_observed": "k" in fields,
                    "payload_field_observed": "p" in fields,
                    "committed_pass_count": int(fields.get("a", "0")),
                    "highest_committed_pass_id": int(fields.get("h", "0")),
                    "loop_length_ticks": int(fields.get("l", "0")),
                    "committed_event_count": int(fields.get("v", "0")),
                    "required_for_initial_playback": fields.get("n", "0") == "1",
                    "committed_terminal_in_window": fields.get("i", "0") == "1",
                    "invalidated": fields.get("x", "0") == "1",
                }
            )
        elif kind == "B":
            begin = fields
        elif kind == "E":
            end = fields
    counts: dict[str, int | None] = {
        "payload_inventory": None,
        "initial_playback_required": None,
        "observed_restore_completions": None,
        "committed_at_checkpoint": None,
    }
    content_representation_observed = False
    window: dict[str, Any] = {}
    if end is not None:
        content_representation_observed = "P" in end or "R" in end
        counts = {
            "payload_inventory": int(end.get("p", "0")),
            "initial_playback_required": int(end.get("i", "0")),
            "observed_restore_completions": int(end.get("c", "0")),
            # The loaded population: the union of the two representations. The representation counts
            # are separate fields, so the union can always be audited against them.
            "committed_at_checkpoint": int(end.get("m", "0")),
            "pass_backed_at_checkpoint": (
                int(end["P"]) if content_representation_observed else None
            ),
            "root_backed_at_checkpoint": (
                int(end["R"]) if content_representation_observed else None
            ),
        }
        window = {
            "start_us": int(end.get("a", "0")),
            "end_us": int(end.get("b", "0")),
            "turns": int(end.get("t", "0")),
            "restarts": int(end.get("r", "0")),
            "slots_observed": int(end.get("s", "0")),
            "admission_complete": end.get("n", "0") == "1",
            "every_initial_playback_slot_observed": end.get("e", "0") == "1",
            "consistent_view": end.get("v", "0") == "1",
            "complete": end.get("x", "0") == "1",
        }
    return {
        "requested": begin is not None or end is not None,
        "begin": begin,
        "end_fields": end,
        "slots": slots,
        "counts": counts,
        "content_representation_observed": content_representation_observed,
        "window": window,
        "complete": bool(window.get("complete")),
        "incomplete_window": not bool(window.get("complete")),
    }


def write_report(directory: Path, out_path: Path) -> dict[str, Any]:
    report = analyze_capture_dir(directory)
    out_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report
