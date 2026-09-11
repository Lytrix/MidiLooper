# Overdub — published LoopSnapshot omits bar-crossing NoteRef

**Status:** Stage 2 RC-A **SHIPPED** + Stage 4 HITL triangulation **SHIPPED** +
Stage 5 unwind **6 / 5 / 3 done** (2026-09-11). Next: consumer Stage 1 retain,
then Stage 2 source-view merge. RC-B remains latent / separate.  
**Consumer containment (do not treat as end state):**  
[`overdub_start_snapshot_omits_bar_crossing_record_note_bugfix.md`](overdub_start_snapshot_omits_bar_crossing_record_note_bugfix.md) Stages 1–2 remaining  
**Evidence:** [`194531`](../../captures/session_20260910_194531.log) id 27 p74 (pre-fix 31);  
[`205031`](../../captures/session_20260910_205031.log) id 26 p73 @ 1480–1541;  
Stage 4 gate [`102837`](../../captures/session_20260911_102837.log);  
Stage 5.1–5.3 gates [`104115`](../../captures/session_20260911_104115.log) /
[`105156`](../../captures/session_20260911_105156.log) /
[`110457`](../../captures/session_20260911_110457.log)

## Architectural marker

Stages 1–6 are **containment/diagnostic**. They must **not** be accepted as proof
that snapshot membership is architecturally complete.

**End-state invariant:** The immutable `LoopSnapshot` contains every committed
playback/display-relevant NoteRef that belongs in its span. Display/source
consumers trust that representation. VisualCache is a temporary live/capture
display base — not a permanent repair authority for Snapshot completeness.

**Stopping rule:** After Stage 6, no further consumer exception stages unless a
new independent failure demands one. This plan is the next RC.

## Debugging boundary

```text
committed events / STOPPED reconstruct   ← trust (note exists)
 → LoopSnapshot construction / bar membership / publish  ← ROOT (incomplete)
 → draw-source / source-view consumers                   ← currently compensate
 → OLED
```

Do **not** change DEC-051 seal, Occupy Hide/Shorten owners, or wrap Stage 2b
upsert unless investigation proves a formal trigger.

## Textbook evidence

```text
committed events:     note exists
visualCache:          note exists
snapshot:             note missing
draw source:          note missing
STOPPED reconstruct:  note exists
```

## Root cause (confirmed)

The defect is **upstream, in snapshot construction** — not in bar membership
indexing, not in the publish gate, and not in any consumer.

Two cooperating defects in `LoopSnapshotPublish.cpp`:

**RC-A — `queueSessionExitSnapshot` discards live-record open notes.**
While RECORDING, `prepareLiveRecordWindow` scans `capture.store` in bar-wide
windows (`targetCompletedTicks = previousCompletedTicks + Config::TICKS_PER_BAR`).
A note whose NoteOn is inside a window and whose NoteOff is past `includeUntil`
is parked in `PendingPassChangeProgress::openNotes` — its **NoteOn event is
already added to the candidate snapshot, but no `SnapshotNoteChange` is emitted
yet**. `PendingPassChangeProgress::complete()` correctly requires
`openNoteCount == 0` for the capture-store path, but at record stop
`queueSessionExitSnapshot` calls `passChangeProgress.reset()` without honoring
that invariant, destroying the parked open notes.

**RC-B — a re-scan cannot re-pair a note whose NoteOn is already in the parent.**
The session-exit committed-chunk scan replays the whole pass over the
live-record candidate. In `appendNextPassChangeFragment` the NoteOn yields
`addEvent == false` (`eventAlreadyInSnapshot`) and `emitNote == false`
(no end yet), so `addedChanges == 0` takes the early `continue` **above** the
`else if (trackNote)` open-note store. The start is dropped a second time. The
later NoteOff stores a `hasEnd`-only open note that never pairs, so
**no note Add change is ever produced** while both MIDI events are present.

Consequence: the published snapshot holds NoteOn+NoteOff for the note but has
**no `LoopSnapshotNote` / NoteRef**, so `noteMembership(pitch, bar)` and
`appendSnapshotNotesForBarRange` cannot see it. Every later snapshot is
parented on that one, so the hole is inherited for the rest of the session.

### Ruled out (verified, do not re-investigate)

| Hypothesis | Verdict |
|---|---|
| Bar membership only indexes the start bar | **False** — `SnapshotNoteBarCursor::takeNextBar` iterates every bar the span touches; `noteMembership` shares one ref across bars |
| Readers need a neighbour-bar scan | **No** — bucket-only read is correct given correct insertion |
| Publish happens mid-construction | **No** — `freeze()` requires `complete()`; publish gates on `remainingSessionExitPublishWork` |
| Wrap journal caps drop notes | **No** — caps split fragments, they do not discard changes |
| Per-bar 256-ref cap | Not reached in these captures |

### Why bar-crossing notes

Live-record windows are bar-aligned and one bar wide, so the "event committed,
note still open" state exists **only** for notes straddling a window boundary.
Loss additionally requires the note to still be open at the single
`reset()` point (record stop) — which is why exactly one note is lost per
record pass, not every bar-crossing note.

`194531`, `TICKS_PER_BAR = 768`, 32 recorded notes, `from=snapshot` at OD1 open:

```text
DIAG,lcr,src,why=open,from=snapshot,notes=31,root_rev=1,play_rev=1
srcspan id=26,p=73,s=1452,e=1500     present  (no boundary crossed)
srcspan id=27  --- ABSENT ---                 (1500 -> 1554 crosses 1536)
srcspan id=28,p=75,s=1554,e=1610     present  (no boundary crossed)
srcspan count=31
```

`id=14 s=764 e=822` crosses bar boundary 768 and **is** present: it was still
open at window 1's boundary but was paired by window 2, because open notes
survive *between* live windows. Only the reset at stop loses them.

### Freshness vs completeness

`publishedSnapshotIsCurrent()` is `revision == playbackRevision` — a
**recency** predicate. It certifies nothing about membership. The `194531`
line above shows `root_rev=1, play_rev=1` (current) with `notes=31`, so a
consumer that correctly trusts the freshness contract still draws a hole.
The contract is not wrong; the content it certifies was built incomplete.

## Architecture checkpoint (before firmware)

| Question | Answer (post-diagnosis) |
|---|---|
| Ownership change? | **NO** — narrows an existing owner. The scan's single storage stays in `PendingPassChangeProgress`; the commit path stops writing it, so the main-loop idle driver becomes the sole writer instead of one of two |
| State transition change? | **NO** — capture/session-exit transitions unchanged. The live-record → committed-pass scan hand-off already happens at record stop; it moves from destroy/recreate to in-place re-target |
| Unbounded MIDI-interval owner? | **NO** — idle snapshot construction only; open-note array is already capped at `kSnapshotWrapJournalMaxChanges` |

Checkpoint result: **patch allowed** (all three answers NO).

## Ownership pin (no duplicated open-note state)

**Storage:** exactly one — `PendingSnapshotPublishBox::passChangeProgress.openNotes`
plus `openNoteCount`. **Do not** add a carry field, a copy, or a second
progress object. `openNotes` also stays inside `PendingPassChangeProgress`:
pairing state belongs to the scanner that owns pairing, and hoisting it to the
box would be a real ownership move for no benefit.

**Sole writer:** the snapshot idle construction driver on the **main loop** —
`stepPendingSnapshotPublish` → `startNextPendingConstruction` →
`appendNextPassChangeFragment` / `prepareLiveRecordWindow`, reached only from
`processDeferredIdleMaintenance` in `main.cpp`.

`passChangeProgress` is referenced only in `LoopSnapshotPublish.cpp` and its
declaration in `LoopSnapshotBoxes.h`. `LoopSnapshotPlayback.cpp` and
`LoopSnapshotLifetime.cpp` touch the box but never the scan.

### The commit path is not allowed to be a second writer

`Loop::queueSessionExitSnapshot` runs on the capture-commit path, and that path
reaches **ISR context**: `clockTimer.begin([] { clockManager.updateInternalClock(); }, …)`
→ `updateInternalClock` → `trackManager.updateAllTracks` → `pendingStop[i]` →
`stopRecordingTrack` → `stopRecording` → `commitCaptureForStop` →
`commitPendingCapturePass` → `queueSessionExitSnapshot`.

So the existing `passChangeProgress.reset()` at record stop is an **ISR write
that destroys a heap-owning object** (`progress.batch`) which the main loop may
be iterating inside `appendNextPassChangeFragment`. That is a pre-existing
hazard, and it is also the reason a "carry then re-seed" design is wrong: no
amount of discipline stops an ISR writer from clobbering the storage.

## Proposed fix

**Remove the scan write from the commit path; perform the hand-off in place on
the main loop.** The same object is re-targeted from live-record to
committed-pass scanning; no state is copied, so there is nothing to duplicate
and no "old" progress to guard against.

1. **`queueSessionExitSnapshot`** — delete the
   `persistLiveRecordCursor()` + `passChangeProgress.reset()` block entirely.
   The commit path then publishes intent only (`sessionExitQueued`,
   `queuedPassIds`, `companionIds`), which is all it is entitled to do from an
   ISR. `lastCommittedPassId_` still lands in `queuedPassIds` because
   `passAlreadyTracked` compares against `passChangeProgress.passId`, which is
   `kInvalidPassId` on the live-record scan.

2. **`startNextPendingConstruction`** — inside the existing
   `if (box.passChangeProgress.active)` branch, and **before** the
   `captureWindowConsumed()` → `prepareLiveRecordWindow` case, add the
   hand-off with an **explicit transactional precondition** (do not
   partially mutate):

```text
sessionExitQueued && fromCaptureStore
    ├─ queuedPassIds available
    │    → hand off in place
    └─ queuedPassIds unavailable
         → do not mutate progress
         → remain outstanding
         → no publication
```

   **Transactional invariant:** no externally observable progress-state field
   may change until all hand-off preconditions are established
   (`sessionExitQueued`, `queuedPassIds` non-empty, expected pass ID readable).
   Read `expectedPassId = queuedPassIds.front()` first; only then
   `persistLiveRecordCursor()` and retarget.

### STAGE-2 HAND-OFF GATE

**Preconditions (all required before any mutation):**
- `sessionExitQueued == true`
- `queuedPassIds` non-empty / expected committed pass ID readable
- `progress.active == true`
- `progress.fromCaptureStore == true`

**Preserve (same object, same storage):**
- same `PendingPassChangeProgress` instance
- `openNotes` / `openNoteCount`
- open-note **NoteId identity set** (not only the count)

**Retarget / clear / reseed:**
- `fromCaptureStore = false`
- `passId = expected queued pass` (then consume that front entry)
- clear: `batch`, `batchIndex`, `cursor`, `storeWindowComplete`,
  `nextStoreIndex`, `includeUntil`
- reseed: `nextEventId` / `nextCanonicalOrder` from `parent->nextEventId()`

**Forbidden:**
- ISR mutation of `passChangeProgress`
- copying `openNotes` / second open-note store / partial hand-off
- publication before the committed scan completes
- changing any other `PendingPassChangeProgress` field without explicit review
- cleanup/refactor of `PendingPassChangeProgress` in the same commit

Ordering matters: after commit, `capture.store` no longer backs the live
window, so the hand-off must pre-empt `prepareLiveRecordWindow`.

Naming: `handoffLiveRecordProgressToCommittedPass`, file-local beside
`prepareLiveRecordWindow`. Both word parts are existing vocabulary —
`handoff` from `handoffPendingSnapshotLength` (move in-flight snapshot state to
a new target without discarding it), `LiveRecord` from `prepareLiveRecordWindow`
/ `appliedLiveRecordCompletedTicks`, `CommittedPass` from
`committedChunksForPass` / `CommittedEventRange`. No new domain nouns.

### `queuedPassIds` ISR-safety pin (verified 2026-09-10)

`PendingSnapshotPublishBox::queuedPassIds` is `std::vector<PassId>` with **no
`reserve()`**. `queueSessionExitSnapshot` (reachable from the capture/clock ISR
path) still does `push_back` — a **pre-existing** heap-growth hazard on first
enqueue into an empty vector. Live-record already creates the box on the main
loop (`stepPendingSnapshotPublish`), so Stage 2 does **not** add
`make_unique` on the stop path for this RC, but it also must **not** silently
“fix” vector growth in the same commit.

**Stage-2 rule:** remove only the ISR `passChangeProgress.reset()` /
`persistLiveRecordCursor()` scan write. Do not introduce new ISR-side queue
ops, capacity schemes, or allocator changes. If ISR-safe queue capacity must
be guaranteed before coding, that is a **separate ISR-safety RC** — stop Stage
2 production coding until that decision is made (see Open before coding).

### Post-hand-off evidence (required)

After a successful hand-off, assert or DIAG-prove (not a new runtime
mechanism — enough to catch a bad transition):

- `fromCaptureStore == false`
- `openNoteCount` unchanged across the call
- preserved open-note **NoteId identity set** unchanged (e.g. `{27}` → `{27}`,
  not merely count `1` → `1`)
- `progress.passId` equals the expected committed pass that was read from
  `queuedPassIds` before mutation

### Every remaining writer of `openNotes`, and why none can clobber it

| Site | Context | Why it cannot lose the entries |
|---|---|---|
| `queueSessionExitSnapshot` | commit path, **can be ISR** | No longer touches the scan after this fix |
| `startNextPendingConstruction` hand-off (new) | main loop | Transitions in place; explicitly preserves `openNotes` |
| `startNextPendingConstruction` reset (queued pass) | main loop | Unreachable while `active` — the `if (active)` branch returns first, and the hand-off keeps it active |
| `prepareLiveRecordWindow` | main loop | `reset()` only under `!progress.active`; the `else` branch already preserves `openNotes` between windows |
| `finishPendingConstruction` reset | main loop | Guarded by `progress.complete()` |
| `discardPendingIfLoopLengthChanged` | main loop | Resets `unpublishedCandidate` **and** `construction` too, so the parent holds neither the event nor the note — consistent, not lossy |

`remainingSessionExitPublishWork` already counts `passChangeProgress.active` as
outstanding work, so publication stays blocked until the handed-off scan
finishes. No publish-gate change needed.

### Observability (required by the per-commit checklist)

`finishPendingConstruction` resets on `complete()`, which for the
committed-pass path is `cursor.complete && batchExhausted()` and does **not**
require `openNoteCount == 0`. Do not tighten it — a pass may legitimately end
with a wrap-held note open. Instead surface it: add `open=%u` (from the
existing `Loop::liveRecordOpenNoteCount()` accessor) to the
`#CAP,DIAG,snapshot_idle` line so any future scan that resets with parked
notes is visible in a capture.

### Do not also land the RC-B guard

Moving the open-note bookkeeping above the `addedChanges == 0` early `continue`
would remove the symptom too, but it is **defensive hardening, not an invariant
requirement**, once the hand-off lands. With the entry preserved, the
re-scanned NoteOn resolves via `findOpenPassNote`, `prospective` already carries
`hasStart`, and the skipped write-back changes nothing.

Scope: the live-record path is gated on `!loop.publishedSnapshot()`, so this RC
is confined to the **first record pass into a slot with no published root**.

## Latent sibling defect (separate RC — do not bundle)

`appendNextPassChangeFragment` pushes the event change **before** reserving an
open-note slot, then `break`s when `addOpenPassNote` returns null without
advancing `batchIndex`. The event lands in the parent, the re-visit sees
`addEvent == false` with no open note, and `addedChanges == 0` skips it
permanently — the same silent loss with a different trigger.

The `addedChanges == 0` guard does **not** repair this: the re-visit fails
`addOpenPassNote` again, converting silent loss into a scan stall
(`progress.active` never clears, nothing publishes). Correct remedy is
ordering — reserve the slot before pushing the event, so a fragment applies
both or neither. Requires 33 concurrently open notes in one pass scan; code
path proven, not yet observed in a capture. Track as its own root cause.

Not required, and explicitly out of scope: bar-membership changes, reader-side
neighbour scans, publish-gate changes, wrap/seal ownership, Hide/Shorten
ownership.

## Stages

1. **Native fixture (hard gate — fail before patch).** Extends
   `test_live_record_held_note_pairs_on_next_bar_off`
   (`test/test_loop_take_survival/`), which covers only the *intra*-record
   window hand-off and therefore passes today.
   - **1A — reproduce current failure** against unpatched code: live-record →
     note crosses the final live-record window → stop → session-exit snapshot
     → published snapshot missing the expected NoteRef / membership. Capture
     the specific NoteId (seed: `id=27`) and failing membership assertion.
   - **1B — keep the fixture in place.** Do **not** begin Stage 2 production
     coding until 1A demonstrably fails. Causal sequence: failing fixture →
     RC-A implementation → same fixture passes. Fixture and implementation are
     separate coding slices.

   **Stage 1A gate (2026-09-11) — FAIL proven (pre-patch).** Stage 1B fixture
   remains; post-Stage-2 the same fixture **PASSES**.

   - Fixture: `test_session_exit_keeps_bar_crossing_live_record_open_note`
     in [`test_loop_take_survival.cpp`](../../test/test_loop_take_survival/test_loop_take_survival.cpp)
   - Command: `pio test -e native --filter test_loop_take_survival`
   - Pre-patch: `Expected Non-NULL` at `published->findNote(noteId, 60)` after
     `drainPendingSnapshotPublish` (`publishedSnapshotIsCurrent()` already true).
     `noteId` is `lastCaptureNoteOnId` after the bar-0 live-record window
     (capture seed `id=27` is evidence shape only).
   - Sibling `test_live_record_held_note_pairs_on_next_bar_off` **PASSED**.

2. **SHIPPED (2026-09-11) — RC-A hand-off.** Removed commit-path
   `persistLiveRecordCursor()` / `passChangeProgress.reset()` from
   `queueSessionExitSnapshot`; added
   `handoffLiveRecordProgressToCommittedPass` before
   `prepareLiveRecordWindow` when `sessionExitQueued && fromCaptureStore`;
   `open=` on `#CAP,DIAG,snapshot_idle,inner`. Owner:
   [`LoopSnapshotPublish.cpp`](../../src/Loop/LoopSnapshotPublish.cpp).
   Native fixture green; RAM1 locals ≥ 4000 on `teensy41-capture-serial`.

3. **SHIPPED (validation sequence complete with Stage 2/4).** Causal order
   held: native fixture → full native → capture-serial RAM1 → HITL `base`.

4. **SHIPPED (2026-09-11) — HITL triangulation.** Accepted on 3-bar
   (`live=2304`) seeds used throughout this investigation; textbook 2-bar
   `id=27@1500–1554` / `live=1536` geometry is **not** required to close
   Stage 4.
   - Gate: [`102837`](../../captures/session_20260911_102837.log) HITL `base`
     **PASS**
   - recorded count = **32**; `DIAG,lcr,src,from=snapshot` **notes=32**
     (pre-fix [`194531`](../../captures/session_20260910_194531.log) was 31)
   - post-record PLAYING `DISP` reconstruct count = **32**
   - bar-crossing membership present: **id=12** across 768, **id=25** across
     1536 (`id=27` at 1691–1762 on the 2304 seed — interior of bar 2)
   - no `DIAG,draw_src,diff,...,new,0,0,0` for a record-pass NoteId

5. Unwind consumer containment (after Stage 4 — **now open**), **one layer per
   change** with native + source validation between removals — do not land the
   whole unwind in one commit. Order: 6 → 5 → 3 call sites → 1 → 2.

## Commit boundary (Stage 2 — one RC commit)

**Commit title:** Preserve live-record open-note state across session-exit
snapshot hand-off

**In this commit:**
- remove commit-path `persistLiveRecordCursor()` / `passChangeProgress.reset()`
- main-loop in-place `handoffLiveRecordProgressToCommittedPass`
- `open=` on `#CAP,DIAG,snapshot_idle`
- transactional hand-off gate + post-hand-off evidence (including NoteId set)

**Absolutely not in Stage 2 (freeze before coding):**
- RC-B / `addOpenPassNote` ordering / `addedChanges == 0` reorder
- open-note capacity / `kSnapshotWrapJournalMaxChanges` changes
- ISR-safe `queuedPassIds` capacity/`reserve` (separate RC if required)
- consumer containment removal (Stage 5+)
- DEC-051 seal / Occupy / Hide / Shorten / wrap Stage 2b upsert
- bar-membership/index / neighbour-bar reader scans
- publish-gate / `publishedSnapshotIsCurrent()` / VisualCache completeness
- second open-note store / carry state / new manager/scheduler
- capture/session-exit state-transition redesign
- cleanup/refactor of `PendingPassChangeProgress` while touching the hand-off

## Consumer Stage 1–6 classification

| Stage | Owner touched | Verdict | Rationale |
|---|---|---|---|
| 1 — retain clean visual over smaller current gather | `shouldRetainVisualCacheOverIncompleteCurrentSnapshotGather` | **REMOVE-LATER** | Exists only because a current snapshot can be incomplete. Membership fix removes the premise |
| 2 — source-view merge for `from=snapshot` | `rebuildOverdubSourceSpanCache` | **REMOVE-LATER** | Same premise. Keep the `!= "snapshot"` origins (prepared/visual/passes/prior) — those are genuinely partial by design |
| 3 — `mergeOmittedCommittedNoteOnIdentitiesInto` at visual/idle-slice/adopt | `LoopVisualCache.cpp` | **REMOVED (2026-09-11)** | Visual/adopt/idle no longer heal incomplete snapshot membership. Helper KEEP for source-view. Gate [`110457`](../../captures/session_20260911_110457.log): `from=snapshot notes=32`, no `draw_src→0` |
| 4 — reapply pending/sealed after any event merge | `applyPendingNoteChangesToDisplayNotes` / `applyPendingSnapshotCompanionsToDisplayNotes` after merge | **KEEP — REQUIRED** | Independent correctness rule: raw-event or reconstruct merge must not resurrect sealed Hide/Shorten. True regardless of snapshot completeness |
| 5 — drop the `visualCacheDirty` veto | `shouldRetainVisualCacheOverIncompleteCurrentSnapshotGather` | **RESTORED (2026-09-11)** | Freshness veto back: dirty visual no longer overrides current snapshot. Gate [`105156`](../../captures/session_20260911_105156.log): `from=snapshot notes=32`, id=27 crosses 1536; zero-length id1/id2 `draw_src→0` only (not bar-crossing) |
| 6 — NoteId heal on draw source | `gatherCommittedDrawSourceBars` + `mergeDisplayNotesByNoteId` | **REMOVED (2026-09-11)** | Unwound after RC-A + Stage 4. Gate [`104115`](../../captures/session_20260911_104115.log): `from=snapshot notes=32`, id=27 crosses 1536, no record `draw_src→0` without the heal |
| Instrumentation (`DIAG,lcr,src`/`srcspan`, `draw_src,diff|layers`) | `DisplayNoteResolve.cpp` | **KEEP** | Only reason this RC is provable. Gated by `SESSION_CAPTURE` |

Order of unwind after the membership fix: 6 → 5 → 3 call sites → 1 → 2.
Keep Stage 4 triangulation criteria and the instrumentation permanently.

**Stage 5 progress:** layers **6 REMOVED**, **5 veto RESTORED**, **3 call
sites REMOVED** (helper KEEP) (2026-09-11). Next: remove incomplete-current
retain predicate (consumer Stage 1), then Stage 2 source-view `from=snapshot`
merge.

## Regression risks

| Risk | Mitigation |
|---|---|
| Preserved entries hold stale geometry vs the committed chunk copy | Nothing is copied; the same entry is kept. The committed scan's NoteOn re-runs `updateOpenPassNoteFromEvent` on it (idempotent) and the NoteOff supplies `endTick` |
| Re-scanned NoteOn creates a second open entry | `findOpenPassNote` matches on `noteId`, so the re-scan updates the preserved entry; the pair emits once |
| Preserved note emits an Add for a note the parent already has | Guarded by the existing `parentHasNote` (`findNoteAtPitch`) check, which suppresses `trackNote` |
| Hand-off runs while `queuedPassIds` is empty | **Do not mutate** progress; leave scan active/outstanding; `remainingSessionExitPublishWork` blocks publication. Never partially retarget the scanner |
| Removing the commit-path reset leaves live-record fields set on a committed scan | The hand-off clears `fromCaptureStore` / `nextStoreIndex` / `includeUntil` / `storeWindowComplete`, so `complete()` and `captureWindowConsumed()` take the committed-path branches |
| Open-note array overflow (`kSnapshotWrapJournalMaxChanges = 32`) | `addOpenPassNote` already returns null and breaks the fragment; carry must respect the same cap |
| Sealed Hide/Shorten target gains a NoteRef it should not have | Companions are applied after passes in the same publish cycle; with `previous` now resolvable, `Remove` actually removes (today it is a silent no-op — see Q6) |
| Consumer heals mask the fix during validation | Validate with the `from=snapshot` source count and `draw_src` diff, which read the snapshot before/independently of the Stage 6 heal |

## Non-goals

- Another paint-time heal
- VisualCache as second completeness authority
- Wrap-held Option C unless this investigation points there
- Bundling RC-B into the Stage 2 commit (latent; separate RC)
- Weakening `publishedSnapshotIsCurrent()` — freshness ≠ completeness
- Silently “fixing” pre-existing `queuedPassIds` ISR `push_back` growth inside
  the RC-A commit
- Requiring `openNoteCount == 0` at committed-pass `finishPendingConstruction`
  (wrap-held notes may remain open; scan complete ≠ all notes closed)

## Pre-implementation review (ChatGPT project, 2026-09-10)

**Source:** ChatGPT project `Refacator datamodel midilooper` → chat
*Review implementation plan*
(`…/c/6aa32fca-0c44-83eb-add1-cf7f83ba056c`). Earlier same-day chat
*Snapshot Root Cause Review* approved Stage 1→2 direction; this review is the
implementation-authority pass.

### Verdict

**APPROVED WITH TIGHTENING**

Proceed? **YES** — Stage 1A failing fixture only, then Stage 2 RC-A-only after
the tightenings below are in the plan. Do not begin Stage 2 production coding
until Stage 1A demonstrably reproduces loss of `id=27`.

### Keep (confirmed)

- RC-A sole Stage-2 target; preserve `openNotes` across live-record →
  committed-pass hand-off
- Single open-note authority under `passChangeProgress` (scanner keeps pairing
  ownership while the scan retargets source representation)
- No ISR write to `passChangeProgress`; main-loop performs the hand-off before
  `prepareLiveRecordWindow()`
- No-queue → no-mutation / transactional hand-off
- Freshness ≠ completeness; do not weaken `publishedSnapshotIsCurrent()`
- RC-B and consumer containment stay out of the RC-A commit
- Consumer unwind order 6 → 5 → 3 call sites → 1 → 2; Stage 4 + instrumentation
  permanent; Stage-3 helper KEEP, membership-repair call sites REMOVE-LATER
- Committed-pass `complete()` must not require `openNoteCount == 0`

### Change / tightenings folded into this plan

1. Stage 1A/1B hard gate: fixture must fail on current code before Stage 2.
2. Hand-off is transactional: establish all preconditions before any progress
   mutation; STAGE-2 HAND-OFF GATE lists preserve/retarget/forbidden fields.
3. `queuedPassIds` ISR-safety pin: verified as pre-existing `std::vector`
   growth hazard; Stage 2 must not silently fix it (separate RC if required).
4. Post-hand-off evidence includes preserved NoteId identity set, not only
   `openNoteCount`.
5. Stage 4 triangulates recorded=32 / snapshot-source=32 / STOPPED
   reconstruct=32 plus `id=27` in both bars via `from=snapshot`.
6. Stage 3 validation is causal (native before HITL interpretation).
7. Consumer unwind is one layer at a time with validation between removals.
8. Expanded Stage-2 exclusion freeze (capacity, seal/Occupy/Hide/Shorten,
   VisualCache completeness, progress refactor, etc.).

### Do not bundle

RC-B, ISR queue-capacity fix, consumer containment removal, DEC-051 /
Occupy / Hide / Shorten / wrap Stage 2b, bar-index, publish-gate,
freshness-predicate, second open-note store, progress cleanup/refactor.

### Architecture target (reviewer)

```text
Capture / committed events
        ↓
LoopSnapshot construction
        ↓
COMPLETE immutable membership
        ↓
Display / playback consumers
```

VisualCache remains a live/capture display base — not a shadow authority for
snapshot completeness.

### Proceed?

- Stage 1A–4: **SHIPPED**. Stage 5 unwind: **6 REMOVED**, **5 veto RESTORED**,
  **3 call sites REMOVED** — HITL [`104115`](../../captures/session_20260911_104115.log) /
  [`105156`](../../captures/session_20260911_105156.log) /
  [`110457`](../../captures/session_20260911_110457.log).
- **YES** for next unwind layer only — consumer Stage 1 retain predicate.
  Then Stage 2. Do not start RC-B in the same commit.
