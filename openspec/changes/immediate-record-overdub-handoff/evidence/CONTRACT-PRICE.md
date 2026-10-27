# C2.4 — contract price: custody receipt and undo field

**Status: the metadata tier is measured; the cap VALUE remains BLOCKED.** This annex prices the two
types the contracts introduce, on the shipping ARM ABI and on the host, using the preserved probe.
It does not measure allocator overhead, stack high-water, target allocator execution or graph
occupancy, so `kCaptureCustodyCapacity` is still not frozen and no capacity is claimed.

Contract being priced: [CONTRACTS.md](../CONTRACTS.md) C1.1 (custody receipt), C1.4/C1.5 (slab and
release), C2.1–C2.4 (charge sites, derivation, blocked terms), C4.3 (`UndoEntry::interval`).
Baseline `faa089b8` plus the existing dirty source. No firmware, test, build, upload, device or SD
change; outputs are scratch-only.

## Method

The preserved probe was extended with **declaration-only mirrors**: `ContractCustodyReceipt`,
`ContractCustodySlab<4>`, `ContractCustodySlab<5>` and `ContractUndoEntryWithInterval`
(`evidence/probes/custody-size.cpp`). Each mirror uses the real production types for its members
(`LoopEventStore::CaptureRange`, `Loop*`, `LoopId`, `UndoEntry`), so the padding and alignment that
decide the target price are the production ones. No firmware type is modified, no mirror is included
by firmware, and no produced object is linked into an image.

`ContractUndoEntryWithInterval` wraps `UndoEntry` and appends the field, which is the worst case: no
existing trailing padding can absorb the new member.

Two runs, same runner and same flags:

| Run | Probe | Purpose |
|---|---|---|
| control | unmodified, before the mirrors were added | confirm the published table still reproduces |
| contract | with the mirrors | price the contract types |

The pre-existing rows are **byte-identical** between the two runs (`diff` exit0), so the flag
snapshot, toolchain and baseline are unchanged and the new rows are additive evidence, not a
re-baseline.

## Measured layout

| Type | Host bytes | Shipping ARM bytes | Note |
|---|---:|---:|---|
| `CaptureRange` | 112 | 80 | unchanged control row |
| `ChunkReservation` | 1,040 | 1,040 | unchanged control row |
| `CaptureStopPresentation` | 160 | 120 | the record the receipt supersedes |
| `CandidateCustodyBox` | 168 | 128 | **superseded**; was "presentation + two 32-bit words" |
| `CaptureStopJobs` | 42,016 | 29,704 | the ring the slab joins |
| `UndoEntry` | 200 | 132 | unchanged control row |
| **`ContractCustodyReceipt`** | **160** | **120** | C1.1 field-complete |
| **`ContractCustodySlabK4`** | **648** | **488** | `K_trace` = 4 + owner word |
| **`ContractCustodySlabK5`** | **808** | **608** | `K_reader` = 5 + owner word |
| **`UndoEntryWithInterval`** | **208** | **144** | C4.3 append-at-end worst case |

The receipt is **exactly the size of the presentation it replaces** (120 ARM). Its three added bytes
(`state`, `obligations`, `custodyIndex`) are absorbed by alignment padding that already exists in
`CaptureStopPresentation`, and it drops `armed`/`retirementActive` because `state` carries them. The
`128`-byte candidate box is superseded by a measured `120`.

## Charge site

`Track::prewarmCaptureStopJobs` performs **one** external allocation per Track and placement-news the
ring into it:

```cpp
auto* storage = ExternalMemoryFirstAllocator<CaptureStopJobs>{}.allocate(1);
captureStopJobs_.reset(new (storage) CaptureStopJobs{});
```

The custody slab is a member of that same object, so the charge is one object per Track:

| Shape | `sizeof(CaptureStopJobs)` | Delta per Track | 8 Tracks |
|---|---:|---:|---:|
| today | 29,704 | — | — |
| K = 4 | 30,192 | +488 | +3,904 |
| K = 5 | 30,312 | +608 | +4,864 |

Placement and fallback, as the contract requires:

- **No static growth.** The array is a heap member of a runtime-allocated object, so there is no
  `.bss`/DTCM/RAM1 growth for the slab itself; the existing 4-byte `unique_ptr` is unchanged.
- **External first, internal fallback.** `ExternalMemoryFirstAllocator` tries `extmem_malloc`, then
  `std::malloc`, then aborts. A *successful* admission can therefore charge the internal heap on the
  fallback path, so the cap must not assume external placement and `R_rec` (C2.3) is not a guarantee
  against that path.

## Metadata is not the binding tier

`floor(A_custody_meta / sizeof(CaptureCustodyReceipt))` from C2.2 is not what limits `K`:

- At 120 bytes the receipt is metadata-neutral per admitted interval; the slab is additive only in
  the transition where a slot record and a receipt briefly coexist.
- Eight Tracks at `K_reader` cost 4,864 external bytes, against a device-measured PSRAM peak with
  roughly 4.3 MB free (recorded on the 2026-10-22/23 device legs). The **pool condition** dominates:
  512 chunks, 333,312 bytes backing per 64-chunk block, up to 2,666,496 bytes across 8 blocks.
- If `presentations[slot]` becomes a handle (C1.1), the replacement is *smaller* than eight full
  records. That is a design consequence, not a measured saving, and it is not claimed: the price
  above is the conservative additive one.

## Corrections to the contract text

Two estimates in [CONTRACTS.md](../CONTRACTS.md) were written before this measurement and are
corrected to the measured values:

| Clause | Was | Measured |
|---|---|---|
| C2.1 receipt row | "≈156–164 target bytes pending probe" | **120 target bytes** |
| C4.3 undo field | "`132 → 140` nominal, `≤ 148` with alignment" | **144** (`+12`, alignment-8 rounding) |

## What C2.4 still needs

Unchanged and still UNKNOWN — none of these is zero:

- target allocator overhead and fragmentation for the larger ring object;
- RAM1 stack/call-frame high-water (the retirement lane builds a 720-byte `CaptureStopJob` on the
  stack);
- target allocator *execution* (this probe compiles and links ARM objects but runs only natively);
- roots/undo/visual/persistence graph occupancy beyond wrapper `sizeof`;
- `R_rec`, which must cover one re-arm grain plus one retirement grain per admitted receipt.

Procedure for the remainder: take the `teensy41-capture-serial` link/map figures with the slab
actually declared, plus the stack high-water from a registered measurement run. Until then
`kCaptureCustodyCapacity` stays absent and no packet may hardcode a number.

## Commands and results

```sh
# control, before the mirrors existed
python3 openspec/changes/immediate-record-overdub-handoff/evidence/probes/run-custody-probes.py \
  --out-dir "$PI_SCRATCH_DIR/custody-control"
# contract price, with the mirrors
python3 openspec/changes/immediate-record-overdub-handoff/evidence/probes/run-custody-probes.py \
  --out-dir "$PI_SCRATCH_DIR/custody-contract"
```

- Both runs: `custody-arm` exit0, `custody-arm-nm` exit0, `custody-host-build` exit0, `custody-host`
  exit0, `custody-allocator-build` exit0, `custody-allocator` exit0.
- `diff` of the two size tables excluding the new rows: **identical**, so no drift.
- Measured outputs stay in session scratch (`custody-sizes.md`, `*-command.txt`, `*-version.txt`).
- Not run here: firmware link, device timing, any HITL stimulus. This annex adds no production
  resource and no capacity claim.
