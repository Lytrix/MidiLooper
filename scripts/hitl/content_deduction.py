"""Validate the overlap-resolve **deduction** between a stopped loop's storage rows and its effective view.

`!CAPTURE_CONTENT` prints a stopped loop three ways (`docs/Guides/HITL_CONTENT_VERIFICATION.md`): the record pass's
rows, each committed overdub pass's rows, and `effective#0` -- the merged view
(`LoopPasses::materializeToEventVector` -> `applyActiveEditPasses`). Where a later producer's note covers an earlier
note of the same pitch, the committed edit pass is **not** a rewrite of storage: it **Hides** the covered note, or
**Shortens** it. `src/EditManager/ResolveConstrainedGeometry.cpp` resolves `CompleteCover`/`OverlapNoteOn` to a full
Hide, and `Loop::resolveOccupancyAtNoteOn` (`src/Loop/LoopPendingNoteChange.cpp`) applies the live-hold rule: with
`holdPhaseTick` being the covering note's own NoteOn tick, a covered note whose NoteOn is **at or after** it is HIDDEN,
and one starting **before** it is SHORTENED to `holdPhaseTick - 1`. A hidden note keeps no remnant, even where it
extends past the covering note's end (`OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md`).

So `count(record) + count(overdub) != count(effective)` is a **logical outcome** when such a covering row exists, not
loss -- and the guide states its lossless-count invariant "for this procedure's material", i.e. for material without
one. This owner decides which of the two a dump shows, by validating the deduction and **naming** it:

  * every effective row must be either a storage row, or a storage row whose NoteOff was shortened to a coverer's
    NoteOn tick minus one;
  * every storage row missing from the effective view must be **covered** by a *later* producer (the dump's own group
    order: the record pass first, then each overdub pass) on the same channel and pitch: a **Hide** needs the covering
    NoteOn at or before the covered one and the covered NoteOn inside the covering note's sounding, and a **Shorten**
    needs the covering NoteOn inside the covered note's own sounding;
  * anything else is reported as **unexplained**, so a genuine difference still fails.

The rule cited here is the owners'; this module re-derives nothing the device already decides, and it never treats an
unexplained difference as a deduction.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Iterable, Optional, Sequence

#: MIDI status nibbles as the dump writes them (`<type>`): a NoteOn with velocity 0 counts as a NoteOff.
NOTE_ON = 144
NOTE_OFF = 128


def _within(tick: int, start: int, end: int, *, loop_length: int) -> bool:
    """True when ``tick`` lies in the wrap-aware sounding interval ``[start, end)``."""
    if loop_length <= 0:
        return False
    if end <= start:
        end += loop_length
    if tick < start:
        tick += loop_length
    return start <= tick < end


@dataclass(frozen=True)
class Row:
    """One `CAPTURE_CONTENT,event` row, with the fields the deduction needs."""

    group: str          # `record#0`, `overdub#0`, `effective#0`
    group_index: int    # the dump's producer order (first appearance) -- record first, then each overdub pass
    tick: int
    status: int
    channel: int
    note: int
    velocity: int

    @property
    def is_note_on(self) -> bool:
        return self.status == NOTE_ON and self.velocity > 0


@dataclass(frozen=True)
class Note:
    """A storage note and the interval it sounds for.

    `ends_at` is the stored NoteOff's tick, or -- for a note with no stored NoteOff -- its own NoteOn tick plus one
    loop, because such a note sounds across the loop boundary rather than stopping at it.
    """

    group: str
    group_index: int
    channel: int
    note: int
    on: Row
    off: Optional[Row]
    ends_at: int

    def covers(self, other: "Note", *, loop_length: int) -> bool:
        """True when this note is sounding at ``other``'s NoteOn tick (same channel and pitch, wrap-aware)."""
        if self.channel != other.channel or self.note != other.note:
            return False
        return _within(other.on.tick, self.on.tick, self.ends_at, loop_length=loop_length)

    def sounds_at(self, tick: int, *, loop_length: int) -> bool:
        """True when this note is sounding at ``tick`` (wrap-aware)."""
        return _within(tick, self.on.tick, self.ends_at, loop_length=loop_length)

    def phase_tick(self, tick: int, *, loop_length: int) -> int:
        """``tick`` read in this note's own iteration: advanced by a loop while it sits before this note's NoteOn.

        That is the comparison the live-hold rule makes (`note.startTick` against `holdPhaseTick`): a coverer that
        starts late in the loop and sounds across the boundary hides a note that starts early in the next iteration.
        """
        while loop_length > 0 and tick < self.on.tick:
            tick += loop_length
        return tick


def producer_group(kind: str) -> bool:
    """True for a producer group (`record#n`/`overdub#n`), False for the merged `effective#n` view."""
    return kind.startswith(("record#", "overdub#"))


def rows_from_kinds(per_kind: dict[str, Sequence[tuple[int, int, int, int, int]]]) -> list[Row]:
    """`Row`s for every parsed group, with the dump's own producer order as `group_index`."""
    order: list[str] = []
    rows: list[Row] = []
    for kind, entries in per_kind.items():
        if kind not in order:
            order.append(kind)
        index = order.index(kind)
        for tick, status, channel, note, velocity in entries:
            rows.append(Row(group=kind, group_index=index, tick=tick, status=status, channel=channel, note=note,
                            velocity=velocity))
    return rows


def storage_rows(rows: Iterable[Row]) -> list[Row]:
    """The producer rows: every group that is not the merged `effective` view."""
    return [row for row in rows if producer_group(row.group)]


def _notes(rows: Sequence[Row], *, loop_length: int) -> list[Note]:
    """Pair each storage NoteOn with the next NoteOff of the same group, channel and pitch, in tick order."""
    notes: list[Note] = []
    ons: dict[tuple[str, int, int], list[Row]] = {}
    offs: dict[tuple[str, int, int], list[Row]] = {}
    for row in rows:
        key = (row.group, row.channel, row.note)
        (ons if row.is_note_on else offs).setdefault(key, []).append(row)
    for key, on_rows in ons.items():
        pending = sorted(offs.get(key, []), key=lambda r: r.tick)
        for on_row in sorted(on_rows, key=lambda r: r.tick):
            match = next((row for row in pending if row.tick >= on_row.tick), None)
            if match is not None:
                pending.remove(match)
            notes.append(Note(group=on_row.group, group_index=on_row.group_index, channel=on_row.channel,
                              note=on_row.note, on=on_row, off=match,
                              ends_at=match.tick if match is not None else on_row.tick + loop_length))
    return notes


def validate_deduction(
    *,
    per_kind: dict[str, Sequence[tuple[int, int, int, int, int]]],
    loop_length: int,
) -> dict[str, Any]:
    """Validate the storage -> effective deduction and name every part of it.

    Returns `{"accounted": bool, "checked": bool, "hidden": [...], "shortened": [...], "unexplained": [...]}`.
    `accounted` is True when every difference between the two views is explained -- including the vacuous case of an
    identical view, which the raw comparison already covers -- and False with the remainder named in `unexplained`.
    `checked` is False when the dump carries no effective rows, so a caller never reads an absent comparison as a pass.
    """
    rows = rows_from_kinds(per_kind)
    storage = storage_rows(rows)
    effective = [row for row in rows if not producer_group(row.group)]
    out: dict[str, Any] = {"accounted": False, "checked": False, "hidden": [], "shortened": [],
                           "unexplained": []}
    if not effective:
        return out
    out["checked"] = True

    # The merged view states no producer, so an effective row is matched on its event fields alone.
    remaining = list(effective)

    def take(row: Row) -> bool:
        key = (row.tick, row.status, row.channel, row.note, row.velocity)
        for candidate in remaining:
            if (candidate.tick, candidate.status, candidate.channel, candidate.note,
                    candidate.velocity) == key:
                remaining.remove(candidate)
                return True
        return False

    notes = _notes(storage, loop_length=loop_length)
    unexplained: list[str] = []
    for note in sorted(notes, key=lambda n: (n.group_index, n.on.tick)):
        later = [c for c in notes
                 if c.group_index > note.group_index and c.group != note.group
                 and c.channel == note.channel and c.note == note.note]
        on_present = take(note.on)
        off_present = note.off is not None and take(note.off)
        if on_present and (note.off is None or off_present):
            continue
        if on_present and note.off is not None:
            # Shorten: the covered note keeps its NoteOn and its NoteOff moves to a coverer's NoteOn tick - 1, so the
            # coverer has to begin *inside* the covered note's own sounding interval.
            shorts = [c for c in later if note.sounds_at(c.on.tick, loop_length=loop_length)]
            if shorts:
                coverer = min(shorts, key=lambda c: c.on.tick)
                moved = coverer.on.tick - 1
                shortened = [r for r in remaining if r.status == NOTE_OFF and r.channel == note.channel
                             and r.note == note.note and r.tick == moved]
                if shortened:
                    remaining.remove(shortened[0])
                    out["shortened"].append(f"{note.group} note {note.note}: Off {note.off.tick} -> {moved} "
                                            f"(covered by {coverer.group} NoteOn {coverer.on.tick})")
                    continue
            unexplained.append(f"{note.group} NoteOff (tick {note.off.tick}, note {note.note}) is not in the "
                               f"effective view and no covering NoteOn explains it")
            continue
        # The NoteOn itself is gone: the Hide case. The coverer starts at or before it and is still sounding there.
        hiding = [c for c in later
                  if c.covers(note, loop_length=loop_length)
                  and c.phase_tick(note.on.tick, loop_length=loop_length) >= c.on.tick]
        if hiding:
            coverer = max(hiding, key=lambda c: c.on.tick)
            out["hidden"].append(f"{note.group} note {note.note} (On {note.on.tick}"
                                 + (f", Off {note.off.tick}" if note.off is not None else ", held")
                                 + f") hidden by {coverer.group} On {coverer.on.tick}")
            continue
        unexplained.append(f"{note.group} NoteOn (tick {note.on.tick}, note {note.note}) is not in the effective "
                           f"view and no covering later row explains it")

    for leftover in remaining:
        unexplained.append(f"effective {leftover.status} tick {leftover.tick} note {leftover.note} is in no "
                           f"storage group")
    out["unexplained"] = unexplained
    out["accounted"] = not unexplained
    return out
