# Upload — 2026-10-28 11:3x — correction 4 (window-denied stops publish their companions)

Owner instruction: "yes dlash" (flash). Correction 4 closes the hole the diagnostic capture named: a stop that
kept its companion path on a **window** denial was still discarded by the owner-superseded branch at
`AtomicTransfer`.

## Image

- HEX: `/Users/eelkejager/Library/Caches/pio-build-250513-215524-teensy41/teensy41-capture-serial/firmware.hex`
- `sha256 551cac47c12d4a094bfd6b285c7ee9d1ce30d1bdd6d9630e0b105652a40e1405`
- 1,343,488 bytes (16.5% usage); no source newer than the HEX
- RAM1: `variables 96,192` / ITCM `code 390,568 + padding 2,648` (floor 840, 1,808 B headroom)

## Uploader

- `/opt/homebrew/bin/teensy_loader_cli` **2.3**, explicit path, one uploader.
- `Read … 1343488 bytes, 16.5% usage` → `Found HalfKay Bootloader` → `Programming……` → `Booting`, no parse or
  write error.
- The owner's capture reader (PID 59286, writing `captures/session_20261028_110951.log`, 2,008,335 B) was stopped
  with a graceful `SIGINT` immediately before the flash.

## Boot proof (separate from the uploader)

- `DIAG,boot_reset,crash=0`
- `BOOT,ram1,196608`, `BOOT,load,ok`
- no `CrashReport` row in the boot window

## Limits

Programming + boot only. The user-visible acceptance (overdubbed notes present after the early overdubs) needs the
owner's re-capture; the change stays un-archived until then. No SD/content action, no commit.
