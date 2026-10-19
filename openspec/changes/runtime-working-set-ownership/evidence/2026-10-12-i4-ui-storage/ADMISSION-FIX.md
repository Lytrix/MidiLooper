# I4 — revision persistence admission correction

## Authorization and scope

Owner answered **yes** to implementing the narrow firmware admission fix. Branch feature/unified-rolling-playback-path @26125ceb plus preserved dirty work. No upload authorized: ask after successful default capture build. No device action, reset, repeated Save, load, scratch mutation, SD destructive operation or Git delivery in this slice. Original track8/slots4–5 permission remains unchanged.

Active repair belongs to load-save-overlay-display-regression verification / set-revision-persistence3.1 — existing commit FSM and4.2 — Save background continuation. Do not close hardware gates from local checks.

## G0 — current execution evidence

- VERIFIED entry: DisplayManager Save row → beginOverlaySaveRowCommit → requestCommitRevision sets existing revisionCommit.pending and exits overlay. Serial entry and dirty-Yes also reuse this job; clean/dirty-No load uses revisionLoad.pending.
- VERIFIED path: main loop samples hasDeferredSaveWork, guards Persistence idle-owner opportunity and calls processDeferredSaveState. hasDeferredSaveWork → hasPersistenceWorkPending excludes revision commit/load. Existing hasRevisionCommitWork/hasRevisionLoadWork report pending or in-progress, excluding unqueued dirty prompt.
- VERIFIED owner/lifetime: StorageManager storageSession singleton owns all jobs; main only admits service. Snapshot/write/validate/catalog/complete and errors/cleanup remain in RevisionCommit/RevisionLoad; pending is retained until scheduler dispatch, in-progress survives between steps.
- VERIFIED continuations: scheduler refuses boot fault, recording (except one sealed-chunk flush), active load SD and post-load holdoff; workspace save precedes revision dispatch. Main preserves post-overdub/STOPPING and heavy-load-frame denial. No changes to these worker gates.
- VERIFIED handoff: immutable revision/catalog/provenance published only by existing completion paths. Playback/display/undo never become storage owners. No new snapshot references, cancellation behavior, allocator or serialized format.
- VERIFIED bound: main scalar admission structurally constant; existing worker row14 is empirically bounded (ExecutionBounds.md), budgets checked between steps, not a new hard timing guarantee. No new class/owner, no scheduling framework (DEC-027 — deferred scheduler north star, DEC-043 — execution bound property).
- VERIFIED device evidence: checkpoint01 request/exit, no dispatch/complete90s; workspace shutdown writes complete but queued revision does not alone admit another turn. See EXECUTION.md.
- VERIFIED coverage: existing test_storage_drain_restore includes real StorageManager scheduler/revision workers and in-memory SD; test_midi_service_drain exercises owner/denial scalar policies.
- UNKNOWN: hardware step duration/completion on corrected image until authorized upload/HITL; unchanged allocator/SD empirical costs are not proven here.
- CONFLICTS: none for existing-owner admission correction. Broader capture-time commit semantics are outside this slice; no gate weakening.
- Hidden dependency: main predicate must be reusable in native fixture without a copied condition. Extract that scalar admission+dispatch boundary into existing MidiServiceDrain namespace, called by main and instantiated with real StorageManager in the storage fixture. No domain success stub on asserted path.

G0 verdict: PASS (evidence, not implementation approval). Architecture fits existing ownership; no reassessment trigger or new DEC entry.

## G2 — execution contract before behavior edit

Owner: main idle opportunity / StorageManager worker. Primary files: src/main.cpp, include/Utils/MidiServiceDrain.h, existing native storage-drain and MIDI service suites. Prerequisites: public revision work queries, real scheduler and SD harness already exist. No new state/queue/format, priority exception or budget.

Mutation: include workspace OR revisionCommit OR revisionLoad pending/in-progress in existing guarded persistence work predicate. Queued sealed chunks retain their separate unconditional route into worker (which enforces capture-only flush). Workspace priority bypass remains conditioned on original workspace priority and !timingCritical. Never broaden hasDeferredSaveWork itself.

Transition oracle: clean stopped workspace/no queue + preserving Save request → retained pending on denied owner/holdoff → one admitted Persistence turn consumes pending and advances real revision FSM → repeated real owner turns write/validate/catalog/complete and publish exact identity/bytes once; no new request and subsequent idle turn does nothing. Also exercise in-progress-only jobs, revision load after commit with exact test-created ID, denied heavy-frame/STOPPING/post-overdub, workspace coexistence, sealed-chunk route and no-work false.

Fixture substitutions: native clock / in-memory SD / physical outputs only. Existing domain workers real; no hand-modelled revision stages. If exact worker completion requires a prerequisite fixture, arrange it through existing production helpers. No new device stimulus/marker: reuse STIM-I4-CHECKPOINT-01@v1, existing recipe and managed path after upload approval. RAM1 headroom must retain padding>=840; no baseline rebase.

Validation: focused native storage-drain+MIDI service+set-revision suites; behavioral RED on revision-only admission; full pio test -e native; pio run -e teensy41-capture-serial with RAM1 line/hash; focused independent review once; git diff --check and strict OpenSpec validation. Native does not qualify hardware or physical OLED. Commit boundary: local firmware/tests/docs only, no Git delivery.

## Decision review

Searched DECISION_LOG (persistence admission/revision scheduler/DEC-027/DEC-043), active commit/overlay requirements, context-map persistence guides. Reuse YES: existing worker/query/opportunity. No owner transfer, migration, timing policy supersession or unrelated lifecycle change. Guide historic capture claims do not override current exclusion gates; preserve them rather than broadening capture persistence behavior.

## Concrete immediate checkpoint blocker included after stronger oracle

The real admission regression initially passed dispatch/COMPLETE, but independently validating the final packed file failed at its footer. Read-only trace proves `stepRevisionCommitValidate` validates full temp, closes it, then calls `openForRewrite` for a header-only patch. That helper explicitly deletes the existing file; published revision is header-only. This is a concrete blocker of the same preserving-checkpoint acceptance, not speculative scope growth. Include only src/StorageManager/RevisionCommit.cpp header-patch reopen: FILE_WRITE_BEGIN (non-truncating offset0), verify unchanged size and successful seek before header write; all write/validate/catalog/complete stages/IDs/formats and general rewrite helper semantics stay unchanged. Regression must retain final independent header/footerCRC/body-presence/Current payload assertions. No broader SD refactor or device action.

## Results

Implemented locally; **NOT UPLOADED**. `serviceDeferredPersistenceTurn` is used by main and the real storage fixture; workspace OR pending/in-progress revision commit/load is now eligible for the same guarded Persistence turn. No StorageManager workspace-query change. Header-only patch now reopens non-truncating and checks file size/seek. No new state, queue, schema, marker or timing budget.

Verification sequence and honest limits:
- Extracted original admission without behavior correction; two real-SD cases failed at missing revision-only admission (plus test-process SIGINT/ERRORED); existing policy cases passed. Narrow query correction made initial18 storage cases pass.
- Stronger published-file oracle exposed destructive header patch. Non-truncating reopen repaired payload retention. The one-shot wholebuffer CRC helper still disagrees with the pre-existing streamed crc32Continue convention; **shared CRC semantics not changed**. Final oracle independently reads header/footer token/fileSize, recomputes streamed CRC with the reader's quantum, finds both occupied LoopSlot bodies, compares exact Current slot bytes, checks no duplicate commit, and loads exact created S1/v1 through production request/worker/COMPLETE. This is production-reader compatibility, not a new standard-CRC or all-route parity claim.
- Final focused `pio test -e native -f test_storage_drain_restore -f test_midi_service_drain -f test_set_revision_persistence`: **134/134**. Five new cases:2 real worker cases +3 guard matrices.21 host checkpoint regressions pass.
- First full-native run failed on duplicated SetBrowserOverlayPolicy TU in offline-boot wrapper; corrected fixture inclusion using existing RECOVERY_BOOT_SUITE macro (test-only). Affected pair retest18pass/2skip. Final `pio test -e native`: **3077total /3070passed /7skipped /0failed**, exit0,107.230s. No hidden blanket-green claim for the earlier run.
- Final `pio run -e teensy41-capture-serial`: SUCCESS. RAM1 variables96,192 / ITCM393,216 = code392,312 + padding904 (floor840;64Bmargin); locals34,880. RAM2variables163,360; EXTRAM11,680. No floor rebase. Relative to recorded prior image, code+64/padding−64; the slice adds no resident state. Built HEXSHA256 **8ed5a9dc97ae56dc0612808d55f353526025c57d54f856ad1067ebea4e518c6b**, candidate only. Intermediate admission-only image90377442 superseded, neither uploaded. Installed image remains d1b93b86.
- One focused independent static review **APPROVE / no concrete defects**. Reviewer did not run commands. Fixture duplicate-inclusion recovery retested locally without another review round.
- `git diff --check` clean; strict overlay change validation valid (previous archive/sync informational prerequisites unchanged). Before/after Git status compared; unrelated dirty work preserved. No Git delivery.

Scratch evidence: i4-admission-red.txt, i4-admission-focused.txt, i4-footer-diagnostic.txt, i4-admission-focused-pass.txt, i4-admission-reuse-retest.txt, i4-admission-native-final.txt, i4-admission-build-final.txt, i4-admission-host.txt under session scratch. Pending device Save request was not touched/reset/cancelled; no hardware command/upload/MIDI in this slice.

Next: ask for flashing the pinned candidate after safe managed preflight. Installed firmware still has pending/unproven revision; shutdown workspace idle is not empty revision queue. Preserve failed run; after authorization use existing registered checkpoint preset/new capture directory and current OLED observation. All I4 hardware/focus/restore/spinner/load gates remain open. Closeout: runtime/tasks/guide/delivery updated; existing-owner correction only, no new architectural decision or exclusions, so no DEC append required.

## Later owner-approved hardware execution

Owner subsequently approved pinnedupload/checkpoint-only rerun: image8ed5a9dc nowinstalled,
registeredcheckpoint02/live+drainedoracle qualifies exactS0001_v0001 with safe two-drain
shutdown and OLEDvisiblecompletion. Speed remains unqualified: owner says much too long,
device44.656534s Save→complete. No broad I4 acceptance or new source/stimulus/budget change.
[I4 — upload/checkpoint evidence](UPLOAD-CHECKPOINT.md).
