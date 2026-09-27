"""Compile the production-route fixture against native hardware boundaries."""
import sys
Import("env")
# Suites that link production code pulling in the Arduino/SD hardware boundaries.
_FIRMWARE_BOUNDARY_SUITES = {
    "test_continuous_capture",
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
}
if env.get("PIOTEST_RUNNING_NAME") in _FIRMWARE_BOUNDARY_SUITES:
    env.Append(CPPPATH=["$PROJECT_DIR/test/test_support/firmware"])
    env.Append(CCFLAGS=["-ffunction-sections", "-fdata-sections"])
    env.Append(LINKFLAGS=["-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"])
