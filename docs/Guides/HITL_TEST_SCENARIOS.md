# HITL test scenarios — overview

Hardware-in-the-loop (HITL) tests drive a connected Teensy over USB MIDI and (when required) USB serial
from the host Mac. Entry point: [`scripts/host_midi_hitl.py`](../../scripts/host_midi_hitl.py).

**Firmware:** use `teensy41-capture-serial` so `#CAP`, `ST`, and track undo/redo lines are available. Default HITL run assumes firmware is already on the device; pass `--build-upload` only when you changed firmware.

**Serial capture modes:**

| Mode | Serial | HITL flags |
|------|--------|------------|
| **B (preferred)** | `capture_session.py` in terminal 1 | `--follow-current-session` — no `--serial-port` |
| **A** | Built into HITL | `--serial-port` + `--boot-settle-ms 10000` — no `capture_session.py` |

Never use `--serial-port` while `capture_session.py` holds the USB serial port.

## Result contract (host harness)

Machine-readable verdicts live in [`scripts/hitl/contract.py`](../../scripts/hitl/contract.py). Plan: [`hitl_framework_hardening_refinement.md`](../Plans/hitl_framework_hardening_refinement.md).

| Verdict | Meaning |
|---------|---------|
| `PASS` | Evidence complete, setup transitions confirmed, firmware assertions pass |
| `FAIL` | Evidence complete, stimulus valid, setup confirmed, firmware contradicted |
| `INCONCLUSIVE_CAPTURE` | Required evidence missing or ambiguous — do not conclude firmware |

Control actions return `CONFIRMED`, `ALREADY_SATISFIED`, `RECOVERY_FAILED`, or `EVIDENCE_UNAVAILABLE`. A retry press is allowed only when evidence after the action baseline proves the requested transition did not happen.

Serial evidence is fresh to the current action baseline. Capture integrity (`COMPLETE` / `INCOMPLETE` / `UNKNOWN`) is separate from firmware assertions: truncated or rotated session logs, a dead managed `capture_session.py` child, or missing serial lines are `INCONCLUSIVE_CAPTURE`, not a firmware fail. Transport liveness (`BAR`/`DFRAME`/…), firmware-progress liveness, and serial-reader liveness are separate signals.

Process exit: `0` PASS, `2` FAIL, `3` INCONCLUSIVE_CAPTURE. Do not rewrite an inconclusive verdict to pass or fail.

**List scenario IDs:**

```bash
.venv/bin/python scripts/host_midi_hitl.py run --list-scenarios
.venv/bin/python scripts/host_midi_hitl.py stimulus list
.venv/bin/python scripts/host_midi_hitl.py run --list-stimuli
```

**Verify-only** (replay a captured serial log without MIDI):

```bash
.venv/bin/python scripts/host_midi_hitl.py run \
  --scenarios <scenario_id> \
  --verify-serial-log captures/<log>.log --verify-only
```

Replace `/dev/cu.usbmodem154944801` with your port (`pio device list`).

---

## Presets, scenarios, and stimuli

| Kind | Meaning |
|------|---------|
| **Preset** | Named bundle of one or more scenarios (`--preset base`) |
| **Scenario** | Single composable phase script (`--scenarios edit_overdub_during_note_edit`) |
| **Stimulus** | Versioned live-MIDI recipe the scenario must send (`STIM-NE-OD-COMPOSE-02` v1 everyday compose; `STIM-NE-OD-COMPOSE-03` v1 last-bar Shorten; `STIM-NE-OD-COMPOSE-04` v1 last-bar Shorten then reopen select/move; `STIM-NE-OD-COMPOSE-01` v1 paint-only) |

Presets are defined in [`scripts/hitl/registry.py`](../../scripts/hitl/registry.py) (`PRESET_SCENARIOS`).
Stimuli are defined in [`scripts/hitl/stimuli/registry.py`](../../scripts/hitl/stimuli/registry.py).
A host serial snapshot for a bound scenario starts with `#HITL,SCENARIO,…` and `#HITL,STIMULUS,…`.
The raw Teensy `#CAP` session log stays firmware-only.

```bash
.venv/bin/python scripts/host_midi_hitl.py stimulus list
.venv/bin/python scripts/host_midi_hitl.py stimulus export-yaml
.venv/bin/python scripts/host_midi_hitl.py stimulus temp STIM-NE-OD-COMPOSE-01 A
```

`stimulus list` is the everyday catalog (`run --list-stimuli` prints the
same lines). `export-yaml` dumps the Python registry. It is a view, not a
second authority. `stimulus temp` writes `captures/stimuli/<id>.yaml` and
does not register the variant. Attach a TEMP header to a live run with
`--stimulus-temp-suffix A` (optional `--stimulus-temp-parent`).

BASE-N primitives: **STIM-BASE-2-2-01**, **STIM-BASE-2-ONE-OD-01**,
**STIM-BASE-2-FIXTURE-01** (2-bar `EDIT_RECORD_FIXTURE`; used by
`edit_minimal` and `edit_overdub_during_note_edit` seeds).

Plan: [`hitl_stimulus_contract_enhancement.md`](../Plans/hitl_stimulus_contract_enhancement.md).

---

## Scenario catalog

| ID | Preset | What it exercises | Serial required | Host verifier |
|----|--------|-------------------|-----------------|---------------|
| `base` | `base` | **STIM-BASE-2-2-01** v1: Clear → record → overdub(s) → optional global undo/redo after last overdub | Recommended | Legacy baseline JSON report |
| `edit_record_prelude` | `edit_full`, `edit_minimal` | 2-bar fixture record for edit suites | Recommended | Record transitions |
| `edit_minimal` | `edit_minimal` | Prelude → enter edit → add/delete/move/length → exit | Recommended | Session enter subset |
| `edit_full` | `edit_full` | Prelude → full note-edit overlap suite | Recommended | Legacy edit baseline gates |
| `edit_overdub_during_note_edit` | `edit_overdub_during_note_edit` | **STIM-EDIT-OD-DURING-NE-01** v1 on **STIM-BASE-2-FIXTURE-01** seed: **record_seed** (EDIT_RECORD_FIXTURE) → overdub → edit → in-edit overdub → **E:** session undo/redo → exit → global undo | **Yes** | `verify_edit_overdub_during_note_edit` |
| `long_loop_display_window` | `long_loop_display_window` | 24+ bar record → NOTE_EDIT window freeze → play/stop long-press snap → hold-to-track | **Yes** | `verify_long_loop_display_window` |
| `revision_commit_save` | `revision_commit_save` | Transport stop (current epoch) → `!REV_COMMIT` → `!REV_CLEANUP` (no catalog pollution) | **Yes** | `verify_revision_commit_save` |
| `revision_load` | `revision_load` | Transport stop → `!REV_COMMIT` → `!REV_LOAD` → `!REV_CLEANUP` | **Yes** | `verify_revision_load` |
| `revision_load_record` | `base`, `revision_load_post_record` | Base record/overdub → commit → load (loop data) | **Yes** | `verify_revision_load` |
| `fader_motor_probe` | `fader_motor_probe` | NOTE_EDIT arm + fader motor pitchbend/note-0 steps (host → Teensy → DROID) | Optional | `verify_fader_motor_probe` |
| `fader_motor_sweep` | `fader_motor_sweep` | Quarter sweep 0 % → 25 % → 50 % → 75 % → 100 % on fader1 (default) | Optional | `verify_fader_motor_probe` |
| `note_edit_select_dependent_faders` | `base`, `note_edit_select_dependent_faders` | **base** (2+2 + 2 overdub) then NOTE_EDIT F1 sweep + toggle | **Yes** | `verify_note_edit_select_dependent_faders` |
| `overdub_occupy_paint` | `overdub_occupy_paint` | **STIM-OCCUPY-PAINT-01** v1: Clear/arm → seed record → first overdub half-bar phrase → second overdub occupy (same-start hide / +1/4 shorten / −1/2 hide) → 2 undo then 2 redo | **Yes** | `verify_occupy_paint` |
| `note_edit_overdub_compose` | `note_edit_overdub_compose` | **STIM-NE-OD-COMPOSE-02** v1: **base** (2-bar record + **one** overdub) then NOTE_EDIT open → in-edit overdub at the selected seed pitch → same-pitch hold move; verifier requires Hide or Shorten; close must not drop `apply_owned` (`canonical=0`). Paint-only leftover: `STIM-NE-OD-COMPOSE-01` (live 12–23) | **Yes** | `verify_note_edit_overdub_compose` |
| `note_edit_overdub_compose_last_bar_shorten` | `note_edit_overdub_compose_last_bar_shorten` | **STIM-NE-OD-COMPOSE-03** v1: same seed and same-pitch live Capture as `02`, then move the seed onto the new note at beat 4 of the last bar; verifier requires Shorten (`EditSessionAction` type=1). Do not edit `01` or `02` | **Yes** | `verify_note_edit_overdub_compose` |
| `note_edit_overdub_compose_last_bar_reopen` | `note_edit_overdub_compose_last_bar_reopen` | **STIM-NE-OD-COMPOSE-04** v1: `03` plus exit NOTE_EDIT, re-enter, select the seed, and move it. Do not edit `01`, `02`, or `03` | **Yes** | `verify_note_edit_overdub_compose` |
| `note_edit_reselect_stacked` | `note_edit_reselect_stacked` | **STIM-NE-RESELECT-STACKED-01** v1 on **STIM-BASE-2-FIXTURE-01**: NOTE_EDIT, move a higher-pitch fixture note onto an occupied sixteenth, deselect, F1 intra-step reselect; PLAYING motor sync is `value_only` | **Yes** | `verify_note_edit_reselect_stacked` |

### `revision_commit_save` (packed revision write + cleanup)

Requires `teensy41-capture-serial` (SESSION_CAPTURE). Host sends serial `!REV_COMMIT` / `!REV_CLEANUP`; firmware backs up `index.bin` / `set.bin`, commits `v####.bin`, then restores catalog state and deletes the test revision.

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset revision_commit_save \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 \
  --phase-wait-ms 500 --press-ms 120 \
  --prior-save-drain-wait-ms 120000 \
  --deferred-save-wait-ms 120000 \
  --revision-commit-wait-ms 180000
```

Run after `base` preset: the scenario waits for the prior deferred save to finish before `!REV_COMMIT`. Serial may emit `#CAP,...,PERS,rev_blocked,...,deferred_save_active` while blocked.

Use `--skip-hitl-cleanup` only when debugging a failed commit (leaves revision on SD).

### `revision_load` (packed revision commit + deferred load + cleanup)

Requires `teensy41-capture-serial` (SESSION_CAPTURE). Commits a revision from Current, loads it back with `!REV_LOAD <setId> <revisionId>`, then restores catalog state with `!REV_CLEANUP`.

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset revision_load \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 \
  --phase-wait-ms 500 --press-ms 120 \
  --prior-save-drain-wait-ms 120000 \
  --deferred-save-wait-ms 120000 \
  --revision-commit-wait-ms 180000 \
  --revision-load-wait-ms 180000
```

Use `--skip-hitl-cleanup` when debugging a failed load. Optional dev wipe of all sets: `--nuke-sets-before-run` (synchronous SD — can stall USB; not part of default pass criteria).

Host unit test: `scripts/test_revision_load_serial_verify.py`.

### `revision_load_record` (base record → commit → load)

Runs canonical **`base`** record/overdub on the selected track (preset injects bar-synced 2+2 baseline flags from HITL-Test-Flow), then **`revision_load_post_record`** (skips transport-stop prelude and the pre-commit deferred-save drain — base already flushed loops to SD). Use this when verifying loop data round-trips through a revision.

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset revision_load_record \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5
```

### `base` (record/overdub baseline)

Default second overdub pass is on (2 bars, C0–B1). Undo/redo after last overdub stop is on by default.

**Mode B (preferred)** — start `capture_session.py` first, then:

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset base \
  --midi-out "Teensy" --midi-in "Teensy" \
  --follow-current-session \
  --track-number 5 --midi-channel 5 \
  --record-bars 2 --overdub-bars 2 \
  --no-fixed-grid-notes --start-transport \
  --overdub-start-delay-bars 0 --overdub-start-delay-beats 1 \
  --phase-wait-ms 500 --final-wait-ms 3000 --press-ms 120 \
  --undo-redo-delay-ms 3000
```

**Mode A** — single terminal, built-in serial (no external capture):

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset base \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --boot-settle-ms 10000 \
  --track-number 5 --midi-channel 5 \
  --record-bars 2 --overdub-bars 2 \
  --no-fixed-grid-notes --start-transport \
  --overdub-start-delay-bars 0 --overdub-start-delay-beats 1 \
  --phase-wait-ms 500 --final-wait-ms 3000 --press-ms 120 \
  --undo-redo-delay-ms 3000
```

Opt-in firmware flash before run: add `--build-upload` (restarts Teensy — restart capture after).

OpenSpec 5.5 adds `VERIFY deferred persist nonblocking` on this preset: sliced `LoopPersist` `PERS,work` after record stop and no `PERS,drain` on the `base` path.

OpenSpec 5.6 adds `VERIFY deferred persist large-loop bounds`: full LoopPersist phase progress, per-slice caps (max `PERS,work` / `DIAG,persist_save` ≤ 100 ms), active snapshot during PLAYING/OVERDUBBING save (±500 ms MO/`playback_build,snapshot` padding), and record-stop `save_request=requested` with slowest stop stage ≤ 100 ms.

Legacy entry: `scripts/host_midi_automation_baseline.py` (delegates to preset `base`).

### Stop/interruption fixture

`scripts/hitl_stop_interruption.py` runs the single-overdub recipe while a
`StopInterventionPort` sends a timed transport Stop after the overdub-stop
button release. It writes `interruption-result.json` (stop precedence, recovery,
all-notes-off, post-stop silence) and `command.json`.

`command.json` is the **inner** `host_midi_hitl.py run` argv — a run artifact,
not an entry point. Executing it directly omits the intervention wrapper and does
not reproduce the fixture. See
[`SNAPSHOT_CONTENT_VERIFICATION.md`](SNAPSHOT_CONTENT_VERIFICATION.md#the-interruption-fixture-concept).

### Snapshot content parity

Dump `record` / `overdub` / `effective` with `!CAPTURE_CONTENT` (transport
stopped), capture playback cycles, and compare inventories. The executable recipe
is the `hitl-content-verification` skill; semantics, `content-review.json`, and
the expected ledger residuals are in
[`SNAPSHOT_CONTENT_VERIFICATION.md`](SNAPSHOT_CONTENT_VERIFICATION.md).

### `overdub_occupy_paint` (source OD1 + occupy OD2 + GUS)

**Stimulus:** `STIM-OCCUPY-PAINT-01` v1 (do not edit in place).

Clear (if needed) → arm → record a **4-bar seed** (pitch 72 only; not an occupy target) → **first overdub** writes the half-bar phrase (48/52/56/60, unshifted) → **second overdub** occupies that phrase:

| OD2 lap | Incoming notes | Occupy case |
|---------|----------------|-------------|
| 1 | 56 at the same on-tick as the source 56 | Same-start Hide |
| 1 | phrase **+1/4 bar** on 48/52 | Shorten (`start < hold`); last note wraps |
| 2 | that +1/4 phrase moved **−1/2 bar** (net **−1/4** vs first overdub) | Hide after shorten; first note wraps |

Then **two undos then two redos** (GUS, not undo-redo-undo-redo). First OD2 `draw_src,layers` still gates adopted-cache paint after the source pass.

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset overdub_occupy_paint \
  --midi-out "Teensy" --midi-in "Teensy" \
  --track-number 5 --loop-slot 1 --midi-channel 5
```

Host verifier: `scripts/test_occupy_paint_serial_verify.py`. Device gates: OD1 `overlap_hold` shorten=0 and hide=0, OD2 `overlap_hold` shorten≥2 and hide≥2, at least two loop-wrap pairs on 48/52/56/60 (half-bar tail On, head Off) from `WRAP` or OD2 `MI,U`, OD2 open `from=snapshot` when first `draw_src` `snap!=0` (`occupy_od2_snap_not_zero` is paint/occupy divergence only), adopted visual count does not shrink, `Overdub undone`≥2 and `Overdub redone`≥2. Phase windows close OD2 on `Overdubbing stopped` / `ODUB,stop,enter` when `ST,OVERDUBBING→PLAYING` is missing so second-overdub first-note, SEVT, and occupy `oh` still score.

### `long_loop_display_window`

Loop must be **> 16 bars** for bounded detailed window.

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset long_loop_display_window \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 --midi-channel 5 \
  --record-bars 24 --start-transport \
  --freeze-wait-ms 8000 --hold-track-ms 4000 --long-press-ms 700 \
  --phase-wait-ms 500 --press-ms 120
```

Host unit test: `scripts/test_long_loop_display_serial_verify.py`.

### `edit_full` / `edit_minimal`

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset edit_full \
  --midi-out "Teensy" --serial-port /dev/cu.usbmodem154944801 \
  --track 5 --record-bars 2 --start-transport
```

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset edit_minimal \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track 5 --midi-channel 5 \
  --loop-slot 2 \
  --record-bars 2 --start-transport
```

`--loop-slot` selects the Loops row (notes 50–57) **before clear**; record arm then uses Record button 36 on that selected slot (ARMED → transport → RECORDING). Omit `--loop-slot` to clear/arm the device-selected slot. The base phase logs `[info] --loop-slot N on track …`.

Legacy entry: `scripts/host_midi_automation_edit_baseline.py`.

### `edit_overdub_during_note_edit`

**Stimulus:** `STIM-EDIT-OD-DURING-NE-01` v1 on seed **STIM-BASE-2-FIXTURE-01** (do not edit in place).

Layered preset runs **`record_seed`** (2-bar `EDIT_RECORD_FIXTURE` via the proven clear/arm path) then the edit+overdub body. Legacy `--preset edit_overdub_during_note_edit` runs the same two phases (`base` seed + body).

```bash
.venv/bin/python scripts/host_midi_hitl.py run --layered --preset edit_overdub_during_note_edit \
  --midi-out "Teensy" --midi-in "Teensy" \
  --track-number 5 --loop-slot 2 --midi-channel 5
```

**E:** session undo after in-edit overdub targets the in-edit overdub layer only. Pre-edit overdub stays.

### `fader_motor_probe` / `fader_motor_sweep`

Isolated DROID motorfader check: host USB MIDI → Teensy (`teensy41-capture-serial-fader-probe`) → DROID
USB host. See **[`DROID_MOTORFADER_PITCHBEND.md`](DROID_MOTORFADER_PITCHBEND.md)** for pitchbend scale,
NOTE_EDIT arm (~50 % snap), and **`pitch_note_off`** trigger ordering.

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset fader_motor_sweep \
  --midi-out "Teensy" --midi-in "Teensy" --settle-ms 2500
```

Optional: `--fader fader2`, `--fader both` (ch16 + ch14 same pitchbend/trigger per step),
`--timing note_pitch_off` (legacy wrong order), `--no-edit-state-pc`.

### NOTE_EDIT fader select refresh (Phase 3 verify)

Manual repro on `teensy41-capture-serial`: fast fader1 sweep then slow final creep; fader1 to physical min.
Wait ≥400 ms quiet; confirm F2/F3/F4 motors match final note. Serial verifier (capture log):

```bash
.venv/bin/python -c "
from hitl.verify.note_edit_fader_select_refresh import verify_note_edit_fader_select_refresh
import sys
lines = open(sys.argv[1]).read().splitlines()
print(verify_note_edit_fader_select_refresh(lines))
" captures/session_YYYYMMDD_HHMMSS.log
```

Pass: each fader1 inbound cluster has `MO,224,14` or `#DBG outbound_step=SEND_F2` within 3 s;
max gap between F2 bursts ≤30 s; pipeline shows `BEGIN` → `ARM` → `SEND_F2` → `TRIGGER_F2` → … → `DONE`
and/or `QUIET_REFRESH` after user-classified quiet.

### `note_edit_overdub_compose` (same-pitch hold)

**Stimulus:** `STIM-NE-OD-COMPOSE-02` v1 (everyday). Do not edit `STIM-NE-OD-COMPOSE-01` (paint-only, live 12–23). Do not register a TEMP id.

Runs **`base`** as a 2-bar record plus **one** overdub (`--second-overdub-bars 0`, no post-stop undo), then the compose cases. `02` streams in-edit Capture at the selected seed pitch, re-selects that seed on the seed layout, and moves it onto a later same-pitch live note. Verifier requires Hide or Shorten, no DROID control-plane `COORD`/`SEVT` (ch13 acks 80–87, ch14/ch15), in-edit overdub `GEOM_APPLY,resolve` ≤ 20 ms, no `commitEditAction` four-view reconstruct traces, `pitch_keeps_mover_dnte` when the log has type=5 (vacuous if none), `commit_skips_unpainted_mover_loop_end` when close writes `ChangeLength` to loop end (`192140` 80–2303), and `pending_same_pitch_note_on_not_loop_end_tail` so a same-pitch On-On-Off does not paint `length == loopLength-start` in the first half (`203112` `48@435` 1869), and `note_edit_close_persists_apply_owned` so NOTE_EDIT close must not log `canonical=0` with `apply_owned>0` (`223532`). Compose `02` device PASS [`225149`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_225149.json) **13/13**. Device PASS [`123953`](../../captures/session_20260909_123953.log) as TEMP-A, then promoted. Plan: [`note_edit_overdub_paint_overlap_compose_bugfix.md`](../Plans/note_edit_overdub_paint_overlap_compose_bugfix.md). Reject-pending plan: [`capture_pending_same_pitch_note_on_reject_bugfix.md`](../Plans/capture_pending_same_pitch_note_on_reject_bugfix.md). Exit persist: [`note_edit_exit_persist_selected_mover_bugfix.md`](../Plans/note_edit_exit_persist_selected_mover_bugfix.md).

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset note_edit_overdub_compose \
  --midi-out "Teensy" --midi-in "Teensy" \
  --track-number 5 --loop-slot 1 --midi-channel 5
```

Paint-only leftover (`STIM-NE-OD-COMPOSE-01`, live 12–23) stays in the catalog for replay of Stage 1 captures. Do not use `--stimulus-temp-suffix A` to recreate TEMP-A; that recipe is `02`.

Verify-only:

```bash
.venv/bin/python scripts/host_midi_hitl.py run \
  --scenarios note_edit_overdub_compose \
  --verify-serial-log captures/<log>.log --verify-only
```

Host unit tests: `scripts/test_note_edit_overdub_compose_serial_verify.py`,
`scripts/test_hitl_stimulus_contract.py`.

### `note_edit_overdub_compose_last_bar_shorten` (seed onto last-bar beat 4)

**Stimulus:** `STIM-NE-OD-COMPOSE-03` v1. Do not edit `STIM-NE-OD-COMPOSE-01` or `02`.

Same seed and same-pitch in-edit Capture as everyday compose. After a live On sits within one beat of last-bar beat 4 (`(bars - 1) * 768 + 3 * 192`; **2112** on a 2304-tick loop), re-select the seed and F2 to that beat. Verifier requires Shorten. Hide-only fails. Device PASS [`133714`](../../captures/session_20260909_133714.log): live **2113**, Shorten `2100–2111`. Plan: [`note_edit_overdub_compose_last_bar_shorten_hitl_enhancement.md`](../Plans/note_edit_overdub_compose_last_bar_shorten_hitl_enhancement.md).

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset note_edit_overdub_compose_last_bar_shorten \
  --midi-out "Teensy" --midi-in "Teensy" \
  --track-number 5 --loop-slot 1 --midi-channel 5
```

### `note_edit_overdub_compose_last_bar_reopen` (Shorten, exit, re-enter, select, move)

**Stimulus:** `STIM-NE-OD-COMPOSE-04` v1. Do not edit `STIM-NE-OD-COMPOSE-01`, `02`, or `03`.

Same last-bar Shorten as `03`, then short-press exit to LOOP_EDIT, short-press re-enter, re-select the seed on the original seed layout, and F2 move. Verifier requires a second `NOTE_EDIT_OPEN`, a non-empty reopen select, and a Move `EditSessionAction`. Also requires no overdub `COORD` ch13 notes 80–87 and no stored ch14/ch15 control-plane `SEVT`. Verifier `commit_skips_trace_reconstruct` fails on `commitEditAction replay_flat` / `take_only` / `session_store` / `loop_materialized`. Verifier `pitch_keeps_mover_dnte` fails when a type=5 `EditSessionAction` has no following `DNTE` at that pitch and start. Verifier `commit_skips_unpainted_mover_loop_end` fails when `commitEditAction incoming ChangeLength` writes `newEnd` at loop end (`loopLength` or `loopLength-1`) and `moverBaselineEnd` was not already there (`192140` 80–2303). Plan: [`note_edit_overdub_compose_shorten_hold_select_bugfix.md`](../Plans/note_edit_overdub_compose_shorten_hold_select_bugfix.md). RC1–RC6: [`note_edit_control_plane_record_display_stall_bugfix.md`](../Plans/note_edit_control_plane_record_display_stall_bugfix.md).

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset note_edit_overdub_compose_last_bar_reopen \
  --midi-out "Teensy" --midi-in "Teensy" \
  --track-number 5 --loop-slot 1 --midi-channel 5
```

### `note_edit_reselect_stacked` (reselect moved note on a stacked sixteenth)

**Stimulus:** `STIM-NE-RESELECT-STACKED-01` v1 on seed **STIM-BASE-2-FIXTURE-01** (no overdub). Do not edit `STIM-NE-OD-COMPOSE-01`, `02`, or `03`.

Moves the recorded fixture step-8 note onto the recorded step-4 sixteenth (not the theoretical 16th grid), deselects on an empty sixteenth, then F1 intra-step pitchbend must apply two different notes in that destination sixteenth. PLAYING `select_motor_sync` / `geometry_motor_sync` must be `sequence=value_only` with no following `DIAG,midi_gap` ≥ 200000 µs (`170818` timed burst). `value_only_notegate_held` requires motor NoteOff ≥ 80 ms after NoteOn (`232309` 1 ms pair). `select_apply_highlight_follows` fails when ≥ 10 `apply=1` note selects have fewer than half as many `DNTE` (`235958` 48/3). `moved_note_kept_after_deselect` fails when `VCACHE,full notes` drops after empty-step (`232309` 8→7). Plan: [`note_edit_reselect_moved_stacked_note_bugfix.md`](../Plans/note_edit_reselect_moved_stacked_note_bugfix.md). F1 lag: [`note_edit_fader1_select_display_lag_bugfix.md`](../Plans/note_edit_fader1_select_display_lag_bugfix.md). Stacked drop: [`note_edit_stacked_move_deselect_display_drop_bugfix.md`](../Plans/note_edit_stacked_move_deselect_display_drop_bugfix.md). Control-plane RC3/RC7: [`note_edit_control_plane_record_display_stall_bugfix.md`](../Plans/note_edit_control_plane_record_display_stall_bugfix.md).

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset note_edit_reselect_stacked \
  --midi-out "Teensy" --midi-in "Teensy" \
  --track-number 5 --loop-slot 1 --midi-channel 5
```

### `note_edit_select_dependent_faders` (F2/F3/F4 motor sync on F1 select)

Runs **`base`** then **`note_edit_select_dependent_faders`** (same pattern as `revision_load_record`). Runner aborts if base fails. Plan: [`note_edit_select_dependent_faders_hitl_enhancement.md`](../Plans/note_edit_select_dependent_faders_hitl_enhancement.md).

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset note_edit_select_dependent_faders \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 --midi-channel 5 \
  --dwell-ms 800 --toggle-dwell-ms 800 --toggle-cycles 6
```

Or run phases in one shell session: `--preset note_edit_select_dependent_faders` (recommended). Split runs: `--preset base` then `--scenarios note_edit_select_dependent_faders` only when the device loop still matches the latest passing base report.

Verify-only (sweep log): `--scenarios note_edit_select_dependent_faders --verify-serial-log captures/note_edit_select_dependent_faders_*_serial.log --verify-only`

### Geometry F1 motor sync guard (host verifier)

After geometry-driven F1 motor flush, inbound F1 must not trigger `select_apply` or exit Move state within **1500 ms**. Host unit tests (no Teensy):

```bash
.venv/bin/python scripts/test_note_edit_geometry_fader1_serial_verify.py
```

Manual: move note with F2/F3 in Move kind; confirm F1 follows and selection/kind stay on moving note. Plan: [`note_edit_geometry_f1_selection_guard_bugfix.md`](../Plans/note_edit_geometry_f1_selection_guard_bugfix.md).

---

## Native (host) tests vs HITL

| Suite | Command | Teensy |
|-------|---------|--------|
| Firmware logic | `pio test -e native` | Not required |
| HITL verifier scripts | `.venv/bin/python scripts/test_*_serial_verify.py` | Not required |
| Full HITL run | `host_midi_hitl.py run …` | **Required** |

Run `pio test -e native` before push/merge. Run relevant HITL preset when changing capture, undo, display, or edit flows covered above.

---

## Manual regression (firmware invariant review)

Hardware steps from [`firmware_ownership_lifetime_review.md`](../Plans/firmware_ownership_lifetime_review.md). Archive capture as `captures/MT-<id>_<date>.log`.

| ID | Covers | Status |
|----|--------|--------|
| **MT-P0** | Materialize stale after loop-length change with committed editPass | Conditional PASS — `session_20260805_222144.log` |
| **MT-P1-undo** | **E:** vs **U:** routing during NOTE_EDIT (session-gated; no global fallthrough) | **PASS** — `session_20260806_003023.log` |
| **MT-P1-fold** | Unified wrap finalize (seal + in-edit fold) | Manual **PASS** (2026-08-06) |
| **MT-P1-display-audition** | NOTE_EDIT geometry while PLAYING (no hang) | Manual **PASS** (2026-08-06) |
| **MT-P5-recovery** | Phase 5 longest valid prefix load | **Parked** — deferred (no Phase 5 code) |
| **MT-hygiene** | Native + NOTE_EDIT smoke | **PASS** — native 828/828; layered **`base`** **parked** (HITL refactor) |

**MT-P1-undo quick check:** enter NOTE_EDIT → two geometry actions → drain **E:** → press global undo while still in edit → serial must show `No session undo available` and **not** `Scoped edit pass undone` → exit edit → global undo must run.

**MT-P1-fold (manual — sufficient for this review):**

1. NOTE_EDIT → in-edit overdub stop near loop end → **E:** undo/redo of overdub layer only → exit → global undo.
2. Archive capture as `captures/MT-P1_fold_in_edit_overdub_<date>.log`.

**Layered `base` HITL:** **parked** — dedicated HITL refactor ([`hitl_cli_rebuild_enhancement.md`](../Plans/hitl_cli_rebuild_enhancement.md)); not a gate for firmware ownership review closeout.

`--preset edit_overdub_during_note_edit` is **not** registered in `registry.py`; do not use until wired.

---

## Related docs

- Implementation notes: [`docs/Plans/hitl_modular_scenarios_enhancement.md`](../Plans/hitl_modular_scenarios_enhancement.md)
- DROID motorfader pitchbend / probe timing: [`DROID_MOTORFADER_PITCHBEND.md`](DROID_MOTORFADER_PITCHBEND.md)
- Cursor rule (canonical `base` command): [`.cursor/rules/HITL-Test-Flow.mdc`](../../.cursor/rules/HITL-Test-Flow.mdc)
- Host result contract: [`hitl_framework_hardening_refinement.md`](../Plans/hitl_framework_hardening_refinement.md)
- Undo/redo storage rules: [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](LOOP_MIDI_STORAGE_AND_VALIDATION.md)
