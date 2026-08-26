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

## Open questions

1. **Why does the same work cost 5x more in one call than another?** Two samples with **identical**
   `nlen=18, lgth=8, shrt=10, noop=0, ev=164` measured `recon=339073` and `recon=68989`. Row counts
   and final event count do not explain it. Candidates to **test, not assume**: working-set size at
   edit time (`cer_win` reports `evcopy=1300` against `evout=237`, so the set the edit walks is much
   larger than the 164 it emits), heap state and allocation cost across the per-row allocations, and
   canonical-span pipeline cost as a function of same-pitch span count. This must be measured before
   any optimization.
2. **Does the immutable snapshot direction remove this path entirely?** If loaded slots publish a
   `LoopSnapshot` on restore
   ([DEC-044](../DECISION_LOG.md#dec-044--immutable-loop-snapshot-model-replaces-maintained-lcr-direction),
   [DEC-045](../DECISION_LOG.md#dec-045--overdub-snapshot-publish-is-idle-stepped)), the compatibility
   gather does not run for them and RC4 disappears for the observed case. **Answer this before
   optimizing `applyNoteEditPassSequence`** — micro-optimizing a path the architecture is already
   removing would be the wrong investment.
3. **Is 367 ms representative or pathological?** Parent-plan Slice 1 (per-rebuild maximum plus slot)
   is the instrument. The `rows=217` population is constant across all 50 non-empty samples in this
   capture, so it is at least stable for this configuration — but one capture with one loaded-slot
   population is not a distribution.
4. **Why are there lengthen rows among replayed overdub companions at all?** All 217 Active rows are
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
