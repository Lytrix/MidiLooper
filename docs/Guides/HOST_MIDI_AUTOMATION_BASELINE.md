# Host-side MIDI automation baseline

Goal: run a repeatable local hardware-in-the-loop baseline where a Mac script drives the connected Teensy over USB MIDI to:
- select each track,
- record dense chromatic notes + CC data,
- stop recording (firmware returns to playing),
- start overdub,
- send dense overdub data,
- stop overdub,
- emit a pass/fail report.

This is the first automation layer for future scenarios (for example slot switching and new loop workflows).

## Script

`scripts/host_midi_automation_baseline.py`

## Dependencies

```bash
python3 -m pip install mido python-rtmidi pyserial
```

## Firmware build to use

Use capture build when you want `#CAP` state assertions (`ST`, `RECA`, `RECS`):

```bash
pio run -e teensy41-capture-serial -t upload
```

Mode A (`--serial-port`) and Mode B (`--follow-current-session`) both enable the serial transport
proxy for bar pacing. The premise behind that proxy is corrected as of 2026-09-30: the board
**does** emit USB MIDI clock while its own transport runs, and the host can read it and drive it
([`HITL_MIDI_CLOCK.md`](HITL_MIDI_CLOCK.md), `scripts/hitl/midi_clock.py`). The proxy remains for
runs whose phase pacing is not yet pulse-based; it is no longer justified by a missing clock.

## List ports first

```bash
python3 - <<'PY'
import mido
print("MIDI outputs:")
for p in mido.get_output_names():
    print(" -", p)
print("MIDI inputs:")
for p in mido.get_input_names():
    print(" -", p)
PY
```

Optionally list serial devices:

```bash
pio device list
```

## Run baseline (all 8 tracks)

With serial assertions enabled:

```bash
python3 scripts/host_midi_automation_baseline.py \
  --midi-out "Teensy" \
  --midi-in "Teensy" \
  --serial-port "/dev/cu.usbmodemXXXX" \
  --start-transport \
  --track-count 8 \
  --record-seconds 3.0 \
  --overdub-seconds 2.0
```

Without serial assertions:

```bash
python3 scripts/host_midi_automation_baseline.py \
  --midi-out "Teensy" \
  --midi-in "Teensy" \
  --start-transport \
  --track-count 8
```

Target a single track directly (example: track 5):

```bash
python3 scripts/host_midi_automation_baseline.py \
  --midi-out "Teensy" \
  --midi-in "Teensy" \
  --track-number 5 \
  --start-transport
```

Use bar-based durations (`2|4|8|16|32|64|128`) instead of seconds:

```bash
python3 scripts/host_midi_automation_baseline.py \
  --midi-out "Teensy" \
  --midi-in "Teensy" \
  --track-number 5 \
  --midi-channel 5 \
  --tempo-bpm 120 \
  --record-bars 8 \
  --overdub-bars 8
```

By default bar-based mode follows incoming MIDI clock (`24 PPQN`) so 8 bars means 8 real bars at the current running tempo.  
When serial capture is enabled (`--serial-port` or `--follow-current-session`), the script uses a
**wall-clock tempo proxy** when recent `#CAP,BPM` or `#CAP,BAR` lines show transport running.
Proxy tempo prefers the latest serial BPM, then `--tempo-bpm` (default `120`) — and in the
mandated `teensy41-capture-serial` build the `#CAP,BPM` family is not emitted at all
(`SESSION_CAPTURE_BPM` defaults to `0`), so the proxy tempo is effectively `--tempo-bpm`. The
device's real tempo is measurable from its own USB MIDI clock instead; see
[`HITL_MIDI_CLOCK.md`](HITL_MIDI_CLOCK.md) and `hitl.midi_clock.MidiClockReader.bpm()`.
Disable bar sync with `--no-bar-sync-from-midi-clock` if you explicitly want tempo-derived seconds only.
If no MIDI clock pulses are observed and serial proxy is inactive, bar-synced phases may record `0` clocks and fail (`phase_not_activated_or_no_clock`).
Use `--max-run-seconds` to enforce a hard timeout for the full run. On timeout, the script exits with failure and still writes a report (`assertions.timed_out = true`).

Default musical pattern in bar-sync mode:
- **Record phase:** 16th-note stream across `C3..C5`
- **Overdub phase:** 8th-note stream across `C1..B2`

You can override the ranges with:
- `--record-low-note` / `--record-high-note`
- `--overdub-low-note` / `--overdub-high-note`

For deterministic drift checks, fixed-note grid mode is enabled by default:
- record phase sends one fixed note on each 16th (`--record-fixed-note`, default `C4`)
- overdub phase sends one fixed note on each 8th (`--overdub-fixed-note`, default `C2`)
- disable with `--no-fixed-grid-notes`

JSON report fields for drift assertion:
- `record_max_abs_grid_jitter_clocks`
- `overdub_max_abs_grid_jitter_clocks`
- `record_mean_abs_grid_jitter_clocks`
- `overdub_mean_abs_grid_jitter_clocks`

To reduce dropped tail notes at phase boundaries, the script waits briefly after each stream before sending stop:
- `--post-stream-settle-ms` (default `120`)

To align stop actions with the intended musical boundary despite button short-press deferral, the bar stream ends early by:
- `--stop-press-advance-clocks` (default `12` clocks)

## Important behavior assumptions

- Control button events use channel 16 and notes from `include/MidiConfig.h`:
  - track select base note `60`,
  - record/overdub button note `36`,
  - global transport note `39` (optional `--start-transport`).
- Dense musical input uses a recordable channel (`--midi-channel`, default `1`); channel 16 is rejected by the script because firmware excludes channel 16 from recording.
- `--midi-channel` chooses the recorded MIDI data channel; `--track-number` chooses which looper track row button gets pressed.
- Before each record start, the script sends a long-press clear command on the selected track by default (to avoid unintended overdub on existing content). Use `--no-clear-before-record` to disable.
- Default post-press wait is tuned close to the short-press window (`--phase-wait-ms 320`) so recording/overdub input starts near phase boundaries while still allowing button resolution.
- Overdub note streaming defaults to a one-beat delayed start:
  - `--overdub-start-delay-bars 0`
  - `--overdub-start-delay-beats 1`
  This keeps overdub entry from landing too early in the loop while preserving repeatable bar-synced capture.
- The script adds a final settle delay before exit (`--final-wait-ms 1200`) so deferred button actions can complete.
- In bar mode, the script records clock pulses seen in the JSON report (`*_clock_pulses_seen`) so you can verify phase length. Counts come from host MIDI in or serial transport proxy pacing — not from `#CAP` clock lines.
- Record stop returns to playback (`STOPPED_RECORDING -> PLAYING`), a second record short press enters overdub (`PLAYING -> OVERDUBBING`), and a third press exits overdub. **The overdub exit may be immediate (`OVERDUBBING -> PLAYING`) or deferred (`OVERDUBBING -> OVERDUB_STOPPING -> PLAYING`); the verifier counts both shapes as one completed stop and closes the overdub phase window on either hop** (2026-09-17).
- First-note latency (serial verification path) — **advisory only** (does not fail the run):
  - `--record-first-note-max-clocks` (default `12`)
  - `--overdub-first-note-max-clocks` (default `12`)
  - Offset is derived from microsecond delta between phase `ST,Track` entry and first `#CAP,MI,U` note-on, converted using serial BPM (or `--tempo-bpm`).
  - Exceeding the max emits `timing_warnings` (e.g. `record_first_note_too_late`); missing first note remains a hard fail.
  - Skew is expected from host stream vs ST anchor and the ~300ms transport debounce.

## Output

Each run writes JSON output into `captures/`:

- `host_midi_automation_baseline_<timestamp>.json`

The report includes:
- per-track sent note/CC counts,
- serial capture marker counts (`RECA`, `RECS`) when enabled,
- track transition counts for `ARMED->RECORDING`, `RECORDING->STOPPED_RECORDING`, `STOPPED_RECORDING->PLAYING`, `PLAYING->OVERDUBBING`, `OVERDUBBING->PLAYING` (the deferred `OVERDUB_STOPPING->PLAYING` hop counts toward this row),
- serial first-note offsets (`record_first_note_offset`, `overdub_first_note_offset`) and advisory `timing_warnings` when latency exceeds threshold,
- `overall_ok` pass/fail summary.

## Next extension points

- Add scenario mode for slot switching while playback is running.
- Add scenario mode for new loop creation flows.
- Keep dense input generator shared so cross-scenario comparisons stay stable.

## Phase and persistence report boundaries

Overdub verification prefers typed ST/ODUB stop evidence over explicit stop
text and missing-typed-evidence reentry-window fallback. `Reentry released` is
job retirement, not a stop boundary: Record/predecessor retirement can occur
during the current overdub. Pairing is bounded by the next distinct machine or
human start; missing stops remain ambiguous rather than consuming later phases.

For managed runs, `_managed_record_evidence_cleanup` keeps ports and capture
open through Record retrieval and canonical `DeviceSession` persistence drain
before the final same-log report snapshot. The report records
`assertions.record_evidence_drain.persistence_shutdown`; incomplete cleanup,
missing result and failed result remain non-PASS. The outer managed shutdown
still drains every exit. No extra persistence sleep or post-hoc verdict override.
Unmanaged runs are unchanged. [Tests and 300 BPM device PASS](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-11-record-return-recovery/HOST_REPORT_REPAIR.md).

## Seeded-edit pass qualification

Record-only edit seeds are admitted only with complete capture, observed intended
committed length/fixture content, a current successful persistence result and
complete Record retrieval plus two canonical persistence drains. Host notes sent,
cached setup approval, overall_ok or the legacy one-bar length tolerance cannot
substitute for that qualification. Generic full-loop legacy admission is unchanged.

`note_edit_reselect_stacked` bounds its proof to the current leg/session. It requires
the intended Move source/destination/pitch and focused NoteId, an applied empty
selection, and both expected notes' distinct current IDs and selected geometry.
Direct occurrence rows take precedence over deferred twins; untagged deferred-only
delivery cannot establish chronology. Offline replay requires observed seed rows,
configured musical channel and exact requested length before using a layout.

F1 source confirmation distinguishes the pre-macro-commit display index from the
final selection index: a fresh direct fader target tick can pair with the same
operation's direct `Note selection changed` row. Wrong/stale/superseded targets
still refuse; queued motor/debug rows alone do not prove live confirmation.
Capture firmware extends the existing `NOTE_EDIT focus rebuild` row with current
pitch/start/end. The verifier binds visible, store-projecting current geometry
to its focus NoteId within the direct selection interval; that identity-qualified
geometry takes precedence over DNTE's mutable display-index compatibility rows.
Conflicting direct geometry refuses proof. This is a capture-only observation,
not a selection behavior, timing guarantee or physical-pixel witness.
[Repair and composed core qualification](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-12-i3b-source-confirmation/EXECUTION.md).

Core success is `PASS_CORE`, not complete timing/retention qualification. Optional
zero-observation cases are `UNMEASURED` with nullable `ok`; broader qualification
remains open. Missing identity/geometry refuses proof rather than guessing indexes.

Fresh-boot admission requires a well-formed current timestamp-qualified `DFRAME`
after both handoff rows; elapsed hold time is not a witness. This proves app-paint
path execution, not physical pixels or completed DMA. Shed witnesses may timeout.
Record drain results retain `stop_job_state` and `last_stop_status` independently
of fence errors; balanced counters do not prove a pending finalizer is idle.
[Local verification and remaining device requirements](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-12-hitl-pass-validation/EXECUTION.md).

## Preserving checkpoint before UI/storage qualification

`i4_checkpoint` (STIM-I4-CHECKPOINT-01@v1) uses the existing managed capture owner
and MIDI-only session to enter Load/Save, prove clamped Save-row focus, and confirm
one preserving Current revision. It never records, clears, changes slots, loads,
or cleans up. Fresh fenced stopped status timestamps qualify every receipt.
Unknown overlay state requires the operator's actual normal-screen/closed-overlay
observation, recorded by `--panel-overlay-closed`; it is not a serial inference.
Exact queue/dispatch/complete/exit receipts and coherent Set/revision identity
are required for `CHECKPOINT_CREATED`. Missing evidence remains `SETUP_INCOMPLETE`.

The first device attempt queued Save and exited normally, but exposed omitted
revision-only admission in the main persistence path. Do not proceed to scratch
mutation/load without a qualified checkpoint, or retry Save/reset blindly.
Managed `complete` shutdown currently proves workspace persistence idle, not an
empty revision queue or a completed revision checkpoint. Firmware admission and
non-truncating header patch were repaired, then the approved image was uploaded.
The second leg qualifies S0001_v0001 but its44.7s wait was rejected. After bounded
serializer optimization, thirdleg creates S0001_v0002 in14.86s with matching ordered
tail workload shape; owner confirms responsive visible completion and acceptable wait.
This stopped-leg timing/receipt qualification is not full SD/MIDI byte parity,
record→PLAYING spinner/load/focus/restore acceptance or a general time limit.
[I4 — optimized installed-image evidence](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/OPTIMIZED-HARDWARE.md).

## Live guarded overlay navigation

`i4_overlay_load` reuses managed capture and registered STIM-I4-OVERLAY-LOAD-01;
invoke the scenario without a preset (preset resolution takes precedence).
It requires owner normal-screen/closed-overlay readiness and a proven test checkpoint.
The capture-only `!OVERLAY_STATUS` query returns one fresh fenced scalar response:
open, mode, row, Set, revision and persistence phase. In dirty mode, row and identity
are the held prompt choice/requested target; otherwise row is the display list row,
Set is the drilled Set and revision is0. It performs no catalog read or cache refresh.
TX pressure produces no response, not a queued old answer. Missing/ambiguous/stale
query proof refuses navigation or confirmation. Dirty scroll awaits the exact next
row, and both history Load and dirtyYes are revalidated immediately before the press.

Playing CAP rows remain retained: main's <=8 flush never writes USB, and TRACE_DRAIN
remains stopped-only. Broker acceptance of TRACE_DRAIN while playing is not a flush.
Snapshots solve live navigation only; existing ordered dirtyYes/new preserving revision/
exact requested revision load/autoexit receipts still qualify final completion.
They never synthesize CAP events or waive absent receipts. Actual highlighted revision
must still be owner-observed with current nonce/session/label proof before Load.
Neither a snapshot nor a phase receipt proves pixels/responsiveness/content parity.

The owner-approved observation imagec455babb is now uploaded. Managed stopped probes
verify the fresh closed/idle query, canonical two-drain shutdown and settled heap readiness.
Owner confirms normal screen/no four-dot overlay problem after upload. This is not
playing-overlay/load responsiveness, USB timing or full hardware load qualification.
[I4 — upload and stopped-query evidence](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/OBSERVATION-UPLOAD.md).

After the Set-index ownership repair, fresh-boot run03 reaches Set1 history on imageecd4f671;
the owner confirms S0001/v0002 is visible and responsive. This closes the missing-Set/browser
visibility prerequisite only. The original identity-timeout report remains incomplete for Load:
no dirtyYes/preserving commit/load-complete/auto-exit proof. Run04 has no completed report or
shutdown evidence, so future device work must establish safe readiness/drain first.
[I4 — Set/history visibility closeout](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/SET-BROWSER-VISIBILITY-CLOSEOUT.md).

## Capture journal boot lifetime

The RAM2 critical-evidence journal is initialized explicitly on its first use each boot;
Teensy startup does not clear DMAMEM. Repeated capture initialization/session headers
retain journal rows, dropped counts and partial-replay cursor. Append, eviction and replay
validate count/index before indexing the fixed32-row storage. Invalid metadata is discarded
without traversal and reported through existing `RING,critical_rejected` counters and a
journal `dropped=1` sentinel. That sentinel denotes unknown evidence loss/an integrity
incident—not one precisely identified lost row. Backpressure keeps the notice pending;
playing flush budgets still perform no USB journal output.

The boot-journal repair image8050aabc is now uploaded. Managed boot handoff and
post-restore readiness were observed, then a separate90s settled window had16 valid
query pairs with increasing uptime and no new reset/reconnect/evidence-loss rows.
Owner confirms normal stable OLED/no reboot loop; canonical teardown completed.
The initial pre-handoff missing-query report remains incomplete, not retroactively PASS.
This qualifies bounded boot recovery, not cold-power-cycle stress or full playing Load.
No Set/card cleanup was needed.
[I4 — boot recovery evidence](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/BOOT-JOURNAL-RECOVERY.md).

## Playing overlay dirty-Yes load qualification

On installedimage8050aabc, registeredmanaged `i4_overlay_load` run06 qualifies
freshPLAYINGentry, actualowner-highlightedS0001_v0002 confirmation, dirtyYes
preservingCurrent revisionS0001_v0003 beforeexactv2load, automaticoverlayexit
andnormalpianorollreturn. Strictcurrent-sessiondrainedoracle, originaloverlay
anddirty-YesreceiptverifiersPASS; ownerconfirmsresponsiveminimalprogress and
no freeze/reboot. Finalcanonicaltwo-drainshutdowncomplete/nolatch.

The earlieridentitywindowtimeout05 andlateapprovalremainoriginalevidence;
the successfulretryconsumedonlyitsnewnonce/session/labelproof. This qualification
doesnotcoverNo/Cancel/clean/reopen-midload/defaultTransport/independentfocus
restore orhardwarecontentbyteparity. No unsafelegacyserialrunnerwasexecuted.
[I4 — playing-load evidence and scope](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/PLAYING-LOAD-QUALIFICATION.md).
