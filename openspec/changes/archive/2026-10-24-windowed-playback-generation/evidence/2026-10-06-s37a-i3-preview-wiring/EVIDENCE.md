# 3.7a (I3) — the preview lane's production callers (3.13's owed half)

Slice evidence for increment **I3**, whose packet was written before the edit ([`EXECUTION.md`](EXECUTION.md)). Task
**3.7a stays open** (I3b the paged legacy consumer, I4/I5 parity + timing), and **3.13 stays open for its save clause**
— what landed here is the production-caller clause its own text owed.

## What landed

**The preview requester and its three lifecycle hooks are now firmware code, not fixture code.** Before this slice the
only call sites of `Track::requestPreviewWindow` and `Track::cancelPreviewWindowForSlot` were the tests (measured);
after it they are the seams that own the transitions:

1. **The revision-advance seam.** `EditManager::flushDeferredNoteEditDisplayRefresh(track)` — the one place a mutation
   (or in-session undo/redo) advances `sessionPlaybackPreviewRevision()` — now asks the track to service the preview
   after the bump, and so does the session **open** path. Both call
   `Track::servicePreviewWindowForNoteEditSession()`, which: compares the session's revision against the lane's frozen
   one; **cancels the superseded preparation by name** (`SourceMutated`) **and requests** the session's own projection
   for the newest revision; is O(1) when nothing changed; and refuses to stack work on a busy lane.
2. **The supersession rule is the lane's, made reachable.** A prepared successor belonging to a revision the session
   has left is released through the shared-view row that states that shape
   (`applyPlaybackViewTransition(slot, ClearOrLoad)` — candidate released, prepared successor retired, identity
   invalidated, served window readable), because otherwise the newest revision could never be prepared: the request
   would be refused as "a successor is already waiting" and the slot would keep serving stale content. The
   supersession cancel happens **whether or not the lane is busy** — a build in flight for the old revision is exactly
   what must not land.
3. **The exit hook.** `EditManager::closeNoteEditSession(track)` yields the preview and **owes the committed handoff**
   (`SessionExited`), performed by the owner of the session rather than by a caller. It runs *before* the exit's own
   deferred-refresh flush, and the service refuses to request a preview while the handoff is owed (a preview request
   clears that obligation) — so the exit cannot be undone by its own flush.
4. **The re-focus hook.** `EditManager::beforeSelectedTrackChange(track)` yields the departing track's preview as
   `SelectedTrackChanged`; a re-focus is not an exit, so nothing becomes owed. The requester's own "not the selected
   track" branch is the documented backstop for a skipped hook.
5. **The admission counts committed content only.** The capture admission's question ("what the interval already
   holds: the interval's inherited content") is about *committed* content, and a served **preview** window is the
   session's uncommitted projection. One origin test in the admission's own read
   (`Track::committedPlaybackWindowServesSlot` / `committedPlaybackWindowEventCountForSlot`), with
   `playbackWindowServesSlot` keeping its generic meaning. `Track::previewWindowSourceActiveForSlot` was added so
   "the focus change yielded the preview **at the focus change**" is assertable at all.

## Verification

| Check | Result |
|---|---|
| focused `-f test_windowed_preview_integration` | **9/9** (5 pre-existing + 4 new I3 fixtures) |
| focused `-f test_runtime_working_set_ownership` | **151/151** (150 + the admission fixture) |
| full `pio test -e native` | **2,825 cases / 7 skipped / 2,818 succeeded / 0 failed** (I2's baseline 2,820 / 2,813) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** — `variables 96,192` / `code 391,832 + padding 1,384` / locals 34,880: **byte-identical to I2's approved line**, so this slice cost no ITCM and no RAM1 re-base was needed |
| `openspec validate windowed-playback-generation --strict` | valid |
| `git diff --check` | clean |
| `git diff --stat -- src include` | 6 files, +179/-3 |

Red-proofs, each run against the final revision and each restored byte-identically (8 tracked files verified by
sha256 after every restore):

| ID | Mutation | Observed |
|---|---|---|
| MR-3.7a-I | the revision-advance seam's service call removed | 1 failed — the mutation fixture ("the new revision is prepared and adopted at the boundary") |
| MR-3.7a-J | the supersession cancel removed | 1 failed — the mutation fixture (`Expected 3 Was 2`: the slot kept the intermediate revision) |
| MR-3.7a-K | the admission's read counts whichever window is served | 1 failed — the admission fixture (`Expected 0 Was 6`: the preview's events were inherited) |
| MR-3.7a-L | the re-focus hook removed | 1 failed — the re-focus fixture ("the focus hook yielded the departing track's preview"), **after** the fixture and the public read were added; the first version of that fixture passed under the mutation and is recorded below |

## Recorded, not smoothed over

- **The packet's seam is not the implemented seam.** The packet placed the requester on the deferred idle turn
  (`processDeferredIdleMaintenance`, `VisualCache`). Driving that turn from the preview suite drags in the whole
  deferred stop/persistence boundary (measured: `completeAdmittedRecordStop`, `requestLoopSlotPersistAndSaveState`,
  `StorageManager::*`, `SlotLoadSession::isActive`, `PersistenceWorkQueue::*`, `RecordStopLength::*` — a cascade, and a
  suite whose link set deliberately excludes them). The seam that owns the revision is
  `flushDeferredNoteEditDisplayRefresh` (the function that bumps it) plus the session open — which is also literally
  what 3.13's own text asks for ("the preview requester at the revision-advance seam"). The deferred-turn call was
  written, measured, and removed; the packet's ladder row is corrected here rather than left describing a call site
  that does not exist.
- **MR-3.7a-L was first a non-red-proof, and the fixture was the defect.** With the hook removed, all 9 cases passed:
  the service's own "not the selected track" branch performed the yield as a backstop, so the fixture could not tell
  the hook from the backstop. Fix: the public read `previewWindowSourceActiveForSlot` (the yield is now observable at
  the focus change itself) and an assertion before anything else runs. The redundancy is intentional and documented
  (the hook yields **at** the focus change; the branch is the backstop), and the mutation now fails exactly that case.
- **The supersession case needed a stronger fixture than the packet's.** The packet's W-37a-12 expected the served
  content to follow one mutation. That passes even without the supersession cancel (the new build simply becomes the
  next successor), so the fixture now performs **two** mutations with **no drain between** them: the first leaves a
  build in flight, the second arrives while it is preparing. Without the cancel the first build completes, the second
  request is refused as a prepared successor, and the slot serves the intermediate revision — the failure MR-3.7a-J
  now produces (`Expected 3 Was 2`).
- **Two pre-existing lane fixtures were re-pointed, not rewritten.** (a) The exit fixture asserted that *its own* call
  owed the handoff (`HandoffOwed`); the exit hook now does it, so the fixture's call is a **repeated** exit and the
  obligation is asserted directly (`previewWindowHandoffOwedForSlot`). (b) The `publishSessionPreviewAt` helper
  asserted `Admitted` from its own request; the seams now request first, so the helper accepts
  `Admitted`/`SuccessorAlreadyPrepared`/`BuildInFlight` and drives the same state to a served window — it asserts the
  *state*, not which caller asked first.
- **A suite-level boundary fact.** The preview suite deliberately does not link the capture-admission boundary, so the
  admission's committed-only rule is asserted where that boundary is linked
  (`test_runtime_working_set_ownership::test_3_7a_i3_the_capture_admission_counts_committed_windows_only`), with the
  committed contrast (a committed window's events *are* counted) so the rule cannot pass by counting nothing.
- **A test-only file was touched and reverted byte-identically**: an experiment added two boundary stubs to
  `off_path_deps.h`; they were removed and the file is identical to `HEAD` again (verified by `git diff --quiet`).

## Fixtures

`test_windowed_preview_integration` — `test_3_7a_i3_the_focus_lane_turn_requests_the_sessions_preview` (W-37a-11: the
session open's seam publishes a Preview-origin window at the session's own revision, the served stream is the session's
projection, and re-entering the seam requests nothing — no fixture calls the requester);
`..._the_mutation_edge_requests_the_new_revision_through_the_turn` (W-37a-12, strengthened: two mutations with no drain
between them; the newest revision is served and never a mix);
`..._the_exit_owes_the_handoff_and_never_re_requests_a_preview` (W-37a-13: the production exit owes the handoff through
the session's owner, the served preview window stays readable across the boundary, and the lane does not re-request);
`..._a_refocus_yields_the_preview_without_owing_a_handoff` (W-37a-14: the yield happens **at** the focus change and
nothing is owed).

`test_runtime_working_set_ownership` — `test_3_7a_i3_the_capture_admission_counts_committed_windows_only` (W-37a-13(a):
a preview window serves the slot, the generic presence question is true, the committed question is false and the
accounting inherits **0**; then the committed contrast, where the committed window's events **are** the accounting).

## Not claimed

No paged (over-`L`) legacy consumer (I3b), no display consumer of a preview window, no short-loop preview window (the
requester is gated on the demoted shape; a short loop keeps the merged NOTE_EDIT preview path — named residual), no
adoption boundary in production for the preview successor (the swap stays the serving path's boundary, which the
default build does not run), no edited-content/long-window/same-tick parity (I4/4.3), no timing envelope, **no device
run**, no fallback removal (3.7b). Tasks **3.7a, 3.12, 3.13 (save clause), 3.14, 4.5** stay open.
