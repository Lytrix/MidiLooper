# Consumer window budget and ownership

**Status:** Architecture recorded 2026-08-17. **No firmware** until this file is in CURRENT_WORK § Now implementing.  
**Date:** 2026-08-17  
**Kind:** architecture  
**Evidence:** [`213401`](../../captures/session_20260817_213401.log)  
**Does not replace:** [`overdub_lifecycle_representation_authority.md`](overdub_lifecycle_representation_authority.md), [`playback_gather_lcr_consume_enhancement.md`](playback_gather_lcr_consume_enhancement.md), DEC-037 LCR as resolver  
**Does not authorize:** a `WindowManager` / `WindowResolver` owner; `WindowRequest` / `WindowPolicy` types; interval reservation; shrinking the 2-bar playback gather; LCR consume on MIDI playback; putting `resolveWindow` on `handleMidiInput`

---

## North star

**No latency-sensitive path may synchronously resolve more content than its consumer contract requires. Every window has an explicit owner, purpose, horizon, and CPU budget. Larger windows must be prepared or processed incrementally outside the latency-sensitive path.**

`LoopContentResolution` answers: given this exact interval, resolve it. It does **not** decide that the caller wants 16 bars.

---

## What [`213401`](../../captures/session_20260817_213401.log) changed

The playback-gather hypothesis (2-bar `PlaybackMergedMidiEvents` too far ahead → late MIDI) is **not** the dominant stall on that capture.

| Consumer | Interval today | Sync on | 64-bar cost | 1-bar cost |
|----------|----------------|---------|-------------|------------|
| MIDI gather | 2 bars | `ensurePlaybackMergedMidiEventsBuilt` | 3–6 ms `playback_build` | — |
| Overdub source `why=open` | `kMaxDetailedWindowBars` (16) via `overdubSourceWindowLengthTicks` | USB button → `establishOverdubSourceView` | `from=win` tot **133 ms** (win 104 + proj 29), 1177 events / 536 notes | tot **3.4 ms**, 10 events / 5 notes |
| Hold `why=hold` | same 16-bar helper | USB note-off → `ensureOverdubSourceNotesForHold` | win **99 ms**, 1177 events, **merged=0** | win **1.0 ms**, 10 events |
| Display | 16-bar piano roll | idle / frame | human-scale | human-scale |

Hold `merged=0` after 99 ms is the request-granularity failure: the consumer paid a 16-bar `resolveWindow` and kept no new notes.

**Dangerous rule in code today:** `tryResolvePreparedWindow` miss → `resolveWindow` of the full requested interval (`from=win`). “Not prepared” means “do the whole thing now” on `handleMidiInput`.

`Loop::overdubSourceWindowLengthTicks()` returns `DisplayWindowUtils::kMaxDetailedWindowBars * TICKS_PER_BAR`. Display’s 16-bar constant governs overdub source and hold.

---

## Owners (existing — do not add a window manager)

| Consumer | Interval owner today | Representation | Path class |
|----------|----------------------|----------------|------------|
| Playback | `PlaybackMergedMidiEvents` on `LoopPlaybackRuntime` | `mergedEvents` | JIT / MIDI send |
| Overdub source | `Loop::overdubSourceView*` + `rebuildOverdubSourceView` | source notes/events | Interactive; **today on USB** |
| Hold | `ensureOverdubSourceNotesForHold` | merge into source view | JIT / USB note-off |
| Display | `DisplayWindowUtils` + `visualCache` | piano-roll notes | Background / human |

DerivedViews already says consumers do not share one list. They still share **one bar count**.

**Formal trigger:** a new `WindowManager` / `WindowResolver` owning mutable window state. **Not** a trigger: a source-only length constant on `Loop`, or a clamp used only by `overdubSourceWindowLengthTicks`.

**Pending names (do not add types until pinned):** `WindowRequest`, `WindowPolicy`, `WindowKind`. Prefer constants and comments on the owners above.

---

## Required horizon vs resolution window

A consumer may need to *know about* a region without synchronously resolving all of it.

- **Required horizon** — interval the consumer’s contract names (sounding-at-S, overlap with this hold, next MIDI deadline, viewport).
- **Resolution window** — interval actually passed to `resolveWindow`. Must be ≤ required horizon unless prepared off-path.

`open` and `hold` must not be assumed to share one policy. RC8 made hold a 16-bar this-pitch JIT because enter’s 16-bar window missed a note; that is a **correctness** horizon, not a proof that USB should resolve 1177 events.

---

## Invariant (latency-sensitive)

A latency-sensitive consumer may never synchronously materialize an arbitrarily sized window.

```text
requested interval
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
| Moving 100 ms onto idle unchanged | Queue without right-sized horizon still does 100 ms of work |
| One global `kMaxDetailedWindowBars` policy | Display, source, and hold are different contracts |
| New Manager / Session / OpenSpec change | Not authorized |
| Breaking RC8 occupied-lane consume | Experiment 1 **measures**; it does not pick production size |

---

## Experiment 1 — clamp source/hold only

**Keep playback and display unchanged.** Detach `overdubSourceWindowLengthTicks` from `kMaxDetailedWindowBars`. Run the 64-bar overdub HITL at successive source/hold lengths:

```text
1, 2, 4, 8, 16 bars
```

Record per length:

```text
lcr,src why=open  win, proj, tot, ev, notes, from
lcr,src why=hold  win, ev, merged, notes
usbnote max, notechg max, midi_input max
late_on / late_off / late_clk  max and count
clockrate
overlap_hold add/hide (correctness — do not ignore)
```

If tot / `usbnote` fall with bar count, window breadth is the dominant variable on this path.

A 1-bar clamp may miss occupied-lane notes that RC8’s 16-bar hold was built to catch. That is data for Experiment 2, not an automatic production pin.

Owner: `Loop::overdubSourceWindowLengthTicks` / `resolveOverdubSourceWindow`. Not `DisplayWindowUtils`. Not `ensurePlaybackMergedMidiEventsBuilt`.

---

## Experiment 2 — minimum required source vs hold horizon

Do not ask “which arbitrary window is safe?” Ask:

- What interval does `why=open` actually require to establish sounding state at S?
- What interval does `why=hold` actually require for this pitch / this hold `[S, E)` (wrap-shaped `[S, L) ∪ [0, E)` included)?

Those contracts may differ. Production policy is the union of **required** intervals, prepared off USB when larger than the sync budget.

---

## Later (not Experiment 1)

Prepared miss → queue + bounded fallback (not unlimited `from=win`).  
JIT horizon as a **consumer property**, LCR still a resolver.  
Optional later types only after a name pin.

---

## Open before coding

1. User pin: Experiment 1 in CURRENT_WORK § Now implementing.
2. Name pin: keep policies as `Loop` constants, or introduce `WindowRequest` / `WindowPolicy` (new domain nouns — not in NAMING.md).
3. Experiment 1 clamp mechanism: compile-time constant vs capture-serial-only override. Prefer one constant on `Loop`, rebuild per length, not a runtime Manager.

### Proceed?

- Architecture: **YES** as north star. Playback gather Stage 2 / Problem B **not** the next firmware.
- Firmware: **NO** until Experiment 1 is named in CURRENT_WORK.
- Experiment 1: extend `Loop::overdubSourceWindowLengthTicks` only.
