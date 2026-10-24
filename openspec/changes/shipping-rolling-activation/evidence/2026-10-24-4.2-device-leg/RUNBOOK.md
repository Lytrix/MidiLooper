# 4.2 device leg — runbook (execution of PACKET.md)

Bench target naming: the packet's **track 8 / slot 5** is **1-based**. Firmware `!` commands and slot
paths are **0-based**, so every command below uses **track 7 / slot 4** (`loop_07_04.bin`). The `LOAD,parity`
row therefore reads `t=7,s=4`.

## Image

- Env `teensy41-capture-serial-window-qualification` (rolling lane ON) plus the temporary
  `-D LOAD_PARITY_ROW=1` seam (row + bounded body upload). HEX SHA-256
  `d551430ffff27afe647f95201041407a6d8fd2c8f22e7fdcec735d78d7f83607`.
- RAM1: `variables 100,288 / ITCM 393,216 (code 392,184 + padding 1,032) / locals 30,784`.
  The default capture env is unchanged (`96,192 / 391,944 + 1,272 / 34,880`). The qualification
  `ram1_variables`/`locals_free` are **temporarily re-based** for the seam's DTCM align edge; the seam and
  the re-base are removed at closeout.
- Flash: `teensy_loader_cli --mcu=TEENSY41 -w -v <hex>` with a short PROGRAM press (loader 2.3+).

## Preflight (old image, completed)

- Original slot body downloaded with the read-only `!SLOT_BYTES` path:
  `captures/4.2-device-leg/original_loop_07_04.bin`, **2,592 B**, SHA-256 `60397de7…`, LSV1,
  `body_offset=10`, `content_identity=1`, `root_identity=3283928152`.

## Steps

All commands: `.venv/bin/python scripts/device_leg_body_upload.py <cmd>`
(defaults are track 7 / slot 4).

1. **Boot measurement on the original (coherent) body**
   - `capture --seconds 45 --grep LOAD,parity --out captures/4.2-device-leg/boot-original.log`
   - Expect a `#CAP,…,LOAD,parity,t=7,s=4,result=ok,events=<n>,walk_us=<n>` row once the background fill
     reaches the slot. This is the load-turn walk cost on a real mixed-seed body.
   - `probe` → `content_identity=1`, `root_identity=3283928152` while the original is loaded.
2. **Coherent control (production-written body)** — `upload --body captures/4.2-device-leg/coherent.lsv1`
   (`BODY,end,ok`), then `reboot --seconds 45 --grep LOAD,parity` and `probe`.
   - Expect `result=ok,events=2`, `content_identity!=0`, `root_identity!=0`.
3. **Diverged body (root NoteOn velocity 90 vs passes 100)** —
   `upload --body captures/4.2-device-leg/diverged.lsv1`, `reboot --seconds 45 --grep LOAD,parity`, `probe`.
   - Expect `result=divergent,events=2`; `content_identity=0`, `root_identity=0` — the guard applied neither
     source and the diverged root was never installed. The file itself is still the 411-byte diverged body
     (`probe` reports its size), so the refusal is not "file missing".
   - Optional served-content cross-check: `!LIB_OBSERVE` (all-slot observation) or select track 8 and
     `!CAPTURE_CONTENT_SLOT 5`; both must show the slot empty, never pitch 62/velocity 90.
4. **Restore the original** — `restore` (`BODY,restore,ok`), `reboot --seconds 45 --grep LOAD,parity`, `probe`.
   - Expect the original identities again (`content_identity=1`, `root_identity=3283928152`), `result=ok`.
   - Confirm the `.legbak` was consumed by the restore.

## Acceptance

- [ ] `LOAD,parity` row observed for `t=7,s=4` with `result=ok` on the original and the coherent control.
- [ ] `result=divergent` on the diverged body, with `content_identity=0` and `root_identity=0` (no current).
- [ ] The diverged root's velocity/pitch never served (empty slot / library observation).
- [ ] `walk_us` for the original recorded; the load job's existing turn bound respected (no new bound).
- [ ] Original restored and verified by identity + SHA readback.
- [ ] HEX and RAM1 recorded; the seam/row and the temporary RAM1 re-base removed at closeout; final HEX re-pinned.

## Rollback

- `restore` renames `loop_07_04.bin.legbak` back over the slot; if the seam is unavailable, the downloaded
  `original_loop_07_04.bin` is the off-device copy. The firmware has no restore command for a whole slot, so
  the `.legbak` is the authoritative path.
- Check `probe` identities match `content_identity=1`, `root_identity=3283928152` and the readback SHA matches
  `60397de7…`.
