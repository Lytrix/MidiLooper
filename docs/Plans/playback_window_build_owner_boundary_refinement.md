# Playback window build — Owner-Boundary Gate (ExecutionBounds)

**Status:** **Slice 0 shipped.** Next authorized coding: **Slice 1A only** (cooperative prepare experiment). Stop after the 1A measurement gate. Slices 1B–5 are roadmap context, not permission.
**Kind:** refinement under the runtime scheduling Owner-Boundary Gate
**Parent roadmap:** [`runtime_scheduling_owner_boundary_admission_refinement.md`](runtime_scheduling_owner_boundary_admission_refinement.md) (O–T–R–C–A–P)
**Bound authority:** [`ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md) + [DEC-043](../DECISION_LOG.md#dec-043-execution-bound-class-is-a-gated-architectural-property)
**Derived-view direction:** absorbs [`loop_content_resolution_span_consume_refinement.md`](loop_content_resolution_span_consume_refinement.md)
**Baseline evidence:** [`session_20260822_174758.log`](../../captures/session_20260822_174758.log) on `feature/overdub-stop-lag-stage-c`

**Do not add now:** playback-window consumption; further 3 ms optimization; paired-companion snapshot; 2-bar horizon change; `RuntimeWorkBudget::admit`; missing-prep policy; optimizing the prepare algorithm.

---

## Unit of work (reframed)

The unit of work is **not** “playback-window optimization.”

> **Can an existing expensive preparation algorithm become a cooperative job without changing its semantics?**

That is higher leverage than chasing remaining 3 ms / 100 ms numbers.

```text
snapshot question (abandoned):
  How can we keep an expensive result around so we don't redo the work?

cooperative question (this plan):
  How can the expensive work itself become compatible with the runtime's execution model?
```

If 1A succeeds, the same primitive can serve many future expensive operations. If 1A fails, that failure is valuable: it names where the preparation algorithm needs structural decomposition — not another cache.

---

## Reusable engineering principle (carry forward)

> **Before optimizing an expensive operation, first ask whether it can be made cooperative: preserve its semantics, expose persistent progress state, divide it into bounded work units, and make each yield/resume continue from the last completed unit without repeating substantial work.**

**Prompt form for another coding model:**

> Take this existing algorithm and make its execution genuinely resumable — persistent state, bounded work units, explicit yield points, and continuation without repeating completed work — **while preserving its semantics.**

> **Do not optimize or redesign the algorithm in this step.** First expose its existing execution as a resumable state machine and instrument it so we can measure the resulting work units.

“While preserving its semantics” prevents “make resumable” from becoming permission to redesign. Land this principle in Slice 0 (plan + short pointer from scheduling roadmap / agent context as appropriate).

---

## Falsification ladder (execute in this order only)

**Do not implement Slices 1B–5 until the 1A measure gate passes.**

```text
0   docs / cleanup / pin boundary
        │
1A  EXPERIMENT — existing prepare → cooperative job
        │         preserve semantics
        │         no optimize / no new cache / no missing-prep policy
        │         state → bounded work → checkpoint → yield → resume
        │
    ★ STOP AND MEASURE ★
        │
        ├── FAIL → decompose prepare algorithm (new investigation)
        │          do NOT start 1B / 2 / 3 / 4
        │
        └── PASS → authorize 1B
                     │
                    1B  shape already-bounded units with T_maint,target
                     │
                    2   horizon-bounded consumer
                     │
                    3   mutation continuity (conservative — snapshot lesson)
                     │
                    4   wire interval path — NO applyNoteEditPassSequence fallback
                     │
                    5   ExecutionBounds reclassify
```

### Slice 0 — docs / cleanup (cheap)

- Land this plan under `docs/Plans/`; record cooperative-job principle.
- Delete dead companion-projection scaffolding (removes snapshot temptation).
- Pin R vs C/A; `CURRENT_WORK` names **Slice 0 + 1A only** — not the rest of the plan as “now implementing.”
- **Do not** put the coding-model resumability task in Slice 0. That task lives only under Slice 1A.

### Slice 1A — Resumability experiment (highest value; falsification experiment)

**This is not a normal feature slice.** It is a falsification experiment. The coding task below is the **first and only implementation task authorized by 1A**.

#### Implementation task

> Take the existing `LoopContentResolution` preparation algorithm and make its
> execution genuinely resumable — persistent state, bounded work units,
> explicit yield points, and continuation without repeating completed work —
> while preserving its semantics.
>
> Do not optimize or redesign the algorithm in this slice.
> Do not introduce a new cache or consumer representation.
> Do not add the 60% operating-target timer.
> Do not implement playback-window consumption.
> Instrument the resulting work units so Slice 1A can be measured.
>
> If an existing operation cannot be made resumable without structurally
> decomposing that operation, stop at that boundary and report it as a
> decomposition requirement. Do not disguise an indivisible operation as
> time-sliced by placing a timer around it.

#### Required implementation evidence

1. Persistent preparation state survives yield.
2. Each yield occurs at a real work-unit boundary.
3. Resume continues from the last completed unit.
4. Completed work is not reconstructed, resorted, replayed, or rescanned.
5. Progress is monotonic.
6. Each work unit has a measured uninterrupted duration.
7. F1–F7 are either converted into resumable work units or explicitly identified as structural decomposition blockers.
8. Native/device oracle equivalence remains unchanged.

#### Authorized stopping point

After the first complete 1A measurement run, **STOP**.  
Do not implement Slice 1B or any later slice from the same task.

#### Slice 1A agent handoff

The coding agent is authorized to implement **only Slice 1A** (after Slice 0 docs/cleanup if not already done).

The agent must treat this as an **experiment**, not as feature delivery.

Success means demonstrating that the existing preparation algorithm can be represented as:

```text
persistent state
    → bounded work unit
    → checkpoint
    → yield
    → resume
```

without semantic change or repetition of substantial completed work.

**Failure is a valid result.**

If any preparation phase requires algorithmic decomposition before it can become genuinely resumable, **stop and report that phase**. Do not proceed to Slice 1B, playback-window consumption, mutation continuity, or interval-path wiring.

After measurement, **return control to the plan/review process**.

**Authorized work for any coding session opened from this plan:** Slice 0 and Slice 1A only. Stop after the 1A measurement gate. Seeing Slices 2–5 in this document is **not** permission to implement them.

| Do | Do not |
|---|---|
| Preserve prepare semantics / oracle equivalence | Optimize the algorithm |
| Persistent progress state; checkpoint cursors | Introduce a new cache |
| Subdivide F1–F7 into already-bounded work units **or** name blockers | Solve missing-preparation policy |
| Instrument `prepare,slice,*` | Enforce 60% timer (that is 1B) |
| Prove cooperative execution | Redesign phases “for speed” |
| Stop after measure | Continue into 1B/2/3/4 “while you’re here” |

**Operational “substantial”:** resumed turn may do O(1) bookkeeping; must not reconstruct, resort, replay, or rescan already-completed preparation.

### ★ Measure gate (mandatory)

After 1A device/native evidence, **stop**. Review in plan/review process — not inside the same coding task:

- Do work units have a known max uninterrupted cost?
- Does resume continue from the last completed unit?
- Are F1–F7 gone or replaced by bounded units / explicit blockers?
- Does normal STOPPED prepare avoid F8-style progress wipe?

**Pass** → authorize Slice 1B in CURRENT_WORK (new session).  
**Fail** → open preparation-algorithm decomposition investigation; **do not** start 1B/2/3/4.

### Slice 1B — only after 1A gate

60%×`T_clock` operating target becomes meaningful only once real work units exist. Before that, a timer is theater. Stop before the **next already-bounded** work unit would exceed the target; record `T_idle_gap`.

### Slices 2–5 — later (blocked)

- **2:** horizon-bounded span emit (not full-loop intermediate; not `tryResolvePreparedWindow` for interval).  
- **3:** mutation continuity — especially conservative; snapshot work showed derived lifecycle vs actual use.  
- **4:** wire consume; **no `applyNoteEditPassSequence` fallback** (else owner boundary did not move). Temporary defer ≠ C policy.  
- **5:** reclassify `#2` only if exit criteria; `#7` stays Unbounded, unreachable from interval path.

---

## Falsification (code-backed — pin before Slice 1A)

Attempt to falsify: resumable preparation, fixed-horizon interval consume, and “interval path never reaches unbounded work.” **Current firmware falsifies all three.** The architecture remains the right *target*; these items must be closed as Slice 1A scope, explicit out-of-scope residuals, or plan gaps — not assumed already true.

### A. Preparation is not cooperative today

Raising event grain or adding a timer **does not** make these slices resumable. They are indivisible whole-structure operations inside `DeviceGateSession::runOneSlice`:

| ID | Site | Why not resumable |
|---|---|---|
| F1 | End of `IndexCommit` | One slice: `sortAndUniqueByNoteId` / `sortTickEventEntriesByTick` on **entire** accumulated vectors |
| F2 | `appendCapturePassChunk` | Up to **256** events (`CHUNK_CAPACITY`) per slice, not 8 |
| F3 | `RebuildPrepare` | One slice: `applyNoteEditPassSequence` on **full** `rebuildEvents` when companion rows exist |
| F4 | `Phase::Window` | One slice: `resolveWindow` over up to 16 bars (not 8-event grain) |
| F5 | Checkpoint fill | One checkpoint/slice but walks **all** `spans` → O(spans) |
| F6 | RebuildSpans finish | `finishCanonicalSpansFromMidi` / `dedupeProjectedDisplayNotes` on full vectors |
| F7 | Reserves | First slice of a range `reserve`s remaining **whole** notes/events; `presentAt.resize(count)` full |

**Progress loss (repeat completed work):**

| ID | Site | Effect |
|---|---|---|
| F8 | `deviceGateReset` | NOTE_EDIT active, length mismatch, dirty+length `Reset` policy — wipes cursors and storage |
| F9 | Dirty same length | `Skip` — prepare stalls (`163135` `skip,dirty`) |
| F10 | Queue requires `!visualCacheDirty` | Cannot even begin while display dirty |

**`publishPreparedOverdubPass` is not O(new pass):** full `byNoteId` / `channelByNoteId` / cumulative `delta` sorts; `std::merge` of **all** spanBoundaries; new spans × all `presentAt` slots; companion project walks all spans. Slice 3 must **measure** this — do not label it structurally bounded from the API name.

**Prepare never advances while PLAYING/OVERDUBBING** (`processDeferredIdleMaintenance` gate). Incomplete prepare → `publishPreparedOverdubPass` no-ops → consumers fall back to full materialize / companion replay.

**Slice 1A implication:** First prove or **subdivide** F1–F7 into already-bounded work units. A wall-clock stop around an indivisible full sort fails the plan’s own yield-point rule.

### B. Fixed-horizon interval claim is invalid today

| ID | Site | Why |
|---|---|---|
| F11 | Long-loop gather | `copyEffectiveCommittedEventsInRange` still runs `applyNoteEditPassSequence` over **all** Active companion rows (cost ∝ EditPass count, not 2 bars) |
| F12 | Short loops ≤16 bars | `ensurePlaybackMergedMidiEventsBuilt` uses **full** `gatherCommittedEventsForDerivedView` / `materializeToEventVector` — **never** the 2-bar window. HITL-sized loops hit this path |
| F13 | `tryResolvePreparedWindow` | Still `emitApplyEditsPerCapturePass` → companion replay. Wiring Slice 4 to this API **reintroduces** F11 |
| F14 | Post-gather | `rebuildPlaybackOrder` always sorts `mergedEvents` after dirty build (scales with emitted size; full loop on short path) |
| F15 | Span emit drift | Replacing companion rows with a walk of **all** spans still falsifies “bounded by horizon, not loop length” |

**Slice 2/4 implication:** Span emit must filter by window **before** materializing events; short-loop playback path must be named in-scope or explicit residual. Do not wire `tryResolvePreparedWindow` as the interval consume API.

### C. Interval path still reaches unbounded work (direct + indirect)

| ID | Path | Unbounded work |
|---|---|---|
| F16 | Today long gather | F11 companion replay |
| F17 | Today short gather | Full-loop materialize + edit apply |
| F18 | After ensure | `rebuildPlaybackOrder` |
| F19 | OVERDUB wrap same tick | `catchUpOverdubWrapPlaybackLedger` → `commitOverdubWrapAtSessionStart` (seal, `rebuildOverdubSourceView` / possible full span rebuild) → `ensurePlaybackMergedMidiEventsBuilt` again → pass replay → `rebuildPlaybackOrder` |
| F20 | NOTE_EDIT | Full `sessionMidiEvents()` assign into `mergedEvents` |
| F21 | Indirect | invalidate / `++playbackRevision` → next clock → ensure with `includeActiveEditRows=true` → F11/F17 |
| F22 | `tryCopyCompanionProjectedWindow` | Called but dead (no builder) — not a save; silence is not a bound |

**Even after careful span emit for long-loop gather only**, residuals remain: F12 short-loop, F14 order rebuild, F19 wrap stack, F20 NOTE_EDIT. Owner-Boundary exit for “entire `#2`” is **false** until those are scoped or bounded.

### D. Plan-gap traps (implementer mistakes that revive failure)

1. Timer around indivisible full sort → fake “time slicing.”  
2. Wire gather to `tryResolvePreparedWindow` instead of horizon-bounded span emit.  
3. Call `startPreparation()` from the interval path when consumable is false.  
4. Treat Slice 4 temporary defer as C-stage missing-prep policy.  
5. Reclassify `#2` Structurally bounded while short-loop full gather and wrap stack still run on the interval path.  
6. Assume `publishPreparedOverdubPass` is O(new) without measurement.

### E. What survives falsification

The **direction** is not falsified: resumable prepare off the interval path + horizon-bounded consume + no historical replay on the interval path is still the right R-stage target. What is falsified is any claim that **today’s** LCR gate or **today’s** `ensurePlaybackMergedMidiEventsBuilt` already satisfy those properties, or that Slice 1B alone (60% timer) delivers Slice 1A.

**Next proof (unchanged, now sharper):** Can the indivisible phases (F1–F7) be split into work units whose max uninterrupted cost is already bounded, with checkpoints that survive yield without F8-style resets during normal STOPPED prepare?

---

## Primary architectural outcome

> **Every preparation operation required by this playback-window path must have an explicit resumable work state and can yield without restarting or repeating substantial completed work.**

(Narrower than “every preparation owner in LoopContentResolution” — only operations on this path.)

**Not the goal:** make `playback_build` fast in isolation.  
**Goal:** make expensive preparation genuinely resumable, then remove that preparation from the MIDI interval path.

```text
expensive operation
       │
       ▼
   resumable state
       │
       ├── do bounded work unit
       ├── checkpoint progress
       └── yield
             │
             ▼
        next idle turn
             │
             └── resume (no restart)
```

Three independent execution properties (do not conflate):

```text
                 EXECUTION
                    │
       ┌────────────┼────────────┐
       ▼            ▼            ▼
   slice bound   resumability   opportunity cadence
   "how long"    "can resume"   "how often" (T_idle_gap)
```

> **R proves bounded resumability.** C/A must establish whether sufficient maintenance opportunity exists to make wall-time progress acceptable. `T_maint,target` is not the entire scheduling model.

A three-minute `T_prepare_total` can be healthy (many small resumable turns). A 100 ms non-preemptible slice is not.

---

## Main interval-path invariant

```text
MIDI interval path:

prepared representation available
    → bounded consume

prepared representation unavailable
    → bounded/cheap alternative OR defer (temporary; see Slice 4 / C)

never
    → historical companion replay
```

> **There is no "normal fallback" to `applyNoteEditPassSequence` on this path.**

**Observation only — no initiation:**

> The interval consumer may **observe** capability state. It must **not** initiate preparation, repair, invalidation recovery, or historical resolution.

Forbidden:

```text
interval → discover unavailable → startPreparation() → return
```

**Missing preparation is not solved by R:**

> R establishes resumable preparation and removes historical replay from the interval path. **R does not establish the policy for an interval arriving before preparation is available.** That policy is a **C-stage scheduling contract**. Slice 4 may use an instrumented temporary defer; that is not final transport behavior.

```text
R  ├── resumable preparation
   ├── yield points
   ├── remove historical replay from interval path
   └── bounded prepared consumption

C  ├── missing-preparation behavior
   ├── MIG / opportunity model (T_idle_gap)
   └── demand vs preparation progress

A  └── interval reservation / admission only after C
```

---

## Final architecture (pursue this)

```text
                 authoritative history
                         │
                         ▼
              resumable preparation
                         │
               bounded idle slices
                         │
                prepared LCR state
                         │
              capability observation
                         │
                         ▼
              playback-window consume
                         │
                 fixed 2-bar horizon
                         │
                         ▼
                   MIDI interval
```

```text
                 MIDI interval
                       │
             ┌─────────┴─────────┐
             ▼                   ▼
        prepared consume       defer /
          (bounded)           cheap path
             │
             X
             │
     historical replay
     NEVER crosses boundary
```

**No arrow from MIDI interval into preparation or repair.**

---

## Consumer capability

| Avoid | Prefer |
|---|---|
| `preparedWindowReady` | `preparedPlaybackWindowConsumable` |

> LCR can satisfy the playback-window query without historical companion replay.

---

## Locked optimization rule

> Do not introduce another representation whose primary purpose is to make the interval path fast. Improve the authoritative derived representation or its execution schedule first.

> Prepared data is never authoritative. Inconsistent → admitted patch **or** unavailable + STOPPED resumable reprepare. **Playback never repairs.**

---

## Operating target (not “budget”, not admission)

Until `T_RT,reserve` is measured:

```text
T_clock(BPM)           = 60 / (BPM × 24)
T_maint,target         ≈ 60% × T_clock     // experimental PROXY only
T_maint,target (ideal) = T_clock − T_RT,reserve − T_jitter
T_hard,investigate     = 25 ms             // legacy investigation guardrail
```

> Until `T_RT,reserve` is measured, **60% of `T_clock` is the operating target used to shape maintenance turns.** It is **not** an admission guarantee and **not** a claim that 60% of every MIDI interval is available for maintenance (MIDI I/O, USB, ISR, clock bookkeeping, jitter, other main-loop work still exist).

| Tempo | T_clock | experimental target |
|---|---|---|
| 120 BPM | 20.83 ms | 12.50 ms |
| 170 BPM | 14.71 ms | 8.82 ms |

### Metrics

| Metric | Meaning |
|---|---|
| `T_prepare_slice` | One uninterrupted prepare turn |
| `T_prepare_total` | Wall clock to finish preparation (may be minutes) |
| `T_playback_window` | Interval-path window-build duration |
| `T_idle_gap` | Time between eligible maintenance opportunities |

**Operating-target language:** Typical and **p95** of `T_prepare_slice` should remain within `T_maint,target`. Systematic excess is a regression even below 25 ms. Any slice > 25 ms → investigate. Do not treat 60% as a formal hard deadline.

**Playback:** No companion-history replay. ~3 ms class = **regression indicator**, not architectural requirement. Known bounded work is the requirement.

---

## Slice 1A — Resumability (“substantial” defined)

**Authorized only via the Slice 1A Resumability experiment + agent handoff above.** Detail below supports that single coding task; it does not authorize later slices.

A resumed turn may perform **O(1) boundary bookkeeping and bounded re-entry work**, but must **not** reconstruct, resort, replay, or rescan the already-completed portion of the preparation.

```text
BAD                         GOOD
turn 1: 0..499              turn 1: 0..499; checkpoint cursor
yield                       yield
turn 2: reconstruct;        turn 2: resume at 500; process 500..999
        process 0..999
```

Prove: state survives yield; progress monotonic; no large op straddles yield boundary; no repeat of completed work.

**DIAG:** `prepare,slice,{start,end,elapsed,work_units,resume_state}` (+ `T_idle_gap`).

No new tempo-target enforcement in 1A.

---

## Slice 1B — Operating-target shaping

> The implementation must expose yield points at work-state boundaries whose **maximum uninterrupted work is already bounded**. The timer determines **how many** such work units may be performed; it does **not** make an indivisible operation preemptible.

> Perform bounded work units and **stop before the next work unit would exceed** the operating target.

**Not** “yield when the turn would exceed the target” after an indivisible 20 ms call. **Not:**

```cpp
while (...) {
  expensiveThing();           // 20 ms
  if (elapsed >= target) break;
}
```

That is not time-sliced.

Record session BPM and `T_idle_gap`. RAM/`SESSION_CAPTURE` = separate prerequisite proof, not mixed into 1A/1B semantics.

---

## Slice 2 — Semantic emission + fixed horizon

> For any requested playback window fully covered by prepared `StateCheckpoints`, span emission produces the same effective MIDI event sequence as the authoritative full-history resolver.

Oracle matrix: Hide/Shorten/Delete/Length; repeated ops; overlaps; same-tick; on/off order; window start/end **inside** a span; wrap; zero-length/deleted; multi channel/pitch.

```text
prepared spans → requested 2-bar interval → bounded traversal → SessionMidiEventVec
```

**Forbidden:** spans → reconstruct full loop → filter 2 bars.

> **The amount of authoritative history traversed by interval consumption must be bounded by the fixed playback horizon, not by total loop length or total EditPass count.**

Demonstrate fixed-horizon — do not infer it from the word “span.” Replacing 1694 companion rows with a loop-sized span walk is still a clock-path failure.

---

## Slice 3 — Mutation continuity (anti-staleness)

> **No interval-path consumer may depend on a prepared representation until its mutation lifecycle has been demonstrated across overdub → undo → wrap.**

Admitted paths (`publishPreparedOverdubPass`, `setPrepared*`, `eraseDisabledSounding`, `restoreDisabledCompanions`) are **not** assumed structurally bounded — **measure** in Slice 3.

**Deliverable table (fill with measured costs):**

| Mutation | Prepared state action | Cost | Can run where |
|---|---|---|---|
| append overdub / wrap seal | incremental publish | measured | idle |
| local capture undo | patch | measured | idle |
| local edit undo | patch | measured | idle |
| snapshot restore | invalidate | — | STOPPED reprepare |
| loop-length change | invalidate | — | STOPPED |
| geometry shift | invalidate | — | STOPPED |
| reclaim / identity erase | invalidate | — | STOPPED |

Hard gate before Slice 4.

---

## Slice 4 — Wire interval consume (architectural prohibition + temporary defer)

- Consumable → bounded span consume  
- Not consumable → instrumented temporary defer / cheap path  
- **Never** `applyNoteEditPassSequence`  
- Observe capability only — **do not** start preparation  

> Slice 4 establishes the architectural prohibition and uses an instrumented temporary defer behavior; **it does not define the final transport behavior for a missing prepared window.** C answers: what if the window is needed NOW but preparation is not ready?

**DIAG / counts (every window build):**

```text
preparedPlaybackWindowConsumable = true|false
window_build,prepared
window_build,deferred
window_build,unexpected_unavailable
window_build,duration
```

> How often does the runtime encounter a window before preparation can satisfy it? That rate sets urgency for C-stage missing-prep policy (0/50k vs 3k/50k are different problems).

~3 ms class when prepared = regression indicator only.

---

## Slice 5 — Owner-Boundary exit

**#2** reclassify only if all hold:

1. No historical companion replay on interval path  
2. No full-loop materialization on interval path  
3. Fixed playback horizon (history walked bounded by horizon)  
4. Bounded max event count  
5. Bounded allocation behavior  
6. No unbounded work hidden in span emission  
7. Mutation lifecycle proven (Slice 3 table)  
8. Missing-prep does not invoke unbounded companion replay  

**#7** (`applyNoteEditPassSequence`): remain **Unbounded**. Valid for STOPPED rebuild, native oracle, non-critical consumers. Win:

```text
#7 remains Unbounded
       └── no longer reachable from #2's interval path
```

Goal is not “make every operation bounded.” Goal is: **unbounded operations must not cross an execution boundary where they are not allowed.**

---

## Slice 0 — Docs + scaffolding

**Status: shipped** (native **1429/1429**).

Landed plan under `docs/Plans/`; cooperative-job principle + roadmap/CURRENT_WORK pin; deleted companion-projection scaffolding (`companionProjectionEvents_`, rebuild/tryCopy/clear, unused `collectActiveCompanionRows`). Capability rename to `preparedPlaybackWindowConsumable` remains a later naming pin when readiness APIs are touched — not required for scaffolding delete.

---

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Execution bound change? | **YES** — #2 reachability; #7 stays Unbounded |

---

## Out of scope

- Interval reservation / Stage A / `RuntimeWorkBudget`
- Final missing-prep transport policy (C)
- Claiming 60% of every MIDI interval is available for maintenance
- Treating 60% as formal hard deadline before `T_RT,reserve`
- Treating ~3 ms as architectural requirement
- Assuming incremental mutation APIs are structurally bounded without measurement
- Expanding to “whole LCR subsystem resumable” without path evidence
- Second consumer cache / paired-companion revival / horizon size change
- Interval path calling `startPreparation()`
- Reclassifying #7 as bounded

---

## Verdict (pin)

**Approve this plan as a falsification ladder.** Coding sessions opened from it may do **Slice 0 and Slice 1A only**, then return to plan/review after measurement. Slices 2–5 in this document are roadmap context — **not** implementation permission.

Paired-companion snapshot revert remains correct.
