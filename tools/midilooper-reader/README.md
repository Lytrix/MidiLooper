# MidiLooper USB folder reader

Copy `/MidiLooper` to the Mac while the card stays in the Teensy. Only existing
files in that folder are exported, including saved sets and recovery subfolders.
Unrelated card folders are not enumerated or exported. Space required is the total
file size, not the card's 32 GB capacity. The SD filesystem metadata must still be
read to locate the folder. This is file copying, not deleted-file recovery.

## Current status

The folder reader was installed and the folder copy verified on 2026-09-26.
It has now been replaced by the recovery looper firmware with explicit upload
authorization. The incident hold was archived and cleared for that installation;
the device reports repaired metadata and a ready, stopped, undo-hydrated boot.
Hardware per-loop content/Undo/Redo and second-boot acceptance remain open.
USB accessory access was approved on macOS.

Completed copy: `captures/midilooper-folder-copy-20260926-195228/download-05/`:
1,007 entries, 6,837,184 bytes, all transfer CRCs and independent local SHA-256,
CRC and length checks passed. One duplicate path was preserved separately:
`current.bad.211210/workspace.bin` has a 154-byte entry in the normal tree and a
77-byte entry in `duplicate_entries/000854.bin`. Manifest `local_path` identifies
both copies. Full report: `captures/midilooper-folder-copy-20260926-195228/verification.json`.

The folder-reader protocol is MLREADER2 with host acknowledgements after every line,
preventing the device from overrunning slow host processing. Earlier incomplete
copies are retained. The standalone reader has no looper save/recovery code.

## Locally tested commands (no device access)

From the repository root:

```sh
.venv/bin/python -m unittest discover -s tools/midilooper-reader -p 'test_*.py'
pio run -d tools/midilooper-reader -e teensy41-capture-serial
```

The independent project uses the installed Teensy platform/framework. It does not
compile or edit production `src/` or `include/`, and does not affect their RAM1
budget. The environment name follows the repository default; this standalone image
is a serial-only utility, not the session-capture looper build.

## Copying, only after the reader was approved and installed

Use the reader's actual USB serial port and a new destination directory:

```sh
.venv/bin/python tools/midilooper-reader/read_midilooper.py \
  --reader-installed --port /dev/cu.usbmodemYOUR_READER \
  --output /path/on/mac/MidiLooper-backup
```

The output contains `MidiLooper/` and `manifest.json`. Files have transfer CRC32
verification and local SHA-256 hashes. `status: complete` requires all declared
bytes, checksums and final file/byte totals to agree. Failure leaves the directory
and manifest explicitly incomplete; retry into a new directory. No files are
overwritten. Default maximum download is 1024 MiB, adjustable via `--max-mib`.
Hex transport uses approximately twice the file bytes over USB, not twice the
local disk space. An empty directory is copied; a missing `/MidiLooper` is an error.

No list/read of arbitrary device paths, mutation command, raw-card image, recursive
scan outside `/MidiLooper`, subprocess upload or automatic restore is available.
Copy completion establishes transport integrity, not that saved loop content is
semantically valid or recoverable. This reader assumes a readable filesystem;
reported card/read errors abort instead of attempting repair.

## Execution packet / decision review

Scope: user-requested folder-only USB copy. Existing looper command dispatch and
USB configuration were searched; neither MTP nor a file-download command exists.
Extending the normal looper would run incident-sensitive boot recovery on upload;
the utility instead isolates that code entirely. No production ownership transfer,
runtime scheduling, MIDI timing, persistence-format change, or active OpenSpec
behavior is implemented. The normal StorageManager remains unchanged. This is an
incident-support artifact, not completion of a firmware stage.

Preconditions: approved reader installation; no normal-looper boot during recovery;
explicit new host output; one serial client. Firmware mounts SD only for an explicit
export, opens `/MidiLooper` and descendants using read-only file handles, and never
mutates the card. Startup only enables serial. USB disconnection can interrupt a
read but cannot interrupt a save because this image has no saving path.

Oracle: binary and empty files reproduce exactly; corrupt/truncated data, wrong
totals, unsafe/duplicate names, device errors and oversized transfers fail; existing
destinations/symlinks are refused. Files are streamed in 256-byte chunks, paths are
bounded to 511 bytes and recursion to 16 directory levels. No slot stimulus is used:
this utility has no recording/transport actions or normal firmware runtime.

Local verification: 9 host tests pass; standalone Teensy build succeeds. Folder-copy device
verification PASS (transfer only; no recovery or semantic-content verdict). Whole-project native suite is not required for this isolated
tool (no production code changed, no push/merge/PR requested). First folder-copy verification is complete. Full-card imaging is no longer a mandatory
prerequisite: the user explicitly requested folder-only access without removing the
card. This does not authorize an upload or filesystem repair.

`--preserve-duplicate-files` explicitly preserves ambiguous duplicate file entries
under `duplicate_entries/`, records their original path in the manifest, and never
overwrites the first entry. Duplicate directories still fail closed. The completed
incident copy used this flag. Later analysis must include these alternate files.

## Offline structural audit

`python3 tools/midilooper-reader/audit_backup.py <download-directory>` prints JSON
without modifying the copy or accessing a device. It verifies every transfer hash,
then checks firmware-format containers and extracts loop lengths. This is not a
note-content or restore-readiness validator. It reproduces the firmware's unusual
incremental checksum; standard CRC32 is used only for the transfer manifest.

## Production decoding on the Mac

`python3 tools/midilooper-reader/decode_backup.py <download-directory> <new-output-directory>`

Verifies the transfer manifest, prepares an isolated Teensy-layout header copy,
and runs only `test_offline_recovery_decode` in the native environment. Requires
PlatformIO and a native compiler. Never connects to the device or uploads.
Produces `decode.json` and `native.log`. MidiEvent's persisted layout is 20 bytes
on Teensy and 32 on Mac; the adapter uses an opaque 32-bit SysEx token and adjusts
one parser pointer check accordingly. SysEx content is refused. Slot decoding is
not workspace restore validation or playback parity. Input files remain untouched.

## Reconstruct missing runtime metadata (host only)

`python3 tools/midilooper-reader/reconstruct_backup.py <verified-download-directory> <new-output-directory>`

Copies current files to a separate candidate, validates the source with the decoder,
uses `RecoveryRuntimeBundle.h` to add runtime metadata, and exercises the production
directory loader plus deferred restore on an in-memory SD card. Checks all original
bytes, restored capture events and snapshot counts. Outputs reconstruction.json,
logs, and candidate/MidiLooper/current. Requires a missing runtime bundle; refuses
to replace an existing one. No serial/device/flash operations.

Replacement defaults: stopped at 120 BPM, first nonempty slot per track selected
and enabled, tracks/slots unmuted, track 1 selected, fresh global undo. These are
explicit recovery choices, not recovered performance settings. The C++ writer has
no installation/rename behavior and is not an automatic firmware recovery feature.

## Test and reconstruct undo (host only)

`python3 tools/midilooper-reader/reconstruct_undo.py BACKUP NEW_OUTPUT` audits the
source, reconstructs actions with the production C++ owner, writes an empty-history
runtime bundle and tests every slot's full Undo/Redo plus history reload. Exit 2 means a
roundtrip or reload failed; never install a candidate bundle based on partial
success. All copied files are rehashed after testing. No device is opened.

With DEC-088 — load-time undo reconstruction, the preserved library derives all
122 actions across 60 nonempty slots from its unchanged pass records. Load/reload,
hydration order and Undo/Redo pass on the host. The candidate bundle is 628 bytes
and stores no undo history. Hardware recovery is still unverified and not installed. Original cross-slot chronology, old redo branch and clear-slot
snapshots are not inferred. See the active change's UNDO-RECONSTRUCTION.md evidence.

### Host-only automatic boot acceptance

`python3 tools/midilooper-reader/reconstruct_undo.py --automatic-recovery BACKUP NEW_OUTPUT`
executes the firmware boot owner against an in-memory SD image of the verified folder
copy, then tests every slot's Undo/Redo, normal reload and hydration ordering. It also
injects invalid sources, failed/short writes, failed renames and resets at each repair
mutation. It never opens a device. Target-layout adaptation is isolated to the host
build; it does not prove physical SD durability or authorize installing firmware.
