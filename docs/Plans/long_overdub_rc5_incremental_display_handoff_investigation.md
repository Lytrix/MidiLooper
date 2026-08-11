# RC5 — Incremental committed-display handoff

**Status:** RC5a–RC5f shipped; device verify PASS for overdub→PLAYING; STOPPED handoff pending device verify  

**Parent:** [`long_overdub_record_tail_wrap_display_bugfix.md`](long_overdub_record_tail_wrap_display_bugfix.md) (RC4f–RC4i)  
**Related:** [`long_overdub_post_stop_display_handoff_bugfix.md`](long_overdub_post_stop_display_handoff_bugfix.md) (RC2)  
**Persistence dependency:** [`long_overdub_stage5a3_critical_reclaim_verification_refinement.md`](long_overdub_stage5a3_critical_reclaim_verification_refinement.md) (5a-3)

**Evidence:** User report after RC4i; captures `session_20260811_170314.log`, `session_20260811_165148.log`; verify `session_20260811_174742.log`

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

## Investigation findings (RC5c-investigation)

### 1. `capturePreview` audit

| Topic | Finding |
|-------|---------|
| Created | `Loop::appendCaptureEventWithResult` → `applyCaptureEventToPreview` (`LoopInternalColdHelpers.cpp`) |
| Geometry | Same `NoteUtils::DisplayNote` fields as committed display (`note`, `velocity`, `startTick`, `endTick`); wrap splits into tail + head segments |
| Note identity | `noteId` stays `kInvalidNoteId` — preview does not assign NoteIds. Paint-safe; note-edit identity is not the handoff consumer |
| Ordering | Append order of closed notes; not a sorted full-loop reconstruct |
| Wrap support | Yes — preferred wrap path + `WrapHeadSegment` head rows |
| Become committed without reconstruct? | **Yes for display paint** — composed `liveDisplayNotes` already is `committed layer + capturePreview.notes` via `replaceCaptureLayer` / `synchronizeCaptureLayer` |
| Discarded on commit | `capturePreview.clear()` in `commitPendingCapturePass` / `discardCapture` / `beginCapture` — after seal, promote must use the **already-composed** `liveDisplayNotes`, not re-read preview |

Prior incremental-append design: [`multi_track_playback_pressure_closure_refinement.md`](multi_track_playback_pressure_closure_refinement.md); tail parity: [`long_overdub_capture_preview_tail_parity_bugfix.md`](long_overdub_capture_preview_tail_parity_bugfix.md).

### 2. Overdub commit delta audit

| Topic | Finding |
|-------|---------|
| Added/removed events | Commit seals capture chunks into an `OverdubPass`; no separate display-note delta API |
| Affected NoteIds | Not exposed as a display delta at stop |
| Affected tick range | Preview has `dirtyBars` / `changedNoteIndices` while capture is live; cleared with preview on commit |
| `playbackRevision` | Bumped in seal/publish (`++playbackRevision`) + `markDisplayCachesStale()` |

**Decision:** Do not invent a new delta owner. The composed display vector **is** the stop-time delta carrier (committed prefix + capture suffix already merged for paint).

### 3. Simplest handoff test

```text
trim playhead tails → preserve liveDisplayNotes → promote into visualCache → match playbackRevision
```

Without `gatherCommittedEventsInWindow(0, loopLength)` + `reconstructDisplayNotes`.

Temporary per-frame tails live beyond `liveDisplayCacheBaseNoteCount_`; `preservedOverdubStopDisplayNoteCount` drops those only.

### 4. Hierarchy choice

**Selected:** (1) Promote/adopt composed capture display notes into `visualCache` at overdub stop.  
Full-loop gather/reconstruct remains the existing recovery path when no preserved frame exists (window gather / idle rebuild).

### 5a-3 memory cross-check

| Question | Answer |
|----------|--------|
| Avoid full-loop temporary allocation? | Yes — no gather buffer + reconstruct on the stop handoff path |
| Avoid duplicating canonical MIDI? | Yes — adopts `DisplayNote`s only; chunk refs unchanged |
| New long-lived full-loop representation? | No — writes the existing `visualCache.notes` slot |
| Allocation on overdub stop? | One `DisplayNoteVec::assign` into `visualCache` (ExternalMemoryFirst); replaces prior cache contents |
| During critical reclaim / deferred persistence? | Handoff does not allocate capture chunks or call reclaim |
| Bounded under memory pressure? | Same capacity class as prior `visualCache`; does not grow chunk pool |
| Steal chunk headroom? | No — display notes are not chunk-pool consumers |

---

## Investigation acceptance checklist

- [x] Can `capturePreview` display notes become committed display notes without reconstruction?
- [x] If not, what information blocks direct promotion? — `noteId` unset (paint OK); promote from composed `liveDisplayNotes` after preview clear
- [x] Can committed display be patched from overdub commit delta? — No separate delta API; composed vector is the delta
- [x] If not, can only the affected tick range be rebuilt? — N/A for chosen promote path
- [x] Worst-case allocation for each approach? — Promote: one DisplayNote assign; full gather: events + reconstruct temps (fallback only)
- [x] Preferred approach avoids full-loop temporary materialization?
- [x] Preserves RC4f–RC4i correctness (wrap tail, rolling window, overview full-loop density)?
- [x] Compatible with pending 5a-3 critical-reclaim / persistence goal?
- [x] Full-loop reconstruction retained only as explicit fallback/recovery?

---

## Implementation shipped

| Stage | Change |
|-------|--------|
| **RC5a** | `refreshViewportAfterOverdubStop` — no `clampPreservedDisplayNoteCount`; trim tails via `preservedOverdubStopDisplayNoteCount`; `stopOverdubbing` / in-edit fold call it |
| **RC5b** | `resolveDisplayNotesCommitted` — `committedDisplayVisualCacheAuthoritative`; dirty cache never overwrites revision-matched preserved frame |
| **RC5c** | Overdub-stop adopt: `visualCache.setNotes(liveDisplayNotes)` + clear dirty; no sync full-loop gather on stop |
| **RC5d** | PLAYING + clean cache → `resolveWindowedDisplayNotes` filter path (cache-hit reuse); not full-vector copy every frame |
| **RC5e** | Overdub→STOPPED (+ in-edit fold→STOPPED): same `refreshViewportAfterOverdubStop` promote as →PLAYING |
| **RC5f** | STOPPED uses incremental committed display path (`preferIncrementalCommittedDisplay`) |

---

## Device acceptance (after RC5c implementation)

| Check | Pass |
|-------|------|
| Overdub stop | No record-only flash; no vertical pitch jump — **PASS** (user: fluidity improved immensely; `174742`) |
| Overdub stop → PLAYING | Rolling window within ~one frame (~33 ms), not 1–2 s stall — **PASS** (same) |
| Loop wrap during overdub | Record tail visible (RC4f guard) |
| Overview minimap | Full-loop density; not window-only |
| `#CAP` / `DFRAME` after overdub→PLAYING | No multi-second gap — **PASS** for that path |
| Transport / play stop → STOPPED | **PASS** — [`180107`](../../captures/session_20260811_180107.log): `PLAYING→STOPPED` DISP +1.6 ms, `visualCache=1222` stable (not mid-idle 768→793 as on `174742`) |

Build gates: `pio test -e native` 995/995; `pio run -e teensy41-capture-serial` SUCCESS; commits `dc2bffa` (RC5a–d), `ce5390b` (RC5e/f).

---

## RC5 follow-up — STOPPED display handoff (investigation)

**Capture:** [`session_20260811_174742.log`](../../captures/session_20260811_174742.log)

### What the capture shows

There is **no** `ST,Track,PLAYING,STOPPED` in this session. The visible stop rebuild is transport stop while overdubbing:

| Marker | Evidence |
|--------|----------|
| User action | Global Transport short press @ 203.377s |
| Path | `Track::stopOverdubbingToStopped` — log `Overdubbing stopped (to STOPPED)` @ 203.459s |
| State | `#CAP,203438231,ST,Track,OVERDUBBING,STOPPED` |
| First DISP | `#CAP,203459927,DISP,4,STOPPED,36864,313,768,313,313,1,24576,16,282` — `visualCache.notes=768`, window frame=313 |
| Later DISP | `#CAP,205009808,...282,793,...` — cache still growing under STOPPED (768→793) |
| Frame gap | Next `DFRAME` after stop DISP: `#CAP,204368466,DFRAME,313,...` (~0.9 s after `203459927`) |

RC5a/c handoff runs only on `stopOverdubbing` → PLAYING (and in-edit fold → PLAYING). **`stopOverdubbingToStopped` does not call `refreshViewportAfterOverdubStop`**, so commit still leaves `visualCacheDirty` and the composed capture frame is not adopted.

### Same gap on `stopPlaying` (PLAYING→STOPPED)

`Track::stopPlaying` only `setState(TRACK_STOPPED)` + `emitDisplayCaptureSnapshot`. Resolve path:

```text
deferVisualRebuild = (isPlaying || isStoppedRecording) && !overdubbing && …
```

Under `TRACK_STOPPED`, `deferVisualRebuild` is **false**, so RC5b preserved-handoff and RC5d’s defer-gated clean-cache preference do **not** apply. Long-loop STOPPED falls through to `resolveWindowedDisplayNotes`, which **gather+reconstructs** when `visualCache` is dirty.

So yes — the same promote/preserve model applies to any transition into STOPPED that would otherwise paint from a dirty cache:

1. **Overdub → STOPPED:** call `refreshViewportAfterOverdubStop` (same RC5a/c) before snapshot emit.
2. **PLAYING → STOPPED:** if `visualCache` already clean/revision-matched, prefer filter/reuse (extend RC5d so STOPPED is not excluded); if dirty, preserve last PLAYING frame until idle finishes — do not sync full gather on the stop button stack.

### Architecture checkpoint (before coding follow-up)

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — extend existing `refreshViewportAfterOverdubStop` / `resolveDisplayNotesCommitted` |
| State transition change? | **NO** — display sync scheduling only |
| Stop-path seal/commit? | Unchanged |

### Implementation shipped (RC5e / RC5f)

| ID | Work |
|----|------|
| **RC5e** | `stopOverdubbingToStopped` + in-edit fold → STOPPED: `refreshViewportAfterOverdubStop` before snapshot emit |
| **RC5f** | `preferIncrementalCommittedDisplay(deferVisualRebuild, track.isStopped())` — clean-cache window filter + revision-matched preserve under STOPPED |

### Device verify PASS — `session_20260811_180107.log`

| Marker | Evidence |
|--------|----------|
| Overdub → PLAYING | `#CAP,204945323,ST,Track,OVERDUBBING,PLAYING` then DISP `visualCache=1222` (adopted; not partial) |
| Play → STOPPED | `#CAP,208434047,ST,Track,PLAYING,STOPPED` → DISP `#CAP,208435685,...STOPPED,...,1222,310...` (+1.6 ms) |
| Cache stability | STOPPED frames keep `visual=1222` (contrast `174742` 768→793 under dirty idle) |
| User | Fluidness confirmed after RC5e/f flash |

Firmware: `ce5390b`.

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
