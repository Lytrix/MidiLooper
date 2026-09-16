"""Compile the production-route fixture against native hardware boundaries."""
import sys
Import("env")
if env.get("PIOTEST_RUNNING_NAME") == "test_continuous_capture":
    env.Append(CPPPATH=["$PROJECT_DIR/test/test_support/firmware"])
    env.Append(CCFLAGS=["-ffunction-sections", "-fdata-sections"])
    env.Append(LINKFLAGS=["-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"])
