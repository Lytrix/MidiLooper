# B.5 — capacity budget

Date: 2026-11-01. Device rows: [`2026-11-01-b5-revstore-probe-raw.txt`](2026-11-01-b5-revstore-probe-raw.txt) plus a
second read on the same boot after the slots loaded (quoted below). Native scenarios:
`test/test_revision_store_capacity`.

Units: 1 page = 64 rows × 16 B = 1 KiB.

## 1. What the firmware already holds in PSRAM (device)

Probe image HEX `9be5e19766e1beab…`, boot verified (`#CAP,1015002,DIAG,boot_reset,crash=0`, `BOOT,load,ok`), transport
stopped. Production parameters: 8 tracks × 8 slots = 64 slots; preferred undo depth 99 per track (minimum 8, hard rail
512 entries).

| Reading | Right after `BOOT,load,ok` | Same boot, slots loaded |
|---|---|---|
| PSRAM / extmem pool | 8 MB / 8,376,828 B | same |
| Pool used | 4,139,232 B | **4,931,976 B** |
| Pool free | 4,237,596 B | **3,444,852 B** |
| Event-store chunks used (of 512, 5,216 B each) | 0 | 68 (667,648 B backed) |
| Live by block size: > 128 KB | 1,048,576 | 1,716,224 |
| ≤ 128 KB | 1,926,795 | 1,943,547 |
| ≤ 8 KB | 984,064 | 1,031,912 |

Reading: about 1 MiB is the capture env's 1 MiB session ring (one block over 128 KB at boot). About 1.9 MB is the
boot-reserved rolling views (DEC-243). The event content itself is only 0.67 MB of chunks. The accounting's high-water
on that boot (5,285,567 B) includes the probe's own 1.1 MB arena from the first run, so **the session high-water of the
firmware alone (playing, overdubbing, stop edges) is not yet measured.** That is the remaining gate (B.5b).

## 2. What one slot needs (native, real store)

Session per slot, the worst case drawn from device evidence:
- a 64-bar Record of 16,640 rows (260 rows per bar, the densest observed bar)
- 30 overdubs of 1,040 rows, each hiding up to 4 earlier notes
- an undo, then a different overdub, every fifth overdub (a discarded branch)
- a reader pinning the previous generation while each overdub is written
- stepped compaction (2,048 rows per step) interleaved with the next overdub whenever the manifest reaches 8 runs

The final revision holds 649 pages of content.

| Compaction policy | History | Peak pages | End pages | Peak / content |
|---|---|---|---|---|
| none | 16 | **703** | 703 | **1.08** |
| whole prefix (Record included) | 16 | 1,804 | 1,164 | 2.78 |
| whole prefix | 99 | 2,169 | 1,529 | 3.34 |
| overdubs only (range `[1, n)`) | 16 | 1,283 | 1,215 | 1.98 |
| overdubs only | 99 | 1,320 | 1,320 | 2.03 |

Every policy ends with the identical ordered stream (hash-checked), and none needed a fallback sort.

**Finding: compaction duplicates content for as long as history references the originals.** Undo to revision k must
reproduce exactly revision k, so the runs it names stay alive. A merged run is therefore a second copy until those
history entries age out. Compacting the Record roughly triples the peak. Range compaction of the overdubs only (added
in this slice), with every matching history entry rewritten on publish (also added), halves that, but it is still
about 2× content. Without compaction the peak is only 8 % above content: the writer's open page, partial last pages
and the pinned previous generation.

## 3. Consequences for the design

1. **Compaction should only take ranges that history has closed.** A range qualifies when every live history entry
   and pin holding any run of it holds the whole range at the same positions. Publishing then rewrites all of them and
   frees the originals at once, so the peak is content plus one range in flight. With that rule the slot figure is the
   "none" row plus the in-flight range, which can be capped (e.g. 256 pages).
2. **Composition must not depend on compaction to stay within budget.** The 10-run window took 3.3 ms (B.4b) with a
   linear cursor scan. With undo depth 99 a manifest legitimately holds tens of runs, so composition needs a heap merge
   (O(log cursors) per row) and should skip runs that have no hides. `kRunsPerManifest` must then be sized to undo depth
   plus margin; a manifest stays tiny at 2 B per run id.
3. **Run descriptors must not embed a max-size page directory.** In this configuration the descriptor tables are
   569,956 B per store, almost all of it a fixed 1,024-entry page directory (2 KiB) in each of 255 runs. That is
   prohibitive across 64 slots. Directories should themselves live in pages (512 page ids per page), with one global
   run table shared by all slots, so a descriptor is about 64 B.

## 4. Budget

Store-wide, not per slot: one page pool shared by all slots. Admission is sized to the same ceiling production uses
today: the event store admits 512 chunks × 256 = 131,072 events.

```
rows at the admission ceiling          131,072 rows            = 2,048 pages
+ per-run waste (avg ½ page; ~640 runs across 64 slots)        ≈   320 pages
+ one compaction range in flight (capped)                      ≈   256 pages
+ writers' open pages, pinned previous generations              ≈    64 pages
= page pool                                                     ≈ 2,688 pages = 2.63 MiB
+ metadata after (3.3): 1,024 runs × 64 B + 792 manifests × 80 B ≈   0.13 MiB
= store total                                                   ≈ 2.76 MiB
```

Compared with today:
- **Same content today:** 512 chunks × 5,216 B = 2.67 MB of event-store backing at the same ceiling. The store needs
  about the same memory for the same admission ceiling, *including* history and compaction headroom, as long as
  rule (3.1) holds.
- **Side by side during migration (stages C–D):** pool free after load is 3.44 MB, so a 2.76 MiB store would leave
  about 0.55 MB. That is not enough until the session high-water is known (B.5b).
- **Once the store replaces them:** it takes over the event-store chunks (up to 2.67 MB at the ceiling) and,
  eventually (stage E), the per-slot capture stagings and some view buffers. The 1.9 MB of boot-reserved rolling views
  is a separate, playback-side obligation that the store does not remove by itself.

## 5. Gates left in B.5

- **B.5a — store changes (3.1–3.3):** history-closed compaction, a heap-merge compose that skips hide-free runs,
  paged directories with a global run table. Re-run this suite and the B.4 probe after.
- **B.5b — session high-water on device:** run the owner-rig baseline chain on the probe image (no store active) and
  read `acct_high` with `!REVSTORE_PROBE` afterwards. That gives the real reserve beside which the store has to fit
  during migration.
