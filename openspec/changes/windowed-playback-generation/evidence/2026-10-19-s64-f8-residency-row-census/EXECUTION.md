# F-8 — the residency row set is readable, but its first row has never been emitted

**Date:** 2026-10-19 · **Branch:** `feature/unified-rolling-playback-path` · **Task:** 4.4 (F-8) · **Type:** read-only
host census over captures already on disk. **No device run, no upload, no firmware line, no SD write.**

## Question

4.4's device note identified the rows that carry "windows resident, root demoted" per slot
(`DIAG,win_gate,…` with `len`, `av`, `sv`, `rt`, `rl`, `ow`) and left the **sweep** open. Before spending a device run,
census what those rows have actually said.

## Method

Every `win_gate` row in every `captures/*/serial.log` on disk (71 rows), grouped by
`(len, av, al, pl, sv, rt, rl, x)`. Field semantics are the emitter's own (`include/Utils/DebugSessionCapture.h`,
`winGateRow`), not inferred: bit 1 `av` = `shouldAvoidFullVisualRebuild` (the demotion bound), bit 2 `al` = span-aligned,
bit 3 `pl` = playing, bit 8 `sv` = current view complete **and** published, bit 9 `rt` = `publishedSnapshotIsCurrent`,
bit 10 `rl` = `coverageRoleAvailable`.

## Result

**Exit census.** `win_track_not_playing` 48 · `win_window_ineligible` 18 · `win_not_servable` 5. No
`win_serving`, `win_ready` or `win_waiting` row exists in any capture. **No row anywhere reads `sv=1`.**

**The bound.** `16 × TICKS_PER_BAR` with `TICKS_PER_BAR = 768` → **12 288 ticks (16 bars)**; `av=1` means *above* it
(F-7's target: `19 200 > 12 288`, 25 bars).

**Demoted lengths resident, and what each says while playing:**

| `len` | bars | `al` | exit when `pl=1` | `sv` | `rt` | `rl` |
|---|---|---|---|---|---|---|
| 15 360 | 20 | 1 | `win_not_servable` | 0 | 1 | 0 |
| 19 200 | 25 | 0 | `win_window_ineligible` | 0 | 1 | 1 |
| 49 152 | 64 | 1 | `win_not_servable` | 0 | 0 | 0 |
| 50 688 | 66 | 1 | `win_not_servable` | 0 | 0 | 0 |

## Reading

**The demoted lengths that qualify for the demand lane are not served, and the demoted length that *is* served is
excluded from it.** 64 and 66 bars are span-aligned but have no current root (`rt=0`, `rl=0`); 20 bars is aligned and
root-current but has no free role (`rl=0`); 25 bars is the only one playing, and the lane refuses it on **alignment**
(`19 200 % 1 536 = 768`). The stream F-7 measured on that slot is the *serving gather* (2-bar span, 1-bar step), which
is what 4.3's PASS covers — not a rolling-lane root return.

So F-8's residency statement has **never been emitted on this library**, and the sweep is not "unswept but started" —
its first row is unobserved. That is a device-state fact, not a parser gap: `windowed_lane.gate_rows` already returns
every field.

## What the next device step must produce

One **demoted, span-aligned** slot served with a current root, so a row arrives with `av=1` **and** `sv=1` at one
`len` — e.g. the 64-bar (49 152-tick) slot reaching a current root while playing. The sweep then varies `len` across
the bound. Recording or switching to create that state is a device write and is **not** authorized by this census.

## Gates

No code changed. `openspec validate windowed-playback-generation --strict` re-run with the task note; no firmware build,
no upload, no device action. 4.4 stays open; F-7's verdict (4.3) is unaffected.
