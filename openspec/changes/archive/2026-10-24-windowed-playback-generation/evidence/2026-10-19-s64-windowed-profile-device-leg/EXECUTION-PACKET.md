# 6.4 — windowed content-verification profile, device leg on the current tree (F-7 boundary half, F-8 servable half, AC-8)

Authority: task **6.4** (`tasks.md`), DEC-108, requirement **A window is served without
whole-root residency** (F-8, `spec.md` line 579) and **Readiness evidence is delivered where it is
produced** (AC-8). Companion verification split: task **6.5**; content-parity owner: task **4.3**
(F-7); residency owner: task **4.4** (F-8). This packet is a G2 execution packet, not evidence that
the task passed.

## Identity and scope

- **Packet/task IDs:** `6.4` (profile exercise on the current tree), with `4.3` (F-7 boundary
  crossings), `4.4` (F-8 servable-while-demoted) and AC-8 read from the lane's rows.
- **Approved contract:** `STIM-WINDOW-CONTENT-01@v1` / preset `windowed_content_parity`
  (`scripts/hitl/stimuli/registry.py`, bench target track 8 / slots 4-5); DEC-108; the two
  requirements above.
- **Status:** specified (edit → fixture n.a. → this device run).
- **Owner and production entry → deferred worker → commit/retirement:**
  `Track::serviceRollingPlaybackWindow` / `Track::stepPlaybackViewMaintenance` (demand + service) →
  window build worker → `build_complete` publication / frozen Record-return completion
  (`Track::completeAdmittedRecordStop`). Entry point: the registered preset via the canonical runner.
- **Primary files:** `scripts/hitl/scenarios/windowed_content_parity.py`,
  `scripts/hitl/scenarios/long_loop_display_window.py`,
  `scripts/hitl/verify/windowed_lane.py`.
- **Preconditions present:** device attached (`/dev/cu.usbmodem154944801`); no shutdown latch
  (`captures/.device-shutdown-incomplete.json` absent); qualification build SUCCESS with RAM1
  `variables 96,192 / ITCM 393,216 (code 392,376 + padding 840) / locals 34,880` (floor 840);
  `git diff 689ec471..HEAD -- src include` **empty**, so the board's retained qualification image
  (`0b4b6fe2…`, commit `689ec471`) is source-identical to HEAD (`a9d41abf`).
- **External blockers:** none for this exercise. The deferred terms (`content_parity` verdict,
  per-slot residency row set) are named below and are **not** claimed here.
- **Non-goals:** no firmware change; no oracle weakening; no switch-target leg (`--switch-target-slot`
  not used — the record-return completion is the registered demand shape); no all-route cutover
  (3.15f), no emitter retirement (3.7b), no 6.5 timing/envelope claim.

## Oracle fixed before behavior edits

The four `must_prove` terms of `STIM-WINDOW-CONTENT-01`, read by `hitl.verify.windowed_lane`:

| Term | Judged here | Pass condition |
|---|---|---|
| `readiness_receipts_under_transport` (AC-8) | yes | ≥1 `#CAP,<us>,DIAG,win_frozen_readiness,…` row inside a running-transport interval, **source-qualified** (names the window that became ready) |
| `windowed_service_coverage_under_transport` (AC-8) | yes | `win_gap` blocks with `running=1` and opportunities > 0; zero named `win_gap_overrun` under transport |
| `content_parity_across_window_boundary` (F-7) | **partly** | boundary `build_complete` publications under transport ≥2; the parity **verdict** is the content pipeline's (this run does not claim it) |
| `root_demoted_residency_over_loop_lengths` (F-8) | **no** | deferred: the per-slot residency row set is not identified |

`oracle_complete` stays `False` by construction, so a green run is never read as F-7/F-8
qualification.

## Controls and coverage

- **Counterexample that must fail:** a run whose only readiness row is the cadence-bounded
  `win_readiness` **after** the transport stops (`under_transport=0`) must not pass AC-8 — this is
  exactly the mistake the s39 packet names (run 2's look-alike pass).
- **Ring integrity control:** `capture_evidence_ring_intact` must show `tierA_evict=0`,
  `overflow=0`, `crit_rej=0`.
- **Reuse:** the existing long-loop content-verification leg, unchanged; no new stimulus, no new
  marker.

## Verification recipe

- Host: `.venv/bin/python -m unittest scripts.test_windowed_content_parity_verify` → 51 tests OK.
- Native: `pio test -e native -f test_runtime_working_set_ownership` → 214/214 PASSED.
- Build: `PLATFORMIO_BUILD_DIR="$PWD/.pio-build-local-fw" pio run -e
  teensy41-capture-serial-window-qualification` → SUCCESS, RAM1 line above.
- Device: flash the build (dedicated HITL preset), post-flash managed drain, then
  `.venv/bin/python scripts/host_midi_hitl.py run --preset windowed_content_parity --track 8
  --loop-slot 4 --midi-channel 8 --build-env teensy41-capture-serial-window-qualification
  --no-boot-reset --record-bars 24 --out-dir captures/<dir>`.
- Verdict rule: the combined verifier's `ok` **and** the row-level terms above; a missing decisive
  row is taken via `scripts/hitl_absence_check.py` against `RING,stats` before being read as absence;
  `INCONCLUSIVE_CAPTURE` is not a pass.

## HITL test recipe (template `docs/Templates/HITL_TEST_RECIPE.md`)

| Field | Fill in |
|---|---|
| Acceptance criterion | On the current qualification image, the registered windowed profile produces a demand, source-qualified frozen readiness receipts and ≥2 window publications **under a running transport** on the demoted long-loop target, with no named overrun and an intact capture ring. |
| Reused stimulus / flow | `STIM-WINDOW-CONTENT-01@v1` / preset `windowed_content_parity` (reuses `run_long_loop_display_window`). |
| New stimulus? | No (canonical stimulus untouched). |
| Gestures | Only via the registered runner: slot select, reused leg's record/stop + NOTE_EDIT window gestures, transport stop for the drain (`windowed_content_parity.py`; `control_constants.py`). |
| Expected markers | No new marker. Reads existing Tier-A: `win_frozen_readiness`, `win_gap`, `win_gap_overrun`, `playback_build` (`build_complete`), `win_gate`, `RING,stats`. |
| Marker tier test | n.a. (no new tag). |
| `check_marker_contract.py` | n.a. (no new marker). |
| RAM1 impact | No instrumentation: qualification padding 840 (floor 840), unchanged. |
| Native test that fails without the change | n.a. (no source change; this is a device exercise). |
| Boot-census / smoke check | Post-flash managed drain (`status=complete, drains=2, ok=true`) before the leg. |
| Full scenario entry point | The registered preset command above; managed-run timeout 360 s (default). |
| Verdict rule | Combined verifier `ok` + the four-term table above; a `win_readiness` row only after the stop does not satisfy AC-8. |
| Evidence to record | The combined verifier JSON, the retained lane rows, the leg report, `metadata.json`/`invocation.json` (hex, commit), the post-run drain, and the absence check for any missing decisive row. |

## Completion record

### Verdict — the 6.4 lane exercise is green on run 2; F-7/F-8 are **not** claimed

**Identity.** `git_commit` `a9d41abf` (`git_dirty true` — evidence/doc files only, no source change),
build env `teensy41-capture-serial-window-qualification`, `firmware_hex_sha256`
`6cf5f874e8c3e08510f9e5312874cc95dca34fcb3da6c5b67fe9a7f2fba8fcd4` (the flashed bytes — the board was
flashed with this build, so metadata and the board agree), RAM1 `96,192 / 393,216 (code 392,376 +
padding 840) / locals 34,880`, CLI track 8 / slot 4 / channel 8 / `record_bars 24` (firmware rows 0-based
track 7 / slot 3, `len=19200` — the record target is the demoted long-loop target in this base leg shape).

**Preconditions.** Build SUCCESS; host verifier suite **51 OK**;
`pio test -e native -f test_runtime_working_set_ownership` **214/214**; flashed the freshly built image
(so the pinned HEX is exactly the flashed bytes); post-flash managed non-reset drain
`captures/i5-s64-windowed-parity-01-preflight/` (`status=complete, drains=2, ok=true`) so the boot's
restore sweep and deferred save settled before the leg.

**Run 1 — the case-1 artifact, diagnosed rather than smoothed.**
`captures/i5-s64-windowed-parity-01` (`host_midi_hitl_windowed_content_parity_20261019_174653.json`):
combined `ok: False`, `leg_exit_code: 2`, leg issues `missing_bounded_window_disp_rows`,
`insufficient_disp_during_note_edit_freeze count=0`, `missing_playing_bounded_window_disp`. **The lane
passed all six checks** (ring intact; demand 410 gap-work blocks / 25 receipts; `source_qualified=29 of
171` frozen under transport; coverage 439/463 at `permitted=64`; 0 overruns; **404** `build_complete`
under transport, `loop_len 768`, revisions {0,1}) and the snapshot passed all three. **Cause:** the reused
leg verifies **inline, pre-drain**, and its display oracle pairs rows by **stream position**
(`_rows_between` / `_rows_after_line`), while the retained ring delivers rows produced under transport only
after a stop/idle flush — so the leg could not see them. Read with the **unchanged** oracles: the inline
snapshot carries **5 053** lines and `long_loop_disp_row_count=0`; the complete capture carries **20 505**
lines, **859** DISP rows, **292** bounded. The complete read clears two of the three issues; the remaining
`insufficient_disp_during_note_edit_freeze` is the **same** pairing gap, not an absent row — the device did
produce a valid bounded row during the freeze (line 17939, device 165.833 s, `window_bars 16`,
`window_start 4876`), just downstream of the leg's position-based slice. The lane oracle already made this
fix in its own time base (`hitl/verify/windowed_lane.py::row_us`, lines 147–163); the leg's
`hitl/verify/display_window.py` is still position-based. s39's pass was luck: its slot-4 pre-existing take
was already 24 bars, so early pre-clear bounded rows fed the `freeze_disp_sparse:marker_fallback`.

**Run 2 — the clean combined verdict (re-run on the same boot).**
`captures/i5-s64-windowed-parity-02` (`host_midi_hitl_windowed_content_parity_20261019_175301.json`):
combined **`ok: True`, `issues: []`, `leg_exit_code: 0`**.

- **Leg `ok=true`** with exactly the s39 warnings (`freeze_disp_sparse:marker_fallback`,
  `hold_disp_sparse:accepted_play_stop_hold_release duration_ms=3102`); `long_loop_disp_row_count=6`,
  `disp_window_row_count=9`, `hold_verify_mode=indirect_button_hold`.
- **Lane passed all six checks:** ring intact (`tierA_evict=0, overflow=0, crit_rej=0`); demand 399
  gap-work blocks / 24 receipts; `readiness_receipts_under_transport` **`source_qualified=27 of 218`
  frozen under transport**, first `adopt_wait/e2_boundary` token 2309 interval `6144+768/19200`; coverage
  425/505 at `permitted=64`; `overrun_rows=0` under transport; `build_complete_under_transport=391`,
  `loop_len 768`, revisions {0,1}.
- **Snapshot passed all three** (`snapshot_progress_during_recording`,
  `snapshot_owner_continues_after_stop`, `record_session_exit_root_published`).
- Shutdown `complete, drains=2, ok=true`; shutdown latch absent.

**Against the acceptance criterion.** The 6.4 lane verdict on the demoted long-loop target is met: a demand
was observed, source-qualified frozen readiness receipts arrived under a running transport, ≥2 boundary
`build_complete` publications occurred under transport, no overrun was named, and the capture ring stayed
intact. The `oracle_complete: False` envelope is unchanged, so this is **not** read as F-7/F-8
qualification: the **F-7 parity verdict** (task 4.3, the content pipeline's) and the **F-8 residency row
set** (task 4.4, unresolved) remain deferred exactly as the four-term table above records.

**Named, not smoothed.** Run 2's leg pass rides the `freeze_disp_sparse:marker_fallback` on the pre-clear
take, as s39's did — the leg's display oracle is still position-based while the lane's is time-based, so a
green leg is currently compatible with the pairing gap that made run 1 fail. If the pairing is wanted
closed, the next candidate slice is the `hitl/verify/display_window.py` time-based pairing fix: a
**host-tooling** change, not a firmware change and not an oracle weakening, and not required for the lane
verdict above. **6.4 stays open** on the F-7 parity verdict and the F-8 residency row set; no firmware line
changed this session.
