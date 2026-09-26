# Production decoder audit — 2026-09-26

## Result and scope

All 64 copied current slot bodies decode; 60 nonempty and four empty.
Capture passes contain 7,840 raw MIDI events. 57 slots contain effective snapshots,
with 2,766 distinct-per-slot snapshot note identities. Three nonempty slots use
raw pass format instead (human identities 6/3, 8/1, 8/3); absence of a serialized
snapshot in that format is not evidence of content loss. No SysEx content passed
through the audit: it explicitly rejects SysEx in passes or snapshots.
These counts do not prove original-note preservation or audible playback parity.

## Host ABI trap, resolved in isolated tooling

Initial direct-native decoder: four empty bodies accepted, 60 nonempty rejected.
Initializing the event pool did not change that. Production StorageLoopIo serializes
`sizeof(MidiEvent)` bytes, and its union contains a pointer. Mac compile measured
size=32/data offset=16; an ARM Cortex-M7 compile of the same header confirmed
size=20/data offset=12 with static assertions. This explains why host round-trip
fixtures can pass while device files fail on a native build.

`prepare_native_decode_headers.py` copies headers to a new temporary directory,
replaces only the SysEx pointer representation with an opaque 32-bit token, and
copies StorageLoopIoBodyFormatV1.cpp changing its pointer-to-integer check to read
that token. Its parser algorithm is otherwise unchanged. Layout assertions pin
20/12. SysEx bytes cannot be dereferenced and SysEx content is rejected by the
harness. This is an explicit ABI adapter, not an unmodified native binary.
Production `src/` and `include/` are unchanged.

## Execution packet and verification

Owner/path: host reader → production raw/LSV1 decoding → scoped candidate destruction.
No runtime publication, device session, SD access or firmware build/upload.
Input: verified download-05; wrapper verifies every manifest hash before parsing.
Every raw parse must consume the whole body. LSV1 uses its production parser,
validates stored object/membership data and constructs the snapshot. Per-slot
results plus native log are preserved beside this report. Decoder objects are
released between slots: this is not an all-library memory/capacity measurement.

Oracle: known one-note record round-trip yields two events and length 1536;
truncated raw/LSV1 headers are refused; all 64 device identities must be reported
exactly once, with matching 20/12 ABI, a successful native exit and all decoded.
Native test has an explicit skip if real files or the target-layout adapter are
absent; the wrapper will not mistake such a skip for successful decoding.
Final repeatable wrapper run: three tests passed, zero skips in selected suite.
Ordinary native control run: two tests passed and copied-files test skipped.
16 existing Python audit/reader tests remain passing.

Command:
`python3 tools/midilooper-reader/decode_backup.py captures/midilooper-folder-copy-20260926-195228/download-05 <new-output-directory>`

Evidence: `production-decode.json` includes source SHA256s and manifest identity;
`production-decode-native.log` contains the 64 results and controls.

## Missing runtime metadata (verified from CurrentSetBootLoad.cpp)

`readCurrentSetFilePreamble` requires the container/anchor header, tempo, transport
state, master length, and track count. `readCurrentSetTrackSlotMetadata` reads each
track state/mute and every slot enabled/mute/loop-id. `readCurrentSetFileEpilogue`
requires selected track, active and selected slots, undo stacks and completion
markers (plus optional metadata trailer). Those settings are not recoverable merely
by decoding the current slot files; the current runtime bundle is absent.

Next: prepare a host-only reconstruction candidate and test it through the real
bundle/load path. Keep every slot byte-for-byte; make replacement transport,
selection/mute/enable and undo choices explicit. Do not transplant a historical
bundle's undo references without validating compatibility. No candidate was built
or approved in this slice; no restore write is authorized. S1.6c.2 — capacity
revalidation remains blocked until a loaded library can be measured.
