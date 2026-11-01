# C4 with hides — not clean: two misattributed NoteOffs, an over-budget import step, and PSRAM exhaustion

Date: 2026-11-01. Image: probe/shadow `b63af14487ecdb16…`. Raw rows:
[`2026-11-01-c4-hides-status-raw.txt`](2026-11-01-c4-hides-status-raw.txt),
[`2026-11-01-c4-hides-windows-raw.txt`](2026-11-01-c4-hides-windows-raw.txt).

## Stimulus selection (DEC-246 amendment 10: C4 counts only with a passing gate)

| Stimulus | Own verdict on this image | Used? |
|---|---|---|
| `overdub_occupy_paint` (C3b) | FAIL: markers absent from this image; undo/redo proof | no |
| `note_edit_overdub_compose` | FAIL: "no later same-pitch live note" (its base step passed) | no |
| `edit_full` | not judged: slow and stopped by me after the owner reported the HITL may be broken | no |
| **midifile owner chain, Record and overdubs both `16ths.mid` on one channel** | **gates pass:** record 1, overdubs 12, cap 13, slot cleared; fresh boot `session_20261101_200525.log`; 0 crashes; 0 pool refusals | **yes** |

Every overdub window lands same-pitch notes on the Record. Production produced Delete (hide) companion edit passes;
the import found **33 hides** in one edit run.

## Result

| Reading | Value |
|---|---|
| Edit index | 8 published, worst step 70 µs, `IndexOnly` used it |
| Window grain | 202 calls, worst 275 µs; 100 `Ready` = 10 identities × 10 windows |
| Shadow publishes / `SourceMoved` / refusals / duplicates | 10 / 3 / 2 / 0 |
| **Import grain worst whole call** | **3,212 µs: over the declared 2,000 µs** |
| C4 windows compared / matched (identity 10, source current) | 10 / 10 |
| **Whole-loop shadow `revision()` vs production passes view** | **1,138 vs 1,136: 0 production-only, 2 shadow-only** |
| Shadow-only rows | NoteOff ch1 pitch 79 t=168; NoteOff ch1 pitch 72 t=504 |
| Production snapshot | not current (`pending_publish=1`) |

## Reading

1. **The importer misattributes NoteOffs on this shape.** The two shadow-only rows are NoteOffs of hidden notes that
   the shadow kept. Its streaming LIFO pairing in append order gave them to a different note of the same pitch, so the
   hide of the real target missed them (`unpaired_offs=0`: they were paired, just wrongly). Production's whole-loop
   gather pairs them correctly and hides both ends. The 662 host checks never contained this shape: dense same-pitch
   16ths with overdubs of the same pattern, hides by companions, and possibly wrap-crossing notes.
2. **The 10/10 window match is not evidence of parity here.** Production's *windowed* gather has the same blind
   spot: a window that does not contain a note's NoteOn keeps its NoteOff, so the windows agree on the wrong answer.
   The whole-loop comparison is the stricter oracle and must be part of C4.
3. **An import step exceeded its bound** (3.2 ms). The likely candidates are `begin` (chunk-count capacity scan plus
   sorting the edit passes) or the edit-sort/emit phases with 33 targets. The importer's step bound is therefore not
   yet proven for edit-carrying sources.
4. **PSRAM exhaustion over long uptime with the shadow present.** Before this run, the board had been up since the
   C3d boot through the `note_edit_overdub_compose` and `edit_full` sessions:
   - its `[Memory] ext` live figure was already 7.7–7.8 MB of the 8.18 MB pool, the shadow's 490 KB included;
   - after the stopped `edit_full` run the pool was refusing (`pool_refused=1706`, `largest_free=0KB`;
     snapshot reserves and display projections denied);
   - `!REBOOT` was refused because the transport was still held.

   B.5b measured production alone at 7.15 MB after one owner session, so a 0.49 MB shadow leaves too little margin
   across several sessions. **The 0.5 MiB coexistence cap is too large for long uptimes on this card.** It also
   plausibly explains part of "the HITL does nothing" on this image. Whether the HITL is broken independently must be
   checked on the default image.

## Required before C4 can be called clean

- A host repro of the misattribution (same-pitch dense Record + same-pattern overdubs + companion hides), then a
  pairing that matches production's (`reconstructDisplayNotes` semantics, wrap-aware) while staying bounded.
- Find which import phase spent 3.2 ms and bound it.
- C4 acceptance includes the whole-loop comparison, not only per-window digests.
- A smaller shadow footprint, or no coexistence beyond a single-session qualification run.
- HITL validity re-checked on the default image (owner concern, 2026-11-01).
