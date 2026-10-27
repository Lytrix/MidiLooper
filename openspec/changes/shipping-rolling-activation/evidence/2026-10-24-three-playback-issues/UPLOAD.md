# Approved shipping image upload — 2026-10-25

Owner approved upload after successful native/build verification. No new source edits or rebuild in this step; uploaded the previously built and hash-verified shipping image. No content test, slot selection, record/overdub, clear, quarantine or manual SD mutation.

- Environment: `teensy41-capture-serial`.
- HEX: `/Users/eelkejager/Library/Caches/pio-build-250513-215524-teensy41/teensy41-capture-serial/firmware.hex`.
- SHA256 before and after: `01f70896ba09add68bc905f7b7a2501601d62a0e30c9638754813e9d9531e91c`.
- Preflight: no-reset/no-content attach through `run_with_managed_capture`; `captures/three-issues-upload-preflight-01/shutdown-result.json` = complete, drains2, oktrue, verified persistence idle.
- Uploader: explicit `/opt/homebrew/bin/teensy_loader_cli`, Homebrew version2.3, transcript confirms `Teensy Loader, Command Line, Version 2.3`. No Teensy Loader GUI/capture process active before upload. CLI started with `--mcu=TEENSY41 -w -v <exactHEX>`; 134-baud serial request entered HalfKay without a PROGRAM press.
- Programming evidence: `captures/three-issues-upload-01/loader.log`, read1,307,648bytes, Found HalfKay Bootloader, Programming, Booting, exit0.
- Independent USB check: `/dev/cu.usbmodem154944801`, VID:PID16C0:0489, Teensy MIDI.
- Fresh serial capture: `captures/three-issues-upload-boot-01/session_20261025_111425.log`; retained early OLED initialization, `BOOT,load,start`, `BOOT,load,ok`, `#CAP,3005005,DIAG,boot_reset,crash=0`, boot setup completion and later progressing owner rows. Observation continued through ~30.6s device uptime.
- Post-upload managed shutdown: `captures/three-issues-upload-boot-01/shutdown-result.json` = complete, drains2, oktrue, verified persistence idle. Device left stopped; no extra reboot.

Upload/boot gate PASS only. This does not qualify odd33-bar continuation, stop timing, physical OLED freshness or the unresolved captured CanonicalTail phase5 prerequisite. Repair task and family-removal boxes stay open. Normal boot restore/persistence metadata writes are firmware behavior, not a claim of byte-identical SD state. Prior native3432pass/7skip and approved RAM1 evidence remain applicable because source/image unchanged.
