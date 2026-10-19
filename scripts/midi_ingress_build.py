"""Opt-in pinned receiver copies; never modify the installed framework."""
import json
import sys
from pathlib import Path

Import("env")
sys.path.insert(0, str(Path(env.subst("$PROJECT_DIR")) / "scripts"))
from midi_ingress_sources import FRAMEWORK_VERSION, SOURCE_SHA256, instrument

framework = Path(env.PioPlatform().get_package_dir("framework-arduinoteensy")).resolve()
if json.loads((framework / "package.json").read_text())["version"] != FRAMEWORK_VERSION:
    raise RuntimeError("Unsupported Teensy framework for ingress receipts")

# Validate every source before creating any copies, even if a library is unused.
copies = {}
for relative in SOURCE_SHA256:
    copies[relative] = instrument(relative, (framework / relative).read_bytes().decode("utf-8"))
root = Path(env.subst("$BUILD_DIR")) / "midi_ingress_sources"
for relative, text in copies.items():
    output = root / relative
    output.parent.mkdir(parents=True, exist_ok=True)
    encoded = text.encode("utf-8")
    if not output.exists() or output.read_bytes() != encoded:
        output.write_bytes(encoded)

# File nodes may belong to variant dirs; match the resolved original source.
originals = {str((framework / relative).resolve()): relative for relative in copies}
def receiver_copy(build_env, node):
    original = str(Path(node.srcnode().get_abspath()).resolve())
    relative = originals.get(original)
    if relative is None:
        return node
    compile_env = build_env.Clone()
    # Preserve quoted includes next to the original source (e.g. usb_desc.h).
    compile_env.Prepend(CPPPATH=[str((framework / relative).parent)])
    return compile_env.Object(target=node.get_abspath() + ".o",
                              source=str(root / relative))[0]

env.AddBuildMiddleware(receiver_copy)
