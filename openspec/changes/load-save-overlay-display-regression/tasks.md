## 1. Investigation baseline

- [x] 1.1 Document root cause: overlay SD reads vs persistence concurrency; missing load-complete exit; boolean mode composition (see `design.md`).
- [x] 1.2 OpenSpec change `load-save-overlay-display-regression` with proposal, design, specs.

## 2. Persistence SD gate (minimal fix)

- [x] 2.1 Add `StorageManager::isOverlayCatalogReadAllowed()` (queued + in-progress + `*SdIoActive`).
- [x] 2.2 Guard `refreshLoadSaveListCache`, `refreshLoadSaveRevisionHistoryCache`, and `resolveLoadSaveWorkspaceDetail` with gate API.
- [x] 2.3 Defer overlay-enter list refresh in `DisplayManager::update` when gate is false; use stale cache if valid.
- [x] 2.4 Native unit tests: gate false during each job type; true when all idle.

## 3. Load-complete lifecycle

- [x] 3.1 On `stepRevisionLoadComplete`, call `exitLoadSaveMode()` when overlay is open.
- [x] 3.2 Verify `consumeRevisionLoadDisplayRefreshPending` + cache invalidation path runs after auto-exit.
- [x] 3.3 Extend native or HITL verify: `LDSV` 1→0 after `rev_load_complete` without manual overlay close.

## 4. Overlay persistence phase (FSM)

- [x] 4.1 Add `SetBrowserOverlayPersistencePhase` (or equivalent) with transitions in StorageManager dirty/load/commit paths.
- [x] 4.2 Wire `getSetBrowserOverlayMode()` minimal branch to phase instead of `isMinimalLoadingOverlayActive(bool×5)`.
- [x] 4.3 Skip `resetSetBrowserOverlayNavigation()` on overlay enter when phase ≠ Idle.
- [x] 4.4 Update `test_set_revision_persistence` phase/minimal-mode tests for new API.
- [x] 4.5 Remove or thin `RevisionLoadPolicy::isMinimalLoadingOverlayActive` if fully superseded.

## 5. Display polish

- [x] 5.1 Clamp persistence dot Y in `drawLoadSaveMinimalLoadingView` / dirty prompt (no negative coordinates).
- [x] 5.2 **I4 — preserving checkpoint blockers, local repair (2026-10-12):** existing main persistence admission now includes revision commit/load pending/in-progress without changing workspace-only queries or timing/owner/chunk guards. Real Save→commit→exact created revision load fixture RED→GREEN; header-only patch now non-truncating after stronger final-file oracle proved payload deletion.5newcases/134focusedpass/fullnative3070pass7skip0fail, defaultcapturebuildSUCCESS/RAM1padding904>=840, one static reviewAPPROVE. BuiltHEX8ed5a9dc NOT uploaded; device6.2–6.4 remainopen. [I4 — admission repair evidence](../runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/ADMISSION-FIX.md).
- [x] 5.3 **I4 — live overlay observation prerequisite, local only (2026-10-12):** capture-only request-only directscalarOVERLAY_STATUS/fenced host query replaces ring-dependent navigation guards; TX pressure refuses without stalequeue, playing no-USBflush/stopped-onlyTRACE_DRAIN retained. Nativecapture24/fullnative3078pass7skip/75hostpass, defaultcaptureSUCCESS/padding1160>=840; one focusedreview's vacuousguardtest corrected/retested, no production defect. DEC-119 — request-only live overlay observation. Candidatec455babb NOT uploaded; no device action or hardware6.2–6.4 checkoff. [I4 — observation evidence](../runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/PLAYING-OBSERVATION-FIX.md).
- [x] 5.4 **I4 — Set/history visibility after boot (narrow hardware prerequisite):** run03 on metadataHEXecd4f671/freshmanagedboot reachesSetrow2→Set1history; ownerconfirmsS0001/v0002 visible/responsive/noLoadcompleted. Canonicalrun03drains2complete; offlinechecks31native/78hostPASS. ClosesmissingSet/browservisibility only, notdirtyYesload/minimalUI/fullI4;6.2–6.4 stayopen andrun04incomplete. [I4 — visibility closeout](../runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/SET-BROWSER-VISIBILITY-CLOSEOUT.md).
- [x] 5.5 **I4 — capture journal boot blocker, local repair:** bootoncefirstusemetadata initialization andboundedindices/existingcritical-lossreporting, reinit/partialreplay retention.57focused/3098fullnativepass7skip/78host/defaultcaptureSUCCESS/RAM1padding1000unchanged≥840; onefocusedreviewAPPROVE. DEC-121 — boot-local capture journal initialization. Image8050aabc subsequentlyuploaded/bootqualified separatelyin5.6;6.2–6.4 remainopen. [I4 — journal fix evidence](../runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/BOOT-JOURNAL-FIX.md).
- [x] 5.6 **I4 — bounded boot recovery after journal repair:** approved8050aabc/defaultupload/hashunchanged, bootsetup/commit/censusready; originalearlyquery01FAIL retained, separate90s02/16validpairs/advancinguptime/no newreset-reconnect-loss/fullheap/drains2/nolatchPASS. OwnernormalstableOLED/no rebootloop. NoLoad/content/SDcleanup;6.2–6.4 remainopen. [I4 — recovery evidence](../runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/BOOT-JOURNAL-RECOVERY.md).

## 6. Verification

- [x] 6.0 **I4 — checkpoint-first setup (2026-10-12):** owner-approved defaultHEX8ed5a9dc upload; registeredcheckpoint02 creates exactS0001_v0001, freshlive/drainedoraclePASS, canonicaltwo-drainscomplete/no latch. OwnerresponsiveOLED/saveindicatorcomplete. **Speed NOT qualified:** Save→dispatch2.235ms, completion44.656534s; owner considers wait much too long. No record/clear/slotchange/load/cleanup; does not close6.2–6.4 or save-status-display4.3 — PLAYING spinner. [I4 — checkpoint device evidence](../runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/UPLOAD-CHECKPOINT.md).
- [x] 6.1 `pio test -e native` — gate + phase tests green. **Verified 2026-10-12 during I4 — UI/storage qualification:** full native3,072total /3,065passed /7skipped /0fail, including gate/phase suite. Native only; device6.2–6.4 stay open. [I4 — UI/storage evidence](../runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/EXECUTION.md).
- [x] 6.2 HITL: MIDI overlay load + dirty Yes path qualified with registered managed `i4_overlay_load` run06, exact v2 owner proof, preservingv3→v2loadcomplete→autoexit; strictdrainedandlegacyoverlayreplaysPASS/drains2complete. [I4 — playing-load evidence](../runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/PLAYING-LOAD-QUALIFICATION.md).
- [x] 6.3 HITL: `revision_load_dirty` **Yes** regression qualified: separatelyprovencheckpointv2 setup + freshdirtyprompt/Yes/newcommitv3/exactloadv2; strictorderedoracleandlegacydirty-YesreplayPASS. No/Cancel variants not claimed. [I4 — dirty-Yes evidence](../runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/PLAYING-LOAD-QUALIFICATION.md).
- [x] 6.4 Manual smoke: fresh7/3PLAYING/global1 beforeenter/Load; owneractualv2highlightapproved, responsiveminimalprogress/nofreeze-reboot/autoexit/normalpianorollreturnedwithouttrackchange. [I4 — physical playing-load witness](../runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/PLAYING-LOAD-QUALIFICATION.md).

**2026-10-12 I4 — UI/storage setup attempt:** registered managed `i4_checkpoint` / STIM-I4-CHECKPOINT-01@v1, installed HEXd1b93b86 reused/no upload. `captures/i4-checkpoint-t8s4-01`: fresh Save-row MIDI enter/row/confirm/request/exit, but no revision dispatch/completion within90s; checkpoint identity unproven, so no scratch mutation or load. Owner observed Save opened/closed and normal panel responsive (not PLAYING load smoke). Main persistence admission omits revision-only work (`hasDeferredSaveWork` is workspace-only); firmware correction not authorized/implemented in this host slice. Canonical shutdown complete/drains2/oktrue/no latch is workspace idle, not empty revision queue.21new checkpoint tests/91focusedpass, one review's concrete findings fixed/retested. Load/dirty/manual tasks remain unchecked; no blind Save retry/reset.

**2026-10-12 follow-up:** owner authorized firmware correction. Both immediate checkpoint blockers repaired and verified locally as5.2 above; upload still awaits separate approval. Prior device failure remains valid for installedd1b93b86; no corrected-image hardware result or overallI4PASS.

## 7. Closeout

- [x] 7.1 Update `docs/Plans/set_revision_persistence_handoff.md` regression note — run06 playingdirtyYes qualification/scope/evidence added, catalogpathnote reconciled.
- [ ] 7.2 Archive or merge deltas into `set-revision-persistence` on green HITL.

**2026-10-12 I4 — observation upload follow-up:** Ownerapprovedc455babb/defaultuploadSUCCESS/hashunchanged/padding1160≥840. Managedpre/postdrainscomplete2/nolatch; stoppedfreshOVERLAY_STATUSclosedidle/settledheapinspectioncomplete/bootreadinessobserved. Ownernormalpanel/no fourdot overlayproblem. No actualplayingnavigation/Load/content/SDdestructive/Git action. Local5.3 prerequisite nowinstalled; device6.2–6.4 remainunchecked. [I4 — upload evidence](../runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/OBSERVATION-UPLOAD.md).

**2026-10-12 I4 — playing-load run02:** Registeredmanagednopreset/noreset/c455babb, freshPLAYING/openroot/18livequeries/ownerresponsive/nofourdotobstruction. No Setrow: secondscroll received/clampsCurrent1(cachedSetCount0); guardrefusesbeforehistory/identity/Load/dirtyYes/Save. Exit2/drains2complete/nolatch. Newobservation worksduringplaying;6.2–6.4 remainunchecked (no actualLoad/minimalloadscreen). Catalog/cache/index/metadata causeUNKNOWN, noSDlossclaim/firmwarefix/upload/reset/clear/Git action. [I4 — run02 evidence](../runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/PLAYING-LOAD-RUN02.md).

**2026-10-13 fullplaying-load attempt:** Newstimulusnotstarted: managedno-resetpreflight01/02 lacksfreshscalar/heapreplies, USBreconnect/bootoutput; ownerconfirmscontinuousrebootloopaftertitle. CauseUNVERIFIED, noLoad/Save/reset/upload/SDdestructiveoperation. Teardowncomplete2/nolatch notstablebootproof.6.2–6.4 remainBLOCKED/open; [I4 — preflight evidence](../runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/FULL-LOAD-PREFLIGHT-BLOCKED.md).

**2026-10-13 boot diagnosis:** Passive5identicalCrashReports pinpointstrnlen; ELFonly3callersallcaptureowner. CriticalJournalRAM2NOLOADnotinitializedproduction, poisonedinitprobeFAIL/unboundedtrustedindex. Verifiedinitdefect/highconfidencejournalfaultcause; exactLR/indexunknown. Nofirmwarefix/upload/Load/SDcleanup;6.2–6.4 remainblocked. [I4 — diagnosis evidence](../runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/BOOT-LOOP-DIAGNOSIS.md).

**2026-10-13 I4 — requestedplaying-load qualification complete (dirtyYes):** Registeredmanagedrun06/8050aabc/nopreset/noreset/freshPLAYING/owneractualv2identityproof, preservingv3→exactv2load/autoexit, strictdrained+overlay+dirty-YesreplaysPASS; ownerresponsiveminimalprogress/nofreeze-reboot/normalpianorollwithouttrackchange. Finaldrains2complete/nolatch,78host/63nativefocusedPASS.05timeout/lateapprovalkept;06newnonceonly.6.2–6.4/7.1checked;7.2archive-sync stillopen (knownprerequisiteINFO/noarchive), broaderNoCancel/clean/defaultTransport/focusrestore/contentparity unclaimed. [I4 — finalqualification](../runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/PLAYING-LOAD-QUALIFICATION.md).
