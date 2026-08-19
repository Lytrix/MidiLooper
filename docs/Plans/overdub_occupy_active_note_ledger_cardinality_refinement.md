# Occupy open-NoteOn ledger cardinality

**Status:** Nested HITL gate **MET**. Structural gate partial. Extra-open (`n>a`) unmasked — do not fold into DEC-042.  
**Date:** 2026-08-19  
**Kind:** refinement  
**Decision:** [DEC-042](../DECISION_LOG.md#dec-042-same-pitch-active-note-identity-is-a-cardinality-problem-not-an-off-identity-problem)  
**Parent:** [`overdub_occupy_unmatched_off_ledger_investigation.md`](overdub_occupy_unmatched_off_ledger_investigation.md)  
**Evidence:** [`092336`](../../captures/session_20260819_092336.log) pitch 12 nested `On@96` / `On@144` / `Off@192`  
**HITL:** [`095902`](../../captures/session_20260819_095902.log)

---

## Invariant

`ActiveNoteLedger` is an **open-NoteOn ledger**: an entry means this particular NoteOn has been applied and its Off has not. A lane may hold several identities. Occupy walks `forEachActive`. `noteId(channel, pitch)` is newest-on-lane only (compatibility).

## NoteOff resolution

- known identity + found → remove that exact Entry
- no identity → LIFO pop most recent open On on the lane
- known identity + not found → orphan, no mutation

Do not stamp Offs. Do not LIFO-fallback a stale identity-bearing Off.

## Storage

Sparse `std::array<Entry, 128>` plus `count_`. Overflow refuses the new On (never evicts). Cap miss is a future gate. Ledger stays in `LoopPlaybackRuntime` EXTMEM; `applyPlaybackEvent` is `TRACK_COLD_MEM` `noinline` so the body does not grow ITCM.

## MIDI wire

`forEachActive` is not deduped. `silenceSlotMidiOutput` sends one physical NoteOff per unique `(channel, pitch)`.

## HITL gate (this RC only)

- nested `n=0 a=1` → `n=1 a=1`
- structural `n=1 a=2` → `n=2 a=2`

Parked: exclusive-end `n=1 a=0`, abutting interior, L4294, prepared-span miss, overflow product handling.

## HITL [`095902`](../../captures/session_20260819_095902.log)

76 `DIAG,lcr,part`. `eq=1` on all 76. `ledger,overflow` **0**. One `RING,overflow` (USB serial, not the ledger cap). All 44 `mismatch` lines have `cu=0` (`ltick==hs`); this occupy did not run catch-up.

| n,a | count | Gate |
|-----|------:|------|
| 1,1 | 24 | agree |
| 0,0 | 7 | agree |
| **0,1** | **0** | **nested empty-ledger MET** |
| **2,2** | **2** | structural both-open works: pitch 24 `hs=120` wrap `744–167`; pitch 30 `hs=144` |
| 1,2 | 1 | leftover: pitch 24 `hs=456`, covering `456–551` (5191) and `456–480` (5197), both `On@456`; ledger `n=1` `lid=5191` |
| 1,0 | 2 | parked exclusive-end |
| **3,1** | **18** | unmasked: extra open ids |
| **2,0** | **15** | unmasked: ledger open, source view empty |
| 2,1 | 6 | extra open ids |
| 4,2 | 1 | extra open ids |

**Nested DEC-042 invariant holds** in this capture: interior covering with an empty ledger did not recur.

**Do not FAIL DEC-042 on `n>a`.** Pitch 12 `hs=336` `n=3 a=1` `lid=5242`: covering `328–432` (5242); ended `240–287` (5218) and `288–327` (5224) stay on later holds (`hs=528`, `hs=720`). `mmevt` includes `Off@287` and `Off@327` (`id=0`). Those Offs were not removed from the ledger. Same extra ids persist across occupies.

All mismatches are `cu=0`, so this snapshot does not show catch-up running. Successor: [`overdub_occupy_catchup_open_note_stack_bugfix.md`](overdub_occupy_catchup_open_note_stack_bugfix.md) — per-phase Off then On in `applyOpenClosedInterval`.
