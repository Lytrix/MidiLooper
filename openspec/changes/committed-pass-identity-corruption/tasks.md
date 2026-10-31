# Tasks — committed pass identity/row corruption

The display oracle `test_overdub_chain_preserves_frame_progress_long_loop` is the RED. Do not weaken it, and do
not use the parked stale-root display fix to hide it. Fixture-first: name the transition before changing code.

## 1. Invariant fixture and boundary trace

- [x] 1.1 Snapshot the **entire semantic Record-pass row vector** (event type, tick, NoteId, channel, pitch, velocity) immediately after Record seal and assert exact equality after **every** overdub commit. On the first unequal commit, print the exact delta (missing expected rows / unexpected current rows) and the overdub index; NoteId 24 is a witness, not the oracle. Record the delta and first failing iteration in `evidence/EXECUTION.md`.
- [x] 1.2 Instrument the first equal→unequal transition only (Record seal/publication, that overdub commit's pass transfer/normalization, certified-prefix resume, companion publication) so it names the owner and job/phase/interval; prove the row vector is correct immediately before the boundary and wrong immediately after it. Use chunk IDs/refcounts/aliases around that boundary as diagnostics, not as the invariant.
- [x] 1.3 After the boundary is named, run only the minimum one-factor controls needed to distinguish its preconditions (for example certified-prefix resume on/off, wrap on/off, or companion publication on/off for that owner). Expand to loop length 16/20/24/32, overdub count or NoteId allocation only when the named owner makes those variables relevant. Record which control toggles the corruption and which stays clean.

## 2. Fix the named writer

- [x] 2.1 Fix only the named owner so a **sealed capture pass is immutable**: later commits, normalization, resume, companion publication, undo/redo and derived work may change pass state or build derived representations, but never add, remove, move or rewrite a sealed row. No NoteId special-case, no display change, no weakening of the oracle.
- [x] 2.2 Prove the inverse: with the fix the whole sealed Record row vector stays equal after every commit, and a deliberately re-introduced writer bug makes the same fixture fail (including when it corrupts a different row than NoteId 24).

## 3. Verification and closeout

- [x] 3.1 Run the focused display/chain suites and `pio test -e native`; record totals/skips/errored suites and the target RAM1 result. Native: 3,737 / 7 skipped / 3,730 succeeded / 0 failed. Target: **SUCCESS at the approved placement** (`variables 96,192`, `locals 34,880`, ITCM `391,336 + 1,880`), with no budget re-base: the DEC-233 `MidiButtonActions` rodata carve-out removed the 4,096 B transfer and the parked display counter moved to `DMAMEM` (the +32 B). The display-only staged build is also SUCCESS at 96,192. The new image `1f9043…` is uploaded and boot-verified (`crash=0`, `BOOT,load,ok`, `BOOT,ram1,192512`); the persisted case-1 content restored (dump 3072, 8 record rows / 4 pairs, effective 10).
- [x] 3.2 Verify the persisted body and served playback of the affected slot (not only the paint): `!CAPTURE_CONTENT` parity plus a playback capture, comparing the Record pass row/NoteId pair. Dump + playback both PASS on device (`captures/case1-playback-final`: `result=PASS`, 10/10 rows per cycle, zero missing/extra, `exact_order=true`, `storage_matches_effective=true`; persistence survives the dump reset).
- [x] 3.3 Device qualification: **case-1 inside-final-bar PASS** on evaluation image `634e0be9…` (boot `crash=0` / `BOOT,load,ok`): Record stop `raw=2928 final=3072` (4 bars), committed Record pass 8 rows / 4 exact pairs, last pair `2336..2368` (rem 64 < grace 128), no wrapped `@32` phantom; representative 8-bar / 12-window chain also PASS (`final=6144`, Record 16 rows / 8 exact pairs, effective 146 rows); exact-boundary control PASS (`raw=3696 final=3072`, no extra bar; documented `NoteId=0` Off at 3071); playback parity PASS with track 8 soloed (`content-review.json` `PASS`, 10/10 rows, zero missing/extra). See `evidence/DEVICE-CASE1.md`.
- [x] 3.4 Update `PROJECT_STATE.md`, `CURRENT_WORK.md` and shipped tracking only to the evidenced level; the parked stale-root display fix is committed or re-based separately, never as part of this change.
