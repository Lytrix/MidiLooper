## Context

See [proposal](proposal.md) for motivation and [delta](specs/timeline-passes/spec.md) for acceptance. Baseline: `7c3f69ff`, branch `feature/unified-rolling-playback-path`; only pre-existing untracked session metadata/export files before planning. No firmware edits or device actions in this planning session.

**Current closeout:** owner/manual device acceptance confirms instant undo/redo repaint after reboot; session174940 corroborates eleven gestures with changing clean cache/paint counts and no transport restart. Native/build/review gates remain valid; no code changed for closeout. Scoped tasks complete, unarchived. This is not automated device inventory/timing or universal qualification. [Latest acceptance and limits](evidence/DEVICE-174940.md); [follow-up packet and regression](evidence/DEVICE-171433.md); [source-reuse checkpoint](evidence/CONTINUATION.md).

## Scope traced — G0 evidence

### Current execution path and state-transition map

**VERIFIED:** `TrackUndo.cpp` routes global overdub undo through `applyUndoEntry`: disable its capture-pass IDs and companion edit IDs, refresh playback, call `refreshVisualCacheAfterPassStateChange`, then `applyPublishedSnapshotSelect`. Redo symmetrically enables those IDs. Captured published roots are optional; `requestPlaybackSourceCurrency` requests ordinary deferred republish/rebuild when the selected root is not current. Undoable/redoable content is retained, not deleted by the gesture.

**VERIFIED:** `Loop::refreshVisualCacheAfterPassStateChange` marks stale and services at most four four-bar slices. In the supplied reboot segment these slices finish **before** the publication request: 204 → 171 notes, `dirty=0`, at 12.660 s. `DisplayManager::resolveDisplayNotesByDrawSource` then reports `n=204,st=ret,why=hold_gap`. The normal repaint loop is alive; cache rebuilding is not the missing edge.

**VERIFIED:** `displaySourcePublicationPending` treats a rootless session-exit publication as a gap. `resolveDisplayNotesByDrawSource` requests a qualified replacement only when the held interval differs from the requested interval. A same-window undo thus retains the old frame despite new cache content. `holdRetainedFrame` also prevents updating its content key. The same branch is used after an ordinary overdub, so a global disable of publication-gap holds would regress Record/overdub handoff.

**VERIFIED:** `Loop::collectQualifiedCommittedDisplayNotes` already selects bounded continuation, retained envelope, prepared window or revision-qualified covered cache. It currently returns `false` for empty results, so it cannot by itself distinguish authoritative emptiness from unavailable content.

### Ownership/lifetime map

- `TrackUndo` owns the editing derivation; `Loop` owns passes, selected root and pending publication.
- `Loop` owns cache revision, built-from revision and dirty-bar coverage. `DisplayManager` owns retained frame, committed prefix, compose scratch and source key.
- `StorageManager` owns parse/session data until `applyLoadedLoopSlotBodyCommit` applies it. `LoadLoopJob` then drops the transferred shell without releasing adopted chunk references. Actual adoption is `Loop::adoptPersistedSnapshot`, reached through `applySnapshotToLoop`.
- `Track::retainPlaybackSourceForSlot` owns up to two existing retained descriptors per allocated runtime; each pins pass IDs and retains chunk references. A new revision marks the former Serving descriptor Retiring. No free descriptor returns `RoleCapacityReached`.
- `tryReleaseRetainedPlaybackSource` excludes current/newer revisions, published-window references, builds and finalization, then releases chunk refs. The reverted admission-path call is absent at this baseline.

### Deferred-work map

Undo/redo queues snapshot currency work, serviced by the existing Snapshot owner; cache refresh and display updates are separate. Playback source admission/build/adoption and source retirement are separate operations. Saving remains the existing deferred request/drain path. This change adds no queue, worker, root lifetime or SD representation.

### Consumers and alternate paths

Display reads its qualified committed source plus accepted preview overlays, never playback `mergedEvents`. Playback reads its own window, never piano-roll notes. Root-backed, stale-root and rootless bodies, stopped undo followed by Play, undo during Play, same-window repaint, scrolled viewport, empty interval, live Record publication gap and successor overdub are separate required cases.

### Resource/timing consequences

`ExecutionBounds.md` classifies display resolve as **empirically bounded** (existing 5000 µs measurement/overrun reuse), draw/transfer as fixed-size structurally bounded, and snapshot construction as structurally bounded. No class promotion or numeric timing claim is made. Display repair may read qualification metadata and reuse existing note compose storage; it must not add pass traversal, full materialization or snapshot construction on paint/MIDI/gesture edges. Preserve window-sized working sets, existing `activeLoopIndex` authority, configured track/slot counts and tempo paths. No new resident fields, pool roles or RAM1 allocation are planned; measure firmware RAM1/ITCM rather than assuming neutrality.

### Existing validation and hidden dependencies

`test_live_capture_display_boundary` includes real draw-source/display production translation units, but does not currently exercise `TrackUndo` plus saved-slot parsing as one chain. `test_runtime_working_set_ownership` supplies playback/undo infrastructure, and `test_storage_loop_io` covers parse/apply. Their existing stubs are not proof of a restore→undo integration path. A shared production-path test fixture must link real owners on every asserted leg; success stubs for undo rebuild/load/adoption are forbidden there.

### VERIFIED / ASSUMPTIONS / UNKNOWNS / CONFLICTS

- **VERIFIED:** display holds a pre-undo frame after its cache is clean; two-role refusal exists in current code; DEC-205 was reverted twice and must not be reapplied.
- **ASSUMPTION:** the owner's redo silence is the same role-capacity shape as the historical native reproduction.
- **UNKNOWN (blocking redo repair):** exact loaded-chunk reference/alias sequence responsible for the rejected release regression. The supplied reboot segment contains no redo gesture. Neither descriptor count nor a third role is an accepted solution.
- **CONFLICTS:** no authority change is proposed. The former frozen display scope did not authorize this repair; the user explicitly selected a new focused scope for both symptoms in this session.
- **G0 verdict:** display boundary traced, production fixture still owed; redo repair BLOCKED on loaded-slot attribution. Only its acceptance/characterization fixture is implementation-ready. No redo production implementation tasks are generated here.

## Goals / Non-Goals

**Goals:** fix qualified same-window display replacement; establish a discriminating saved-slot oracle for redo without reusing the unsafe release; keep fixture/native/build/device status separate.

**Non-goals:** see proposal. In particular, do not infer a chunk refcount defect merely from the prior device report or “fix” it by changing reference accounting without a loaded-slot trace.

## Decisions

### Prefer a qualified current replacement over an obsolete held frame

On a publication-gap handoff, permit a complete current committed source to replace the held frame for the requested interval even when that interval did not move. Reuse the existing atomic candidate→compose→frame swap and overlay application exactly once. No unpublished snapshot is a source.

Reuse `Loop`'s existing cache qualification conditions (built-from revision plus requested-window dirty-bar coverage), exposing a const metadata query if needed. A qualified cache containing zero notes is a successful empty replacement; an unqualified cache leaves the existing fallback intact. Do not change the existing multi-provider collector's `false` semantics indiscriminately: other consumers depend on it. For a cache-qualified replacement, filter only the existing display representation, not committed MIDI storage. The query must reject partial coverage and stale revision, including an empty stale cache.

**Apply prerequisite (owner-approved 2026-10-29):** the final slice currently passes `false` for fallback coverage, so a complete rootless pass-state refresh has correct notes but no current `builtFromRevision`. Certify that existing revision only after all dirty bars clear and actual PassesResolve plus its admitted copy completed without projection refusal or rejected-overlay provenance during the explicit pass-state refresh. Do not qualify merely because the optimistic fallback-complete local started true; do not stamp partial, denied or rejected work. This is an existing owner's metadata correction, not a new state/lifetime model. [Apply evidence](evidence/APPLY.md).

Preserve track/slot identity, requested interval, current key and prefix bookkeeping on adoption. Preserve existing live Record/overdub holds and overlay-only growth where no complete current replacement exists. A fixture must prove exactly which qualified source replaces the frame; a lower note count alone is insufficient.

Adoption additionally requires successful admitted filtering/copying into compose storage. Allocation refusal retains the old frame/key and cannot be presented as an authoritative empty result. Revalidate qualification for the current track/slot/generation after deferred invalidation, including clear/load replacement, transport/clock changes and root completion; never retain a pointer across display turns. See [contract review](REVIEW.md).

Alternatives rejected: unconditional frame clear (blank/flicker and partial content); bypassing every gap hold (breaks Record continuity); waiting for a whole root (unnecessary delay already observed); another whole-loop cache rebuild (the cache is already correct).

### Redo: fixture first; no speculative resource repair

Use a real host serialized slot body and production deserializer/apply/undo derivation, not manually assigned committed-chunk IDs. Capture chunk reference counts, lifecycle/readability, retained roles/revisions, source-token changes and emitted event tuples before/after every gesture and service boundary. Reproduce the third-revision capacity refusal and distinguish payload loss from alias-token invalidation or missing adoption. A failure must name its first bad boundary.

The initial attribution packet did not authorize a production repair. On continuation the owner explicitly approved the narrow same-inventory descriptor-reuse contract below. Third source roles, admission-path chunk release, refcount changes and a new ownership model remain forbidden.

## Continuation evidence — G0 source turnover

**VERIFIED:** the existing retirement method has no production caller. On actual loaded content, invoking it explicitly outside admission releases one descriptor reference, leaves every live pass payload readable with positive refcounts and changes the global alias token. A subsequent redo then serves all expected events. This native observation does not establish the cause of the historical device failure and does not authorize reconnecting the rejected release call.

**VERIFIED:** the two Record failures were fixture prerequisites: source body LoopId700 disagreed with Track's slot descriptor, so the first stop worker rejected its job as stale. Matching the source body to `Track::loopIdForSlot` admits the real job; its stored pass contains both accepted rows. Effective-query readiness follows the separate ordinary publication owner. Driving that owner before querying preserves the same expected two-event result. Both untruncated/truncated Record controls now PASS with no Record firmware edits. Older baseline-failure conclusions did not identify a product defect and are superseded by this attribution.

**G0 verdict:** PASS for the following narrow same-owner repair. No physical payload-loss, new resource/lifetime mechanism or persistent format is inferred. Scope, paths, owners and RAM1 constraints remain as above. Remaining source mismatches/readers retain the existing capacity refusal, not a new queue or silent overwrite.

## Execution packet — same-inventory source descriptor reuse (owner-approved)

- **Owner:** Track source admission; two existing `RetainedPlaybackSource` descriptors and their original owned chunks. No body ownership transfer/release/re-retain occurs on reuse.
- **Primary files:** `TrackPlaybackWindowBuild.cpp`, `Track.h`; saved-slot/native source-lifecycle fixtures. Existing merged/window consumers and release API remain the owners they already are.
- **Preconditions:** no free descriptor; one older **Retiring** descriptor passes the existing no-live-reader retirement predicate; the Track/slot is not capturing, overdubbing or owning finalization, no view/bridge build, retained gather or coverage-source borrow remains; exact ordered capture-pass IDs, edit-pass IDs and all committed chunk IDs match the current slot. State flags may differ (undo/redo); stored rows/bodies may not.
- **Reader tightening:** the existing release predicate's coverage-build test alone excludes some generic/counting states. Reuse separately excludes active primary/bridge builds, retained gather and owned coverage-source borrow, and any complete current/ready/retained view naming the candidate revision. Physical release retains its original behavior; only the existing pure terms are shared.
- **Mutation:** share the existing pure retirement eligibility predicate with the release method; admission may reuse an eligible same-inventory descriptor by updating its revision/generation/role and rebuilding the existing interval index. Keep its pins/chunk ownership unchanged. Mark the former Serving descriptor Retiring through the existing handoff. Never call release from admission, clear a body list, or increment references on reuse.
- **Postconditions:** repeated state-only undo/redo can admit a current source without exceeding two roles, changing the alias token, freeing chunks or growing refcounts. A still-needed descriptor and every mismatching inventory remain unchanged and return the existing `RoleCapacityReached` outcome. Index/source currency agree at the normal publication point.
- **Failure/cancellation:** reader/capture/build/finalization/membership/slot replacement refusal leaves old identity, pins, ownership and index intact. Existing callers retain their existing capacity/deferred behavior; this packet adds no request carrier. Current/older-version readers are never re-labelled while their source is in use.
- **Resource/timing:** exact metadata comparison is linear in the already-owned descriptor ID lists, with at most two role candidates. It allocates nothing; reuse avoids the old descriptor/vector/chunk-retain work. Existing index rebuild is unchanged. No new resident field, new Manager, new synchronous caller or capacity claim; admission remains part of the existing unbounded window-build owner, not a new timing guarantee. Measure build RAM1/bank line and leave device timing open.
- **Oracle:** corrected real saved-slot rooted/rootless fixtures play one complete original cycle, then eight undo/redo cycles with a simultaneously sounding control; exact expected MIDI tuples after each edge. After two initial role admissions, assert bounded two-role count, unchanged chunk refs and alias token across reuse, stored payloads intact and desired revision/index current. Negative cases protect published window, active build/gather/source borrow, finalization, live capture/session, changed pass/chunk membership, changed edit IDs and slot/track identities. No manufactured ready root in the restored target.
- **Fixture-first baseline:** the repeated playback tests already RED at rootless first redo and rooted second undo. Add explicit reuse invariants and negative cases before the mutation; do not lower counts or accept silence/stale content. The corrected Record identity/readiness controls are required regression gates, as are ordinary fresh Record and successor overdub controls.
- **Validation:** focused reboot, runtime working-set, continuous capture, shared finalization, storage and display native suites; full `pio test -e native`; `pio run -e teensy41-capture-serial` and exact HEX/RAM1 evidence. One focused independent review for this new lifecycle slice; fix/retest concrete findings. Native content proof is not device timing or device acceptance.
- **Commit boundary:** independently reviewable source-reuse implementation and fixtures on working branch, preserving prior display work. No automatic commit, push, upload or SD action.
- **Status:** implemented and independently reviewed; focused644/644, full native3629 pass/7 existing skips, final expanded reboot30/30; clean build/RAM1/hash verified. Upload/device NOT RUN. [Evidence](evidence/CONTINUATION.md).

## Execution packet — display replacement

- **Status:** implemented/native verified, including the additional scrolled-window/slot/load/save-active controls; clean build verified. Device qualification remains open. [Current results](evidence/CONTINUATION.md); original REDs and review-finding retest remain in Apply evidence.
- **Owner/files:** `DisplayManager::resolveDisplayNotesByDrawSource`, existing `Loop` cache qualification; `DisplayNoteWindowGather.cpp`, `LoopVisualCache.cpp`, `Loop.h`, existing display/undo/storage test support as needed.
- **Preconditions:** loaded rootless slot; pre-undo frame; actual global undo; current covered cache produced by ordinary refresh; pending replacement publication. Include the production fixture linkage as part of this slice before behavior edits.
- **Mutation:** qualify and adopt current cache/source for the requested window before retaining an obsolete frame; represent qualified empty distinctly from source unavailable.
- **Postconditions:** next completed display resolve/compose has exactly the expected surviving notes, correct generation/slot/window key and no stale prefix; redo symmetrically restores notes when available. Root remains unpublished until its ordinary worker completes.
- **Must preserve:** Record/overdub gap fallback, partial-cache rejection, overlays once, no pass changes from display, existing allocation/admission and frame-in-progress rules.
- **Oracle:** deserialize an 8-bar body with disjoint record/overdub notes (fixed IDs/ticks/channels/velocity), destroy staging; render both; global undo; assert disabled pass and unchanged stored payload; assert refreshed cache's literal expected survivors; with publication withheld, complete a normal display turn and compare exact DisplayNotes/key; redo and repeat. Include zero survivors in viewport, stale/partial/denied cache, 24-bar scrolled viewport, alternate track/slot and live Record/successor controls. Observe before/after each owner turn.
- **Outcome semantics:** current covered cache = ready, including empty; stale/partial/unavailable = waiting and existing coherent fallback; slot/generation replacement = no cross-slot adoption. Do not relabel a fallback frame as current.
- **Controls:** existing host clock, I/O and display-test access; withhold publication through controlled worker stepping, not sleeps. At most 4096 service grains per test drain; exhaustion reports revisions, slot, window and dirty coverage. No stubbed success on parse/apply/global undo/resolve.
- **Validation:** demonstrate pre-fix same-window RED; focused `pio test -e native -f test_live_capture_display_boundary -f test_runtime_working_set_ownership -f test_storage_loop_io`, plus full `pio test -e native`; `pio run -e teensy41-capture-serial` with RAM1/ITCM/hash. Record exact suite/case counts and red/green evidence under `evidence/`.
- **Review/commit:** focused independent review per `docs/Agents/reviewer.md`; display fixture+repair is one independently buildable working-branch boundary. No commit/push requested in this session.

## Execution packet — loaded-slot redo attribution

- **Status:** attribution complete. Original rooted/rootless REDs and Record fixture mistakes are recorded; corrected Record controls and eight-cycle rooted/rootless parity now PASS under the separately approved same-inventory repair. Historical failures do not remain product diagnoses.
- **Owners/files:** real `StorageLoopIo` parse/apply, `Loop` adoption, `TrackUndo` editing derivation and `Track` playback entry/maintenance. Extend existing native harness/support only; no production behavior mutation in this packet.
- **Oracle:** serialize fixed record+two-overdub content to host memory/files, release original owners, parse/apply into fresh Track/Loop, release staging, reconstruct undo via production; first window must emit literal expected tuples. Undo latest overdub → base+earlier overdub; redo → all; repeat eight times with advancing clock/worker steps. Compare per-cycle note pairs/count/IDs/timing, not just `isPlaying`. Exercise passes-only and parity-accepted root body; retain a control unrelated sounding slot.
- **Independent invariants:** pass IDs/state/order/geometry and chunk bytes preserved; live ownership references never zero; deferred parser/old runtime teardown cannot release current content. Log actual role/refcount/token transitions; do not prescribe assumed absolute counts across ownership layers. Check the full original cycle before any undo to catch the prior restore regression.
- **Safety controls:** run real Record start/stop on separate fresh/restored fixtures and ordinary successor overdubs; distinguish explicit replacement Record semantics from source-maintenance data loss. No live device recording or card mutation.
- **Outcome:** baseline RED at a named playback boundary or honest non-reproduction; never mark the repair implemented because a characterization names `RoleCapacityReached`. Preserve the exact failing trace for the follow-up design. Test-only failure must remain visible, not silently skipped or treated as suite success.
- **Validation/commit:** focused native production-path suite plus storage/display controls and full native; a fixtures-only working-branch boundary. Shared fixture prerequisites precede the display repair if linking requires them. Mutation tests of rejected code belong only in isolated scratch copies and must not enter production.

## Risks / Trade-offs

- Qualified empty versus unavailable is easy to conflate → explicit metadata qualification and stale-empty negative case.
- Broad handoff relaxation can regress capture continuity → limit to current complete source; run existing Record/overdub/display matrix unchanged.
- Native storage/global-undo stubs can manufacture a pass → real linked owners, staging destruction and first-window oracle required.
- Additional source roles mask lifetime defects → forbidden in this change until a separate reviewed contract.
- No device timing proof follows from native content parity → retain hardware gate and ask before upload.

## Migration Plan

No format or user-data migration. Display and source-reuse slices remain separately reviewable on the working branch. Both are implemented and native/build verified. Any firmware upload still requires explicit session authorization; hardware recipe/stimulus/target registration must precede device actions. Roll back only this change's code if needed, never reset user data or replay the rejected admission release.

## Session closeout

Scope authorization: user selected both symptoms with saved-slot fixtures. Architecture-changing redo questions are explicitly blocked, not silently delegated. No firmware/test edit, build, native run, upload, serial command, SD operation, commit or delivered fix in this planning phase. Runtime docs point here. Existing DEC-205 exclusions stand; no new architectural model or supersession is approved.
