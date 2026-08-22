# LoopContentResolution span consume refinement

**Status:** Architectural review and direction. **Authorizes no firmware.**
**Scope:** Establish `LoopContentResolution` spans / checkpoints as the single derived authority for effective committed notes around the current tick, replacing the reverted paired-companion gather consume.
**Supersedes:** `paired_companion_suffix_catchup_refinement.md`, `paired_companion_playback_event_reuse_and_naming_refinement.md`, `paired_companion_length_last_wins_refinement.md` — all reverted with `PairedCompanionState`.

---

## 1. Why the paired companion consume was reverted

The paired-companion projection was built so that playback-window construction could use a
pre-resolved companion view instead of replaying companion `EditPass` history through
`applyNoteEditPassSequence`. On device it never served that purpose.

From [`163135`](../../captures/session_20260822_163135.log), captured on the reverted branch. The
`paired_proj` / `win_apply` gather tags shipped with that work and are **not** emitted after the
revert:

| Measure | Value |
|---|---|
| Gathers served `from=paired_proj` | **0** |
| Gathers served `from=win_apply` | **33** |
| `idle_gather` | median 1.61 s, max 7.75 s |
| `midi_gap` | max 7.82 s |
| `playback_build` | median 4.1 ms, max 153.8 ms |
| `#CAPTURE_RECONNECT` | 1 |

The projection was never once consumed, so every gather replayed companion history live. The
observed USB drop followed multi-second `win_apply` gathers after undo, not a fault in the
projection itself — the projection simply was not there.

Undo is the structural reason it could not be there. `Loop::clearCompanionProjectionCache`
wipes the projection on any non-suffix history change, and an undo that disables capture or
companion rows is not an append-only suffix. Stage 4 suffix catch-up preserved the projection
across **overdub seals** only; it had no answer for undo.

---

## 2. Overlap resolution never used it

Reverting the projection does not touch overlap correctness. The overlap path reads two other
stores:

| Moment | Owner | Data |
|---|---|---|
| Note-on (occupy) | `Track::snapshotOverlapHoldCandidates` → `Loop::collectOverdubNoteOnParticipantIds` | Open note identities from `ActiveNoteLedger` |
| Note-off (geometry) | `Loop::accumulatePendingNoteChangesForIncomingNote` → `OverlapCandidateLookup::appendNotesForIds` → `resolveConstrainedGeometry` | Windowed spans from `overdubSourceViewNotes_` |

Companion rows are **produced** by `Loop::sealPendingNoteChangesToEditPasses` at wrap and stop.
Keeping companion logic on the ledger is the shipped design, not a change.

---

## 3. The architectural defect: two authorities, opposite invalidation

Two derived views answered the same question. The one wired to the hot consumer had the weaker
invalidation model. This is the defect that must not be recreated.

| Property | `PairedCompanionState` (reverted) | `LoopContentResolution` prepared |
|---|---|---|
| Undo of a capture pass | wipes | `setPreparedCapturePassState` + `restampPreparedPlaybackRevision` — keeps serving |
| Undo of a companion row | wipes | `setPreparedEditPassState` + restamp — keeps serving |
| Restores pre-edit geometry after undo | no | `eraseDisabledSounding`, `restoreDisabledCompanions` at query time |
| Wrap / stop append | suffix catch-up (new code) | `publishPreparedOverdubPass` (already shipped) |
| Present-at-tick query | none | `tryCollectPreparedPresentNoteIdsAtTick`, `tryResolvePreparedState` |
| Consumed by playback-window build | yes | **no** |
| Available in production builds | yes | **no** — `SESSION_CAPTURE` only |

`Loop::copyEffectiveCommittedEventsInRange` contains no reference to `LoopContentResolution`, so
`ensurePlaybackMergedMidiEventsBuilt` cannot reach the cache that survives undo. It only sees the
one that does not.

### Post-revert baseline

After the revert there is no live companion cache at all. `Loop::tryCopyCompanionProjectedWindow`
is copy-only by design (it must not rebuild on the clock path), and
`Loop::rebuildCompanionProjectionForCurrentRevision` — its only builder — has **no callers**.
`companionProjectionEvents_` is therefore never populated and `companionProjectionReady_` never
becomes true.

Every `copyEffectiveCommittedEventsInRange` call with `includeActiveEditRows` consequently replays
active companion rows through `applyNoteEditPassSequence`. That is slow and scales with companion
row count, but it is single-authority and cannot be stale. It is the correct baseline to build
from.

---

## 4. Only the span path is companion-free

This distinction decides the direction.

`LoopContentResolution::resolveWindow` on the prepared `TickIndex` still calls
`applyNoteEditPassSequence` per capture pass inside `emitApplyEditsPerCapturePass`, over every
active companion row. It restricts events to the window, so it is cheaper than a full replay, but
its cost still scales with companion row count — and companion rows grow with every overdub stop.

`LoopContentResolution::resolveState` on `StateCheckpoints` does not. Companion effects are baked
into `NoteSpan` at build time; `spanBoundaries` and the sparse `presentAt` checkpoints answer
present-at-tick directly, and undo is handled by post-filtering disabled passes and restoring
disabled companions.

**A better event-replay cache cannot solve this. A span-derived representation can.** Where a
consumer needs events, emit them from spans rather than replaying companion rows.

---

## 5. Blocker: the prepare never completes

Both caches were unreachable for the same reason — the first build does not finish.

| Capture | Observation |
|---|---|
| [`155153`](../../captures/session_20260822_155153.log) | `idx` phase advanced **13.5 events/s** (1512 events in 112 s); never completed |
| [`163135`](../../captures/session_20260822_163135.log) | 331 s in `prep` across passes 14–18; ended `DIAG,lcr,skip,dirty` |

`Track::processDeferredContentResolutionDeviceGate` runs exactly one
`LoopContentResolution::deviceGateRunOneSlice` per idle-maintenance turn, and each slice processes
`kDeviceGateEventsPerSlice` (8) events.

That grain has a recorded origin. Task 5.10 bounded slices to `kDeviceGateSliceBudgetUs` (50 ms),
then 5.11 replaced the budget with the 8-event count because *"a single PSRAM emplace already
exceeds 50 ms"* ([`121702`](../../captures/session_20260815_121702.log)). Tasks 5.15 and 5.17
subsequently replaced those per-entry PSRAM associative inserts with flat bulk arrays under
`ExternalMemoryFirstAllocator`. **The justification for the grain was removed; the grain stayed.**
`kDeviceGateSliceBudgetUs` is still defined and still not enforced.

Until this is fixed, no derived-view design is reachable in a real session.

---

## 6. Direction

**Invariant:** There is one derived authority for effective committed notes. It survives undo by
patching pass state in place and restoring geometry at query time, and it grows at overdub wrap by
appending the sealed pass. No second cache may be wired to the playback-window build.

Reuse, do not rebuild:

* `StateCheckpoints` (`spans`, `spanBoundaries`, `presentAt`, `channelByNoteId`) as the representation;
* `publishPreparedOverdubPass` for wrap and stop append;
* `setPreparedCapturePassState` / `setPreparedEditPassState` / `restampPreparedPlaybackRevision` for undo;
* `eraseDisabledSounding` / `restoreDisabledCompanions` for post-undo geometry;
* `ActiveNoteLedger` unchanged as occupy authority;
* `overdubSourceViewNotes_` unchanged as note-off geometry baseline.

---

## 7. Slice order (none authorized)

| Slice | Content | Gate |
|---|---|---|
| 1 — reachability | Enforce a per-turn time budget in `processDeferredContentResolutionDeviceGate` instead of the 8-event grain; remove the `SESSION_CAPTURE`-only gate on prepare | Device: gate reaches `deviceGateComplete` on the 68-bar loop; no idle turn over the `ExecutionBounds` ceiling |
| 2 — span event emission | Emit window events from `NoteSpan` without `applyNoteEditPassSequence` | Native: equals the `resolveWindow` oracle on companion histories including Delete and Length |
| 3 — consumer wiring | `copyEffectiveCommittedEventsInRange` consults the prepared span path before the `win_apply` fallback | Device: `from=win_apply` absent while prepared is ready; `playback_build` stays in its measured class |
| 4 — undo proof | Undo during PLAYING and STOPPED keeps prepared serving | Device: no `win_apply` after undo; `idle_gather` bounded |

Slice 1 must pass before Slice 3 is designed. A consumer wired to an unreachable cache is what
produced the reverted work.

---

## 8. Out of scope

Do not introduce or reopen:

* a third derived store, or any new Manager or parallel authority;
* paired-companion persistence or boot restoration;
* prepare on the MIDI interval path, the gather path, or `beginCapture`;
* `RuntimeWorkBudget::admit` or interval reservation;
* changes to last-wins or Off-tick Length semantics;
* changes to `ActiveNoteLedger` occupy or `resolveConstrainedGeometry`.

---

## 9. Open questions

1. Does removing the `SESSION_CAPTURE` gate on prepare fit the RAM1 / ITCM budget in a
   production build? `sDeviceGateSession` is `DMAMEM`; the instrumentation is separable from the
   index, but the cost has not been measured.
2. What per-turn budget replaces the 8-event grain? `ExecutionBounds` gives a tempo-dependent
   operating target and a 25 ms hard ceiling; `T_RT,reserve` is still unmeasured.
3. `rebuildCompanionProjectionForCurrentRevision`, `companionProjectionEvents_`,
   `companionProjectionReady_`, `companionProjectionCompanionOnlyRows_` and
   `tryCopyCompanionProjectedWindow` are now unreachable scaffolding. Delete them, or keep
   `tryCopyCompanionProjectedWindow` as the consume hook that Slice 3 repoints at the span path?
   Deleting first keeps the single-authority invariant obvious; keeping it risks a second cache
   reappearing by habit.
4. Slice 3 changes the gather result source. Does it need a `DIAG` tag naming the source, given the
   reverted `paired_proj` / `win_apply` tags are gone and there is currently no way to observe
   which path served a gather?
