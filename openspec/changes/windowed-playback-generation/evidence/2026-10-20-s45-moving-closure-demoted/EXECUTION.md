# Task 4.5 — landed increment: the moving closure on a long/demoted serving handoff

**Status: LANDED 2026-10-20** (firmware + native fixture; 4.5 stays open — see *Still owed*). DEC-148. No upload; the
board still runs the F-8 sweep image (HEX `7532e6ad…`, built from `5df573a2`).

## The item this closes

I9 closed the moving closure for the **merged** route: an obligation the replacement keeps while moving its own NoteOff
behind the phase the walk resumes from is closed at the handoff, not left to a walk that can no longer reach it. Its
"still owed" line was exact:

> the **moving closure inside a long/demoted slot's serving handoff** (the merged store is a two-bar slice there, so the
> windowed lane owns it — 3.7a/3.14)

A demoted slot is served by **interval windows**, and `reconcilePlaybackLedgerAfterFullLoopRebuild` returns early on a
slice by design: absence in a two-bar window is not removal evidence (measured by
`test_rolling_path_interval_ab_revision_trace_serves_each_interval_its_own_revision`, which asserts the seam closes
nothing). So the one case that could strand a sounding instance was the one where the edit **keeps** the note and moves
its end **behind the playhead**: the serviced pass can never reach that tick again, and nothing on the demoted route was
planning a closure for it.

## Decision (DEC-148)

The **phase half** of the handoff's own predicate is applied to streams that are *not* whole-loop authority:

1. Only a NoteOff the stream **carries**, at or behind the served position, is evidence. An identity the stream does not
   mention is never planned — the rule that keeps an event-empty window from silencing a sustained note.
2. The served position is the owner's existing term (`playbackClosureServeFloor`), with the lane's own projection phase as
   the fallback when the merged walk has not stamped `lastTickInLoop` (a demoted slot is exactly that case).
   `UINT32_MAX` = nothing served → nothing closed early.
3. The closure is emitted at the **edit's own edge**, through the same wire-and-ledger order as the handoff
   (`closePlannedHandoffClosures`), and **before** the presence settlement — the required NoteOff precedes any
   replacement's note-ons by construction.
4. A planned closure the ledger no longer holds is **suppressed** (`applyClosureSettlementForSlot`'s new guard), which is
   the spec's own "obsolete closures SHALL be suppressed" and prevents a second, orphan NoteOff.

Owner: `Track::reconcileAfterEditPassStateChange` (the entry the undo/redo path calls) and
`ActiveNoteLedger::planExpiredClosures`. No new state, no new field, no new tick source.

### Why not the two obvious alternatives

| Alternative | Why not |
|---|---|
| Close at the interval boundary | Needs the **incoming** revision's content for the interval that just finished; the lane never builds it. The edit is the only owner holding the new end. |
| Let the structural (absence) half run on the edit batch | Wrong: an edit batch says nothing about identities it does not mention, so it would close untouched soundings — blanket silence in a new form. |
| Wait for the next traversal of the interval | That is the defect: a whole loop of an audible note whose end the content already passed. |

## RED first (test-only, before any firmware edit)

`test_s45_the_moving_closure_closes_when_an_edit_moves_the_note_off_behind_the_playhead`, built on the 24-bar demoted
harness of the 6.5 A/B fixture:

- a demoted slot is **served** from a two-bar interval window (`playbackWindowServesSlot`, adopted at its own deadline);
- two obligations are articulated inside it: `kHeld` (its end moved) and `kKept` (re-stated unchanged);
- the edit lands **after** the moved end's tick has passed and **before** the interval ends, through the reconcile entry
  the undo/redo path calls;
- contract: the moved note closes **once, now**, the untouched one keeps sounding with no NoteOff and no retrigger, and a
  later interval's handoff closes nothing again.

RED on the shipped code, at exactly the contract assertion:

```
Expected 0 to be greater than or equal to 1. the settlement names the obligation the edit invalidated
```

## Red-proofs (each mutation restored byte-identically)

| Mutation | Result |
|---|---|
| `planExpiredClosures` replaced by a default plan in the reconcile | exactly the new fixture fails at the first contract assertion; 220/220 others pass |
| the obsolete-closure guard removed (`isActive`) | exactly the new fixture fails: `Expected 1 Was 2. the moved end closes the sounding instance exactly once, now` |

`src/Track/TrackPlaybackWindowBuild.cpp` sha256 before and after each restore: `5bcb3c773437c2a1894fe3a5b099e6e5cb1ce28988d5b517e1f520905047ae38` (mutation 1 re-run on the committed form; the first pass ran before a whitespace-only reflow of the same block and restored byte-identically then too).

## Gates

| Gate | Result |
|---|---|
| focused (`test_runtime_working_set_ownership`) | **221/221** (220 + the new fixture) |
| host (`scripts.test_windowed_content_parity_verify`) | **57/57** (unaffected; run for the closeout gate) |
| `teensy41-capture-serial` | SUCCESS — `code 392,216 + padding 1,000` (unchanged) |
| `teensy41-capture-serial-window-qualification` | SUCCESS — `code 392,376 + padding 840` (approved floor, unchanged; 0 B ITCM) |
| full `pio test -e native` | **3338 cases: 7 skipped, 3331 succeeded, 0 failed** |

The new predicate and the guard both land in `TRACK_COLD_MEM`/FLASHMEM code, which is why ITCM does not move.

## Still owed on 4.5 (unchanged by this increment)

- **3.14c** — the publication-owner request for the overdub-session identity source; its own design decision.
- **Displayed provenance** — RWO S4e, sibling change, cross-dependency.
- **Device leg** — this increment is native-only; the board has not run it.
