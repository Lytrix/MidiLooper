# RC4e — Rolling window during PLAYING+overdub

**Status:** Fix shipped — awaiting verification capture  
**Parent:** [`long_overdub_display_freeze_bugfix.md`](long_overdub_display_freeze_bugfix.md) § RC4

## Problem

Detailed rolling window shows full content during recording but committed notes stuck on initial gather span (~18 bars) during PLAYING+overdub until transport stops.

## Fix

[`TrackDeferredMaintenance`](../../src/Track/TrackDeferredMaintenance.cpp): include `isOverdubbing()` in deferred visual-cache idle maintenance with the same **bounded neighborhood** as PLAYING (`kMaxDetailedWindowBars + 4` bars). Overdub committed path can use progressively built `visualCache` instead of stale window gather only.

## DISP verification

Do **not** require stable `wNotes` while `wStart` advances.

Required: composed notes **intersect** current paint window — not permanently tied to bars 0–18.

Failure pattern: `wStart` advances but note tick spans remain in initial gather region.

## Acceptance

| Criterion | Verify |
|-----------|--------|
| Window follows playhead | Notes beyond ~18-bar gather visible during transport |
| Recording unchanged | capturePreview authoritative |
| Bounded maintenance | No unbounded full-loop rebuild during long overdub |
| `DFRAME` stable | No sustained stalls from idle slices |
