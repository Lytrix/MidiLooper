"""Compile the production-route fixture against native hardware boundaries."""
import sys
Import("env")
# Suites that link production code pulling in the Arduino/SD hardware boundaries.
_FIRMWARE_BOUNDARY_SUITES = {
    "test_continuous_capture",
    "test_storage_manager_load",
}
if env.get("PIOTEST_RUNNING_NAME") in _FIRMWARE_BOUNDARY_SUITES:
    env.Append(CPPPATH=["$PROJECT_DIR/test/test_support/firmware"])
    env.Append(CCFLAGS=["-ffunction-sections", "-fdata-sections"])
    env.Append(LINKFLAGS=["-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"])
