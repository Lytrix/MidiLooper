"""State-aware HITL control actions.

Never retry a state-changing control press unless evidence after the
baseline proves the requested transition did not happen.
"""

from __future__ import annotations

import time
from collections.abc import Callable
from typing import Optional

from hitl.contract import ActionEvidence, ActionOutcome, ActionResultKind
from hitl.serial_collector import RunAbort

ConfirmFn = Callable[[list[str], int], bool]
LatestStateFn = Callable[[list[str], int], Optional[str]]
SendFn = Callable[[], None]
SnapshotFn = Callable[[], list[str]]


def _matched_line_after(lines: list[str], baseline_index: int) -> tuple[Optional[int], Optional[str]]:
    if baseline_index < 0:
        baseline_index = 0
    for index in range(baseline_index, len(lines)):
        return index, lines[index]
    return None, None


def request_state_transition(
    *,
    snapshot: SnapshotFn,
    requested: str,
    already_satisfied: ConfirmFn,
    confirmed: ConfirmFn,
    disproved: ConfirmFn,
    send_request: SendFn,
    blocked: Optional[ConfirmFn] = None,
    timeout_s: float,
    max_retries: int = 1,
    abort: Optional[RunAbort] = None,
    latest_state: Optional[LatestStateFn] = None,
    poll_s: float = 0.01,
) -> ActionOutcome:
    """Observe baseline, request once, confirm, then reconcile before any retry.

    ``already_satisfied(lines, baseline)`` is evaluated on the pre-request snapshot.
    ``confirmed`` / ``blocked`` are polled during the wait.
    ``disproved`` is evaluated **only after** the wait timeout — remaining in the
    source state mid-wait is not proof the request failed. Early mid-wait disproof
    caused record-stop short presses to collapse into a double-press undo
    ([`session_20260910_122002.log`]).
    """
    lines = snapshot()
    baseline = len(lines)
    current_state = latest_state(lines, 0) if latest_state is not None else None
    if already_satisfied(lines, 0):
        match_index, match_line = _matched_line_after(lines, max(baseline - 1, 0))
        return ActionOutcome(
            ActionResultKind.ALREADY_SATISFIED,
            ActionEvidence(
                requested=requested,
                baseline_index=baseline,
                matched_line_index=match_index,
                matched_line=match_line,
                latest_state=current_state,
                retry_count=0,
                diagnostic="already_satisfied_before_request",
            ),
        )

    retries = 0
    send_request()
    while True:
        deadline = time.monotonic() + max(timeout_s, 0.0)
        while time.monotonic() < deadline:
            if abort is not None and abort.check() is not None:
                live = snapshot()
                return ActionOutcome(
                    ActionResultKind.EVIDENCE_UNAVAILABLE,
                    ActionEvidence(
                        requested=requested,
                        baseline_index=baseline,
                        latest_state=latest_state(live, baseline) if latest_state else None,
                        retry_count=retries,
                        diagnostic=str(abort.check()),
                    ),
                )
            live = snapshot()
            state = latest_state(live, baseline) if latest_state is not None else None
            if blocked is not None and blocked(live, baseline):
                match_index, match_line = _matched_line_after(live, baseline)
                return ActionOutcome(
                    ActionResultKind.RECOVERY_FAILED,
                    ActionEvidence(
                        requested=requested,
                        baseline_index=baseline,
                        matched_line_index=match_index,
                        matched_line=match_line,
                        latest_state=state,
                        retry_count=retries,
                        diagnostic="blocked",
                    ),
                )
            if confirmed(live, baseline):
                match_index, match_line = _matched_line_after(live, baseline)
                return ActionOutcome(
                    ActionResultKind.CONFIRMED,
                    ActionEvidence(
                        requested=requested,
                        baseline_index=baseline,
                        matched_line_index=match_index,
                        matched_line=match_line,
                        latest_state=state,
                        retry_count=retries,
                        diagnostic="confirmed",
                    ),
                )
            time.sleep(max(poll_s, 0.001))

        live = snapshot()
        state = latest_state(live, baseline) if latest_state is not None else None
        if blocked is not None and blocked(live, baseline):
            match_index, match_line = _matched_line_after(live, baseline)
            return ActionOutcome(
                ActionResultKind.RECOVERY_FAILED,
                ActionEvidence(
                    requested=requested,
                    baseline_index=baseline,
                    matched_line_index=match_index,
                    matched_line=match_line,
                    latest_state=state,
                    retry_count=retries,
                    diagnostic="blocked",
                ),
            )
        if confirmed(live, baseline):
            match_index, match_line = _matched_line_after(live, baseline)
            return ActionOutcome(
                ActionResultKind.CONFIRMED,
                ActionEvidence(
                    requested=requested,
                    baseline_index=baseline,
                    matched_line_index=match_index,
                    matched_line=match_line,
                    latest_state=state,
                    retry_count=retries,
                    diagnostic="confirmed_after_wait",
                ),
            )
        proved_absent = disproved(live, baseline)
        if not proved_absent:
            return ActionOutcome(
                ActionResultKind.EVIDENCE_UNAVAILABLE,
                ActionEvidence(
                    requested=requested,
                    baseline_index=baseline,
                    latest_state=state,
                    retry_count=retries,
                    diagnostic="timeout_without_disproof",
                ),
            )
        if retries >= max_retries:
            return ActionOutcome(
                ActionResultKind.RECOVERY_FAILED,
                ActionEvidence(
                    requested=requested,
                    baseline_index=baseline,
                    latest_state=state,
                    retry_count=retries,
                    diagnostic="disproved_after_retries",
                ),
            )
        retries += 1
        send_request()


def outcome_allows_continue(outcome: ActionOutcome) -> bool:
    return outcome.ok


def log_action_outcome(outcome: ActionOutcome, *, prefix: str = "[hitl]") -> None:
    ev = outcome.evidence
    print(
        f"{prefix} ACTION {outcome.kind} requested={ev.requested} "
        f"retries={ev.retry_count} state={ev.latest_state} {ev.diagnostic}"
    )
