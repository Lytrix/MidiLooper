# S4.1 device leg — the working-set lease release row, observed

**2026-10-01.** Stage **S4.1 — release when the slot leaves the active set**. Host tooling plus one authorized
device campaign; **no firmware change** (`src/`, `include/` untouched, and the rebuilt image's hex is
byte-identical to the already-flashed one).

## What was owed

`tasks.md` S4.1 is checked on native evidence (three fixtures in `test_runtime_working_set_ownership`, red-proved
in three layers), with one recorded obligation: *"the device row for the lease release has not been observed on a
run — the emission `DIAG,ws_release,<slot>,<generation>,<outcome>` already exists and is Tier-A, so what is owed
is a run in which the path fires for the slot under test plus an oracle that reads the row."*

## What the campaign found first (and why five earlier attempts saw nothing)

A read-only survey of the whole capture corpus found the row **33 times** across 33 logs, in ordinary runs of the
mandated base preset — so the row was never unreachable. The five `scripts/hitl_s41_committing_switch_probe.py`
attempts of 2026-09-27 chased the wrong edge for a structural reason the code confirms: in that shape the stop
runs down the *retained* route, and `Track::invalidatePlaybackMergedMidiEvents` returns early whenever a deferred
stop job is still live, so no sweep runs; and on a capture-switch shape the row names the **incoming** slot
(`captures/s41-lease-20260927_013834`: `ws_release,4,1,released` for the switch target, nothing for the retired
slot A). The route that fires it is the third one the retention oracle's own limitation text names: **the
playback-reset/undo path** (`TrackUndo.cpp` → `Track::resetPlaybackStateForSlot` → `invalidatePlaybackMergedMidiEvents`
→ `releaseEligiblePlaybackRuntimeSlots`), where `noLiveOwnerForSlot` is true because the capture stop already
retired.

## The reading

Command (one per run; `--slot` is the button's 1-based `Loop N`, the row's own slot field is 0-based):

```
.venv/bin/python scripts/hitl_s41_lease_release_read.py --log <session.log> --slot 1 --generation 0 \
    --metadata <run metadata> --out lease-release-read.json
```

| run | log (sha256) | row | slot | generation | outcome | edge (same device clock) |
|---|---|---|---|---|---|---|
| base preset, **PASS** verdict, 2026-09-30 | `captures/session_20260930_142350.log` (`c9d6ac85…`) | `#CAP,3794271032,DIAG,ws_release,0,3,released` | 0 = `Loop 1` | 3 | released | `[3794.269] Button press: Record/Overdub (double)` → `[3794.269] MIDI: Undo (entries=2)` → `GS,15,36,1` |
| base preset at HEAD, 2026-10-01 | `captures/session_20261001_092818.log` (`b125ef0d…`) | `#CAP,69967546,DIAG,ws_release,0,0,released` | 0 = `Loop 1` | 0 | released | `[69.966] Button press: Record/Overdub (double)` → `[69.966] MIDI: Undo (entries=3)` → `GS,15,36,1` |
| base preset at HEAD, 2026-10-01 | `captures/session_20261001_093140.log` (`329b7ee7…`) | `#CAP,53786276,DIAG,ws_release,0,0,released` | 0 = `Loop 1` | 0 | released | the same double-press undo edge |
| base preset at HEAD, 2026-10-01 | `captures/session_20261001_093315.log` (`80b05bf9…`) | `#CAP,68930336,DIAG,ws_release,0,0,released` | 0 = `Loop 1` | 0 | released | the same double-press undo edge |
| **negative** — the switch shape of 2026-09-27, read for the *retired* slot | `captures/s41-lease-20260927_013834/session_20260927_013834.log` | `#CAP,486025325,DIAG,ws_release,4,1,released` (slot 4 only) | Loop 4 → `absent` | — | — | the reader refuses to substitute a sibling slot's row |

Identity of the 2026-10-01 runs (`run-identity.json`): `git_commit f63d5a023f2bdf84fee15b10601e0638b872eff1`,
`firmware_hex_sha256 577841d6b6e2c338a08c3b2b5e6948c95ecbe40ba266b1bd1cbbbbbe59d26785`, preset `base`, track 5,
loop slot 1, `build_env teensy41-capture-serial`, uploaded in the first run of the campaign. The 2026-09-30 PASS
run carries the **same** hex (`577841d6…`, recorded in `docs/Runtime/PROJECT_STATE.md`), which is why it is cited
beside today's runs rather than instead of them. `git_dirty` reads `true` on runs 2–3 because this slice's
**host-tooling** edits (`scripts/hitl/managed_capture.py`, `scripts/test_managed_record_drain.py`) were in the
tree by then; no firmware source was touched, and the rebuild's hex proves it.

The edge reading is by **device clock, not file position**, and that is load-bearing: a managed capture writes
the console stream and the `#CAP` ring stream separately, so the human row that caused the reset sits ~13 000
lines away from the `#CAP` rows it explains in `session_20261001_092818.log`. Both clocks are the device's own
(`[69.966]` s against `69967546` µs), so `hitl.lease_release.release_context` selects the edge by clock.

## The oracle half

* `scripts/hitl/lease_release.py` owns the row's wire form and its reading: the three outcomes, the deliberate
  silence of `NotHeld` (absence means "no sweep here", never "the release failed"), and the one 0-based→1-based
  conversion so a reader can print both.
* `scripts/hitl_s41_lease_release_read.py` is the S4.1 device-leg reader: exit 0 when the slot under test has a
  `released` row (optionally at a required generation), 2 when it does not, 3 when the log is unreadable. It
  quotes the run's own identity when given the run's metadata, and `hitl.retention_probe` now takes the same
  wire form from the shared owner instead of defining its own.
* `scripts/test_lease_release.py` **13/13**: verbatim rows with provenance, the `RING,record_stats,DIAG,latch`
  read-back proved *not* to parse as a release, a sibling slot proved not to stand in, absence and
  `refused_busy`/`stale_generation` proved not to count as success, the clock-window edge selection proved to
  drop a stop edge 12 s earlier, and every CLI verdict and exit code.

## The host defect this campaign found and fixed

Every base run at HEAD ended `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)` on
`Record evidence cleanup failed: 'DIAG,latch'`. `ManagedCaptureSession.drain_record_evidence` reconciled *every*
counted family against a row counter that only sees `RECA`/`RECS`/`RID`, so the latch family task 2.2 added to
the stopped-only read-back raised `KeyError: 'DIAG,latch'` and turned an otherwise verified run into a failed
one. The reconciliation now names which families carry a row cross-check (`reconciled_families`) and which are
reported only (`unreconciled_families`) instead of dropping them, with `test_managed_record_drain` **23/23**
(two new) and the mutation red-proved: iterating `stats` again reproduces exactly `KeyError: 'DIAG,latch'`
(*Ran 23 tests … FAILED (errors=1)*), restored byte-identically (sha256 `ba410b93…`).

## Verdict honesty

* The firmware row is the evidence S4.1 owed, and it is emitted on all four runs above.
* The 2026-09-30 observation is a **PASS**-verdict base run (`host_midi_automation_baseline_20260930_142629.json`:
  `overall_ok true`, `verdict PASS`, `issues []`).
* Today's three HEAD runs reproduced the row but their *baseline* verdicts were non-PASS for reasons unrelated
  to this stage, each named here rather than hidden: run 1 `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)` from the
  drain defect above (since fixed and red-proved); run 2 `SETUP_UNCONFIRMED` with
  `overdub_stored_span_too_short`/`second_overdub_stored_span_too_short` plus a missing persistence-result stage;
  run 3 `EVIDENCE_MISSING` with `persistence_active_snapshot_unavailable_during_save`. These are the documented
  non-repeatable-green baseline problems of the 2026-09-30 handoff, not S4.1 findings, and this record does not
  claim a clean baseline for them.
* **Not claimed:** no firmware or behaviour change; the native fixtures remain S4.1's behaviour acceptance, and
  this record adds the device observation and its reader. The retired-slot case stays covered natively (the
  switch shape's device row names the incoming slot, which the reader reports instead of substituting).

## Reproduce

```
# build + upload the pinned image, then drive the mandated base preset on the authorized target
.venv/bin/python scripts/host_midi_hitl.py run --preset base --track 5 --loop-slot 1 --midi-channel 1 \
    --build-upload --build-env teensy41-capture-serial --out-dir captures
.venv/bin/python scripts/hitl_s41_lease_release_read.py --log captures/session_<...>.log --slot 1 \
    --metadata <out_dir>/metadata.json --out s41-lease-release.json
```

Decision: recorded as **DEC-099** — the lease release is read from its own row by one owner, and the
evidence drain reconciles only the families its log can count.

Files: `lease-release-read.json` (HEAD run, generation required), `lease-release-read-20260930-pass.json`
(the PASS run), `lease-release-read-20260927-switch-negative.json` (the sibling-slot negative),
`run-identity.json`, and the two verbatim excerpts (CR stripped, blank rows dropped, source rows named in the
file's own header).
