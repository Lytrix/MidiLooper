# Approved post-Record handoff upload — 2026-10-25

Owner approved uploading the successfully built shipping image. No source changes/rebuild in this step.

- Env `teensy41-capture-serial`; HEX `/Users/eelkejager/Library/Caches/pio-build-250513-215524-teensy41/teensy41-capture-serial/firmware.hex`.
- SHA256 before/after `74392e15f3d2184f3c3948faee5715aca01c69178d86f313b5b7bebf18a4af7f`.
- No-reset/no-content managed preflight: `captures/post-record-upload-preflight-01/shutdown-result.json`, complete/drains2/oktrue, verified persistence idle.
- Explicit `/opt/homebrew/bin/teensy_loader_cli --mcu=TEENSY41 -w -v <exactHEX>`; version2.3 confirmed in transcript. No competing loader/capture observed.134-baud serial request entered HalfKay without PROGRAM press.
- `captures/post-record-upload-01/loader.log`:1,307,648bytes read, HalfKay found, Programming, Booting, exit0.
- Fresh serial capture `captures/post-record-upload-boot-01/session_20261025_120930.log`: `boot_reset,crash=0` at3,005,017us, `BOOT,load,ok`, boot setup complete.
- Independent USB enumeration: `/dev/cu.usbmodem154944801`,16C0:0489,Teensy MIDI.
- Post-upload managed shutdown `captures/post-record-upload-boot-01/shutdown-result.json`: complete/drains2/oktrue, verified persistence idle. Board left stopped.

Installed image is now74392e15 (replaces01f70896). No recording/overdub stimulus, loop selection, clear, quarantine or manual SD mutation. Normal boot restore/persistence drain may write firmware metadata; no byte-identical SD claim. Upload/boot PASS only: retained one-press device response/timing is not yet tested, and the measured normalization delay has not been optimized. Native3439pass/7skip, build/RAM1/review evidence from EXECUTION.md remains applicable to this exact image.
