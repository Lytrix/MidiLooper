# Offline undo reconstruction — host acceptance and reload blocker

2026-09-26. User requested “ok test and reconstruct if possible”. No serial/device
access, upload, normal-firmware restore or SD writes. Original folder copy remains
byte-identical, and the incident hold remains latched.

## Scope and transition oracle

Use existing `TrackUndo::rebuildTrackFromLoopContent` against copied passes after
production directory load and deferred restore. Reconstruct a separate diagnostic
bundle using the actual C++ global-undo serializer. For every nonempty slot, undo
all its reconstructed entries, then redo them: require each cursor transition,
bounded snapshot publication, identical materialized 20-byte MIDI records, identical
loop length, and identical published note IDs/pitch/velocity/start/end. Reload the
serialized history through the same loader; refusal to preserve it blocks installation.

## Result

The production owner reconstructs **122 actions across all 60 nonempty slots**:
60 record additions, 11 overdub additions, 51 loop-boundary changes. All 60 full
Undo → Redo roundtrips pass; four empty slots correctly have no actions (64 slot
processes pass). This is derived usable history, not the original global chronology.
The copied pass data has not been flattened or discarded.

The separate diagnostic runtime bundle is **8,846 bytes**, SHA256
`f3d0e0781798dfbd93758ddb5beb904e1876515a7d9f722dfa7da0796c4ac7c3`.
It is **not approved for installation**. On reload only **9 of 122 actions remain**
(per track: 3,0,0,0,0,1,0,5). The wrapper exits **2**, despite all slot roundtrips
passing, because persistence acceptance failed. Full original event bytes still
match after reload; losing the action stack is distinct from losing loop content.

## Policy correction discovered before the proposed fix

The observed loss of actions is not by itself proof of a production bug: LSV1 load
is deliberately required to start at undo depth zero by DEC-057 — snapshot body
format and the newer active specs. The previous recommendation to fix this as an
ordinary load defect was incorrect. See [the policy review](UNDO-LOAD-POLICY-REVIEW.md).
The host reconstruction and roundtrip results stand; normal restart-safe undo needs
an explicit behavior change. No firmware change has been made.

## Concrete load boundary

`StorageManager::processDeferredUndoSnapshots` reads the saved stack then rebuilds
it from presently loaded passes. `applyLoadedLoopSlotBodyCommit` clears the slot's
history for LSV1 payloads; raw payloads rebuild their slot history. Deferred loading
and undo hydration therefore cannot be treated as independent complete operations.
The fixture follows hydration when no SlotLoadSession is active while background
admission proceeds. Serializing a reconstructed stack alone does not solve this.
If the policy change is approved, the next firmware slice must establish one correct history publication/rebuild point
relative to slot loading and prove history survives deferred load/reload without
inventing cross-slot chronology. No production firmware was changed here.

## Harness scope and corrections

The isolated header adapter uses the target's 20-byte event layout; it rejects SysEx
restoration. A copied `StorageLoopIo.cpp` selects actual target clear/rebuild hooks
that ordinary native compilation substitutes away. Public undo/redo, content-history,
serialization, pass materialization and snapshot publication use production code.
Stopped-only live-capture/display/edit-preview routes fail closed if called; the
geometry UI callback asserts no attached edit slot, where production is a no-op.
These are host behavioral tests, not hardware timing or MIDI-output parity.

The three raw-format slots have no persisted snapshot root. The first version
compared their absent roots against redo-built roots; it also encountered a stalled
publication on track 8/slot 3. Establishing the initial root through the existing
`requestCommittedPassesRepublish` owner before the test makes all three pass. These
were test-baseline failures, **not demonstrated firmware defects**. No publication
failure was bypassed or ignored: each initial and subsequent publication is bounded
and asserted complete. The load/reload action-loss finding remains independently red.

## Limits

Recovered actions come from the active content branch and its saved grouping.
The original order of operations across slots, pre-incident undo cursor/redo branch,
and clear-slot snapshots are not reconstructed. No independent note/control edit
entries were derived in this library. Do not promise that every historical gesture
can be undone. Disabled passes, if present, remain preserved in the original files.

## Reproduction and artifacts

Run from repository root (host only):

```sh
python3 tools/midilooper-reader/reconstruct_undo.py \
  captures/midilooper-folder-copy-20260926-195228/download-05 \
  /absolute/path/to/new-output-directory
```

Output must be new and outside the source backup. Each target uses a fresh process;
the script audits the full source before/after and compares every original SHA256.
Current result: `captures/midilooper-folder-copy-20260926-195228/undo-reconstruction-03/`.
Durable copies here: `undo-reconstruction.json`, `reconstructed-actions.json`,
`diagnostic-history.bundle.bin`, and `undo-test-logs.tar.gz` with all target/build/
reload logs. `undo-artifact-hashes.json` records artifact and relevant source hashes.
Action JSON uses zero-based track/slot; summary slot results use one-based identities.

Regression: Python reader/audit 16 tests pass; affected ordinary native 12 passed /
4 explicit skips. Ordinary native results are recorded
in `undo-ordinary-regression.log`; recovery-specific cases explicitly skip outside
the isolated target-layout runner and are not counted as recovery evidence.

This completes host reconstruction/testing where possible. Restart-safe restoration
and any device installation remain open. The earlier 628-byte default-history
candidate remains separate and is not silently replaced by this diagnostic bundle.
