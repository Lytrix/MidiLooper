"""Run-scoped target authorization: the manifest a driver is driven under (task 4.1).

The registry declares each profile's authorized targets once, beside its recipe
(`StimulusSpec.authorization`, authored in `registry.py`). This module turns a driver's
`--authorization` value into the record the run is driven under, and refuses a target
outside it **before** the run can mutate anything.

Three inputs, and none of them is silent:

  - **a stimulus id** -- that profile's declared manifest is consulted and enforced;
  - **`cli`** (the default, the compatibility path) -- the explicit CLI targets are the
    declared authority, and the report says so instead of pretending a manifest was checked;
  - **an id whose profile declares no manifest, or an unknown id** -- refused. A
    manifest-based run without a manifest is exactly the case this task exists to stop, so
    it fails closed rather than degrading to "no declaration, no check".

The refusal is a *record*, not a traceback: a run that stopped before mutating still says
what it was asked for and what was authorized, and the caller writes that into its report.
"""

from __future__ import annotations

from typing import Any, Iterable, Optional

from hitl.stimuli.registry import get_stimulus
from hitl.stimuli.spec import Authorization

CLI_SOURCE = "cli"


class AuthorizationRefused(RuntimeError):
    """The run's targets are not covered by the authorization it named."""

    def __init__(self, value: Optional[str], violations: Iterable[str], record: dict) -> None:
        self.value = value
        self.violations = tuple(violations)
        self.record = dict(record)
        super().__init__("; ".join(self.violations) or "unauthorized target")


def targets_text(*, track: int, slots: Iterable[int]) -> str:
    slot_list = list(slots)
    return f"track {track} slots " + (",".join(str(slot) for slot in slot_list) or "none")


def _record(
    value: Optional[str],
    *,
    track: int,
    slots: Iterable[int],
    source: str,
    stimulus_id: Optional[str] = None,
    fingerprint: Optional[str] = None,
    declared: Optional[Authorization] = None,
    notes: str = "",
) -> dict[str, Any]:
    slot_list = [int(slot) for slot in slots]
    return {
        "source": source,
        "value": value,
        "stimulus_id": stimulus_id,
        "fingerprint": fingerprint,
        "declared": None if declared is None else declared.as_dict(),
        "track": int(track),
        "slots": slot_list,
        "targets": targets_text(track=track, slots=slot_list),
        "notes": notes,
    }


def authorize_targets(
    value: Optional[str],
    *,
    track: int,
    slots: Iterable[int],
) -> dict[str, Any]:
    """The record this run is authorized under, or an `AuthorizationRefused`.

    `value` is `--authorization`: a stimulus id, `cli`, or `None`/empty for the default
    compatibility path. The caller must call this **before** its first gesture, its first
    clear and its first board reset: the whole point is that an unauthorized target stops
    the run while there is still nothing to undo.
    """
    if value in (None, "", CLI_SOURCE):
        return _record(
            value or CLI_SOURCE, track=track, slots=slots, source=CLI_SOURCE,
            notes=("the explicit CLI targets are the declared authority: no stimulus manifest was "
                   "consulted, so this run's scope is only as wide as its arguments"),
        )

    try:
        spec = get_stimulus(str(value))
    except KeyError as exc:
        name = str(value)
        raise AuthorizationRefused(
            name, [f"unknown authorization {name!r}: {exc}"],
            _record(name, track=track, slots=slots, source="unknown",
                    notes="no such stimulus is registered, so nothing authorized these targets"),
        ) from exc

    common = dict(
        track=track, slots=slots, source="manifest",
        stimulus_id=spec.stimulus_id, fingerprint=spec.fingerprint_key(),
    )
    declared = spec.authorization
    if declared is None:
        raise AuthorizationRefused(
            str(value),
            [f"{spec.fingerprint_key()} declares no authorized targets, so no run may be driven under it"],
            _record(str(value), declared=None, notes=(
                "the profile exists but carries no target authorization manifest; declare one "
                "(`StimulusSpec.authorization`) or drive the run on the explicit CLI path"),
                **common),
        )

    record = _record(str(value), declared=declared, notes=(
        f"authorized by {spec.fingerprint_key()}: {declared.source}"), **common)
    violations = declared.violations(track=track, slots=list(slots))
    if violations:
        raise AuthorizationRefused(str(value), violations, record)
    return record
