# NOTE_EDIT + overdub paint/overlap compose

**Status:** Stage 1 device PASS. Stage 2 firmware + native + device PASS.  
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

1. Stage 1 reuse pin — **resolved, then device-corrected.** `captureActive` always `CaptureAwareGather` (`rebuildDisplayNotesInWindow(..., includeActiveCapture=true)`). NOTE_EDIT does not refresh visual cache; `appendCaptureEvent` only bumps `captureDisplayRevision`, which is now in the paint-cache key. [`110622`](../../captures/session_20260909_110622.log) proved clean visual cache is the pre-capture 49-note compose.
2. Prepared-hit during Capture — **Stage 2.** Keep snapshot `noteMembership` for committed notes, and add live-capture identities from the composed DisplayNotes. Do not construct LCR on the fader (6.0).

### Proceed?

- Stage 1 YES (device `112612`). Stage 2 YES — extend Stage 1 committed-base cache; scoped hold upsert only.

## Required stimulus

```yaml
required_stimulus:
  scenario: note_edit_overdub_compose
  stimulus: STIM-NE-OD-COMPOSE-02
  version: 1

must_prove:
  - base_recorded
  - committed_od1
  - in_edit_overdub_active
  - live_capture_notes_present
  - selected_note_moved
  - same_pitch_hold_overlap
```

Stage 1 paint leftover remains `STIM-NE-OD-COMPOSE-01` (live 12–23). Do not edit `01`. TEMP-A was promoted to `02`; do not register `STIM-NE-OD-COMPOSE-01-TEMP-A`. Plan: [`hitl_stimulus_contract_enhancement.md`](hitl_stimulus_contract_enhancement.md).

## Verification

| Gate | Pass |
|---|---|
| Native Stage 1 | **PASS.** Dirty cache + current snapshot + `captureActive` → gather includes snapshot notes **and** the live Capture note. Snapshot-only omits that note. Paint of that composed base keeps `EditSession.store` empty and current state mover-only. `pio test -e native`: **1795 succeeded, 1 skipped**. |
| Native Stage 2 | **PASS.** Mover-hold vs a composed DisplayNote upserts only that overlay row. Hide emits with empty apply scratch. `unionAnalyzeOverlapNoteIds` keeps snapshot ids and adds live Capture. `pio test -e native`: **1798 succeeded, 1 skipped**. |
| Device Stage 1 | **PASS** [`112612`](../../captures/session_20260909_112612.log) / [`112643`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_112643.json): `NOTE_EDIT_OPEN` visual **48**; in-edit overdub `DFRAME` **53 → 56 → 57 → 63** with **18** live notes (12–23); after stop **66**. Prior FAIL [`110622`](../../captures/session_20260909_110622.log) stayed **49** until stop. |
| Device Stage 2 | **PASS** [`123953`](../../captures/session_20260909_123953.log) / [`124023`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_124023.json). Proven as `STIM-NE-OD-COMPOSE-01-TEMP-A`, promoted to `STIM-NE-OD-COMPOSE-02`. Live 48s at ticks **1563–2200**; seed kept; mover **0→1536**; `candidates_hit` candidateCount **1**; `HideNote` type=2 noteId=52 @ 1640–1667; `storeNoteOns=0`. Canonical `01` live 12–23 cannot prove this. |
| RAM1 | **PASS.** locals **7008** on `teensy41-capture-serial` |

## Do not

- Close NOTE_EDIT in `beginOverdubSession`
- Fill current state from every visual-cache note
- Rematerialize the apply scratch on current-root open
- Treat Overlay / draw-source gather as a new owner
- Start hydrate 4b–5, DEC-054 Stage 2, or STOPPED idle repair
- Add an unbounded rematerialize on the MIDI interval path
