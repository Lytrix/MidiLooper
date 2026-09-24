#!/usr/bin/env python3
"""Read the S1.3 post-restore census checkpoint from a forced reset.

**The checkpoint is an explicit marker, not a delay and not "last terminal".** The firmware emits
`#CAP,DIAG,post_restore_census,...` once per boot when every condition holds, because neither restore
terminal identifies it: publish installs content and emits `committed`, cleanup runs on a *separate*
turn, deferred playback prewarm and boot undo hydration follow, and the remaining-library background
admission scan is incremental on top. The marker carries the readiness fields and `free_chunks`, which is
the value S2b.3 needs (`post_restore_free_chunks >= peak_capture_reservation + 16`).

Readiness the marker attests (see `StorageManager::emitPostRestoreCensusIfReady`):
`admission_complete` (the scan finished, so a momentarily empty queue cannot qualify),
`interactive_ready` (queue empty, no live `SlotLoadSession`, both job instances inactive),
`undo_hydrated`, `transport_stopped`, `deferred_prewarm_idle` (no prepared-pair request and no
post-commit merged-MIDI prewarm is still owed), and no post-load holdoff / in-flight persistence work.

The marker is a **timestamped** Tier-A capture line (`#CAP,<micros>,DIAG,post_restore_census,...`), so
`CaptureLineTier` classifies it and it is not lost to the timing-critical flush budget. `census.json`
records both the readiness attestation and the timestamped form, so a marker that somehow printed in the
old un-timestamped shape is a recorded failure rather than a silent pass.

**What the `capture_prewarm` walk fields mean** (`TrackPlaybackWindowBuild.cpp`): `full` = capture
preparation requested, `ready` = canonical capture output admitted, `credits` = available reserved
canonical capture chunks. These describe the **capture reservation**, not restored events; boot leaves
committed-but-inactive slots cold by design (S2b.1), so `credits=0` on a restored slot is expected and is
**not** evidence about growth. The script reports them as reservation telemetry and does not classify
"restored" from them.

Restored-slot identity and content come from the skill's independent `!CAPTURE_CONTENT` dump, not from
this walk.

Device prep follows the skill: reset by re-uploading (the only reset proven reliable here), then attach.

Usage:
  .venv/bin/python scripts/hitl_boot_census.py --out-dir captures/census-01 [--no-reset]
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

import serial

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

ROW_RE = re.compile(
    r"capture_prewarm,track=(\d+),slot=(\d+),full=(\d+),ready=(\d+),credits=(\d+),free_chunks=(\d+)")
PHASE_RE = re.compile(r"capture_prewarm,phase=(\w+).*free_chunks=(\d+)")
MARKER = "post_restore_census"
# The marker is a key=value DIAG line, so new readiness fields must not break the reader: parse the
# fields generically and read the ones this script reports.
FIELD_RE = re.compile(r"([a-z_]+)=(\d+)")


def wait_for_port(timeout_s: float = 30.0) -> str | None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        found = sorted(Path("/dev").glob("cu.usbmodem*"))
        if found:
            return str(found[0])
        time.sleep(0.1)
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--no-reset", action="store_true")
    ap.add_argument("--settle-s", type=float, default=20.0)
    args = ap.parse_args()

    out_dir: Path = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=False)

    if not args.no_reset:
        subprocess.run(["pio", "run", "-e", "teensy41-capture-serial", "-t", "upload"],
                       capture_output=True, timeout=180, check=False, cwd=str(REPO_ROOT))
    port = wait_for_port()
    if port is None:
        print("[census] no port after reset")
        return 1

    lines: list[str] = []
    stop = False
    ser = serial.Serial(port, 115200, timeout=0.1)
    log = (out_dir / "boot-census.log").open("w", encoding="utf-8")

    def reader() -> None:
        while not stop:
            try:
                raw = ser.readline()
            except Exception:
                break
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").rstrip()
            lines.append(line)
            log.write(line + "\n")

    t = threading.Thread(target=reader, daemon=True)
    t.start()
    # Wait for the explicit checkpoint marker. The settle is only an upper bound for a boot that never
    # reaches it (which is itself the finding: a required readiness condition never held).
    deadline = time.monotonic() + args.settle_s
    while time.monotonic() < deadline:
        if any(MARKER in line for line in lines):
            # Let the surrounding lines land before closing.
            time.sleep(1.0)
            break
        time.sleep(0.2)
    stop = True
    time.sleep(0.3)
    try:
        ser.close()
    except Exception:
        pass
    log.close()

    phases = [{"phase": m.group(1), "free_chunks": int(m.group(2))}
              for line in lines for m in [PHASE_RE.search(line)] if m]
    rows = [{"track": int(m.group(1)), "slot": int(m.group(2)), "full": int(m.group(3)),
             "ready": int(m.group(4)), "credits": int(m.group(5)), "free_chunks": int(m.group(6))}
            for line in lines for m in [ROW_RE.search(line)] if m]
    banner = any("boot_reset" in l for l in lines)
    reconcile = sum(1 for l in lines if "could not reconcile" in l)

    # Every pair, in arrival order, with the delta across it. The first pair is the enabled-set walk; a
    # later pair is the library walk. Neither is "after restores completed" on its own.
    checkpoints = []
    pending_before = None
    for p in phases:
        if p["phase"] == "before":
            pending_before = p["free_chunks"]
        elif p["phase"] == "after" and pending_before is not None:
            checkpoints.append({"before": pending_before, "after": p["free_chunks"],
                                "delta": p["free_chunks"] - pending_before})
            pending_before = None

    # Prefer the timestamped capture record; the operator `[StorageManager]` line is emitted first and is
    # not the Tier-A record. Selecting "the first line containing the marker" would report a false
    # un-timestamped failure when both are present.
    marker_line = next(
        (l for l in lines if re.match(r"#CAP,\d+,DIAG,post_restore_census,", l)), None)
    if marker_line is None:
        marker_line = next((l for l in lines if MARKER in l), None)
    marker = None
    if marker_line:
        # Every `key=value` after the marker name. `free_chunks` is the S2b.3 figure; the readiness
        # fields are the attestation that the checkpoint is a settled state.
        fields = {m.group(1): int(m.group(2)) for m in FIELD_RE.finditer(marker_line)}
        marker = {
            "free_chunks": fields.get("free_chunks"),
            "admission_complete": fields.get("admission_complete", 0),
            "interactive_ready": fields.get("interactive_ready", 0),
            "undo_hydrated": fields.get("undo_hydrated", 0),
            "transport_stopped": fields.get("transport_stopped", 0),
            "deferred_prewarm_idle": fields.get("deferred_prewarm_idle", 0),
            "heap_free": fields.get("heap_free"),
            "line": marker_line,
        }

    # A marker line is only the checkpoint when every readiness field is attested AND the line is the
    # timestamped capture form (`#CAP,<micros>,DIAG,post_restore_census,...`). The operator Serial line
    # alone would mean the emitter fired without the tiered record.
    readiness_keys = ("admission_complete", "interactive_ready", "undo_hydrated",
                      "transport_stopped", "deferred_prewarm_idle")
    readiness_attested = bool(marker) and all(marker.get(k) == 1 for k in readiness_keys)
    timestamped = bool(marker) and re.match(r"#CAP,\d+,DIAG,post_restore_census,", marker["line"]) is not None

    summary = {
        "port": port,
        "boot_banner": banner,
        "reconcile_warnings": reconcile,
        "checkpoint_reached": marker is not None,
        "checkpoint_readiness_attested": readiness_attested,
        "checkpoint_timestamped_capture_line": timestamped,
        "post_restore_free_chunks": marker["free_chunks"] if marker else None,
        "checkpoint_fields": marker,
        "checkpoints": checkpoints,
        "first_checkpoint": checkpoints[0] if checkpoints else None,
        "last_checkpoint": checkpoints[-1] if checkpoints else None,
        # Capture-reservation telemetry, reported for what it is. NOT a restoration signal.
        "walk_rows": rows,
        "walk_row_count": len(rows),
        "peak_capture_credits": max((r["credits"] for r in rows), default=0),
        "capture_fields_are_reservation_not_restore": True,
        "restore_terminals_seen": sum(
            1 for l in lines if "LoadLoopJob done" in l or "loop slot restore" in l.lower()),
        "post_restore_checkpoint_available": marker is not None,
        # S2b.3's acceptance term, when the marker landed. `peak_capture_credits` is the captured
        # reservation at the checkpoint; the +16 is CHUNK_RESERVE.
        "s2b3_min_required_free_chunks": (max((r["credits"] for r in rows), default=0) + 16)
        if marker else None,
        "s2b3_condition_met": (marker["free_chunks"] >= max((r["credits"] for r in rows), default=0) + 16)
        if marker else None,
    }
    (out_dir / "census.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")

    print(f"[census] banner={banner} reconcile={reconcile} "
          f"checkpoint_reached={marker is not None}")
    if marker:
        print(f"[census] POST-RESTORE free_chunks={marker['free_chunks']} "
              f"heap_free={marker['heap_free']} (admission={marker['admission_complete']} "
              f"interactive={marker['interactive_ready']} undo={marker['undo_hydrated']} "
              f"stopped={marker['transport_stopped']} "
              f"deferred_prewarm_idle={marker['deferred_prewarm_idle']})")
        print(f"[census] marker readiness_attested={readiness_attested} "
              f"timestamped_capture_line={timestamped}")
        if not timestamped:
            print("[census] WARNING: the marker was not the timestamped `#CAP,<micros>,DIAG,...` form; "
                  "it is not Tier-A-classified and the tier parser mis-reads its tag.")
    else:
        print("[census] checkpoint NOT reached in the window: a readiness condition never held. "
              "That is a finding, not a timing miss.")
    for i, c in enumerate(checkpoints):
        label = "enabled-set walk" if i == 0 else ("library walk" if i == 1 else f"walk {i}")
        print(f"   checkpoint {i} ({label}): {c['before']} -> {c['after']} (delta {c['delta']:+d})")
    print(f"[census] walk rows={len(rows)} peak capture credits={summary['peak_capture_credits']} "
          f"(reservation telemetry; zero is EXPECTED for restored slots)")
    print(f"[census] restore terminals seen in window={summary['restore_terminals_seen']} "
          f"-> post-restore checkpoint available={summary['post_restore_checkpoint_available']}")
    if marker:
        print(f"[census] S2b.3: free={marker['free_chunks']} vs required "
              f"{summary['s2b3_min_required_free_chunks']} "
              f"(peak_capture_reservation+16) -> met={summary['s2b3_condition_met']}")
    print("[census] The walk pairs above are walk checkpoints, not the post-restore one; the post-restore "
          "figure is the marker's free_chunks.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
