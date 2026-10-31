## Context

**Narrow question this change exists to answer:** at which ownership transition does Record-pass NoteId 24
change from its correct bar-23 On/Off pair into `On@12 pitch 48` with no matching Off?

**VERIFIED evidence (native, `test_live_capture_display_boundary`):** the 24-bar / 22-overdub chain's **first
readable committed Record pass** already contains `12/N|1, 12/N|24, …` (47 rows) with length 17664. Id 24 has
only `12/N`; the bar-23 pair is gone. The live capture store held the correct pair (`On@17676 Off@17712`)
immediately before `stopRecording`, so the sealed pass is **born corrupt at the Record stop**, before any
overdub commit. The same probe fails with the previous display gather, so the pass content is bad independently
of display-source selection.

**Root cause (task 1.2):** `computeRecordStopLengthTicks(18432, 17712)` trimmed the take to 23 bars because
`computeLoopLengthTicks` treated a last event 48 ticks inside bar 23 as a bar-line row (1/6-bar grace).
`Loop::projectCaptureAtRecordStop` then wrapped the NoteOn (`17676 % 17664 = 12`) and dropped the NoteOff (not
a NoteOn, `tick >= length`). The same shape produced the phantom `[12..811]` private-source pair in the 32-bar
handoff fixture.

**VERIFIED:** the display resolver is not the defect; it is correctly exposing bad stored content. Its NoteId
pairing collapses two same-pitch/start Ons, which is why the symptom reads as disappearing notes.

## Goals / Non-Goals

**Goals:** a per-overdub full-record snapshot/equality fixture; the first breaking transition named; only the
controls needed to distinguish that transition's preconditions; a fix at the named owner; the display oracle
kept strict.

**Non-Goals:** display-source repair (parked separately), DEC-195 root republishing, SD/undo schema changes,
generic pass-inventory refactors, unbounded loops or new queues.

## Decisions

### 1. Fixture-first: the whole sealed Record row vector, per overdub

Snapshot the complete semantic Record-pass row vector immediately after Record seal:

```text
event type, tick, NoteId, channel, pitch, velocity
```

After **each** overdub commit, re-read the sealed Record pass through the same path and require exact equality
(rows sorted by the tuple to remove storage/layer order). NoteId 24 is a witness: it must appear in the delta,
but it is not the oracle, and the fixture must fail if corruption first hits another row or merely moves. The
first unequal commit is the transition under test; record its job/phase/interval and the exact delta (missing
expected rows / unexpected current rows). Do not start from the resolver.

### 2. Trace the boundary, then run only the controls that boundary needs

Trace the sealed rows through: initial Record pass creation → Record seal/publication → each overdub commit
that can touch/copy/rewrite the Record inventory → canonical/prefix normalization → any DEC-177 append/resume →
final `loop.passes`. Once the first corrupting transition is named, A/B only the variables that can distinguish
its preconditions (for example certified-prefix resume on/off if the break is in the resume path; wrap on/off
if it is in a window transfer). Expand to loop length 16/20/24/32, overdub count or NoteId allocation only when
the named owner makes them relevant. The existing `test_overdub_chain_*` fixtures and the chain-48 dump are the
controls.

### 3. Fix the writer, never the reader

Only the named writer is changed: **once a capture pass is sealed, its rows are immutable forever.** Later
work may change the pass state (undo/redo/disable) or build a derived representation, never add/remove/move or
rewrite a sealed row. If the cause is a shared/aliased chunk or a rewrite target, fix ownership at that
boundary; do not special-case NoteId 24 or the display. Keep source integrity separate from derived correctness:
a root/companion/playback projection must preserve the sealed identity and pairing, but is not allowed to
become the authority that reshapes the source.

## Risks / Trade-offs

- A chunk alias can also corrupt playback/persistence, not just display → check the persisted body after the
  fix, not only the paint.
- The 24-bar native shape may differ from the device's 20-bar chain → validate the matrix on device before
  claiming the symptom fixed.
- Do not weaken `test_overdub_chain_preserves_frame_progress_long_loop`; it is the gate that exposed this.
