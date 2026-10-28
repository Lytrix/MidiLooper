# Display-chain regression execution

## Baseline (task 1.1)

2026-10-28, `feature/unified-rolling-playback-path`, HEAD
`b71fc578332054788b3f4a085d1f57eb709a5357` **plus pre-existing dirty edits**.
Snapshot: `captures/display-freeze-apply-baseline-20261028/manifest.json` and `tracked.diff`.
Tracked-diff SHA256: `bb092b46cfb35c19c39b135605f3e09b4ddcb4cf9eb4b456e77b514b9ed3a441`.
This snapshot records tracked changes and targeted source hashes, not a complete copy of untracked tooling.

`src/Track/TrackDeferredMaintenance.cpp` SHA256:
`1739135b08bf7d586a7d3d293ce978fbb2332bbcd233ec8c82e6a6a82f7ddb83`.
Source baseline has 4-grain/600-us burst and a remaining-playback-time check; the non-capture native build
does not execute capture-gated elapsed-time telemetry/budget checks. Therefore native grain-order results
do not establish target microsecond timing.

Recorded device controls, **not independently re-flashed or re-qualified here**:
- Pre-burst capture `session_20261028_155453.log`, documented image `290e9caa…`.
- Reported regression `session_20261028_162431.log`, documented image `fbab9dcc…`, described as 8 grains/1500 us.
- Current source is not asserted byte-identical to either image. No uploaded image is inferred from HEAD.

## Fixed execution packet

Contract: [design packet A](../design.md#execution-packet-a--regression-observation-and-fixture).
First use native clock and DMA observation seams; no target instrumentation or policy changes.
The A/B mutation is a test-TU-only `STOP_GRAIN_BURST_DISABLED` define before the shared harness includes
the real stop worker. All remaining source and the input/owner trace must stay identical; remove only that
test define after the control run. Source hashes are checked after comparison. This isolates the current
source's burst mechanism, **not** an exact replay of the historical uploaded image.

## Fixture and oracle

Added to `test/test_live_capture_display_boundary/test_live_capture_display_boundary.cpp`:
- `test_overdub_chain_preserves_frame_progress_one_bar`
- `test_overdub_chain_preserves_frame_progress_long_loop` (24 bars)
- `test_stationary_viewport_counts_real_completed_frames_without_transition_receipts`
- `test_chain_oracle_separates_missing_service_from_missing_content`

Setup records and publishes actual Record content (no empty fabricated snapshot). The chain starts the next
overdub before servicing the predecessor's Stop. Each press interval grants 20 Track maintenance opportunities,
interleaving the production seven-owner/extra-Snapshot selector, wrap admission, canonical-priority selector,
playback workers and active load/display continuation. The 20 opportunities are **not** 20 main-loop iterations:
load/display/persistence/window/provision turns are interleaved and do not become extra stop grains. A fixed
80-iteration outer cap detects loss of those 20 opportunities. No rescue drain runs inside the measured chain.

The harness uses real admission policies, Track/Loop workers, DisplayManager phases and DMA I/O seam, but not
the whole `main.cpp` static orchestration, target interval timer, USB traffic or SD contention. It does not prove
the main-loop clock/deadline admission or physical pixel cadence. This limitation is not hidden as native
reproduction of the reported device freeze.

Expected notes are an explicit Record/overdub NoteId+pitch+tick list captured when a frame enters Resolve.
Each completed frame must contain those expected in-window identities exactly once. Inputs accepted after
Resolve belong to the next frame, matching the existing display contract. The same-viewport control observes
four actual submissions and four separate completions; it does not depend on transition receipt emission.
Negative oracle controls reject unchanged phase/submission, omitted NoteId and changed geometry independently.
Existing busy-DMA/live-after-resolve/focus/undo/geometry controls are retained.

Early fixture-construction failures (latest note demanded before three display phases were granted) were fixed
by observing the accepted frontier at Resolve and distinguishing maintenance opportunities from loop iterations.
They are NOT counted as production regression evidence; no final oracle or production threshold was weakened.

## Results — current-source A/B

Commands: `pio test -e native -f test_live_capture_display_boundary`, the built native `program` for the
unfiltered diagnostic output, and a full `pio test -e native`. Logs are kept in the baseline capture directory.

Final fixture state — **PASSED, 40/40 cases**:
- `test_overdub_chain_preserves_frame_progress_one_bar`: 22 accepted overdub presses, **86 completed frames
  during the measured chain**, 0 retained starts, 0 retained stops, final 23 capture passes.
- `test_overdub_chain_preserves_frame_progress_long_loop` (24 bars): **145 completed frames**, same acceptance.
- Maximum observed stop-queue depth stayed within the existing cap of 4; every completed frame passed the
  expected-NoteId/pitch/tick oracle exactly once.

Only `STOP_GRAIN_BURST_DISABLED` in the test TU, identical trace:
- **Identical 86 / 145 completed frames, 0 retained stops, 0 retained starts.** The burst therefore has no
  effect on display-frame progress at this cadence.
- Temporary define removed; final run re-verified 40/40.

### Oracle correction (recorded, not a silent weakening)

The first version asserted a stop must be served inside one press interval. That is stricter than the
contract: DEC-078 accepts and retains a stop and serves it exactly once later. The final fixture keeps
retention legal, counts and prints it, and instead asserts: obligations are served before the next press
(bounded, with a diagnostic failure if not), display frames complete inside the measured chain, and the final
pass count is exactly `1 + accepted`. A raised inner-iteration cap (80→120) only allows the production owner
rotation to reach its 20 maintenance turns when a display phase owns some iterations; the counted turns are
unchanged, so this is not extra domain service.

### Interpretation

- The burst is **not** implicated by this trace, and disabling it does not restore anything. Removing it would
  be an unproven rollback of a working throughput fix.
- Native cannot reproduce the reported freeze: frames and content stay qualified across 22 presses on both
  loop lengths. Combined with the recorded device DMA counters (147 completed transfers inside the alleged
  hole), the remaining candidate is **stale composition/source or viewport content on device**, not missing
  display service. That boundary is device-only.
- Retained-and-restarted chains still expose a separate, pre-existing behavior: starting a new interval while
  a stop is retained loses older intervals' passes (17 of 23). The fixture now avoids that shape because a
  player's next press follows a served interval; it is **not** a display regression and was not repaired here.

## Other verification

- `pio test -e native -f test_shared_capture_finalization`: 142/142 passed.
- `pio test -e native -f test_midi_service_drain -f test_display_dma_health`: 51/51 passed.
- `pio test -e native` (final state): exit 0, 2m04.074s, **3563 cases, 3556 succeeded, 7 skipped, 0 failed**.
  Log: `native-final.txt`.
- `openspec validate display-freeze-under-overdub-chain --strict`: passed. `git diff --check`: passed.
- Production source hashes re-verified identical to the pinned baseline; the temporary burst control define is
  absent. Final fixture SHA256: `180c4101874d828a9b200164c0dcc77189de9fc3e5ef89c0fd1109af5f5a1271`.
- Earlier pre-correction runs of this suite and full native are preserved as `display-baseline*.txt`,
  `display-single-grain*.txt` and `native-full.txt` (2 failing characterizations at that time). They are
  historical, not the final state.
- Firmware build, target diagnostics, upload, reboot, hardware stimulus and SD operations: NOT RUN.

## Readiness

### Existing device evidence corrects the suspected boundary

Read-only parsing of `captures/session_20261028_162431.log` finds:

- At 108.434 s (capture line 3536), `dma_frames=1480`; at 122.870 s (line 3831),
  `dma_frames=1627`. Thus **147 completed DMA transfers in 14.436 s**, inside the alleged
  107.6→123.6 s frame hole; `blocking_frames=0`, `timeouts=0` at both endpoints.
- At 123.877 s (line 3855), `dma_frames=1637`; at 139.111 s (line 4039),
  `dma_frames=1809`: **172 completed transfers in 15.234 s** in the next transition gap.
- `DisplayManager::reportDisplayDmaTransport` reads `dmaCompletedFrameCount()`, not a submission counter.
  These are completed-transfer deltas; they do not prove that framebuffer note content or playhead pixels changed.

Therefore the earlier claim that frames were not served throughout these intervals is falsified by existing
device evidence. The visible symptom could be stale composition/source/viewport content despite healthy
transfers. This is a narrower unresolved question than a complete display-worker/transport freeze. No new
target heartbeat is needed to establish this distinction. Earlier pre-burst captures may contain appended
sessions/resets, so no pooled same-uptime timing comparison against them is asserted.

Packet A is **complete for the native half**: the combined fixture and controls exist, the 22-press chain runs
to exactly 23 capture passes, and the final full native suite is green. Packet B stays **BLOCKED**, for a
narrower and better-evidenced reason than "no reproduction":

- Native frames and content stay qualified across 22 presses with and without the burst, so a firmware change
  cannot be selected or proven from this harness.
- Recorded device DMA counters show transfers continuing inside the alleged freeze, so the surviving question
  is stale composition/source/viewport content on hardware, not display service.
- Device observation therefore needs owner-authorized hardware time at the exact press cadence, with a
  registered stimulus and a fixed-size observation at the demonstrated boundary. Task 2.2 is **not performed**;
  native evidence does not suffice to close the user-visible symptom.

Do not add speculative firmware watchdogs, remove the burst, or relax the content oracle. No production
behavior changed in this round.

## Production repair (DEC-179) — results

Root cause of the reported boot hang, from `session_20261028_162431.log` (second `boot_reset`, line 85168):

- focus `track=7 active=3 selected=3`; `RRQ 7,3 … committed` at 9.04 s after a 205 ms load;
  `cer_win … evcopy=1059` proves the body carried content;
- all **64** slots `terminal … committed`, no `parse failed`, no refusal;
- yet `alloc_census` reported `cap=0 edit=0` in **92/92** samples, `DISP,3` reported `loopLen=0`, and **112/112**
  `DFRAME` samples painted `notes=0` for 198 s, while frames completed ~17/s with `DMA timeouts=0`.

`applyLoadedLoopSlotBodyCommit` returned silently when the LSV1 parity gate disagreed, and the request still
recorded `committed`. An empty slot therefore stayed empty with a success row.

Landed (DEC-179): passes applied into an empty slot (no unverified root, parity `NoCheckpoint`, caches stale);
populated slot keeps its generation and reports `Superseded`; typed outcome `LoopSlotBodyApply`; one row per case;
selected slot paints first in both idle lanes; every committed capture pass marks display ranges.

Verification: `test_storage_loop_io` **64/64**, `test_live_capture_display_boundary` **41/41**,
`test_storage_manager_load` + `test_library_observation_session` **46/46**, full native
**3565 / 3558 passed / 7 skipped / 0 failed** (`native-after-fixes.txt`), `teensy41-capture-serial` **SUCCESS**
with RAM1 unchanged (`variables 96,192 / code 390,584 + padding 2,632`).

Discriminating fixtures: `test_4_2_a_parity_mismatch_into_an_empty_slot_restores_the_saved_passes`;
`test_painted_notes_follow_committed_passes_on_a_stationary_viewport` (failed before the range marking);
retained control `test_4_2_a_failed_parity_leaves_the_existing_slot_generation_intact`.
Three older assertions that pinned "apply neither on parity failure" into an *empty* slot were updated with the
contract change (DEC-179); the populated-slot protection is unchanged.

NOT RUN: upload, boot, hardware observation, SD access. Device acceptance must show the focused slot with
non-zero length and painted notes on first boot, and no regression in stop throughput.

## DEC-183/184 — boot first-paint hold named and released (2026-10-28, device-verified)

**Named (DEC-183).** The ~4 s between the focus restore commit and the first paint was the boot title hold, not a
refusal, a root wait, or the sweep. `#CAP,…,DIAG,paint_gate` (emitted from the owner selection in `loop()`) on
`session_20261028_214829` reads `drain=1, restore=1` with the restore queue draining 24 -> 0 while the selected slot
had already committed: focus restore 6.85 s, `boot_commit` 10.88 s, first content 11.01 s. The queue is the boot's own
admission — each track's selected slot plus two neighbours, 8 x 3 = 24 restores — and `bootInteractiveReady()`
requires all of it.

Two probe defects were fixed before the row told the truth: the RAM2 rate-limit stamp was never initialised (DMAMEM
zero-init is not guaranteed, so the once-per-second gate never opened — this also explains the plain-`Serial` attempt
that produced nothing), and the emit had to move from inside the idle lane to the owner selection.

**Released (DEC-184).** `BootGate::focusSlotSettled` (content present, no focus-restore work owed, no live
`SlotLoadSession`, no in-flight `LoadLoopJob`) releases the hold at the focus commit; the whole-queue path remains the
fallback for an empty selected slot, and the enqueue is idempotent so the post-boot sweep cannot double-queue.

Device, three boots in `session_20261028_215511.log`, measured per boot (the file repeats the boot banner at lines 1,
608, 9706 — pooling them was a reading error, not a device behaviour):

| boot | 7/3 commit | `boot_commit` | first content | full (359) | served twice |
|---|---|---|---|---|---|
| 1 (partial) | 6.66 s | 7.04 s | not captured | - | none |
| 2 | 8.90 s | 9.28 s | 9.37 s | 9.84 s | none |
| 3 | 8.93 s | 9.31 s | 9.39 s | 9.86 s | none |

**Focus commit -> first content: 4.16 s -> 0.46-0.47 s.** Guards: all 26 restores complete (last 15.46 s / 17.03 s)
with the panel live; cache build bounded (12 slice rows, no continuation spin); no capture overflow; RAM1 unchanged.

Gates: `test_boot_focus_handoff` 8/8 (RED before the header), full native **3580 / 3573 passed / 7 skipped / 0 failed**,
`teensy41-capture-serial` SUCCESS, HEX `sha256 0e5d7bfc…`.
