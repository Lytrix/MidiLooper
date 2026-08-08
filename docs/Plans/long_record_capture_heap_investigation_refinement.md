---
name: long record capture heap investigation
openspec_change: runtime-derived-representation-heap
overview: "SUPERSEDED by M6 — see multi_track_playback_pressure_closure_refinement.md. Parser fix done; DIAG firmware cancelled."
todos:
  - id: hitl-misparse-fix
    content: Whitelist PERS heap stages; parse PERS,diag chunk pool separately
    status: completed
  - id: superseded-by-m6
    content: "All firmware work moved to OpenSpec M6 — multi_track_playback_pressure_closure_refinement.md"
    status: pending
  - id: instrument-extmem-fallback
    content: Count ExternalMemoryFirstAllocator malloc fallbacks per callsite during RECORDING
    status: cancelled
  - id: diag-heap-firmware
    content: "#CAP,DIAG,heap telemetry — cancelled (RAM1 ~−50 KB on capture-serial)"
    status: cancelled
isProject: false
---

# Long record capture — internal heap investigation

> **Authoritative plan (review before build):** [`multi_track_playback_pressure_closure_refinement.md`](multi_track_playback_pressure_closure_refinement.md)  
> **OpenSpec tasks:** [`openspec/changes/runtime-derived-representation-heap/tasks.md`](../../openspec/changes/runtime-derived-representation-heap/) § **M6**

This doc retains **trigger-run evidence** only. Implementation checklist lives in M6.

---

## Trigger run (2026-07-14) — HITL record stall

| Field | Value |
|-------|-------|
| Serial | [`captures/host_midi_automation_serial_20260714_162017.log`](../../captures/host_midi_automation_serial_20260714_162017.log) |
| Config | track 7, 64+64 bars, 20s heartbeat |
| Abort | Silence ~bar 9 RECORDING; no `RECS` |

**Corrected heap:** 28672 bytes (`PERS,result`), not 16 (misparse fixed).

---

## Manual sessions (2026-07-14) — multi-track overdub

| Log | Result |
|-----|--------|
| [`174731`](../../captures/session_20260714_174731.log) | **PASS** — 64-bar + overdub; 0 `RING,overflow` |
| [`175327`](../../captures/session_20260714_175327.log) | **FAIL** — heavy MO + ring overflow spiral → reboot |

See M6 plan for closure criteria.

---

## Completed

- [`scripts/hitl/persistence_rows.py`](../../scripts/hitl/persistence_rows.py) + [`scripts/test_persistence_row_parse.py`](../../scripts/test_persistence_row_parse.py)

## Cancelled

- `#CAP,DIAG,heap` firmware (RAM1)
- Extmem fallback counter firmware (use existing stage lines)
- “Behavior patch after DIAG only” — proceed with M6 fixes using existing telemetry

---

## Historical suspects (unchanged — addressed in M6)

1. `ensurePlaybackWindowBuilt` → `loop.midiEvents()` full materialize (M6 Phase 1)
2. Display RECORDING/OVERDUBBING per-frame rebuild (M6 Phase 2)
3. Idle materialize on non-selected tracks during capture (M6 Phase 3)

**Related:** [`64bar_regression_commit_analysis_enhancement.md`](64bar_regression_commit_analysis_enhancement.md), [`next_session_handoff_overdub_uip_architecture.md`](next_session_handoff_overdub_uip_architecture.md)
