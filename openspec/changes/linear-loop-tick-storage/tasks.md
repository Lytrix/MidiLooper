# Tasks — linear-loop-tick-storage

## 0. OpenSpec and docs

- [x] 0.1 `proposal.md`, `design.md`, delta specs, `tasks.md`
- [x] 0.2 Handoff [`docs/plans/linear_loop_tick_storage_enhancement.md`](../../docs/plans/linear_loop_tick_storage_enhancement.md)
- [x] 0.3 Park `note-edit-tick-coordinates-and-audition` — [`PARKED.md`](../note-edit-tick-coordinates-and-audition/PARKED.md)
- [x] 0.3.1 Spec gaps closed — dual normalize boundaries, edit closure set, projection separation, set-window fader rules
- [x] 0.4 Append DECISION_LOG — [DEC-013](../docs/DECISION_LOG.md#dec-013-linear-loop-tick-validate-vs-normalize)
- [ ] 0.5 Register in PROJECT_STATE / CURRENT_WORK when prioritized

## 1. Subsystems + invariants (Phase 1)

- [x] 1.1 `LoopTickNormalize` — scoped `normalize()` + `NormalizeScope` / `NormalizeOptions` (Phase 1 refactor)
- [x] 1.2 `LoopEventValidation` — check registry
- [x] 1.3 Refactor `validateAndCleanupMidiEvents` — orphan repair only
- [x] 1.4 Native `test/test_loop_tick_normalize/`
- [x] 1.5 Native `test/test_loop_event_validation/`
- [x] 1.6 Projection boundary comment in `Track::ensurePlaybackWindowBuilt`
- [x] 1.7 Write-back audit
- [x] 1.8 **Dual normalization boundary inventory**:

| Hook | API | Scope | Phase |
|------|-----|-------|-------|
| `publishDependentFaderLatch` (after staged pipeline) | `normalizeWindow` | **Edit closure set** | 2 |
| `commitAllPendingNoteEditActions` | `normalizeAll` | Full session store | 2 |
| `closeNoteEditPass` / undo snapshot push | after `normalizeAll` | — | 2 / 5 |
| Capture / overdub stop → `commitCapturePass` | `normalizeWindow` (capture closure) | 3 |
| `setLoopLengthWithWrapping` | `normalizeAll` | 4 |
| `EditApply` pass replay (geometry ticks) | `normalizeWindow` on closure | 5 |

## 1.9 Dev SD reset (before SD HITL)

- [ ] 1.9.1 `resetDevelopmentPersistence()` — extend `nukeHitlSetsCatalog` to wipe `current/` + RAM index
- [ ] 1.9.2 Serial `!DEV_RESET_SD` + `#CAP,PERS,dev_reset_sd,…,ok|failed`
- [ ] 1.9.3 Operator steps in handoff

## 2. Note edit geometry (Phase 2 — fixes 152335)

- [x] 2.1 **Canonical mutation** — `NoteMovementUtils` linear off writes; no `% loopLength` on storage mutation; no normalize mid-`moveNote`
- [x] 2.2 **Dual normalize wiring** — `buildEditClosureNoteIds`; micro at `publishDependentFaderLatch`; macro `normalizeAll` at `commitAllPendingNoteEditActions`
- [x] 2.3 **Set window fader range** — current: full-loop window; F1/F2 via `NoteEditLengthFaderMapping` `% loopLength` + `selectableDisplayNotesForEditUi` window filter (partial slide deferred §2.7)
- [x] 2.4 **Playback verification** — Tier 2 session store in `ensurePlaybackWindowBuilt`; non-edit **`mergeMaterializedPassesWithCapture`**; HITL §2.6 pending
- [x] 2.5 Native: 1344→1345 move preserves linear off@1536 at macro commit
- [ ] 2.6 HITL: 152335 repro
- [x] 2.6.1 **Focus linear ticks** — `syncNoteEditFocusLinearFromSessionStore` before pre-commit; move/length focus uses linear off; re-select baseline from session (fixes spurious `ChangeLength` on wrapped re-select)
- [x] 2.6.2 **Re-select display** — `finalReconstructAndSelect` by `NoteId`+start; `pruneOverlapNotesBeforePreCommit`; linear overlap baselines in `upsertOverlapNote`
- [x] 2.6.3 **Normalize synth guard** — Pass-2 skip promotion when span to loop end > 49 ticks (moved wrap collapse); scrub stale wrap-head offs on in-loop move
- [x] 2.6.4 **Pitch/reselect ghosts** — linear focus sync after pitch; mover excluded from pitch-lane restore; micro-before-macro at commit; scrub by noteId
- [ ] 2.7 **Future (out of scope):** partial set-window slide when F2 hits min/max on long loops (see design D13)

## 3. Capture / stop / overdub (Phase 3)

- [ ] 3.1 `LoopStopFinalize` — linear off where possible; `normalizeWindow` on capture closure at stop commit
- [ ] 3.2 Open capture notes allowed until stop; invariants after macro/pass commit
- [ ] 3.3 Update `test_loop_stop_finalize`

## 4. Loop length change (Phase 4)

- [ ] 4.1 `setLoopLengthWithWrapping` — `normalizeAll` at boundary; no tick remap on extension
- [ ] 4.2 Projection: shorten hides notes; lengthen restores from storage (native tests)
- [ ] 4.3 HITL stretch + shorten/restore

## 5. SD load, passes (Phase 5)

- [ ] 5.1 `StorageLoopIo` load — canonical check mask; reject non-canonical
- [ ] 5.2 `LoopPasses::materialize` / `EditApply` — linear geometry; normalize at pass boundaries
- [ ] 5.3 `change-length-commit-rematerialize` paths
- [ ] 5.4 Undo snapshots after `normalizeAll` at macro boundary

## 6. Verification + docs (Phase 6)

- [ ] 6.1 `pio test -e native` — full suite
- [ ] 6.2 Regression matrix scenarios
- [ ] 6.3 HITL full matrix — see [HITL map](#hitl-regression-map)
- [ ] 6.4 Update NOTE_WRAPPING_LOGIC + LOOP_MIDI_STORAGE
- [ ] 6.5 `/opsx:archive`

## Regression matrix → test map

| Spec scenario | Test |
|---------------|------|
| Move across loop end | `test_loop_tick_normalize` + HITL 152335 |
| Resize across loop end | native edit apply + HITL |
| Extend loop length | native stretch + HITL |
| Shorten / lengthen restore | native projection + HITL |
| Undo wrapped edit | `test_note_edit_session_undo` or native |
| Save/load round-trip | `test_storage_loop_io` |
| Overdub wrapped | `test_loop_stop_finalize` |
| Same-pitch overlap | `test_edit_apply` |
| Playback unchanged | native playback order + HITL |
| Display wrap | `test_noteutils_reconstruct` |
| Session-store playback during edit | HITL verification (§2.4) |

## HITL regression map

| Scenario | Preset / args | Serial verify |
|----------|---------------|---------------|
| Move past loop end (152335) | edit HITL; on@1344 len 191; move +1 | linear off preserved; no off@0 |
| Loop stretch | extend 2→4 bars after wrapped note | derived length unchanged |
| Shorten / lengthen restore | shorten below note on; lengthen back | note reappears |
| Fader regression | note edit fader baseline | F2–F4 after micro+macro normalize |
| Session playback during edit | long loop + edit during playback | session store audible (Tier 2) |
| Full-loop fader wrap | window = loop length | F2 wrap at extremes |

## Operator checklist

1. Flash `teensy41-capture-serial`
2. `!DEV_RESET_SD` → confirm `#CAP,PERS,dev_reset_sd,…,ok`
3. Re-record loops
4. HITL regression matrix
