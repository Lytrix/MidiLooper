# NOTE_EDIT hydrate solution path

**Status:** Decided — not now. Architecture PASS; firmware not authorized.  
**Date:** 2026-08-16  
**Kind:** enhancement  
**Work identity:** this file. Separate from parked wrap-move persist, LCR 6C/6D/6.3, and scheduler grooming.  
**Architecture (proposal):** [`note_edit_selectedtick_lcr_resolution_architecture.md`](note_edit_selectedtick_lcr_resolution_architecture.md)  
**Authority:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype) amendment 2026-08-16  
**Overdub analog:** [6E](loop_content_resolution_overdub_state_evaluation_refinement.md) `resolveState(tick)` consume  
**Does not authorize:** firmware; a new OpenSpec change; a new DEC; `EditHydrateSession` / resumable open FSM; folding into grooming Slice 5

---

## What this work is

NOTE_EDIT still rematerializes the loop on open (`openNoteEditSession` → `rebuildVisualCacheFromPasses` + `rematerializeEditView`). Overdub already left that model: consume prepared LoopContentResolution (LCR) around `currentTick`, identity lookup, overlay compose.

This work is that same consume shape for the editor. Query origin is `selectedTick`. The proposal is the architecture. This file is the **work path** so it is not a leftover LCR 6.4 production-swap task.

Play / display / analyze / LEDs are consumers ([DerivedViews.md](../Authority/Architecture/DerivedViews.md) § Consumers). Hydrate moves NOTE_EDIT **analyze** onto prepared `LoopContentResolution` around `selectedTick`. It does not make LCR the piano-roll list and does not delete `visualCache`.

Grooming already named the gap **NOTE_EDIT hydrate** and forbade folding it into idle-slice / `ensureVisualCacheBuilt` audits. That name stays for this work. It is **not** `lazy-slot-hydration` (SD load to COMMITTED) and not undo-snapshot hydrate.

---

## Solution (from the proposal — do not invent a second one)

```text
prepared LCR ∪ NoteEditCurrentState overlay
    │
    ├─ Select: tickEvents / spanBoundaries neighborhood around selectedTick
    ├─ Overlap: indexed identities → appendNoteEvents (same hold as overdub)
    └─ Paint / audition: visualCache + settled overlay (not a second loop)
```

Same 6.0 rule as overdub: NOTE_EDIT open and fader **consume** already-prepared LCR. Miss is legacy compatibility (`visualCache` / today’s semantics), not a second analysis authority. Never construct / sort / checkpoint / `ensure*` LCR on those stacks. Path B (live geometry into LCR) stays forbidden.

Stages, invariants, overlay lifecycle, and work-shape gates live in the proposal. Do not duplicate them here. Execute them in order when this work is in CURRENT_WORK § Now implementing.

| Stage | Owner | One-line invariant |
|-------|--------|--------------------|
| 0 | docs | Pins recorded (done) |
| 1 | `SelectNavigation` | Neighborhood select; not `resolveState` |
| 2 | overlap candidate source | Indexed identities; `appendNoteEvents` identity-bounded |
| 3 | `NoteGeometryResolver` | Prepared hit is analysis authority; miss is legacy |
| 4a | `openNoteEditSession` | Open does not rematerialize the loop to analyze |
| 4b | Stage 1 after 4a | First select uses the neighborhood query |
| 4c | `ensurePlaybackMergedMidiEventsBuilt` | Playback window + overlay; not `sessionMidiEvents()` replace |
| 5 | device | 16-bar / ~1000 notes; no `VCACHE,full` on select/overlap |

---

## What this work is not

| Not | Why |
|-----|-----|
| Remaining LCR `loop-content-resolution` 6.4 firmware | Producer stays DEC-037. This is a **consumer** work item. OpenSpec 6.4 is a pointer only. |
| Wrap-move persist / UNDO_WARM | Wrap-move persist is **parked** (current rematerialize / session-store structure is part of the defect). Do not mix persist LIFO patches into this work. Re-evaluate [`201446`](../../captures/session_20260816_201446.log) after hydrate if the shorten remains. |
| Grooming Slice 4e / 4f / 5 | Grooming must not implement editor hydrate. |
| `lazy-slot-hydration` | SD slot load. Different owner (`StorageManager` / `LoadLoopJob`). |
| A new hydrate session that resumes across `loop()` before NOTE_EDIT is ready | That is a **state-transition change** (defer open until hydrate finishes). The proposal’s checkpoint is **NO**. Consume-when-ready + legacy miss, same as 6C. |
| New types: `EditHydrateSession`, `EditSourceView`, `NoteEditSourceView` | NAMING / DEC-037 rejected new domain nouns. Working set is a Runtime Request: prepared LCR × interval around `selectedTick`. |
| Redesign `rematerializeEditView` / `materializeToEventVector` / `EditSession.store` | Prove the consumer first. |

---

## Prerequisites (already decided; not all live)

| Prerequisite | Status |
|--------------|--------|
| LCR `resolveState` / `resolveWindow` native | PASS |
| 6.0 no construct on button | accepted — same rule on NOTE_EDIT open / fader |
| 6A / 6C consume-when-ready | 6A device PASS; 6C native |
| 6D.4 post-commit publish | landed; not all of LCR live |
| 6E overlap query | native PASS; not wired to Track |
| Wrap-move persist | **parked** — current-structure issue; not a start gate |

Start firmware only when this file is in CURRENT_WORK § Now implementing.

---

## When this work starts

1. Pre-implementation review against the proposal (owners, stages 1→5, work-shape gates).
2. One stage per session unless the user authorizes more.
3. Native first. Commit each verified stage.
4. Do not open a second OpenSpec change unless a later stage needs a spec delta beyond the DEC-037 amendment.

---

## Files (when authorized)

Listed in the proposal. Expected first owners: `SelectNavigation`, `EditSelectNoteState`, `NoteGeometryResolver`, `NoteEditSessionLifecycle`, `TrackPlaybackWindowBuild`.
