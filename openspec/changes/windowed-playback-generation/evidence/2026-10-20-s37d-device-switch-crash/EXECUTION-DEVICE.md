# 3.7d device read — blocked by a reproducible device crash on a queued slot switch

Date: 2026-10-20. Task: 3.7d (the exit's term travels with the fix). Branch:
`feature/unified-rolling-playback-path`, HEAD `6ccb75f8`. Type: device only; no firmware line changed in the runs below.

**Verdict: 3.7d stays open.** The device read it owes was not obtained: the one gesture that creates the demand
(`--switch-target-slot`, the wrapper's own documented demand) rebooted the board on four of four attempts, before any
`launch_gate`/`win_gate` row for the switched-to slot could be emitted. The fault is **not** in the 3.7d term; it is a
latent initialisation defect in two gate-row dedupe tables, reproduced on two images. **The crash is fixed and
device-verified below (DEC-151); the read is still owed for a second, separate reason**: the queued switch does not
commit inside the settle window, so the target's demand turn never runs.

## What was run

Registered preset only (`windowed_content_parity`, track 8 / slot 4 / channel 8); no stimulus edited, no captured
`command.json` executed, no oracle term relaxed.

| Run | Capture | Config | Result |
|---|---|---|---|
| 02 | `captures/i5-s37d-device-32bar-02` | `--record-bars 32 --no-boot-reset --switch-target-slot 5` | `win_requested` 0; crash at 14:19:12 |
| 03 | `captures/i5-s37d-device-32bar-03` | `--record-bars 32 --boot-reset --switch-target-slot 5 --switch-settle-s 60` | 32-bar take ok (`length=24576`); `win_requested` 0; crash at 14:24:58 |
| 04 | `captures/i5-s37d-device-32bar-04` | same as 03 | crash at 14:32:41 |
| fill | `captures/i5-s37d-fill-slot5-01` | `--loop-slot 5 --record-bars 24 --boot-reset` (no switch) | **green**, no crash — slot 5 filled, all lane checks pass |
| 05 | `captures/i5-s37d-device-32bar-05` | same as 03, on a fresh build of the current tree | crash at 14:46:21 |

`captures/i5-s37d-device-32bar-01` (2026-10-20 earlier, plain record-and-play, no switch) had no crash and no
`win_requested` — a plain record-and-play owes no demand, which is why the switch was used.

Images: runs 02–04 on lane `99247cb5…` (5.3 re-qualification image); run 05 on a fresh lane build of the current tree,
`9b1f8e7d…` (`code 392,376 + padding 840`, RAM1 line identical to `99247cb5…`). The crash is identical on both.

## The crash

Four occurrences, same signature, always immediately after the switch gesture:

```
[174.618] [INFO] Button press: Loop 5 (short)
[174.683] [INFO] Loop 5: Queued playback switch
#CAP,174683610,DIAG,disp_window,t=7,s=4,st=PLAYING,ed=0,es=0,len=18432,w=0,b=16
#CAPTURE_RECONNECT,...            <- device rebooted (capture reattached)
```

CrashReport (`!CRASH_LOG`, stopped-only read; no SD write):

```
A problem occurred at (system time) 14:19:12 / 14:24:58 / 14:32:41 / 14:46:21
  Code was executing from address 0x59768
  CFSR: 82  (DACCVIOL) Data Access Violation
  (MMARVALID) Accessed Address: 0xD2E3A540
  Reboot was caused by auto reboot after fault or bad interrupt detected
```

`arm-none-eabi-addr2line -e firmware.elf -f -C 0x59768` on the run-05 image → **`strcmp`**. The accessed address is not
in any mapped region (ITCM `0x0…`, DTCM `0x200…`, OCRAM `0x202…`, PSRAM `0x700…`): it is a garbage pointer being
dereferenced as a C string.

## Root cause (VERIFIED)

`Track::reportPlaybackLaunchGateRow` keeps its transition state per (track, slot) in `DMAMEM`:

```cpp
struct LaunchGateLastState {          // src/Track/TrackPlaybackWindowBuild.cpp
  const char* exit = nullptr;         // default member initialisers
  uint32_t loopLengthTicks = 0;
  uint16_t flags = 0;
  uint8_t launchPreparation = 0;
};
DMAMEM LaunchGateLastState sLaunchGateLast[kLaunchGateSlotCount];
...
if (last.exit != nullptr && std::strcmp(last.exit, exit) == 0 && ...) return;
last.exit = exit;                     // first write happens only after the compare
```

`DMAMEM` places the object in `.dmabuffers`, which the Teensy core linker script collects into

```
.bss.dma (NOLOAD) : { *(.hab_log) *(.dmabuffers) ... } > RAM
```

i.e. **NOBITS/NOLOAD**: the section is not in the image and the startup code (`memory_clear(&_sbss, &_ebss)`) clears
only DTCM `.bss`. Default member initialisers are therefore **not applied** to these tables; their bytes are whatever
OCRAM held before the reset (stale DMA/heap content from an earlier boot or image). This is the repository's written
trap — "`.bss.dma` is ALLOC-only (no initial image): never rely on initialisers for DMAMEM state" (the rule 5.3 had to
follow for the receipt sampler).

Confirmed on the run-05 ELF:

```
20207de4 00000300 b (anonymous namespace)::sLaunchGateLast
20208164 000000a0 b (anonymous namespace)::sRollingGateLast
[ 9] .bss.dma  NOBITS  20200000 ... 0290a0  WA
```

The mechanism, and why it is deterministic on this leg:

- A queued switch prepares its launch through `preparePlaybackLaunchForSlot`, which is the **first** caller of
  `reportPlaybackLaunchGateRow` for the target slot in that boot. No earlier gesture wrote that entry.
- `last.exit` is stale and non-null → `strcmp(stale, "launch_…")` → DACCVIOL → hard fault → auto reboot.
- The entry is never written (the crash precedes `last.exit = exit`), so the same stale bytes survive the reboot and the
  next switch to that slot crashes again. Four for four, two images.
- The neighbouring `sRollingGateLast` has the same defect, but is partly masked: its compare is gated on `last.seen`,
  which is also stale, so a stale `seen == 0` silently takes the "emit" branch. A stale `seen != 0` with a matching key
  and a stale `exit` is the same fault on the first `win_gate` row of a boot.
- Recording directly on a slot (the fill run) never reaches the launch path, which is why it neither crashed nor proved
  the entry.

## Why this blocks 3.7d specifically

3.7d's device read needs a `win_blocked_by_request` row whose `rq` names the `PlaybackViewPendingReason` that stopped a
retained demand. The lane only *requests* a window when a launch binds a first window, and the registered wrapper's
documented demand is exactly the switch. Every switch attempt reboots the board during launch preparation, so the lane
never reaches the demand turn. No post-3.7d capture in the tree carries that exit with a named `rq` (all existing rows
are `rq=0`, from pre-increment images: `captures/i5-s64-f8-parity-32bar-01`, `…-demoted-aligned-03`, `…-37c-0x`, …).

## The fix (applied; DEC-151)

Both tables now prove "written since boot" with a **written magic** — the pattern `Diagnostics::DiagLastRecordSlot` and
5.3's sampler arming already use — instead of relying on a member initialiser the linker never emits:

- `include/Utils/GateRowLastState.h` (new) owns the two structs, the two magics (`'LGL1'`/`'RGL1'`) and
  `ensureInitialized(state, magic)`, which resets the whole entry on a magic mismatch and reports it.
- `Track::reportPlaybackLaunchGateRow` / `reportPlaybackRollingGateRow` call it on the entry **before** the compare, so
  `exit` can never be read from stale RAM (the rolling entry's `seen`, key and length are stale in the same way, which
  is why the reset is whole-entry).
- No row, token, field or cadence changed; no DTCM state; no new owner. The whole-entry reset is deliberate: a stale
  `key`/`flags` could otherwise *suppress* a first row rather than crash on it.

**Gates** (`EXECUTION-PACKET.md` carries the packet):

- New native suite `test/test_gate_row_last_state` **5/5**, feeding the helper the stale shape (`memset` + non-null
  `exit`, and a stale `seen`), the written shape, and a wrong-magic stamp. **Red:** with the magic check disabled the
  same five fail; the header was restored **byte-identically** (`sha256 a9a634f2…`).
- Full native **3343 / 7 skipped / 3336 succeeded / 0 failed**. Lane build `fa7197e7…` SUCCESS, RAM1
  `variables 96,192 / code 392,376 + padding 840` (floor unchanged), RAM2 `168,384` (**+288 B** = 64 × 4 + 8 × 4 for
  the magics). Base env SUCCESS at `variables 96,192 / code 392,216 + padding 1,000` (unchanged).
- **Device** (`captures/i5-s37d-device-32bar-06`, `-07`, image `fa7197e7…`, same preset and switch): `crash=1` rows
  **0** (was four of four), and the formerly faulting path emitted **7 `launch_gate` rows for `t=7,s=4`**
  (`launch_requested`, `launch_requested_match` ×3, `launch_ready`, `launch_ready_owns_source`,
  `launch_building_match`). The transition oracle — "the run survives the switch and names the target's launch" — is met.

## The second blocker the fixed leg exposed (recorded, not claimed)

With the switch no longer rebooting, both runs show the queued switch **never committing** inside the settle window:

- Run 06 (settle 60 s) and run 07 (settle 120 s, two traversals of the 32-bar loop): `snap_gate,t=7,s=4,a=3` repeats
  **450** times — the pending-publish loop is the target while the active loop stays slot 4 — with
  `p=0,sep=0,fc=0,wj=0,hp=1,cl=1,idle=0,elig=0`, and the target has **no `win_gate` row at all**.
- The target's demand is nevertheless prepared: `win_frozen_readiness,7,4` records `owner_trace,transition`,
  `qualification_started`, `build_admitted`, `completed_unpublished` and `released_without_site` in both runs — the same
  precursor the 2026-10-19 episode showed 87 µs before its `win_blocked_by_request`.
- So the lane's demand turn for the target (`serviceRollingPlaybackWindow` on the now-active slot) is what never runs;
  the exit 3.7d names cannot be reached until the queued switch commits. That is the S4b/DEC-102 switch-commit path, not
  the 3.7d row, and it is the next thing to establish.

Named limit: the *provenance* of the exact stale value `0xD2E3A540` (which earlier writer left those overflow bytes) is
not established and is not needed — the invariant violation (a null-check that depends on an initialiser the linker
does not emit) is sufficient, is pinned by the two images, and is now removed.
