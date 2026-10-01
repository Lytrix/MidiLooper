# REGRESSION — a 24-bar take makes the device fault at boot (2026-10-01): diagnosis, containment, and the localized defect

**Reported by the owner:** *"the teensy now reboots at start; the serial communication drop was a teensy hang during
the last test."* Both halves are confirmed, and they are the same fault.

## 1. What happened, in order

| when | what | evidence |
|---|---|---|
| the S1.5 24-bar leg | the 24-bar record completed (`length=18432`) and the overdub **started** (`Overdubbing started @ tick 1591`) — the capacity answer stands — but at the **overdub wrap commit** the device died | `captures/s15-24bar-01/session_20261001_190233.log`: `[165.478] Overdubbing wrap committed @ tick 18432` → `#CAPTURE_RECONNECT` → `Loaded DROID button configuration` → `#CAP,3539338,DIAG,boot_reset,crash=1` |
| after that | the device **crash-loops at boot**, ~14 s per cycle, and the USB CDC drops each time (the owner's "reboots at start" and the serial drop are the same event) | `captures/regression-24bar-bootloop-01/boot-cycle.log`: 6 cycles, each ending in a port drop |
| every cycle | after the boot rows, the last activity before the fatal is a **restore of track 5 Loop 1** | same log: `BOOT,load,ok` → … → `#CAP,…,RRQ,4,0,0,0,running,none,none` (0-based track 4 ⇒ **track 5**, slot 0 ⇒ **Loop 1**) → the next writes never arrive |

**Track 5 Loop 1 is exactly where that leg recorded its 24-bar take.** So: the take was persisted, and **its restore
at every boot is what faults** — which is why the loop is deterministic rather than random.

## 2. The fault, decoded

The Teensy core printed its own report (captured verbatim in `boot-cycle.log`):

```
CrashReport:
  Code was executing from address 0x600928B6
  CFSR: 82   (DACCVIOL) Data Access Violation
             (MMARVALID) Accessed Address: 0xB000
  Reboot was caused by auto reboot after fault or bad interrupt detected
```

`0x600928B6` (the flash region) resolves, with the project's own toolchain against the flashed image's ELF, to:

```
void std::vector<SnapshotEventChange, std::allocator<SnapshotEventChange> >::
     _M_realloc_insert<SnapshotEventChange const&>(...)
```

**And the accessed address is arithmetic, not noise:** `0xB000` = 45,056 = **512 × 88**, i.e. a vector with
**512 elements and a null `data()`** whose growth computed `null + 512 × sizeof(SnapshotEventChange)` and wrote
there. `SnapshotEventChange` is the element of `LoopSnapshotChangeSet::events` — a plain struct holding two
`std::vector`s (`include/LoopSnapshot.h:211-214`) — and the **body-load path builds one by pushing one change per
event**:

```cpp
// src/StorageLoopIoBodyFormatV1.cpp:478-500 (the LSv1 gather → snapshot construction)
LoopSnapshotChangeSet changes;
for (const Lsv1GatheredNote& note : payload.notes) { … changes.notes.push_back(change); }
for (const Lsv1GatheredEvent& event : payload.events) { … changes.events.push_back(change); }
LoopSnapshotConstruction construction(parent, changes);
```

A 24-bar body has enough events to take that vector through many reallocations, and the one that faulted had a
**null base with a 512-element size** — the signature of a vector object whose *header* was corrupted or whose
storage was taken away while its size survived (a byte-copy of a vector, a move-from without the size reset, or an
overwrite of the enclosing object). Localizing *which* of those it is, and fixing it, is the next slice — this file
records the lead, not a diagnosis of the mechanism.

## 3. Containment (done, and reversible)

The firmware's own quarantine was used — the repo's canonical, non-destructive tool (`!QUARANTINE_WORKSPACE` pairs
with `!RESTORE_WORKSPACE`); nothing was deleted:

```
[StorageManager] Quarantined: /MidiLooper/current -> /MidiLooper/current.bad.5202
[StorageManager] Quarantined: /MidiLooper/recovery/checkpoints -> /MidiLooper/recovery/checkpoints.bad.5202
[StorageManager] Workspace quarantine complete.
```

**The loop is over:** a 25 s passive read after it shows **zero** `boot_reset` rows, no `CrashReport`, and a steady
pool (`live=1782 KB`, `blocks=55/55`, `refused=0/0B`). Evidence: `captures/regression-24bar-bootloop-01/quarantine-attempt-01.log`.

**Consequence the owner must know:** `/MidiLooper/current` is where the **slot library** lives
(`CurrentSetStorage::kCurrentSlotsDir = "/MidiLooper/current/slots"`), so the device now boots with an **empty
library**. The content is **quarantined, not lost** (`current.bad.5202`), and `!RESTORE_WORKSPACE` brings it back —
but restoring it **re-introduces the 24-bar body and therefore the loop**, so restore must follow the fix (or be
paired with dropping that one slot).

## 4. Is this my session's doing?

**No session change touches the faulting path.** This session's firmware work was the telemetry cadence
(`Utils/TelemetryCadence.h`), the ring resynchronisation (`recoverUnreadableHead`/`headResyncDropBytes`), the direct
persistence receipt (`persistenceResultImmediate`), and the `PERS,drain_progress` wrapper. The fault is in
`vector<SnapshotEventChange>::_M_realloc_insert` on the **body-load/snapshot-construction** path.

It is also **not the first occurrence of this class**: the repo already records two
`_M_realloc_insert`-in-a-vector faults — `captures/task23-undo-crash` (2026-09-16, boot-time undo over-reservation,
`crash=1`) and the `vector<DisplayNote, ExternalMemoryFirstAllocator<DisplayNote>>` reallocation in the 24-bar heap
runs — so this is the **third** instance of "a vector reallocation faults on large content", and the first where the
faulting vector is the snapshot change set.

## 5. What this means for the S1.5 record

The capacity answer the leg produced is unaffected — the rows that carry it (`Recording stopped … length=18432`,
`Overdubbing started`, `refused=0/0B` on every census row) were all emitted *before* the fault, and the finding runs
to "the overdub reserve is admitted on the grown library". But the leg's **own verdict** was
`INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)` and the run **ended in a device crash**, so the resolution should be read
as *admitted, and the take's later wrap commit crashes the device* — the second half being a new, more serious
finding that this file owns.

## 6. ROOT CAUSE FOUND AND FIXED (2026-10-01, same day)

### The mechanism, proven from the disassembly

The faulting instruction (`0x600928B6`) is a **byte store**: `strb.w r3, [r8, fp]` with `r8` (the destination
base) **= 0** and `fp` (the byte offset) **= 45,056 = 512 × 88**. So the reallocation was copying change-set
elements into a **NULL buffer**, and the write only faulted once the offset left the mapped region: the first
32 KB of the address space is **ITCM (writable RAM)**, so the writes at `0x0…0x7FFF` silently corrupted ITCM and
the first *unmapped* address — `0xB000` — raised `DACCVIOL`.

Why was the buffer null? Because **Teensyduino's `operator new` is `return malloc(size);`** — no null check, no
throw (`cores/teensy4/new.cpp`) — while `std::vector`'s growth path assumes a throwing allocator and never checks
the pointer. So a **failed `malloc`** (a fragmented internal heap, with a ~45-180 KB request) hands the vector
null, and the vector writes into it. That single mechanism explains all three `_M_realloc_insert` faults this
repo has recorded (this one, `captures/task23-undo-crash`, and the `vector<DisplayNote>` reallocation in the
24-bar heap runs).

### The fix

`LoopSnapshotChangeSet` — the change set both the body-load path and the publish/journal path grow — now allocates
its two vectors with **`ExternalMemoryFirstAllocator`** (`include/LoopSnapshot.h`), which prefers the **PSRAM**
pool for exactly this kind of length-scaling, non-hot buffer and, if both pools are ever exhausted, prints a
**named FATAL** and stops instead of returning null. The body-load path additionally **reserves** both vectors up
front from the counts the gather already knows (`src/StorageLoopIoBodyFormatV1.cpp`), so the growth-by-content
reallocation churn — what made the failure likely — is gone. A pair of **`static_assert`s** pins the vector types,
so the fix cannot be silently reverted.

### Verification

| check | result |
|---|---|
| full native | **2,667 cases / 7 skipped / 2,660 succeeded / 0 failed** |
| `teensy41-capture-serial` | SUCCESS, RAM1 guard `variables 96,192 / ITAG 393,216 (code 391,880 + padding 1,336) / locals 34,880`, floor 840 met (496 B headroom) |
| **red-proof of the guard** | reverting `events` to the default allocator **fails the build**: `static assertion failed: LoopSnapshotChangeSet::events must use ExternalMemoryFirstAllocator`; the file was restored byte-identically (sha256 `fdf246fa…`) |
| fixed image | `eca0b0139d864734ae5a3164126c20e18cbde60eb2c6b437df851126f988fc5a` |

### Device verification (2026-10-01, complete)

All three steps of the approved plan ran on `/dev/cu.usbmodem154944801`:

| step | command | result |
|---|---|---|
| flash | `pio run -e teensy41-capture-serial -t upload` | SUCCESS; RAM1 guard `padding 1,336`, floor 840 met |
| clean boot | `!REBOOT` | `#CAP,854780,DIAG,boot_reset,crash=0`, **no `CrashReport`**, one boot in 75 s, 71.9 s continuous uptime |
| restore | `!RESTORE_WORKSPACE 5202` | `Restored: /MidiLooper/current.bad.5202 -> /MidiLooper/current` and `checkpoints.bad.5202 -> checkpoints`; parked current=1 checkpoints=1, restored current=1 checkpoints=1 |
| boot with the library | `!REBOOT` (twice) | `crash=0`, `Current workspace loaded successfully`, `BOOT,load,ok`, `post_restore_census free_chunks=407 admission_complete=1 interactive_ready=1 undo_hydrated=1 transport_stopped=1 deferred_prewarm_idle=1` |

The 24-bar body is present **and current** after the load, read from the library observation:

```
LIBOBS,S,t=4,s=0,st=2,c=n,p=3,o=2,q=5,w=0,m=1,g=0,e=0,k=1,kp=1,kr=1,kc=1,a=1,h=1,l=18432,v=1152,n=1,i=0,x=0
```

— track 5 (**t=4**), Loop 1 (**s=0**), **`l=18432` ticks = 24 bars**, 1,152 events, `kp=1 kr=1 kc=1` (published,
revision and coverage all current). The observation itself completed without a restart: `LIBOBS,E,…,t=64,r=0,s=64,p=64`
and `LIBOBS,status,…,turns=64,restarts=0,collected=64,emitted=64`.

The library is not a stub: the observation reports content-filled slots on seven tracks with independent lengths
(`t=0,s=0 l=15360 v=729`; `t=1,s=2 l=13824 v=989`; `t=4,s=0 l=18432 v=1152`; `t=7,s=0 l=13056 v=1742`), and the pool
is still consumed (`chunks_free=407` of 512, snapshots 842 KB, external `live=3,319 KB` against 1,801 KB empty).

So the fault is now **re-verified on hardware**: the same body that crashed the boot loop loads and reads back current.

### A hazard this run re-encountered (already documented; not this change's scope)

`!RESTORE_WORKSPACE` with no stamp restores the **maximum numeric** `…bad.<stamp>` tree
(`findNewestQuarantinedSibling` compares stamps numerically). The stamp is `millis()` — device *uptime* when the tree
was parked, not wall-clock — so a bare restore would have selected `current.bad.623624` (parked after ~10 min of uptime)
instead of this session's `current.bad.5202` (parked ~5.2 s into a boot, i.e. what the quarantine-during-boot window
produces). The restore here was therefore issued as `!RESTORE_WORKSPACE 5202`, the stamp this session's own quarantine
row named. This is the same trap already recorded in `docs/Runtime/CURRENT_WORK.md` for the 2026-09-17 session (*"the
stamp … cannot be derived from the host … identification must use the boot signature, never the name"*), whose standing
rule is the `list`-first procedure in `.cursor/rules/Destructive-SD-Operations.mdc`. Followed here; nothing new to fix,
and the record is cross-referenced rather than duplicated.

## 7. Captures

| capture | what it holds |
|---|---|
| `captures/regression-fix-step2-boot-01.log` | flashed-image boot with the library still quarantined; `boot_reset,crash=0` |
| `captures/regression-fix-step3a-quarantine-list-01.log` | quarantine inventory (21 `current.bad.*`, 22 `checkpoints.bad.*`) |
| `captures/regression-fix-step3b-restore-01.log` | the restore itself (both trees, four flags = 1) |
| `captures/regression-fix-step3c-reboot-restored-01.log` | first boot with the restored library: one boot, `crash=0`, `BOOT,load,ok` |
| `captures/regression-fix-step3c-libobserve-01.log` | the library observation: 64/64 slots, `restarts=0`, the 24-bar slot row |
| `captures/regression-fix-step3d-reboot-repeat-01.log` | repeat boot: `crash=0`, `post_restore_census` with the library admitted |
