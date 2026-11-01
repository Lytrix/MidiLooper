# C4a — production-equal NoteOff attribution, a bounded seal, and the whole-loop C4 comparator

Date: 2026-11-01. Owner instruction: "Fix pairing first" (after
[C4 with hides — not clean](2026-11-01-c4-hides-not-clean.md)).

- Native: full suite **3,911 cases, 3,903 passed, 8 skipped**. Of these, `test_revision_store_differential` and
  `test_revision_manifest_store` are 67 / 67, with **664 import-parity checks**.
- Device: probe/shadow image, HEX `dcefc2c452402dee…`. Two forced fresh boots:
  - `captures/session_20261101_202857.log`;
  - `captures/session_20261101_203140.log`.

  Both show `boot_reset,crash=0`, `BOOT,load,ok`, 0 crash rows and 0 `MEM_DENIED`.
- RAM1:
  - default image unchanged (96,192 / code 391,944 / padding 1,272);
  - probe image 96,192 / 392,072 / 1,144 (floor 840).
- Raw rows:
  - run 1: [status](2026-11-01-c4a-status-raw.txt), [windows](2026-11-01-c4a-windows-raw.txt);
  - run 2: [status](2026-11-01-c4a-run2-status-raw.txt), [windows](2026-11-01-c4a-run2-windows-raw.txt).

## Changes

1. **NoteOff attribution is production's rule, shared by the importer and the host oracle.**
   - `LanePairer` (`RevisionImport.h`) walks each lane (channel and pitch) in tick order, with NoteOff before NoteOn
     at an equal tick, and pairs LIFO.
   - A still-open NoteOn then takes the unique unpaired NoteOff earlier in the loop (the wrap): the semantics of
     EditApply `findNoteOffForOnIndex`.
   - The importer appends a pass with NoteOff ids cleared, then attributes the sealed run in two bounded phases,
     `PairWalk` and `PairWrap`, stamping ids in the staged run with `setStagedRowNoteId`.
   - The host harness's `committedPassRows` uses the same class.
   - The old append-order LIFO table is gone.
2. **Host repro of the device misattribution:** `test_wrap_overlapping_same_pitch_notes_are_attributed_like_production`.
   - Shape: same-pitch notes overlap across the loop wrap, and an overdub hides the first one.
   - It was RED with the streaming pairing and is GREEN now.
3. **Zero-length suppression no longer depends on a stamped id.** Production's tick-order walk leaves a zero-length
   note's NoteOff unpaired, because it precedes the NoteOn at an equal tick. `fullRebuildSnapshotCompat()` now also
   accepts an unattributed NoteOff of the same lane on the same tick. A legato NoteOff of the previous note keeps its
   id and is not taken. The 3 unpaired NoteOffs counted host-side are these zero-length rows.
4. **The seal is bounded.**
   - `stepSealStagedRun(budget)` makes the commit-time sort of an over-budget run, and the hide sort, a resumable
     heapsort: one sift per budget unit.
   - The importer's Captures and EditEmit phases use it.
   - `sealStagedRun()` stays as the unbounded wrapper for the non-staged tests.
   - New test: `test_staged_seal_is_bounded_per_step_and_exact`, 300 scattered rows sealed 8 sifts at a time.
5. **No per-edit allocation in `EditApply`:** the per-edit `MidiEventVec` became one scratch vector, reserved with
   the job (at boot on the device).
6. **The C4 comparator includes the whole loop.** `!REVSHADOW_WINDOWS` merges the digests of the current identity's
   windows and compares them with production's whole-loop passes gather.
   - New row: `windows_whole_loop,covered,shadow_rows,production_rows,match,c4_clean`.
   - `c4_clean` requires all of: the source is current, every window matches, the windows cover the loop exactly, and
     the whole loop matches.
7. **Per-phase import timing:** `import_phases` reports the maximum call time by the phase the call started in, and
   `slow_seals`.

## Device — owner chain, Record and overdubs both `16ths.mid` (scratch track 8 / slot 4)

| Reading | Run 1 | Run 2 |
|---|---|---|
| Stimulus gate (record / overdubs / cap / cleared) | 1 / 12 / 13 / yes | 1 / 12 / 13 / yes |
| Shadow publishes / `SourceMoved` / refusals / duplicates | 13 / 1 / 2 / 0 | 13 / 1 / 2 / 0 |
| Import rows / unpaired NoteOffs / **edit runs / hides** | 1,204 / 0 / **0 / 0** | 1,206 / 0 / **0 / 0** |
| **Import grain worst whole call** | **197 µs** | **179 µs** |
| Phase maxima begin / captures / pair walk (µs); slow seals | 145 / 176 / 197; 0 | 100 / 179 / 179; 0 |
| Window grain calls / `Ready` / max | 263 / 130 / 238 µs | 263 / 130 / 239 µs |
| C4 windows compared / matched (identity 13, source current) | 10 / 10 | 10 / 10 |
| **Whole loop shadow vs production passes** | **1,204 / 1,204, match** | **1,206 / 1,206, match** |
| `c4_clean` | **1** | **1** |
| Shadow total bytes | 494,940 | 494,940 |

## Reading

1. **C4 is clean under the strict whole-loop oracle** for this stimulus, in two independent fresh-boot runs. The
   import grain is 10× inside its 2 ms bound.
2. **The hide-carrying shape did not recur.** Production produced no companion edit passes in either run, so the 33
   hides of the earlier run are not reproducible on demand with this stimulus. Which overdub notes overlap committed
   ones depends on timing. The pairing fix is therefore proven on the host (the repro test plus 664 parity checks)
   and on device only for hide-free sources.
3. **The 3,212 µs step is not yet attributed.** Seal sorting is now bounded, but neither run sorted a run
   (`slow_seals=0`), and neither carried edits. The remaining candidates are edit-carrying paths: `begin` with many
   edit passes, `EditApply`'s `applyNoteEditPass`, and the `EditSort` heapsort. `import_phases` will name the phase on
   the next hide-carrying run.

## Still owed (C4a)

- A **hide-carrying device run** with `c4_clean=1` and an `import_phases` reading. It needs a stimulus whose own gate
  passes and that reliably produces companion hides. That includes the HITL validity re-check on the default image
  (owner concern, 2026-11-01).
- **Shadow footprint:** 494,940 B is inside the 0.5 MiB cap, but long uptimes exhausted PSRAM.
