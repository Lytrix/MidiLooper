# NOTE_EDIT hydrate solution path

**Status:** Hydrate 2–3 device functional PASS [`231008`](../../captures/session_20260908_231008.log). Hydrate 1 device functional PASS [`224533`](../../captures/session_20260908_224533.log). Bound to [DEC-055](../DECISION_LOG.md#dec-055--note-edit-working-set-is-overlay-delta-on-opened-loopsnapshot). Do not start hydrate 4a–5.  
**Date:** 2026-08-16 (overlap retarget 2026-08-17)  
**Kind:** enhancement  
**Work identity:** this file. Separate from parked wrap-move persist, closed LCR 6C/6D, playback gather, and scheduler grooming.  
**Architecture (proposal):** [`note_edit_selectedtick_lcr_resolution_architecture.md`](note_edit_selectedtick_lcr_resolution_architecture.md)  
**Sibling (overlap participants):** [`overdub_participant_loop_content_architecture.md`](overdub_participant_loop_content_architecture.md) §5 NOTE_EDIT sibling  
**Authority:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype) amendment 2026-08-16  
**Overdub analog:** notes present vs incoming hold LinearSpan (same participant class). Select stays a `selectedTick` neighborhood.  
**Does not authorize:** hydrate 4a–5 firmware; a new OpenSpec change; a new DEC; `EditHydrateSession` / resumable open FSM; folding into grooming Slice 5; Stages 1–5 from “LCR × interval around `selectedTick`”

---

## What this work is

NOTE_EDIT still rematerializes the loop on open (`openNoteEditSession` → `rebuildVisualCacheFromPasses` + `rematerializeEditView`). Overdub already left that model: participant discovery vs the incoming hold LinearSpan, identity lookup, overlay compose.

This work is that same consume shape for the editor. Overlap query origin is the **selected / mover LinearSpan**, not `selectedTick`. Select encoder origin stays `selectedTick` (neighborhood navigation). The proposal is the architecture. This file is the **work path** so it is not a leftover LCR 6.4 production-swap task.

Play / display / analyze / LEDs are consumers ([DerivedViews.md](../Authority/Architecture/DerivedViews.md) § Consumers). Hydrate moves NOTE_EDIT **analyze** onto the overdub sibling participant query. It does not make LCR the piano-roll list and does not delete `visualCache`. Do not implement overlap as “prepared LCR × interval around `selectedTick`” — that is the 16-bar analog.

Grooming already named the gap **NOTE_EDIT hydrate** and forbade folding it into idle-slice / `ensureVisualCacheBuilt` audits. That name stays for this work. It is **not** `lazy-slot-hydration` (SD load to COMMITTED) and not undo-snapshot hydrate.

---

## Solution (from the proposal — do not invent a second one)

```text
prepared LCR ∪ NoteEditCurrentState overlay
    │
    ├─ Select: tickEvents / spanBoundaries neighborhood around selectedTick
    ├─ Overlap: participants vs selected/mover LinearSpan (same class as overdub)
    └─ Paint / audition: visualCache + settled overlay (not a second loop)
```

Same 6.0 rule as overdub: NOTE_EDIT open and fader **consume** already-prepared LCR. Miss is legacy compatibility (`visualCache` / today’s semantics), not a second analysis authority. Never construct / sort / checkpoint / `ensure*` LCR on those stacks. Path B (live geometry into LCR) stays forbidden.

Stages, invariants, overlay lifecycle, and work-shape gates live in the proposal. Do not duplicate them here. Execute them in order when this work is in CURRENT_WORK § Now implementing.

| Stage | Owner | One-line invariant |
|-------|--------|--------------------|
| 0 | docs | Pins recorded (done) |
| 1 | `SelectNavigation` | Neighborhood select; not `resolveState` |
| 2 | overlap candidate source | Indexed identities vs mover hold; `noteMembership` hit, visual-cache miss |
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
| New types: `EditHydrateSession`, `EditSourceView`, `NoteEditSourceView` | NAMING / DEC-037 rejected new domain nouns. Overlap working set is the participant query vs selected/mover LinearSpan. Select is a Runtime Request: prepared LCR × neighborhood around `selectedTick`. |
| Redesign `rematerializeEditView` / `materializeToEventVector` / `EditSession.store` | Prove the consumer first. |

---

## Prerequisites (already decided; not all live)

| Prerequisite | Status |
|--------------|--------|
| LCR `resolveState` / `resolveWindow` native | PASS |
| 6.0 no construct on button | accepted — same rule on NOTE_EDIT open / fader |
| 6A / 6C consume-when-ready | 6A device PASS; 6C **closed** [`205928`](../../captures/session_20260815_205928.log) / [`210508`](../../captures/session_20260815_210508.log) (3b stays) |
| 6D.4 post-commit publish | **HITL PASS** same captures; not all of LCR live |
| 6E overlap query | native PASS; not wired to Track |
| Wrap-move persist | **parked** — current-structure issue; not a start gate |

Hydrate 1 shipped. Hydrate 2–3 are named in CURRENT_WORK § Now implementing. Do not start 4a–5.

---

## Pre-implementation review (Hydrate 1)

### Ready

- `SelectNavigation::buildSelectNavigationSlots` is the slot owner
- Encoder `stepSelectNavSlot` already rebuilds at `getSelectedTick()` then ±1
- Existing note order is start 16th, then pitch
- `includeSelectedTickIfMissing` stays

### Resolved (user / code)

| Topic | Decision |
|---|---|
| Neighborhood | 16th containing `selectedTick` ± 1 16th (3 steps), wrap |
| Sounding-through | Skip — same as today (onset 16th only) |
| Fader | Pitchbend → 16th in `navLength`, then neighborhood at that tick |
| Chords | Same 16th stays pitch-ordered; encoder steps through them; fader does not spend travel per chord note |
| LCR consume | Not this stage. Miss/legacy still filters the already-built selectable list once |
| `resolveState` | Not Select |
| Hydrate 2–5 / 4a | Not this stage |

### Open before coding

None.

### Proceed?

YES — Hydrate 1 only.

---

## Pre-implementation review (Hydrate 2–3)

### Ready

- `OverlapNoteIdObservation::collectAnalyzeOverlapNoteIds` / `LoopSnapshot::noteMembership`
- `OverlapCandidateLookup::appendNotesForIds` empty-set gate
- `collectEvaluationScopeNoteIds` sticky rule
- `NoteGeometryResolver::resolve` is the consume owner

### Resolved (user / code)

| Topic | Decision |
|---|---|
| Query origin | Mover LinearSpan, not `selectedTick` |
| Prepared hit | `noteMembership(pitch, bar)` for hold bars |
| Miss | `collectAnalyzeOverlapNoteIds` on visual cache |
| Edges | Inclusive analyze (packed Hide). Overdub exclusive stays |
| Sticky | Current-state participants stay in scope |
| LCR | Do not construct / `ensure*` on the fader stack |
| Hydrate 4a–5 | Not this stage |

### Open before coding

None.

### Proceed?

YES — Hydrate 2–3 only.

---

## When this work starts

1. Pre-implementation review against the proposal (owners, stages 1→5, work-shape gates).
2. One stage per session unless the user authorizes more.
3. Native first. Commit each verified stage.
4. Do not open a second OpenSpec change unless a later stage needs a spec delta beyond the DEC-037 amendment.

---

## Files (when authorized)

Listed in the proposal. Expected first owners: `SelectNavigation`, `EditSelectNoteState`, `NoteGeometryResolver`, `NoteEditSessionLifecycle`, `TrackPlaybackWindowBuild`.

---

## Hydrate 1 device ([`224533`](../../captures/session_20260908_224533.log))

**Invariant holds.** Select emits a 3-step neighborhood. Fader maps pitchbend
to a 16th (`selected_tick` steps of 48; `slot=1` for the in-window target).
`NOTE_EDIT_OPEN` 88 notes. First Move `UNDO_WARM,push,e00,761,88,3`.

**Not a Hydrate 1 fail — fast bars / fader 2.** 248 Move + 63 Pitch
`GEOM_APPLY,resolve` (median **27731** µs, max **55965** µs). Phases:
reconstruct med **7586**, analyze **6435**, overlay **6023**. Peak
`hostdrain` **1448215** µs at 184.95 s sits on consecutive kind=3 resolves
at **29 ms**. Select-only `DFRAME` gaps **1465 / 1658 / 1931** ms match
Stage 3 [`221906`](../../captures/session_20260908_221906.log). Later
`focus_snap` **41–193 ms** (`push,cache_miss`). Those are hydrate 2/3 /
lossless-undo leftovers.

---

## Hydrate 2–3 native (in tree)

**Invariant:** Resolver scope is mover-hold candidates plus sticky
current-state participants. Not every same-pitch note.

Prepared hit: `tryCollectPreparedOverlapHoldNoteIds` →
`noteMembership(pitch, bar)` for hold bars ±1. Miss:
`collectAnalyzeOverlapNoteIds` on the already-built visual cache.
Inclusive analyze edges keep packed abut Hide. Overdub exclusive hold
is unchanged. Empty hold does not gather.

**Native:** `test_overlap_candidate_lookup` +
`test_edit_session_interaction` hold-scope pin. Full suite **1786
succeeded, 1 skipped**. RAM1 locals **7008**.

**Device functional PASS [`231008`](../../captures/session_20260908_231008.log):**
`NOTE_EDIT_OPEN` `current=1` `root_rev=4` **75**. First Move
`UNDO_WARM,push,e00,621,75,3` then `candidates_hit,3,0,3`.
72 hit / 258 miss. Hit resolve med **8743** µs (scope med **4**). Miss
resolve med **22206** µs (scan **72–73**). Hit-window actions: Hide **7**,
Shorten **10**, Restore **7**. Reconstruct med **6411** µs. Transport was
STOPPED after 86.148 s. Fast-fader `hostdrain` peak **1443580** µs at
160.038 s is miss-window leftover. Do not start hydrate 4a–5.
