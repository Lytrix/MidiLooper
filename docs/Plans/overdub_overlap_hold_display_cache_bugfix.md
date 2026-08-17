# Overdub occupied lane — source-view consume + display parity (RC11 / RC12)

**Status:** RC11 shipped (2026-08-17); RC12 queued  
**Date:** 2026-08-17  
**Kind:** bugfix  
**Parent:** [`overdub_overlap_hold_same_start_bugfix.md`](overdub_overlap_hold_same_start_bugfix.md)  
**Authority diagram:** [`overdub_lifecycle_representation_authority.md`](overdub_lifecycle_representation_authority.md) — lifecycle + representation authority (debugging; not a state-machine spec)  
**Evidence:** [`132647`](../../captures/session_20260817_132647.log), [`132857`](../../captures/session_20260817_132857.log)

---

## Invariant (one sentence)

**An overdub note must consume the currently resolved occupied geometry for that pitch, and every representation (persist, source rebuild, display) must show that same resolved geometry.**

Product rule:

> During overdub, if the incoming note occupies an already occupied pitch/time lane, the existing sounding material must be shortened or hidden, and every subsequent representation must show that same result.

Three responsibilities: **consume** (which note is displaced) → **persist** (Hide/Shorten sealed) → **display** (same picture).

**RC12 depends on RC11.** RC11 establishes correct consumption and persistence; RC12 removes the competing display reconstruction. Do not land RC12 before RC11 is verified.

---

## Architecture checkpoint

| Question | RC11 | RC12 |
|----------|------|------|
| **Ownership change?** | NO — extend `Loop::accumulatePendingNoteChangesForIncomingNote` | NO — extend display/cache call sites |
| **State transition change?** | NO | NO |
| **New overlap resolver?** | **FORBIDDEN** — representation/selection only | **FORBIDDEN** |

RC11/RC12 are **not** another overlap owner. They make the existing **source view** authoritative enough for the existing consume path, then make display use the same representation.

---

## Problem

At note-off, `selected` is built from:

1. notes in `overlapNoteIds` (sounding at hold start S), and  
2. notes **newly** merged by `ensureOverdubSourceNotesForHold` (JIT).

Missing third case:

```text
note already in overdubSourceViewNotes_
AND overlaps incoming span
AND not in overlapNoteIds (or IDs incomplete)
AND not newly JIT-merged (merged=0)
```

Hold IDs and source view are **not equivalent**: IDs = “sounding at S”; source view = current resolved geometry in the overdub window. [`132647`](../../captures/session_20260817_132647.log): `merged=0` on every hold; record 60@64–288 stays; Add-only at note-off; LCR wrap `notes=4` vs `slice_clean notes=5`.

Separate failure (RC12): visual cache uses `appendOverdubPassDisplayNotes` after LCR idle slice, re-inserting overdub geometry beside an un-hidden record row — display disagrees with `rebuildOverdubSourceView`.

North star:

```text
incoming note
    → source view + pending changes
    → consume (existing path)
    → sealed edit changes
    → resolved source view
        ├── next consume
        └── display
```

Not: visual cache independently reinterpreting layered overdub state.

---

## RC11 — establish correct overdub consumption

**Owner:** `Loop::accumulatePendingNoteChangesForIncomingNote` ([`src/Loop/LoopPendingNoteChange.cpp`](../../src/Loop/LoopPendingNoteChange.cpp))

**Change:** After hold-ID lookup and JIT merge, **complete** `selected` from the authoritative source view:

```text
incoming span
      ↓
active source-view window (resolveOverdubSourceWindow at consumeStart)
      ↓
same pitch only (notes already in overdubSourceViewNotes_)
      ↓
overlap test with [consumeStart, consumeEnd)
      ↓
union into selected (if not already present)
```

- This is a **completion** step, not `if (overlapNoteIds.empty())`. IDs can be non-empty but incomplete (e.g. A in IDs, B in view, both overlap incoming → `selected = {A, B}`).
- Bounded to the **active overdub source window** — not a full-loop scan. Only same-pitch notes already in `overdubSourceViewNotes_` that intersect that window are eligible.

Still forbidden here:

- Full-loop materialize / dump all pitches into source view  
- Gate 3 empty-set **full-session** scan (only same-pitch notes already in the overdub source view)  
- `resolveState` on MIDI path  
- New consume owner  

**Observability:** Emit `#CAP,DIAG,overlap_hold` from `stopOverdubbingToStopped` (same `logOverdubStopStage("seal")` as overdub→PLAYING stop).

**Stage commit:** RC11 only — one root cause (consume selection).

---

## RC12 — display uses the same resolved representation

**Depends on:** RC11 shipped and native tests green.

**Owners:**

1. [`DisplayManager::resolveDisplayNotesLiveCapture`](../../src/DisplayManager/DisplayNoteResolveLiveCapture.cpp) — while `hasOverdubSourceView()`, committed prefix from `overdubSourceViewNotes_` (+ capture preview), not `visualCache`. RC10 pending paint copy unchanged for in-bar Hide/Shorten before seal.
2. [`Loop::rebuildVisualCacheIdleSlice`](../../src/Loop/LoopVisualCache.cpp) — when slice used LCR (`tryResolvePreparedWindow` / `resolveWindow`), do **not** call `appendOverdubPassDisplayNotes` (1-bar loop always hits wrap-edge append today).

**Do not** “fix” `appendOverdubPassDisplayNotes` itself. RC12 is: **do not run a second reconstruction after the authoritative LCR picture** — not making the old flatten path smarter.

**Stage commit:** RC12 only — representation parity.

---

## Forbidden (both RCs)

- Full-loop `materializeToEventVector` on consume or display hot path  
- Visual cache as consume input  
- `resolveState` on MIDI/button path  
- New top-level overlap resolver / Manager  
- State-transition or ownership changes  
- Re-litigate frozen RC8/RC9/RC10  

---

## Native tests

| Test | Invariant |
|------|-----------|
| `test_overdub_consumes_existing_source_view_overlap` (rename from `test_empty_overlap_ids_add_only_when_source_overlaps`) | In-view overlap + **empty** hold IDs → Hide or Shorten, not Add-only |
| `test_overdub_consumes_source_view_when_hold_ids_incomplete` (**new**) | Source view A+B; hold IDs **{A only}**; incoming overlaps A+B → `selected` includes both → Hide/Shorten on B |
| Keep RC8 64-bar JIT tests | Ahead note via JIT when not in enter view |
| `test_pending_hide_applies_to_display_notes_not_source_view` | RC10 paint copy (unchanged) |

Rename rationale: invariant is **occupied source-view overlap must not become Add-only**, not “empty IDs behave differently.”

---

## Device gate (order: data first, then pixels)

Same 1-bar occupied lane as evidence captures. Prove layer by layer.

### Gate 1 — consume

**Primary proof:** native assertion or serial that incoming 60@64–288 → `selected` contains existing 60@64–288 → `resolveConstrainedGeometry` → pending Hide or Shorten at note-off.

**Corroborating telemetry:** `#CAP,DIAG,overlap_hold` hide/shorten > 0 at stop (delayed; not the sole Gate 1 criterion).

### Gate 2 — persistence

After wrap: sealed companion EditPass(es) for the displaced record noteId.

### Gate 3 — source rebuild

`DIAG,lcr,src,why=wrap,notes=N` — resolved source view matches committed geometry (no ghost record at same start).

### Gate 4 — display

`slice_clean notes=N` matches LCR N (not N+1). Live `DISP` committed picture matches source-view resolution after RC12. Stop `DNTE`: one 60@64, not stacked 176 + 224.

### Gate 5 — regression

1-wrap [`005745`](../../captures/session_20260817_005745.log) green; wrap `beginCapture` must not emit `why=open`.

---

## Implementation files (expected)

| RC | Files |
|----|--------|
| RC11 | `LoopPendingNoteChange.cpp`, `TrackStopTelemetryColdHelpers.cpp` or `TrackOverdubLifecycle.cpp` (overlap_hold on transport stop), `test_pending_note_change.cpp` |
| RC12 | `DisplayNoteResolveLiveCapture.cpp`, `LoopVisualCache.cpp`, optional visual-cache native fixture |

---

## Pre-implementation review

### Ready

- Root cause: incomplete `selected` at note-off, not missing geometry engine  
- RC11 before RC12 sequencing  
- Partial-ID test blocks empty-only conditional  

### Failure tree (implementation)

```text
overdub occupied lane
        → source-view overlap selected at note-off?  NO → RC11
        → Hide/Shorten persisted?                      NO → inspect seal
        → rebuilt source view correct?                 NO → inspect seal/LCR
        → display matches source?                      NO → RC12
        → PASS
```

### Proceed

YES — RC11 shipped; implement RC12 next (separate stage commit).

---

## RC11 validation (2026-08-17)

- `Loop::accumulatePendingNoteChangesForIncomingNote` completes `selected` from `overdubSourceViewNotes_` bounded by active source window (`DisplayWindowUtils::noteIntersectsWindow`) + incoming overlap.
- `stopOverdubbingToStopped` emits `logOverdubStopStage(..., "seal")` with `overlap_hold` telemetry.
- Native: `test_overdub_consumes_existing_source_view_overlap`, `test_overdub_consumes_source_view_when_hold_ids_incomplete` — 1294/1294 pass.
- Firmware: `teensy41-capture-serial` RAM1 free **4512**.
- Device gates 1–5: pending HITL after flash.
