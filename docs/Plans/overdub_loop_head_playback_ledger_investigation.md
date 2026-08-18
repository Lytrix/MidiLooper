# Loop-head playback ledger after wrap

**Status:** Closed — firmware in [`overdub_loop_head_playback_ledger_bugfix.md`](overdub_loop_head_playback_ledger_bugfix.md). Native PASS. HITL open.  
**Date:** 2026-08-18  
**Kind:** investigation  
**Parent:** [`overdub_present_at_tick_jit_architecture.md`](overdub_present_at_tick_jit_architecture.md)  
**Does not reopen:** [`overdub_wrap_committed_pass_playback_bugfix.md`](overdub_wrap_committed_pass_playback_bugfix.md) (HITL PASS), occupy fallback, `collectOverdubNoteOnParticipantIds`, wrap-rebuild, `PresentNoteVec`

---

## Question

After a wrap commit, what playback event establishes `ActiveNoteLedger` for notes that are logically present at loop tick **0**?

Do **not** assume carry-across vs NoteOn-at-0 vs loop-head catch-up until the event/ledger trace answers it.

Occupy stays a reader of `Entry.noteId`. This is not an occupy bug until playback state at 0 is shown to be correct and occupy still misses.

---

## Boundary vs wrap-S (closed)

[`185831`](../../captures/session_20260818_185831.log) track 6, 1-bar **768**, OVERDUBBING. Wrap 1: abs **1464**, storage **696**.

```text
wrap S = 696
  → commit pass
  → apply (prev, S]
  → occupy 71 @ 704  n=1 a=1 b=1   PASS
```

Then:

```text
loop head storage 0
  → source-view 60 present (a=1 b=1)
  → ledger empty (n=0)
  → occupy 60 @ 0   FAIL
```

Later the same pitch at storage **64** is `n=1 a=1 b=1`. Pitch 60 can enter the ledger. The miss is the **0** transition after wrap, not occupy lookup.

Do not add a second catch-up (`loop head → re-seed ledger`) until this trace names which playback event should have written the Entry.

---

## Pin (first wrap only)

USB is ch 4. Track 6 playback channel is 7. This capture has **no** NoteOn/NoteOff `MO` on ch 7 (same as [`152745`](../../captures/session_20260818_152745.log)). Mute still writes the ledger; **MO cannot be used as the ch-7 ledger-emit trace.** Occupies below are the ledger observation.

### User 60 (MI ch 4) first overdub bar → wrap → 64

| CAP | Event | Storage | Occupy |
|-----|--------|---------|--------|
| `52233405` | MI On | **0** | `n=0 a=0 b=0` — first bar, source empty |
| `52252743` | MI Off | **8** | |
| `52401106` | MI On | **64** | `n=0 a=0 b=0` |
| `53312283` | MI Off | **416** | |
| `53616472` | MI On | **528** | `n=0 a=0 b=0` — no Off before wrap |
| `54053959` | wrap `why=wrap` notes=12 | **696** | 71 @ 704 `n=1 a=1 b=1` |
| `54242711` | MI Off | **0** | |
| `54243168` | MI On | **0** | **`n=0 a=1 b=1` `54243271`** |
| `54257232` | MI Off | **8** | |
| `54400075` | MI On | **64** | **`n=1 a=1 b=1` `54400110`** |

Later wraps’ 60-at-0 occupies are `a=0 b=0` (source-view no longer has 60 at 0). This miss is **wrap 1 → first loop head**.

---

## Proven

1. Wrap-S `(prev, S]` catch-up is **not** this miss (71 @ 704 `n=1`).
2. At occupy 0, `Entry` for 60 is **inactive**. Wrap-held 60 from On@528 did **not** still occupy the ledger slot at that occupy.
3. By storage 64, a 60 `Entry` **is** active (`n=1 a=1 b=1`). The stream can apply 60; loop head 0 is the failing step.
4. First-bar user 60 includes a committed-looking span **0–8** and On@**528** still open at wrap.

### Carry-528 ruled out for this capture

`didPlaybackEventCross` is `prev < ev && ev <= current`. Clock stays FIFO with notes (`MidiDispatchOrder`).

On@528: clock at 528 runs **before** the MI On is appended. `lastTickInLoop` is already 528 when the event enters capture. Later clocks use `prev=528`, so `528 < 528` is false. Capture playback never applies that On@528.

Wrap `(prev, S]` at 696 is `(~688, 696]`. 528 is outside it. `extractOpenCaptureNoteOns` removes the open On@528 before `commitCapturePass`. Sealed pass is the closed spans (**0–8**, **64–416**), not the hold.

Source-view 60 at 0 is the wrap-committed **0–8** NoteOn. 64 works because `(56, 64]` is a normal forward interval.

### Expected write at loop head

```text
wrap reanchor at S=696 → cursor past phase <= 696 (including 0)
  → clocks 704…760
  → storage 0: didDisplayPlayheadWrapBackward resets nextEventIndex
               (COORD projStart 768 → 1536 proves this wrap ran)
  → atLoopStart (760, 0]: evTick <= 0
  → wrap-committed NoteOn @ 0 → ledger
  → then USB On occupy
```

That path is already in `playCommittedLoopMidi`. Native 0-clock cursor math **PASS** once On@0 is in the sealed pass.

Production wrap seal ran Q16 min-length on OverdubWrap (span 8 < 12), so On@0 never entered `lastCommittedPassId()`. Q16 is hot stop only. Fix: [`overdub_loop_head_playback_ledger_bugfix.md`](overdub_loop_head_playback_ledger_bugfix.md).

## Not in this capture

- Wrap-pass chunk dump (inferred from MI On/Off + extract-open, not read from `committedChunkIds`)
- Cursor value at the 0 clock
- Direct `applyPlaybackLedgerEvent` CAP (no ch-7 MO)

## Still open

HITL: first loop-head 60 @ 0 `n=1 a=1 b=1` after the wrap-seal fix. Native pin is closed.

---

## Hard don’ts

- Do not change the wrap-S `(prev, S]` path to “also apply loop head”
- Do not touch `collectOverdubNoteOnParticipantIds`
- Do not occupy fallback / `PresentNoteVec` / source-view patch
- Do not reopen wrap-rebuild as the occupy prerequisite
