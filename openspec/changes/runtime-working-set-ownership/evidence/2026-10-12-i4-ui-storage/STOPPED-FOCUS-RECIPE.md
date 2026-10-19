# I4 — stopped NOTE_EDIT focus (slot-selection-focus8.1)

Owner requests continuing nextvalidation. Authorizedscratchtrack8/slots4–5 only, preservingCurrent/testcheckpoints; installed8050aabc; no firmware/upload/reset/SDdestructive/Gitpermission. Base2canonical/stimuluscataloglistedbeforeauthoring. Host-onlyadapter reusesmanagedDeviceSession/singlecaptureowner/follower/actions.

## G2 — execution packet / fixed oracle

Checkpoint-first: registeredi4_checkpoint newdirectory mustprovewholeCurrentrevision beforeeitherseed. Previousv3preservespre-v2Current; newcheckpointcoverscurrentpost-v2 state. Two separate existing`base` runs (no preset) use explicitRecord-only/TEMPBase2variant, hostclock300BPM,2bars,channel8, low=high60 forslot4 /84 forslot5; noOD/undo. Canonicalrecipesuntouched; actualCLIoverrides/sourceconfigcaptured. ExistingRecordseedqualification mustpass bothreports/canonicaldrains beforefocus. Bothslotsmustbefilled: productionstoppedemptyshortslot stillarmsrecord eveninNOTE_EDIT, so filled-emptyshortcut rejectedbytrace.

Focus-only newregisteredSTIM-I4-STOPPED-FOCUS-01 /i4_stopped_focus. Guardconfig8/4, managedowner/follower, provennewcheckpoint, twoqualifiedseedreports(targetslots/pitches/32On32Off/1536ticks), freshselectedtrack7/active3or4 STOPPED/global0, freshoverlayclosedphase0. Read-onlyfencedTRACK_STATUS beforeeverygesture; no serialmutation orsecondowner.

Normalize edit mode by existing global EDIT long (not a slot hold), provefreshLOOP_EDIT ack; selectfilledslot4 withsharedshorthelper andproveSTOPPED/slot3; EDITshort entersNOTE_EDIT, provefreshmodeack. Then A(slot4pitch60)→B(slot5pitch84)→A onlynonselectedfilledshortslotpresses whileNOTE_EDIT/currenttrack7/stopped/overlayclosed. Do notpresstype same slot (couldstartplayback), longslot (clear), emptytarget (arm), ortogglemodeblindly.

Each epochrequires fresh targetstatus STOPPED/global0/expectedactive slot, currentNOTE_EDIT ack, DISPtarget/1536ticks/32visualnotes/committedcontent andDNTEexpectedpitch afterepochfloor; latestmode/slot/pitchmustagree, no stale/oppositeslotrowsasPASS. NOTE_EDIT focusreopen emitscurrentmodeack throughproductionarrival. Windowtimestampqualification includesCAPmicros andtimestampedloggeracks; no assumptionsfromoldboot. FreshCapturearm/record/playing/reset/critical-loss refusal. Scalarqueriesprovenstopped/defaultYes don't independentlyprovepixels; requireactualOLEDstate/pitchlabels aftereachstep.

Owner witnesses via freshnonce/session/label request/reply perphase≤90s; noidentityapprovalreused; no reply changesdriverbehaviorunlessmatching. At most3stages/≤270shumanwait +boundedgesture8s each under managed360s. Windowpinnedbeforefirstdeviceexecution basedonprior85–95sowner-responsetiming, nottosmoothamachinefailure. RetainreportswhenUNMEASURED/failure; canonicalStop/drain2 everyexit. No note/faderedit is sent; selectionmetadata saves allowed. No fullcontentbyteparity/independentrestore/playinggridclaim.

## HITL_TEST_RECIPE

Reuse existing checkpoint/baseRecord/actionhelpers; new distinctfocusstimulus registeredaftercataloglist, parentrecipesnotedited. Gestures: globalEDITlong, EDITshort, nonselectedfilledslotshort. No handrolledbytes. ExistingtimestampedGS/DISP/DNTE/NOTE_EDITmodeacks/selected-trackqueries, no newfirmwaremarker/TierA/rate/RAM1cost. Hostfreshness/target/seed/panelnegativecasesbeforehardware; onefocusedhostreview. Fullnative/hostchecks, strictOpenSpec/diff. No firmwarebuildneededhost-only.

Deviceentry `host_midi_hitl.py run --scenarios i4_stopped_focus --track 8 --loop-slot 4 --midi-channel 8 --panel-overlay-closed --no-boot-reset --out-dir captures/i4-stopped-focus-t8-01`; no preset/capturedcommand/scaffoldentry. Sources fixedtonewcheckpoint/seeds underthispacket; freshdirectory required.

## Results

Pending hostpreparation/review/ownerreadiness/checkpoint/seeds/hardware.8.1 remainsuncheckeduntilactualmachine+OLED+shutdown oracle passes. Allotherfocus/restoreblockedclauses unchanged.

## Host prerequisite checks / review

Registeredfocusadapter/newstimulusready; finalfocused84hostPASS, fullnative3099pass7skip/114suites. OnefocusedreviewREQUESTCHANGES found unsafepressdurationoverride andunlinkedcheckpoint/seedchronology. Unsafe900ms casebehaviouralRED, fixedtoexact120msrefusalbeforenosend; sourcebundlesbindmetadata/session/image/shutdown/embeddedrecorddrain andcompletion-before-nextstart, hashesretained. NewAPIprovenancenegativetests coverreversed/overlap/sharedsession/wrongimage/shutdownmismatch. Findingsfixed/retested, no secondreviewapprovalclaim. No firmware/build/uploadchange.

## Owner-approved TEMP timed observation variant

Canonicalrun01 reachedfreshstoppedNOTE_EDITslot4/pitch60/DISP32 butownerreplyarrived149safterrequest, past90s; driverrefusedbeforeB, shutdowncomplete/drains2/nolatch. OriginalreportFAIL/lateproofretained/notreused.

UserexplicitlyapprovesTEMPobservation-onlypass with20sheldA/B/Aviews andpost-teardownOLEDwitness. Canonicalspec/gatedmode unchanged. New`--observe-only` permittedonlywithactualrunStimulusKIND_TEMPORARY; allcheckpoint/seed/image/chronology/120ms/stopped/closed/mode/slot/pitchguardsremain. Duringeach20sdwell poll/revalidateexacttargetandfreshmode/display/pitch; anychangeaborts. Bound≤~120sunderexistingmanaged360s, no deadlineforuserreplyafterteardown.

Machinecollectionreturns`machine_ok=true`, `ok=false`, verdictMACHINE_PASS_OLED_PENDING, noownerproof. Exit0meansobservationscollectedonly; no8.1checkoffuntilseparateactualownerfullA/B/Aconfirmation andindependentdrainedreplay/shutdown verification. This doesnotrelaxmachineacceptanceorallowdestructivegestures. No sourcefixturesreseeded, no firmware/update/SDoperation. TEMPentryadds`--observe-only --stimulus-temp-parent STIM-I4-STOPPED-FOCUS-01 --stimulus-temp-suffix TIMED-OBSERVE --stimulus-temp-reason 'Owner reply latency149s exceededlive90s gate; holdguardedviews20s andcollectOLEDwitnessafterteardown'` innewcaptures/i4-stopped-focus-t8-02. Canonicalrecipesuntouched.
