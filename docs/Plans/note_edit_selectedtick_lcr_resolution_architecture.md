# NOTE_EDIT around `selectedTick` — LoopContentResolution consume

**Status:** Architecture **PASS** — DEC-037 amendment 2026-08-16. Implementation stages **PASS WITH AMENDMENTS** (this revision); firmware not authorized.  
**Date:** 2026-08-16  
**Kind:** architecture  
**Work identity:** [`note_edit_hydrate_enhancement.md`](note_edit_hydrate_enhancement.md) — separate from LCR 6.x and wrap-move persist  
**Parent:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype) Editor consumer; [6E](loop_content_resolution_overdub_state_evaluation_refinement.md) overdub `resolveState(tick)` consume  
**Does not authorize:** firmware; deleting `visualCache`; deleting `rematerializeEditView`; putting LCR construct on NOTE_EDIT open or fader; a new `*View` / `*Manager`; replacing `NoteGeometryResolver`; redesigning `EditSession.store` during this consumer migration

---

## Resolved pins (2026-08-16)

| Pin | Decision |
|-----|----------|
| **Display** | Idle `visualCache` stays the piano-roll committed list. LCR consume is select / overlap / analyze only. |
| **Select encoder** | Neighborhood around `selectedTick` only. Not a full-loop onset list. Navigation query is `tickEvents` / `spanBoundaries`, **not** `resolveState(selectedTick)` as the select contract. |
| **visualCache on analyze miss** | Legacy compatibility path with today’s semantics. Not a second analysis authority. |
| **Participating notes** | After mover stop + one `DisplayManager::update`, find overlaps vs the **current** mover span. Within one Move/Pitch action, that set updates: leavers `RestoreNote` (non-participant); new intersections join Active. Not DEC-030 sticky overlap end-of-participation (`Ended` on deselect). |
| **Audition** | All settled this-session overlay rows sound when `currentTick` passes them — not only the currently highlighted note. Same as wrap-1 notes still playing during wrap 2. |
| **Next select / overlap** | Settled overlay is the session delta. Next query is prepared LCR ∪ overlay, the same consume shape as overdub `resolveState` after 6D.4 publish. Do not insert live geometry into LCR (Path B). |
| **Session store** | Not required for that audition. Overlay those NoteIds onto the existing playback window. Keep the store only as neighborhood apply scratch until identity apply is proven. |
| **Display compose** | Same as overdub: committed `visualCache` + this-session overlay. Session is overlay state (`NoteEditCurrentState` neighborhood rows), not a second copy of the loop. |
| **Decision vehicle** | DEC-037 amendment. No new DEC. |

### Implementation amendments (review 2026-08-16)

Architecture approved. Stages as first written are **not** approved. Accepted amendments:

1. Separate Select’s onset/navigation query from `resolveState()`.
2. `resolveWindow` no-reconstruction is a Stage 2 measurement gate.
3. Prove `appendNoteEvents(NoteId)` is identity-bounded, not hidden O(N).
4. One-frame participant find is a trigger; data source is LCR ∪ overlay, not `visualCache` rebuild.
5. Mover → Active → settled → restored transitions are the overlay lifecycle table.
6. Split Stage 4 into 4a open / 4b first select / 4c playback overlay.
7. Stage 1 measures work shape (walks, neighborhood bound, wall time, allocation), not only `fullMaterializeCount == 0`.
8. Prepared-miss is a legacy compatibility path, not a second analysis authority.
9. Dedicated settled-overlay wrap/audition regression (Stage 4c).
10. `resolveWindow` must not return a reconstructed whole-loop `DisplayNote` vector. Do not redesign `rematerializeEditView` / `materializeToEventVector` / `EditSession.store` during this migration.

Device gates measure **work shape**, not only function-call absence.

---

## Overdub display principle (compose, not a second loop)

`DisplayManager::resolveDisplayNotesLiveCapture` paints overdub as two layers:

```text
committed layer     visualCache.notes
                    (window-filter when long; hold last frame if dirty)
                         │
                         │  rebuildCommittedLayer — does not rematerialize
                         ▼
liveDisplayNotes    [ committed prefix | capturePreview suffix ]
                         ▲
capture layer       capturePreview.notes
                    applyCaptureEventToPreview — one MIDI event
                    replaceCaptureLayer / synchronizeCaptureLayer
                    open notes grow to currentTick (applyCapturePlayheadTails)
```

`capturePreview` is this-pass state only: `notes`, `noteStates`, `openNoteIndices`, `changedNoteIndices`. Live MIDI for the pass lives in `capture.store` (append-only this pass). The committed loop is not copied into that session.

Pending overlap Shorten/Hide is commit/MIDI state. It does not rewrite `visualCache` on the hot path. After stop, idle slices refresh the committed layer.

NOTE_EDIT paint today does the opposite: `projectedNoteEditDisplayNotes` can rematerialize the whole loop when `visualCacheDirty`, and `EditSession.store` is a full-loop event vector. That is a second loop structure.

```text
OVERDUB paint                         NOTE_EDIT paint (pinned)

visualCache (committed)               visualCache (committed)     unchanged
+ capturePreview (this pass)          + NoteEditCurrentState      mover + participants only
compose: prefix + suffix              compose: base + overlay Hide/Shorten/move/add
capture.store = this-pass MIDI        no full-loop session store for paint
```

`NoteEditCurrentState` is the overlay owner (DEC-029). It must stay neighborhood-sized, like `capturePreview`. Filling it from every `visualCache` row (`ensureCurrentStateVisibleRowsFromVisualCache`) recreates the full structure. `EditSession.store` is not a display layer.

---

## This-session edits join the next query (overdub 6D.4 analog)

Overdub makes a finished edit visible to the **next** note by publishing, not by rematerializing:

```text
wrap N seals
  → OverdubPass + companion Shorten/Hide
  → 6D.4 delta on prepared LCR
  → wrap N+1 resolveState sees wrap N
live capture of the current wrap stays out of LCR (Path B forbidden)
```

6E: frozen `overdubSourceViewNotes_` does **not** contain same-session prior-wrap notes. 6D.4 + wrap commit is what makes them participate.

NOTE_EDIT has no loop-wrap seal. The publish moment is **mover stop + one `DisplayManager::update`**. Overlap membership is the **current** mover span of this Move/Pitch action, not a set that accumulates for the whole session.

```text
during motion     mover overlay only
after 1 frame     find participants vs current mover [S, E)
                  still intersecting → Active Shorten/Hide (join/stay in delta)
                  no longer intersecting → RestoreNote to committedSpan
                                           drop from overlap delta
                                           (paint visualCache again)
                  currentState is queryable for the remaining set
if fader moves again in the same action
                  same find vs the new span (restore leavers, add new)
action released   remaining Active participants stay in session delta
                  (next select / overlap / audition)
deselect          DEC-030 sticky overlap end-of-participation /
                  DEC-034 overlap shorten seals at the user-triggered commit
                  — not this path
```

Leave during the action is existing `RestoreNote` leave-restore ([`MOVE_NOTE_LOGIC.md`](../Guides/MOVE_NOTE_LOGIC.md) stage 7: Active participant + overlap closure cleared). It is **not** DEC-030 sticky overlap end-of-participation. That `Ended` flag leaves a shortened stub after deselect; intra-action leave restores geometry.

```text
next query =  tryResolvePreparedState / resolveWindow     // committed
              ∪ NoteEditCurrentState settled rows         // mover + remaining Active
```

Do **not** write `LoopPasses` / LCR indexes on that settle. `saveNoteEditPass` stays the content record at deselect/close. `applyOwnedEditPassRows` stays the action log (overdub `pendingNoteChanges` analog). Path B still forbidden.

A note moved into the neighborhood is selectable. A note still shortened in this session is the overlap source at the shortened span. A restored leaver is the committed span again (LCR / `visualCache`). Hidden overlay rows are not selectable and do not sound.

---

## Problem

NOTE_EDIT still treats a full-loop materialized `DisplayNote` list as the working set:

```text
openNoteEditSession
  → rematerializeEditView          // materializeToEventVector → session store
  → rebuildVisualCacheFromPasses   // gather + reconstruct ALL notes
  → NoteGeometryResolver
       visualCacheNotesForSelectedSlot
       collectEvaluationScopeNoteIds(liveStore)   // all NoteOns when currentState empty
       overlayUneditedBaselineMapFromDisplayNotes // walk every cache row
  → projectNoteEditDisplayNotes    // overlay over that full list
  → SelectNavigation               // notes × every 16th step
```

On a **16-bar loop with ~1000 notes**, that list is the whole loop. Piano-roll window size (`kMaxDetailedWindowBars` = 16) does not shrink it. Every select, move, length, and overlap analyze pays O(notes) reconstruct and O(notes) pairwise scope.

Overdub already left that model. Entry copies a prepared window or a clean cache (DEC-036 3b). Overlap does **not** rematerialize: `PendingNote.overlapNoteIds` + `appendNotesForIds`, geometry `[S, E)`, Shorten/Hide. 6E pins the next step: consume prepared `resolveState(tick)` (pitch-filtered), not a premapped note map.

NOTE_EDIT needs the same query shape. The editor origin is `selectedTick`, not `currentTick`.

---

## Mapping (no new domain nouns)

| Overdub (shipped / 6E pin) | NOTE_EDIT (this proposal) |
|----------------------------|---------------------------|
| `currentTick` / `playheadPhaseTick` | `selectedTick` (bracket) |
| Incoming MIDI note `[S, E)` | Selected note(s) span `[start, end)` |
| Prepared `resolveState(tick)` | Overlap sounding-at-S only. **Not** the Select contract |
| Prepared `resolveWindow` around playhead | Overlap ons in `[S, E)`. Identities / compact records, not a reconstructed `DisplayNote` vector |
| `tickEvents` / `spanBoundaries` | Select neighborhood navigation around `selectedTick` |
| `appendNoteEvents(noteId)` | Same — mover and overlap targets |
| `overlapNoteIds` + `appendNotesForIds` | Same set + lookup on resolved candidates |
| `existingNoteOverlapsIncomingHold` | Same half-open rule |
| Shorten / Hide / Add (DEC-031/032) | Same `EditSessionAction` kinds |
| 6.0: no LCR construct on the overdub button | No LCR construct on NOTE_EDIT open or fader |
| 3b `visualCache.notes` copy fallback | Prepared **miss** only: legacy compatibility with today’s semantics, not a second analysis authority |
| `NoteGeometryResolver` not the overdub owner | `NoteGeometryResolver` stays live overlap Resolution |

DEC-037 already drew the Editor as a `resolveWindow` / `resolveNotes` consumer. It was never wired. Display 6A and overdub 6C/6E were. This proposal is that missing consumer, not a second resolution owner.

Rejected names: `EditSourceView`, `NoteEditSourceView`, `EditorWindowCache`, `LoopContentResolver`. Working set is a **Runtime Request**: prepared LCR × interval around `selectedTick`.

---

## Why a 16-bar window is not the fix

`Loop::resolveOverdubSourceWindow` centers a 16-bar window on the playhead. When `windowLength >= loopLength`, it collapses to `[0, loopLength)`. A 16-bar loop therefore still yields every note if the consumer reconstructs `DisplayNote`s.

```text
WRONG:  selectedTick → 16-bar resolveWindow → reconstruct 1000 DisplayNotes
        → same visualCache cost, different function name

RIGHT:  selectedTick → tickEvents / spanBoundaries neighborhood   // Select navigation
        selected span → indexed identities, then appendNoteEvents  // overlap
        selected NoteId → appendNoteEvents                         // mover geometry
        display interval → visualCache + overlay                   // paint only
```

The overdub waste 6E named is **candidate find via a reconstructed note list**. NOTE_EDIT has the same waste in `visualCache` + `liveStore` walks. Indexed LCR already has the find structures: `tickEvents`, `spanBoundaries`, `byNoteId`.

---

## Current vs target

```text
TODAY                                         TARGET

Passes / EditPass                             Passes / EditPass
  │                                             │
  ├─ materialize ALL  ─► session store          │  (idle) LCR index + checkpoints
  ├─ reconstruct ALL  ─► visualCache            │
  └─ pairwise ALL     ─► geometry               ▼
                                      LoopContentResolution (prepared)
                                                │
                              ┌─────────────────┼─────────────────┐
                              ▼                 ▼                 ▼
                         tickEvents        resolveWindow     appendNoteEvents
                         spanBoundaries    resolveState(S)   (by NoteId)
                         (Select nav)      (overlap find)    (identity geometry)
                              │                 │                 │
                              └────────┬────────┴────────┬────────┘
                                       ▼                 ▼
                              NoteGeometryResolver    DisplayManager
                              (overlap Resolution)    (paint window)
                                       │
                                       ▼
                              EditPass commit (unchanged)
```

```text
OVERDUB (6E Path A)                         NOTE_EDIT (this proposal)

prepared LCR (history + 6D.4 delta)         prepared LCR ∪ settled currentState
  → resolveState(currentTick)                 → Select: tickEvents neighborhood
  → pitch-filter sounding                     → Overlap: indexed [S, E) identities
  → incoming [S, E)                           → selected note(s) [start, end)
  → Shorten/Hide                              → same geometry
  → wrap seal: OverdubPass + companions       → settle: overlay queryable
  → stop: one U:                              → close: saveNoteEditPass + E:
```

Live capture stays out of LCR (6E Path B remains forbidden). Live NOTE_EDIT motion stays mover-only until the one-frame find; overlap membership then tracks the current mover span of this action (Restore leavers, Active on new intersections). Remaining Active rows after the action are the session delta for the next select, overlap, and audition. LCR answers **committed** effective state. Overlay answers **this session**.

```text
                     COMMITTED CONTENT
                           │
                           ▼
                  LoopContentResolution
                           │
             ┌─────────────┼──────────────┐
             │             │              │
             ▼             ▼              ▼
         select         overlap       playback
       neighborhood    candidates      window
             │             │              │
             │             ▼              │
             │     NoteGeometryResolver   │
             │             │              │
             └─────────────┼──────────────┘
                           ▼
                  NoteEditCurrentState
                    session overlay
                           │
              ┌────────────┴────────────┐
              ▼                         ▼
           display                    audition
       committed cache             playback window
        + overlay                    + overlay
              │                         │
              ▼                         ▼
             OLED                       MIDI
```

```text
visualCache          paint base only
EditSession.store    temporary apply/session scratch
LCR                  committed query authority
CurrentState         mutable session delta
```

---

## Owners (checkpoint)

| Question | Answer |
|----------|--------|
| **Ownership change?** | **NO** new owner. LCR stays producer of prepared derived state (DEC-037). `NoteGeometryResolver` stays live NOTE_EDIT overlap Resolution. `EditManager` stays session / `selectedTick` / current-state owner. `Loop` stays pass + visual-cache owner. `DisplayManager` stays draw. |
| **State transition change?** | **NO.** NOTE_EDIT still opens, selects, applies, deselects, commits `EditPass`, session-gates **E:**. What changes is the **analyze interval**: `DerivedViews` v1 `[0, loopLength)` → interval around `selectedTick` / selected span. |
| **Reuse** | YES — Select: `tickEvents` / `spanBoundaries`. Overlap: `tryResolvePreparedState` / indexed window identities / `appendNoteEvents` / `OverlapCandidateLookup::appendNotesForIds` / `existingNoteOverlapsIncomingHold`. Do not add a parallel note map. Do not define Select as `resolveState`. |
| **Formal trigger?** | DerivedViews analysis interval is a documented architecture change. Treat as a **DEC-037 amendment** (Editor consumer), not a new Manager. New DEC only if session store is removed or `NoteGeometryResolver` loses overlap ownership. |

`visualCache` unification ([`note_edit_visual_cache_display_unification_refinement.md`](note_edit_visual_cache_display_unification_refinement.md)) stays the **LOOP_EDIT / idle display** committed list. It is **not** the NOTE_EDIT analyze authority. Same split overdub already made: `visualCache` is not the overdub-overlap authority (`overdubSourceViewNotes_` / 6E `resolveState`).

---

## Invariants

1. **One resolution owner.** Committed effective MIDI at a tick or window comes from `LoopContentResolution`. Geometry actions (Hide / Shorten / Restore / Move) still come from `NoteGeometryResolver` + DEC-031/032.
2. **Consume, do not construct, on the gesture path.** NOTE_EDIT open, encoder select, and fader geometry may only consume already-prepared LCR. Miss → **legacy compatibility path** (today’s `visualCache` / windowed gather semantics). Not a second analysis authority. Never `ensure*` rebuild on that stack. Same as OpenSpec 6.0 / 6.5.
3. **Select and overlap are different query contracts.** Select is a bounded onset/navigation query (`tickEvents` / `spanBoundaries` around `selectedTick`). Overlap is identity-addressed hold intersection. `resolveState` is sounding-at-tick (overlap at S). It is not the definition of Select.
4. **Selected note(s) are the incoming hold.** Overlap candidates are same-pitch notes whose linearized `[start, end)` intersects the selected span. Empty candidate set does not gather or reconstruct (overdub Gate 3).
5. **Prepared-hit overlap lookup is indexed → bounded identities → identity geometry.** It must not materialize, `reconstructDisplayNotes`, walk `visualCache`, walk `EditSession.store`, or per-candidate full-store scan. `resolveWindow` must not return a reconstructed whole-loop `DisplayNote` vector merely because a consumer used to expect one.
6. **Lookup is identity-addressed and identity-bounded.** `appendNoteEvents(noteId)` cost is O(events belonging to that `NoteId`), not O(total loop events) per id. Do not `findLinearNoteSpanForNoteId` per id on a full flatten (overdub Gate 4).
7. **One-frame participant find is independent of paint rebuild.** Trigger is a completed `DisplayManager::update`. Data source is prepared LCR ∪ `NoteEditCurrentState`. Not `visualCache` rebuild, not playback refresh, not `kDeferredNoteEditPlaybackRefreshIdleMs`.
8. **Zero-length notes stay invalid.** `startTick < endTick`. Endpoint touch is not overlap.
9. **This-session overlay is not LCR.** `NoteEditCurrentState` rows overlay resolved committed notes. Do not insert live fader geometry into LCR (Path B).
10. **EditPass commit is unchanged.** `saveNoteEditPass` remains the content record. Undo routing stays session-gated **E:** while NOTE_EDIT is open.
11. **Determinism.** For a fixed active pass set, edit history, `selectedTick`, and selected span, overlap ids and action kinds match the overdub geometry oracle and today’s resolver fixtures.

---

## Overlay lifecycle (authoritative)

| Phase | Overlay | Overlap find |
|-------|---------|--------------|
| **MOTION** | Mover only | None (not on fader stack) |
| **STOP + one `DisplayManager::update`** | Unchanged until find returns | Indexed query vs current mover `[S, E)`; source is LCR ∪ current-state, not paint cache |
| **PARTICIPANT** (still intersecting) | Active Shorten/Hide | Join / stay in session delta |
| **LEAVER** (no longer intersecting) | `RestoreNote` to `committedSpan`; drop from overlap delta | Not DEC-030 sticky overlap end-of-participation |
| **ACTION RELEASE** | Remaining Active → **settled** session overlay | Next select / overlap / audition consume LCR ∪ settled overlay |
| **DESELECT** | DEC-030 sticky overlap end-of-participation / DEC-034 overlap shorten seals at the user-triggered commit | Not the intra-action path |

If the fader moves again inside the same Move/Pitch action, repeat STOP+find against the new span.

---

## Query shapes

### Select (bracket) — navigation contract, not sounding-state

`resolveState(selectedTick)` is sounding-at-tick. That is the overlap query at S. It is **not** Select.

```text
selectedTick
    ↓
bounded navigation query (tickEvents / spanBoundaries neighborhood)
    ↓
candidate NoteIds / onset positions
    ↓
appendNoteEvents(NoteId) only if geometry is required
    ↓
EditorSelection
```

Pin before Stage 1 (deterministic neighborhood rule, reuse existing select order where it already exists):

- nearest onset before / after `selectedTick`
- notes that begin exactly at `selectedTick`
- simultaneous onsets
- notes already sounding through `selectedTick` (select vs skip)
- wrap-boundary ordering
- order after the selection itself moves

Today `SelectNavigation::buildSelectNavigationSlots` walks every display note for every 16th of the loop (`notes × steps`). Encoder navigation is a **neighborhood around `selectedTick`** only. Do not build a full-loop onset list. Do not reconstruct `visualCache.notes` to navigate. Do not define Select as `resolveState()`.

### Overlap (selected note as hold) — identity contract

```text
selected note(s) N with span [S, E) at pitch P
  → indexed query: sounding at S + ons in [S, E), pitch P
  → bounded candidate NoteIds (exclude N)
  → appendNoteEvents / appendNotesForIds   // O(events of those ids)
  → existingNoteOverlapsIncomingHold
  → Shorten / Hide
```

Prepared-hit overlap **must** perform: indexed query → bounded candidate enumeration → identity lookup.

It **must not** perform: `materialize`, `reconstructDisplayNotes`, full `visualCache` walk, full `EditSession.store` walk, per-candidate full-store linear scan.

`resolveWindow` may feed the indexed query. It must not be implemented as reconstruct-then-filter. Return identities / compact resolution records; fetch geometry with `appendNoteEvents` only where required.

Stage 2 counters (device + native): `candidate_count`, `identity_lookup_count`, `identity_lookup_scan_count`, `append_event_count`. `identity_lookup_scan_count` and `append_event_count` must not scale with total loop size.

Multi-select: each selected span is an incoming hold. Sticky off-lane participants already in `NoteEditCurrentState` stay in evaluation scope (today’s `isSticky` rule). They are not rediscovered by walking the whole `liveStore`.

### Live fader (continuous geometry)

During motion, apply **mover geometry only**. Do not collect participating notes on the fader stack.

After the last geometry input, wait **one completed `DisplayManager::update`**, then find participants. The update is only the **trigger**. The find reads prepared LCR ∪ `NoteEditCurrentState`. It must not wait for `visualCache` rebuild or playback refresh.

```text
last mover apply
  → next DisplayManager::update completes     // trigger only
  → indexed overlap find vs current mover span
  → NoteGeometryResolver (same eligiblePairs / constrained geometry)
```

That grace is not `kDeferredNoteEditPlaybackRefreshIdleMs` (80 ms playback preview rebuild). Different owner.

`ensureCurrentStateVisibleRowsFromVisualCache` must not be the way every note enters current-state.

### Display (paint)

Idle `visualCache` slices stay the piano-roll committed list. Paint consumer only. A dirty or partial cache must not block NOTE_EDIT select or overlap.

---

## Session store and audition

Today PLAYING NOTE_EDIT replaces the whole playback stream with the session store:

```text
ensurePlaybackMergedMidiEventsBuilt
  noteEditPreview → assign(sessionMidiEvents())
  window = [0, loopLength)
```

That full replace is why every note stays audible, and why edited geometry auditions. It is also why open rematerializes ~1000 events.

The product requirement is the overdub wrap analog: every **settled** this-session overlay row stays audible when `currentTick` crosses it, including after you select another note. Unrelated notes keep the committed playback window.

```text
TODAY:  sessionMidiEvents() full replace
TARGET: committed playback window around currentTick
        + overlay currentState MIDI for every settled this-session NoteId
```

Session store is **not** the audition owner. Overlay those identities into the playback window after the one-frame participant find. `refreshPlaybackPreview` still bumps `sessionPreviewRevision_` so the window rebuilds; the payload is the overlay, not a full flatten.

| Use | Session store |
|-----|----------------|
| Analyze / overlap candidates | No — LCR consume |
| Audition while highlighted | No — overlay on playback window around `currentTick` |
| Apply `EditSessionAction` | Keep as neighborhood scratch until identity apply is proven |
| Open (`rematerializeEditView`) | Must not flatten the whole loop before first select |

Deleting the store remains a later DEC. First Editor-consume stages do not require it for audition.

---

## Debugging boundary

```text
LoopPasses / EditPass / LCR indexes     ← frozen DEC-037 representations
        ↓
prepared LCR (indexes, not DisplayNote reconstruct)
        ↓
Select: tickEvents / spanBoundaries neighborhood
Overlap: indexed identities → appendNoteEvents
        ↓
NoteGeometryResolver                    ← live overlap Resolution (unchanged owner)
        ↓
NoteEditCurrentState overlay + EditPass commit
```

Do not reopen: 5.15 / 5.17 / 5.7c / 5.18 index representations; overdub Gate 0–4 geometry; wrap-stub / LIFO pairing bugfixes; `materializeToEventVector` as the long-term owner.

---

## Sequencing

Work identity is [`note_edit_hydrate_enhancement.md`](note_edit_hydrate_enhancement.md), not remaining LCR OpenSpec 6.4 firmware. Not in CURRENT_WORK § Now implementing. Wrap-move persist is **parked** (current rematerialize / session-store structure is part of [`201446`](../../captures/session_20260816_201446.log) 576→336). It is not a start gate. Do not resume LIFO/wrap-off persist patches from this work.

Prerequisites (already decided, not all live):

| Prerequisite | Status | Why this needs it |
|--------------|--------|-------------------|
| LCR `resolveState` / `resolveWindow` native | PASS | Editor consume oracle |
| 6.0 no construct on button | accepted | Same rule on NOTE_EDIT open / fader |
| 6A idle display consume | PASS | Prepared window exists |
| 6C consume-when-ready | native; device slower than 3b | Miss remains legacy compatibility |
| 6D.4 post-commit publish | landed; not all of LCR live | Edit after a new pass must see restamp |
| 6E `resolveState(tick)` overlap | native PASS; not wired to Track | Exact overlap query to reuse |
| Wrap-move persist | **parked** | Current-structure issue; not a start gate. Re-evaluate [`201446`](../../captures/session_20260816_201446.log) after hydrate |

LCR is still the producer of prepared state, not a replacement for `visualCache` on MIDI/display until DEC-037’s three gates pass. Editor consume can proceed as a **parallel consumer** the same way 6C did: miss → fallback; hit → no reconstruct.

---

## Stages (firmware not authorized until these gates; wrap-move persist is parked)

Commit each verified stage before the next. Native first. Do not redesign `rematerializeEditView`, `materializeToEventVector`, or `EditSession.store` in these stages. Prove the consumer, then ask which old machinery is unreachable.

Work-shape gates measure counters and wall time / allocation, not only “function X was not called.”

### Stage 0 — Pins (doc) ✅

DEC-037 amendment 2026-08-16. Architecture PASS. This revision records implementation amendments. No new DEC. No new domain noun.

### Stage 1 — Select neighborhood (navigation query)

**Owner:** `EditSelectNoteState` / `SelectNavigation`.  
**Invariant:** Select is a bounded `tickEvents` / `spanBoundaries` neighborhood around `selectedTick`. Not `resolveState`. Not a full-loop onset list.  
**Gate:** `fullMaterializeCount == 0`; `visualCache` walk count == 0; `liveStore` walk count == 0; candidate enumeration bounded by the navigation neighborhood; wall time and allocation recorded on a 16-bar / ~1000-note fixture. Selection must not scale with loop-wide materialization.

### Stage 2 — Overlap candidates = overdub hold (indexed identities)

**Owner:** candidate collection feeding `NoteGeometryResolver`.  
**Invariant:** selected note `[S, E)` produces the same Shorten/Hide target ids as `accumulatePendingNoteChangesFromSourceNotes` for that hold. Prepared hit: indexed query → bounded identities → `appendNoteEvents`. No reconstruct, no `DisplayNote` vector as the window result.  
**Gate:** overdub overlap fixtures with the selected note as incoming hold; empty set does not gather (Gate 3). Counters: `candidate_count`, `identity_lookup_count`, `identity_lookup_scan_count`, `append_event_count`. Scan/append must not scale with total loop size. `appendNoteEvents(id)` is O(events of that id).

### Stage 3 — Resolver consume + legacy miss path

**Owner:** `NoteGeometryResolver::resolve` candidate source.  
**Invariant:** prepared **hit** is the analysis authority (LCR ∪ overlay). Prepared **miss** is a **legacy compatibility path** with today’s semantics — temporary, not a second authority. No `ensure*` LCR rebuild on the fader stack. Intra-action leave is `RestoreNote` per overlay lifecycle table.  
**Test:** native overlap + move/length fixtures stay PASS; prepared-miss matches current behavior.

### Stage 4a — Open without full-loop analysis

**Owner:** `openNoteEditSession`.  
**Invariant:** open does not rematerialize the loop in order to analyze or paint the first frame. Session store may still exist as apply scratch.  
**Gate:** no `VCACHE,full` / full `materializeToEventVector` required before first `DNTE`.

### Stage 4b — First select after open

**Owner:** Stage 1 path after 4a.  
**Invariant:** first select uses the neighborhood query; does not depend on `rematerializeEditView` of all events.  
**Gate:** same Stage 1 counters on the open→select sequence.

### Stage 4c — Playback overlay (not full-stream replace)

**Owner:** `ensurePlaybackMergedMidiEventsBuilt`.  
**Invariant:** PLAYING NOTE_EDIT is committed playback window around `currentTick` plus settled overlay `NoteId`s. Not `assign(sessionMidiEvents())` of the whole loop.  
**Test (dedicated):** A selected; B overlapped by A; A deselected; C selected; playhead wraps → A and B session modifications remain audible; C does not remove A/B; unrelated committed notes remain audible. Also: wrap 1→2, participant leave (`RestoreNote`), new participant, shortened tail, Hidden, moved note.

### Stage 5 — Integrated device gate

16-bar / ~1000 notes: enter NOTE_EDIT, select, lengthen across a same-pitch neighbor, move off and restore, deselect/commit. No multi-second MIDI/OLED stall. No `VCACHE,full` on select/overlap. Work-shape counters hold. Overlap row kinds match overdub geometry. Score against `GEOM_APPLY,resolve` 16.8–52.3 ms on smaller loops — stay in the RC-K `notechg` class, not visual-cache rebuild seconds.

---

## Do not

- Put `resolveState(LoopPasses, …)` (full rematerialize overload) on NOTE_EDIT open or fader
- Define Select as `resolveState(selectedTick)`
- Insert live session geometry into LCR (Path B)
- Make `visualCache` the overlap / analyze authority
- Treat the miss path as a second equally valid analysis authority
- Replace `NoteGeometryResolver` with LCR
- Invent `EditSourceView` or a second note map
- Make `resolveWindow()` return a reconstructed whole-loop `DisplayNote` vector because a consumer used to expect one
- Delete or redesign `materializeToEventVector` / `rematerializeEditView` / `EditSession.store` in Stages 1–5
- Raise `OverlapNoteIdSet` capacity as a substitute for indexed find
- Fold wrap-move persist, UNDO_WARM, or 6.3 playback gather into this work
- Resume wrap-move persist LIFO/wrap-off patches (that RC is parked)
- Run participating-note find on the last fader event
- Chain participant find on `visualCache` rebuild or playback refresh
- Full-replace `mergedEvents` from a whole-loop session store for NOTE_EDIT audition

---

## Risks

| Risk | Why it is real | Mitigation |
|------|----------------|------------|
| Prepared LCR not ready on NOTE_EDIT during PLAYING | 6C: LCR slices are STOPPED-only | Consume-when-ready; miss is legacy compatibility only |
| 16-bar loop = full window | Window length does not reduce note count | Identity queries + work-shape counters |
| `resolveWindow` reconstruct-then-filter | Satisfies the API while defeating the cost model | Stage 2 gate: no `DisplayNote` vector, no reconstruct |
| `appendNoteEvents` hidden O(N) | 5.15-class device cost in an “indexed” helper | Stage 2 `append_event_count` vs loop size |
| Participant find waits on paint | Display update becomes a cache rebuild barrier | Trigger ≠ data source |
| Session store still O(all) for apply | Apply currently mutates a full event vector | Neighborhood scratch; do not redesign store in this migration |
| Sticky overlap participants | Resolver scope includes off-lane sticky ids | Keep `isSticky` from current-state |
| Overlap find during drag | Today every resolve walks scope | Overlay lifecycle table |

---

## Files (expected, after stage authorization)

| Path | Role |
|------|------|
| `src/Utils/SelectNavigation.cpp` | Neighborhood `tickEvents` / `spanBoundaries` slots |
| `src/EditStates/EditSelectNoteState.cpp` | Select from navigation query, not `resolveState` |
| `src/EditManager/NoteGeometryResolver.cpp` | Candidate source swap; leave-restore lifecycle |
| `src/EditManager/EditSessionInteraction.cpp` | Stop walking full `liveStore` for scope |
| `src/EditManager/NoteEditSessionLifecycle.cpp` | Stage 4a open without full-loop analysis |
| `src/Track/TrackPlaybackWindowBuild.cpp` | Stage 4c overlay; stop full session-store replace |
| `src/Loop/LoopCapture.cpp` | Pattern only — consume-when-ready |
| `include/OverlapCandidateLookup` / `LoopPendingNoteChange.cpp` | Reuse geometry, do not fork |
| `docs/Authority/Architecture/DerivedViews.md` | Play / display / analyze / LED consumers; analysis interval |
| `docs/DECISION_LOG.md` | DEC-037 Editor amendment |

---

## Verdict

**Architecture: PASS.** NOTE_EDIT is an LCR consumer, not another materialization owner. `selectedTick` is the editor origin. No new DEC. No new representation.

**Implementation plan: PASS WITH AMENDMENTS** (this revision). Firmware stays unauthorized until a stage is explicitly started from [`note_edit_hydrate_enhancement.md`](note_edit_hydrate_enhancement.md). Wrap-move persist is parked. Prove select, overlap, fader, open, then audition as separate stages. Measure work shape, not only function-call absence.
