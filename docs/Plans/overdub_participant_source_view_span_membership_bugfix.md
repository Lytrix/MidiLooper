# Source-view membership from prepared NoteSpans

**Status:** Native span fill `b94bd2b`. Device [`024225`](../../captures/session_20260818_024225.log): prepared A==B; empty span copy wiped overdub display — fallback in this commit. 021716 storage-64 pin not exercised.
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
      ← this slice
 → occupy / consume / notePresentAt / B collect
      ← do not reopen
```

When prepared is ready, A consumes those `NoteSpan`s. MIDI reconstruct (`finishOpenNotes=false`) plus `appendOverdubPassWrapPairedNotes` is fallback only and must not imitate span finish semantics.

Do **not** fold occupied storage-576 into this slice until pre-wrap `A==B` is proven on device.

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

---

## Hard don'ts

- Phase 3 consume-from-LCR / `tryCollectPreparedPresentNoteIdsAtTick` as consume
- Wrap pairing on merged record+overdub (015618)
- Changing B / `notePresentAt` / occupy membership
- Record-pass wrap fill as the 021716 pre-wrap fix
- Teaching the unprepared MIDI path to finish opens or imitate `NoteSpan` rebuild
- Finishing opens on a partial 16-bar window
