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
    # runtime-profiling-layer slice 3 (DEC-239): the DEC-231 preparation owners linked with the profiler on.
    "test_runtime_profiler_sites",
    # Immediate Record/overdub handoff: native-only proof/measurement suites reuse production owners.
    "test_immediate_capture_handoff",
    "test_immediate_visual_cache_probe",
    "test_frozen_record_reader_survival",
    # windowed-playback-generation 3.15e slice 3: the live-overlay edge oracle compiles the
    # rolling lane and links the real hot path; the finalization suite (rolling 0 by default)
    # cannot host these fixtures.
    "test_overlay_edge",
    # I5 — rolling Record diagnosis: the matched one-bar RecordPublished retirement reproducer compiles
    # the finalization suite's real Track/Loop/storage closure with the rolling reader enabled, so it
    # needs the same Arduino/SD hardware boundaries and the same dead-strip settings.
    "test_rolling_record_retirement",
    # revision-manifest-prototype C5.3: the designated track's manifest-backed committed lane over the same real
    # Track/Loop/storage closure with the rolling reader enabled.
    "test_revision_manifest_lane",
    "test_playback_read_witness",
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
    "test_live_capture_display_boundary",
    "test_reboot_undo_redo",
    # realtime-idle-owner-admission-bound slice 2: the windowed/whole-loop source parity oracle reuses
    # the live-capture display boundary closure so it exercises production `LoopContentResolution` over
    # production `Loop` state rather than a model.
    "test_overdub_source_window_parity",
    # runtime-working-set-ownership I4 (Set browser index ownership): the suite links the catalog SD
    # owners (`SetRevisionCatalog`, the legacy reconcile and the boot ensure) against the in-memory
    # card, so it needs the same Arduino/SD hardware boundaries.
    "test_set_catalog_index_ownership",
    "test_midi_input_clock_integration",
    "test_midi_ingress_receipts",
    "test_midi_ingress_cost_probe",
}
if env.get("PIOTEST_RUNNING_NAME") in _FIRMWARE_BOUNDARY_SUITES:
    env.Append(CPPPATH=["$PROJECT_DIR/test/test_support/firmware"])
    env.Append(CCFLAGS=["-ffunction-sections", "-fdata-sections"])
    env.Append(LINKFLAGS=["-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"])
    if env.get("PIOTEST_RUNNING_NAME") in {"test_live_capture_display_boundary", "test_immediate_visual_cache_probe", "test_reboot_undo_redo", "test_overdub_source_window_parity"}:
        env.Prepend(CPPPATH=["$PROJECT_DIR/test/test_live_capture_display_boundary/firmware"])
    if env.get("PIOTEST_RUNNING_NAME") == "test_midi_input_clock_integration":
        env.Prepend(CPPPATH=["$PROJECT_DIR/test/test_midi_input_clock_integration/firmware"])
    if env.get("PIOTEST_RUNNING_NAME") in {"test_midi_ingress_receipts", "test_midi_ingress_cost_probe"}:
        env.Prepend(CPPPATH=["$PROJECT_DIR/test/test_midi_ingress_receipts/firmware"])

# Actual pinned transport bodies, generated only under this suite's build dir.
if env.get("PIOTEST_RUNNING_NAME") == "test_midi_ingress_transport":
    from pathlib import Path
    sys.path.insert(0, env.subst("$PROJECT_DIR/scripts"))
    from midi_ingress_native_fixture import generate
    framework = Path(os.environ.get("MIDI_INGRESS_FRAMEWORK_ROOT",
        str(Path.home() / ".platformio/packages/framework-arduinoteensy")))
    generated = generate(framework, env.subst("$BUILD_DIR"))
    env.Prepend(CPPPATH=[str(generated), "$PROJECT_DIR/test/test_midi_ingress_transport",
                        str(framework / "libraries/MIDI/src")])

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

# I5 Capture phase assignment uses the identical production body on host/target.
# Out-of-line target placement avoids inline-COMDAT .flashmem section conflicts.
if env.get("PIOTEST_RUNNING_NAME"):
    env.BuildSources(
        os.path.join("$BUILD_DIR", "capture_phase_state"),
        os.path.join("$PROJECT_DIR", "src", "Loop"),
        src_filter=["+<CapturePhaseState.cpp>"],
    )
