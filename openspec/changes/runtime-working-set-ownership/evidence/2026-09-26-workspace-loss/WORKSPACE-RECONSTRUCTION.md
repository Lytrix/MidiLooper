# Workspace reconstruction — host accepted, not installed

2026-09-26. User requested reconstruction/testing and asked whether the Teensy
could perform similar recovery in C++. No device access occurred.

## Candidate

`captures/midilooper-folder-copy-20260926-195228/reconstructed-03/candidate/MidiLooper/current/`

All 65 original current-workspace files (64 slots and workspace.bin) are byte-identical
to verified download-05: 442,803 bytes. Only runtime.bundle.bin was added: 628 bytes.
Total candidate: 443,431 bytes (about 433 KiB). Epoch 930 matches current workspace metadata.
Bundle SHA256: `6302d6e7428b87fb566a98ccba9e06f27c9c4e0bd5c1a7c7fd87e3f5a0acbdaf`.
A byte-exact copy is `candidate-runtime.bundle.bin` beside this report; originals
and per-file SHA256s are listed in `reconstruction.json`.

Explicit replacement settings, not recovered originals:
- Transport idle/stopped, tempo 120 BPM, master length 0 (derive during use).
- First nonzero-length slot on each track selected and enabled; other slots disabled.
- Tracks and slots unmuted; selected track 1.
- Fresh global undo stack, default anchor fields/timestamps. Original global undo
  history and performance mix/selection are not recovered. Serialized pass/edit
  history in each untouched slot remains present; runtime may rebuild local history.

## Production-path test

`RecoveryRuntimeBundle.h` writes through existing CurrentSetStorage,
CurrentWorkspaceStorage and RuntimeBundleFooter serializers. It owns no filename,
scan, deletion, rename or installation step. Caller supplies an isolated File.
`test_offline_workspace_recovery` reuses the real persistence/load harness with
an in-memory SD boundary. A temporary header wrapper exposes only the private
production directory loader; no production header was edited. The previously
validated Teensy ABI adapter is used (20-byte MIDI event, opaque SysEx token).

Checks completed:
- Missing bundle refused; truncated bundle refused.
- Invalid selected-slot defaults refused before writing, and closed output refused.
- Reconstructed bundle accepted by `loadCurrentSetFromDirectory`.
- Loaded tempo/transport, enabled/muted flags and active slot indices match defaults.
- Production background-admission and restore jobs drain in 2,863 simulated turns,
  with a 100,000-turn bound. This is not a device performance measurement.
- All 64 restored lengths match. All **7,840 capture-pass MIDI event records** match
  source-decoded event bytes, with matching record/overdub pass identities/counts.
- All 57 serialized snapshot note inventories match source counts (2,766 note identities).
- All original in-memory SD files remain identical after restore.
- Output epoch checksum and SAVE footer pass; host copy rehashed after testing.

The first harness cut stopped after one admission slice; the second drove admission
but left nativeMillis frozen, so the production 2-second holdoff never expired.
Both were corrected in the test, not bypassed in production. No target timing claim.

Final repeatable wrapper run: source decode **3/3**, reconstruction **2/2**.
Ordinary-build affected-suite regression: **12 passed / 2 explicit skips** (real-file
recovery fixtures require isolated ABI configuration), including the existing drain
suite. Python audit/reader tests: **16 passed**. No full native run, firmware build,
device run, power-loss test or original-vs-current audible parity claim.
Evidence: reconstruction-native.log, reconstruction-regression.log, reconstruction.json
(source hashes), and the candidate bundle. Production src/ and include/ unchanged.

Run:
`python3 tools/midilooper-reader/reconstruct_backup.py <verified-download-directory> <new-output-directory>`

## C++ recovery on the Teensy: feasible, not shipped

The reusable writer is C++ with existing File/serialization dependencies and no Mac
API. Its host acceptance supports reusing this work on Teensy, but does not establish
firmware fit, device timing, SD durability or safe automatic recovery.

A device implementation belongs to StorageManager, as a metadata-salvage fallback
when intact current slot files survive but the runtime bundle is missing/unreadable.
It must validate the slot generation/format/content first, state the replacement
settings, write a separate candidate, close/drain/reopen/validate it, then install
with a crash-safe commit retaining recoverable previous state. Interrupted candidate
writes must never replace valid workspace data. Power interruption at each commit
boundary, repeated boot, malformed slots, no surviving content, firmware RAM1 floor,
and bounded stopped-only work need device acceptance. Do not modify the deployed
checksum algorithm incidentally: historical files use its existing continuation semantics.

This procedure does not recreate erased or corrupt notes. Do not infer that every
`.bad.*` bundle is usable, or transplant its undo references into current slots.
Normal boot cleanup removes named `.tmp` files, not the completed runtime bundle;
the present test exercises directory load and deferred restore, not the entire
RTC/index-reconcile/hardware boot chain.

Recovery installation and normal firmware upload remain unperformed and outside
this host-only authorization. The hold stays latched. S1.6c.2 — capacity revalidation
remains blocked until the library is loaded and measured on hardware.

Closeout: DEC-087 — host metadata reconstruction records the candidate-only defaults
and exclusions. Runtime scope/state, deliverable tracking and OpenSpec tasks updated.
