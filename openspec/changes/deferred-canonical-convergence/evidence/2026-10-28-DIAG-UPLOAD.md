# Upload — 2026-10-28 11:0x — diagnostic image (Record-normalization denial naming)

Owner instruction: "ok do that" (flash the diagnostic image after the correction-3 session stalled in the
Record-normalization lane). The only behavioral difference from the correction-3 image is one extra field in an
existing warning row: `Deferred Record stop stalled: normalization <term> deny=<reason> cursor=<phase> …`.

## Image

- HEX: `/Users/eelkejager/Library/Caches/pio-build-250513-215524-teensy41/teensy41-capture-serial/firmware.hex`
- `sha256 876cadaf4d527bf1d35584bc213dda905c1c88529a825ac77824a4a7a893358b`
- 1,343,488 bytes (16.5% usage); no source newer than the HEX
- RAM1: `variables 96,192` / ITCM `code 390,568 + padding 2,648` (floor 840, 1,808 B headroom)

## Uploader

- `/opt/homebrew/bin/teensy_loader_cli` **2.3** (Homebrew), explicit path, one uploader.
- First attempt (foreground) timed out after 120 s waiting for HalfKay; no serial device was present at that
  moment. The board was in the bootloader on the retry:
  `Read … 1343488 bytes, 16.5% usage` → `Found HalfKay Bootloader` → `Programming……` → `Booting`, no parse error,
  no write error. Retry ran to completion; no abort mid-write.

## Boot proof (separate from the uploader)

- `#CAP,3005003,DIAG,boot_reset,crash=0`
- `#CAP,BOOT,load,start`, `#CAP,BOOT,ram1,196608`, `#CAP,BOOT,load,ok`
- no `CrashReport` row in the boot window

## Next

Owner re-capture: one Record (the same first-take shape if convenient), then a couple of overdubs. The stall row
(normalization wedge) now names the refusing predicate and the cursor phase; that name selects the repair.
No SD/content action, no stimulus, no transport command, no commit.
