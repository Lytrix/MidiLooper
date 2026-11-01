# C2b + C3a — the shadow store on the device, under the owner chain

Date: 2026-11-01. Image: env `teensy41-capture-serial-revstore-probe`, final HEX `dbc009f754beadec…`. Each run
started from a forced fresh boot verified in its session log (`boot_reset,crash=0`, `BOOT,load,ok`). RAM1 is identical
to the default capture image (96,192 / 391,944 + 1,144 padding in the probe image; the default image is unchanged at
96,192 / 391,944 / 1,272, so the new registry row cost nothing there).

## What landed (DEC-246, all behind `REVISION_STORE_PROBE`)

- **`RevisionShadow`** (`src/RevisionStore/RevisionShadow.cpp`, routed to flash). One `Store` (pool 448 × 1 KiB pages;
  tables included it is **487,176 B**, inside the 0.5 MiB cap; history 2) and the diagnostic window buffer are
  `extmem_malloc`ed once in `initAtBoot()`. A refusal is named; nothing is allocated afterwards.
- **C2b:** the registry gains `IdleOwnerChild::RevisionEditIndexGrain` (`revision_edit_index`, 2,000 µs max, 500 µs
  fine grain), pinned by `test_c2b_revision_edit_index_grain_is_declared`. Header-only gate constants mirror it (no
  ODR use of the registry array). **One call site**: the Snapshot owner turn in `runDeferredIdleMaintenance`, before
  the stopped/real-time split, so it serves both regimes. It runs `stepEditIndex(256)` when owed, admitted by
  `boundedIdleOwnerChildMayRun`, and records the whole-call time.
- **C3a:** while the transport is stopped, the selected track's active slot is imported (`importLoop`) when its frozen
  source identity (track, slot, loopId, playbackRevision, committed-pass revision) changed. Never while playing.
- **`!REVSHADOW_STATUS`** (stopped-only) prints the import identity, result and timing, the grain statistics, and a
  multiset diff of the shadow's `IndexOnly` read against production's effective view under `revision()` and
  `productionCompat()`. Production's view is the published snapshot when current, otherwise production's
  passes-based gather (`copyEffectiveCommittedEventsInRange`); the row names which.

## Results

**Boot (t7 s3, restored slot):** import `NoRecordPass`, with `playbackRevision 0` and committed-pass revision 0. A
boot-restored slot can carry only a published root and no materialised `Loop::passes`, so a passes-based import sees
nothing. **C3 must cover restored slots** (import from the published root, or from the persisted pass records). As
built, the shadow cannot mirror a restored loop.

**Owner chain** (20-bar Record + 12 overdubs + internal-clock hold, slot 4 cleared first; three runs, record 1 /
overdubs 12 / cap 13 each):

| Reading | Run 1 | Run 3 (final image) |
|---|---|---|
| Import result | Ok | Ok |
| Capture runs / rows / unpaired NoteOffs | 13 / 1,044 / 0 | 13 / 1,042 / 0 |
| **Import time (one stopped-lane unit)** | **156.8 ms** | **136.0 ms** |
| Production snapshot current after stop | yes | **no** (`pending_publish=1`, still after ~1 min stopped) |
| Production view compared | snapshot, 1,044 rows | passes gather, 1,042 rows |
| **Shadow `revision()` vs production** | — (diff tooling added later) | **1,042 / 1,042, 0 production-only, 0 shadow-only** |
| Shadow `productionCompat()` vs production | 1,020 vs 1,044 | 1,022 vs 1,042: **20 production-only, all ch1 NoteOffs** |
| Edit-index grains | 0 (nothing owed) | 0 (nothing owed) |

(Run 2 read production while its snapshot was not current and the passes fallback did not exist yet, so it compared
nothing. It is kept only for the import timing, 161.8 ms.)

## Findings

1. **Exact device parity for the revision view.** On the device, under the owner chain, the shadow manifest read the
   real-time way equals production's passes-based effective content row for row (1,042 / 1,042).
2. **Production's cross-pass collapse is path-dependent, not a production semantic.** The passes gather never
   collapses. The incrementally published snapshot of run 1 kept all 1,044 rows. Only the host harness's full rebuild
   (`requestCommittedPassesRebuild`) collapsed identical NoteOffs. So `productionCompat()`'s collapse reproduces one
   publication path, not "production". C4 must name the production surface it compares against, and DEC-245's
   "keep both" is what production's own steady surfaces already do on the device.
3. **The shadow import is a 136–162 ms blocking unit** in the stopped lane. The owner turn admitted it because the
   stopped regime has no budget. C3b's step-by-step import against a frozen source identity is required before anything
   imports under transport, and a stopped-lane bound should cover it too.
4. **Restored slots are invisible to a passes-based import** (finding above).
5. **Production's snapshot stayed un-current** (`pending_publish=1`) for over a minute after the chain while stopped
   in run 3. That is a production observation, relevant to the overdub-audibility investigation; not investigated
   here.
6. **C2b on the device is unexercised:** the owner chain carries no hides, so no index was owed. The whole-call grain
   time on the device still needs a hide-carrying session (overlap companions or NOTE_EDIT).

## Next

- C3b: a step-by-step import against a frozen source identity, with up-front capacity, finishing honestly or
  cancelling; and a source for restored slots.
- A hide-carrying device session to measure `RevisionEditIndexGrain` on the device.
- C4: name the production surface (snapshot vs passes) per comparison.
