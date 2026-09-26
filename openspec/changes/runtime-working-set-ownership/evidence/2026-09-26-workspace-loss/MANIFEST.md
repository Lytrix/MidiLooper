# Evidence preservation — 2026-09-26 workspace-load failure

**Status: DEVICE ACTIVITY STOPPED. This directory is a preservation record, not a recovery.**
No further upload, reset, clear, quarantine, load or rebuild was performed after the failure. The SD card
has **not** been touched, and no further firmware boot was induced, because boot recovery can modify
evidence.

This record exists because the decisive artifacts live under `captures/`, which is **gitignored**. They are
copied here so they survive a sweep.

## What is established

1. **The device's current workspace no longer loads, and the runtime comes up empty.** Post-loss boot:
   BOOT,load,start
   [StorageManager] ERROR: CurrentSet meta missing /MidiLooper/current/temp/runtime.bundle.bin
   [StorageManager] Current workspace load failed.
   [StorageManager] Current workspace load failed; attempting boot recovery chain.
   [StorageManager] Boot recovery chain exhausted; starting empty.
   BOOT,load,fail
   [StorageManager] post_restore_census free_chunks=480 admission_complete=1 interactive_ready=1 undo_hydrated=1 transport_stopped=1 deferred_prewarm_idle=1 heap_free=192512
   #CAP,4938622,DIAG,post_restore_census,free_chunks=480,admission_complete=1,interactive_ready=1,undo_hydrated=1,transport_stopped=1,deferred_prewarm_idle=1,heap_free=192512
2. **The library was intact 32 minutes earlier.** The pre-loss read-only census (18:16) and the post-loss one
   (18:48) disagree completely:

   | | pre-loss (18:16) | post-loss (18:48) |
   |---|---|---|
   | `payload_inventory` | 64 | 0 |
   | `committed_at_checkpoint` | 58 | 0 |
   | `initial_playback_required` | 8 | 8 |
   | observation `complete` | True | True |
   | `consistent_view` | False | False |

   The post-loss observation is **complete** (`incomplete_window: false`, all 64 slots observed), and
   `free_chunks=480/512` corroborates independently that almost nothing is resident (pre-loss: ~386-403).
   So this is not an observation artifact.
3. **The clear run's session log ends mid-save.** Last three non-blank lines:
   #CAP,37537095,PERS,work,7263,0,0,LoopPersist,slot:7:3,finalize_slice,ok,write=Header,finalize=VerifyAndRename
   #CAP,37584641,PERS,work,45276,0,0,LoopPersist,slot:7:3,done,ok,write=Header,finalize=Idle
   #CAP,37586180,PERS,work,27,0,0,TrackMeta,track:7,start,ok
   A `PERS,work,...,TrackMeta,track:7,start,ok` with **no completion** is a workspace save still in flight
   when the session closed.

## What is NOT established

- **That the library files are erased.** A workspace-*load* failure and an empty runtime are established; the
  contents of the SD card are not. The manifest/`runtime.bundle.bin` may be missing while slot payloads,
  saved sets and revisions survive. This is exactly what recovery path (a) must inspect — **on a copy**.
- **The exact failure chain.** The interrupted save is a *suspect*, not a proven cause. Ordering, whether the
  temp was discarded by hygiene or never completed, and whether the next reset compounded it are open.

## Build and run identities

| Run | git_commit | firmware_hex_sha256 |
|---|---|---|
| retention run 18:17 (passing) | `5629e7e4f96d` | `da59a7eeb3e0bdd0…` |
| clear run 18:47 (interrupted save) | `bbedfeac5bc8` | `6d9272dd0eccddcd…` |

Host tooling at the faulty revision (HEAD `bbedfeac5bc8`):

| file | blob at HEAD |
|---|---|
| `scripts/hitl_retention_handoff.py` | `3c71b34736a119d2b7e4c041b80169e8fb9b51df` |
| `scripts/hitl/retention_probe.py` | `017daae430d972f084effc2477699e9f38c99980` |
| `scripts/hitl_boot_census.py` | `1512f2498ea1e98de718d7fef246eb88fd937d94` |

## The suspect host defect, at the faulty revision

`scripts/hitl_retention_handoff.py` (preserved verbatim as `driver-at-fault-HEAD.py`) has **no drain on any
early exit**. Lines of interest:

   - L538: `subprocess.run(["pio", "run", "-e", args.build_env, "-t", "upload"], check=True)`
   - L638: `if args.prepare_only:`
   - L651: `return 0`
   - L768: `return 0`
   - L776: `return 0 if report["result"] == "DROVE_SEQUENCE" else 1`

The clear marks the slot dirty and admits a deferred workspace save; `--prepare-only` then returns
immediately, closing the capture session and the serial port while the save is still writing. The HITL skill
(section 7) documents this exact failure mode — *"serial log ends mid-write (e.g. mid `PERS,work`)"*, *"a
bespoke reader that closes right after `stop` reproduces this every time"* — and prescribes
**stop -> drain -> stop -> drain** with the port held open. The driver does not implement it.

## Exact commands that produced the decisive artifacts

```
# 1. deliberate clear of the authorized target (this is the run that ends mid-save)
.venv/bin/python scripts/hitl_retention_handoff.py --track 8 --slot-a 4 --slot-b 4 --slot-c 4 \
    --out-dir captures/rwso-clear-t8s4-20260926_184747 --prepare-only

# 2. read-only census (its --upload reset the board and revealed the failed load)
.venv/bin/python scripts/hitl_boot_census.py \
    --out-dir captures/s1f6c2-verify-20260926_184850 --library-observe
```

## Preserved artifacts

| file | bytes | sha256 (truncated) |
|---|---|---|
| `clear-run-handoff.json` | 1,336 | `3d6e3c0a7913702920f52074bd4130fd…` |
| `clear-run-metadata.json` | 654 | `41ca4edad9359320e60c8eac39285f99…` |
| `clear-run-session.log` | 959,468 | `68f5650fe135a14dc02c92b398fa8e36…` |
| `driver-at-fault-HEAD.py` | 47,090 | `c31f5f441c18553ae58088066ee1987c…` |
| `post-loss-boot-census.log` | 40,828 | `fa5e78a08e9d99338b44be63e1ad4a64…` |
| `post-loss-census.json` | 54,392 | `e434889851b776ca74897d9d8a86a486…` |
| `pre-loss-census.json` | 57,184 | `16ed4630f02e24b288dd73cdc03f9252…` |
| `retention-run-metadata.json` | 651 | `476c1cc2f05d90d65d0f8c3c21acbc72…` |
| `retention-run-review.json` | 7,650 | `b546c343950ae5cc12d95d136e4ecbbb…` |
| `retention-run-session.log` | 1,081,707 | `a62270b998544e9b065acad266544ea0…` |

Originals remain at their `captures/...` paths (gitignored) and must not be deleted. The two session logs are
copied whole rather than excerpted so the ordering can be re-derived independently.
