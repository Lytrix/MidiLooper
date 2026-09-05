# Occupy-paint 191913 findings — four sibling Bugs

**Status:** Stage 0 revert shipped. Bug 2 On-tick compose path pin in progress (overrides 4→1→2 for this investigation only).  
**Date:** 2026-09-05  
**Kind:** bugfix  
**Authority capture:** [`captures/session_20260905_191913.log`](../../captures/session_20260905_191913.log) / [`captures/host_midi_hitl_occupy_paint_20260905_192033.json`](../../captures/host_midi_hitl_occupy_paint_20260905_192033.json)  
**Parked pairing finding:** [`175026`](../../captures/session_20260905_175026.log) `52@664`  
**Parent:** [`overdub_paint_shorten_live_stop_bugfix.md`](overdub_paint_shorten_live_stop_bugfix.md)

**Does not authorize:** NoteId reconstruct pairing fallbacks; `applyCapturePlayheadTails` patch; occupy On / `holdStart-1` change; merging Bug 3 with RC2 Hide-over-Shorten; treating 2304 as a bad seed

---

## Stage 0 — RC3 pairing reverted

`44f47e94` is reverted. Reconstruct Off walk is the exact pre-commit LIFO / wrap pair. No fallbacks.

Parked diagnostic `diagnostic_only_reconstruct_175026_tagged_off_pairing` (`TEST_IGNORE`): LIFO today = 144; desired later = 376. **Not an invariant.**

### Post-revert baseline gate

Fill after the revert commit’s host tests. Do not start Bug 4 until this table is recorded.

| Check | Result |
|---|---|
| `NoteUtils.cpp` vs `44f47e94^` | identical (`git diff` empty) |
| `diagnostic_only_reconstruct_175026_tagged_off_pairing` | SKIPPED — `Parked 175026 pairing finding. LIFO today=144; NoteId pair is not an invariant.` |
| idle-slice `39@192` | PASS expect 1 (`test_idle_slice_stale_overlay_rejects_and_uses_committed_fallback`) |
| RAM1 locals | **6848** (`teensy41-capture-serial`) |
| `pio test -e native` | 1651 passed, 1 skipped (diagnostic), 1 pre-existing fail `test_same_pass_playhead_tails_keep_closed_bar_four_end` (Expected 2976 Was 2296). Not this revert. |
| 191913 Bugs 1–4 still independently named | yes |

```text
44f47e94 reverted
        ↓
known baseline restored
        ↓
175026 pairing mismatch returns
        ↓
191913 Bugs remain independently observable
```

---

## Order

Bug 4 → Bug 1 → Bug 2 → Bug 3. One stage-commit each. Reassess reconstruct pairing only after those owners.

**This slice:** Bug 2 On-tick draw-source path pin only. Do not start Bug 4, Bug 1, Bug 3, or Bug 2 firmware.

---

## Bug 4 — HITL rejects a valid non-3072 loop

**Failure:** Baseline reports `record_loop_length_mismatch` (2304 vs 3072) on a sealed loop occupy-paint must accept. `verify_occupy_paint` falls back to `4 * TICKS_PER_BAR` when length parse misses.

**Owner:** Host baseline record-length check + `scripts/hitl/verify/occupy_paint.py` (`parse_loop_length` / `4 * TICKS_PER_BAR` fallback)

**Invariant:** Occupy-paint and baseline occupy/display gates score the actual sealed loop length and tempo. A 2304-tick loop is a valid seed.

**Boundary:** Do not change firmware loop length to 3072. Do not drop the OD1 `shorten=0 hide=0` gate (Bug 3).

---

## Bug 1 — Draw-source frame omits playing notes

**Failure:** At OD2 open, visual cache has 21 notes and the painted frame has 15. `hidden=0`.

**Owner:** `DisplayManager::resolveDisplayNotesByDrawSource`

**Invariant:** Within the existing draw-window eligibility rules, every visual-cache note that is drawable and still sounding must appear in the painted frame unless an effective Hide removes it. Not `every visualCache row → frame`.

**Evidence:** `#CAP,201346846,DIAG,draw_src,layers,visual,21,...,frame,15,hidden,0`

**Boundary:** Not occupy On. Not reconstruct pairing.

---

## Bug 2 — Occupy Hide not painted; playhead lengthens the note

RC1 continuation. Occupy classification stays trusted.

**Failure:** Notes that occupy classified for Hide/Shorten stay on the roll and grow with the current tick. Shorten to incoming On `holdStart-1` is not painted at that On.

**Owner:** `Loop::applyPendingHideAndShortenToNotes`

**Invariant:** After occupy writes Hide for a present-at-hold `NoteId`, that painted row is gone. After Shorten, the painted end is `holdStart-1` for that same id. The row must not follow `applyCapturePlayheadTails`. The Shorten must be in the next draw-source frame after that On, not on a later dirty-bar idle walk.

**Authority pin [`193815`](../../captures/session_20260905_193815.log)** (3072-tick loop, one OVERDUBBING session, second loop after wrap):

| Clock | Fact |
|---|---|
| `#CAP,40111972` | occupy `63` `n=1 a=1 as=1536 ae=2208 hs=1920` — Shorten end must be **1919** at this On |
| next `draw_src` | no `63@1536` Shorten |
| entire OD `draw_src,diff` | one unique change: `id=7` `78@288` **320→336** (lengthen 16), always `bar,0` |
| `draw_src,layers` | `visual,63` the whole pass; `hidden,0` |
| `#CAP,43113929` | `oh` `shorten,2 hide,2` — first wrap after that On (`emitOverlapHoldTotals` is wrap/stop only). Pending existed between occupy and wrap |
| `VCACHE,stale_range` | `first,0 last,3 total,4` — dirty bars refresh later, not at the On |

Occupy selected the overlapping span. The frame never paints `holdStart-1`. The only live geometry change is a bar-0 lengthen. Later bar refreshes (user: bar 4, then 1, then 3, then 2) are the dirty-bar walk, not On-tick realization.

Host pin `test_pending_shorten_193815_63_1536_hold_1919_matrix` records what `applyPendingNoteChangesToDisplayNotes` does for each device-shaped paint (pending Shorten id=40 `1536→1919`):

| Paint | After apply |
|---|---|
| same NoteId, closed `1536-2208` | end **1919** (RC1 already) |
| `kInvalidNoteId`, closed | stay **2208** |
| same NoteId, open hold `1920/1920` | skip |
| same pitch+start, other id | stay **2208** |

Occupy geometry is matrix row 1 (closed `1536–2208`). **Live painted `NoteId` is not in `193815`** (DNTE has no id; this occupy has no `lid`/`mismatch`; no `srcspan` for `p=63`). Stopped effective/frame `1919` is not proof the live row carried the occupy id.

Host pin `test_draw_source_compose_193815_63_1536_forks` runs the draw-source sequence (prefix + overlays + `applyPendingNoteChangesToDisplayNotes` + overdub tails). Host stand-in ids label layer shapes only.

| Fork | Host compose | Device? |
|---|---|---|
| A — `63@1536` absent at apply | empty prefix + open `1920/1920` only → no `1536` after pending or tails | not named — live frame dump missing |
| B — live id ≠ occupy id | closed `1536–2208` with invalid/other id stays **2208** | not named — live `NoteId` missing |
| C — pending 1919, tails emit differs | same-id closed → **1919**; tails grow `1920` only; `1536` stays **1919** | **false** for rewriting `1536–2208` |

Device recapture [`202821`](../../captures/session_20260905_202821.log) (`teensy41-capture-serial-investigation-diag`):

| Clock | Paint `63@1536` |
|---|---|
| first-loop On `#CAP,21219138` `hs=1536` `n=0` | `present=1` `id=0` (`kInvalidNoteId`); `after_pending` end **1536**; `after_tails` grows 1640…2200 |
| after Off | `id=0` end **2208** |
| `#CAP,25220593` `hs=0` `n=0` | `id` becomes **73**, end still **2208** |
| occupy `#CAP,30465195` `n=1 a=1 as=1536 ae=2208 hs=2016` | next `after_pending` and `after_tails`: `present=1` `id=73` end **2015** (`holdStart-1`) |

| Fork | Device at occupy On |
|---|---|
| A — row absent | **false** (`present=1`) |
| B — live id ≠ occupy id | **false** at this On (`id=73`, end **2015**) |
| C — tails overwrite Shorten | **false** (pending 2015, tails 2015) |

On-tick `applyPending` painted Shorten when the row carried NoteId 73. First-loop `id=0` + tails growth is the open-hold skip, not this occupy miss. Do not patch tails. Owner stays `Loop::applyPendingHideAndShortenToNotes`. No owner move.

**Boundary:** Do not change occupy On or `holdStart-1`. Do not reopen RC3 pairing. Not Bug 4 (this loop is 3072).

---

## Bug 3 — Source-pass seal invents Shorten

**Failure:** OD1 live occupy is `n=0` / `oh` 0/0. OD1 stop seal reports `shorten=4`.

**Owner:** `Loop::sealPendingNoteChangesToEditPasses`

**Invariant:** A source pass that never occupied must seal with shorten=0 hide=0.

**Evidence:** `#CAP,195977633,DIAG,oh,...,shorten,0,hide,0` then `#CAP,200699695,DIAG,oh,...,shorten,4,hide,0`

**Boundary:** Invented Shorten, not RC2 Hide-over-Shorten. Not reconstruct.
