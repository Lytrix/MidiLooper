# Wrap publication ownership map (owner ruling 2026-11-03)

Status: design input, not implementation authority. No code changes. It answers one question: how can a finished
overdub interval reach a bounded publication worker, without copying it on the Clock, without losing consequences,
and without unbounded accumulation? It also asks which existing owner already does each part.

Authority this must satisfy: `docs/Authority/Architecture/ExecutionBounds.md` row 22.

> Wrap and session-exit only enqueue bounded work ... Construction remains **not** admitted to `handleMidiInput()`,
> clock dispatch, track-tick, or the `CommitReason::OverdubWrap` call stack; the wrap path queues the fragment only.

Today the wrap fails this, and not through construction. *Building* the fragments it enqueues is content-sized: 439
change sets in about 608 ms (`2026-11-03-c5-3b-successor-grant-budget.md`, "Wrap journal attribution").

## 1. Today: what the wrap reads, and when each input stops being stable

`Track::commitOverdubWrap`, in Clock dispatch:

| step | reads | writes |
|---|---|---|
| `extractOpenCaptureNoteOns` | `capture.store` | removes held NoteOns from it (they re-enter B) |
| `commitCapturePass(OverdubWrap)` | `capture.store` | **pass A**: an immutable committed chunk list; `playbackRevision` |
| `queueOverdubWrapSnapshotJournal(A)` | **`pendingNoteChanges_`** (mutable), the parent root or `unpublishedCandidate`, pass A's chunks | `box.queuedWrapJournals` (all fragments, content-sized), `box.journaledPassIds` |
| `sealPendingNoteChangesToEditPasses` | `pendingNoteChanges_` | **Shorten/Hide → companion EditPass rows** (durable, identity-addressed); then **clears** `pendingNoteChanges_` |
| `recordLastOverdubSeal` | companion IDs | `lastOverdubSealCompanionIds_`; stamps the journal fragments' `targetRevision` |
| `beginCapture(Overdub)` + carry | held NoteOns | interval B's capture, prewarmed staging |

Input lifetimes:

- **`pendingNoteChanges_` is the only mutable input, and it lives for one Clock.** The seal clears it in the same
  call, and B writes it from its first NoteOff. So the journal cannot simply be called later: by then its input is
  gone, or it is B's.
- **After the seal, the interval's whole semantic content is already durable and immutable.**
  - **Adds** are the notes in pass A's committed chunks. The seal turns only Shorten/Hide into EditPasses
    (`sealPendingNoteChangesToEditPasses`, the `Shorten`/`Hide` filter), so an Add row carries nothing that pass A
    does not already hold.
  - **Shorten/Hide** are the companion EditPass rows. They are addressed by ID, and the seal's own `EditPassIdList`
    names them.
  - **Open notes crossing the wrap** are not in A. Their NoteOn moves to B before commit, so A has no Add row for
    them and B will (pinned in §4).
  - **Non-note events** are in A's chunks.
- **Mutation after the seal.** A companion row only toggles `Active` ↔ `Disabled`: by undo (`TrackUndo`
  `setEditPassState`) or by pass removal (`LoopPasses`). A pass's chunks never change. Both mutations already
  invalidate pending publication through the DEC-073 coverage watermark (`coveredPassRevision`,
  `rebuildFromPassInventory`). So B cannot change A's content, and undo is an existing, named path.

Conclusion: **no row freeze is needed.** The owner's "ownership transfer of already-stored interval data" exists
after the seal: the transfer is `(passId A, companion EditPassIdList A)`, in O(1), and the list can be moved.
`swapPendingNoteChanges` (the deferred stop route's O(1) handoff into `CaptureStopJob::pendingChanges`) is the
precedent for moving a *pre-seal* set. The wrap does not need it, because the wrap seals in the same Clock.

## 2. The existing publication owner already has a bounded producer for A's notes

`PendingSnapshotPublishBox` (`src/Loop/LoopSnapshotBoxes.h`) has two mutually exclusive producers for an overdub pass.
`passAlreadyTracked` makes them exclusive:

```
(a) wrap journal     commitOverdubWrap → queueOverdubWrapSnapshotJournal → box.queuedWrapJournals
                     (built eagerly from pendingNoteChanges_, in the Clock)        + box.journaledPassIds
(b) pass producer    box.queuedPassIds → passChangeProgress → startNextPassChangeFragment
                     (refillPassChangeBatch / appendNextPassChangeFragment: a cursor over the pass's immutable
                      chunks, journal-capped at 32 changes, one-pitch findNoteAtPitch, NoteOn/NoteOff pairing via
                      open-note tracking, DEC-047 fragment transaction)
          ↓ both
     LoopSnapshotConstruction (row 21, bar-bounded) → atomic publication when everything queued is complete
```

- (b) already runs during overdub. `stepPendingSnapshotPublish` calls `enqueueLiveOverdubPasses` on every turn that
  may start construction. That call queues every committed session pass the box does not track.
- **Today the wrap pass is skipped only because (a) marked it journaled.** Without (a), (b) picks the pass up on the
  next idle publish turn, from immutable data, in bounded units, with no Clock work.

Notes and non-note events therefore need no new producer.

## 3. The one real gap: companions mid-session

- (b) produces a pass's notes and events, not its companions.
- The box applies companions only at session exit: `if (box.sessionExitQueued && !box.companionsApplied)` →
  `appendCompanionChanges` (`appendEditRowChange` + `appendCompanionEventChanges`).
- That call is also one unbounded change set per step. It runs in the worker, not the Clock, but it is still
  content-sized.
- (a) is today's only mid-session carrier of a wrap's Shorten/Hide. That is the reason it exists.

So the extension is one producer kind inside the existing box, not a new transaction manager: **a bounded companion
fragment producer.**

- **Input:** an ordered `EditPassIdList` moved in at the wrap, queued *after* its pass.
- **Unit:** a cursor over the IDs, at most 32 changes per fragment, under the same DEC-047 transaction.
- **Translation:** the existing `appendEditRowChange` / `appendCompanionEventChanges`, which are identity-addressed;
  `appendEditRowChange`'s `findNote` is the semantic Create→Replace check, not dead work.
- **Same owner:** the session-exit companion step becomes this producer too. That also bounds the session-exit path.

Once the manifest reader replaces the legacy snapshot path, the producer kinds disappear behind the box. MIDI ingress,
interval ownership and the publication barrier stay as they are.

## 4. Successive wraps and capacity exhaustion

- **A second wrap before A publishes.**
  - The box queues A's obligation and then B's: `queuedPassIds` for each pass, with its companion list after it.
    They are consumed FIFO.
  - Order is required: B's companions may target A's notes (B shortens a note A added). FIFO applies A first.
  - Publication stays atomic. The root advances only when everything queued is complete (row 22), so no prefix is
    ever visible.
  - What accumulates is one pass ID plus one moved ID list per wrap. That is an index of content that is durable
    anyway, never a copy of it. Today, every wrap holds a full private fragment copy in `queuedWrapJournals`.
- **Capacity exhaustion.** This is lossless by inventory.
  - The box is an index, and `loop.passes` is the source of truth.
  - If queueing the obligation cannot allocate, nothing is lost. `enqueueLiveOverdubPasses` re-discovers every
    untracked committed pass. `queueSessionExitSnapshot` re-discovers every untracked Active EditPass (it already
    enumerates both), and DEC-073's watermark keeps the root from claiming coverage it lacks.
  - The open design point is not storage. It is a **defined policy for the root's lag**:
    - Atomic publication waits for the queue to empty. Under continuous overdub, the queue can refill as fast as it
      drains, and the root never advances. The stale-root overlay keeps playback correct (DEC fix `2b948d2a`), but
      the lag is unbounded.
    - The policy needs one of two things. Either a throughput bound (the producer drains one loop's pass within one
      loop: about 41 fragments for 1,318 events at 2 units per idle turn, to be measured), or a named
      "publish completed prefix of whole obligations" rule.
    - That rule would be an amendment to row 22's atomicity, so it is an owner decision.

## 5. What stays on the Clock after the change, and other open costs

- **Stays:** pass commit (29 ms), seal (3 ms here, O(Shorten/Hide)), `recordLastOverdubSeal`, session bookkeeping
  and carry.
- **Leaves:** the whole journal (608 ms).
- **Separately open, not part of this slice:**
  - Held-NoteOn extraction: 98 ms with one held note.
  - The C3.4 forced deadline with outstanding completions: unmeasured. These sweeps only showed it is cheap when
    nothing is owed.
  - `recordLastOverdubSeal`'s overlay merge, which scans `passes.editPasses` once per overlay ID.
  - The layout-sensitive probe boot hang: a qualification risk. The uncommitted breadcrumbs that make it boot are not
    a fix.

## 6. RED specification (the next slice; the deferred worker is not implemented)

All of these run on host against the production path (`Track::commitOverdubWrap`, then `stepPendingSnapshotPublish`
until idle). The reference is today's journal-based result.

1. **Clock ownership.** A wrap with N fresh Adds builds no snapshot change set in the Clock.
   - Witness: a native counter of journal row translations or `LoopSnapshotChangeSet` construction inside
     `commitOverdubWrap`, which must be 0.
   - `box.queuedWrapJournals` stays empty, and pass A is a tracked obligation.
   - RED today: 439-style rows are translated in the wrap.
2. **Immutable input.** Wrap A, begin B. In B, add notes, shorten one of A's notes, hide a source note, and wrap B.
   Then drain.
   - The published root equals the sequential reference: A applied, then B.
   - A's contribution also equals A drained alone, before B existed.
3. **Semantic parity.**
   - The final root is identical to today's for Add, Shorten, Hide and non-note events.
   - A note held across the wrap is absent from A and present once in B, with the same NoteId.
   - A C3.4-owed completion is answered before A's seal: its transform is in A's companions, and so in the root.
4. **Publication identity.**
   - The root never shows A's notes without A's companions; there is no partial publish.
   - The published revision and coverage after the drain equal today's: `targetRevision` and `coveredPassRevision`
     both equal the post-seal `playbackRevision`.
   - The stale-root overlay keeps A audible while the root lags (extending
     `test_stale_root_overlay_keeps_a_journaled_wrap_pass_after_session_close` to the queued-pass form).
5. **Two rapid wraps.**
   - A and B both wrap before any publish turn.
   - The box holds two pass obligations and two companion lists, and no fragment copies.
   - The drain applies them FIFO; B's companion that targets A's note applies after A's pass.
6. **Capacity.**
   - With the obligation queue's allocation failed by injection, the next publish turn still re-discovers the pass
     and its companions, and the drained root is identical.
   - Nothing is double-applied: one fragment per change, checked by counting in the construction.

## 7. Recommendation

- **Change.** Stop building the wrap journal in the Clock. Hand off `(pass A, companion list A)` to the existing box
  as two FIFO obligations. Let the existing pass producer take A's notes. Add one bounded companion producer, which
  also replaces the session-exit companion step.
- **No new lifecycle and no new pool.** The input is already immutable after the seal.
- **Main risk.** The publication-lag policy (§4), which is an owner decision.
- **Second risk.** A semantic difference between the producers that the parity RED has to catch:
  - (b) adds a note only if the parent lacks it (`findNoteAtPitch`, `eventAlreadyInSnapshot`).
  - (a) adds every Add row unconditionally.
  - For fresh notes they agree. For a re-recorded NoteId, they could differ.
