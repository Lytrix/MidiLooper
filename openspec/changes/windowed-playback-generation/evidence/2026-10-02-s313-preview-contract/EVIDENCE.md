# Slice 3.13 — the preview source contract (identity, freezing, cancellation, committed handoff)

**Change:** `windowed-playback-generation` · **Task:** 3.13 (**the lane landed; the task stays open**) ·
**Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Slice 3.13
**Contract:** DEC-108 item 5, item 12(e) (NOTE_EDIT preview keeps its own preview-source contract with its own identity
and invalidation and never masquerades as committed currency); OPEN-3 (preview moves to immutable deferred publication);
the spec's *Preview content is a distinct source and is never committed currency*.

**What the task asks:** publish preview through the **same immutable deferred discipline** as committed content, defining
**source freezing** (the preview revision a build works from), **cancellation** (session mutation, track re-focus, undo,
exit between build grains) and **committed-content handoff** at session exit, with preview identified as preview and
never accepted as committed currency — and without narrowing the live mutable fallback's removal on preview's account.

---

## What landed

| File | Change |
|---|---|
| `include/RuntimePlaybackView.h` | `PlaybackWindowOrigin { Committed, Preview }`, carried in **`PlaybackViewSourceIdentity`** (the source contract a view came from) and in **`PlaybackWindowIdentity`** (the consumer-facing tuple), both **part of equality** — so a preview window at the same slot, generation, revision and interval is *not equal* to the committed one it stands in front of. |
| `include/TrackPlaybackRuntime.h` | The preview lane's own state on the per-slot runtime: `previewSourceRevision` (the lane's freeze token — the session's revision, never a second committed currency), `previewSourceActive` (**whose** in-flight/served work this is, so a cancellation can never cancel a committed build) and `previewHandoffOwed` (the exit handoff is owed until a committed window serves). |
| `include/Track.h`, `src/Track/TrackPlaybackWindowBuild.cpp` | `Track::requestPreviewWindow(slot, previewRevision, source, sourceCount, currentPhase)` — the same deferred-publication lane as the committed one, with the **whole-loop** coverage the session's projection is, the frozen preview revision as currency and `origin == Preview` in the identity; `Track::cancelPreviewWindowForSlot(slot, reason)` for the three lifecycle reasons (`SourceMutated`, `SelectedTrackChanged`, `SessionExited`), returning a **named** `PreviewCancelOutcome`; and `servedPlaybackWindowIsPreviewForSlot` / `previewWindowHandoffOwedForSlot` as the read-only answers a consumer and a fixture ask. `publishedPlaybackWindowIdentityForSlot` now copies the origin **from the source identity it was built from** — identified by construction, not by a flag a caller must remember. |

**The design points that carry the requirement:**

- **One publication lane, two source contracts.** Preview reuses `requestPlaybackViewMaintenance` → the owner's bounded
  grains → the atomic publication primitive; what differs is the source, its currency and its identity. Nothing about
  the preview path weakens the committed one: they share the lane, and the *origin* tells them apart.
- **Source freezing is a revision, not a copy.** The lane freezes `previewRevision` and the projection pointer, exactly
  as the committed lane freezes its enumerated scratch; a session mutation makes the frozen revision stale, and a stale
  completion cannot pass as current. No content-sized second buffer is introduced.
- **Cancellation is the shared-view row, not a new teardown.** `cancelPreviewWindowForSlot` applies
  `PlaybackViewTransition::ClearOrLoad` — the row that releases **only** the candidate's resources, retires a prepared
  successor and invalidates the operation identity — and touches nothing else. The served window stays readable
  throughout, which is the invariant every row carries: a mutated source makes the served preview stale **by its own
  revision**, and staleness is answered as currency rather than by tearing a window.
- **The exit handoff is a boundary, not a gap.** `SessionExited` clears the lane's ownership and makes the committed
  successor *owed*; the preview window keeps serving until a committed successor is **adopted**, at which point the
  obligation is discharged. A **repeated** exit answers `AlreadyHandedOff` rather than re-requesting.
- **"No dependency on the live mutable fallback" is structural:** the lane publishes through the immutable primitive, so
  the fallback's removal (3.7) is not narrowed on preview's account. The fixture also pins the *other* direction — the
  committed request is what discharges the handoff, so committed playback never waits on a preview path.

## The oracle, case by case

| Case | Observed |
|---|---|
| identified as preview | the published window carries `origin == Preview`; `servedPlaybackWindowIsPreviewForSlot` answers true |
| its own currency | `playbackRevision` is the **session's** preview revision, not the loop's committed one; the interval is whole-loop (a session is not an interval) |
| never committed currency | a committed identity at the same slot/generation/revision/interval is **not equal** to the preview window (`==` false, `!=` true) |
| nothing to publish | a preview with no content is refused `NotServable` instead of publishing "no events" over edited content |
| cancellation with nothing of its own | `NothingToCancel` — it must not cancel a committed build |
| an in-flight build cancelled | `BuildCancelled`, the owner owes nothing further, and **no window is published** (asserted on a slot with no served window, so "nothing published" is observable) |
| the exit handoff | `HandoffOwed`; the preview window is **field-for-field unchanged** and still serving; a repeated exit is `AlreadyHandedOff` |
| the handoff's boundary | the committed successor is prepared, the served window is *still* preview, the handoff is still owed; after adoption the served origin is `Committed` and the handoff is discharged |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **61/61** (was 59; +2 fixtures) |
| `pio test -e native` (full) | **2692 cases: 7 skipped, 2685 succeeded, 0 failed** (baseline 2690 → +2) |
| `pio run -e teensy41-capture-serial` (clean rebuild) | **SUCCESS** — `variables 96,192` **unchanged** / `code 392,328 + padding 888` / locals 34,880, floor 840, **48 B headroom** |

**The size pin was re-based, measured and recorded** (the rule: re-base with the reason at the assert): the three
preview-lane fields are 8 B of payload but land before members whose alignment then moves, so the **target** figure is
`sizeof(LoopPlaybackRuntime) == 6,512 → 6,624` (**+112 B**, read from a temporary compile-time probe that was removed
again). The **host** pin is unchanged at 6,544 (its slack absorbed them). The state lives in extmem per-slot storage, so
it costs no RAM1 `variables` and no DTCM — and the RAM1 line's `variables` is byte-identical to the pre-slice value.
**Recorded, not smoothed over:** the preview lane's ~256 B of ITCM code took the RAM1 **padding** from 1,144 to **888**,
which still passes the 840 floor with **48 B** to spare — a thin margin worth naming before the next slice adds code to
this path.

## Red-proofs

Five mutations, each restored **byte-identically** (`shasum -a 256 -c` against the pre-mutation hash of all five files):

| # | Mutation | Observed failure |
|---|---|---|
| M22 | the origin is not carried into the window identity | *"the published window is identified as preview"* (both fixtures) |
| M23 | the origin is not part of identity equality | *"a committed window is not the preview window at the same revision and interval"* |
| M24 | the cancellation does not release the in-flight build | *"the cancelled build owes nothing further"* |
| M25 | the exit does not owe the committed handoff | `Expected TRUE Was FALSE` on the owed handoff |
| M26 | the lane does not mark its own in-flight build | `Expected 2 Was 0` — the cancel answered `NothingToCancel` |

## Not claimed

- **No production requester for either lane.** Nothing in production requests a committed *or* preview window yet
  (3.7's own gate), so the preview lane is **inert in today's running system**: the fixtures supply the preview
  projection, the revision and the lifecycle calls. The lane, its identity, its cancellation and its handoff are
  implemented and exercised; the production *callers* (the preview requester, and the `EditManager` lifecycle hooks for
  mutation/re-focus/exit) are owed to the same stage that admits the committed requester.
- **Fixture 2.12's full matrix is not run.** Its window-lane clauses (identified as preview, invalidated by its own
  lifecycle, no mixed/stale publication, never committed currency, committed handoff) are asserted here. Its
  *integration* clauses — edit **while playing** on a long loop, cross window boundaries, in-session undo/redo, switch
  the selected track, mutate/exit **between build grains**, then save, and assert **no mixed or stale preview
  publication** — need a suite that links **both** the window lane (`track_harness.inc`) and the real edit stack
  (`EditManager.cpp`, as `capture_storage_harness.inc` does); the RWO suite deliberately stubs the edit session
  ("this suite never opens an edit session"), and driving a real one there would be a new harness combination rather
  than a fixture. Named as owed.
- No deadline/timing claim (F-2 stays open), no envelope claim beyond the per-slot size figure, and no device evidence.
- **Task 3.13 stays open** on the strength of the item above.
