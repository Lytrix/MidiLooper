# Reopened — 2026-06-22

**Status:** Active (was parked 2026-06-20)

## Why reopened

Edit HITL serial analysis (`host_midi_automation_edit_baseline_20260622_015537` vs `015318`)
confirmed **D1 live record display** regression: `#CAP DISP` shows growing `loopLen` with
`take>0` but `frameNotes==0` for the entire RECORD window in the failing-on-display run.

Spike notes: [BUG.md](./BUG.md) patch history 2026-06-22.

## Apply order (unchanged)

1. Spike D1 serial — **done** (see BUG.md)
2. **D3** length-mode leak (P0 stretch on reselect)
3. **D2** edit display refresh
4. **D1** firmware fixes (branch order, revision bump, open-note tails)
5. HITL `live_record_display` gate + docs
