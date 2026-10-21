# Boot and uploader diagnosis — 2026-10-21

## Scope and installed image

Owner requested diagnosis, then an exact build/boot test of commit
`2b876c840e1ba39c2e963376d26676e8084cc04d` (2026-10-21 16:24:37 +0200,
preview lifecycle/parity). Owner separately approved diagnostic uploads and the
historical qualification-image upload. No production firmware source changed.

**Left on board:** that historical commit's
`teensy41-capture-serial-window-qualification`, HEX SHA-256
`b34b2bfcef2697840c999b80abf901c827e4051c961a6fcd5e9b63bdf402e0c2`.
Owner subsequently confirmed it appears to upload and boot perfectly.
Current working branch remains `feature/unified-rolling-playback-path` at
`519c447b`; no branch rollback. Historical detached worktree and distinct build
output are under session scratch. OLED library pinned clean at
`66663b29780a0cea7fa5eaa7f83690dfc318e426`; it needed a local clone because
`.gitmodules` is absent despite the tracked gitlink. No native tests run in this
diagnostic session; no content qualification or playback pass claimed.

## Proven host-side failure: bundled CLI parser boundary

Installed PlatformIO tool package `tool-teensy` 1.159.0 wraps an x86_64 CLI whose
banner is **Teensy Loader, Command Line, Version 2.2**. It rejects the historical
HEX with `Warning, HEX parse error line 65553` and exits before programming.

Independent full-file parsing verified every record length and checksum, no
address overlaps, and 81,690 lines spanning `0x60000000..0x6013f000` (exclusive
end). Historical line 65553 is:

```text
:10FFF000DEE70B9A02980B90026052B192880A9841
```

The active extended address is `0x600f0000`: this record ends at flash-relative
`0x100000` (1 MiB). Checksums also pass for both current clean HEX files.

Parser-only synthetic probes each had an extended-address record, a 16-byte
data record, and a deliberately malformed `:INVALID` final record. Both loaders
must reject before opening hardware; a line-2 failure isolates the address check,
a line-3 failure means the data passed and the deliberately invalid record failed.

| Data address | Bundled CLI 2.2 | Homebrew CLI 2.3 |
|---|---|---|
| `0x600fffe0` | rejects line 3 | rejects line 3 |
| `0x600ffff0` | **rejects line 2** | rejects line 3 |
| `0x60100000` | **rejects line 2** | rejects line 3 |

Mach-O section inspection corroborates 2.2's roughly 2 MiB BSS (two image/mask
buffers) versus 2.3's roughly 32 MiB. The binary's observed limit, not merely its
version string or a possibly different upstream source tag, is the evidence.
Do not truncate/rewrite valid firmware to accommodate this loader.

**Preferred procedure:** [Teensy firmware upload guide](../../../../../docs/Guides/TEENSY_FIRMWARE_UPLOAD.md).
Explicit Homebrew CLI 2.3+ path, GUI Auto disabled, one uploader, exact image hash,
PROGRAM recovery, programming result and separate boot verification. PlatformIO's
current GUI upload target can return `SUCCESS` without establishing these facts.
Homebrew 2.3 also explicitly reports soft reboot is not implemented for OSX.

## Historical build and boot result

Clean isolated qualification build succeeded: RAM1 variables **96,192 B**, ITCM
code **392,376 B** + padding **840 B**, locals **34,880 B**; RAM2 variables
**168,384 B**, EXTRAM **11,680 B**, flash code **1,195,856 B**.

The first modern-loader programming attempt failed partway with `error writing
to Teensy`; this was **not** counted as a boot test. The bundled retry produced
the parser error above. After GUI Auto was disabled and PROGRAM pressed, the
final **Homebrew CLI 2.3** upload read **1,306,624 bytes**, completed programming,
reported **Booting**, and exited **0**. Over a 45-second passive observation:

```text
USB: 16C0:0489 / Teensy MIDI
#CAP,854947,DIAG,boot_reset,crash=0
BOOT,load,start
BOOT,load,ok
```

104,109 serial bytes captured. Normal startup recovery ran; no content-test,
quarantine, delete, rename, or explicit SD-cleanup command was issued.

## Earlier current-image observations: do not over-attribute

At current branch HEAD, clean default HEX `d1e72da5842173e55c201b6c5fa3dc728cc134fbfd05701b070a390a5a554ac8`
booted (`crash=0`, `BOOT,load,ok`, MIDI+Serial). Clean qualification HEX
`ea6e8a02cb99ba65adb6792dfa7125d45ffe1a16bcee23c71839edd75e1cddf5`
did not enumerate in the observed windows, including after CLI 2.3 reported
programming-through-Booting/exit 0. The initial serial-only device was another
sketch, as confirmed by the owner, not proof of a project crash-report mode.

Owner-approved scratch-only diagnostics replaced the call to `__libc_init_array`
with a Thumb self-loop at `0x600015da`, leaving interrupts enabled. The default
control enumerated; the current qualification variant did not. This narrows that
observation to the pre-constructor/core/USB/image-layout boundary, not application
setup, playback, or SD restore. It does not establish an exact faulting instruction
or prove flash contents were read back and verified; the loader transcript alone
is not readback. Do not call the old-parser defect the sole cause of all no-USB
observations, or patch startup based on this evidence alone.

The only `src/`/`include/` difference between the successful historical commit
and current HEAD is `c94eb011` (wrap continuation): 23 inserted lines in two
files. This is a regression candidate, **not a proven fault mechanism**. Its
“zero ITCM” description means **zero added ITCM bytes**, not missing ITCM;
both qualification builds report 392,376 B code + 840 B padding.

An independent static startup check found a four-byte short copy of final unwind
metadata due to orphan `.fini` placement, but all executable ITCM code is copied;
that issue was not proven responsible and was not changed.

No task checkbox changed. All-route cutover, sweep device legs and hardware
service qualification remain open. No architecture/ownership decision made.

## Subsequent latest-HEAD retry — installed-image update

Owner next requested latest HEAD build/upload. Branch remained at `519c447b`;
documentation edits preserved, no production code diff. Clean qualification build
passes the same budget: code392,376 + padding840, locals34,880. Exact HEX SHA-256:
`3a93701d13d7b0b73ad302bd088314fae016de37d592825bbb2e6263079412ea`.
After post-build upload approval, GUI Auto off and a short PROGRAM press, explicit
Homebrew CLI2.3 read1,307,648bytes, completed programming, reported Booting/exit0.
Over45s passive observation: no Teensy serial port enumerated and zero serial bytes.
This attempt did not encounter the old parser error; it reproduces the latest
qualification no-USB observation with the preferred uploader after the historical
control had booted. No native/content tests, additional flash or rollback followed.
**Current leave-behind supersedes the historical leave-behind above: latest HEAD
qualification is last programmed, board not enumerating.** Exact root cause remains
unresolved; successful programming transcript is still not flash readback proof.
Transcripts remain in session scratch as `latest-build.log`,
`boot-latest-modern-final-upload.log` and `boot-latest-modern-final-serial.log`.

## Layout-only +8 diagnostic — completed, no USB

Owner approved the proposed layout-only test, then separately approved upload
after successful build and binary verification. Detached scratch worktree at
HEAD519c447b with the same pinned OLED library; only linker change is8bytes added
at end of `.text.progmem`. Separate clean build passes unchanged RAM1 budget.

Fresh compilation changed the RTC fallback timestamp from `__TIME__` (two bytes
in an ITCM literal). Reusing the control RTC object was undone by recompilation;
therefore objcopy generated a derived diagnostic ELF using the original control's
entire `.text.itcm` section at the variant's shifted flash LMA. Final comparison
against preserved failing control proves:

- `.text.headers`, `.text.itcm`, `.data`, `.fini`, `.ARM.exidx` byte-identical;
- `.text.progmem` is original constants plus8padding bytes;
- `.text.code` differs only at bytes0x600015f0/0x600015f1/0x600015fc, the two
  relocated startup literals for ITCM/data load sources;
- RAM section VMAs/sizes unchanged; ITCM LMA0x600cb8f8→0x600cb900,
  data LMA0x6012b5b4→0x6012b5bc; all HEX record checksums valid.

Final diagnostic HEX SHA-256:
`3f60a588de1e071a1e839d6f755f286cb202bb5fa8a132395ab3ad14d93b492c`.

First CLI2.3 attempt waited for HalfKay and hit the45s process limit mid-write;
this is an incomplete upload, not boot evidence. Same-image retry after another
PROGRAM press, GUI Auto off and180s loader allowance completed programming,
reported Booting/exit0, then45s observation showed **no USB and zero serial bytes**.
No content stimulus/SD-cleanup command or further flash was issued.

**Latest leave-behind: this scratch +8 layout diagnostic; device not enumerating.**
Main working tree firmware/config are unmodified. This test does not support
eight-byte load-source alignment padding as a cure; it does not rule out every
other layout-sensitive fault or establish an exact failing startup stage. No
production fix, native rerun or playback qualification follows.

Scratch artifacts: `layout-test-packet.md`, `layout-probe-head/`,
`layout-probe-build/`, `boot-images/latest-before-layout-test.{elf,hex}`,
`boot-images/latest-layout-plus8.{elf,hex}`, `boot-latest-layout-plus8-upload.log`
and `boot-latest-layout-plus8-retry-{upload,serial}.log`.

## PSRAM bypass with pre-constructor pause — completed, no USB

Owner approved this test and separately approved upload after validation. Use
the original failing latest control ELF/HEX3a93701d…, not the +8layout variant.
Scratch script `make_psram_bypass.py` checks exact instruction bytes and replaces:

-0x600015ac: call to `configure_external_ram` (`a0f04cf8`) → two NOPs (`00bf00bf`);
-0x600015da: constructor call (`c3f0e5fd`) → self-loop/NOP (`fee700bf`).

Exactly8ELF bytes differ, layout and all other content unchanged. Disassembly
confirms USB init remains and constructors/main are unreachable; interrupts stay
enabled. HEX record lengths/checksums all valid. Derived diagnostic SHA256:
`67dcae4303b6a762a5b12ca3591ed035600cd56b7573bf05391b18aaf24dfe9d`.

After PROGRAM and GUI Auto off, Homebrew CLI2.3 completes programming, reports
Booting and exit0.45s observation: no USB serial port and0bytes; separate
`system_profiler SPUSBDataType` inventory likewise contains noTeensy. Application
serial rows were not expected; USB enumeration itself is the test oracle.

Bypassing PSRAM initialization did not remove the failure. A hang inside that
call alone is therefore not a sufficient explanation; we have not identified
whether execution fails before it, elsewhere in remaining core/USB initialization,
or because actual flash/reset state differs from intended. No readback proof.

**Last programmed image now supersedes +8leave-behind: PSRAM-bypassed latest with
pre-constructor pause; board not enumerating.** No restore/new upload after test.
No application setup or SD recovery can run, no production source/config edits,
no native/content test. Scratch outputs `boot-images/latest-no-psram-prectors.*`
and `boot-latest-no-psram-prectors-{upload,serial}.log` retained.


## Resolution (2026-10-22) — the current-qualification no-USB **is** the wrap-continuation change, and it is fixed

The open observation in this packet (current qualification writes complete but the board never enumerates, while the historical
`2b876c84` image boots) was resolved by bisection and then by a fix that is now on the branch.

**Bisection (each row is a real build, flashed with Homebrew `teensy_loader_cli` 2.3, classified by `/dev/cu.usbmodem*` **and**
`Teensy MIDI`):**

| Build | Source | Boot |
|---|---|---|
| default `c25bb872…` | HEAD, lane off | OK |
| lane `3fbd5f4b…` | `0f71c75d` (3.15d slice 2) | OK |
| lane `0810db67…` | `2b876c84` (slice 3, rebuilt) | OK |
| lane `e013df31…` | `2b876c84` **+ 1 KiB `.csf` pad** (the failing image's exact size) | OK |
| lane `a4a66e83…` | `c94eb011` (3.15e wrap continuation) | **hangs** (2 verified flashes + the owner's own upload) |
| lane `e50b8bcb…` | `c94eb011`, new **call** disabled | OK |
| lane `83a54349…` | `c94eb011`, call kept, helper **body** `return;` | OK |
| lane `f93db70e…` | `c94eb011`, drain body moved to ITCM | OK |
| lane `7ece334b…` | **final fix** (ITCM drain + `handleNoteEditFold` pay-down) | **OK** |

**Mechanism.** The 3.15e drain body (`drainPagedOwedTail`) called `advancePlaybackCursor`, which is an **ITCM** function, from a
**flash**-resident helper. That makes the linker emit the image's only **flash→ITCM veneer** (`0x600c44d8` in the hanging image);
no boot-reachable path uses it, so the failure is a **link/placement artifact**, not the drain's logic. Image size is ruled out by
the fixed-size pad control above (the pad image ends at the same address as the hanging one and boots), and the USB descriptor is
ruled out offline (both ELFs carry the MIDI+Serial device descriptor, PID `0x0489`, and `Teensy MIDI` in UTF-16).

**Fix (committed `50754fe8`).** The drain body is `Track::servicePagedOwedTailTurn`, defined in
`src/Track/TrackPlaybackHotPath.cpp` (the ITCM hot-path TU) and called immediately after the flash boundary judgement on the same
turn, with the tick captured **before** it — no new veneer. Its 256 B ITCM cost is paid down by moving the cold NOTE_EDIT fold
helper `Track::handleNoteEditFold` (700 B, UI-only) to flash. The native oracle caught the first version's dropped
`passRebasePending = false` (4 994 instead of ≥5 000) and is green after the restore.

**Gates.** default `variables 96,192 / code 391,688 + padding 1,528`; lane `96,192 / code 391,848 + padding 1,368` (floor 840,
**no re-base**); 3.15e oracle 3/3; full native **3 383 / 7 skipped / 3 376 succeeded / 0 failed**; `openspec validate --strict`
valid; `git diff --check` clean. Board left on `7ece334b…`.
