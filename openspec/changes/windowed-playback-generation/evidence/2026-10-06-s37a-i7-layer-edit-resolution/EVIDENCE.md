# 3.7a (I7) — the retained-source gather resolves the pinned revision's note edits **per layer**

Slice of task **3.7a** (*"Wire the retained-source interval index into the production window consumer while retaining the
legacy fallback"*), carrying the residual I1 named as **I2's** and I2/I3 then deferred: the retained-source gather
refused a revision whose pins carried an Active Note row (`RetainedWindowGatherResult::PinnedEditRowsUnsupported`), so a
rootless (demoted / restored) slot **with note edits** was served *nothing* by the window lane. Packet written before the
edit: [`EXECUTION.md`](EXECUTION.md). Commit: *"3.7a/I7 — the retained revision's note edits are resolved per layer, and
the paged lane refuses what it cannot resolve"*.

## 1. What the refusal was, and why it could not simply be dropped

`Loop::copyEffectiveCommittedEventsInRange` — the effective-content owner, and this change's differential oracle —
resolves Active Note rows **per committed list** and then merges, and says why in its own comment: *"Merged-then-apply
lets LIFO Delete pair a hidden On with another pass's Off (144743 48@616 / Off@1016)"*. I1 therefore refused such a
revision by name rather than answering it flat, and named per-layer resolution as the follow-up. Two consequences were
measured by reading the shipped code before the edit:

* a rootless slot whose revision carries note edits: the gather refused → `requestPlaybackWindow` answered
  `SourceUnavailable`, the deferred lane named `EditRowsUnsupported` and `committedPlaybackRetryCanProgress` declined to
  re-attempt it — while the merged-window fallback is **empty** for that shape by construction. Silence for exactly the
  content 3.14's undo/redo handoff and the restored-library shape live in.
* **and the paged legacy lane was a latent wrong-content path the moment the refusal went away.** `beginPagedPlaybackForSlot`
  pages the *same* pinned lists through `collectPinnedCaptureLists` and resolves **no** rows; today the gather's refusal
  is what makes that unreachable for such a revision. Removing the refusal without a replacement would have paged raw
  pinned bodies, i.e. sounded content the edited revision does not have.

## 2. What landed

1. **The per-layer half of the gather** (`gatherRetainedWindowLayers`, FLASH). The traversal primitive is bounded to the
   layers already resolved **plus the current one** (`appendWindowBudgeted(lists.data(), layerIndex + 1, …)`), so a call
   can never cross a layer boundary: a layer whose traversal completes is one whose events the caller's scratch holds on
   their own. Each completed layer is then resolved in the owner's own order — `sortRetainedWindowLayer` (hand, stable,
   the same reason the existing single-traversal sort is hand: I1 measured `std::sort`'s instantiation at 1,280 B of ITCM)
   → `applyNoteEditPassSequence(layer, rows, loopLengthTicks)` (the shipped owner of the rule, never re-stated) →
   `mergeRetainedWindowLayer` (the owner's own merge rule, `std::merge` semantics: on equal ticks the **accumulated**
   side wins), merged backwards into the room `resize` just made, so no second buffer and no second allocation.
2. **One layer per call.** A layer whose slice returns no event costs no slice, and the traversal advances to the next
   layer inside the same call — so the number of calls a demand needs is driven by its content rather than by its pass
   count. The **last** layer's resolution *is* the completing call (no extra call is spent reporting a completion this
   one already established), and `gather.inspectedChunks`/`eventsAppended` stay per-call as the callers' accounting
   expects.
3. **The rows are the revision's own.** `collectPinnedActiveEditRows` collects the Active Note rows **filtered to the
   passes this revision pinned** (a row created after the retention belongs to a later revision), and
   `retainedRevisionHasActiveEditRows` answers the gather's yes/no question with a plain scan — the rows themselves never
   enter the gather's frame (see §5, the ITCM finding).
4. **The caller owns the layer scratch.** The primitive may not hold it: two callers can gather for the same slot (the
   deferred lane through `runtime.retainedGatherEditRows` and a requester through its own local), and a shared buffer
   would corrupt either. It is released with the window (`discardRetainedGather`), by the same swap-not-clear rule.
5. **The paged lane refuses by name.** `beginPagedPlaybackForSlot` now refuses a revision whose pins carry an Active Note
   row — before any ring exists — as `PagedPlaybackOutcome::PinnedEditRowsUnresolved`, with
   `PlaybackViewPendingReason::PinnedEditRowsUnresolved` as the demand's own reason at both paging origins and
   `committedPlaybackRetryCanProgress` declining to re-attempt it (the 3.12f pattern, applied to the other refusal).
   Serving unresolved bodies is wrong content; not serving is an answer with a name.
6. **The dead vocabulary is gone**: `RetainedWindowGatherResult::PinnedEditRowsUnsupported` and
   `RetainedGatherRefusal::EditRowsUnsupported` are removed rather than left behind as a name nothing can produce.

The index's empty-interval shortcut is now taken **only when no Active Note row is pinned**: a `Create` row adds events the
committed bodies do not carry, so the owner's per-layer answer for such a revision is *not* "empty".

## 3. Verification

| Check | Result |
|---|---|
| focused `pio test -e native -f test_runtime_working_set_ownership` | **166 / 166** (baseline 164; the two new fixtures) |
| full `pio test -e native` | **2,844 cases / 7 skipped / 2,837 succeeded / 0 failed** (baseline 2,842 / 2,835; no suite ERRORED/FAILED) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** — the RAM1 line is **byte-identical to the pre-slice baseline**: `variables 96,192 / ITCM 393,216 (code 392,248 + padding 968) / locals 34,880`, floor 840, **128 B of headroom** |
| `openspec validate windowed-playback-generation --strict` | valid |
| `git diff --check` | clean |
| `git diff --stat` | `include/Track.h` +16/-`, include/TrackPlaybackRuntime.h` +17/-`, `src/Track/TrackPlaybackRuntime.cpp` +22/-`, `src/Track/TrackPlaybackWindowBuild.cpp` +269/-…, the suite +326/-… |

**`LoopPlaybackRuntime` size pins re-based, compiler-measured:** target **7,384 → 7,400** (+16 B) and host
**9,528 → 9,552** (+24 B), for the caller-owned layer scratch (one `SessionMidiEventVec` header per ABI). The target
figure was measured on this ABI with a temporary compile-time probe, then the probe was removed and the pin set to the
measured value — not estimated.

### The fixtures

`test_3_7a_i7_pinned_note_edits_are_resolved_per_layer` — a two-layer revision whose **same-pitch pairs cross** in tick
space (record: On@200 / Off@800; overdub: On@100 / Off@300) plus a pinned **Delete** of the overdub note, and a filler of
130 pairs so the record layer's window content is over one slice:

* the owner's answer is asserted **by its own shape** first (the record pair survives, the overdub pair is gone), then
  the **flat** answer is computed from the same window and asserted to **differ** — so the fixture cannot pass while the
  two resolutions coincide;
* the gather is driven **call by call** (more than one call; per-call `eventsAppended ≤ kRetainedGatherSliceEvents`; the
  final answer element-wise equal to the owner's);
* through **production**: `requestPlaybackWindow` answers `Admitted` (not the old `SourceUnavailable`) and the deferred
  turn publishes the window with a **null** root identity, `origin == Committed`;
* an Active row the revision **did not pin** is not applied (the state is reached by mutating the pass set directly, which
  production cannot reach — see §5).

`test_3_7a_i7_the_paged_lane_refuses_pinned_note_edits_it_cannot_resolve` — the over-capacity shape (2,100 events, 17
slices, admission refusal) with a pinned row: the lane answers `PinnedEditRowsUnresolved`, **no ring is established**
(`pagedActiveForInterval` false), the demand stays owed with that reason, and the retry gate is closed.

### Red-proofs (all reverted; the firmware file restored **byte-identically**, `sha256 48c57b2fe9d7c78e…`)

| ID | Mutation | Measured |
|---|---|---|
| **RP-I7-flat** | the rows applied to the merged window instead of per layer | **RED** — *"Expected 264 Was 266. the retained answer is exactly the owner's per-layer content"* (the mis-paired delete leaves 2 wrong events behind) |
| **RP-I7-merge** | the resolved layer appended instead of merged by tick | **RED** — *"Expected 150 Was 200"* (the overdub layer's surviving pair must interleave, not follow) |
| **RP-I7-pins** | the rows taken from the live Active set instead of the pins | **RED** — *"Expected 1 Was 0. an unpinned Active row is not applied"* |
| **RP-I7-guard** | the pager's pinned-edit-row guard removed | **RED** — *"no ring is established for a revision the pager cannot resolve"* |

The first attempt at **RP-I7-merge** was a **non-red-proof** and is recorded rather than reworded: the fixture's overdub
layer had been *entirely deleted* by the row, so appending it was indistinguishable from merging it. The fixture was
strengthened with a surviving pair in that layer (ticks 150/160, interleaving the record layer's 200/800), which is what
makes the merge rule observable — and the mutation then failed it.

## 4. The ITCM finding, and how it was paid

The first working form of the per-layer path cost **+320 B of ITCM** and failed the RAM1 guard (`code 392,568 + padding
648 < 840`). `nm` attributed it exactly: `std::vector<EditPass, …>::push_back(EditPass const&)` emitted **out of line**
(204 B), `~vector()` (82 B) and two `InternalHeapFirstAllocator<…>::allocate` partials (32 B each) — i.e. the *container's*
machinery, placed in `.text` because the gather is a large function and GCC declined to inline it there. Moving the row
collection into a **small dedicated flash helper**, and the rows themselves out of the gather's frame into the per-layer
helper, returned the line to **byte-identical with the baseline**. Recorded because it is the same finding I1 made about
`std::sort` (+1,280 B) one level down: in this tree, what costs ITCM is not the loop but the standard machinery an
uninlined call site drags in.

Symbol-level detail, for a reader who compares `nm` output: two 32 B `.part.0` allocator fragments and a 16 B growth of
`LoopPlaybackRuntime::LoopPlaybackRuntime()` (plus 4 B in an unrelated function) appear in `.text` while the section total is
unchanged — the fragments overlap existing code and the **guard line** is the authority, not the symbol list.

## 5. Named, not claimed

* **The pin filter is redundant in production, and load-bearing in the fixture.** The gather only serves when the retained
  revision is the loop's current one (`playbackWindowSourceIsCurrentForSlot`), and an edit advances `playbackRevision` —
  so a row added after retention cannot reach it. The fixture reaches that state by mutating `passes.editPasses` directly
  (production cannot), which is what makes **RP-I7-pins** red. The state is therefore a *safety* property, not a
  reachable one; it is kept so a window can never serve a later revision's edit under an earlier revision's identity.
* **A revision with more layers than the callers' ceiling cannot be gathered for a window.** One layer per call is the
  granularity, and both callers cap the calls (`kRetainedGatherMaxSlices` = 18): a revision with more layers than that
  refuses on the request path and is routed to the pager, which refuses it by name (§2.5). Named residual.
* **Multi-overdub layer order is pass-set order here, and `mergeSequence` order in the owner's own list collector.** With
  one overdub (this fixture) they agree; whether they can differ for several overdubs is not settled by this slice.
* **The pager resolves nothing, deliberately.** Refusing is the I3c-b/3.12f trade (wrong content is worse than no
  content); per-layer page production is **not** implemented and is not claimed.
* **Same-tick order is still 4.3's gate.** The per-layer sort is stable and the owner's per-layer sort is not, so content
  with duplicate ticks inside one layer can order differently; the fixture's layers hold distinct ticks, and its
  element-wise comparison is on tick/kind.
* **The per-layer scratch's allocation is not admission-gated.** It is a caller-owned `SessionMidiEventVec` taken
  off-clock; point-of-use admission for this family is DEC-115's follow-up list, not this slice.
* **No device leg, no timing claim, no `ExecutionBounds.md` row change, no fallback removal (3.7b).** The paged lane's
  refusal keeps `kMaxOpenNotes`, the closure criterion and the paging ring exactly as 3.12f left them.

**Documentation finding (recorded, not silently corrected):** `docs/Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md`
states that *"Playback `mergedMidiEvents` (`copyEffectiveCommittedEventsInRange`) concats active chunks, tick-sorts, then
applies `applyNoteEditPassSequence` on the combined list"*. The shipped `copyEffectiveCommittedEventsInRange` applies the
rows **per committed list** and then merges; the **published-snapshot** window gather is the path that applies its
unpublished-order companions to the combined overlay. This slice touched exactly that rule, so the guide now carries the
per-layer statement and the distinction; the older sentence is corrected in place rather than left standing.
