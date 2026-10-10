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
