# Overdub overlap-resolve note evaluations

**Status:** Durable evaluation catalog. **Not architecture authority.** Update this file when overlap-resolve owners change.  
**Date:** 2026-08-19  
**Companion:** [`OVERDUB_LEDGER_NOTE_EVALUATIONS.md`](OVERDUB_LEDGER_NOTE_EVALUATIONS.md)  
**Active investigation:** [`../Plans/overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md`](../Plans/overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md)

This catalog is every **note evaluation** in overlap resolve: candidate selection, interaction classification, exclusive-end / Hide geometry, and where that result survives. It is **not** open ledger identities and **not** source-view covering identities.

Hard boundary:

```
resolver owns exclusive-end geometry
ledger owns open identities
source-view covering identities answer present-at-hold
```

Do not converge those three because a mismatch between open ledger identities and source-view covering identities looks suspicious. **Gate 5A shipped:** `findNoteOffForOnIndex` equal-tick Off-before-On (`midiEventChronologicalLess`); not a full tick sort. HITL [`161349`](../../captures/session_20260819_161349.log) extra covering `a>n` **0**. **Gate 5B** (Length `6073` 551 provenance) has effect proven and source not proven — **no implementation**. Rebuild cannot manufacture Length-167. Do not “fix” this RC by changing the resolver, `OverlapCandidateLookup`, or ledger ownership.

**Update this catalog in the same change** if any of these moved: `resolveConstrainedGeometry`, `classifyEditSessionInteraction`, `computeShortenedEndTick`, `accumulatePendingNoteChangesForIncomingNote`, `sealPendingNoteChangesToEditPasses`, `applyChangeLengthById` shorten/lengthen, `rebuildOverdubSourceView`, `collectOverdubSourceHoldParticipantIds`, pending overlay vs cache.

---

## Four decisions (keep separate)

| Decision | Question | Owner |
|---|---|---|
| Candidate selection | Should A even be considered? | `OverlapCandidateLookup` + `existingNoteOverlapsIncomingHold` |
| Interaction classification | What relationship does A have to B? | `classifyEditSessionInteraction` |
| Geometry resolution | What should A become? | `resolveConstrainedGeometry` |
| Persistence / handoff | Where does that result survive? | Pending → (display overlay) → sealed EditPass. `rebuildOverdubSourceView` can drop it from the **cache** without deleting the EditPass |

Wrong final geometry is not a license to change candidate lookup.

---

## Lifecycle (elevate: resolver runs at B's Off)

```
B On
  → capture store only (already appended)
  → no A geometry mutation
  → no Pending Shorten/Hide
  → not a ledger Entry, not a closed source-view span

B remains open across overdub wrap
  → extractOpenCaptureNoteOns carries the On into the next capture pass
  → still no resolver run, still no A mutation
  → live capture continuation is not B's resolved source-view span
  → missing Off means incoming geometry is not yet closed
  → do not shorten A because B already has a start tick

B Off  (live, or synthesized stop Off)
  → B is a closed incoming linear span
  → resolve A against B
  → Pending Shorten/Hide (A) + Pending Add (B, overlay only)
  → pending geometry exists

seal (wrap / stop)
  → Shorten/Hide → companion EditPass (Length / Delete)
  → Add is not sealed — B's durability is the capture pass already committed
  → clearPendingNoteChanges

rebuildOverdubSourceView
  → clears overdubSourceViewNotes_
  → fills from reconstructDisplayNotes (pin: from=win, prep=0)
  → source-view cache can stop reflecting resolved geometry even when
    pending/sealed Length still exists elsewhere
```

**B's live capture continuation is not the same object as B's eventual resolved source-view span.** A start at 168 without an Off does not authorize shortening A.

---

## Geometry stores (not one “geometry”)

```
raw capture On/Off ticks
    ↓  B Off: prior.tick → newEvt.tick (already in capture.store)
incoming closed span
    ↓  wrap split if end < start
one or two linear consume intervals
    ↓
resolver causing span (linear only)
    ↓
ConstrainedNoteGeometry
    ↓
PendingNoteChange     ← session overlay; Add ≠ persistence
    ↓
display paint overlay (applyPendingNoteChangesToDisplayNotes)
    ↓
sealed EditPass Length/Delete   (Shorten/Hide only)
    ↓
persistent note event geometry (Off tick via shortenNoteEndById)
```

`overdubSourceViewNotes_` is a cache. Source-view covering identities are read from it **without** applying pending. Firmware wrap/stop does **not** call `applyPendingNoteChangesToOverdubSourceView` (that function is used from native tests; wrap path seals then rebuilds). Display paint overlays pending Hide/Shorten onto a paint vector only.

---

## Resolver input: linear intervals only

`resolveConstrainedGeometry` never receives a circular `[start, end)` with `end < start`.

| Incoming | What the resolver sees |
|---|---|
| `endTick > startTick` | One call: causing span `[startTick, endTick)` |
| `endTick < startTick` (wrap-crossing) | Two calls, same causing `noteId`, chronological order: `[startTick, loopLen)` then `[0, endTick)`. Upsert last-write is the second call |
| `endTick == startTick` | Rejected; no pending change |

Do not “simplify” wrap by passing a wrapped pair into `resolveConstrainedGeometry`. That would change its semantics.

---

## Exclusive end vs storage Off tick

`computeShortenedEndTick` is a first-class evaluation, not a footnote. **Do not call the stored Off tick an exclusive end.**

```
Resolver geometry:
    [start, exclusiveEnd)

Storage:
    NoteOn@start
    NoteOff@(exclusiveEnd - 1)
    computeShortenedEndTick = causingStart - 1
    (if causingStart == 0: loopLength - 1)

Therefore:
    B On@168
    → A exclusiveEnd = 168
    → A storage Off = 167
```

Pending Shorten, sealed Length, overlay `DisplayNote.endTick`, and gathered Off all store **167**, not 168. Native pin (`test_pending_shorten_long_source_on_overlap` / `test_pending_shorten_applies_to_display_notes`): incoming B `[120, 160)` → pending `endTick = 119` → overlay paint 119 → sealed Length 119 → gathered Off@119. `shortenNoteEndById` writes that Off tick; it does not invent a second exclusive-end rule on the shorten path.

`displayNotePresentAtHold` is inclusive start, exclusive end (`start <= s < DisplayNote.endTick`). Reconstruct and overlay put the **Off tick** in `DisplayNote.endTick` (seed Off@200 → cache `endTick == 200`; overlay of Shorten 119 → paint `endTick == 119`). Occupy `as=`/`ae=` dump those cache fields.

**Named conversion for this pin** (`applyChangeLengthById` shorten → `shortenNoteEndById` writes `NoteOff.tick = 167` → `exclusiveEndForLoopOff(167) = 168` → `displayInclusiveEndTick(168) = 167`):

| Representation | Value | Occupies hold 167? |
|---|---|---|
| Resolver exclusive | `[144, 168)` | yes (inside reconstruct only) |
| Length / Off / `DisplayNote.endTick` / `ae=` | **167** | occupy covering `s < 167` → **no** |
| L2324 cache `ae=168` | Off@168 pairing | occupy covering `s < 168` |

Do not call the Length-167 DisplayNote `[144,168)`. Tick 167 is the disagreement. Do not collapse cache `ae=168` with storage Off 167 — that mix is Gate 1.

**Do not “correct” the `-1` because interval language is `[start, exclusiveEnd)`.** The `-1` converts exclusiveEnd to the Off event tick.

---

## Inclusive classifier vs half-open covering (intentional)

Two different overlap tests. They are **sequential gates on overdub**, not an either/or.

| Layer | Test | Abut `existingEnd == incomingStart` / `targetEnd == causingStart` |
|---|---|---|
| Candidate consume | `existingEnd > incomingStart` (half-open) | **Rejected** |
| Classification | `causingStart <= targetEnd && targetStart <= causingEnd` (inclusive both edges) | `OverlapNoteOff` |

Classifier comments (`linearSpansOverlapForAnalysis`, `classifyEditSessionInteraction`): start-abut must stay `OverlapNoteOff` and Shorten to `causingStart - 1`. `BoundaryTouch` + full Restore jumped +2 ticks on a 1-tick leave ([`225119`](../../captures/session_20260804_225119.log)). Packed end\|start Hide is `OverlapNoteOn` when `causingEnd == targetStart` ([`223208`](../../captures/session_20260804_223208.log)).

**How A can appear in `selected` and still miss classification:** `OverlapCandidateLookup::appendNotesForIds` copies occupy/source-view rows by `noteId` with **no** consume filter. Pairing in `accumulatePendingNoteChangesFromSourceNotes` then applies `existingNoteOverlapsIncomingHold` again. Start-abut is rejected there, so A does **not** reach `classifyEditSessionInteraction` on the overdub consume path. Consume overlap gates pairing, not merely window discovery.

The classifier’s start-abut `OverlapNoteOff` case is reached from NOTE_EDIT candidate intake (`determineConstrainedGeometryTargetNoteIds`), which does not use that consume gate. Do not read the two tables as “overdub start-abut still shortens.” That case is unreachable once consume has rejected A.

Do not change the classifier to half-open as a “cleanup.” That is an off-by-one regression site for NOTE_EDIT.

---

## Pending overwrite (`upsertSourceTransform`)

Only Shorten/Hide upsert. Last write for that `noteId` **replaces** the previous Shorten/Hide entirely (`existing = change`). Add always `push_back` (never upserts).

| Sequence | Result |
|---|---|
| Shorten end 168, then Shorten end 120 | **120 wins** |
| Shorten, then Hide | **Hide wins** |
| Hide, then Shorten | **Shorten wins** (last write; Hide does not stick) |
| Shorten, then a later resolve that does not emit Shorten/Hide (BoundaryTouch / unchanged) | **No upsert** — previous transform remains; baseline is not resurrected |

Within **one** `resolveConstrainedGeometry` call: `CompleteCover` / `OverlapNoteOn` Hide beats Shorten; among `OverlapNoteOff`s, **shortest** `computeShortenedEndTick` wins.

Across wrap-split, `accumulatePendingNoteChangesForIncomingNote` makes the two resolver calls in **deterministic chronological segment order**: `[startTick, loopLen)` first, then `[0, endTick)` (`endTick > 0`). Upsert last-write for a wrap-crossing B is therefore the **second** segment’s Shorten/Hide for that `noteId`. The first segment does not remain the pending transform if the second emits one.

Across later notes constraining the same A, upsert last-write is the pending invariant: **canonical latest constrained result**. Repeated observations must not resurrect baseline (they don't upsert when unconstrained). They can replace a prior constraint with a different constraint.

---

## Add is not pending persistence

`PendingNoteChangeKind::Add` is always recorded for the now-closed B. **`sealPendingNoteChangesToEditPasses` ignores Add.**

| What | Where B lives |
|---|---|
| Durable B | Capture store — On and Off are already appended **before** `accumulatePendingNoteChangesForIncomingNote` (`recordMidiEvents` then resolve). Wrap/stop `commitCapturePass` persists that pass |
| Pending Add | Live overlay only: `applyPendingNoteChangesToOverdubSourceView` merges Add into the source-view cache (native tests). Display paint does not add B from this path unless that merge ran |
| Companion EditPass | A’s Shorten/Hide only |

**“Pending change” does not mean “pending persistence” for Add.** B may already exist in capture while A’s resolver geometry exists only in pending / companion EditPass. That is the investigation split: B does not cause A’s resolver geometry until B’s Off, but B’s events can already be in another representation.

---

## `rebuildOverdubSourceView` is a destructive **cache** producer

```
resolver geometry
      ↓
PendingNoteChange (+ display overlay; source-view cache often still pre-resolution)
      ↓
seal → companion EditPass   ← durable Shorten/Hide (may still exist)
      ↓
rebuildOverdubSourceView
      ↓  clear()
canonical reconstructed notes (reconstructDisplayNotes)
      ↓
source-view cache no longer shows resolved geometry unless gather replayed it
```

`rebuildOverdubSourceView` can erase the resolved geometry from the **source-view representation** even when that geometry still exists in pending or in the companion EditPass. This RC is cache/handoff loss, not “the EditPass was never committed.”

When `tryResolvePreparedWindow` misses (`from=win`, pin `prep=0`), gather consume is:

1. `gatherActiveResolvedEvents` — record layer + each active overdub layer
2. per layer: `applyActiveEdits` → `applyNoteEditPassSequence` (Length/Delete on **that layer only**)
3. `mergeSortedMidiVectors` — **tick-only**; equal tick keeps the earlier pass first. This is not `resolvedEventLess`
4. `reconstructDisplayNotes` (`overdubPassWrapPairing` false) — walks **vector order**; does not call `sortMidiEventsChronologically`
5. `appendOverdubPassWrapPairedNotes` (per-pass reconstruct with wrap pairing; geometry match, not `noteId`)

`uniqueIdentifiedResolvedEvents` is TickIndex / `from=prep` only. Untagged Offs are never unique-dropped on `from=win`.

Playback `mergedMidiEvents` (`copyEffectiveCommittedEventsInRange`) concats active chunks, tick-sorts, then applies `applyNoteEditPassSequence` on the **combined** list. Source-view gather applies edits per layer, then merges. Same Active rows; different LIFO pairing if Ons and Offs live in different passes.

Pin [`121141`](../../captures/session_20260819_121141.log) L2324 `6079 144–168` is reconstruct pairing to committed Off@168, not Length Off@167 (absent from pitch-24 `mmevt`). Do not treat occupy `ae=168` as sealed Length payload.

Gate 2: wrap L2772 gather `applyActiveEdits` makes Off@168 disappear. Reconstruct then pairs 6079 to Off@360 → cache `144–360`. `appendOverdubPassWrapPairedNotes` cannot replace `144–168` with `144–360`. Q16 min-length is skipped on `CommitReason::OverdubWrap`. Gate 4: Off@168 is **moved** by Length of `6073` (not deleted; a second Off@551 is occupy dump-capped).

Gate 3: B's committed pair is On@168 `6073` + Off@264 (resolver incoming `[168, 264)`). Clock at `hs=312` has applied Off@264 and not Off@360 (`didPlaybackEventCross` `(prev, tick]`). Wrap-pair `168–360` is absent at L2827 and first dumped at L3298; it is not B's end. Committed B does not cover hold 312. Intended occupy with Length-167 A (`ae=167`, covering `s < 167`) and no wrap-pair is `n=1 a=0`. `a=0` does not mean the ledger is wrong. “312 = B only” is withdrawn.

Gate 4 pinned: on L2324 tick-sorted order, **before 5A**, `findNoteOffForOnIndex` paired Off@168 to `6073`. Only a Length row targeting `6073` removed Off@168 while keeping both Ons. **Gate 5A shipped:** equal-tick Off-before-On; On-then-Off Length `6079` 167 yields A `144–167` and B `168–264`. Length `6073` 551 leaves Off@168 and moves Off@264. **Gate 5B:** that Length still reconstructs B `168–360` (covers 312); provenance not proven — do not skip Length by `targetNoteId`. Device EditPass index is not dumped.

---

## JIT merge is not a second geometry authority

`ensureOverdubSourceNotesForHold` (loops longer than the source window): reconstructs a window, keeps same-pitch notes, identity-filters, **merges by `noteId`**. `mergeDisplayNotesIntoOverdubSourceView` skips if that `noteId` already exists — it does **not** replace geometry.

JIT may supply **missing candidates**. The `PendingNoteChange` is still `resolveConstrainedGeometry` output. Duplicate candidate discovery (`unionSelectedNote` also skips existing `noteId`) must not become a second exclusive-end owner. The 768-tick pin never takes JIT (`why=hold` absent).

---

## Evaluations, in order

### 1. May the resolver run at all?

| Evaluation | Owner | True / accepted | False / rejected |
|---|---|---|---|
| Event is a NoteOff | `Track::appendCaptureEvent` overdub branch; `Track::finalizePendingNotes` | Live Off, or synthesized stop Off via `appendCaptureNoteOffAtPhase` | NoteOn — capture only |
| Source view established | `Loop::accumulatePendingNoteChangesForIncomingNote` | `overdubSourceViewEstablished_` and loop length > 0 | Return false |
| Incoming span non-empty | same | `endTick != startTick`; wrap-crossing when `endTick < startTick` | Return false |
| Wrap while B still open | `Track::commitOverdubWrapAtSessionStart` | Does **not** call `finalizePendingNotes` | Open B stays live capture; **no** resolver run |

### 2. Candidate selection (not geometry)

| Evaluation | Owner | Accepts | Rejects |
|---|---|---|---|
| Occupy ids present? | `OverlapCandidateLookup::shouldLookupSpans` | Non-empty occupy set | Empty — skip id lookup |
| Id → span | `appendNotesForIds` | Source-view row with that `noteId` | Missing id |
| Window intersect | `DisplayWindowUtils::noteIntersectsWindow` | Intersects source window | Outside window |
| Consume overlap | `existingNoteOverlapsIncomingHold` | Half-open linearized overlap (`existingEnd > incomingStart`) | Abut; `start == end`. Applied again at pairing, so occupy-id rows that only abut never reach the classifier |
| Same pitch, not causing | `accumulatePendingNoteChangesFromSourceNotes` | `note == pitch`, valid id ≠ causing | Other pitch / self |
| Long-loop JIT | `ensureOverdubSourceNotesForHold` | Missing same-pitch window notes (see JIT authority) | 768-tick pin |

`OverlapNoteIdObservation::collectObservedOverlapNoteIds` / `collectGeometryOverlapNoteIds` are diagnostic Gate 1 helpers, not production selection.

### 3. Classification then resolution

Inputs are **linear** spans. Overdub uses source-view ticks; NOTE_EDIT linearizes via `projectNoteBaselineForEditAnalysis`.

| Type | Geometry | Resolver consequence |
|---|---|---|
| `CompleteCover` | `targetStart >= causingStart && targetEnd <= causingEnd` | Hide |
| `OverlapNoteOn` | Inclusive overlap and `targetStart >= causingStart` (not fully contained) | Hide |
| `OverlapNoteOff` | Target starts before causing and `targetEnd >= causingStart` (includes start-abut) | Shorten to `computeShortenedEndTick` |
| `BoundaryTouch` | No inclusive overlap; edges share a tick | Leave baseline (no upsert) |

`resolveConstrainedGeometry` maps `CompleteCover` **or** `OverlapNoteOn` to Hide. Inverted / empty (`endTick <= startTick`) Hides. Optional min-length remove Hides when `(end - start) < noteMinLengthTicks`.

### 4. What is stored after resolve?

| Kind | When | Payload | Persistence |
|---|---|---|---|
| Shorten | Visible and start/end differ from baseline | New start/end (end = Off tick) | Sealed Length EditPass |
| Hide | `visible == false` | Baseline start/end kept on the row | Sealed Delete EditPass |
| Add | Always, closed B | B's `[startTick, endTick)` | **Not sealed.** Capture pass already holds B. Overlay only |

### 5. What commits the storage Off tick into passes?

| Step | Owner | What it writes |
|---|---|---|
| Seal | `sealPendingNoteChangesToEditPasses` | Companion rows at `kOverdubCompanionEditPassIndex` (255). Then `clearPendingNoteChanges` |
| Replay | `applyNoteEditPass` Length | `applyChangeLengthById` |
| Length **shorten** (`newEnd < refEnd`) | `shortenNoteEndById` | Moves that note's Off tick. No sibling exclusive-end scan |
| Length **lengthen** (`newEnd > refEnd`) | `applyChangeLengthById` | Own `newStart - 1` shorten + contained deletes. Audit only if it can overwrite this path's committed exclusive end |

---

## Adjacent evaluations that are **not** overlap resolve

| Evaluation | Owner | Question |
|---|---|---|
| LIFO Off↔On pairing | `reconstructDisplayNotes` | Which Off closes which On |
| Wrap-held pair heuristic | `overdubPassWrapPairing` | Pairing fallback — source of duplicate B `168–360` at L3298. **Not** B's committed Off (Gate 3: Off@264) |
| Wrapped-pair predicate | `isWrappedLoopNotePair` | Tail On + head Off is one note (`offTick < onTick` strictly) |
| Present at hold (source-view covering identities) | `displayNotePresentAtHold` on **source-view cache** (no pending overlay) | Does this cache span cover hold |
| Identity exists | `committedPlaybackNoteOnIdentityValid` | NoteOn in full-loop merged stream |
| Open ledger identities | `collectOverdubNoteOnParticipantIds` | Open ledger Entries. See ledger catalog |

---

## NOTE_EDIT vs overdub

Same resolver, different intake.

| | Overdub | NOTE_EDIT |
|---|---|---|
| Trigger | Later note's Off | Session interaction analysis |
| Candidates | Source-view notes overlapping consume interval | `determineConstrainedGeometryTargetNoteIds` |
| Outcome | `PendingNoteChange` → companion EditPass (Shorten/Hide) | `ConstrainedNoteGeometry` → session / commit EditPass |

---

## Follow-up, not this RC

Same-tick On/Off at the **loop wrap**: one continuous note; Hide notes strictly inside (`CompleteCover`). `isWrappedLoopNotePair` rejects `offTick == onTick`. Do not widen that predicate first. Do not touch `rebuildPlaybackOrder` equal-phase ordering.
