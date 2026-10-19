"""Exercise build middleware without SCons or installed-package writes."""
import sys
sys.dont_write_bytecode = True
import hashlib
import json
import os
from pathlib import Path
import runpy
import tempfile
import unittest
from midi_ingress_sources import SOURCE_SHA256, instrument

PROJECT = Path(__file__).resolve().parents[1]
FRAMEWORK = Path(os.environ.get("MIDI_INGRESS_FRAMEWORK_ROOT", str(Path.home() / ".platformio/packages/framework-arduinoteensy")))

class Node:
    def __init__(self, source, target=None):
        self.source, self.target = str(source), str(target or source)
    def srcnode(self): return Node(self.source)
    def get_abspath(self): return self.target

class Compile:
    def __init__(self): self.includes = []; self.objects = []
    def Prepend(self, CPPPATH): self.includes += CPPPATH
    def Object(self, **kwargs): self.objects.append(kwargs); return [kwargs]

class Env:
    def __init__(self, root): self.root = root; self.compiler = Compile(); self.callback = None
    def subst(self, name): return {"$PROJECT_DIR": str(PROJECT), "$BUILD_DIR": str(self.root / "build")}[name]
    def PioPlatform(self): return self
    def get_package_dir(self, name):
        assert name == "framework-arduinoteensy"
        return str(self.root / "framework")
    def AddBuildMiddleware(self, callback): self.callback = callback
    def Clone(self): return self.compiler

class MiddlewareTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=os.environ.get("PI_SCRATCH_DIR"))
        self.root = Path(self.temp.name); self.env = Env(self.root)
        self.package = self.root / "framework"; self.package.mkdir()
        (self.package / "package.json").write_text(json.dumps({"version": "1.159.0"}))
        for relative in SOURCE_SHA256:
            output = self.package / relative; output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes((FRAMEWORK / relative).read_bytes())
    def tearDown(self): self.temp.cleanup()
    def load(self):
        return runpy.run_path(str(PROJECT / "scripts/midi_ingress_build.py"),
                             init_globals={"Import": lambda name: None, "env": self.env})
    def test_all_originals_unchanged_and_generated_copies_exact(self):
        self.load()
        for relative, digest in SOURCE_SHA256.items():
            original = (self.package / relative).read_bytes()
            self.assertEqual(hashlib.sha256(original).hexdigest(), digest)
            self.assertEqual((self.root / "build/midi_ingress_sources" / relative).read_bytes(),
                             instrument(relative, original.decode()).encode())
    def test_source_node_replaced_with_original_target_and_include_directory(self):
        self.load()
        for relative in SOURCE_SHA256:
            target = self.root / "variant" / relative
            result = self.env.callback(self.env, Node(self.package / relative, target))
            self.assertEqual(result["target"], str(target) + ".o")
            self.assertEqual(result["source"], str(self.root / "build/midi_ingress_sources" / relative))
            self.assertIn(str((self.package / relative).parent), self.env.compiler.includes)
        unrelated = Node(self.root / "user/midi.cpp")
        self.assertIs(unrelated, self.env.callback(self.env, unrelated))
    def test_version_drift_fails_before_generating(self):
        (self.package / "package.json").write_text('{"version":"1.160.0"}')
        with self.assertRaisesRegex(RuntimeError, "Unsupported"): self.load()
        self.assertFalse((self.root / "build").exists())
    def test_last_source_drift_fails_before_any_copy(self):
        relative = list(SOURCE_SHA256)[-1]
        with (self.package / relative).open("ab") as handle: handle.write(b"\n// drift")
        with self.assertRaises(ValueError): self.load()
        self.assertFalse((self.root / "build").exists())
    def test_repeated_load_does_not_rewrite_identical_copies(self):
        self.load()
        copies = list((self.root / "build").rglob("*.c")) + list((self.root / "build").rglob("*.cpp"))
        times = [path.stat().st_mtime_ns for path in copies]
        self.load()
        self.assertEqual(times, [path.stat().st_mtime_ns for path in copies])

if __name__ == "__main__": unittest.main()
