# Boot-title first-frame repair

## Scope and contract

Owner authorized the title-only first fix in chat; DMA must remain enabled.
Owner: DisplayManager. Production path: main setup -> beginBootOled -> drawBootScreen -> SSD1322_API::display; later update polls completion during boot hold.
Primary firmware file: src/DisplayManager/BootDisplay.cpp.
Existing fixture: test/test_live_capture_display_boundary; native raster and asynchronous display endpoint, real boot drawing and update.
Preconditions: startup calls early OLED initialization then title drawing; no earlier display transfer. SSD1322_API initializes framebuffer and drawBootScreen clears/fills the full frame.
Mutation: remove early initialization's redundant blank clear/submission; title owns the first transfer.
Postcondition: first submitted frame contains title, no competing blank frame; completion is still polled during boot hold.
Preserve: DMA default enabled, immutable in-flight staging, existing hold/readiness, normal render phases, transport fallback/retry, memory budgets, unrelated dirty work.
Non-goals: top-row dots/frozen normal frame attribution, renderer rewrite, new queue/buffer/state, recovery policy, SD/MIDI/content operations or upload without approval.
Commit boundary: independently buildable local fix plus fixture; no Git delivery requested.

## Historical review and readiness

Searched DECISION_LOG for display/DMA/title/boot screen; no superseded owner/transport decision required. Existing archived recoverable-async-display-dma tasks 3.1/4.2 preserve fixed DMA snapshot and boot behavior; runtime spec separately requires hardware qualification, not inference from native tests.
Prior scratch probe compiled actual SSD1322_API.cpp: blank submission -> busy title call -> completion -> repeated polling leaves only one blank submission. Timeout/full-frame SPI fallback/cooldown/DMA retry/recovery passes under simulated bus. Live read-only observation found allocator headroom and progressing DMA counters, not physical pixels.
No architecture conflict or formal reassessment trigger: existing DisplayManager owner, no state/ownership/lifetime transfer. Execution bound unchanged; removes one setup-only fixed-frame submission. No RAM1/PSRAM allocation added. Persistence/undo/playback untouched.

## Oracle fixed before firmware edits

- Fresh display endpoint: beginBootOled initializes but submits zero frames, inactive transport.
- drawBootScreen: exactly one submission, in-flight title raster nonblank with title brightness; zero completions until explicit endpoint completion.
- update while title holds: pending endpoint completes exactly once; completed bytes match title submission, no blank replacement or normal draw.
- finishBootSetup before hold expires: title still held; after hold expiry normal resolve resumes.
Counterexample: original blank submission fails zero-before-title / nonblank-first-transfer assertion. Fixture links real boot methods, not a copied boot sequence. Raster/SPI/DMA hardware endpoints remain simulated; no physical-panel claim.
Bounded control: native time and explicit completion only, no sleeps.

## Verification recipe

Focused: pio test -e native -f test_live_capture_display_boundary; first run must fail the new boot regression against unmodified firmware, repaired run must pass all cases.
Broad: pio test -e native.
Firmware: pio run -e teensy41-capture-serial; preserve RAM1 guard floor without rebase.
Docs: git diff --check; openspec validate runtime-working-set-ownership --strict.
Device: NOT RUN; no new stimulus/marker authored; upload and physical title confirmation require user approval. Existing boot observation entry can collect completion evidence but does not prove physical pixels.
Review: direct review sufficient for removal of redundant setup call; no new lifecycle state or deferred work.

## Local verification before upload

Locally verified; device NOT RUN.

- Baseline regression: `pio test -e native -f test_live_capture_display_boundary` exits1, new boot case Expected0 Was1 at early submission; nine existing cases pass. PlatformIO additionally reports SIGHUP/ERRORED after the assertion, so this is not recorded as a clean failed-suite termination.
- Repair: remove only `clearDisplayBuffer()` from `beginBootOled`, with explanation; title clearing/drawing/submission unchanged. No queue/retry state needed because the competing transfer no longer exists.
- Focused: same command exits0,10/10; actual title font/scaled glyph path, submitted/completed full-buffer equality, readiness/hold and resumption covered. Existing macro-redefinition warning remains.
- Full native: `pio test -e native` exits0,3072 total /3065 succeeded /7 skipped /0 failed;112 suites.
- Capture build: `pio run -e teensy41-capture-serial` SUCCESS. RAM1 variables96,192 / ITCM393,216 (code392,248 + padding968) / locals34,880; guard floor840,128B margin. RAM2 static163,360; EXTRAM11,680. No rebase, no new allocation/state.
- HEX SHA256: `7678b719f2c26c21942baff617ff7780a1f3f5d71fd75dc3b3ba5d78185e463b`; built, NOT uploaded.
- Direct review: main setup is the sole production caller of `beginBootOled`, followed immediately by complete `drawBootScreen`; standalone `DisplayManager::setup` retains its clear. Transport driver/fallback/polling unchanged. This is removal of redundant setup work, not a non-trivial lifecycle change requiring independent review.
- `openspec validate runtime-working-set-ownership --strict` valid; `git diff --check` clean before final documentary closeout.
- Stimulus catalog listed with `.venv/bin/python scripts/host_midi_hitl.py stimulus list`; existing base boot route remains available, no canonical stimulus changed. No managed content run or device/SD action performed for this fix; a content run is not permission inferred from upload approval.
- Session closeout: runtime scope/status, local task and display guide updated; no architectural decision/ownership/bound/persistence change, no new DEC entry or shipped-deliverable claim. No Git delivery requested.
- Only boot-title local task eligible for checkoff. Physical title visibility, top-row dots/frozen normal frames and parent display/continuity acceptance remain open.

## Authorized upload and physical-panel confirmation

Owner approved upload in chat. Preflight `captures/boot-title-preflash-01/shutdown.json` reports complete/drains2/ok true; no incomplete-shutdown latch. Existing SerialLog capture plus DeviceSession canonical drain and MidiClockMaster realtime Stop were reused; no toggle, recording, slot clear or SD-destructive action.
`pio run -e teensy41-capture-serial -t upload` SUCCESS; HEX SHA256 remains `7678b719f2c26c21942baff617ff7780a1f3f5d71fd75dc3b3ba5d78185e463b`, RAM1 floor intact.
Postflash `captures/boot-title-postflash-01/serial.log` has fresh `DIAG,boot_reset,crash=0`, DMA ready at20MHz, Boot setup complete and boot_commit at11.559s. DMA completions advance to228; blocking_frames0/degraded0/timeouts0/rejections0/recoveries0. No observed FATAL or DMA_STAGING_CORRUPT. Complete heap inspection: internal heap_tail147,456B, free chunks36,504B/largest35,424B; external free4,931,436B/largest gap4,850,976B. Postflash shutdown complete/drains2/ok true, no latch; device left stopped/drained.
Owner explicitly confirms: "yes the title appeared and the display is not hanging any more". This establishes physical title visibility and absence of the reported hang on this boot, alongside serial DMA progress with no SPI fallback. Broader display/timing/long-duration/continuity gates are not certified. No repeat build/test/firmware change or Git delivery during upload follow-up.

## Narrow Git delivery

Owner requested commit after hardware confirmation. Full native re-run passes3072total/3065succeeded/7skipped/0failed. Commit scope is the one-call firmware correction, display guide, this evidence and only title-specific additions to shared runtime/deliverable/tasks documents. The native boot regression resides in the pre-existing untracked live-display suite, whose fixture/production dependencies include unrelated dirty work; that suite and those dependencies remain uncommitted rather than being absorbed into this fix. Native results above describe the tested working tree, not a clean-checkout qualification of unrelated pending slices. No push or PR requested.
