# 3.9 hardware half — deliberate latch recovery, pinned qualification image, and the registered content leg

**Scope (2026-10-09):** `windowed-playback-generation` task **3.9 — deliver readiness receipts by the two delivery
rules**, specifically its remaining **hardware half**, plus the post-step **Snapshot** confirmation landed with
[increment 6](../2026-10-09-receipt-snapshot-gaps/EXECUTION.md). Both of those increments were reported **device NOT
RUN** in `docs/Runtime/PROJECT_STATE.md` and `docs/Runtime/CURRENT_WORK.md`: the source-qualified frozen receipt was
verified natively and by build only, and the lane verifier's tightened requirement had never been exercised against a
real capture. This packet is that run.

No firmware, test, script, scheduler, admission, SD-schema or default-gate change. The only non-documentary writes in
this session were the deliberate latch clear and the qualification-image build/upload.

Authority: [`DEC-108 — windowed playback generation`](../../../../../docs/DECISION_LOG.md#dec-108--windowed-playback-generation-one-publication-owner-window-sized-reservation-pinned-source-retention-and-readiness-as-completed-publication),
task 3.9 and its increments 1–6 in [`tasks.md`](../../tasks.md), and the requirement
[`Readiness evidence is delivered where it is produced`](../../specs/windowed-playback-generation/spec.md#requirement-readiness-evidence-is-delivered-where-it-is-produced)
(AC-8's two scenarios). Keepers: `Track::stepPlaybackViewMaintenance` (frozen Record completion terminal),
`Track::processSnapshotOwnerTurn`, `Loop::stepPendingSnapshotPublish`. Entry point:
[`.agents/skills/hitl-content-verification`](../../../../../.agents/skills/hitl-content-verification/SKILL.md) with the
registered `windowed_content_parity` preset on the dedicated evaluation env
`teensy41-capture-serial-window-qualification`.

**Session authorization.** The owner named the destructive target in this session: **track 8 / slot 4** clear + overwrite
(the leg clears and re-records its target). The upload is the documented exception for a dedicated HITL evaluation
preset. No quarantine, delete or rename was performed, and no SD path was touched outside the registered leg's own
Clear of the authorized slot.

## 1 — Deliberately resolving the shutdown latch

The latch `captures/.device-shutdown-incomplete.json` was recorded 2026-10-09
(`recorded_at 1791546739.172249`) as:

```json
{"status": "incomplete_disconnected", "drains": 0,
 "detail": "transport stop press failed: TypeError(\"'NoneType' object is not callable\")", "ok": false, "context": ""}
```

Its own detail names a **host-side fault** — a `TypeError` inside the drain helper's own transport-stop callable, before
any stop press reached the device — so the payload is not evidence that a save was interrupted. That is the reason the
module distinguishes `incomplete_disconnected` from `incomplete_timeout`, and it is why the clear is a *deliberate
recovery* rather than a deletion: the hazard the latch exists for (the next reset landing on an unfinished save) has to
be discharged by a **verified drain first**.

**Recovery drain (non-reset attach).** `ManagedCaptureSession(boot_reset="never")` attached to
`/dev/cu.usbmodem154944801` (`attach_no_reset after 0.1s`), a fenced read-only `!CAP_IDENTITY_STATS` reply proved the
serial owner was answering, and the canonical `hitl.device_shutdown.DeviceSession` protocol then ran two
idempotent-MIDI-Stop → verified-idle cycles:

| Evidence | Value |
|---|---|
| `captures/s39-hw-receipts-preflight/shutdown-result.json` | `status=complete, drains=2, ok=true` |
| device rows proving both cycles | `#CAP,1806958577,PERS,result,54206,143360,143360,ok` + `SAVE,completed,0` + `SAVE,idle,0`, and `#CAP,1808969363,PERS,result,46495,143360,143360,ok` + `SAVE,completed,0` + `SAVE,idle,0` |
| fenced read-only reply | received |
| crash signature | none (`crash=0`; the capture carries a live `idle_maint`/`DFRAME` stream to 1809.9 s) |

**The clear.** Only after that verified drain, via the module's own deliberate API
(`hitl.device_shutdown.clear_latch`), and recorded with the preserved payload:

| Artifact | Content |
|---|---|
| `captures/s39-hw-receipts-preflight/latch-before.json` | the historical payload, copied before anything could rewrite it |
| `captures/s39-hw-receipts-preflight/latch-clear.json` | action, timestamp, the payload, the verified-drain justification, `latch_after: null` |

`latch_reason()` is now `None` and the latch file is gone. **What this does and does not mean:** it removes the
*refusal* to reset/upload, on the basis that the device reached verified persistence idle before any reset; it is not a
statement that the historical payload's underlying host bug was a device fault, and the drain helper's own
`TypeError` was the earlier session's, not this one's (this session's helper sent both stops successfully — two
`[drain] sent one idempotent MIDI Stop` lines in the run log).

## 2 — Pin: the qualification image

| Item | Value |
|---|---|
| Env | `teensy41-capture-serial-window-qualification` (rolling lane enabled; dedicated evaluation env) |
| Build dir | `.pio-build-local-fw` (`PLATFORMIO_BUILD_DIR`; gitignored) |
| Default `teensy41-capture-serial` build | **SUCCESS** — `[RAM1 budget] variables 96,192 / ITCM 393,216 (code 390,360 + padding 2,856) / locals 34,880` |
| Qualification build | **SUCCESS** at the same figures |
| Approved budget (`scripts/ram1_budget.json`) | `ram1_variables 96192`, `itcm_bytes 393216`, `padding_floor 840` → **byte-identical to the pin, no re-base** |
| Qualification HEX SHA-256 | `43663800e4ada204551913f3601bed9b5bafadb4c1b0c32d3df162635144fd1e` |
| Upload | `pio run -e teensy41-capture-serial-window-qualification -t upload` **SUCCESS** (17.1 s); the HEX SHA-256 was re-checked **after** the upload and is unchanged |
| Git identity | `c7fa52177017105b78414c93e366d57253bc60eb`, dirty tree (`metadata.git_dirty=true`) |

**The HEX SHA-256 differs from the increment-6 packet's `de71f7c5…`, and that is not a tree difference.** `src/RtcTime.cpp`
parses `__DATE__`/`__TIME__` into the compile-time RTC seed, so every rebuild of an unchanged tree yields a different
image hash. The **reproducible** pin for this image is therefore the RAM1 line (`96,192 / 390,360 + 2,856`) together with
the git commit — both exact — and the HEX SHA-256 identifies *the bytes actually flashed for this run*, which is what
the run metadata is for. Recorded rather than smoothed, because the two hash values look like a drift and are not.

**Post-flash drain.** `/dev/cu.usbmodem154944801` re-enumerated; a second managed non-reset drain reached
`status=complete, drains=2, ok=true` (`captures/s39-hw-receipts-postflash-drain/shutdown-result.json`), i.e. the boot's
own deferred save was settled **before** the leg drove anything. This is also what avoids the trap a previous session
hit, where the first post-flash Clear was refused by the firmware's deferred-save gate.

## 3 — The registered content leg

```
.venv/bin/python scripts/host_midi_hitl.py run --preset windowed_content_parity \
  --track 8 --loop-slot 4 --midi-channel 8 \
  --build-env teensy41-capture-serial-window-qualification \
  --no-boot-reset --record-bars 24 --out-dir captures/s39-hw-receipts-run1
```

Run identity (metadata.json / invocation.json): `git_commit c7fa5217…`, `git_dirty true`,
`firmware_hex_sha256 43663800…`, `build_env …-window-qualification`, track 8, loop slot 4.
The reused long-loop leg cleared and re-recorded the authorized slot, reached `RECS,stop,3,18980,0,18980,19200,0` at
device 128.912026 s (`RECORDING→STOPPED_RECORDING→PLAYING`), drove its NOTE_EDIT window gestures, then stopped the
transport and drained 12 s.

**Combined verifier: `ok: true`, `issues: []`, process exit 0.** Every check of the lane oracle, the reused leg's own
display oracle and all three Snapshot checks passed on hardware:

| Check | Result | Detail as emitted |
|---|---|---|
| `readiness_receipts_under_transport` | **PASS** | `receipts=1 under_transport=0 source_qualified=8 of 8 frozen_receipts_under_transport first=complete_ready/clock_edge token=1154 interval=0+768/19200` |
| `window_boundaries_crossed_under_transport` | **PASS** | `build_complete_under_transport=8 loop_len=19200 revisions=['0','1']` |
| `windowed_service_coverage_under_transport` | **PASS** | `blocks=625 under_transport=609 with_opportunities=609 permitted=64` |
| `no_windowed_overrun_under_transport` | **PASS** | `overrun_rows=0 under_transport=0` |
| `capture_evidence_ring_intact` | **PASS** | `critical_evidence_loss_rows=0 … stats_readbacks=2 deltas={'tierA_evict': 0, 'overflow': 0, 'crit_rej': 0, 'wipe': 0}` |
| `window_demand_observed` | **PASS** | `gap blocks with eligible/progress turns=588; readiness receipts=1` |
| `record_session_exit_root_published` | **PASS** | `record_rv=0 published_rv=1 post_step_confirmed=True owed_index=8667` |
| leg display oracle | **PASS** | `ok=true`, 9 `DISP` rows, 2 warnings (`freeze_disp_sparse:marker_fallback`, `hold_disp_sparse` duration 3101 ms) |

### 3a — Source-qualified frozen receipts, verbatim

Eight `DIAG,win_frozen_readiness` rows, **all eight source-qualified and all eight under transport** — the requirement
that was added to the verifier with increment 6 and had never met a real capture:

```
#CAP,129031499,DIAG,win_frozen_readiness,7,3,complete_ready,clock_edge,0,174,0,1154,0,768,19200
#CAP,129546147,DIAG,win_frozen_readiness,7,3,complete_ready,clock_edge,0,745,0,1154,768,768,19200
#CAP,131532666,DIAG,win_frozen_readiness,7,3,complete_ready,clock_edge,0,19181,0,1154,768,768,19200
#CAP,131620267,DIAG,win_frozen_readiness,7,3,complete_ready,clock_edge,0,716,0,1154,1536,768,19200
#CAP,133551723,DIAG,win_frozen_readiness,7,3,complete_ready,clock_edge,0,742,0,1154,2304,768,19200
#CAP,135062579,DIAG,win_frozen_readiness,7,3,complete_ready,clock_edge,1,161,0,1154,2304,768,19200
#CAP,135607447,DIAG,win_frozen_readiness,7,3,complete_ready,clock_edge,1,720,0,1154,3072,768,19200
#CAP,137559841,DIAG,win_frozen_readiness,7,3,complete_ready,clock_edge,1,738,0,1154,3840,768,19200
```

Read row by row: track 7 / slot 3 (the CLI's track 8 / slot 4), readiness `complete_ready`, holder `clock_edge`, a
**nonzero content token 1154 carried in full**, a source generation 0, and a covered interval that walks the loop
(`start` 0→768→1536→2304→3072→3840, `len` 768, `loopLen` 19200) with no crossing and no repeat. The revision changes
0→1 between the 5th and 6th rows, and the frozen family keeps naming the completion across that change.

**The frozen family is what answers the term, and this run shows why.** The cadence-bounded `DIAG,win_readiness` row
appeared **once** in the whole 1.26 MB capture, at device 177.985712 s — *after* the transport stopped, so
`under_transport=0` — carrying `7,3,ready,ready,1,4484` (this take was committed by then). Reading that row as the
answer is exactly the mistake that made run 2 look like a pass. The event-shaped frozen family emitted 8 receipts in the
same run, all under transport, all source-qualified.

### 3b — Post-step Snapshot confirmation, verbatim

The publication is at device 138.616821 s, ~9.7 s after the record stop at 128.912026 s — comfortably inside the
captured window, which is what the two previous attempts could not show (their publish landed at the capture-window
edge and no row followed it):

```
#CAP,138616821,DIAG,snapshot_unit,kind=publish,ph=0,ld=0,p=1,q=0,a=1,if=0,hb=147456,ha=147456,psram=4294967295,n=17368,b=1274632,s=49
#CAP,138616946,DIAG,snap_gate,t=7,s=3,a=3,u=2,p=0,sep=0,fc=0,wj=0,hp=1,lr=1,vc=1,rec=0,play=1,od=0,step=0,cl=1,idle=0,elig=0,st=5021,cu=0,rv=1
```

The last row that still owed the session exit is `owed_index=8667`
(`#CAP,138441723,DIAG,snap_gate,t=7,s=3,a=3,u=2,p=1,sep=1,fc=1,wj=0,hp=0,lr=1,vc=1,rec=0,play=1,od=0,step=1,cl=1,idle=1,elig=1,st=4995,cu=0,rv=0`), and the settled `p=0,sep=0,fc=0,hp=1` row with
`rv=1 ≠ record_rv=0` **follows** it, 125 µs after the publish. That is precisely what the hardened check now demands
instead of accepting a restatement of the demand. The `publish` is the anonymous `snapshot_unit` row; the attribution to
target t=7 s=3 comes from the post-step `snap_gate` row beside it.

### 3c — Named observations, not folded into the pass

- **The generic cadence receipt is still a sample, and in this run it landed after the stop.** One row, `under_transport=0`.
  This is the known declared-interval behaviour and the reason the evidence surface fences on the event-shaped family.
- **Task 3.10's overrun did not recur here:** `overrun_rows=0` against run 3's single `win_gap_overrun,64,1,69,1,10`.
  That is one favorable observation, **not** a 3.10 result; 3.10's envelope derivation is untouched.
- **The reused leg's own length expectation still disagrees with the device:** the leg asked for 24 bars and notes
  `expected_loop_len_ticks: 18432`, while the committed take is the 25-bar/19,200-tick geometry seen in every recent run
  (`RECS,stop,…,18980,0,18980,19200,0`). Host expectation, not a firmware defect, and unchanged by this session.
- **The lane rows are qualification-image-observable only.** The plain capture env compiles the rolling lane out by
  design; the acceptance surface is this env.
- **No `--switch-target-slot` was used.** The frozen Record-return completion names itself without it, so the defined
  demand shape for this oracle is the registered leg's own; the switch shape's `switch_target_span_alignment` check was
  not exercised here.

## 4 — Post-run drain and verification gates

| Step | Result |
|---|---|
| Post-run managed drain | `status=complete, drains=2, ok=true` (`captures/s39-hw-receipts-postrun-drain/shutdown-result.json`); latch remains absent |
| Full native `pio test -e native` | **2,892 cases / 7 skipped / 2,885 succeeded / 0 failed** |
| Host verifier suite `scripts/test_windowed_content_parity_verify.py` | **50 tests, OK** |
| `openspec validate --strict` | `windowed-playback-generation` valid; `runtime-working-set-ownership` valid |
| `git diff --check` | clean |
| Default build `teensy41-capture-serial` | **SUCCESS**, RAM1 `96,192 / 390,360 + 2,856` |
| Qualification build | **SUCCESS**, same RAM1 line — no re-base |

The device is left stopped, drained to verified persistence idle, with no latch and no in-flight save.

## 5 — What this does and does not close

**Closes the hardware half of task 3.9.** The requirement's two scenarios are now met by a real capture:
*a readiness receipt is delivered under transport* (8/8 source-qualified frozen receipts while the transport ran, via
the direct-when-room / Tier-A path), and *diagnostics do not cost evidence* (625 coverage blocks, 1,141 gate rows and
23,923 capture lines / 16,901 `#CAP` rows of diagnostics in the same run, with `tierA_evict=0, overflow=0, crit_rej=0` — the routines that
were shed are the non-Tier-A `shed=66`, and every acceptance-fenced row survived). The post-step Snapshot confirmation
that increment 6 added is likewise exercised on hardware for the first time.

**Not claimed:** no timing/execution-envelope claim (`ExecutionBounds.md` untouched); no scheduler change and no
attribution for the 69-tick overrun (still task 3.10's, and it did not recur in this one run); no all-route reader
migration (3.15a–f), no legacy-emitter retirement (3.7b); the cadence-bounded generic receipt is still a declared-interval
sample; the reusable drain helper used for the pre/post drains is a session helper, not a registered entry point - it
only attaches without a reset and sends idempotent MIDI Stops, and the registered leg itself remains the only measured
device path.
