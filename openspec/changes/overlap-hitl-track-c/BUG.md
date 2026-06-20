# BUG — Overlap HITL Track C (insert / M0 home / split-overlap)

**Change:** `overlap-hitl-track-c`  
**Parent:** `m8-edit` §4.7 (full HITL edit baseline — M8 pass verify **OK**, Track C **fail**)  
**Primary capture:** `20260620_161431` (+ `_serial.log` sibling)  
**Prior green reference:** `20260619_203729` (Track B verifier timing fix; insert still open)

---

## User-visible symptom

Full **note-edit HITL baseline** completes all MIDI steps and passes **M8 pass verify**, but three sub-verifiers fail:

| # | Verifier | Issue keys | Report `ok` |
|---|----------|------------|-------------|
| 1 | `_verify_delay_move_insert_reorder` | `insert_not_restored_after_move_back`, `insert_missing_after_in_edit_redo` | false |
| 2 | `_verify_long_over_short_pitch_restore` + `_verify_change_length_store_rebuild` | `m0_not_home_after_round_trip`, `after_overlap_round_trip_home:native_m0_count:0!=1`, `after_overlap_round_trip_home:native_m0_pitch_67_count:0!=1` | false |
| 3 | `_verify_split_overlap_note_round_trip` | `split_overlap_note_mover_not_home`, `split_victim_mover_not_home` | false |

JSON: [`captures/host_midi_automation_edit_baseline_20260620_161431.json`](../../../captures/host_midi_automation_edit_baseline_20260620_161431.json)

---

## Serial proof — capture `161431`

### Track 1 — insert / reorder

| Step | Serial line | Event |
|------|-------------|--------|
| Insert create | 7831 | `Created 32nd note (pitch=60, tick=48-72)` (second insert) |
| Move P0 over insert | 7938 | `POSITION EDIT: step 24 -> 1 (tick 1168 -> 64)` |
| Temp delete insert | 7997 | `Temporarily deleting … pitch=60 tick=48` |
| Move back off insert | 8078 | `POSITION EDIT: step 1 -> 24 (tick 64 -> 1168)` |
| **Restore on move-back** | 8107–8110 | `Restoring hidden overlap note: pitch=60, start=48, end=72` — **insert restored in store** |
| Verifier gate | — | Looks for `Restoring deleted note` or `Will restore note` — **string mismatch** |
| In-edit undo | 11125–11150 | `Note edit pass undone` → recon **7 notes** (record baseline) |
| In-edit redo | 11950–11978 | `Note edit pass redone` → recon **6 notes** (insert @48 present but B@208 missing) |
| Verifier redo gate | — | Requires `redo_count > undo_count` after `Overdub redone` — **fails 6 > 7** |

**Track 1 split:**

- **1A (verifier):** restore log alias — firmware uses `Restoring hidden overlap note`, not `Restoring deleted note`.
- **1B (firmware):** global pass redo rematerialize drops committed notes (6 vs ≥8 expected with insert + edits).

### Track 2 — M0 home + native parity at overlap round-trip home

| Step | Serial line | Event |
|------|-------------|--------|
| Round-trip home move | 6185 | `POSITION EDIT: step 2 -> 0 (tick 112 -> 16)` — focus pitch **67** |
| Pre-move scratch recon | 6214, 6238 | `Reconstruction complete` with `Final note: pitch=67, start=**112**, end=784` — tagged `position_edit:112->16` |
| Post-move store | 6242–6243 | `Moved note events: pitch=67 start->16 end->688`; `#CAP,DNTE,67,16,16,672,3` — **correct home** |
| Verifier home snap | — | `_snapshot_position_edit_to_home` picks **first** `Reconstruction complete` after POSITION EDIT → **inventory at tick 112**, not 16 |
| Native parity @ home | — | `_assert_native_lengthen_rematerialize_parity` with `m0_pitch=67` → `native_m0_count:0!=1` |

**Track 2 root cause (leading):** HITL snapshot selection uses **pre-move scratch reconstruction** before `Moved note events` / final DNTE; not missing move on device at line 6242.

**Secondary (open):** After line 7455, M0@16 shortened to `end=399`. Native parity at home expects `long_end_tick` (step 14 → tick 672). Confirm whether home checkpoint must be **before** short-over-long B scenario or whether lengthened-end AC is still required at that checkpoint.

### Track 3 — split-overlap mover at home

| Observation | Signal |
|-------------|--------|
| Inner A at home | `inner_a_recaptured_at_home: true` (serial OK) |
| Mover at home | `mover_at_home: false` — same home snapshot as Track 2 |
| Inventory lookup | `_inventory_note_near(pitch=67, start=m0_tick=16)` → **empty** when snapshot has M0 @112 |

**Track 3 root cause (leading):** Same snapshot timing bug as Track 2; not a separate split-overlap engine gap on this capture.

---

## Ranked hypotheses

| ID | Track | Hypothesis | Evidence | Priority |
|----|-------|------------|----------|----------|
| **H1** | 2, 3 | Verifier home snapshot = pre-move scratch recon | 6238 vs 6243 | **P0** — fix verifier or add post-move recon tag |
| **H2** | 1 | Verifier restore log string stale | 8110 uses `Restoring hidden overlap note` | **P0** — quick verifier fix |
| **H3** | 1 | Pass redo rematerialize incomplete | redo recon 6 notes, missing B@208 | **P1** — firmware `NoteEditPassClosed` / `applyEdits` |
| **H4** | 2 | Native lengthen-end AC wrong lifecycle point | M0 shortened to 399 before in-edit undo | **P2** — clarify AC boundary |
| **H5** | 1 | In-edit undo uses global pass undo not session stack | `Note edit pass undone` not `NoteEditSession undo` | **P1** — user locked: session stack only (§2) |

---

## Patch history

| Date | Action | Result |
|------|--------|--------|
| 2026-06-20 | OpenSpec change opened | This BUG.md + parent Track C mapping |
| 2026-06-20 | §1 verifier fixes + log replay `161431` | All four sub-verifiers green (H1 + restore log alias) |
| 2026-06-20 | §2.1 session undo pushes | `NoteEditManager` move/length/pitch — live HITL pending |
