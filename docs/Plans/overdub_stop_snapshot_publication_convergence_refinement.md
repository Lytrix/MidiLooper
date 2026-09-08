# Overdub-stop snapshot publication convergence

**Status:** Device PASS [`145633`](../../captures/session_20260908_145633.log).  
**OpenSpec:** `immutable-loop-snapshot-passes` task `4.5a`  
**Parent:** Bug B after 4.4c. 4.4c stays closed.

## Contract

```text
OVERDUBBING composed liveDisplayNotes
        │
        ▼
STOP  commit + queueSessionExitSnapshot
        │
        ├── first post-stop frame = HOLD
        │     no pairrows,at=passes
        │
        ├── Snapshot idle N=2, extra turns while
        │     postOverdubPlayingMidiDrain && sessionExitQueued
        │
        ├── storePublishedSnapshot
        │
        └── first NEW committed paint = current LoopSnapshot
```

Do not construct on the stop path. Do not use Overlay as a display source.
Do not add `requestSnapshotConvergence` / `snapshotPublicationUrgent_`.

## Evidence

[`141434`](../../captures/session_20260908_141434.log): `refreshViewportAfterOverdubStop` called
`resolveDisplayNotes(passes)` twice → `pairrows` 154/177 and
`ODUB,stop,display` 42428 / 54947. Successor published at +1.233 s
(`pending=0` `root_rev=5`).

## Owners

| Role | Identifier |
|---|---|
| Hold | `DisplayManager::refreshViewportAfterOverdubStop` |
| Paint gate | `shouldHoldOverdubStopHandoffUntilCurrentSnapshot` |
| Enqueue | `Loop::queueSessionExitSnapshot` |
| Publish | `Loop::stepPendingSnapshotPublish` / `storePublishedSnapshot` |
| Frequency | `selectDeferredIdleOwner(..., sessionExitDrainPending)` |

## Stages

1. Remove both stop-path `resolveDisplayNotes(passes)` calls. CAP `why=stop,from=hold`. `ODUB,stop,display` outcome `hold`.
2. Current snapshot paints via `resolveDisplayNotes(*snapshot)`. Session-exit pending holds compose. No last-resort passes/Overlay while that pending flag is set.
3. Extra Snapshot turn after every non-Snapshot owner while drain + `sessionExitQueued`. Skip VisualCache reconstruct of that loop until the successor is current. `N=2` unchanged.
4. `DIAG,snapshot_idle,post_stop,old_root,new_root,play_rev` on session-exit publish.
5. Native predicates + `test_midi_service_drain`. Device: stop stack has no `pairrows`; later `root_rev == playback_rev` and first new paint is Snapshot.

Accepted trade: held frame may still show last OVERDUBBING occupy geometry (`225234`) until the successor is current.

## Device PASS — [`145633`](../../captures/session_20260908_145633.log)

Two overdub stops. Neither stop stack has `pairrows`.

| Stop | Hold | `ODUB,stop,display` | Publish | Horizon |
|---|---|---|---|---|
| 1 | `from=hold,notes=156,root_rev=3,play_rev=7` | child **0**, outcome `hold` (9.8 ms from enter; seal already ran) | `post_stop,old_root=3,new_root=7,play_rev=7` then `pending=0` `root_rev=7` | 1.594 s / 584 Snapshot idle rows / 0 `VCACHE,idle_src` |
| 2 | `from=hold,notes=219,root_rev=9,play_rev=13` | child **0**, outcome `hold` | `post_stop,old_root=9,new_root=13,play_rev=13` then `pending=0` `root_rev=13` | 1.563 s / 572 Snapshot idle rows / 0 `VCACHE,idle_src` |

`141434` display 42–55 ms was `pairrows,at=passes`. Here display child is 0. The ~1.6 s to publish is session-exit construction at `N=2` (wrap journals + queued pass; `fragment` / `applied` advance), not VisualCache cadence. At 120 BPM that is under one bar.

First tagged new committed source after stop 1: next overdub `why=open,from=snapshot` `root_rev=cache_rev=play_rev=7`. Stop 2 successor is current before transport STOPPED; held 219-note frame stays until then. No last-resort `paint_src` / `pairrows` after either stop.
