"""Task 6.4's windowed leg: the existing long-loop content-verification leg, read for the windowed lane.

The design's own rule is that F-7/F-8 *feed* the content-verification pipeline rather than building a second one
(`design.md`: "fed the windowed/demoted cases (new stimulus profile), not replaced"). So this module adds no driver of
its own: it reuses `run_long_loop_display_window` -- a leg that records a loop far longer than the served window
(`--record-bars`, default 24, and its own run refuses <= 16), keeps the transport running, and already has a host-tested
verifier -- and pairs it with the windowed lane's own checks (`hitl.verify.windowed_lane`).

**The one thing this wrapper owns is the target.** The reused leg takes no slot argument: it clears and records on
whatever slot is *active* on the selected track, so a run that names a slot without selecting it would report a scope it
never used. The wrapper therefore consumes the runner's `--loop-slot` -- refusing a run that does not name one -- and
presses that slot's own select control before delegating. Without this the leg would not even start: the runner appends
`--loop-slot N` to the legacy arguments and the leg's parser (strict) rejects it.
"""

from __future__ import annotations

import time
from typing import Any

from pathlib import Path

from hitl.scenarios.long_loop_display_window import run_long_loop_display_window
from hitl.verify.windowed_lane import verify_windowed_content_parity

#: The control press the slot selection uses; the leg's own `--press-ms` is its business, this is the wrapper's.
SLOT_SELECT_PRESS_MS = 120
#: How long the post-stop drain reads before the run ends. The ring is retained under transport, so the lane's rows
#: produced while a track plays are only delivered once the transport stops and an **idle** flush runs; a run that ends
#: while the transport is still running delivers none of them (measured 2026-10-05).
POST_STOP_DRAIN_S = 12.0
TRANSPORT_STOP_PRESS_MS = 120
#: How long the switch leg waits for the queued switch's target to be prepared and committed before the drain. The
#: preparation is off-clock (the `SlotProvision` turn) but it is what gives the window lane its **demand**, so the wait
#: must be long enough for that turn to be served at least once under transport.
SWITCH_SETTLE_S = 20.0
#: The edit control (note 38) held long enough to **exit** the loop-edit session a slot selection opens. Measured on
#: device (run 04): selecting the target enters `LOOP_EDIT`, and while that session is open the queued switch's
#: preparation is never reached -- no provision row, no prewarm, no lane work -- so the switch holds until its bound.
#: The firmware's own long-press window is 350 ms (`EDIT_BUTTON_DEBOUNCE_MS`); 700 ms is the value the reused leg uses.
EDIT_EXIT_PRESS_MS = 700
RECORD_PRESS_MS = 120


def split_flag(legacy: list[str], name: str) -> tuple[int | None, list[str]]:
    """Take `<name> N` out of the legacy arguments, returning the value and what the reused leg may see.

    The wrapper owns flags the reused leg has no argument for (`--loop-slot`, `--switch-target-slot`); forwarding one
    would abort the leg's **strict** parser before it drives anything -- which is exactly how the switch leg failed its
    first attempt. Only the first occurrence is honoured: a repeat is a caller error and is left for the leg to reject
    rather than silently ignored.
    """
    value: int | None = None
    remaining: list[str] = []
    index = 0
    while index < len(legacy):
        token = legacy[index]
        if token == name and index + 1 < len(legacy):
            if value is None:
                value = int(legacy[index + 1])
                index += 2
                continue
        remaining.append(token)
        index += 1
    return value, remaining


def split_loop_slot_flag(legacy: list[str]) -> tuple[int | None, list[str]]:
    """Take `--loop-slot N` out of the forwarded legacy arguments."""
    return split_flag(legacy, "--loop-slot")


def split_switch_target_slot_flag(legacy: list[str]) -> tuple[int | None, list[str]]:
    """Take `--switch-target-slot N` out of the forwarded legacy arguments."""
    return split_flag(legacy, "--switch-target-slot")


def select_loop_slot(out_port: Any, slot: int, *, press_ms: int = SLOT_SELECT_PRESS_MS) -> None:
    """Press the slot's own select control, so the leg's clear/record target is the declared one."""
    from hitl.control_constants import CONTROL_CHANNEL_1BASED, LOOP_SELECT_NOTE_BASE
    from hitl.midi_io import _send_short_press

    _send_short_press(
        out_port,
        note=LOOP_SELECT_NOTE_BASE + (slot - 1),
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
        _caller="windowed_content_parity",
    )


def last_transport_row_running(lines: list[str]) -> bool | None:
    """Did the last transport edge in this capture leave the transport **running**?

    `None` when the capture carries no transport edge at all. The stop press is only sent when the answer is `True`:
    pressing a *toggle* on a stopped transport would start one, which is the opposite of a drain.
    """
    running: bool | None = None
    for line in lines:
        if "Transport started" in line:
            running = True
        elif "Transport stopped" in line:
            running = False
    return running


def press_global_transport_stop(out_port: Any, *, press_ms: int = TRANSPORT_STOP_PRESS_MS) -> None:
    """Short-press the global transport, which toggles it -- called only when the capture says it is running."""
    from hitl.control_constants import CONTROL_CHANNEL_1BASED, GLOBAL_TRANSPORT_NOTE
    from hitl.midi_io import _send_short_press

    _send_short_press(
        out_port,
        note=GLOBAL_TRANSPORT_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
        _caller="windowed_content_parity",
    )


def switch_target_slot_from(legacy: list[str]) -> int | None:
    """The filled, non-active slot the switch leg should switch *to*, or `None` for the base leg only.

    This is what gives the window lane a **demand** under transport: a plain record-and-play owes no window (measured),
    while a switching target owes one because the launch prepares it. Absent by default, so the base behaviour is
    unchanged and the two shapes stay comparable.
    """
    return split_switch_target_slot_flag(legacy)[0]


def press_record_for_active_slot(out_port: Any, *, press_ms: int = RECORD_PRESS_MS) -> None:
    """Press the record control, whose meaning the **firmware** decides from the target's own state.

    With the track playing and the selected slot **filled**, `MidiButtonActions` cannot enter its recording branch
    (`slotCanArmForRecord = !hasCommittedPassesInSlot(slot)` is false) and queues a playback switch instead, so this
    gesture cannot overwrite the target. That is why the switch leg selects the target first and then presses this.
    """
    from hitl.control_constants import CONTROL_CHANNEL_1BASED, RECORD_BUTTON_NOTE
    from hitl.midi_io import _send_short_press

    _send_short_press(
        out_port,
        note=RECORD_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
        _caller="windowed_content_parity",
    )


def press_edit_exit(out_port: Any, *, press_ms: int = EDIT_EXIT_PRESS_MS) -> None:
    """Hold the edit control long enough to leave the loop-edit session a slot selection opens.

    Measured, not assumed: run 04 selected the target, the device entered `LOOP_EDIT`, and the queued switch then sat
    untouched for its whole bound — no provision row, no capture prewarm, and no lane work — while the lane refuses
    while an edit session is active. The S4b device legs prepared their switches with no such session open.
    """
    from hitl.control_constants import CONTROL_CHANNEL_1BASED, EDIT_BUTTON_NOTE
    from hitl.midi_io import _send_short_press

    _send_short_press(
        out_port,
        note=EDIT_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
        _caller="windowed_content_parity",
    )


def switch_leg(out_port: Any, target_slot: int, *, settle_s: float = SWITCH_SETTLE_S) -> None:
    """Select the target, leave the edit session it opens, then press its record control and wait for the switch.

    The order is the whole safety argument: the record control acts on the *selected* slot, and on a filled one while the
    track plays the firmware can only queue a playback switch. Reversing those two would put the press on whatever slot
    happened to be active. The **edit exit** sits between them because the selection opens a session the switch's
    preparation does not survive (run 04).
    """
    if not 1 <= target_slot <= 8:
        raise ValueError(f"switch target slot {target_slot} is outside 1..8")
    select_loop_slot(out_port, target_slot)
    time.sleep(0.5)
    press_edit_exit(out_port)
    time.sleep(0.5)
    press_record_for_active_slot(out_port)
    time.sleep(settle_s)


def serial_port_from(args: object) -> str | None:
    """The serial port this run was given, wherever the runner kept it.

    The layered runner passes `--serial-port` through to the leg's own arguments rather than setting `args.serial_port`,
    so the drain has to look in both places -- otherwise it returns early and the retained rows are never read (found by
    the first run that had a drain to do).
    """
    direct = getattr(args, "serial_port", None)
    if direct:
        return str(direct)
    legacy = list(getattr(args, "legacy_args", []) or [])
    for index, token in enumerate(legacy):
        if token == "--serial-port" and index + 1 < len(legacy):
            return legacy[index + 1]
    return None


def _leg_capture_log(out_dir: Path) -> Path | None:
    logs = sorted(out_dir.glob("host_midi_hitl_long_loop_display_serial_*.log"))
    return logs[-1] if logs else None


def run_post_leg_phase(
    args: object,
    *,
    switch_target_slot: int | None = None,
    drain_s: float = POST_STOP_DRAIN_S,
    switch_settle_s: float = SWITCH_SETTLE_S,
) -> Path | None:
    """Everything the driver owes **after** the reused leg: the switch leg (optional), then the stop-and-drain.

    Both halves are gated on the transport actually running, for the same reason: a switch or a drain performed with the
    transport stopped is not the regime this leg exists for (the lane's rows are produced *under transport*), and the stop
    press is a toggle that would otherwise **start** a transport.
    """
    out_dir = Path(getattr(args, "out_dir", Path("captures")))
    port = serial_port_from(args)
    if not port:
        print("[windowed-content-parity] no serial port on this run: the retained rows cannot be drained")
        return None
    log = _leg_capture_log(out_dir)
    if log is None:
        return None
    lines = log.read_text(encoding="utf-8").splitlines()
    if last_transport_row_running(lines) is not True:
        print("[windowed-content-parity] the capture ends with the transport stopped: neither a switch leg nor a drain "
              "belongs here")
        return None

    import mido

    from hitl.midi_io import _find_midi_port
    from hitl.serial_collector import SerialCaptureCollector

    collector = SerialCaptureCollector(port, 115200)
    collector.start()
    time.sleep(1.0)
    out_port = mido.open_output(_find_midi_port("Teensy", False))
    try:
        if switch_target_slot is not None:
            if not 1 <= switch_target_slot <= 8:
                print(f"[windowed-content-parity] --switch-target-slot {switch_target_slot} is outside 1..8")
                return None
            print(f"[windowed-content-parity] switch leg: selecting slot {switch_target_slot} and pressing its record "
                  f"control to queue a switch while the transport runs")
            switch_leg(out_port, switch_target_slot, settle_s=switch_settle_s)

        print("[windowed-content-parity] stopping the transport so the retained Tier-A rows drain")
        press_global_transport_stop(out_port)
    finally:
        out_port.close()
    time.sleep(drain_s)
    rows = collector.snapshot()
    collector.stop()
    from datetime import datetime

    drain_path = out_dir / f"host_midi_hitl_windowed_post_stop_drain_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"
    drain_path.write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"[windowed-content-parity] post-stop drain: {drain_path} ({len(rows)} lines)")
    return drain_path


def run_windowed_content_parity(args: object) -> int:
    """Select the declared slot, run the reused leg, then drain what the transport retained."""
    legacy = list(getattr(args, "legacy_args", []) or [])
    slot, remaining = split_loop_slot_flag(legacy)
    switch_target, remaining = split_switch_target_slot_flag(remaining)
    if slot is None:
        print(
            "[windowed-content-parity] --loop-slot is required: the reused leg clears and records on the *active* "
            "slot, so the target must be named and selected rather than inherited from whatever the device had"
        )
        return 2
    if not 1 <= slot <= 8:
        print(f"[windowed-content-parity] --loop-slot {slot} is outside 1..8")
        return 2

    setattr(args, "legacy_args", remaining)

    import mido

    from hitl.midi_io import _find_midi_port

    out_port = mido.open_output(_find_midi_port("Teensy", False))
    try:
        print(f"[windowed-content-parity] selecting loop slot {slot} before the reused leg runs")
        select_loop_slot(out_port, slot)
        time.sleep(0.5)
    finally:
        out_port.close()

    code = run_long_loop_display_window(args)
    # The lane's rows are produced under transport and the ring is retained while one runs, so the leg's own capture
    # cannot contain them; the switch leg (when asked for) and the drain are what make them observable at all.
    run_post_leg_phase(args, switch_target_slot=switch_target)
    return code


def verify_windowed_content_parity_leg(lines: list[str], args: object) -> dict[str, Any]:
    """The reused leg's oracle **plus** the windowed lane's checks (see `hitl.verify.windowed_lane`)."""
    return verify_windowed_content_parity(lines, args)
