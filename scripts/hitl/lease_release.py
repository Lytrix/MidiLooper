#!/usr/bin/env python3
"""`DIAG,ws_release,<slot>,<generation>,<outcome>` -- the working-set lease release (S4.1).

One owner for this row's wire form and its reading, because several consumers have to agree on what a
release row *is*: the retention oracle reports it, the S4.1 device-leg reader gates on it, and the change's
own record cites it. It names the **lease**, which `DIAG,canonical_release` does not -- `canonical_release`
reports the retiring *occupancy* (the capacity a successor can reuse), while this row reports the
`WorkingSetLeaseSlot` release with the identity the caller presented and the outcome the owner returned.

Outcomes (`Track::releaseEligiblePlaybackRuntimeSlots` -> `workingSetOutcomeLabel`):

    released           the slot's runtime was reset and its lease released
    refused_busy       a live owner refused the reset; the lease survives (the S4.1 guard)
    stale_generation   a late release on a reused slot, rejected by identity (the S4.2 rejection)

`NotHeld` -- the ordinary "nothing here" answer -- is deliberately **not** emitted, so a slot with no row
means "no release was attempted here, or the slot held no lease", never "the release failed". A reader must
therefore treat absence as absence.

Base convention, because it has cost two wrong readings in this change: the firmware's slot field is
**0-based** (`RECA,3` is what the button calls `Loop 4`), while the human `Loop N:` rows and every driver CLI
are **1-based**. This module speaks 0-based slots and exposes `LeaseRelease.slot_number` for the single
conversion, so a reader can print both instead of leaving the base to be guessed.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Iterable, Optional, Sequence

WS_RELEASE_TAG = "DIAG,ws_release,"
WS_RELEASE_RE = re.compile(r"^DIAG,ws_release,(\d+),(\d+),([a-z_]+)$")

_CAP_ROW_RE = re.compile(r"^#CAP,(\d+),(.*)$")
# The human rows a release edge is attributed to: `[69.966] [INFO] MIDI: Undo (entries=3)`, a phase print.
_HUMAN_ROW_RE = re.compile(r"^\[(\d+(?:\.\d+)?)\]\s+\[([A-Z]+)\]\s*(.*)$")
# Rows that can carry *why* a sweep ran: a control gesture (`GS,<ch>,<note>,<value>` -- the reset-triggering
# press) or a capture-stop/retirement edge. This is attribution, not gating: the reader quotes them.
_EDGE_MARKERS = (
    "GS,",
    "RECS,stage,record_stop,",
    "RECS,stop",
    "ST,Track,",
    "DIAG,canonical_release,",
    "ODUB,stop,",
    "DIAG,reentry",
)


@dataclass(frozen=True)
class LeaseRelease:
    """One release row, with the line number it was read from (1-based, for citations)."""

    us: int
    slot0: int
    generation: int
    outcome: str
    line_no: int

    @property
    def slot_number(self) -> int:
        """The 1-based loop number: the button's `Loop N` and every driver CLI's `--slot`."""
        return self.slot0 + 1

    @property
    def body(self) -> str:
        """The row without its `#CAP,<us>,` envelope."""
        return f"{WS_RELEASE_TAG}{self.slot0},{self.generation},{self.outcome}"

    @property
    def raw(self) -> str:
        return f"#CAP,{self.us},{self.body}"

    @property
    def released(self) -> bool:
        return self.outcome == "released"


def cap_row_body(line: str) -> Optional[tuple[int, str]]:
    """`(#CAP device microseconds, body)` for a captured row, else None."""
    match = _CAP_ROW_RE.match(line.strip())
    if not match:
        return None
    return int(match.group(1)), match.group(2)


def lease_release_rows(lines: Iterable[str]) -> list[LeaseRelease]:
    """Every working-set lease release row in order, each with the line it came from."""
    rows: list[LeaseRelease] = []
    for index, line in enumerate(lines, start=1):
        body = cap_row_body(line)
        if body is None:
            continue
        match = WS_RELEASE_RE.match(body[1])
        if match:
            rows.append(LeaseRelease(us=body[0], slot0=int(match.group(1)),
                                     generation=int(match.group(2)), outcome=match.group(3),
                                     line_no=index))
    return rows


def release_rows_for_slot(rows: Sequence[LeaseRelease], slot0: int) -> list[LeaseRelease]:
    """The release rows naming this slot. A row for a sibling slot is not this slot's release."""
    return [row for row in rows if row.slot0 == slot0]


def released_slot_numbers(rows: Sequence[LeaseRelease]) -> list[int]:
    """The 1-based loop numbers that were actually released (`released`, not merely named)."""
    return sorted({row.slot_number for row in rows if row.released})


def outcome_for_slot(rows: Sequence[LeaseRelease], slot0: int) -> str:
    """`released`, the last non-released outcome, or `absent` -- absence is absence, never a pass."""
    named = release_rows_for_slot(rows, slot0)
    if not named:
        return "absent"
    if any(row.released for row in named):
        return "released"
    return named[-1].outcome


def release_context(lines: Sequence[str], row: LeaseRelease, *, window_us: int = 2_000_000,
                    limit: int = 3) -> list[str]:
    """The rows **on the same device clock** just before the release, in order, nearest last.

    Attribution has to be by clock, not by file position: a managed capture writes the console stream and the
    `#CAP` ring stream separately, so the human `[<seconds>]` row that caused a reset can sit thousands of
    lines away from the `#CAP` rows it explains (the base preset's undo does exactly that,
    `captures/session_20261001_092818.log`: `[69.966] [INFO] MIDI: Undo` against
    `#CAP,69967546,DIAG,ws_release,0,0,released`). Both clocks are the device's own -- the human row's seconds
    and the `#CAP` row's microseconds agree -- so the window selects the edge and drops the unrelated
    thousands of rows between them. Human rows are quoted always; captured rows only when they carry a gesture
    or a stop/retirement marker, so a note-off burst does not crowd out the cause.

    This is quoting, not gating: the reader prints these beside the row and the operator reads the edge.
    """
    quoted: list[tuple[int, str]] = []
    for line in lines:
        text = line.strip()
        if not text or text == row.raw:
            continue
        clock: int | None = None
        display = text
        human = _HUMAN_ROW_RE.match(text)
        cap = _CAP_ROW_RE.match(text)
        if human:
            clock = int(float(human.group(1)) * 1_000_000)
            display = f"[{human.group(1)}] [{human.group(2)}] {human.group(3)}"
        elif cap and any(marker in text for marker in _EDGE_MARKERS):
            clock = int(cap.group(1))
        if clock is None:
            continue
        delta = row.us - clock
        if 0 <= delta <= window_us:
            quoted.append((clock, display))
    quoted.sort()
    return [display for _clock, display in quoted[-limit:]]
