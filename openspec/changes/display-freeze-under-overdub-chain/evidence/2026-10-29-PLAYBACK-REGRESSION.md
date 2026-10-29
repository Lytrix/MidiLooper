# Playback regression recovery — execution packet

Owner request: repair silent playback after the retained-Start stall changes, preserving all recorded
slots. Baseline: `2edb1f2e`, dirty `feature/unified-rolling-playback-path`; earlier session's edits preserved.

## Evidence / G0 — implementation discovery

- VERIFIED: retained-Start stall-fix call sites are already absent from the current tree. Its unused
  declarations are not evidence of a running gate. No playback-hot-path diff exists against HEAD.
- VERIFIED: `session_20261029_012142.log` has eight `#PAINT_BEAT` rows, advancing play ticks and zero
  output deltas after the first sample. That first delta includes control feedback and does not prove
  playback. The stopped `#STOP_OUTPUT barrier=1` does not establish a playing-state output barrier.
- VERIFIED: the compatibility playback gather returns no events without a root or retained Record
  extent (`LoopSnapshotPlayback.cpp`). A restored passes-only slot can have neither, and no publication
  request. A launch target requests on-demand publication; an already-active playing slot does not.
- UNKNOWN: which earlier edit first made the device reach this state. The previous claims of an
  orphan-release-caused lost publication, damaged saved data, and barrier-caused global silence are
  withdrawn, not premises for this packet.
- Owners/lifetimes: Track selects slot and deferred opportunity; Loop owns committed passes and its
  private publication candidate; its existing publisher alone installs the immutable root. MIDI consumes
  the existing merged/window reader. Storage and undo keep their existing ownership and identities.
- Alternate paths: active and layered sounding slots, other tracks, current/stale roots, already-pending
  publication, stopped/inactive slots, live capture and pending stop jobs must be distinguished.
- Bound: reuse the existing Snapshot construction quantum. No pass gather, publication or new allocation
  on the MIDI tick; no source-lifetime redesign, new resident state, source pins, or SD writes.
- Historical review: searched DECISION_LOG for rootless, publication, DEC-108, DEC-150, DEC-175,
  DEC-180, DEC-191; reuse the existing `requestCommittedPassesRepublish` operation and Snapshot owner.
  No new playback source, format, root-current waiver or publication-during-live-capture policy.

G0 PASS for testing this missing-producer boundary; origin of the reported regression remains UNKNOWN.

## Oracle fixed before production edits

Seed two tracks with bar-spaced committed passes, remove their settled roots to model the loaded
passes-only shape, and leave no pending publisher. Include an enabled layered slot and a disabled slot.
Use production boot prewarm, Start, Snapshot maintenance, request/rolling maintenance and MIDI tick.
At fixed native clock/ticks, bounded Snapshot turns must make the sounding slots current without
restarting an in-flight candidate; disabled slots must remain rootless. Then compare emitted pairs
against seeded pitch/tick/channel/NoteId, including a second playback cycle. No re-record, pass removal,
direct injection of a ready playback window or direct publication stepping in the acceptance drive.

The discriminating baseline failure is a sounding slot still having no root/source after the bounded
production owner drive. Controls: stopped slots do not trigger bulk hydration; existing source jobs and
current roots are retained; live capture/stop ownership is not bypassed. Native ordering is not physical
MIDI timing proof.

## Contract (G1/G2 — same-owner recovery)

Proposed minimal mutation, subject to the RED: when the existing Snapshot turn has no pending work,
select one enabled playing slot lacking a root. Request its existing on-demand committed-pass republish
once, then use the unchanged publisher and construction quantum. Never clear/restart pending work,
never queue all loaded slots, never recover while a live capture/stop owns the Track, never replace a
stale-but-selected root. Remove only this session's unproven barrier and load-time rebuild patches.
No commit, upload, device reset, SD manipulation or re-record is implied.

Primary files: `TrackSlotPool.cpp`, `TrackDeferredMaintenance.cpp`; native production harness:
`test_runtime_working_set_ownership`. Focused regressions: `test_continuous_capture`,
`test_storage_manager_load`, `test_storage_loop_io`. Incremental target build:
`pio run -e teensy41-capture-serial`; unchanged RAM1 floor. Device gate remains open pending upload
permission and a registered/reused non-destructive playback stimulus.

## Focused independent contract review

Review verdict: PASS WITH TIGHTENING (review session `ses_e7ae111c0ffeQj554v4Nxgm6Zs`). Incorporated:

- Recovery predicate is Track PLAYING, allocated/enabled slot, nonzero length, committed passes,
  no selected root (not merely stale), no publication pending anywhere on the Track, no capture/stop,
  no open overdub session, and an Empty session-exit claim. Slot enablement uses the owning Track,
  not UI focus. One private query is shared by selection and admission to avoid a blocked sibling.
- After admission the Loop's existing request owns completion; focus/slot changes do not retarget it.
  Disable/Stop prevents new recovery; existing publication obeys its existing completion/cancellation
  policy. No receipt/claim settlement is added. Existing jobs always have priority.
- Admission enumerates the pass/edit identity inventory and may allocate the existing pending box;
  only construction is quantum-bounded. This reuses the launch request operation, never moves that
  operation to the MIDI tick, and makes no physical timing guarantee.
- Main's selected/background dispatch was traced: `canServiceBackgroundSnapshotOwnerTurn` already
  admits playing tracks without a pending publication. The test must drive that predicate and the
  existing round-robin background opportunity, not grant every Track unlimited turns.

## Completion evidence

Fixture RED: `pio test -e native -f test_runtime_working_set_ownership`, source-less sounding slots
remain source-less after 20,000 turns. All 246 existing cases passed; one new behavioral failure,
runner SIGHUP/ERRORED at failing exit (not a green suite). Log:
`/private/var/folders/ms/9n65q9vx7gxf9k911t93v34w0000gn/T/opencode/playback-regression-red.log`.
Production mutation: implemented under DEC-192. Two new cases in `test_runtime_working_set_ownership`:
`test_playing_pass_only_slots_recover_their_missing_playback_source` and
`test_playback_source_recovery_preserves_stopped_disabled_capture_and_claim_owners`.
The source test now includes the ordinary committed requester/rolling owner while recovery and
MIDI consumption run; the Snapshot opportunity is one background Track/unit with unrelated UI focus.
Exact output: **288** accepted events over two complete 24-bar cycles across three sounding slots,
including NoteId, tick, type, pitch, channel, velocity and exact count (zero sink drops).

Checks:

- `pio test -e native -f test_runtime_working_set_ownership -f test_continuous_capture -f test_storage_manager_load -f test_storage_loop_io -f test_live_capture_display_boundary`: exit 0,
  **500/500** (248/107/36/66/43), no skips/errors. Log `playback-regression-focused.log` in the same
  approved temporary directory as the RED. Later strengthened requester control: same runtime suite
  exit 0, **248/248**, log `playback-regression-requester-control.log`.
- Mutation: replace only the recovery request guard with `false && predicate`; same source assertion
  fails, other 247 cases pass. Restored. Log `playback-regression-mutation.log`.
  Repeated against the strengthened ordinary-requester oracle: same behavioral failure/247 controls
  pass (`playback-regression-mutation-final.log`); restore verified **248/248**, exit 0
  (`playback-regression-final-native.log`).
- `pio run -e teensy41-capture-serial`: incremental build exit 0, 33.978 s. RAM1 variables 96,192,
  code 391,656, padding 1,560, locals 34,880; RAM2 variables 169,888. No budget rebase.
  HEX SHA-256 `c4841c911ed0b746a12f444475afb5455a16954547758075948963508b6b48ea`.
- Same focused independent reviewer checked the actual patch: APPROVE; no blocking issue, limitations
  explicitly retained for post-admission clear/load/undo/focus interleavings (unchanged existing owners).
- Full native intentionally not repeated (owner requested incremental/focused verification, no PR/push).
- `.venv/bin/python scripts/host_midi_hitl.py stimulus list` completed. Existing content presets seed/
  overwrite scratch content and were **not run** on the owner's saved set. Device acceptance needs a
  non-destructive saved-set playback stimulus/recipe and upload authorization; no device operation,
  source diagnostics or serial markers were added. Device gate remains OPEN.

### Authorized flash-only follow-up

Owner approved upload, then explicitly narrowed execution: "only flash the device I will do the test".
No hardware test or transport/content gesture performed. GUI loader quit to keep one uploader active.
Approved HEX hash rechecked before programming:
`c4841c911ed0b746a12f444475afb5455a16954547758075948963508b6b48ea`.
Homebrew `/opt/homebrew/bin/teensy_loader_cli` 2.3 reported `Found HalfKay Bootloader`,
`Programming`, `Booting`, exit 0. Full transcript: `captures/playback-recovery-flash-20261029/upload.log`.
Separate `pio device list` confirmed `/dev/cu.usbmodem154944801`, VID:PID `16C0:0489`,
description `Teensy MIDI`. No serial port opened for testing; no boot-load/crash-status assertion.
Device playback acceptance/task 5.4 remains OPEN for the owner's test. No re-record or SD operation.

Session closeout: runtime scope/state, decision log, playback/bounds docs and task status updated.
`git diff --check` and `openspec validate display-freeze-under-overdub-chain --strict` both pass.
No content/SD migration, no commit/PR/push; upload only as authorized above. Barrier/load-time causal patches removed;
originating regression attribution and the reverted retained-Start gate remain explicitly unclosed.
