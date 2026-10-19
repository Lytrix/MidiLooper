# I4 — remaining focus/restore validation

Owner: update closeoutplan and continue next validation. Existing I4 scope permits slot-selection-focus8.1–8.5 qualification and scratchtrack8/slots4–5; no new firmwarebehavior/upload/SDdestructive/Gitpermission. Installed8050aabc unchanged, exactplayingdirtyYesload alreadyqualified; v2loaded/v3preservespriorCurrent. Do not requeue qualifiedsave/load.

## Selected next validation / G2 packet

Task8.5 missing-Transport fallback subclause only. Requirement: when Transport absent, active and selected pertrack equal firstoccupiedslot; emptytrackdefaults0. Native-only realrevision/load oracle; no hardwarefooterfabrication orcardedit. Whole8.5 staysopen onknownindependentrestoredefect andunmeasureddevicefallback.

Reuse existing realSD/TrackManager/RevisionCommit/RevisionLoad/nativepersist-owner fixture test_storage_drain_restore. Produceactualfixturecheckpoint withtrack0occupiedslots1/3 (notes60/67), thenin-memory-only replaceTransportchunktype withGlobal andrestampstreamedfooterCRC. Layout/bodyoffsets/SlotIndex entries/loopbodies remain unchanged; publicparsermustconfirm Transportabsent beforeload. This isvalidunknown-to-loaderGlobalpayload, not a canonicalstimulus ordevicefileedit.

Oracle fixedbeforetestedit: requestexactcreatedrevision→productionPersistenceadmission→actualloadValidate declaresusedDefaultTransport→realWrite/ReloadRam/Complete within4096ownerturns. Beforeload setruntimeactive/selected7 onalltracks throughsystemrestoreAPI(noedit/persistencehooks). Finaltrack0active1/selected1, occupiedloops1/3contentpresent; emptytracksactive0/selected0. These explicit indices discriminatefallbackfrompriorruntime7 andfromsecondoccupied3. Exactsame loadedrevision identity andcompletionreceipt required. No copiedFSM orfooter-onlyoracle.

Verification: focusedtest_storage_drain_restore expectedexisting22+new1; full `pio test -e native` beforefinishingplanpacket, hostquery/bootorder/focusneighbors asappropriate; strictslot-focus/overlayvalidation/diff. No firmwarebuild/upload neededbecauseonlynativefixture/docschange; no numericaldevice/pixels/timingclaim. No independentproductionreview neededfor test-onlyvalidation (no runtimebehaviorchange); oracle grounded in spec/publicreaders/realproductionentry.

## Remaining blocked/queued

8.1 stoppedNOTE_EDIT hardware needsdistinctcurrentcontents inauthorizedslots andownerOLEDwatch; v2targetslot4 isEMPTY, so sameempty→empty gestures do notqualify. 8.2/8.3 playingNextGrid wording conflictswithDEC-025 — split focus/LoopEnd; no policyswitch permitted. HeldslotreleaseLONG_PRESS canclearselectedslot, so blanketlayerhelperunsafe.8.4 andindependentrestore8.5 knownstoppedruntimecollapse remainblocked; nofixpermissioninferred.

## Results

Native missing-Transport production-path validation PASS. Retainedcase `test_i4_revision_without_transport_restores_first_occupied_runtime_indices` createsrealcheckpoint1/1, provesdirectoryoccupiedslots1/3, removestypedTransportpresence/restampsstreamedfooter inin-memorycardonly, parksruntimeindices7, thenrequests/stepsrealValidate→Write→ReloadRam→Complete. `usedDefaultTransport` observed; finaltrack0active1/selected1 andemptytracks0/0; bothcommittedloops remainpresent. Expectedvaluespinnedbeforetestedit; no copiedFSM/footer-onlyPASS.

- `pio test -e native -f test_storage_drain_restore`:23/23PASS includingthenewcase (`i4-focus-default-transport-native.txt`).
- Full `pio test -e native`:3106total/3099passed/7skipped/0failed,114suitesPASSED (`i4-focus-default-transport-full-native.txt`).
- Hostbootorder/liveoverlay/guardchecks25/25PASS (`i4-focus-default-transport-host.txt`).
- `openspec validate slot-selection-focus --strict`:valid; diffhygienePASS. No firmwarebuild/upload needed/test+docsonly; installed8050aabc unchanged.

Task7.8 nativefallbackevidencechecked; compound8.5 remainsunchecked (independentrestorefailure/hardwarefallbacknotproven). No empty-vs-emptyhardwarefocuspass claimed. Next8.1 stoppedNOTE_EDIT needsdiscriminating8/4–5contents/ownerOLEDreadiness; no deviceaction inthisslice. No firmware/policy/SDoperation/Gitdelivery. Plan'slatesthandoff updatedwithoutdiffaccumulation.
