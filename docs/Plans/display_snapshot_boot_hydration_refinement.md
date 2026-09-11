# Display snapshot-authoritative boot hydration

**Status:** Host shipped. Device hydration gate open.
**Host:** `teensy41-capture-serial` RAM1 locals **6848**.
**Kind:** refinement
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41)
**Parent:** [`display_visual_cache_snapshot_fill_refinement.md`](display_visual_cache_snapshot_fill_refinement.md)
**Does not start:** OpenSpec `3.5`/`3.6` checkoff, interval reservation, allocator limits, content discard.

---

## Destination

Three states stay separate:

- SD slot restoration is complete (`StorageManager::bootInteractiveReady()`).
- The focus loop has a current published `LoopSnapshot`, or it has no committed content.
- `visualCache` has copied snapshot membership.

Title exit uses restore completion only (`bootInteractiveReady()`). Automatic loaded snapshot publication is off after restore/adopt (`023222` reboot on loaded `start`). After `boot_commit`, VisualCache Stage 2.1 fallback fills from passes until a current root exists. Explicit selected-slot loaded hydration is a later slice. See [`loaded_hydration_stability_bugfix.md`](loaded_hydration_stability_bugfix.md).

---

## 012655 classification

[`012655`](../../captures/session_20260908_012655.log) proves:

- transport was STOPPED after title exit;
- PSRAM snapshot allocation was in progress (`snapshot_alloc` `nodes=2816`, `bytes=360000`);
- the log contains no `CrashReport`.

It does **not** prove heap exhaustion, stack overflow, watchdog starvation, or allocator failure. No allocator limit or content discard is authorized from that log.

[`015331`](../../captures/session_20260908_015331.log) classified the title-boot reset:

- `boot_reset,crash=0` on three reconnects — no `CrashReport`;
- heap stayed at 315392 bytes through the last unit — not heap exhaustion;
- no `snapshot_slab_fail`;
- last units were loaded `start`/`step` at `nodes=1738` / 9 slabs;
- no `boot_commit`;
- `selectStoppedSnapshotTrack` interleaved `q=4` and `q=1` (neighbor 66-bar publication on the title screen).

Title Snapshot turns stay off the title screen. After `boot_commit`, loaded
hydration prefers the selected track and does not start a neighbor while
another loaded construction is in flight.

[`021247`](../../captures/session_20260908_021247.log) left the title at
8.17 s (`boot_commit=242310`), then interleaved `q=4/1/0/1` snapshot units.
Two publishes landed (`n=756`, `n=1315`). The viewed 66-bar `VCACHE` stayed
`notes=0`. The log ends at 21.1 s / `n=2701` with no `CAPTURE_RECONNECT` and
no `snapshot_slab_fail`.

[`020111`](../../captures/session_20260908_020111.log) never left the title screen:
focus T0/S5 loaded publication was still `q=4` at 112 s (`nodes=35976`,
`slabs=1719`). Title exit is restore completion only. Snapshot hydration
starts after `boot_commit`.

Capture-only lines now classify the next reset:

| Line | Distinguishes |
|---|---|
| `DIAG,boot_reset,crash=` | Previous boot left a `CrashReport` (stack / fault) or not |
| `DIAG,snapshot_unit` | Last completed unit kind, loaded phase, pending counts, heap before/after, slab counters |
| `DIAG,snapshot_slab_fail` | `extmem_malloc` / slab allocate failure immediately before abort |
| `DIAG,snapshot_alloc` | Existing node/byte/slab climb |

`psram=` on `snapshot_unit` is `DebugSessionCapture::kUnsampledHeapBytes`. `MemoryMonitor::getExternalMemoryPoolFreeBytes()` walks the smalloc header chain (593 ms in [`141815`](../../captures/session_20260812_141815.log)) and is not sampled per unit.

---

## Runtime scheduling

- Reuse `main.cpp::loop()` and `MidiServiceDrain::DeferredIdleOwner`. No new scheduler.
- `queueLoadedSnapshotPublish()` stays queue-only.
- `PendingSnapshotPublishBox::loadedHydrationPhase` distinguishes loaded publication from session-exit.
- Automatic loaded construction is not admitted after restore/adopt (`023222`).
- After `boot_commit`, VisualCache Stage 2.1 fallback may fill from passes until a current root exists. Title drain still withholds that fallback.
- Session-exit Snapshot idle is unchanged. `queueLoadedSnapshotPublish()` stays for an explicit later trigger.
- Visual-cache snapshot-membership copy runs only after `publishedSnapshotIsCurrent()`.

---

## Architecture checkpoint

1. Ownership change? **NO** — same `Loop` publication owner and same boot title owner in `main.cpp`.
2. State transition change? **YES** — title exits on restore completion, not visual-cache or snapshot current. Authorized by this plan. [`020111`](../../captures/session_20260908_020111.log) stayed on title after `load_frame` went idle at 13.8 s while focus T0/S5 climbed to 36k nodes.
3. Unbounded MIDI-interval owner? **NO** — existing idle Snapshot unit caps. No PSRAM pool walk.

---

## Validation

- Native: loaded queue/restore, empty-loop readiness, one-unit loaded start, current-snapshot-only visual-cache copy, session-exit parent lookup preserved.
- Firmware: `teensy41-capture-serial` RAM1, then `pio test -e native`.
- Device: title exits without pass-based visual fallback on the title screen; after `boot_commit` STOPPED slots can show Stage 2.1 fallback notes; no automatic loaded `snapshot_unit` `ld=1` during post-boot idle (`023222`).
