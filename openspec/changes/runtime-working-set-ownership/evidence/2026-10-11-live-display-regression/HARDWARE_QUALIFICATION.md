# Live display hardware qualification — completed short-loop workload

## Scope and identity

Owner-authorized continuation, 2026-10-11, canonical unchanged base/STIM-BASE-2-2-01, host300BPM, only track8/slot4 overwritten. Record/overdub phase stops remain buttons; final transport gesture remains host realtime Stop. No SD quarantine/rename/delete, other-slot clear, stimulus edit, scheduler/budget change or Git delivery.

Working branch `feature/unified-rolling-playback-path`, git `abaa30975d2cd2650fbce6638ef9e6d3783ad5d7`+dirty. Final flashed capture image SHA-256 `6f176b90352435e54510606f2bc55496d3aed7569684712009f04af2d6650465`, unchanged RAM1 variables96192/code392376/padding840/floor840/locals34880, RAM2163360/EXTRAM11680.

## Run chronology and independent verdicts

1. `live-display-verification-t8s4-01`, earlier image5ba53338: baseline PASS and completed shutdown, but firstOD display32→0→1 with426.729ms count plateau. This is failed display continuity, not overall display qualification. Milestones put Record pass publication28.392s before OD entry29.065371s; `DISP` reports committed1. Thus the native pre-publication ordering below is a broader coverage control, not proof of the device's exact internal predicate state. The correction extends retention across the full slot stop/no-published-display-source interval, rather than relying solely on Snapshot queue state.
2. Native exact early admission ordering RED2→0 and expected3→1, then repaired via existing per-slot stop-obligation metadata in cold `displaySourcePublicationPending`. Same slot complete frame survives finalization before Snapshot queue; live Record without stopped obligation remains incremental, existing published roots do not enter this hold. No playback-view storage read or new state. Nine display cases pass; full native3071total/3064succeeded/7skipped/0failed,97.757s. One focused review: no scoped findings; reviewer did not run tests/builds independently.
3. Corrected image uploaded after `live-display-preflight-02` two verified drains. Runtime MIDI did not return in30s; no postflash session/content gesture opened. Owner reconnected board; USB serial only then owner requested upload again. Repeat upload succeeded, MIDI returned, `live-display-postflash-03` two verified drains. No incomplete latch was cleared/discarded.
4. `live-display-verification-t8s4-02` was interrupted before report/final cleanup receipt. Owner identifies visible blackout as the reboot transition and withdraws it as an issue. Preserve this incomplete run; **exclude it from live-display qualification**. No reboot/boot-gate firmware or host change made. Non-reset `live-display-interrupt-drain-01` establishes complete/drains2/ok before next run.
5. Exact unchanged recipe rerun `live-display-verification-t8s4-03` completes. Log `session_20261011_201435.log`, report `host_midi_automation_baseline_20261011_201603.json`; metadata/invocation/shutdown artifacts in that directory. This completed run is the final qualification evidence below.

## Completed-run result

Report **PASS**, overall_ok=true, COMPLETE capture, stimulus/setup confirmed, contradiction=false, assertions_pass=true, issues empty. Record32On/32Off/0open, firstOD16/16/0, secondOD8/8/0. Persistence first_missing=None/resultok. Report-time and outer managed shutdown both complete/drains2/ok, latch absent; device left stopped/drained.

Measured source rows are streamed/sorted by device timestamp, not text order. Actual ST windows: Record26.492669s→27.764060s, firstOD29.212480s→31.615706s, secondOD40.259657s→42.870148s. Generic retired-job release is not an OD stop.

| Count-progress measurement | Pre-display-repair PASS imagea9a65d2f | Corrected completed image6f176b90 |
|---|---:|---:|
| FirstOD minimum composed count |32|32|
| FirstOD dispatch→next count-progress proxy maximum |412.592ms|**65.629ms**|
| FirstOD same proxy median |43.983ms|53.034ms|
| Record ordinal-count proxy maximum |78.815ms|64.516ms|
| Record same proxy median |52.171ms|39.315ms|
| SecondOD dispatch→next count-progress proxy maximum |66.006ms|67.142ms|
| SecondOD minimum composed count |48|48|

FirstOD progresses32→33→34→35→36→37→38→39→40→41, then publication/wrap composition changes count46..53. No32→0 or live-delta-only frame. The largest raw32→33 count interval262.794ms largely precedes first incoming note; first entering NoteOn→next count progress is36.193ms. SecondOD raw48→49 interval471.366ms also mostly precedes stimulus ingress; first incoming note→increase37.862ms. Do not label these raw count intervals262/471ms display stalls.

Accepted OD dispatch→COORD maxima532us/1656us; these exclude USB/host queue residence. Record initial7 NoteOns dispatch within3.860ms: initial dispatch batching remains separate and uncorrected by this display repair.

Playback first return32On/32Off, exact pitches48..79/ticks0..1488/48-step sequence; maximum consecutive musical MO gap52.961ms. Through firstOD stop maximum67.394ms. No regression to missing second bar or seconds-long backlog burst.

DMA active-phase census advances completed124 at26.934s to420 at42.137s;16 census rows, all degraded/blocking/timeouts/rejections/recoveries zero. DFRAME samples continue during Record and both OD phases. Final ring readback shed5/tierA_evict0/overflow0/crit_rej0. Display-after-phase/transport/undo-redo report checks pass.

## Qualified scope and limits

**Observed success:** this pinned two-bar300BPM external-clock workload preserves the recorded base through the Record-stop/display-source handoff, displays live count progress without the old long plateau/base blanking, maintains MIDI content/scheduling, persists successfully and drains safely. Native assigned-NoteId/interval/submission/completion regressions additionally cover overdub before pass/Snapshot publication; no claim is made that the completed device run used that exact ordering.

**Not established:** count progress is not individual NoteId→physical-pixel latency; DISP is change-filtered, DFRAME sampled every30 transfer attempts, DMA census rate-limited. The Record ordinal proxy assumes monotone unique-pitch progression. Counts can change from wrap/sealed overlays/root adoption without new input, so they are not exact identity parity. No numerical freshness deadline has been declared. No24-bar moving-viewport device leg, multi-target/source/maximum-role qualification or physical per-frame identity/completion trace was run. S7.5b.6 — display frame continuity and combined envelope remain unchecked; baseline PASS does not close those broader gates.

Analysis helper/result artifacts are session scratch: `compare_live_display_hardware.py`, `live-display-qualified-repeat-analysis.log`, `live_display_hardware_results.json`; raw source artifacts are the immutable run directory. Historical failed/interrupted reports/logs were not rewritten.
