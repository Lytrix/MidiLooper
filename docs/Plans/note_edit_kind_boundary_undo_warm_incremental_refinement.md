# NOTE_EDIT playing geometry apply latency — refinement plan

**Status:** Active (revised 2026-08-05 after Phase 1 profile)

**Supersedes:** Incremental `UndoWarmJob` / baseline-diff slicing plan (scrapped — profile disproved the hypothesis).

**Problem:** First coarse or pitch geometry move after note select feels like the display skips an update; subsequent moves track smoothly.

**Primary evidence:** [`captures/session_20260805_234116.log`](../../captures/session_20260805_234116.log) (`UNDO_WARM` instrumentation, commit `fe7e061`).

**Secondary evidence:** [`captures/session_20260805_231739.log`](../../captures/session_20260805_231739.log) — pre-profile session that motivated undo-warm work; the ~663 ms gap was misattributed to undo build.

**Related shipped work:** Per-frame playing geometry coalesce (`cb9b59b`), deferred playback rebuild, F1 motor settle, `scheduleKindBoundaryUndoWarm` on select.

**Context:** [`docs/Plans/firmware_ownership_lifetime_review.md`](firmware_ownership_lifetime_review.md) (NOTE_EDIT + PLAYING stability).

---

## Phase 1 profile — conclusions (done)

Instrumentation: `#CAP,...,UNDO_WARM,...` via `logger.info` in `buildSessionUndoEntry`, `processKindBoundaryUndoWarm`, `pushSessionUndoOnKindChange` (`teensy41-capture-serial`).

Grep: `rg UNDO_WARM captures/session_20260805_234116.log`

### Undo warm is not the first-move bottleneck

Typical build (33 `baselineMap` entries, 68 session MIDI events):

| Phase | Time | Share of build |
|-------|------|----------------|
| `read_events` | 0–3 µs | ~0% |
| `baseline_probe` | 77–152 µs | ~2% |
| `edit_rows` | 0–6 µs | ~0% |
| `flat_copy` / `overlap_resolve` | never logged | `needsBaselineMapDiff=0` always |
| **`focus_snap`** | **3.1–5.4 ms** | **~50–65%** |
| **`build,total`** | **5.7–8.4 ms** | |
| **`warm,complete`** | **~6.0–6.5 ms** | |

Kind-boundary undo preparation is **~6–8 ms**, not hundreds of ms. Chunking baseline diff or cooperative undo schedulers would not fix perceived first-move lag.

### First-move lag is queue → apply → pipeline delay

**First coarse move after select (transport + NOTE_EDIT):**

```
21.330  POSITION EDIT logged (coarse handler)
21.330–21.339  UNDO_WARM push,cache_miss (~8.4 ms, kind=Move)
21.668  Note movement + GeometryPipeline (+329 ms)
21.669  Display selection refresh
```

**Cache hit — undo is instant, geometry still late:**

```
26.293  push,cache_hit (0 µs)
26.949  GeometryPipeline (+656 ms)
```

**Second coarse move in same gesture (21.699):** geometry applies in **the same millisecond** as `POSITION EDIT`. First move is the outlier.

### Minor undo-warm hygiene (not P0)

- Three full `warm,complete` (~6 ms each) within 110 ms on one select (~17.74 s) — warm re-scheduled without coalescing.
- `push,cache_miss` on first kind change when warm had not completed before user moved (~8 ms cost only).

---

## Revised problem statement

```mermaid
sequenceDiagram
  participant Fader as Coarse fader MIDI
  participant CS as ControlSurfaceManager
  participant Loop as main loop
  participant EM as EditManager
  participant Disp as Display

  Fader->>CS: POSITION EDIT (playing)
  CS->>CS: queuePendingPlayingEditMove
  Note over CS,Loop: Suspected gap 300–650 ms
  Loop->>CS: processPendingPlayingEditGeometry
  CS->>EM: beginGeometryMutation (~8 ms undo)
  EM->>EM: moveNote / GeometryPipeline
  EM->>Disp: bumpSessionPreviewRevision
```

**North star:** First playing geometry apply should reach `GeometryPipeline` + display invalidation in the **same main-loop frame** as the queued fader target (or the next frame at ~30 ms display cadence), matching subsequent moves.

**Not in scope:** Undo row semantics, global undo routing, SD persist slicing, full cooperative idle scheduler.

---

## Work stream A — Instrument the apply path (next)

**Goal:** Measure where the 300–650 ms gap lives. Behavior-preserving.

Add `SESSION_CAPTURE` lines (same pattern as `UNDO_WARM`):

| Marker | When | Fields |
|--------|------|--------|
| `GEOM_APPLY,queue` | `queuePendingPlayingEditMove/Length/Pitch` | kind, target tick, `millis`, transport running |
| `GEOM_APPLY,dequeue` | start of `processPendingPlayingEditGeometry` | queued age ms, kind |
| `GEOM_APPLY,undo` | after `beginGeometryMutation` in apply path | ok/fail, elapsed µs |
| `GEOM_APPLY,focus` | after `ensureNoteEditFocusForLiveEdit` in apply path | elapsed µs, kind |
| `GEOM_APPLY,resolve` | after `applyNoteEditChange` / length overlap | elapsed µs, applied, kind |
| `GEOM_APPLY,done` | after `finishGeometryDriverSideEffects` | applied, display revision |

Also log when `processPendingPlayingEditGeometry` returns early (`pending None`, transport stopped).

**Files:** `src/ControlSurface/PlayingEditGeometryDefer.cpp`, optionally `NoteEditGeometryApply` mutation paths (pipeline boundary).

**Gate:** One HITL capture; table queue age vs pipeline time for first vs second move.

---

## Work stream B — Fix first-move apply delay (after A pinpoints owner)

**Hypotheses to confirm with `GEOM_APPLY` (ordered by log evidence):**

1. **Pending geometry not applied for many frames** — queue at fader input, `processPendingPlayingEditGeometry` runs but pending cleared/stale, or apply blocked until a later condition (transport edge, `startEditingEnabled`, grace).
2. **First `moveNoteToPosition` path only** — heavy one-shot work between `beginGeometryMutation` and `applyNoteEditChange` (e.g. `ensureNoteEditFocusForLiveEdit`, focus rebuild, session flat materialize) that subsequent moves skip.
3. **Main-loop ordering** — `processPendingPlayingEditGeometry` runs before/after work that blocks for hundreds of ms on first kind change only (deferred load, persistence, display path).
4. **Coalescing overwrites without apply** — rapid coarse input replaces pending target; first target not applied until fader settles (would show long queue age in A).

**Likely fix directions (implement only after A):**

| If A shows… | Fix direction |
|-------------|----------------|
| Long queue age, short pipeline | Apply pending geometry earlier in loop; ensure same-frame dequeue after MIDI; avoid dropping first pending |
| Short queue age, long gap inside `moveNoteToPosition` | Split/profile `ensureNoteEditFocusForLiveEdit` + pre-pipeline; cache focus bridge on select |
| First move only, instant second | One-shot init deferred to select/warm; don't repeat on every apply |
| Pending never dequeued until transport edge | Fix transport gating on `processPendingPlayingEditGeometry` |

**Files (expected):** `ControlSurfaceManager.cpp` (`processPendingPlayingEditGeometry`, coarse/fine/pitch handlers), `EditManager.cpp` (`moveNoteToPosition`, `ensureNoteEditFocusForLiveEdit`), possibly `main.cpp` loop order.

**Gate:** HITL — NOTE_EDIT + PLAYING, first coarse move: `GEOM_APPLY,done` within 35 ms of `GEOM_APPLY,queue`; display note position updates on first tick.

---

## Work stream C — Undo warm hygiene (P2, optional)

Only after stream B — undo is ~6 ms and not user-visible compared to 300+ ms apply gap.

| Item | Effort | Benefit |
|------|--------|---------|
| Coalesce duplicate warms on rapid select (skip if job complete + revision match) | Low | Saves ~12 ms on triple-select |
| Ensure warm completes before first geometry when user waits after select | Low | Avoids 8 ms `cache_miss` on first kind change |
| Shrink `focus_snap` (`snapshotFocusForSessionUndo` baseline trim) | Medium | Saves ~3–5 ms per warm — negligible vs apply gap |

**Do not implement:** `UndoWarmJob` FSM, baseline-map cursor slicing, cooperative scheduler for undo — profile does not justify complexity.

---

## Call-site map (apply path)

| Function | Role |
|----------|------|
| `handleCoarseFaderInput` / fine / pitch | Log `POSITION EDIT`; queue when playing |
| `queuePendingPlayingEdit*` | Hold latest target |
| `ControlSurfaceManager::update` | `processPendingPlayingEditGeometry` each frame |
| `processPendingPlayingEditGeometry` | warm → `moveNoteToPosition` / pitch / length |
| `EditManager::beginGeometryMutation` | kind-boundary undo push (~8 ms first time) |
| `NoteEditGeometryApply::applyNoteEditChange` | `NoteGeometryResolver::resolve` |
| `finishGeometryDriverSideEffects` | selection sync; F1 motor defer |
| `maybeUpdateDisplayForNoteEditSelection` | force display when revision bumped |

**Main loop order** (`main.cpp`): `handleMidiInput` → … → `controlSurfaceManager.update()` (includes `processPendingPlayingEditGeometry`) → `maybeUpdateDisplayForNoteEditSelection` → deferred load/display frame.

---

## Pre-implementation review (stream B)

### Ready

- Profile disproves undo-chunking hypothesis.
- First vs second move divergence is reproducible in capture.
- Per-frame geometry coalesce already shipped; gap persists on first apply.

### Resolved

| Topic | Decision |
|-------|----------|
| Primary bottleneck | Apply-path latency, not `buildSessionUndoEntry` |
| Undo chunking | Scrap |
| Next step | `GEOM_APPLY` instrumentation (stream A) |
| OpenSpec | Not required for instrumentation; `/opsx:propose` only if apply semantics change |

### Open before stream B fix

1. Pin queue age vs in-function gap from stream A capture.
2. Confirm transport running during first-move lag case.
3. Trace `ensureNoteEditFocusForLiveEdit` on first vs second `moveNoteToPosition`.

---

## Verification

| Stream | Automated | Manual |
|--------|-----------|--------|
| A (instrument) | — | Capture: `GEOM_APPLY` + `UNDO_WARM` timeline for first/second move |
| B (fix) | `pio test -e native` | NOTE_EDIT + PLAYING: first coarse/pitch updates display ≤35 ms |
| C (hygiene) | `test_note_edit_session_undo` unchanged | Optional: warm completes before move when idle after select |

---

## Implementation checklist

- [x] **Profile** — `UNDO_WARM` timing; [`session_20260805_234116.log`](../../captures/session_20260805_234116.log) analyzed
- [x] **Stream A** — `GEOM_APPLY` queue → dequeue → undo → focus → pipeline → done (`teensy41-capture-serial`)
- [x] **Stream B (partial)** — `pushEntry(SessionUndoEntry&&)`; cache-hit `std::move(kindBoundaryUndoCache_)` (no double extmem map copy)
- [x] **Stream B** — `UNDO_PUSH` phase timing; root cause: `getExternalMemoryPoolFreeBytes()` on hot path (fixed — optimistic extmem admit per `LoopEventStore`)
- [ ] **Stream B** — fix root cause from A (first-move apply latency)
- [ ] **Stream C** — (optional) warm coalescing + `focus_snap` trim

---

## Scrap log (why old phases were removed)

| Old phase | Reason removed |
|-----------|----------------|
| UndoWarmJob FSM | Undo is 6–8 ms; structure without user benefit |
| Baseline diff slicing | `baseline_probe` + `edit_rows` &lt; 200 µs combined |
| Cooperative undo scheduler | No frame starvation from undo |
| Pin session / incremental warm slices | Solves wrong problem |

Keep `UNDO_WARM` instrumentation until stream B is fixed — useful regression signal for undo regressions, not for display lag.
