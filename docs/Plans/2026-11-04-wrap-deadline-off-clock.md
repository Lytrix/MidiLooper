# Overdub wrap: the C3.4 deadline leaves the Clock (design packet, revision 2, 2026-11-04)

Not authority. Owner ruling 2026-11-04 ("C3.4 design-packet ruling — approved with four amendments"). Amends
`openspec/changes/immediate-record-overdub-handoff/CONTRACTS.md` C3.4 and closes DEC-247's open item. This revision
answers the ruling's three pre-implementation items: the owner/state transition diagram, the source-cut lifetime proof,
and the capacity/backpressure accounting. It also returns four conflicts with the existing model (§5). No production
code has been written.

## 1. Measured load (what the design must carry)

`captures/session_20261104_140156.log`, STIM-LANE-CADENCE-TEMP-01 intense, track 8 slot 4 (33 bars, 300 BPM):

| wrap | lap | added notes | owed completions (set + overflow) | sealed Hide | Clock |
|---|---|---|---|---|---|
| 99,072 | 25.6 s | 512 | 24 (15 + 9) | 464 | 6.97 s |
| 123,648 | 25.6 s | 512 | **518 (16 + 502)** | 75 | **123.46 s** |

- Each forced answer was a `resolveDisplayNotesInWindow` over about the whole loop: the closure window is the longest
  committed note span, 23,552 of 24,576 ticks. Average 230 ms; the worst was 514 ms, with 2,464 candidates.
- There were 0 failed allocations in the 123 s wrap.

## 2. Owner/state transition diagram

```
MIDI path (unchanged)                    Clock: commitOverdubWrap (fixed cost)          Maintenance: discharge lane (bounded)
---------------------                    -------------------------------------          -------------------------------------
NoteOff -> Add row, completion owed      commit pass P (existing)                        FIFO over WrapObligations, oldest first
  set slot (16) or Add.consequenceOwed   T1 swap live pending set <-> prewarmed empty     per obligation O(P):
                                         T2 copy the 16-slot set into O(P)  (256 B)         S1 prepare O(P)'s cut cache:
                                         T3 stamp cut(P) (5 words) + record index iP           one union window, TilePrepCursor,
                                         T4 push record P (passId, empty companions)           kGrainEventsPerTurn per turn
                                         T5 enqueue O(P); live set count = 0                 S2 answer owed completions from the cut,
                                         begin capture (existing)                              k per turn, into O(P).pending
                                                                                            S3 seal O(P).pending (decided + answered)
                                                                                               as edit passes, <= 32 rows per turn,
                                                                                               appended to record P's companions,
                                                                                               state = record P's activity
                                                                                            S4 source-view / display / overlap-total
                                                                                               updates (moved off the wrap)
                                                                                            S5 retire O(P); publication may pass P

Obligation states:  Queued --(S1 starts)--> Preparing --(cut ready)--> Answering --(all owed answered)--> Sealing
                    --(all rows sealed)--> Retired
                    any --(record P dropped by a redo-tail drop or session clear)--> Cancelled (no rows sealed)
Record P activity (session undo/redo) never changes the obligation state; it only sets the state of the rows S3
writes (Active while P is active, Disabled while it is undone), and undo/redo flips them with the record.
Publication (DEC-248): record P is not produced while O(P) is not Retired; records after P wait behind it (causal).
Stop / session close: Queued..Sealing obligations stay owned by the Loop; discharge continues while stopped (§5 C4);
stop acceptance never waits for them. The session exit's publication waits for all of them.
```

What moves off the Clock, with today's wrap cost, measured in the `wrap_stall` phases:

| moved | today on the Clock | grows with |
|---|---|---|
| C3.4 deadline (set + overflow) | 123.35 s | owed × whole-loop resolve |
| decided-companion seal (`saveNoteEditPass` per row) | part of `WrapSeal` | decided rows |
| `applyPendingNoteChangesToOverdubSourceView` | 32 ms | pending rows |
| `recountOverlapHoldTotalsFromPending` | 0.6 ms | pending rows |
| `markAffectedDisplayCacheRanges` | in bookkeeping, 3.4 ms | companions |

**Per-wrap Clock-work bound for the transferred phases:**
- T1-T5 are O(1) in rows: one vector swap, a 256 B copy, a 20 B stamp and one amortised `push_back` onto prewarmed
  record capacity.
- Two existing wrap phases that are not C3.4 work and stay content-sized:
  - `commitCapturePass` (34-43 ms here; `verifyCaptureHotStop` copies the capture store);
  - held-NoteOn extraction (18-22 ms).

  They are named in the bound, not hidden. The RED pins the transferred phases at constant work (counted work units,
  not time), and reports the two remaining phases separately.

## 3. Source cut: definition and lifetime proof

**The cut is what today's retained path would read for interval P's completions.** That is:
- the committed content visible to the overdub source at P's wrap, plus
- P's own pending set (P's decided Shorten/Hide and the rows the completions append to),

excluding P's own committed pass. The current forced derive reads P's pass too, so it differs from the retained path.
R2's differential pins which of the two is the oracle (§6).

**The oracle is settled by the RED (2026-11-04).** The two answers differ, and the forced deadline's is the wrong one.
- The forced deadline derives each completion from committed `passes`, so it never sees the interval's own pending
  Shorten rows.
- Every owed completion over a long source note therefore re-shortens the full-length note, and the last one
  answered wins.
- The retained path shortens the source once, to the first overlapping overdub note. Example: source 37@0-6000 ends
  at 50, before the overdub note at 51.
- `test_continuous_capture` (`WRAP_DEADLINE_RED`, `wrap_deadline_reds.inc`): R2-R7 and G3 have equal event counts but
  different NoteOff ticks.
- This is a pre-existing C3.4 correctness defect: DEC-247's single-completion fixture could not see it. The retained
  path is the oracle.

`cut(P) = { pre-session rows in their state at P's wrap } ∪ { pass + companions of records 0..iP-1 } ∪ O(P).pending`,
with NoteId horizon `h(P) = nextNoteId_` at P's wrap as an additional filter.

Rows are selected **by record membership, not by an id watermark.** Late companions of an earlier record O < P (sealed
after P's wrap) are part of O, and therefore of cut(P). FIFO ordering (O retires before P starts S2) makes them
complete before any P answer. That is the "later intervals may edit older NoteIds" amendment, seen from the other
side: Q > P's companions are excluded because Q is not a record in 0..iP-1, whatever NoteId they edit.

**Lifetime: every writer that can change a row the cut reads, after P's wrap.**

| writer | reaches rows in cut(P)? | how the cut stays immutable |
|---|---|---|
| Session undo/redo (`undoOverdubSession` / `redoOverdubSession`, the only undo inside a session; `TrackUndo::undoForLoop` routes there) | yes, records 0..iP-1 once the cursor passes below P (LIFO) | **By construction:** `pushOverdubSessionPass` drops the redo tail, so at P's wrap every record 0..iP is active. cut(P) treats records 0..iP-1 as Active regardless of their current state. No snapshot is needed. |
| Redo-tail drop (a new push after undo) | destroys records ≥ cursor | Only records ≥ the cursor, never ones < P while P exists. A dropped P is Cancelled with its record. |
| Later records Q > P (pass, companions, late companions) | no | Excluded by membership. |
| Global undo, clear, load, slot switch | no; not reachable while the session is open (undo routes to the session), and the others close the session | Session close is a Cancelled/handover edge (§5 C3). |
| NOTE_EDIT exit during an in-edit overdub (`closeNoteEditPass` with `hasOverdubSession()`; `replaceNoteEditPass` disables stale rows) | **yes: pre-session edit rows** | **Not immutable today.** §5 C2. |
| Disabled-pass reclamation (`reclaimUnreferencedDisabledPasses`) | only through global-undo and clear paths, outside a session | O(P) pins its record range through the existing `SlotPassReferences` (to verify in the RED). |

**No publication→discharge cycle.**
- S1 reads `passes` through `stepTilePreparation`, never the published root (the live worker does the same).
- S2 reads only O(P)'s cut cache and O(P).pending.
- Publication waits on S5; discharge waits on nothing publication produces.

## 4. Capacity and backpressure (518 owed, multiple intervals)

| item | per obligation | where | notes |
|---|---|---|---|
| obligation record (state, cut stamp, iP, h(P), cursors) | about 48 B | slot runtime (heap), not the 2,288 B `Loop` shell | no RAM1 growth |
| 16-slot set copy | 256 B (16 x 16 B) | same | |
| owned pending set | 983 rows x 24 B = 23.6 KB measured | `InternalHeapFirstAllocator`: internal first, PSRAM fallback | moved by swap, never copied |
| cut cache | ≤ 2,464 notes x 16 B = 39 KB | `DisplayNoteVec` (PSRAM first) | **one at a time** (FIFO), not per obligation |
| prewarmed empty pending vectors | K + 1, reserved at session open | allocator as above | the Clock swap needs one free |

- **Depth K = 2** (current + one behind) bounds the memory: about 48 KB of pending storage plus one 39 KB cut.
- **Throughput needed:** 518 answers and up to 512 seals per 25.6 s lap, which is about 20 answers/s and 20 seals/s.
  - Plus one cut preparation per lap: about one whole-loop resolve of work (about 0.5 s on the device) spread over
    `kGrainEventsPerTurn` grains.
  - S2 per answer is O(candidates of that pitch), so the measured worst case is about 2,464 comparisons.
- **Backpressure when K is full at a wrap: the existing DEC-069 deferral.** The wrap sets `overdubWrapCommitPending_`
  and is retried on the next playback turn, exactly as for an occupied stop worker. The interval continues; nothing is
  answered or dropped on the Clock.
  - Whether a multi-lap interval is acceptable as the overload behaviour is the owner's call (§5 C5).
- **The current discharge lane does not reach that throughput:**
  - `kDeferredDischargePerTurn = 1`;
  - turns only while `isOverdubbing()` and the round-robin idle owner is `VisualCache`;
  - each live tile's lookup is the longest note span, here the whole loop, so up to 64 whole-loop reads per
    generation move;
  - DEC-247's device run measured 50 discharges per run.

  S1's single union-window cut is what makes the rate reachable. The packet needs a device measurement of S1-S3 turns
  per second before it can claim the lane keeps up (R6).

## 5. Conflicts with the existing model (returned before implementation, per the ruling)

**Rulings (2026-11-04).**
- **C1: owner chose (b), "owed too (exact)".** While an earlier obligation owes on a pitch, the live interval's
  decisions on that pitch are recorded as owed and answered in FIFO after it. G3 is in scope.
- **C2-C5: no separate ruling.** The packet's stated preferences apply until the owner says otherwise: C2 (a), C3 as
  written, C4 the gate extension, and C5 the DEC-069 deferral.

- **C1. The next interval's live decisions (exactness).**
  - **Conflict.** Interval Q starts on the Clock edge, while P's late companions don't exist yet. Q's MIDI-path
    decisions (decided Shorten/Hide, occupancy) are made against a source that lacks them. Today P's answers are
    sealed before Q's first NoteOff. So exact parity holds for **P's** answers (§3), but not for Q's live decisions
    on pitches where P still owes.
  - **Options:**
    - (a) Accept, and define the result: companions apply in record order (P before Q, guaranteed by FIFO sealing),
      with a RED that pins the final content for the overlap classes (Q shortens S, P hides S, and so on).
    - (b) While an earlier obligation is outstanding, Q's decisions on that pitch become owed too, and are answered in
      FIFO after P. That is exact, but more of Q's work moves off the MIDI path.
  - **Preferred: (b) for exactness; (a) is smaller.** Owner call.
- **C2. NOTE_EDIT exit inside an in-edit overdub mutates pre-session rows.**
  - **Options:**
    - (a) A bounded flip journal (row id, previous state, sequence) that cut evaluation consults. The NOTE_EDIT commit
      is refused by name while the journal is full.
    - (b) A NOTE_EDIT commit while obligations are outstanding is deferred until they retire. That is a user-visible
      wait off the Clock.
  - **Preferred: (a).**
- **C3. Record destroyed, not undone.**
  - A redo-tail drop, or session close without exit, destroys a record. Its obligation is Cancelled with it: its notes
    no longer exist in any reachable state.
  - This is not "parking on undo". An undone-but-reachable record keeps resolving (§2).
  - Session exit hands live obligations to the exit's inventory (DEC-248) instead of cancelling them.
- **C4. Discharge while stopped needs the worker gate extended.**
  - Today: `isOverdubbing() && deferredOwner == VisualCache`.
  - The gate gains "the active Loop owns a non-retired WrapObligation". It is the same lane and owner, with no new
    worker.
- **C5. Overload behaviour.** With K full, the wrap defers (DEC-069), so an overloaded session produces a longer
  interval. The alternative, refusing new completions, loses decisions (C3.4 defect). Owner call.

## 6. RED -> GREEN contract (strengthened per the ruling)

| # | Statement |
|---|---|
| R1 | Per-wrap Clock work for the transferred phases is constant: counted units (rows touched, derives, allocations, edit passes saved) are identical for 0, 50, 518 and 2,000 owed, and derives are 0. |
| R2 | Exact source-cut parity: every carried answer equals the retained-path answer for cut(P), as an independent sequential oracle. It covers decided and owed rows, and occupancy and span completions. |
| R3 | Later edits to older NoteIds: a later record Q's Shorten/Hide on a NoteId below h(P) never changes P's answers; a late companion of an earlier record O < P always does. |
| R4 | Multi-wrap backlog: 3 wraps of 518 owed each retire in FIFO order with no loss, duplicate or overwrite. Memory stays within K; the K-full wrap defers through DEC-069 and is retried. |
| R5 | Undone-record progress: undo P mid-answer; P's obligation still retires, its rows are written Disabled, redo enables them, and the content equals the never-undone run. A redo-tail drop cancels exactly that obligation. |
| R6 | Stopped-transport discharge: stop with obligations outstanding; stop acceptance does not wait; obligations retire while stopped; the session exit publishes after them. |
| R7 | Publication/discharge liveness: record P never publishes before O(P) retires; discharge never waits on publication; both converge (no cycle) under the backlog fixture. |
| G1 | Prepared spans answered by the live lane before the wrap seal unchanged (no added delay). |
| G2 | Stop and session close leave no obligation unanswered or answered twice; DEC-248's hand-over sees each record's companions exactly once. |
| G3 | (C1 option b only) Q's decisions on pitches owed by P equal the sequential oracle. |

The stop interval's own forced deadline (`TrackCaptureStopCommit.cpp:200`) stays synchronous in this scope. The C3.4
realtime contract is **not** declared fixed while it remains.

## 7. Out of scope

- The heap-tail dropped-companion defect: its own slice, RED gated `MEMORY_PRESSURE_RED`.
- Fixed-PSRAM storage migration.
- The stop interval's forced deadline.
- `verifyCaptureHotStop` and held-NoteOn extraction on the wrap.
