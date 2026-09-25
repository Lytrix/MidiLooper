`s13_teensy_lsv1.bin` is a synthetic HITL two-bar recording from authorized track 6
slot 4, captured on 2026-09-25 in `captures/s13-slope-slot4-04/`. It contains only
generated notes, no user recording. This is the LSV1 body (outer epoch header and
SAVE token removed), using the Teensy 20-byte MidiEvent ABI, not host serialization.
The 62 effective events / 1,536 ticks match the stopped RAM dump. Used as a golden
host-decoder interoperability fixture; does not establish intended-stimulus parity.
