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

## Open questions

1. **What mechanism makes identical work 7–10x slower in round 2?** The correlation with
   `snapshot_idle` at 48.4% is established; the mechanism is **not**. Two candidates, both requiring
   a discriminating measurement rather than assumption:
   - **Wall-clock inflation.** `g_reconstructUs` measures `micros()` deltas, so it includes every
     interrupt and any preemption. If snapshot construction and reconstruction interleave, elapsed
     time inflates while CPU work is unchanged. Note 48.4% competing load alone yields roughly 2x,
     **not** 9x, so this cannot be the whole explanation.
   - **Allocator spill to PSRAM.** `InternalHeapFirstAllocator::allocate` tries `malloc` and, on
     failure, falls back to `extmem_malloc`. `reconstructNotesForPitch` allocates per row, so under
     heap pressure its vectors could land in external memory, which is far slower than internal RAM.
     `min_ever=120 KB` shows real pressure but **not** exhaustion, so a spill is unproven — it would
     require a specific large allocation to fail under fragmentation.

   These are cheap to separate: count spilled allocations, or report CPU-time separately from wall
   time, for the reconstruction span only. **Do this before any optimization**, because the two
   candidates lead to completely different fixes.
2. **Are the six signatures six destinations, or successive windows of one destination?** The
   `161834` recapture identifies four playback destinations, but it does not carry `cer_range` /
   `cer_edit` lines, so it cannot map each of the six compatibility-gather signatures directly to a
   destination. The dominant long-loop stream is `T0/S6`; retain this question for the RC4
   investigation if direct mapping is required.
3. **Does the immutable snapshot direction remove this path entirely?** If loaded slots publish a
   `LoopSnapshot` on restore
   ([DEC-044](../DECISION_LOG.md#dec-044--immutable-loop-snapshot-model-replaces-maintained-lcr-direction),
   [DEC-045](../DECISION_LOG.md#dec-045--overdub-snapshot-publish-is-idle-stepped)), the compatibility
   gather does not run for them and RC4 disappears for the observed case. **Answer this before
   optimizing `applyNoteEditPassSequence`** — micro-optimizing a path the architecture is already
   removing would be the wrong investment.
4. ~~**Is 367 ms representative or pathological?**~~ **Answered by `160244`: pathological.** It is the
   contended cost of a destination whose uncontended cost is 30–100 ms.
5. **Why are there lengthen rows among replayed overdub companions at all?** All 217 Active rows are
   companions (`comp=217`). A Shorten companion replayed against events that already reflect it
   should take the `noop` or `shorten` branch, yet 8 rows per gather take **lengthen**. Whether that
   is correct replay semantics or a replay-idempotency defect is unresolved, and it matters: if those
   8 rows should not be lengthening, RC4's fix may be a correctness fix rather than a performance
   one.


## Sequencing (agreed)

1. **Slice 1 measurement closure** in the parent plan — separate the two per-edge rebuilds, record
   slot, establish whether 367 ms is representative.
2. **This investigation** — answer open questions 1, 2 and 4. Read-only plus targeted instrumentation.
3. **Measure RC4's candidate resolution** before choosing Ring A's final prefetch lead.
4. **Size G1** in the parent plan from the resulting representative rebuild cost — **not** from
   today's 367 ms.
5. **Ring B / RC2** proceed independently; they do not depend on RC4.
6. **Ring A** after G1 is actually sized.

**Do not** lock the prefetch lead around 367 ms. If RC4 resolution brings a rebuild to tens of
milliseconds, the required lead changes by an order of magnitude and the envelope table in the parent
plan becomes comfortable at every eligible count.

## Not in scope

Micro-optimization of `applyNoteEditPassSequence` before question 2 is answered; changes to
`CommittedEventRange` windowed traversal (measured at 23 ms, not the problem); the 2-bar content
window; Ring A or Ring B admission; RC3 snapshot convergence.
