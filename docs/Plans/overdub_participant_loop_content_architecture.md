# Overdub participant discovery from loop content

**Status:** Architecture contract **pinned** 2026-08-17. Phase 0b identity mapping **done**. Phase 1 firmware **not authorized**. PresentNote C++ rename **not authorized**.  
**Date:** 2026-08-17  
**Kind:** architecture + implementation  
**Parent:** [`consumer_window_budget_ownership_architecture.md`](consumer_window_budget_ownership_architecture.md)  
**Related:** [`overdub_lifecycle_representation_authority.md`](overdub_lifecycle_representation_authority.md), [`playback_gather_lcr_consume_enhancement.md`](playback_gather_lcr_consume_enhancement.md), DEC-037 LoopContentResolution  
**Evidence:** [`213401`](../../captures/session_20260817_213401.log)  
**Supersedes:** `playback_sounding_state_overdub_participant_architecture.md` (MIDI-execution hypothesis)  
**Does not authorize:** shrinking `kOverdubSourceWindowBars`; tying overdub to `Track::sendMidiEvent` / `ActiveNoteLedger`; a `WindowManager` / `WindowRequest`; replacing `overdubSourceView` in the first change; Phase 1 observation firmware; renaming LCR `SoundingNote` in C++ until a naming-only go

**Approved type name (docs now, C++ later):** `PresentNote` = which loop notes are present at tick S. Not a new Manager.

---

## North star

> **Overdub participant discovery must query canonical note state at the current loop tick. If a note is present at that tick, it is an overlap candidate regardless of whether its MIDI NOTE ON has already been emitted, when it was physically emitted, or whether it is present in the current playback event window.**

Shorter: overdub needs to know which loop notes are **present** at the current loop tick, regardless of whether the corresponding MIDI event has physically been emitted yet, and regardless of mute.

---

## Three vocabularies (do not mix)

| Question | Name | Use |
|----------|------|-----|
| Which loop notes are present at tick S? | **PresentNote** | Overdub participant discovery. Today’s LCR `SoundingNote` / `resolveState` / `soundingAt` / `noteSoundsAt` / `soundingAtHoldOnly` ask this. Independent of send and mute. |
| Has this note been sent to MIDI output? | **Sounding** (MIDI output only) | Coupled to `sendMidiEvent` / audible output. Today that is **not** the LCR type. Do not keep LCR results named Sounding after the naming step. |
| Is this note ON in playback execution, including when muted? | **ActiveNote** | [`ActiveNoteLedger`](../../include/ActiveNoteLedger.h). Mute suppresses send; the ledger still runs. Do not call this sounding (that couples to mute). Do not use it for overdub. |

```text
ActiveNoteLedger
    = what playback execution currently considers ON (mute-decoupled)

PresentNote / canonical note state
    = which loop notes are present at loop tick S

Overdub
    = must use PresentNote / canonical note state
```

---

## 1. Objective

Remove the need for overdub note-on / hold handling to create a broad source-resolution window merely to discover which same-pitch notes are **present** at tick `S`.

The missing information in [`213401`](../../captures/session_20260817_213401.log) is:

> The current tick does not have a cheap query of which canonical notes are present at that tick.

Not: which MIDI notes have been sent and are still on.

---

## 2. Normative example (tick 32 / 33 / 40 / 60)

This example is the behavior the implementation must preserve.

```text
canonical note:
    NoteId 2
    pitch 31
    NOTE ON @ tick 32

playback:
    scheduled/send event @ tick 32
    physically emitted @ tick 33 because of latency

overdub:
    MIDI IN NOTE ON pitch 31 @ tick 40
        ↓
    NoteId 2 is still present at tick 40
        ↓
    NoteId 2 becomes an overlap candidate

    MIDI IN NOTE OFF pitch 31 @ tick 60
        ↓
    NoteId 2 is still present at tick 60
        ↓
    add NOTE OFF for NoteId 2 @ tick 60
```

```text
NoteId 2
pitch 31
present from 32
        │
        ├── tick 33 → MIDI event happens
        ├── tick 40 → overdub sees it present
        └── tick 60 → overdub sees it present
```

`ActiveNoteLedger` at tick 32 may still be OFF if send has not caught up. Overdub at 40 and 60 must still see NoteId 2 present. Mute must not empty that set.

---

## 3. Withdrawn hypothesis — MIDI execution as overdub truth

The previous draft proposed updating participant state at `Track::sendMidiEvent` (NOTE ON add / NOTE OFF remove).

That is the wrong world.

```text
PLAYBACK EXECUTION:
    Has this note been sent?
    Is ActiveNoteLedger ON?
    → ActiveNote (mute-decoupled execution)
      sendMidiEvent, mute, emit

OVERDUB:
    Is this same-pitch note present at tick S?
    → canonical note state / PresentNote
      independent of send, mute, playing/stopped, gather window
```

Phase 0 negative result:

| Existing | World | Why overdub cannot own it |
|----------|-------|---------------------------|
| `Track::sendMidiEvent` | execution / sounding (output) | Overdub would depend on playing and on MIDI having been emitted |
| `ActiveNoteLedger` | ActiveNote | `(channel, pitch)` hang-prevention; no `NoteId`; one slot per pitch; send-timed |
| `collectOverlapHoldPlaybackNoteOn` | execution | Only inserts into already-open `pendingNotes` |
| `PlaybackMergedMidiEvents` | execution gather | 2-bar event window. A NOTE ON from 8 bars ago is not in it |

Playback and overdub may share **canonical note traversal** work. They remain semantically independent. An already-prepared present-at-S result may be consumed **opportunistically**. Overdub must not require playback execution to produce that result.

---

## 4. Problem statement

Today the overdub path can do:

```text
USB note / overdub
    ↓
ensureOverdubSourceNotesForHold
    ↓
prepared miss
    ↓
resolveWindow(full overdub source window)
```

The source window is `Loop::kOverdubSourceWindowBars` (default 16). On the 64-bar capture:

```text
why=open:  tot 133 ms, 1177 events, 536 notes
why=hold:  win 99 ms, 1177 events, merged=0
```

Hold `merged=0` is the request-granularity failure: a 16-bar reconstruct to rediscover notes already present at `S`.

Current:

```text
"I need to know what participates at S"
        ↓
"resolve 16 bars around S"
        ↓
"reconstruct notes"
        ↓
"find participants"
```

Desired:

```text
"I need to know which notes of pitch P are present at S"
        ↓
"consume present-at-S if available, else smallest canonical query for present(S)"
        ↓
"RC8 participant selection"
        ↓
"participants"
```

### Cost model (outranks 1-bar vs 64-bar)

What must disappear is the geometric multiplier:

```text
loop length × arbitrary source window × reconstruction
```

Legitimate cost:

```text
canonical note traversal to S + notes present at S + participant filtering
```

or, if preparation already guarantees the result:

```text
notes present at S + participant filtering
```

A 64-bar loop may cost more than a 1-bar loop if finding the relevant span requires traversal. It must not pay a 16-bar reconstruct because the window is 16.

A passing Experiment 1 clamp (1/2/4/8/16) is evidence, not this contract.

---

## 5. Two independent worlds

```text
                    LOOP CONTENT
                         │
             ┌───────────┴───────────┐
             │                       │
             ▼                       ▼
     canonical note traversal    playback execution
             │                       │
             ▼                       ▼
      notes present at S         MIDI output
             │                   ActiveNoteLedger
             ▼
      overdub participants
```

---

## 6. Present-at-S is a definition, not a shared mutable object

One canonical meaning of **notes present at loop tick S**. Multiple consumers may obtain that result through prepared lookup or traversal. None becomes an execution-state authority.

“Surf along” means opportunistic reuse of an already-available canonical note-state result. It is **not** a requirement to maintain a continuously advanced occupancy store coupled to playback.

Advance vs query is **implementation, not ownership**:

> Overdub consumes an existing present-at-S result when available; otherwise it requests the smallest canonical-content resolution capable of answering which notes are present at S.

---

## 7. Goals

1. Identify same-pitch participants at `S` without `resolveWindow(16 bars)` on USB.
2. Use canonical note identity (`NoteId` + span as required by RC8), not ActiveNote / MIDI-output state.
3. Keep participant discovery independent of playing, mute, and gather window.
4. Consume already-available present-at-S results opportunistically; do not duplicate a second window.
5. Preserve RC8 occupied-lane / wrap consume ([`155450`](../../captures/session_20260817_155450.log)).
6. Keep LCR as resolver of explicitly requested ticks/intervals.
7. Keep playback gather ownership unchanged (`ensurePlaybackMergedMidiEventsBuilt`).
8. Avoid `WindowManager` / `WindowRequest` / `WindowPolicy`.
9. Use **PresentNote** for present-at-S; do not invent a Manager.

Secondary: USB latency closer to 1-bar behavior; source/hold JIT no longer inherits a 16-bar geometric window.

---

## 8. Non-goals

* changing the 2-bar playback gather;
* putting `resolveWindow` on MIDI playback;
* replacing LCR;
* redesigning the overdub consume → seal → rebuild spine;
* deleting `overdubSourceView` in the first change;
* treating `PlaybackMergedMidiEvents` as notes present at S;
* treating `ActiveNoteLedger` as the participant store;
* treating LCR present-at-S (`SoundingNote` today) as MIDI execution state;
* copying present-at-S at every bar as a new derived owner (DEC-037 `soundingAt` heap fail [`225351`](../../captures/session_20260814_225351.log));
* Phase 1 firmware before user authorization;
* C++ rename `SoundingNote` → `PresentNote` before a naming-only go.

---

## 9. Note-on flow

```text
incoming NOTE ON @ S
        │
        ▼
notes present at S (canonical)
        │
        ▼
same-pitch / RC8 participant rules
        │
        ▼
overdub participant ids
        │
        ▼
capture new note
```

Not `resolveWindow(16 bars)` as the normal discovery mechanism.

Today `Track::snapshotOverlapHoldCandidates` calls `ensureOverdubSourceNotesForHold` then walks `overdubSourceViewNotes()`. That walk is the RC8 rule. The fill path is the 16-bar window. Replace the fill, not the RC8 rule, and only after parity.

---

## 10. Participant selection and parity

Notes present at `S` are not automatically the overlap result.

```text
notes present at S
        │
        ▼
pitch / lane / overlap rules
        │
        ▼
participants
```

First implementation keeps the old path and compares:

```text
A = existing source-window participant result
B = present-at-S participant result
```

Accept only when `A` identities == `B` identities.

### Hard architectural test

```text
same canonical loop
same tick S
same incoming pitch P

PLAYING                         → participants A
STOPPED                         → participants A
MUTED                           → participants A
PLAYING but outside playback gather → participants A
```

If any of those produce different participant identities, the implementation has leaked MIDI execution semantics into canonical note state.

Other cases still required: 1-bar and 64-bar loops; interior overdub; overdub enter; occupied lane; same-pitch overlap; different-pitch notes; note spanning `S`; loop-boundary crossing; note-on/off near wrap; multiple simultaneous present notes.

---

## 11. Note-off

At `E`, classify what the current path takes from source/LCR:

```text
already available from notes present at S (captured on the transaction)
available from the overdub transaction
requires canonical note state at E
genuinely requires LCR interval resolution
```

Only the last category retains an LCR window. Wrap-shaped consume `[S, L) ∪ [0, E)` stays the consume geometry ([`overdub_lifecycle_representation_authority.md`](overdub_lifecycle_representation_authority.md)). Present-at-S is not assumed to replace every source-view operation.

---

## 12. `overdubSourceView`

Do not delete it in the first change.

```text
OLD:
note-on
  → ensureOverdubSourceNotesForHold()
  → source window
  → DisplayNote walk
  → participants

NEW experimental:
note-on
  → notes present at S
  → same RC8 participant selection
```

Run both until `A == B`. Then remove the 16-bar lookup. Then classify remaining source-view responsibilities.

---

## 13. Relationship to LCR

LCR stays:

> Given this exact tick or interval, resolve it.

`resolveState` is the existing **canonical note-state query** for notes present at S (C++ type still `SoundingNote` until rename).

Prepared miss must **not** silently become unlimited 16-bar `resolveWindow` on USB. Development failure is an explicit miss (`PARTICIPANT_MISS`). Design a bounded fallback only after the miss class is understood.

DEC-037 6.0 still holds: overdub start must not cold-build LCR. Preparation stays idle/background.

DEC-037 per-bar copied `soundingAt` as an O(history) store remains rejected ([`225351`](../../captures/session_20260814_225351.log)). This plan is a present-at-S **query**, not a proportional history replica.

---

## 14. Relationship to consumer-window architecture

Parent invariant:

> No latency-sensitive path may synchronously resolve more content than its consumer contract requires.

For overdub note-on, the contract is **notes present at S** (then pitch/RC8), not a geometric window.

```text
Consumer-window architecture
        │
        ├── Playback → JIT gather (events to send)
        │
        ├── Display → visual window
        │
        └── Overdub → notes present at current tick
                         │
                         └── resolve an interval only if present-at-S cannot answer
```

---

## 15. Wrap

A note whose canonical span crosses the loop boundary is present on both sides of wrap. Presence follows **linear span containment**, not “was a NOTE ON sent this iteration.” RC8 wrap consume remains authoritative. Phase 0b wrap-predicate comparison is in §18.

---

## 16. Phases

| Phase | Work | Firmware |
|-------|------|----------|
| **0** | Negative result: execution ledger is the wrong owner. **Done.** | none |
| **0b** | Identity mapping `resolveState` vs RC8. Site classification Present / Sounding / Active. **Done** (this file). | none |
| **Naming** | `SoundingNote` → `PresentNote` at present-at-S sites only. | naming-only, **not authorized** |
| **1** | Prototype present-at-S query at overdub note-on **alongside** source-window fill. | observation only, **not authorized** |
| **2** | Parity A vs B including the hard PLAYING/STOPPED/MUTED/outside-gather test. | keep old path |
| **3** | Disable redundant source-window lookup for note-on discovery. Measure cost. | after parity |
| **4** | Note-off classification. | after Phase 3 |
| **5** | Make present-at-S the owner of that one responsibility. Leave unrelated `overdubSourceView`. | narrow migration |

---

## 17. Failure / fallback

If present-at-S cannot answer, do **not** fall back to unlimited 16-bar synchronous `resolveWindow` on USB.

Explicit miss first. Bounded fallback only after the miss class is understood.

---

## 18. Phase 0b findings — identity mapping

**Decisive question:** Can `resolveState` / `tryResolvePreparedState` provide enough canonical identity to reproduce RC8’s `(NoteId, LinearSpan)` participant set exactly?

**Answer:** **Not from the `SoundingNote` result type alone.** Presence filtering inside LCR uses full spans. The **emitted** `SoundingNote` drops `endTick`. RC8 selection and later consume need start **and** end (plus wrap). Do not extend `SoundingNote` speculatively; the missing end already exists on internal `NoteSpan` and on source-view `DisplayNote`.

### 18.1 Three paths

```text
canonical loop content
        │
        ├── resolveState(passes)          → SoundingNote (cold; full-loop reconstruct)
        ├── tryResolvePreparedState(S)    → SoundingNote (prepared spans + checkpoints)
        └── snapshotOverlapHoldCandidates → DisplayNote walk (RC8 gold)
```

**RC8 gold** — `Track::snapshotOverlapHoldCandidates`:

1. `ensureOverdubSourceNotesForHold(holdStart, pitch, …, soundingAtHoldOnly=true)` (16-bar `resolveWindow` fill).
2. Walk `overdubSourceViewNotes_`.
3. Keep `note == pitch` and `noteId != kInvalidNoteId`.
4. Phase start/end; skip zero-length; if `end < start` after phase, `end += loopLength`.
5. Present if `linearStart <= S < linearEnd` **or** the same test shifted by `loopLength` (same-start included).
6. Insert `note.noteId` into `PendingNote.overlapNoteIds`.
7. Span for consume later comes from the source-view `DisplayNote`, not from the id set.

**Cold `resolveState(passes, loopLength, tick)`:**

1. `gatherActiveResolvedEvents` for the **full loop**.
2. `reconstructDisplayNotes`.
3. Keep notes where `noteSoundsAt` is true.
4. Emit `{channel, pitch, noteId, onTick=startTick}`. **No endTick.**
5. This is a full-loop reconstruct — not the USB replacement.

**Prepared `tryResolvePreparedState(tick, playbackRevision)`:**

Requires `preparedWindowReady`, non-empty `spans`, `spanBoundaries`, and `soundingAt`. Miss returns false (does not rebuild). Internal `StateCheckpoints::NoteSpan` has `startTick` and `endTick` plus a nested `SoundingNote`. Replay: seed from checkpoint `soundingAt[i]`, then `applySpanBoundaryAtTick` (add at start, erase at end). Output is still `SoundingNote` (no end).

### 18.2 Field mapping

| Required by RC8 | Available on `SoundingNote` (emitted) | Available on `NoteSpan` (internal) | Available on RC8 `DisplayNote` |
|-----------------|----------------------------------------|------------------------------------|--------------------------------|
| pitch | yes (`pitch`) | yes | yes (`note`) |
| channel / lane | yes (`channel`) — extra; RC8 does not filter channel (DEC-033, `DisplayNote` has no channel) | yes | no |
| NoteId | yes | yes | yes |
| start tick | yes (`onTick`) | yes (`startTick`) | yes |
| end tick | **no** | **yes** | **yes** |
| wrap representation | only via how `noteSoundsAt` filtered membership | start/end as stored | phase + optional `+ loopLength` |
| simultaneous same-pitch notes | `upsertSounding` keys by `noteId` | one span per id in that table | distinct `noteId` inserts |

**NoteId uniqueness:** both paths treat one `noteId` as one note. If two `DisplayNote` rows ever shared a `noteId` with different spans, both would collapse. No evidence in this mapping that production assigns two live spans the same id.

**Discovery vs consume:** RC8 **discovery** stores `NoteId` only. Consume looks up span on `overdubSourceViewNotes_`. So `SoundingNote.noteId` can match the **id set** if membership at S matches. LinearSpan is still required to *decide* membership and to *consume*. Membership today uses end tick **inside** LCR (`noteSoundsAt` / `NoteSpan`) even though the emitted struct drops it.

### 18.3 Wrap predicates — not proven equal

`noteSoundsAt` (LCR):

* If `isWrappedLoopNotePair(start, end, L)` (`off < on` and `(on - off) > L/2`): present when `tick >= start || tick < end`.
* Else: present when `tick >= start && tick < end`.

RC8 / `displayNoteSoundingAtHold`:

* Phase start/end; skip zero-length; if phased `end < start`, `end += L` **without** the half-loop test.
* Present when `start <= S < end` or the same interval shifted by `L`.

Inclusive start / exclusive end match for non-wrap.

**Gap:** a pair with `end < start` that **fails** `isWrappedLoopNotePair` (span not greater than half the loop) is never present under `noteSoundsAt`, but **is** present under RC8’s linearize-if-end-before-start rule. Phase 2 must prove production `DisplayNote` rows never take that shape, or the predicates must be unified **before** replacing the 16-bar fill. Do not assume they are the same.

### 18.4 Prepared miss vs cold cost

| Path | Cost character |
|------|----------------|
| Cold `resolveState(passes)` | Full-loop gather + reconstruct + filter. Same geometric class as today’s 16-bar (or worse: whole loop). **Not** the USB replacement. |
| `tryResolvePreparedState` | Checkpoint seed + boundary replay. Cost ≈ traversal from checkpoint to S + present set. Matches the desired cost model **when prepared**. |
| Prepared miss | Returns false. Must not become 16-bar `resolveWindow` on USB. |

Prepared prerequisites: idle device gate completed, `playbackRevision` stamp match, `spans` / `spanBoundaries` / `soundingAt` non-empty. DEC-037 6.0: do not cold-build this on the overdub button.

### 18.5 Site classification (Present / Sounding / Active)

**PresentNote (notes present at S) — LCR + overdub hold fill**

* `struct SoundingNote` / `SoundingNoteVec` in [`LoopContentResolution.h`](../../include/LoopContentResolution.h)
* `resolveState`, `tryResolvePreparedState`, `resolveStateFromSpanBoundaries`
* `StateCheckpoints::soundingAt`, `NoteSpan`, `spanBoundaries`
* `noteSoundsAt`, `upsertSounding`, `eraseSounding`, `applySpanBoundaryAtTick`
* `Loop::soundingAtHoldOnly` / `ensureOverdubSourceNotesForHold` / `displayNoteSoundingAtHold`
* native tests in `test_loop_content_resolution`, `test_overdub_source_view` that call `tryResolvePreparedState`

**ActiveNote (execution, mute-decoupled)**

* `ActiveNoteLedger` / `LoopPlaybackRuntime::ledger`
* `Track::sendMidiEvent` noteOn/noteOff ledger updates
* mute: send suppressed; ledger still runs

**Sounding / MIDI output**

* `midiHandler.sendMidiEvent` from `sendMidiEvent` when `playbackEmitMidiOutput_`
* Not an LCR type today

**MIDI event helper (not PresentNote, not ledger)**

* `isSoundingNoteOn` in [`LoopEventValidation.cpp`](../../src/Utils/LoopEventValidation.cpp) = `NoteOn && velocity > 0` for pairing. Naming debt; do not fold into PresentNote.

### 18.6 What Phase 0b does not choose

* Whether consume should read `NoteSpan.endTick` or keep looking up `DisplayNote` by `noteId` on `overdubSourceViewNotes_`.
* Whether to unify wrap predicates now.
* C++ rename (naming-only go still required).

---

## 19. Acceptance

**Performance:** no ~100 ms `resolveWindow` on normal note-on/hold participant discovery; no 1177-event USB source resolve; `late_clk == 0`. Cost follows §4, not loop-length × 16-bar reconstruct.

**Correctness:** RC8 occupied-lane; same-pitch overlap; `NoteId` + linear-span identity; wrap (after predicate unification if 18.3 requires it); notes present at `S` whose NOTE ON is outside the gather window; multiple present notes; hide/consume; note-off finalization; **hard PLAYING / STOPPED / MUTED / outside-gather identity equality**.

---

## 20. Invariant

> **A canonical note whose linear span contains tick `S` is present at `S` for overdub, regardless of whether playback has emitted its NOTE ON, whether MIDI output is sounding, whether `ActiveNoteLedger` is ON, whether that NOTE ON remains in the current playback gather, or whether transport is playing or the track is muted.**

> **Overdub participant discovery consumes notes present at `S` from loop content before considering any historical window resolution. An already-prepared present-at-S result may be consumed opportunistically; overdub must not require playback execution to produce that result.**

---

## 21. Expected architecture

```text
                    canonical loop content
                            │
             ┌──────────────┴──────────────┐
             ▼                             ▼
    notes present at S               playback gather
    resolveState / prepared          PlaybackMergedMidiEvents
    (definition; query or                    │
     opportunistic consume)                  ▼
             │                       MIDI execution
             ▼                       ActiveNoteLedger
     pitch P participants            sendMidiEvent
             │                             │
             ▼                             ▼
      overdub transaction              MIDI output
             │
             ▼
        note-off snapshot
```

One **definition** of notes present at a tick. Overdub does not reconstruct a 16-bar window to rediscover it. Playback send is a separate consumer.

---

## 22. Next action

Do **not** implement a new window size.

Do **not** implement send-side present-at-S.

Do **not** start Phase 1 until authorized.

Do **not** rename `SoundingNote` → `PresentNote` in C++ until a naming-only go.

If present-at-S parity succeeds after wrap-predicate proof, the likely change is:

> Replace the 16-bar discovery fill with the existing exact-tick canonical query (`tryResolvePreparedState` / `resolveState`), while leaving RC8 consume/transaction machinery intact — and obtaining LinearSpan from `NoteSpan` or source-view lookup, not from the emitted `SoundingNote` alone.

---

## Architecture gate (investigation only)

| Question | Answer |
|----------|--------|
| **Owner module** | Loop content via LCR `resolveState` / prepared spans. Overdub reads present-at-S; playback send does not write it. |
| **Primary invariant** | Notes present at `S` are canonical span containment, independent of MIDI execution, mute, and gather. |
| **Ownership change?** | NO while consuming existing `resolveState`. YES (stop) if a new Manager is proposed. `PresentNote` is a type rename, not a new owner. |
| **State transition change?** | NO until Phase 3 disables the source-window lookup. |
| **Behavior-preserving?** | YES until Phase 3. |
| **Reuse** | YES — present-at-S query already exists (`resolveState`). Do not overload `ActiveNoteLedger`. Do not add `resolveWindow` on USB. |
| **Phase scope** | Phase 0b done. Phase 1 and C++ rename not authorized. |
