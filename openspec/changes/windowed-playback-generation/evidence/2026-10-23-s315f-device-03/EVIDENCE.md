# 3.15f — device leg, session 3 (2026-10-23): paired NOTE_EDIT content, but the lane does not serve the preview

**Change:** `windowed-playback-generation` · **Task:** 3.15f · **Branch:** `feature/unified-rolling-playback-path` ·
**Commit:** `26912ab6` (firmware unchanged; host tooling and this record follow) ·
**Status: content oracle paired; route oracle FAILS on the device. 3.15f stays open.**

## Images

| Image | HEX SHA-256 |
|---|---|
| default (`teensy41-capture-serial`) | `561b6feb5b86fbbda6ff3428f21237f5aa6b8ebb871055d7c31e975dab757497` |
| qualification (`teensy41-capture-serial-window-qualification`) | `e1c07416f1d18ae91c2d4b88b53db697e307853fb80e0089c1d4aaee344ac316` |

Both were flashed with Homebrew `teensy_loader_cli 2.3` (134-baud reset, `Found HalfKay Bootloader`, programming,
`Booting`, exit 0) and booted separately (`16C0:0489`, `Teensy MIDI`, `BOOT,load`). No GUI was running.

## Host fixture fixed: the actual nav slot, a required acknowledgement, and commit on depart

The 2026-10-22 step-based probe is replaced in `scripts/hitl_note_edit_min_probe.py`:

- the target is the freshly captured seed's **actual persisted tick**, and the nav slot is built from that seed's
  `SelectNavigation` layout (no hard-coded step tick);
- the stopped playhead is positioned at the target tick before the session opens, so the session bracket selects the
  intended note (a fresh session otherwise restores the previous bracket — measured at 392 while the playhead was 200);
- the edit is sent only after a fresh firmware acknowledgement names the intended tick
  (`#DBG select_apply selected_tick=…`); an empty step or any other tick fails the leg before the fader gesture;
- the coarse destination is the farthest **reachable** empty 16th step (a one-step delta is below the significant-movement
  threshold; step 31 is unreachable by the mapping; step 0 is the fader rail);
- NOTE_EDIT is departed so the pending edit **commits** before the content dump (`EditSessionDepart` commits; the earlier
  probe dumped too early and saw the pre-edit content);
- every other persisted note must keep its tick, and the previous persisted focus (pitch 55) is asserted explicitly.

### Paired result (same sparse seed on both images)

Seed: `8/60, 200/64, 392/67, 584/60, 872/64, 1064/65, 1256/69, 1352/55`.

| Image | Selection ack | Moved | Post rows | Note 55 |
|---|---|---|---|---|
| qualification | `selected_tick=200` (`unchanged_note`) | 64 `200 -> 1448` | `8/60, 1448/64, 392/67, 584/60, 872/64, 1064/65, 1256/69, 1352/55` | `1352` unchanged |
| default | `selected_tick=200` (`unchanged_note`) | 64 `200 -> 1448` | identical | `1352` unchanged |

The probe's earlier accidental `1352 -> 344` (stale focus reuse) is now a permanent negative: the previous focus and all
other notes are asserted unchanged. Evidence:
`captures/2026-10-23-s315f-lane-editfixture-seed-04`, `captures/2026-10-23-s315f-lane-noteedit-probe-22`,
`captures/2026-10-23-s315f-default-editfixture-seed-04`, `captures/2026-10-23-s315f-default-noteedit-probe-22`.

## Route oracle: the lane never serves the NOTE_EDIT preview on the short loop

`scripts/hitl_note_edit_handoff_probe.py` (new) drives the same edit **while the transport runs** on the qualification
image: stop-position the playhead at 200, start the host clock at 120 BPM, settle 12 clocks (the earlier one-clock entry
raced the transport and hit the `win_track_not_playing` gates), enter NOTE_EDIT (entry focus `200`), move the selected
note 64 `200 -> 1448` at demand tick ~665, keep playing 768 clocks (four two-bar loops), stop, drain the lane's retained
rows, depart (commit), dump.

- **Content:** PASS — post rows `8/60, 1448/64, 392/67, 584/60, 872/64, 1064/65, 1256/69, 1352/55`; note 55 unchanged.
- **Lane rows:** all **71** `playback_read` rows for track 4 / slot 0 are `origin=0` (**Committed**); the only
  `playback_build` row is path `snapshot` (the committed window); there is no preview `build_complete`, no
  `win_readiness`, and no preview source receipt. The `win_gate` rows show the edit-active state (`ed=1`) with the
  rolling lane excluded, which is expected — the NOTE_EDIT preview is a separate requester.
- **Emitted MIDI:** the committed 64@200 sounds every cycle (host times 0.532, 4.532, 8.531, 12.533, 16.533 s); the
  edited note 64@1448 (which would follow 55@1352 by 96 ticks) **never appears**. The edit never became audible while the
  session was open.
- **C arithmetic:** demand ~665 → the earliest loop boundary whose absolute lead clears the DEC-136 minimum is
  **3,072** (`665 + 1,792 = 2,457`; 1,536 fails). No preview frame is served at 1,536, 3,072 or later.

Evidence: `captures/2026-10-23-s315f-lane-handoff-04` (`run_serial.log`, `midi.log`, `handoff.json`). A first live run
(`handoff-01`) is the control for the stale-bracket failure: it moved 67@392 instead of 64@200, and its post content
shows `392 -> 1448` for pitch 67.

## Cause hypothesis and the next RED

The preview demand is not admitted/served while the committed window/ready/pending occupies the lane. Two concrete
suspects, both on the preview service's own path:

1. `servicePreviewWindowForNoteEditSession` returns on `runtime->pendingViewRequest_.active` — a retained
   `SuccessorAlreadyPrepared` / `BuildInFlight` refusal is a promise, but the preview service never re-attempts it after
   the committed role frees (the committed/pending consumer only serves CommittedSnapshot/Launch sources).
2. `retainRefusedPlaybackViewRequest` computes the retained deadline with `handoffLeadTicks` (the next occurrence), not
   C's `earliestEligibleHandoffLead`; a retained short-loop preview promise is therefore positioned at an impossible
   next occurrence, not at the C boundary.

The next fixture must be **native and RED**: a committed window serving (and/or a committed ready successor) plus a
NOTE_EDIT preview demand; the preview must be admitted at the C-positioned boundary and served there, with the committed
content continuing until then. The fix must not add a second admission path, must not change the DEC-136 envelope, and
must keep the miss/supersession rules.

## Native contract (unchanged; for the comparison)

Timed successor adoption GREEN (`test_3_15f_c_the_successor_waits_for_its_positioned_tick`); preview **36/36**; RWO
**239/239**; finalization **95/95**; prewarm **247/247**; full native **3,420 / 7 skipped / 3,413 succeeded**; runtime
**7,920 / 10,120**; images **391,752 + 1,464** (default) and **391,992 + 1,224** (qualification). Measured two-bar
NOTE_EDIT latency **2,972 ticks** (1.94 loops, ~7.7 s at 120 BPM) at the fixture's demand phase (100 → deadline 3,072);
the device's demand phase (~665) targets the same 3,072 boundary, i.e. a latency of ~2,407 ticks from that demand.

## Not claimed

No 3.15f checkoff, no route parity, no cutover. The NOTE_EDIT **content** oracle is paired on both images; the **route**
oracle (the windowed preview serving the edit at the positioned boundary) fails on the device and owns the next RED.
