#!/usr/bin/env python3
"""S1.3 / S2b.3 device slope: grow the restored library across reboots and find the failure point.

Records one scratch slot per step, forces a *verified* reset, then reads the boot census
(`DIAG,capture_prewarm` phases) and the per-slot library walk. Each step contributes one
(N committed slots -> free_chunks, explicit chunk costs) row, so the reservation slope can be read off
hardware rather than inferred.

The reset is explicit and proved by the boot banner; a session that silently continues would report the
pre-reboot state as if it were restored, which is the trap this script exists to avoid.

Usage:
  .venv/bin/python scripts/hitl_library_growth_sweep.py --out-dir captures/s13-sweep-01 \
      --track 6 --slots 8 --midi-channel 6 --record-bars 4

  .venv/bin/python scripts/hitl_library_growth_sweep.py --out-dir captures/s13-sweep-02 \
      --track 6 --slots 8 --midi-channel 6 --record-bars 4 --cumulative   # keep prior slots
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

import mido
import serial

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from hitl.control_constants import (  # noqa: E402
    CONTROL_CHANNEL_1BASED,
    LOOP_SELECT_NOTE_BASE,
    RECORD_BUTTON_NOTE,
    TRACK_SELECT_NOTE_BASE,
)

MIDI_CLOCK = 0xF8
MIDI_STOP = 0xFC
PREWARM_RE = re.compile(r"DIAG,capture_prewarm,(.*)$")
FREE_RE = re.compile(r"free_chunks=(\d+)")
ROW_RE = re.compile(r"track=(\d+),slot=(\d+),full=(\d+),ready=(\d+),credits=(\d+),free_chunks=(\d+)")
# S1.3/S2b.3: the authoritative post-restore figure is the explicit census checkpoint, not the
# capture_prewarm walk pair (which is reservation telemetry for the enabled/library sets).
CENSUS_RE = re.compile(r"#CAP,\d+,DIAG,post_restore_census,(.*)$")
CENSUS_FIELD_RE = re.compile(r"([a-z_]+)=(\d+)")


class Session:
    def __init__(self, port: str, path: Path, *, attempts: int = 10) -> None:
        self.lines: list[str] = []
        self._stop = False
        self._handle = path.open("w", encoding="utf-8")
        # A reset re-enumerates the port; opening it the instant the node appears can still fail, so
        # retry briefly instead of aborting a long sweep on a transient.
        last: Exception | None = None
        for attempt in range(attempts):
            try:
                self._ser = serial.Serial(port, 115200, timeout=0.2)
                break
            except Exception as exc:  # noqa: BLE001 - transient enum
                last = exc
                time.sleep(0.5 * (attempt + 1))
        else:
            self._handle.close()
            raise RuntimeError(f"serial port never opened: {port}") from last
        self._t = threading.Thread(target=self._read, daemon=True)
        self._t.start()

    def _read(self) -> None:
        while not self._stop:
            try:
                raw = self._ser.readline()
            except Exception:
                break
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").rstrip()
            self.lines.append(line)
            self._handle.write(line + "\n")

    def close(self) -> None:
        self._stop = True
        time.sleep(0.3)
        try:
            self._ser.close()
        except Exception:
            pass
        self._handle.close()


def wait_for_port(prefix: str = "/dev/cu.usbmodem", timeout_s: float = 25.0) -> str | None:
    """Return the first re-enumerated USB serial port, or None.

    A Teensy reset can come back at a *different* `/dev/cu.usbmodem*` path, so the sweep must resolve the
    port after every reset instead of pinning the one it started with.
    """
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        found = sorted(Path("/dev").glob(Path(prefix).name + "*"))
        if found:
            return str(found[0])
        time.sleep(0.2)
    return None


def force_reset(port: str) -> bool:
    """Reset by re-uploading, which programs and reboots back into the sketch.

    Deliberately **not** `teensy_loader_cli -b` (boot only): that parks the board in HalfKay with no
    sketch, so it never re-enumerates and the sweep loses the device. This is the same path the HITL
    harness uses and the only reset proven reliable in this session.
    """
    cmd = ["pio", "run", "-e", "teensy41-capture-serial", "-t", "upload"]
    try:
        subprocess.run(cmd, capture_output=True, timeout=180, check=False,
                       cwd=str(REPO_ROOT))
    except Exception:
        pass
    return wait_for_port(timeout_s=30.0) is not None


def send_note(out, note: int, channel_1based: int, hold_ms: int) -> None:
    out.send(mido.Message("note_on", channel=channel_1based - 1, note=note, velocity=127))
    time.sleep(max(hold_ms, 1) / 1000.0)
    out.send(mido.Message("note_off", channel=channel_1based - 1, note=note, velocity=0))


def stream_bars(out, *, bars: float, bpm: float, channel_1based: int,
                low: int, high: int, start: bool) -> None:
    total = int(round(bars * 96))
    period = 60.0 / (bpm * 24.0)
    if start:
        out.send(mido.Message("start"))
    for i in range(total):
        out.send(mido.Message.from_bytes([MIDI_CLOCK]))
        if i % 6 == 0:
            send_note(out, low + ((i // 6) % max(1, high - low + 1)), channel_1based, 40)
        time.sleep(period)



def drain(session: Session, seconds: float) -> int:
    """Keep reading so the device is never blocked writing to an unread USB buffer.

    The Teensy wedges if the host stops reading while the firmware is still emitting: `Serial.print`
    blocks once the buffer fills, with no crash signature. Every stop therefore drains, and the port is
    never closed while writes are in flight.
    """
    end = time.monotonic() + seconds
    before = len(session.lines)
    # The Session reader thread is what drains; this just holds the port open for the window.
    while time.monotonic() < end:
        time.sleep(0.05)
    return len(session.lines) - before


def stop_transport_cleanly(out, session: Session, *, drain_s: float = 3.0) -> None:
    """The canonical stop protocol: STOP, drain, STOP, drain.

    The second stop is the safety net -- it is only reached once the first stop's saves have been read
    out, which is exactly what keeps the device from blocking on stdout mid-save.
    """
    out.send(mido.Message.from_bytes([MIDI_STOP]))
    drain(session, drain_s)
    out.send(mido.Message.from_bytes([MIDI_STOP]))
    drain(session, drain_s)


def boot_census(session: Session) -> dict[str, object]:
    """Read the boot census: phase lines, the per-(track,slot) library walk, and the checkpoint marker.

    The walk pair is reservation telemetry; the **post-restore** figure S2b.3/S1.3 pin to is the
    `post_restore_census` marker, which attests every readiness condition first.
    """
    phases, rows = [], []
    marker: dict[str, object] | None = None
    for line in session.lines:
        census = CENSUS_RE.search(line)
        if census:
            fields = {m.group(1): int(m.group(2)) for m in CENSUS_FIELD_RE.finditer(census.group(1))}
            marker = fields
            continue
        m = PREWARM_RE.search(line)
        if not m:
            continue
        payload = m.group(1)
        free = FREE_RE.search(payload)
        if "phase=" in payload:
            phases.append({"phase": payload.split(",")[0].split("=")[-1],
                           "free_chunks": int(free.group(1)) if free else None,
                           "raw": payload})
        else:
            r = ROW_RE.search(payload)
            if r:
                rows.append({"track": int(r.group(1)), "slot": int(r.group(2)),
                             "full": int(r.group(3)), "ready": int(r.group(4)),
                             "credits": int(r.group(5)), "free_chunks": int(r.group(6))})
    pre = next((p["free_chunks"] for p in phases if p["phase"] == "before"), None)
    post = next((p["free_chunks"] for p in phases if p["phase"] == "after"), None)
    return {"phase_before_free": pre, "phase_after_free": post,
            "post_restore_free_chunks": marker.get("free_chunks") if marker else None,
            "census_marker": marker,
            "library_rows": rows, "phases": phases}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--port", default="/dev/cu.usbmodem154944801")
    ap.add_argument("--track", type=int, default=6)
    ap.add_argument("--slots", type=int, default=8)
    ap.add_argument("--first-slot", type=int, default=1)
    ap.add_argument("--midi-channel", type=int, default=6)
    ap.add_argument("--bpm", type=float, default=120.0)
    ap.add_argument("--record-bars", type=float, default=4.0)
    ap.add_argument("--settle-ms", type=int, default=9000)
    ap.add_argument("--boot-wait-s", type=float, default=12.0)
    ap.add_argument("--cumulative", action="store_true",
                    help="Do not clear the track before each step, so slots accumulate.")
    args = ap.parse_args()

    out_dir: Path = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=False)
    steps: list[dict[str, object]] = []
    t0 = time.monotonic()

    for i in range(args.slots):
        slot = args.first_slot + i
        step_dir = out_dir / f"step{i:02d}_slot{slot}"
        step_dir.mkdir(parents=True, exist_ok=True)

        # ---- record into this slot ----
        args.port = wait_for_port() or args.port
        s = Session(args.port, step_dir / "record.log")
        out = mido.open_output("Teensy MIDI")
        in_port = mido.open_input("Teensy MIDI")
        try:
            time.sleep(args.boot_wait_s)
            send_note(out, TRACK_SELECT_NOTE_BASE + (args.track - 1), CONTROL_CHANNEL_1BASED, 120)
            time.sleep(0.4)
            send_note(out, LOOP_SELECT_NOTE_BASE + (slot - 1), CONTROL_CHANNEL_1BASED, 120)
            time.sleep(0.6)
            if not args.cumulative and slot == args.first_slot and i == 0:
                pass  # first slot of a fresh sweep; nothing to clear
            stream_bars(out, bars=args.record_bars, bpm=args.bpm,
                        channel_1based=args.midi_channel, low=48, high=79, start=True)
            time.sleep(0.2)
            send_note(out, RECORD_BUTTON_NOTE, CONTROL_CHANNEL_1BASED, 120)
            time.sleep(args.settle_ms / 1000.0)
            stop_transport_cleanly(out, s)
            # Keep draining after the stops so any final PERS/DIAG burst is read out before the port
            # closes; closing into an in-flight write is what wedged the board.
            drain(s, 2.0)
            saved = sum(1 for l in s.lines if "saved successfully" in l)
            print(f"[sweep] step {i} slot {slot}: saved_lines={saved} total_lines={len(s.lines)}")
        finally:
            out.close()
            in_port.close()
            s.close()

        # ---- verified reset + boot census ----
        reset_ok = force_reset(args.port)
        time.sleep(2.0)
        args.port = wait_for_port() or args.port
        banner, reconcile, loaded, empty_states = False, 0, [], []
        census = {"phase_before_free": None, "phase_after_free": None,
                  "post_restore_free_chunks": None, "census_marker": None, "library_rows": []}
        try:
            s2 = Session(args.port, step_dir / "boot.log")
        except Exception as exc:  # noqa: BLE001 - recorded, not fatal
            print(f"[sweep] step {i}: could not open the post-reset port ({exc})")
            steps.append({"step": i, "slot": slot, "saved_lines": saved, "reset_ok": reset_ok,
                          "boot_banner": False, "error": str(exc)})
            continue
        try:
            time.sleep(args.boot_wait_s)
            banner = any("boot_reset" in l for l in s2.lines)
            reconcile = sum(1 for l in s2.lines if "could not reconcile" in l)
            census = boot_census(s2)
            # N = distinct (track, slot) requests that reached `committed` this boot, from the A1.8 RRQ line.
            committed = set()
            for l in s2.lines:
                m = re.search(r"RRQ,(\d+),(\d+),\d+,\d+,terminal,none,committed", l)
                if m:
                    committed.add((int(m.group(1)), int(m.group(2))))
            committed_slots = len(committed)
            empty_states = [l for l in s2.lines if "loaded state: EMPTY" in l]
            loaded = [l for l in s2.lines if "loaded state:" in l]
        finally:
            s2.close()

        row = {"step": i, "slot": slot, "saved_lines": saved, "reset_ok": reset_ok,
               "boot_banner": banner, "reconcile_warnings": reconcile,
               "free_before": census["phase_before_free"],
               "free_after": census["phase_after_free"],
               "n_committed_slots": committed_slots,
               "post_restore_free_chunks": census["post_restore_free_chunks"],
               "census_marker": census["census_marker"],
               "library_rows": census["library_rows"],
               "empty_state_count": len(empty_states),
               "loaded_state_lines": loaded}
        steps.append(row)
        print(f"[sweep] step {i} slot {slot}: banner={banner} reconcile={reconcile} "
              f"N={committed_slots} post_restore_free={row['post_restore_free_chunks']} "
              f"(walk {row['free_before']}->{row['free_after']}) "
              f"rows={len(census['library_rows'])} elapsed={time.monotonic()-t0:.0f}s")

        if not banner:
            print("[sweep] STOP: no boot banner, the reset did not take")
            break
        if reconcile:
            print("[sweep] STOP: catalog reconcile failed (the failure point)")
            break

    (out_dir / "sweep-result.json").write_text(
        json.dumps({"steps": steps, "cumulative": args.cumulative,
                    "record_bars": args.record_bars, "track": args.track}, indent=2),
        encoding="utf-8")
    print(f"\n[sweep] wrote {out_dir / 'sweep-result.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
