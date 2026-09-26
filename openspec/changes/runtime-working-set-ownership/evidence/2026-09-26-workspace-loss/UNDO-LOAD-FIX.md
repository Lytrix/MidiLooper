# Load-time undo reconstruction — implemented, host verified

2026-09-26. User approved DEC-088 — load-time undo reconstruction and clarified:
redo is needed after an Undo, while the loop is already loaded. No device access,
upload, serial session, reset or SD write occurred; incident hold remains latched.

## Final behavior

Successful LSV1 slot load installs its saved snapshot unchanged, then uses the same
`TrackUndo::rebuildSlotFromLoopContent` owner as raw-format load. Derived units come
from saved active passes/grouping. No history root is replayed or pinned at load.
Redo begins empty for the restored slot; later Undo makes its action available for
Redo. Runtime history and old redo are not persisted.

`appendContentUndoEntriesForSlot` inserts loaded applied units before the existing
redo partition. `repositionGlobalUndoStackTipForSlot` preserves that partition,
putting focus applied entries last and focus redo first. It also rebuilds the moved
payloads when focus has no entries, instead of leaving moved-from entries in place.
Other slots' redo and selected-slot identity survive background load. Ordinary new
musical commits still use `dropRedoBranch` unchanged.

Production change: StorageLoopIo.cpp, TrackUndo.cpp, GlobalUndoStack.h. No new
owner, state field, queue or storage schema. No reset of the global history on each
slot commit. Scope/contract/red-team review is in ARCHITECTURE-REVIEW.md's approved
undo-load execution packet. The policy authority and current guide are updated.

## Discriminating evidence

- Before fix, removing the test-side history rebuild fails the pinned library
  acceptance (`Expected 2 Was 0`). Saved actions cannot silently be recreated by a
  test after the loader has dropped them.
- Before fix, focus reordering fails the applied/redo partition regression. The
  existing function forced cursor to end. Both red logs are preserved here.
- After fix, **122 actions** (60 record, 11 overdub, 51 boundary) derive from the
  **628-byte bundle with empty undo headers** plus unchanged slot files. The earlier
  8,846-byte diagnostic history bundle is not the recovery mechanism.
- **All 60 nonempty slots** pass Undo-all → Redo-all with identical materialized
  MIDI event bytes, lengths and published note IDs/pitch/velocity/start/end. Four
  empty slots have no actions; 64 fresh-process slot runs pass.
- Reload compares the full emitted slot/kind/pass/group-count inventory to the
  independently preserved offline audit, not just a total, and exercises the full
  Undo/Redo sequence again without a test-side history rebuild.
- Deferred hydration before, interleaved with, and after slot restoration all pass.
- A runtime Undo on focus slot 0 followed by repeated production load commits of
  slot 1 retains the exact redo entry ID and unchanged undone musical content;
  Redo then restores the original content and note identities. Loaded roots remain
  the exact installed roots; repeated commits do not duplicate actions.
- Focused ordinary native suites: **68 passed**. Reader/audit Python: **16 passed**.
- Full native: **2,461 passed / 5 skipped**, zero failures, 5m06.688s. The isolated
  recovery cases explicitly skip in ordinary native and are proven separately by
  the target-layout runner. OpenSpec change validation passes.
- Capture-serial firmware build **SUCCESS**. RAM1 variables **96,160**, code
  **392,376**, padding **840**, locals **34,912**: approved floor met, static RAM1
  unchanged. FLASH code 1,032,584. This does not measure device timing or dynamic
  metadata memory under user load.

## Candidate

`captures/midilooper-folder-copy-20260926-195228/reconstructed-undo-04/candidate/`
contains 65 original files unchanged plus the 628-byte runtime bundle: **443,431
bytes** total. Bundle SHA256:
`6302d6e7428b87fb566a98ccba9e06f27c9c4e0bd5c1a7c7fd87e3f5a0acbdaf`.
Undo is reconstructed by the updated firmware, not stored in the bundle.
Built firmware SHA256:
`df130adacb0a939fa6ba830e2fe5b62f831b1942a596d1360f2c4971c52d7889`.
The candidate still uses the explicit recovery settings (stopped/120 BPM, first
nonempty slot selected/enabled per track, unmuted). No automatic boot salvage or
restore command has been added. The normal device remains on its standalone reader.

## Limits and next device gate

Original cross-slot chronology, old undo cursor/redo branch, clear-slot snapshots
and missing historical data are not recovered. The host fixture establishes the
preserved library, not arbitrary corrupt/unsupported history or MIDI-output parity.
Existing body-format refusal tests remain green. Source files were rehashed before
and after; no backup bytes changed.

Registered stimuli were listed, not run (`host_midi_hitl.py stimulus list`).
STIM-BASE-2-2-01 — record/overdub with post-stop undo/redo is reusable for runtime
regression; it does not by itself verify reboot reconstruction. The explicit load/
reboot/Undo/Redo hardware acceptance remains open until the hold is deliberately
resolved and a matching device sequence is authorized. No hardware pass claimed.

## Reproduction

```sh
python3 tools/midilooper-reader/reconstruct_undo.py \
  captures/midilooper-folder-copy-20260926-195228/download-05 \
  /absolute/path/to/new-output-directory
pio test -e native
pio run -e teensy41-capture-serial
```

Host runner exits 0; it checks original hashes, per-slot content, reload identities,
hydration orders and runtime redo. `undo-load-acceptance.json`, `undo-load-candidate.json`,
`undo-load-logs.tar.gz`, red logs, native/build logs and the production patch are
preserved beside this report. `undo-load-hashes.json` records source/artifact hashes.
