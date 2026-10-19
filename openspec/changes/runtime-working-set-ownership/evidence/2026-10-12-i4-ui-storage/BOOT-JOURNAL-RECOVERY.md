# I4 — approved capture-journal recovery upload and boot verification

## Outcome / authorization

Owner approved upload of candidate8050aabc and boot-stability verification after the diagnosed fault loop. Installed default `teensy41-capture-serial` image SHA256 **8050aabcf64dc7ecd634371c824dad7ee0709057ee8fa60c91c6a54e979942f7**; hash checked before/after successful upload and in both managed-capture metadata files. Workingbranch feature/unified-rolling-playback-path @26125ceb+preserved dirty work. Firmware source unchanged during this slice.

Boot fault loop **not observed after handoff**; settled90s verification passes, owner confirms normal stable OLED/no reboot loop. Fullplaying-load/dirtyYes/restore qualification remains open: no Record/Save/Load/overlay/content gesture performed.

## Recovery safeguards / upload

Old firmware was in a confirmed fault loop and ordinary stable query preflight was unavailable. Explicit recovery approval, pinned successful localbuild/native/review evidence, no live serialowner and no host incomplete-shutdown latch checked. Priorpreflightcomplete/drains2 artifacts are NOT claimed as stable readiness of the old rebooting image. No SD quarantine/delete/rename/re-save/clear workaround.

`pio run -e teensy41-capture-serial -t upload`:SUCCESS12.872s; defaultRAM1variables96192/code392216/padding1000>=840/locals34880 unchanged; USB re-enumerated. Hash unchanged. Logsession scratchboot-journal-upload.txt. No furtherreset requested.

## First capture — boot transition observed, report remains incomplete

`captures/i4-boot-journal-recovery-01`, managedsingleowner/no-reset, read-only TRACK_STATUS/OVERLAY_STATUS/TRACE_DRAIN/heap requests; canonicalDeviceSessionteardown. Observer90.6005s/16samples: firstpairmissing before handoff; originalboot-recovery.jsonokfalse/issuesfresh_query_missing preserved. Subsequent15pairs validSTOPPED7/3/global0/overlayclosedidle, uptime13891967→94685307 with no reconnect.

Observed `DisplayManager: Boot setup complete`; boot_commit13620870; post_restore_census23002990 withadmission_complete/interactive_ready/undo_hydrated/transport_stopped/deferred_prewarm_idle all1. One startupCrashReport/resetcrash1 record appears beforehandoff (old/recent fault report carried into startup); identicalresetrow is replayed but one distinct timestamp1924393. No laterreset/reconnect orcriticalevidence-loss rows. This record is not waved away as crash0/freshbootwithoutfault; measuredclaim is stableexecutionafterhandoff.

Finalshutdowncomplete/drains2/oktrue and completeinternal+externalheapinspection. No fullrun01PASS: missingearlyquery remains genuine setup gap.

## Settled confirmation — PASS

`captures/i4-boot-journal-recovery-02`: separate90.0122s managedno-resetwindow afterhandoff,16/16querypairs valid, target7/3STOPPED/global0 andoverlayclosed/root0/phase0 throughout. Firstuptime183980382 >run01last94685307; final271394803. Cross-runuptimeadvanceandper-sampleadvance verified; no new bootreset, reconnect orcriticalevidence-loss rows. Priorhandofflinked explicitly, not a fabricated freshbootreceipt. Observation spansseveralformer~14srebootcycles; no cold-power-cycle/generalstressclaim.

Reportoktrue/issuesempty. Completeheapinspection before/after, finalshutdowncomplete/drains2/oktrue, hostlatchabsent, no serialowner left running. Owner: **“Yes — normal screen, stable, no reboot loop.”** Stored inrun02/oled-observation.json.

## Checks / limits / closeout

Independentofflineassertions verifybothmetadatahashes, originalrun01failure, querycounts/ranges/crosscontinuity, no reconnect/loss, bothdrains andnolatch. Native3098pass7skip/78host/build/review evidence reused unchanged from BOOT-JOURNAL-FIX.md; not rerun forupload-onlyslice. Initialscratchobserveredit hadout-of-boundsline, noedit applied; refreshed/correctedbeforecontinuation, no repositoryhostcodechange. Read-onlyprobe/currentimagecommands only; firmware's normal boot/persistence/crash logging may write its own state, so no blanketzeroSDwritesclaim.

Hardware5.6 — bounded boot recovery qualified. DEC-121 status/guide/runtime/current/delivery/tasks updated; playingLoad6.2–6.4 stillunchecked. Stronger supportforjournal-initcausality, not completehardwareLR proof. Current/checkpoints retained; no manualSDoperation orGitdelivery. Next fullplayingload stillneedscurrentownerreadiness andfreshhighlightedS0001_v0002 identityapproval beforeLoad.
