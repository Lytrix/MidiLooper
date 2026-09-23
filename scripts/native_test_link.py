"""Compile the production-route fixture against native hardware boundaries."""
import sys
Import("env")
# Suites that link production code pulling in the Arduino/SD hardware boundaries.
_FIRMWARE_BOUNDARY_SUITES = {
    "test_continuous_capture",
    "test_storage_manager_load",
    # Packet A (A1.7) / Packet C: the real blocking persistence drain, which pulls the persistence
    # owners the load suite does not link.
    "test_storage_drain_restore",
    # Packet D (A4.1): the boot catalog reconcile outcome over the in-memory card.
    "test_storage_reconcile_index",
}
if env.get("PIOTEST_RUNNING_NAME") in _FIRMWARE_BOUNDARY_SUITES:
    env.Append(CPPPATH=["$PROJECT_DIR/test/test_support/firmware"])
    env.Append(CCFLAGS=["-ffunction-sections", "-fdata-sections"])
    env.Append(LINKFLAGS=["-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"])
