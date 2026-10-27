# Truncated Record stop and overdub NoteOff display retention

## Execution packet / scope

User hardware clarification: immediate overdub Start works, but Record bars2–15 disappear after Stop; live overdub notes appear while held and disappear at NoteOff until Record publication. Counts alone do not prove retention. Active owner scope is immediate-record-overdub-handoff capture-state-guards / realtime-visual-cache and current display authority. Existing ratified sealed custody and job-owned previews remain owners; no new buffer/role/scheduler, no raw MIDI gather/sort, no command-edge reconstruction, no playback/admission/companion fallback changes. Preserve all prior dirty work and installed1131e857 image. No upload or device-content action in this slice.

Historical review: reuse `CaptureStopJob::preview`, its O(1) swap/owner-qualified borrow and bounded live-ring queries (already used for overdub Stop), `resolveDisplayNotesByDrawSource` compose scratch, qualified source keys, current-root takeover. Prior held-frame rule forbids old-window content painted against a new viewport, not reconstruction of a complete coherent replacement from an existing complete stopped preview. Late Record publication must remain predecessor-local (DEC-159/DEC-160); resources/preview copies cannot move to a new owner or new persistent field.

## Oracle fixed before edits

1. Long Record24bars plus a partial trailing bar, distinct accepted NoteIds in every bar. Paint the last live Record window; Stop truncates to24bars and rewinds to phase200. Before any canonical worker, paint the post-stop first16bar window: all expected retained Record NoteIds/coordinates for bars1–16 are visible once, and the trimmed partial-bar note is absent. Never reuse late-window content as though it covered the first16bars.
2. Same truncated take, immediately start overdub while A private. Send a distinct NoteOn, paint while open; send NoteOff, paint at least3frames. The same accepted NoteId remains at exact closed coordinates. A's base notes remain; no canonical publication needed. The test checks submission/completion, not only counts.
3. Current-root publication eventually replaces preview source coherently; repeated frames, aligned-stop held-window, slot/replacement/empty/filter controls remain correct. Existing moving-window test may retain a prior interval when no complete alternative source exists; do not weaken its no-mixed-viewport oracle.

## Production paths / resource/timing boundaries

Actual Track Record admission/Stop, `completeAdmittedRecordStop`, DisplayManager update/resolve/paint/DMA boundary; native clock/physical display stubs only. The job already has a preview field; move is constant work and the existing preview vectors remain charged to their active/finalizing roles, no second copy. Any body retirement follows existing deferred owners. A synchronous paint read of preview rows is the existing overlay traversal, not a newly claimed owner bound. RAM1 and external retained-preview lifetime must be reported; no timing qualification from native ordering.

## Verification recipe and current evidence

- RED first: isolated `PLATFORMIO_BUILD_DIR="$PI_SCRATCH_DIR/native-display-noteoff" pio test -e native -f test_live_capture_display_boundary` with both new default cases.
- Focused display, visual continuation, shared finalization, immediate handoff, continuous capture, rolling retirement as affected.
- Full native, capture firmware build/RAM1, one focused independent review of this slice against scratch baseline; fix/retest concrete findings only.
- Strict OpenSpec validation and `git diff --check`; runtime/tasks/display authority updated at closeout.
- Device/UI/physical timing NOT RUN, no upload implied. This slice cannot close hardware acceptance from native tests.

## Completion

Implemented and verified natively/in the capture build. Hardware confirmation NOT RUN; installed1131e857 image unchanged.

### Reproduced failures and correction

- Initial RED: truncated-stop source key stayed at6328 instead of the rewound first-window0; the open overdub note painted, but its closed NoteId vanished. Existing14display cases remained GREEN.
- Root: partial-bar truncation invalidates the old tail window's geometry, while held-frame composition can still discard the replacement candidate. Open-note tails are painted independently, so they appear temporarily and disappear at NoteOff. Record Stop also left its whole preview in the producer that the successor clears.
- Correction: Record Stop uses the existing job-owned preview swap/borrow, O(1), just like overdub Stop. `recordStopPreviewForSlot` qualifies that complete provisional representation by live job slot/LoopId/phase and exposes its existing frozen closure tick when requested. Draw-source base gather filters it into the requested final-geometry window, without wrapping trimmed trailing rows or claiming committed currency. A complete same-interval preview permits a coherent replacement when the old window mismatches; absent it, existing held-window policy remains.
- Record borrow releases and preview clear occur on publication/empty/retirement/supersession, before descriptor reuse. Capacity is reused, not newly copied at Stop. The longer retained Record preview lifetime and any successor preview capacity remain active/finalizing-role costs; no target PSRAM fragmentation/headroom claim is inferred from no new fields or native results.

### One focused independent review

Review found two concrete gaps: retired Record preview rows survived until descriptor reuse, and open Record holds projected to loop-end1535 rather than frozen Stop1400. Both got new REDs first: retirement assertion failed and endpoint expected1400/observed1535. Fixed by clearing existing preview metadata/vectors on the deferred terminal paths and projecting open holds with the job-qualified canonical closure endpoint. Post-fix18/18display PASS, including5distinct Record cycles and identical held-note geometry before/after publication. No second review pass or blanket approval claim.

### Final checks

- Focused related suites: rolling Record retirement/shared finalization/immediate handoff/continuous capture/visual continuation237/237 (pre-review), all included again in final full-native run.
- `PLATFORMIO_BUILD_DIR="$PI_SCRATCH_DIR/native-display-noteoff" pio test -e native`: **3481 total / 7 skipped / 3474 succeeded / 0 failed**,270.34s; display18/18. Scratch log `display-noteoff-final-native.log`.
- `pio run -e teensy41-capture-serial`: **SUCCESS**,46.84s. RAM1 variables96,192 / code389,864 + padding3,352 / locals34,880; floor840 unchanged,2,512B margin. Compared with prior1131e857 build: code+128/padding−128, no RAM1 variables or struct-state addition. RAM2 variables168,576, EXTRAM variables11,680. Scratch log `display-noteoff-final-firmware.log`.
- Final HEX SHA-256 **`7a5b42bb5887f43e49ba4d97eaf5e0b54708e4ab5e49dfe5aa3888068f9358d1`**, NOTuploaded. Earlier8271c39f was a pre-review build, not the final artifact.
- Registered stimuli listed read-only; none changed/run. Fresh pixels/content/timing and external-memory/stack acceptance remain open. No reset/upload/device/SD operation, commit or push.
- Closeout uses the existing display authority and ratified custody/finalization decisions; no new owner, resource class, scheduler or exclusion. Playback first-bar supply and companion-finalization fallbacks are deliberately unchanged, not claimed fixed.

### Subsequent owner-approved upload

Exact final7a5b42bb… image installed via Homebrew CLI2.3, full Programming/Booting **exit0**.
Fresh crash0/loadok and Teensy MIDI USB16C0:0489 verified independently; post-boot status STOPPED,
pending0,queue0/4. No content stimulus/clear or explicit SD mutation commanded. This supersedes
the historical NOTuploaded/installed1131e857 statements above, not the open behavioral hardware gate.
[Upload receipt](2026-10-26-DISPLAY-NOTEOFF-UPLOAD.md).
