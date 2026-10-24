"""Read the Record Snapshot owner progress needed by S7.5b.5 HITL runs.

This is separate from the rolling-window lane oracle: it answers whether bounded
Snapshot work progresses during Record, continues after stop, and publishes the
Record session-exit root.
"""
from __future__ import annotations

from typing import Any

from hitl.verify.windowed_lane import row_us

SNAPSHOT_GATE_ROW = "DIAG,snap_gate,"


def snapshot_gate_rows(lines: list[str]) -> list[dict[str, Any]]:
    """Parse qualification-only Snapshot owner rows by their emitted key names."""
    required = ("t", "s", "rec", "step", "elig", "st", "cu", "rv", "p", "sep", "fc", "hp")
    rows: list[dict[str, Any]] = []
    for index, line in enumerate(lines):
        if SNAPSHOT_GATE_ROW not in line:
            continue
        fields: dict[str, str] = {}
        for item in line.split(SNAPSHOT_GATE_ROW, 1)[1].split(","):
            if "=" not in item:
                continue
            key, value = item.split("=", 1)
            fields[key.strip()] = value.strip()
        if any(key not in fields for key in required):
            continue
        try:
            parsed = {key: int(fields[key]) for key in required}
        except ValueError:
            continue
        rows.append({"index": index, "us": row_us(line), **parsed})
    return rows


def verify_record_snapshot_progress(
    lines: list[str], *, track_number: int | None, loop_slot: int | None
) -> dict[str, Any]:
    """Verify progress before/after the last Record stop for a declared 1-based target."""
    # `DIAG,snap_gate` is qualification-only instrumentation (DebugSessionCapture.h). A capture with none of
    # those rows at all is a build that does not compile the instrument (shipping / ROLLING=0 cold image), not a
    # failed snapshot: the term is not applicable there. A qualification capture with rows for other targets but
    # not the declared one still fails below.
    if not snapshot_gate_rows(lines):
        return {"applicable": False, "ok": True, "checks": [],
                "detail": "no DIAG,snap_gate rows in this capture; snapshot gate is qualification-only"}
    stop_us = None
    for line in lines:
        if "Recording stopped @" in line:
            stop_us = row_us(line)
    if stop_us is None:
        issue = "no timestamped Recording stopped row; Snapshot progress is unobservable"
        return {"applicable": True, "ok": False, "checks": [], "issues": [issue], "detail": issue}
    if track_number is None or loop_slot is None:
        issue = "record Snapshot verification requires declared track and loop slot"
        return {"applicable": True, "ok": False, "checks": [], "issues": [issue], "detail": issue}

    expected_track, expected_slot = track_number - 1, loop_slot - 1
    rows = [row for row in snapshot_gate_rows(lines)
            if row["t"] == expected_track and row["s"] == expected_slot and row["us"] is not None]
    if not rows:
        issue = f"no DIAG,snap_gate rows for record target t={expected_track} s={expected_slot}"
        return {"applicable": True, "ok": False, "checks": [], "issues": [issue], "detail": issue}

    before_stop = [row for row in rows if row["us"] < stop_us]
    after_stop = [row for row in rows if row["us"] >= stop_us]
    live = [row for row in before_stop if row["rec"] == 1 and row["elig"] == 1 and row["step"] == 1]
    live_progress = any(row["cu"] > 0 for row in live)
    live_max_cu = max((row["cu"] for row in live), default=0)
    record_rv = before_stop[-1]["rv"] if before_stop else None
    record_st = max((row["st"] for row in before_stop), default=0)

    session_exit = [row for row in after_stop if row["rec"] == 0 and row["sep"] == 1]
    eligible_exit = [row for row in session_exit if row["elig"] == 1 and row["step"] == 1]
    exit_st = max((row["st"] for row in eligible_exit), default=0)
    exit_max_cu = max((row["cu"] for row in eligible_exit), default=0)
    continued = bool(eligible_exit) and exit_st > record_st and exit_max_cu > 0

    # **The post-step confirmation.** A publication can be the owner's **final** eligible turn, and the row family is
    # read *before* and *after* the step -- so "some settled row exists that names a new revision" is not enough: if the
    # publication were that final turn with no row after it, the last thing on the wire would still be the row that still
    # **owed** the work, and the check would have nothing to read. The confirming row therefore has to come *after* the
    # last row that still owed the session exit (`p`/`sep` set), which is what makes it a confirmation of the step rather
    # than a restatement of the demand.
    owed = [row for row in after_stop if row["p"] == 1 or row["sep"] == 1]
    owed_index = owed[-1]["index"] if owed else -1
    published_rows = [row for row in after_stop
                      if row["p"] == 0 and row["sep"] == 0 and row["fc"] == 0 and row["hp"] == 1
                      and row["index"] > owed_index]
    published = record_rv is not None and any(row["rv"] != record_rv for row in published_rows)
    published_rv = next((row["rv"] for row in reversed(published_rows)
                         if record_rv is not None and row["rv"] != record_rv), None)

    checks = [
        {"name": "snapshot_progress_during_recording", "ok": live_progress,
         "detail": f"eligible_record_rows={len(live)} max_cu={live_max_cu}"},
        {"name": "snapshot_owner_continues_after_stop", "ok": continued,
         "detail": f"session_exit_rows={len(session_exit)} eligible_rows={len(eligible_exit)} "
                   f"st_before={record_st} st_after={exit_st} max_cu={exit_max_cu}"},
        {"name": "record_session_exit_root_published", "ok": published,
         "detail": f"record_rv={record_rv} published_rv={published_rv} "
                   f"post_step_confirmed={bool(published_rows)} owed_index={owed_index}"},
    ]
    issues = [f"{check['name']}: {check['detail']}" for check in checks if not check["ok"]]
    return {
        "applicable": True,
        "ok": not issues,
        "checks": checks,
        "issues": issues,
        "target": {"track": expected_track, "slot": expected_slot},
        "record_stop_us": stop_us,
    }
