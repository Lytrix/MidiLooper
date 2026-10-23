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

**Correction (2026-10-08, same session).** The loop **does** reach the wire: the device's own `MO` log for that capture
carries `417` note-on rows on channel **8** *1-based* = mido channel **7** *0-based*, and the host capture's channel list
was read with mido's 0-based numbering — so "channel 8" was absent only from the host list, not from the device. The
`INCONCLUSIVE_CAPTURE` therefore means the verifier could not **align** a cycle (its anchor is a unique effective event),
not that the loop is silent. Treat the verdict as unestablished alignment, and re-run with an explicit cycle window
before drawing any content conclusion.

## 5b. The real symptom: after a Record stop the track can go completely silent

Reading the same three runs by *what the device emitted after the Record stop* separates them sharply (the `MO` row is
`MO,<type>,<ch>,<d1>,<d2>` with `ch` **1-based**; `LED` rows are the MIDI-LED feedback):

| run | committed loop | rows after the stop | MIDI out after the stop | LED after the stop |
|---|---|---|---|---|
| run2 | 17664 (23 bars, `al=0`) | 699 | **none on any channel** | **0** |
| run3 | 19200 (25 bars, `al=0`) | 1051 | **none on any channel** | **0** |
| run4 | 18432 (24 bars, `al=1`) | 11581 | ch8 = 1107 rows (the loop), 1 row per other channel | 1021 |

In run2 the transport then ran a further **94 s** (90.7 s → 185.2 s) and in run3 **110 s** (92.9 s → 203.3 s) with the
track at `pl=1` and only `DIAG,win_gate` / `DIAG,snap_gate` rows arriving — no playback, no LED. `captures/s64-demand-01`
(the 2026-10-07 lane run) shows the same shape: no channel-8 note-on during the leg, and only 20 in the whole drain.

The one device-side difference these captures show is what the target slot's Snapshot owner is doing: in the silent runs
it is engaged continuously for the target and its root revision never leaves **0**

```
run2: 45 snap_gate rows after the stop, max cu=212, last rv=0
run3: 48 snap_gate rows after the stop, max cu=218, last rv=0
run4:  0 snap_gate rows for the target after the stop, playback present
```

which is the same "given turns, publishes nothing" shape the S7.5b.5 packet recorded. That is a **correlation in three
captures, not an isolated cause** — run4 is the counterexample in which playback works and the owner never appears.

## 5c. The gate that can produce the silence (code reading, not a device measurement)

`Track::playMidiEvents` (`src/Track/TrackPlaybackHotPath.cpp`) returns before emitting on any of three terms:

1. `transportStopOutputBarrier_`, or a pending deferred overdub stop whose job admission does not
   `permitsPlaybackEmission()` — the latter is `!pending || destination == Playing`, so a Record stop **returning to
   play** is permitted and this term is not the silent case;
2. `isStoppedRecording()` — a track left in `STOPPED_RECORDING` emits nothing;
3. `!hasAudiblePlaybackSource(activeLoopIndex)` — audible iff `loop.hasCommittedPasses()`, **or** the slot's runtime has a
   servable deferred record view (`hasServableDeferredRecordView()`, the S4f.19 retained bar-1 return window "while a
   pending deferred job owns that window").

So a Record stop whose pass/root is not yet **committed** has no audible source of its own: it leans entirely on the
retained return window. The silent captures are consistent with that (owner engaged, `rv` stuck at `0`, no emission), and
`hasAudiblePlaybackSource`'s own comment names the failure mode: *"Without it the interval has no source yet and must stay
silent rather than fabricate one."* Whether the *cause* is (2), (3) or the Snapshot never committing is **not** isolated
by these captures — that needs a run that prints the track state and the audible-source predicate at the stop.

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

## 8. Controlled rerun — 24-bar record on track 8 slot 4, transport held running (2026-10-08, later)

Reproducing the exact gesture the owner asked for: record 24 bars on track 8 **slot 4** and then **keep playing**
(no switch leg, no immediate stop — the transport is held for a further 70 s by a session-scratch driver that reuses the
leg's own record sequence; no new MIDI bytes, no firmware change). Capture:
`captures/e2-slot4-record-play`.

| measured | value |
|---|---|
| committed take | `18432` ticks = **24 bars** (`raw=18885`, truncation rewind applied) |
| first channel-8 note-on after the Record stop | **+0.103 s** |
| channel-8 note-ons before the transport stop | **795** (the loop plays continuously) |
| max channel-8 inter-onset gap | **0.135 s** |
| LED rows after the stop | 3423 |
| post-stop window measured | ~119 s (1547.9 s -> 1666.6 s transport stop) |
| leg's own display oracle | `ok=True` (warnings only) |
| persistence | graceful drain `status=complete`, take persisted |

Follow-up dump of that take (`captures/e2-slot4-record-play-playback`): `length=18432`, `rows,record,0,1147`,
`rows,effective,0,1147`, and the dump session's own `MO` log again carries **418** note-ons on device channel 8. So the
loop is present and audible both live and after a reboot/restore, while `verify_playback_content.py` still returns
`INCONCLUSIVE_CAPTURE` — that verdict is a **cycle-alignment limitation of the verifier**, not silence and not content
loss. It deserves its own bounded fix (or an explicit cycle window).

### 8b. Correlation found, mechanism still open

Every comparable run now sorts by the committed loop's length:

| run | committed | bars | `% 1536` | post-stop playback |
|---|---|---|---|---|
| run2 | 17664 | 23 | 768 | **none** (94 s: no MIDI, no LED) |
| run3 | 19200 | 25 | 768 | **none** (110 s: no MIDI, no LED) |
| run4 | 18432 | 24 | 0 | present |
| slot4-play (this run) | 18432 | 24 | 0 | present, gap <= 0.135 s |

That is a clean separation over four comparable runs, but it is a **correlation without a located mechanism**:
`RuntimePlaybackView::bindRawCoverage`, `LoopPlaybackRuntime::bindFirstRecordingBar` and `hasServableDeferredRecordView`
show **no span/alignment requirement** — the bind needs only `rawCoveragePrepared && complete` and
`lengthTicks <= loopLengthTicks - startPhase`, both satisfied for 17664/19200. The record-stop bind-failure warning
(`First-record bar-1 playback binding unavailable`) is **absent** from run2/run3, so their return window was bound too.
Neither the `hasAudiblePlaybackSource` term (`hasCommittedPasses`, which run2/run3 lack while `rv` stays 0) nor the
emission gate explains why their LED feedback also stops. Do not treat "odd bar count" as the cause; treat it as the
discriminator to explain.

## 9. Immediate post-stop content comparison (2026-10-08 follow-up)

Run4's persisted `CAPTURE_CONTENT,event,record` inventory (`…-run4-playback/serial.log`) contains 16 channel-8 NoteOns in **each** of bars 1–4 (loop-local `[768,3840)`). In the original run4 session, the actual stop is at device time 167.728714 s, phase 473; compare `#CAP` device timestamps against its `BAR` rows at 168.520299 (bar 1), 170.507971 (bar 2), 172.504190 (bar 3), 174.500167 (bar 4) and 176.498468 (bar 5). The channel-8 NoteOn counts in those four traversals are **1, 0, 0, 16**, respectively, against **16, 16, 16, 16** in the committed inventory. There are 15 channel-8 NoteOns in the partial bar 0 after stop, so this is **stutter/omitted early-bar output, not five bars of total silence**. The 1.999 s and 4.122 s inter-onset gaps surround the missing bars. The timing comparison uses device `MO` (1-based channel 8), not host mido's 0-based channel.

The complete committed pass and later playback prove the notes exist; they do not alone identify which owner/reader gate dropped these early traversals. A `DIAG,win_gap` opportunity measurement does not establish a complete successor for the affected bar. The run4 ring reports Tier-A eviction (§3); missing rows by themselves are not an absence proof, but the separately persisted inventory and later full bar make the content mismatch a concrete reproduction target. A new flagged native 24-bar stopped-Record fixture with 16 pairs/bar, phase473 stop and bounded 64-tick Track owner opportunities is RED at the first early bar: frozen Capture reader Idle, no ready despite an eligible holder. The production frozen builder scans the whole 768-row range per one-bar window (≥24 fill grants plus ordering) before publication, while fewer than five such opportunities fit before bar 1. This proves a source-inspection/readiness blocker for the controlled shape and is consistent with the device gap, not exclusive attribution of all device symptoms. The separate two-bar root-current oracle fails at `CannotCompleteBeforeHandoff` with a **published** root; it is not this private-root long-loop reproduction. No firmware fix or hardware acceptance is claimed; see the E2 — long Record frozen-range scan RED packet.
