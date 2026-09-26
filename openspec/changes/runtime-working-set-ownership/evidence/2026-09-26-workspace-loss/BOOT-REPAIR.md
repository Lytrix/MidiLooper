# Automatic boot metadata repair — host acceptance

2026-09-26. DEC-089 — boot metadata repair. Implemented on the existing branch;
uncommitted worktree identity is pinned by source hashes, not HEAD alone. No device
access, upload, reset, SD write, quarantine or recovery installation occurred.

## What executes

`StorageManager::loadState` inspects runtime metadata before cleanup. For missing or
corrupt metadata it requires valid workspace metadata and all 64 slot files, writes
and validates a separate metadata candidate, preserves any corrupt final, promotes
the candidate, then uses normal deferred load/undo reconstruction. The original
workspace and loop payloads remain unchanged. On refusal, the boot and save paths
are blocked; no empty fallback is saved. Normative prerequisites/defaults/limits:
[Storage architecture](../../../../../docs/Authority/Architecture/Storage.md#automatic-runtime-metadata-repair-at-boot).

## Completed verification

- Isolated production boot acceptance: six cases pass, including all 276 simulated
  reboot cuts after repair mutations. Missing/corrupt slot, bad workspace CRC/missing
  workspace, unsupported body, newer epoch, wrong slot identity all refuse without
  writing. Failed open, short/failed write, failed backup/promotion and exhausted
  candidate namespace preserve evidence and refuse saves. Retrying interrupted work
  succeeds from unchanged sources. Empty-card preflight keeps fresh boot eligible.
- Preserved library: 64/64 slot roundtrips; 60 nonempty; 122 reconstructed undo units.
  Healthy normal reboot preserves existing workspace bytes, makes no repair files,
  and retains all 122 units. Hydration ordering before/after passes (during restore
  is exercised by the baseline). Original backup hashes unchanged.
- Pre-fix production boot fails the new assertion: `Automatic boot reconstruction
  refused`, direct executable exit 1. Earlier attempts that failed to link or find
  Unity were harness errors and are not behavioral red proofs.
- One initial healthy-boot assertion incorrectly required the entire SD file map
  unchanged: normal boot reconciles `sets/index.bin`. The final oracle requires
  all original workspace bytes unchanged and no added current-workspace repair files;
  it permits the existing catalog reconciliation. That corrected run passes.
- Full native: 2,461 passed / 7 skipped. Recovery/ABI cases skipped there run in the
  isolated target-layout build above. Python reader/audit: 16 passed. OpenSpec strict
  validation and `git diff --check` pass.
- `teensy41-capture-serial`: SUCCESS, RAM1 variables 96,160, code 392,360, padding
  856, locals 34,912. Approved floor 840; 16 bytes headroom. New recovery functions
  and the existing cold `loadState` execute from flash. No floor rebase.

## Reproduction and evidence

`python3 tools/midilooper-reader/reconstruct_undo.py --automatic-recovery
captures/midilooper-folder-copy-20260926-195228/download-05 NEW_OUTPUT`

Final output: `captures/midilooper-folder-copy-20260926-195228/boot-final-02`.
The JSON acceptance, compressed per-slot/fault/reload logs, red proof, full native,
firmware, Python and OpenSpec logs are preserved beside this document as
`boot-repair-*`; `boot-repair-hashes.json` pins files, production sources and image.
The original backup is never a repair destination.

## Boundaries and next gate

This proves firmware behavior against the host SD emulator and the real copied
payloads. It does not prove physical power-loss/FAT durability, target boot duration
or arbitrary memory-pressure recovery. Reconstruction validates one slot at a time
with existing parser allocations before active service. It conservatively refuses
partial libraries and old slot bodies without current epoch headers. Original tempo,
mutes, selection and global undo chronology are not restored; documented recovery
defaults replace settings, and redo starts empty.

Device acceptance remains OPEN under the latched incident hold. The host stimulus
registry was listed; existing record/overdub baselines do not exercise corrupt-bundle
boot recovery. Before installation, prepare a dedicated approved boot-observation
stimulus using the existing missing-bundle incident state (do not corrupt or clear
more user data), capture the complete boot and verify loaded content/Undo/Redo and a
second healthy boot. No canonical stimulus was changed and no hardware run occurred.
