# Playback window and deferred owner round-robin admission

**Status:** **Ring B / RC2 closed (2026-08-26).** Slices 2a, 2b, and 3 are device-verified.
Ring A firmware is still not authorized. Residual **4b.1b** (indivisible
`NoteUtils::reconstructDisplayNotes` in `Loop::rebuildVisualCacheIdleSlice`) moved to
[`playback_window_build_owner_boundary_refinement.md`](playback_window_build_owner_boundary_refinement.md)
§ *4b.1b*. Two root causes were named from device evidence. Slice ordering and gates are at
the end. Ring A still answers **yes** on ownership, transitions, and execution bound.

**Kind:** refinement under the runtime scheduling Owner-Boundary Gate

**Bound authority:** [`ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md) +
[DEC-043](../DECISION_LOG.md#dec-043-execution-bound-class-is-a-gated-architectural-property)

**Structure authority:** [`RuntimeArchitecture.md`](../Authority/Architecture/RuntimeArchitecture.md),
[`IntervalProjection.md`](../Authority/Architecture/IntervalProjection.md)

**Parent roadmap:** [`runtime_scheduling_owner_boundary_admission_refinement.md`](runtime_scheduling_owner_boundary_admission_refinement.md)
**GitHub:** [#39](https://github.com/Lytrix/MidiLooper/issues/39) Ring A. Parent [#38](https://github.com/Lytrix/MidiLooper/issues/38). Work: [Work](https://github.com/users/Lytrix/projects/1)

**Sibling:** [`playback_window_build_owner_boundary_refinement.md`](playback_window_build_owner_boundary_refinement.md)
(F11 companion-replay finding is reused here, not re-derived)

**Baseline evidence:** [`session_20260826_141838.log`](../../captures/session_20260826_141838.log)
— record 15.37–52.13 s (18 bars, `loopLengthTicks=13824`), overdub 1 at 53.48–91.59 s (wrap at
89.58 s), overdub 2 at 96.66–103.50 s, STOPPED from 104.28 s. Build
`teensy41-capture-serial` (`SESSION_CAPTURE_INVESTIGATION_DIAG=0`).

**Proposed decision id:** DEC-046 (append only on approval; DECISION_LOG next id is 046).

**Do not add now:** interval reservation; `RuntimeWorkBudget::admit`; a new scheduler class or
Manager; a **fourth execution-bound class**; a priority scheduler; a second authoritative
representation; sizing against absolute capacity (64 destinations, 300 BPM, pathological slot
enablement); snapshot construction optimization; paired companion projection.

**Review round 1 (2026-08-26)** accepted the architecture direction and the RC1/RC2/RC3 separation,
and required four changes before `DEC-046`. All four are applied: the structural-bound claim is
withdrawn (§ *What Ring A does and does not bound*); `prefetchTicks` is derived rather than chosen
(§ *Prefetch lead relationship*, gate **G1**); RC2 is promoted to invariant **R** with the three
repairs as enforcement points; and the missed-prefetch failure policy is explicit (invariant **A1**).
The slice order is reframed as measurement closure → representation repair → admission.

**Review round 2 (2026-08-26)** narrowed the sizing basis without redesigning the rings. Ring A's
population is now the **playback-relevant** destinations whose window needs preparation, not all
enabled pairs (invariant **A2**); a **design envelope** (16 tracks x 2 active slots, ≤ 240 BPM) is
stated separately from absolute capacity; the 64-destination / 300 BPM calculation is **removed as a
blocking gate**; G1 becomes representative envelope sizing rather than worst-case characterization;
and the playback horizon is reopened as a horizon-and-bias question. Investigating that question
found the window is **playhead-centred**, so the forward horizon is already one bar and the
available lever is bias rather than size.

**Review round 3 (2026-08-26)** accepted the sizing basis and required four tightenings, all
applied: the `(N-1) x T_clock` arbitration term is labelled a **conservative** bound for
continuously-due destinations rather than the steady state; **A1** is split into invariant and
failure policy so the failure branch is not unreachable by definition; Ring B's dependency skip is
given **normative non-priority semantics**; and **2a is barred from shipping ahead of Ring B**
absent device evidence that the restored path is unexercised. Content window stays at 2 bars.

**Design session (round 4)** derived playback relevance from the runtime state machine and closed
the last blocking item. Finding: **no state check exists anywhere on the playback-window path** —
`engineShouldRun` is `return slotEnabled`, `getEffectivePlaybackTick` returns the global tick
regardless of state, and neither playback entry point consults track state. `141838` confirms the
consequence: one track ever changed state, nine slots were enabled, and window starts advanced one
bar per ~2 s for the whole session without emitting. `TRACK_STOPPED` is the leak. Queued-destination
prewarm keeps its commit-boundary exemption.

---

## RC1 — Playback window rebuilds converge on the bar edge inside one clock dispatch

### Symptom

Five discrete freezes at bar edges of the first overdub. Gaps in capture output, each ending with
an out-of-band clock catch-up burst:

| Bar entered | Gap start (s) | Gap (ms) |
|---|---|---|
| 4 | 59.366 | 82 |
| 5 | 61.364 | 83 |
| 6 | 63.369 | **282** |
| 7 | 65.377 | **322** |
| 8 | 67.366 | 182 |

No gap above 60 ms occurred anywhere in the 37 s record pass, and none after bar 8.

Audible effect is a late note-off, not a dropped note: `DIAG,late_event,off,264136,4608` — the
note-off due at the bar-6 boundary (tick 4608) was delivered **264 ms** late. Same window:
`late_off 264136,4` and `late_on 265136,5`.

### Proven owner

Every span in the call chain reports the same single-call maximum in the window ending 67.914 s:

```
#CAP,67914019,DIAG,midi_input,344341,3
#CAP,67914026,DIAG,clk,344337,3
#CAP,67914032,DIAG,tracks,344319,3
#CAP,67914039,DIAG,usbdev,344338,3
#CAP,67914074,DIAG,usbdisp,344337,3
#CAP,67914095,DIAG,usbclk,344337,3
#CAP,67914009,DIAG,midi_gap,26165,298
```

`RuntimeTimingTelemetry::recordSample` keeps a per-call maximum, so **one**
`TrackManager::updateAllTracks` call ran **344 ms**, reached through
`MidiHandler::handleMidiInput` → USB device drain → USB device dispatch → clock message →
`ClockManager` clock dispatch. `midi_gap` stayed at 26 ms, so this was not a service-order gap:
the time was consumed inside the clock dispatch. `count=3` matches the three largest gaps in that
window (63.4 / 65.4 / 67.4 s). Nothing else in the chain is material — `usbcap` 35 µs,
`usbnote` 672 µs, `noteappend` 152 µs, `notechg` 452 µs, `noterecon` 0.

This is [`ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md) inventory **row 1**
(`TrackManager::updateAllTracks` from clock dispatch, **Unbounded**) multiplied by **row 2**
(`Track::ensurePlaybackMergedMidiEventsBuilt`, **Unbounded**).

### Why it converges

`ensurePlaybackMergedMidiEventsBuilt` keeps its long-loop window while
`playhead + TICKS_PER_BAR/2 >= winStart && playhead < winEnd`, then centres a fresh 2-bar window
on the playhead. The reuse test therefore fails at the moment the playhead crosses `winEnd`, which
for a 768-tick window offset is one bar after the last rebuild — the same bar edge for every slot
that was built on the previous edge.

`TrackManager::updateAllTracks` runs `playMidiEvents` for the active slot and
`playMidiEventsForSlot` for each additional enabled slot, gated only by
`PlaybackMidiOutput::engineShouldRun(slotEnabled[i][s])` — independent of track state. This session
restored 9 enabled slots at boot (`enabledMask` t0 `0x40`, t1 `0x4`, t2 `0x5`, t3 `0x2`,
t4–t7 `0x1`).

Measured convergence: `DIAG,counter,PlaybackMergedMidiEventsRebuild,41` across 22 bar edges is
**two** content-sized rebuilds per edge, both admitted in the interval that crosses the edge.

Each gap begins immediately after the `cer_win` line of the first of those two rebuilds:

```
65.377  DIAG,cer_win,us=22323,span=434,copy=10909,filter=10709,lists=18,chunks=19,
        nospan=0,outside=9,copied=10,evcopy=1300,evout=237
                          ← 322 ms with no capture output ←
65.699  BPM 72.822 … 119.576         (clock catch-up)
65.700  DIAG,playback_build,7896,4608,1536,1
```

`lists=18, chunks=19, evcopy≈1300` is a loop with **18 active committed passes**. It is not the
overdubbed track: before its first wrap that loop has one record pass, `overdubSessionPassIds_` is
empty, and `isRecordPassId` excludes the record pass from the unpublished overlay, so
`tryGatherPublishedSnapshotEventsInWindow` makes no `CommittedEventRange` call and emits no
`cer_win`. The `playback_build` window start for this loop walks monotonically to `19200` at
`rev=1`, past the 13824-tick recorded loop, so it is a long loaded slot with **no published
snapshot**, taking the compatibility path `gatherCommittedEventsInWindow` →
`copyEffectiveCommittedEventsInRange` on every bar edge.

### Attribution closed — Slice 0 result

**Capture:** [`session_20260826_153941`](../../captures/session_20260826_153941.log) on
`teensy41-capture-serial-investigation-diag` after the RAM1 fix. Same hardware configuration as
`141838` — nine enabled slots at boot, one track transitioning. The stall reproduced larger:
`midi_input 393845,3`, `clk 393839`, `tracks 393818`, `usbdev 393841`, `usbdisp/usbclk 393839`.

**The unattributed remainder is `applyNoteEditPassSequence`, and inside it, note reconstruction.**
One `#CAP` group at 113.539 s:

```
DIAG,cer_win,us=23391,...,evcopy=1300,evout=237
DIAG,cer_edit,us=343020,del=1235,ulen=341642,recon=339073,ndel=40,nlen=18,lgth=8,shrt=10,noop=0,ev=164
DIAG,cer_copy,us=343328,copyin=143,core=343062,copyout=123,ev=164
DIAG,cer_range,us=367412,collect=56,append=23511,sort=220,edit=343625,lists=18,rows=217,evout=164
DIAG,cer_rows,active=217,disabled=0,comp=217,ne=0,del=185,crt=0,ulen=32,upit=0,uvel=0,urng=0
```

Nesting the figures:

| Span | µs | Share of parent |
|---|---|---|
| `tracks` (one `updateAllTracks`) | 393818 | — |
| `cer_range` (`copyEffectiveCommittedEventsInRange`) | 367412 | 93% of `tracks` |
| ├ `append` (windowed CER traversal) | 23511 | 6% |
| ├ `collect` + `sort` | 276 | <0.1% |
| └ `edit` (`applyNoteEditPassSequence`) | **343625** | **94%** |
|   └ `ulen` (length-update path) | 341642 | 99.4% of `edit` |
|     └ `recon` (note reconstruction) | **339073** | **99.3% of `ulen`** |

**The row population is constant.** `cer_rows` reports `active=217, comp=217` in **all 50** non-empty
samples — 217 Active EditPass rows, *every one* a companion row, split 185 deletes and 32 length
updates. The F11 hypothesis is now proven: the edit projection's cost scales with EditPass count,
not with the 2-bar window. 217 rows are replayed to produce **164** output events.

**Cost tracks executed length updates, but not linearly.** Across the eight largest samples,
`recon` is 98–99% of `ulen` every time, and `nlen` (executed length updates) is the main driver —
18 → 339 ms, 16 → 287 ms, 12 → 170 ms. But the same `nlen=18` appears at both **339073** and
**68989** µs, so per-row reconstruction cost is content-dependent, not fixed. Roughly 19 ms per
length update at the worst.

**This is the compatibility path, not the snapshot path.** `cer_range` first appears at 52.014 s —
before this session's long record began at 61.168 s — so the emitting loop is not the recorded
track. Every publish in the session (`RECS,stage,publish` / `ODUB,stop,seal`) belongs to the one
transitioning track. The loop carrying 18 chunk lists and 217 companion rows is a **loaded slot with
no published snapshot**, reaching `copyEffectiveCommittedEventsInRange` through
`gatherCommittedEventsInWindow`.

### Consequence — RC4 is a separate root cause

Ring A bounds **how many** rebuilds enter a dispatch. Slice 0 shows the surviving problem is **how
long one costs**: ~367 ms, of which 92% is reconstruction inside the edit projection. Checked against
the envelope table (`H_fwd_wall` = 1000 ms at 240 BPM), a 367 ms rebuild clears the `L < H_fwd`
requirement at every eligible count but **violates the `L <= H_fwd/2` target from 16 eligible
destinations upward** (344 ms budget). Admission scheduling alone therefore only just fits the design
envelope, with no margin for growth in EditPass count — and `rows=217` is already a fixed population
carried by a single loaded slot.

**RC4 — the length-update path re-runs note reconstruction per row on the compatibility gather.**
Per [`Multi-Stage-Bugfix-Workflow`](../../.cursor/rules/Multi-Stage-Bugfix-Workflow.mdc) this is a
distinct root cause from RC1 and must not be folded into Ring A: RC1 is admission multiplicity, RC4
is unit cost. It also has a candidate resolution already inside the architecture direction rather
than needing new machinery — a published `LoopSnapshot` carries edits already applied, so the
compatibility gather (and with it this entire cost) does not exist for a slot with a published root
([DEC-044](../DECISION_LOG.md#dec-044--immutable-loop-snapshot-model-replaces-maintained-lcr-direction),
[DEC-045](../DECISION_LOG.md#dec-045--overdub-snapshot-publish-is-idle-stepped)). Whether loaded slots
should publish on restore is the design question; it is **not** authorized here.

### Revision from `160244` — unit cost dominates multiplicity in this capture

[`session_20260826_160244`](../../captures/session_20260826_160244.log) changes the weighting between
RC1 and RC4, and Ring A's expected benefit must be restated honestly before Slice 4:

- **A single rebuild accounts for essentially the whole stall.** `tracks` max is **391.1 ms** and the
  largest single `copyEffectiveCommittedEventsInRange` is **362.9 ms**. There is no room left for
  multiplicity to be the main term in that sample.
- **Observed multiplicity is 2, not 5–16.** The 67 `cer_range` samples have a **median spacing of
  1998 ms** (one loop turn). Only 8 inter-arrival gaps fall under 50 ms, all in the 11.9–16.8 ms band
  — adjacent dispatches, i.e. **pairs**. Destinations in this capture are already largely
  self-staggered.
- **Therefore Ring A's benefit here is bounded by a factor of about 2**, while RC4 is a factor of
  roughly 9 (contended vs uncontended for identical input). Ring A remains architecturally correct —
  it removes an unbounded admission multiplier that is not bounded by anything today — but it must
  **not** be presented as the fix for the observed 391 ms.
- **`representativeRebuildWall` is 30–100 ms, not 367 ms.** The uncontended band is 29.5–98.9 ms;
  306–363 ms is the contended band. G1 must consume the former plus an explicit decision on whether
  the contended state may persist.

This strengthens rather than weakens the agreed sequencing: sizing a prefetch lead against 367 ms
would bake a contention artifact into the scheduling architecture.

**Sequencing decision (user, 2026-08-26).** RC4 is **investigated now**, in
[`playback_window_edit_projection_unit_cost_investigation.md`](playback_window_edit_projection_unit_cost_investigation.md).
Its **firmware fix remains a separate change** and cannot be smuggled into the Ring A implementation.
"Not authorized here" therefore means *the fix is out of scope for this plan*, **not** *ignore RC4
until after Ring A*. Order: Slice 1 measurement closure → RC4 investigation → measure RC4's candidate
resolution → **size G1 from the post-resolution cost** → Ring B / RC2 independently → Ring A last.
The prefetch lead must **not** be locked around today's 367 ms: at 240 BPM that single rebuild spans
~35 clock dispatch periods, and if RC4 resolution brings it to tens of milliseconds the required lead
changes by an order of magnitude.

### Attribution gap (original — closed by Slice 0 above)

~314 ms of the 344 ms is **not attributable from this capture**, for two mechanical reasons:

1. `RuntimeTimingTelemetry::recordPlaybackRebuild` stores a single pending sample and
   `emitPendingDeadlineOneShots` emits the **last** one. With two rebuilds per interval, the larger
   rebuild's total is overwritten — the `7896` above belongs to the *second* rebuild, not the one
   that emitted the 22 ms `cer_win`.
2. `DIAG,cer_range` (`collect` / `append` / `sort` / `edit` / `rows`) and
   `DIAG,lcr,vch,gather_*` are compiled out at `SESSION_CAPTURE_INVESTIGATION_DIAG=0`.

The one candidate this capture cannot exclude is the F11 finding already recorded in
[`playback_window_build_owner_boundary_refinement.md`](playback_window_build_owner_boundary_refinement.md):
`copyEffectiveCommittedEventsInRange` runs `applyNoteEditPassSequence` over **all** Active
companion rows, so its cost scales with EditPass count and not with the 2-bar window
([`ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md) row 7). It is stated here as
the open hypothesis, **not** as a root cause.

**Closing measurement (no code change):** recapture the same gesture on
`teensy41-capture-serial-investigation-diag` and read `DIAG,cer_range,…,edit=,rows=` against the
same bar edges.

---

## RC2 — Overdub wrap leaves the committed pass with no consumer representation

### Symptom

At the wrap, the display loses every overdubbed note and does not recover until STOPPED, ~16 s
later.

```
89.484  MO 128,5,84,0 / 144,5,83,100
89.554  [WARN] [MIDI] capture hot stop verify: non-canonical storage (check=2)
89.578  [DEBUG] [TRACK] Overdub wrap committed @ tick 14640
89.559  VCACHE,stale,ev,-1,notes,270,first,0,last,17,total,18,dirty,1
89.578  VCACHE,stale_range,ev,-1,notes,270,dsz,18,dcnt,18,dirty,1
89.573  DIAG,lcr,srcbefore,why=wrap,count=48
89.573  DIAG,lcr,srcafter,why=wrap,from=cache,before=48,copy=48,after=240
89.193  DISP,0,OVERDUBBING,13824,452,270,452,452,1,0,16,402
89.610  DISP,0,OVERDUBBING,13824,273,270,273,273,1,0,16,243
```

`DISP` fields are `slot, state, loopLen, sourceEventCount, visualNotes, frameNotes, bufferEvents,
hasCommittedPasses, windowStartTick, windowBars, windowNoteCount`. `sourceEventCount` drops
452 → 273 and `windowNoteCount` 402 → 243 while `visualNotes` stays at **270** — the record-pass
count — for the whole remainder of the session. The full content only appears at 106.118 s:
`DISP,0,STOPPED,13824,504,504,504,504,1,328,16,453`.

### Proven chain

The wrap moves 179 live `capture.store` events into an Active `OverdubPass` and, by design, only
records a pass id (`CommitReason::OverdubWrap`;
[`ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md) row 22 — "wrap only records a
pass id"). After that the content is reachable by no consumer, because all four routes are closed
at once:

1. **The published root is record-only.** The only publish events in the session are the record
   seal (`RECS,stage,publish … ok` at 52.149 s, `finalize … published` at 52.157 s) and the overdub
   **stop** seal (`ODUB,stop,seal … published` at 91.592 s). `DIAG,snapshot_idle … pending=1` runs
   continuously between them and never publishes, so no snapshot carries the wrap pass.
2. **`visualCache` is invalidated to a dirty record-only prefix** — 270 notes, `dcnt,18` of 18 bars
   dirty.
3. **Idle refill is off in exactly this state.** In `Track::processDeferredIdleMaintenance`,
   `deferredDerivedViewMaintenance` requires `isPlaying() || isStoppedRecording()`, so OVERDUBBING
   is excluded outright, and it also excludes PLAYING when a published root exists and
   `visualCache` is non-empty. The track was OVERDUBBING → PLAYING → OVERDUBBING until 103.50 s, so
   the 18 dirty bars were never serviced. `DIAG,loop_rem,idle_reconstruct` first appears at
   104.541 s, and `visualNotes` reaches 504 at 106.118 s.
4. **The overdub paint path is gated, against a documented invariant.** In
   `DisplayManager::rebuildOverdubCommittedDisplayLayer` the coverage branch correctly declines —
   `visualCacheCoversFollowWindow` fails on the 18 dirty bars. But the snapshot-gather branch below
   it carries the 4b.4 budget guard `loop.visualCache.notes.empty()`, which is false (270 notes), so
   paint falls through to the last branch — `havePaintWindow && !loop.visualCache.notes.empty()` →
   `filterNotesToFollowWindow(loop.visualCache.notes, …)` — painting the record-only cache, 243
   notes in the 16-bar window.

   [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) § task 4.4 already states the invariant this
   breaks: *"A nonempty dirty `visualCache` prefix does not authorize the paint window —
   `visualCacheCoversFollowWindow` must pass or the window gathers from the snapshot"*
   ([`235954`](../../captures/session_20260825_235954.log)). The 4b.4 guard reintroduced exactly the
   condition that invariant forbids, and `141034` closed 4b.4 on the display-cost metric
   (`timing_max,DisplayResolveLiveCapture=8608`) without a content check.

### Correctness consequence beyond display

`Loop::rebuildOverdubSourceSpanCache` reseeded the overlap-resolve source span **from that dirty
record-only `visualCache`** — `srcafter,why=wrap,from=cache,before=48,copy=48,after=240` where the
true content was 452. Every overlap-hold decision for the remainder of the overdub therefore ran
against content missing 179 notes. The same 89.554 s line records
`capture hot stop verify: non-canonical storage (check=2)`.

---

## RC3 — Snapshot construction does not converge (named, not bundled)

`DIAG,snapshot_idle` consumed roughly **1.0 s of CPU per 2 s of wall clock**, continuously from
16 s to 77 s — 2992 turns, ~24 s of CPU — for an 18-bar / ~500-note loop, publishing nothing
between 52.157 s and the 91.577 s stop seal.

| Wall window | turns | CPU (µs) |
|---|---|---|
| 40–42 s | 82 | 917564 |
| 44–46 s | 82 | 1129514 |
| 54–56 s | 150 | 1083388 |
| 68–70 s | 106 | 1048285 |
| 74–76 s | 97 | 1063050 |

A fair ring bounds this owner's **share**; it does not make it converge. This is a separate root
cause and must not be folded into RC1 or RC2 (Multi-Stage-Bugfix-Workflow: one root cause per
commit). Likely reading point is the `LoopSnapshotConstruction::step` note in `ExecutionBounds.md`
row 21 — "Starting a pass still reconstructs that pass's committed events into a change set".

---

## Proposed fix — one round-robin, two rings

RC1 and RC2 are the same shape: an owner's turn is suppressed by **another owner's state** instead
of being taken from a fixed rotation. The fix is one explicit round-robin, in two rings because the
owners sit on different paths. Coordination stays with the existing owners
(`TrackManager::updateAllTracks` and `main.cpp` idle service order) — `ExecutionBounds.md` does not
authorize a new scheduler and none is proposed (invariant O).

### Design envelope for this phase

Sizing decisions in this plan are made against the intended normal operating envelope, kept
explicitly separate from absolute representable capacity:

| | Design envelope (sizes this phase) | Absolute capacity (deferred) |
|---|---|---|
| Tracks | up to **16** (final architecture; `NUM_TRACKS` is 8 today) | maximum representable |
| Slots per track | up to **2** active under normal maximum usage | `MAX_LOOPS_PER_TRACK` = 8 |
| Destinations | ≤ 32 addressable, and Ring A rotates only the **playback-relevant** subset | pathological all-slot enablement |
| Tempo | ≤ **240 BPM** | `ClockManager` clamp `[20, 300]` |
| Playback horizon | **1 bar forward** as the candidate (see geometry below) | — |

The right column produces documented **capacity limits** later, once the architecture has
stabilized. It does not constrain this phase. Sizing today for 64 destinations at 300 BPM would
optimize against a configuration the instrument is not intended to operate in, and the earlier
revision of this document was wrong to make that a blocking gate.

### Ring A — interval path

**Owner:** `TrackManager::updateAllTracks`.

#### Eligible population — demand-driven, not every enabled destination

Ring A is not a fairness mechanism over every destination that exists. It arbitrates only among
destinations that actually require playback-window maintenance:

```
enabled destination
  ↓  participates in playback
playback-relevant
  ↓  forward horizon within prefetch lead
window needs preparation
  ↓
eligible for Ring A this dispatch
```

Only the final set consumes turns. A destination that is enabled but not participating in playback
never enters the rotation. A participating destination whose window is comfortably ahead of the
playhead leaves the rotation until it becomes due again, rather than holding a position to preserve
a theoretical fairness property. The ring is a **dynamic eligible set**, not a permanent
`enabledSlotCount`-position ring.

**Invariant A2:** no playback-relevant destination that needs a refresh may be indefinitely
displaced by another playback-relevant destination. That is the property worth enforcing; the
maximum representable destination count is not.

**Rule:** at most **one** eligible destination may run a content-sized
`ensurePlaybackMergedMidiEventsBuilt` per clock dispatch. `playbackWindowRefreshCursor_` advances
one position per dispatch through the eligible set whether or not it rebuilt, so no eligible
destination can be starved by a neighbour. Every other destination reuses its current window for
that interval.

**Self-staggering.** Prefetch plus one-admission-per-dispatch de-phases a synchronized burst: the
first destination refreshes `lead` ahead of its expiry, the next one dispatch later, and so on, so
their replacement windows are centred on slightly different playheads and their *next* due points
separate by one dispatch each. A simultaneous all-destinations-due burst — the RC1 phenomenon — is
therefore a transient at transport start or slot enable, not a steady state. This is a property of
the mechanism, not a measured result; Slice 4 verification must confirm the eligible-set size decays
after the first rotation rather than assuming it.

#### Design session — what makes a destination playback-relevant

**Framing:** a destination is playback-relevant when the runtime is **obligated to maintain a
playback window for it during the current transport state**. The predicate is derived below from the
runtime state machine, not from the performance problem that exposed it.

**The obligation condition is missing entirely today.** Tracing the path end to end:

- `updateAllTracks` gates on `PlaybackMidiOutput::engineShouldRun(slotEnabled[i][s])`, and
  `engineShouldRun` is `return slotEnabled;` — nothing else.
- `Track::getEffectivePlaybackTick(currentTick)` returns `currentTick` unless jam playback is
  active, so a destination's playhead advances with the global transport **regardless of track
  state**.
- `playMidiEvents` / `playMidiEventsForSlot` return early only on `isStoppedRecording()` and on
  `!loop.hasCommittedPasses() || loop.loopLengthTicks == 0`. **No state check.**
- `playCommittedLoopMidi` then calls `ensurePlaybackMergedMidiEventsBuilt` **before** the
  `mergedEvents.empty()` return.

So any enabled slot holding committed content rebuilds its window once per bar for the whole
session, whether or not its track ever entered PLAYING.

**Confirmed on `141838`:** only **one** track ever changes state in the entire capture (the nine
`ST,Track` lines are all one track), yet **nine** slots were enabled at boot (`enabledMask` t0
`0x40`, t1 `0x4`, t2 `0x5`, t3 `0x2`, t4–t7 `0x1`). `DIAG,playback_build` window starts advance by
exactly one bar every ~2 s — `0, 768, 1536, 2304, …` — continuously from 15.409 s, the moment
recording began. Emission is *not* affected: MO counts on every channel other than the recorded
channel 5 are 9 or 10, matching the 9 transition-driven all-notes-off broadcasts, so non-playing
destinations rebuild windows without emitting. The cost is pure waste, which is exactly why it never
surfaced as a correctness bug.

**Derived obligation table:**

| Candidate condition | Establishes obligation? | Derivation |
|---|---|---|
| Slot enabled | **Necessary, not sufficient** | Today's only gate; carries no statement about playback |
| Track playback engine running for this slot | **YES — the missing condition** | Emission and ledger advance only happen while the engine runs; nothing else makes a window's content reachable |
| Has committed content (`hasCommittedPasses() && loopLengthTicks > 0`) | **Necessary** | Already enforced *before* the window build, so it costs nothing today — but hoist it into eligibility so the ring does not rotate over destinations it will refuse |
| Selected / active destination | **NO** | Layered slots emit through `playMidiEventsForSlot`; active-vs-layered selects `PlaybackEmitPolicy`, not obligation |
| Muted (track mute, slot mute, solo) | **NO** | Documented intent in `PlaybackMidiOutput.h`: "Mute/solo/slot-mute suppress MIDI send on the track channel and output ports. Enabled slots keep running the playback engine." The engine must keep cursor and ledger coherent so unmuting does not strand or drop notes. Dropping muted destinations from Ring A would break that |
| Queued for playback | **NO for rotation, YES for readiness** | Resolves open item 3 — see below |
| `STOPPED_RECORDING` | **NO** | Already an explicit early return in both entry points |

**By track state**, obligation exists in `TRACK_PLAYING` and `TRACK_OVERDUBBING`. It does not exist
in `TRACK_EMPTY` or `TRACK_ARMED` (nothing started), or `TRACK_STOPPED_RECORDING` (already excluded
in code). **`TRACK_STOPPED` is the leak** — silent by definition, yet fully rotating today.
`TRACK_RECORDING` **is included** (resolved 2026-08-26, `DEC-046` item 2): layered-slot playback
during recording is **intentional**, so other enabled slots on the same track retain playback-window
obligation. The recorded loop itself stays excluded by lack of committed passes, not by state.

**Resulting predicate** — a destination is playback-relevant when all hold:

```
slotEnabled[track][slot]
  && trackPlaybackEngineRunning(track)      // PLAYING, OVERDUBBING or RECORDING (DEC-046 item 2)
  && loop.hasCommittedPasses()
  && loop.loopLengthTicks > 0
```

Ownership: this belongs to `TrackManager` alongside `isTrackAudible`, as the engine-side counterpart
of that audibility predicate — `isTrackAudible` answers "should this be heard", the new predicate
answers "must a window be maintained". Both are `TrackManager` policy over `(track, slot)`, so
[`TrackManagerPlaybackPolicy.cpp`](../../src/TrackManager/TrackManagerPlaybackPolicy.cpp) is the
owner and no new module appears. Naming follows action + scope; the concrete name is settled at
implementation against `NAMING.md`.

**Open item 3 resolved — queued destinations keep their commit-boundary exemption.** A queued
destination needs a *ready* window, not a *rotating* one, and the code already implements exactly
that: `updateAllTracks` refuses to commit a slot switch while
`!isPlaybackMergedMidiEventsReadyForSlot(targetSlot)` on a long loop, and prewarm happens off the
clock path in `main.cpp` and `MidiButtonActions.cpp` via `ensurePlaybackMergedEventsForSlot`. Ring A
therefore admits a destination only once obligation begins — at the commit — and prewarm stays
outside the per-dispatch cap.

**Required trigger change (IntervalProjection):** the reuse test must become a **leading-edge
prefetch** — refresh when `playhead + prefetchTicks >= winEnd` — instead of refreshing on expiry.
The 2-bar window size does not change; only the trigger moves. This is the bounded span selection
that [`IntervalProjection.md`](../Authority/Architecture/IntervalProjection.md) already requires of
timing-critical callers ("Timing-critical callers must select a bounded span range before
projection").

**Large-work boundary:** because the replacement window is built before the current one runs out,
the selected `mergedEvents` stays selected until the replacement is complete, and publication
remains the single existing assignment — satisfying
[`RuntimeArchitecture.md`](../Authority/Architecture/RuntimeArchitecture.md) § Large-work boundary.

#### What Ring A does and does not bound

Ring A bounds **how many** content-sized rebuilds enter one clock dispatch. It does **not** bound
**how long** one of them takes. The unit cap and the maximum indivisible step are different
quantities here, and only the first is enforced:

| Property | Value | Enforced |
|---|---|---|
| Admissions per clock dispatch | 1 | **Yes** — cursor |
| Max indivisible step | one `ensurePlaybackMergedMidiEventsBuilt` (content-sized, hundreds of ms observed) | **No** |
| Progress | `playbackWindowRefreshCursor_` advances every dispatch | Yes |
| Rotation period | `eligibleCount` dispatches (dynamic, demand-driven) | Yes |
| Missed deadline | see failure policy below | Yes |

**Row 2 therefore stays `Unbounded`.** Admission multiplicity is not an execution-bound class, in
the same sense that `ExecutionBounds.md` already states *"'Off-clock', 'deferred', and cold/flash
placement are not execution-bound classes."* Reclassifying row 2 requires the window build itself
to become resumable with an enforced per-turn cap; that work is **not** in this plan. Row 1 stays
`Unbounded` and loses its largest content-sized multiplier — a note in the inventory, not a class
change.

This keeps the inventory honest against the ~60% clock-interval maintenance target: at 120 BPM a
clock dispatch is 20.8 ms, so a single 344 ms rebuild is ~16 dispatch periods. Ring A stops nine of
those from stacking; it does not make one of them fit.

> **Rejected wording.** Calling row 2 "admission-bounded" would introduce a fourth bound class.
> `ExecutionBounds.md` defines exactly three and [DEC-043](../DECISION_LOG.md#dec-043-execution-bound-class-is-a-gated-architectural-property)
> makes the class set a gated property, so the class stays `Unbounded` and the admission cap is
> recorded as a separate property of row 1's call site.

#### Scheduling geometry — the forward horizon is already one bar

`ensurePlaybackMergedMidiEventsBuilt` in [`TrackPlaybackWindowBuild.cpp`](../../src/Track/TrackPlaybackWindowBuild.cpp)
centres the long-loop window on the playhead:

```cpp
constexpr uint32_t kMergedMidiEventsGatherBars = 2;
const uint32_t winLen = kMergedMidiEventsGatherBars * Config::TICKS_PER_BAR;
uint32_t winStart = playhead > (winLen / 2) ? playhead - (winLen / 2) : 0;
```

So the "2-bar window" is 2 bars of **content**, of which only `winLen / 2` — **one bar, 768 ticks**
— lies ahead of the playhead at creation. The backward half is not slack: the comment in the same
block states it exists for "LoopEnd launch + clock catch-up", and reuse deliberately tolerates a
`TICKS_PER_BAR / 2` backward margin.

This reframes the 1-bar question. The candidate 1-bar **forward horizon** is what the current
design already provides; taking total content to 1 bar would halve the forward horizon to half a
bar, not hold it at a bar. The available lever is therefore the window's **bias**, not its size:
a 2-bar window biased forward (for example `winStart = playhead - TICKS_PER_BAR / 4`) yields
1.75 bars of forward horizon at identical content cost. How much backward margin LoopEnd launch and
clock catch-up actually require is an open question (item 3) and bounds how far the bias can move.

**Notation:** `H_fwd` is the forward horizon in ticks — currently `winLen / 2` = 768.

#### Prefetch lead relationship (gate G1)

The lead cannot be chosen as a musical fraction. A replacement must be complete before the playhead
reaches `winEnd`, and a destination may wait a full rotation for its turn:

```
leadWall  >=  (N - 1) x clockIntervalWall     (conservative arbitration bound, see below)
            +  representativeRebuildWall
            +  safetyMargin

required:  leadWall  <  H_fwd_wall           (otherwise the trigger fires at window creation)
target:    leadWall  <= H_fwd_wall / 2       (each destination spends at most half its
                                              window period waiting for or doing preparation)
```

**The arbitration term is a conservative admission bound, not the steady state.** For an eligible
set of `N` destinations that all remain **continuously due** for a full rotation, the maximum
admission wait is `(N - 1) x clockIntervalWall`. That premise contradicts the mechanism: eligibility
is dynamic, and prefetch removes a destination from the eligible set as soon as its refresh
succeeds, so the steady-state eligible population is expected to be smaller than the momentary peak.
`N` is therefore the **conservative** figure to size against, and the self-staggering that reduces
it is a property **Slice 4 verifies rather than assumes**. Sizing against the conservative bound
while expecting better is the intended asymmetry; sizing against the expected steady state before it
is demonstrated is not.

Rebuild cost is fixed in **wall time** while the lead is spent in **ticks**, so a given tick lead
buys less wall time as tempo rises: the **upper** end of the operating tempo range binds, not the
lower. Correcting my earlier sizing note — it used the 2.6 ms **internal tick** as the dispatch
period, but `TICKS_PER_CLOCK` is 8, so a clock dispatch is 20.8 ms at 120 BPM and 10.4 ms at
240 BPM. That factor of eight is where the arbitration term comes from, and it is why
`TICKS_PER_BAR / 4` was never a defensible starting point.

Within the design envelope (`H_fwd` = 768 ticks, 240 BPM ⇒ tick 1.302 ms, dispatch 10.42 ms,
`H_fwd_wall` = **1000 ms**), the conservative arbitration term and the resulting room for a rebuild
are:

| `N` continuously due | Arbitration | Room under `L < H_fwd` | Room under `L <= H_fwd/2` |
|---|---|---|---|
| 8 | 73 ms | rebuild + margin < 927 ms | < 427 ms |
| 16 | 156 ms | rebuild + margin < 844 ms | < 344 ms |
| 32 | 323 ms | rebuild + margin < 677 ms | **< 177 ms** |

The 32 row is the **normal upper envelope**, not a Ring A requirement. Ring A is sized for the
maximum number of destinations that are simultaneously playback-relevant *and* due for a refresh —
which is what `N` denotes — and 32 addressable destinations is an upper bound on that, not a
prediction of it. Treating 32 as a Ring A design target would reintroduce the same
capacity-driven sizing this revision removed, one level down.

One bar at 240 BPM is **1 second**, not 2 — four quarter notes at 250 ms each. (2 s per bar
corresponds to 120 BPM.) At 120 BPM every figure above doubles, so the envelope's upper tempo is
the binding case as expected.

The readable result: the envelope is comfortable under `L < H_fwd` at every eligible count, and
comfortable under the stricter `L <= H_fwd/2` target up to ~16 eligible destinations. Only the
32-eligible case is tight, and only against the stricter target. That is a workable design space —
which is precisely why the extreme case does not need to be measured to proceed.

**G1 — representative operating-envelope sizing.** Before Ring A implementation: select the
playback horizon (and bias), then derive a prefetch lead sufficient for the design envelope and the
observed eligible-destination population, from enough representative rebuild measurements to make
that choice. **`representativeRebuildWall` is taken after RC4's candidate resolution is measured, not
from today's 367 ms** — see the RC4 sequencing decision above. **Extreme theoretical capacity is not a
prerequisite.** Absolute capacity limits —
maximum representable slots, the full `[20, 300]` tempo clamp, pathological all-slot enablement —
are established later, once the architecture has stabilized, and may be expressed as documented
capacity limits rather than as constraints this phase must solve.

### G1 result — 2026-08-26

**Status:** **Satisfied** for the current horizon. This records the derived lead. It does **not**
authorize Slice 4 / Ring A firmware.

**Horizon / bias selected for this sizing.** Keep the current playhead-centred 2-bar window in
`Track::ensurePlaybackMergedMidiEventsBuilt`:

- content = `kMergedMidiEventsGatherBars * TICKS_PER_BAR` = 1536 ticks
- `H_fwd` = `winLen / 2` = **768 ticks**
- backward half stays one bar for LoopEnd launch + clock catch-up

Window bias (item 2) is not moved. If a later measurement changes `H_fwd`, G1 is re-derived. The
current reuse test (`TICKS_PER_BAR / 2` backward tolerance) stays as-is.

**`representativeRebuildWall`.** Snapshot-path maximum from
[`202654`](../../captures/session_20260826_202654.log): eight `path=snapshot` samples, all `T4/S0`,
305–358 µs. **`representativeRebuildWall` = 358 µs.** `T0/S6` compatibility (maximum 325.541 ms)
is excluded. The same capture's non-`T0/S6` compatibility ceiling is `T4/S0` 11.859 ms (`T6/S0`
6.768 ms); that ceiling is a check, not the named representative.

**Envelope constants at the binding tempo (240 BPM).**

| Symbol | Value |
|---|---|
| `TICKS_PER_BAR` | 768 |
| `TICKS_PER_CLOCK` | 8 |
| tick wall | 250 ms / 192 = 1.302083 ms |
| `T_clock` | 10.416667 ms |
| `H_fwd_wall` | 1000.000 ms |
| `H_fwd / 2` | 500.000 ms |
| `N` | **32** (DEC-046 envelope: 16 tracks × 2 slots) |
| `safetyMargin` | **1 × `T_clock`** = 10.417 ms |

**Derived lead.**

```
leadWall >= (32 - 1) × 10.416667 ms + 0.358 ms + 10.417 ms
         = 333.692 ms
         = 256.27 ticks
```

Clock-align up to 264 ticks (33 clocks = 343.750 ms). The same-capture non-`T0/S6` compatibility
ceiling (11.859 ms) needs 265.11 ticks at this `N` and margin, so the recorded lead is the next
clock multiple:

| Derived | Value |
|---|---|
| **`prefetchTicks`** | **272** (34 clocks) |
| `leadWall` at 240 BPM | 354.167 ms |
| Required `leadWall < H_fwd` | 354.167 < 1000 |
| Target `leadWall <= H_fwd / 2` | 354.167 <= 500 |

`N=32` is no longer tight: rebuild + margin at the snapshot representative is 10.775 ms against
177 ms of `H_fwd / 2` room after arbitration. Substituting the 11.859 ms non-`T0/S6` ceiling still
clears both inequalities. Substituting the excluded `T0/S6` 325.541 ms would consume the entire
177 ms room and is why that destination is not a G1 input.

Observed destinations in `202654` are `T0/S6` (ignored), `T4/S0`, and `T6/S0`. `161834` also
reported `T2/S2` at 9.406 ms. `<count>` is still not an eligible-set size; Slice 4 verifies
self-staggering rather than assuming it. A lead sized only for the four observed destinations
would be ~40 ticks and is **not** used.

**Not authorized by this result:** writing `prefetchTicks` into firmware, changing the reuse
test, moving window bias, or starting Ring A.

#### Invariant A1 and the missed-prefetch failure policy

The invariant and the policy are separate statements. The invariant says what the architecture must
achieve within the envelope; the policy says what happens if it does not. Stating them as one
sentence would make the failure branch unreachable by definition, which is exactly the reading to
avoid.

> **Invariant A1.** Under the design envelope, a playback-relevant destination must have its
> replacement window ready before its current window expires.
>
> **Failure policy.** If A1 is violated, the existing immutable window remains selected and **no
> synchronous rebuild is permitted on the clock path**.

A1 is an intended property, not yet a demonstrated one — which is precisely why the policy exists
and why its diagnostic is counted. Concretely, when the playhead reaches `winEnd` and no replacement
is ready:

1. Do **not** call `ensurePlaybackMergedMidiEventsBuilt` from the dispatch. That is the RC1
   behavior this plan removes; reintroducing it as an emergency path reinstates the 344 ms stall
   at exactly the worst moment.
2. Keep the existing window selected. Events outside it are not emitted for that interval.
3. Emit a distinct diagnostic (`DIAG,playback_window_starved,slot,…,winEnd,…,playhead,…`) and count
   it. A starved interval is an **architecture failure to fix**, not a tolerated mode — it means
   the lead was undersized, the eligible set exceeded what the horizon supports, or a destination
   was admitted that should not have been playback-relevant.

Starvation counts are also the signal that separates the design envelope from absolute capacity: if
they only appear beyond the envelope, they document a capacity limit; if they appear inside it, G1's
sizing is wrong.

The audible consequence of the policy (missing notes in a starved interval) is deliberately made
loud and countable rather than converted into a timing stall. If starvation is observed at all
during Slice 4 verification, Slice 4 fails and G1 is re-derived.

### Ring B — idle path

**Owners:** `main.cpp` idle service order + `Track::processDeferredIdleMaintenance`.

Replace the three ad-hoc pairwise gates now in tree with **one** cursor over the declared deferred
owners:

| # | Owner |
|---|---|
| 1 | `Loop::stepPendingSnapshotPublish(1)` |
| 2 | `Loop::rebuildVisualCacheIdleSlice` |
| 3 | `Loop::applyDeferredVisualCacheWrapAppend` |
| 4 | `StorageManager::processDeferredSaveState` |
| 5 | `runDeferredLoadAndDisplayFrame` |

**Rule:** one owner advances per idle turn; the cursor advances unconditionally, including when the
selected owner has no work. Per-owner eligibility predicates stay, but **no owner's pending state
may suppress another owner's turn** — that suppression is the RC2 mechanism.

**Dependency rule for owner 3 (not a priority scheduler).** A pending wrap append is not merely
queued work: until it runs, the representation that owners 2 and 5 and the paint path read does not
cover the wrapped bars. Equal five-way rotation would let those owners consume turns producing
answers derived from content known to be missing. So owner 3 carries one eligibility rule:

> While `deferredVisualCacheWrapAppendPending_` is set, an owner whose output depends on visual-cache
> coverage of the wrapped bars is not eligible; the cursor skips it and it becomes eligible again on
> the next rotation after the append completes.

This is still round-robin admission — the cursor advances unconditionally, no owner is starved, and
no owner gains a standing priority. It respects a **content dependency**, which is different in kind
from the "don't run A because B is pending" *budget* suppression that Ring B removes. The
distinction is worth stating in the DEC entry so a later reader does not mistake this for the
pattern being deleted.

**Non-priority semantics (normative).** Skipping an ineligible owner does **not** transfer its turn
to another owner as priority; it means that no work from that owner is admitted at that cursor
position. The cursor position is consumed either way. Owner 3 therefore gains no extra turns, runs no
sooner in the rotation than its position allows, and the rule remains a **validity condition on the
skipped owner's own work** — that owner's output would be derived from content known to be missing.
The moment a skip starts buying another owner an earlier turn, the rule has become a hidden priority
mechanism and has left this design.

**Removed (per session decision to replace, not coexist):**

- `Track::snapshotIdleRoundRobinTurn_` and `MidiServiceDrain::shouldRoundRobinSnapshotIdle`
- `Track::visualIdleCadenceCounter_` (the `% 4` visual cadence) and
  `MidiServiceDrain::shouldConstrainVisualIdleForUnpublishedSnapshot`
- `MidiServiceDrain::shouldRunSnapshotOnPostStopLane` /
  `shouldRunPersistOnPostStopLane` and the `StorageManager::postStopSnapshotRanThisTurn()` /
  `notePostStopSnapshotRan()` / `notePostStopPersistRan()` lane toggles

**Also removed:** the OVERDUBBING exclusion on `deferredDerivedViewMaintenance`. The 241–278 ms
`idle_reconstruct` spikes that motivated it
([`session_20260825_230556`](../../captures/session_20260825_230556.log)) are a **unit-cap**
problem; the existing 1-bar constrained slice is the cap. Turning the owner off instead makes wrap
content unreachable, which is what `141838` measures.

### RC2 invariant — representation coverage

The three repairs below are not independent patches. They are enforcement points for one invariant,
and it is the invariant — not the individual guards — that `DEC-046` should record:

> **Invariant R (representation coverage).** A derived representation may satisfy a consumer query
> only when its coverage corresponds to the requested interval at the requested revision. A
> representation that does not cover the interval must be refused, not returned as partial.

What failed at wrap was not staleness. The record-only `visualCache` was **accepted as authoritative
for a range it did not cover** — 270 notes over 18 bars all marked dirty, returned to two different
consumers as if complete. Staleness would have been a freshness question with a revision answer;
this was a coverage question answered by a non-empty container.

Invariant R is the consumer-side counterpart of the immutable-snapshot direction in
[DEC-044](../DECISION_LOG.md#dec-044-immutable-loop-snapshot-model-replaces-maintained-lcr-direction):
committed history is authoritative and immutable, derived representations are answers about an
interval, and an answer that does not span the question is not a weaker answer — it is not an
answer. `visualCacheCoversFollowWindow` already encodes exactly this test; the failure in `141838`
is that two call paths reached content **without** consulting it.

Each repair is the enforcement point for one consumer:

| # | Consumer | Enforcement |
|---|---|---|
| 1 | Overdub paint path | Coverage test must gate **every** branch that reads `visualCache.notes`, not only the first |
| 2 | The wrapped bars themselves | Coverage must be **produced**, on a guaranteed turn, or no consumer can ever be satisfied |
| 3 | Overlap-resolve source span | Coverage test must gate the cache seed; a refused seed must fall back, not truncate |

Repair 2 is what makes the invariant satisfiable rather than merely restrictive: without it, refusing
uncovered reads would correctly refuse *forever*.

### RC2 repairs (existing owners only)

1. **`DisplayManager::rebuildOverdubCommittedDisplayLayer`** — drop the 4b.4
   `loop.visualCache.notes.empty()` term from the snapshot-gather branch. No replacement predicate
   is needed: `visualCacheCoversFollowWindow` is already evaluated on the branch above, so reaching
   the gather branch *means* coverage failed. The final fall-through
   (`havePaintWindow && !loop.visualCache.notes.empty()` → `filterNotesToFollowWindow`) must also
   stop painting a cache that failed that coverage test. Restoring the invariant reopens 4b.4's
   cost concern, which is why the gather must be admitted through Ring B's turn (repair 2) rather
   than run unconditionally on the paint path — `CURRENT_WORK`'s standing "do not CER-gather on the
   overdub paint path" constraint stays in force.
2. **Wrap publishes its own content.** Run `Loop::appendOverdubPassWrapPairedNotes` for the bars the
   wrap pass actually wrote, through the existing `deferredVisualCacheWrapAppendPending_` flag and
   `Loop::applyDeferredVisualCacheWrapAppend`, with a guaranteed Ring B turn (owner 3 above).
3. **`Loop::rebuildOverdubSourceSpanCache`** — reject the `visualCache.notes` seed when
   `visualCacheDirty` and the dirty bars intersect the requested span. `141838` shows that seed
   producing 240 notes where the true content was 452.

   **This repair is a semantic-correctness gate, not a display gate.** The source span feeds
   overlap resolution, so an incomplete span does not produce a wrong picture — it produces wrong
   *note resolution decisions*, and those get committed. Its gate is therefore independent of the
   display gate: the test must show that the source used for subsequent overlap resolution is never
   incomplete, not merely that `visualNotes` recovers. A refused seed must fall back to a covering
   source; silently returning the truncated span is the failure being fixed.

---

## Architecture checkpoint (answers)

| Question | Answer |
|---|---|
| Ownership change? | **YES** — `updateAllTracks` gains admission ownership for playback-window refresh; Ring B centralizes deferred-owner turn ownership |
| State transition change? | **YES** — when a playback window becomes ready moves from expiry to prefetch |
| Execution bound change? | **YES** — row 1's content-sized multiplier is capped at 1 admission per dispatch, and invariant A1 adds a failure policy. **No class is reclassified:** rows 1 and 2 both stay `Unbounded` |
| Behavior-preserving? | **NO** for display after wrap (that is the RC2 fix); **YES** for emitted MIDI content |
| Reuse decision | **YES** — extend `TrackManager::updateAllTracks`, `ensurePlaybackMergedMidiEventsBuilt`, `Track::processDeferredIdleMaintenance`, `DisplayManager::rebuildOverdubCommittedDisplayLayer`, `Loop::rebuildOverdubSourceSpanCache`. No new owner, no new Manager |

Three "yes" answers ⇒ design session required before any firmware edit
([`architecture-checkpoint-bugfix`](../../.cursor/rules/architecture-checkpoint-bugfix.mdc),
`ExecutionBounds.md` rule 2). This document is that design artifact; it needs user approval and a
`DEC-046` entry before Slice 1.

---

## Slice order and gates

Slices 0 and 1 together are **measurement closure for RC1**, not ordinary fixes. `141838` proves a
344 ms `updateAllTracks` occurred and proves two rebuilds were admitted per bar edge; it does not
establish the duration of either one, because `recordPlaybackRebuild` overwrites the first with the
second. Ring A's lead relationship consumes a representative per-rebuild figure, so the admission
change cannot be sized until closure is complete. The order is **measurement closure →
representation repair → admission change**, and Slice 1 is inside the first phase rather than a
normal firmware slice that happens to come first.

Closure is scoped to **what the next decision needs**: a representative per-rebuild duration inside
the design envelope, and the observed eligible-destination count per bar edge. It is not an attempt
to characterize worst-case capacity.

| Phase | Slice | Content | Gate |
|---|---|---|---|
| **Measurement closure (RC1)** | **0** | Recapture to attribute RC1's remaining ~314 ms | **DONE** — [`153941`](../../captures/session_20260826_153941.log): `edit`/`recon` is 92% of the rebuild. RAM1 blocker fixed en route. Surfaced **RC4** |
| | **1** | `recordPlaybackRebuild` keeps per-window **maximum** + slot/path; count rebuild calls before emission | **DONE for destination attribution** — [`161834`](../../captures/session_20260826_161834.log) reports four destinations: `T0/S6` (57 lines, max 166.325 ms), `T4/S0` (34 lines, max 25.439 ms), `T6/S0` (2 lines, max 10.965 ms), and `T2/S2` (1 line, max 9.406 ms). The maximum sample carries its track/slot. The deferred line now also carries `path=snapshot|compatibility|note_edit`, captured at the existing playback branch point. [`202654`](../../captures/session_20260826_202654.log) validates 82 emitted samples: 74 compatibility, 8 snapshot, and no note-edit samples. **Telemetry limitation:** `<count>` counts rebuild calls before the next one-shot emit, not distinct destinations per bar edge; it is not used as an eligible-destination count. Native: `test_playback_rebuild_keeps_maximum_and_counts_destinations` |
| **Representation repair (RC2)** | **2a** | Repairs 1 + 2 — coverage gate on the paint path, wrap append produces coverage | **DONE** — [`183452`](../../captures/session_20260826_183452.log) / [`185141`](../../captures/session_20260826_185141.log): wrap `DISP` rises (286→466 / 271→437). Zero `srcafter,why=wrap,from=cache`. Shipped with Slice 3 |
| | **2b** | Repair 3 — source-span seed coverage gate | **DONE** — same captures: first wrap `from=passes` before `VCACHE,stale_range`; `srcafter,why=wrap` never seeds from a dirty cache |
| **Admission (RC1)** | **3** | Ring B cursor replaces the three pairwise gates; owner-3 dependency rule | **DONE** — dirty bars drain during OVERDUBBING. Reconstruct residual **4b.1b** moved to the owner-boundary plan; it does not keep this slice open |
| | **4** | Ring A: playback-relevant eligible set + cursor + prefetch trigger + horizon/bias choice + invariants A1/A2 | **G1 satisfied** — `prefetchTicks` **272**, `representativeRebuildWall` **358 µs**, horizon unchanged. Device: no `tracks` sample above one dispatch period at bar edges; `late_off` back to the record-pass class (≤ 27 ms); **zero** `playback_window_starved` inside the design envelope; eligible-set size decays after the first rotation (self-staggering confirmed); `ExecutionBounds.md` row 1 admission note updated in the same commit. **Firmware still not authorized.** |
| **Separate** | **—** | RC3 convergence | Separate plan; not authorized here |
| **Separate** | **—** | **RC4** unit cost — reconstruction per length-update row on the compatibility gather | **Investigation closed for G1.** [`playback_window_edit_projection_unit_cost_investigation.md`](playback_window_edit_projection_unit_cost_investigation.md). G1 now uses the snapshot-path representative (358 µs), not the 367 ms `T0/S6` stall. **Fix remains a separate change.** |

**Verification for every slice:** `pio run -e teensy41-capture-serial` first (RAM1 locals ≥ 4000),
then `pio test -e native`, then device recapture per
[`HITL-Test-Flow`](../../.cursor/rules/HITL-Test-Flow.mdc).

### Slice 0 blocker — the investigation-diag build is not device-viable

Measured 2026-08-26, both envs built from the same tree:

| Env | RAM1 variables | RAM1 code + padding | **free for local variables** | Verdict |
|---|---|---|---|---|
| `teensy41-capture-serial` | 93696 | 424796 + 1188 | **4608** | PASS (floor 4000) |
| `teensy41-capture-serial-investigation-diag` | 97792 | 424860 + 1124 | **512** | **FAIL** |

PlatformIO reports `SUCCESS` for both, but 512 bytes of DTCM stack is not runnable — `cer_range`
alone builds its line in a `char line[224]` stack buffer, so the emitting frame is a large fraction
of the entire remaining stack. This is the case
[`testing-workflow`](../../.cursor/rules/testing-workflow.mdc) calls out explicitly: "a PlatformIO
`SUCCESS` with a few hundred bytes free is a RAM1 **FAIL** — the hex is not device-viable." The
board was **not** flashed.

**Attribution of the 4096-byte cost** (ELF section diff, not inference): `.data` +1024 and `.bss`
+3072. ITCM is unchanged (424796 + 1188 = 424860 + 1124 = 425984 = 13 x 32768), so this is data, not
code. Symbol-size comparison shows no existing symbol grew and only four new 4-byte counters appear,
so the growth is aggregate rather than one buffer.

**One confirmed contributor:** `SESSION_CAPTURE_INVESTIGATION_DIAG` format strings that are **not**
flash-placed. `LoopMaterialization.cpp` and `EditApply.cpp` define `LOOP_DIAG_FMT` / `EDIT_DIAG_FMT`
as `PSTR(literal)` on Arduino, but `LoopVisualCache.cpp` (4 literals) and
`LoopPendingNoteChange.cpp` (6 literals) have **no such macro**, so their `#CAP` format strings land
in RAM1 `.data`. That violates
[`Instrumentation-Session-Capture-Gating`](../../.cursor/rules/Instrumentation-Session-Capture-Gating.mdc):
"place instrumentation code in cold/flash sections … so RAM1/ITCM pressure does not grow from
debug-only paths." Ten literals at ~80–100 chars accounts for most of the `.data` delta; the `.bss`
+3072 is not yet attributed.

`cer_range` shares the single `SESSION_CAPTURE_INVESTIGATION_DIAG` gate with every other
investigation marker, so the marker Slice 0 needs could not be enabled independently.

#### Resolved — diag format strings moved to flash

`LOOP_VCACHE_DIAG_FMT` and `LOOP_NOTECHANGE_DIAG_FMT` added to `LoopVisualCache.cpp` (4 literals)
and `LoopPendingNoteChange.cpp` (6 literals), following the existing `LOOP_DIAG_FMT` /
`EDIT_DIAG_FMT` pattern — `PSTR(literal)` under `SESSION_CAPTURE && __IMXRT1062__`, passthrough
otherwise. No behavior change; instrumentation only.

| Env | RAM1 variables | free for locals | Verdict |
|---|---|---|---|
| `teensy41-capture-serial` | 93696 | 4608 | PASS (unchanged) |
| `teensy41-capture-serial-investigation-diag` | 93696 (was 97792) | **4608** (was 512) | **PASS** |

The full 4096 bytes came back — including the `.bss` +3072 that the symbol diff had not attributed,
so the literals were the entire cause. Flash absorbed them (`data` 78692 → 79796). Both envs now
report identical RAM1 variables, so the investigation build costs **no** DTCM over the default
capture build. `pio test -e native`: **1475/1475 pass**.

Architecture checkpoint: ownership **no**, transitions **no**, unbounded interval work **no** —
minimal patch, string placement only.

**Slice 0 is unblocked** and its recapture can now run on a device-viable hex.

### Ordering constraint — 2a must not ship ahead of Ring B

Repair 1 deliberately restores a path the 4b.4 guard had disabled for cost reasons, and Ring B
admission (Slice 3) is the mechanism intended to bound that path. Landing 2a into the runtime first
would knowingly create an intermediate regression: the invariant restored, the admission protection
absent.

**Constraint:** 2a may implement and land the coverage logic with native correctness tests, but its
**firmware is not shippable — and its device gate is not considered met — until Ring B admission
exists.** Root-cause sequencing is preserved (2a stays its own commit for its own root cause), while
the runtime never runs the restored path unprotected:

```
2a  coverage logic + native correctness       (commit; not shipped)
 ↓
3   Ring B admission                          (commit)
 ↓
2a  device activation and verification        (gate closes here)
```

The single exception: if 2a's device recapture demonstrates that the restored gather path is **not
exercised in the problematic runtime condition** — no `DisplayCaptureFullGather` growth and
`timing_max,DisplayResolveLiveCapture` inside the 4b.4-closed class (`141034` measured 8608 µs) —
then the intermediate state is safe on evidence and 2a may close before Slice 3. That must be shown,
not assumed.

---

## Pre-implementation review

### Ready

- RC1 owner is proven from `midi_input`/`clk`/`tracks`/`usbdev`/`usbdisp`/`usbclk` all reporting
  the same 344 ms single-call maximum, with `midi_gap` at 26 ms.
- RC1 convergence is proven from `PlaybackMergedMidiEventsRebuild=41` over 22 bar edges plus the
  `cer_win` / `playback_build` pairing at each gap.
- **Per-rebuild duration is now proven** — `153941` gives 367 ms for one
  `copyEffectiveCommittedEventsInRange`, 92% of it reconstruction inside the edit projection. This is
  a proven *observation*, **not** G1's input: two samples with identical row counts differ 5x
  (339 ms vs 69 ms), so representativeness is unestablished. Slice 1 establishes it; RC4 may change
  the figure by an order of magnitude before G1 consumes it.
- RC2 is proven end to end from the wrap log, `VCACHE` counts, `DISP` counts, the
  `deferredDerivedViewMaintenance` state gate, and the 4b.4 guard condition in tree.
- All owners named exist; no new abstraction is required.
- Timing constants verified in tree: `INTERNAL_PPQN=192`, `TICKS_PER_CLOCK=8`, `TICKS_PER_BAR=768`,
  `NUM_TRACKS=8`, `MAX_LOOPS_PER_TRACK=8`, `ClockManager` BPM clamp `[20, 300]`.
- Window geometry verified in tree: `kMergedMidiEventsGatherBars = 2` with
  `winStart = playhead - winLen/2`, so forward horizon is 768 ticks; reuse tolerates a
  `TICKS_PER_BAR / 2` backward margin.

### Resolved

| Topic | Decision |
|---|---|
| Mechanism | One round-robin, two rings (interval + idle) |
| Ring B scope | **Replace** `snapshotIdleRoundRobinTurn_`, `visualIdleCadenceCounter_`, and the post-stop persist lane — not coexist |
| New owner | None. Extend existing owners only |
| RC3 | Named, not bundled |
| Bound class | **No reclassification.** Rows 1 and 2 stay `Unbounded`; Ring A caps admission multiplicity, which is not a bound class. No fourth class introduced (DEC-043) |
| Ring A population | **Playback-relevant destinations whose window needs preparation** — a dynamic eligible set, not all enabled `(track, slot)` pairs. Invariant **A2** |
| Design envelope | 16 tracks x 2 active slots, ≤ 240 BPM, playback-relevant only. Absolute capacity (64 destinations, 300 BPM, pathological enablement) is **deferred** and yields capacity limits later |
| `prefetchTicks` | Derived from the lead **relationship** against the chosen horizon within the design envelope; **not** a musical fraction and **not** sized against extreme capacity. Gate G1 is envelope sizing, not worst-case characterization |
| Playback horizon | **Keep 2-bar content / 1-bar forward horizon** as the current candidate. Forward horizon is already 1 bar (window is playhead-centred); the lever is window **bias**, not size, and content size does not change in this phase |
| Arbitration term | `(N-1) x T_clock` is the **conservative** bound for continuously-due destinations, not the expected steady state. Self-staggering is verified in Slice 4, not assumed |
| Invariant A1 | Stated separately from its failure policy so the failure branch is not unreachable by definition |
| Owner-3 skip | **Non-priority, normative:** a skip consumes the cursor position and transfers no turn. It is a validity condition on the skipped owner's own work |
| 2a ordering | 2a lands with native correctness but is **not shippable before Slice 3**, unless device evidence shows the restored path is not exercised in the problematic condition |
| Missed prefetch | Existing immutable window stays selected; **never** a synchronous rebuild on the clock path; starvation is a counted architecture failure (invariant A1) |
| RC2 framing | One invariant **R** (representation coverage); the three repairs are its enforcement points |
| Repair 3 | Semantic-correctness gate, independent of the display gate |
| Owner 3 | Content-dependency eligibility rule, not a priority scheduler |
| Slice order | Measurement closure (0+1) → representation repair (2a/2b) → admission (3, 4) |

### Open before coding

1. ~~**1a — RECORDING and layered slots.**~~ **Resolved 2026-08-26 (`DEC-046` item 2): included.**
   Layered-slot playback during recording is intentional, so `TRACK_RECORDING` joins PLAYING and
   OVERDUBBING in `trackPlaybackEngineRunning`. The recorded loop stays excluded by lack of committed
   passes.
2. **Window bias.** Content size stays at 2 bars this phase. The open question is how far `winStart`
   may move forward, which requires knowing how much backward margin "LoopEnd launch + clock
   catch-up" actually needs. The reuse test's `TICKS_PER_BAR / 2` backward tolerance is evidence of
   the intent but not a derivation of the requirement.
3. ~~**OpenSpec phase scope (4b.1 / 4b.2).**~~ **Resolved 2026-08-26:** 4b.1 and 4b.2
   are closed under Ring B round-robin in
   [`immutable-loop-snapshot-passes/tasks.md`](../../openspec/changes/immutable-loop-snapshot-passes/tasks.md).
   Pass condition for 4b.1 is one owner per cursor turn (no true same-turn
   snapshot/persist stack). 4b.2 covers first-record start-after-bar-1 + live
   append under the five-owner cursor. Residual **4b.2c** (PLAYING/OVERDUBBING
   `snapshot_idle` after first published root vs `DEC-045` / [`233308`](../../captures/session_20260825_233308.log))
   stays an open investigation and does not reopen Ring B. **4b.3** remains open
   (post-stop `usbdisp`/`load_frame`/`late_on`; exclude `T0/S6` when scoring).
   Slice 4 / Ring A still needs separate OpenSpec confirmation.
**Resolved since round 2** — 4b.4 reopen sequencing: 2a lands with native correctness but is not
shippable before Ring B (§ *Ordering constraint*), with a stated evidence exception. No longer open.

**Resolved in the design session (round 4)** — playback relevance is derived and owned
(§ *Design session*), and queued-destination prewarm keeps its commit-boundary exemption.
**Item 1a resolved 2026-08-26**, so the playback-relevance question is fully closed.

**Withdrawn as a blocking item:** maximum destination count and the 300 BPM ceiling. Sizing against
64 destinations at 300 BPM is deferred to capacity documentation and does not gate this phase.

### Proceed?

**`DEC-046` is ACCEPTED (2026-08-26)** as architecture direction, and it explicitly does **not**
authorize firmware Slices 1–4. Gate state after round 4 and item 1a:

| Gate | State |
|---|---|
| RC1 attribution | **Closed** — `153941` + `160244` + `161834` + `162522`; all repeated `cer_edit` compatibility gathers map directly to `T0/S6`, with `T4/S0`, `T6/S0`, and `T2/S2` also observed |
| Ring A population — playback relevance | **Closed** — predicate derived, owner named, item 1a resolved (`RECORDING` included) |
| Queued-destination prewarm | Settled — commit-boundary exemption kept |
| Design envelope (16 x 2, ≤ 240 BPM) | Settled |
| 64 destinations / 300 BPM | Correctly deferred |
| 1-bar forward horizon, 2-bar content | Settled as the candidate |
| Window bias | Open — not blocking G1 at current `H_fwd` = 768; re-derive G1 if bias moves |
| Prefetch equation | Settled; arbitration term labelled conservative |
| A1 | Settled — invariant and policy separated |
| Ring B + owner-3 semantics | Settled |
| RC2 invariant R | Settled |
| 2a → 3 ordering | Settled — 2a not shippable before Ring B |
| RC3 separation | Settled — separate plan |
| G1 | **Satisfied** — `prefetchTicks` 272; `representativeRebuildWall` 358 µs; horizon unchanged |

**Slice 1 destination attribution is complete.** `161834` identifies the observed destinations, and
`162522` directly maps all repeated `cer_edit` compatibility gathers to `T0/S6`. Its `<count>` field
is rebuild-call count before one-shot emission, not distinct-destination multiplicity; no eligible-set
count is inferred from it. RC4 question 3 is answered from code: loaded restore does not publish a
`LoopSnapshot`, so `T0/S6` stays on the compatibility gather. `165024` rules out the measured
internal-first spill and nested-clock mechanisms. `cer_recon` is in tree to split internal-heap
`filt` from PSRAM-first `span`/`proj` and to record IntervalTimer ISR `itickus`; recapture
pending. `DEC-046` is recorded. The only remaining open admission item is item 2 (bias), which
affects `H_fwd` and therefore G1 but not the admission model.
**G1 is satisfied (2026-08-26).** Horizon stays playhead-centred (`H_fwd` = 768).
`representativeRebuildWall` = 358 µs from `202654` `T4/S0` `path=snapshot`.
`prefetchTicks` = **272**. `T0/S6` compatibility is excluded. Slice 4 / Ring A
firmware remains unauthorized. Publish-on-restore for `T0/S6` remains a separate
change.

## Implementation status — Ring B / RC2

**Closed (2026-08-26).** Slices 2a, 2b, and 3 are device-verified. Residual
**4b.1b** (indivisible `NoteUtils::reconstructDisplayNotes` in
`Loop::rebuildVisualCacheIdleSlice`) is not a Ring B/RC2 gate. It lives on
[`playback_window_build_owner_boundary_refinement.md`](playback_window_build_owner_boundary_refinement.md)
§ *4b.1b*. RC4 firmware repair, Ring A, and remaining snapshot consumer
migrations stay out of scope.

The implementation extends the existing `main.cpp` service order,
`Track::processDeferredIdleMaintenance`, `DisplayManager` overdub paint path,
and `Loop` visual/source-cache owners. The RC2 source-cache repair rejects an
unproven partial prior span cache, falls back to committed-pass reconstruction,
and reports the actual source origin in the existing diagnostics. Native
verification passed all 1476 tests.

Device PASS [`183452`](../../captures/session_20260826_183452.log): first wrap
reports `from=passes` before `VCACHE,stale_range`; `DISP` rises 286→466.
Reconfirmed [`185141`](../../captures/session_20260826_185141.log): `from=passes`;
`DISP` 271→437. Zero `srcafter,why=wrap,from=cache`. Dirty bars drain during
OVERDUBBING. First-overdub `midi_gap` 222.596 ms → 146.802 ms versus
[`175350`](../../captures/session_20260826_175350.log); later steady overdub
24.325 ms versus 366 ms. Remaining reconstruct timings are recorded on the
owner-boundary plan, not here.

## 4b.2c investigation — root boundary and pass cardinality

**Status:** Closed 2026-08-26. Capture
[`232633`](../../captures/session_20260826_232633.log) verifies the root
boundary with explicit root and queue telemetry; no DEC-045 violation was
reproduced.

### Confirmed boundary

`RECS,stage,publish,...,ok` is emitted by `Loop::commitCapturePass` after
`commitPendingCapturePass`. It confirms that the compatibility `LoopPass` was
committed; it is not the publication of the immutable `LoopSnapshot` root.
The root is stored later by `Loop::stepPendingSnapshotPublish` when the
session-exit queue is drained, the construction is complete, and the
unpublished candidate exists.

This distinction explains the large apparent post-publish volumes:

- [`183452`](../../captures/session_20260826_183452.log) reports the record
  pass `publish` stage at about 55.752 s. `snapshot_idle` then continues
  through PLAYING and the first OVERDUBBING window, ending with
  `pending=0` at about 80.059 s.
- [`202654`](../../captures/session_20260826_202654.log),
  [`213537`](../../captures/session_20260826_213537.log),
  [`214703`](../../captures/session_20260826_214703.log), and
  [`220551`](../../captures/session_20260826_220551.log) show the same
  shape: idle construction follows the pass-publish marker and terminates
  at the final `pending=0` turn.

The `pending=0` line is emitted after `stepPendingSnapshotPublish` returns;
on the session-exit completion turn that method has already stored the
candidate root. No later `snapshot_idle` steps appear in PLAYING or
OVERDUBBING in these runs. Therefore the observed 1400/679-line volumes are
the first-root construction interval, not successor construction after an
already published root. This is consistent with the DEC-045 guard in
`MidiServiceDrain::allowSnapshotIdleStep`.

The telemetry-enabled capture [`232633`](../../captures/session_20260826_232633.log)
provides the decisive boundary. It contains 634 `snapshot_idle` rows, with
343 in RECORDING, 137 in PLAYING, and 154 in OVERDUBBING. Before root
publication, the PLAYING/OVERDUBBING rows consistently report
`root=0x0`, `root_rev=0`, `playback_rev=1`, `construction=1`,
`queued=0`, `applied=0`, `exit=1`, and `inflight=1`. This identifies one
in-flight first-record construction (pass 1), with no queued or applied
successor passes. The final row reports
`pending=0,root=0x7030ad1c,root_rev=1,playback_rev=1,queued=0,applied=0,
construction=0,exit=0,inflight=0`; no later PLAYING or OVERDUBBING
`snapshot_idle` row occurs. The apparent post-publish volume was therefore
the single first-root construction interval, not stacked overdub
construction. `DEC-045` remains unchanged.

### First resolve timing report — 2026-08-26

The first report correlates the cumulative
`DisplayResolveLiveCaptureTime` summary with the nearest recorded stop
`phase`/`playbackTick` anchor. The resolve timing is accumulated across the
capture; it is not the duration of a single call at that tick.

- [`214703`](../../captures/session_20260826_214703.log): tick `256`;
  total `41.587 ms`, 499 resolves, average `83.3 µs`, maximum `2.618 ms`.
- [`220551`](../../captures/session_20260826_220551.log): tick `5368`;
  total `30.751 ms`, 422 resolves, average `72.9 µs`, maximum `1.643 ms`.
- [`220915`](../../captures/session_20260826_220915.log): tick `8`;
  total `11.169 ms`, 244 resolves, average `45.8 µs`, maximum `92 µs`.
- [`232633`](../../captures/session_20260826_232633.log): tick `320`;
  total `14.631 ms`, 277 resolves, average `52.8 µs`, maximum `110 µs`.
  Its `COORD` marker also reports `storage=320`, `proj=320`, and
  `display=320`.

The current capture data does not record `currentTick` for each individual
resolve call. Per-call tick attribution requires additional
`SESSION_CAPTURE`-gated instrumentation.

### Pass separation versus diagnostic aggregation

The implementation does separate committed overdub passes:

- `TrackOverdubWrap::maybeCommitOverdubWrap` calls
  `Loop::pushOverdubSessionPass` for each committed wrap pass.
- `Loop::enqueueLiveOverdubPasses` adds each untracked `PassId` separately to
  `PendingSnapshotPublishBox::queuedPassIds`.
- `startNextPendingConstruction` removes one `PassId` and creates one
  `LoopSnapshotConstruction` for that pass; completed IDs are retained in
  `appliedPassIds`.

The pending work is nevertheless grouped under one
`PendingSnapshotPublishBox` for the active overdub session. The current
`snapshot_idle` line logs only `hasPendingSnapshotPublish()`, which is a
boolean. It does not report `queuedPassIds`, `appliedPassIds`,
`constructionPassId`, `sessionExitQueued`, or the root identity. Consequently
`pending=1` cannot distinguish one in-flight construction from several
sequentially queued pass constructions. This is an observability limitation,
not evidence that distinct overdub pass content was merged into one pass.

### Synthetic note-off finding

The separate overdub-stop capture investigation is confirmed by
[`214703`](../../captures/session_20260826_214703.log):
`finalizePendingNotes` reported no pending notes and no capture note-offs at
stop, followed by `CLN,overdub,wrap_synth,3`. Stored verification then showed
existing head `NoteOff` events such as `F@112` together with synthetic
`F@6143` events for the same notes. `6143` is `loopLengthTicks - 1` for the
captured loop. The duplicate boundary terminations explain the observed
previous/stop bar disruption.

The synthetic events are inserted by
`LoopStopFinalize::finalizeWrapWindowOnStore` through
`Loop::finalizeCaptureWrapWindowAtStop`; `silenceTrackMidiOutput` is separate
and sends CC123 rather than storing note-offs. The remaining ownership question
is why those tail notes are still unresolved when the wrap window is sealed;
that is tracked by the existing overdub wrap note-off plan and is not folded
into 4b.2c.

### Closure evidence

Keep the DEC-045 policy unchanged. The existing `Loop` snapshot publication
owner emits a scoped capture-only diagnostic with the root identity,
published/playback revisions, and `PendingSnapshotPublishBox` state
(`queued`, `applied`, current `constructionPassId`, `sessionExitQueued`, and
`inflight`) at each `snapshot_idle` line. The telemetry is
`SESSION_CAPTURE`-gated and does not alter construction or scheduling
behavior. Capture `232633` verified the boundary, so no policy amendment or
additional pass aggregation change is required for 4b.2c.

## 4b.3 profiler instrumentation

**Status:** Instrumented 2026-08-26; device recapture required.

`TrackManager::updateAllTracks` now records one maximum and over-budget count
for each configured track iteration in the existing five-second runtime timing
window. The capture-only marker is:

```text
#CAP,<micros>,DIAG,track_update,<track>,<maxUs>,<overCount>
```

The measurement covers the existing per-track body, including playback and
enabled-slot work. The aggregate `tracks` marker remains unchanged, so the
per-track rows can be compared directly with the approximately 98 ms
`usbclk`/`tracks` stall without changing the clock dispatch or track order.
Measurement calls compile out when runtime timing is disabled. Per-track
accumulators live in DMAMEM and the capture format string is `PSTR`, so
`teensy41-capture-serial` stays at RAM1 locals **4640**.

Device capture [`000834`](../../captures/session_20260827_000834.log)
emits the `track_update` rows. Record 13.2–22.3 s, overdub 23.4–32.7 s
and 33.7–42.5 s, stop at 46.6 s. `ODUB,stop,display` is 4.762 ms /
4.705 ms.

| Window s | `tracks` µs | Dominant `track_update` | `usbclk` µs | `usbdisp` µs |
|---|---:|---|---:|---:|
| 20.889 | 5032 | T6 4950 | 5049 | 5260 |
| 25.889 | 6581 | T6 5450 (over 3); T4 3706 | 6596 | 7610 |
| 30.889 | 7100 | T6 5959 (over 2) | 7116 | 7117 |
| 35.889 | 7449 | T4 7289 (over 1); T6 6046 (over 3) | 7472 | 7595 |
| 40.889 | 7385 | T6 6240 (over 2); T4 4035 | 7401 | 7809 |
| 45.889 | 11284 | T4 11114 (over 3); T6 6520 (over 3) | 11305 | 11305 |
| 50.890 STOPPED | 2145 | T4 2029 | 2161 | **116743** (`usbtrans` 116729) |

`playback_build` maxima in the same run: T6/S0 `compatibility` 5953 µs
(21 rebuilds) and T4/S0 `compatibility` 7981 µs (3 rebuilds). T0–T3 and
T5/T7 stay under 50 µs after the first live window. The 11.3 ms
`usbclk`/`tracks` peak is T4. The 116.7 ms post-stop `usbdisp` is
`usbtrans`, not `updateAllTracks`.

## Future RC — overdub overlap target semantics

**Status:** Deferred; not part of Ring B/RC2.

The current overdub path has two distinct identities:

- the incoming note belongs to the live `Capture` for the current overdub;
- overlap `Shorten`/`Hide` changes target `NoteId`s selected from
  `overdubSourceViewNotes_`, which represents already committed loop content,
  including earlier overdub passes.

`Track::recordMidiEvents` invokes
`Loop::accumulatePendingNoteChangesForIncomingNote` when the current capture
NoteOff closes an incoming note. The resolver then creates pending transforms
for overlapping source notes, and
`Loop::sealPendingNoteChangesToEditPasses` persists those transforms as
companion edit passes. Therefore a current overdub NoteOff can intentionally
shorten or hide notes from a previous overdub. Ring B scheduling and RC2
coverage enforcement do not change this target-selection policy; fixing RC2
will only make the source representation more complete.

This future RC must first decide the intended overlap policy—whether a new
overdub should modify overlapping committed notes, modify only the current
capture, or use another explicit replacement rule—before changing the
resolver. The investigation must prove target `NoteId`s and resulting
pass content with a multi-overdub same-pitch fixture, then add native
coverage and a device check if the selected policy is not host-complete.
Keep this RC separate from RC2 so source-cache coverage cannot be
misattributed as the cause of overlap-target behavior.
