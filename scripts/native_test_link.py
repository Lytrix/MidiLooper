"""Compile the production-route fixture against native hardware boundaries."""
import os
import sys
Import("env")
# Suites that link production code pulling in the Arduino/SD hardware boundaries.
_FIRMWARE_BOUNDARY_SUITES = {
    "test_capture_ring",
    "test_continuous_capture",
    "test_midi_button_timing",
    "test_shared_capture_finalization",
    # S3 gate: the runtime working-set acceptance suite drives the production admission paths
    # (boot/load, the record arm, the stop admission and the selection mechanism), so it needs the
    # same hardware boundaries as the continuous-capture harness it shares.
    "test_runtime_working_set_ownership",
    "test_storage_manager_load",
    # Packet A (A1.7) / Packet C: the real blocking persistence drain, which pulls the persistence
    # owners the load suite does not link.
    "test_storage_drain_restore",
    "test_offline_workspace_recovery",
    "test_offline_undo_recovery",
    "test_offline_boot_recovery",
    # Packet D (A4.1): the boot catalog reconcile outcome over the in-memory card.
    "test_storage_reconcile_index",
    # Packet C (A3.4a): the real MIDI/controller dispatch route, which pulls the button manager and the
    # configured action table.
    "test_drain_reentry_callback_safety",
    # restored-library-stability 1.3: the on-demand library observation links the load/restore owners
    # against the in-memory SD card, so it needs the same hardware boundaries as the load suite.
    "test_library_observation_session",
    # windowed-playback-generation 3.13 (fixture 2.12 integration): the note-edit owner is linked
    # **real** (`src/EditManager*.cpp`) alongside the window lane, so the suite needs the same
    # Arduino/SD hardware boundaries as the continuous-capture harness it builds on.
    "test_windowed_preview_integration",
    # runtime-working-set-ownership S7.5b.8a: the entry/focus/feedback boundary oracle links the real
    # button action, the real session/feedback seam and the real note-edit owner, so it needs the same
    # Arduino/SD hardware boundaries as the windowed-preview and continuous-capture harnesses.
    "test_note_edit_entry_boundary",
}
if env.get("PIOTEST_RUNNING_NAME") in _FIRMWARE_BOUNDARY_SUITES:
    env.Append(CPPPATH=["$PROJECT_DIR/test/test_support/firmware"])
    env.Append(CCFLAGS=["-ffunction-sections", "-fdata-sections"])
    env.Append(LINKFLAGS=["-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"])

# DEC-115 (pool containment): a **weak** native default for the projection gate's host symbol.
# Suites that compile a projection translation unit (`Utils/NoteUtils.cpp`, `Utils/DisplayWindowUtils.cpp`,
# `Loop/LoopVisualCache.cpp`) odr-use `MemoryMonitor::admitOperationMemory` through the inline projection
# admission helper in `include/Utils/MemoryMonitor.h`. A suite that includes
# `test_support/MemoryMonitorNativeDeps.cpp` links the real gate, which wins over a weak definition; every
# other suite takes this stub, so no suite needs per-file wiring and no include order matters. Native test
# builds only -- the device's gate is the real one in `src/Utils/MemoryMonitor.cpp`.
if env.get("PIOTEST_RUNNING_NAME"):
    env.BuildSources(
        os.path.join("$BUILD_DIR", "projection_gate_weak"),
        os.path.join("$PROJECT_DIR", "test", "test_support"),
        src_filter=["+<ProjectionGateWeakStub.cpp>"],
    )

# S7.5a envelope instruments: the same weak-default treatment for their telemetry entry points. A suite that
# compiles an owner reporting them (e.g. `LoopPlaybackRuntime::replenishCanonicalCaptureChunk`) without linking
# `src/Utils/RuntimeTimingTelemetry.cpp` takes the weak no-op; a suite that links the real module (or includes
# the .cpp, as `test_runtime_timing_telemetry` does) keeps the real accounting.
if env.get("PIOTEST_RUNNING_NAME"):
    env.BuildSources(
        os.path.join("$BUILD_DIR", "envelope_instr_weak"),
        os.path.join("$PROJECT_DIR", "test", "test_support"),
        src_filter=["+<EnvelopeInstrWeakStub.cpp>"],
    )
