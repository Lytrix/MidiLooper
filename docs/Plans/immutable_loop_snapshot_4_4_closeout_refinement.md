# Immutable loop snapshot — parent 4.4 closeout

**Status:** Inventory complete — parent OpenSpec task **4.4 closed**  
**OpenSpec:** `immutable-loop-snapshot-passes` task `4.4`  
**Date:** 2026-09-09  
**Does not start:** 4.5 cross-gate verify, 4.6 snapshot-identity freshness, hydrate 4b–5, raise `N`, §5 undo/load/persistence, §6 compatibility retirement.

## Debugging boundary

```text
4.4a–4.4h + 4.5a
    ← shipped (current-root display / NOTE_EDIT / analysis / validation)
parent 4.4 inventory
    ← this document
4.5 / 4.6 / §5 / §6
    ← later; intentional pass paths below stay until those phases
```

## Closeout invariant

```text
When publishedSnapshotIsCurrent() is true, display, overdub source,
NOTE_EDIT committed base / open / commit rematerialize / bake,
idle validate, and windowed paint use LoopSnapshot content as the
normal source. Pass rematerialize / mergeActiveCapturePasses remains
only (a) no-current-root fallback or (b) capture-only / deferred
oracle dumps named below.
```

## Inventory — production `src/` call sites

Classification:

| Class | Meaning |
|---|---|
| **Current-root normal** | Must use snapshot; leftover would block 4.4 close |
| **No-current fallback** | Gated; keep |
| **Capture / oracle** | Intentional; 4.4g–4.4h leave alone |
| **Out of 4.4** | Owned by 4.6, §5, or §6 — not a 4.4 leftover |

### Current-root normal — all migrated (no leftover)

| Owner | Current-root path | Slice |
|---|---|---|
| `rebuildOverdubSourceSpanCache` | `appendSnapshotNotesForBarRange`; skips post-fill `materializeToEventVector` when `sourceOrigin=snapshot` | 4.4a / 4.4c |
| `resolveDisplayNotesCommitted` incremental | `resolveDisplayNotes(*snapshot)` | 4.4b / 4.5a |
| `rebuildVisualCacheIdleSlice` / `rebuildVisualCacheFromPasses` | span copy / `resolveDisplayNotes(*snapshot)` | 4.4b / 4.4e |
| `rebuildDisplayNotesInWindow` | `tryCopyCurrentSnapshotDisplayNotesInWindow` before event gather | 4.4h |
| NOTE_EDIT dirty committed base | `NoteEditCommittedBaseKind::Snapshot` → `resolveDisplayNotes(*snapshot)` | 4.4d |
| `openNoteEditSession` | current root skips rematerialize + skips `assignMissingNoteIds(loop.midiEvents())` (`noteEditOpenRematerializesLoop`) | 4.4e / 4.4f / hydrate 4a |
| `copyNoteEditCommitBase` / bake / focus | pinned open snapshot events + session edit rows | 4.4e |
| `applySessionEditRows` | `tryCopyNoteEditOpenedSnapshotEvents` before exclude-materialize | 4.4f |
| `copyIdleValidateEvents` / idle validate | span-filtered snapshot events | 4.4g |
| Overdub analysis gather | current-root source skips pass flatten | 4.4a |

### No-current-root fallback — keep

| Caller | Gate |
|---|---|
| `rebuildOverdubSourceSpanCache` → `resolveDisplayNotes(passes)` + identity `materializeToEventVector` | `!publishedSnapshotIsCurrent()` |
| `resolveDisplayNotesCommitted` STOPPED / last-resort | after current-root and hold branches miss |
| `rebuildVisualCache*` / `DisplayColdHelpers::rebuildDisplayNotesInWindow` | `!tryCopy…` / `!usedSnapshot` |
| `NoteEditCommittedBaseKind::PassMaterialize` | selector when not Snapshot |
| `rematerializeEditView` / `copyNoteEditCommitBase` | snapshot copy miss |
| `openNoteEditSession` rematerialize branch | `noteEditOpenRematerializesLoop(true)` only |
| `applySessionEditRows` | opened-snapshot copy miss |

### Capture / deferred oracle — keep (not 4.4 migration)

| Caller | Role |
|---|---|
| `commitEditAction` `take_only` → `mergeActiveCapturePasses` | SESSION_CAPTURE reconstruct parity dump |
| `TrackDeferredMaintenance` SEVT / stored verification flatten | deferred `#CAP` oracle |
| `TrackDeferredMaintenance` deferred REVT `mergeActiveCapturePasses` | capture telemetry |
| Live-capture merge in window gather (`includeActiveCapture`) | active capture window; not committed-root identity |

### Out of 4.4 scope — later phases

| Caller | Why not 4.4 |
|---|---|
| `Track::legacyMidiEventsFromCommitted` / `materializeEditViewFromPasses` | Inactive-session edit boundary; freshness → **4.6** |
| `Loop::midiEvents()` / STOPPED idle `materializeEditViewFromPasses` when store stale | Compatibility effective-store cache; retirement → **4.6 / §6** |
| `Track::findLastEventTick` → `mergeActiveCapturePasses` | Record-stop length, not display/NOTE_EDIT identity |
| `materializePassesExcludingEditPasses` | Dead wrapper; no callers — leave (4.4f) |
| Snapshot-identity undo / clear-slot / SD layout | **§5** |
| Remove maintained LCR / pass replay as normal path | **§6** after 4.5–4.6 |

## Architecture checkpoint (closeout)

| Question | Answer |
|---|---|
| Ownership change? | NO — inventory / checkoff only |
| State-transition change? | NO |
| Unbounded MIDI-interval owner? | NO |

## Decision

**Close parent 4.4.** Sub-slices 4.4a–4.4h and 4.5a already shipped the migrations named in the task. Inventory finds no remaining current-root normal-path display / NOTE_EDIT / analysis / validation oracle inside 4.4 scope.

**Next authorized OpenSpec steps (pick one later):**

1. **4.5** — verify with existing HITL + native gates  
2. **4.6** — snapshot identity for derived-representation freshness (includes `legacyMidiEventsFromCommitted` / effective-store consumers)

## Evidence anchors (already shipped)

| Slice | Device / native |
|---|---|
| 4.4a–4.4c | native overdub source + span provenance |
| 4.5a | [`145633`](../../captures/session_20260908_145633.log) |
| 4.4d–4.4e | [`162207`](../../captures/session_20260908_162207.log) … [`174646`](../../captures/session_20260908_174646.log) |
| 4.4f | [`191346`](../../captures/session_20260908_191346.log) |
| 4.4g | [`193700`](../../captures/session_20260908_193700.log) / [`194438`](../../captures/session_20260908_194438.log) |
| 4.4h | [`201106`](../../captures/session_20260908_201106.log) |
