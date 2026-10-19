"""Pinned, insertion-only receiver receipt hooks for Teensyduino 1.159.0.

No filesystem access or build-system policy lives here. Call instrument() with
UTF-8 source text read without newline conversion; write its result to a build
copy, never to the installed package. All anchors are validated before output.
extract_function_body() returns the actual brace-inclusive body for native
platform-boundary fixtures, not a copied receiver state machine.
"""

import hashlib
import re
from types import MappingProxyType

FRAMEWORK_VERSION = "1.159.0"
USB_DEVICE_SOURCE = "cores/teensy4/usb_midi.c"
DIN_SOURCE = "cores/teensy4/HardwareSerial.cpp"
USB_HOST_SOURCE = "libraries/USBHost_t36/midi.cpp"
SOURCE_SHA256 = MappingProxyType({
    USB_DEVICE_SOURCE: "6e3e1ebb1c677c7f04bf92be06a3dd33e779b518ccded7135a04196b36b476d5",
    DIN_SOURCE: "54f61046d7f09e834214dbc024b299731e7b771f351483bf94d58ece09ae8571",
    USB_HOST_SOURCE: "e6933d23485cfe8d80f4cdb5379f665ab2f501c5143ab8b0b34fdca1a4492ce6",
})
ROUTES = MappingProxyType({USB_DEVICE_SOURCE: 0, DIN_SOURCE: 1, USB_HOST_SOURCE: 2})
HOOK_INCLUDE = '#include "Utils/MidiIngressHooks.h"\n'


class SourceDriftError(ValueError):
    """Unsupported source, hash drift, repeated instrumentation or anchor drift."""


def _code_mask(text):
    """Mask comments and string/character literals, preserving offsets/newlines."""
    out = list(text)
    i = 0
    while i < len(text):
        start = i
        if text.startswith("//", i):
            end = text.find("\n", i + 2)
            i = len(text) if end < 0 else end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            if end < 0:
                raise ValueError("unterminated block comment")
            i = end + 2
        elif text.startswith('R"', i):
            # C++ raw strings, including prefixed u8R/LR forms (prefix remains).
            end = text.find("(", i + 2)
            if end < 0 or end - (i + 2) > 16:
                raise ValueError("invalid raw string delimiter")
            delimiter = text[i + 2:end]
            closing = ")" + delimiter + '"'
            end = text.find(closing, end + 1)
            if end < 0:
                raise ValueError("unterminated raw string")
            i = end + len(closing)
        elif text[i] in "\"'":
            quote = text[i]
            i += 1
            while i < len(text):
                if text[i] == "\\":
                    i += 2
                elif text[i] == quote:
                    i += 1
                    break
                else:
                    i += 1
            else:
                raise ValueError("unterminated quoted literal")
        else:
            i += 1
            continue
        for n in range(start, min(i, len(text))):
            if text[n] != "\n":
                out[n] = " "
    return "".join(out)


def _balanced_end(mask, start, opening, closing):
    depth = 0
    for i in range(start, len(mask)):
        if mask[i] == opening:
            depth += 1
        elif mask[i] == closing:
            depth -= 1
            if depth == 0:
                return i + 1
    raise ValueError("unbalanced " + opening + closing)


def _function_span(source_text, function_name):
    """Find exactly one definition; reject declarations, calls and overloads."""
    if not re.fullmatch(r"[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*", function_name):
        raise ValueError("expected a named C/C++ function")
    mask = _code_mask(source_text)
    pattern = r"(?<![\w:])" + re.escape(function_name) + r"\s*\("
    spans = []
    for match in re.finditer(pattern, mask):
        end = _balanced_end(mask, mask.index("(", match.start(), match.end()), "(", ")")
        # The pinned receiver definitions have no trailing qualifiers. Support
        # common C++ const/noexcept/override/final qualifiers for fixture use.
        suffix = re.match(r"\s*(?:(?:const|noexcept|override|final)\s*)*\{", mask[end:])
        if suffix:
            start = end + suffix.end() - 1
            spans.append((start, _balanced_end(mask, start, "{", "}")))
    if len(spans) != 1:
        raise ValueError(f"{function_name}: expected one definition, found {len(spans)}")
    return spans[0]


def extract_function_body(source_text, function_name):
    """Return the unique function's body, including its opening/closing braces.

    Declarations and calls are ignored. Nested braces inside comments, escaped
    string/character literals and C++ raw strings do not terminate the body.
    Ambiguous overloads, missing definitions and malformed input raise ValueError.
    This is a bounded source-fixture helper, not a general C++ parser (constructor
    initializer lists, function-try-blocks and templates are outside its contract).
    """
    start, end = _function_span(source_text, function_name)
    return source_text[start:end]


# Each entry is (function or None for file scope, exact anchor, before, after).
# Only new lines are inserted: every original byte, license, payload operation
# and relative ordering survives. Guards are per-function, never replace-all.
def _before(function, anchor, hook):
    return (function, anchor, hook, "")


def _after(function, anchor, hook):
    return (function, anchor, "", hook)


_USB_EDITS = (
    _after(None, '#include "usb_midi.h"\n', HOOK_INCLUDE),
    _before("usb_midi_configure", "\trx_head = 0;\n", "\tMIDI_INGRESS_RESET(0, 0, RX_NUM);\n"),
    _after("rx_event", "\tif (len > 0) {\n", "\t\tMIDI_INGRESS_PACKET(0, 0, RX_NUM);\n"),
    _before("rx_event", "\t\trx_head = head;\n", "\t\tMIDI_INGRESS_PUBLISH(0, 0, i, 0, len / 4);\n"),
    _before("usb_midi_read_message", "\tuint32_t n = 0;\n", "\tMIDI_INGRESS_CLEAR_LAST(0, 0);\n"),
    _before("usb_midi_read_message", "\t\trx_index[i] += 4;\n", "\t\tMIDI_INGRESS_CONSUME(0, 0, i, rx_index[i] / 4);\n"),
)
_HOST_EDITS = (
    _after(None, '#include "USBHost_t36.h"  // Read this header first for key info\n', HOOK_INCLUDE),
    _before("MIDIDeviceBase::claim", "\trx_head = 0;\n", "\tMIDI_INGRESS_RESET(2, this, rx_queue_size);\n"),
    _before("MIDIDeviceBase::rx_data", "\tfor (uint32_t i=0; i < len; i++) {\n", "\tMIDI_INGRESS_PACKET(2, this, rx_queue_size);\n"),
    _after("MIDIDeviceBase::rx_data", "\t\t\trx_queue[head] = msg;\n", "\t\t\tMIDI_INGRESS_PUBLISH(2, this, head, i, 1);\n"),
    _before("MIDIDeviceBase::disconnect", "\t// should rx_queue be cleared?\n", "\tMIDI_INGRESS_INVALIDATE(2, this);\n"),
    _before("MIDIDeviceBase::read", "\tuint32_t n, head, tail, avail, ch, type1, type2, b1;\n", "\tMIDI_INGRESS_CLEAR_LAST(2, this);\n"),
    _before("MIDIDeviceBase::read", "\trx_tail = tail;\n", "\tMIDI_INGRESS_CONSUME(2, this, tail, 0);\n"),
)
_DIN_EDITS = (
    _after(None, '#include "HardwareSerial.h"\n', HOOK_INCLUDE),
    *(_before("HardwareSerialIMXRT::" + function, "\trx_buffer_head_ = 0;\n",
              "\tMIDI_INGRESS_RESET(1, this, rx_buffer_total_size_);\n")
      for function in ("begin", "end", "addMemoryForRead")),
    _before("HardwareSerialIMXRT::clear", "\trx_buffer_head_ = rx_buffer_tail_;\n", "\tMIDI_INGRESS_RESET(1, this, rx_buffer_total_size_);\n"),
    _before("HardwareSerialIMXRT::peek", "\t\t\t\trx_buffer_head_ = 1;\n",
            "\t\t\t\tMIDI_INGRESS_LATE_PACKET(1, this, rx_buffer_total_size_);\n"
            "\t\t\t\tMIDI_INGRESS_PUBLISH(1, this, 1, 0, 1);\n"),
    _before("HardwareSerialIMXRT::read", "\tIMXRT_LPUART_t *port = (IMXRT_LPUART_t *)port_addr;\n", "\tMIDI_INGRESS_CLEAR_LAST(1, this);\n"),
    _after("HardwareSerialIMXRT::read", "\t\t\tc = port->DATA & 0x3ff;\t\t// Use only up to 10 bits of data\n",
           "\t\t\tif (c >= 0) MIDI_INGRESS_DIRECT(1, this);\n"),
    _before("HardwareSerialIMXRT::read", "\trx_buffer_tail_ = tail;\n", "\tMIDI_INGRESS_CONSUME(1, this, tail, 0);\n"),
    _after("HardwareSerialIMXRT::IRQHandler", "\t\tif (avail) {\n", "\t\t\tMIDI_INGRESS_PACKET(1, this, rx_buffer_total_size_);\n"),
    # Reuse the original acceptance branch: no extra volatile tail read.
    _before("HardwareSerialIMXRT::IRQHandler", "\t\t\t} while (--avail > 0) ;\n",
            "\t\t\t\telse MIDI_INGRESS_INVALIDATE(1, this);\n"),
    # WATER's three-bit avail is 1..7, decreases once per FIFO byte. 7-avail
    # is a strictly increasing 0..6 transfer offset, including dropped bytes;
    # using this existing counter avoids extra locals in disabled builds.
    _after("HardwareSerialIMXRT::IRQHandler", "\t\t\t\t\thead = newhead;\n",
           "\t\t\t\t\tMIDI_INGRESS_PUBLISH(1, this, head, 7 - avail, 1);\n"),
)
_EDITS = MappingProxyType({USB_DEVICE_SOURCE: _USB_EDITS, DIN_SOURCE: _DIN_EDITS, USB_HOST_SOURCE: _HOST_EDITS})


def instrument(relative_path, source_text):
    """Return a deterministically instrumented copy or fail closed before edits.

    Paths must exactly match SOURCE_SHA256 keys (no suffix/absolute matching).
    The SHA256 covers the full UTF-8 input, including licenses and line endings.
    Double instrumentation is refused explicitly, not silently made idempotent.
    """
    if relative_path not in SOURCE_SHA256:
        raise SourceDriftError(f"unsupported receiver source: {relative_path}")
    if "MIDI_INGRESS_" in source_text or "Utils/MidiIngressHooks.h" in source_text:
        raise SourceDriftError(f"{relative_path}: already instrumented")
    actual = hashlib.sha256(source_text.encode("utf-8")).hexdigest()
    if actual != SOURCE_SHA256[relative_path]:
        raise SourceDriftError(f"{relative_path}: SHA256 drift: expected {SOURCE_SHA256[relative_path]}, got {actual}")
    insertions = []
    for function, anchor, before, after in _EDITS[relative_path]:
        try:
            start, end = (0, len(source_text)) if function is None else _function_span(source_text, function)
        except ValueError as error:
            raise SourceDriftError(f"{relative_path}: {error}") from error
        window = source_text[start:end]
        count = window.count(anchor)
        if count != 1:
            raise SourceDriftError(f"{relative_path}: {function or 'file'} anchor {anchor!r}: expected once, found {count}")
        position = start + window.index(anchor)
        if before:
            insertions.append((position, before))
        if after:
            insertions.append((position + len(anchor), after))
    # Apply only after every hash/function/anchor guard has succeeded.
    result = source_text
    for position, addition in sorted(insertions, reverse=True):
        result = result[:position] + addition + result[position:]
    return result
