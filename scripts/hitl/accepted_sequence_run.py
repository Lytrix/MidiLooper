"""The accepted-sequence run scope: one session, one report, one leg (task 4.4b).

This is the *host orchestration* half of what used to be `scripts/hitl_accepted_sequence.py`'s `main`:

* it resolves the run's **target authorization** (task 4.1) and refuses before anything is touched;
* it owns the **session** — the managed capture child, the guarded MIDI output, the transport-stop/drain
  shutdown — and the run's **report** (`accepted-sequence.json`, written at every milestone, because a run
  that died silently is what the measured `rec-01`/`mixed-01`/`od-08` attempts produced);
* the **sequence** itself is the flow (`hitl.flows.accepted_sequence.drive_leg`), which appends to this
  module's `events`/`preparation` sinks and returns a `LegOutcome` this module writes.
* the verdict is not decided here: `scripts/hitl/accepted_sequence_probe.py` reads the capture.

Two entry points share it, which is the point of the split — the standalone CLI wrapper
(`scripts/hitl_accepted_sequence.py`, argument-compatible) and the registered scenario
(`hitl.scenarios.accepted_sequence`, driven by `host_midi_hitl.py run`). Neither owns a sequence of its own
and neither can drift from the other's behaviour, because the sequence and this scope have exactly one
implementation.
"""

from __future__ import annotations

import hashlib
import json
import subprocess
import time
import uuid
from dataclasses import dataclass
from pathlib import Path

import mido

from hitl.actions.cancellation import GuardedMidiOut, begin_run
from hitl.config import DEFAULT_CAPTURE_SERIAL_PORT
from hitl.device_shutdown import DeviceSession, refuse_reset_if_latched
from hitl.flows import stopped_preparation
from hitl.flows.accepted_sequence import (
    DEFAULT_ARM_TIMEOUT_S,
    DEFAULT_CENSUS_ATTEMPTS,
    DEFAULT_CENSUS_RETRY_DELAY_S,
    DEFAULT_PREDECESSOR_PUBLICATION_TIMEOUT_S,
    DEFAULT_SEED_SETTLE_TIMEOUT_S,
    LegRequest,
    LegSink,
    _Ctx,
    drive_leg,
    to_firmware_slot,
)
from hitl.managed_capture import ManagedCaptureSession
from hitl.midi_io import _find_midi_port
from hitl.midi_clock import MidiClockReader
from hitl.stimuli.authorization import AuthorizationRefused, authorize_targets

ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class RunRequest:
    """One leg run as the caller declares it: the CLI wrapper's arguments, resolved.

    Both entry points build this -- the argument-compatible wrapper from its own parsed flags, the
    registered scenario from the runner's translation -- so the run scope, the report and the sequence see
    exactly one shape.
    """

    leg: str
    track: int
    slot_a: int
    slot_b: int
    out_dir: Path
    authorization: str
    upload: bool = True
    boot_reset: bool = False
    build_env: str = "teensy41-capture-serial"
    capture_port: str = DEFAULT_CAPTURE_SERIAL_PORT
    boot_wait_s: float = 10.0
    upload_attempts: int = 3
    upload_retry_delay_s: float = 3.0
    press_ms: int = 120
    bars: int = 2
    notes: int = 16
    phase_timeout_s: float = 20.0
    settle_s: float = 8.0
    arm_timeout_s: float = DEFAULT_ARM_TIMEOUT_S
    seed_settle_timeout_s: float = DEFAULT_SEED_SETTLE_TIMEOUT_S
    predecessor_publication_timeout_s: float = DEFAULT_PREDECESSOR_PUBLICATION_TIMEOUT_S
    clear_targets: bool = True
    clear_attempts: int = 4
    clear_retry_delay_s: float = 4.0
    clear_timeout_s: float = stopped_preparation.DEFAULT_CLEAR_TIMEOUT_S
    census_attempts: int = DEFAULT_CENSUS_ATTEMPTS
    census_retry_delay_s: float = DEFAULT_CENSUS_RETRY_DELAY_S

    @property
    def track_index0(self) -> int:
        return self.track - 1

    @property
    def slot_a0(self) -> int:
        return to_firmware_slot(self.slot_a)

    @property
    def slot_b0(self) -> int:
        return to_firmware_slot(self.slot_b)


def build_identity() -> dict:
    """Best-effort build identity for the oracle's fresh-boot/identity gates.

    Same source of truth as the retention driver: the git commit plus the newest built `firmware.hex`
    under the PlatformIO cache for this env.
    """
    identity: dict = {}
    try:
        commit = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True,
                                cwd=str(ROOT), check=True).stdout.strip()
        if commit:
            identity["git_commit"] = commit
    except Exception:
        pass
    cache = Path.home() / "Library" / "Caches"
    candidates = sorted(cache.glob("pio-build-*/teensy41-capture-serial/firmware.hex"),
                        key=lambda p: p.stat().st_mtime, reverse=True)
    if candidates:
        digest = hashlib.sha256()
        with candidates[0].open("rb") as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b""):
                digest.update(chunk)
        identity["firmware_hex_sha256"] = digest.hexdigest()
    return identity


def _report_stimulus_id(leg: str) -> str:
    #  Each leg is driven as a temporary variant forked from the profile registered for its scenario (the
    #  sanctioned `stimulus temp` mechanism -- never an edit of a canonical stimulus): the record and overdub
    #  legs fork `STIM-BASE-2-ONE-OD-01`, and the **mixed** leg forks its own `STIM-BASE-2-MIXED-01`, whose
    #  declaration is what authorizes its two targets (A verified empty and idle, B seeded and verified).
    if leg == "mixed":
        return "STIM-BASE-2-MIXED-01-TEMP-ACCEPTED-SEQ-MIXED"
    return f"STIM-BASE-2-ONE-OD-01-TEMP-ACCEPTED-SEQ-{'REC' if leg == 'rec' else 'OD'}"


def run_accepted_sequence(request: RunRequest) -> int:
    """Run one leg end to end and return the run's exit code.

    Exit codes: 0 `DROVE_LEG`, 3 a named non-drive (preparation, transport, switch, switch-retention,
    unauthorized target, cancelled), 4 a latched shutdown refused the reset, 5 every upload attempt failed.
    """
    out_dir: Path = request.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=False)
    a0, b0, t0 = request.slot_a0, request.slot_b0, request.track_index0

    identity = build_identity()
    (out_dir / "metadata.json").write_text(json.dumps(identity, indent=2) + "\n")
    events: list[dict] = []
    preparation: list[dict] = []
    report: dict = {"leg": request.leg, "track": request.track,
                    "slot_a": request.slot_a, "slot_b": request.slot_b,
                    "slot_a_firmware": a0, "slot_b_firmware": b0,
                    "stimulus": _report_stimulus_id(request.leg),
                    "clear_targets": bool(request.clear_targets), "preparation": preparation,
                    "census": None, "identity": identity,
                    "events": events, "result": "INCOMPLETE", "upload": bool(request.upload)}

    def write_report() -> None:
        """Persist the report as the run progresses.

        Written at every milestone, not only at the end: `captures/s4f22-dec096-od-08` died inside the
        census decode and left **no** report at all, so its cause had to be reconstructed from the log --
        the same evidence gap `rec-01`/`mixed-01` had. A later write always supersedes an earlier one.
        """
        (out_dir / "accepted-sequence.json").write_text(json.dumps(report, indent=2) + "\n")

    # Task 4.1: the run's target authorization is resolved and enforced **here** -- before the board is
    # reset or re-uploaded, before the read-only census, and before any MIDI gesture -- so an unauthorized
    # target stops the run while there is still nothing to undo. A refused run still publishes a report:
    # the refusal names what was asked for and what was authorized.
    try:
        report["authorization"] = authorize_targets(request.authorization, track=request.track,
                                                   slots=(request.slot_a, request.slot_b))
    except AuthorizationRefused as exc:
        report["authorization"] = exc.record
        report["result"] = "UNAUTHORIZED_TARGET"
        events.append({"event": "unauthorized_target", "violations": list(exc.violations)})
        write_report()
        print(f"[s4f13] refusing before any gesture: {'; '.join(exc.violations)}")
        return 3

    if request.upload:
        refusal = refuse_reset_if_latched(tool="hitl_accepted_sequence")
        if refusal is not None:
            print(refusal)
            report["result"] = "RESET_REFUSED_AFTER_INCOMPLETE_SHUTDOWN"
            write_report()
            return 4
        # The upload is a **retried** step, and its failure is reported rather than raised. Measured
        # 2026-09-27: `teensy-gui` intermittently fails with "Teensy Loader is unable to read your compiled
        # sketch (r)" and then succeeds on the very next attempt, so a single failed attempt is not a reason
        # to abort a device session -- and `check=True` turned it into an unhandled traceback instead of a
        # legible result, which is the defect this replaces.
        upload_ok = False
        attempts: list[dict] = []
        for attempt in range(1, max(1, request.upload_attempts) + 1):
            print(f"[s4f13] re-uploading {request.build_env} to force a real reset "
                  f"(attempt {attempt}/{request.upload_attempts})")
            proc = subprocess.run(["pio", "run", "-e", request.build_env, "-t", "upload"],
                                  capture_output=True, text=True)
            attempts.append({"attempt": attempt, "returncode": proc.returncode})
            if proc.returncode == 0:
                upload_ok = True
                break
            tail = "\n".join((proc.stdout or "").splitlines()[-6:])
            print(f"[s4f13] upload attempt {attempt} failed (rc={proc.returncode}); tail:\n{tail}")
            time.sleep(request.upload_retry_delay_s)
        report["upload_attempts"] = attempts
        if not upload_ok:
            report["result"] = "UPLOAD_FAILED"
            report["session_log"] = None
            write_report()
            print("[s4f13] every upload attempt failed; no leg was driven and the device was left as it was")
            return 5
        time.sleep(1.0)

    with ManagedCaptureSession(port=request.capture_port, boot_wait_s=request.boot_wait_s,
                              out_dir=out_dir, run_token=uuid.uuid4().hex,
                              boot_reset=request.boot_reset) as capture:
        capture_log = capture.session_log
        if capture_log is None:
            raise RuntimeError("managed capture did not publish a session log")
        report["anchor_row"] = len(_lines(capture_log))
        # The capture ring holds rows until its owner is asked to flush them, so a receipt wait drains too.
        drain = getattr(capture, "drain_capture_ring", None)
        clock_reader = None
        try:
            clock_reader = MidiClockReader(mido.open_input(_find_midi_port("Teensy", is_input=True)))
        except Exception as exc:  # noqa: BLE001 - tests may use fake ports; the flow names a missing clock if needed.
            events.append({"event": "clock_reader_unavailable", "detail": f"{type(exc).__name__}: {exc}"})
        # The run's own scope and its guarded output. Every test edge goes through the guard, so a cancelled
        # run cannot press again; `guard.run()` below absorbs the cancellation, releases whatever was held,
        # and unregisters the run, while the shutdown inside it still stops the transport and drains.
        scene = begin_run(name=f"accepted-sequence-{request.leg}")
        out_port = GuardedMidiOut(mido.open_output(_find_midi_port("Teensy", is_input=False)), scope=scene)
        ctx = _Ctx(out_port, press_ms=request.press_ms)

        sink = LegSink(events=events, preparation=preparation)
        leg = LegRequest(
            leg=request.leg, track=request.track, slot_a=request.slot_a, slot_b=request.slot_b,
            press_ms=request.press_ms, bars=request.bars, notes=request.notes,
            phase_timeout_s=request.phase_timeout_s, settle_s=request.settle_s,
            arm_timeout_s=request.arm_timeout_s, seed_settle_timeout_s=request.seed_settle_timeout_s,
            predecessor_publication_timeout_s=request.predecessor_publication_timeout_s,
            clear_targets=request.clear_targets, clear_attempts=request.clear_attempts,
            clear_retry_delay_s=request.clear_retry_delay_s, clear_timeout_s=request.clear_timeout_s,
            census_attempts=request.census_attempts, census_retry_delay_s=request.census_retry_delay_s)

        def on_progress(outcome) -> None:
            """The flow's milestone hook: put what is known in the report and persist it."""
            report["census"] = outcome.census_state
            if outcome.preparation_error:
                report["preparation_error"] = outcome.preparation_error
            if outcome.result != "INCOMPLETE":
                report["result"] = outcome.result
            write_report()

        with out_port.run() as run_record, DeviceSession(
                capture_log_getter=lambda: capture_log,
                # The shutdown's transport stop is a teardown edge, not a test gesture: it runs in the guard's
                # permitted phase, so a cancelled run can still leave the device stopped and drained.
                send_transport_stop=lambda: out_port.teardown_call(lambda: _stop_transport(
                    out_port, capture_log, press_ms=request.press_ms, events=events, label="shutdown")),
                out_dir=out_dir,
                context=f"accepted-sequence-{request.leg}",
                events=events) as shutdown_session:

            write_report()
            outcome = drive_leg(leg, ctx=ctx, capture=capture, capture_log=capture_log,
                                out_port=out_port, sink=sink, drain=drain, on_progress=on_progress,
                                clock_reader=clock_reader)
            report["census"] = outcome.census_state
            report["plan"] = list(outcome.plan)
            report["identity_stats_rows"] = len(outcome.identity_stats_rows or [])
            if outcome.leg_start_row is not None:
                report["leg_start_row"] = outcome.leg_start_row
            report["leg_rows"] = outcome.leg_rows
            # The window's own end is the **flow's** mark, taken when the plan returned (it is the last thing
            # `drive_leg` knows before the settle): this point is already past the settle and the final
            # stopped-only stop, so using it would put the run's own teardown inside the measured window --
            # measured in `captures/s4f22-dec096-od-13`, where the final stop lands 8 s after the last press.
            # `None` is kept as a key so the report's shape is stable; the oracle reads it as "not bounded".
            report["leg_window_end_row"] = outcome.leg_window_end_row
            report["leg_end_row"] = len(_lines(capture_log))
            report["result"] = outcome.result
            if outcome.preparation_error:
                report["preparation_error"] = outcome.preparation_error
            write_report()

    # The run's own record, whether it finished or was cancelled. A cancelled run keeps the partial report it
    # has: the result names the cancel (with its reason and the guard's edge counts) instead of the milestone
    # it happened to stop at, and the process still exits nonzero below.
    report["midi"] = run_record.as_dict()
    if run_record.cancelled:
        report["result"] = "CANCELLED"
        report["cancel_reason"] = run_record.reason
        events.append({"event": "run_cancelled", "reason": run_record.reason,
                       "released_notes": run_record.released_notes,
                       "refused_after_cancel": run_record.record.get("refused_after_cancel")})
        print(f"[s4f13] run cancelled: {run_record.reason}", flush=True)
    report["shutdown"] = shutdown_session.result.as_dict()
    if not shutdown_session.result.ok:
        report["result"] = f"SHUTDOWN_{shutdown_session.result.status.upper()}"
    report["session_log"] = str(capture_log)
    write_report()

    print(json.dumps({k: v for k, v in report.items() if k not in ("events", "plan")}, indent=2))
    for e in events:
        print(f"  {e}")
    print(f"[s4f13] leg {request.leg}: {report['result']}; inspect with the oracle:")
    print(f"  .venv/bin/python scripts/hitl/accepted_sequence_probe.py --session-log {capture_log} "
          f"--slot-a {request.slot_a} --slot-b {request.slot_b} --leg {request.leg} "
          f"--metadata {out_dir / 'metadata.json'}")
    return 0 if report["result"] == "DROVE_LEG" else 3


def _lines(path: Path) -> list[str]:
    return path.read_text(encoding="utf-8", errors="replace").splitlines() if path.exists() else []


def _stop_transport(out_port, capture_log: Path, *, press_ms: int, events: list[dict],
                    label: str = "final") -> bool:
    """Stop the transport, tolerating a run that is already cancelled.

    The shutdown's own stop is a teardown edge: it runs inside the guard's permitted phase, so it must not
    raise the run's cancellation back into the shutdown. The flow owns the same operation for the leg's
    phases (`flows.accepted_sequence._ensure_transport`); this thin wrapper exists only so the shutdown can
    call it without importing a private name from the flow.
    """
    from hitl.flows.accepted_sequence import _ensure_transport

    return _ensure_transport(out_port, capture_log, running=False, press_ms=press_ms,
                             events=events, label=label)
