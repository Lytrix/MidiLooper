# F-8 — a second demoted length for parity, and the alignment/role/root qualifiers falsified

**Date:** 2026-10-19 · **Base:** `4eb3342b` · **Branch:** `feature/unified-rolling-playback-path` · **Task:** 4.4 (F-8)
· **Type:** device, owner-authorized writes (scoped clear + record of the registered target only). **No firmware line
changed, no upload issued** (`--no-boot-reset`; no compile in any run's log), no undo, no rename.

Companion: [`2026-10-19-s64-f8-residency-row-census/EXECUTION.md`](../2026-10-19-s64-f8-residency-row-census/EXECUTION.md),
which carries the corrected census and the withdrawal of an earlier "the row was never emitted" claim.

## What today's device work adds

| Item | Status before today | Today |
|---|---|---|
| residency — **windows resident, root demoted** (`av=1` **and** `sv=1`) | already observable on disk at **23, 24, 25, 26 bars** (1 265 + 1 447 + 7 756 + 2 rows; mostly `pl=1`) | a fifth length, **32 bars** (24 576 ticks) — and here the statement holds **under transport** (`pl=1`) |
| content parity of the demoted state | one length: 25 bars (4.3's F-7 `PASS`) | a second length: **32 bars** — `content-review.json` **`PASS`**, 3 cycles × 1 536 events, `inventory_equal` **and** `exact_order` true on all three |
| `win_busy` | never recorded in this project | recorded twice, both in the 32-bar run |
| the **recipe** that produces a demoted take | `--record-bars` was tried at 23/24 | the axis is now pinned: the device records exactly what is asked — `23 → 17 664`, `32 → 24 576`, i.e. 768 ticks per bar |

## The rows

**23 bars — `captures/i5-s64-f8-demoted-aligned-01/`** (`--record-bars 23`; device reported
`Recording stopped @ tick 285 (recStart=0 length=17664)`):

```
win_gate,t=7,s=3,x=win_track_not_playing,len=17664,ep=1,av=1,al=0,pl=0,ed=0,od=0,pv=0,rw=1,sv=1,rt=1,rl=0,ow=0,dm=1,...
```

**758** `playback_build,build_complete` rows name track 7 / slot 3 in that run, each with `windowLengthTicks` **1 536**.

**32 bars — `captures/i5-s64-f8-demoted-aligned-02/`** (`--record-bars 32`; device reported
`Recording stopped @ tick 314 (recStart=0 length=24576)`), where the statement also holds **while the transport runs**
(`pl=1`) across five distinct exits:

```
win_gate,t=7,s=3,x=win_track_not_playing,len=24576,ep=1,av=1,al=1,pl=1,...,rw=1,sv=1,rt=0,rl=0,ow=0,dm=0,...
win_gate,t=7,s=3,x=win_blocked,           len=24576,ep=1,av=1,al=1,pl=1,...,rw=1,sv=1,rt=1,rl=0,ow=0,dm=1,...
win_gate,t=7,s=3,x=win_track_not_playing, len=24576,ep=1,av=1,al=1,pl=1,ed=1,...,rw=1,sv=1,rt=1,rl=0,ow=0,dm=1,...
win_gate,t=7,s=3,x=win_busy,              len=24576,ep=1,av=1,al=1,pl=1,...,rw=1,sv=1,rt=1,rl=0,ow=0,dm=1,...
win_gate,t=7,s=3,x=win_track_not_playing, len=24576,ep=1,av=1,al=1,pl=0,...,rw=1,sv=1,rt=1,rl=0,ow=0,dm=1,...
```

**1 028** `build_complete` rows name the target there, same 1 536-tick span. `win_blocked` is not new (it appears from
2026-10-06 in `captures/s64-37c-01/`); `win_busy` is (2 rows in the whole project, both here).

## Parity at the second demoted length

`captures/i5-s64-f8-parity-32bar-01/`, the content pipeline's own two stages on the **32-bar** take:

```
CAPTURE_CONTENT,begin,track=8,slot=3,length=24576      (record == effective == 1536 rows)
result: PASS   cycle_cut: event_count   storage_matches_effective: true
cycle0/1/2: rows=1536 missing=0 extra=0 inventory_equal=True exact_order=True
```

31 distinct `windowStartTick` values and the same 1 536-tick span appear in this capture, so the parity is again
measured across advancing window boundaries.

## The qualifiers this falsifies

An earlier note in this session — including the superseded first version of the companion census — asserted that F-8's
row needs a slot that is demoted **and span-aligned and root-current and role-free**. **The rows refuse all three extra
qualifiers:**

* **`al` is irrelevant to `sv`.** The 23-bar row carries `sv=1` with `al=0` (`17 664 % 1 536 = 768`) as well as with
  `al=1` at 32 bars; the census's 7 756 rows at 25 bars are `al=0` throughout. `al` gates the *rolling demand* entry
  (`win_window_ineligible`), not the maintenance path that publishes the window.
* **`rl` is irrelevant.** Every `sv=1` row here reads `rl=0`, and the census's rows at 25 bars read `rl=1`.
* **`rt` is irrelevant.** One `sv=1` sample here carries `rt=0`, so a current *root* is not a precondition of a resident
  window.

What both lengths do share is **`rw=1`** — a bound record-return window for a pending job — i.e. these runs are
**freshly recorded** long loops in the DEC-136 record-return shape. That is a property of *these two runs*, not a
requirement of the statement: the census's older rows at 23 and 24 bars also carry `rw=1`, which is consistent with the
same shape.

## Cost, and what remains

**Writes:** the registered target (track 8 / slot 4) was scoped-cleared and re-recorded twice — 23 bars, then 32 bars —
under the owner's total authorization granted this session. The 25-bar take that F-7's evidence rests on is replaced on
the device; F-7's captured log is unchanged on disk.

**Remaining in F-8:** the parity half now stands at **two** demoted lengths (25 and 32 bars) against a residency half
that spans five (23/24/25/26/32); the axis is `record-bars`, so further lengths are a repeatable recipe, not a search.
The retained-source figure (3.6a) and 6.3's wider sweeps stay with their owners. `oracle_complete` stays `False`; 4.4
and 6.4 stay open on their other clauses.

## Run identity

| Run | Env | Pinned hex | git | CLI |
|---|---|---|---|---|
| `i5-s64-f8-demoted-aligned-01` | `teensy41-capture-serial-window-qualification` | `0b4b6fe2…` | `4eb3342b` (dirty: docs/evidence only) | track 8 / slot 4 / ch 8, `--record-bars 23`, `--no-boot-reset` |
| `i5-s64-f8-demoted-aligned-02` | same | same | same | … `--record-bars 32` |
| `i5-s64-f8-parity-32bar-01` | — (content pipeline) | — | same | `--select-track 8 --dump-slot 4 --capture-seconds 200` |

Both preset runs end with the leg's own exit code 2 (`unexpected_loop_len`, display-row checks) and lane terms read
`under_transport=0`; that is the **leg's** verdict on its recipe's expectations, not a row-level absence, and the
residency and parity rows above are read from the captures directly. The board was not flashed; no build was run.
