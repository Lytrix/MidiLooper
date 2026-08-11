# Long overdub post-stop display handoff

**Status:** Planned — RC2  
**Evidence:** [`session_20260811_013056.log`](../../captures/session_20260811_013056.log)  
**Parent:** [`long_overdub_display_freeze_bugfix.md`](long_overdub_display_freeze_bugfix.md)

## Problem

After overdub stop, the first display snapshot contained 2883 notes:

```text
1872 visualCache notes + 1011 capturePreview notes = 2883 frame notes
```

`refreshViewportAfterRecordStop()` preserves the live composed vector after its cache boundaries
are cleared. That vector can include playhead-mutated tails and temporary wrap-head rows.
`resolveDisplayNotesCommitted()` may return it while deferred save work is active.

## Invariant

The first post-stop long-loop frame must come from bounded canonical committed data. Temporary live
tail geometry must not become committed display authority.

## Architecture checkpoint

- **Owner:** `DisplayManager`.
- **Ownership change:** NO.
- **State-transition change:** NO.
- **Stop path:** No synchronous rebuild in `Track` or `Loop` stop/commit functions.

## Scope

1. Prioritize existing long-loop window reconstruction before stale live capture fallback.
2. Retain stale data only when canonical cache/window data cannot be produced.
3. Ensure temporary rows beyond the live composition base are not preserved across the handoff.
4. Verify the first post-stop capture snapshot is window-bounded and contains no inherited
   tick-zero playhead tail.

## Out of scope

Capture-preview lifecycle parity (RC1), USB Host button input (RC3), and Stage 5 storage pressure.
