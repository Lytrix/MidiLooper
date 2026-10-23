# 3.15f — device leg, session 3 (2026-10-23): paired NOTE_EDIT content, but the lane does not serve the preview

**Change:** `windowed-playback-generation` · **Task:** 3.15f · **Branch:** `feature/unified-rolling-playback-path` ·
**Commit:** `26912ab6` (firmware unchanged; host tooling and this record follow) ·
**Status: content oracle paired; three focused fixes are green and the windowed preview **serves** on the device
when the lane is free (runs 07/09). The display/loop‑prefix stall hypothesis is **falsified** (display max 2.3 ms,
over=0; largest stream gap 1.58 s), so the display refresh stays the single revision owner; a requester‑cadence caller
was tested and **reverted** (run 13: no preview build ever). The blocker is the **committed lane's retained
demand/source on the shared runtime** (`retained_identity_refreshed`), which the session's preview must displace while
NOTE_EDIT is open. 3.15f stays open.**

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

## Focused fix landed (same day): a foreign promise no longer strands the preview

`servicePreviewWindowForNoteEditSession` now distinguishes **whose** promise holds the shared pending-demand slot. A
retained committed/capture promise is a foreign promise, not this session's standing demand: the service proceeds to the
preview request (the preview retains its own promise if refused, and the committed handoff is re-requested at the exit).
This lane's own preview promise still returns, so the ordinary cadence never stacks demands. No admission path, constant
or envelope changed.

- `test_3_15f_c_a_foreign_pending_promise_does_not_strand_the_preview_demand` — **RED before** (`1 failed, 36
  succeeded`), **GREEN after** (37/37 then 38/38). The fixture stages the committed promise in the shared slot, opens
  NOTE_EDIT, mutates and calls the production service; before the fix the preview never built.
- `test_3_15f_c_a_committed_current_yields_at_the_positioned_handoff` — the committed current keeps serving until the
  positioned handoff, then the preview is adopted and served. Green, so the native admission/position/wait is **not** the
  device defect.
- Gates after the fix: preview **38/38**; RWO **239/239**; finalization **95/95**; prewarm **247/247**; overlay edge
  **7/7**; full native **3,422 / 7 skipped / 3,415 succeeded**; both images at the exact pins (default `391,752 + 1,464`,
  qualification `391,992 + 1,224`). New HEX: qualification
  `7acd996f78c54af4419e3b4d958c35539987fd1e611356df29387e7f57919ef2`, default
  `8932881d740dedf0b933f9eb07bfe017943196253c732db355ee62573081e627`.

### Device rerun on the fixed qualification image — builds now, serving still committed

`captures/2026-10-23-s315f-lane-handoff-05`: the same live edit (entry focus 200; 64 `200 -> 1448`; content PASS) now
produces **three `note_edit` build rows** (previously zero): revisions 1, 2 and 3 at 82.65/82.80/83.88 s. So the fix
unblocks the preview admission/build on the device. But the served stream still keeps the committed 64@200
(0.62/2.28/4.53/6.28/8.53/10.28/12.53/14.28/16.53 s), the edited 64@1448 never sounds, and there is no preview
`playback_read` — only `playback_available` rows with `origin=Committed`. The route oracle therefore still fails.

The remaining gap is therefore **not** the admission/position/wait (the native committed-current fixture adopts at H,
and the device builds the positioned revision) but the device's **live NOTE_EDIT serving/owner turns**: the positioned
preview completion is not adopted/served while the committed available reader runs. The next device-facing diagnostic is
the preview ready's publication/adoption state at its H on the running device (a witness/row for the ready and the
NOTE_EDIT resolver), or a native fixture that drives the live `playback_available` reader with a positioned preview
ready.

## Second/third focused fixes (same day): the preview serves on the device

The native fixtures found the two remaining mechanisms and both are green:

- **A bound committed frame must not defer the prepared successor at its H.**
  `resolveNoteEditPreviewSourceForTurn` returned early on `boundToOtherLanesWindow` before its adoption block. On the
  device the committed reader's frame is bound to the committed current, so the positioned preview was never adopted.
  The seam now computes `positionedHandoffReached`/`previewSuccessorDue` before that check and lets a due successor
  outrank the binding. RED: `test_3_15f_c_a_committed_current_yields_at_the_positioned_handoff` with the committed
  frame staged (`38 test cases: 1 failed`), GREEN after.
- **The preview's own retryable "not now" refusal is re-attempted.** A `SuccessorAlreadyPrepared`/`BuildInFlight`
  (or a position-invalidated / cleared-budget) own promise is not terminal; when the role frees the service proceeds to
  the request. The next device run (`handoff-06`) showed the failure shape: the preview's **own** session-open promise
  was retained, so **no** `note_edit` build appeared at all. RED:
  `test_3_15f_c_an_own_prepared_refusal_is_reattempted` (`1 failed`), GREEN after (`39/39`).
- Gates after the three fixes: preview **39/39**; full native **3,423 / 7 skipped / 3,416 succeeded**; pins unchanged
  (`391,752 + 1,464` / `391,992 + 1,224`). New HEX: qualification
  `49d849eec1476bfe265b92c9022612a95a2e730f3b3b8f100d70e1c5054dfca8`, default
  `d9350f365b26985cce351f9b0e1fea314e5f5c67963e9dee7e1ee9e10f37d68e`.

### Device rerun on `49d849ee…` — the preview now serves; the served revision lags the edit

`captures/2026-10-23-s315f-lane-handoff-07` (same gesture; content PASS):

- **The preview serves.** 22 `playback_read` rows with `origin=1`; the first at absolute tick **4,608** (host 89.2 s),
  storage generation 15. The committed stream serves before it. This is the first device evidence of the windowed
  NOTE_EDIT route actually serving on the short loop.
- **Two remaining defects, both measured:**
  1. **The first served handoff is 4,608, not the C-priced 3,072.** The build/ready missed the 3,072 occurrence and
     the miss→reposition path (DEC-147) selected the next loop; the latency from the demand is **3,937 ticks** (from
     the entry 4,512). C's rule is honoured (4,608 is the next eligible boundary: lead 3,937 ≥ the bound), but the
     device's first H was missed, so the measured latency is one loop above the native 2,972 figure.
  2. **The served preview carries the pre-edit session revision.** The movement is at device time 78.956 s
     (`EditSessionAction type=3 start=1448`); the preview's revision-3 `note_edit` build only appears at **95.17 s**
     (16 s later), after the revision-1/2 preview has already been served at 4,608. The emitted channel-4 stream keeps
     the committed 64@200 (12.53/16.53 s) and the edited 64@1448 never sounds. The session's **preview revision bump**
     runs on `flushDeferredNoteEditDisplayRefresh` (the control-surface deferred refresh); on the device it did not
     reach the lane within the play window.

The next RED is therefore the live revision flush: the NOTE_EDIT movement's preview revision must reach the lane
promptly (the device's `scheduleDeferredNoteEditDisplayRefresh`/`processDeferredNoteEditDisplayRefresh` cadence under
transport), so the served preview is the edited revision. The C admission/position/wait and the adoption path are now
green natively and the preview serves on the device.

### Run 08 (32 s play) — no preview build; and the display/loop‑prefix stall hypothesis is falsified

`captures/2026-10-23-s315f-lane-handoff-08` (same image, longer play after the edit): **no `note_edit` build at all**
(`playback_build=1`, the committed `snapshot`), 129 committed `playback_read` rows, and the committed 64@200 eight times;
the edited 64@1448 never sounds. The movement was at device time 139.7 s.

The combined `PERF` row is **capped at 256 bytes** (`char line[256]` + `snprintf` in
`HotPathTelemetry::emitSummary`), so its display fields were cut at the source. A dedicated short
`PERF,display,s=…,max=…,avg=…,over=…` row was added (image `5d16cf96…`) and measured in run 09:

- `PERF,display,s=2830,max=2306,avg=322,over=0` and `s=3807,max=2306,avg=344,over=0` — the display update's maximum is
  **2.3 ms** against a **30 ms** budget, with **zero** over‑budget samples.
- The largest gap between consecutive device‑timestamped serial rows in the whole run is **1.58 s** (the ~1.5 s
  display‑frame cadence); there is no 16 s loop stall. The stream continues through the interval.

So the **display/loop‑prefix stall hypothesis is false**: the display refresh is healthy and the deferred refresh is not
starved. The single preview‑revision owner (the display refresh) is therefore **correct**, and no window‑owner revision
advancement was added.

What run 09 actually shows is a **service‑cadence/guard** delay, not a revision‑owner or display problem. The movement is
at 82.831 s; the session‑open preview (revision 1, the committed content) is what serves at 4,608 (93.041 s); the
movement's revision‑3 `note_edit` build only starts at **99.053 s**, after the served window. The preview revision does
reach the lane (the revision‑3 build exists) — the request after the flush is refused/deferred by the lane's
serving/completion/ready state and re‑attempted only on the window‑owner turn.

The working hypothesis at run 09 was the preview service's **re‑attempt cadence after a revision bump** (the native
`test_3_15d_slice3_an_edit_while_the_preview_serves_is_carried_by_the_next_revision` covers the steady case). Run 13
below tests a requester‑cadence caller and shows the real blocker is the committed lane's retained demand, not the
cadence.

### Run 13 — the requester‑cadence caller was tested and reverted; the blocker is the committed lane's retained demand

A per‑loop requester caller was added to `ControlSurfaceManager::update` (after the deferred refresh; **not** a revision
owner — the display refresh still bumps the revision) and measured in `captures/2026-10-23-s315f-lane-handoff-13`
(image `5b3d1e89…`). The playing session opened at focus 200, the edit committed `200 -> 1448` (content PASS), but the
preview **never built**: the only build rows are `snapshot` (393.898 s) and **`retained_identity_refreshed`**
(426.951 s); there is no `note_edit` row, 71 read rows are all committed, and the committed 64@200 sounds five times.

`retained_identity_refreshed` is the **committed** lane's retained demand (`refreshRetainedIdentity…` on the shared
`pendingViewRequest_`). The preview request therefore coalesces/refuses behind the committed lane's active
demand/source on the shared runtime: the session's preview never gets the lane, even with a more frequent requester.
The caller is **not** the fix and was reverted (the board/build return to `5d16cf96…` = `df05ea78`'s diagnostic image);
the native suites were not affected and the pins are unchanged. The display refresh remains the sole revision owner.

The next RED is the **lane arbitration**: while NOTE_EDIT is open the session's preview is the content the transport
should carry, so the committed lane's retained demand/source on the same slot must yield to it (the committed handoff is
re‑requested at the exit). A native fixture with the committed retained demand/source active plus a NOTE_EDIT preview
demand must show the preview admitted/served — RED before that arbitration.

## Native contract (unchanged; for the comparison)

Timed successor adoption GREEN (`test_3_15f_c_the_successor_waits_for_its_positioned_tick`); preview **36/36**; RWO
**239/239**; finalization **95/95**; prewarm **247/247**; full native **3,420 / 7 skipped / 3,413 succeeded**; runtime
**7,920 / 10,120**; images **391,752 + 1,464** (default) and **391,992 + 1,224** (qualification). Measured two-bar
NOTE_EDIT latency **2,972 ticks** (1.94 loops, ~7.7 s at 120 BPM) at the fixture's demand phase (100 → deadline 3,072);
the device's demand phase (~665) targets the same 3,072 boundary, i.e. a latency of ~2,407 ticks from that demand.

## Not claimed

No 3.15f checkoff, no route parity, no cutover. The NOTE_EDIT **content** oracle is paired (on the pre-fix images; the
live probe's content half re-passes on the fixed qualification image), and three focused fixes are native RED→GREEN.
The **windowed preview now serves on the device** (runs 07/09: `origin=1` read rows; first at tick 4,608) — the serving
route itself is exercised. The display refresh is healthy and is the **single** revision owner (the dedicated
`PERF,display` row measured 2.3 ms max / 0 over budget; largest stream gap 1.58 s), so the earlier loop/display‑stall
suspicion is retracted; no window‑owner revision advancement was added. A per‑loop requester caller was tested
(run 13) and **reverted**: with it the preview never built (`retained_identity_refreshed`, no `note_edit` row). The
blocker is the **committed lane's retained demand/source on the shared runtime**: while NOTE_EDIT is open the session's
preview must displace/defer it (the committed handoff is re‑requested at the exit). The first served handoff also
missed the priced 3,072 (miss→reposition to 4,608). The lane arbitration owns the next RED; 3.15f stays open.
