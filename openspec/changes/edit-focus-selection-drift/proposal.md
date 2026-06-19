# Proposal — edit focus selection drift (HITL bug, parked)

**Change:** `edit-focus-selection-drift`  
**Kind:** bug  
**Status:** Recheck complete — see [note-edit-hitl-focus-restore](../note-edit-hitl-focus-restore/)  
**Parent:** [note-edit-modification-session](../note-edit-modification-session/)  
**Triggered by:** [note-edit-focus-reads](../note-edit-focus-reads/) Phase **2a** smoke HITL (`235109`)

## Why

Phase 2a replaced **movingNote** UI reads with **focus.last** in fader paths. Native tests stay green and Phase 1 AC1/AC2/AC4 pass on the pre-2a capture, but a **fresh post-upload** edit baseline shows **selection/focus drift** during the long scenario: delete targets the wrong note and overlap round-trip home fails.

Do **not** patch during 2a/2b/2c/2d unless a change clearly introduces the regression. **Recheck** full edit baseline once all focus-read phases land.

## What (recheck only)

- Full `host_midi_automation_edit_baseline.py` + serial verification
- `verify_overlap_hidden_ac.py` on the same serial log (AC3/AC5 failed on `235109`)
- Confirm whether drift is fixed by 2b encoder bridge, 2c overlap writer retirement, or needs a dedicated fix

## Non-Goals (until recheck)

- Root-cause patch loop parallel to Phase 2b–2d
- Reopening Phase 1 overlap-hidden select contract

## Evidence

[BUG.md](./BUG.md)
