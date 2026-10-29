## Why

After reboot, undo removes an overdub from playback but leaves its notes painted; redo can silence the slot. The previous retained-source repair was reverted after a restored-slot playback regression, so this needs saved-slot production-path tests, not another reapplication.

## What Changes

- Make global undo/redo repaint the resulting committed content without waiting for a replacement root when a current, complete display source is already available.
- Require restored-slot playback to survive repeated undo/redo without losing the saved MIDI payloads.
- Establish actual deserialize/apply/undo/display/playback coverage, including rootless bodies, deferred publication, empty results and Record-path safety.
- Separate the confirmed display boundary from the unresolved redo ownership boundary. No production redo repair is authorized until its loaded-slot oracle and contract are established.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `timeline-passes`: add explicit restored global undo/redo display and playback consistency requirements, including non-destructive source turnover.

## Impact

Existing owners only: `TrackUndo`, `Loop` display qualification, `DisplayManager` draw-source handoff and `Track` playback-source retention. Primary paths are `src/TrackUndo.cpp`, `src/DisplayManager/DisplayNoteWindowGather.cpp`, `src/Loop/LoopVisualCache.cpp`, `src/Track/TrackPlaybackWindowBuild.cpp` and saved-slot fixtures using `StorageLoopIo`.

Brownfield references: [timeline passes](../../specs/timeline-passes/spec.md), [delivery tracking](../../../docs/DELIVERABLE_TRACKING.md), [current scope](../../../docs/Runtime/CURRENT_WORK.md#now-implementing), [display architecture](../../../docs/Authority/Architecture/Display.md). Investigation and contracts live in [design.md](design.md).

## Evidence and open questions

- **VERIFIED:** `captures/session_20261029_143903.log`, reboot segment: undo at 12.627 s; cache clean with 171 notes at 12.660 s, but draw receipt retains 204 with `why=hold_gap`; playback paint receipts still show 204 against cache 171 at 17.880–22.922 s.
- **VERIFIED:** current Git HEAD `7c3f69ff` reverted DEC-205; the newest PROJECT_STATE entry predates its reapplication/second revert. Git and the final DEC-205 entry establish the current code boundary.
- **ASSUMPTION:** the observed redo silence shares the two-retained-role capacity refusal previously reproduced natively.
- **UNKNOWN:** the exact loaded-slot reference/alias invalidation sequence behind the rejected repair. The supplied reboot segment has undo but no redo; it is not a fresh redo reproduction.
- Product decisions: none open. Implementation decisions for redo remain blocked, not delegated to Apply.

## Non-goals

No SD operation, firmware upload, storage-format migration, undo-history redesign, added retained-source role, new queue/lease, playback-length-policy migration, boot-wide hydration, or reapplication of DEC-205's admission-path release. No claim of a shipped fix from planning artifacts.
