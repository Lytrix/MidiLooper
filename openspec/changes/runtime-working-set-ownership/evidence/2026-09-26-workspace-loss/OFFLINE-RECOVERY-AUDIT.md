# Offline recovery audit — 2026-09-26

Scope: incident-support prerequisite for **S1.6c.2 — capacity revalidation**.
No device access, SD writes, upload, normal firmware boot or recovery performed.
Input is exclusively the verified `captures/midilooper-folder-copy-20260926-195228/download-05` copy.
All 1,007 manifest entries reverified by SHA256, CRC32 and length before inspection.

## Findings

- Current workspace has 64 slot files and `workspace.bin`, but no runtime bundle.
- All 64 current slot files pass the on-device 256-byte incremental body checksum
  and SAVE footer checks. Metadata identifies 60 nonzero loop lengths and four zero lengths.
  These are **not** decoded-note or successful-load counts.
- Current workspace metadata passes its checksum, epoch 930.
- 60/64 slot loop lengths agree with the preserved pre-loss census. Differences
  (human one-based track/slot): 8/4: 19200→0; 8/5: 0→6144;
  8/6: 0→6144; 8/7: 0→1536. This comparison does not prove note preservation
  or attribute any change to a specific command.
- 69 revision containers pass their header and firmware payload checksums.
  54 also pass this audit's current-format chunk walk; 15 older S0001 revisions
  fail that walk. Treat those as unsupported/rejected by this audit, not proof
  of corruption: historical layout compatibility has not been investigated.
- The largest revision candidates have only 22 nonzero-length slot chunks
  (S0003/v0017); latest S0003/v0018 has 21. Saved sets are not a replacement
  for the current large library.
- Seven historical runtime-bundle files pass the chosen epoch/footer checksum
  checks, including two `.bad.*` files. This does not make any of them compatible
  with the current slots. No bundle was selected for transplantation.

## Checksum compatibility finding

`PersistenceSchema::crc32Continue` complements its input before a helper that
also complements its input; it is not standard incremental CRC32. The audit
reproduces current device algorithms: epoch finalizer 256-byte chunks seeded via
Continue(0); revision file reader 512-byte chunks with first chunk via crc32.
A direct compile of the production PersistenceSchema.cpp confirms
Continue(0, "123456789") = d202d277. Standard CRC rejection is therefore not
sufficient to label these files corrupt. No firmware checksum behavior changed.

## Execution packet / limits

Oracle: manifest mismatch aborts audit; bad footer/checksum, truncated section or
chunk, duplicate slot chunk and count mismatch fail the relevant structural check.
No source files are rewritten. Seven focused audit tests plus nine reader tests
pass (16 total). Report is reproducible with:

`python3 tools/midilooper-reader/audit_backup.py captures/midilooper-folder-copy-20260926-195228/download-05`

Report: `offline-audit.json`, SHA256 `3031219a40c92e0800a4491f3d5b28197a08b9651d47fcea6d6fd694d4ae6536`.
The report explicitly excludes full note/pass decoding, revision directory/body
agreement, runtime-bundle semantic compatibility and restore readiness.

## Next bounded slice

Use the production slot decoder against the 64 current host files, validate notes,
passes and effective roots, and establish exactly which runtime metadata a recovered
workspace needs. Prepare and validate a candidate **on the host only** before
requesting any recovery write. Do not combine a historical bundle with current
slots merely because both have valid checksums. Normal upload/recovery hold remains.
Capacity acceptance stays blocked until a recovered library is loaded and measured.
