"""The two response-driven acceptance legs as registered runner scenarios (task 4.4).

Design decision 4: *register separate record and overdub scenarios in the existing registry; the standalone
driver becomes an argument-compatible wrapper … with unsupported legacy combinations rejected explicitly.*
This module is the registration half:

- **Argument translation** from the runner's CLI to the wrapper's CLI, as a table (`PASSTHROUGH_FLAGS`)
  plus the derived flags, so a caller can see exactly what a run is given (`accepted_sequence_argv`).
- **Explicit refusals** (`UnsupportedCombination`): every combination the leg cannot honour is named
  *before* the run reaches the device — a mismatched `--leg`, an ambiguous first target, a missing second
  target, one target used twice, a note channel that is not the track's own, an unknown legacy flag.
- **The run-scoped authorization default**: a run launched through the runner is authorized by the
  registered profile's own manifest (task 4.1), so the runner path is stricter than the standalone CLI's
  `cli` default instead of silently wider.
- **The verifier** (`verify_accepted_sequence`) is the offline oracle itself, so `--verify-only` through
  the runner reaches the same verdict as the standalone oracle over the same log.

The leg **sequence** is the stateless flow (`hitl.flows.accepted_sequence.drive_leg`); the **session,
authorization and report** are the host run scope (`hitl.accepted_sequence_run`); the standalone CLI
(`scripts/hitl_accepted_sequence.py`) is the argument-compatible wrapper both entry points agree on. This
module declares intent (which leg, which targets, which authorization, which bounds) and owns the verdict.
"""

from __future__ import annotations

from pathlib import Path
from typing import Optional

#: Registered scenario id -> the leg the driver must drive.
LEG_FOR_SCENARIO: dict[str, str] = {
    "accepted_sequence_rec": "rec",
    "accepted_sequence_od": "od",
    "accepted_sequence_mixed": "mixed",
}

#: Registered scenario id -> the stimulus profile whose declared target scope authorizes the run (task
#: 4.1). A run through the runner is therefore authorized by the profile, not by whatever it was pointed at.
PROFILE_FOR_SCENARIO: dict[str, str] = {
    "accepted_sequence_rec": "STIM-BASE-2-2-01",
    "accepted_sequence_od": "STIM-BASE-2-ONE-OD-01",
    "accepted_sequence_mixed": "STIM-BASE-2-MIXED-01",
}

#: The wrapper flags a legacy argument list may carry through to the leg, verbatim.
PASSTHROUGH_FLAGS: tuple[str, ...] = (
    "--bars",
    "--notes",
    "--press-ms",
    "--settle-s",
    "--phase-timeout-s",
    "--arm-timeout-s",
    "--seed-settle-timeout-s",
    "--predecessor-publication-timeout-s",
    "--clear-attempts",
    "--clear-retry-delay-s",
    "--clear-timeout-s",
    "--census-attempts",
    "--census-retry-delay-s",
    "--upload-attempts",
    "--upload-retry-delay-s",
    "--boot-wait-s",
    "--capture-port",
)

#: Flags the wrapper itself takes a value for. The runner's own target bookkeeping names ride along with
#: them (`--track-number`/`--track`/`--loop-slot`/`--midi-channel`/`--out-dir` are appended by
#: `legacy_args_with_slot_flags`), so the loop must consume their value without forwarding it.
_VALUE_FLAGS: tuple[str, ...] = PASSTHROUGH_FLAGS + (
    "--authorization", "--slot-a", "--slot-b", "--leg",
    "--track-number", "--track", "--loop-slot", "--midi-channel", "--out-dir",
)

#: Flags the runner owns end to end and this scenario consumes without a value. Their presence is not an
#: error, it is simply not the leg's business.
_BARE_FLAGS: tuple[str, ...] = (
    "--no-clear-targets", "--clear-targets", "--layered", "--build-upload", "--verify-only",
    "--boot-reset", "--no-boot-reset", "--no-managed-capture", "--list-scenarios", "--list-stimuli",
    "--arm-snapshot-yield", "--follow-current-session", "--no-upload", "--upload",
)

#: Flags the runner owns end to end that carry a value this scenario consumes.
_RUNNER_VALUE_FLAGS: tuple[str, ...] = (
    "--verify-serial-log", "--build-env", "--follow-serial-log", "--serial-port",
    "--stimulus-temp-suffix", "--stimulus-temp-parent", "--stimulus-temp-reason",
)

#: The oracle's own evidence inputs, consumed by `verify_accepted_sequence` exactly as the standalone oracle
#: takes them. Without them a route would silently report a *weaker* verdict than the oracle on the same log
#: (the identity and capture-integrity predicates would be unknown), which would break the one thing 4.4
#: asks of the two entry points -- that they agree.
_EVIDENCE_VALUE_FLAGS: tuple[str, ...] = ("--metadata", "--base-report", "--content-report", "--timing-report")


class UnsupportedCombination(ValueError):
    """A combination the leg cannot honour, named before the run reaches the device."""


def _flag_value(legacy: list[str], flag: str) -> Optional[str]:
    for index, token in enumerate(legacy):
        if token == flag and index + 1 < len(legacy):
            return legacy[index + 1]
    return None


def _has_flag(legacy: list[str], flag: str) -> bool:
    return flag in legacy


def _int(value: object, *, flag: str) -> int:
    try:
        return int(str(value))
    except (TypeError, ValueError) as exc:
        raise UnsupportedCombination(f"{flag} needs an integer, got {value!r}") from exc


def accepted_sequence_targets(args: object, *, scenario_id: str) -> tuple[str, int, int, int]:
    """The leg's `(leg, track, slot_a, slot_b)`, or a named refusal.

    The leg comes from the registration, never from the command line: a `--leg` that disagrees with the
    scenario is a wrong-incarnation run, not a parameter.
    """
    if scenario_id not in LEG_FOR_SCENARIO:
        raise UnsupportedCombination(
            f"{scenario_id!r} is not a response-driven acceptance scenario "
            f"(known: {', '.join(sorted(LEG_FOR_SCENARIO))})")
    leg = LEG_FOR_SCENARIO[scenario_id]
    legacy = [str(token) for token in getattr(args, "legacy_args", []) or []]

    if _has_flag(legacy, "--leg"):
        asked = _flag_value(legacy, "--leg")
        if asked != leg:
            raise UnsupportedCombination(
                f"{scenario_id} drives the {leg!r} leg; --leg {asked!r} is a different incarnation of the "
                "same recipe and is not supported through this scenario")

    track = getattr(args, "track_number", None)
    if track is None:
        track = _flag_value(legacy, "--track-number") or _flag_value(legacy, "--track")
    if track is None:
        raise UnsupportedCombination(
            f"{scenario_id} needs the track it drives: pass --track <1-8> (no derived default)")
    track = _int(track, flag="--track")
    if not 1 <= track <= 8:
        raise UnsupportedCombination(f"--track must be 1-8, got {track}")

    loop_slot = getattr(args, "loop_slot", None)
    if loop_slot is None:
        loop_slot = _flag_value(legacy, "--loop-slot")
    if loop_slot is None:
        raise UnsupportedCombination(
            f"{scenario_id} needs its first target: pass --loop-slot <1-8> (the leg's slot A)")
    slot_a = _int(loop_slot, flag="--loop-slot")

    # `--slot-a` is the wrapper's own name for the same target; accepting both spellings is the point of
    # compatibility, but accepting two *different* values is not.
    explicit_a = _flag_value(legacy, "--slot-a")
    if explicit_a is not None and _int(explicit_a, flag="--slot-a") != slot_a:
        raise UnsupportedCombination(
            f"ambiguous first target: --loop-slot {slot_a} and --slot-a {explicit_a} disagree")

    explicit_b = _flag_value(legacy, "--slot-b")
    if explicit_b is None:
        raise UnsupportedCombination(
            f"{scenario_id} needs its second target: pass --slot-b <1-8> (the leg is defined by moving "
            "between two targets)")
    slot_b = _int(explicit_b, flag="--slot-b")
    if not 1 <= slot_a <= 8 or not 1 <= slot_b <= 8:
        raise UnsupportedCombination(f"slot targets must be 1-8, got --loop-slot {slot_a} --slot-b {slot_b}")
    if slot_a == slot_b:
        raise UnsupportedCombination(
            f"--loop-slot and --slot-b must differ, got {slot_a} twice: the leg is defined by moving "
            "between two targets")

    channel = getattr(args, "midi_channel", None)
    legacy_channel = _flag_value(legacy, "--midi-channel")
    if channel is not None and legacy_channel is not None and (
            _int(legacy_channel, flag="--midi-channel") != _int(channel, flag="--midi-channel")):
        raise UnsupportedCombination(
            f"ambiguous note channel: --midi-channel {channel} (runner) and --midi-channel "
            f"{legacy_channel} (legacy) disagree")
    if channel is None:
        channel = legacy_channel
    if channel is None:
        raise UnsupportedCombination(
            f"{scenario_id} needs --midi-channel (the runner requires it and this leg verifies it)")
    channel = _int(channel, flag="--midi-channel")
    if channel != track:
        # The leg injects note input on the channel the firmware maps to the track it drives
        # (`channel = track_index0 = track - 1` in the wrapper and in `_run_plan`). A different channel
        # would record into a target the run did not prepare.
        raise UnsupportedCombination(
            f"--midi-channel {channel} is not the track's own channel: this leg injects note input on "
            f"channel {track} (1-based) for --track {track}, so --midi-channel must equal --track")

    return leg, track, slot_a, slot_b


def _extra_passthrough(legacy: list[str]) -> list[str]:
    """The allowlisted wrapper flags, verbatim; anything the leg does not know is refused by name.

    A legacy argument list reaching a scenario is untranslated by definition (the runner parses with
    `parse_known_args`), so the refusal *is* the compatibility contract: a flag the leg cannot honour must
    stop the run here rather than be dropped or, worse, interpreted as something else.
    """
    out: list[str] = []
    index = 0
    while index < len(legacy):
        token = legacy[index]
        if not token.startswith("--"):
            raise UnsupportedCombination(
                f"unexpected positional argument {token!r}: this scenario takes flags only")
        if token in PASSTHROUGH_FLAGS:
            if index + 1 >= len(legacy):
                raise UnsupportedCombination(f"{token} needs a value")
            out.extend([token, legacy[index + 1]])
            index += 2
            continue
        if token in _VALUE_FLAGS:
            index += 2
            continue
        if token in _BARE_FLAGS:
            index += 1
            continue
        if token in _RUNNER_VALUE_FLAGS or token in _EVIDENCE_VALUE_FLAGS:
            index += 2
            continue
        raise UnsupportedCombination(
            f"unsupported flag {token!r} for this scenario. Accepted: the runner's target flags, the leg's "
            f"own --slot-a/--slot-b/--leg/--authorization/--no-clear-targets, and: "
            + ", ".join(PASSTHROUGH_FLAGS))
    return out


def accepted_sequence_argv(args: object, *, scenario_id: str) -> list[str]:
    """The wrapper's argv for this run: derived flags, the profile's authorization, the allowlist, verbatim."""
    leg, track, slot_a, slot_b = accepted_sequence_targets(args, scenario_id=scenario_id)
    legacy = [str(token) for token in getattr(args, "legacy_args", []) or []]

    authorization = _flag_value(legacy, "--authorization") or PROFILE_FOR_SCENARIO[scenario_id]
    out_dir = Path(getattr(args, "out_dir", Path("captures"))) / f"accepted-sequence-{leg}"

    argv = [
        "--leg", leg,
        "--track", str(track),
        "--slot-a", str(slot_a),
        "--slot-b", str(slot_b),
        "--authorization", str(authorization),
        "--out-dir", str(out_dir),
    ]
    argv.extend(_extra_passthrough(legacy))
    if _has_flag(legacy, "--no-clear-targets"):
        argv.append("--no-clear-targets")
    if getattr(args, "boot_reset", False) or _has_flag(legacy, "--boot-reset"):
        # This scenario owns its capture, so the runner's boot-reset decision reaches the wrapper, which
        # performs it inside the capture it opens.
        argv.append("--boot-reset")
    # The runner owns the firmware upload (`--build-upload`). When it has done it the leg must not repeat
    # it; otherwise the wrapper's own upload step (and its latched-shutdown refusal) is preserved exactly as
    # the standalone CLI performs it. An explicit `--upload`/`--no-upload` is the caller's own statement and
    # wins, because that is the wrapper's flag and dropping it silently would change the run.
    explicit = ("--no-upload" if _has_flag(legacy, "--no-upload")
                else ("--upload" if _has_flag(legacy, "--upload") else None))
    argv.append(explicit or ("--no-upload" if getattr(args, "build_upload", False) else "--upload"))
    return argv


def run_accepted_sequence(args: object) -> int:
    """Drive this scenario's leg through the argument-compatible wrapper."""
    scenario_id = str(getattr(args, "active_scenario_id", "") or "")
    try:
        argv = accepted_sequence_argv(args, scenario_id=scenario_id)
    except UnsupportedCombination as exc:
        print(f"[hitl] {scenario_id or 'accepted_sequence'}: unsupported combination: {exc}")
        return 2
    setattr(args, "accepted_sequence_argv", list(argv))
    print(f"[hitl] {scenario_id}: driving the leg through the standalone wrapper: {' '.join(argv)}", flush=True)
    from hitl_accepted_sequence import main as wrapper_main

    return wrapper_main(argv)


def verify_accepted_sequence(lines: list[str], args: object) -> dict[str, object]:
    """The offline oracle's verdict over this log: the same answer the standalone oracle reaches.

    `ok` means the oracle's `ACCEPTED_SEQUENCE_PASS`. Every other verdict is a recorded non-pass with its
    failing link named, so the runner's exit code carries the oracle's own distinction instead of a
    boolean that hides `SETUP_INCOMPLETE` behind `INSUFFICIENT_EVIDENCE`.
    """
    from hitl.accepted_sequence_probe import OK, analyse

    scenario_id = str(getattr(args, "active_scenario_id", "") or "")
    try:
        leg, _track, slot_a, slot_b = accepted_sequence_targets(args, scenario_id=scenario_id)
    except UnsupportedCombination as exc:
        return {"ok": False, "verdict": "UNSUPPORTED_COMBINATION", "issues": [str(exc)],
                "scenario_id": scenario_id}
    metadata, base_report, inputs = _oracle_evidence(args)
    analysis = analyse("\n".join(lines), leg=leg, slot_a=slot_a, slot_b=slot_b,
                       metadata=metadata, base_report=base_report, inputs=inputs)
    failures = [f"{check.name}: {check.detail}" for check in analysis.checks if check.status != OK]
    return {
        "ok": analysis.verdict == "ACCEPTED_SEQUENCE_PASS",
        "verdict": analysis.verdict,
        "leg": leg,
        "slot_a": slot_a,
        "slot_b": slot_b,
        "checks_total": len(analysis.checks),
        "issues": failures,
        "limitations": list(analysis.limitations),
    }


def _oracle_evidence(args: object) -> tuple[Optional[dict], Optional[dict], dict]:
    """`(metadata, base_report, inputs)` from the runner's arguments, as the standalone oracle reads them.

    A path that is absent or unreadable yields None, which the oracle already treats as "this evidence was
    not supplied" -- it does not invent a verdict from it. `inputs` is the conjunction's external halves: the
    content-parity and timing reports, plus the teardown record the run itself wrote (`--run-report`), so the
    runner route vetoes on exactly what the CLI route vetoes on.
    """
    import json
    from pathlib import Path as _Path

    legacy = [str(token) for token in getattr(args, "legacy_args", []) or []]

    def load(flag: str) -> Optional[dict]:
        raw = _flag_value(legacy, flag)
        if raw is None:
            return None
        path = _Path(raw)
        if not path.is_file():
            return None
        return json.loads(path.read_text(encoding="utf-8"))

    inputs = {
        "content_parity": load("--content-report"),
        "timing": load("--timing-report"),
        "shutdown": (load("--run-report") or {}).get("shutdown"),
    }
    return load("--metadata"), load("--base-report"), inputs
