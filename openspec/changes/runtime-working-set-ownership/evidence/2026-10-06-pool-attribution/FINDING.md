# Pool attribution — the park's requesting owner is the display projection, and the pool does not ratchet down at a leg boundary

**Date:** 2026-10-06 (measurement episode; **no firmware change, no SD delete, no quarantine**).
**Scope:** the pasted brief's two attribution questions — *does the pool leak or is it legitimately retained
across repeated long-loop legs*, and *name the requesting owner at the refusal*. It is the measurement the
`windowed-playback-generation` hardware half (6.4/6.5 — windowed HITL profile, native/hardware split) needs
before a long device leg is safe to drive, and the failure-attribution clause of **S1.6b — failure attribution**
in this change.

## 1. Identity — the ELF used to resolve the crash-site is the flashed image

| item | value |
|---|---|
| tree | `147f90d5` (`git status` clean) |
| env | `teensy41-capture-serial`, rebuilt locally (no upload) |
| rebuilt hex sha256 | `9697b7acacb262d201a4f84699c2ecd99c552219493ef0e8e83ba7d66cf6ec33` |
| recorded flashed hex | same value — `captures/i5-armrepro-01-windowed/metadata.json` |
| RAM1 line | `variables 96,192 / ITCM 393,216 (code 392,360 + padding 856) / locals 34,880` — the I5 landed figure |

The rebuild is **byte-identical** to the image that parked (`firmware.hex` sha256), so `addr2line` against the
rebuilt `firmware.elf` names the owner of the parked run's refusal address. The previous session's **pre-I5**
artifact that occupied the shared build cache is preserved at `captures/prei5-image-backup/` (hex
`4273f9e1a0c2…`); the A/B flash is therefore still reproducible without a rebuild.

## 2. The park, and the owner the refusal names

`captures/i5-armrepro-01-windowed/session_20261006_113601.log` (device 116.4 s → 117.2 s):

```
[Memory] ext,116418041,phase=tick,live=6401KB,high=6773KB,blocks=940/1211,alloc=40998KB,freed=34596KB,
  untracked=0,unknown=0,refused=0/0B,cls=5/8/732/1606/933/3114KB,clsn=694/17/147/54/21/7,
  tags=584/715/148/0/1/0/58KB,chunks_free=378,snap=1098KB,slab_live=40,slab_used=1248KB,
  slab_unused=31KB,pin1=0,pin24=0,heap_free=144KB,heap_min=144KB,accounting=2
#MEM_EXT_REFUSED,117158581,bytes=393216,count=1,live=6797KB,blocks=940,heap_free=144KB,tag=0,
  tag_live=4841KB,tags=584/715/148/0/1/0/58/0/0/10/2/64/66/68/96/144/0,accounting=2,caller=0x6009ea0f
[ExternalMemoryFirstAllocator] FATAL: both external memory and internal heap are exhausted!
```

* `caller=0x6009ea0f` resolves (addr2line, `-f -C`) to
  `std::vector<NoteUtils::DisplayNote, ExternalMemoryFirstAllocator<NoteUtils::DisplayNote>>::reserve(unsigned int)`
  — i.e. inside the **DisplayNote vector reserve**, the allocator's own caller.
* `bytes=393216` with `sizeof(NoteUtils::DisplayNote) == 16` (verified: `uint32_t` NoteId + 2 × `uint8_t`
  + 2 × `uint32_t` = 14 B padded to 16 on both the host and the cortex-m7 toolchain) ⇒ the refused request is a
  **24,576-element display-note copy (384 KB, size class 5)**.
* Every `reserve` on that instantiation in the tree belongs to a **display / note-edit projection** owner
  (call sites enumerated by disassembling the same ELF): `dedupeProjectedDisplayNoteVec` (the `NoteVector`
  instantiation — `src/Utils/NoteUtils.cpp:905`), `filterSelectableDisplayNotes`,
  `applyPendingHideAndShortenToNotes`, `Loop::applyPendingNoteChangesToOverdubSourceView`,
  `Loop::accumulatePendingNoteChangesForIncomingNote`, `Loop::adoptComposedDisplayNotesFromViewport`,
  `Loop::rebuildVisualCacheIdleSlice`, `DisplayWindowUtils::keepShortestEndAtPitchStart{,OnCommittedPrefix}`,
  `DisplayWindowUtils::filterDisplayNotesToWindow{,ByWindowInclusion}`,
  `projectNoteEditDisplayNotes<InternalHeapFirstAllocator<MidiEvent>>`.
* **Attribution:** the refusing allocation is a *display projection copy*. It is **not** the windowed lane
  (no `PlaybackView*`/`WindowPagedStream`/retained-source owner appears at the address), and `tag=0` with
  `tag_live=4841KB` says the bulk of the pool has **no attribution bracket**.
* **Binding constraint is contiguity, not total free bytes:** at the refusal the pool held `live=6401` of
  8192 KB (≈1.78 MB unallocated) yet the 384 KB request failed, and the class vector shows where the bytes sit
  (`cls=5/8/732/1606/933/3114KB` — 3,114 KB in only 7 large blocks). The `largest_gap`/`largest_request`
  figures that would prove this are **unreachable** — see §5.1.
* **Why it parks instead of refusing:** `ExternalMemoryFirstAllocator::allocate` tries extmem, then `malloc`,
  then prints the FATAL line and calls `abort()` (`include/Utils/ExternalMemoryFirstAllocator.h:62`). That is
  defect #1 recorded 2026-09-17 (`docs/Runtime/CURRENT_WORK.md:3764`) and **DEC-075's scope boundary leaves it
  unfixed** (`docs/DECISION_LOG.md:797`): no `DEC-###` and no task owns making it deny instead of park.

## 3. Retention: the pool does not ratchet down at a leg boundary (measured today, one boot)

Device state: power-cycled, library restored (`!LIB_OBSERVE`: 64/64 slots, 64 payload, 43 committed at the
checkpoint). Runs, all `--no-boot-reset` (attach, no reset):

| step | capture | `live` | notes |
|---|---|---|---|
| at rest, transport stopped | `captures/attr-02-state`, `attr-04-leg1` | **3274 KB**, `blocks=748/898`, `tags 1..17 = 584/0/0/0/0/0/0/0/0/0/0/0/0/0/0/0/0` (tag_total **584 KB**) | **58 % of live is untagged** (2690 KB); `chunks_free=402`, `snap=813`, `slab_live=33/slab_used 1025`, `heap_free=144` |
| leg 1 (base preset, track 8 slot 4) | `captures/attr-04-leg1` | 3274 → **4675 KB in one 1 s step**, holds **4702 KB** | delta = tag2 `ViewPrewarm` **+715**, tag3 `CaptureStaging` **+148**, tag7 `PlaybackOrder` **+58**, tag5 +1 ⇒ **+922 tagged / +506 untagged** (class2 +477, class3 +83, class4 +864) |
| after the leg (12 s) | `captures/attr-04-leg1` | 4702 KB, **flat** | all counters identical |
| scoped clear of track 8 slot 4 | `captures/attr-05-clear4` | 4702 KB, **flat for 16 s** | `alloc=8346 / freed=3644` **identical** to the pre-gesture row, classes/tags/blocks/`chunks_free` identical; firmware row `MIDI: Clear removed persisted slot 4 payload (absent)` |
| leg 2 (base preset, track 8 slot 5) | `captures/attr-07-leg2` | **attaches at 4702 KB, byte-identical counters** (`alloc=8346 / freed=3644`) | nothing returned between leg 1, the stop and the clear (~2 min) |
| leg 2's own attempt | `captures/attr-07-leg2` | churn `alloc 8346→18865`, `freed 3644→14121` (**10,477 KB returned**), net **+41 KB**, `blocks high-water 1377` vs `live 913` | aborted at its own precondition (`clear before transport start failed`) |

**Reading.** One leg's footprint is dominated by the *arm residency* (view prewarm 715 KB + staging 148 KB +
order 58 KB + ~506 KB untagged) and it is **retained after the leg ends** and is **not returned across a
transport stop, a clear gesture, or the next leg's attach**. The allocator does release memory heavily
(10.5 MB churned through during leg 2, 34.6 MB over leg 1) — so this is not "nothing is ever freed"; it is
residency of an armed/committed slot set that the pool's own numbers do not attribute. The per-class
decomposition of that residency is exactly the `P`/`C`/`D`/`O` class budget **S2.2/S2.3** measured, whose
conservative sum was already reported as fitting **only** if the cardinalities are not all maximal
(12,626 KiB against an 8,192 KiB pool).

**Recorded rather than smoothed over — what this does *not* decide:**
* The clear gesture's **RAM-level completion is not established** by these rows: the firmware reported the
  *persisted* payload, no `Track cleared` row and no `chunks_free` delta appeared, and no admission row was
  emitted. "The clear released nothing" is therefore **one observation of a flat pool across the gesture**,
  not proof that a *completed* RAM clear is release-free.
* Leg 2 aborted at its own precondition (`clear before transport start failed`) — the recurring bench setup
  failure recorded in earlier episodes — so no second-leg growth figure exists.
* **Leak versus legitimate retention is therefore not yet separable** from these counters. What *is* separable
  and actionable: **58 % of the residency carries no tag at all**, so no owner can be named for it without
  adding brackets.

## 4. Where the windowed lane stands (checked, not assumed)

* `Track::retainPlaybackSourceForSlot` / `tryReleaseRetainedPlaybackSource` have **no production caller**
  (`src/Track/TrackPlaybackWindowBuild.cpp:1226`/`:1323`; the only callers are
  `test/test_runtime_working_set_ownership/*`), so `playbackWindowSourceIsCurrentForSlot` answers only through
  `publishedSnapshotIsCurrent()` on a shipped image: the retained-source serving family (3.7a I1/I2/I3b/I3c/I5)
  is **unreachable in production** until something admits a retained source.
* `LoopPlaybackRuntime::discardRetainedGather` releases the gather's `SessionMidiEventVec` **by swap** — the
  capacity is deliberately not left resident per slot (`src/Track/TrackPlaybackRuntime.cpp:2167`), and
  `pagedWindow.release()` releases the page ring. So the lane holds no per-slot pool residency to attribute.
* The paged/retained path is **not** behind `PLAYBACK_WINDOW_ROLLING_ENABLED` (only the rolling lane is), so it
  *can* be production-visible; but it cannot be the retainer while no retained source exists.

## 5. Instrument findings (recorded, not fixed here)

1. **`!HEAP_STATS` refuses `external_clock` on this device in every attempt** — after the power cycle with the
   transport playing, and twice with the transport stopped, clock source `INT` (`#CAP,…,CS,EXT,INT` at 1024.1 s
   and no later transition) and `#CAP,…,DIAG,clockrate,0`. The gate is
   `isExternalClockPresent() || recentClock(<1 s)` (`StorageManagerHitlSerial.cpp:786-789`) and
   `onMidiClockPulse` sets the observed-pulse flag **even while stopped** (`ClockManager.cpp:222-230`), so
   clock pulses were arriving within 1 s of each request (sender not identified). Consequence: the **only**
   source of `largest_gap`/`largest_request` — the fragmentation figure this episode needed — is unavailable,
   and the stopped allocator inspection should be read as unreachable here. Not a firmware claim.
2. **The "wedge" of the previous session was host-side, and partly an orphaned reader.** A leftover
   `scripts/capture_session.py` (`PID 54817`, `PPID 1`) had held `/dev/cu.usbmodem154944801` for 13 minutes and
   was still draining the device (763 KB log, growing at kill time), so the device was never wedged; every
   tool that opened the port afterwards read nothing (my first census: 0 lines). `kill -TERM 54817` freed the
   port and the device answered `!LIB_OBSERVE` immediately. This is the skill's documented host-side trap
   (closing/duplicating the serial fd while the device is still emitting) plus a leaked session.
3. **`#MEM_EXT_REFUSED` already names the requester** (`caller=0x…`, resolvable against the byte-identical ELF)
   and the size, so "name the requesting owner in the refusal" is *already* half-delivered for the pool path.
   What is missing is (a) an **operation-level refusal** (deny instead of `abort()`), and (b) any **admission
   gate** for the display-projection owners, which is the one allocation class observed to still reach the
   allocator unguarded.

## 6. Not claimed

* No leak is proven and no retention rule is named as the culprit; the leak-versus-residency question is
  explicitly left open (§3).
* No windowed-lane attribution: the lane's retention has no production caller and its gather storage is
  released by construction (§4).
* No firmware, test, DEC or OpenSpec task change; no admission gate added; no `large-request` figure measured.
* No SD delete/quarantine. Device writes were confined to the authorized bench range (**track 8 slots 4, 5**,
  with the owner's explicit clear authorization for this session) and to the two `base`-preset legs.

## 7. Captures and commands

```
captures/attr-01-baseline/        hitl_boot_census.py --no-reset --library-observe --heap-stats   (port was held by the orphan; empty)
captures/attr-02-state/           hitl_boot_census.py --no-reset --library-observe --heap-stats   (64/64 slots; HEAP_STATS refused)
captures/attr-04-leg1/            host_midi_hitl.py run --preset base --track 8 --loop-slot 4 --midi-channel 8 --no-boot-reset
captures/attr-05-clear4/          hitl_clear_reload_probe.py --track 8 --slots 4 --no-upload
captures/attr-06-heap/            hitl_boot_census.py --no-reset --heap-stats                     (HEAP_STATS refused)
captures/attr-07-leg2/            host_midi_hitl.py run --preset base --track 8 --loop-slot 5 --midi-channel 8 --no-boot-reset
captures/i5-armrepro-01-windowed/ the park (previous session), device 116-117 s
captures/prei5-image-backup/      the previous session's pre-I5 hex/elf (hex 4273f9e1a0c2…)
```

Both `base`-preset legs ran `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)` on their own oracles (`record phase saw
0 midi clock pulses`; `clear before transport start failed`) — they are used here for the **pool rows**, which
they carry, and are not cited as leg passes.
