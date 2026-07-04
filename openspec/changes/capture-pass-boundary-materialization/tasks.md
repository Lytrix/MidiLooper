# Tasks — capture-pass-boundary-materialization

## 1. Incremental capture sanity

- [x] 1.1 Add `CaptureIncrementalSanity` module (pair-close, wrap slice, budget slice)
- [x] 1.2 Add `LoopEventValidation::repairOrphanNoteEvents` shared helper
- [x] 1.3 Wire `Loop::appendCaptureEvent` pair-close hook
- [x] 1.4 Add `Track::processCaptureIncrementalSanity` + main loop call

## 2. Hot stop (Q16 + verify)

- [x] 2.1 `removePairsShorterThanNoteMinLength` on `sealCapture` and `finalizeLoopAtStop`
- [x] 2.2 `verifyCaptureHotStop` — canonical mask log-only

## 3. Tests + docs

- [x] 3.1 Native `test/test_capture_incremental_sanity/`
- [x] 3.2 Native `test/test_capture_note_min_length/`
- [x] 3.3 Update LOOP_MIDI guide tier 1b

## 4. Deferred (Q9)

- [ ] 4.1 Boundary split at capture pass edges — separate design session
