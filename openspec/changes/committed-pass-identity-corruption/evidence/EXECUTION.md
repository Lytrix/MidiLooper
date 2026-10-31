# Execution — committed pass identity/row corruption

## 2026-10-31 — task 1.1: the Record pass is born corrupt at seal

**Fixture:** `test_overdub_chain_preserves_frame_progress_long_loop` (24 bars) now snapshots the **entire semantic
Record-pass row vector** immediately after Record seal and compares it with the expected pairs. It also probes
the live capture store before and after geometry growth and the first readable committed pass during the
deferred stop drain.

**Result: the sealed Record pass is already 47 rows before any overdub commit.** The failure is therefore not in
an overdub transfer, normalization, prefix resume or companion publication:

```text
first readable Record pass turn=249 length=17664 rows=47
  extra   @t12   N id=24 p=48 v=100
  missing @t17676 N id=24 p=48 v=100   (bar 23 pair)
  missing @t17712 F id=24 p=48 v=0
```

The live capture store holds the correct pair at both probe points:

- `pre-growth` store: id 24 On@17676 Off@17712
- `post-growth` store: id 24 On@17676 Off@17712

**Named boundary — Record stop length derivation + stop projection:**

1. `RecordStopLength::computeRecordStopLengthTicks(18432, 17712)`
   - `quantizeTransportRecordLength(18432)` = 18432 (24 bars)
   - `computeLoopLengthTicks(17712)`: fullBars = 23, rem = 48 ≤ grace (768/6 = 128) → **17664 (23 bars)**
   - `min(18432, 17664)` = **17664**
2. `Track::stopRecording` writes that length and calls `Loop::projectCaptureAtRecordStop(17664)`.
3. `LoopEventStore::projectRecordStopTick` wraps **NoteOns only** at/after the length and drops everything else:
   - On@17676 → `17676 % 17664` = **12** (kept)
   - Off@17712 → not a NoteOn, `17712 >= 17664` → **dropped**
4. The committed pass is sealed with an orphan On at the loop start and the bar-23 pair gone.

`applySealedRecordRowsUnchanged` then never sees a change: the corruption exists before the seal, so the
immutability oracle would pass on a consistently corrupt vector. This is exactly why task 1.1 snapshots the
**expected** rows, not only the post-seal baseline.

## Attribution

- Not an alias/write-target or derived-representation mutation: the corruption is produced by the **Record stop
  writer** before the pass is sealed.
- Not caused by the display-source fix, DEC-233, or an overdub commit. The 20-bar device chain-48 dump was clean
  because its last Record event fell outside the trailing-bar grace window.
- The unit of the defect: a stop whose transport covers bar N and whose last event is strictly inside bar N
  (`0 < rem <= grace`) trims to N bars; the projection then wraps an On without its Off.

## Fix — task 2.1: keep the bar that contains the last event

`RecordStopLength::computeLoopLengthTicks` now closes the bar only when the last event is **exactly on the bar
line** (`rem == 0`). A row strictly inside the bar (`0 < rem <= grace`) is content in that bar and keeps it:

```text
-before: fullBars            (24-bar transport + last event at bar 23 + 48 -> 23 bars)
+after:  fullBars + 1        (                                          -> 24 bars)
```

The growing-record path is unchanged: `computeRecordStopLengthTicks` still takes
`min(quantizeTransportRecordLength(rawLength), contentLength)`, and during recording `rawLength` is the
in-bar playhead tick, so the transport term remains the limiting one. The fix only changes the outcome when the
transport already covers the bar — exactly the stop-at-the-bar-line case.

RED → GREEN:

- `test_record_stop_keeps_the_bar_containing_the_last_event`: expected 18432, was 17664; passes after the fix.
- `test_record_stop_trims_at_an_exact_bar_line`: 23 bars stays 23 bars (control).
- `test_record_stop_length`: 9/9.
- `test_live_capture_display_boundary`: 49/49; the 24-bar chain seals **48** rows, the sealed Record vector is
  unchanged after all 22 overdubs, and the long-chain display oracle passes.

Residual, not exercised by this fixture: the projection can still wrap an overflow NoteOn whose NoteOff is also
at/after the quantized length (both drop). Real input closes pending notes at the stop tick; if a reachable
production shape exists, it belongs to the same stop writer and must be covered by a control before this change
closes (task 1.3/3.3).

## Second control — the 32-bar private predecessor (same defect, audible pair)

`test_immediate_capture_handoff` records 32 bars (note `12..96` in every bar) and stops at `32 * 768 + 200`.
The old rule trimmed that to **31 bars** (last event at bar 31 + 96, inside the grace), projected the 32nd
bar's On to tick 12 and dropped its Off. The private-source differential then surfaced a phantom pair:

```text
old rule: C decisions = 2  [780..811 kind=Shorten] [12..811 kind=Shorten]   <- the 12..811 pair is the orphan
new rule: C decisions = 1  [780..811 kind=Shorten]                          <- the only real pair under the hold
```

The old test expectation (`2 decisions`) was encoding the phantom pair. It is amended to exactly one decision
with explicit `780..811` geometry; the phantom's start tick (12) is now absent. This strengthens the oracle
(it pins the clean pair) rather than weakening it.

## Gates (2026-10-31)

- `pio test -e native` (full): **3,737 cases, 7 skipped, 3,730 succeeded, 0 failed** (`/tmp/native_identity_fix3.log`).
- `test_record_stop_length`: 9/9; `test_loop_event_store`: 53/53, including the boundary oracle
  `test_record_stop_projection_preserves_rows_when_length_covers_max_tick` (a covering length leaves every
  row tuple unchanged); `test_live_capture_display_boundary`: 49/49; `test_immediate_capture_handoff`: 21/21
  (with the amended private-source expectation).
- Target build: **blocked by a pre-existing RAM1 budget shift, not by this fix.** Clean builds with the old
  and new length rule both report `variables 100,320 / code 391,336 + padding 1,880 / locals 30,752`; the
  approved default-env baseline is `96,192 / 391,304 + 1,912 / 34,880`. A staged-only build (HEAD + the parked
  display hunks) reports `96,224 / 391,976 + 1,240 / 34,848`, so the 4,096 B
  DTCM transfer matches the documented DEC-232 display-investigation align transfer, not this change (exact
  symbol attribution was not completed). The budget file already documents the
  same transfer in its `teensy41-capture-serial-investigation-diag` entry (`ram1_variables 100,480`). No
  upload or device run was performed: the target gate is red and the fix was not flashed.
- Device qualification: **case 1 (inside-final-bar) PASS** on an evaluation image built by temporarily
  bypassing the red RAM1 post-script (image `634e0be9…`, boot `crash=0` / `BOOT,load,ok`); the Record stop
  chose 3072 (4 bars) from raw 2928 and the committed Record pass is 8 rows / 4 exact pairs with the last pair
  at `2336..2368` (rem 64 < grace 128). Full record: `evidence/DEVICE-CASE1.md`.
- Exact-boundary control: PASS (`raw=3696 final=3072`, no extra bar; boundary note closed by the documented
  `NoteId=0` Off at 3071).
- Representative 8-bar / 12-window chain: PASS (`final=6144`; Record 16 rows / 8 exact pairs; effective 146
  rows).
- Playback parity: PASS after the test fix — `--solo-track 8` isolates the target, and the verifier normalizes
  the served channel (track 8 outputs channel 8, the take is on channel 1). `content-review.json` is `PASS`,
  two cycles with 10/10 rows, zero missing/extra, `exact_order=true`, `storage_matches_effective=true`.

## Device qualification plan (not run; target gate blocked)

Order is deliberately separate from DEC-234's logic: resolve the DEC-232/display-investigation RAM1 placement
first (no budget re-base in this change), then qualify on hardware.

1. **Inside-final-bar case** (mirrors the RED): record a pattern whose last NoteOff is 1..grace-1 ticks inside
   the final bar, stop exactly at the bar line. Assert the committed loop includes that bar; dump the committed
   Record pass; every On/Off tuple is at its original ticks; no wrapped `@12` phantom.
2. **Exact-boundary control**: record a pattern whose last event is exactly on the bar line. Assert the loop
   does **not** gain an unnecessary extra bar (content length = the closed bar).
3. **Representative overdub chain**: after 1-2 pass, rerun one ordinary chain and check Record rows expected ==
   committed, phantom wrapped rows == 0, playback content and the committed display gather both clean.

The 20-bar `chords.mid`/`16ths.mid` chain-48 patterns cannot exercise case 1 by themselves: their last Record
event falls
outside the trailing-bar grace. A single-short-note-per-bar temporary pattern (temporary variant, not a
canonical stimulus edit) is the minimal case-1 input.

## Next controls (1.3, conditional)

- Stop at the bar line with the last event strictly inside the trailing bar (this fix) — covered natively.
- Stop **before** the bar line with an On/Off pair entirely beyond the quantized length — projection pairing
  control, open.
- Device variant: a Record whose last note starts at the top of the final bar and stops at the bar line. The
  20-bar chain-48 pattern's last event falls outside the grace, so it does not exercise this boundary by itself.
