# SD imaging and recovery inspection — PROCEDURE FOR APPROVAL

**Status: PROPOSED, NOT EXECUTED. No device access, no card access, no boot.**
This document requires the owner's explicit authorization before any step below is run. It exists because
recovery path (a) is "preserve the SD card before recovery attempts", and preservation has to be read-only
and taken *before* anything else touches the card.

## Why an image first, and why read-only

The device currently reports a workspace **load** failure, not a confirmed erasure. The card may still hold
every slot payload, the saved sets and the revisions. Two things can destroy that:

1. **Another firmware boot.** The firmware writes on boot: the observed post-loss boot already ran
   `Boot hygiene: discarded 1 incomplete current-workspace temp file(s)` and
   `[StorageManager] Boot hygiene: removed incomplete runtime bundle temp`. Every additional boot can discard
   or rewrite the exact partial files that would explain the failure. **Do not reset or re-upload.**
2. **Writing to the card at all.** Mounting read-write, `fsck`, Spotlight indexing, or a stray `.DS_Store`
   can all mutate it. The card is user data and the firmware has no restore command.

So: image the raw device, then do every inspection on the **copy**. The card is touched by `dd` reading from
it and by nothing else.

## Hard constraints

- **No firmware boot, reset, or upload** at any point. The device stays powered but untouched.
- **No write of any kind to the card** — no mount read-write, no rename, no delete, no `!QUARANTINE_WORKSPACE`,
  no `fsck`, no repair.
- **No quarantine, no loading a saved set, no rebuilding the fixture.** Those are later decisions, taken from
  what the copy contains.
- Steps that need `sudo` are marked; on macOS, reading a raw disk device requires it.

## What is on the card (the firmware's own layout)

From `include/CurrentSetStorage.h` and friends, so the inspection is targeted rather than exploratory:

| Path | What it is | What it would tell us |
|---|---|---|
| `/MidiLooper/current/temp/runtime.bundle.bin` | the workspace runtime bundle | **the file the boot reported missing** — absent, zero-length, or short is the direct evidence |
| `/MidiLooper/current/temp/runtime.bundle.bin.tmp` | in-progress bundle write | presence/size = an interrupted save, the suspect |
| `/MidiLooper/current/workspace.bin` (+`.tmp`) | workspace manifest | whether the committed manifest survived |
| `/MidiLooper/current/transport.bin`, `global.bin` | transport/global state | partial-loss boundary: which parts of the workspace survive |
| `/MidiLooper/current/slots/loop_TT_SS.bin` (+`.tmp`, `.sealj`) | **per-slot payloads** | **how many of the ~58 slots still have content** — the crux of "erased" vs "not loadable" |
| `/MidiLooper/current/undo/` | undo stack bodies | scope of loss |
| `/MidiLooper/sets/index.bin` (+`.tmp`) and `/MidiLooper/sets/**` | saved sets / revisions | **the recovery path**: whether a loadable earlier revision exists |

## Procedure

### Step 1 — identify the card, without mounting it

```bash
diskutil list                     # find the external physical disk for the Teensy SD
```
Record the identifier (`/dev/diskN`), its size and its name in the run notes. Sanity-check the size against
the card (a wrong identifier here would image the boot disk).

### Step 2 — unmount, do not eject

```bash
diskutil unmountDisk /dev/diskN   # sudo may be required
```
The card stays attached; the volume is detached so the raw device can be read cleanly. **Do not** use
`eject`.

### Step 3 — image the raw device, read-only (`sudo`)

```bash
sudo dd if=/dev/rdiskN of=<image-path> bs=4m
```
`rdiskN` (raw) is markedly faster than `diskN`. Note that `dd` here only **reads** the device: `if=` is the
card and `of=` is a file. Record the byte count `dd` reports.

### Step 4 — verify and freeze the image

```bash
shasum -a 256 <image-path>                 # record this; it is the reference for every later claim
hdiutil imageinfo <image-path>             # confirm the image is readable and structured
ls -l <image-path>
chmod 444 <image-path>                     # freeze it; inspection happens on copies of the copy
```
Then **re-image once more to a second file and compare the two hashes**. Ideally both are identical, which
shows the read was stable; if they differ, the card is being modified or is failing, and that is itself a
finding to stop on.

### Step 5 — inspect the COPY, read-only

Attach the image read-only (never read-write):

```bash
hdiutil attach -readonly -nobrowse <image-path>
```
Then inventory, in this order:

1. **The direct cause.** Does `current/temp/runtime.bundle.bin` exist, and what is its exact size? Is
   `runtime.bundle.bin.tmp` present, and how far did it get? Compare against `workspace.bin`'s declared
   bundle size if the manifest records one.
2. **The surviving library.** Count `current/slots/loop_*.bin` and sum their sizes. This answers the question
   the host evidence cannot: whether the slots are still on the card (load failure) or gone (erasure).
3. **The rest of the workspace.** `workspace.bin`, `transport.bin`, `global.bin`, `undo/` — presence and size.
4. **The recovery path.** `sets/index.bin` and the `sets/` tree: how many saved sets/revisions exist, and
   their timestamps relative to the incident.

Every inspection command must be a read (`ls`, `stat`, `shasum`, `xxd -l`, copies **out of** the image).
Detach when done: `hdiutil detach <mountpoint>`.

### Step 6 — report, then stop

Write the findings into the evidence directory next to `MANIFEST.md`, and stop. **No recovery action is taken
in the same session** — the choice of recovery follows from the findings, and is a separate decision.

## Reading the outcomes

| Finding | Implication |
|---|---|
| slot payloads present, bundle missing/partial | load failure with the library intact — the interrupted save damaged only the bundle; recovery is plausible |
| `sets/` holds a revision predating the incident | a loadable earlier state exists — the concrete recovery candidate |
| slot payloads absent too | the loss is broader than the bundle; the library must be treated as gone |
| second image hash differs from the first | the card is changing or failing — stop and report; do not proceed to recovery |
| `.tmp` files present and truncated | direct evidence of the interrupted write, corroborating the host-side chain |

## What this procedure deliberately does not do

- It does not boot the device, so it cannot itself trigger a further boot hygiene pass.
- It does not repair, quarantine, rename, delete, or load anything.
- It does not decide recovery. Path (a) is **preservation and inspection**; choosing an action from the
  findings needs a separate authorization.
