# I4 — playing overlay observation fix

## Scope / authority / decision review

Owner approved investigating and implementing a narrow playing-safe observation fix after overlay run01 aborted before Load. No upload, new device run, reset, dismissal, content action or Git delivery authorized. Existing overlay verification tasks6.2–6.4 remain open. Authority: PROJECT_INTENT music-keeps-running, ARCHITECTURE_RULES existing-owner extension and ExecutionBounds row18; IMPLEMENTATION_READINESS execution packet. Searched DECISION_LOG for capture ring/TRACE_DRAIN/capture drain/bounded serial; no decision authorizes changing playing flush policy. Reuse YES: capture-only `processLiveObservationCommand`, direct TX-room-gated scalar response, managed nonce/end-nonce fenced request and strict parser.

## G0 — discovery (PASS, not implementation approval)

VERIFIED: `emitCapPrintf` queues LDSV/OVLY/PERS; `main` uses `flushCaptureBuffer(8)` while any track is recording/audibly playing; that path performs no USB write and preserves Tier-A. `!TRACE_DRAIN` is stopped-only. Run01 produced LDSV1 at1596446817 but it appeared during teardown, after navigation wait refused. `!TRACK_STATUS`/TRACE_FENCE are direct scalar requests; no ring fallback under TX pressure. Existing overlay mode/row/dirty target are owned by StorageManager/DisplayManager/LooperState and can be read without SD or cache refresh. Command owner runs from main, not clock ISR. Publication/persistence/undo unchanged; no new state, lifetime, continuation, queue or resource owner.

UNKNOWN: actual USB backend latency and OLED pixels/responsiveness; native proof does not establish either. No architectural conflict: reuse existing observation child rather than weaken playing no-Serial ring flush. History identity remains owner-confirmed; snapshot set/row is not proof of highlighted revision pixels. Snapshot closed state is not load acceptance.

## G2 — execution contract fixed before production edits

Owner: existing capture-only live command dispatcher + DebugSessionCapture emitter; host managed capture owns serial, protocol parser owns framing. Primary files: HitlLiveObservationCommands, StorageManagerHitlSerial, DebugSessionCapture/header, capture_session allowlist, managed_capture/protocol, i4_overlay_load and existing tests.

Input/transition oracle: retained critical ring + host fenced `!OVERLAY_STATUS` -> one fresh scalar response describing open/mode/row/set/rev/phase; no ring/content/navigation/persistence/undo mutation. Snapshot reads existing getters only, no catalog scan/read/refresh. Backpressure -> no output and no deferred/stale reply; next request reflects current scalars. Exact command only; malformed/duplicate/incomplete responses rejected. Response capped at128 bytes; one request-only row, no continuous telemetry/new Tier-A family. USB backend remains outside structural timing claim, as for TRACK_STATUS. Main <=8 flush and stopped-only TRACE_DRAIN preserved.

Host: fresh overlay query before any gesture requires closed; each navigation gesture awaits corresponding fresh query state under existing8s bound, with missing/stale/wrong mode/row/target refusing. Fresh current history2/row0/drilledSet1 before owner proof and Load; dirty1 target1/rev2 before selecting Yes; fresh dirty row0/target again immediately before Yes. Owner nonce/session/label mandatory, never replaced by query. Completion uses original exact dirtyYes/newcommit/load/autoexit oracle; no fabricated CAP events or snapshot-only PASS. No hand-rolled MIDI, new catalog index guessing, transport stop workaround or canonical stimulus edits.

Controls: existing real capture ring fixture substitutes USB only; fixture cases for retained ring, exact dispatch, TX127 refusal/TX128 acceptance and maximum-width response. Host parser/broker/fenced managed-method tests plus navigation tests with no live CAP receipts, stale/wrong query, changed preconfirm state. New API cannot behaviorally run before linked; pre-fix unknown-command dispatch test provides behavioral RED. No synthetic PASS for missing symbol.

Verification: `pio test -e native -f test_capture_ring`, focused host suites, full `pio test -e native`, `pio run -e teensy41-capture-serial`, RAM1 padding>=840 without rebase; one focused independent review, strict OpenSpec/diff hygiene. Evidence logs in session scratch. Hardware NOT RUN pending separate upload approval; installedcdfe4b51 unchanged.

## HITL_TEST_RECIPE — observation addition

Reuse registered STIM-I4-OVERLAY-LOAD-01 and existing managed/actions, no new stimulus. New row `#OVERLAY_STATUS` is a request-only direct protocol response (like #TRACK_STATUS), not CAP/Tier-A telemetry: no ring fallback/tier registration, no per-event rate. Existing LDSV/OVLY/PERS oracle unchanged. Native tests precede build; instrumentation SESSION_CAPTURE-only, flash-placed command/emitter, no new RAM/PSRAM store. Before/after RAM1 and verification results recorded below. Device entry remains no-preset/no-reset registered scenario only after separate image approval and panel readiness; no run in this slice.

## Completion record

Implemented per contract; DEC-119 — request-only live overlay observation records the existing-owner extension and excluded playing ring drain. No firmware lifecycle/content/persistence/undo/navigation mutation or new runtime storage.

- Pre-fix production dispatch regression: `i4-overlay-observation-native-red.txt` proves `live overlay scalar command must be recognized` FAILED (20otherspassed; runner additionallySIGHUP/ERRORED). Unknown command was a behavioral RED, not missing-symbol proof.
- Focused `pio test -e native -f test_capture_ring`:24/24pass, including direct framed response, retained ring, TX127 refusal/TX128 current-state retry, maximum-width output cap, existing no-USB playingflush tests.
- Final host command `PYTHONPATH=scripts .venv/bin/python -m unittest scripts.test_overlay_live_status scripts.test_i4_overlay_load scripts.test_hitl_live_observation scripts.test_save_while_playing scripts.test_i4_checkpoint`:75/75pass (`i4-overlay-observation-host-final.txt`). Includes13new protocol/query/hostflow cases, no-live-CAP navigation, exactnextrow debounce, stale/duplicate/wrongquery rejection, ownerproof and changedhistory/dirtyYes refusal, final snapshot-only failure.
- One focused reviewer c569e138 found no production defect; found an older guard test passing at the new earlier closed-overlay guard. Corrected with a valid freshclosed sample, exact playing_entry_precondition_changed assertion and bothstatusreads/noenter proof;75hostretestpass. No repeatreview/approval claim.
- Full `pio test -e native`:3085total/3078passed/7skipped/0failed (`i4-overlay-observation-full-native.txt`,113passed suite summary rows). Combined300s tool timeout occurred afternativecompletion and interrupted firmwarecompilation; no test failure inferred. No leftoverbuildprocess; resumedonlyfirmwarebuild with600s bound.
- `pio run -e teensy41-capture-serial`:SUCCESS (`i4-overlay-observation-build-resume.txt`), RAM1variables96192/code392056/padding1160>=840/locals34880; RAM2variables163360/EXTRAM11680 unchanged. New command/emitter flash-placed; no RAM1/state/store addition. Built HEX SHA256 `c455babbce6c4cdc1ab7678b90d82da3e32bb18f87c883908664872a95346f0d`.
- `openspec validate load-save-overlay-display-regression --strict`:valid; unchanged archive-prerequisite INFO about missingmodifiedrevision-loadheader/absentset-browsermain-spec retained, no archive. Diffhygiene clean.
- All named check logs are in this session scratch directory. Device NOT RUN, candidate NOT uploaded. Installedcdfe4b51/checkpoints/Current/overlay-visiblepanel remain untouched. Host on oldimage nowrefuses missingquery beforegestures. Fourdot/pixels/responsiveness and actualpreservingload/autoexit/content remain UNMEASURED. No general USBtiming/safetyguarantee fromnative orTXroom.

SESSION_CLOSEOUT: runtime/current/guide/inventory/localtask/deliveryevidence updated; explicit decision/exclusions recorded; no SDmigration/Git delivery or hardwaretaskclosure. Next requires separate uploadapproval, safe manageddrains/readiness, current ownerpanelwitness and registered guardedload with actualcheckpointidentityproof. Do not blindlydismiss/reset/load to hide priorpanelobservation.

## Separately approved upload completion

Ownerapprovedc455babb/defaultuploadSUCCESS/hashunchanged; managedpre/postdrains2complete/nolatch/stoppednewqueryclosedidle/settledheapinspectioncomplete. Ownernormalpanel/no fourdot overlayproblem. Installedc455babb nowanswersquery; localNOTuploaded notes above are historical implementation-slice results. No playingnavigation/Load inuploadslice. [Upload/readiness evidence](OBSERVATION-UPLOAD.md).
