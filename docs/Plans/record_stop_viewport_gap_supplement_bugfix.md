# Record-stop viewport gap — low-bar supplement (GAP-8c)

**Status:** **REVERTED (2026-09-12)** — device regression [`204310`](../../captures/session_20260912_204310.log); **architectural design session required** before re-implementation  
**Kind:** bugfix (display-only, publication-gap UX) — **frozen at revert**  
**Parent:** [`record_stop_snapshot_publication_gap_display_bugfix.md`](record_stop_snapshot_publication_gap_display_bugfix.md) — stage 8c  
**Revert commit:** `3c9dcd4a` (reverts `c16ec598` — GAP-8c + bundled GAP-8b)

---

## Revert reason (device)

Post-`c16ec598` capture [`session_20260912_204310.log`](../../captures/session_20260912_204310.log):

| Symptom | Evidence |
|---------|----------|
| Notes disappear on record stop | `DISP` STOPPED: `win_notes` **22 → 15** (`176795854` → `178277964`) after `RECORDING → STOPPED_RECORDING` (`176623898`) |
| Beat/grid regression | `BAR,376,0` at `176736198` immediately after stop (tick **376** at bar **0** while loop length is 15360) |

Native fixtures passed; device showed broader display/transport harm than the targeted bars 2–4 gap. **Do not re-apply the passes-fallback supplement without a design session** that separates record-stop viewport handoff from overdub stale-root (GAP-8b) and proves no STOPPED-state frame loss.

---

## Original symptom (pre-revert target)

After **record stop → PLAYING**, bars **2–4** briefly empty until session-exit publish. Anchor: [`session_20260912_202721.log`](../../captures/session_20260912_202721.log).

---

## Debugging boundary (unchanged intent)

```text
gap handoff retain (GAP-8 / GAP-8b)
  → supplement uncovered bars   ← GAP-8c was here (REVERTED)
  → paint
```

**Out of scope until design:** `refreshViewportAfterRecordStop` policy change; sync publish on stop.

---

## Reverted implementation (for design reference only)

- `displayBarAdequatelyCoveredForGapSupplement` + `gapSupplementBarLeftEdgeCoverageTicks`
- `supplementDrawSourceFromUnpublishedSnapshotForUncoveredBars`: unpublished snapshot, then `gatherCommittedDrawSourceBars` `SessionExitGapPasses`
- Bundled **GAP-8b:** `isPlaybackSessionExitPublicationGap` stale-root predicate

---

## Next step

**Design session** — ownership, when supplement may run (STOPPED vs PLAYING only), interaction with `shouldHoldSessionExitPublicationGapHandoff`. **GAP-8b also reverted** (§6.6.5) — do not re-land without design session.
