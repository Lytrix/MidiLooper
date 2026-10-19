"""Explicit diagnostic opt-in inside the default capture environment only."""
import os

Import("env")

value = os.environ.get("MIDI_INGRESS_COST_QUALIFICATION", "0")
if value not in ("", "0", "1"):
    raise RuntimeError("MIDI_INGRESS_COST_QUALIFICATION must be 0 or 1")
if value == "1":
    if env.subst("$PIOENV") != "teensy41-capture-serial":
        raise RuntimeError("Ingress-cost opt-in requires teensy41-capture-serial")
    env.Append(CPPDEFINES=[("MIDI_INGRESS_RECEIPTS_ENABLED", 1),
                          ("MIDI_INGRESS_COST_QUALIFICATION", 1)])
    env.Append(CCFLAGS=["-fstack-usage"])
    # Existing middleware validates all installed hashes before writing copies.
    env.SConscript(env.subst("$PROJECT_DIR/scripts/midi_ingress_build.py"), exports={"env": env})
