# Roadmap (future sequencing)

**Informational only — not implementation authority.** Agents must **not** derive scope from this file. Implement only what [CURRENT_WORK.md](CURRENT_WORK.md) allows.

For shipped vs next in firmware: [DELIVERABLE_TRACKING.md](../DELIVERABLE_TRACKING.md). For OpenSpec process: [DELIVERY_RULES.md](../00-authority/DELIVERY_RULES.md).

Last updated: 2026-08-08 (doc hygiene review)

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
| Last | D13 arrangement capture | After R1–R5 in [phase-3-multi-loop.md](../plans/phase-3-multi-loop.md) |

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

## Notes

- Parked OpenSpec: `archive/20260617-parked-jam-recording-d13/` — do not start early
- Historical `docs/plans/*_handoff.md` may describe completed or abandoned slices — not the live queue
- Active OpenSpec folder list: [PROJECT_STATE.md](PROJECT_STATE.md) (operational), not this roadmap
