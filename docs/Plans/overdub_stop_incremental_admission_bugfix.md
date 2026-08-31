# Overdub stop incremental admission and publication throughput

**Status:** Firmware in tree. Stage 2 device PASS [`152513`](../../captures/session_20260831_152513.log). Stage 3 drain device PASS [`153113`](../../captures/session_20260831_153113.log). Do not check off OpenSpec `3.5` / `3.6`.  
**Date:** 2026-08-31  
**Kind:** bugfix  
**Parent:** [`playback_merged_compatibility_rebuild_clock_interval_bugfix.md`](playback_merged_compatibility_rebuild_clock_interval_bugfix.md)  
**Decision:** [`DEC-049`](../DECISION_LOG.md#dec-049--overdub-stop-admits-the-seal-and-accelerates-snapshot-publication)  
**Evidence:** [`143203`](../../captures/session_20260831_143203.log) T4 = 11 bars / 8448 ticks. Wrap incremental at `rev=2`; every overdub stop paid a full-loop rebuild at `rev=4/6/8/10`.

**Does not start:** The 23.850 ms `handleMidiInput` stop-path `seal_fx_publish_prepared_overdub`. Bounding ineligible fallback rebuilds. `T_RT_reserve`. OpenSpec `3.5` / `3.6` checkoff.

---

## Invariant (one sentence)

Stopping overdubbing resets playback traversal and does not invalidate a still-valid full-loop merged representation solely because the overdub state changed. While a publication backlog is present, Snapshot idle is a fixed 2-of-6 admission ratio.

---

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — gather owner stays `ensurePlaybackMergedMidiEventsBuilt`; publication owner stays `stepPendingSnapshotPublish` |
| Musical stop semantics? | NO — sealed pass and companions still become audible |
| State transition change? | NO — OVERDUBBING → PLAYING is unchanged |
| New owner / scheduler? | NO |
| Unbounded interval owner? | NO — Stage 2 removes work from the interval path. Stage 3 changes admission frequency of an already structurally bounded unit |

Reuse: YES — extend `Track::stopOverdubbing` and `MidiServiceDrain::selectDeferredIdleOwner`.

---

## `resetPlaybackState` classification (code-level gate)

Complete body of `Track::resetPlaybackState` at implementation time:

```text
KEEP (reanchorPlaybackCycle):
    nextEventIndex = 0
    lastTickInLoop = phase (or 0 if loopLengthTicks == 0)
    projectionCycleStartTick from currentTick and phase

DO NOT KEEP (representation invalidation):
    invalidatePlaybackMergedMidiEvents(true)

ALREADY SEPARATE (stopOverdubbing, before reanchor):
    silenceTrackMidiOutput()
    playbackRuntime.clearAllLedgers()
    pendingNotes.clear()
```

`Track::reanchorPlaybackCycle` is called from `Track::stopOverdubbing` only.
`stopOverdubbingToStopped`, `handleNoteEditFold`, and `stopRecording` stay on
`resetPlaybackState`.

---

## Stages

1. **Path label** — `Loop::tryGatherPublishedSnapshotEventsInWindow` reports
   `Snapshot` / `Overlay` / `None`. CAP `playback_build` uses that outcome:
   `snapshot`, `overlay`, `compatibility`.
2. **Stop admits the receipt** — `stopOverdubbing` reanchors; next
   `playCommittedLoopMidi` may `tryAdmitLastOverdubSealIntoMergedEvents`.
3. **Publication throughput** — `selectDeferredIdleOwner` inserts one extra
   Snapshot turn after each complete 5-owner cycle while
   `publishedSnapshotRevision() < playbackRevision` and
   `hasPendingSnapshotPublish()`. Snapshot 2/6; every other owner 1/6.

Stage 3 attaches to
[`snapshot_publication_timing_bugfix.md`](snapshot_publication_timing_bugfix.md)
as admission throughput. Earlier stages of that plan covered per-admission cost.

---

## Device gate

Loop of 16 bars or fewer, record, at least one wrap, several overdub stops.

- Eligible stop → next playback admission is `playback_build,incremental`.
- After the final stop, Snapshot follows 2-of-6 and `root_rev` meets
  `playback_rev` if per-unit progress remains healthy.
- Remaining full-loop rebuilds log `overlay` or `snapshot` according to the
  branch taken.
- No new clock-path owner above 8.82 ms on the playback tick after the stop.

### Recapture [`152513`](../../captures/session_20260831_152513.log)

T4 = 16 bars / 12288 ticks. One wrap, five overdub stops.

| Event | Seal / stop | Next T4 `playback_build` | Same-tick `playback_order` |
|---|---|---|---|
| First full-loop gather | record stop | `compatibility,3692,0,12288,rev=1` | — |
| Wrap | `wrap,total=84249` @ 83.706 s | `incremental,0,0,12288,rev=3` | 3002 µs / 811 events |
| Stop 1 | @ 97.540 s tick 18328 | `incremental,0,0,12288,rev=5` | 2554 µs / 895 events |
| Stop 2 | @ 105.345 s tick 21328 | `incremental,0,0,12288,rev=7` | 2642 µs / 949 events |
| Stop 3 | @ 107.757 s tick 22192 | `incremental,0,0,12288,rev=8` | 2812 µs / 961 events |
| Stop 4 | @ 110.158 s tick 23112 | `incremental,0,0,12288,rev=9` | 2840 µs / 977 events |
| Stop 5 | @ 117.255 s tick 25840 | `incremental,0,0,12288,rev=11` | 3112 µs / 998 events |

No T4 full-loop `playback_build` after the first record-stop gather. All other `compatibility` rows are T6 2-bar windows (`winLen=1536`). No `overlay` or `snapshot` path rows: no published root exists (`root=0x0` on every `snapshot_idle`).

After the last stop the file holds 6.6 s / 1506 `snapshot_idle` parent rows at `pending=1` / `root_rev=0` / `playback_rev=11` / `queued=4` / `applied=1` / `construction=5`. `publish=0` on every child. Stage 3 drain is not observed in that file.

### Recapture [`153113`](../../captures/session_20260831_153113.log)

T4 = 11 bars / 8448 ticks. One wrap, three overdub stops. Stage 2 holds: wrap `rev=3` and stops `rev=5/6/7` are `playback_build,incremental`. Same-tick `playback_order` is 1.87–2.14 ms.

Stage 3 drain: last stop @ 501.854 s (`playback_rev=7`, `queued=2`, `applied=12`, `construction=7`, `root=0x0`). 16.197 s later, `pending=0` / `root=0x703fadf0` / `root_rev=7` / `playback_rev=7` / `publish=9`. Queued went 2 → 1 → 0; applied 12 → 13 → 14. Next T4 full-loop rebuild is `playback_build,snapshot,2947,0,8448,rev=7`. No `overlay` rows. CAP `snapshot_idle` has no owner field, so the 2-of-6 turn ratio is not countable in this file.
