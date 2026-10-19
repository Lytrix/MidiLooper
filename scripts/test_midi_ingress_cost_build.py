"""Cost opt-in is inert by default and cannot silently select another env."""
import os
from pathlib import Path
import runpy
import unittest
from unittest.mock import patch

PROJECT = Path(__file__).resolve().parents[1]
class Env:
    def __init__(self, name="teensy41-capture-serial"):
        self.name, self.appended, self.scripts = name, [], []
    def subst(self, text):
        return text.replace("$PIOENV", self.name).replace("$PROJECT_DIR", str(PROJECT))
    def Append(self, **kwargs): self.appended.append(kwargs)
    def SConscript(self, path, **kwargs): self.scripts.append((path, kwargs))

class CostGateTests(unittest.TestCase):
    def load(self, value=None, name="teensy41-capture-serial"):
        env = Env(name)
        with patch.dict(os.environ, {}, clear=False):
            os.environ.pop("MIDI_INGRESS_COST_QUALIFICATION", None)
            if value is not None: os.environ["MIDI_INGRESS_COST_QUALIFICATION"] = value
            runpy.run_path(str(PROJECT / "scripts/midi_ingress_cost_build.py"),
                          init_globals={"Import": lambda _: None, "env": env})
        return env
    def test_unset_empty_and_zero_are_exact_noops(self):
        for value in (None, "", "0"):
            env = self.load(value)
            self.assertEqual(env.appended, []); self.assertEqual(env.scripts, [])
    def test_one_appends_both_gates_and_existing_pinned_middleware(self):
        env = self.load("1")
        self.assertEqual(env.appended, [dict(CPPDEFINES=[("MIDI_INGRESS_RECEIPTS_ENABLED", 1),
            ("MIDI_INGRESS_COST_QUALIFICATION", 1)]), dict(CCFLAGS=["-fstack-usage"])])
        self.assertEqual(env.scripts, [(str(PROJECT / "scripts/midi_ingress_build.py"), {"exports": {"env": env}})])
    def test_invalid_values_refuse(self):
        for value in ("true", " 1", "1 ", "2", "-1"):
            with self.assertRaisesRegex(RuntimeError, "must be 0 or 1"): self.load(value)
    def test_enabled_wrong_env_refuses(self):
        for name in ("native", "teensy41", "teensy41-capture-serial-ingress"):
            with self.assertRaisesRegex(RuntimeError, "requires teensy41-capture-serial"):
                self.load("1", name)
    def test_off_does_not_touch_other_environments(self):
        env = self.load("0", "teensy41")
        self.assertFalse(env.appended or env.scripts)
if __name__ == "__main__": unittest.main()
