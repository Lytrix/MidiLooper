# S4f.11 — acceptance → execution latency per interaction class (device)

Invariant 31 / DEC-078 separates **acceptance** (immediate, unconditional) from **execution** (scheduled by the MVCC
pipeline), which makes "how long from acceptance to execution" a first-class quantity. Design Revision 17 declares an
expectation per class and states that *measurement replaces the numbers*. This is that measurement, taken from the
**device's own rows** with `scripts/hitl_interaction_latency.py` (owner: `hitl.interaction_latency`); every sample,
its resolution and the rows it came from are in the JSON matrices beside the captures.

## Method (why a number here is a device number)

- **Both ends are device time.** `#CAP,<us>,…` carries `micros()`; a directly printed row carries the same clock as
  its `[<s>.<ms>]` prefix, and that coarser resolution travels with the sample (`resolution_us`).
- **Where a family carries its own event time, that time is used, not the ring's flush time.** `stop_state`'s
  `at_us` is when the transition happened; on this leg A's row was emitted 169.5 ms after its event
  (`#CAP` prefix 48 182 318 us against `at_us` 48 012 823 us), so a prefix-based reading would have reported the
  flush delay as execution latency and flagged a false budget exceedance.
- **Pairs are bounded by their own interaction.** A press-driven pair must land before the next press (or, for the
  switch handoff, inside the successor interval's own life); a stop pair must land inside its interval; a load pair
  before that target's next request. Rows are read in **file** order while spans are device time, so a row outside
  the span is skipped, never a reason to stop scanning — the ring flushes in batches.
- **A class the capture does not exercise is a named gap**, never a number.

## Measured

| Interaction class | Measured (device) | Rows | Declared (Revision 17) | Reading |
|---|---|---|---|---|
| Stop overdub | **106 µs** (A interval 3), **87 µs** (B interval 4) | `DIAG,reentry,bound` → `DIAG,stop_state,…,6,7` (`at_us`) | microseconds (same service turn / next playhead boundary) | **Within budget** — the same service turn |
| Record / overdub start | **103 µs** (A, B), 202 µs (od-undo capture) | `GS,<ctl>,36,<press>` → `ODUB,stage,manager_enter` | prewarm-bound; under the arm deadline (no figure declared) | **Informational**: no numeric deadline exists to compare, and the values are 3–4 orders below any candidate |
| Slot / track switch — route recognised | **177.7 ms** | `GS,<ctl>,54,<press>` → `Loop 5: Switch gesture from slot 4` | must stay under the window's switch granularity (S4b: 31 ms at 1/16 beat, 16 ms at 1/32; today's unit is one bar = 2,000 ms) | **Exceeds the forward target, within today's unit** — reported as an event for S4b (this is the *capture* switch, not the queued playback switch S4b tunes) |
| Slot / track switch — capture handoff | **645.2 ms** (B admitted 0.637 s into A's 1.523 s window) | the same press → `DIAG,reentry,admit,2015,4,4,1` | same S4b target | **Exceeds**; the handoff contains the predecessor's accepted stop plus the successor's admission, which is the L5 link itself |
| Undo / redo | **47 ms** (undo), **52 ms** (redo) — **warm root** | `MIDI: Undo (entries=3)` → `Undo applied:` | ~1–2 s **when the root is cold** | Warm case measured and far inside the figure; the **cold** case is a gap (below) |
| Load loop | leg's own slot: dispatch **1.554 s**, complete **1.673 s**. Boot-restore burst (64 slots): worst 51.5 s | `RRQ,…,queued,…` → `RRQ,…,running,…` / `RRQ,…,terminal,…` | seconds | **Within "seconds"** for the single-slot numbers; the burst maximum is the restore **queue**, and the class says so |

**Sources.** `captures/s45-odleg1a-20261002-163640-od` (image `ae591d95…`, fresh boot, accepted-sequence od leg:
stop overdub, start, switch route, handoff, load) with `interaction-latency.json` beside its run report;
`captures/s4f20-odundo-10-3` (undo/redo, warm root; `start` cross-check).

## Reported events (never a widened budget)

1. `switch`: 177,738 µs > 31,000 µs — the capture switch's route recognition. Observation for S4b, whose subject is
   the queued playback switch's granularity, not this handoff.
2. `switch_handoff`: 645,226 µs > 31,000 µs — the same caveat; the number is the predecessor-stop plus
   successor-admission path that L5 measures.
3. `load_loop` at boot: the 51.5 s maximum is the 64-slot restore queue, not one load; the class's own note carries
   that context so the number is not read as a load cost.

## Open, and why S4f.11 is **not** checked off

- **Cold undo/redo** has no device number. The declared figure is for a *cold* root (reconstruction from its
  descriptor), and no registered scenario drives that condition: the undo in `s4f20-odundo-10-3` runs immediately
  after the interaction that created the root (47/52 ms). A cold-root capture needs a route that loads/switches first
  — that is a scenario registration obligation, not a measurement that can be invented from a warm run.
- The `switch` class's Revision-17 subject is the queued playback switch; measuring it needs a scenario that queues a
  playback switch, which the accepted-sequence od leg does not drive (its switch is a capture handoff). The route and
  handoff numbers above are the honest neighbours, labelled as such.
- Undo/redo and load classes above come from two different boots; the campaign's single-session requirement (Phase 3)
  will fold them into one capture when a registered route exists.

No task checkoff follows from this file: it supplies the measured numbers S4f.11 asks for on four of six classes,
names the two gaps with the route each one needs, and reports the exceedances rather than adjusting any budget.

## The two gap classes, resolved as findings (2026-10-02, later the same day)

**Queued playback switch — exercised, measurable, no new route needed.** The class's Revision-17 subject is the switch
that goes through the SD-aware **playback** branch rather than the capture-switch, and registered scenarios already
produce it: `Loop 4: Queued playback switch (filled slot)` appears in
`captures/s45-hitl-20261002-150603-od`, `captures/s45-prepfix-20261002-151549-od`, `captures/s4f13-closure-od1` and
`captures/s4f13-mixed-fixed` (each with its own `GS,<ctl0>,<slot note>,<press>` row before it). What remains is an
owner extension -- the switch class currently pairs the *capture* route row only
(`scripts/hitl/interaction_latency.py`: "This sample is the *capture* switch's route recognition, not the queued
playback switch S4b tunes") -- so it is a measurement change, not a device or flow change.

**Cold-root undo/redo — blocked by S5, not by a missing route.** Revision 4 defines the cold state as **"slot left the
active set"** and states that "on demotion, hot entries are compacted to cold descriptors" and that on reactivation
"historical states stay compact until an Undo/Redo actually requests one -- which reconstructs or selects that
historical state **off-clock**" (`design.md:196-231`). No slot-demotion mechanism exists in the firmware today (the
`demote*` symbols are the **load job's** own handoff, `StorageManager/LoadJob*`), so every undo root reachable now is
hot -- which is what the measured 47/52 ms warm samples show, and why the declared ~1-2 s figure has nothing to measure
yet. The class therefore needs **S5.1/S5.2a** (demotion plus the cold-descriptor audit) before S4f.11 can close; that is
recorded as a dependency rather than papered over with an improvised "cold" run.

The undo/redo *vocabulary* is confirmed for that future route: `captures/s4f20-odundo-02` pairs
`[55.419] MIDI: Undo (entries=3)` with `[55.486] Undo applied: kind=1 undo_count=6` and `[55.486] [TRACK] Overdub
undone @ tick 12675`, and `[58.566] MIDI: Redo (entries=1)` with `[58.640] Redo applied` -- millisecond-resolution
printed rows, warm, 67 ms and 74 ms.

**Both acceptance campaigns are closed in the same session** (three consecutive `ACCEPTED_SEQUENCE_PASS` each, one boot
each, replay-verified bundles): overdub `captures/s45-camp4-20261002-200100-od1/-od2/-od3`, record
`captures/s45-camp5-rec-1/-2/-3`. Those runs are the acceptance evidence the S4.5 gate asks for, not new latency
classes.
