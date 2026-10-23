# F-8 — the residency row set is already populated, and an earlier reading of this file was wrong

**Date:** 2026-10-19 · **Branch:** `feature/unified-rolling-playback-path` · **Task:** 4.4 (F-8) · **Type:** read-only host
census over captures already on disk, plus two owner-authorized device runs the same day (see the companion
[`2026-10-19-s64-f8-residency-and-parity-sweep/EXECUTION.md`](../2026-10-19-s64-f8-residency-and-parity-sweep/EXECUTION.md)).

## Correction — the first version of this file was wrong, and how

The first version reported **71** `win_gate` rows and concluded that **no row anywhere reads `sv=1`**, so F-8's
statement "windows resident, root demoted" had "never been emitted". That was an artifact of the glob: it read only
`captures/*/serial.log`. The pipeline's own captures are mostly named `session_*.log`, `host_midi_hitl_long_loop_display_serial_*.log`
and `host_midi_hitl_windowed_post_stop_drain_*.log`, which that pattern misses.

Read correctly — `captures/*/*.log` — there are **19 215** `win_gate` rows, and `sv=1` together with `av=1` occurs at
**five** demoted lengths, thousands of rows, most of them **while the transport runs**. The claim is withdrawn.

## The census, correctly scoped

`sv=1` = the prewarmed view set's current view is **complete and published** (the "windows resident" half);
`av=1` = the length is **above the demotion bound** of `16 × TICKS_PER_BAR` = 12 288 ticks / 16 bars at 768 ticks per
bar (the "root demoted" half). One row carrying both is F-8's per-slot statement.

| `len` | bars | `av=1 && sv=1` rows | of which `pl=1` | first capture |
|---|---|---|---|---|
| 17 664 | 23 | 1 265 | 1 261 | `captures/s64-etarget-01/host_midi_hitl_long_loop_display_serial_20261005_174245.log` |
| 18 432 | 24 | 1 447 | 1 439 | `captures/s75b-snapshot-gate-fix-20261008-wrapcheck/session_20261007_182948.log` |
| 19 200 | 25 | 7 756 | 7 728 | `captures/s75b-snapshot-gate-fix-20261008-wrapcheck/session_20261007_182948.log` |
| 19 968 | 26 | 2 | 2 | `captures/i10-wq-07-windowed/session_20261006_211500.log` |
| 24 576 | 32 | 11 | 8 | `captures/i5-s64-f8-parity-32bar-01/serial.log` (today's run) |

Sample row (25 bars, playing):

```
win_gate,t=7,s=4,x=win_track_not_playing,len=19200,ep=1,av=1,al=0,pl=1,ed=0,od=1,pv=0,rw=1,sv=1,rt=0,rl=1,ow=0,dm=0,...
```

**So F-8's residency half is not merely reached — it is long-standing**: five demoted lengths across captures from
2026-10-05, 10-06, 10-07 and 10-08, mostly under transport. It was never a device-state gap.

## What the census does still establish

* **`win_serving` and `win_ready` are absent globally: 0 rows in all 19 215.** A *resident* published window
  (`sv=1`) is common; the demand lane's own **serving** exit has never been recorded in this project. That is a real
  observation about the lane (and belongs to 6.4's receipts discussion), not an absence in F-8's row set.
* **`win_busy` occurs exactly twice, and both are today's run** (`i5-s64-f8-demoted-aligned-02`). `win_blocked` is
  *not* new — it appears from 2026-10-06 (`captures/s64-37c-01/`) onward. An earlier draft of the companion file
  claimed both were firsts; only `win_busy` is.
* Field semantics in this note are the emitter's own (`include/Utils/DebugSessionCapture.h` `winGateRow`), not
  inferred from neighbouring fields.

## What F-8 actually still owes

The residency half is **populated across five lengths** and needs no further device work to be *stated*. What is
genuinely thin is the **parity half**, which today stands at two demoted lengths — 19 200 ticks (25 bars) from 4.3's
F-7 `PASS`, and 24 576 ticks (32 bars) from today's
[companion reading](../2026-10-19-s64-f8-residency-and-parity-sweep/EXECUTION.md) — and, as 4.4's task note already
says, 3.6a's retained-source figure and 6.3's wider sweeps stay with their owners. `oracle_complete` stays `False`.

## Reproduce

```
.venv/bin/python - <<'PY'
import glob, collections
n=0; lens=collections.Counter()
for f in glob.glob("captures/*/*.log"):          # NOT captures/*/serial.log
    for l in open(f, errors="replace"):
        if "win_gate" not in l: continue
        n+=1
        d=dict(p.split("=",1) for p in l.split(",") if "=" in p)
        if d.get("av")=="1" and d.get("sv")=="1": lens[int(d["len"])]+=1
print(n, dict(lens))
PY
```

## Gates

No code changed, no firmware line, no upload. Today's two device runs are the owner-authorized writes recorded in the
companion file; this census itself reads only `captures/`. `openspec validate windowed-playback-generation --strict`
re-run with the task note.
