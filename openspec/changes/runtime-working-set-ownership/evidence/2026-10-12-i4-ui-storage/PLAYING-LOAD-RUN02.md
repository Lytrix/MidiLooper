# I4 — guarded playing-load run02: incomplete at Set browser

Owner requested Playing-load qualification and confirmed current normal screen/closedoverlay/readiness towatch. Registered STIM-I4-OVERLAY-LOAD-01 through `host_midi_hitl.py run --scenarios i4_overlay_load --track 8 --loop-slot 4 --midi-channel 8 --panel-overlay-closed --no-boot-reset --out-dir captures/i4-overlay-load-t8s4-02`. No preset/reset/upload. Installed HEXc455babb (fullhash inmetadata), Git26125ceb+dirty/workingbranch preserved. Test-created checkpoint reference S0001_v0002 remains target, never guessed or changed.

## Actual witnesses

- Fresh initial target7/active3STOPPED/global0 at515336554; closedroot query515651766.
- After authorized transport start: targetPLAYING/global1 at516985307 and immediateentryguard517543327.
- Actual overlayopen LDSV1 at518115185; directlive snapshot518198618 open1/root0/row0/phase0. This fixes priorrun01's live-receiptvisibility blocker; no playingringdrainpolicy change.
- Rootup/clamp OVLY0/0 at519237368. FirstRECORDshort receipt520070688 yieldsrootrow1/Current at520070922; livequery520600861 row1.
- FreshstillPLAYING/global1 at520908686. SecondRECORDshort received521421585; resultingOVLY0/1 at521422101 (clamped), notrow2. Subsequentdirectfreshqueries remainrow1 through529119935.18overlayresponses total.
- Driverrefused `missing_fresh_overlay_snapshot_{'open': 1, 'mode': 0, 'row': 2, 'phase': 0}`, exit2. No historygesture, owneridentityrequest/reply, Load, dirtyYes or preservingSave. Originalreport `loaded_checkpoint` names intendedtarget only; it is NOT a completedload.
- Canonicalfinalshutdown complete/drains2/oktrue; hostlatchabsent, no captureprocess remains. Nooverflow/tier_a_evicted diagnostic found inthislog. No contentgesture/clear/SDdestructive/Git action.

## Physical evidence / exact outcome

Owner: **“Save and Current only; responsive, no four-dot obstruction.”** Stored inrun/oled-observation.json. Qualifiedscope: overlayopening andordinaryrootnavigation remainresponsive duringplaying, newdirectquery observable duringplaying. No minimal-loading-screen/loadcompletion/pixeldeadline/contentparity/wholemanualsmoke PASS. Tasks6.2–6.4 remainunchecked because Load wasnever reached.

## Read-only blocker trace

`SetBrowserOverlayPolicy::rootWorkspaceListRowCount` returns2+cachedSetCount. `DisplayManager::adjustLoadSaveListSelection` clamps totalRows-1 and emits resultingrow; observedCurrent1→Current1 confirmscachedSetCount0 foractualsecondscroll. `refreshLoadSaveListCache` sets count from `StorageManager::listSetRevisionBrowserEntries`, then markscachevalid evenifcount0. `DisplayManager::update` attemptsrefresh onoverlayenter only wheninvalid andcataloggateallows; no identicalretryinfollowingordinaryframes. Listing readsSetindex/nextSetId andacceptsSetmetadata onlywhenread/identity/latestRevision pass. Zeroresultsdo not distinguishmissing/unreadableindex, rejectedmetadata, ornot-yetrefreshed/staleemptycache. Runtimequeryphase0 alone doesnotprove workspaceSDgateidle.

ActualSDrootcauseUNKNOWN; successfulearliercheckpointcreation isnot proofbrowserindex currentlyreadable, andemptybrowser isnot proofmissingrevisionpayloads. No firmwarebehavior/cache/catalog/SD change toforcePASS. No blindretry/reset/dismissal. Deviceleftstopped withresponsiveoverlayvisible. Next bounded work is diagnosingtheSetcatalog/cache admission path andrepaironlyaprovenblocker underseparatefirmwareauthorization; retainCurrent/checkpoints/originalfailedruns.

## Checks / closeout

Read-only artifactassertions pass: reportincomplete, exactLDSV/secondscrollreceipts,18directqueries/no row2, noidentityfiles/revisionrequests, completedcanonicaldrains/nolatch. Previousnative3078pass7skip/75host/defaultbuild/review evidence applies to unchangedsource; no native/build rerun neededfor hardware-onlytest. Runtime/task/delivery/remainingvalidation docs updated; fullplaying-load acceptance staysopen.
