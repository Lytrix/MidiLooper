# The tempo fader on device, and the first 300 BPM HITL legs

**Status: the fader is device-verified; the 300 BPM legs are inconclusive and their causes are named.** Nothing here
declares a limit, a qualification or an in-envelope run, and **S7.5a/S7.5b/S7.5 stay unchecked**. The device work was
read-only apart from the global tempo: no record/clear of loop content by the probe, no SD mutation.

## 1. The BPM fader, verified on hardware

Image: the tree at `aa581792` + the probe fix, uploaded to `teensy41-capture-serial` (upload SUCCESS).
Entry point: `scripts/hitl_tempo_fader_probe.py` → `captures/tempo-fader-01/tempo-fader.log`.

| Sent (signed pitchbend, ch15) | Device `TEMPO: PB=` | Expected | Verdict |
|---|---|---|---|
| −1672 | 120.0 BPM | 120.0 | PASS |
| −1666 | 120.1 BPM | 120.1 | PASS |
| −1661 | 120.2 BPM | 120.2 | PASS |
| 8191 (full upper travel) | 300.0 BPM | 300.0 | PASS |

* The probe's own resolution check printed **`PASS: 0.1 BPM steps are distinguishable [120.0, 120.1, 120.2]`** — the
  discriminator the retired 7-bit CC 102 fader could not express.
* The session-entry park is a pitchbend: `Sent tempo CC feedback: 132.3 BPM -> PB=-998`.
* **Caveat, stated rather than smoothed:** the probe's mode gate was **inconclusive** on this run — the ADVISORY
  fired because the device was already in `LOOP_EDIT` (left there by an earlier session), so "outside `LOOP_EDIT` the
  tempo pitchbend does nothing" was **not** proved here.
* Process note (cost 20 minutes): the probe's `--serial-port` default is a pinned device path; after a flash the
  Teensy re-enumerates and the path can be briefly absent, so the run failed until the port was re-resolved. The
  skill's re-enumerate loop (`for i in $(seq 1 25); do ls /dev/cu.usbmodem* …`) is the fix; the probe should not
  carry a pinned default. First probe attempt also crashed on mido's message type: it is `pitchwheel` and its `pitch`
  is the **signed** value (−8192..8191), which is also what the firmware logs.

## 2. The default HITL at 300 BPM: two attempts

The fixture's timing vocabulary is musical: `MidiClockMaster.start(bpm=…)` drives a pinned 24 PPQN grid the device
**slaves** to (`--clock-source host`), while the default `--clock-source device` makes the host *read* the device
clock (`MidiClockReader.bpm`, a rolling pulse-interval median) and wait on **pulse indices** (one bar =
`MIDI_CLOCKS_PER_BAR`, a 16th = `MIDI_CLOCKS_PER_BEAT / 4`). `--tempo-bpm` is the tempo argument.

| Leg | Invocation | Outcome |
|---|---|---|
| A | device at 300 BPM, default clock source | `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)`, abort **`midi clock missing before record phase`**, 46 `,BAR,` rows — the fixture's "read the device clock" precondition never satisfied while the device ran on its own clock |
| B | `--clock-source host --tempo-bpm 300` | `INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)`; **reached record and materialized a loop**, `RECS,stop,3,2248,0,2248,2304,0`, 2546 `MO` rows, 33 `REVT` rows, advisory `record_first_note_too_late` |

So the mechanism the owner remembered — *host-owned clock, musical waits* — is what let leg B reach record at all.
Directory: `captures/base-300bpm-hostclock-t8s4-01` (leg A: `captures/base-300bpm-t8s4-01`).

## 3. Finding A — the committed length is **not** tempo-derived

From leg B's report:

```
config.record_bars                       = 2
record_loop_length.expected_final_length = 1536   (2 bars, the fixture's intent)
record_loop_length.actual_final_length   = 2304   (3 bars)
record_loop_length.raw_length            = 2248
record_loop_length.final_length_ok       = True   <- accepted: the committed length IS bar-aligned
overdub_wrap_storage.loop_length_ticks   = 2304
stored_overdub_span.bars_reached = 3 vs expected_bars = 2, bars_match_ok = True
```

`RECA,3,0` at 252469957 arms; the stop edge lands at 254825317, i.e. **2248 raw ticks** — one bar past the intent —
and the device bar-aligns to 2304. `final_length_ok` is true because the length is bar-aligned, **not** because it
matched `record_bars`.

**The same 2304-vs-1536 shape appears in the 120 BPM runs** (`captures/envdist-t8s4-01`, and the earlier
note-edit-enter leg committed 2304 against a 24-bar intent). This is therefore a **pre-existing stop-edge/fixture
pacing issue, tempo-independent** — not something 300 BPM introduced.

## 4. Finding B — the fixture's first note per phase is **not** tempo-following

The bound is musical (`*_first_note_max_clocks = 12`), so it shrinks in wall-clock as tempo rises, while the fixture's
first note did not follow it:

| Phase | Phase start (µs) | First note (µs) | Offset | 12-clock allowance |
|---|---|---|---|---|
| Record | 252469957 | 252688922 | **219.0 ms** | 96 ticks = **100 ms** @300 BPM (250 ms @120) |
| Overdub 1 | 256192497 | 256528176 | **335.7 ms** | 100 ms |
| Overdub 2 | 267302701 | 267595284 | **292.6 ms** | 100 ms |

At 120 BPM the same allowance is 250 ms, so 219 ms passes and 335 ms is marginal: this is exactly why the
lateness only surfaces at 300 BPM. The device is not late; the **fixture's first note arrives 2–3× later than its
own musical bound allows**. The fixture is musical in its *bar waits* and not in its *first-note scheduling*.

## 5. Finding C — the wrap gap is suggestive, **not** adjudicated

Stamped emission rows by loop bar (tick//768): **0: 741, 1: 696, 2: 518, 3: 0, 4: 0, 5: 1** — a gap where the second
overdub's wrapped content should be. That is consistent with the owner's "last overdub missing notes after wrap".

**Discarded:** a stored-event (`REVT`) per-bar histogram computed from the same log, because the field I parsed
turned out not to be the tick (it read "all stored events in bar 0", which contradicts the emission spread). It is
noted here so the reading is not reused.

The adjudicating instrument is the skill's content pipeline (`dump_and_capture.py` → `!CAPTURE_CONTENT` plus playback
cycles → `verify_playback_content.py` → `content-review.json` with per-bar counts and `inventory_equal`). Counting
`MO` rows is a hint, not a verdict.

## 6. The leg's own failing predicates

```
assertions.serial_verification.ok = false ; overall_ok = false
issues = [overdub_unmatched_open_notes, record_stored_revt_missing]
```

`overdub_unmatched_open_notes` is the class the HITL skill classifies as **"dense generator left notes held at stop;
setup verdict only, unrelated to snapshot content"**. `record_stored_revt_missing` is a stored-event-row expectation
in a run that also carried the known "SEVT arming fix pending" advisories. Neither is continuity evidence.

## 7. Owed

1. **Fixture:** make each phase's first note tempo-following (Finding B) and fix the stop edge so `record_bars` is
   honoured (Finding A).
2. **Content verdict** on the wrap gap (Finding C) via the content-verification pipeline.
3. Evidence write-ups: this packet covers the probe and both legs; `PROJECT_STATE`/`CURRENT_WORK` closeout entries for
   `c5056585`, `aa581792` and `69754c97` remain owed.

## 8. Correction (same day): the first-note lateness is **not** 300 BPM-specific

§4 above was written from the 300 BPM leg alone and said the lateness "only surfaces at 300 BPM". Comparing the same
report fields against the 120 BPM leg (`captures/envdist-t8s4-01`) shows that is wrong, and the cross-tempo view is
more useful:

| Phase | 120 BPM `offset_clocks` (of which capture latency) | 300 BPM `offset_clocks` (of which capture latency) | Bound |
|---|---|---|---|
| Record | **16** (16) — already `record_first_note_too_late` | **26** (26) | 12 |
| Overdub 1 | 28 (**4**) | 40 (**16**) | 12 |
| Overdub 2 | 31 (**7**) | 35 (**11**) | 12 |

What that changes:

* **`record_first_note_too_late` is pre-existing**, not introduced by 300 BPM: at 120 BPM the record phase's first note
  is already 16 clocks past a 12-clock bound, and the same advisory appears in the 120 BPM run's own timing warnings.
* What *is* tempo-dependent is the **shape**: the offset and its capture-latency component both grow with tempo
  (record 16→26, overdub1 capture latency 4→16, overdub2 7→11), which is the signature of a **wall-clock-paced
  first-note send** rather than a clock-indexed one — the same wall-clock delay costs 2.5× more clocks at 300 BPM.
* The **overdub** offsets are mostly *not* capture latency (28 with 4, 40 with 16): the generator places the first
  overdub note later in the bar by design, so its offset is a musical placement, while the **record** phase's offset
  *is* entirely capture latency (16 with 16, 26 with 26). Those are two different things and should not be fixed by
  the same change.

So the fixture fix splits in two: (a) the **record** first note, whose whole offset is capture latency, needs the
phase-start-to-first-send path to become clock-indexed (and its 12-clock bound re-examined, since it fails at 120 BPM
alone); (b) the **overdub** first note needs its intended musical placement preserved while whatever wall-clock
component it carries is removed.

## 9. Refinement: the wall-clock component is a host *observation* delay, not a sleep in the phase

§8 split the first-note offset into an intended musical placement plus a wall-clock component. The obvious candidate —
the flat `time.sleep(0.35)` at `scripts/hitl/legacy_record_baseline.py:1029/1047` — is **wrong**, and reading the block
is what shows it: those sleeps live in `_prepare_deferred_save_before_clear`, i.e. in the transport-stop/clear prep
that runs **before** the record phase begins, so they cannot sit between `phase_start_ts` and the first note.

What the shape actually fits, given `extract_first_note_offset` measures from the **device's** phase boundary to the
**device's** timestamp of the first incoming note:

* A host that starts its note stream **after observing** the device's phase transition (a serial-polled reaction, tens
  of milliseconds and jittery) produces exactly this signature: an offset that is partly musical, partly fixed-in-time,
  growing in clocks with tempo, and **varying per phase** (the fitted wall-clock component was ~139 / ~167 / ~55 ms for
  record / overdub1 / overdub2 — a spread that a single sleep could not produce, but polling jitter plus per-phase
  ordering can).
* The record phase passes `phase_start_delay_clocks` = 0, i.e. the fixture *intends* the first recorded note at the
  phase start — so on that phase the whole measured offset is the observation/reaction delay.

So the fix is not to delete a sleep. It is to **re-anchor the note stream's start on the clock grid**: when the host is
the clock master (`--clock-source host`, where it owns the 24 PPQN grid and knows the phase's first pulse index) the
generator should begin on that pulse index rather than on the observation of a serial row; and the overdubs' intended
placements (`phase_start_delay_clocks` ≈ 20 and ~28 clocks) must be preserved as musical positions, not replaced by a
wall-clock wait.

Evidence status: **no fixture change has been made.** This section corrects the candidate mechanism so the next attempt
starts from the code rather than from the sleep I first suspected.

## 10. The fixture already has a grid-scheduled record stream — this may be a *selection* problem

Reading the module's stream shapes settles the shape of the fix:

```
  226  _stream_dense_chromatic
  267  _stream_overdub_wrap_note_off_test
  346  _stream_pattern_for_seconds            <- wall-clock ("for seconds") generator
 1087  _stream_fixture_record_wall_scheduled  <- wall-clock ("wall scheduled") record stream
 1184  _stream_fixture_record_serial_anchored <- anchored on *observing* serial, i.e. §9's delay
 1388  _stream_fixture_record_bars            <- bar/index based: the grid-scheduled one
```

So the base preset has (at least) three record-stream shapes and two of them are exactly the defect classes §8/§9
measured: `_stream_fixture_record_wall_scheduled` is wall-clock by name, and `_stream_fixture_record_serial_anchored`
anchors the stream on the *observation* of device serial — which is the reaction delay that inflates `offset_clocks`
with tempo. **`_stream_fixture_record_bars` is the grid/bar-scheduled stream**, i.e. the tempo-following one the owner
remembered.

That reframes the work: the next step is not to write a tempo-following generator but to **find which stream the base
preset selects and on what condition**, and make the qualifying run use the bar-scheduled shape (or convert the
selected shape's start to a bar/pulse index). If the selection is already a flag, the 300 BPM leg may need no fixture
code at all — only the right invocation.

This is recorded as the next step rather than a conclusion: **no fixture change has been made** (nothing in
`legacy_record_baseline.py` was edited), and the selection point has not yet been read.

## 11. Which configuration is most stable: three runs compared

The three stream shapes of §10 are **not** freely selectable call sites: the cascade at
`legacy_record_baseline.py:1499–1544` picks the **serial-anchored** stream whenever serial capture plus a `RECA`
receipt exist (the normal case, including both 300 BPM runs below), path 2 (`wall_scheduled`) needs *no* clock
reader **and** no master and is therefore unreachable with any `--clock-source`, and `_stream_fixture_record_bars`
(`:6512`) belongs to a different function/path. So what could be varied was the **phase anchor**: host clock vs
device clock.

| Run | `overall_ok` | record off/lat | od1 off/lat | od2 off/lat | record length exp/act | issues |
|---|---|---|---|---|---|---|
| 120 BPM, device clock (`envdist-t8s4-01`) | **True** | 16/16 | 28/4 | 31/7 | 1536/1536 | — |
| 300 BPM, host clock | False | 26/26 | 40/16 | 35/11 | 1536/**2304** | `overdub_unmatched_open_notes`, `record_stored_revt_missing` |
| 300 BPM, device clock | False | **14/14** | 40/16 | **27/3** | **1536/1536** | `overdub_stored_span_too_short`, `second_overdub_stored_span_too_short` |

*(`record_first_note_too_late` is an advisory in all three; offsets are `offset_clocks/capture_latency_clocks`.)*

**Verdict: the device clock is the more stable configuration at 300 BPM**, and it is the right base to fix from:

* **The stop edge is correct only with the device clock** — 1536/1536 exact, against the host clock's 2304 (one bar
  late, the defect §3 already recorded). This is a reason to prefer it, not a preference.
* **The first-note offsets improve under it**: record 26 → **14** clocks (of which 14 is latency, against a 12-clock
  bound), od2 35/11 → **27/3** (capture latency back in line with the 120 BPM baseline's 7).
* **What does not improve**: overdub 1 stays at 40/16 in both 300 BPM variants, and both 300 BPM runs still carry
  stored-span / unmatched-open-note issues — the class the content pipeline must adjudicate (§5).
* **Neither 300 BPM configuration is stable yet**: the 120 BPM device-clock run remains the only `overall_ok=True`.

**Why the residual is where it is:** path 1 exists to fix a real bug — its own header (`:1491`) records that the
modelled transport-start wall clock put the fixture's stream *ahead* of the take — but it then anchors the stream on
a **polled observation** (`parse_record_start_tick` in a 20 ms loop), which is exactly the wall-clock term of §8/§9.
The next change belongs inside that path: start the stream on a **pulse index** derived from the receipt's tick
rather than on the instant the receipt was observed.

## 12. The stream is already clock-indexed — the false advisory is a *checker* attribution

Reading `_stream_fixture_record_serial_anchored` (`:1184`) shows the stream is **not** wall-clock paced at all:

```python
anchor = TransportTickAnchor.from_collector(serial_collector, bpm, fallback_tick=reca_start_tick)
while anchor.estimated_tick() < end_transport_tick:
    anchor.refresh(serial_collector)
    transport_offset   = max(0, anchor.estimated_tick() - reca_start_tick)
    target_phase_clock = min(target_clocks, transport_offset // TICKS_PER_CLOCK)
    if phase_clock < target_phase_clock:
        phase_clock += 1
        _emit_fixture_steps_for_clock(phase_clock=phase_clock, step_clocks=6, ...)
```

Notes are emitted **per clock index** off the device's own tick (`step_clocks = 6` = one 16th note at 24 PPQN), which
is exactly the tempo-following behaviour the owner remembered. So the residual is not in the stream.

It is in the **checker's attribution**. `capture_latency_clocks = max(0, offset_clocks - phase_start_delay_clocks)`,
and the record phase passes `phase_start_delay_clocks = 0` — but the fixture's first record note is placed at a
**non-zero step** by design. That intended placement is therefore counted as capture latency:

| Run | record offset | fixture's intended first-note placement (step x 6 clocks) | residual latency |
|---|---|---|---|
| 120 BPM, device clock | 16 | 12 | **~4 clocks** (5 ms) |
| 300 BPM, device clock | 14 | 12 | **~2 clocks** (1.7 ms) |
| 300 BPM, host clock | 26 | 12 | ~14 clocks (the host-clock anchoring is the real outlier) |

Two consequences:

1. **`record_first_note_too_late` is a false positive** and always will be while the caller passes `0`: the record
   phase's first note is 12 clocks in *by design*, so it trips the 12-clock bound at every tempo. The advisory is not
   evidence about the device or the stream.
2. **The real capture latency of the first note is small and tempo-stable** (~4 clocks at 120, ~2 clocks at 300), and
   the genuine outlier is the **host-clock** run (26 - 12 = 14 clocks), which is consistent with section 11's verdict
   that the device clock is the correct base.

**The change to make is therefore in the verification half, not the stream**: pass the fixture's intended first-note
placement (its first `step` x `step_clocks`) as `phase_start_delay_clocks` for the record phase — and re-read the
overdub phases the same way, since od1's 40/16 may likewise carry its intended placement. That makes
`capture_latency_clocks` mean what its name says and retires the advisory as a false positive rather than by raising a
bound. **No change has been made yet**; this section records what the read established.

**Superseded in part by §14:** the intended placement this section inferred for the *base record* phase (12
clocks) is not that stream's placement -- reading the recorded leg shows it emits the immediate step 0 and is
genuinely ~13 clocks late. §14 keeps this section's *checker* finding (the subtrahend must be the fixture's own
placement, not a constant) and refutes the number.

## 13. Plan step 0 executed: the granularity pin (the 12-vs-13 question answered from the code)

A second-opinion review of §12's plan flagged that `legacy_edit_baseline.py:1775` schedules fixture step 2 at phase
clock **13** while the wall scheduler targets **12**, and asked for that to be decided and tested explicitly. Reading
the emitter settles it:

```python
current_16th = (phase_clock + step_clocks - 1) // step_clocks - 1
while next_emit_step <= current_16th and next_emit_step < target_bars * 16:
    for entry in fixture_by_step.get(next_emit_step, []):
        out_port.send(... note_on ...)
        held.append((entry.pitch, phase_clock + entry.gate_steps * step_clocks))
    next_emit_step += 1
```

With `step_clocks = 6`, step `s` is first emitted when `ceil(phase_clock/6) - 1 >= s`, i.e. at
**`phase_clock >= 6s + 1`** — so step 2 goes out at clock **13**, exactly as the review measured. The musical placement
is `2 x 6 = 12`; the extra clock is the emitter's own ceiling-minus-one granularity.

**Decision (intent = musical placement):** the verifier's subtrahend is the note's **musical** clock (`first_step x
step_clocks` = 12), and the emitter's +1 clock stays in `capture_latency_clocks`. Declaring 13 instead would fold the
fixture's own emission granularity into "intent" — the failure mode the review explicitly warns against ("don't fold
actual lateness into intent just to clear the warning"). The measured record residual (~2 clocks at 300 BPM, ~4 at 120)
is therefore now fully explained: 1 clock of emitter granularity plus 1-3 clocks of real host/device latency.

**Fixture inconsistency, now explained rather than assumed:** the wall-scheduled variant targets 12 and this one emits
at 13, so the two pacing modes differ by that one clock *by construction*. The plan's first test asserts exactly this
(`step 2, step_clocks 6` -> intent 12; an emission at 13 shows as 1 clock of latency), so the difference is pinned
instead of being absorbed into whichever number a future edit finds convenient.

The review's other two corrections are confirmed in the code as well and are in the plan: the overdub path really does
use its own generator (`_stream_pattern_for_bars`, called at `:5026` with its own `phase_start_delay_bars/beats`), so
the fix must have **every** generator declare its own intent and must retire the duplicated
`_phase_start_delay_clocks(...)` calls at `:4399`/`:4593`.

**Status: plan step 0 done; steps 1-4 (generators declare intent, single mapping threaded into
`_build_serial_verification`, per-mode + delayed-overdub + genuine-lateness + no-inflation tests, then the device
re-run and the content verdict) are not implemented.** No code was changed by this section.

**Superseded in part by §14:** this pin is correct for a fixture whose first note is at step 2 and is now tested
as such (`scripts/test_hitl_first_note_intent.py`), but the base preset's record stream emits the *immediate*
step 0, so its declaration is 0 -- and steps 1-3 are implemented there. Steps 1-3 landed; step 4's device re-run
and the content verdict are not run (§14 says why).

## 14. Steps 1-3 implemented, and step 4's premise is refuted by the fixture's own stored content

**Status: the intent mapping landed (host tooling only; no firmware line, no device leg, no limit declared).**
Section 13's *granularity* decision stands and is now tested; its *step-2 premise* does **not** hold for the base
preset's record stream, so the record-phase advisory is not an intent problem and step 4's expected outcome
("the advisory gone with `capture_latency_clocks ≈ 2`") cannot be reached by this change.

### What landed

* `scripts/hitl/transport_clock.py` -- `stream_pattern_for_bars` declares `first_note_intent_clocks` in its own
  `timing` evidence: `phase_delay_clocks + (0 if emit_immediate_first_step else step_clocks)`. The two branches
  differ by construction and that is intended: the immediate step *is* the downbeat and lands on the phase's
  first clock (so the +1 stays latency), while the non-immediate grid's first step is emitted exactly on its own
  grid clock (its `>=` on an integer counter has no ceiling artifact).
* `scripts/hitl/legacy_record_baseline.py`:
  * `FIXTURE_STEP_CLOCKS` (= `RECORD_GRID_STEP_CLOCKS`, one 16th at 24 PPQN) and
    `_fixture_first_note_intent_clocks(fixture, step_clocks)`: the fixture's first emitted step as a *musical*
    clock position (`step × step_clocks`). The emitter's own clock (`step_clocks × s + 1`) is deliberately not
    part of it.
  * All three record stream shapes return their own declaration: `_stream_fixture_record_wall_scheduled`,
    `_stream_fixture_record_serial_anchored`, and `_stream_fixture_record_bars` (which passes its sub-shape's
    declaration through).
  * `FirstNoteIntent` + `_declared_intent_clocks` + `_agreed_declared_intent` + `_first_note_intents`: **one**
    per-phase mapping, threaded into `_build_serial_verification(..., first_note_intent_clocks=...)` and used for
    the record, overdub and second-overdub offsets. `source` says who declared it (`streamer`, from the timing
    evidence the run records into `per_track_stats`, or `run_default`, the run's own
    `--*start-delay-bars/beats`). The duplicated `_phase_start_delay_clocks(...)` calls in the verifier are
    retired; the fallback survives only for a streamer that declares nothing (the occupy and wrap-note-off
    flows). The row keys live in one `FIRST_NOTE_INTENT_ROW_KEYS` constant shared by the writer and the reader,
    so a rename cannot silently degrade every phase to `run_default`; and because the row that owns the verified
    phase window is not identifiable from the rows alone (the layered `verify/capture.py` path passes no
    `phase_after_index`), a phase is only taken from the streamer when the run's tracks *agree* -- a
    disagreement falls back to the run-declared delay rather than subtracting another track's placement.
* `scripts/test_hitl_first_note_intent.py` -- **20/20**: the granularity pin (step 2, `step_clocks 6` →
  intent 12, emission 13, so 1 clock stays latency), one declaration test per pacing mode (wall schedule and the
  serial tick anchor with a *non-zero* first step -- the discriminating input -- the bars mode, and the grid
  pattern with and without the immediate step), the edit fixture's own downbeat through the shape that streams
  it, the delayed overdub's own placement, genuine lateness still warning (`intent + bound + 1`), no intent
  inflation, and the mapping's ownership, fallback and cross-track agreement. **Eight** mutations red-proved
  with byte-identical restores: the grid intent folds the emitter clock; the fixture intent folds it; the
  mapping ignores the declaration; the verifier drops the record intent; "declared nothing" reads as a
  declaration; the serial-anchored declaration reverts to the old constant; the mapping trusts one row instead
  of agreement; the reader stops sharing the writer's row key.
* One independent review pass (adversarial, read-only) found no runtime defect and three test-level gaps; all
  three are repaired above (the serial-anchored declaration's discriminating input, the two tautological
  assertions, and the writer/reader key coupling), with the added cross-track agreement guard coming out of the
  same finding. Full host sweep **1,404 tests, 6F+1E** -- the same pre-existing failure set as the pristine
  `a8d50522` worktree (which ran 1,387 tests: the difference is exactly this new suite), with no new failure.

### The refutation: the base record phase intends the downbeat

Section 12 read the record phase's intended placement as 12 clocks (step 2 × 6). The recorded 300 BPM device
clock leg says otherwise, three ways:

| Evidence | Reading |
|---|---|
| Note-ons in the record window | **33** = 1 immediate step + 32 grid steps (2 bars × 16), pitches ascending from 48 = `--record-low-note`, spacing 6 clocks |
| The stream shape | `_stream_pattern_for_bars(step_clocks=RECORD_GRID_STEP_CLOCKS, phase_start_delay_bars/beats=0, emit_immediate_first_step=True)` -- the first note *is* step 0 on the first clock |
| The device's own stored grid (`REVT`) | the 48..79 grid's first stored tick is **108** at 300 BPM (13.5 clocks), matching the measured 14-clock offset; the ticks 9/57 (120 BPM: 25/72) belong to a different pitch family (77-79), so `stored_record_grid.first_tick` does *not* name the record grid's first note |

So the fixture's first record note is 108 ticks late at 300 BPM, and the advisory is **true**:
`capture_latency_clocks` of 14 is host pre-stream latency (the fixture starts streaming after it has confirmed
RECORDING), which is a *pacing* term. Declaring 12 would have hidden exactly 12 clocks of real lateness -- the
failure mode section 13 was written to prevent. Section 13's granularity pin remains correct *for a fixture whose
first note is step 2*; no run in this packet uses such a fixture, and the edit fixture in the tree starts at step 0.

### Behaviour for this configuration: none, replayed on the recorded leg

The real 300 BPM device-clock capture, run through the new verifier twice -- with the recorded row (no
declaration → `run_default`) and with the row the run now writes (streamer declarations 0/24/24):

| Phase | recorded report | replayed, old row | replayed, new row |
|---|---|---|---|
| record | offset 14, intent 0, latency 14 | 14 / 0 / 14 `run_default` | 14 / 0 / 14 `streamer` |
| overdub 1 | offset 40, intent 24, latency 16 | 40 / 24 / 16 `run_default` | 40 / 24 / 16 `streamer` |
| overdub 2 | offset 27, intent 24, latency 3 | 27 / 24 / 3 `run_default` | 27 / 24 / 3 `streamer` |
| `timing_warnings` | `record_first_note_too_late` | same | same |

The base preset's declaration *equals* its run default on every phase, so the advisory is unchanged and a device
re-run cannot show it cleared. What the change buys is ownership: a streamer that emits its first note a step into
the bar now describes that placement, and the verifier no longer guesses it from the CLI.

### Owed, and now separated

1. **The record-phase advisory is a pacing problem, not an intent problem (recommended next).** The base record
   phase starts its stream after confirming RECORDING, which costs 14 clocks of host latency at 300 BPM (16 at
   120). The measured shape that avoids it already exists in this file: the serial-anchored path
   (`_stream_fixture_record_serial_anchored`, used by the edit-fixture seed) lands its first note on the
   recorder's own `RECA` tick, and the edit seed's first note measured 0 clocks
   (`captures/live-current-root-entry-t8s4-05`). Re-anchoring the base record stream on the clock grid is a
   fixture change with a device leg to prove it -- not an "intent".
2. **The content verdict on the stored-span/wrap class (section 5) is still not taken**, and is a separate device
   leg through the skill's content pipeline (`dump_and_capture.py` → `!CAPTURE_CONTENT` → playback cycles →
   `verify_playback_content.py`); it needs a per-session authorization for the bench target's content. Neither
   the device re-run nor the content leg was run in this session: a device leg cannot show a change the replay
   above proves is a no-op.

**Not changed:** no firmware line, no build, no upload, no SD or device action, no limit or envelope declared, no
`DEC-###`, and S7.5a/S7.5b/S7.5 plus S7.5b.8c/d stay unchecked.

## 15. Device re-run at 300 BPM on this tree: the fixture still does not complete a leg, and the change is inert here

Owner-requested run ("run the hitl at 300bpm to see how it now performs"). **Host tooling as landed; no firmware
change, no upload, no reset, no SD delete/rename.** Invocations are the canonical entry point
(`host_midi_hitl.py run --preset base --track 8 --loop-slot 4 --midi-channel 8 --no-boot-reset
--capture-boot-wait 30 --tempo-bpm 300`), differing only in `--clock-source` and the out-dir.

Tempo: the device was at **132.3 BPM** on attach, pinned to **300.0 BPM** with the sanctioned probe
(`hitl_tempo_fader_probe.py --set-tempo 300 --skip-full-travel`, `captures/tempo-pin-300b-before-leg`; its
`PASS: pitchbend=8191 -> 300.0 BPM`, with the same mode-gate advisory as section 1 -- the device was already in
`LOOP_EDIT`, so "outside `LOOP_EDIT` the pitchbend does nothing" is again not proved).

| Leg | Clock | Abort | Record length (final/expected) | Record first note | Warnings | Shutdown |
|---|---|---|---|---|---|---|
| `base-300bpm-after-intent-t8s4-01` | device | `midi clock missing before record phase` | 3072/1536 (raw 3110) | none streamed | -- | complete/2 |
| `base-300bpm-devclock-after-intent-t8s4-02` | device | `recording not confirmed before note stream` | 768/1536 (raw 607) | none streamed | -- | complete/2 |
| `base-300bpm-hostclock-after-intent-t8s4-03` | host | `second overdub transition not confirmed` | **2304**/1536 (raw 2312) | **33 clocks** (offset 33, intent 0) | `record_first_note_too_late` | incomplete -> see below |
| `base-300bpm-devclock-after-intent-t8s4-04` | device | `second overdub transition not confirmed` | 2304/1536 (raw 2394) | none streamed | -- | complete/2 |

**Reading it.** Leg 1 is the recorded "leg A" failure reproduced exactly, and its cause is now named by the
device's own rows: the previous session left the transport **running**, so the arm landed on a playing track
(`Loop 4: Start Recording` -> `Recording started @ tick 22827`, i.e. no `ARMED` step and no `ARMED->RECORDING`
row), the host never streamed a note, and the fixture then refused at its clock preamble. Leg 2 did reach
`ST,Track,STOPPED,ARMED` -> `ARMED,RECORDING` (a 768-tick take, `Recording started @ tick 0`), but the host's
confirmation window missed it and it aborted before streaming. Leg 3 reached record, overdub 1 and a second
overdub attempt on the host clock: the take came out **2304 ticks (3 bars) against the 2-bar intent** -- the same
shape section 3 recorded as tempo-independent -- and the record first note measured **33 clocks** against the
12-clock bound (worse than the recorded host-clock leg's 26, and consistent with section 11's verdict that the
device clock is the better base). Its stored overdub spans passed (`16/15`, `20/7`, `bars 2/2`, `span_ok`), so
content storage is not what failed. Leg 4 reproduced the second-overdub refusal on the device clock.

**The change is not implicated, and the reports say so in a new field.** Every leg aborted *before* its per-track
row was appended (`per_track_stats` is empty in all four), so the verifier used the **run defaults** -- exactly
the pre-change subtrahend -- and the new `intent_source` field records that provenance in each report
(`run_default`, with `intent` 0 for record and 24 for the overdub phases). The arithmetic is identical to the
recorded legs: leg 3's `record latency 33 = offset 33 - 0`, `od1 latency 7 = 31 - 24`. Nothing here measures the
declaration path, which needs a leg that reaches its per-track row (the replay in section 14 covers it).

**Device safety.** Leg 3's shutdown was `incomplete_timeout` (`drains 0`, "persistence never reached verified
idle") because a `LoopPersist slot:7:4` workspace save was still slicing when the log ended; the latch was
honoured (no reset attempted) and **discharged in the documented order**: a non-reset attach, two idempotent
realtime MIDI Stop -> verified-persistence-idle cycles (`shutdown_device` -> `complete, drains 2, ok true`,
`captures/drain-after-300bpm-hostclock-03/`), the latch payload preserved as `latch-before.json`, and only then
`clear_latch()`. Leg 4 then drained cleanly on its own (`complete/2`). **Left as is:** the transport stopped,
persistence drained, no latch, and track 8 / slot 4 **`ARMED`** with an empty capture (`DISP,3,ARMED`) -- an
armed target consumes the next press, so the next leg must carry a fresh boot (`--boot-reset`, now unblocked) and
re-pin the tempo after it; no further press was sent here.

**What this changes about the plan:** nothing in sections 13-14. The blocker at 300 BPM is the fixture's
setup/confirm handshake (a clock preamble evaluated against a device that may be running, a `RECORDING`
confirmation window that can miss the row, a second-overdub confirmation that can fail) plus the 3-bar stop edge
-- all pre-existing, all on the *pacing* side. The bounded next step is unchanged: anchor the base record
stream's start on the clock grid (section 14 owed-1), with these four legs as the "before" baseline.

## 16. Boot-reset leg: a clean boot does not complete it either, and the image on the board is newer than the recorded legs'

Owner-requested follow-up ("one more leg with `--boot-reset` + a fresh 300 BPM pin"). **Soft** reboot only
(`--boot-reset` asks the device for `!REBOOT`; no upload, no flash, no SD operation), device clock, same
invocation otherwise:

`base-300bpm-bootreset-t8s4-05` -- the boot reset is confirmed by the device itself
(`#CAP,2163003,DIAG,boot_reset,crash=0` + the full `BOOT,scan/t0..t7/load` sequence). It still ends
`INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)` with **`second overdub transition not confirmed`**, but it gets
further than any leg in section 15: `ST,Track,STOPPED,ARMED` -> `ARMED,RECORDING` (a clean `Recording started @
tick 0`), a 3-bar take (`Recording stopped @ tick 379 (recStart=0 length=2304)`, report `2304/1536`, raw 2683),
one overdub admitted (`OVERDUBBING: 1`) with no note rows, and the record first note at **37 clocks**
(`offset 37`, `intent 0`, `intent_source=run_default`; warning `record_first_note_too_late`). Shutdown
`complete/2`, no latch.

**The tempo question is settled, and no extra pin was needed.** The device's own bar cadence in that leg shows
it was at **300 BPM during the record phase** (`BAR` deltas 0.80 s = 768 ticks at 300 BPM) after a slow first
bar (2.39 s = ~100 BPM) while the boot loaded -- i.e. the tempo my probe pinned is **persisted** and a soft
reboot restores it. There is no host-side tempo pin in a managed run (the only pitchbend sender in the tree is
`hitl_tempo_fader_probe.py`), so `--tempo-bpm 300` is the host's expectation, never the device's setting.

**A confound this run exposes, named rather than smoothed:** the board's image carries `DIAG,env_dist` and
`DIAG,late_route` (18 and 6 rows here) and `DIAG,envelope_instr` (20), i.e. it is built from the S7.5a
increment-3/4 firmware (`888d2188`, 2026-10-11) -- **newer than the image the recorded 300 BPM legs of sections
2/11/12 ran on** (2026-10-10, before those firmware commits). So this session's "300 BPM performance"
(the second-overdub refusal; a 33-37-clock record first note against the recorded device-clock leg's 14) is a
comparison across *different device images*, and neither this evidence nor section 15 attributes it to the
host-side intent change -- which those reports record as inert (`intent_source=run_default`, empty
`per_track_stats`). Any future before/after at 300 BPM must pin the image as well as the tempo.
**Left as is:** shutdown `complete/2` and no latch, and a read-only attach afterwards
(`hitl_boot_census.py --no-reset --settle-s 6 --heap-stats`, `captures/state-after-300bpm-06`, no reset, no SD
write) refines the state: the device's heap inspection is **refused with `track_work`**, i.e. the aborted
overdub's obligations are still held even though the transport is stopped and persistence drained (the last
display frame before the run's Stops reads `OVERDUBBING`). Nothing further was pressed into it: the next leg must
carry its own `--boot-reset`, exactly as the run's own flow expects.

## 17. Pre-stream probe: the readiness checks cost 0.5 ms, and the residual 10 clocks is the host's row observation

"Continue with the next bounded step" -- the named step was to anchor the base record stream's start on the clock
grid. Before changing anything, a **temporary env-guarded probe** (`HITL_PRESEAM_PROBE`, monotonic timestamps at
the arm-confirm, the clock readiness and the reader probe; withdrawn afterwards, tree clean) attributed the
pre-stream gap on one leg:

`base-300bpm-preseam-probe-t8s4-07` (boot reset, device clock, 300 BPM, tree `e36f25ec` + the probe):

```
[probe] arm-confirm -> clock-readiness   0.3 ms
[probe] clock-readiness -> reader-probe  0.2 ms
record first note: 10 clocks (offset 10, intent 0, intent_source=run_default), no warning
record loop length: 2304/1536 (raw 2479); abort `second overdub transition not confirmed`; shutdown complete/2
```

**Ruled out:** the 24-pulse preamble (`ensure_transport_clock`'s `min_clocks=24` is never reached in a managed run
-- `use_serial_transport_proxy` short-circuits it, which is why the checks cost 0.5 ms and not the 1 s the
preamble would need), and the readiness/probe code as the cost. **Ruled in:** the residual ~10 clocks is the
host's **observation of the `RECORDING` row** (the serial poll cadence inside the arm's confirmation) plus the
streamer's own entry (<= 1 clock), and the 33-37 clocks measured in sections 15-16 came largely from the *device
state* rather than this code -- the same path measured **10 clocks with no advisory at all** on a clean boot
(inside the 12-clock bound).

**Re-scoped next step.** The first-note pacing is therefore no longer the blocker for a passing 300 BPM leg: on a
clean boot it is in bound and silent. What still stops a leg is (a) the **3-bar stop edge** (`2304/1536`,
reproduced on the device clock in every leg here, against the recorded 2026-10-10 device-clock leg's exact
`1536/1536`) and (b) the **second-overdub confirmation refusal**. Both are device-state/image dependent (section
16), so the next step is either to remove the image confound first -- build `teensy41-capture-serial` at this tree
and upload it, which needs an explicit upload approval -- or to diagnose the stop-edge scheduling from the
captures (host-only): the in-stream stop press should land on the 2-bar boundary in device-clock mode, and the
recordings say it lands a bar later.
