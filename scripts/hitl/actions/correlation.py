"""Correlated gesture waits for the composable action layer (task 3.1 of `response-driven-hitl-acceptance`).

A press in this layer used to be fire-and-forget: it put MIDI on the wire and returned. The device, however,
answers a gesture in **four distinguishable stages**, and a flow that cannot tell them apart cannot tell "the
press was never recognised" from "it was recognised but refused" or from "it was accepted and then executed":

    SEND       the host put the gesture on the wire (host monotonic time, note, channel, press kind)
    RECOGNIZE  the device named the gesture it read (its own `Button press: Loop N (…)` / `Loop N: …` row)
    ACCEPT     the device took the request **for this identity** (an `ARMED` / accepted / queued row)
    EXECUTE    the device did it (the state edge / `Recording started` / the stop's admission or retirement)

`CorrelatedGesture` records those stages in order from device rows the caller supplies, and it refuses to
advance on evidence that is not this gesture's: every acceptable row must carry the identity **that wire form
uses** (a human row names the slot `Loop 4:`, an in-ring row names it `DISP,3,`), so a foreign slot's row cannot
substitute. A *drained* row older than the device clock observed before the press cannot substitute either --
that check reuses `hitl.device_time`, the canonical owner of the rule, instead of re-deriving it.

**Nothing here re-presses or retries**: a stage that never arrives stays pending and is reported by name, which
is the property the drivers kept re-deriving inline.

A row that answers this gesture *differently* -- the device read a double tap where a short press was sent, or
consumed the press into another outcome -- is neither silence nor this gesture's evidence: it is recorded as
`contradicted`. That is the same absence-versus-contradiction split the acceptance oracle uses (a missing edge is
a gap, a conflicting one is a failure), so a flow can name what happened instead of reporting a plain timeout.

**The action layer is the owner of SEND**: `record_send` is what every press helper in this package calls, so the
stage is recorded by the code that actually put the MIDI on the wire, once, instead of by each driver.

Time is injectable (`now` / `sleep`), so a bounded wait is testable without real time and without a device.

`PendingSwitchGate` is the other half of the same task: while a queued or re-affirmed playback switch is still
counting down, a follow-up press on the same button merges into the next gesture up (measured 2026-09-27: two
countdowns for one switch). The gate names that state from the rows; the policy (wait or abort) stays with the
flow.
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from enum import Enum
from typing import Callable, Optional, Sequence

from hitl.control_constants import CONTROL_CHANNEL_1BASED
from hitl.device_time import device_stamp_us, human_line_device_us

#: Longest accepted evidence text kept for a report, so a row cannot bloat the record.
_EVIDENCE_CHARS = 160


def record_send(correlate: Optional["CorrelatedGesture"], *, note: int, kind: str,
                before_device_us: Optional[int] = None,
                channel: int = CONTROL_CHANNEL_1BASED) -> None:
    """Record the SEND stage of `correlate` for a press just placed on the wire.

    Called by the press helpers themselves, which is the point: the stage is written where the note is sent, so
    no flow has to remember to declare its own gesture. `before_device_us` is the device clock the caller
    observed **before** the press; a caller that cannot supply it gets a gesture without that floor, which the
    record says honestly (`send.before_device_us is None`) rather than implying protection it does not have.
    """
    if correlate is None:
        return
    correlate.mark_sent(note=note, channel=channel, kind=kind, before_device_us=before_device_us)


class Stage(str, Enum):
    """The four stages a gesture passes through, in this order."""

    SEND = "send"
    RECOGNIZE = "recognize"
    ACCEPT = "accept"
    EXECUTE = "execute"


STAGE_ORDER: tuple[Stage, ...] = (Stage.SEND, Stage.RECOGNIZE, Stage.ACCEPT, Stage.EXECUTE)


class StageOutcome(str, Enum):
    PENDING = "pending"
    MET = "met"
    REFUSED = "refused"
    CONTRADICTED = "contradicted"


@dataclass(frozen=True)
class Wire:
    """One acceptable row for a stage: a substring, plus the identity tokens that row must also carry.

    Two families can answer the same stage -- the device's directly printed `Loop N: …` line and its in-ring
    `DISP,<slot0>,…` row -- and they name the same slot differently, so the identity lives here on each wire
    rather than once on the gesture. That is what keeps a foreign slot's row from substituting.
    """

    text: str
    identity: tuple[str, ...] = ()


@dataclass(frozen=True)
class StageRules:
    """What satisfies each stage. A stage with no entry is not awaited (it stays pending by design).

    `conflict` is the vocabulary of rows that prove the gesture was read as something *else* (another
    classification, or a press the device consumed into a different outcome). It defaults to empty: a caller
    that has no such rows gets a pending stage, never a guessed contradiction.
    """

    recognize: tuple[Wire, ...] = ()
    accept: tuple[Wire, ...] = ()
    execute: tuple[Wire, ...] = ()
    refusal: tuple[str, ...] = ()
    conflict: tuple[Wire, ...] = ()

    def any_of(self, stage: Stage) -> tuple[Wire, ...]:
        return {Stage.RECOGNIZE: self.recognize, Stage.ACCEPT: self.accept,
                Stage.EXECUTE: self.execute}.get(stage, ())


def _matches(wires: Sequence[Wire], line: str) -> Optional[Wire]:
    """The first wire whose text and identity tokens are all present in `line`, or None."""
    return next((w for w in wires if w.text in line and all(token in line for token in w.identity)), None)


def _row_clock_us(line: str) -> Optional[int]:
    """A row's own device clock, whichever of the two encodings it carries.

    Both families carry the same clock: `#CAP,<us>,…` writes `micros()` directly, and a directly printed logger
    line (`[<s>.<ms>] [LEVEL] …`) writes it as a formatted prefix. A row with neither carries none, and is live
    by construction (the firmware writes those straight to `Serial`, not through the capture ring).
    """
    stamp = device_stamp_us(line)
    return human_line_device_us(line) if stamp is None else stamp




@dataclass
class CorrelatedGesture:
    """One gesture's four stages, advanced only by evidence that is *this* gesture's and later than it."""

    name: str
    rules: StageRules = field(default_factory=StageRules)
    now: Callable[[], float] = time.monotonic
    sleep: Callable[[float], None] = time.sleep
    sent_at: Optional[float] = None
    sent_clock_us: Optional[int] = None
    outcomes: dict[Stage, StageOutcome] = field(default_factory=dict)
    evidence: dict[Stage, str] = field(default_factory=dict)
    send: dict[str, object] = field(default_factory=dict)

    def mark_sent(self, *, note: int, channel: int, kind: str,
                  before_device_us: Optional[int] = None) -> None:
        """Record the SEND stage. `before_device_us` is the device clock observed *before* the press.

        That floor is what makes the wait immune to drained history: the ring flushes in batches, so a row
        emitted before the press can land in the log *after* it, and without the floor it would read as this
        gesture's own answer.
        """
        self.sent_at = self.now()
        self.sent_clock_us = before_device_us
        self.send = {"note": note, "channel": channel, "kind": kind,
                     "before_device_us": before_device_us}
        self.outcomes[Stage.SEND] = StageOutcome.MET
        self.evidence[Stage.SEND] = f"{kind} note={note} channel={channel}"

    def awaited(self, stage: Stage) -> bool:
        """Whether this gesture waits for `stage` at all.

        A stage the caller declared no rule for is **not awaited** (`StageRules`' own contract), and SEND is
        awaited only by a gesture the host actually sent: a caller that never called `mark_sent` has a pure
        *state* wait (the driver's already-armed path, where the press happened in an earlier gesture) and its
        record says so -- `send` stays `pending` there rather than pretending otherwise.
        """
        if stage is Stage.SEND:
            return self.sent_at is not None
        return bool(self.rules.any_of(stage))

    def pending_stage(self) -> Optional[Stage]:
        for stage in STAGE_ORDER:
            if self.outcomes.get(stage) is StageOutcome.MET:
                continue
            if not self.awaited(stage):
                continue
            return stage
        return None

    def met(self, stage: Stage) -> bool:
        return self.outcomes.get(stage) is StageOutcome.MET

    def consider(self, line: str) -> Optional[Stage]:
        """Advance the next pending stage when `line` is this gesture's evidence for it.

        Returns the stage it advanced (or refused / contradicted), or None when the row is not usable evidence.
        Only the *next* pending stage can advance, so a row that only proves execution cannot skip recognition
        and acceptance: the stages stay distinct in the record instead of collapsing into "something happened".
        """
        stage = self.pending_stage()
        if stage is None or stage is Stage.SEND:
            return None
        if any(token in line for token in self.rules.refusal):
            self.outcomes[stage] = StageOutcome.REFUSED
            self.evidence[stage] = line.strip()[:_EVIDENCE_CHARS]
            return stage
        if _matches(self.rules.conflict, line) is not None:
            # Answered as something else: the press reached the device, but not as the gesture we sent.
            self.outcomes[stage] = StageOutcome.CONTRADICTED
            self.evidence[stage] = line.strip()[:_EVIDENCE_CHARS]
            return stage
        if _matches(self.rules.any_of(stage), line) is None:
            return None
        stamp = _row_clock_us(line)
        if stamp is not None and self.sent_clock_us is not None and stamp <= self.sent_clock_us:
            # At or below the floor: the floor is itself a stamp the host had already read, so this row cannot
            # be news. The ring flushes in batches and would otherwise read a drained record as the receipt.
            return None
        self.outcomes[stage] = StageOutcome.MET
        self.evidence[stage] = line.strip()[:_EVIDENCE_CHARS]
        return stage

    def wait_for(self, stage: Stage, rows: Callable[[], Sequence[str]], *, timeout_s: float,
                 poll_s: float = 0.02, cancel: Optional[Callable[[], bool]] = None) -> bool:
        """Bounded wait for `stage`, fed by `rows()`. Returns whether it was met; never presses anything.

        Every pass re-reads the whole snapshot the caller supplies (so a caller that must flush a lagging ring
        can do that inside `rows()`, on its own cadence), and a refused or contradicted stage returns False
        immediately rather than waiting out the bound.

        `cancel` is the run's own scope check (`hitl.actions.cancellation`): once it says the run is over, this
        wait stops at once instead of sleeping out the rest of its bound, because a watchdog that fired has
        already taken that decision. A cancelled wait is a *stopped* wait, not a met one, so a caller that needs
        to tell them apart asks its scope (whose reason is the cause).
        """
        deadline = self.now() + max(timeout_s, 0.0)
        while True:
            if cancel is not None and cancel():
                return False
            for line in rows():
                self.consider(line)
            if self.met(stage):
                return True
            if self.outcomes.get(stage) in (StageOutcome.REFUSED, StageOutcome.CONTRADICTED):
                return False
            if self.now() >= deadline:
                return False
            self.sleep(poll_s)

    def snapshot(self) -> dict[str, object]:
        """An immutable-shaped record of the gesture for a run report (`outcome` per stage, with evidence)."""
        return {
            "name": self.name,
            "send": dict(self.send),
            "stages": {stage.value: (self.outcomes.get(stage) or StageOutcome.PENDING).value
                       for stage in STAGE_ORDER},
            "evidence": {stage.value: self.evidence.get(stage, "") for stage in STAGE_ORDER},
        }

    def blocking_stage(self) -> Optional[str]:
        """The first stage that is not met, named for a failure message (None when every stage is met)."""
        stage = self.pending_stage()
        return None if stage is None else stage.value

    def contradicted(self) -> Optional[str]:
        """The stage the device answered as something else, named for the failure message, or None."""
        for stage in STAGE_ORDER:
            if self.outcomes.get(stage) is StageOutcome.CONTRADICTED:
                return stage.value
        return None


@dataclass(frozen=True)
class PendingSwitchGate:
    """Whether a requested capture-switch is still counting down, so a follow-up press must not be sent.

    `pending` rows are the ones a quantized switch leaves while it waits for its boundary; `released` rows are
    the ones that prove it landed. Both are caller-supplied so the gate carries no scenario's vocabulary.

    **Attribution is the guard**: a row only counts when it names this `target`, and the device's playback-start
    row (`[TRACK] Playback started @ tick …`) carries no slot or track at all, so it can never discharge a
    target's gate on its own -- which is why `Playback started` appears in the default `released` vocabulary: it
    is named so that a *caller* can see it is handled, and refused for naming nothing.
    """

    pending: tuple[str, ...] = ("Queued playback switch", "Reaffirmed playback switch")
    released: tuple[str, ...] = ("Switch gesture from slot", "Playback started")
    target: str = ""

    def state(self, lines: Sequence[str]) -> str:
        """`clear` (nothing pending), `pending` (still counting down), or `released` (it landed)."""
        pending_at = -1
        released_at = -1
        for index, line in enumerate(lines):
            if self.target not in line:
                continue                      # names no target: not this switch's evidence either way
            if any(token in line for token in self.released):
                released_at = index
            elif any(token in line for token in self.pending):
                pending_at = index
        if pending_at < 0:
            return "clear"
        return "released" if released_at > pending_at else "pending"

    def blocked(self, lines: Sequence[str]) -> bool:
        """True while a switch for this target is pending and has not been released by a later row."""
        return self.state(lines) == "pending"
