# Source-view membership from prepared NoteSpans

**Status:** Native span fill `b94bd2b`; empty-copy fallback `81ce13a`; window-noteId span copy; Hide-all-spans; B collect Active/window parity (032228 wrap 6–7). Device RC12 flood **PASS**. 021716 storage-64-before-wrap is not a blocking gate.
**Date:** 2026-08-18  
**Kind:** bugfix  
**Parent:** [`overdub_participant_loop_content_architecture.md`](overdub_participant_loop_content_architecture.md)  
**Evidence:** [`021716`](../../captures/session_20260818_021716.log) — 1-bar (`DISP` `OVERDUBBING,768`); before first `why=wrap`, `notes=10`, storage 64 pitch 86/60 `a=0,b=2`, hold `from=win` `merged=0`  
**Does not start:** Phase 3 consume-from-LCR; wrap pairing on merged record+overdub; occupied storage-576; finishing opens on a partial 16-bar window

---

## Debugging boundary

```text
… → prepared checkpoints (finishCanonicalSpansFromMidi + later publish)
      ← trust span membership unless prepared miss
 → rebuildOverdubSourceView fill of overdubSourceViewNotes_
      ← shipped (span copy + window-NoteOn filter)
 → projectSealedCompanionsOntoCheckpoints Hide/Shorten
      ← shipped (every checkpoint span with targetNoteId)
 → tryCollectPreparedPresentNoteIdsAtTick
      ← this slice: skip index orphans and Disabled-pass companion restore
 → occupy / consume / notePresentAt
      ← do not reopen
```

When prepared is ready, A consumes those `NoteSpan`s. MIDI reconstruct (`finishOpenNotes=false`) plus `appendOverdubPassWrapPairedNotes` is fallback only and must not imitate span finish semantics.

Occupy uses A. Do **not** put B extras into A (030219 flood). Do **not** remove the window-NoteOn anti-flood filter. 021716 storage-64-before-wrap is not a blocking gate.

---

## Invariant

Prepared state is authoritative when available; MIDI reconstruction remains the fallback when it is not.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — `Loop::rebuildOverdubSourceView` still owns `overdubSourceViewNotes_` |
| **State transition change?** | NO |

Reuse: YES — extend `rebuildOverdubSourceView`; add `LoopContentResolution::tryCopyPreparedSpansToDisplayNotes`. Same membership B already walks. Occupy still reads A after rebuild.

---

## Root cause

Wrap-paired overdub fill attacked a **consequence**. [`021716`](../../captures/session_20260818_021716.log) diverges **before the first wrap**:

```text
prepared checkpoint
       │
       ├── B → NoteSpans → sees open finished note
       │
       └── A → MIDI reconstruct (finishOpenNotes=false)
                    ↓
                  misses it
```

`merged=0` on `why=hold,from=win` means `Loop::ensureOverdubSourceNotesForHold` reconstructed live passes and still did not produce those extras. They exist only on prepared `NoteSpan`s. Occupy uses A; A empty → occupy empty → B extras are never Hidden.

On a 1-bar loop `wrapTailStartTick(768) == 0`, so default wrap pairing is not blocked. Extending wrap-paired fill to `RecordPass` cannot create notes that live reconstruct already failed to produce.

---

## Fix

```text
rebuildOverdubSourceView()
    │
    ├── preparedWindowReady
    │      ↓
    │   NoteSpans → DisplayNotes
    │
    └── no prepared window
           ↓
        existing MIDI reconstruction
           +
        wrap-paired fallback
```

Prepared fields used: `startTick`, `endTick`, `noteId`, pitch. One `DisplayNote` per span (wrap head+tail may share a `NoteId`). Hide encodes `endTick == startTick`; those rows are omitted so occupy can drop the id. After wrap, `publishPreparedOverdubPass` already appends isolation spans before rebuild; prepared fill picks up wrap Adds. Wrap-paired fill stays on the visual-cache path and on the unprepared fallback only.

---

## Native pins

| Pin | Fixture | After this slice |
|-----|---------|------------------|
| Finished open covering 64 | unpaired `ON@10` id 2 | A==B `[2]` before wrap |
| Complete wrap pair covering 64 | `ON@720 OFF@96` | A==B `[1]` (already was; not the finished-open class) |
| Seven unpaired ONs covering 64 | pitches 60–66, ids 1–7 | source-view count 7; A==B per pitch |
| Prepared vs unprepared open tail | `72@80` no OFF | prepared includes `endTick == loopLength-1`; unprepared still omits |
| Window noteIds only | two record notes; copy with only id 1 in `windowEvents` | id 2 span dropped |
| Wrap-pair Hide at 64 | `ON@720 OFF@96` id 1, 1-bar, Delete id 1 | A and B empty of 1 at tick 64 |
| Undo to record then rewrap | same-pitch wraps, two session undos, occupy again | A==B; B omits Disabled wrap ids |
| Existing wrap-crossing / occupy+publish | unchanged fixtures | A==B stays |

---

## Device gate

1-bar overdub like 021716; `eq=1` at storage 64 **before** first wrap. Then confirm wrap/occupy pins still hold. Occupied extras after wrap should shrink because occupy can now Hide the pre-wrap ids. Leave storage-576 as a later class.

### [`024225`](../../captures/session_20260818_024225.log) — 2026-08-18

1-bar (`DISP` `OVERDUBBING,768`). Loop was cleared to **1** visual note before overdub (`DISP` 31→1, then `PLAYING` 1). `COORD` `storage,64` count = **0**.

| Fact | Count |
|------|-------|
| `lcr,part` | 122 |
| `from=prep` `eq=1` | 110 (`a=b` in {0,1,2,3}) |
| `from=prep` `eq=0` | **0** |
| `from=miss` | 12 (sessions 3–4; `why=open,from=win`) |
| Pre-wrap `from=prep` | 15, all `a=0,b=0,eq=1` |
| First `why=open` | `from=span,ev=2,notes=0` |
| First `why=wrap` | `from=span,ev=30,notes=14` |
| `lcr,mat` | 1, at 12.9 s (`notes=30`); later `vch` after delete is `ev=2,notes=1` |

RC12 paints `overdubSourceViewNotes_` while OVERDUBBING. Empty span copy made `DISP` `PLAYING` 1 → `OVERDUBBING` 0. Visual cache still had the record note (`VCACHE` `notes=1` at first wrap). After wraps, isolation spans filled A (`notes=14`) and undos rebuilt source view; STOPPED used visual cache and matched. Empty copy is a miss: `tryCopyPreparedSpansToDisplayNotes` returns false when every span is skipped; rebuild falls back to MIDI reconstruct of the gathered window (`ev=2` → the record note).

Prepared identity on this gesture is clean. It does **not** prove the 021716 pre-wrap finished-open pin (`a=0,b=2` at storage 64 on a 14-note STOPPED loop). Re-run that gesture without clearing to one note. Do not start Phase 3. Do not fold storage-576 into this slice from this log.

### [`025337`](../../captures/session_20260818_025337.log) — display wipe PASS

1-bar (768). Firmware `81ce13a` empty-copy fallback.

| Fact | Value |
|------|-------|
| First `why=open` | `from=win,ev=2,notes=1` (not `from=span,notes=0`) |
| `DISP` enter | `PLAYING` 1 → `OVERDUBBING` 1 |
| Session 2 `why=open` | `from=win,ev=14,notes=8`; `DISP` `8,10,8,8` (painted matches A) |
| `lcr,part` | 69; `from=prep` `eq=1` **60**; `eq=0` **0**; `from=miss` 9 |
| Pre-wrap `from=prep` | 5, all `a=1,b=1` |
| `COORD` storage 64 | 11, all **after** first `why=wrap` |

Record layer stayed on screen at enter. Prepared A==B still clean. The 021716 storage-64-before-wrap pin remains open.

### [`025916`](../../captures/session_20260818_025916.log) — storage 64 before wrap, prepared miss

1-bar (768). Loop was **1** visual note (`DISP` STOPPED/PLAYING/OVERDUB enter all `1`), not 021716's 14-note STOPPED population.

| Fact | Value |
|------|-------|
| First `why=open` | `from=win,ev=2,notes=1` |
| `DISP` enter | `PLAYING` 1 → `OVERDUBBING` 1 |
| `COORD` storage 64 before first wrap | **2** — `abs,64` pitch 74 / 48 |
| Those holds | `from=miss,a=0`; source `notes=1` `merged=0` |
| Pre-wrap `lcr,part` | 5, all `from=miss` |
| First `why=wrap` | `from=span,ev=10,notes=35`; `DISP` 6 → 35 |
| After wrap, storage 64 | `from=prep` `eq=1` (74 `a=5,b=5`; 48 `a=4,b=4`) |
| Session totals | 27 `lcr,part`; 22 `from=prep` `eq=1`; **`eq=0` = 0**; 5 `from=miss` |

Display wipe still holds. Storage 64 before wrap was exercised, but prepared was not ready, so there is no A vs B at that tick. 021716's pin is `from=prep` `a=0,b=2` at storage 64 on a populated loop (`notes=10`, STOPPED `DISP` 14). Re-run that gesture: do not clear to one note; hold at storage 64 only after `from=prep` appears, before the first wrap. Do not start Phase 3. Do not fold storage-576 into this slice.

### [`030219`](../../captures/session_20260818_030219.log) — same miss class; pitches match 021716

1-bar (768). Loop still **1** visual note. Pitches at storage 64 are **86 / 60** (same as 021716), `abs,832`.

| Fact | Value |
|------|-------|
| First `why=open` | `from=win,ev=2,notes=1` |
| `DISP` enter | `PLAYING` 1 → `OVERDUBBING` 1 |
| Storage 64 before first wrap | `from=miss,a=0`; source `notes=1` |
| Pre-wrap `lcr,part` | 6, all `from=miss` |
| First `why=wrap` | `from=span,ev=8,notes=34`; `DISP` 6 → 35 |
| After wrap, storage 64 | `from=prep` `eq=1` (86/60 `a=1,b=1` then `a=2,b=2`) |
| Session totals | 27 `lcr,part`; 21 `from=prep` `eq=1`; **`eq=0` = 0**; 6 `from=miss` |

Same miss as 025916. Does not close 021716. Gate remains: STOPPED `DISP` ~14, source `notes` ~10, `from=prep` at storage 64 **before** `why=wrap`, expect `eq=1`.

No `lcr,mat` in this capture — device gate did not rebuild for the 1-note loop. Wrap `publishPreparedOverdubPass` restamps leftover checkpoints; RC12 then paints them:

| After | `lcr,src` (RC12) | `lcr,vch` (MIDI of same window) |
|-------|------------------|----------------------------------|
| wrap 1 | `from=span,ev=8,notes=34` | `ev=8,notes=4` |
| wrap 2 | `from=span,ev=10,notes=35` | `ev=10,notes=5` |
| undo | `from=span,ev=8,notes=34` | `ev=8,notes=4` |

**Fix:** `tryCopyPreparedSpansToDisplayNotes` keeps only spans whose `noteId` is a NoteOn in the gathered window, skips index orphans, and misses on loop-length mismatch. Finished opens that are in the window MIDI still copy (021716 native pins). Stale checkpoint / companion rows that are not in `ev` do not.

### [`030958`](../../captures/session_20260818_030958.log) — RC12 flood PASS

1-bar (768). Capture starts already OVERDUBBING (no `why=open`). STOPPED earlier `DISP` 6. Device gate did complete (`lcr,mat` at 11.97 s). Firmware `e86a2ce` window-noteId filter.

| After | `lcr,src` | `lcr,vch` | `notes` delta |
|-------|-----------|-----------|----------------|
| wrap 1 | `from=span,ev=28,notes=14` | `ev=28,notes=14` | **0** |
| wrap 2 | `from=span,ev=28,notes=15` | `ev=28,notes=15` | **0** |
| later wrap/undo | 15–23 | 15–22 | 0 or **+1** |

Not the 030219 34-vs-4 leftover dump. The occasional +1 is one extra finished-open span vs MIDI reconstruct, not a prior-run pile.

`COORD` storage **64** count = **0**. Pre-wrap holds are storage 384/432/480, `from=prep`; one pre-wrap `eq=0` (pitch 72 `a=1,b=2`). Session: 77 `lcr,part`, all `from=prep`; 34 `eq=1`, **43 `eq=0`**. Always `a < b` with `ao=0` `bo>0`. 021716 pin stays open. Occupied storage-576 appears after wrap 1 (`abs,5184`) — later class. Do not start Phase 3.

030958 `eq=0` is not leftover flood. On a 1-bar loop the source window is the full loop. Wrap head+tail share a `NoteId`; `projectSealedCompanionsOntoCheckpoints` used to Hide only the first matching span. The leftover tail stayed in B; window flatten dropped the NoteOn so A’s window-NoteOn filter dropped the id (`a=1,b=2,bo=1`). Hide/Shorten now walks every checkpoint span with `targetNoteId` and records one `PreparedCompanion` from the first original. B collect and the window-NoteOn filter stay unchanged.

### [`032228`](../../captures/session_20260818_032228.log) — flood PASS; wrap 1–4 occupy mostly `eq=1`; wrap 6–7 `eq=0` returns

1-bar (`DISP` `OVERDUBBING,768`). Capture starts already OVERDUBBING (no `why=open`); first OVERDUB `DISP` notes **5**. 58 `lcr,part`, all `from=prep`. No pre-wrap parts. Device gate completed (`lcr,mat` early). Firmware Hide-all-spans `7760811`.

| Gate | Result |
|------|--------|
| wrap `lcr,src notes` vs next `lcr,vch notes` | **PASS** — all 7 wraps delta **0** (5=5, 5=5, 10=10, 14=14 ×4) |
| occupied `eq=0` after wrap 1+ | **partial** — wrap 1–4: **1 / 34**; session **17 / 58** (030958 was 43 / 77) |
| 021716 storage-64 before wrap | **not this capture** |

| After wrap | `lcr,part` | `eq=1` | `eq=0` |
|------------|------------|--------|--------|
| 1 (`notes=5`) | 6 | 6 | **0** |
| 2 | 10 | 9 | 1 (`60` `a=1,b=2` storage **528**) |
| 3 (`notes=10`) | 9 | 9 | **0** (`a=0,b=0` at 528/576/0/96/…) |
| 4 (`notes=14`) | 9 | 9 | **0** (`a=1,b=1` at those same ticks) |
| 5 | 9 | 8 | 1 (`72` `a=0,b=1` storage **480**, src `notes=5` after undo) |
| 6 | 9 | 0 | **9** |
| 7 | 6 | 0 | **6** |

Every `eq=0` is `ao=0` `bo=1`. Wrap 6–7 extras sit on the same storage ticks that were `eq=1` on wrap 4 (528, 576, 0, 96, 192, 240, 384, 432, 480), including occupied **576**. Storage **64** appears twice after wrap 1 (pitch 86 and 60) and both are `a=1,b=1`. STOPPED `DISP` 15.

A matches `lcr,vch`. Occupy is not missing window-visible ids. B restored Disabled wrap-layer companion originals (and index orphans) that A already omitted. `tryCollectPreparedPresentNoteIdsAtTick` now skips `found==nullptr` spans, skips Disabled-pass companion restore, and skips Disabled companion restore when an Active companion already targets that `NoteId`. Native: `test_prepared_hold_ids_pin_undo_to_record_then_rewrap_a_equals_b`. Phase 2a undo still restores an Active wrap id (`10`). Do not start Phase 3. Do not consume B.

---

## Hard don'ts

- Phase 3 consume-from-LCR / `tryCollectPreparedPresentNoteIdsAtTick` as consume
- Wrap pairing on merged record+overdub (015618)
- Putting B extras into A / occupy membership / `notePresentAt`
- Removing the window-NoteOn anti-flood filter to chase A==B
- Record-pass wrap fill as the 021716 pre-wrap fix
- Teaching the unprepared MIDI path to finish opens or imitate `NoteSpan` rebuild
- Finishing opens on a partial 16-bar window
