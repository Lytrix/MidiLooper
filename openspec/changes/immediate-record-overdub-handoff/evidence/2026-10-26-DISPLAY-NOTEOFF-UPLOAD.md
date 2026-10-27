# Approved display-retention image upload

User explicitly approved upload of the final display fix; no source/build change in this upload step. Branch `feature/unified-rolling-playback-path`.

- Environment: `teensy41-capture-serial`.
- Exact HEX: `/Users/eelkejager/Library/Caches/pio-build-250513-215524-teensy41/teensy41-capture-serial/firmware.hex`.
- SHA-256 verified before programming: **`7a5b42bb5887f43e49ba4d97eaf5e0b54708e4ab5e49dfe5aa3888068f9358d1`**.
- Prior verification: native3481total/7skip/3474pass/0fail, capture build SUCCESS, RAM1 padding3,352/floor840 unchanged.
- No uploader GUI/competing loader or serial owner found before upload. Explicit Homebrew `/opt/homebrew/bin/teensy_loader_cli`2.3 used, not PlatformIO's bundled2.2.
- Command: `teensy_loader_cli --mcu=TEENSY41 -w -v <exact HEX above>`;134-baud bootloader request reached HalfKay, no PROGRAM press needed.
- Loader read1,323,008bytes, completed Programming, printed **Booting**, and returned **exit0** (waited parent retained result).
- Independent USB evidence after programming: `/dev/cu.usbmodem154944801`, Teensy MIDI, **16C0:0489**, serial15494480.
- Fresh serial: `#CAP,1026005,DIAG,boot_reset,crash=0` and `#CAP,BOOT,load,ok`.
- Read-only post-boot status at device9.013397s: **STOPPED,pending0,queue0/4**, phase/order/ranked/canonical/prefix/tail/bridge/wrap all0.
- Session scratch evidence: `display-fix-upload-loader.log`, `display-fix-upload-boot.log`, `display-fix-upload-result.json`. No serial contention/error observed during this upload verification.

Installed image is now7a5b42bb… (replaces1131e857). No recording/overdub stimulus, clear, quarantine, delete/rename, explicit SD mutation, commit or push commanded. Firmware boot/restore may perform ordinary metadata persistence; no byte-identical-SD claim. Fresh physical display behavior/content/timing/memory acceptance remains open; upload/boot is not that qualification. The earlier NOTuploaded statements in the execution record are historical and superseded by this receipt.
