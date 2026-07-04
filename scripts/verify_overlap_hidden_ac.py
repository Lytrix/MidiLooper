#!/usr/bin/env python3
"""Verify BUG.md AC1–AC5 for overlap-hidden-note-select from edit baseline serial log."""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

B_STEP_TICK = 4 * 48  # fixture B step 4
B_PITCH = 64
A_PITCH = 67  # M0 after overlap round-trip pitch change
D_STEP_TICK = 18 * 48  # fixture D step 18
TICK_TOLERANCE = 48  # one 16th
MIN_LENGTHENED_M0_TICKS = 300  # lengthened M0 >> default 2-step gate (~96)

FINAL_NOTE_PAT = re.compile(
    r"Final note: pitch=(\d+), start=(\d+), end=(\d+)"
)
SESSION_DROP_PAT = re.compile(
    r"session_store: M64@(\d+) missing in recon flatEvents=(\d+)"
)
TAKE_ONLY_PAT = re.compile(
    r"take_only: M64 start=\d+ end=\d+ flatEvents=(\d+)"
)
RESTORE_DELETED_PAT = re.compile(
    r"Restoring deleted note: pitch=(\d+), start=(\d+), end=(\d+)"
)
RESTORE_HIDDEN_PAT = re.compile(
    r"Restoring hidden overlap note: pitch=(\d+), start=(\d+), end=(\d+)"
)


def _tick_near(actual: int, expected: int, tolerance: int = TICK_TOLERANCE) -> bool:
    return abs(actual - expected) <= tolerance


def _overlap_note_matches(
    pitch_a: int,
    start_a: int,
    pitch_b: int,
    start_b: int,
    *,
    end_a: int | None = None,
    end_b: int | None = None,
) -> bool:
    if pitch_a != pitch_b:
        return False
    if not _tick_near(start_a, start_b):
        return False
    if end_a is not None and end_b is not None and not _tick_near(end_a, end_b):
        return False
    return True


def _m0_home_start(start: int) -> bool:
    for anchor in (0, 8, 16, 17):
        if _tick_near(start, anchor):
            return True
    return start <= TICK_TOLERANCE


def parse_final_notes(lines: list[str]) -> list[tuple[int, int, int]]:
    notes: list[tuple[int, int, int]] = []
    for line in lines:
        m = FINAL_NOTE_PAT.search(line)
        if m:
            notes.append((int(m.group(1)), int(m.group(2)), int(m.group(3))))
    return notes


def has_lengthened_m0_survivor(notes: list[tuple[int, int, int]]) -> bool:
    for pitch, start, end in notes:
        if pitch != A_PITCH:
            continue
        if _m0_home_start(start) and (end - start) >= MIN_LENGTHENED_M0_TICKS:
            return True
    return False


def has_d_delay_survivor(
    notes: list[tuple[int, int, int]], b_start: int
) -> bool:
    for pitch, start, _end in notes:
        if pitch != B_PITCH:
            continue
        if _tick_near(start, b_start):
            continue
        if _tick_near(start, D_STEP_TICK):
            return True
    return False


def parse_hidden(lines: list[str]) -> list[dict]:
    hidden: list[dict] = []
    hide_pat = re.compile(
        r"Stored hidden overlap note: pitch=(\d+), start=(\d+), end=(\d+)"
    )
    restores: list[dict] = []
    for i, line in enumerate(lines):
        for restore_pat in (RESTORE_DELETED_PAT, RESTORE_HIDDEN_PAT):
            m = restore_pat.search(line)
            if m:
                restores.append(
                    {
                        "line": i,
                        "pitch": int(m.group(1)),
                        "start": int(m.group(2)),
                        "end": int(m.group(3)),
                    }
                )
                break
    for i, line in enumerate(lines):
        m = hide_pat.search(line)
        if not m:
            continue
        pitch, start, end = int(m.group(1)), int(m.group(2)), int(m.group(3))
        restore_line: int | None = None
        for r in restores:
            if r["line"] <= i:
                continue
            if _overlap_note_matches(
                pitch, start, r["pitch"], r["start"], end_a=end, end_b=r["end"]
            ):
                restore_line = r["line"]
                break
        hidden.append(
            {
                "line": i,
                "pitch": pitch,
                "start": start,
                "end": end,
                "restore_line": restore_line,
            }
        )
    return hidden


def parse_selects(lines: list[str]) -> list[dict]:
    sel_pat = re.compile(
        r"Select fader: selected note (-?\d+) at tick (\d+)"
    )
    dnte_pat = re.compile(r"#CAP,\d+,DNTE,(\d+),(\d+),(\d+),(\d+),(-?\d+)")
    out: list[dict] = []
    for i, line in enumerate(lines):
        m = sel_pat.search(line)
        if not m:
            continue
        note_idx = int(m.group(1))
        tick = int(m.group(2))
        dnte = None
        for j in range(i, min(i + 20, len(lines))):
            dm = dnte_pat.search(lines[j])
            if dm:
                dnte = {
                    "pitch": int(dm.group(1)),
                    "storage_start": int(dm.group(2)),
                    "display_start": int(dm.group(3)),
                    "selected_idx": int(dm.group(5)),
                }
                break
        out.append({"line": i, "note_idx": note_idx, "tick": tick, "dnte": dnte})
    return out


def check_ac1(hidden: list[dict], selects: list[dict]) -> tuple[bool, str]:
    issues: list[str] = []
    unrestored = 0
    for h in hidden:
        for s in selects:
            if s["line"] <= h["line"]:
                continue
            if h["restore_line"] is not None and s["line"] >= h["restore_line"]:
                continue
            if s["dnte"] is None:
                continue
            dnte = s["dnte"]
            if not _overlap_note_matches(
                h["pitch"], h["start"], dnte["pitch"], dnte["storage_start"]
            ):
                continue
            if not (
                _tick_near(s["tick"], h["start"])
                or _tick_near(dnte["display_start"], h["start"])
            ):
                continue
            issues.append(
                f"select@{s['line']} picked unrestored hidden {h['pitch']}@{h['start']} "
                f"(hidden line {h['line']})"
            )
        if h["restore_line"] is None:
            unrestored += 1
    if issues:
        return False, "; ".join(issues[:3])
    restored = sum(1 for h in hidden if h["restore_line"] is not None)
    return True, (
        f"no select on unrestored Hidden overlap "
        f"({len(hidden)} hide, {restored} restored before re-select)"
    )


def check_ac2(selects: list[dict]) -> tuple[bool, str]:
    issues: list[str] = []
    checked = 0
    for s in selects:
        if s["note_idx"] < 0:
            continue
        if s["dnte"] is None:
            continue
        checked += 1
        if s["dnte"]["display_start"] % 1536 != s["tick"] % 1536:
            issues.append(
                f"select@{s['line']} tick={s['tick']} "
                f"!= DNTE display_start={s['dnte']['display_start']}"
            )
        if s["dnte"]["selected_idx"] != s["note_idx"]:
            issues.append(
                f"select@{s['line']} idx={s['note_idx']} "
                f"!= DNTE sel={s['dnte']['selected_idx']}"
            )
    if issues:
        return False, "; ".join(issues[:3])
    return True, f"{checked} DNTE-backed selects aligned (212149 index≠tick class absent)"


def check_ac3(lines: list[str]) -> tuple[bool, str]:
    delete_pat = re.compile(
        r"Deleting note (?:noteId=\d+ )?pitch=(\d+), start=(\d+), end=(\d+)"
    )
    cl_before = re.compile(r"Edit committed ChangeLength start=8")
    deletes: list[tuple[int, int, int]] = []
    for i, line in enumerate(lines):
        m = delete_pat.search(line)
        if m:
            deletes.append((int(m.group(1)), int(m.group(2)), int(m.group(3)), i))
    # Fixture delete B: pitch 64 near step 4 (~192–240)
    b_deletes = [
        d for d in deletes if d[0] == B_PITCH and abs(d[1] - B_STEP_TICK) <= TICK_TOLERANCE
    ]
    if not b_deletes:
        return False, f"no delete pitch={B_PITCH} near tick {B_STEP_TICK}; got {deletes}"
    pitch, start, end, line_no = b_deletes[0]
    # Pre-delete ChangeLength on long mover @8 is the 212149 failure signature
    window = lines[max(0, line_no - 30) : line_no]
    if any(cl_before.search(w) for w in window):
        return False, f"ChangeLength on mover@8 before delete B @{start} (line {line_no})"
    wrong = [d for d in deletes if d[0] == 67 and d[1] <= 16]
    if wrong:
        return False, f"deleted long mover M67@{wrong[0][1]} instead of B"
    return True, f"delete B pitch={pitch} start={start} end={end} (line {line_no})"


def check_ac4(lines: list[str]) -> tuple[bool, str]:
    disp_pat = re.compile(r"#CAP,\d+,DISP,0,PLAYING,1536,(\d+),(\d+),(\d+),")
    mismatches = 0
    samples = 0
    for line in lines:
        m = disp_pat.search(line)
        if not m:
            continue
        flat_events = int(m.group(1))
        frame_notes = int(m.group(3))
        samples += 1
        # frameNotes is filtered display count; should not exceed flatEvents
        if frame_notes > flat_events:
            mismatches += 1
    if samples == 0:
        return False, "no NOTE_EDIT DISP samples"
    return True, f"{samples} DISP frames; frameNotes<=flatEvents ({mismatches} violations)"


def check_ac5(lines: list[str]) -> tuple[bool, str]:
    delete_pat = re.compile(
        r"Deleting note (?:noteId=\d+ )?pitch=(\d+), start=(\d+), end=(\d+)"
    )
    delete_line = None
    b_start: int | None = None
    for i, line in enumerate(lines):
        m = delete_pat.search(line)
        if not m or int(m.group(1)) != B_PITCH:
            continue
        start = int(m.group(2))
        if abs(start - B_STEP_TICK) <= TICK_TOLERANCE:
            delete_line = i
            b_start = start
            break
    if delete_line is None or b_start is None:
        return False, "delete B not found"

    window = lines[delete_line : delete_line + 12]
    session_drop = False
    flat_after: int | None = None
    for w in window:
        sm = SESSION_DROP_PAT.search(w)
        if sm and _tick_near(int(sm.group(1)), b_start):
            session_drop = True
            flat_after = int(sm.group(2))
            break
    if not session_drop:
        return False, f"post-delete session_store did not drop B @{b_start}"

    flat_before: int | None = None
    for w in window:
        tm = TAKE_ONLY_PAT.search(w)
        if tm:
            flat_before = int(tm.group(1))
            break
    flat_detail = ""
    if flat_before is not None and flat_after is not None:
        flat_detail = f"; flatEvents {flat_before}→{flat_after}"

    after_notes = parse_final_notes(lines[delete_line:])
    has_m67 = has_lengthened_m0_survivor(after_notes)
    has_d = has_d_delay_survivor(after_notes, b_start)
    if not has_m67:
        return False, (
            f"M67 lengthened missing after delete (212149-class reset){flat_detail}"
        )
    if not has_d:
        return False, f"D-delay M64 missing after delete{flat_detail}"
    return True, (
        f"M67 home+lengthened + D-delay survive delete B@{b_start}; "
        f"session_store drop{flat_detail}"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("serial_log", type=Path)
    args = parser.parse_args()
    lines = args.serial_log.read_text(encoding="utf-8", errors="replace").splitlines()
    hidden = parse_hidden(lines)
    selects = parse_selects(lines)
    checks = {
        "AC1": check_ac1(hidden, selects),
        "AC2": check_ac2(selects),
        "AC3": check_ac3(lines),
        "AC4": check_ac4(lines),
        "AC5": check_ac5(lines),
    }
    all_ok = True
    for ac, (ok, detail) in checks.items():
        status = "PASS" if ok else "FAIL"
        print(f"{ac}: {status} — {detail}")
        all_ok = all_ok and ok
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
