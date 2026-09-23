#!/usr/bin/env python3
"""Reduce a HITL session log to the runtime-working-set census.

`runtime-working-set-ownership` (S1.1/S1.3/S1.4/S1.5, S2b.3) is measured from three
line families the firmware already emits. Nothing on the host parsed them before this
script, so every number in `ARCHITECTURE-REVIEW.md` was read by hand.

* `#CAP,<us>,DIAG,capture_prewarm,...` — the boot/load census. `phase=before`/`after`
  bracket a sequence, and each per-track line carries the *resolved* `slot=` plus
  `free_chunks=`, so the per-committed-slot cost falls out as the `free_chunks` delta
  on tracks whose slot is non-zero.
* `[Memory] ext,<us>,phase=...,...` — the 1 Hz tick. Carries live/high water, the six
  size classes, `tags=` for **tags 1..7 only** (`MemoryMonitor.cpp` prints
  `tagBytes[1..7]`), the snapshot slab census, and `chunks_free=`.
* `#MEM_EXT_REFUSED,<us>,...tags=<vector>` — the only line carrying the **full** tag
  vector (tags 1..16 via `formatTagVector`), emitted only when the pool refuses.

Deliberate naming note: the tick's `pin1=`/`pin24=` fields are **not** canonical-chunk
pins. They are `snapshotSlabCensus().slabsAtMostOneNode` / `slabsTwoToFourNodes`
(`MemoryMonitor.cpp`), i.e. slab-occupancy buckets, and they are labelled as such here
so the distinction cannot be lost again.

Usage:
    .venv/bin/python scripts/parse_working_set_census.py <log-or-dir> [...] [--json OUT]
    .venv/bin/python scripts/parse_working_set_census.py --tick-table <log>
"""
from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, field
from pathlib import Path

# Tag numbering is the `ExternalPoolAccounting::Tag` enum.
TAG_NAMES: dict[int, str] = {
    1: "CaptureStopJobs",
    2: "ViewPrewarm",
    3: "CaptureStaging",
    4: "CanonicalOutput",
    5: "TransferHorizon",
    6: "CompanionHistory",
    7: "PlaybackOrder",
    8: "ResetPassTimeline",
    9: "PrepareFirstBar",
    10: "CompanionPairLookup",
    11: "CompanionSpans",
    12: "CompanionReconstruction",
    13: "CompanionApply",
    14: "CompanionBridgeMap",
    15: "CompanionPasses",
    16: "CanonicalPassInputs",
}

# Tags the 1 Hz tick actually prints.
TICK_TAGS = tuple(range(1, 8))

PREWARM_RE = re.compile(
    r"#CAP,(?P<us>\d+),DIAG,capture_prewarm,(?P<body>.+)$"
)
PREWARM_FIELD_RE = re.compile(r"(?P<key>[a-z_]+)=(?P<value>[^,]*)")
BOOT_RE = re.compile(r"#CAP,BOOT,(?P<body>.+)$")
EXT_RE = re.compile(r"\[Memory\] ext,(?P<body>.+)$")
REFUSED_RE = re.compile(r"#MEM_EXT_REFUSED,(?P<body>.+)$")
MEM_DENIED_RE = re.compile(r"#MEM_DENIED,(?P<body>.+)$")
OWNERS_RE = re.compile(r"#MEM_OWNERS,(?P<body>.+)$")

_INT_RE = re.compile(r"(-?\d+)")


def _fields(body: str) -> dict[str, str]:
    return {m.group("key"): m.group("value") for m in PREWARM_FIELD_RE.finditer(body)}


def _ints(value: str) -> list[int]:
    return [int(x) for x in _INT_RE.findall(value)]


def _kb(label: str) -> int:
    return int(_INT_RE.search(label).group(1)) if _INT_RE.search(label) else 0


@dataclass
class PrewarmLine:
    line_no: int
    us: int
    phase: str | None
    track: int | None
    slot: int | None
    full: int | None
    ready: int | None
    credits: int | None
    free_chunks: int | None
    heap_free_kb: int | None

    @property
    def is_sequence_marker(self) -> bool:
        return self.phase in ("before", "after") and self.track is None


@dataclass
class TickLine:
    line_no: int
    us: int
    live_kb: int
    high_kb: int
    chunks_free: int
    cls_kb: list[int]
    clsn: list[int]
    tags_kb: dict[int, int]
    snap_kb: int
    slab_live: int
    slab_used_kb: int
    slab_unused_kb: int
    slabs_at_most_one_node: int
    slabs_two_to_four_nodes: int
    heap_free_kb: int
    heap_min_kb: int
    refused_count: int

    @property
    def tag_total_kb(self) -> int:
        return sum(self.tags_kb.values())


@dataclass
class RefusalLine:
    line_no: int
    us: int
    bytes_refused: int
    count: int
    live_kb: int
    blocks: int
    tag: int
    tag_kb: int
    tag_vector_kb: dict[int, int]


@dataclass
class OwnersLine:
    """`#MEM_OWNERS` — the deliberate full per-owner census (tags 1..17)."""

    line_no: int
    us: int
    phase: str
    tag_total_kb: int
    tags_kb: dict[int, int]
    slab_live: int
    slab_used_kb: int
    slab_unused_kb: int
    slabs_at_most_one_node: int
    slabs_two_to_four_nodes: int
    snap_kb: int
    chunks_free: int
    live_kb: int
    blocks: int
    untracked: int
    unknown_frees: int
    heap_free_kb: int
    heap_min_kb: int


@dataclass
class Census:
    path: Path
    boot: list[tuple[int, str]] = field(default_factory=list)
    prewarm: list[PrewarmLine] = field(default_factory=list)
    ticks: list[TickLine] = field(default_factory=list)
    refusals: list[RefusalLine] = field(default_factory=list)
    owners: list[OwnersLine] = field(default_factory=list)
    mem_denied: list[tuple[int, str]] = field(default_factory=list)


def parse_log(path: Path) -> Census:
    census = Census(path=path)
    with path.open(encoding="utf-8", errors="replace") as handle:
        for line_no, raw in enumerate(handle, start=1):
            line = raw.rstrip("\n")
            if "#CAP,BOOT," in line:
                m = BOOT_RE.search(line)
                if m:
                    census.boot.append((line_no, m.group("body")))
                continue
            if "capture_prewarm" in line:
                m = PREWARM_RE.search(line)
                if m:
                    f = _fields(m.group("body"))
                    census.prewarm.append(
                        PrewarmLine(
                            line_no=line_no,
                            us=int(m.group("us")),
                            phase=f.get("phase"),
                            track=int(f["track"]) if "track" in f else None,
                            slot=int(f["slot"]) if "slot" in f else None,
                            full=int(f["full"]) if "full" in f else None,
                            ready=int(f["ready"]) if "ready" in f else None,
                            credits=int(f["credits"]) if "credits" in f else None,
                            free_chunks=int(f["free_chunks"]) if "free_chunks" in f else None,
                            heap_free_kb=_kb(f["heap_free"]) if "heap_free" in f else None,
                        )
                    )
                continue
            if "[Memory] ext," in line:
                m = EXT_RE.search(line)
                if m:
                    f = _fields(m.group("body"))
                    cls = _ints(f.get("cls", ""))
                    clsn = _ints(f.get("clsn", ""))
                    tag_values = _ints(f.get("tags", ""))
                    census.ticks.append(
                        TickLine(
                            line_no=line_no,
                            us=int(f.get("us", "0")) if f.get("us", "").isdigit() else 0,
                            live_kb=_kb(f.get("live", "0")),
                            high_kb=_kb(f.get("high", "0")),
                            chunks_free=int(f.get("chunks_free", "0") or 0),
                            cls_kb=cls,
                            clsn=clsn,
                            tags_kb={
                                tag: tag_values[i]
                                for i, tag in enumerate(TICK_TAGS)
                                if i < len(tag_values)
                            },
                            snap_kb=_kb(f.get("snap", "0")),
                            slab_live=int(f.get("slab_live", "0") or 0),
                            slab_used_kb=_kb(f.get("slab_used", "0")),
                            slab_unused_kb=_kb(f.get("slab_unused", "0")),
                            slabs_at_most_one_node=int(f.get("pin1", "0") or 0),
                            slabs_two_to_four_nodes=int(f.get("pin24", "0") or 0),
                            heap_free_kb=_kb(f.get("heap_free", "0")),
                            heap_min_kb=_kb(f.get("heap_min", "0")),
                            refused_count=int((f.get("refused", "0/0B").split("/") or ["0"])[0] or 0),
                        )
                    )
                continue
            if "#MEM_EXT_REFUSED," in line:
                m = REFUSED_RE.search(line)
                if m:
                    f = _fields(m.group("body"))
                    vector = _ints(f.get("tags", ""))
                    census.refusals.append(
                        RefusalLine(
                            line_no=line_no,
                            us=int(f.get("us", "0") or 0),
                            bytes_refused=int(f.get("bytes", "0") or 0),
                            count=int(f.get("count", "0") or 0),
                            live_kb=_kb(f.get("live", "0")),
                            blocks=int(f.get("blocks", "0") or 0),
                            tag=int(f.get("tag", "0") or 0),
                            tag_kb=_kb(f.get("tag_live", "0")),
                            tag_vector_kb={
                                i + 1: v for i, v in enumerate(vector) if i + 1 in TAG_NAMES
                            },
                        )
                    )
                continue
            if "#MEM_OWNERS," in line:
                m = OWNERS_RE.search(line)
                if m:
                    f = _fields(m.group("body"))
                    vector = _ints(f.get("tags", ""))
                    census.owners.append(
                        OwnersLine(
                            line_no=line_no,
                            us=int(f.get("us", "0") or 0),
                            phase=f.get("phase", "-"),
                            tag_total_kb=_kb(f.get("tag_total", "0")),
                            tags_kb={
                                i + 1: v for i, v in enumerate(vector) if i + 1 in TAG_NAMES
                            },
                            slab_live=int(f.get("slab_live", "0") or 0),
                            slab_used_kb=_kb(f.get("slab_used", "0")),
                            slab_unused_kb=_kb(f.get("slab_unused", "0")),
                            slabs_at_most_one_node=int(f.get("slabs_at_most_one", "0") or 0),
                            slabs_two_to_four_nodes=int(f.get("slabs_two_to_four", "0") or 0),
                            snap_kb=_kb(f.get("snap", "0")),
                            chunks_free=int(f.get("chunks_free", "0") or 0),
                            live_kb=_kb(f.get("live", "0")),
                            blocks=int(f.get("blocks", "0") or 0),
                            untracked=int(f.get("untracked", "0") or 0),
                            unknown_frees=int(f.get("unknown", "0") or 0),
                            heap_free_kb=_kb(f.get("heap_free", "0")),
                            heap_min_kb=_kb(f.get("heap_min", "0")),
                        )
                    )
                continue
            if "#MEM_DENIED," in line:
                m = MEM_DENIED_RE.search(line)
                if m:
                    census.mem_denied.append((line_no, m.group("body")))
    return census


def boot_census(census: Census) -> dict[str, object]:
    """Per-sequence free_chunks spend, and the per-committed-slot cost."""
    sequences: list[dict[str, object]] = []
    current: dict[str, object] | None = None
    for line in census.prewarm:
        if line.phase == "before":
            current = {"before_free_chunks": line.free_chunks, "rows": []}
            sequences.append(current)
            continue
        if line.phase == "after":
            if current is not None:
                current["after_free_chunks"] = line.free_chunks
            current = None
            continue
        if current is not None:
            rows = current["rows"]
            assert isinstance(rows, list)
            rows.append(
                {
                    "line": line.line_no,
                    "track": line.track,
                    "slot": line.slot,
                    "free_chunks": line.free_chunks,
                    "credits": line.credits,
                }
            )

    # delta = the drop attributed to this row (previous free_chunks - this one)
    for seq in sequences:
        rows = seq["rows"]
        assert isinstance(rows, list)
        previous = seq.get("before_free_chunks")
        for row in rows:
            if previous is not None and row["free_chunks"] is not None:
                row["delta"] = previous - row["free_chunks"]
                previous = row["free_chunks"]

    committed = [
        row
        for seq in sequences
        for row in seq["rows"]  # type: ignore[union-attr]
        if row.get("slot")
    ]
    # Per sequence, not blended: sequence 1 walks the *default enabled set* (every track
    # costs a flat amount with slot=0), while sequence 2 resolves the library (only
    # committed slots cost anything). Folding both into one "slot=0" bucket would report
    # the enabled-set cost as if it were a committed-slot cost.
    per_sequence = []
    for index, seq in enumerate(sequences, start=1):
        rows = seq["rows"]
        assert isinstance(rows, list)
        per_sequence.append(
            {
                "index": index,
                "before_free_chunks": seq.get("before_free_chunks"),
                "after_free_chunks": seq.get("after_free_chunks"),
                "total_spend": (
                    seq.get("before_free_chunks") - seq.get("after_free_chunks")
                    if seq.get("before_free_chunks") is not None
                    and seq.get("after_free_chunks") is not None
                    else None
                ),
                "zero_slot_deltas": sorted(
                    {row.get("delta") for row in rows if not row.get("slot")}
                ),
                "committed_slot_deltas": sorted(
                    {row.get("delta") for row in rows if row.get("slot")}
                ),
                "committed_slots": [
                    {"track": row["track"], "slot": row["slot"], "delta": row.get("delta")}
                    for row in rows
                    if row.get("slot")
                ],
            }
        )
    return {
        "sequences": sequences,
        "per_sequence": per_sequence,
        "committed_slots": committed,
        "committed_slot_count": len(committed),
        "delta_per_committed_slot": sorted({row.get("delta") for row in committed}),
    }


def tick_summary(census: Census) -> dict[str, object] | None:
    if not census.ticks:
        return None
    first, last = census.ticks[0], census.ticks[-1]
    return {
        "count": len(census.ticks),
        "first_line": first.line_no,
        "last_line": last.line_no,
        "first_us": first.us,
        "last_us": last.us,
        "live_kb": {
            "first": first.live_kb,
            "last": last.live_kb,
            "min": min(t.live_kb for t in census.ticks),
            "max": max(t.live_kb for t in census.ticks),
        },
        "chunks_free": {
            "first": first.chunks_free,
            "last": last.chunks_free,
            "min": min(t.chunks_free for t in census.ticks),
            "max": max(t.chunks_free for t in census.ticks),
        },
        "slab_live": {
            "max": max(t.slab_live for t in census.ticks),
            "last": last.slab_live,
        },
        "slab_unused_kb": {
            "max": max(t.slab_unused_kb for t in census.ticks),
            "last": last.slab_unused_kb,
        },
        "slabs_at_most_one_node": {  # the tick's misleading `pin1=`
            "max": max(t.slabs_at_most_one_node for t in census.ticks),
            "last": last.slabs_at_most_one_node,
        },
        "slabs_two_to_four_nodes": {  # the tick's misleading `pin24=`
            "max": max(t.slabs_two_to_four_nodes for t in census.ticks),
            "last": last.slabs_two_to_four_nodes,
        },
        "snap_kb": {"max": max(t.snap_kb for t in census.ticks), "last": last.snap_kb},
        "tag_kb_max": {
            TAG_NAMES[tag]: max(t.tags_kb.get(tag, 0) for t in census.ticks)
            for tag in TICK_TAGS
        },
        "tag_total_kb_max": max(t.tag_total_kb for t in census.ticks),
        "refused_max": max(t.refused_count for t in census.ticks),
    }


def refusal_summary(census: Census) -> list[dict[str, object]]:
    out: list[dict[str, object]] = []
    for r in census.refusals:
        out.append(
            {
                "line": r.line_no,
                "us": r.us,
                "bytes_refused": r.bytes_refused,
                "count": r.count,
                "live_kb": r.live_kb,
                "refusing_tag": TAG_NAMES.get(r.tag, str(r.tag)),
                "refusing_tag_kb": r.tag_kb,
                "full_tag_vector_kb": {
                    TAG_NAMES[t]: v for t, v in sorted(r.tag_vector_kb.items())
                },
            }
        )
    return out


def owners_summary(census: Census) -> dict[str, object] | None:
    """Per-owner maxima from `#MEM_OWNERS` — the only source for tags 8..16."""
    if not census.owners:
        return None
    max_by_tag = {
        TAG_NAMES[tag]: max(o.tags_kb.get(tag, 0) for o in census.owners)
        for tag in sorted(TAG_NAMES)
    }
    return {
        "count": len(census.owners),
        "first_line": census.owners[0].line_no,
        "last_line": census.owners[-1].line_no,
        "tag_kb_max": max_by_tag,
        "tag_kb_max_nonzero": {k: v for k, v in max_by_tag.items() if v},
        "tag_total_kb_max": max(o.tag_total_kb for o in census.owners),
        "live_kb_max": max(o.live_kb for o in census.owners),
        "chunks_free_min": min(o.chunks_free for o in census.owners),
        "slab_live_max": max(o.slab_live for o in census.owners),
        "slab_unused_kb_max": max(o.slab_unused_kb for o in census.owners),
        "slabs_at_most_one_max": max(o.slabs_at_most_one_node for o in census.owners),
        # Gates on trusting `live`: both must stay zero.
        "untracked_max": max(o.untracked for o in census.owners),
        "unknown_frees_max": max(o.unknown_frees for o in census.owners),
    }


def collect(paths: list[Path]) -> list[Path]:
    logs: list[Path] = []
    for path in paths:
        if path.is_dir():
            logs.extend(sorted(path.glob("session_*.log")))
        else:
            logs.append(path)
    return logs


def render(path: Path, census: Census) -> str:
    out = [f"=== {path} ==="]
    if census.boot:
        out.append(f"boot markers: {len(census.boot)}  first={census.boot[0][1]!r}")
    boot = boot_census(census)
    out.append(f"prewarm sequences: {len(boot['sequences'])}")
    for i, seq in enumerate(boot["per_sequence"], start=1):
        out.append(
            f"  seq {i}: free_chunks {seq['before_free_chunks']} -> "
            f"{seq['after_free_chunks']} (spend {seq['total_spend']})  "
            f"committed_deltas={seq['committed_slot_deltas']}  "
            f"zero_slot_deltas={seq['zero_slot_deltas']}"
        )
    out.append(
        f"  committed slots: {boot['committed_slot_count']}  "
        f"delta/slot={boot['delta_per_committed_slot']}"
    )
    summary = tick_summary(census)
    if summary is None:
        out.append("ticks: none")
    else:
        out.append(
            f"ticks: {summary['count']}  live_kb {summary['live_kb']}  "
            f"chunks_free {summary['chunks_free']}"
        )
        out.append(
            f"  slab_live max={summary['slab_live']['max']} "
            f"slab_unused_kb max={summary['slab_unused_kb']['max']} "
            f"slabs<=1node max={summary['slabs_at_most_one_node']['max']} "
            f"slabs2-4 max={summary['slabs_two_to_four_nodes']['max']}"
        )
        tags = " ".join(f"{k}={v}" for k, v in summary["tag_kb_max"].items())
        out.append(f"  tags(1..7) max KB: {tags}")
    refusals = refusal_summary(census)
    if refusals:
        out.append(f"pool refusals: {len(refusals)}")
        for r in refusals:
            out.append(
                f"  L{r['line']} count={r['count']} bytes={r['bytes_refused']} "
                f"refusing_tag={r['refusing_tag']}"
            )
    owners = owners_summary(census)
    if owners is None:
        out.append("owners (#MEM_OWNERS): none")
    else:
        out.append(
            f"owners (#MEM_OWNERS): {owners['count']} samples  "
            f"live_kb_max={owners['live_kb_max']}  "
            f"tag_total_kb_max={owners['tag_total_kb_max']}  "
            f"chunks_free_min={owners['chunks_free_min']}  "
            f"untracked_max={owners['untracked_max']}  "
            f"unknown_frees_max={owners['unknown_frees_max']}"
        )
        for name, kb in owners["tag_kb_max_nonzero"].items():
            out.append(f"  {name}: {kb} KB")
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("paths", nargs="+", type=Path, help="session log(s) or capture dir(s)")
    parser.add_argument("--json", type=Path, default=None, help="write the full census here")
    parser.add_argument(
        "--tick-table",
        action="store_true",
        help="print every tick line as a table row (us, live, chunks_free, slab_live, tags)",
    )
    args = parser.parse_args()

    logs = collect(args.paths)
    if not logs:
        print("no session logs found")
        return 1

    payload: dict[str, object] = {}
    for log in logs:
        census = parse_log(log)
        print(render(log, census))
        payload[str(log)] = {
            "boot": census.boot,
            "boot_census": boot_census(census),
            "tick_summary": tick_summary(census),
            "owners_summary": owners_summary(census),
            "refusals": refusal_summary(census),
            "mem_denied": census.mem_denied,
        }
        if args.tick_table:
            print("  us,live_kb,chunks_free,slab_live,slab_unused_kb,snap_kb,tags_1_7_kb")
            for t in census.ticks:
                tags = "/".join(str(t.tags_kb.get(tag, 0)) for tag in TICK_TAGS)
                print(
                    f"  {t.us},{t.live_kb},{t.chunks_free},{t.slab_live},"
                    f"{t.slab_unused_kb},{t.snap_kb},{tags}"
                )

    if args.json:
        args.json.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        print(f"\nwrote {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
