# RC5 — Incremental committed-display handoff

**Status:** Investigation required before RC5c implementation  
**Parent:** [`long_overdub_record_tail_wrap_display_bugfix.md`](long_overdub_record_tail_wrap_display_bugfix.md) (RC4f–RC4i)  
**Related:** [`long_overdub_post_stop_display_handoff_bugfix.md`](long_overdub_post_stop_display_handoff_bugfix.md) (RC2)  
**Persistence dependency:** [`long_overdub_stage5a3_critical_reclaim_verification_refinement.md`](long_overdub_stage5a3_critical_reclaim_verification_refinement.md) (5a-3)

**Evidence:** User report after RC4i; captures `session_20260811_170314.log`, `session_20260811_165148.log`

---

## Purpose

Before implementing RC5c's proposed deferred full-loop gather, verify whether the committed display representation can be updated **incrementally** at overdub stop instead of discarding existing display notes and reconstructing the entire loop.

RC5 correctly identifies:

- separate overdub-stop handoff;
- revision-gated display authority;
- preserved-frame / stale-while-revalidate behavior;
- avoiding synchronous full-loop work on transport transitions.

However, **RC5c must not treat a revision change as automatic justification for full committed gather**. That is a **fallback**, not the normal architecture.

---

## Problems (UX)

| Symptom | Impact |
|---------|--------|
| Overdub-stop flash | Record-only frame; vertical pitch jump; overdub reappears on next frame(s) |
| Transition lag (~1–2 s) | OLED stutters after overdub stop / play/stop; pre-RC4 felt idle-paced between 30 FPS ticks |

RC4f–RC4i correctness is largely acceptable; RC5 targets **handoff polish** and **main-loop budget**.

---

## Root causes (code-backed)

### A — Overdub stop uses record-stop handoff

[`Track::stopOverdubbing`](../../src/Track/TrackOverdubLifecycle.cpp) calls [`refreshViewportAfterRecordStop`](../../src/DisplayManager/DisplayCacheLifecycle.cpp), which applies `clampPreservedDisplayNoteCount` — strips capture suffix. During overdub, `liveDisplayNotes` = committed record + `capturePreview` overdub. After commit + clamp, overdub rows vanish until rebuild. RC2 invariant applies to **record stop**, not **overdub stop**.

### B — Stale `visualCache` after commit

`commitCaptureForStop` → `markDisplayCachesStale` keeps old `visualCache.notes`. RC4g can assign stale cache without matching `playbackRevision`.

### C — Synchronous heavy work on transitions

RC4i full-loop gather in `rebuildCommittedLayer`; RC4g full-vector assign each PLAYING frame; `emitOverdubStopDisplaySnapshot` → full `resolveDisplayNotes` on stop stack; stacks with `SC_REC_FLUSH_PENDING_REVTS(256)`.

---

## Architecture question (checkpoint before RC5c code)

> If the display already contains committed notes plus incrementally maintained `capturePreview`, can overdub commit **promote or patch** that representation instead of rebuilding all committed display notes from canonical MIDI?

**Preferred model:**

```text
overdub:     committed display + capturePreview → display

overdub stop: commit capture → promote/merge capture display → committed display → window projection → display
```

**Not:**

```text
overdub stop: commit → invalidate → gather entire loop → reconstruct all display notes → window projection
```

**Key architectural goal:**

> A committed display revision should be incrementally maintainable. A revision change is not, by itself, justification for discarding all existing display notes and rebuilding the full loop.

| Checkpoint | Answer (target) |
|------------|-----------------|
| Owner | `DisplayManager` compose + `Loop::capturePreview` / `visualCache`; commit exposes delta |
| Ownership change? | **NO** — extend existing preview/compose path |
| State transition change? | **NO** on commit/seal; display sync scheduling only |
| Stop path | No sync full-loop rebuild in `commitCaptureForStop` / `stopOverdubbing` |

---

## Investigation required before RC5c implementation

### 1. Audit `capturePreview`

**Primary files:** [`include/VisualCache.h`](../../include/VisualCache.h) (`CapturePreview`), [`src/Loop/LoopCapture.cpp`](../../src/Loop/LoopCapture.cpp) (`applyCaptureEventToPreview`), [`src/DisplayManager/DisplayNoteResolveLiveCapture.cpp`](../../src/DisplayManager/DisplayNoteResolveLiveCapture.cpp) (`replaceCaptureLayer`, `synchronizeCaptureLayer`).

Document:

- where preview notes are created;
- whether preview notes contain the same geometry as committed display notes;
- note identity representation;
- tick ordering;
- wrap-span support;
- whether preview can become committed display notes without `reconstructDisplayNotes`;
- what is discarded when capture ends (`capturePreview.clear()` in commit path).

Reuse prior capturePreview incremental-append audit if available.

### 2. Audit overdub commit delta

**Primary files:** [`src/Track/TrackCaptureStopCommit.cpp`](../../src/Track/TrackCaptureStopCommit.cpp) (`commitCaptureForStop`), [`src/Loop/LoopCapture.cpp`](../../src/Loop/LoopCapture.cpp) (seal/commit).

At overdub→canonical transition, identify whether commit already exposes:

- added / removed / replaced events or notes;
- affected `NoteId`s;
- affected tick range(s);
- `playbackRevision` bump.

Do **not** add a new owner to manufacture delta if commit/capture structures already hold it.

### 3. Test simplest handoff

Investigate whether normal overdub stop can be:

```text
committedDisplay += capturePreview   (or adopt capturePreview; then clear preview)
```

without:

```text
gatherCommittedEventsInWindow(0, loopLength) + reconstructDisplayNotes(...)
```

If direct promotion fails, determine whether bounded merge of affected range suffices.

### 4. Incremental hierarchy (implementation preference)

1. **Promote/adopt capture display notes**
2. **Patch added/removed/changed notes**
3. **Rebuild only affected tick range**
4. **Full committed representation — fallback/recovery only**

Do not jump from "revision changed" to "full-loop rebuild" by default.

---

## Revised RC5c direction

Replace:

> `playbackRevision` bump → mark `visualCache` dirty → defer full gather.

With:

> `playbackRevision` bump → identify committed delta → incrementally update committed display where possible.

Revision mismatch must **not** automatically invalidate the entire display representation.

```text
revision N + committed display A
    → commit overdub (delta D) → revision N+1
    → A + D → committed display B
```

not discard A and rebuild B from scratch.

---

## RC5a / RC5b (unchanged intent)

### RC5a — Overdub-stop handoff

- Separate handoff from record stop.
- Record stop may strip capture suffix; overdub stop must **not**.
- Preserve composed frame until revision-matched committed representation is ready.

### RC5b — Revision-gated authority

- Stale `visualCache` never authoritative for new revision.
- Revision mismatch triggers **incremental sync** where possible, not automatic full reconstruction.

---

## RC5d (unchanged intent)

Steady-state PLAYING:

```text
revision-matched committed representation → cheap window projection → OLED
```

not full `visualCache.notes` vector copy every frame. Use [`resolveWindowedDisplayNotes`](../../src/DisplayManager/DisplayNoteWindowGather.cpp) filter path when cache is covered and revision-matched.

---

## Relationship to 5a-3 (persistence / reclaim)

Display must not become another hidden full-loop duplication or allocation pressure source.

Full reconstruction risks:

```text
canonical chunks + gathered events + reconstructed display notes + temporary vectors
```

RC5 implementation must answer:

- Does incremental promotion avoid full-loop temporary allocation?
- Does it avoid duplicating canonical MIDI events?
- Any new long-lived full-loop representation?
- Allocation on overdub stop?
- Allocation during critical reclaim / deferred persistence?
- Bounded maintenance under 5a-3 memory-pressure conditions?
- New display cache consuming chunk headroom needed for reclaim?

**Constraint:** Do not solve display lag with another unbounded or duplicated full-loop representation.

---

## Implementation order

| Stage | Work |
|-------|------|
| **RC5a** | Overdub-stop handoff semantics (no capture clamp) |
| **RC5b** | Prevent stale `visualCache` as authority after revision change |
| **RC5c-investigation** | Audits 1–4 + memory cross-check vs 5a-3 (this doc § Investigation) |
| **RC5c-implementation** | Cheapest proven incremental mechanism; full-loop only as fallback |
| **RC5d** | Filter-not-copy PLAYING path |
| **RC5e** | Defer stop snapshot resolve only if profiling still shows stop-stack cost |

---

## Investigation acceptance checklist

- [ ] Can `capturePreview` display notes become committed display notes without reconstruction?
- [ ] If not, what information blocks direct promotion?
- [ ] Can committed display be patched from overdub commit delta?
- [ ] If not, can only the affected tick range be rebuilt?
- [ ] Worst-case allocation for each approach?
- [ ] Preferred approach avoids full-loop temporary materialization?
- [ ] Preserves RC4f–RC4i correctness (wrap tail, rolling window, overview full-loop density)?
- [ ] Compatible with pending 5a-3 critical-reclaim / persistence goal?
- [ ] Full-loop reconstruction retained only as explicit fallback/recovery?

---

## Device acceptance (after RC5c implementation)

| Check | Pass |
|-------|------|
| Overdub stop | No record-only flash; no vertical pitch jump |
| Overdub stop → PLAYING | Rolling window within ~one frame (~33 ms), not 1–2 s stall |
| Loop wrap during overdub | Record tail visible (RC4f guard) |
| Overview minimap | Full-loop density; not window-only |
| `#CAP` / `DFRAME` | No multi-second `DFRAME` gap after stop |

Build gates: `pio test -e native`; `pio run -e teensy41-capture-serial`.

---

## Desired end state

```text
                 canonical MIDI
                       │
                 commit delta
                       │
          ┌────────────┴────────────┐
          │                         │
    capture preview            existing display
          │                         │
          └──────────┬──────────────┘
                     ▼
             incremental update
                     │
                     ▼
          revision-matched display
                     │
                     ▼
              window projection
                     │
                     ▼
                    OLED
```

With preserved previous frame as fallback while update is pending, and full-loop reconstruction as recovery/exception only.

---

## Out of scope

- Changing `commitCaptureForStop` seal/undo ownership
- Stage 5a append-deny / reclaim telemetry (separate; cross-check only)
- Synchronous `ensureVisualCacheBuilt` on every transport button
