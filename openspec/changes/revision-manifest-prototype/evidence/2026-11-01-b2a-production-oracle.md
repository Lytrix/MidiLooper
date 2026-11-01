# B.2a — the production oracle for "the ordered effective stream of revision N"

Date: 2026-11-01. Host-only; no firmware change. Suite: `test/test_revision_store_differential`.

## Decision

**Oracle: the published `LoopSnapshot` edit view** — `Loop::tryCopyCurrentSnapshotEditViewEvents`, reached through
the production path `beginCapture` → `appendCaptureEvent` → `commitCapturePass` → `requestCommittedPassesRebuild` →
`stepPendingSnapshotPublish` until current. It is the content authority named by NAMING (`LoopSnapshot`, *published
root*) and the one `copyEffectiveCommittedEvents` returns. The compatibility gather and the playback order are
derived surfaces and are not the oracle.

**Store input:** the store is fed each committed pass's chunk rows (`CommittedEventRange::full` over that pass's
`committedChunkIds`), not the raw input. Capture finalisation (min length, pairing, wrap) stays with its production
owner; the comparison is about storage and composition.

**Parity key:** sequences of (tick, equal-tick class) groups, each group compared as a multiset of
(status, data1, data2). The harness also checks that each producer's own group sequence is ordered by
(tick, class).

## Findings

1. **Production has no single equal-tick order.** Four surfaces, four keys:

   | Surface | Equal-tick key | Stable? |
   |---|---|---|
   | Snapshot edit view (`LoopContentResolution::resolveWindow` → `resolvedEventLess`) | raw type byte, channel, data1, noteId | `std::sort` — no |
   | `LoopEventStore` private canonical cursor | NoteOff < NoteOn < other | yes |
   | Compatibility gather (`copyEffectiveCommittedEventsFromPasses`) | tick only | `std::sort` — no |
   | Playback window build (`TrackPlaybackWindowBuild`) | phase, tick, then insertion Off < On < other | residual from `std::sort` — no |

   The only key all four agree on is (tick, class) — with one exception: the edit view sorts a velocity-0 NoteOn by its
   type byte (as an On), while the class key treats it as an Off. Not hit by the current cases.

2. **Rows that are identical up to data2 have no defined order in the oracle.** Sustain `CC64 127` then `CC64 0` on
   one tick are equal under `resolvedEventLess`, so their order is whatever `std::sort` leaves. In the probe they kept
   append order (small groups go through insertion sort), but that is not guaranteed for larger groups. That is an
   audible ambiguity in production; the store's (run, seq) tie-break is deterministic. Owner question: should
   append order become the defined within-class order?

3. **Named divergence — span-less notes.** A zero-length note (NoteOn + velocity-0 NoteOn on one tick) is committed to
   the chunk rows, but the edit view keeps note events only when their noteId has a display span
   (`copyEditViewEventsFromSnapshot` → `snapshotSpanHasNoteId`), so production yields 2 rows where the store yields 4.
   "Effective content" therefore includes a span rule above storage. Pinned as an ignored test; which layer owns
   the rule is a B.3 decision (seal-time normalisation in the store vs. the same span filter on composition).

4. **Same-pitch overlap across passes is not resolved at commit on this path.** Record 60 @96–480 plus an overdub
   60 @288–384 keeps both notes in production as in the store. Overlap-resolve happens through the live overdub
   session's pending note changes (`PendingNoteChangeKind::Hide` / `Shorten`), which map directly onto the store's
   hides plus replacement rows — the B.3 input.

## Cases

| Case | Result |
|---|---|
| Record + two overdubs, same-tick class collisions across passes | parity |
| Wrap-crossing Record note | parity |
| Same-tick sustain 127/0 + same-pitch cross-pass overlap | parity |
| Zero-length note | **divergence** (production 2 rows, store 4) — ignored, pinned |

## Next (B.2b)

Seeded random operation sequences on this harness: undo/redo (`undoOverdubSession` / `redoOverdubSession` against
`Store::undo` / `redo`), commit after undo, longer takes, live overdub sessions that produce pending note changes, and
shrinking to the first divergent revision.
