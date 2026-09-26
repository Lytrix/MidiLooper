"""Prepare an isolated host header tree with the Teensy's persisted event layout.

Production parsers stay unchanged. The 32-bit SysEx pointer is an opaque token:
this audit does not dereference it or support restoring SysEx payloads.
"""
from pathlib import Path
import argparse
import shutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('destination', type=Path)
args = parser.parse_args()
source = Path(__file__).resolve().parents[2] / 'include'
if args.destination.exists():
    raise SystemExit('Destination must be new; refusing overwrite')
shutil.copytree(source, args.destination)
p = args.destination / 'MidiEvent.h'
s = p.read_text()
needle = 'const uint8_t* data;'
assert s.count(needle) == 1
s = s.replace('struct MidiEvent {', '''struct RecoveryOpaquePointer32 {
    uint32_t token;
    RecoveryOpaquePointer32() = default;
    RecoveryOpaquePointer32(const uint8_t*) : token(0) {}
    RecoveryOpaquePointer32& operator=(const uint8_t*) { token = 0; return *this; }
    operator const uint8_t*() const { return nullptr; }
};
struct MidiEvent {''').replace(needle, 'RecoveryOpaquePointer32 data;')
s += '\nstatic_assert(sizeof(MidiEvent) == 20, "Teensy persisted event size");\nstatic_assert(offsetof(MidiEvent, data) == 12, "Teensy persisted event data offset");\n'
p.write_text(s)
print(args.destination)

body = (source.parent / 'src/StorageLoopIoBodyFormatV1.cpp').read_text()
needle = 'reinterpret_cast<uintptr_t>(event.data.sysexData.data)'
assert body.count(needle) == 1
body = body.replace(needle, 'event.data.sysexData.data.token')
(args.destination / 'RecoveryStorageLoopIoBodyFormatV1.cpp').write_text(body)

# Expose only the existing directory loader to the isolated native recovery fixture.
p = args.destination / 'StorageManager.h'
s = p.read_text()
needle = 'private:\n    static bool loadCurrentSetFromSd'
assert s.count(needle) == 1
s = s.replace(needle, 'public:\n    static bool recoveryLoadDirectoryForTest(const char* path, LooperState& state) {\n        return loadCurrentSetFromDirectory(path, state);\n    }\nprivate:\n    static bool loadCurrentSetFromSd')
p.write_text(s)

# Native substitutes away target undo hooks. Select the actual target branches
# in the isolated copy so recovery tests cannot pass by skipping undo behavior.
p = source.parent / 'src/StorageLoopIo.cpp'
s = p.read_text()
start = s.index('void STORAGE_LOOP_IO_MEM applyLoadedLoopSlotBodyCommit(')
end = s.index('void applyLoopSlotMetadataToLoop(', start)
section = s[start:end]
assert section.count('#if defined(PIO_UNIT_TEST_NATIVE)') == 1
section = section.replace('#if defined(PIO_UNIT_TEST_NATIVE)', '#if 0')
s = s[:start] + section + s[end:]
(args.destination / 'RecoveryStorageLoopIo.cpp').write_text(s)
