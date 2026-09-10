# HITL framework hardening

**Status:** Stages 0–4 in tree.  
**Kind:** refinement  
**Does not start:** firmware changes, new stimuli

Host-only. Plan: make HITL actions state-aware, serial evidence fresh, and
verdicts honest before using this harness as an autonomous-agent benchmark.

## Stage 0 — contract freeze (shipped this slice)

Machine-readable host contract in [`scripts/hitl/contract.py`](../../scripts/hitl/contract.py).

`HITL_RESULT_CONTRACT`:

| Verdict | When |
|---|---|
| `PASS` | required evidence present, required transitions confirmed, firmware assertions pass |
| `FAIL` | capture complete, stimulus valid, setup transitions confirmed, firmware assertion contradicted |
| `INCONCLUSIVE_CAPTURE` | required evidence unavailable or ambiguous; no firmware conclusion |

`ACTION_RESULT`: `CONFIRMED` | `ALREADY_SATISFIED` | `RECOVERY_FAILED` | `EVIDENCE_UNAVAILABLE`

`INCONCLUSIVE_CAPTURE` must not be rewritten to `PASS` or `FAIL`. Process
exit may still be non-zero (`3`) so required gates fail closed.

## Stage 1 — recovery kernel (shipped this slice)

Rule: **never retry a state-changing control action unless evidence after
the baseline proves the requested transition did not happen.**

Owner: [`scripts/hitl/recovery.py`](../../scripts/hitl/recovery.py) `request_state_transition`.

Wired into clear, record arm, record stop, overdub start/stop, and
`ensure_loop_edit_before_record()`. Unconfirmed NOTE_EDIT exit aborts.

## Stage 2 — capture integrity (shipped this slice)

Collectors keep three liveness clocks:

| Signal | What counts |
|---|---|
| Serial reader | any received line, including `DFRAME` |
| Firmware | heartbeat-resetting lines (`DFRAME` / `PERS,work` excluded) |
| Transport | `#CAP` `BAR` / `DFRAME` / `BPM` / `ST` / `MI` / `MO` / `RECA` / `RECS` after the action baseline |

Historical pre-baseline `BAR`/`DFRAME` must not authorize a new transport-proxy phase.
Follow mode marks capture `INCOMPLETE` on file truncation, session-pointer rotation, or managed `capture_session.py` child exit. Those abort immediately; they are not firmware `FAIL`.

## Stage 3 — phase sessions (shipped this slice)

`extract_phase_sessions` returns matched record/overdub windows plus
`PhaseAmbiguity` (`MISSING_STOP`, `STALE_STOP`, …). Pairing is scoped to
`after_index`, a `#HITL,` header, or the follow collector's bootstrap
end. First-note timing ignores Note On velocity 0 and does not fall back
to whole-log BPM.

## Stage 4 — honest verdicts (shipped this slice)

Live and verify-only baseline reports emit `hitl_report` with `PASS` /
`FAIL` / `INCONCLUSIVE_CAPTURE`. Sparse capture plus host MIDI counts
cannot pass. Process exit is `0` / `2` / `3`. New host serial snapshots
from Mode A get a `#HITL,STIMULUS` header; verify-only of headerless
logs stays compatible unless `--require-stimulus-header`.

## Remaining

Dedicated live smoke complete for `base` after harness fixes
([`session_20260910_122812.log`](../../captures/session_20260910_122812.log)
`PASS`). Fixes landed:

- Do not toggle transport before arm when already STOPPED.
- Arm with Record button 36 (ARMED → transport → RECORDING).
- Evaluate `disproved` only after the action wait timeout.
- Minimal settle after transport start for first-note latency.

Operational: OpenSpec 4.5 continue with `overdub_occupy_paint` then compose.
