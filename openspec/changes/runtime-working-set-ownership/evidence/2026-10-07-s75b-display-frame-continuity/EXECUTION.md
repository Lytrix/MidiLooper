# S7.5b.6 — display frame continuity during Record Snapshot publication

## Identity and scope

- **Task:** `runtime-working-set-ownership` S7.5b.6 — keep the existing piano-roll draw source responsive while the committed Record Snapshot is being built.
- **Existing owners:** `DisplayManager::resolveDisplayNotesByDrawSource` composes/retains the displayed frame; `Loop` owns `visualCache` and Snapshot publication; the existing `Loop::rebuildVisualCacheIdleSlice` remains the only idle cache builder. No shared playback/display representation is introduced.
- **Evidence:** `captures/s64-37c-04/host_midi_hitl_windowed_post_stop_drain_20261007_015406.log`: `DISP` update gaps reach about 5.06 s in the 21.31 s Record stop-to-Snapshot-publication interval; the capture also has `RING,shed,127634` and `RING,tier_a_evicted,3`, so serial cadence is diagnostic, not proof of physical panel cadence. User reports display lag during bars 1–11.
- **Relevant authority:** runtime spec “Display compose reuses owned frame storage” and visual-tier requirements; `DEC-115` — refused derived projection retains the previously served content and retries; `DEC-081` — display work cannot block playback/persistence. No display authority is moved to playback or Snapshot construction.
- **Non-goals:** new renderer, second visual cache/validity counter, per-bar `LoopSnapshot` roots, full-loop materialization on paint, replacing the draw-source pipeline, guessed millisecond budget, or changing playback/persistence policy.

## Oracle fixed before behavior edits

Production-path fixture using the real draw-source entry and current display frame storage, with a RecordPublished stale-root state and a controlled Snapshot owner.

1. Seed a complete visible frame for the active slot, then commit a Record take that invalidates its visual revision and queues session-exit Snapshot construction while transport continues.
2. Interleave real `DisplayManager::resolveDisplayNotesByDrawSource` calls with Snapshot construction and the playback-view owner across the visible-window movement. Assert the displayed frame is never cleared, partially adopted, mixed across identities, or left on the wrong playhead window; a failed/refused replacement keeps the prior complete frame.
3. Where the existing draw-source contract supplies a complete committed/preview bar for the visible interval, compare exact `(NoteId, pitch, start, end)` rows against an independent per-bar oracle. Once the current Snapshot is published, assert atomic adoption of its draw source and no duplicate rows.
4. Assert display resolution does not call a synchronous whole-loop rebuild, consume Snapshot builder state, or alter the playback window; the Snapshot and persistence owners continue making progress.
5. Red proof: disable the stale-gap frame-retention/adoption rule and require the fixture to observe blank, stale-window, or mixed-frame output at its exact failing assertion.

## Bounds and controls

- Preserve the existing `DisplayManager` frame and `Loop` visual-cache ownership; reuse pending-paint backing and existing source identity/revision.
- Display may retain the last complete frame while no complete replacement is available; it must not publish a partial Snapshot candidate as a complete draw source.
- The native fixture proves content/identity transition correctness, not physical OLED refresh cadence. Any numeric display freshness limit belongs to S7.5a and must be measured before qualification.

## Verification recipe

- Baseline/focused: `pio test -e native -f test_loop_take_survival` and the focused display integration/test suite touched by the production draw-source path.
- Broad: `pio test -e native`; default `teensy41-capture-serial` build and RAM1 guard.
- Docs: strict OpenSpec validation for `runtime-working-set-ownership`; `git diff --check`.
- Device: new authorized post-stop display/playback capture with pinned firmware identity and loss-free critical rows; no upload without authorization.

## Completion record

 - **Production change:** `DisplayManager::resolveDisplayNotesByDrawSource` now retains the prior complete frame and its original draw-source key throughout the stale-root gap; it no longer supplements that frame from unpublished Snapshot candidate rows or applies late source recovery into the retained frame. The complete candidate is gathered after the current root is published.
 - **Fixture / focused native:** replaced the Loop-level unpublished-supplement clone with `test_gap_handoff_keeps_complete_frame_until_current_snapshot`, which asserts the retained complete-frame content stays unchanged through a partial Snapshot step and a replacement is gathered only after current-root publication. Focused display utilities **125/125**, loop-take survival **143/143**, shared finalization **66/66**, ownership **177/177**; full native **2,859 / 2,852 succeeded / 7 skipped / 0 failed**; both capture builds pass RAM1; strict OpenSpec validation and `git diff --check` pass.
 - **Still not established:** the fixture does not drive the real `DisplayManager::resolveDisplayNotesByDrawSource` entry through repeated playhead-window movement and display composition; therefore the packet's production draw-source parity/identity oracle and its red proof are still owed. No device display cadence or hardware acceptance. S7.5b.6 remains unchecked.
