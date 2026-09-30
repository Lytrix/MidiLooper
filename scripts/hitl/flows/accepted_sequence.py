"""The accepted-sequence leg sequence as a stateless flow (task 4.4b).

Design decision 4: *scenario modules declare intent; stateless flows compose actions and narrow observation
waits.* This module is the sequence itself, and it owns no session:

* the gestures are **data** (`leg_gestures`), so the order — which is the requirement — is asserted without a
  device;
* every wait is a narrow, bounded observation of the device's own slot-scoped rows
  (`CorrelatedGesture`, `await_markers`) with no send path of its own;
* scoped stopped preparation (task 4.1) runs through its own flow (`stopped_preparation.clear_slot`) and
  classifies only the firmware's evidence;
* the run's session — managed capture, guarded MIDI, report — belongs to the caller
  (`hitl.accepted_sequence_run`), which is also the report's owner: this module appends to the caller's
  `LegSink` (`events`, `preparation`) and returns a `LegOutcome` the caller writes.

Two legs, each a **managed run** and each asserting the same acceptance contract (DEC-078 / invariant 31): a
stop is *accepted* on the first press, the successor starts while the predecessor still finalizes, and the
successor's own stop executes exactly once after the predecessor releases — with no second press anywhere.

    (a) RECORD leg:   record A -> stop A (one press) -> switch to B -> record B -> stop B
    (b) OVERDUB leg:  seed B then A -> overdub A -> capture-switch to B (stops A)
                      -> overdub B -> stop B once -> continue transport

The overdub leg reuses the proven retention prefix (`hitl_retention_handoff.critical_window_gestures`). It
does not substitute the retention run's empty C target for the populated overdub successor B. A driven leg is
not acceptance: interval identity, overlap, content and timing remain separate evidence obligations.

Imports are deliberately limited to what a flow may reach (`hitl.actions`, `hitl.context`, `hitl.config`,
`hitl.flows`, plus the host helper modules that speak no protocol of their own). The one conversation that
needs the serial owner — the stopped-only per-family counter read-back — goes through the session's own
accessor (`ManagedCaptureSession.identity_stat_lines`), because neither a flow nor a scenario may import
`hitl.serial` and two callers asking that question must not each grow their own parser.

Base convention: the CLI, the action layer and this flow are **1-based** (the loop-button number); the
firmware's `RECA`/`RECS`/`reentry`/`ws_release` slot fields are **0-based**; human `Loop N:` rows are 1-based.
The single conversion site is `to_firmware_slot`.
"""

from __future__ import annotations

import re
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

import mido

from hitl.actions import slots
from hitl.actions.cancellation import active_cancel_scope, cancellable_sleep
from hitl.actions.correlation import (
    CorrelatedGesture,
    PendingSwitchGate,
    Stage,
    StageRules,
    Wire,
)
from hitl.actions.observation import await_markers
from hitl.control_constants import (
    CONTROL_CHANNEL_1BASED,
    GLOBAL_TRANSPORT_NOTE,
    LOOP_SELECT_NOTE_BASE,
    RECORD_BUTTON_NOTE,
    TRACK_SELECT_NOTE_BASE,
)
from hitl.device_time import device_stamp_us, human_line_device_us
from hitl.flows import stopped_preparation
from hitl.library_observation import observation_window
from hitl.midi_io import _send_short_press

TRANSPORT_STARTED = "Transport started"
TRANSPORT_STOPPED = "Transport stopped"


class PreparationFailure(RuntimeError):
    """Scoped stopped preparation did not complete for an authorized target.

    Raised instead of pressing on: the flow must stop before recording into a target whose clear was
    refused, aborted or never confirmed.
    """


@dataclass(frozen=True)
class Gesture:
    """One press in a leg, as data so the order can be asserted without a device."""

    step: str
    kind: str          # "track" | "slot_record" | "record" | "transport"
    value: int         # slot/track number (1-based); 0 for the buttons


def leg_gestures(leg: str, *, track: int, a: int, b: int) -> list[Gesture]:
    """Ordered user gestures; overdub uses the proven capture-switch stop route.

    For overdub, B's loop press stops A and switches in one action, the record
    button starts B, and B's loop button requests its stop once. Both A and B
    must already have committed seed content. No empty-slot carrier is assumed.
    """
    if leg == "rec":
        return [
            Gesture("select_track", "track", track),
            Gesture("a_record_start", "record", 0),      # arm/start the take on A
            Gesture("a_stop", "transport", 0),           # ONE press: stop A
            Gesture("b_switch_start", "slot_record", b),  # switch to B; B starts recording
            Gesture("b_stop", "record", 0),              # ONE press: stop B's session
        ]
    if leg == "od":
        # The switch IS A's stop. A separate stop first leaves PLAYING and routes B's
        # button through the SD-aware playback queue instead of the capture-switch.
        # Reuse the device-proven retention prefix; C is a separate record successor
        # and is deliberately not substituted for the populated overdub target B.
        from hitl_retention_handoff import critical_window_gestures
        return [Gesture(g.step, g.kind, g.value) for g in
                critical_window_gestures(track=track, a=a, b=b, c=8)
                if g.step != "c_switch_request"]
    raise ValueError(f"unknown leg {leg!r}")


def to_firmware_slot(slot_1based: int) -> int:
    """The one 1-based -> 0-based conversion site (button/track numbering -> firmware row fields)."""
    return slot_1based - 1


class _Ctx:
    """Minimal ActionContext stand-in: the slot actions need only `out_port` and `config.press_ms`."""

    class _Cfg:
        press_ms = 120

    def __init__(self, out_port, press_ms: int = 120):
        self.out_port = out_port
        self.config = self._Cfg()
        self.config.press_ms = press_ms


def _lines(path: Path) -> list[str]:
    return path.read_text(encoding="utf-8", errors="replace").splitlines() if path.exists() else []


def _wait_for(capture_log: Path, after: int, markers: tuple[str, ...], timeout_s: float) -> bool:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        tail = _lines(capture_log)[after:]
        if any(m in line for line in tail for m in markers):
            return True
        time.sleep(0.05)
    return False


def _transport_running(capture_log: Path) -> bool:
    state = False
    for line in _lines(capture_log):
        if TRANSPORT_STARTED in line:
            state = True
        elif TRANSPORT_STOPPED in line:
            state = False
    return state


def _ensure_transport(out_port, capture_log: Path, *, running: bool, press_ms: int,
                      events: list[dict], label: str = "", attempts: int = 3) -> bool:
    tag = f"{label}_" if label else ""
    for attempt in range(attempts):
        if _transport_running(capture_log) == running:
            events.append({"event": f"{tag}transport_{'start' if running else 'stop'}", "attempt": attempt})
            return True
        before = len(_lines(capture_log))
        _send_short_press(out_port, note=GLOBAL_TRANSPORT_NOTE,
                          channel_1based=CONTROL_CHANNEL_1BASED, press_ms=press_ms)
        _wait_for(capture_log, before, (TRANSPORT_STARTED if running else TRANSPORT_STOPPED,), timeout_s=5.0)
        time.sleep(0.5)
    ok = _transport_running(capture_log) == running
    events.append({"event": f"{tag}transport_{'start' if running else 'stop'}", "confirmed": ok})
    return ok


# A loop button's short/double/triple discrimination means the firmware holds the second tap until its tap
# window expires, and a repeat press inside that window merges. Measured on device 2026-09-27: my first leg
# pressed slot 5 twice 345 ms apart with a record press between them, and the firmware logged
#   Loop 5: Queued playback switch   ->  Loop 5: Reaffirmed playback switch   ->  Loop 5: Reaffirmed ...
# i.e. **two countdowns for the same switch**. The firmware side of that is `queuePlayingSlotSwitch`, which
# clears and re-requests without checking whether the same target is already pending, so the quantized switch
# restarts from the current tick. This capture-switch scenario aborts on a playback queue; an unattributed
# Playback-started row cannot prove that the requested target became active.
DEFAULT_TAP_WINDOW_S = 0.35


def _switch_pending(capture_log: Path, since: int, slot: int) -> bool:
    """An unexpected playback queue invalidates this capture-switch leg.

    The *rule* is the action layer's `PendingSwitchGate`, so which rows mean "a playback switch for this target
    is queued" has one owner, together with the fact that an unattributed `Playback started` (`[TRACK] …`, no
    slot) discharges nothing. What stays here is this leg's policy: the rows are read from the leg's own start
    mark, and **any** queue row for the target invalidates the premise -- even one a later
    `Loop N: Switch gesture from slot M` landed -- because this scenario requires the capture-switch route
    rather than a playback switch. Abort rather than press again.
    """
    gate = PendingSwitchGate(target=f"Loop {slot}:")
    return gate.state(_lines(capture_log)[max(since, 0):]) != "clear"


class _PressSpacing:
    """Keep a repeated press of the same note outside the firmware's tap window.

    A loop button discriminates short / double / triple / long, so the firmware *holds* the second tap until
    its window expires, and a repeat press inside it merges into the next gesture up. Measured on device
    2026-09-30 (`captures/s4f22-dec096-rec-01`): two `Loop 3` taps 0 ms apart were reported as
    `Button press: Loop 3 (double)` -- `OVERDUB_FOR_SLOT` -- so the seed's record press never happened at all.
    The same applies to a press that lands *before* a long press, which truncates it (measured 2026-09-28:
    900 ms requested, 458 ms recognized).
    """

    def __init__(self, min_gap_s: float = DEFAULT_TAP_WINDOW_S, *, sleep=None, now=None):
        self._min_gap_s = min_gap_s
        self._sleep = sleep
        self._now = now
        self._last_press: dict[int, float] = {}

    def before(self, note: int) -> float:
        """Block until `note` may be pressed again. Returns the seconds waited (0.0 when already outside)."""
        sleep = self._sleep or time.sleep
        now = (self._now or time.monotonic)()
        last = self._last_press.get(note)
        waited = 0.0
        if last is not None:
            gap = now - last
            if gap < self._min_gap_s:
                waited = self._min_gap_s - gap
                sleep(waited)
        self._last_press[note] = (self._now or time.monotonic)()
        return waited


#: The firmware's own per-slot display row: `#CAP,<us>,DISP,<slot0>,<STATE>,…`. A focus change emits one for
#: the newly focused slot and an arm/record transition emits one for the slot it belongs to, so it is the
#: slot-scoped receipt a gesture can be confirmed against -- and its **absence** is the evidence that the
#: gesture was swallowed. Measured (`captures/s4f22-dec096-mixed-01`): the press that did select its target
#: (`Loop 4 (short)` 17.021 s) left `#CAP,17202864,DISP,3,STOPPED,0,0`, while the two presses a pending
#: predecessor Record job swallowed (`Loop 3 (short)` 39.520 s and 42.647 s) left **no** `DISP` row for that
#: slot at all -- which is how a silent press used to turn into a leg with no take.
_SLOT_DISP_RE = re.compile(r"^#CAP,(\d+),DISP,(\d+),([A-Z_]+)")


def _slot_disp_state(line: str) -> Optional[tuple[int, str]]:
    """`(slot0, state)` of a `DISP` row, or None when the line is not one."""
    match = _SLOT_DISP_RE.match(line.strip())
    if match is None:
        return None
    return int(match.group(2)), match.group(3)


def _gesture_clock_us(capture_log: Path, after: int) -> Optional[int]:
    """The newest device clock the host had observed **before** the gesture at `after`.

    This is the floor for "did the device name this gesture's effect?": every human row
    (`[<s>.<ms>] [LEVEL] …`) carries the device clock, so the newest one before the gesture says how far the
    device's clock had advanced when the press was sent. A row stamped at or below it was emitted before the
    gesture and is never its receipt, however late the ring flushed it.

    Deliberately **not** the gesture's own press row: that row is only written once the tap window expires, and
    matching it by text picks the wrong press when several of the same button are already in the file --
    measured on device (`captures/s4f22-dec096-od-04`): the floor came out *later* than the gesture
    (70.557 s for a press at 58.199 s), so every current `RECORDING` row was rejected and a working seed was
    reported as never armed.
    """
    best: Optional[int] = None
    for line in _lines(capture_log)[:max(after, 0)]:
        stamp = human_line_device_us(line)
        if stamp is not None and (best is None or stamp > best):
            best = stamp
    return best


def read_family_counters(capture, *, timeout_s: float = 6.0) -> Optional[list[str]]:
    """Ask the session for its per-family counters; None when the session cannot answer.

    Task 2.2: the read-back is what makes a **counted** loss visible, so the leg's own capture can be gated on
    it (the oracle's `family_accounting`). The conversation belongs to the session
    (`ManagedCaptureSession.identity_stat_lines`), which owns the request, its fence and the decode: a flow may
    not import `hitl.serial`, and this must stay read-only and non-fatal -- a session that cannot answer costs
    the accounting evidence, never the leg.
    """
    read = getattr(capture, "identity_stat_lines", None)
    if read is None:
        return None
    try:
        return read(timeout_s=timeout_s)
    except Exception:  # a read-back must never be the thing that fails a run
        return None


def _drain_now(drain) -> None:
    """Ask the capture owner to flush the ring, tolerating a session that has already gone."""
    if drain is None:
        return
    try:
        drain()
    except Exception:  # a drain request must never be the thing that fails a run
        pass


def _slot_disp_rows(capture_log: Path, after: int, slot0: int,
                    states: Optional[tuple[str, ...]] = None,
                    human: tuple[str, ...] = ()) -> list[str]:
    """The rows after `after` that name the gesture: `slot0`'s `DISP` states, or one of the `human` markers.

    This is the row *source*, deliberately with no freshness rule of its own: whether a row is news belongs to
    the wait (`CorrelatedGesture` refuses anything at or below the device clock observed before the gesture) so
    the rule has one owner instead of a copy here. File order is not meaningful -- the capture ring flushes in
    batches and can put a take's later rows *before* an earlier flood of them (`captures/s4f22-dec096-od-03`),
    which is why nothing here filters by position.

    The `human` markers are the firmware's own logger rows, which are written **straight to `Serial`** and are
    therefore live by construction -- they are not held in the ring at all. That matters when the ring has not
    been flushed: measured (`captures/s4f22-dec096-od-10`) the seed's take started at 30.160 s
    (`[30.160] [TRACK] Recording started @ tick 0` with `Loop 5: Start Recording`) while its `DISP` rows only
    became readable after the take stopped at 35.864 s, so a `DISP`-only gate pressed the same button again --
    which the firmware reads as *Stop Recording* -- and refused the seed the device had actually made.
    """
    rows: list[str] = []
    for line in _lines(capture_log)[max(after, 0):]:
        parsed = _slot_disp_state(line)
        if parsed is not None and parsed[0] == slot0 and (states is None or parsed[1] in states):
            rows.append(line)
        elif human and any(marker in line for marker in human):
            rows.append(line)
    return rows


def _is_news(line: str, floor_us: Optional[int]) -> bool:
    """Whether `line` is news relative to the device clock `floor_us` (None: no floor was observed).

    Both families carry the same clock in different encodings (`#CAP,<us>,…` directly, a directly printed logger
    line as `[<s>.<ms>] …`); a row carrying neither has none and is live by construction.
    """
    if floor_us is None:
        return True
    stamp = device_stamp_us(line)
    if stamp is None:
        stamp = human_line_device_us(line)
    return stamp is None or stamp > floor_us


def _slot_receipt_wires(slot0: int, states: Optional[tuple[str, ...]],
                        human: tuple[str, ...]) -> tuple[Wire, ...]:
    """The rows that can be a receipt for `slot0`: its own `DISP` states, plus the named printed rows.

    Each wire carries the identity **its own family** uses -- a `DISP` row is the slot's 0-based index inside a
    `#CAP` row, a human row names the 1-based slot in the caller's own marker -- so the two families can answer
    the same stage without either one's identity substituting for the other's.
    """
    wires = [Wire(f"DISP,{slot0},{state}", ("#CAP,",)) for state in states] if states is not None \
        else [Wire(f"DISP,{slot0},", ("#CAP,",))]
    wires.extend(Wire(marker) for marker in human)
    return tuple(wires)


def _slot_ready_state(capture_log: Path, after: int, slot0: int, slot_number: int) -> Optional[str]:
    """The device's *current* readiness for `slot0` as of `after`: `"recording"`, `"armed"` or None.

    Read from the rows the firmware writes **straight to `Serial`** (`Loop N: Start Recording`,
    `Recording started @ tick`), so the answer does not wait on the capture ring: the ring can hold a take's
    rows for the whole take (`captures/s4f22-dec096-od-10`). `Recording started` is only trusted when the
    slot-scoped arm row for **this** slot came first, since that row names no slot. This is a *state read*, not
    a gesture wait, so it applies the freshness floor itself (`_is_news`) rather than a gesture's.
    """
    floor = _gesture_clock_us(capture_log, after)
    rows = [line for line in _slot_disp_rows(capture_log, after, slot0, None,
                                             (f"Loop {slot_number}: Start Recording",
                                              "Recording started @ tick"))
            if _is_news(line, floor)]
    state: Optional[str] = None
    for line in rows:
        if f"Loop {slot_number}: Start Recording" in line:
            state = "armed"
        elif state == "armed" and "Recording started @ tick" in line:
            state = "recording"
    return state


def _await_slot_disp(capture_log: Path, after: int, slot0: int,
                     states: Optional[tuple[str, ...]], timeout_s: float,
                     *, human: tuple[str, ...] = (), drain=None) -> Optional[str]:
    """Wait for a receipt naming `slot0`'s gesture; None when none arrives inside `timeout_s`.

    The wait is the action layer's (`CorrelatedGesture`): its staleness floor is the device clock observed
    **before** the gesture (`_gesture_clock_us`), so a row at or below it is never this gesture's receipt
    however late the ring flushed it, and the row must carry this slot's own identity. What stays here is the
    vocabulary (`states` on this slot's `DISP` rows, plus the `human` rows the firmware prints straight to
    `Serial`) and the ring flush, which this wait owns on its own cadence: the ring holds rows until the owner
    is asked to flush it and nothing else drains it during a seed phase, so evidence can otherwise be absent
    from the log for tens of seconds while the device has already done the thing (measured: `od-05`'s take
    committed, and the `RECS,stop,4,2118` row that proves it reached the log only on a later drain).

    Returns the row that decided it, for the report.
    """
    gesture = CorrelatedGesture(
        name=f"slot_{slot0}_receipt",
        rules=StageRules(accept=_slot_receipt_wires(slot0, states, human)),
        now=time.monotonic,
        sleep=time.sleep,
    )
    gesture.mark_sent(note=LOOP_SELECT_NOTE_BASE + slot0, channel=CONTROL_CHANNEL_1BASED,
                      kind="short", before_device_us=_gesture_clock_us(capture_log, after))
    next_drain = time.monotonic() + DRAIN_EVERY_S

    def rows() -> list[str]:
        nonlocal next_drain
        now = time.monotonic()
        if now >= next_drain:
            _drain_now(drain)
            next_drain = now + DRAIN_EVERY_S
        return _slot_disp_rows(capture_log, after, slot0, states, human)

    scope = active_cancel_scope()
    met = gesture.wait_for(Stage.ACCEPT, rows, timeout_s=timeout_s, poll_s=0.05,
                           cancel=(None if scope is None else lambda: scope.cancelled))
    if scope is not None:
        # A cancelled run leaves here with its own reason instead of being reported as a missing receipt.
        scope.check()
    if not met:
        return None
    return gesture.evidence[Stage.ACCEPT]


def _await_markers(capture_log: Path, after: int, markers: tuple[str, ...], timeout_s: float,
                   *, drain=None) -> bool:
    """Wait for any of `markers` in the rows after `after`, flushing the capture ring as it waits.

    The wait itself is the action layer's observation facade (`hitl.actions.observation`): one owner for the
    bound, the poll cadence, the ring-flush cadence and cancellation, with this flow supplying only its own
    row window. Cancel-aware like the receipt waits: a cancelled run stops here with its own reason instead of
    sitting out a commitment/persistence bound while the capture is being released.
    """
    scope = active_cancel_scope()
    found = await_markers(
        lambda: _lines(capture_log)[max(after, 0):], markers, timeout_s=timeout_s,
        poll_s=0.05,
        on_poll=(None if drain is None else lambda: _drain_now(drain)),
        cancel=(None if scope is None else lambda: scope.cancelled),
    )
    if scope is not None:
        scope.check()
    return found


#: How often a receipt wait may ask the owner to flush the capture ring.
DRAIN_EVERY_S = 2.0
#: The census is a read-only *request* whose answer depends on the device's own state, so it is retried inside
#: the run's bound before a target is declared undecidable. Two measured shapes need the retries:
#: a **lost** window (`captures/s4f22-dec096-od-02`: the first request was lost, the next answered 64 slots,
#: settled) and an **incomplete sweep while the boot restore is still running** (`…-od-06`/`…-od-07`: the
#: focus track's slots are restored at boot, so the census read 8, then 9, then 14 of 64 slots as known and the
#: run's own target stayed unknown -- the earlier runs read `occupied: 64, unknown: 0` because their census
#: landed after the restore had finished). The budget is sized for that restore, not for the request.
DEFAULT_CENSUS_ATTEMPTS = 8
DEFAULT_CENSUS_RETRY_DELAY_S = 4.0
#: Bound for the device to name a gesture's effect with its own slot-scoped `DISP` row. Measured: a selection
#: change appears in ~0.2 s and an arm in ~0.04 s, while a *swallowed* press never appears at all.
DEFAULT_ARM_TIMEOUT_S = 6.0
#: Bounded seed presses, each gated on a slot-scoped `DISP … ARMED` row. Measured on device
#: (`captures/s4f22-dec096-mixed-01`): press 1 left no row at all (a predecessor's in-flight job held the
#: selection), press 2 only moved the focus (`DISP,2,STOPPED`), so the arm takes a third press once the
#: target really is selected -- while a *healthy* target arms on the first one and never reaches the bound.
#: A target the run has *already* armed (the clear's own selection press does that on an empty slot) is not
#: pressed at all: that press is a toggle (`captures/s4f22-dec096-od-10`).
DEFAULT_SEED_PRESS_ATTEMPTS = 3
#: Bound for the seeded take's finalization receipt after its commit stop. Informational: the seed's own
#: selection/arm receipts are the gates, and this only records whether the release edge arrived.
DEFAULT_SEED_SETTLE_TIMEOUT_S = 10.0


#: Bound for the seeded take's *publication* before the leg's first gesture. A record stop's deferred stages
#: (`finalize` / `visual_cache_request` / `revt_queue` / `save_request`) outlive the seed by tens of seconds on a
#: fresh boot, and the device refuses a record admission until the obligation is gone: measured
#: (`captures/s4f22-dec096-od-12`) slot 3's publication landed 40 s after its stop, which is what
#: `record admission deferred while a stop obligation is live for slot 3` was waiting for.
DEFAULT_PREDECESSOR_PUBLICATION_TIMEOUT_S = 75.0


def _leg_deferral_row(capture_log: Path, after: int) -> str:
    """The device's own row explaining why it deferred a record admission after `after`, or ""."""
    return next((line.strip() for line in _lines(capture_log)[max(after, 0):]
                 if "record admission deferred" in line or "Overdub start rejected" in line), "")


def _run_plan(out_port, capture_log: Path, plan: list[Gesture], *, slot_b: int,
              press_ms: int, phase_timeout_s: float, events: list[dict],
              note_channel: int = 7, overdub_notes: int = 16, drain=None) -> str:
    """Drive one bounded leg, stopping before the next press on a bad transition."""
    leg_mark = len(_lines(capture_log))
    last_slot_press_at = 0.0
    for gesture in plan:
        if _switch_pending(capture_log, leg_mark, slot_b):
            events.append({"event": "leg_aborted_pending_switch", "gesture": gesture.step})
            return "UNEXPECTED_PLAYBACK_SWITCH"
        if any("switch and record start retained" in line
               for line in _lines(capture_log)[leg_mark:]):
            events.append({"event": "leg_aborted_retained_switch", "gesture": gesture.step})
            return "UNEXPECTED_RETAINED_SWITCH"
        if gesture.step in ("a_record_start", "b_switch_start"):
            if not _ensure_transport(out_port, capture_log, running=True, press_ms=press_ms,
                                     events=events, label=gesture.step):
                return "TRANSPORT_START_UNCONFIRMED"
        if gesture.kind == "slot_record":
            gap = time.monotonic() - last_slot_press_at
            if gap < DEFAULT_TAP_WINDOW_S:
                time.sleep(DEFAULT_TAP_WINDOW_S - gap)
            last_slot_press_at = time.monotonic()
        mark = len(_lines(capture_log))
        _emit(out_port, gesture, press_ms=press_ms)
        events.append({"event": f"gesture:{gesture.step}", "line": mark,
                       "host_monotonic_ns": time.monotonic_ns()})
        if gesture.step in ("a_overdub_start", "b_overdub_start"):
            if not _await_markers(capture_log, mark, ("Overdubbing started @ tick",), phase_timeout_s,
                                  drain=drain):
                # The device names its own reason when it defers: `record admission deferred while a stop
                # obligation is live for slot N` (measured, `captures/s4f22-dec096-od-11`, where the
                # predecessor take's publication only landed 38 s after its stop -- long after the seed's own
                # settle bound). A deferred admission is not an absent take, so it is reported as what the
                # device said instead of a bare "start unconfirmed".
                device_row = _leg_deferral_row(capture_log, mark)
                events.append({"event": "leg_aborted_start_unconfirmed", "gesture": gesture.step,
                               "device_row": device_row[:180]})
                return "OVERDUB_START_DEFERRED" if device_row else "OVERDUB_START_UNCONFIRMED"
            # An empty overdub retires too quickly to stage the successor. Give
            # A real work, and B a distinct note for the subsequent content check.
            count = max(1, overdub_notes) if gesture.step == "a_overdub_start" else 1
            base = 60 if gesture.step == "a_overdub_start" else 79
            for i in range(count):
                pitch = base + i % 12
                out_port.send(mido.Message("note_on", note=pitch, velocity=100, channel=note_channel))
                time.sleep(0.02)
                out_port.send(mido.Message("note_off", note=pitch, velocity=0, channel=note_channel))
            events.append({"event": f"{gesture.step}_notes", "count": count,
                           "channel": note_channel, "note_base": base})
        elif gesture.step == "b_switch":
            if not _await_markers(capture_log, mark,
                                  (f"Loop {slot_b}: Switch gesture from slot ",
                                   f"Loop {slot_b}: Queued playback switch",
                                   f"Loop {slot_b}: Reaffirmed playback switch"),
                                  phase_timeout_s, drain=drain):
                return "SWITCH_UNCONFIRMED"
        time.sleep(DEFAULT_TAP_WINDOW_S)
    return "DROVE_LEG"


def _emit(out_port, gesture: Gesture, *, press_ms: int) -> None:
    if gesture.kind == "track":
        slots.press_track(_Ctx(out_port), gesture.value, press_ms=press_ms)
    elif gesture.kind == "slot_record":
        slots.press_slot_record(_Ctx(out_port), gesture.value, press_ms=press_ms)
    elif gesture.kind == "record":
        _send_short_press(out_port, note=RECORD_BUTTON_NOTE,
                          channel_1based=CONTROL_CHANNEL_1BASED, press_ms=press_ms)
    elif gesture.kind == "transport":
        _send_short_press(out_port, note=GLOBAL_TRANSPORT_NOTE,
                          channel_1based=CONTROL_CHANNEL_1BASED, press_ms=press_ms)
    else:  # pragma: no cover - guarded by the plan
        raise ValueError(f"unknown gesture kind {gesture.kind!r}")


def _run_census_once(capture, capture_log: Path, events: list[dict]) -> Optional[dict]:
    """One read-only per-slot occupancy request; None when it cannot be trusted.

    The observation is requested through the single serial owner and decoded once by the
    library-observation owner. An incomplete window returns None; the caller retries it a
    bounded number of times and then refuses by name rather than treating "unknown" as
    "empty" or pressing into an unknown target.
    """
    nonce, end_nonce = uuid.uuid4().hex, uuid.uuid4().hex
    ok, detail = capture.send_command("!LIB_OBSERVE", nonce=nonce, end_nonce=end_nonce)
    if not ok:
        events.append({"event": "census_refused", "detail": detail})
        return None
    family = capture.library_observation(nonce, end_nonce, timeout_s=45.0)
    if family is None:
        events.append({"event": "census_lost"})
        return None
    # Decode ONLY this request's fenced family. Re-parsing the whole log can pick an
    # earlier family (a boot-time census or a superseded request) and report the wrong
    # library, which then mis-drives target preparation. A family the decoder cannot read is a
    # *named* unreadable observation, never a traceback: measured on device
    # (`captures/s4f22-dec096-od-08`) the `LIBOBS,E` row carried an empty `p=` field and the
    # decode raised inside `library_observation.observation_window`, which killed the run before it
    # could write a report -- the same defect class this slice exists to remove.
    try:
        occupied, device_complete = observation_window(family.rows)
    except Exception as exc:  # noqa: BLE001 - any decode failure is an untrustworthy observation
        events.append({"event": "census_unreadable", "detail": f"{type(exc).__name__}: {exc}",
                       "rows": len(getattr(family, "rows", []) or []),
                       "tail": [line.strip() for line in (getattr(family, "rows", []) or [])[-2:]]})
        return None
    events.append({"event": "census",
                   "slots": 0 if occupied is None else len(occupied),
                   "occupied": 0 if occupied is None else sum(1 for v in occupied.values() if v is True),
                   "unknown": 0 if occupied is None else sum(1 for v in occupied.values() if v is None),
                   "device_settled": bool(device_complete)})
    return occupied


def _run_census(capture, capture_log: Path, events: list[dict], *,
                attempts: int = DEFAULT_CENSUS_ATTEMPTS,
                retry_delay_s: float = DEFAULT_CENSUS_RETRY_DELAY_S,
                require: tuple[tuple[int, int], ...] = ()) -> Optional[dict]:
    """The census with a bounded retry, because one bad window is not an unknown library.

    Two measured shapes are delivery outcomes rather than properties of the library, and both are retried inside
    the same bound: a **lost** window (`captures/s4f22-dec096-od-02`: the first request was lost, the next
    answered 64 slots and a settled device) and an **incomplete sweep** (`…-od-06`: the family closed with only
    8 of 64 slots carrying evidence and `x=0`, so 56 slots read as unknown). `require` names the targets the
    caller must actually be able to decide -- the retry keeps trying while any of them is unknown, and the last
    observation is returned either way, so the caller refuses by name rather than treating unknown as empty.
    """
    total = max(1, attempts)
    occupied: Optional[dict] = None
    for attempt in range(total):
        occupied = _run_census_once(capture, capture_log, events)
        if occupied is not None and all(occupied.get(key) is not None for key in require):
            return occupied
        if attempt + 1 < total:
            events.append({"event": "census_retry", "attempt": attempt + 1})
            time.sleep(retry_delay_s)
    return occupied


@dataclass(frozen=True)
class LegRequest:
    """What one leg run *is*, with no live object in it: the intent the caller declares."""

    leg: str
    track: int
    slot_a: int
    slot_b: int
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


@dataclass
class LegSink:
    """The caller's report sinks: this flow appends, the run scope owns the report and writes it."""

    events: list[dict] = field(default_factory=list)
    preparation: list[dict] = field(default_factory=list)


@dataclass
class LegOutcome:
    """What the run's report needs from the leg, and nothing that belongs to the report itself."""

    result: str = "INCOMPLETE"
    census: Optional[dict] = None
    plan: list[dict] = field(default_factory=list)
    identity_stats_rows: Optional[list[str]] = None
    leg_start_row: Optional[int] = None
    leg_rows: int = 0
    preparation_error: str = ""
    census_state: Optional[dict] = None

def _progress(outcome: "LegOutcome", on_progress) -> None:
    """Report a milestone to the caller, whose report must exist even if this run dies next.

    Measured (`captures/s4f22-dec096-od-08`): the run died inside the census decode and left **no** report at
    all, so its cause had to be reconstructed from the log. The caller's hook is what persists what is known
    so far, which is why the flow calls it as soon as the census and the preparation outcome exist.
    """
    if on_progress is not None:
        on_progress(outcome)



def drive_leg(request: LegRequest, *, ctx, capture, capture_log: Path, out_port,
              sink: LegSink, drain=None, on_progress=None) -> LegOutcome:
    """Prepare both targets, drive this request's leg, then read the leg's own accounting.

    The sequence in order (each step's evidence is the device's own):

    1. a bounded, read-only census of the targets the run actually has to decide;
    2. scoped stopped preparation per target (`stopped_preparation.clear_slot`), skipped for an
       independently confirmed empty target and refused before any gesture when the census is unavailable;
    3. a seeded, committed take per target, every step gated on that slot's own receipt;
    4. the leg's gesture plan, driven with no second press anywhere;
    5. the final transport stop and the stopped-only per-family counter read-back.

    Every failure is named and returned rather than raised: the caller still writes its report, which is what
    the measured `rec-01`/`mixed-01`/`od-08` runs failed to do.
    """
    events, preparation = sink.events, sink.preparation
    a0, b0, t0 = request.slot_a0, request.slot_b0, request.track_index0
    outcome = LegOutcome()

    census = (_run_census(capture, capture_log, events, attempts=request.census_attempts,
                          retry_delay_s=request.census_retry_delay_s,
                          require=((t0, a0), (t0, b0)))
              if request.clear_targets else None)
    outcome.census = census
    # The keys are the census's own `(track_index0, slot0)` pairs, exactly as the read-only observation
    # reported them -- not this run's 1-based CLI numbering, which is a different coordinate system.
    outcome.census_state = (None if census is None else
                            {f"{track}:{slot}": (None if value is None else bool(value))
                             for (track, slot), value in sorted(census.items())})

    # Milestone: the census and its decision exist, so the caller persists the report now -- measured
    # (`captures/s4f22-dec096-od-08`) a run that dies after this point must still leave its cause behind.
    _progress(outcome, on_progress)

    spacing = _PressSpacing()

    def _press_slot(slot_number: int) -> None:
        """Short-press a loop button, never inside another press's tap window.

        A repeat press inside the firmware's window merges into the next gesture up
        (`Loop N (double)` = `OVERDUB_FOR_SLOT`), which is how `captures/s4f22-dec096-rec-01` lost its
        seed press entirely: two taps of the same button arrived 0 ms apart.
        """
        spacing.before(LOOP_SELECT_NOTE_BASE + slot_number - 1)
        slots.press_slot_record(ctx, slot_number, press_ms=request.press_ms)

    def seed(slot_number: int, label: str, note_base: int) -> None:
        """Record a short take into `slot_number`, committed, so it is a *filled* slot afterwards.

        The overdub leg seeds B first and A last, as in the retention
        driver. A short press while A is capturing then stops A and
        switches to populated B, which can actually overdub.

        Every step is confirmed from the device's own slot-scoped evidence, and every abort is a
        named `PreparationFailure` -- which the caller catches, so the run still writes its report
        instead of dying with a traceback (measured: `captures/s4f22-dec096-rec-01` and
        `…-mixed-01` produced no report at all, so their cause had to be reconstructed from the log).

        * the census decides whether a clear is needed; an **unavailable** census refuses
          *before any gesture*, because a lost census made this driver long-press a filled,
          non-selected target, which the firmware answers by *starting playback*
          (`Loop N: long-press start (not playing)`), never by clearing
          (`captures/s4f22-dec096-od-02`);
        * a filled target is selected first and the selection must be named by its own `DISP`
          row, because a long press on a filled, non-selected slot starts playback;
        * the seed press must produce a slot-scoped `DISP … ARMED` row for **this** slot: a
          predecessor's in-flight stop can hold the selection, and the firmware then swallows the
          gesture silently (`captures/s4f22-dec096-mixed-01`: `Loop 3 (short)` at 39.520 s and
          42.647 s left no slot-3 row at all). Silence is therefore a named failure here, never a
          blind re-press plus a transport start.
        """
        slot0 = to_firmware_slot(slot_number)
        spacing.before(TRACK_SELECT_NOTE_BASE + request.track - 1)
        slots.press_track(ctx, request.track, press_ms=request.press_ms)
        time.sleep(0.4)
        if not _ensure_transport(out_port, capture_log, running=False, press_ms=request.press_ms,
                                 events=events, label=f"seed_{label}_stop"):
            raise PreparationFailure("could not stop the transport before seeding")
        # The window for this seed's own readiness starts here, before the clear: the clear's
        # selection press is what arms an *empty* target, so its rows are part of this seed.
        seed_mark = len(_lines(capture_log))
        if request.clear_targets:
            # Scoped stopped preparation (task 4.1): the read-only census decides
            # whether a clear is needed at all, then the target's own loop-button
            # gesture clears it and the firmware's evidence must confirm. No reboot,
            # no manual step, and a lost census never reads as "empty" -- it refuses
            # before any gesture (see the docstring).
            known_occupied = census.get((t0, slot0)) if census is not None else None
            if known_occupied is None:
                observed = [e for e in events if e.get("event") == "census"]
                last = observed[-1] if observed else {}
                events.append({"event": f"prepare_slot_{label}_census_unavailable",
                               "slot": slot_number, **last})
                raise PreparationFailure(
                    f"census unavailable for slot {slot_number} after "
                    f"{request.census_attempts} attempt(s); "
                    + (f"the last observation read {last['occupied']} occupied / "
                       f"{last['unknown']} unknown of {last['slots']} slots "
                       f"(device_settled={last['device_settled']}). "
                       if "occupied" in last else "no observation completed. ")
                    + "Refusing before any gesture (a filled, non-selected target is started by a "
                      "long press instead of cleared)")
            if known_occupied is False:
                preparation.append({"slot": slot_number, "status": "ALREADY_EMPTY",
                                    "detail": "census: no committed content"})
                events.append({"event": f"prepare_slot_{label}", "slot": slot_number,
                               "status": "ALREADY_EMPTY", "detail": "census"})
            else:
                # A filled target that is not the selected slot would *start playback* on a
                # long press (`CLEAR_TRACK_FOR_SLOT`'s not-selected branch) instead of
                # clearing, so select it first (the operator's own gesture) and require the
                # device's own slot-scoped receipt for that selection before pressing the
                # clear. A refused selection is not a reason to clear anyway.
                select_mark = len(_lines(capture_log))
                _press_slot(slot_number)
                selected_row = _await_slot_disp(capture_log, select_mark, slot0, None,
                                                request.arm_timeout_s, drain=drain)
                events.append({"event": f"prepare_slot_{label}_selected", "slot": slot_number,
                               "confirmed": selected_row is not None,
                               "row": None if selected_row is None else selected_row.strip()})
                if selected_row is None:
                    raise PreparationFailure(
                        f"slot {slot_number} was not selected by its own press (no DISP row for "
                        f"slot {slot0} within {request.arm_timeout_s:g}s); still filled and not "
                        "selected, where a long press starts playback instead of clearing")
                # A refused or aborted clear means the firmware could not
                # complete its deferred save first; that is an explicit
                # "save in flight", so wait for it to quiesce and retry the
                # gesture a bounded number of times rather than giving up.
                attempts = max(1, request.clear_attempts)
                outcome_of_clear = None

                def target_emptiness() -> Optional[bool]:
                    """Fresh read-only census state for this target; None when unavailable.

                    A recognized long press is not completion: the firmware acknowledges the
                    gesture before deciding, so a silent slot is only ALREADY_EMPTY when the
                    census independently classifies it empty (RAM and SD both observed absent).
                    """
                    fresh = _run_census(capture, capture_log, events,
                                        attempts=request.census_attempts,
                                        retry_delay_s=request.census_retry_delay_s,
                                        require=((t0, slot0),))
                    if fresh is None:
                        return None
                    return fresh.get((t0, slot0))

                for attempt in range(attempts):
                    spacing.before(LOOP_SELECT_NOTE_BASE + slot_number - 1)
                    outcome_of_clear = stopped_preparation.clear_slot(
                        ctx, capture_log, track_index0=t0, slot0=slot0,
                        timeout_s=request.clear_timeout_s,
                        emptiness_reader=target_emptiness)
                    events.append({"event": f"prepare_slot_{label}", "attempt": attempt,
                                   **outcome_of_clear.as_dict()})
                    if outcome_of_clear.ok or outcome_of_clear.status not in (
                            stopped_preparation.ClearStatus.REFUSED,
                            stopped_preparation.ClearStatus.ABORTED):
                        break
                    if attempt + 1 < attempts:
                        time.sleep(request.clear_retry_delay_s)
                preparation.append(outcome_of_clear.as_dict())
                if not outcome_of_clear.ok:
                    raise PreparationFailure(
                        f"slot {slot_number} scoped clear {outcome_of_clear.status.value}: "
                        f"{outcome_of_clear.detail}")
        # Seed the take. The device's own transition rows name the gesture: `Loop N: Start Recording`
        # announces the arm and `Recording started @ tick` the take, both written **straight to Serial**
        # (live by construction, not held in the capture ring), with the slot's `DISP … ARMED/RECORDING`
        # rows as the slot-scoped companion.
        #
        # Measure the state *before* pressing. The clear's own selection press arms an **empty** target
        # (`handleToggleRecordForSlot` on an empty slot), so a seed press on top of that is a toggle:
        # measured (`captures/s4f22-dec096-od-10`) the driver pressed again, the firmware logged
        # `Loop 5: Stop Recording`, and the take the device had already started was thrown away.
        ready = _slot_ready_state(capture_log, seed_mark, slot0, slot_number)
        armed_row = None
        press_mark = seed_mark
        if ready is not None:
            armed_row = ready
            events.append({"event": f"seed_{label}_already_ready", "slot": slot_number,
                           "state": ready})
        else:
            for attempt in range(DEFAULT_SEED_PRESS_ATTEMPTS):
                press_mark = len(_lines(capture_log))
                _press_slot(slot_number)
                armed_row = _await_slot_disp(capture_log, press_mark, slot0,
                                             ("ARMED", "RECORDING"), request.arm_timeout_s,
                                             human=(f"Loop {slot_number}: Start Recording",),
                                             drain=drain)
                events.append({"event": f"seed_{label}_press", "attempt": attempt,
                               "slot": slot_number, "armed": armed_row is not None})
                if armed_row is not None:
                    break
        if armed_row is None:
            raise PreparationFailure(
                f"slot {slot_number} never armed: {DEFAULT_SEED_PRESS_ATTEMPTS} press(es) left no "
                f"receipt for slot {slot0} within {request.arm_timeout_s:g}s (a predecessor's in-flight "
                "stop holds the selection of a filled target; pressing again and starting the "
                "transport would record nothing)")
        _ensure_transport(out_port, capture_log, running=True, press_ms=request.press_ms,
                          events=events, label=f"seed_{label}_start")
        recording = _await_slot_disp(capture_log, press_mark, slot0, ("RECORDING",),
                                     request.phase_timeout_s,
                                     human=("Recording started @ tick",),
                                     drain=drain) is not None
        events.append({"event": f"seed_{label}_recording", "confirmed": recording})
        if not recording:
            raise PreparationFailure(
                f"slot {slot_number} armed but never reached RECORDING within "
                f"{request.phase_timeout_s:g}s; there is no take to score")
        span_s = max(request.bars, 1) * 2.0
        gap_s = span_s / (request.notes + 1)
        for i in range(request.notes):
            pitch = note_base + (i % 12)
            out_port.send(mido.Message("note_on", note=pitch, velocity=100, channel=t0))
            time.sleep(0.03)
            out_port.send(mido.Message("note_off", note=pitch, velocity=0, channel=t0))
            time.sleep(max(gap_s - 0.03, 0.01))
        # The mark goes in *before* the commit press: the device emits this take's
        # `RECS,stop,<slot>` at the stop edge, which can land in the log before the transport-stop row
        # the press's own confirmation waits for -- measured (`captures/s4f22-dec096-od-09`): the RECS
        # row is stamped 35.809 s while `Transport stopped` only arrived at 40.414 s, so a mark taken
        # after the confirmation sat *behind* the receipt its own press had produced.
        commit_mark = len(_lines(capture_log))
        _ensure_transport(out_port, capture_log, running=False, press_ms=request.press_ms,
                          events=events, label=f"seed_{label}_commit")
        committed = _await_markers(capture_log, commit_mark,
                                   (f",RECS,stop,{slot0},", f",RECS,stopToStopped,{slot0},"),
                                   request.phase_timeout_s, drain=drain)
        events.append({"event": f"seed_{label}_committed", "confirmed": committed})
        if not committed:
            raise PreparationFailure(f"slot {slot_number} seed commit unconfirmed")
        # Recorded, not gated: whether the seeded take's finalization release edge arrived inside the
        # bound. The *next* seed's own selection/arm receipts are its gates, and they fail by name when
        # a predecessor still holds the selection, so this only has to make the latency attributable.
        settled = _await_markers(capture_log, commit_mark,
                                 (f"Reentry released slot={slot0}",
                                  f"Record pass undo reset for slot {slot0}",
                                  "PERS,result,"), request.seed_settle_timeout_s, drain=drain)
        events.append({"event": f"seed_{label}_finalization_settled", "slot": slot_number,
                       "confirmed": settled})

    # B must contain a loop for the overdub successor. A is seeded last
    # so its record-button gesture addresses A without a playback queue.
    leg_mark = len(_lines(capture_log))
    prepared = True
    try:
        if request.leg == "od":
            seed(request.slot_b, "b", 64)
        seed(request.slot_a, "a", 60)
    except PreparationFailure as exc:
        prepared = False
        outcome.result = "PREPARATION_FAILED"
        outcome.preparation_error = str(exc)
        events.append({"event": "preparation_failed", "detail": str(exc)})
        print(f"[s4f13] {exc}")
    _progress(outcome, on_progress)
    if prepared:
        plan = leg_gestures(request.leg, track=request.track, a=request.slot_a, b=request.slot_b)
        outcome.plan = [{"step": g.step, "kind": g.kind, "value": g.value} for g in plan]
        if not _ensure_transport(out_port, capture_log, running=True, press_ms=request.press_ms,
                                 events=events, label="leg_start"):
            outcome.result = "TRANSPORT_START_UNCONFIRMED"
        else:
            # Wait for the seeded takes' publication before the leg's first gesture: the device makes a
            # record admission wait on the predecessor's stop obligation, and that obligation is only
            # gone once the deferred stages have published (measured ~40 s after the stop). Recorded,
            # not fatal -- the leg still reports the device's own deferral row if it arrives anyway.
            published = _await_markers(
                capture_log, leg_mark,
                (f"Record pass undo reset for slot {a0}", f"Reentry released slot={a0}",
                 f"Record pass undo reset for slot {b0}", f"Reentry released slot={b0}"),
                request.predecessor_publication_timeout_s, drain=drain)
            events.append({"event": "leg_predecessor_publication", "settled": published})
            outcome.leg_start_row = len(_lines(capture_log))
            outcome.result = _run_plan(
                out_port, capture_log, plan, slot_b=request.slot_b, press_ms=request.press_ms,
                phase_timeout_s=request.phase_timeout_s, events=events,
                note_channel=t0, overdub_notes=request.notes, drain=drain)
    # The leg's settle is the run's longest stretch with nothing on the wire, so it is the one place a
    # cancel would otherwise wait out a whole phase before the capture could be released.
    cancellable_sleep(request.settle_s, scope=active_cancel_scope())
    outcome.leg_rows = len(_lines(capture_log)) - leg_mark

    _ensure_transport(out_port, capture_log, running=False, press_ms=request.press_ms,
                      events=events, label="final")
    # Task 2.2: the leg's own accounting, read back while the transport is stopped (the request is
    # stopped-only). The oracle gates on these counters, so a leg whose counted families lost rows --
    # the latch's family included -- cannot read as accepted. Bounded and non-fatal: a session that
    # cannot answer costs the accounting evidence, and the leg still reports what it drove.
    stats_rows = read_family_counters(capture)
    outcome.identity_stats_rows = stats_rows
    events.append({"event": "family_counters_read_back",
                   "rows": 0 if stats_rows is None else len(stats_rows)})
    _drain_now(drain)
    time.sleep(0.2)
    return outcome
