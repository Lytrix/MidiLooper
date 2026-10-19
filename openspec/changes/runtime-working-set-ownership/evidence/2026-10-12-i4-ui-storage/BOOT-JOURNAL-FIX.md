# I4 — capture critical journal boot initialization / bounds fix

Owner approved narrow implementation after BOOT-LOOP-DIAGNOSIS.md. No upload/device observation/reset/Load/SD operation/Git delivery authorized. Active overlay verification6.2–6.4 blocked; this is their immediate boot blocker only.

## G0 / decision review / G1

Reuse existing DebugSessionCapture journal, ring, initialization and critical_rejected reporting. Searches DECISION_LOG for DMAMEM/journal/boot initialization; no decision permits reliance on NOLOAD static initializers. Existing owner extension, no new queue/lease/state machine/SD representation; one ordinary-bss bool establishes whether existing RAM2 journal metadata was initialized this boot. Journal ownership/lifetime is boot-local; startup clears .bss, not .bss.dma. Captured PC maps to strnlen, allthree linked callers captureowner; exacthardwarecaller unknown. Firmware fix must not claim hardware recovery until separate approved upload.

## G2 — frozen execution contract

Primary files: src/Utils/DebugSessionCapture.cpp and existing test_capture_ring fixture. Owner remains DebugSessionCapture. First use includes journalCriticalLine (early rejected append before sessionHeader), journalCriticalHeadRecord (eviction), emitCriticalJournal (stopped flush before init) and initCaptureBuffer. Ensure once-only initialization before any metadata access: clear count/index/dropped/started only, not payload; ordinary-bss guard startsfalse eachboot. No allocation, content/SD access, new marker or periodic serial output. Existing32row/192byte bounds remain. Repeated initCaptureBuffer/sessionHeader preserves captured currentboot journal/ring.

Bounds validation before append/replay: count<=32, dumpIndex<=count, and nonzero index implies dumpStarted. Invalid metadata must never derive a row pointer. Discard unusable journal metadata (count/index0/startedfalse, one dropped sentinel) and increment existing ringcriticalRejectedPending/Total once with first-rejection timestamp. No traversal based on corrupt values, no payload clearing, ring content unchanged. Return refusal for that journal operation; subsequent operations can use safely resetmetadata. Existing stopped emission exposes critical_rejected and critical_journal dropped count; playing<=8 flush still writes noUSB and cannot replay journal.

Oracle/fixtures before production edit:
- Poison NOLOAD metadata withactualbad-address-compatibleindex; productioninit resets scalar state, noUSB/noallocation;
- early real rejected Record receipt before init followed by init/sessionHeader is retained;
- first stopped flush onpoisonedjournal safe/no staleoutput;
- after validinit count>capacity/index>count/nonstartednonzeroindex refused without rowread, existingloss countersonce; backpressure retains loss notice until stoppedroom;
- repeatedinit/partialreplayretaincursor/content/dropcounts; validfull32capacity journal replay exactlyonce; existing critical-row replay/noUSBplaying tests unchanged.

Fixture boundary: existingrealproductionTU, USB substituted, native timehook; do not preclearjournal fornewstartupcases. pre-fixregressionexpectbehavioralinitfailure; dangerousfaultvalue onlyusedininitcase (neverpre-fixdumpdereference). BoundsRED uses zeroTXroom so oldcode reachesorretainsbadstate without dereference; mutationguards afterimplementation.

## Verification recipe

pio test -e native -f test_capture_ring; focusedneighbor capture tier/diagnostics; full pio test -e native; pio run -e teensy41-capture-serial withpadding>=840/no rebase. Onefocusedindependentreview; correct/retest concretefindings,no broadreviewloop. Scratchlogs retainRED/GREEN/build evidence. Existing imageecd4f671/ RAM1padding1000 baseline. Newhelperflashplaced, unchangedRAM2payload/PSRAM. No newHITLstimulus/marker; reuse managedboot/preflight after separateuploadapproval.

## Completion

Implemented and locally verified; DEC-121 — boot-local capture journal initialization records lifecycle/exclusions. Device NOT RUN/NOT uploaded; fullplayingLoad/manual/boot hardware gates remainopen.

- Pre-fix `pio test -e native -f test_capture_ring`: two genuine failures (poisonedinit Expected0Was28374339, invalidcount Expected0Was33),24oldcasespassed. The initial run also had one authoring mistake expectingmicros0/noCRLF instead ofnative1000000/CRLF; corrected to exactRECScontent suffix before productionfix, not counted asbehavioralRED. RunneradditionallySIGHUP/ERRORED onfailurerun (`boot-journal-red.txt`).
- Focused `pio test -e native -f test_capture_ring -f test_capture_line_tier -f test_diagnostics`:57/57PASS; capture32cases (24prior+8new), neighbors25. Poisonstartup/firstflush/earlyappend/reinit/partialTX/fullcapacity/corruptindices/append+evictionrefusal/lossnotice retained. Logboot-journal-focused.txt.
- Originalscratchproductioninitprobe recompiles/runsPASS: count/index/dropped0/started0 (`boot-journal-init-green.txt`), againstpreviousbehavioralFAIL. No unsafehardwarepointerdereference.
- Full `pio test -e native`:3105total/3098passed/7skipped/0failed, all114suitesPASSED (`boot-journal-full-native.txt`,exit0). No mandatorysuiteomitted.
- `pio run -e teensy41-capture-serial`:SUCCESS (`boot-journal-build.txt`,exit0). RAM1variables96192/ITCM393216(code392216+padding1000)/locals34880; RAM2variables163360/EXTRAM11680 unchanged; approvedpaddingfloor840 retained, no rebase. Candidate HEX SHA256 **8050aabcf64dc7ecd634371c824dad7ee0709057ee8fa60c91c6a54e979942f7**. No upload.
- One focused code-reviewer7b7bda03 APPROVE/no concretefindings. Reviewed firstuse/reinit/bounds/refusal/playingguard/testoracles; source-only, didnotrunchecks. TXpressuresubstitute doesnotmodelyield-drivenreentrancy; existingnestedflush guard retained. No hardwarecausality/PASS inferred.
- Focusedhostquery/load/checkpoint/save/bootorder suites78/78PASS (`boot-journal-host.txt`). Markercontractaudit90prefixes/0over (`boot-journal-marker-audit.txt`); nonewmarker/tierregistration.
- Strict `openspec validate load-save-overlay-display-regression --strict`:valid; pre-existingarchiveprerequisiteINFO retained(noarchive). Diffhygienepass. Namedlogs inthissessionscratch.
- Implementationtouches onlycaptureownerandnativefixture; metadataresetdoesnotclear6KBpayload, onenewbootflagordinarybss (measuredglobalsunchanged), helpersflashplaced. Content/SD/undo/slot/owner/budgets/playingnoUSB policy unchanged.

SESSION_CLOSEOUT: decision/runtime/current/task/delivery/guide evidence updated. Local5.5 prerequisite eligibleonly; actualbootrecovery/playingLoad6.2–6.4 unqualified. Boardstillecd4f671/faultloop; separateuploadapprovalthenmanagedreadiness/bootfaultobservation needed, preservingCurrent/checkpoints. No device/reset/Load/SDcleanup/Gitdelivery inimplementation slice.

## Separately approved upload / boot recovery

Ownerapproved8050aabc/defaultuploadSUCCESS/hashunchanged. OriginalfirstobservationearlyqueryFAIL preserved; settled90scontinuation16validpairs/continuousuptime/no newreset-reconnect-loss/fullheap/drains2/nolatchPASS. OwnernormalstableOLED/no rebootloop. Installedupdate/bootrecoveryqualified, notfullplayingLoad. EarlierNOTuploaded notes describeimplementation slice. [Recovery evidence](BOOT-JOURNAL-RECOVERY.md).
