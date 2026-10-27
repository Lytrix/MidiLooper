# P-D — bounded visual continuation

## Result
C5.2a's window-scoped alternative, strengthened to a whole-loop, pass-local, **bounded**
continuation, and enabled on the derived-view lane. Every step is capped at 8 rows; the whole-pass
`resolveDisplayNotes` fallback is never selected above the 512-event cap. `P-D-1` flipped only after the
bound was proved.

## What landed
- `LoopContentResolution`: `BoundedDisplaySession` continuation (order → begin → blayer 8 → bapply 1 →
  bspans 8 → bproj 8 → bemit 8 → ready), no sort, no whole-vector copy, no checked-after-call µs budget;
  workspace released at ready; any key change (revision/length/loopId/pass count/committed rows)
  discards the candidate.
- `LoopVisualCache`: attempt order root → prepared → bounded continuation; below the cap the existing
  synchronous resolve still runs (bounded by that cap); the frame is retained while the continuation works.
- Producer grain on the derived-view lane and the STOPPED branch (not `SESSION_CAPTURE`-gated, not
  transport-idle gated). `DeviceGateSession`'s eleven indivisible steps keep their own lane and are no
  longer the derived-view continuation.
- Design finding: promoting the prepared index would have failed P-D-2 (it swaps endpoints on the
  fixture: id1000 Length→40 vs overdub [24,44)), which is why the pass-local bounded path was chosen.

## Verification (parent-verified)
- Visual probe controls **4/4**, `IMMEDIATE_VISUAL_CASE=bound` **4/4** (was exit 1): `CONTINUATION
  turns=801 max_units=8 first_publish_turn=801 rows=1584`.
- Handoff **18/18**; `source` 10/10, `stop` 4/4, `empty` 3/3.
- Full native **3,469 total / 7 skipped / 3,462 passed / 0 failed**.
- Build **SUCCESS**: variables 96,192 / code **389,672** + padding **3,544** / locals 34,880; floor 840
  unchanged (`scripts/ram1_budget.json` diff empty) with 2,704 B headroom.
- RAM1 recovery without a re-base: two demonstrably cold LSV1 **load-path** helpers
  (`buildLoopSnapshotFromValidatedPayload`, `readLoopSnapshotPayload`) moved to flash via the TU's
  existing `LSV1_IO_MEM`, following `.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc:111-116`. Both are reached
  only from already-flash load/recovery entries (ExecutionBounds row 15, `lsv1_snapshot_payload`; boot
  bundle recovery) — no input, playback-emission, capture-append or per-turn stop grain. Placement only, so
  no bound row is reclassified.

## Not closed
- Device gates NOT RUN: PSRAM head-room, stack high-water, allocator, input-service gap/output lateness.
- P-A's manager-route `start` profile remains the one intentional RED (publication-gated admission).
- No upload/reset/device/SD/content/commit; installed `74392e15` unchanged.
