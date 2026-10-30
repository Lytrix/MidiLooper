# Teensy firmware upload — macOS

## Preferred uploader

Use **Homebrew Teensy Loader CLI 2.3 or newer**, explicitly selected by path.
On the current Apple Silicon host this is `/opt/homebrew/bin/teensy_loader_cli`.
Check the installed version with `brew list --versions teensy_loader_cli` and
retain the actual loader version banner in the upload transcript. If absent or
older, arrange installation/update before proceeding; do not silently fall back
to PlatformIO's bundled loader. Do not overwrite package-managed binaries.

**Do not use the installed PlatformIO bundled CLI 2.2** at
`~/.platformio/packages/tool-teensy/teensy_loader_cli` for these firmware images.
Its parser rejects data reaching the **1 MiB** boundary, even though a Teensy 4.1
has room for the image. The observed `HEX parse error line 65553` is this host-side
limit, not a bad C++ instruction or evidence of insufficient ITCM. Other HEX
layouts can fail at a different line number. Do not truncate the HEX to evade it.

`pio run ... -t upload` currently selects `teensy-gui`, not the preferred CLI.
Its `SUCCESS` is not proof that the GUI accepted the HEX, finished programming,
or booted the requested image. Do not use this path as the default agent upload
procedure. A GUI/manual exception requires a checked compatible loader and its
own programming/boot evidence, not just the PlatformIO exit code.

## Procedure

1. Use `teensy41-capture-serial` unless another env is explicitly authorized.
   For device diagnosis, clean-build first; do not assume incremental artifacts
   have identical provenance. Use separate worktree/build directories for a
   historical commit; preserve the current branch and dependency revisions.
2. After a successful build and budget checks, **ask before flashing**, except
   for the existing dedicated-HITL exception. Hash the exact built HEX. Resolve
   its path from the selected build directory, not an assumed `.pio/build` path.
   **Run the native suite in parallel when this slice needs the host gate:** start
   `pio test -e native` in the background before or while building, then continue
   through flash, boot verification and HITL while it runs; collect the native
   result before checkoff. If PlatformIO's project lock serializes the commands,
   build first and start native before flashing. A native failure (or an
   `[ERRORED]` suite / lower count) still blocks acceptance unless it is the
   deliberate RED of the slice. Ordering and caveats:
   [`Testing-Workflow.mdc`](../../.cursor/rules/Testing-Workflow.mdc).
3. Disable Auto in any open Teensy Loader GUI. Use **one uploader at a time**.
   This uploader choice also applies to dedicated HITL evaluation. If a runner's
   `--build-upload` invokes the old `pio ... -t upload` path, build and flash
   separately with the preferred uploader, then run the authoritative preset
   without `--build-upload` and verify its pinned hash matches the flashed HEX.
   A build-env selection alone does not flash an image.
4. Start the preferred CLI with the exact HEX path. On macOS do not rely on CLI
   `-s`: version 2.3 reports `Soft reboot is not implemented for OSX`. Ask for a
   **short PROGRAM press** if HalfKay is absent. Never request a 15-second hold
   (factory erase) as routine recovery. Bound waiting; avoid aborting mid-write.

```bash
uploader=/opt/homebrew/bin/teensy_loader_cli
hex="/absolute/path/to/the/approved/build/firmware.hex"
shasum -a 256 "$hex"
"$uploader" --mcu=TEENSY41 -w -v "$hex"
```

5. Retain version, hash, full programming result, exit code and `Booting`.
   A parse error or `error writing to Teensy` is **not a completed upload**;
   do not classify its subsequent silence as a firmware boot failure. Recover
   through PROGRAM and retry the same checked image, not arbitrary source edits.
6. Verify **boot separately**: USB enumeration and fresh serial evidence such as
   `boot_reset,crash=0` and `BOOT,load,ok`. The expected MIDI+Serial identity is
   `16C0:0489` / `Teensy MIDI`. An ELF string scan alone is not a USB descriptor
   check; a different installed sketch can legitimately report `16C0:0483`.
   Capture early and allow normal boot/library restoration to settle. Follow the
   managed capture/drain rules before starting a content test.
7. Record the image left on the board. Normal boot recovery may write workspace
   metadata; no upload permission implies permission to quarantine/delete SD data.
8. **Retain the HEX (mandatory since 2026-10-29).** Builds here are routinely made on deliberately
   dirty trees, so the source tree does not identify an image. After every authorized flash, copy the
   exact programmed HEX into `captures/<image-id>/` and write beside it:
   `sha256.txt`, `git-head.txt`, `git-status.txt`, `git-diff.patch` (when the tree is dirty),
   `build-flags.txt` (env, `PLATFORMIO_BUILD_DIR`, RAM1/ITCM figures) and `upload.log`. The project
   build cache holds only the newest `firmware.hex`, so an overwritten build is unrecoverable.
   Rationale: four 2026-10-29 images (`852b15f7…`, `0dbf08c1…`, `3e79bd1b…`, `c06f2e43…`) are
   referenced by the DEC-193 playback regression but cannot be re-flashed or rebuilt exactly.

## Evidence and limits

[2026-10-21 boot/uploader diagnosis](../../openspec/changes/windowed-playback-generation/evidence/2026-10-21-boot-uploader-diagnosis/EVIDENCE.md)
records checksum validation, parser-only address-boundary probes, and a successful
16:24 historical qualification-image boot using CLI 2.3. The older CLI parse limit
is proven; it does **not** by itself explain every earlier no-USB observation
after a newer loader reported a completed write. Keep those findings separate.
