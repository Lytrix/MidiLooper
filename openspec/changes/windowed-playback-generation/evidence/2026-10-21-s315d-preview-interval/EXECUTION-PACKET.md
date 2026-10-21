# 3.15d slice 1 — interval-qualified preview composition (G2 execution packet)

Authority: [IMPLEMENTATION_READINESS.md § G2 execution packet](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice).
This packet covers **one slice** of task 3.15d. It is not evidence that 3.15d, 2.12, 3.7b or 6.4 passed.

Status: **specified** (oracle fixed, no production line changed yet).

## Slice map (3.15d is larger than one reviewable change)

| Slice | Behaviour change | Gate |
|---|---|---|
| **1 (this packet)** | The preview is requested as the **same interval the committed route uses**, composed from committed interval content plus the identity-qualified session delta; an empty *effective* interval is a complete silent composition. Both loop lengths request. | Native: window identity/interval/content, empty-interval admission, currency. No audible change (the merged NOTE_EDIT path still serves). |
| 2 | The clock edge adopts a **`Preview`-origin** fixed view while `isNoteEditActive()`, beside the frozen-Capture branch; selective closures for removed/replaced identities; merged NOTE_EDIT remains the named compatibility fallback. | Native: adoption RED when the branch is suppressed, ledger continuity, stale-preview asymmetry. |
| 3 | 3.15d checkoff: short/long parity, lifecycle (mutation/undo/redo/re-focus/exit) through the single reader, 2.12's save clause re-read against the interval source. | Focused + full native + both builds; device parity stays 3.15f. |

Slices 2 and 3 are **not** in this packet and are not implied by it.

## Identity and scope

- **Task / fixture:** `3.15d` (serve short and long NOTE_EDIT from the same interval window reader) — slice 1; fixture `2.12` (preview-source fixture) composition half.
- **Approved contract:** DEC-116 §2 and the design's *NOTE_EDIT is a source composition, not a second playback engine* (`design.md`), plus design slice **S4**'s predicate replacements (1) and (2). No new DEC: this slice executes a recorded contract, it does not change it.
- **Owner and path:** `Track::requestPreviewWindow` → `Track::requestPlaybackViewMaintenance` → existing `LoopPlaybackRuntime` build/`ready`/`current` roles; the production requester is `Track::servicePreviewWindowForNoteEditSession`, run on the focus-lane deferred turn (`NoteEditSessionLifecycle.cpp`, `NoteEditDisplayProjection.cpp`). `EditManager` keeps session mutation and preview-revision ownership.
- **Primary files:** `src/Track/TrackPlaybackWindowBuild.cpp` (`requestPreviewWindow`, `servicePreviewWindowForNoteEditSession`, a new caller-owned interval-composition helper), `include/Track.h` (signature only if the composition needs an explicit present/empty term), `src/Track/TrackPlaybackHotPath.cpp` **read-only** in this slice.
- **Preconditions present:** preview lane production requester (3.7a I3 / 3.13), `Preview` origin in `PlaybackViewSourceIdentity`, `previewSourceRevision/Active/HandoffOwed`, cancel-by-name lifecycle (`SourceMutated`, `SelectedTrackChanged`, `SessionExited`), `RuntimePlaybackViewFill` already treats `sourceCount == 0` as a **complete filled empty source** (`sourceFilled = true`), the existing 11-case preview integration suite, and the merged NOTE_EDIT composition rule (`ensurePlaybackMergedMidiEventsBuilt`, current-root branch) as the compatibility reference.
- **Prerequisites included in this slice:** the composition helper itself and its parity assertion against the merged rule; interval qualification in the requester.
- **External blockers:** none for slice 1. Slice 2 depends on this slice; 3.15f depends on 3.10/6.3/6.5 and 3.14/4.5.
- **Mutation:** replace the whole-loop coverage in `requestPreviewWindow` with `playbackWindowInterval(loopLength, shouldAvoidFullVisualRebuild(loopLength), phase, coverage)`; replace the `sourceCount == 0 ⇒ NotServable` refusal with "absent composition only" (`sourceCount != 0 && source == nullptr` is the invalid shape, matching `RuntimePlaybackViewFill::begin`); remove the demoted-only gate and the empty-`sessionMidiEvents()` refusal in the service function; compose the source from the **interval** instead of passing the whole-loop projection.
- **Postconditions:** a preview window is published with the committed interval geometry, `origin == Preview`, the session's preview revision as freeze token, and content equal to the specified composition for that interval; an empty effective interval publishes a complete silent window that replaces the stale one.
- **Explicit non-goals:** edge adoption during NOTE_EDIT; `fixedViewEligible`; any change to the merged fallback or its fixtures; selective closure/ledger behaviour for removed identities; display (RWO S4e); save/storage; device leg; 3.7b; DEC-147/DEC-155 positioning; admission constants.
- **Commit boundary:** independently buildable; the merged fallback keeps serving NOTE_EDIT, so the audible route is unchanged and a revert restores today's image by construction (rolling stays behind `PLAYBACK_WINDOW_ROLLING_ENABLED`).

## Oracle fixed before behavior edits

Scenario base: 32-bar loop (`kLongLoopTicks = 32 × 768 = 24 576`) with four known notes at ticks 1 000 / 3 000 / 5 000 / 7 000, committed passes present, transport playing at a known phase, session opened through `EditManager::openNoteEditSession` (not by calling the requester). Playhead chosen so the 2-bar interval is `{start = 2 304, length = 1 536, loop = 24 576}` (playhead 3 072). The interval contains notes B (3 000) and C (5 000) only.

**P1 — long loop, non-empty effective interval.**
- Delta: B's end moves from 3 048 to 3 120; C is deleted in-session; A and D are outside the interval and unchanged.
- Steps: open session → drain the lane with the existing one-grain turn (`drainLane`).
- Expected after the turn: exactly one published window; `identity.origin == Preview`; `identity.playbackRevision == editManager.sessionPlaybackPreviewRevision()`; `identity.interval == {2 304, 1 536, 24 576}`; content = `{B.on@3 000, B.off@3 120}` (hand-written expectation; C absent, A/D absent because outside).
- Counterexample that must fail: whole-loop coverage (`{0, 24 576, 24 576}`) or the whole-loop projection's content (A, B, C, D).

**P2 — empty effective interval is Admitted.**
- Delta: C (the interval's only note in a second scenario variant) is deleted.
- Expected: `requestPreviewWindow` returns `Admitted`; the lane completes a `ready`/`current` with `sourceFilled` and **zero** events; the published identity is still the preview revision and the interval; the previously served preview does not survive as the served window.
- Counterexample that must fail: `NotServable` on an empty composition, or the old window still served (stale retention).

**P3 — short loop (≤ 16 bars) requests.**
- Base: 2-bar loop (`1 536`), two notes, session open, transport playing.
- Expected: the service function requests (the demoted-only gate is gone); `identity.interval == {0, 1 536, 1 536}`; content equals the composition; the merged fallback's audible stream is **unchanged** in this slice (asserted by the existing short-loop characterisation, not by a new emitter).
- Counterexample that must fail: no request (the old gate) or a two-bar clamped interval for a loop shorter than the span.

**P4 — currency under supersession.**
- Two mutations through the production flush with no drain between them (existing `test_3_7a_i3_the_mutation_edge_requests_the_new_revision_through_the_turn` shape).
- Expected: the superseded preparation is cancelled by name, the newest revision is the only publishable one, and no served window mixes revisions.
- Counterexample that must fail: the intermediate revision publishing after the newer request.

**P5 — preview is never committed currency.**
- Expected: `publishedPlaybackWindowIdentityForSlot` reports `Preview`; the snapshot identity is null; the exit still yields the source and owes the committed handoff (`previewHandoffOwed`), and the refocus path still owes nothing.
- Counterexample that must fail: a preview window comparing equal to a committed identity at the same revision/generation.

**Preserved invariants:** no allocation in the requester or the service turn; at most one prepared successor; the served window stays readable across a cancellation; exactly one published window per revision; the merged fallback's fixtures unchanged.

## Controls and coverage

- **Seams reused:** `clockManager.assignCurrentTickSilently`, `track.forceSetState`, `track.prewarmPlaybackForSlot`, `stepPlaybackViewMaintenance` (one grain per turn), `LoopEventStore::initPool` — the existing `test_windowed_preview_integration` harness (`commitLongLoop`, `readyLongLoopPlaying`, `laneTurn`, `drainLane`, `servedEventKeys`).
- **Production paths exercised:** the session-open seam (`openNoteEditSession` → deferred flush → `servicePreviewWindowForNoteEditSession`), the requester, the admission, the fill/order roles, and publication. The fixture does **not** call `requestPreviewWindow` to establish the request.
- **Independent expected content:** hand-written row expectations per scenario (P1/P2/P3); no oracle derived from the implementation's own output. The merged composition rule is used only as a **parity reference** for the interval subset, never as the expectation.
- **RED controls:** (ii) make the empty composition refuse again → P2 RED; force whole-loop coverage → P1/P3 RED; drop the interval filter from the composition → P1 RED with the outside notes present; keep the old window on a delete → P2 stale-retention RED. Each mutation restored byte-identically.
- **Bounded drain:** `drainLane`'s 2 048-turn bound; failure prints the last owner state (`hasPendingPlaybackViewMaintenance`, build phase, ready index).
- **Untestable before link:** none — the slice changes existing functions; no new symbol prevents a pre-fix run. The pre-fix RED is behavioural (`Admitted` vs `NotServable`, interval vs whole loop).

## Verification recipe

- Focused: `pio test -e native -f test_windowed_preview_integration` (11 existing cases must stay green; new P1–P5 cases added), `pio test -e native -f test_runtime_working_set_ownership` (preview cases at the `requestPreviewWindow` call sites, the `:8419` short-loop pair and the `:14575` interval case).
- Full: `pio test -e native` — last recorded baseline **3 352 cases / 7 skipped / 3 345 succeeded / 0 failed** at `555354a4` (3.7h); re-measure at slice start, the closeout `02bbf828` is docs-only.
- Builds + RAM1: `pio run -e teensy41-capture-serial` and `pio run -e teensy41-capture-serial-window-qualification`; pins to preserve: default `variables 96 192 / code 392 216 + padding 1 000`, qualification `392 376 + 840` (floor, `scripts/ram1_budget.json`).
- Specs/diff: `openspec validate windowed-playback-generation --strict`, `git diff --check`.
- **Device:** NOT RUN in this slice — the audible route is unchanged (merged fallback), so a device run would not judge this slice; the route's device parity belongs to 3.15f.
- Evidence destination: this directory (`EXECUTION.md` for the completion record).

## Completion record

Every row is **NOT RUN** until the slice lands; a planned command is not a pass.

| Check | Status |
|---|---|
| Oracle/contract unchanged | NOT RUN |
| Fixture implemented (P1–P5) + discriminating RED | NOT RUN |
| Focused native (`test_windowed_preview_integration`, `test_runtime_working_set_ownership`) | NOT RUN |
| Full native | NOT RUN |
| Both builds + RAM1 pins | NOT RUN |
| Strict validation / `git diff --check` | NOT RUN |
| Device | NOT APPLICABLE (route unchanged; 3.15f owns it) |
| 3.15d / 2.12 checkoff | NOT ELIGIBLE (slices 2–3 owed) |
