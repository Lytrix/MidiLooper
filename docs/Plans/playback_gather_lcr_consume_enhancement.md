# Playback gather LCR consume

**Status:** Architecture **re-ranked** 2026-08-17 around **per-event MIDI deadline correctness**. Problem A: prove whether today’s 2-bar fill causes late MIDI. Problem B: usable region (only if A shows late MIDI). Problem C: LCR of the **required** horizon (not automatically 2-bar LCR). **No firmware** until this file is in CURRENT_WORK § Now implementing.  
**Date:** 2026-08-17  
**Kind:** enhancement  
**Work identity:** this file. Separate from remaining `loop-content-resolution` 6.x, NOTE_EDIT hydrate, and LED lookup.  
**Authority:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype) amendment 2026-08-17; [Playback.md](../Authority/Architecture/Playback.md); [DerivedViews.md](../Authority/Architecture/DerivedViews.md) § Consumers; DEC-016 interval × representation; [runtime scheduling contract](runtime_scheduling_admission_model_architecture.md) (interval reservation is **not** authorized)  
**OpenSpec pointer:** `loop-content-resolution` task 6.3 is **moved** here — not remaining production-swap firmware on that change.  
**Does not authorize:** firmware; a new OpenSpec change; a new DEC; a new `Horizon` type / Manager / Session / FSM; a playback-scheduler redesign; interval reservation / `RuntimeWorkBudget`; a periodic JIT timer; using display refresh cadence as the JIT clock; a production `MIDI_GUARD` / lead-threshold policy; treating `midi_gap` or `playback_lead_us` as the acceptance criterion; **2-bar LCR consume as the default next step after Problem A**; **budget-driven advance as architecture before evidence**; wrapping **firmware placement** before Problem B; putting `resolveWindow` on `handleMidiInput` or the BAR `loop()` prefix; deleting `materializeToEventVector`; `resolveNotes` as the playback primitive; hydrate 4c; short-loop full gather swap; domain `PlaybackWindow`; incremental `mergedEvents` patch; choosing `PLAYBACK_WINDOW_MIN_BARS` / `MAX_BARS` / half-bar as production size; treating `playbackRevision` mismatch as horizon invalidation

**Working name:** **playback horizon** = the derived interval already stored on `PlaybackMergedMidiEvents` (`windowStartTick` / `windowLengthTicks` / `mergedEvents`). Today: **2-bar** long-loop fill. Size is an experimental variable, not a production policy until Problem B plus a user pin.

**Hypothesis (the experiment this plan is built around):** the current 2-bar window may be **too far ahead of the playhead**. Because it contains so much future material, a mutation can invalidate a large prepared region and cause a rebuild even though only a relatively small amount of future MIDI needs to be guaranteed. That excess work can occupy CPU long enough to lose a MIDI deadline opportunity and produce an **actual late MIDI event**.

The experiment must establish this chain, including the last arrow:

```text
2-bar horizon
      ↓
mutation
      ↓
unnecessarily large affected/prepared work
      ↓
CPU contention
      ↓
MIDI deadline opportunity lost
      ↓
actual late MIDI event     ← required to claim “2 bars causes MIDI lag”
```

That is **not** “the gather is too slow.” It is **not** “smaller is better.” Rebuilds without late MIDI are an **optimization** question, not a correctness proof.

**Hard acceptance:** for every MIDI event that should be emitted (note-on, note-off, and MIDI clock **scored independently**):

```text
lateness_us = actual sendMidiEvent() − scheduled_deadline
late_event_count == 0
max_lateness_us <= 0
```

`midi_gap` and `playback_lead_us` are **diagnostics**, not the pass criterion.

Do **not** make 2-bar LCR the next firmware after proving a 2-bar rebuild problem. Faster 2-bar rebuilds can hide a wrong interval. Sequence: **A prove the problem exists → B usable region (only if A shows late MIDI) → C LCR of that required horizon.** If A has late MIDI = 0, the lag hypothesis is **not demonstrated**; 2-bar LCR or stop is then an optimization choice, not a correctness fix.

---

## North star

**Hypothesis:** the current 2-bar window may be too far ahead of the playhead. A mutation can force a large rebuild of material MIDI does not need for seconds. That is only a MIDI-lag hypothesis if the chain ends in an **actual late send**.

```text
                  window size
                      │
                      ▼
mutation ──────► does it affect prepared window?
                      │
                      ▼
                 rebuild cost
                      │
                      ▼
                CPU contention
                      │
                      ▼
                MIDI deadline   ← hard gate
```

2-bar vs a smaller horizon:

```text
2-bar:  |----------------------------------------|
        ^                    ^
        playhead             far future
             mutation → rebuild all

JIT:    |----------|
        ^          ^
        playhead   prepared boundary
             mutation → only currently relevant future
```

**“Smaller is better” is not the hypothesis.** You are not looking for the fastest rebuild. You are looking for the **usable region**: MIDI remains provably on time while rebuild pressure falls.

```text
                    HARD TRUTH
                        │
                        ▼
             ┌─────────────────────┐
             │ MIDI deadline miss? │
             └──────────┬──────────┘
                        │
              ┌─────────┴─────────┐
             NO                  YES
              │                   │
       continue /             investigate
       optimization           cause (rebuild /
       decision               CPU / competing work)
```

If 2-bar shows rebuilds = 30, cost = 12 ms, **late MIDI = 0**, the hypothesis “2 bars causes MIDI lag” is **not demonstrated**. Smaller horizons are then optional. If late MIDI = 7 coincident with those rebuilds, Problem B (1 bar / 1/2 / 1/4, each with late MIDI = 0 mandatory) is justified.

```text
                    MIDI correctness
                         ▲
              ┌─────────────────────┐
              │     usable region   │
              └─────────────────────┘
        too small                 too large
        many refills              stale / rebuild
        MIDI collisions           expensive work
```

Example outcomes (all valid; user pin after numbers):

| 2-bar | smaller | Meaning |
|-------|---------|---------|
| late MIDI 3, rebuild 15 ms | late MIDI 0, rebuild 5 ms | Compelling correctness fix |
| late MIDI 0, rebuild 15 ms | late MIDI 0, rebuild 5 ms | 2-bar already musically safe; smaller is an optimization choice |
| — | 1/8-bar late MIDI 12 | Other cliff: too small |

Making the **same 2-bar rebuild cheaper with LCR** can look “fixed” without testing the hypothesis. **Do not take that path as the default after Problem A.**

Budget-driven “prepare what the gap allows” may **emerge** from Problem B measurements. It is **not** a scheduler architecture to implement before that evidence.

### Three problems (in order)

**Problem A — prove the problem exists.** Keep the actual 2-bar implementation. Mutation → rebuild? how much work? **did MIDI actually miss its deadline?** No LCR. No size change. Control (no mutation) vs mutation stress on the **same** deadline metric.

**Problem B — usable horizon.** **Only if A demonstrates late MIDI** (or the user pins an optimization experiment despite late MIDI = 0). Candidate lengths. Mandatory: late MIDI = 0. Then rebuild pressure, CPU, mutation visibility. Goal is the usable region, not the smallest.

**Problem C — LCR of the selected preparation.** Only after knowing what interval actually needs to be prepared. Same `mergedEvents` consumer; miss keeps gather.

`capture.store` remains an **authorized parallel playback stream**. Capture, edit, overdub, and loop switch remain test cases of the same consumer. They are not reasons to skip A/B.

### Properties

1. **Safe** — every required note-on, note-off, and MIDI clock is sent **on or before its scheduled deadline**. This is the hard gate. `midi_gap` is not the gate.
2. **Stable** — a content mutation does not imply a horizon rebuild. `revision bump ≠ horizon invalidation`. New MIDI is not, by itself, a rebuild.
3. **Measurable** — Stage 1–2 hooks on today’s 2-bar fill; no geometry change until Problem B plus user pin.
4. **Schedulable (measure, do not implement)** — rebuild CPU vs competing work (display) as **cause** of a deadline miss. No new scheduler, no interval reservation, no budget-driven firmware on A.
5. **Seam / wrap placement** — today’s fill is linear-clamped. Wrapping firmware is not bundled with A or with C.

Do **not** add incremental `mergedEvents` insert in this change. Live overdub already sends `capture.store` on the parallel stream without bumping `playbackRevision` on `appendCaptureEvent`.

### Two cliffs (both must be visible)

Too **large**: mutation intersects a long prepared interval → rebuild far more than the next MIDI lead needs → CPU contention → possible late MIDI; or stale prepared state (visibility).

Too **small**: refill so often that preparation itself collides with MIDI deadlines.

At 120 BPM, 2 bars is **~4 s** of musical lead versus a ~20.8 ms clock. That is why “too far ahead” is plausible. **Prepared lead is diagnostic only** — it does not prove lag. Proof is per-event `lateness_us`.

### Latency quantities (not one number)

The horizon is **not** itself the latency. Three parts:

```text
T0 mutation
 │
 ├── preparation latency ──► T1 authorized path contains event
 │
 ├── prepared lead ────────► Tdeadline (event’s MIDI deadline)
 │
 └── delivery latency ─────► T2 MIDI output
```

**T2 − T0 = preparation + waiting-to-consume + delivery.**

Prepared lead at T1 and “how much of the 2-bar extent is needed” are **diagnostics**. They must not become a proxy for correctness. Correctness is `lateness_us` on each send.

Loop switch is **launch-to-ready lead**, not mutation visibility. Score both; do not fold launch into `T2−T0`.

### Scheduling measurements (Problem A — diagnostics, not a new policy)

These classify **why** a deadline miss happened. They are **not** the acceptance criterion. The gate remains `lateness_us` per event.

Keep these scores to classify rebuild vs MIDI-needed lead. **Do not implement** a JIT driver, budget-driven advance, or lead-threshold policy during Problem A.

**Architectural pin (measurement only):** remaining CPU room vs remaining playback lead. MIDI deadline availability is the hard constraint. Display cadence is not a clock. Huge 2-bar `playback_lead_us` is expected and is **diagnostic only**; Problem A is **whether unnecessary rebuilds + rebuild cost produce late MIDI**, not “4 s feels large.”

Three quantities (instrumentation names, not production types):

| Name | Meaning |
|------|---------|
| `playback_lead_us` | How far the prepared horizon currently reaches relative to the next MIDI that must be produced |
| `deadline_lead_us` | Time remaining before the next **hard** MIDI deadline (clock or note-on/off) |
| `resolve_us` | Cost of the proposed step |

Then two independent remainders:
```text
deadline_lead_us - midi_guard_us - resolve_us  = remaining CPU margin
prepared_event_deadline - T1                   = remaining playback lead
```

A 15 ms resolve with 20 ms **remaining playback lead** is a JIT-sized-horizon example. On the **first path**, 2-bar lead is seconds; Problem A scores **remaining CPU margin vs 2–25 ms gather**, plus whether a mutation forced a rebuild. Do not treat huge `playback_lead_us` as proof that 2 bars is “too large.”

At 120 BPM MIDI clock ≈ 20.8 ms; at 240 BPM ≈ 10.4 ms. Observed work is **2–25 ms**. A 25 ms gather cannot hide inside one clock gap. That is the collision hypothesis for occasional lag — **not** a reason to shrink the musical window.

`midi_guard_us` is an **experimental analysis margin**, not a Config constant and not interval reservation. Do not start work known not to finish inside remaining CPU margin. Do not implement the decision tree below as firmware on the first path.

```text
                 playback lead
                      │
                      ▼
              ┌───────────────┐
              │ Is lead safe? │
              └───────┬───────┘
                  yes  │  no
             normal    │    urgent
             maintenance
                       ▼
              next hard MIDI deadline
                       │
              ┌────────┴────────┐
              │ enough CPU room │
              │ to start work?  │
              └───────┬─────────┘
                  yes  │  no
                       ▼
                 JIT work step
```

Decision tree only — not a FSM. **Do not implement thresholds** in this change.

**Do not put display in the primary driver.** ~30 ms is a refresh cadence, not a scheduling clock:

```text
             HARD
MIDI  ──────────────────────────

             SOFT
DISPLAY ────────┐       ┌───────
                │       │

             OPPORTUNISTIC
JIT       ────┐     ┌──────┐
              │     │      │
```

What matters is **display occupancy**, not that another frame is nominally due. 1.5 ms every 30 ms is a different problem from an occasional 15 ms block. Stage 3 scores `display_due_age` and `display_occupancy_us` as competing-consumer context, not as the JIT algorithm.

Distinguish existing telemetry from this driver:

| Quantity | Meaning | Not this |
|----------|---------|----------|
| MIDI Input Gap (`DIAG,midi_gap`) | Time between `handleMidiInput()` entries (observed **after** work) | Distribution of *available* gaps **before** work; `deadline_lead_us` |
| `deadline_lead_us` | Predicted time to next hard MIDI | Display refresh period |
| `playback_lead_us` | Horizon vs next MIDI that must be produced | Window length in bars |
| Display occupancy | CPU time actually blocked | `DISPLAY_UPDATE_INTERVAL` / ~30 ms period |

Eventual policy *shape* (budget-driven advance) may **emerge from Problem B**. It is not firmware on A and not a scheduler to design first.

### Invariant (one sentence)

**Until Problem B plus a user pin, the long-loop consumer keeps today’s 2-bar `PlaybackMergedMidiEvents` interval; every scored event must reach an authorized playback path before its MIDI deadline; MIDI send never constructs LCR; a mutation does not imply a rebuild. LCR prepares the horizon Problem B names (or 2-bar if A does not demonstrate late MIDI), not “faster 2-bar” as the default next step.**

```text
playhead
   │
   ├── already consumed
   │
   ├── 2-bar prepared interval   (PlaybackMergedMidiEvents)
   │
   └── authoritative stores

scheduler:  stores → horizon  (existing admission / idle)
forbidden:  horizon overrun by playhead
6.0:        no LCR construct on handleMidiInput / play fill miss
```

This is a **readiness predicate / scheduling decision**, not a lifecycle. No `PlaybackHorizonSession`, no `HorizonState::PREPARING`.

```text
authoritative stores
       ↓
is required interval prepared?
       ↓
yes ─────────→ consume
       │
       no
       ↓
can resolve now?
       │
   yes │ no
       ↓  ↓
    prepare  gather
```

### Horizon readiness (concrete predicate)

Prepared horizon is **usable** iff all of:

- revision/state is compatible (see Stage 2: compatibility is **not** “stamp equals `playbackRevision`” as a definition of invalidation);
- required origin / launch / playhead interval is **contained** (today: linear 2-bar membership; wrap-aware membership is native/second-optimization);
- interval geometry matches (start, length, loop length).

**Non-empty `mergedEvents` is not evidence of readiness.** A zero-event prepared horizon is valid (silence in that interval is still a prepared interval).

Launch tick ∈ horizon via membership, not only non-empty + stamp.

### Horizon invalidation (target distinction; not Stage 1 firmware)

```text
today:
  mutation → playbackRevision++ → rebuild

target (measure first; redesign later if Stage 2 proves it):
  mutation → determine horizon impact
                    ├── no impact → keep horizon
                    └── impact  → prepare / replace horizon
```

`playbackRevision` mismatch is **not** defined as horizon invalidation. A mutation whose affected interval is disjoint from the **currently required** horizon must not cause a rebuild (acceptance criterion; Stage 2 traces today’s opposite).

**Intersection is a temporal predicate.** Evaluate `affected-interval ∩ horizon` against the horizon that is required **when the mutation becomes relevant**, not the horizon that happened to exist at T0. The horizon can advance between T0 and the rebuild decision; pinning intersection to T0’s interval can keep a stale buffer that the next quantum then consumes incorrectly. Do not solve that in Stage 1; do not attach intersection permanently to T0.

---

## What this work is not

| Not | Why |
|-----|-----|
| Remaining LCR 6.3 firmware | Consumer work. OpenSpec 6.3 is a pointer. |
| Domain `PlaybackWindow` | `slot-performance-interaction` Phase 3–4. Engine buffer stays `PlaybackMergedMidiEvents`. |
| NOTE_EDIT hydrate / LED lookup / 6C-on-button | Own work paths. |
| Incremental `mergedEvents` patch | Second problem. Do not add ordered insert unless a later stage is authorized after measurement. |
| Bar-constant **change** | Problem A keeps today’s 2-bar fill. Candidate lengths are Problem B **experiments**, not Config policy. |
| 2-bar LCR as default after A | Can mask the hypothesis. LCR is Problem C of the **required** horizon. |
| Wrapping firmware placement | Not bundled with A or C. |
| Interval reservation / playback scheduler / budget-driven firmware | Not authorized before Problem B evidence plus user pin. |
| `PlaybackHorizon` struct | Existing `windowStartTick` / `windowLengthTicks` / `mergedEvents` is the buffer. |

---

## Architecture checkpoint (before coding)

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — `ensurePlaybackMergedMidiEventsBuilt` owns the derived interval. LCR stays producer. Capture send stays the existing parallel stream. |
| **State transition change?** | NO — no new session/FSM. Readiness predicate + consume / prepare / gather. |
| **Interval change?** | NO during Problem A (Stage 1–3). Problem B may **experiment** with candidate lengths after A + user pin. Production size remains unchosen until then. |
| **Reuse?** | YES — existing fill owner, `tryResolvePreparedWindow`, capture stream. No `PlaybackHorizon` type. |
| **Forbidden?** | LCR on MIDI; 2-bar LCR consume as the default next step after A; `resolveNotes`; delete `materializeToEventVector`; hydrate 4c; stamp-mismatch as *defined* invalidation; new Manager/FSM; scheduler redesign; interval reservation; wrapping firmware with A or C; periodic JIT timer; display cadence as JIT clock. |

---

## Today (code) — facts Problem A must not forget

**Owner:** `ensurePlaybackMergedMidiEventsBuilt`.

| Fact | Proof |
|------|--------|
| Long-loop fill is 2 bars, **linear-clamped** (no seam) | `kMergedMidiEventsGatherBars = 2`; `winStart` pinned to `[0, L - winLen]` |
| Reuse is revision + linear playhead-in-window + half-bar margin | `builtFromRevision == playbackRevision` |
| Live capture send does **not** go through `mergedEvents` | `playCommittedLoopMidi` → `makeCapturePlaybackStream(loop.capture.store)` |
| `appendCaptureEvent` does **not** `++playbackRevision` | `Loop::appendCaptureEventWithResult` |
| Rebuild of committed `mergedEvents` **does** merge capture on fill | `gatherCommittedEventsInWindowWithCapture` — only when the committed window rebuilds |
| `++playbackRevision` **does** force that rebuild | pass commit/restore, `setCapturePassState`, EditPass snapshot/restore, undo touch — then stamp mismatch |
| LCR miss → gather | `tryResolvePreparedWindow` false; idle display already this shape |
| Wrapping **filter** exists; wrapping **placement** does not | `tickInHalfOpenWindow` / LCR two ranges vs clamp |

`playbackRevision` today means **content changed somewhere**, not **content changed in the interval playback is about to consume**. Stamp mismatch → whole-window gather is **safe and anti-JIT**. Trace before changing.

---

## Measurement (the hypothesis experiment)

Hooks (SESSION_CAPTURE). Do not invent a second telemetry owner — extend `PlaybackBuild` / existing `#CAP` / `RuntimeTimingTelemetry`.

### MIDI deadline correctness (primary)

For every MIDI event that should be emitted:

```text
scheduled_deadline
        ↓
actual sendMidiEvent()
        ↓
lateness_us = send − deadline
```

Hard acceptance (deadline definition precise enough that equality is on time):

```text
late_event_count == 0
max_lateness_us <= 0
```

Score **note-on, note-off, and MIDI clock independently**. Perfect notes with late clock (or the reverse) is a miss of that class. Clock is scored continuously while PLAYING, not only on mutation-linked notes.

`midi_gap` diagnoses **why** a miss happened. It is **not** the acceptance criterion.

**Shared with runtime scheduling.** The same `lateness_us` gate is the musical correctness criterion for [`runtime_scheduling_admission_model_architecture.md`](runtime_scheduling_admission_model_architecture.md). MIDI Input Gap remains that contract’s **interval / contention** diagnostic. Stage 1 hooks serve both plans (one owner: `RuntimeTimingTelemetry`). **Per-event measurement is not a per-event `#CAP` line** — see [Stage 1 logging](#stage-1-logging--do-not-emit-per-midi-event). This file does not start the Owner-Boundary Gate or interval reservation. The scheduling roadmap does not start this file.

| Measurement | Purpose |
|-------------|---------|
| late MIDI events (on / off / clock) | **hard correctness gate** |
| max MIDI lateness (per class) | **hard correctness evidence** |
| MIDI clock jitter | timing quality |
| rebuild count | test hypothesis |
| rebuild duration | explain CPU pressure |
| affected interval ∩ prepared interval | identify unnecessary rebuilds |
| CPU occupancy during rebuild | explain collision |
| display occupancy | competing workload |
| mutation → MIDI send | musical responsiveness |
| prepared lead | **diagnostic only** |

### Control vs mutation (required in Stage 3)

Same deadline metric in both:

| | Control (PLAYING, no mutations, 2-bar) | Mutation stress (overdub/edit/undo, 2-bar) |
|---|----------------------------------------|--------------------------------------------|
| late MIDI (on/off/clock) | | |
| max lateness | | |
| rebuilds | | |
| CPU spikes | | |

If **control already produces late MIDI**, the 2-bar rebuild hypothesis is **not sufficient** to explain the problem. If control is on time and mutation runs produce late MIDI **coincident with large rebuilds**, that is strong evidence.

### Mutation visibility (in-loop content)

| T | Meaning |
|---|---------|
| T0 | Mutation: capture append, EditPass, overdub commit, undo |
| T1 | Authorized path contains that mutation (`mergedEvents` **or** capture stream) |
| Tdeadline | Event’s MIDI deadline |
| T2 | `sendMidiEvent` for that note-on/off (clock scored separately, continuously) |

Score **T2 − scheduled_deadline** (`lateness_us`) as the correctness number. Score T2 − T0 and Tdeadline − T1 as responsiveness / lead **diagnostics**.

Cases: overdub on, overdub off, moved note, deleted note, inserted note.

**Boundary-crossing note edit (Problem A / later JIT — score, do not shrink from it):**

```text
          horizon
        [---------]
old note:        [----]
new note:                 [----]
```

Three cases, using **mutation affected interval** (not a single `changedTick`):

1. affected interval entirely **behind** the currently required horizon;
2. affected interval entirely **inside** the currently required horizon;
3. affected interval **crosses** the currently required horizon boundary.

The affected interval must **conservatively cover every playback tick whose observable MIDI result may have changed**:

| Mutation | Conservative coverage |
|----------|------------------------|
| Note move | `union(old span, new span)` |
| Delete | old span |
| Insert | new span |
| Pass replacement | affected pass region |
| Loop-length change | potentially the whole tiled cycle |

“Intersects” for rebuild means that coverage overlaps the **currently required** horizon (temporal; not T0’s horizon). Case 3 is where implementation semantics become clear.

### Launch readiness (slot / LoopEnd)

Separate quantity: **launch-to-ready lead** — time from slot/launch decision until the required launch interval is a usable prepared horizon (predicate above). Do not score this as mutation `T2−T0`.

### Same captures also score

LCR `win=` µs, `midi_gap`, `clockrate`, `idle_maint` opportunity, `playbackRevision` vs **affected interval ∩ currently required horizon**. Count separately (zero is valid until later stages):

```text
horizon advance count
full rebuild count
LCR resolve count
gather fallback count
```

Advance is not identical to rebuild: a later budget-driven policy may extend the interval without replacing the whole buffer.

```text
                    lateness_us          ← hard gate
                         │
        ┌────────────────┼────────────────┐
        ▼                ▼                ▼
   rebuild / CPU    prepared lead      midi_gap
   (hypothesis)     (diagnostic)       (diagnostic)
```

How **small** is Problem B, not A. Candidate comparison is **after** A **and only if A showed late MIDI** (unless the user pins an optimization experiment). Gate remains `late_event_count == 0` per class.

**Problem A question:** Does mutation on the current 2-bar fill produce **actual late MIDI** coincident with unnecessarily large rebuilds, relative to a no-mutation control?

Per mutation: affected ∩ 2-bar? rebuild? CPU? **late on / off / clock?**

Unnecessary-rebuild counts (intersect vs disjoint) explain **pressure**. They do not prove lag without late events.

Prepared-lead “how much of 2 bars is needed” is diagnostic.

**Problem B question:** What range of horizons keeps `late_event_count == 0` (on, off, clock) while rebuild pressure falls? Usable region, not smallest.

**Problem C question:** Can LCR make preparation of **that required horizon** cheap enough? Not automatically “LCR of 2 bars.”

If A control and mutation both have late MIDI = 0, “2 bars causes MIDI lag” is **not demonstrated**. Rebuild volume may still justify an optimization pin; that is not Problem B-as-correctness.

For every playback rebuild (explains pressure; does not pass without `lateness_us`):

```text
T_rebuild
window_length
affected_interval
current_required_interval
rebuild_reason
rebuild_cost
MIDI deadlines during rebuild
late on / off / clock
```

Target shape of pressure evidence (example, not a prediction):

```text
100 mutations
├── 72 intersect 2-bar window → 72 rebuilds
├── 28 don't intersect → 28 unnecessary rebuilds
└── rebuild cost: 8–20 ms
```

Prepared-lead “how much of the 2-bar extent is needed before the next MIDI deadline” stays diagnostic.

---

## Geometry (Problem A: today’s 2-bar linear fill)

Problem A does **not** change production placement or length. Native wrap-membership helper may exist as a parked test. Firmware wrapping placement is not bundled with A or C.

Today: `kMergedMidiEventsGatherBars = 2`; `winStart` pinned to `[0, L - winLen]`.

Problem B candidate lengths, if authorized after A, use `tickInHalfOpenWindow` with precondition `0 < lengthTicks <= loopLengthTicks`. That helper is **not** Stage 1 firmware.

---

## Stages

One stage per session unless the user authorizes more. Native first. Sequence: **A → B → C**. Do not run 2-bar LCR as the default after A.

### Stage 0 — pins (this document)

Done. No firmware.

### Stage 1 — measurement hooks on today’s 2-bar fill

**Owner:** `RuntimeTimingTelemetry` accumulators at send + rebuild one-shots. Note-on/off: `Track::sendMidiEvent` after the actual `midiHandler.sendMidiEvent`. Clock: `MidiHandler::sendClock` (internal master) — **not** `SC_MIDI_OUT_EVENT` and **not** `sendMidiEvent` (clock already bypasses that). Rebuild: extend existing `DIAG_TIMING_RECORD(PlaybackBuild)` / `PlaybackMergedMidiEventsRebuild` with a rare one-shot, not a line per MIDI event.  
**Invariant:** **No geometry change.** Capture append does not increment `playbackRevision`. Empty `mergedEvents` is still a valid prepared interval. `midi_gap` may be logged; it is not the gate. **No per-event `#CAP` on the MIDI send path.**

Firmware: hooks only. No MIN/MAX. No LCR consume. Logging shape is pinned below.

**Status:** firmware links `teensy41-capture-serial` (RAM1 code 425852, locals 4768). Native 1307/1307. ISR lateness stores are ITCM. Device score is Stage 3.

First link crossed the 32 KB ITCM page (`code` 426188). Recovery: `FLASHMEM` on `ClockManager::onMidiStart` / `toggleTransport` (not the tick ISR). Putting the lateness recorders in FLASHMEM then hung USB at transport start ([`212654`](../../captures/session_20260817_212654.log) — last `#CAP` 23.994 s, last BAR idle `0,0`). Those recorders are ITCM with `noteClockPulse`. `maybeEmit` drains one-shots so `loop()` has one telemetry call. Note lateness records `evt.tick`.

---

## Stage 1 logging — do not emit per MIDI event

Investigation 2026-08-17, before firmware. **Per-event serial on the MIDI path is already known to stall timing.** Stage 1 must **measure every event** and **emit summaries / rare one-shots**. Do not add a `#CAP` line per note-on, note-off, or clock.

### What the gate needs vs what serial can carry

| Need | How to get it without per-event TX |
|------|-------------------------------------|
| `late_event_count` / `max_lateness_us` per class | Hot path: integer subtract + `recordSample` (same as `midi_gap`). Emit 5 s Tier-A `DIAG,late_on` / `late_off` / `late_clk` from `RuntimeTimingTelemetry::maybeEmit` |
| Coincidence with a rebuild | Rebuild one-shot (window, reason, `PlaybackBuild` µs) + **first-late** one-shot in that stall. Correlate by `#CAP` micros. Do not dump every on-time send |
| Clock scored while PLAYING | Count + max in the same 5 s window. Firmware **must not** log per-pulse `0xF8` |

`lateness_us` is computed at send. Equality is on time. On-time events update counters only.

### Proof that per-event logging on this path is harmful

| Existing implementation | What it already decided | Evidence in code / captures |
|-------------------------|-------------------------|-----------------------------|
| `MidiHandler::handleMidiMessage` | Skip `SC_MIDI_IN` for Clock — “24 PPQN; skip synchronous capture” | `src/MidiHandler.cpp` |
| `MidiHandler::sendMidiEvent` | Skip `SC_MIDI_OUT_EVENT` for Clock | same file |
| HITL rule | Firmware does **not** log per-pulse clock in `#CAP,MI` | `.cursor/rules/HITL-Test-Flow.mdc` |
| `Track::sendMidiEvent` | Per-note `logger.log` is `LOG_TRACE` only — “logging every loop note at DEBUG blocks USB Serial for milliseconds and freezes the UI” | `TrackPlaybackHotPath.cpp` |
| SESSION_CAPTURE `logger.log` OUT NoteOn/Off | Compiled **out** on capture builds | `MidiHandler::sendMidiEvent` `#if !defined(SESSION_CAPTURE)` |
| Per-note Serial DEBUG on input | Disabled on capture builds — “saturates USB CDC (~960 lines/30s) and starves `#CAP` flush” | `handleMidiMessage` comment, `session_20260811_021117` |
| `SC_MIDI_OUT` / `MO` | Not Tier-A. Under ring pressure, `shouldSampleMidiOutCapture` drops 7/8 MO lines. PLAYING flush (`maxRecords=8`) **never calls USB Serial** — it only drops non-Tier-A head records | `DebugSessionCapture.cpp` |
| `SC_PLAYBACK_FRAME` (`PBF`) | Macro exists; **zero call sites** | not used |
| SEVT per stored event (RC-L3) | One line per event over 3554 events: **342 ms** on the stop path; ring overflow evicted `seal` / `finalize` | [`013917`](../../captures/session_20260813_013917.log); now idle 64-event slices, one-shot per boot |
| MO flood from slot playback | Background slots ~1 MO/frame froze display ≈2 s | [`122107`](../../captures/session_20260713_122107.log) vs [`115434`](../../captures/session_20260713_115434.log) |
| Ring full → Serial.println | Soft-lock under PLAYING + CAP flood; path removed | `emitCapLineOrSerial` comment; [`013003`](../../captures/session_20260719_013003.log) |
| PLAYING flush of 8 | Must not Serial; leftover USB write blocked after `f_flush_leave` | [`013003`](../../captures/session_20260719_013003.log) / [`013315`](../../captures/session_20260719_013315.log) |
| Tier-A vs volume | RC-S0a: `SEVT`/`REVT`/`DFRAME`/`MO` evicted every DIAG window across a record/overdub pass | [`141815`](../../captures/session_20260812_141815.log) |
| `maybeLogStoredNoteCount` / `overlap_hold` | Explicitly **not** a SEVT dump — one-shot inventory, Tier-A | `DebugSessionCapture.h` |
| Architecture snapshot on overdub stop | Flooded the ring; dump removed from that path | [`post_overdub_playing_midi_drain_bugfix.md`](post_overdub_playing_midi_drain_bugfix.md) |

At 120 BPM, MIDI clock is **48 pulses/s**. A line per clock plus notes would refill the 96 KB PSRAM ring, starve Tier-A DIAG, and during PLAYING would not even transmit (flush drops non-Tier-A and leaves Tier-A unsent). Making per-event lateness **Tier-A** would wedge the ring the same way RC-S0a did.

Host reconstruction from existing `MO` lines cannot be the gate: MO is sampled under pressure, dropped during PLAYING flush, and **clock has no MO**.

### Pattern to extend (do not invent a second owner)

S0 already does the right thing:

```text
hot path:  recordSample(max, overCount)     // no Serial, no ring
5 s loop:  maybeEmit → DIAG,<tag>,<maxUs>,<overCount>   // Tier-A
rare:      loop_rem when duration >= 50 ms
rebuild:   DIAG_TIMING_RECORD(PlaybackBuild) in ensurePlaybackMergedMidiEventsBuilt
bar align: SC_UPDATE → BAR (once per bar, not per clock)
```

Stage 1 adds three accumulators (`late_on` / `late_off` / `late_clk`) to that emit window, plus:

1. **First-late one-shot** (Tier-A, same family as `loop_rem`): class + `lateness_us` + tick. Rate-limit so a stall does not emit 48 clock lines/s. Reset the one-shot arm when the 5 s window emits (or after the stall ends).
2. **Rebuild one-shot** at `ensurePlaybackMergedMidiEventsBuilt` when a gather actually ran: duration, window start/length, revision. Reuse `PlaybackBuild` max; do not snapshot every Diagnostics counter (that dump already overflowed rings on overdub stop).
3. **FLASHMEM / noinline** for emit helpers so span strings stay out of ITCM/DTCM (same as `recordLoopRemainderIfMeasuring`).

### Forbidden on Stage 1

- `#CAP,MO` / `#CAP,MI` for Clock
- `#CAP` line per on-time note-on/off
- Re-enabling `logger.log` DEBUG on `sendMidiEvent`
- Calling `SC_PLAYBACK_FRAME`
- Per-event lateness as Tier-A text (would protect a flood)
- `emitArchitectureMetricsSnapshot` from the playback hot path
- Blocking `Serial.printf` at send time (ring append is FLASHMEM and still costs; do not even queue on-time events)

### Clock hook placement (code fact)

`MidiHandler::sendClock()` writes USB/DIN realtime directly. It does **not** go through `sendMidiEvent`, so a hook “only at `sendMidiEvent`” would score notes and miss outgoing clock. Incoming external clock is already counted (`noteClockPulse`) and timed (`noteClockDispatch` includes `updateAllTracks`). Stage 1 outgoing-clock lateness is **internal master `sendClock`**. External-slave clock quality stays `clockrate` + `clk` duration until a later pin; do not add per-pulse input capture to close that.

### Proceed?

Logging shape: **YES** — accumulate per event, emit 5 s + rare late/rebuild one-shots.  
Firmware Stage 1: **links**. First-late one-shot arms until the 5 s `maybeEmit` window (implementer pin). On time = sent before the next playback tick (notes) or before `clockDue + one internal tick` (outgoing clock). ISR paths only store integers; `#CAP` drains on the main loop via `maybeEmit`.

### Stage 2 — mutation affected interval vs 2-bar window (trace, no stamp redesign)

**Owner:** existing `++playbackRevision` call sites + rebuild counter. Instrumentation-only: **mutation affected interval**.

**Invariant:** A capture append does not rebuild committed `mergedEvents`. A revision bump **does** today — count intersect vs disjoint vs rebuild (the 72/28 table). Does **not** authorize stamp redesign or shrinking.

Do **not** change stamp meaning. Do not define mismatch as invalidation.

### Stage 3 — Problem A: prove whether 2-bar rebuilds cause late MIDI

**Control:** PLAYING, no mutations, today’s 2-bar fill. Score late on/off/clock, max lateness, rebuilds, CPU spikes.

**Mutation stress:** same metric; overdub / NOTE_EDIT (including boundary cases) / undo as available.

**Pass (correctness):** if control already has late MIDI, **stop** — the 2-bar rebuild hypothesis is not a sufficient explanation. If control is on time and mutation produces late on/off/clock **coincident with large rebuilds**, the lag hypothesis is demonstrated. Rebuild count/cost without late events is pressure, not proof.

Rebuild count/cost and affected ∩ prepared explain **why**. They do not pass the stage without the deadline table.

**Do not pick a smaller window. Do not implement LCR consume. Do not implement budget-driven advance.**

**Stop and ask.** late MIDI = 0 on mutation → lag hypothesis **not demonstrated** (optimization pin optional). late MIDI > 0 coincident with large rebuilds → **Problem B**, not 2-bar LCR.

Device [`213401`](../../captures/session_20260817_213401.log) (after ISR ITCM fix): 64-bar overdub enter/hold stalls are `lcr,src,why=open` 133 ms / `why=hold` 99 ms / `usbnote` 396 ms. 2-bar `playback_build` is 3–6 ms. 1-bar overdub in the same capture has `late_*` 0. Do not treat this capture as a pin to shrink the gather window or to consume LCR on playback.

### Stage 4 — Problem B: usable region (experiment, not policy)

**Only after Stage 3 shows late MIDI (or an explicit optimization pin).** Candidate horizons (2-bar control vs 1 / 1/2 / 1/4 / 1/8 as **measurement lengths**, not Config). Mandatory: `late_event_count == 0` for on, off, and clock. Then rebuild pressure, CPU, mutation visibility.

Too-small cliff: many refills + late MIDI. Too-large cliff: unnecessary rebuilds; late MIDI only if still present.

**Stop and ask** with the usable region. Do not install a production length. Not looking for the fastest rebuild.

### Stage 5–6 — Problem C: LCR of the **required** horizon

Native equivalence then firmware consume of the horizon Problem B (or 2-bar if A did not demonstrate late MIDI) named. Miss → gather. Capture stream unchanged. 6.0 holds. Geometry is whatever B pinned — **not** “LCR plus a new size in the same commit” unless the user explicitly authorizes both.

Device gate: `late_event_count == 0` and `max_lateness_us <= 0` for note-on, note-off, and clock; gather vs LCR cost is supporting. `midi_gap` is diagnostic.

Stamp-mismatch rebuilds: report as debt unless they match `affected ∩ currently required horizon`. Not the target design.

### Wrap placement

Parked. Not bundled with A or C.

---

## Architecture acceptance

The work is successful only if:

1. No new playback-horizon domain object or manager exists.
2. `PlaybackMergedMidiEvents` remains the derived playback buffer.
3. `capture.store` remains a separate authorized MIDI stream.
4. MIDI callbacks never construct or resolve LCR.
5. Problem A does not change 2-bar geometry. Candidate lengths are Problem B experiments after A + user pin.
6. A zero-event prepared interval is valid.
7. `playbackRevision` mismatch is not itself defined as horizon invalidation.
8. Stage 2 traces unnecessary rebuilds (affected disjoint from required interval). Stamp redesign is not Stage 1–3 firmware.
9. LCR (Problem C) equals gather on the **required** horizon (count, order, multiplicity).
10. **Hard gate:** `late_event_count == 0` and `max_lateness_us <= 0` for note-on, note-off, and MIDI clock independently. `midi_gap` and prepared lead are not the pass criterion.
11. 2-bar LCR is **not** the default next step after A. If A has late MIDI = 0, “2 bars causes MIDI lag” is not demonstrated.
12. Stage 3 compares control vs mutation on the same deadline metric. Late MIDI in control means the rebuild hypothesis is not a sufficient explanation.
13. Wrap placement is not bundled with A or C. No `RuntimeWorkBudget` / interval reservation / budget-driven firmware before B.
14. Same `lateness_us` hooks are the musical gate for the runtime scheduling contract. MIG remains that contract’s interval diagnostic. This file does not start interval reservation.

---

## Files (when authorized)

| Stage | Files |
|-------|--------|
| 1–3 | CAP next to `PlaybackBuild`; affected-interval / rebuild / late-MIDI hooks; no wrap-placement production helper |
| 4 | Device / CAP candidate-horizon analysis after user pin — no Config length |
| 5–6 | Native LCR equivalence + `TrackPlaybackWindowBuild.cpp` consume of the **required** horizon |

Do not add `PlaybackGatherSession`, `PlaybackHorizon`, or a new Manager.

---

## When this work starts

1. This file in CURRENT_WORK § Now implementing.
2. Stage 1–2 hooks/trace. Stage 3 Problem A. **No** LCR consume and **no** length change until A + user pin.
3. Problem B usable region only if A supports the hypothesis (or user pins an experiment anyway).
4. Problem C LCR of the horizon B (or 2-bar if A did not demonstrate late MIDI) named. Architecture gate before C firmware.

---

## Pre-implementation review (2026-08-17, hypothesis)

### Ready

- Capture send is already a parallel stream; append does not bump revision.
- `tryResolvePreparedWindow` miss path exists.
- `PlaybackBuild` exists for fill cost.
- Existing `PlaybackMergedMidiEvents` fields are the buffer — no new type.

### Resolved (user / code)

| Topic | Decision |
|-------|----------|
| Hypothesis | 2-bar too far ahead → mutation → large rebuild → CPU contention → **actual late send** |
| Not the hypothesis | “Gather is too slow”; “smaller is better”; “cheaper 2-bar LCR first” |
| Hard gate | per-event `lateness_us` **measured** at send; `late_event_count == 0`; `max_lateness_us <= 0`; note-on, note-off, clock **independent**. Emit is 5 s `RuntimeTimingTelemetry` + rare late/rebuild one-shots — **not** a `#CAP` line per MIDI event. `midi_gap` / prepared lead are diagnostics. **Same gate** for runtime scheduling product correctness |
| Stage 1 serial | Accumulate on the hot path. Do not log Clock as `MI`/`MO`. Do not re-enable DEBUG `logger.log` on send. Do not call `SC_PLAYBACK_FRAME`. Rebuild uses `PlaybackBuild` one-shot |
| First-late one-shot | Arm until 5 s `maybeEmit`. ISR stores pending only; main loop emits `DIAG,late_event` |
| Scheduling | MIG = interval/contention. Do not start Owner-Boundary Gate or interval reservation from this file |
| Control | Stage 3 requires PLAYING / no-mutation vs mutation on the **same** deadline metric |
| Sequence | A prove late MIDI exists → B usable region → C LCR of required horizon |
| 2-bar LCR after A | **Not default** — can mask the hypothesis |
| If A did not demonstrate late MIDI | 2-bar LCR or stop is an **optimization** choice, not a correctness fix |
| Size policy | Unchosen until B + user pin |
| Budget-driven | May emerge from B; not scheduler architecture first |
| Capture | Authorized parallel path |
| Stability | Mutation ≠ rebuild; revision bump ≠ invalidation |
| Display | Occupancy context; not a clock |
| Wrap | Parked; not with A or C |
| Incremental `mergedEvents` | Out of scope |
| FSM / scheduler | No new session; no interval reservation |

### Open before coding

1. Stage 3: control vs mutation `lateness_us` (on/off/clock) — **user pin** of B vs not-demonstrated lag (optimization / 2-bar LCR / stop).
2. Stamp meaning: Stage 2 evidence; redesign not in Stage 1–3.
3. Problem B candidate set (which lengths) — user pin, not agent guess.

### Proceed?

- Architecture: **hypothesis experiment**. Stage 1 hooks **link**. Stage 2: mutation trace. Stage 3: Problem A. B and C gated.
- Firmware Stage 1: YES (CURRENT_WORK). No geometry change. Not on device until upload.
