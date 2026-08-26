# Edit projection unit cost — note reconstruction per length-update row

**Status:** **Investigation authorized. Firmware fix NOT authorized.** The fix is a separate change
and must not be folded into Ring A / RC1
([`Multi-Stage-Bugfix-Workflow`](../../.cursor/rules/Multi-Stage-Bugfix-Workflow.mdc)).

**Kind:** investigation

**Parent:** [`playback_window_round_robin_admission_refinement.md`](playback_window_round_robin_admission_refinement.md)
§ *Consequence — RC4 is a separate root cause*

**Root cause id:** RC4 (RC1 = admission multiplicity, RC3 = snapshot convergence, RC4 = unit cost)

**Evidence:** [`session_20260826_153941`](../../captures/session_20260826_153941.log) on
`teensy41-capture-serial-investigation-diag`

**Bound authority:** [`ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md) rows 2 and 7

---

## The problem in one line

A single `Track::ensurePlaybackMergedMidiEventsBuilt` costs **367 ms**, of which **339 ms** is note
reconstruction re-run **once per length-update row** while replaying 217 EditPass rows on the
compatibility gather path.

Ring A caps how many such rebuilds enter one clock dispatch. It does nothing about the unit cost, and
367 ms is **~35 clock dispatch periods at 240 BPM**:

| BPM | Clock dispatch period | 367 ms in dispatch periods |
|---|---|---|
| 120 | 20.8 ms | ~18 |
| 170 | 14.7 ms | ~25 |
| 240 | 10.4 ms | ~35 |

Against the ~60%-of-interval maintenance objective this is not a near miss; it is three orders of
magnitude out. Ring A makes the situation less catastrophic, not acceptable.

---

## Proven chain

Measured spans from one `#CAP` group at 113.539 s:

```
DIAG,cer_win,us=23391,...,evcopy=1300,evout=237
DIAG,cer_edit,us=343020,del=1235,ulen=341642,recon=339073,ndel=40,nlen=18,lgth=8,shrt=10,noop=0,ev=164
DIAG,cer_copy,us=343328,copyin=143,core=343062,copyout=123,ev=164
DIAG,cer_range,us=367412,collect=56,append=23511,sort=220,edit=343625,lists=18,rows=217,evout=164
DIAG,cer_rows,active=217,disabled=0,comp=217,ne=0,del=185,crt=0,ulen=32,upit=0,uvel=0,urng=0
```

| Span | µs | Note |
|---|---|---|
| `cer_range` | 367412 | `Loop::copyEffectiveCommittedEventsInRange` |
| `append` | 23511 | windowed `CommittedEventRange` traversal — **not** the problem |
| `collect` + `sort` | 276 | negligible |
| `edit` | 343625 | `applyNoteEditPassSequence` |
| `ulen` | 341642 | length-update rows only |
| **`recon`** | **339073** | `g_reconstructUs` — 99.3% of `ulen` |
| `copyin` / `copyout` | 143 / 123 | the `SessionMidiEventVec` ↔ `MidiEventVec` conversion is **not** the cost |

## Named owner

`applyChangeLengthById` in [`EditApply.cpp`](../../src/EditManager/EditApply.cpp) reconstructs **only
on the lengthen branch**:

```cpp
if (newEnd == refEnd) { ++g_noOpPathCount; return; }          // no reconstruct
if (newEnd < refEnd)  { ++g_shortenPathCount;                  // no reconstruct
                        shortenNoteEndById(...); return; }
++g_lengthenPathCount;                                          // reconstruct below
const auto allNotes = reconstructNotesForPitch(events, onEvt.data.noteData.note, loopLength);
```

`reconstructNotesForPitch` does three things per call, for one pitch:

1. allocates `MidiEventVec pitchEvents` and `reserve(events.size())` — a fresh allocation per row;
2. **linear scan of every event** in the working set to filter by pitch;
3. `NoteUtils::reconstructNotes` → `reconstructNotesImpl` → `buildCanonicalSpansFromMidi` +
   `displayNotesFromCanonicalSpanVec`, i.e. the **full canonical-span pipeline** with its own span
   vector and active-note-stack allocations.

The measured split confirms the branch attribution: `shrt=10` shorten rows contributed **nothing** to
`recon`, while `lgth=8` lengthen rows carry all 339 ms — **~42 ms per lengthen call**.

## Scope: compatibility gather only

`cer_range` first appears at **52.014 s**, before this session's long record began at 61.168 s, and
every publish in the capture (`RECS,stage,publish`, `ODUB,stop,seal`) belongs to the single
transitioning track. So the loop carrying `lists=18` and `rows=217` is a **loaded slot with no
published root**, reaching `copyEffectiveCommittedEventsInRange` through
`Loop::gatherCommittedEventsInWindow` rather than
`tryGatherPublishedSnapshotEventsInWindow`.

A published `LoopSnapshot` carries edits **already applied**. If loaded slots published on restore,
this entire replay — and therefore this entire cost — would not exist for them.

## Unit cost is environment-dependent, not input-dependent (proven)

[`session_20260826_160244`](../../captures/session_20260826_160244.log) settles this. Its 16
`cer_edit` samples fall into **six recurring input signatures that repeat in the same order in three
rounds**, roughly one sample every 2 s (one loop turn). Five signatures appear in all three rounds
with **byte-identical** `ndel / nlen / lgth / shrt / ev`:

| `ndel,nlen,lgth,shrt,ev` | Round 1 `recon` (12–53 s) | Round 2 `recon` (54–98 s) | Round 3 `recon` (98–109 s) | R2 / R1 |
|---|---|---|---|---|
| `66,4,2,2,154` | 8 531 | **78 605** | 9 597 | **9.2x** |
| `48,9,2,7,168` | 8 141 | **73 822** | 10 618 | **9.1x** |
| `40,16,7,9,181` | 28 663 | **278 869** | 33 501 | **9.7x** |
| `40,18,8,10,164` | 38 237 | **334 201** | 41 119 | **8.7x** |
| `35,12,4,8,124` | 22 031 | **161 886** | 19 809 | **7.3x** |

**Identical input, 7.3x–9.7x cost.** The inflation is uniform across every signature and confined to
one time window, so unit cost is **not** a function of row count, lengthen count, or event count. The
5x gap seen in `153941` was the same effect sampled twice, not input variance.

Consequently:

- **367 ms is not representative.** It is the *contended* cost of one destination whose uncontended
  cost is 30–100 ms. `cer_range` totals split cleanly into a normal band of **29.5–98.9 ms** and a
  contended band of **306–363 ms**.
- **Any G1 lead sized on 367 ms would be sized on a contention artifact.** The correct input is the
  uncontended representative cost *plus* an explicit decision about whether the contended state is
  permitted to persist at all.

### What round 2 coincides with

Round 2 falls inside the first long overdub (`PLAYING -> OVERDUBBING` at 54.181 s,
`OVERDUBBING -> PLAYING` at 97.863 s). Measured load per 9 s window:

| Window | Transport | `snapshot_idle` share | `cer_range` sum |
|---|---|---|---|
| 20–29 s | RECORDING | 28.9% | 202 ms (normal) |
| **60–69 s** | **long OVERDUBBING** | **48.4%** | **1062 ms (inflated)** |
| 100–109 s | PLAYING + short OVERDUBBING | **0%** | 226 ms (normal) |

The heap also fell from `min_ever=340 KB` at 5 s to `min_ever=120 KB` by 135 s.

## Slice 1 recapture — destination attribution

[`session_20260826_161834`](../../captures/session_20260826_161834.log) carries the extended
`playback_build` line:

```
DIAG,playback_build,<us>,<winStart>,<winLen>,<revision>,<track>,<slot>,<count>
```

The 94 emitted maximum samples identify four destinations:

| Destination | Lines | Rebuild-call count | Maximum |
|---|---:|---:|---:|
| `T0/S6` | 57 | 127 | 166.325 ms |
| `T4/S0` | 34 | 37 | 25.439 ms |
| `T6/S0` | 2 | 2 | 10.965 ms |
| `T2/S2` | 1 | 7 | 9.406 ms |

`T0/S6` is the dominant long-loop destination: it supplies the repeated 1536-tick window
rebuilds, while `T4/S0` supplies the other recurring stream. The max sample's track/slot
attribution is therefore working.

**Important telemetry boundary:** `<count>` is the number of rebuild calls accumulated until the
next `emitPendingDeadlineOneShots` call. It is **not** the number of distinct destinations and is
not a per-bar-edge eligible-set count. For example, repeated `T0/S6 ... count=2` means that the
same destination was recorded twice before emission; it does not prove two eligible destinations.
The capture closes destination attribution, but it does not measure Ring A's eligible-set size.

## `162522` direct pairing — RC4 belongs to `T0/S6`

[`session_20260826_162522`](../../captures/session_20260826_162522.log) includes both
`cer_range` and the extended `playback_build` line. Of 58 `cer_range` samples, 48 have the
`lists=18, rows=217` compatibility-gather shape. Every repeated `cer_edit` sample with that shape
is followed by a `playback_build` for **`T0/S6`**:

| Destination | Playback-build lines | Rebuild-call count | Median | Maximum |
|---|---:|---:|---:|---:|
| `T0/S6` | 49 | 112 | 24.625 ms | **360.754 ms** |
| `T4/S0` | 19 | 20 | 8.375 ms | 12.696 ms |
| `T6/S0` | 1 | 1 | 8.792 ms | 8.792 ms |
| `T2/S2` | 1 | 7 | 9.462 ms | 9.462 ms |

The 15 repeated `cer_edit` samples are therefore successive compatibility-gather windows of the
single `T0/S6` destination. The small `cer_range` samples (`lists=1/2, rows=0`) are separate
empty/edit-free gathers and are not the RC4 cost.

The inflated pair is direct:

```
cer_range      360668 us, edit=337731 us, rows=217, evout=164
playback_build 360754 us, winStart=4608, winLen=1536, revision=1, track=0, slot=6
```

## What `162522` rules out, and what it does not

During the inflated interval, `snapshot_idle` lines appear **after** the compatibility gather and
its `playback_build` line. The capture is on external MIDI clock (`External MIDI clock detected`);
`ClockManager::onMidiClockPulse` calls `TrackManager::updateAllTracks`, while snapshot idle is a
main-loop owner. Therefore snapshot construction is not executing concurrently inside the measured
`g_reconstructUs` span. It can still change allocator state before a later gather, and higher-priority
interrupt work remains a separate unmeasured source of wall-time inflation, but the earlier explanation
"snapshot construction and reconstruction interleave" is not established and must not be used.

The input remains stable: the inflated and normal `T0/S6` windows retain the same 217 replay rows and
the same five repeated `cer_edit` signatures. The first run has no allocator-route telemetry, so
`InternalHeapFirstAllocator` spill to PSRAM remains **unproven**.

## Mechanism telemetry (`165024` captured; measured candidates closed)

Investigation-diag now emits a second line next to each reported `cer_edit`:

```
DIAG,cer_heap,recon=<us>,ihit=<n>,spill=<n>,sbytes=<n>,clk=<n>,iclk=<n>,ext=<0|1>
```

| Field | Meaning |
|---|---|
| `recon` | Same `g_reconstructUs` as `cer_edit` — pairs the two lines |
| `ihit` | `reconstructNotesForPitch` calls whose `pitchEvents` buffer stayed on the internal heap |
| `spill` / `sbytes` | Those calls whose `pitchEvents.reserve` landed in the external pool, and reserved bytes |
| `clk` | `ClockManager::onMidiClockPulse` entries during the span |
| `iclk` | `ClockManager::updateInternalClock` entries that pass the `CLOCK_EXTERNAL` guard |
| `ext` | `1` when the working `MidiEventVec` already lives in the external pool at sequence start |

`DisplayNoteVec` and the canonical-span temporaries use `ExternalMemoryFirstAllocator` on every
call, so those being in PSRAM does not discriminate. `cer_heap` does not hook
`InternalHeapFirstAllocator::allocate` (that template lives in a header and blew ITCM). It
inspects `pitchEvents.data()` after `reserve` and `events.data()` at sequence start.

`165024` contains 16 paired `cer_edit` / `cer_heap` rows. Every row reports `spill=0`,
`sbytes=0`, and `ext=0`; `ihit` equals the number of executed lengthen rows (`lgth`). Thus the
measured `InternalHeapFirstAllocator` buffers in this span stay in internal RAM, including the
working `MidiEventVec` and every `pitchEvents` reserve. Every row also reports `clk=0` and
`iclk=0`, so no nested MIDI-clock or internal-clock dispatch occurred during reconstruction.
The repeated signatures still reproduce the normal, intermediate, and inflated bands:
`T0/S6` `nlen=18` is `30.095 ms`, `339.941 ms`, and `187.764 ms` across the three rounds.

How to interpret the capture:

- `165024` has `spill=0` and `ext=0` on both fast and slow bands, which **rules out** an
  `InternalHeapFirstAllocator` route change for the measured working and pitch-event buffers.
- `165024` has `clk=0` and `iclk=0` on every row, which **rules out** nested clock dispatch as
  the source of the inflated reconstruction spans.
- These fields do not measure the time spent inside `ExternalMemoryFirstAllocator` calls or
  preemption by other higher-priority interrupts. Those remain separate, unmeasured sources of
  wall-time variation.

Investigation-diag now also emits:

```
DIAG,cer_recon,recon=<us>,filt=<us>,span=<us>,proj=<us>,itick=<n>,itickus=<us>
```

| Field | Meaning |
|---|---|
| `filt` | Internal-heap pitch filter in `reconstructNotesForPitch` (`reserve` + scan) |
| `span` | `buildCanonicalSpansFromMidi` — PSRAM-first stacks and event walk |
| `proj` | `displayNotesFromCanonicalSpanVec` — PSRAM-first project + dedupe |
| `itick` / `itickus` | `ClockManager::updateInternalClock` IntervalTimer entries and wall time during reconstruct, including `CLOCK_EXTERNAL` early returns |

How to read `cer_recon`:

- If `filt` inflates with `recon` while walking internal-heap events, the remaining cause is
  preemption (IntervalTimer or another ISR). `itickus` is the measured IntervalTimer share.
- If `filt` stays in the fast band while `span`/`proj` inflate, the remaining cause is the
  PSRAM-first reconstruct pipeline.
- `itickus` does not include the Teensy USB hardware ISR. That core path is not hooked.

## Open questions

1. ~~**What mechanism makes identical `T0/S6` work 7–10x slower in round 2?**~~
   **Answered by `171159`:** the inflation is in the PSRAM-first reconstruct pipeline
   (`buildCanonicalSpansFromMidi` + `displayNotesFromCanonicalSpanVec`), not in the
   internal-heap pitch filter and not in IntervalTimer ISR wall time.
   [`171159`](../../captures/session_20260826_171159.log) paired RECORDING vs OVERDUBBING
   windows of the same `cer_rows` signature (`active=217`, `comp=217`). The `nlen=18`
   `lgth=8` row is `recon=37391` / `filt=36` / `span=18361` / `proj=18921` at 28.183 s,
   then `recon=295800` / `filt=36` / `span=162790` / `proj=132843` at 64.458 s.
   `filt` is unchanged. `span` and `proj` take the 7.9x. Every `cer_recon` row has
   `itickus=0` while `itick` scales with reconstruct wall time (`itick=14` vs `115`),
   so `ClockManager::updateInternalClock` early-returns are counted and each is below
   `micros()` resolution. [`170149`](../../captures/session_20260826_170149.log) rebooted
   at first clock after Record on function-local statics; that probe is now file-scope
   volatiles. The Teensy USB hardware ISR remains unhooked.
2. ~~**Are the six signatures six destinations, or successive windows of one destination?**~~
   **Answered by `162522`: successive windows of `T0/S6`.**
3. ~~**Does the immutable snapshot direction remove this path entirely?**~~ **Answered from code:
   not for loaded slots today.** `Loop::adoptPersistedSnapshot` and `Loop::restorePassesSnapshot`
   restore pass storage and call `notifyCommittedContentChanged()` → `markPassDerivedStale()`.
   They do not call `queueSessionExitSnapshot`, `storePublishedSnapshot`, or
   `stepPendingSnapshotPublish`. `Loop::tryGatherPublishedSnapshotEventsInWindow` returns false
   when `publishedSnapshot()` is null, so playback falls through to
   `gatherCommittedEventsInWindow` → `copyEffectiveCommittedEventsInRange` →
   `applyNoteEditPassSequence`. Snapshot publish is queued at session exit
   (`queueSessionExitSnapshot`) and stepped from idle (`DEC-045`). A published root already
   removes this replay for that slot. Restore does not create that root, so the observed `T0/S6`
   compatibility gather remains. Adding publish-on-restore (or an idle bootstrap that constructs
   a root from already-committed loaded passes) would remove this path; that is a separate
   change, not an `applyNoteEditPassSequence` optimization.
4. ~~**Is 367 ms representative or pathological?**~~ **Answered by `160244`: pathological.** It is the
   contended cost of a destination whose uncontended cost is 30–100 ms.
5. **Why are there lengthen rows among replayed overdub companions at all?** All 217 Active rows are
   companions (`comp=217`). A Shorten companion replayed against events that already reflect it
   should take the `noop` or `shorten` branch, yet 8 rows per gather take **lengthen**. Whether that
   is correct replay semantics or a replay-idempotency defect is unresolved, and it matters: if those
   8 rows should not be lengthening, RC4's fix may be a correctness fix rather than a performance
   one.


## Sequencing (agreed)

1. ~~**Slice 1 measurement closure**~~ **Closed by `161834` and `162522`:** destination attribution
   maps the compatibility-gather signatures to `T0/S6`.
2. **This investigation** — question 3 is answered from code (loaded restore does not publish).
   Question 1 is closed by `165024` + `171159`: spill, nested clock, IntervalTimer ISR
   wall time, and internal-heap `filt` are not the inflation. The remaining measured
   cost is PSRAM-first `span`/`proj`. Question 5 remains open.
3. **Measure RC4's candidate resolution** before choosing Ring A's final prefetch lead.
4. **Size G1** in the parent plan from the resulting representative rebuild cost — **not** from
   today's 367 ms.
5. **Ring B / RC2** proceed independently; they do not depend on RC4.
6. **Ring A** after G1 is actually sized.

**Do not** lock the prefetch lead around 367 ms. If RC4 resolution brings a rebuild to tens of
milliseconds, the required lead changes by an order of magnitude and the envelope table in the parent
plan becomes comfortable at every eligible count.

## Not in scope

Micro-optimization of `applyNoteEditPassSequence` before question 1 is closed and a candidate
resolution is chosen; changes to `CommittedEventRange` windowed traversal (measured at 23 ms, not
the problem); the 2-bar content window; Ring A or Ring B admission; RC3 snapshot convergence.
