> Superseded scope, 2026-09-26: user explicitly chose a folder-only USB copy of
> `/MidiLooper`, leaving the card in place and unrelated data excluded. That copy
> is complete. This raw-image proposal is historical, not a prerequisite for the
> approved offline recovery analysis. Recovery writes still require approval.

# SD imaging and recovery inspection — PROCEDURE FOR APPROVAL

**Status: PROPOSED, NOT EXECUTED. No card access, no device access, no boot.**
This document is a recommendation, **not** authorization to access or image the card. Recovery is now the
priority, so the plan is the **full raw image first** — a quick file inventory is explicitly *not* an
acceptable substitute: it can miss deleted files and the filesystem metadata needed to reconstruct the
workspace. The faster inventory is only a fallback if imaging proves impractical, and only with that tradeoff
explicitly accepted.

## Why an image first, and why read-only

The device reports a workspace **load** failure, not a confirmed erasure. The card may still hold every slot
payload, the saved sets and the revisions. Two things destroy that:

1. **Another firmware boot.** The firmware writes on boot: the observed post-loss boot already ran
   `Boot hygiene: discarded 1 incomplete current-workspace temp file(s)` and
   `[StorageManager] Boot hygiene: removed incomplete runtime bundle temp`. Every boot can discard the very
   partial files that would explain the failure. **Do not reset or re-upload** (the device hold stays latched).
2. **Writing to the card at all.** Mounting read-write, Spotlight indexing, `fsck`, or a stray `.DS_Store`
   can mutate it. The card is user data and the firmware has no restore command.

So: image the raw device, verify once, preserve that master, and do every inspection on a working copy.

## The approval boundary

- **Read-only imaging and inspection require the owner's authorization** to access the card (this document is
  the request).
- **Any recovery *write* — restoring, loading a saved set, rewriting the workspace, quarantining, renaming,
  deleting, or rebuilding — requires a separate, explicit approval** taken *after* reviewing the preserved
  evidence and the image findings.
- The boundary is therefore **approval after evidence review**, not the session in which findings occur.
  (An earlier draft prohibited recovery merely because findings were made in the same session; that was the
  wrong boundary and is corrected here.)

## Hard constraints

- **No firmware boot, reset, or upload.** The device stays powered but untouched; the hold latch remains set.
- **No write of any kind to the card** — no read-write mount, no rename, no delete, no
  `!QUARANTINE_WORKSPACE`, no `fsck`, no repair, no Spotlight indexing.
- **No quarantine, no saved-set load, no rebuild** (see the approval boundary above).

## Prerequisites (all host-side, before touching the card)

### P1 — A recovery-capable imager with a resumable map

`ddrescue` is the tool for this: it writes a **map file**, does a first pass, then retries only the incomplete
regions, so a failing card is never re-read end-to-end just to obtain matching hashes.

Checks on this machine: `ddrescue`, `dc3dd`, `guymager` are **not installed**; only `dd`, `hdiutil` and `asr`
are present. `brew` is available, so `brew install gddrescue` provides `ddrescue`. **This install is a
prerequisite that needs a decision** — it is host-only and does not touch the card, but it has not been run.

### P2 — A separate destination with enough space (the arithmetic matters)

A raw master image is the card's **full capacity** regardless of how much data is on it, and the plan is a
master **plus** a working copy:

```
required = card_capacity (master)  +  card_capacity (working copy)  +  ~10% headroom
```

Measured now: the only candidate volume has **51 GiB available**. That supports a card up to roughly
**23 GiB** for master + copy. So:

- If the card is ≤ ~23 GiB → master + working copy fits.
- If it is larger (32 GB cards are common) → either a different destination with more space is chosen, or the
  working copy is dropped and the master is attached **read-only** for inspection. Attaching read-only never
  writes the master, so the preservation intent still holds; it is a stated fallback, not a silent one.

The destination must be a **different physical device** from the card (never the card itself, never a volume on
the same disk), and Spotlight indexing on it should be off (`mdutil -i off <volume>`) so indexing cannot write
while the image is being produced.

### P3 — Prevent automatic mounting and writes

macOS may auto-mount a newly attached card. Mitigations, in order of preference:

1. **Hardware write blocker** between the card and the reader, if one is available — the only measure that is
   enforced in hardware rather than by discipline.
2. Unmount the moment the card is identified (`diskutil unmountDisk`), and confirm nothing is mounted before
   imaging.
3. Keep the card attached to the same reader for the whole pass, and do not let a file manager or backup tool
   touch the destination while the image is being written.

## Procedure

### Step 1 — identify the card and its capacity, unambiguously

```bash
diskutil list                       # list all disks
diskutil info /dev/diskN            # confirm: removable, external, exact capacity, media name
system_profiler SPUSBDataType       # confirm which reader is on the bus
```

Require **all** of: exactly one plausible external/removable candidate (or an explicit size/name match to
disambiguate several), a capacity that matches the physical card, and a media name consistent with the card.
Record identifier, capacity and name in the run notes. A wrong identifier here would image the boot disk.

### Step 2 — unmount, do not eject

```bash
diskutil unmountDisk /dev/diskN     # sudo may be required
```
The card stays attached; the volume detaches so the raw device can be read cleanly. **Do not** `eject`, and
confirm afterwards that no partition of that disk is mounted.

### Step 3 — image to the master, with a map (`sudo`)

Preferred, resumable, and safe on a failing card:

```bash
sudo ddrescue -d -r1 /dev/rdiskN <master-image> <mapfile>
```
`-d` reads the raw device directly; `-r1` retries bad regions once on the first pass (further retries are a
separate, deliberate pass). Then, only if the map shows unresolved regions and only with the tradeoff
accepted:

```bash
sudo ddrescue -d -r3 /dev/rdiskN <master-image> <mapfile>    # resumes from the map; re-reads ONLY the gaps
```

Fallback if `ddrescue` is not installed and installing it is declined:

```bash
sudo dd if=/dev/rdiskN of=<master-image> bs=4m
```
Record exactly which route was used. `if=` is the card and `of=` is a file: this reads the card and writes
only to the destination.

### Step 4 — verify the master **once**, and do not chase matching hashes

```bash
ddrescuelog -t <mapfile>            # ddrescue: the authoritative read report
shasum -a 256 <master-image>        # record once as the reference for every later claim
hdiutil imageinfo <master-image>    # readable and structured
chmod 444 <master-image>
```

**If read errors appear, do not repeatedly reread the card to obtain matching hashes.** That is the
behaviour this procedure explicitly avoids: retries are bounded, driven by the map, and every unresolved
region is recorded as a **finding** — a bad region is evidence about the card's health, not a verification
nuisance. Re-reading a failing card end-to-end risks turning a recoverable read into a lost one.

### Step 5 — make the working copy, then inspect that

```bash
cp <master-image> <working-copy>            # if space allows (see P2)
hdiutil attach -readonly -nobrowse <working-copy>
```
If space does not allow a second copy, attach the **master** read-only instead and note the deviation.

Inventory, in this order (paths are the firmware's, from `include/CurrentSetStorage.h`):

| Path | What it is | What it would tell us |
|---|---|---|
| `/MidiLooper/current/temp/runtime.bundle.bin` | the workspace runtime bundle | **the file the boot reported missing** — absent, zero-length, or short is the direct evidence |
| `.../runtime.bundle.bin.tmp` | in-progress bundle write | presence/size = an interrupted save, the leading suspect |
| `/MidiLooper/current/workspace.bin` (+`.tmp`) | workspace manifest | whether the committed manifest survived |
| `/MidiLooper/current/transport.bin`, `global.bin` | transport/global state | the partial-loss boundary |
| `/MidiLooper/current/slots/loop_TT_SS.bin` (+`.tmp`, `.sealj`) | **per-slot payloads** | **how many of the ~58 slots still hold content** — the crux of "erased" vs "not loadable" |
| `/MidiLooper/current/undo/` | undo stack bodies | scope of loss |
| `/MidiLooper/sets/index.bin` (+`.tmp`), `/MidiLooper/sets/**` | saved sets / revisions | **the recovery path**: whether a loadable earlier revision exists |

Also, because the full image is taken, use it for what a file listing cannot show: freed/deleted directory
entries, filesystem metadata, and slack space — the reason the full raw image was preferred over an
inventory.

Every inspection command must be a read (`ls`, `stat`, `shasum`, `xxd -l`, copies **out of** the image).
Detach when done: `hdiutil detach <mountpoint>`.

### Step 6 — report, then await approval

Write findings into this evidence directory next to `MANIFEST.md`. Then **stop and request explicit approval
before any recovery write.** Recovery is the priority — but it proceeds by approval after this review, not by
proceeding automatically.

## Reading the outcomes

| Finding | Implication |
|---|---|
| slot payloads present, bundle missing/partial | load failure with the library intact — recovery is plausible |
| `sets/` holds a revision predating the incident | a loadable earlier state exists — the concrete recovery candidate |
| slot payloads absent too | the loss is broader than the bundle; the library must be treated as gone |
| unresolved read regions in the map | **stop and report**: the card is failing; a bounded, map-driven retry pass is a decision, not a reflex |
| `.tmp` files present and truncated | direct evidence of the interrupted write, corroborating the host-side chain |

## What this procedure deliberately does not do

- It does not boot the device, so it cannot trigger another boot-hygiene pass.
- It does not repair, quarantine, rename, delete, load, or rebuild anything.
- It does not decide recovery: it produces the evidence that decision requires, and stops for approval.

## Open decisions this document puts to the owner

1. **Authorization to access and image the card** (read-only), and confirmation that the device hold stays on.
2. **Install `gddrescue`** (`brew install gddrescue`) to get the map-based, resumable imager — or accept `dd`
   as a lesser fallback.
3. **Destination**: whether the existing 51 GiB volume is the target (which caps the card at ~23 GiB for
   master + working copy), or a larger separate destination is provided.
4. **Write blocker**: whether a hardware write blocker is available.
