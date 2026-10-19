"""Generate pinned actual receiver bodies into BUILD_DIR for native fixtures.

No installed writes. Call generate(framework_dir, build_dir) from the native
suite registration, adding the returned directory to CPPPATH. The suite itself
includes the real receipt implementation; do not link that TU a second time.
"""
from pathlib import Path
import argparse
import hashlib

PARSER_SHA256 = {
    "MIDI.h": "99852ac870c6986900be250b60d51b572f8f4ef4a75ae11d909177bce59a74e1",
    "MIDI.hpp": "505480cb0b74138fa23dc5e578b2bf0e2801bca21c9da318461fa501146ccaff",
    "midi_Settings.h": "4c4d8726c2748125f71156327c6efb9d7f2c1897959395e7b0bc50e2101639ca",
    "midi_Defs.h": "b3d54c243f37d8acd9aa6299faddcdf13f21f5ab4de210e37bc467939c0f90ce",
    "midi_Message.h": "0b43cbd65e3a0bf1619ae31a228bb9e3fb353cb9f7959835eb3fb76befc465bd",
}
from midi_ingress_sources import (
    instrument, extract_function_body, USB_DEVICE_SOURCE, USB_HOST_SOURCE, DIN_SOURCE,
)

FUNCTIONS = {
    USB_DEVICE_SOURCE: (
        ("rx_event", "void rx_event(transfer_t *t)"),
        ("usb_midi_read_message", "uint32_t usb_midi_read_message()"),
    ),
    USB_HOST_SOURCE: (
        ("MIDIDeviceBase::rx_data", "void MIDIDeviceBase::rx_data(const Transfer_t *transfer)"),
        ("MIDIDeviceBase::read", "bool MIDIDeviceBase::read(uint8_t channel)"),
        ("MIDIDeviceBase::disconnect", "void MIDIDeviceBase::disconnect()"),
        ("MIDIDeviceBase::sysex_byte", "void MIDIDeviceBase::sysex_byte(uint8_t b)"),
    ),
    DIN_SOURCE: tuple(
        ("HardwareSerialIMXRT::" + name, result + " HardwareSerialIMXRT::" + name + "()")
        for name, result in (("IRQHandler", "void"), ("read", "int"), ("peek", "int"), ("clear", "void"))
    ),
}


def generate(framework_dir, build_dir):
    """Validate all sources before emitting anything; return fixture include dir."""
    framework_dir, build_dir = Path(framework_dir), Path(build_dir)
    for name, digest in PARSER_SHA256.items():
        if hashlib.sha256((framework_dir / "libraries/MIDI/src" / name).read_bytes()).hexdigest() != digest:
            raise ValueError("Unsupported MIDI parser source: " + name)
    outputs = {}
    for relative, functions in FUNCTIONS.items():
        original = framework_dir.joinpath(relative).read_bytes().decode("utf-8")
        hooked = instrument(relative, original)  # full-source hash and anchor guards
        label = {USB_DEVICE_SOURCE: "usb", USB_HOST_SOURCE: "host", DIN_SOURCE: "din"}[relative]
        for suffix, text in ((label, hooked), ("usb_missing", original)) if label == "usb" else ((label, hooked),):
            outputs[suffix + "_bodies.inc"] = (
                "// Generated from pinned " + relative + "; do not author or commit.\n" +
                "\n".join(signature + "\n" + extract_function_body(text, name)
                          for name, signature in functions) + "\n"
            )
    output_dir = build_dir / "midi_ingress_native_fixture"
    output_dir.mkdir(parents=True, exist_ok=True)
    for name, content in outputs.items():
        output_dir.joinpath(name).write_text(content, encoding="utf-8")
    return output_dir


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--framework-dir", required=True)
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    print(generate(args.framework_dir, args.build_dir))
