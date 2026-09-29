---
name: hitl-content-verification
description: Run hardware-in-the-loop content verification on the Teensy looper and read its evidence. Chooses the authoritative HITL entry point, prepares/resets the device, dumps committed record/overdub/effective content, captures playback, and compares inventories. Also carries the read-only restored-library attribution path (!LIB_OBSERVE, no content run). Use when investigating snapshot or playback content parity, missing or duplicated rows, dense-content representability, stop/interruption reproduction, restored-library or per-slot inventory questions, or when asked to rerun and verify a HITL run on hardware.
---

# HITL content verification

Prove that committed loop content reaches the MIDI output row for row, and read
the evidence correctly. Semantics and artifact meanings live in
[`docs/Guides/HITL_CONTENT_VERIFICATION.md`](../../../docs/Guides/HITL_CONTENT_VERIFICATION.md);
this skill is the executable recipe.

## 1. Pick the entry point (decision tree)

| Goal | Entry point |
|---|---|
| Baseline record/overdub regression | `host_midi_hitl.py run --preset base ...` |
| Stop/interruption reproduction | `scripts/hitl_stop_interruption.py ...` (wrapper) |
| Snapshot/content investigation | content-verification pipeline (section 5) |
| Native logic regression only | `pio test -e native [-f <suite>]` |

**Never execute a captured `command.json` as an entry point.** It is the inner
`host_midi_hitl.py` argv written by the interruption wrapper; running it directly
omits the intervention and does not reproduce the fixture.

## 2. Preconditions

- Firmware is `teensy41-capture-serial` (needed for `#CAP` and `!CAPTURE_CONTENT`).
- `.venv` provides `mido`, `pyserial`, `python-rtmidi`.
- Device visible: `pio device list` shows `/dev/cu.usbmodem*`.
- The slot under test is the authorized scratch slot (default track 5 / slot 1).

## 3. Prepare the device (the boot is a decision now)

A managed run no longer waits for a boot it does not need. `--boot-reset-mode auto` (the default)
asks the device read-only whether it is already stopped and attaches **without** a reset when it is,
so the scenario starts as soon as the device is driveable (`captures/s4f22-step11-h3`: settle 0.1 s,
readiness on the first probe). Pass `--boot-reset` when the run must carry the fresh-boot banner
(the accepted-sequence oracle's gate); the capture then reads the firmware's own `!REBOOT`
acknowledgement instead of waiting a fixed window, and names a refusal (`!REBOOT is transport-gated`).
The harness repairs a transport a killed run left running with one idempotent MIDI Stop and then owns a
fresh boot, because the repair makes the capture stream untrustworthy by *position* (see the
`s4f22-step11-h2` note in `docs/Runtime/CURRENT_WORK.md`).

Opening serial may still reset the Teensy. **Symptom of a stale run:** the session log has no boot
banner, device uptime is large, and the run ends `INCONCLUSIVE_CAPTURE` with `RING,overflow` and no
phase boundaries.

Reliable reset (rebuild the sketch) when the device state itself must be reset:

```bash
pio run -e teensy41-capture-serial -t upload
for i in $(seq 1 25); do ls /dev/cu.usbmodem* >/dev/null 2>&1 && break; sleep 1; done
```

## 4. Sandbox and build constraints

- If the default PlatformIO cache is not writable, set
  `PLATFORMIO_BUILD_DIR="$PWD/.pio-build-local-fw"` (gitignored).
- Do **not** point `PLATFORMIO_CORE_DIR` at a fresh directory: it downloads a
  newer Teensy toolchain whose section handling fails this build
  (`NoteEditFocus*.cpp` `.flashmem` section-type conflicts). Use the pinned
  `~/.platformio`.
- HITL runs open the USB serial and MIDI ports; that access is outside a
  workspace-only file sandbox.

## 5. Create content, then run the pipeline

Content creation is the existing HITL (section 1). For a dense capacity re-stress
use the densest recipe (notes 0–127, 1-clock step and gate) with 4 record bars
and 2 overdub bars. Then dump and verify:

```bash
.venv/bin/python .agents/skills/hitl-content-verification/scripts/dump_and_capture.py \
  --out-dir captures/<run>-playback
.venv/bin/python .agents/skills/hitl-content-verification/scripts/verify_playback_content.py \
  captures/<run>-playback
```

`dump_and_capture.py` stops transport, sends `!CAPTURE_CONTENT`, starts playback,
and records cycles. `verify_playback_content.py` writes `content-review.json`
with inventory differences and per-bar counts.

## 6. Evidence expectations (do not accept exit 0 alone)

A content-verification result is complete only when all hold:

- `content-review.json` exists for **this** run, with `inventory_equal: true` on
  every cycle.
- `storage_matches_effective: true` (`record + overdub == effective`).
- `max_bar_refs` is reported; `capacity_path_exercised` is true if the goal was a
  capacity re-stress. A run below the old 256-reference ceiling is a parity check
  only, not a capacity re-stress.
- Every remaining `missing` row is classified against the expected residuals in
  the guide (loop-boundary `NoteId=0` Off; coincident same-pitch Off dedupe).
  Anything else is unexplained and must be investigated.
- The session log is fresh (boot banner at the top) and free of `RING,overflow`.
- The run's `metadata.json` (managed runs) records `git_commit` and
  `firmware_hex_sha256` — cite them as the run identity, and treat a missing or
  null firmware hash as "this evidence is not pinned to a build".

For a HITL run, also check `interruption-result.json` (`result: PASS`) and the
base report `overall_ok`; `INCONCLUSIVE_CAPTURE` is not a pass.

## 7. Failure recovery

| Symptom | Action |
|---|---|
| `INCONCLUSIVE_CAPTURE`, `RING,overflow`, no boot banner | Device did not reset; re-upload and rerun (section 3). |
| `no_device_stop_inside_stopping` | Interruption timing miss; rerun the wrapper (timing is flaky, not a content fail). |
| `overdub_unmatched_open_notes` | Dense generator left notes held at stop; setup verdict only, unrelated to snapshot content. |
| Harness exits 3 with a watchdog message | Run exceeded the documented bound; raise `HITL_MANAGED_RUN_TIMEOUT_S` only for a legitimately long run. |
| USB serial port busy / no port | A leaked capture session; the managed harness kills it on exit, but confirm no stray `capture_session.py`. |
| Build fails with `.flashmem` section conflicts | You set a fresh `PLATFORMIO_CORE_DIR`; unset it (section 4). |
| Verifier cannot find an anchor | Use `verify_playback_content.py` (auto-selects an anchor); a fixed anchor is fixture-specific. |
| Device wedges after transport stop: serial log ends mid-write (e.g. mid `PERS,work`), no boot line, `crash=0`, board still on USB but never answers | **Host-side fault, not firmware.** Closing the serial fd while the device is still emitting blocks `Serial.print` once the USB buffer fills, and the firmware then waits forever with no crash signature. Stop with the canonical protocol — `stop`, drain, `stop`, drain — and keep the port open through any `!CAPTURE_CONTENT` dump. A bespoke reader that closes right after `stop` reproduces this every time. Recovery: reset/re-upload. |
| Device vanishes from USB entirely (`/dev/cu.usbmodem*` gone, no Teensy on the bus) | You reset with a **boot-only** request: `teensy_loader_cli --mcu=TEENSY41 -b` (or `-b -s`) parks the board in HalfKay with no sketch, so it never re-enumerates. Reboot by **re-uploading** (`pio run -e teensy41-capture-serial -t upload`), which programs and returns to the sketch. A `134`-baud open is a soft-reboot request and is less reliable than the upload path. |

## 8. Reuse for later verification scenarios

The reusable sections are **device prep (3)**, **sandbox rules (4)**, **capture
(5)**, **evidence (6)**, and **recovery (7)**. A new verification scenario (for
example preview parity or persistence restore) reuses all of them and replaces
only section 5's content-creation command plus any scenario-specific expected
counts. Prefer extending this skill over adding a near-duplicate.

## 9. Read-only restored-library attribution (no content run)

When the question is *which slots the device actually restored* rather than *what
the restored loop contains*, do **not** run the content pipeline. Use the
read-only boot-census entry point, which resets by re-uploading and then sends
only `!LIB_OBSERVE` — it writes nothing, clears nothing, and never starts the
transport:

```
.venv/bin/python scripts/hitl_boot_census.py --out-dir captures/<dir> \
    --library-observe --settle-s 25 --observe-window-s 30
# add --observe-early-close-s 0.15 to exercise the incomplete-window case
```

`census.json` then carries `library_observation`: the per-slot observed state, a
nullable cause, and the four window-scoped quantities (payload inventory,
initial-playback set, restore completions **in the window**, committed at the
checkpoint). An observation whose `end` frame is missing or reads `complete=0` is
**incomplete** — its counts are not the settled library state. Contract and the
full reading recipe: [`RUNTIME_STORAGE_AND_PERSISTENCE.md`](../../../docs/Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md#the-on-demand-library-observation).
