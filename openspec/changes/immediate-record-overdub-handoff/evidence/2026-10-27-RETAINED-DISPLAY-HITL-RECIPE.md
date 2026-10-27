# HITL test recipe — retained display source across a wrap (task 4.19 / task 4.4)

Filled from [`docs/Templates/HITL_TEST_RECIPE.md`](../../../../docs/Templates/HITL_TEST_RECIPE.md), traps checked against
[`docs/Agents/hitl_authoring_traps.md`](../../../../docs/Agents/hitl_authoring_traps.md).
Change: [`2026-10-27-RETAINED-DISPLAY-SOURCE.md`](2026-10-27-RETAINED-DISPLAY-SOURCE.md). **Status: ready to run; not run.**

| Field | Fill in |
|---|---|
| Acceptance criterion (one sentence) | After a Record pass and further overdubs, a loop wrap must not take the Record base off the glass: the piano roll keeps painting committed content while the replacement source is not ready, with no ≥30 s return delay after transport stop. |
| Reused stimulus / flow | **No new stimulus.** The authoritative entry point is the wrapper `scripts/hitl_stop_interruption.py` in the S1.5 long-take shape (`--record-bars 24 --overdub-bars 24 --normal-stop`, the shape that produced `captures/s15-24bar-01`). The registered 2-bar baseline `STIM-BASE-2-2-01` is **not** sufficient here: a 2-bar loop requests the whole loop, so no moving-window provider turn occurs and the defect's route is never entered. |
| New stimulus? | No. (If the owner prefers a registered stimulus over the wrapper's own gestures, the next step is `host_midi_hitl.py stimulus scaffold STIM-BASE-2-2-01 <suffix> --bars 24`, which writes a TEMP variant under `captures/stimuli/` — a deliberate later step, not taken here.) |
| Gestures used | None hand-rolled. The wrapper composes `scripts/hitl/actions/` (`buttons.py`, `slots.py`, `transport.py`, `observation.py`) and `control_constants.py` (record 36, play/stop 40, control channel 16). |
| Expected markers | `DIAG,disp_turn` — Tier-A, ≤1 row / 100 ms while the request stays diverged, emitted from `DebugSessionCapture::displayWindowDrawTurn`; carries `req=`, `pub=`, `n=`, `st=ret\|fresh`, `why=<provider\|hold_gap>`, `key=`. `DFRAME` — per frame (other tier). `VCACHE,idle_src` — per idle slice (other tier), names the gather source. `!CAPTURE_CONTENT` — on demand. |
| Marker tier test | **n.a.** — no new tag. `DIAG,disp_turn` is already classified by both prefix lists (`DIAG,disp_`); the new value is the `why=retained` token inside that existing row. |
| `check_marker_contract.py` output | `OK   DIAG,disp_turn,: emitted=src/Utils/DebugSessionCapture.cpp:2229 tier-a-prefix='DIAG,disp_' (10/31 bytes)` |
| RAM1 impact | **None** — no instrumentation added (the refusal names are in-memory `const char*`; the envelope's storage is `DMAMEM`/external). Padding before → after: `3,224` → `3,224`, `variables 96,192`, locals `34,880`, no re-base. |
| Native test that fails without the change | `test_live_capture_display_boundary` → `test_display_source_progresses_with_pending_successor_stop_and_across_wrap` (fails with the change reverted at *"first wrapped frame must retain Record content while its replacement source is not ready"*), plus fixtures `…_survives_the_wrap_that_appends_a_pass`, `…_stands_down_once_the_sealed_tier_is_released`, `…_never_serves_another_slot_and_survives_the_focus_change`. |
| Boot-census / smoke check | After the flash: attach only, confirm the boot banner and `boot_reset,crash=0`, then `!LIB_OBSERVE` for the census. Expect **no** `CrashReport`, admission `interactive_ready=1`. Do not read absence as evidence: `scripts/hitl_absence_check.py <session.log> 'DIAG,disp_turn,'`. |
| Full scenario entry point | `.venv/bin/python scripts/hitl_stop_interruption.py --track-number <T> --loop-slot <S> --record-bars 24 --overdub-bars 24 --normal-stop --out-dir captures/s4.19-wrap-<date>` — managed run, bounded by `HITL_MANAGED_RUN_TIMEOUT_S` (default 360 s). **This wrapper clears the named scratch slot**, so `<T>`/`<S>` need the owner's authorization in the same session. |
| Verdict rule | Read the rows, not the exit code. **PASS** = (a) **0** rows with `st=ret,why=hold_gap` inside the post-wrap transport-running interval (the defect's own signature: 266 such rows in `captures/session_20261027_084714.log`); (b) **≥1** row with `why=retained` proving the retained envelope served, with `key=` equal to `req=` on that row (the served interval is the requested one); (c) `n=` never collapses across the wrap while content is retained (the failing capture went `67→68→71` then `96→88` and only reached 463 rows 31.7 s after stop); (d) `!CAPTURE_CONTENT` stored/effective parity unchanged. **Missing vs displaced:** a missing `why=retained` with `RING,stats` reporting loss is *not* evidence of absence; with no loss it means the producer never promoted — then read `VCACHE,idle_src` for which gather source ran. |
| Evidence to record | `captures/<run>/session.log` (tail only, or `scripts/parse_*.py`), the `metadata.json`/`invocation.json` pair the managed run writes, and the `DIAG,disp_turn` rows quoted for (a)–(c). |

## Two owner gates before this can run

1. **Flash**: `teensy41-capture-serial` builds green with the pinned RAM1 line; programming a board needs the owner's explicit go-ahead (and a macOS `teensy_loader_cli` 2.3+ per `docs/Guides/TEENSY_FIRMWARE_UPLOAD.md`).
2. **Destructive slot clear**: the wrapper clears the named scratch slot on the card. Per `Destructive-SD-Operations.mdc` the owner must name an authorized track/slot in the same session; the firmware has no restore command.

Order of work: native test (done) → build + RAM1 line (done) → **ask** → smoke check → full scenario → rows read and recorded.
