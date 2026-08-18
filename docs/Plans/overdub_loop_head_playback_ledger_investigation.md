# Loop-head playback ledger after wrap

**Status:** Investigation. No firmware.  
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

## Not in this capture

- Wrap-pass chunk list / NoteOn–NoteOff order at 0
- Cursor `nextEventIndex` at the 0 clock
- Direct `applyPlaybackLedgerEvent` CAP (no ch-7 MO)

## Open (do not pick yet)

1. **Carry across wrap** — On@528 should still be on the ledger at 0. `n=0` means it was never applied, or an Off cleared it before this occupy.
2. **Wrap-committed NoteOn at 0** — first-bar 0–8 should re-fire at loop head. `n=0` means that On was not applied before occupy.
3. **Loop-head stream / cursor** — `didDisplayPlayheadWrapBackward` + `isPlaybackCatchUpWindow` + reanchor-at-S should still allow the 0-clock advance to apply phase-0 events. Not proven whether the cursor skipped them or an Off at 0 won last-writer.

Trace path (no new owner):

```text
PlaybackMergedMidiEvents
  → wrap cursor / reanchor at S / nextEventIndex on backward wrap
  → playbackCursorAdvanceSend
  → ActiveNoteLedger
```

Include `lastCommittedPassId()` NoteOn/NoteOff for pitch 60.

---

## Hard don’ts

- Do not change the wrap-S `(prev, S]` path to “also apply loop head”
- Do not touch `collectOverdubNoteOnParticipantIds`
- Do not occupy fallback / `PresentNoteVec` / source-view patch
- Do not reopen wrap-rebuild as the occupy prerequisite
