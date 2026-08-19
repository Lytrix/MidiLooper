# Occupy open-NoteOn ledger cardinality

**Status:** Native shipped. Firmware links; RAM1 code **425804** / locals **4768** (baseline 425852/4768). HITL gate open.  
**Date:** 2026-08-19  
**Kind:** refinement  
**Decision:** [DEC-042](../DECISION_LOG.md#dec-042-same-pitch-active-note-identity-is-a-cardinality-problem-not-an-off-identity-problem)  
**Parent:** [`overdub_occupy_unmatched_off_ledger_investigation.md`](overdub_occupy_unmatched_off_ledger_investigation.md)  
**Evidence:** [`092336`](../../captures/session_20260819_092336.log) pitch 12 nested `On@96` / `On@144` / `Off@192`

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
