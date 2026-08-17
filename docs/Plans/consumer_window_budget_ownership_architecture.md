# Consumer window budget and ownership

**Status:** Architecture **pinned** 2026-08-17. Experiment 1 **pinned** (detach source/hold from display 16; default remains 16 until a measurement rebuild).  
**Date:** 2026-08-17  
**Kind:** architecture  
**Evidence:** [`213401`](../../captures/session_20260817_213401.log)  
**Does not replace:** [`overdub_lifecycle_representation_authority.md`](overdub_lifecycle_representation_authority.md), [`playback_gather_lcr_consume_enhancement.md`](playback_gather_lcr_consume_enhancement.md), DEC-037 LCR as resolver  
**Does not authorize:** a `WindowManager` / `WindowResolver` owner; `WindowRequest` / `WindowPolicy` / `WindowKind` types; interval reservation; shrinking the 2-bar playback gather; LCR consume on MIDI playback; treating a timing-passing clamp as production policy

---

## North star

**No latency-sensitive path may synchronously resolve more content than its consumer contract requires. Every window has an explicit owner, purpose, horizon, and CPU budget. Larger windows must be prepared or processed incrementally outside the latency-sensitive path.**

### LCR invariant (DEC-037)

`LoopContentResolution` answers: given this exact interval, resolve it. It does **not** decide that the caller wants 16 bars. Consumer policy must not leak into LCR.

### No window manager

The horizons accidentally converge on 16 bars. They do not lack a common manager. Owners stay:

- `PlaybackMergedMidiEvents` on `LoopPlaybackRuntime`
- `Loop::overdubSourceView*` / `rebuildOverdubSourceView`
- `ensureOverdubSourceNotesForHold`
- `DisplayWindowUtils`

---

## Two independent questions

```text
                 CONSUMER
                    │
                    │ 1. How much content does this consumer need?
                    ▼
             owner-specific horizon
                    │
                    ▼
             exact LCR interval
                    │
                    ▼
          LoopContentResolution
```

```text
              preparation / scheduling
                       │
                       │ 2. When is it safe to resolve that content?
                       ▼
                 consumer owner
```

Today those are fused: “the consumer wants 16 bars, and because it isn’t prepared, resolve all 16 bars right now.”

---

## What [`213401`](../../captures/session_20260817_213401.log) changed

The playback-gather hypothesis (2-bar `PlaybackMergedMidiEvents` too far ahead → late MIDI) is **not** the dominant stall on that capture.

| Consumer | Interval today | Sync on | 64-bar cost | 1-bar cost |
|----------|----------------|---------|-------------|------------|
| MIDI gather | 2 bars | `ensurePlaybackMergedMidiEventsBuilt` | 3–6 ms `playback_build` | — |
| Overdub source `why=open` | was display 16 via `overdubSourceWindowLengthTicks` | USB button → `establishOverdubSourceView` | `from=win` tot **133 ms** (win 104 + proj 29), 1177 events / 536 notes | tot **3.4 ms**, 10 events / 5 notes |
| Hold `why=hold` | same helper | USB note-off → `ensureOverdubSourceNotesForHold` | win **99 ms**, 1177 events, **merged=0** | win **1.0 ms**, 10 events |
| Display | 16-bar piano roll | idle / frame | human-scale | human-scale |

Hold `merged=0` after 99 ms is the request-granularity failure: the consumer paid a 16-bar `resolveWindow` and kept no new notes.

**Dangerous rule in code today:** `tryResolvePreparedWindow` miss → `resolveWindow` of the full requested interval (`from=win`). “Not prepared” means “do the whole thing now” on `handleMidiInput`.

---

## Required information vs resolution window

A consumer may need to *know about* a region without synchronously resolving all of it.

- **Required information** — what the consumer’s contract must determine (sounding-at-S, pitch boundaries through the hold, next MIDI deadline, viewport). Not automatically a geometric interval size.
- **Synchronous resolution window** — interval actually passed to `resolveWindow` on a latency-sensitive path.

**Synchronous rule:** a synchronous resolution window must contain only information required by the consumer’s contract, and must remain within its synchronous CPU budget.

Do **not** use “resolution window ≤ required horizon” as an invariant. The resolver may need context beyond the obvious consumer interval (a sounding note whose start precedes the requested range). The invariant is **bounded work / required information**, not geometric containment.

`open` and `hold` must not be assumed to share one policy. RC8 made hold a 16-bar this-pitch JIT because enter’s window missed a note; that is a **correctness** information requirement, not a proof that USB should resolve 1177 events.

---

## Invariant (latency-sensitive)

A latency-sensitive consumer may never synchronously materialize an arbitrarily sized window.

```text
requested information
       │
       ▼
prepared?
   │       │
  yes      no
   │       │
   ▼       ▼
consume   bounded fallback
             │
             ▼
       queue preparation
```

“Not prepared” must not mean “resolve the 16-bar display window now.”

Tiers (vocabulary only, not a new FSM):

| Tier | Examples | Sync resolve |
|------|----------|--------------|
| Critical / JIT | MIDI send, USB MIDI, clock, hold decide | Never large |
| Interactive | overdub enter, loop switch, note edit | Bounded; else prepared |
| Background | visual cache, future gather, persist prep | Cooperative budget |

---

## What this is not

| Not | Why |
|-----|-----|
| Playback Problem B length clamp | 2-bar gather was 3–6 ms; not the 154/396 ms stall |
| Faster `resolveWindow` as the first fix | 1-bar vs 64-bar is window breadth; `merged=0` is request size |
| Moving 100 ms onto idle unchanged | Queue without right-sized information still does 100 ms of work |
| One global `kMaxDetailedWindowBars` policy | Display, source, and hold are different contracts |
| New Manager / Session / OpenSpec / `WindowRequest` types | Not authorized |
| “Largest window that doesn’t stall” | Experiment 1 evidence, not the architecture |
| Breaking RC8 occupied-lane consume | Experiment 1 **measures**; it does not pick production size |

---

## Experiment 1 — cost vs source/hold breadth

**Keep playback and display unchanged.** Source/hold length is `Loop::kOverdubSourceWindowBars` (not `kMaxDetailedWindowBars`). Default **16** until a measurement rebuild. Run the 64-bar overdub HITL at:

```text
1, 2, 4, 8, 16 bars
```

one rebuild per length. Do not pick a production size in advance.

Curves (all required):

```text
window bars → resolved events
window bars → resolveWindow µs
window bars → USB/input stall (usbnote / midi_input / ODUB complete)
window bars → overlap/hold correctness (add/hide; RC8)
```

If the first three correlate, the display-derived 16-bar horizon is the wrong consumer policy. The fourth may show the smallest *fast* window is not the smallest *correct* window.

Also record: `from`, `proj`, `tot`, `merged`, `late_on` / `late_off` / `late_clk`, `clockrate`. CAP lines include `bars=` so captures self-identify.

**Sequence (do not skip):**

```text
Experiment 1  →  measure cost vs breadth
Experiment 2  →  determine information actually required
              →  define owner-specific horizon
              →  only then preparation / bounded fallback
```

If 4 bars happens to pass the timing gate, that is **evidence**, not the final architecture.

Owner: `Loop::overdubSourceWindowLengthTicks` / `resolveOverdubSourceWindow`. Not `DisplayWindowUtils`. Not `ensurePlaybackMergedMidiEventsBuilt`.

---

## Experiment 2 — minimum required source vs hold information

Do not ask “which arbitrary window is safe?”

For `open`: what information is required to establish which canonical notes are **present** at S?

For `hold`: not merely `[S, E)`. Occupied-lane already showed **history matters**. The question is:

> What minimum information is required to determine whether this pitch is present at `S`, and whether it remains/changes through `E`, including loop wrap — independent of MIDI send and mute?

**Preferred answer (2026-08-17, Phase 0b):** overdub consumes **notes present at tick `S`**. MIDI-execution sounding state is withdrawn. `SoundingNote` alone lacks `endTick` (it lives on `NoteSpan` / `DisplayNote`). Wrap predicates are not proven equal. Plan: [`overdub_participant_loop_content_architecture.md`](overdub_participant_loop_content_architecture.md). Phase 1 firmware not authorized. Do not start a 1/2/4/8/16 production clamp from this file.

---

## Later (not Experiment 1)

Prepared miss → queue + bounded fallback (not unlimited `from=win`).  
JIT horizon as a **consumer property**, LCR still a resolver.

---

## Architecture gate (Experiment 1)

| Question | Answer |
|----------|--------|
| **Owner module** | `Loop::overdubSourceWindowLengthTicks` |
| **Primary invariant** | LCR does not choose 16 bars; source/hold breadth is a Loop constant, not display’s |
| **Ownership change?** | NO |
| **State transition change?** | NO |
| **Behavior-preserving?** | YES at default 16 |
| **Reuse** | YES — extend `overdubSourceWindowLengthTicks` |
| **Phase scope** | Experiment 1 detach + `bars=` CAP; playback/display untouched |

### Proceed?

- Architecture: **YES**. No `WindowRequest` / manager.
- Experiment 1: **YES** — detach; measure series; do not pin production size.
- Playback gather Stage 2 / Problem B: **not** the next firmware.
