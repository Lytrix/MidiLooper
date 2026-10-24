# 2.1 prepared pair — device gate, partial (bench-limited)

Date: 2026-10-24
Board: `/dev/cu.usbmodem154944801` (Teensy MIDI)
Bench workspace: `151444` (25-bar campaign tree; not empty)
Loader: Homebrew `teensy_loader_cli` 2.3 (2.2 rejected the image size earlier)

## Flash and boot

| image | HEX sha256 | loader | result |
|---|---|---|---|
| shipping | `e5a26dd68ecf9a46…` | 1,306,624 bytes, programmed, booting | CDC re-enumerated |
| cold rollback | `e7e4b4fc4934d833…` (`captures/device-images/cold-rollback-current.hex`) | 1,287,168 bytes, programmed, booting | CDC re-enumerated |

The 134-baud request put the board in HalfKay; `teensy_loader_cli --mcu=TEENSY41 -w` programmed both. The board is
left on the cold rollback image.

## Base preset runs

Same args on both images: `run --preset base --track 5 --loop-slot 1 --midi-channel 1 --build-env
teensy41-capture-serial --boot-reset --clock-source host --tempo-bpm 120`.

| run | result | stored overdub | stored second | distinct | display |
|---|---|---|---|---|---|
| shipping `2026-10-24-2.1-base-ship` | `INCONCLUSIVE_CAPTURE (EVIDENCE_MISSING)`; issue `record_stored_revt_missing` | 16 / 15, span 1296, bars 2/2 | 20 / 7, span 1209, bars 2/2 | 15, 7 | overdub/transport ok |
| cold `2026-10-24-2.1-base-cold` | `PASS` | 32 / 15, span 1296, bars 2/2 | 40 / 7, span 1209, bars 2/2 | 15, 7 | overdub/transport ok; RAM2 147,456 |
| cold rerun `2026-10-24-2.1-base-cold-2` | `PASS` | 32 / 15 | 40 / 7 | 15, 7 | same |

Both images record and overdub, and the **distinct inventory, spans and bar counts match** (15 and 7; 1296/1209
ticks; 2/2 bars). The shipping run keeps the same host evidence gate as the 1.4 shipping base
(`record_stored_revt_missing`); the cold image carries the REVT instrument and returns PASS.

## Why this is not yet a clean cold/shipping parity pair

- The **stored event totals differ** (shipping 16/20; cold 32/40) while the distinct inventories are identical. The
  cold run stores each note twice; the base preset's clear does not reset the committed passes (the cold rerun is
  unchanged at 32/40, not cumulative), and the runs did not start from the same slot state after the flash.
- The 1.4 **empty-bench** base pair remains the clean reference (record 32/32, overdub 16/16, second 8/8, loop 1536).
  This bench (`151444`) is the 25-bar campaign tree and cannot produce that comparison without parking it.
- The mixed switch (`accepted_sequence_mixed`) was **not** rerun: it is setup-limited (seed_b finalization release)
  and targets the campaign slots 4/5. The 2.1 playback route is covered by the native full suite
  (3432 / 7 skipped / 3425 passed) and the two base runs above.

## Metadata caveat

All three run `metadata.json` files pin `firmware_hex_sha256 =
e5a26dd68ecf9a46dedc85474e7fee98e2146b4ceb106b990015ef57c23260f8` because the runner records the last **build
artifact**, not the image actually flashed. For the cold runs the authoritative pin is the loader output plus the
cold HEX sha256 `e7e4b4fc4934d833…`; do not cite the cold metadata hash as the run identity.

## Status

**2.1 remains unchecked.** The device gate is partially executed with a named bench limitation, not waived. Next:
park the workspace (`!QUARANTINE_WORKSPACE`, restorable with `!RESTORE_WORKSPACE`) or an authorized clean-slot
procedure, then rerun the base pair and the mixed switch gesture on both images. The previous 1.4/1.5 quarantine was
session-authorized; this session has not yet authorized parking `151444`.

Rollback: shipping `e5a26dd6…` ↔ cold `e7e4b4fc…`; board left on cold.
