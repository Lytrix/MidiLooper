#!/usr/bin/env python3
"""S4f.13 — the accepted-sequence reproducer's argument-compatible CLI wrapper.

The sequence this drives is `hitl.flows.accepted_sequence`; the session, the authorization and the report
belong to `hitl.accepted_sequence_run`; the verdict belongs to `scripts/hitl/accepted_sequence_probe.py`.
This module keeps the flags, the exit codes and the artifact layout the recorded device recipes and the
oracle expect, and nothing else (task 4.4b: the standalone driver became a wrapper).

Two legs, each a **managed run** and each asserting the same acceptance contract (DEC-078 / invariant 31):
a stop is *accepted* on the first press, the successor starts while the predecessor still finalizes, and the
successor's own stop executes exactly once after the predecessor releases -- with no second press anywhere.

    (a) RECORD leg:   record A -> stop A (one press) -> switch to B -> record B -> stop B
    (b) OVERDUB leg:  seed B then A -> overdub A -> capture-switch to B (stops A)
                      -> overdub B -> stop B once -> continue transport

The overdub leg reuses the proven retention prefix. It does not substitute the retention run's empty C target
for the populated overdub successor B. A driven leg is not acceptance: interval identity, overlap, content and
timing remain separate evidence obligations.

Scoped stopped preparation is integrated (task 4.1): before each seed the flow long-presses that authorized
slot's own loop button and requires the firmware's own clear evidence (`CLEARED` or an idempotent
`ALREADY_EMPTY`); a refusal, abort, wrong target or unconfirmed request stops the leg before any record
gesture. No manual clear and no reboot prepare a repeat.

Stimulus provenance (AGENTS.md: never edit a canonical stimulus; no registered stimulus covers this shape):
each leg is a **TEMP variant** forked from `STIM-BASE-2-ONE-OD-01` via the sanctioned
`scripts/host_midi_hitl.py stimulus temp` mechanism -- `…-TEMP-ACCEPTED-SEQ-REC` and `…-TEMP-ACCEPTED-SEQ-OD`,
written under `captures/stimuli/` and deliberately **not registered**. The registered scenarios
(`accepted_sequence_rec`/`_od`) drive the same wrapper; promote a canonical stimulus only if the owner accepts
it as the reproducer.

Base convention: this CLI and the action layer are **1-based** (the loop-button number); the firmware's
`RECA`/`RECS`/`reentry`/`ws_release` slot fields are **0-based**; human `Loop N:` rows are 1-based. The single
conversion site is `hitl.flows.accepted_sequence.to_firmware_slot`.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from hitl.accepted_sequence_run import RunRequest, run_accepted_sequence  # noqa: E402
from hitl.config import DEFAULT_CAPTURE_SERIAL_PORT  # noqa: E402
from hitl.flows.accepted_sequence import (  # noqa: E402
    DEFAULT_ARM_TIMEOUT_S,
    DEFAULT_CENSUS_ATTEMPTS,
    DEFAULT_CENSUS_RETRY_DELAY_S,
    DEFAULT_PREDECESSOR_PUBLICATION_TIMEOUT_S,
    DEFAULT_SEED_SETTLE_TIMEOUT_S,
)
from hitl.flows import stopped_preparation  # noqa: E402
from hitl.stimuli.authorization import CLI_SOURCE  # noqa: E402


def build_parser() -> argparse.ArgumentParser:
    """The wrapper's own argument surface: unchanged from the pre-split driver, plus --authorization."""
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--leg", choices=("rec", "od"), required=True,
                   help="rec = S4f.13(a) record leg; od = S4f.13(b) overdub leg")
    p.add_argument("--track", type=int, required=True, choices=range(1, 9))
    p.add_argument("--slot-a", type=int, required=True, choices=range(1, 9))
    p.add_argument("--slot-b", type=int, required=True, choices=range(1, 9))
    p.add_argument("--out-dir", type=Path, required=True)
    p.add_argument("--press-ms", type=int, default=120)
    p.add_argument("--bars", type=int, default=2)
    p.add_argument("--notes", type=int, default=16)
    p.add_argument("--phase-timeout-s", type=float, default=20.0)
    p.add_argument("--settle-s", type=float, default=8.0)
    p.add_argument("--capture-port", default=DEFAULT_CAPTURE_SERIAL_PORT)
    p.add_argument("--boot-wait-s", type=float, default=10.0)
    p.add_argument("--upload", dest="upload", action="store_true", default=True)
    p.add_argument("--no-upload", dest="upload", action="store_false")
    p.add_argument("--upload-attempts", type=int, default=3,
                   help="`teensy-gui` fails intermittently and succeeds on a retry, so the upload is retried")
    p.add_argument("--upload-retry-delay-s", type=float, default=3.0)
    p.add_argument("--boot-reset", action="store_true",
                   help="Reset the board inside the capture so the boot banner makes this run "
                        "verdict-grade (the oracle's fresh-boot gate)")
    p.add_argument("--clear-attempts", type=int, default=4,
                   help="Bounded retries when the firmware refuses a clear while a save is in flight")
    p.add_argument("--clear-retry-delay-s", type=float, default=4.0)
    p.add_argument("--clear-timeout-s", type=float, default=stopped_preparation.DEFAULT_CLEAR_TIMEOUT_S,
                   help="Bound for one scoped stopped clear's own completion evidence")
    p.add_argument("--census-attempts", type=int, default=DEFAULT_CENSUS_ATTEMPTS,
                   help="Bounded retries of the read-only census; one lost window is not an unknown "
                        "library, and a census that still cannot be read refuses before any gesture")
    p.add_argument("--census-retry-delay-s", type=float, default=DEFAULT_CENSUS_RETRY_DELAY_S)
    p.add_argument("--arm-timeout-s", type=float, default=DEFAULT_ARM_TIMEOUT_S,
                   help="Bound for the device to name a gesture's effect with its own slot-scoped DISP "
                        "row (a selection change or an arm); a swallowed press never names one")
    p.add_argument("--seed-settle-timeout-s", type=float, default=DEFAULT_SEED_SETTLE_TIMEOUT_S,
                   help="Bound for a seeded take's finalization receipt after its commit stop (recorded, "
                        "not a gate)")
    p.add_argument("--predecessor-publication-timeout-s", type=float,
                   default=DEFAULT_PREDECESSOR_PUBLICATION_TIMEOUT_S,
                   help="Bound for the seeded take's *publication* (the stop obligation the device makes a "
                        "record admission wait on) before the leg's first gesture; measured ~40 s after the "
                        "stop on device")
    p.add_argument("--no-clear-targets", dest="clear_targets", action="store_false", default=True,
                   help="Do not clear the target slots first; requires them to be empty already")
    p.add_argument("--authorization", default=CLI_SOURCE,
                   help="Target authorization manifest (task 4.1): a stimulus id whose declared scope must "
                        "cover --track/--slot-a/--slot-b, or 'cli' (the default) for the explicit CLI targets "
                        "as the declared authority. A target outside the manifest is refused before any "
                        "gesture or board reset, and the report records the authorization the run was under.")
    return p


def request_from_args(args: argparse.Namespace) -> RunRequest:
    """The parsed flags as the run scope's request: one shape for both entry points."""
    return RunRequest(
        leg=args.leg, track=args.track, slot_a=args.slot_a, slot_b=args.slot_b,
        out_dir=args.out_dir, authorization=args.authorization, upload=bool(args.upload),
        boot_reset=bool(args.boot_reset), capture_port=args.capture_port,
        boot_wait_s=args.boot_wait_s, upload_attempts=args.upload_attempts,
        upload_retry_delay_s=args.upload_retry_delay_s, press_ms=args.press_ms, bars=args.bars,
        notes=args.notes, phase_timeout_s=args.phase_timeout_s, settle_s=args.settle_s,
        arm_timeout_s=args.arm_timeout_s, seed_settle_timeout_s=args.seed_settle_timeout_s,
        predecessor_publication_timeout_s=args.predecessor_publication_timeout_s,
        clear_targets=bool(args.clear_targets), clear_attempts=args.clear_attempts,
        clear_retry_delay_s=args.clear_retry_delay_s, clear_timeout_s=args.clear_timeout_s,
        census_attempts=args.census_attempts, census_retry_delay_s=args.census_retry_delay_s)


def main(argv: list[str] | None = None) -> int:
    p = build_parser()
    args = p.parse_args(argv)

    if args.slot_a == args.slot_b:
        p.error("--slot-a and --slot-b must differ: the leg is defined by switching between them")

    return run_accepted_sequence(request_from_args(args))


if __name__ == "__main__":
    raise SystemExit(main())
