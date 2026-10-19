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

## Second reading — device, read-only, 2026-10-19 (no upload, no write, no record, no clear)

The host census above says the row *has not* been emitted. Two read-only device reads say **why**, on the library as
it stands now.

**(a) The candidate slot is gone.** The only demoted **span-aligned root-current** state on disk was
`t=0,s=0,len=15360` (20 bars), in the 2026-10-08/09 E2 captures. Today it is empty:

```
.venv/bin/python .agents/skills/hitl-content-verification/scripts/dump_and_capture.py \
    --out-dir captures/i5-s64-f8-residency-t1s1-01 --select-track 1 --dump-slot 1 --dump-only
CAPTURE_CONTENT,begin,track=1,slot=0,length=0
CAPTURE_CONTENT,rows,record,0,0
CAPTURE_CONTENT,rows,effective,0,0
CAPTURE_CONTENT,end
```

**(b) The library holds exactly one current root, and it is on the slot the lane excludes.** A read-only library
observation clears the question:

```
.venv/bin/python scripts/hitl_boot_census.py --out-dir captures/i5-s64-f8-library-census-01 \
    --no-reset --library-observe --settle-s 25 --observe-window-s 30
LIBRARY OBSERVATION slots=64 complete=True payload_inventory=64 initial_playback=8 \
    restore_completions=0 committed_at_checkpoint=22 (pass_backed=22 root_backed=1)
```

Per slot: 22 occupied, **all pass-backed**, and **`root_backed=1`** — `t=7,s=3` (track 8 / slot 4), i.e. the 25-bar
target whose length is **not** span-aligned (`19 200 % 1 536 = 768`), so the demand lane refuses it on `al` while the
serving gather builds its windows. **Every other occupied slot has no root at all** (`has_root_content=false`), so
even the span-aligned demoted ones (64 bars `t=1,s=3`, 66 bars `t=6,s=0`) cannot be served: there is nothing current
to serve from. `initial_playback=8` is the plausible reason `rl=0` (no free coverage role) wherever a row did occur —
eight slots sound at boot.

**Together these settle the route.** A residency row with `av=1` **and** `sv=1` needs a slot that is demoted, **span-aligned**,
**root-current** and **role-free** at once. The library can supply at most the first two on any single slot today, and
the read-only gestures that remain (`SELECT_TRACK`, `!CAPTURE_CONTENT_SLOT`, `!LIB_OBSERVE`, playback) cannot publish a
root. **So F-8's first row cannot be produced read-only: it needs one write that publishes a root on a demoted
span-aligned slot** (a record/overdub on such a slot, or a switch that lands on one). That write has not been made.

*Caveat, recorded rather than smoothed:* `win_gate` is the **demand lane's** row, and a slot the demand lane serves
exits as `win_serving`; a slot served only by the **serving gather** reports through `playback_build`/`win_gap` and
never as `sv=1`. So part of what F-8 wants may be unobservable for a restored library whose slots carry no root — which
is itself a finding for 4.4's "residency statement per slot", and the reason the route above names the state the
residency claim actually needs.

## Gates

No code changed. `openspec validate windowed-playback-generation --strict` re-run with the task note; no firmware
build, no upload (the census ran `--no-reset`; the board restarted only because the serial port was opened), no SD
write, no clear, no record. 4.4 stays open; F-7's verdict (4.3) is unaffected.
