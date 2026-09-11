# Loaded hydration stability

**Status:** Device PASS [`024731`](../../captures/session_20260908_024731.log).
**Host:** `teensy41-capture-serial` RAM1 locals **6848**.
**Kind:** bugfix
**Parent:** [`display_snapshot_boot_hydration_refinement.md`](display_snapshot_boot_hydration_refinement.md)
**Does not start:** explicit selected-slot loaded hydration, OpenSpec `3.5`/`3.6`, allocator limits.

---

## Root-cause anchor

[`023222`](../../captures/session_20260908_023222.log) reboots immediately after
a loaded snapshot `start` unit (`ph=2, ld=1, q=0, a=2`) and before the next
`step`/`finish`. The reboot is after `boot_commit`. The log has no
`snapshot_slab_fail` and no `CrashReport`.

## Destination

- Restore/adopt no longer auto-queues loaded snapshot publication.
- Post-boot piano-roll notes use Stage 2.1 pass fallback until a current root
  exists.
- Record/overdub session-exit snapshot publication is unchanged.
- `queueLoadedSnapshotPublish()` stays for an explicit later selected-slot
  trigger.

## Architecture checkpoint

1. Ownership change? **NO** — same `Loop` publication owner and same Snapshot
   idle owner.
2. State transition change? **YES** — loaded publication no longer auto-starts
   after restore/adopt. Authorized by this plan.
3. Unbounded MIDI-interval owner? **NO** — this slice removes idle work.

## Validation

- Native: adopt/restore do not queue; explicit `queueLoadedSnapshotPublish`
  still constructs; loaded visual-cache idle uses pass fallback; session-exit
  stays `fromLoadedPasses=false`; `allowLoadedSnapshotHydration` is false.
- Firmware: `teensy41-capture-serial` RAM1 locals floor.
- Device PASS [`024731`](../../captures/session_20260908_024731.log):
  `boot_commit` at 7.28 s; no `CAPTURE_RECONNECT`; no `snapshot_slab_fail`;
  zero `ld=1` units; STOPPED `DISP` nonzero (32 / 352 / 1429). Post-boot
  `VCACHE` stays `root_rev,0` / `idle_src,Fallback`. First `snapshot_unit`
  is session-exit `ld=0` at 100.5 s during RECORDING.
