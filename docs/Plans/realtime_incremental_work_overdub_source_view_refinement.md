# Overdub source-view reuse (80 ms entry floor)

**Status:** Option B withdrawn from production. RC-K3 restored. Native source-view / pending-note tests expect a filled view at `beginCapture`. Device re-measure vs [`225803`](../../captures/session_20260812_225803.log) / [`021304`](../../captures/session_20260813_021304.log) open.  
**Date:** 2026-08-13  
**Parent:** [`realtime_incremental_work_overdub_note_change_bugfix.md`](realtime_incremental_work_overdub_note_change_bugfix.md) (RC-K1–K3 / RC-L1 verified)  
**Handoff:** Production overlap uses RC-K3 `overdubSourceViewNotes_` filled once at `establishOverdubSourceView`. Option B pitch-query on note-off is disconnected from that path. Long-term candidate collection: [`overdub_playback_observation_overlap_refinement.md`](overdub_playback_observation_overlap_refinement.md). `OverlapNoteIdObservation` is test/diagnostic; production selection is normalized geometry + `[S, E)`.  
**Scheduling:** [`runtime_scheduling_admission_model_architecture.md`](runtime_scheduling_admission_model_architecture.md) §3.1, §6.4, §10 · roadmap R1A / G1 in [`runtime_scheduling_owner_boundary_admission_refinement.md`](runtime_scheduling_owner_boundary_admission_refinement.md)  
**Evidence:** RC-K3 known-good [`session_20260812_225803.log`](../../captures/session_20260812_225803.log); Option B stall [`session_20260813_021304.log`](../../captures/session_20260813_021304.log)

This is a **targeted bounded-work** follow-through, not interval reservation.

---

## What the 80 ms is

`Track::startOverdubbing` → `Loop::beginCapture(Overdub)` → `Loop::establishOverdubSourceView`:

1. `gatherCommittedEvents(overdubSourceViewEvents_)` — flatten every committed chunk, then `sortMidiEventsByTick`
2. `NoteUtils::reconstructDisplayNotes` — full-loop spans + projection into `overdubSourceViewNotes_`

That runs inside the overdub **button** dispatch (`SC_ODUB_STAGE begin_capture`). There is no `handleMidiInput()` entry inside it. [`225803`](../../captures/session_20260812_225803.log): 76.7 / 80.2 / 78.6 / 82.9 ms across four overdubs on a ~64-bar loop (`loop_events` 2639 → 2949). First USB note 108–242 ms after PLAYING→OVERDUBBING.

RC-K3 moved this work off the note-off path (where it was 177 ms **per note-off**). RC-L1 removed the per-span PSRAM allocation inside reconstruct. The remaining cost is one full flatten+reconstruct per overdub **entry**.

`accumulatePendingNoteChangesForIncomingNote` already only needs same-pitch notes that intersect the incoming span (`noteIntersectsWindow`). It does not need a second full copy of the loop. Production has no callers of `gatherOverdubSourceViewEventsInWindow` / `gatherOverdubSourceViewNotesInWindow` outside tests.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | Depends on option. Extending `Loop::accumulatePendingNoteChangesForIncomingNote` / `establishOverdubSourceView` is NO. Making `visualCache` the overlap authority is YES (display cache vs capture overlap). |
| **State transition change?** | Depends on option. Skipping the full flatten while still setting `overdubSourceViewEstablished_` at `beginCapture` is NO if overlap results match. Slicing or deferring until after the first note-off is YES — that path currently `return false` and skips Shorten/Hide. Precomputing during PLAYING changes when the flag becomes true. |

If either answer is YES without an explicit choice below → stop, do not patch.

This is the contract §10 overdub-delta invariant (overdub mutation must not rebuild the pre-existing loop) applied to the **entry** edge that RC-K3 created. It is also the measured proof of contract §6.4: PLAYING→OVERDUBBING is not constant-cost.

---

## What is already in memory at the button

`startOverdubbing` order today:

```text
setState(OVERDUBBING)
markDisplayCachesStale()          // dirtyBars = all 1; does NOT clear visualCache.notes
beginCapture → establishOverdubSourceView()   // full flatten + reconstruct
```

| Structure | Owner | At PLAYING→OVERDUBBING | Usable for overlap? |
|-----------|-------|------------------------|---------------------|
| Committed chunks (`passes` / `LoopEventStore`) | `Loop` | Live source of truth | Yes — `CommittedEventRange::inWindow` already skips non-intersecting chunks |
| `visualCache.notes` | `Loop` (display) | Still the PLAYING list after `markDisplayCachesStale`; idle slices mutate it only after this call returns | Yes **if** that list equals a full reconstruct (unproven for slice-built caches) |
| `passesMaterializedStore_` | `Loop` | Fresh only with edit passes / after materialize | Same flatten as gather when fresh |
| Playback merged events | `Track` playback runtime | Windowed playback, not overlap geometry | No |
| `overdubSourceViewNotes_` | `Loop` | Empty until establish finishes | Built by the 80 ms |

Preferred direction: **query the committed chunks or the already-built note list**. Do not flatten the whole loop on the button path.

---

## Why scheduling leftovers do not cut this 80 ms

| Open item | Relation |
|-----------|----------|
| **Interval reservation** | Wrong tool. Investigation §31a: no reservation over background work can bound the MIDI Input Gap while a MIDI-dispatched control action is the cost. This 80 ms **is** that control action (`startOverdubbing` from the button). |
| **S3 service density** | Extra MIDI drains would not run *during* `beginCapture`. |
| **RC-J** | Same *class* (transport edge does loop-sized work) on STOP, different owner (`timingCriticalTrackActive` false → deferred save). Do not bundle. |
| **RC-S0c** | Tier-A serial transmit. Helps *see* RECORD windows; does not shorten `begin_capture`. |
| **§31d display bailout** | Independent 33 ms resolve vs 5 ms budget. Sharing `visualCache` (Option A) is the only overlap with this 80 ms. |
| **Boot `stale_all` 748 ms** | Visual-cache invalidation storm after backfill. Same `markDisplayCachesStale` family, not overdub entry. |

Keep those on the scheduling queue. This plan only removes the entry flatten.

---

## Options

### Option A — adopt `visualCache.notes` at establish (copy, do not alias)

At `establishOverdubSourceView`, if `visualCache.notes` is non-empty, **copy** it into `overdubSourceViewNotes_` and skip gather+reconstruct. `markDisplayCachesStale` does not clear notes, and idle slices have not run yet, so the PLAYING list is still sitting there. Copy is required: `rebuildVisualCacheIdleSlice` mutates `visualCache.notes` during the pass.

Skip filling `overdubSourceViewEvents_` unless a production caller appears (today: tests only).

| | |
|--|--|
| Reuses | Display note list already built by idle slices / full rebuild |
| Ownership | Borderline. Overlap would trust a display-derived list. Loop still owns both members. Treat as YES if we declare `visualCache` the overlap authority; NO if it is a one-time copy with a fallback reconstruct when notes are empty. |
| Transitions | NO — `overdubSourceViewEstablished_` still flips at `beginCapture` before the first note-off |
| Gate | Native fixture: after `slice_clean`, `visualCache.notes` vs `reconstructDisplayNotes(gatherCommittedEvents())` including wrap head/tail. If they disagree, A is unsafe. |
| Fallback | Empty / dirty-with-empty-notes → today's full reconstruct (boot, first play, post-commit dirty window) |

`begin_capture` becomes a vector copy when PLAYING had a complete cache (the [`225803`](../../captures/session_20260812_225803.log) case: `slice_clean` 1430 notes at 6.6 s, then overdub at 280 s). First overdub after a commit while the cache is still dirty still pays the 80 ms.

### Option B — per note-off windowed chunk query (recommended)

Stop flattening at entry. `beginCapture` only records `overdubSourceViewLoopLengthTicks_` and sets `overdubSourceViewEstablished_ = true`.

On each note-off, `accumulatePendingNoteChangesForIncomingNote` already has the overlap window (`startTick`, `windowLength`). Use `gatherCommittedEventsInWindow` → `CommittedEventRange::inWindow` (wrap-aware chunk skip) → reconstruct **that window** → pair. That is the existing windowed pass machinery, used as a query, not as a second full loop copy.

| | |
|--|--|
| Reuses | Committed chunks already in PSRAM; `CommittedEventRange::inWindow`; `gatherCommittedEventsInWindow` |
| Ownership | NO — extend `accumulatePendingNoteChangesForIncomingNote`. `Loop` stays the overlap owner. Chunks stay the source of truth. |
| Transitions | NO if overlap results match today's full-list filter. YES if the first note-off can miss a wrap candidate. |
| Matches §15 | Yes: cost proportional to the incoming span and intersecting chunks, not to loop length |
| Inverse of RC-K3 | RC-K3 cached a full reconstruct because *full* reconstruct per note-off was 177 ms. Windowed reconstruct of one note-span is a different cost model. [`225803`](../../captures/session_20260812_225803.log) `notechg` is already 1.09 ms with a full in-memory list; B must stay under the same 5 ms ceiling. |
| Gate | Native fixture: windowed reconstruct+pair vs full `overdubSourceViewNotes_` filter for (1) interior note, (2) wrap tail→head, (3) wrap head-off at tick 0. If any case disagrees, expand the gather window to include `wrapTailStartTick`…loop end plus the head, or reject B. |
| Tests to extend | `test_pending_note_change` (wrap shorten/hide), `test_overdub_source_view` (establish no longer fills the full event vector — update assertions) |

`begin_capture` should fall to the `set_state` / `undo_session` scale (microseconds to low milliseconds). Note-off keeps `noterecon` as a real probe: it will be non-zero again, but must stay ≪ 5 ms.

### Option C — precompute during PLAYING idle

Fill `overdubSourceViewNotes_` from `rebuildVisualCacheIdleSlice` / a sibling slice so `beginCapture` is an adopt. Same work as the visual cache, done twice unless merged with A.

| | |
|--|--|
| Reuses | Idle-slice pattern |
| Ownership | NO if Loop fills its own member |
| Transitions | YES — `overdubSourceViewEstablished_` becomes true in PLAYING, must invalidate on undo, slot switch, edit commit, overdub stop. Design session required. |
| Verdict | Do not take this unless A and B both fail the wrap fixture. It duplicates visual-cache lifetime. |

### Option D — slice establish across `handleMidiInput()` entries at the button

Keep the full rebuild, cut it into quanta with `handleMidiInput` between slices.

| | |
|--|--|
| Reuses | `rebuildVisualCacheIdleSlice` quantum idea |
| Ownership | NO |
| Transitions | YES — until the last slice, `overdubSourceViewEstablished_` is false and the first note-offs skip overlap (`return false` today) |
| Verdict | Rejected unless the product accepts “first notes of a pass do not shorten/hide”. |

### Option E — observation split only

Add `ODUB,stage,gather` / `ODUB,stage,reconstruct` inside `establishOverdubSourceView`. No behavior change.

Useful if choosing between “gather is the 80 ms” vs “reconstruct is the 80 ms” before A/B. Not required to implement B: B removes both. Do E first only if the wrap fixture for B is going to take a session and you want the split on device in parallel.

---

## Recommended path

**Option B**, with the wrap-equivalence native fixture as a hard gate before firmware.

That matches “reuse chunked windowed passes already in memory” without making display cache the overlap authority, and without a PLAYING-lifetime flag.

If the wrap fixture fails even with wrap-region padding:

1. Stop.
2. Fall back to **Option A** (copy `visualCache.notes` when non-empty) plus the slice-vs-full fixture.
3. Do not invent a third cache (Option C) and do not slice the button (Option D).

Do not start interval reservation, RC-J, or RC-S0c as part of this work.

---

## Open before coding

1. **Wrap equivalence (B):** does `gatherCommittedEventsInWindow` + reconstruct on the incoming span (plus wrap padding if needed) produce the same Shorten/Hide set as scanning `overdubSourceViewNotes_`? Native fixture decides; do not guess.
2. **Empty-cache overdub (A, if B fails):** first overdub before any `slice_clean` still needs a path. Full reconstruct remains the fallback, not a new owner.
3. **`overdubSourceViewEvents_`:** production unused. B may leave it empty; update `test_overdub_source_view` rather than keep filling it “for the tests”.
4. **Probe contract:** B makes `noterecon` non-zero on note-off again. Exit criterion is `notechg` still under 5 ms **and** `begin_capture` no longer tracks loop length, not `noterecon == 0`.

---

## Device gate (after a chosen option)

Same shape as RC-L1: grown-loop overdub, `teensy41-capture-serial`.

| Probe | [`225803`](../../captures/session_20260812_225803.log) | [`002329`](../../captures/session_20260813_002329.log) |
|-------|--------|--------|
| Loop | ~64 bars, `loop_events` 2639→2949 | 10 bars (7680 ticks), `loop_events` 1220, 6 chunks |
| `begin_capture` / overdub start | 77–83 ms | `PERF,overdub_start` **183 µs** (full `startOverdubbing`). `#CAP` `ODUB,stage,begin_capture` not in this log (`RING,overflow` at stop; `#CAP` gap ~14s–129s) |
| `notechg` | 1.09 ms | peak **1395 µs** (1.40 ms); other windows 241–243 µs or 0 |
| `noterecon` | 0 | peak **1374 µs**; tracks `notechg` (`notepair` 16–17 µs) |
| `clockrate` during OVERDUB | 47–48 | **47–48**. Stop window 39 (RC-J, not this path) |
| first `MI,U` after PLAYING→OVERDUBBING | 108–242 ms | Overdub started 36.951 s (`Overdubbing started @ tick 960`). First `#CAP` `MI,U` in the surviving tail is 128.951 s — not an entry-latency measurement |

---

## Phase 0 audit (`overdubSourceViewEstablished_`)

Reads/writes are only:

| Site | Role |
|------|------|
| `establishOverdubSourceView` | set true after full flatten+reconstruct |
| `clearOverdubSourceView` | set false |
| `beginCapture` | Overdub → establish; else clear |
| `discardCapture` / `commitPendingCapturePass` / `Loop` reset | clear |
| `accumulatePendingNoteChangesForIncomingNote` | `return false` if not established |
| `gatherOverdubSourceView*InWindow` | empty out if not established |
| `appendCaptureEventWithResult` | skip reverse-tick dedup when established |
| `Track::recordMidiEvents` | note-off overlap only when `hasOverdubSourceView()` |
| `Track::finalizePendingNotes` | skip overlap-restore drop when established |

No hidden consumer requires the event vector to be populated. Changing established to mean “lazy query is valid” is safe **if** the first note-off still produces the same Shorten/Hide set. That is the fixture contract. It does not currently hold.

---

## Phase 1–2 native fixture (blocked)

Tests live in `test_pending_note_change` (`test_windowed_overlap_matches_full_*`).

Reference: `beginCapture` full `overdubSourceViewNotes_` → `accumulatePendingNoteChangesFromSourceNotes`.

Candidate: committed chunks whose tick span intersects the incoming window (whole chunk, no event-level filter), plus wrap head/tail when the incoming span hits `wrapTailStartTick` / `TICKS_PER_BAR`, then reconstruct → same pairing helper.

### What passed

When the overlapping note’s on and off sit in a chunk whose `[firstTick, lastTick]` intersects the incoming window:

| Case | Test |
|------|------|
| Interior note entirely inside the loop | `test_windowed_overlap_matches_full_interior` |
| Incoming starting near loop end | `test_windowed_overlap_matches_full_incoming_near_loop_end` |
| Incoming ending after wrap (unwrapped `endTick > loopLen`) | `test_windowed_overlap_matches_full_incoming_ending_after_wrap` (+ vs head / vs tail / multiple spans) |
| Incoming beginning exactly at tick 0 | `test_windowed_overlap_matches_full_incoming_at_tick_zero` |
| Candidate entirely in the loop tail | `test_windowed_overlap_matches_full_candidate_in_tail` |
| Candidate entirely in the loop head | `test_windowed_overlap_matches_full_candidate_in_head` |
| Candidate spanning tail → head | `test_windowed_overlap_matches_full_wrap_tail_to_head` |
| Boundary-touch (incoming starts at source end; incoming ends at source start; incoming starts at wrap on) | `test_windowed_overlap_matches_full_boundary_*` |
| Multiple same-pitch candidates around the wrap | `test_windowed_overlap_matches_full_multiple_same_pitch_around_wrap` |
| Multiple projected/overlapping spans | `test_windowed_overlap_matches_full_multiple_overlapping_spans` |

### What failed (stop condition)

`test_windowed_overlap_matches_full_note_split_across_chunks`:

- Chunk 0 holds the pitch-60 note-on (span ends at 255).
- Chunk 1 holds the matching note-off at tick 400.
- Incoming window `[300, 350)` is inside the sounding interval and intersects **neither** chunk span.
- Reference: 1 Shorten. Candidate: 0.

`gatherCommittedEventsInWindow` is worse: it also drops events whose ticks lie outside the incoming span, so a long note with on at 50 and off at 200 is invisible to incoming `[120, 160)` even when both events share one chunk. That failed first; whole-chunk gather was the Phase 2 expansion; it is still not enough when the sounding interval spans a gap between chunks.

The expansion that would include this note is “any chunk that could contain a still-sounding note-on,” i.e. chunks with `firstTick < incomingEnd` or `lastTick > incomingStart`. Every chunk satisfies one of those two predicates, so that expansion is a full gather of **all pitches**.

---

## Phase 3 Option A native fixture (rejected)

Tests: `test_slice_cache_overlap_matches_full_*` in `test_pending_note_change`. Wrap and long spanning-note cases fail (1 Shorten vs 0). Interior / same-bar split-chunk pass. Idle slices miss wrap on/off and notes longer than the pad. Option A is unsafe on a slice-built cache.

---

## Phase 4 Option B — pitch-scoped open-note walk (shipped native)

Chunks are already in the PSRAM pool at note-off. `establishOverdubSourceView` only records loop length and sets `overdubSourceViewEstablished_`. It does not flatten or reconstruct.

`accumulatePendingNoteChangesForIncomingNote` calls `Loop::gatherCommittedNoteEventsForPitch`:

1. Walk loaded committed chunk lists in merge order.
2. Keep note-on/off events of the incoming pitch (`LoopEventStore::appendChunkRefNoteEventsForPitch`).
3. Sort by tick. Reconstruct pairs an unpaired on in chunk N with the off in chunk N+1 (or later), including tick gaps between chunk spans.
4. Pair Shorten/Hide against that pitch-only note list.

Edit-pass loops fall back to `gatherCommittedEvents` then pitch-filter (materialized geometry). Capture events are not included (committed chunks only).

Native: `test_pending_note_change` wrap matrix + `test_windowed_overlap_matches_full_note_split_across_chunks` PASS. `test_overdub_source_view` PASS.

**Follow-through (003009):** that edit-pass fallback full-materialized the loop on every note-off once Shorten/Hide companions existed. Replaced by a pitch-relevant source scan + relevant-row `applyNoteEditPassSequence`. Plan: [`realtime_incremental_work_overdub_note_off_pitch_query_refinement.md`](realtime_incremental_work_overdub_note_off_pitch_query_refinement.md). Audit **B**. Native 1072 passed / 2 skipped.

Device gate: grown-loop overdub vs [`003009`](../../captures/session_20260813_003009.log) — **withdrawn.** Option B note-off reconstruct in [`021304`](../../captures/session_20260813_021304.log) was 292 ms per note-off (`noterecon` 291775 µs, `clockrate` 36). Production restored RC-K3: `establishOverdubSourceView` gathers + reconstructs once; `accumulatePendingNoteChangesForIncomingNote` reads `overdubSourceViewNotes_`. `gatherCommittedNoteEventsForPitch` remains for tests only.

---

## Option B withdrawal (2026-08-13)

Do not optimize Option B further. [`021304`](../../captures/session_20260813_021304.log) on a 68-bar / 3714-event loop: `begin_capture` 11 µs; first overdub `noterecon` **291775 µs**, `notechg` **292976 µs**, `notepair` 968 µs, `clockrate` 36. RC-K3 [`225803`](../../captures/session_20260812_225803.log): `begin_capture` 77–83 ms once; overdub `noterecon` 0, `notechg` ~1.09 ms, `clockrate` 47–48.

Long-term candidate collection is playback-driven; selection stays normalized geometry + `[S, E)`. Active plan: [`overdub_playback_observation_overlap_refinement.md`](overdub_playback_observation_overlap_refinement.md). `OverlapNoteIdObservation` is test/diagnostic, not a production source of truth. Native Gates 0–4 landed; `PendingNote.overlapNoteIds` collection is wired; note-off still RC-K3. PLAYING idle prebuild was reverted (`73f0489`).

---

## Pre-implementation review

### Ready

- Owner and call path traced: `startOverdubbing` → `beginCapture` → `establishOverdubSourceView`; overlap consumer is `accumulatePendingNoteChangesForIncomingNote` only.
- `hasOverdubSourceView()` consumers do not require a populated event vector.

### Resolved (code)

| Topic | Decision |
|-------|----------|
| Interval reservation cannot bound this | investigation §31a; work is inside button dispatch |
| Event-window gather is not equivalent | fixture; long-note events sit outside the incoming span |
| Whole intersecting chunks are not equivalent | split-chunk gap window; 1 Shorten vs 0 |
| “Sounding chunk” expansion of all pitches | full gather |
| Option A slice-built cache | wrap + long spanning note miss Shorten; rejected |
| Option B pitch-scoped walk | unpaired on stays open across loaded chunks; native matrix + split-chunk PASS |

### Open

1. Grown-loop re-measure (64-bar / ~3000 events, same shape as [`225803`](../../captures/session_20260812_225803.log)). [`002329`](../../captures/session_20260813_002329.log) is a 10-bar / 1220-event overdub.

### Proceed?

- Option B firmware is in. 10-bar device numbers meet the note-off and start budgets. Grown-loop capture next if you want the same loop as 225803.
