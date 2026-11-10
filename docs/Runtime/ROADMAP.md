# Roadmap (future sequencing)

**Informational only — not implementation authority.** Agents must **not** derive scope from this file. Implement only what [CURRENT_WORK.md](CURRENT_WORK.md) allows.

For shipped vs next in firmware: [DELIVERABLE_TRACKING.md](../DELIVERABLE_TRACKING.md). For OpenSpec process: [DELIVERY_RULES.md](../Authority/DELIVERY_RULES.md).

Last updated: 2026-11-10 (sets browser future features)

---

## Milestone sequence (post-M7, locked order)

| Order | Milestone | Status |
|-------|-----------|--------|
| Done | M1–M7 epoch cutover | Shipped |
| Done | M8 edit, pool-budget, loop-ownership, long-record-memory-headroom | Archived → `openspec/specs/` |
| **Now** | Persistence / overlay / Set revisions | See [CURRENT_WORK.md](CURRENT_WORK.md) |
| Next | Hardening — playback-window polish | Propose OpenSpec when persistence archives |
| Then | JamRecorder — JamAction / D3 | Not D13 MIDI capture |
| Then | M10 — Jam entity + Scenes + SD | After jam-recorder |
| Last | D13 arrangement capture | After R1–R5 in [phase-3-multi-loop.md](../Plans/phase-3-multi-loop.md) |

## Dependencies

| Goal | Blocked until |
|------|----------------|
| D13 jam capture | JamRecorder + M10; R1–R5 resolved; DEC-003 |
| Scenes row firmware | M10 entity + SD model |
| GPIO base module parity | Surface-agnostic Actions layer (DEC-005) |
| `currentset-savedset-storage-layout` | Revision model stable or superseded |

## Future goals (not scheduled)

- Phase 3 jam/arrangement capture into slots
- Encoder + 4-button base module revival
- CC value editing on faders (MVP table in DELIVERABLE_TRACKING)
- Portability: Drumboy Pro platform split (PROJECT_INTENT decision 5)
- DROID LFO pulse slot feedback
- Engine sets browser — **get a loop: add as layer** (owner 2026-11-10): besides USE (replace), merge the picked loop
  onto what the destination holds — a new pass on its current state, overdub overlap rules, the slot keeping its loop
  length (the play window, DEC-257, covers a longer source); one undo step. The picker (DEC-272) offers it on a filled
  destination. Design: browser plan §6 / §9.
- Engine surface — **encoder CC menu** (owner 2026-11-10): the encoder's long push opens a CC menu — choose a CC, then
  turn the encoder to set it — together with a CC mode and its view.
- Engine sets browser — **library compaction** (owner 2026-11-10): reclaim the card space emptied sets and stages leave
  (their blobs stay in the log, DEC-268) and the index records the trash marks spend (4,096 in all; EMPTY TRASH itself
  needs one). Needs a power-safe scheme for rewriting the log and index (older records, parent references and the
  recovery checkpoint kept valid); two index copies are one candidate, not yet a requirement. Design: browser plan §9.

## Notes

- Parked OpenSpec: `archive/20260617-parked-jam-recording-d13/` — do not start early
- Historical `docs/Plans/*_handoff.md` may describe completed or abandoned slices — not the live queue
- Active OpenSpec folder list: [PROJECT_STATE.md](PROJECT_STATE.md) (operational), not this roadmap
