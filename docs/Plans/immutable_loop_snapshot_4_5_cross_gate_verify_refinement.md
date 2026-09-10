# Immutable loop snapshot — OpenSpec 4.5 cross-gate verify

**Status:** **SHIPPED** (2026-09-10) — OpenSpec task `4.5` checked off.  
**OpenSpec:** `immutable-loop-snapshot-passes` task `4.5`  
**Parent:** 4.4 closed
([`immutable_loop_snapshot_4_4_closeout_refinement.md`](immutable_loop_snapshot_4_4_closeout_refinement.md)).  
**Next:** [`immutable_loop_snapshot_4_6_snapshot_identity_freshness_refinement.md`](immutable_loop_snapshot_4_6_snapshot_identity_freshness_refinement.md).

## Gate log (final)

| Gate | Result |
|---|---|
| HITL `base` | PASS — [`122812`](../../captures/session_20260910_122812.log) / [`122857`](../../captures/host_midi_automation_baseline_20260910_122857.json) |
| HITL `overdub_occupy_paint` | PASS — [`123445`](../../captures/session_20260910_123445.log) / [`123557`](../../captures/host_midi_hitl_occupy_paint_20260910_123557.json) |
| HITL `note_edit_overdub_compose` (`02`) | PASS **14/14** — [`142119`](../../captures/note_edit_overdub_compose_20260910_142119_serial.log) (`resolve` 8123 µs, `reconstruct` 2837 µs) after RC1+RC-A ([`note_edit_apply_passmaterialize_deferral_bugfix.md`](note_edit_apply_passmaterialize_deferral_bugfix.md)) |
| RAM1 `teensy41-capture-serial` | PASS — locals **7008** |
| `pio test -e native` | PASS — 1820 succeeded, 1 skipped (RC-A verify) |

Prior compose fail [`124645`](../../captures/note_edit_overdub_compose_20260910_124645_serial.log): resolve 61442 µs (PassMaterialize on wrap-stale apply). Closed by RC-A.

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — verify only (+ RC-A selector policy already shipped) |
| State-transition change? | NO |
| Unbounded MIDI-interval owner? | NO |

## Out of scope (remains)

- 4.6 snapshot-identity freshness  
- Snapshot-identity undo (§5)  
- Compatibility retirement (§6)  
