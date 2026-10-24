# 4.2 device leg — evidence (in progress)

Bench target: the packet's **track 8 / slot 5** = firmware **track 7 / slot 4** (0-based; `loop_07_04.bin`).
The `LOAD,parity` row therefore reads `t=7,s=4`.

## Image / gates

- Qualification + temporary `LOAD_PARITY_ROW` seam. Final seam HEX (v2, header-stripping upload):
  `95a39b2206f1fb196e1356a84b3121c197984e421350e3553e1aabe68e3aa083`.
  First seam build (v1, superseded): `d551430ffff27afe647f95201041407a6d8fd2c8f22e7fdcec735d78d7f83607`.
- RAM1: `variables 100,288 / ITCM 393,216 (code 392,184 + padding 1,032) / locals 30,784`. Default
  capture env unchanged at `96,192 / 391,944 + 1,272 / 34,880`. The qualification variables/locals are
  temporarily re-based for the seam's DTCM align edge (see `scripts/ram1_budget.json`).
- Full native after the seam test addition: **3,443 / 7 skipped / 3,436 succeeded**; storage **63/63**.

## Preflight backup

- Original slot downloaded before any write: `captures/4.2-device-leg/original_loop_07_04.bin`,
  **2,592 B**, SHA-256 `60397de7dd5b669c9ad6874df9ddb3e6a263340ee12092c03ab306199a0b84e2`, LSV1,
  epoch `body_offset=10`, `content_identity=1`, `root_identity=3283928152`.
- Original metadata: `loopId=4`, `startLoopTick=0`, **`loopLengthTicks=1536`** (2 bars), `loopStartTick=0`,
  `nextPassId=3`, `nextNoteId=18`, `nextMergeSequence=2`, `lastCommittedPassId=2`.

## Observed on the device

- After the (v1) seam image booted with the original slot intact, the reboot capture produced:
  - `#CAP,5701165,LOAD,parity,t=7,s=4,result=ok,events=34,walk_us=120` — **coherent** production body,
    34 events, 120 µs streaming walk.
  - `#CAP,5890043,LOAD,parity,t=7,s=3,result=divergent,events=26,walk_us=38` — an **existing** body on the
    card (track 8 slot 4) whose root disagrees with its passes; the guard refused it. This is independent
    device evidence for the 4.2 class, not an injected fixture.
- `probe` on 7/4 while the original was installed: `content_identity=1`, `root_identity=3283928152`,
  size 2,592, body offset 10.

## Defects found and fixed during the leg

1. **v1 upload left its 8-byte transfer header at the head of the slot file.** The loader parsed
   `header + LSV1 body` as a **legacy** body, failed (`LoadLoopJob parse failed`), and retried forever on
   the title screen; the parity guard was never reached. v2 copies the payload (offset 8) to a second
   temp file, appends `SAVE`, and renames that over the slot.
2. The first body generation used **1 bar** while the bench slot is a **2-bar** loop (`loopLengthTicks=1536`);
   regeneration now matches `loopId=4` and 2 bars. (With defect 1 present, both bodies failed before the
   guard, so this alone did not explain the title stall.)
3. Flashing the 1.26 MiB image failed four times mid-write with `error writing to Teensy` at different byte
   counts (USB link/power), then succeeded after a full replug/port change. The image was unchanged.

## Still pending

- v2 image flash + boot with the restored original; coherent 2-bar upload → `result=ok` and fast boot;
  diverged 2-bar upload → `result=divergent`, `content_identity=0`, `root_identity=0`; restore + identity
  check.
- Served-content cross-check (`!LIB_OBSERVE` / selected-track `!CAPTURE_CONTENT_SLOT`).
- Remove the seam/row, revert the temporary RAM1 re-base, re-pin the final HEX.
