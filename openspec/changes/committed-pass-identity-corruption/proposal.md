## Why

The committed **Record pass** can lose its row/NoteId pairing across a long overdub chain. The display oracle in
`test_overdub_chain_preserves_frame_progress_long_loop` now exposes it directly (the committed source is read
instead of a stale root):

```
record pass rows: 12/N|1  12/N|24  48/F|1  780/N|2 ...     (47 rows, expected 48)
id 24 (the bar-23 pair) has only: 12/N    <- orphan On at the loop start, no matching Off
```

A spurious `NoteOn @tick 12 pitch 48 NoteId 24` appears and the bar-23 note loses its pair. The windowed display
resolver pairs by NoteId, so the duplicate collapses with the real note at the same pitch/start — a direct
"notes disappear / come back" mechanism. The same corruption is present with and without the parked stale-root
display-source fix, so it is an independent content defect (the stale root had been masking it).

## What Changes

- Name the **ownership transition** at which NoteId 24 changes from its correct bar-23 On/Off pair into
  `On@12 pitch 48` with no matching Off.
- Snapshot the **entire semantic Record-pass row vector** (event type, tick, NoteId, channel, pitch, velocity)
  immediately after Record seal and require exact equality after **every** overdub commit. NoteId 24 is the
  chain's witness, not the oracle; the first unequal commit names the transition and the delta names the row.
- After that transition is named, run only the one-factor controls needed to distinguish its preconditions;
  expand to loop length, wrap and overdub count only when the named boundary makes them relevant.
- Fix the named owner so a **sealed capture pass is immutable**: later commits, normalization, prefix resume,
  companion publication, undo/redo and derived-cache work may change pass state or create derived
  representations, but may not add, remove, move or rewrite rows in that sealed pass.
- Keep source integrity separate from derived-representation correctness: a root, resolved view, companion
  projection or playback window must preserve the sealed NoteId and pairing, and is never the authority for
  repairing or reshaping the sealed rows.
- Keep the display oracle strict; do not use a stale root or a broader source selection to hide bad content.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `note-edit-stable-note-id`: sealed capture-pass rows are immutable across later overdub commits,
  normalization, prefix resume, companion publication, undo/redo and derived work; derived representations
  preserve the sealed NoteId and pairing.

## Impact

Owner found (task 1.2): the **Record stop writer**, before the pass is sealed — `RecordStopLength::
computeLoopLengthTicks` trims a trailing bar when the last event is strictly inside it, and
`Loop::projectCaptureAtRecordStop` wraps the resulting overflow NoteOn while dropping its NoteOff.
`Loop::passes` inventory writes, the deferred pass transfer, the DEC-177 prefix resume and companion
publication were not implicated for this shape; the sealed pass is born with the orphan row, so the
immutability oracle alone would not catch it. No SD format, undo schema or display-source change is implied.

Evidence: `openspec/changes/overdub-stop-edge-closure/evidence/DISPLAY-SOURCE-STALE-ROOT.md`,
`captures/chain48-content/serial.log` (the 20-bar device dump is clean; the 24-bar native chain is RED).
