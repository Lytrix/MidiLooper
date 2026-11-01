# B.4 — on-target cost of the prototype store (Teensy 4.1, not wired to playback)

Date: 2026-11-01. Image: env `teensy41-capture-serial-revstore-probe`, HEX `e5db1dfc14a439a9…` (SHA-256 prefix).
Boot verified over serial: `#CAP,1669002,DIAG,boot_reset,crash=0`, `BOOT,load,ok`. Raw rows:
[`2026-11-01-b4-revstore-probe-raw.txt`](2026-11-01-b4-revstore-probe-raw.txt). Runner: `scripts/revstore_probe.py`.

## Set-up

- The default capture image plus the stopped-only serial command `!REVSTORE_PROBE` (compiled only with
  `REVISION_STORE_PROBE=1`). RAM1 is identical to `teensy41-capture-serial` (variables 96,192; code 391,944;
  padding 1,272), so the probe costs no RAM1 or ITCM; its RAM1 budget is a reference to the default env.
- Probe code runs **from flash** (routed by `linker/imxrt1062_t41_lcr.ld`), with the store's 1.04 MiB arena in PSRAM
  (`extmem_malloc` per run, freed after). The figures are therefore a conservative bound: ITCM-resident hot paths
  would be faster.
- Content: a 64-bar loop. The densest take is 16,640 rows (8,320 notes, 260 rows per bar, the densest observed
  device bar), appended in capture order starting mid-loop so it wraps. On top of it go eight overdubs of 1,040 rows
  each and a one-row overdub. Timing comes from the cycle counter at 600 MHz.

## Results

| Operation | Size | avg | max |
|---|---|---|---|
| Store construction / reset (one-time, boot) | 1 MiB arena | 42.3 ms | — |
| `beginPass` (admission: run + page + manifest slot) | — | 51–88 µs | 95 µs |
| `appendMidi` (per row) | — | < 1 µs | 11 µs |
| Commit of a 1-row pass (manifest publication only) | — | **< 1 µs** | — |
| Commit + seal, Record | 1,000 rows | 1.4 ms | |
| Commit + seal, Record | 4,000 rows | 18.1 ms | |
| Commit + seal, Record | 16,640 rows | **133.8 ms** | |
| Commit + seal, overdub | 1,040 rows | 1.4 ms | 1.7 ms |
| Compose a 2-bar window, 10 runs | ≤ 886 rows | **2.09 ms** | **2.79 ms** |
| Compose a 2-bar window, 1 run (after undo) | ≤ 619 rows | 0.49 ms | 0.65 ms |
| Compose a 2-bar window, compacted (1 run) | ≤ 886 rows | 0.85 ms | 1.44 ms |
| Undo / redo (manifest selection) | — | **≤ 2 µs** | |
| Compaction of all 10 runs (synchronous) | 24,961 rows | **67.4 ms** | |

Compaction peak: free pages 627 before, 237 after. The 390-page (390 KiB) compacted run coexists with the old runs,
which revisions in history still reference.

## Reading against the stage-B question

**Fits the envelope, no real-time construction:**
- **Publication is free.** Committing a manifest takes under 1 µs, and undo/redo take 2 µs or less. The
  "revision exists at commit" property costs nothing measurable.
- **Admission is tiny.** Starting a pass takes under 0.1 ms with zero bulk allocation, against today's 1.79 MB per-slot
  record prewarm (DEC-243) and the 89 ms staging allocation and 45 ms source-view open on the overdub-start edge.
- **Append is under 1 µs on average and 11 µs at most**, so it can sit on the capture path.
- **A 2-bar window from 10 runs takes 2.8 ms worst case, from flash.** That is within the 8 ms real-time child budget
  (DEC-212) and well inside a 2-bar period. Run count costs about 2.4× between 1 and 10 runs, mostly the per-row hide
  check against later runs; compaction brings it back to 1.4 ms worst case.

**Does not fit as built:**
- **Seal is a content-sized sort.** A heapsort over PSRAM rows grows super-linearly: 1.4 ms at 1k rows, 18 ms at 4k,
  134 ms at 16.6k. It is under the 8 ms budget only below roughly 2,500 rows, so a dense Record stop cannot seal on
  the edge. Two ways out, either keeping commit instant:
  1. *Sort-free seal:* capture appends are already in tick order except for one wrap point. Keep the run as two sorted
     segments (before and after the wrap) and let composition treat them as two cursors. No sort at all; equal-tick
     order is still append order (DEC-244). This is the preferred direction.
  2. *Deferred seal:* publish the manifest at commit with the run marked unsorted, and sort it in bounded grains
     off-clock. Readers of an unsorted run would need a fallback scan, which brings back a "not ready yet" state, so
     this is less attractive.
- **Compaction is content-sized too:** 67 ms for 25k rows, about 370 rows/ms. At the 8 ms budget that is a grain of
  about 2,900 rows per step. The prototype's `compactCurrentPrefix` is synchronous, so a step-bounded version is
  required before stage D. Compaction never decides correctness (the old manifest stays valid), so a slow compaction
  only costs window-compose time (2.8 ms vs 1.4 ms here).
- **`beginPass` takes 51–95 µs** because it value-initialises a 512-entry page directory and scans slot tables
  linearly. Harmless, but easy to cut.

## Not measured

- An ITCM-resident compose (the realistic hot path); the flash figures are an upper bound.
- Seal and compose interleaved with live playback and MIDI ingress (the probe ran with the transport stopped).
- The capacity budget (B.5): pool size, history depth, compaction peak across all slots.
