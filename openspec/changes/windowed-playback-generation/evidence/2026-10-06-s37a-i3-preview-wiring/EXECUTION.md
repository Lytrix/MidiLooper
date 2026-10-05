# Task 3.7a — increment I3: the preview lane's production callers (3.13's owed half)

Execution packet for **I3** of slice 3.7a, written **before** the production edit it authorizes. Authority:
[IMPLEMENTATION_READINESS.md](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice),
[`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md), the slice's ladder
([`../2026-10-06-s37a-production-consumer/EXECUTION.md`](../2026-10-06-s37a-production-consumer/EXECUTION.md) row **I3**),
`tasks.md` items **3.13** (its owed production callers) and **3.7a**, **DEC-108** item 5, **§13(d)**/**§14**.

**Ladder correction, recorded:** the first packet's I3 bundled two halves — 3.12's paged legacy serving and 3.13's
preview callers. This increment lands the **preview** half only. The **paged** half becomes **I3b**, because it needs a
new *emission* path for an interval that cannot fit one window (a change in the hottest code path) and its timing
envelope is explicitly unmeasured — `WindowPagedStream` proves a storage bound, not throughput. Bundling it here would
have put an unmeasured hot-path change behind a proven-caller change.

## Identity and scope

- **What is owed by 3.13** (its own words): *"the production callers — the preview requester at the revision-advance
  seam and the `EditManager` mutation/re-focus/exit hooks — which belong with the stage that admits the committed
  requester, since a published window is consumed by the windowed serving path."* The lane itself (`requestPreviewWindow`,
  `cancelPreviewWindowForSlot`, the preview identity, the exit handoff) landed with 3.13's fixtures; **no production
  caller exists yet** (measured: the only call sites of either function are the fixtures).
- **What I3 adds:**
  1. `Track::servicePreviewWindowForNoteEditSession()` — the requester's caller, run on the **focus lane's own deferred
     turn** (`processDeferredIdleMaintenance`, the `VisualCache` owner): when the selected track has an active
     note-edit session on a **demoted** loop and the session's preview revision has moved past what the lane froze, the
     superseded preparation is cancelled **by name** (`SourceMutated`) and the new revision is requested through
     `requestPreviewWindow` with the session's own projection. O(1) when nothing changed.
  2. The **exit hook**: `EditManager::closeNoteEditSession` cancels with `SessionExited`, so the committed successor
     becomes owed — the handoff the spec names, performed by the owner of the session rather than by a caller.
  3. The **re-focus hook**: `EditManager::beforeSelectedTrackChange` cancels the *departing* track's preview with
     `SelectedTrackChanged` (a re-focus is not an exit: nothing is owed).
  4. The **admission's committed-currency read**: the capture admission's window question ("what the window serving this
     slot already holds: the interval's inherited content") must not count a **preview** window — the preview is the
     session's uncommitted projection, and the contract says a consumer that requires committed currency refuses it
     *without inspecting anything else* (identity origin). One committed-only read, used by the admission.
- **Preserved:** the merged NOTE_EDIT preview path (the fallback that serves editing today) is untouched; the lane's
  fixtures (which call the requester/cancel directly) stay green; `playbackWindowServesSlot` keeps its generic
  "a window serves this slot" meaning; every existing refusal name stands.
- **Primary files:** `include/Track.h`, `src/Track/TrackPlaybackWindowBuild.cpp`,
  `src/Track/TrackDeferredMaintenance.cpp`, `src/Track/TrackCaptureInput.cpp`,
  `src/EditManager/NoteEditSessionLifecycle.cpp`, `src/EditManager/EditSessionDepart.cpp`,
  `test/test_windowed_preview_integration/…`.
- **Non-goals:** no new lane, scheduler, clock or allocation; no change to the preview's coverage policy (whole loop);
  no short-loop preview window (named residual below); no paged serving (I3b); no display consumer; no device run.
- **Commit boundary:** independently buildable; with no session open, the turn's added work is one boolean check.

## Oracle fixed before behavior edits

| Case | Initial state | Ordered actions | Expected observation | Counterexample that must fail |
|---|---|---|---|---|
| **W-37a-11** the turn is the requester | demoted long loop on the selected track, ready and playing, **no** window serving the slot | open the session (`EditManager::openNoteEditSession`); drive the **production** maintenance turn (`processDeferredIdleMaintenance`, `VisualCache`) a few times; drain the lane | the lane has the session's revision as its frozen preview source (`previewSourceActive`, `previewSourceRevision == sessionPlaybackPreviewRevision()`), a **Preview**-origin window is served, its interval is the whole loop, and the served stream equals the session's projection | the fixture having to call `requestPreviewWindow` itself (today's state); or a window published with `Committed` origin; or the lane serving committed content as preview |
| **W-37a-12** the mutation edge is the firmware's | as W-37a-11, a preview window already served | mutate through the session's own path and flush the deferred refresh; drive the turn again | the lane's frozen source is the **new** revision, the superseded preparation was cancelled by name (`SourceMutated`) and not left to complete, and the served stream is the **new** projection — never a mix | serving the pre-mutation content under the new revision; or the lane keeping the old frozen revision (no cancellation ⇒ a stale completion is still possible) |
| **W-37a-13** the exit owes and the admission is committed-only | as W-37a-11 | (a) assert the admission's window read while a preview is served; (b) close the session through the production path; (c) drive the turn again | (a) `committedPlaybackWindowServesSlot` is **false** and `captureIntervalAdmissionForSlot(activeSlot).acceptedEffectiveEvents` counts **0** inherited events for the preview-served slot, while the generic `playbackWindowServesSlot` is still true and the preview window stays readable; (b) the exit yields the source and **owes** the handoff (`previewWindowHandoffOwedForSlot`), the served preview window still readable; (c) the turn does not re-request and the handoff stays owed | counting the uncommitted projection as inherited committed content (the pre-I3 admission read); or dropping the handoff, or re-requesting a preview after the exit |
| **W-37a-14** a re-focus is not an exit | as W-37a-11 on track 0 | select another track (`TrackManager::setSelectedTrack`) | track 0's preview source is yielded by the **focus** hook (`SelectedTrackChanged`) and **no** handoff is owed (a re-focus is not an exit) | leaving track 0's preview source active for a track nobody previews; or owing a handoff on a re-focus |

**Invariants asserted, not assumed:** no allocation on the added path; the served window is never torn; the fallback
merged preview path is unchanged; the lane's own fixtures stay green; the requester is idempotent per revision (driving
the turn twice requests nothing the second time).

## Controls and coverage

- **Production paths exercised:** the deferred focus-lane turn (`Track::processDeferredIdleMaintenance`), the session
  lifecycle (`openNoteEditSession` / the mutation + deferred-refresh flush / `closeNoteEditSession`),
  `TrackManager::setSelectedTrack`, and the capture admission's window read
  (`Track::captureIntervalAdmissionForSlot`). No fixture calls the requester or the cancel directly.
- **Harness:** `test/test_windowed_preview_integration` already links the **real** note-edit owner beside the **window
  lane** and exposes the session projection reads (`sessionProjection()`, `mutateSessionAndFlush`, `drainLane`,
  `commitLongLoop`, `readyLongLoopPlaying`) — the suite this increment extends.
- **Named residual:** a **short** loop (not demoted) keeps the merged NOTE_EDIT preview path; the window lane's
  requester is gated on the demoted shape, which is where a window serves the slot at all today.
- **Not covered here:** over-`L` content (I3b), the display consumer of a preview window, the save clause (6.x), the
  hardware leg.

## Red-proofs (I3)

| ID | Mutation | Must fail |
|---|---|---|
| **MR-3.7a-I** | the maintenance turn's preview service call removed | W-37a-11 (and W-37a-12/13, which build on it) |
| **MR-3.7a-J** | the supersession cancel removed (a moved revision requests without cancelling the old preparation) | W-37a-12 |
| **MR-3.7a-K** | the admission's window read counts preview content again | W-37a-13(a) |
| **MR-3.7a-L** | the re-focus hook removed (the turn alone decides) | W-37a-14 |

Each mutation is reverted and the files restored byte-identically (sha256 recorded); a mutation that does not fail its
fixture is recorded as a non-red-proof rather than reworded.

## Verification recipe

- Focused: `pio test -e native -f test_windowed_preview_integration` and
  `-f test_runtime_working_set_ownership` (the lane's own fixtures, unchanged), plus
  `-f test_capture_admission`-class suites that read the admission accounting (to be named from the tree).
- Broad: `pio test -e native` — the baseline is **2,820 cases / 7 skipped / 2,813 succeeded / 0 failed**.
- Build/RAM1: `pio run -e teensy41-capture-serial` through the RAM1 budget guard; the approved line is
  `variables 96,192` with `code 391,832 + padding 1,384` (floor 840).
- Hygiene: `openspec validate windowed-playback-generation --strict`, `git diff --check`, `git diff --stat -- src include`.
- Device: **NOT RUN**.
- Evidence path: `evidence/2026-10-06-s37a-i3-preview-wiring/`.

## Completion record

Filled after the run; planned results are never copied here as passes. Detail: [`EVIDENCE.md`](EVIDENCE.md).

- [x] Increment I3 implemented: the preview requester at the revision-advance seam (`flushDeferredNoteEditDisplayRefresh`
      and the session open), the exit hook (`closeNoteEditSession`, before the exit's own flush), the re-focus hook
      (`beforeSelectedTrackChange`), the supersession rule (stale prepared successor released through `ClearOrLoad`, the
      cancel performed whether or not the lane is busy), and the admission's committed-only read.
- [x] Fixtures W-37a-11 … W-37a-14 implemented and discriminating (four cases, none of which calls the requester), plus
      the admission fixture in `test_runtime_working_set_ownership` where the capture-admission boundary is linked.
- [x] Mutations red-proved with byte-identical restores: **MR-3.7a-I** 1 fail, **MR-3.7a-J** 1 fail, **MR-3.7a-K**
      1 fail, **MR-3.7a-L** 1 fail — **after two recorded repairs**: the re-focus fixture was first a non-red-proof
      (the service's own non-selected branch masked the hook) and gained the public read
      `previewWindowSourceActiveForSlot`; the supersession fixture was first too weak and now performs two mutations
      with no drain between them.
- [x] **Seam correction recorded:** the packet named `processDeferredIdleMaintenance`; that turn drags the whole
      deferred stop/persistence boundary into this suite's link set (measured cascade), and the seam that owns the
      revision is the flush that bumps it plus the session open — which is 3.13's own wording. The deferred-turn call
      was written, measured and removed.
- [x] Focused **9/9** (preview suite) and **151/151** (working-set suite); full native **2,825 cases / 7 skipped /
      2,818 succeeded / 0 failed**.
- [x] Capture build **SUCCESS** — the RAM1 line is **byte-identical to I2's** (`variables 96,192` / `code 391,832 +
      padding 1,384`), so no re-base and no ITCM cost.
- [x] Strict validation valid; `git diff --check` clean.
- [x] Task checkoff: **3.7a stays open** — I3b (the paged legacy consumer), I4/I5 (parity, timing) and the device leg
      remain; **3.13 stays open for its save clause** while its production-caller clause is now delivered.
