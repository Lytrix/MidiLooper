# NOTE_EDIT + overdub paint/overlap compose

**Status:** Plan authorized. Do not start firmware until an implement session names Stage 1.  
**Kind:** bugfix  
**Evidence:** [`session_20260909_002450.log`](../../captures/session_20260909_002450.log)  
**Parents:** Hydrate 4a [`note_edit_hydrate_enhancement.md`](note_edit_hydrate_enhancement.md), DEC-055 overlay delta, DEC-056 companion admission  
**Does not start:** hydrate 4b–5, close-NOTE_EDIT-before-overdub, open rematerialize, DEC-054 Stage 2, STOPPED idle `pending=1`, a new display-view noun

## Debugging boundary

```
NOTE_EDIT open (Hydrate 4a empty apply scratch, mover-only current state)
  ← trust. Do not refill the store or all visual-cache rows.
saveNoteEditPass / companion admission (DEC-056)
  ← trust when a row is saved on a stale root.
empty-store bake skip
  ← trust. No-edit close must not rematerialize.
        │
        ▼
ensureNoteEditDisplayProjectionCachesBuilt committed base
  + NoteGeometryResolver committedDisplayNotes
  while captureActive / OVERDUBBING
        ← this slice
projectNoteEditDisplayNotes overlay
  ← keep. Overlay still mutates the composed base.
close-NOTE_EDIT-before-overdub
  ← rejected. Session stays open.
```

`LoopPersist` / SD save is not this RC. [`002450`](../../captures/session_20260909_002450.log)
has no `LoopPersist` after 31.184 s.

## Contract

While NOTE_EDIT is active and the selected loop has live Capture
(OVERDUBBING), piano-roll paint and overlap Resolution use the **same
composed committed display** that LOOP_EDIT already uses for overdub:
published snapshot / overdub source **plus** live capture notes.  
`NoteEditCurrentState` stays the overlay. The apply scratch stays empty
until project.

```text
overdub draw-source compose
  snapshot or source DisplayNotes
  + live Capture notes
        │
        ▼
NOTE_EDIT committed base
        │
        +-- paint: projectNoteEditDisplayNotes + current state
        +-- overlap: NoteGeometryResolver committedDisplayNotes
```

## Capture pin ([`002450`](../../captures/session_20260909_002450.log))

| Clock | Fact |
|---|---|
| 33.107 / 36.435 s | No-edit open/close. `skip empty-store bake`. Bake RC closed. |
| 53.290 s | NOTE_EDIT open `session_events=0` `current=1` `root_rev=4` notes **79** |
| 55.785 s | `PLAYING → OVERDUBBING` while the session stays active |
| 55.788 s | Overdub source `from=snapshot` `notes=79` `root_rev=4` `play_rev=4` |
| 57.628 s | `VCACHE,full` notes **79** |
| 59.560 s | First Move note 86. `storeNoteOns=0` `overlapNotes=0` `actions=` `candidates_hit` |
| 61.075 s | Resolver `candidates=1` `interactions=1` still no overlap action list; only Move 86 |
| 61.327 s | `VCACHE,full` notes **83** (live capture entered the cache) |
| 61.448 s | `candidates_miss,83` after the root is no longer the sole analyze source |
| 62.873 s | First `overlapNotes=1` |
| 74.900 s | Commit `Delete` 77 + `Update` 86. `M63@0 missing in recon` on session store and loop materialize |
| 74.991 s+ | Every focus rebuild still lists `overlapParticipantNoteId=77` |
| DFRAME | 79 → 86 → 85 while the mover is dragged |
| 31.184 s | Last `LoopPersist` in the file (first overdub stop). None during this window. |

## Root causes (two commits)

### Stage 1 — paint committed base includes live Capture

**Owner:** `EditManager::ensureNoteEditDisplayProjectionCachesBuilt`

Today, dirty visual cache picks:

| Condition | Base | Live Capture |
|---|---|---|
| `visualCacheDirty && publishedSnapshotIsCurrent()` | `resolveDisplayNotes(*snapshot)` | omitted |
| `visualCacheDirty && !publishedSnapshotIsCurrent()` | `materializeToEventVector` | omitted |
| else | `getVisualNotesForSlot` | present only if the cache already merged them |

`shouldResolveNoteEditCommittedBaseFromSnapshot` is the first row. During
OVERDUBBING that row is the 79-note snapshot while draw-source compose
already has live notes. `playbackRevision` changes invalidate the paint
cache, so the next frame rebuilds from snapshot or passes and the live
notes vanish until a later select/move hits a clean visual cache.

**Invariant:** If `loop.captureActive()`, NOTE_EDIT committed base is the
existing overdub compose (`rebuildDisplayNotesInWindow` /
`tryGatherPublishedSnapshotEventsInWindowWithCapture` / clean visual
cache). It is not snapshot-only and not pass-rematerialize-only.

Reuse: YES — the gather already used by `resolveDisplayNotesByDrawSource`.
Do not add a second compose owner.

### Stage 2 — overlap uses that same composed base

**Owner:** `NoteGeometryResolver::resolve`

Today `committedDisplayNotes` is `visualCacheNotesForSelectedSlot`. The
prepared hit path (`tryCollectPreparedOverlapHoldNoteIds`) queries the
published snapshot. While Capture is active and the snapshot is still
current, live notes are not candidates. After the snapshot goes stale,
miss scans the visual cache (83) and overlap starts late.

`appendOverlapTargetActions` / `appendCausingNoteActions` still require a
current-state row or a store span (`editableRowProjectsToStore`). Hydrate
4a leaves those missing for anyone except the mover, so a found candidate
emits no Hide/Shorten.

**Invariant:** Overlap candidate source and action build see the Stage 1
composed DisplayNotes. A mover-hold participant is upserted into
`NoteEditCurrentState` as a scoped overlay row (Visible / Hidden /
Shortened). That is not an all-notes visual-cache fill.

Reuse: YES — extend `overlayUneditedBaselineMapFromDisplayNotes` /
`ensureBaselineMapEntriesForEvaluationScope` / `appendOverlapTargetActions`.
Do not rematerialize `EditSession.store` on open or fader.

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — extend committed-base selection and the resolver’s display-note source |
| State-transition change? | NO — NOTE_EDIT stays open during overdub |
| Unbounded MIDI-interval owner? | NO if Stage 1 reuses the existing capture gather / visual cache. **Stop** if the patch rematerializes the full loop on every overdub frame or fader tick |

Reuse: YES — `ensureNoteEditDisplayProjectionCachesBuilt`,
`NoteGeometryResolver`, existing draw-source compose.

## Pre-implementation review

### Ready

- NOTE_EDIT paint already goes through `projectedNoteEditDisplayNotes`
- Overdub LOOP_EDIT paint already merges snapshot + live Capture
- Resolver already takes a `committedDisplayNotes` vector
- Hydrate 4a pins stay: `noteEditOpenRematerializesLoop(current)` is false;
  `noteEditFocusRebuildFillsCurrentStateFromVisualCache()` is false

### Resolved (user / code)

| Topic | Decision |
|---|---|
| Close NOTE_EDIT on overdub entry | Rejected. Not this slice. |
| Restore open rematerialize | Rejected. Undoes Hydrate 4a. |
| New view type | Rejected. No `EditSourceView` / `NoteEditSourceView`. |
| Persist / each overdubPass | Not the RC. No `LoopPersist` in the fail window. |
| Stage commits | Stage 1 paint, then Stage 2 overlap. One RC per commit. |

### Open before coding

1. Stage 1 reuse pin: call `rebuildDisplayNotesInWindow(..., includeActiveCapture=true)` vs consume the already-built visual cache when it is the overdub compose. Implementer picks the existing function; do not write a third merge.
2. Prepared-hit during Capture: keep snapshot `noteMembership` for committed notes, and add live-capture identities from the composed DisplayNotes. Do not construct LCR on the fader (6.0).

### Proceed?

- Plan YES. Firmware NO until the implement session starts Stage 1.

## Verification

| Gate | Pass |
|---|---|
| Native Stage 1 | Dirty cache + current snapshot + `captureActive` → committed base includes a live-capture note that the snapshot alone omits |
| Native Stage 2 | Mover-hold vs that composed note emits Hide or Shorten with empty apply scratch and mover-only current state at start |
| Device | NOTE_EDIT open → overdub → drag across an existing note: overlap paints immediately; live overdub notes stay on the roll; select does not “restore” notes that never should have left |
| RAM1 | locals ≥ 4000 on `teensy41-capture-serial` |

## Do not

- Close NOTE_EDIT in `beginOverdubSession`
- Fill current state from every visual-cache note
- Rematerialize the apply scratch on current-root open
- Treat Overlay / draw-source gather as a new owner
- Start hydrate 4b–5, DEC-054 Stage 2, or STOPPED idle repair
- Add an unbounded rematerialize on the MIDI interval path
