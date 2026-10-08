# Device run: E2 lane-enabled HITL — the lane is served under transport within its bound, and the active slot's first window is still owed

**2026-10-08.** Continuation of **E2 / 3.15b — window reader handoff** device work: the pinned, lane-enabled HITL
measurement the previous session stopped short of (§"HITL boundary" in
[`../2026-10-08-s315-end-state-timing/EXECUTION.md`](../2026-10-08-s315-end-state-timing/EXECUTION.md)). The posture is the
one AGENTS.md allows for a dedicated HITL preset: build/upload and run without a separate flash prompt.

Registered profile `windowed_content_parity` (STIM-WINDOW-CONTENT-01) through the existing content-verification
pipeline; no canonical stimulus edited, no firmware source changed in this session.

| item | value |
|---|---|
| image | `teensy41-capture-serial-window-qualification` (`PLAYBACK_WINDOW_ROLLING_ENABLED=1`, `PLAYBACK_WINDOW_ROLLING_QUALIFICATION=1`) |
| `firmware.hex` sha256 | `e40a598e76797a6239c8160ef9e66a09cfd247d321b7125665b978a245adc0cf` (identical across every run below) |
| git | `68214b8de418e9a671db8d7b6e0266d47df10687` + dirty worktree |
| gesture | clear + record slot 4 (track 8), NOTE_EDIT window checks, exit; then the switch leg to slot 5 and the post-stop drain |
| captures | `captures/e2-windowed-qualification-20261008-run2`, `…-run3`, `…-run4`, `…-run4-drain`, `…-run4-playback`, `…-census2` |

Invocations (managed capture owns the port):

```
host_midi_hitl.py run --preset windowed_content_parity --track 8 --loop-slot 4 --midi-channel 8 \
  --build-env teensy41-capture-serial-window-qualification --no-boot-reset \
  --record-bars 24 --switch-target-slot 5 --switch-settle-s 60 --out-dir captures/e2-windowed-qualification-20261008-run4
```

## 1. The lane is served on hardware, inside its declared bound

Run 4 is the first lane-enabled run in which the active slot's window lane is **given and consumes work under
transport** and the gap report is complete:

```
window_demand_observed               true   gap blocks with eligible/progress turns=11; readiness receipts=1
readiness_receipts_under_transport   PASS   receipts=1 under_transport=1  first=owes_build/window_build_owner
windowed_service_coverage_under_transport PASS blocks=78 under_transport=45 with_opportunities=45 permitted=64
no_windowed_overrun_under_transport  PASS   overrun_rows=0
```

Across the 78 `DIAG,win_gap` schema-3 blocks (45 of them `transportRunning=1`):

| measured | value |
|---|---|
| max opportunity gap (`maxDeltaTicks`) | **59 ticks** (overall and under transport) |
| declared permitted advance | 64 ticks → **zero overruns** (`overruns=0`, `overrunMaxTicks=0` in every block) |
| max eligible-predecessor gap | 13 ticks |
| max eligible / progress / denied turns | 183 / 38 / **0** |
| max worker time | 398 µs |

So the earlier native-only drain numbers now have a hardware counterpart: on this image, under a real 24-bar record
and playback with Snapshots/persistence/display running, the window owner's own opportunity gap stayed under its
declared bound. This is a **measurement, not a passing contract**: see §3 for the parts that still fail.

## 2. What the lane is refused by, per run

The record-stop quantisation decides whether the loop is span-aligned (`al = loopLengthTicks % (2*TICKS_PER_BAR) == 0`):

| run | `--record-bars` | committed | ticks | aligned | outcome |
|---|---|---|---|---|---|
| run2 | 23 | 23 bars | 17664 | **no** | `win_gate … al=0 od=1 rw=1` — refused |
| run3 | 24 | 25 bars | 19200 | **no** | `win_gate … al=0 od=1 rw=1` — refused |
| run4 | 24 | 24 bars | 18432 | **yes** | lane served under transport (§1) |

Even when the commit is span-aligned (run4), the last gate rows for the **record target** (`t=7 s=3`) carry
`od=1` (a deferred overdub stop is pending) and `rw=1` (the record-return window is bound) while `pl=1`. The
`win_track_not_playing` exit and those two flags are the same statement the native work already names: after a Record
stop the active slot's own first window is still owed, and the record-return/deferred-stop state is what the gate
refuses on, not the entry trio (`ep=1 av=1 al=1`).

## 3. What still fails in the same run (not claimed)

* `capture_evidence_ring_intact` **FAIL** — one Tier-A row was evicted (`tier_a_evicted=1`, reported delta 3). This run
  is therefore **not** an unbroken-evidence capture; the numbers in §1 are still read from the surviving rows, but the
  ring's own integrity check is red and must be re-established.
* `window_boundaries_crossed_under_transport` **FAIL** — `build_complete_under_transport=1` (`loop_len=19200`,
  revision `1`); the lane published **once** under transport, not the ≥2 boundary crossings the check wants.
* `switch_target_span_alignment` **FAIL** — no `win_gate` row *under transport* for the switch target `t=7 s=4`.
* **Record session-exit root is not published.** In run2/run3 `snapshot: record_session_exit_root_published` FAILs
  (`record_rv=0`, `published_rv=None`) after the record stop, while the Snapshot owner's own rows climb (`snap_gate …
  cu=4…132, st=4750…4816` and `rv=0`). In run4 the verifier reports **no `DIAG,snap_gate` rows for the record target
  `t=7 s=3`** at all. So the whole-root publication the E2 handoff depends on was not observed on hardware in any of
  these runs.
* The reused leg's own display oracle fails on this image (`missing_bounded_window_disp_rows`,
  `insufficient_disp_during_note_edit_freeze count=0`, `missing_playing_bounded_window_disp`) — expected shape under the
  rolling lane, but it means the leg half of the combined report is **not** green.

## 4. Persistence: a failing scenario leaves the take unsaved

The scenario exits 2, the harness aborts the remaining scenarios and closes the capture **without** a device
shutdown, so the Record stop's deferred save stays owed. The take is then lost on the next reset: run3's committed
18432/19200-tick slot read back as **length 0** in the follow-up `!CAPTURE_CONTENT` dump and in the read-only boot
census (`track 7 slot 3 … len 0`). Only after an explicit graceful drain did the run4 take persist
(`CAPTURE_CONTENT,begin,track=8,slot=3,length=18432`, `rows,record,0,1147`). The drain was driven by a session-scratch
helper calling `hitl.device_shutdown.shutdown_device` with `cycles=2` (result `status=complete`, *2 stop+drain cycle(s)
reached verified persistence idle*). This is a **harness-sequencing** caveat, not a firmware verdict: the loop is only
durable once a stopping drain has run.

`dump_and_capture.py` + `verify_playback_content.py --midi-channel 8` over `captures/e2-windowed-qualification-20261008-run4-playback`:

```
content-review verdict: INCONCLUSIVE_CAPTURE — no unique effective event recurs in the playback window (effective_rows_all=1147)
```

The committed slot is present (`18432` ticks, `1147` record rows) but its playback never reaches the wire: the captured
output carries `note_on` on channels **0,1,2,6,7,14,15** and **none on channel 8**, i.e. the other tracks' restored
content sounds while the loop under test does not. So exact playback/content acceptance for the windowed lane is
**NOT established** in this run.

## 6. Device wedge observed once (recovered)

After run3 the board wedged with the transport nominally running but the tick frozen (`All Notes Off sent @ tick 61401`
and `Record stop retarget: phase=16 dest=1` repeating) and `!CAPTURE_CONTENT` answered `denied` (transport-gated).
Recovered by re-uploading the same pinned env (`pio run -e teensy41-capture-serial-window-qualification -t upload`),
after which the read-only boot census was clean (`LIBRARY OBSERVATION slots=64 complete=True`, 42 committed slots).
No manual SD operation.

## 7. Non-claims and next step

* No firmware source change, no default-route change, no task checkoff: **3.15a–f — single-path migration**,
  **3.10 — full bounded service** and **3.7b — legacy emitter retirement** stay unchecked; default rolling stays off.
* Not claimed: root-ready publication or the root reader handoff, record-return continuity, repeated boundary
  crossings under transport, display/frame continuity, exact MIDI/ledger/content acceptance, or a worst-case hardware
  envelope.
* Next, in order: (a) re-run for an unbroken ring (`capture_evidence_ring_intact`) and ≥2 under-transport
  publications so the §1 measurement is valid on a clean capture; (b) explain the missing/unpublished record
  session-exit root for `t=7 s=3` on this image — it is the standing blocker under both the lane's `sv`/`od`/`rw`
  terms and the content-parity `INCONCLUSIVE`; (c) keep the graceful-drain step in the recipe so a failing scenario
  does not lose its take before content verification.
