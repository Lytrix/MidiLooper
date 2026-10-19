# F-7 — the demoted target's served stream equals `effective`, cycle by cycle, across advancing window boundaries

**Date:** 2026-10-19 · **Base:** `e625603d` · **Branch:** `feature/unified-rolling-playback-path` · **Task:** 4.3 (F-7) under 6.4
· **Type:** host tooling + one device read and one device capture. **No firmware line changed**, no upload, no clear, no
record. **Verdict: F-7 PASS** on the demoted target.

## Verdict

`verify_playback_content.py` on `captures/i5-s64-f7-playback-01`:

```
result: PASS        length: 19200        cycle_cut: event_count
anchor: null        cycle_phase: 0        storage_matches_effective: true
cycle0: rows=1152 missing=0 (residual=0 unexplained=0) extra=0 inventory_equal=True exact_order=True
cycle1: rows=1152 missing=0 (residual=0 unexplained=0) extra=0 inventory_equal=True exact_order=True
cycle2: rows=1152 missing=0 (residual=0 unexplained=0) extra=0 inventory_equal=True exact_order=True
```

Three complete loop cycles, each **1 152 events** — the whole loop — with nothing missing, nothing extra, and the
**order identical** to `effective`. `exact_order` is reported separately from the inventory verdict and was true for
every cycle; on a 1 152-event cycle that is a far stronger statement than the PASS requires.

## The boundaries were crossed, and they advanced

The claim is parity *across a window boundary*, so the capture must show the window moving. It does: 80
`playback_build` rows for this target (`track 7 / slot 3` in the row's 0-based identity = track 8 / slot 4), all with
reason `snapshot`, **`windowLengthTicks` 1 536 (2 bars)** and `windowStartTick` stepping **768 (1 bar)** — the fields
are the emitter's own (`DebugSessionCapture::playbackRebuild`: `…,<path>,<durationUs>,<windowStartTick>,<windowLengthTicks>,…`).
Across the capture the start took **24 distinct values** (`0 … 17 664`) with **79 advances**.

Because the step is one bar, a 19 200-tick loop has **25 window positions per cycle** (the 1 536-tick span **overlaps**
its neighbour by one bar), so each compared cycle crossed **≈25 boundaries** and the three cycles ≈75 — the capture's
79 advances over its ~3.5 cycles agree. The parity is therefore measured across the transitions, not around them. That
successive windows overlap is also why `extra = 0` matters: the consumer served the shared bar **once** per cycle, so
the capture is direct evidence against double-serving at the transition (4.3 still owes that rule its own statement).

Two `win_gate` rows for the same target state the premises from the device's own flags
(`win_gate,t=7,s=3,len=19200,ep=1,av=1,al=0,pl=1,rt=1,rl=1,dm=1`): the loop is **demoted** (`av=1` — bit 1 is
`shouldAvoidFullVisualRebuild`, i.e. the length is **above the 16-bar bound**, `16 × 768 = 12 288` ticks, and
`19 200 > 12 288`), the wait is `pl=1` at the sampling instant, and the length is **not span-aligned** (`al=0`:
`19 200 % 1 536 = 768`), so the *demand* lane excludes the slot (`x=win_window_ineligible`) while the **playback
gather** is what builds the 1 536-tick windows above. The parity verdict therefore belongs to the serving gather, and
this capture does not claim a rolling-lane root-return.

## What had to change to measure it (host tooling only)

### 1. Reach the target without a record-toggle gesture

`!CAPTURE_CONTENT` dumps the selected track's **active** slot. The demoted target is not the active slot, and the loop
button's *short* press is `TOGGLE_RECORD_FOR_SLOT` — pressing it to "select" the slot would record over the take being
dumped. So `dump_and_capture.py` gained, additively (defaults unchanged):

| Flag | Effect |
|---|---|
| `--select-track N` | Short-presses the track button through `hitl.actions.slots.press_track` (a `SELECT_TRACK`). |
| `--dump-slot N` | Sends `!CAPTURE_CONTENT_SLOT n-1` — the slot is named in the request, 1-based CLI → 0-based wire. |
| `--dump-only` | Write the dump and stop (the read-only residency check). |

Pinned by `scripts/test_dump_and_capture_target.py` (**4 OK**), including that no loop-button note is ever sent.

### 2. Cut cycles on a loop that has no unique event

The shipped cut anchors on a signature occurring **exactly once** in `effective`. This take is fully periodic — 1 152
rows forming **160 distinct signatures that each occur 3 or 32 times** (a 12-pitch ascending pattern ×32, a CC74 0–127
sweep ×3) — so the anchor rule is unsatisfiable and the first run returned `INCONCLUSIVE_CAPTURE`.

`verify_playback_content.py` now falls back to cutting by **event count**: `effective` is one complete ordered loop, so
a cycle is a consecutive chunk of exactly that many events, starting at the capture's first event equal to the loop's
own first event and then running **strictly** — no later phase search, because on internals that repeat a search would
re-align a dropped event away instead of reporting it. `cycle_cut` and `cycle_phase` record which rule was used, and
the comparison against `effective` is unchanged.

Pinned by `scripts/test_verify_playback_content_cycle_cut.py` (**4 OK**): a clean periodic capture passes on two
cycles, **a dropped event fails** (the property a phase search would have destroyed), a capture shorter than one loop
declines, and the cut starts at the loop's first event rather than at a stray prefix.

## Residency (recorded beside the verdict)

The target is **resident and survives a cold boot**: `CAPTURE_CONTENT,begin,track=8,slot=3,length=19200`,
`record` = `effective` = 1 152 rows, re-read in the playback run whose log carries rows from ~74 ms device uptime. The
restore returns the demoted loop intact, and `storage_matches_effective` is true.

## Gates

| Gate | Result |
|---|---|
| `scripts.test_dump_and_capture_target` | 4 OK |
| `scripts.test_verify_playback_content_cycle_cut` | 4 OK |
| affected host batch | recorded in the commit |
| `content-review.json` | `PASS`, 3 cycles, `inventory_equal` and `exact_order` true on all three |

## What this does and does not claim

* **Claimed:** on a demoted **25-bar** target (track 8 / slot 4: `19 200` ticks at 768 ticks/bar, above the 16-bar
  demotion bound) the MIDI actually served by the windowed playback gather, captured over three full loop cycles while
  the window advanced **79 times in 1-bar steps** (1 536-tick span, 25 positions per cycle), equals `effective` in
  inventory **and**
  order for every cycle.
* **Not claimed:** F-8's residency **sweep** over loop lengths (the demoted lengths reached and read *per length*) is
  still owed; 4.3's closure obligation at the transition and its overlap rule are separate open items; 6.5's hardware
  claims are untouched. `oracle_complete` stays `False` and 6.4 stays open.
* The board was not flashed in this session; the capture's identity chain is the pinned qualification image from the
  2026-10-19 device leg (`6cf5f874…`), unchanged since.
