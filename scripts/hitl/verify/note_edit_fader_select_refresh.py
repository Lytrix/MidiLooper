"""Verify NOTE_EDIT fader1 select → fader2 dependent refresh from capture serial."""

from __future__ import annotations

import re
from dataclasses import dataclass


@dataclass
class FaderSelectRefreshResult:
    f1_clusters: int
    clusters_without_f2_within_3s: int
    max_f2_gap_s: float
    quiet_refresh_count: int
    send_f2_count: int
    ok: bool
    details: list[str]


def _wall_times_and_lines(lines: list[str]) -> list[tuple[float, str]]:
    out: list[tuple[float, str]] = []
    last_t = 0.0
    for line in lines:
        m = re.match(r"\[(\d+\.\d+)\]", line)
        if m:
            last_t = float(m.group(1))
        out.append((last_t, line))
    return out


def verify_note_edit_fader_select_refresh(lines: list[str]) -> dict[str, object]:
    """Gate: after each fader1 inbound cluster, F2 outbound within 3 s; no gap > 30 s."""
    stamped = _wall_times_and_lines(lines)
    f1_times: list[float] = []
    f2_times: list[float] = []
    quiet_refresh = 0
    send_f2_log = 0

    for t, line in stamped:
        if "#DBG select_slot" in line and "ignored=0" in line:
            pass
        if "QUIET_REFRESH" in line or "#DBG outbound_step=QUIET_REFRESH" in line:
            quiet_refresh += 1
        if "#DBG outbound_step=SEND_F2" in line:
            send_f2_log += 1
        if "#CAP" in line and ",MI,H,224,16," in line and t > 0:
            f1_times.append(t)
        if "#CAP" in line and ",MO,224,14," in line and t > 0:
            f2_times.append(t)
        if "#DBG outbound_step=SEND_F2" in line and t > 0:
            f2_times.append(t)

    f2_times = sorted(set(f2_times))

    clusters: list[list[float]] = []
    if f1_times:
        cluster = [f1_times[0]]
        for t in f1_times[1:]:
            if t - cluster[-1] < 0.35:
                cluster.append(t)
            else:
                clusters.append(cluster)
                cluster = [t]
        clusters.append(cluster)

    updates = sorted(f2_times)
    missing = 0
    max_gap = 0.0
    details: list[str] = []
    for c in clusters:
        end = c[-1]
        nxt = next((u for u in updates if u >= end), None)
        if nxt is None:
            missing += 1
            details.append(f"cluster end {end:.1f}s: no F2/update")
        elif nxt - end > 3.0:
            missing += 1
            details.append(f"cluster end {end:.1f}s: F2 after {nxt - end:.1f}s")

    for i in range(1, len(f2_times)):
        gap = f2_times[i] - f2_times[i - 1]
        max_gap = max(max_gap, gap)

    ok = missing == 0 and max_gap <= 30.0
    result = FaderSelectRefreshResult(
        f1_clusters=len(clusters),
        clusters_without_f2_within_3s=missing,
        max_f2_gap_s=max_gap,
        quiet_refresh_count=quiet_refresh,
        send_f2_count=max(send_f2_log, len(f2_times)),
        ok=ok,
        details=details[:10],
    )
    return {
        "note_edit_fader_select_refresh_ok": ok,
        "f1_clusters": result.f1_clusters,
        "clusters_missing_f2_within_3s": result.clusters_without_f2_within_3s,
        "max_f2_gap_s": result.max_f2_gap_s,
        "quiet_refresh_count": result.quiet_refresh_count,
        "send_f2_count": result.send_f2_count,
        "details": result.details,
    }
