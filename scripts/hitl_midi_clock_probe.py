#!/usr/bin/env python3
"""Device check for the HITL MIDI clock contract (`hitl/midi_clock.py`).

Two directions, both against the real board:

* **read** (default) -- the device is clock master. The probe learns its tempo from its own
  USB MIDI clock, then verifies that pulse-index bar boundaries land on a constant phase
  offset (no accumulating error) and reports jitter and dropped-pulse gaps.
* **drive** (`--drive-bars N`) -- the host is clock master. It sends 24 PPQN clock at
  `--drive-bpm` and verifies the device slaves to it (its own clock output stops).

Read is the safe default: it changes no device state except the transport, and it restores
the transport state it found. Driving changes the device's *live tempo* (the device adopts
the host rate through `ClockManager::setBpmFloat`), so with `--drive-bpm` the probe drives
the device's previously measured tempo back for 2 s before releasing it. The probe never
sends a bare realtime `Start`/`Continue` without clock: that alone leaves the device in
`CLOCK_EXTERNAL`, where it emits no clock of its own.

Not a HITL content run: no record, no overdub, no clear, no loop writes.

Run:
  .venv/bin/python scripts/hitl_midi_clock_probe.py --observe-bars 4
  .venv/bin/python scripts/hitl_midi_clock_probe.py --observe-bars 4 --drive-bars 4 --drive-bpm 120
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from pathlib import Path

import mido

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.control_constants import (  # noqa: E402
    CONTROL_CHANNEL_1BASED,
    GLOBAL_TRANSPORT_NOTE,
    MIDI_CLOCKS_PER_BAR,
)
from hitl.midi_clock import MidiClockMaster, MidiClockReader  # noqa: E402


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--midi-in", default="Teensy MIDI")
    parser.add_argument("--midi-out", default="Teensy MIDI")
    parser.add_argument("--serial-port", default=None,
                        help="Optional: collect #CAP,BPM / DIAG,clockrate verdicts.")
    parser.add_argument("--serial-baud", type=int, default=115200)
    parser.add_argument("--observe-bars", type=int, default=4)
    parser.add_argument("--drive-bars", type=int, default=0,
                        help="Host-master direction: send this many bars of clock (0 = skip).")
    parser.add_argument("--drive-bpm", type=float, default=None,
                        help="Host clock tempo; default = the device tempo just measured.")
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument("--out", type=Path, default=Path("captures/midi-clock-probe"))
    return parser.parse_args()


class SerialEvidence:
    """Non-blocking #CAP collector; the port stays open until the probe is done."""

    def __init__(self, port: str | None, baud: int) -> None:
        self._ser = None
        self.lines: list[str] = []
        if port is None:
            return
        import serial  # localised: the probe works without serial installed

        self._ser = serial.Serial(port, baud, timeout=0)

    def pump(self) -> None:
        if self._ser is None:
            return
        try:
            pending = self._ser.in_waiting
            if pending:
                self.lines.extend(self._ser.read(pending).decode("utf-8", "replace").splitlines())
        except Exception:  # pragma: no cover - a lost port must not fail the measurement
            pass

    def matching(self, pattern: str) -> list[str]:
        return [line for line in self.lines if re.search(pattern, line)]

    def close(self) -> None:
        if self._ser is not None:
            self._ser.close()


def _send_short_press(port: mido.ports.BaseOutput, *, press_ms: int) -> None:
    channel = CONTROL_CHANNEL_1BASED - 1
    port.send(mido.Message("note_on", channel=channel, note=GLOBAL_TRANSPORT_NOTE, velocity=127))
    time.sleep(max(press_ms, 1) / 1000.0)
    port.send(mido.Message("note_off", channel=channel, note=GLOBAL_TRANSPORT_NOTE, velocity=0))


def _transport_state(reader: MidiClockReader, *, window_s: float) -> bool:
    """True when the device's own clock is flowing, i.e. its transport is running."""
    deadline = time.monotonic() + window_s
    while time.monotonic() < deadline:
        reader.poll()
        if reader.pulse_seen():
            return True
        time.sleep(0.002)
    return reader.pulse_seen()


def _ensure_transport(reader: MidiClockReader, out_port: mido.ports.BaseOutput, *,
                      press_ms: int) -> tuple[bool, bool]:
    """Return (running, we_started_it). Press-and-verify: the press is a toggle."""
    if _transport_state(reader, window_s=1.5):
        print("[clock] device transport already running")
        return True, False
    for attempt in range(1, 4):
        print(f"[clock] transport stopped; pressing transport (attempt {attempt}/3)")
        _send_short_press(out_port, press_ms=press_ms)
        if _transport_state(reader, window_s=2.0):
            period = reader.period()
            if period:
                print(f"[clock] transport started; device clock "
                      f"{reader.observed_pulses()} pulses, {reader.bpm() or 0:.2f} BPM "
                      f"({1.0 / period:.2f} Hz)")
            else:
                print("[clock] transport started; device clock pulses present")
            return True, True
        print("[clock] no device clock after the press")
    return False, False


def keys_agree(measured: float | None, reference: float, *, tolerance_bpm: float = 1.0) -> bool:
    """True when a clock-derived tempo is within ``tolerance_bpm`` of the reference."""
    return measured is not None and abs(measured - reference) <= tolerance_bpm


def _observe_bars(reader: MidiClockReader, bars: int) -> dict[str, object]:
    """Time each bar boundary on the device's own pulse grid."""
    samples: list[dict[str, float]] = []
    origin_index = reader.index_now()
    origin_time = time.monotonic()
    for bar in range(1, bars + 1):
        bpm = reader.bpm() or 120.0
        # Three bar-lengths of slack: the wait is phase-paced, and a bar at 120 BPM is 2 s.
        timeout_s = 3.0 * (60.0 / bpm) * 4.0
        reached = reader.wait_pulse(origin_index + bar * MIDI_CLOCKS_PER_BAR, timeout_s=timeout_s)
        elapsed = time.monotonic() - origin_time
        samples.append({
            "bar": float(bar),
            "reached": 1.0 if reached else 0.0,
            "elapsed_s": elapsed,
            "expected_s": bar * (60.0 / (reader.bpm() or bpm)) * 4.0,
            "bpm_at_boundary": reader.bpm() or 0.0,
            "gaps": float(reader.gap_count()),
        })
    reached_samples = [s for s in samples if s["reached"]]
    offsets = [s["elapsed_s"] - s["expected_s"] for s in reached_samples]
    return {
        "bars": samples,
        "constant_offset_ms": (offsets[0] * 1e3) if offsets else 0.0,
        "drift_ms": ((offsets[-1] - offsets[0]) * 1e3) if len(offsets) > 1 else 0.0,
        "reached_all": len(reached_samples) == len(samples),
    }


def main() -> int:
    args = _parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    evidence = SerialEvidence(args.serial_port, args.serial_baud)
    report: dict[str, object] = {"args": {k: str(v) for k, v in vars(args).items()}}
    ok = True

    print(f"[clock] opening MIDI in={args.midi_in!r} out={args.midi_out!r}")
    with mido.open_output(args.midi_out) as out_port, mido.open_input(args.midi_in) as in_port:
        reader = MidiClockReader(in_port)
        running, we_started = _ensure_transport(reader, out_port, press_ms=args.press_ms)
        report["transport_running"] = running
        report["transport_started_by_probe"] = we_started
        if not running:
            print("[clock] FAIL: no device clock; the device must be running to emit clock")
            evidence.close()
            return 2

        # Let the rolling median settle before measuring.
        settle_end = time.monotonic() + 0.6
        while time.monotonic() < settle_end:
            reader.poll()
            time.sleep(0.002)
        evidence.pump()
        stats_before = reader.stats()
        print(f"[clock] device tempo: {stats_before['bpm']:.3f} BPM "
              f"(period {stats_before['period_ms']:.3f} ms, jitter {stats_before['jitter_ms']:.3f} ms, "
              f"gaps {int(stats_before['gaps'])})")

        observed = _observe_bars(reader, max(args.observe_bars, 1))
        evidence.pump()
        report["read"] = {"stats": reader.stats(), **observed}
        print(f"[clock] read: {args.observe_bars} bars, constant offset "
              f"{observed['constant_offset_ms']:+.1f} ms, drift {observed['drift_ms']:+.1f} ms, "
              f"gaps {int(reader.gap_count())}")
        if not observed["reached_all"]:
            print("[clock] FAIL: the device clock stopped before the observed bars elapsed")
            ok = False
        if abs(observed["drift_ms"]) > 40.0:
            print(f"[clock] FAIL: bar boundaries drifted {observed['drift_ms']:+.1f} ms over "
                  f"{args.observe_bars} bars")
            ok = False
        if not stats_before["bpm"]:
            print("[clock] FAIL: no tempo could be derived from the device clock")
            ok = False

        device_bpm = stats_before["bpm"]

        if args.drive_bars > 0:
            drive_bpm = args.drive_bpm if args.drive_bpm else device_bpm
            master = MidiClockMaster(out_port)
            echo_reader = MidiClockReader(in_port)
            print(f"[clock] drive: host master at {drive_bpm:.3f} BPM for {args.drive_bars} bars")
            master.start()
            pacing = master.bars(args.drive_bars, bpm=drive_bpm, echo_reader=echo_reader)
            echo = pacing["device_clock_echo_tail_pulses"]
            report["drive"] = {
                "bpm": drive_bpm,
                "bars": float(args.drive_bars),
                "pacing": pacing,
                "device_clock_echo_pulses": pacing["device_clock_echo_pulses"],
                "device_clock_echo_tail_pulses": echo,
            }
            print(f"[clock] drive pacing: mean {pacing['mean_err_ms']:.3f} ms / max "
                  f"{pacing['max_err_ms']:.3f} ms per pulse, drift {pacing['drift_ms']:+.3f} ms")
            print(f"[clock] device clock while driven: {int(pacing['device_clock_echo_pulses'])} pulses "
                  f"in total, {int(echo)} after the settle quarter")
            if echo > 0:
                print(f"[clock] FAIL: the device still emitted {int(echo)} of its own clock pulses "
                      "after the settle quarter (it did not slave to the host clock)")
                ok = False
            else:
                print("[clock] PASS: device slaved to the host clock (no clock of its own)")
            if args.drive_bpm and abs(args.drive_bpm - device_bpm) > 0.5:
                # The device adopted the host rate; put its own tempo back before releasing.
                print(f"[clock] drive: restoring the device tempo {device_bpm:.3f} BPM")
                master.bars(max(1, args.drive_bars), bpm=device_bpm)
            # Release by stopping the clock, not with a realtime Stop: a Stop stops the
            # device's transport, and the point here is to watch it clock itself again.
            # The revert is `ClockManager::checkClockSource` on the 500 ms timeout, run from
            # `loop()`, so a busy loop (a deferred save) can delay it. Wait for the resume
            # rather than assuming a fixed window, and report the latency.
            restored = MidiClockReader(in_port)
            released_at = time.monotonic()
            deadline = released_at + 8.0
            while time.monotonic() < deadline and not restored.pulse_seen():
                restored.poll()
                time.sleep(0.002)
            resumed_s = (time.monotonic() - released_at) if restored.pulse_seen() else None
            while time.monotonic() < released_at + (resumed_s or 0.0) + 2.0:
                restored.poll()
                time.sleep(0.002)
            report["drive"]["release_resume_s"] = resumed_s if resumed_s is not None else -1.0
            report["restored_bpm"] = restored.bpm() or 0.0
            restored_bpm = restored.bpm()
            if resumed_s is None:
                print("[clock] FAIL: the device emitted no clock of its own within 8 s of release")
                ok = False
            else:
                print(f"[clock] after release: device back on its own clock after {resumed_s:.2f} s "
                      f"at {restored_bpm or 0:.3f} BPM (was {device_bpm:.3f})")
                # The device's tempo is a 24-pulse sliding-window estimate, so a driven clock
                # moves it by a couple of BPM (`ClockManager::onMidiClockPulse` smooths every
                # window). Only a persistent move is a failure; a pin needs the documented
                # CC102 tempo fader, and the residual is reported for the run record.
                if not keys_agree(restored_bpm, device_bpm, tolerance_bpm=5.0):
                    print("[clock] FAIL: the device did not return to its own tempo after the drive")
                    ok = False
                elif not keys_agree(restored_bpm, device_bpm, tolerance_bpm=1.0):
                    print(f"[clock] NOTE: the drive moved the device tempo by "
                          f"{(restored_bpm or 0) - device_bpm:+.2f} BPM (pin it with the CC102 "
                          "tempo fader when the run needs an exact figure)")

        evidence.pump()
        capt_bpm = evidence.matching(r",BPM,")
        clockrate = evidence.matching(r"clockrate")
        report["serial"] = {"bpm_rows": len(capt_bpm), "clockrate_rows": clockrate[-3:]}
        print(f"[clock] serial verdicts: #CAP,BPM rows={len(capt_bpm)} "
              f"(requires a build with SESSION_CAPTURE_BPM), clockrate={clockrate[-1:] or 'none'}")

        if we_started:
            print("[clock] restoring the transport state found (stopped)")
            _send_short_press(out_port, press_ms=args.press_ms)
            time.sleep(0.5)
            stop_reader = MidiClockReader(in_port)
            end = time.monotonic() + 1.0
            while time.monotonic() < end:
                stop_reader.poll()
                time.sleep(0.002)
            report["transport_running_after"] = stop_reader.observed_pulses() > 0

    evidence.close()
    report["ok"] = ok
    log_path = args.out / "midi_clock_probe.json"
    log_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"[clock] report: {log_path}")
    print("[clock] PASS" if ok else "[clock] FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
