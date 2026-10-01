# EVIDENCE — S2b.3 post-restore half (2026-10-01, current image, large library)

**Run:** `captures/s3gate-boot-census-01` (`scripts/hitl_boot_census.py`, reset by re-upload then attach)
**Image:** `a1f4e4c0d2f43ee354782cc6e4fa56ab4c8e268cb774e60a252f763189b2696c`
**Requirement (S2b.3):** with N committed slots, `LoopEventStore::freeChunkCount()` **after boot** is
`>= peak reservation + CHUNK_RESERVE` (16), and the boot pool `live` figure is recorded against S1.5.

## Measured

```
[census] banner=True reconcile=0 checkpoint_reached=True
[census] POST-RESTORE free_chunks=397 heap_free=159744
         (admission=1 interactive=1 undo=1 stopped=1 deferred_prewarm_idle=1)
[census] marker readiness_attested=True timestamped_capture_line=True
   checkpoint 0 (enabled-set walk): 512 -> 480 (delta -32)
   checkpoint 1 (library walk):     480 -> 480 (delta +0)
[census] walk rows=17 peak capture credits=0 (reservation telemetry; zero is EXPECTED for restored slots)
[census] restore terminals seen in window=2 -> post-restore checkpoint available=True
[census] S2b.3: free=397 vs required 16 (peak_capture_reservation+16) -> met=True
```

* **`post_restore_free_chunks = 397` of 512** against a required **16** → the condition is **met** with 381 chunks
  of margin, and the marker is a *timestamped* capture line whose readiness the firmware attests
  (`admission/interactive/undo/stopped/deferred_prewarm_idle` all 1), so it is the post-restore state and not a
  walk artefact.
* **The per-slot slope is absent on this library too:** the library walk goes `480 → 480` across all 17 `(track,
  slot)` rows with `credits=0` on every one — the same flat-reservation shape the 2026-09-23 device half recorded,
  now on a much larger committed set.
* **Boot pool `live` against S1.5:** the boot census log's first `[Memory] ext` rows read **`live=1801 KB`**
  (then 1949 KB once the restore and the enabled set settle), with `untracked=0`/`unknown=0`.
* Context: the enabled set costs 32 chunks (512 → 480, the S2b.1 shape), and the restore adds nothing on top in the
  walk; `restore_terminals_seen = 2`.

## Not claimed

* No baseline comparison: the task's own 2026-09-23 correction says the *baseline's* post-restore value was never
  measured, and this run is HEAD-only.
* N is reported as "the current library" rather than as a counted N with a per-N sweep; the **slope-to-failure
  sweep** (varying N across boots) is a separate open item under S2b gates, and this run does not answer it.
* The walk checkpoints (512 → 480, 480 → 480) are walk pairs; the post-restore figure is the marker's `free_chunks`
  (397), as the script itself states.
