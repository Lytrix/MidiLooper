# I4 — approved observation-image upload

Owner answered yes to uploading the verified candidate; upload/read-only readiness only, not another load run. Branch feature/unified-rolling-playback-path @26125cebb08980342ba56ed2b77e5397783ac8e5+preserveddirtywork. Approved/default `teensy41-capture-serial` HEX SHA256 `c455babbce6c4cdc1ab7678b90d82da3e32bb18f87c883908664872a95346f0d` checked before/after upload, unchanged. Identity established by successful approved upload plus artifact hash, not binary readback.

## Preflight / upload

Scratch-only probe reuses HitlConfig/run_with_managed_capture/active_managed_capture and canonicalDeviceSession; no second serialowner, no contentgesture. `captures/i4-overlay-observation-preflash-01`: no-resetfresh7/3STOPPED/global0 at1601950715, completeheapinspection, no latch, shutdowncomplete/drains2/oktrue. Preflightmetadata hash refers to cachedcandidate, not oldinstalledimage. Explicithash/shutdown/latch guards passed before flashing.

`pio run -e teensy41-capture-serial -t upload`:SUCCESS14.898s; defaultRAM1variables96192/code392056/padding1160>=840/locals34880 unchanged. USB re-enumerated. Uploadlog session scratch `i4-overlay-observation-upload.txt`. No extra reset, Save/Load/Record/clear/SDdestructive/Git action.

## Post-upload read-only readiness

`captures/i4-overlay-observation-postflash-01`: freshSTOPPED/global0 at12896906 and exactnewOVERLAY_STATUS at13405718 (open0/mode0/row0/set0/rev0/phase0). Initial/endheapinspectionrefusedstorage_work, so this earlyprobe alone is not settledreadiness. Keptoriginalartifact; shutdowncomplete/drains2/oktrue.

Allowedrestoretofinish, no-resetread-onlyattach `captures/i4-overlay-observation-postflash-02`: freshSTOPPED/global0 at51330947 and freshquery51637908, sameclosed/idlevalues, completeinternal+externalheapinspection before/after, shutdowncomplete/drains2/oktrue. Observed `DisplayManager: Boot setup complete`, boot_commit at14715567, post_restore_census at24842620 withadmission_complete/interactive_ready/undo_hydrated/transport_stopped/deferred_prewarm_idle all1. No boot_resetbanner retained; not a separatefresh-bootqualification. Finalhostlatchabsent.

Owner post-upload OLED: **“Normal screen, no four-dot overlay problem.”** Stored in postflash02/oled-observation.json. This confirms currentnormalpanel/closedoverlay, not responsivenessorabsenceofregression underplayingload. No playingtransport, overlaynavigation, identityhandshake or Load in thisuploadslice. Stoppedqueryhardwareproof only; exactguardedplayingdirtyYesload/pixels/USBtiming/independentrestore remainopen.

## Handoff

Installedimage nowc455babb. Reuse guardedregisteredscenario only after currentownerreadiness; beforeLoad requireactualhighlightedS0001_v0002 proof withfreshnonce/session/label. Current/testcheckpoints retained; no SDcleanup. Native3078pass7skip/75host/defaultbuild/review evidence reused unchanged from PLAYING-OBSERVATION-FIX.md; uploadchanged no source. No Gitdelivery. Runtime/guide/tasks/delivery updated; no hardwareloadtaskcheckoff.
