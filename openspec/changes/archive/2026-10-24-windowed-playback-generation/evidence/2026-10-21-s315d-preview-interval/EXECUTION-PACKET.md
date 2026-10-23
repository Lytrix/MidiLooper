# 3.15d slice 1 — interval-qualified preview composition (G2 execution packet)

Authority: [IMPLEMENTATION_READINESS.md § G2 execution packet](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice).
This packet covers **one slice** of task 3.15d. It is not evidence that 3.15d, 2.12, 3.7b or 6.4 passed.

Status: **landed** (slice 1 committed; slices 2–3 owed). Completion record and RED/GREEN evidence: [`EXECUTION.md`](EXECUTION.md).

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
- **Primary files:** `src/Track/TrackPlaybackWindowBuild.cpp` (`requestPreviewWindow`, `servicePreviewWindowForNoteEditSession`, the new `Track::composePreviewIntervalSourceForSlot` and the file-local delta rule shared with `ensurePlaybackMergedMidiEventsBuilt`), `include/Track.h` (the composition declaration), `include/TrackPlaybackRuntime.h` + `src/Track/TrackPlaybackRuntime.cpp` (the slot-resident composition scratch and the measured size pins), `src/Track/TrackPlaybackHotPath.cpp` **read-only** in this slice.
- **Preconditions present:** preview lane production requester (3.7a I3 / 3.13), `Preview` origin in `PlaybackViewSourceIdentity`, `previewSourceRevision/Active/HandoffOwed`, cancel-by-name lifecycle (`SourceMutated`, `SelectedTrackChanged`, `SessionExited`), `RuntimePlaybackViewFill` already treats `sourceCount == 0` as a **complete filled empty source** (`sourceFilled = true`), the existing 11-case preview integration suite, and the merged NOTE_EDIT composition rule (`ensurePlaybackMergedMidiEventsBuilt`, current-root branch) as the compatibility reference.
- **Prerequisites included in this slice:** the composition helper itself and its parity assertion against the merged rule; interval qualification in the requester.
- **External blockers:** none for slice 1. Slice 2 depends on this slice; 3.15f depends on 3.10/6.3/6.5 and 3.14/4.5.
- **Mutation:** replace the whole-loop coverage in `requestPreviewWindow` with `playbackWindowInterval(loopLength, shouldAvoidFullVisualRebuild(loopLength), phase, coverage)`; replace the `sourceCount == 0 ⇒ NotServable` refusal with "absent composition only" (`sourceCount != 0 && source == nullptr` is the invalid shape, matching `RuntimePlaybackViewFill::begin`); remove the demoted-only gate and the empty-`sessionMidiEvents()` refusal in the service function; compose the source from the **interval** instead of passing the whole-loop projection. The composition lives in the slot runtime's resident scratch (`LoopPlaybackRuntime::previewSourceEvents`), written only after the service's busy guard, so a live build's source is never overwritten.
- **Postconditions:** a preview window is published with the committed interval geometry, `origin == Preview`, the session's preview revision as freeze token, and content equal to the specified composition for that interval; an empty effective interval publishes a complete silent window that replaces the stale one.
- **Explicit non-goals:** edge adoption during NOTE_EDIT; `fixedViewEligible`; any **behavioural** change to the merged fallback (its delta rule is extracted verbatim into the shared helper so the two readers cannot drift; its fixtures are unchanged); selective closure/ledger behaviour for removed identities; display (RWO S4e); save/storage; device leg; 3.7b; DEC-147/DEC-155 positioning; admission constants.
- **Commit boundary:** independently buildable; the merged fallback keeps serving NOTE_EDIT, so the audible route is unchanged and a revert restores today's image by construction (rolling stays behind `PLAYBACK_WINDOW_ROLLING_ENABLED`).

## Oracle fixed before behavior edits

Scenario base: the suite's existing **64-bar root fixture** — `commitLongLoop(track, 0, kLongLoopTicks = 64 × 768, 6)`
plus `publishCommittedRootForSlot(loop, 64)`, which installs one identified note per bar (pitch 48, on at `bar × 768`,
off at `+48`, `noteId = 2000 + bar`). A current published root is what makes the session store **only its delta**, which
is the composition's discriminating property; the playhead is placed at **3 072** so the two-bar interval is
`{start = 2 304, length = 1 536, loop = 49 152}` and contains bar 3 (`noteId 2003`, 2 304/2 352) and bar 4
(`noteId 2004`, 3 072/3 120) — and nothing else. (Geometry note: the packet originally named a 32-bar / four-note base
with interval `{2 304, 1 536, 24 576}`. The executed base is the suite's root fixture with the same interval *shape* and
the same discriminating content — an interval holding one edited and one untouched identity — because that fixture
already writes identity-qualified rows, which the 32-bar base would have had to rebuild by hand. The oracle's
expectations below are hand-written, not derived from the implementation.)

**P1 — long loop, non-empty effective interval.**
- Delta: bar 3's end moves from 2 352 to 2 400; bar 4 is **unchanged committed content in the same interval**; bars
  outside the interval are untouched.
- Steps: open session → apply the delta → `refreshNoteEditSessionProjection` → the production flush edge
  (`invalidateCaches(true)` + `flushDeferredNoteEditDisplayRefresh`) → drain the lane → take the boundary if the newest
  revision completed as the successor.
- Expected: exactly one published window; `identity.origin == Preview`; `identity.playbackRevision ==` the flush's
  preview revision; `identity.interval == {2 304, 1 536, 49 152}`; content = `{on@2 304, off@2 400, on@3 072,
  off@3 120}` — hand-written. Bar 4's rows are the discriminator: a delta-only source omits them.
- Counterexample that must fail: whole-loop coverage (`{0, 49 152, 49 152}`), or the whole-loop projection's content,
  or the delta alone.

**P2 — empty effective interval is Admitted.**
- First a non-empty preview serves (P1's delta), then **both** identities the interval holds are deleted
  (`markRowDeleted(2003)` for the edited row; `upsertRow(2004, …, Deleted)` for the untouched committed row, because
  `markRowDeleted` only marks a row that already exists).
- Expected: the service composes zero events over a **valid** interval and `requestPreviewWindow` returns `Admitted`;
  the lane publishes that emptiness at the new revision and it is adopted at the boundary; the previously served
  preview does not survive as the served window; `identity.interval` still states `{2 304, 1 536}`.
- Counterexample that must fail: `NotServable` on an empty composition, or the old window still served (stale
  retention).

**P3 — short loop (≤ 16 bars) requests.**
- Base: **4-bar** loop (`3 072`), two committed notes (1 024, 2 048), session opened through `openNoteEditSession`.
- Expected: the service function requests (the demoted-only gate is gone); `identity.interval == {0, 3 072, 3 072}`;
  content equals the session projection's interval subset; the merged fallback's audible stream is **unchanged** in this
  slice (asserted by the existing short-loop characterisation, not by a new emitter).
- Counterexample that must fail: no request (the old gate) or a two-bar clamped interval for a loop shorter than the
  span. (4 bars rather than the packet's 2 gives the handoff headroom the admission needs: `lead = 768` against
  `(0 − phase) mod length`.)

**P4 — currency under supersession.**
- Two edits to the same identity through the production flush with no drain between them (existing
  `test_3_7a_i3_the_mutation_edge_requests_the_new_revision_through_the_turn` shape), the second moving the same note's
  end to 2 500.
- Expected: the superseded preparation is cancelled by name, the newest revision is the only publishable one, and the
  served content is `{on@2 304, off@2 500, on@3 072, off@3 120}` — the intermediate `off@2 400` never serves.
- Counterexample that must fail: the intermediate revision publishing after the newer request.

**P5 — preview is never committed currency.**
- Expected: `publishedPlaybackWindowIdentityForSlot` reports `Preview`; the source identity names no published
  committed root (null `snapshotIdentity`); `servedPlaybackWindowIsPreviewForSlot` is true and
  `committedPlaybackWindowServesSlot` / `committedPlaybackWindowEventCountForSlot` answer committed-only; a
  `PlaybackViewSourceIdentity` differing only in `origin` is **not** equal; and the exit still yields the source and
  owes the committed handoff while the preview keeps serving.
- Counterexample that must fail: a preview window comparing equal to a committed identity at the same
  revision/generation. (The re-focus path's "owes nothing" clause is covered by the existing
  `test_a_selected_track_change_cancels_the_departing_tracks_preview_by_its_own_lifecycle`.)

**Preserved invariants:** no allocation in the requester or the service turn; at most one prepared successor; the served window stays readable across a cancellation; exactly one published window per revision; the merged fallback's fixtures unchanged.

## Controls and coverage

- **Seams reused:** `clockManager.assignCurrentTickSilently`, `track.forceSetState`, `track.prewarmPlaybackForSlot`, `stepPlaybackViewMaintenance` (one grain per turn), `LoopEventStore::initPool` — the existing `test_windowed_preview_integration` harness (`commitLongLoop`, `readyLongLoopPlaying`, `laneTurn`, `drainLane`, `servedEventKeys`).
- **Production paths exercised:** the session-open seam (`openNoteEditSession` → deferred flush → `servicePreviewWindowForNoteEditSession`), the requester, the admission, the fill/order roles, and publication. The fixture does **not** call `requestPreviewWindow` to establish the request.
- **Independent expected content:** hand-written row expectations per scenario (P1/P2/P3); no oracle derived from the implementation's own output. The merged composition rule is used only as a **parity reference** for the interval subset, never as the expectation.
- **RED controls:** (ii) make the empty composition refuse again → P2 RED; force whole-loop coverage → P1/P3 RED; drop the interval filter from the composition → P1 RED with the outside notes present; keep the old window on a delete → P2 stale-retention RED. Each mutation restored byte-identically.
- **Bounded drain:** `drainLane`'s 2 048-turn bound; failure prints the last owner state (`hasPendingPlaybackViewMaintenance`, build phase, ready index).
- **Untestable before link:** none — the slice changes existing functions; no new symbol prevents a pre-fix run. The pre-fix RED is behavioural (`Admitted` vs `NotServable`, interval vs whole loop).

## Verification recipe

- Focused: `pio test -e native -f test_windowed_preview_integration` (16 cases: the 11 existing plus P1–P5),
  `pio test -e native -f test_runtime_working_set_ownership` (the preview cases at the `requestPreviewWindow` call
  sites, including `test_preview_window_is_a_distinct_source_and_never_committed_currency` and
  `test_3_7a_i3_the_capture_admission_counts_committed_windows_only`).
- Full: `pio test -e native` — last recorded baseline **3 352 cases / 7 skipped / 3 345 succeeded / 0 failed** at
  `555354a4` (3.7h); re-measure at slice start, the closeout `02bbf828` is docs-only.
- Builds + RAM1: `pio run -e teensy41-capture-serial` and `pio run -e teensy41-capture-serial-window-qualification`;
  pins to preserve: default `variables 96 192 / code 392 216 + padding 1 000`, qualification `392 376 + 840` (floor,
  `scripts/ram1_budget.json`).
- Specs/diff: `openspec validate windowed-playback-generation --strict`, `git diff --check`.
- **Device:** NOT RUN in this slice — the audible route is unchanged (merged fallback), so a device run would not judge
  this slice; the route's device parity belongs to 3.15f. (Attempted later under the session's HITL authorization: three
  managed NOTE_EDIT compose runs on this slice's image, no leg pass, readings in [`EXECUTION.md`](EXECUTION.md).)
- Evidence destination: this directory (`EXECUTION.md` for the completion record).

## Completion record

| Check | Status | Evidence |
|---|---|---|
| Oracle/contract unchanged | **PASS** | P1–P5 keep the recorded predicates; the executed geometry is the suite's 64-bar root fixture (see the geometry note above), with the same interval shape and hand-written expectations. |
| Fixture implemented (P1–P5) + discriminating RED | **PASS** | `test_windowed_preview_integration`: 5 new cases. Pre-fix RED with the production change stashed: P1 (`Expected 6253 Was 0`), P2 (`Expected 0 Was 2`), P3 (`Expected 3072 Was 0`), P4 (`Expected 14 Was 4`), plus the contract-updated existing fixtures (`test_the_session_owns…`, `test_a_mutation_between_build_grains…`, `test_a_selected_track_change…`, `test_the_session_exit…`). P5 is **GREEN pre-fix by design** — it re-asserts a currency invariant this slice must preserve, not a new behaviour. |
| Focused native | **PASS** | `test_windowed_preview_integration` 16/16; `test_runtime_working_set_ownership` 230/230. |
| Full native | **PASS** | **3 357 cases / 7 skipped / 3 350 succeeded / 0 failed** (`pio test -e native`, 3:19). Baseline was 3 352/7/3 345 — +5 new cases. |
| Both builds + RAM1 pins | **PASS** | default `variables 96 192 / code 392 216 + padding 1 000`; qualification `392 376 + 840` — both exactly at the approved pins. `sizeof(LoopPlaybackRuntime)` re-based and measured (7 896 → **7 904** target, 10 080 → **10 104** host); the delta rule is `TRACK_COLD_MEM` because an ITCM copy cost 400 B of the pinned budget. |
| Strict validation / `git diff --check` | **PASS** | `openspec validate windowed-playback-generation --strict` → valid; `git diff --check` clean. |
| Device | ATTEMPTED — **no pass** | Three managed runs of the registered NOTE_EDIT compose leg on this slice's image (`75d47454` / `2fe13ada…`, track 8 / slot 4 / channel 8): base leg PASS once (`-02`), NOTE_EDIT sessions opened and played with `crash=0` and no ring/heap anomaly in all three, and the leg's same-pitch interaction failed in each (seed re-select; no later same-pitch live note). Failures are interaction/base-grid, not this slice; the composer feeds a lane the audible route does not consume. Readings in [`EXECUTION.md`](EXECUTION.md). 3.15f owns route parity. |
| 3.15d / 2.12 checkoff | NOT ELIGIBLE | Slices 2–3 owed (edge adoption during NOTE_EDIT, short/long parity + lifecycle + 2.12's save clause). |
