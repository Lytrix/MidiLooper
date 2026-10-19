"""Host checks for pinned receiver transforms; installed sources are read-only.

Set MIDI_INGRESS_FRAMEWORK_ROOT to a Teensyduino 1.159.0 package if needed.
Only compiler intermediates in TemporaryDirectory are written, never fixtures
or generated receiver files in the repository/installed framework.
"""

import sys

# Keep even a plain script invocation from creating repository __pycache__.
sys.dont_write_bytecode = True

import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import midi_ingress_sources as ingress

ROOT = Path(__file__).resolve().parents[1]
FRAMEWORK = Path(os.environ.get(
    "MIDI_INGRESS_FRAMEWORK_ROOT",
    str(Path.home() / ".platformio/packages/framework-arduinoteensy"),
))


def without_hooks(text):
    return "".join(line for line in text.splitlines(keepends=True)
                   if "MIDI_INGRESS_" not in line and "Utils/MidiIngressHooks.h" not in line)


class ExtractionTests(unittest.TestCase):
    def test_balanced_literals_comments_raw_strings_and_prototype(self):
        body = r'''{
    char c = '}'; const char *s = "escaped \" } {";
    const char *raw = u8R"tag({ } // /* \" )tag";
    // } pretend foo() {
    /* } { foo() { */
    if (1) { return; }
}'''
        text = "void foo();\nvoid bar() { foo(); }\nvoid foo() " + body + "\nvoid last() {}"
        self.assertEqual(ingress.extract_function_body(text, "foo"), body)

    def test_qualified_name_and_multiline_arguments(self):
        text = "void X::foo(\nint x, void (*fn)(int)\n) const noexcept { if (x) { fn(x); } }"
        self.assertEqual(ingress.extract_function_body(text, "X::foo"), "{ if (x) { fn(x); } }")
        with self.assertRaises(ValueError):
            ingress.extract_function_body(text, "foo")

    def test_comment_and_string_fake_definitions_ignored(self):
        text = '/* void f() { } */ const char *s="void f() {}"; void f() { return; }'
        self.assertEqual(ingress.extract_function_body(text, "f"), "{ return; }")

    def test_missing_prototype_call_and_overload_rejected(self):
        for text in ("void f();", "void g() { f(); }", "void f() {} void f(int x) {}"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                ingress.extract_function_body(text, "f")

    def test_malformed_source_and_invalid_name_rejected(self):
        for text in ('void f() {', 'void f( {', 'void f() { /*', 'void f() { "oops',
                     'void f() { R"x(oops'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                ingress.extract_function_body(text, "f")
        with self.assertRaises(ValueError):
            ingress.extract_function_body("void f() {}", "f.*")


class RealSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not all((FRAMEWORK / name).is_file() for name in ingress.SOURCE_SHA256):
            raise unittest.SkipTest("pinned framework not installed; set MIDI_INGRESS_FRAMEWORK_ROOT")
        cls.original = {name: (FRAMEWORK / name).read_bytes().decode("utf-8")
                        for name in ingress.SOURCE_SHA256}
        cls.instrumented = {name: ingress.instrument(name, text)
                            for name, text in cls.original.items()}

    def body(self, path, name):
        return ingress.extract_function_body(self.instrumented[path], name)

    def ordered(self, text, *parts):
        positions = [text.index(part) for part in parts]
        self.assertEqual(positions, sorted(positions), parts)
        self.assertEqual(len(positions), len(set(positions)))

    def test_exact_pinned_hashes(self):
        for path, text in self.original.items():
            with self.subTest(path=path):
                self.assertEqual(hashlib.sha256(text.encode()).hexdigest(), ingress.SOURCE_SHA256[path])

    def test_deterministic_insertions_only_and_licenses_untouched(self):
        for path, original in self.original.items():
            with self.subTest(path=path):
                result = self.instrumented[path]
                self.assertEqual(result, ingress.instrument(path, original))
                self.assertEqual(without_hooks(result), original)
                license_end = original.index("*/") + 2
                self.assertEqual(result[:license_end], original[:license_end])
                self.assertEqual(result.count(ingress.HOOK_INCLUDE), 1)

    def test_expected_hook_counts_and_route_owner(self):
        for path, expected in ((ingress.USB_DEVICE_SOURCE, 5), (ingress.DIN_SOURCE, 12),
                               (ingress.USB_HOST_SOURCE, 6)):
            with self.subTest(path=path):
                hooks = re.findall(r"MIDI_INGRESS_\w+\([^;]+\);", self.instrumented[path])
                self.assertEqual(len(hooks), expected)
                owner = "0" if ingress.ROUTES[path] == 0 else "this"
                self.assertTrue(all(hook.split("(")[1].startswith(f"{ingress.ROUTES[path]}, {owner},")
                                    or hook.endswith(f"({ingress.ROUTES[path]}, {owner});") for hook in hooks))

    def test_every_source_byte_mutation_and_newline_drift_rejected(self):
        for path, text in self.original.items():
            for mutated in (text + "\n", text.replace("Copyright", "CopyRight", 1),
                            text.replace("\n", "\r\n"), text[:-1]):
                with self.subTest(path=path, kind=mutated[:25]), self.assertRaisesRegex(
                        ingress.SourceDriftError, "SHA256 drift"):
                    ingress.instrument(path, mutated)

    def test_double_instrumentation_and_partial_markers_rejected(self):
        for path, text in self.original.items():
            for changed in (self.instrumented[path], text + "\n// MIDI_INGRESS_PACKET",
                            text + ingress.HOOK_INCLUDE):
                with self.subTest(path=path), self.assertRaisesRegex(
                        ingress.SourceDriftError, "already instrumented"):
                    ingress.instrument(path, changed)

    def test_wrong_unknown_and_absolute_paths_rejected(self):
        for name in ("usb_midi.c", "/" + ingress.USB_DEVICE_SOURCE, "../" + ingress.DIN_SOURCE):
            with self.subTest(name=name), self.assertRaisesRegex(ingress.SourceDriftError, "unsupported"):
                ingress.instrument(name, self.original[ingress.USB_DEVICE_SOURCE])
        with self.assertRaisesRegex(ingress.SourceDriftError, "SHA256 drift"):
            ingress.instrument(ingress.DIN_SOURCE, self.original[ingress.USB_DEVICE_SOURCE])

    def test_each_anchor_missing_or_duplicate_fails_even_if_hash_guard_bypassed(self):
        # White-box fault injection isolates the independent uniqueness guard:
        # production hashes remain immutable; only this mock accepts each mutant.
        for path, edits in ingress._EDITS.items():
            for function, anchor, _, _ in edits:
                text = self.original[path]
                start, end = (0, len(text)) if function is None else ingress._function_span(text, function)
                pos = start + text[start:end].index(anchor)
                for count, replacement in ((0, ""), (2, anchor + anchor)):
                    mutated = text[:pos] + replacement + text[pos + len(anchor):]
                    pins = {**ingress.SOURCE_SHA256, path: hashlib.sha256(mutated.encode()).hexdigest()}
                    with self.subTest(path=path, function=function, count=count), patch.object(
                            ingress, "SOURCE_SHA256", pins), self.assertRaises(ingress.SourceDriftError):
                        ingress.instrument(path, mutated)

    def test_usb_reset_packet_publication_and_zero_packet(self):
        reset = self.body(ingress.USB_DEVICE_SOURCE, "usb_midi_configure")
        self.ordered(reset, "MIDI_INGRESS_RESET(0, 0, RX_NUM);", "rx_head = 0;", "rx_queue_transfer(i)")
        rx = self.body(ingress.USB_DEVICE_SOURCE, "rx_event")
        self.ordered(rx, "if (len > 0)", "MIDI_INGRESS_PACKET(0, 0, RX_NUM);", "rx_count[i] = len;",
                     "MIDI_INGRESS_PUBLISH(0, 0, i, 0, len / 4);", "rx_head = head;")
        self.assertNotIn("MIDI_INGRESS_", rx[rx.index("} else {"):])

    def test_usb_consume_before_offset_advance_release_and_requeue(self):
        read = self.body(ingress.USB_DEVICE_SOURCE, "usb_midi_read_message")
        self.assertTrue(read.startswith("{\n\tMIDI_INGRESS_CLEAR_LAST(0, 0);\n"))
        self.ordered(read, "n = *(uint32_t *)p;", "MIDI_INGRESS_CONSUME(0, 0, i, rx_index[i] / 4);",
                     "rx_index[i] += 4;", "rx_tail = tail;", "rx_queue_transfer(i);")

    def test_host_claim_reset_only_at_original_queue_reset(self):
        claim = self.body(ingress.USB_HOST_SOURCE, "MIDIDeviceBase::claim")
        self.assertIn("MIDI_INGRESS_RESET(2, this, rx_queue_size);\n\trx_head = 0;", claim)
        self.assertNotIn("MIDI_INGRESS_", self.body(ingress.USB_HOST_SOURCE, "MIDIDeviceBase::init"))

    def test_host_packet_once_and_nonzero_words_publish_original_offsets(self):
        rx = self.body(ingress.USB_HOST_SOURCE, "MIDIDeviceBase::rx_data")
        self.ordered(rx, "MIDI_INGRESS_PACKET(2, this, rx_queue_size);", "for (uint32_t i=0;",
                     "if (msg)", "rx_queue[head] = msg;", "MIDI_INGRESS_PUBLISH(2, this, head, i, 1);",
                     "rx_head = head;")
        self.assertEqual(rx.count("MIDI_INGRESS_PACKET"), 1)
        self.assertIn("rx_tail = tail;", rx)

    def test_host_read_clear_and_consume_before_tail_release_and_callbacks(self):
        read = self.body(ingress.USB_HOST_SOURCE, "MIDIDeviceBase::read")
        self.assertTrue(read.startswith("{\n\tMIDI_INGRESS_CLEAR_LAST(2, this);\n"))
        self.ordered(read, "n = rx_queue[tail];", "MIDI_INGRESS_CONSUME(2, this, tail, 0);", "rx_tail = tail;")
        self.assertNotIn("MIDI_INGRESS_PACKET", read)

    def test_host_disconnect_invalidates_but_retains_payload_queue(self):
        body = self.body(ingress.USB_HOST_SOURCE, "MIDIDeviceBase::disconnect")
        self.assertIn("MIDI_INGRESS_INVALIDATE(2, this);", body)
        self.assertEqual(without_hooks(body), ingress.extract_function_body(
            self.original[ingress.USB_HOST_SOURCE], "MIDIDeviceBase::disconnect"))
        self.assertNotIn("rx_head =", body)
        self.assertNotIn("rx_tail =", body)

    def test_din_all_four_queue_reset_paths_capacity_after_resize(self):
        for name in ("begin", "end", "clear", "addMemoryForRead"):
            with self.subTest(name=name):
                body = self.body(ingress.DIN_SOURCE, "HardwareSerialIMXRT::" + name)
                self.assertEqual(body.count("MIDI_INGRESS_RESET(1, this, rx_buffer_total_size_);"), 1)
                self.ordered(body, "MIDI_INGRESS_RESET", "rx_buffer_head_ =")
                if name == "addMemoryForRead":
                    self.ordered(body, "rx_buffer_total_size_ = rx_buffer_size_;", "MIDI_INGRESS_RESET")
                if name == "end":
                    self.ordered(body, "if (!(hardware->ccm_register & hardware->ccm_value)) return;",
                                 "MIDI_INGRESS_RESET")

    def test_din_fifo_batch_publish_acceptance_and_drop_invalidation(self):
        body = self.body(ingress.DIN_SOURCE, "HardwareSerialIMXRT::IRQHandler")
        self.ordered(body, "if (avail)", "MIDI_INGRESS_PACKET(1, this, rx_buffer_total_size_);", "do {",
                     "if (newhead != rx_buffer_tail_)", "head = newhead;",
                     "MIDI_INGRESS_PUBLISH(1, this, head, 7 - avail, 1);",
                     "else MIDI_INGRESS_INVALIDATE(1, this);", "rx_buffer_head_ = head;")
        self.assertEqual(body.count("MIDI_INGRESS_PACKET"), 1)
        for batch in range(1, 8):
            offsets = [7 - avail for avail in range(batch, 0, -1)]
            self.assertEqual(offsets, sorted(set(offsets)))
            self.assertTrue(all(0 <= offset < 7 for offset in offsets))

    def test_din_read_clear_consume_correct_tail_and_late_direct_only(self):
        body = self.body(ingress.DIN_SOURCE, "HardwareSerialIMXRT::read")
        self.assertTrue(body.startswith("{\n\tMIDI_INGRESS_CLEAR_LAST(1, this);\n"))
        self.ordered(body, "if (++tail >= rx_buffer_total_size_)", "MIDI_INGRESS_CONSUME(1, this, tail, 0);",
                     "rx_buffer_tail_ = tail;")
        self.ordered(body, "c = port->DATA & 0x3ff;", "if (c >= 0) MIDI_INGRESS_DIRECT(1, this);",
                     "__enable_irq();")
        self.assertNotIn("MIDI_INGRESS_PACKET", body)
        self.assertNotIn("MIDI_INGRESS_PUBLISH", body)

    def test_din_empty_peek_late_packet_publish_without_consumption(self):
        body = self.body(ingress.DIN_SOURCE, "HardwareSerialIMXRT::peek")
        self.ordered(body, "c = port->DATA & 0x3ff;", "MIDI_INGRESS_LATE_PACKET(1, this, rx_buffer_total_size_);",
                     "MIDI_INGRESS_PUBLISH(1, this, 1, 0, 1);", "rx_buffer_head_ = 1;",
                     "rx_buffer_tail_ = 0;", "rx_buffer_[1] = c;")
        self.assertNotIn("MIDI_INGRESS_CONSUME", body)
        self.assertNotIn("MIDI_INGRESS_CLEAR_LAST", body)

    def test_extract_all_instrumented_function_bodies_preserves_original(self):
        for path, edits in ingress._EDITS.items():
            for name in {edit[0] for edit in edits if edit[0]}:
                with self.subTest(path=path, name=name):
                    self.assertEqual(without_hooks(self.body(path, name)),
                                     ingress.extract_function_body(self.original[path], name))

    def test_hook_calls_compile_as_c_and_cpp_with_profile_on_and_off(self):
        compiler = shutil.which("clang") or shutil.which("gcc")
        if not compiler:
            self.skipTest("no C/C++ compiler available")
        for language, paths in (("c", (ingress.USB_DEVICE_SOURCE,)),
                                ("c++", tuple(ingress.SOURCE_SHA256))):
            for enabled in (0, 1):
                # This checks the real ABI calls, independent of branch scaffolding;
                # full source preprocessing below preserves the actual else branch.
                hooks = "\n".join(re.sub(r"\belse\s+", "", line)
                                  for path in paths for line in self.instrumented[path].splitlines()
                                  if "MIDI_INGRESS_" in line)
                declarations = "int RX_NUM=6, rx_queue_size=400, rx_buffer_total_size_=64;\n" \
                    "int i=0, len=4, head=1, tail=1, avail=1, newhead=1, rx_buffer_tail_=0, c=0;\n" \
                    "int rx_index[6]={0};\n"
                source = ingress.HOOK_INCLUDE
                source += ("struct Fixture { void run() {\n" if language == "c++" else "void run(void) {\n")
                source += declarations + hooks + "\n}" + ("};\n" if language == "c++" else "\n")
                with self.subTest(language=language, enabled=enabled):
                    result = subprocess.run([compiler, "-x", language,
                                             "-std=c++17" if language == "c++" else "-std=c11",
                                             "-fsyntax-only", f"-DMIDI_INGRESS_RECEIPTS_ENABLED={enabled}",
                                             "-I", str(ROOT / "include"), "-"], input=source,
                                            capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stderr)

    def test_full_source_preprocessing_profile_on_and_off(self):
        compiler = shutil.which("clang") or shutil.which("gcc")
        if not compiler:
            self.skipTest("no preprocessor available")
        # Header stubs only, not replacement receiver logic. Preprocess actual
        # complete transformed files in C/C++ and verify disabled calls vanish.
        with tempfile.TemporaryDirectory(prefix="midi-ingress-", dir=os.environ.get("PI_SCRATCH_DIR")) as temp:
            directory = Path(temp)
            for text in self.original.values():
                for name in re.findall(r'^#include ["<]([^">]+)[">]', text, re.MULTILINE):
                    if name in ("string.h", "stdint.h"):
                        continue
                    header = directory / name
                    header.parent.mkdir(parents=True, exist_ok=True)
                    header.write_text("", encoding="utf-8")
            for path, source in self.instrumented.items():
                language = "c" if path == ingress.USB_DEVICE_SOURCE else "c++"
                for enabled in (0, 1):
                    with self.subTest(path=path, enabled=enabled):
                        result = subprocess.run([compiler, "-E", "-P", "-x", language,
                                                 "-DMIDI_INTERFACE=1", "-DARDUINO_TEENSY41=1",
                                                 f"-DMIDI_INGRESS_RECEIPTS_ENABLED={enabled}",
                                                 "-I", str(directory), "-I", str(ROOT / "include"), "-"],
                                                input=source, capture_output=True, text=True, timeout=30)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        self.assertNotIn("MIDI_INGRESS_", result.stdout)
                        if enabled:
                            self.assertIn("midi_ingress_reset(", result.stdout)
                            self.assertIn("midi_ingress_consume(", result.stdout)
                        else:
                            self.assertNotIn("midi_ingress_", result.stdout)


if __name__ == "__main__":
    unittest.main()
