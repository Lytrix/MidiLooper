# 6.5 — force 2's rolling half: the interval A/B revision trace on the rolling chain (2026-10-17)

**Disposition: task 6.5's last *native* gap is now forced by a production-path fixture; the task stays open on its
hardware half, and nothing is checked off.** Force 2 (the interval A/B revision trace) was recorded as *partial* since
2026-10-05 — covered on the shared-view path, named as a gap on the rolling chain. It is now forced on the rolling chain
itself, and the fixture's own probe found that the chain treats a mid-service revision change differently from the
shared-view path: the prepared successor is **stale** and the episode ends at the boundary, so the post-edit revision is
served in the **next aligned window the chain chooses**, not in the interval immediately after A.

No production line changed. The fixture is test-only; the two mutations that reproduce the design rule and the token
liveness control are below.

## The fixture

`test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp` →
`test_rolling_path_interval_ab_revision_trace_serves_each_interval_its_own_revision`.

Shape: the existing demoted 24-bar target through the production edge (`assert_s4b_7_a_demoted_target_through_the_turn`),
slot 2, whose **published root is a revision** the fixture builds itself:

- **r0** carries two bar-spaced notes (pitch 48 at bars 6 and 7) and a sustained note `kHeld = 70` at `[6100, 6200)` —
  its On lies inside A's window `[4608, 6144)` and its Off lies beyond A's boundary, so the edit has an *open obligation*
  to deal with rather than a note that politely closed inside A;
- **r1** (published while A serves) removes that note and places `kAdded = 72` inside the later window, keeping the
  bar-spaced notes.

Driven through the production entry only: `serviceRollingPlaybackWindow` → the real builder grains
(`stepPlaybackViewMaintenance`) → the boundary adoption at the frame's own deadline (`resolveRollingPlaybackWindow`), on
`rollingTestTick` ticks. The emitted trace is read from `MidiSendCapture`, the ledger from the existing test accessor,
and the closure rule from the seam the rebuild owners call (`planClosureSettlementForSlot` /
`applyClosureSettlementForSlot`).

## Measured chain sequence (probe of this slice, then asserted)

```
d1    Requested  current=2 rev=2 startPhase=4608 deadline=4608
d384  Ready      current=2 rev=2            events=1     (legacy fallback emits bar 6)
d4608 Serving    current=1 rev=2            events=4     (A adopted at its own deadline)
                 [edit published: rev 3, kHeld removed, kAdded added]
d6144 (Idle)     current=1 rev=3            events=8     (the successor prepared from r0 is stale)
d6208 Requested  startPhase=10752 deadline=10752          (the chain's next aligned window: bar fourteen)
d6400 Ready      current=1 rev=3
```

Asserted, in order:

1. A is adopted at its own deadline (`adoptedAt == 4608`) and emits its own content (`emittedAtEnd > emittedAtAdoption`);
2. the edit ends that episode **at its boundary** (`endedAt == 6144`): a successor prepared before the revision moved
   cannot be adopted;
3. the chain then demands a **later aligned window** (`laterAt > 0`, `laterStartPhase >= aEnd + 2 bars`,
   `laterStartPhase % (2 bars) == 0`, measured bar 14 = 10,752);
4. the trace separates the two revisions: r0's sustained note is articulated **exactly once** and never again
   (`onsHeldA == 1`, `onsHeldLater == 0`), r0's bar-spaced content is **never re-served as fresh** after A ends
   (`onsStaleAfterEdit == 0`), and r1's replacement note is emitted **exactly once with one closure**, and never before
   the window that serves r1 (`onsAdded == 1 == offsAdded`, `onsAddedBeforeLater == 0`).

## The closure rule at the seam (why the wire carries no closure here)

A window is a **slice**, and the repo's own rule — `handoffAbsenceMeansRemoval(incomingIsWholeLoop, eventEmptyWindow)` —
says a slice's absence is *not* removal evidence. The fixture pins that where it is decided:

| Call | Result |
|---|---|
| `planClosureSettlementForSlot(slot, postEdit, /*incomingIsWholeLoop=*/false)` | `closureCount == 0`, `preservedCount >= 1` — the stranded obligation is **preserved**, not closed and not dropped |
| `applyClosureSettlementForSlot(slot, slicePlan)` | emits **0** events, and the ledger entry is **still sounding** |
| `planClosureSettlementForSlot(…, /*incomingIsWholeLoop=*/true)` | `closureCount == 1` — whole-loop authority *is* removal evidence |
| `applyClosureSettlementForSlot(slot, wholePlan)` | emits **exactly one** NoteOff for `kHeld`, at the wire, and the ledger entry is discharged |
| the same whole-loop plan taken again | `closureCount == 0` — exactly once, never twice |

The "closures before the replacement's note-ons" half is the seam's assembly order (`applyClosureSettlementForSlot`
closes and the *caller* emits note-ons afterwards), so it is asserted as one closure and no duplicate rather than
invented as a wire interleaving.

## Mutations and the one non-red-proof

Production restored **byte-identically** after each (`cmp`), and `src/Track/TrackPlaybackWindowBuild.cpp` stayed at
`sha256 0cae6d6b662c00d2b545dc2ba5774a59faa845d2c62582d8b481bcb268e0f62a` throughout.

| ID | Change | Result |
|---|---|---|
| **M2** | `planClosureSettlementForSlot`: `absenceMeansRemoval` forced `true` (every window treated as whole-loop) | **RED — the fixture fails at "the slice handoff closes nothing"** (`Expected 0 Was 1`); the pre-existing `test_cross_generation_closures_are_reconciled_and_an_empty_window_silences_nothing` fails at "a slice cannot prove a sounding note was removed" |
| **M1** | `rollingCandidateReadyAtBoundary`: both revision-currency terms removed | **214/214 — a non-red-proof.** The stale candidate is still refused, so the revision currency is defended beyond that one predicate |
| **M3** | M1 **plus** the `ready.source.playbackRevision != loop.playbackRevision` term in `resolveRollingPlaybackWindow`'s Ready branch | **214/214 — a non-red-proof** for the same reason; a further term (`playbackWindowSourceIsCurrentForSlot`) stands in the path |
| **Liveness control** | the trace assertion's expectation moved off the measured value (`onsAdded` 1 → 2), fixture untouched otherwise | **RED at exactly that assertion** (`Expected 2 Was 1`) — the measurement is live even though the guard mutations do not move it |

M1 and M3 are reported as they happened: they do not become evidence by being omitted, and they do not weaken the
fixture's *closure* half, which M2 red-proves. What M1/M3 do establish is recorded as a fact about the code: the
"stale successor after a mid-service revision move" outcome is redundantly guarded, so this fixture pins the
**outcome** (episode ends, later aligned window carries the new revision) rather than a single line.

## Verification

- Focused: `pio test -e native -f test_runtime_working_set_ownership` — **214 cases, 214 succeeded, 0 failed**.
- Full native: `pio test -e native` — **3,304 cases / 7 skipped / 3,297 succeeded / 0 failed** (pre-change **3,303 / 7 /
  3,296**; the delta is this one case).
- `git diff --check` clean; `openspec validate windowed-playback-generation --strict` valid.
- **No production source change**: `src/` and `include/` carry only the pre-existing dirty tree of this branch, and the
  three mutation sites were restored byte-identically after every run.

## Not claimed

- **Task 6.5 stays open**: its hardware half (grain duration and maximum service gaps — now *measured* in
  [`2026-10-17-s65-hardware-claims-reading`](../2026-10-17-s65-hardware-claims-reading/EVIDENCE.md) — sustained
  multi-track contention, external-clock behaviour and memory high-water with boundary content parity) is still owed.
- **No NOTE_EDIT integration** (task 3.15d): the edit here is a published-root revision, not an `EditManager` session
  row; the seam assertion is the rule NOTE_EDIT's owner will call, not that owner's behaviour.
- **No device observation** of the closure ordering, and no claim that the wire ordering is proved beyond the seam's
  assembly order.
- The later window's exact start phase (bar fourteen) is **measured on this fixture**, not a derivation — the
  alignment rule behind it is the chain's own and is not claimed here.
