# I4 — playing overlay dirty-Yes load qualification: PASS

## Authorized run and exact scope

Owner requested fullplaying-load validation after approvedbootrecovery. Existingpermission: scratchtrack8/slots4–5, wholeCurrentpreservingtestrevisions, loadonlytest-createdS0001_v0002. Noother-slotclear/Setdelete/quarantine/rename/nuke/cleanup/Gitdelivery. No firmware/hostcode/stimulus/thresholdchange, reset orupload inthisqualification slice.

Registered STIM-I4-OVERLAY-LOAD-01 via `host_midi_hitl.py run --scenarios i4_overlay_load --track 8 --loop-slot 4 --midi-channel 8 --panel-overlay-closed --no-boot-reset --out-dir captures/i4-overlay-load-t8s4-06`. No preset. Git26125cebb08980342ba56ed2b77e5397783ac8e5+dirty/workingbranch; installeddefaultcaptureHEX8050aabcf64dc7ecd634371c824dad7ee0709057ee8fa60c91c6a54e979942f7 pinnedinmetadata/priorapprovedupload. WholeCurrentpreservingcommit creates S0001_v0003 before exactv0002 Load. This is pipeline/identity qualification, not wholeworkspacehardwarebyteinventory proof.

## Failed attempt retained / human gate

Run05reachedv2history but its90sidentitywindowexpired: request11:01:26, driverreport11:02:56, lateownerreply11:03:01. Reportokfalse/owner_checkpoint_identity_timeout, canonicaldrains2complete/nolatch, noSave/Loadreceipt. Lateapproval retained but neverreused. OwnerdoubleEDITinitiallydidnotclosehistory; laterconfirmedmainscreenvisibleafterdoubleEDIT. No blindtoggle/reboot orcapturereportrewrite.

Run06usesfreshnonce3d11703e88fb418ea9f1fa29edd4687b/currentsession_log/labelS0001_v0002. Request11:07:30, reply11:08:55 (inside90s), actualdriverconsumedexactproof and revalidatedfreshPLAYING/historyfocusbeforeLoad. Owner: “Yes — highlighted S0001/v0002; approve preserving Load.” No guessedcatalogtarget orstaleapproval.

## Frozen strict transition oracle / independent drained replay

| Stage | Device micros | Identity/result |
|---|---:|---|
| Playing immediatelybeforeenter | 142931549 | track7/active3 PLAYING/global1 |
| Overlay enter | 143454560 | LDSV1 |
| Playing beforeLoad | 235566677 | freshtrack7/active3PLAYING/global1 |
| Historypreconfirm | 236380527 | open1/history2/row0/Set1/phase0 |
| Dirty prompt | 237119569 | targetSet1/rev2/shown |
| DirtyYespreconfirm | 239297799 | open1/dirty1/row0/Set1/rev2/phase0 |
| Dirty Yes | 239992855 | save_then_load |
| Preservingcommitrequest | 239992869 | queued |
| Commitdispatch | 240037730 | run |
| Commitcomplete | 256052476 | S0001_v0003 |
| Loadrequest | 256052500 | exactSet1/rev2queued |
| Loaddispatch | 256055836 | exactSet1/rev2run |
| Loadcomplete | 266532150 | S0001_v0002 |
| Automaticoverlayexit | 266532217 | LDSV0, no manualexit gesture |
| Post-exitdisplay | 266619915 | targetslotEMPTY inrestoredv2, thenDFRAME266857810 andlaterframes |

Save-then-loadYes→complete26.539295s; observedworkloadonly, nogenericdeadline/speed/contentparityclaim. ActualrestoredtargetslotEMPTY isnotanindependentfocusrestore/contentfailureoracle; noassertionexpectedseedtakeinthisoldercheckpoint.

Independent `verify_load` replay of entiredrainedlog/freshuint32floors matcheslivewitnessmap/preservingidentity exactly, oktrue/issuesempty. Originallegacyoverlayverifierpasses with exacttarget andactualdirtypromptsearchanchor; nohistoryconfirmmarkeremitted, so no inventedOVLY2confirmreceipt. Originallegacydirty-Yesverifierpasses using separatelyprovenv2setupcommit receiptfromcheckpoint03 + currentfreshorderedreceipts (newdirtyRecordnotfabricated/required becauseprotectedCurrentwasalreadydirty). Strictoracle, notweaklegacyanchor, establishes allordering/identityconstraints.

Metadata/nopreset/currentrequestreply/sourcecheckpoint/entryguards/preservingv3/loaddispatchcompletion/autoexit/shutdownindependentlyasserted. Currentmeasuredwindowhasnooverflow/critical_rejected/tier_a_evicted/wiped/newbootresetrows. Completecanonicalshutdown/drains2/oktrue, finalheapinspectioncomplete/nolatch/noactivecaptureowner. Runexit0.

Artifacts: run06/i4-overlay-load.json(original), human-load-request/reply.json, metadata.json, sessionlog, shutdown-result.json, independentdrained-load-verification.json andoled-observation.json. Run05failedartifactskept.

## Physical OLED acceptance

Owner: **“Yes — responsive progress, automatic exit, normal piano roll returned.”** Thequestionexplicitlycoveredminimalloading/progressduringpreservingSave→Load, nofreeze/reboot, autoexit/normalpianorollwithouttrackchange. Storedseparately; originalmachineoled_observationUNMEASURED fieldnotrewritten. Serialphase/framesalonewereNOTusedaspixelproof.

## Verification / closeout

Freshhostfocused suites78/78PASS (`i4-playing-load-closeout-host.txt`); nativecapture/storage/catalogfocused63/63PASS (`i4-playing-load-closeout-native.txt`). Fullnative3098pass7skip/defaultbuild/review evidence reused unchangedfromboot-journalfix; no source/build/uploadchange inthisdevice-onlyslice. FirstsupplementaryreplayattemptStopIterationonabsenthistoryconfirmanchor; corrected toactualdirtypromptanchor, strictloadassertionsalreadypassed andremainunchanged. No verdictwaiver.

Closeoverlay6.2 — MIDIoverlaydirtyYesload,6.3 — dirty-loadregressionYes and6.4 — playingloadminimal-screenmanualsmoke. Task7.1regressionhandoffupdated. NotcloseNo/Cancel/cleanvariant/reopen-midload/defaultTransport/independentactive-selectedrestore/wholehardwarecontentparity/wholeI4; existingfocusrestoreblockers remainseparate. Noarchiving/specsyncorGitdelivery. KnownarchiveprerequisiteINFO remains;7.2stillopen. Deviceleftstopped, loadedtestv2/Currentandpreservingtestv3 retained.
