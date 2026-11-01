# Execution packet — phase 2 (first failed predicate, route reject)

From `docs/Templates/EXECUTION_PACKET.md`. Tasks 2.1–2.3. Report-only: no route, admission, producer or emission change.

## Identity and scope

- **Read (task 2.1).** For a committed long loop in PLAYING, the tick resolves its source in
  `Track::resolveQualificationPlaybackSourceForTurn`:
  1. the NOTE_EDIT preview, the live overdub overlay, and the frozen Record reader (specialised readers, each
     returns early when it serves);
  2. a cold one-bar layered frame (one-bar loops only);
  3. `resolveFixedPlaybackSourceForTurn`: the full-loop bridge, then the published full-loop view (these serve
     short loops; a 20-bar loop has neither);
  4. `Track::resolveRollingPlaybackWindow`: the windowed lane's consumer side.
  `Track::playCommittedLoopMidi` then tries the paged delivery and finally selects the merged fallback
  (`selectMergedPlaybackFallbackForTick`).
- **Enum.** `PlaybackTickContext::RouteReject`. One value per predicate, in decision order. Two groups: the reader's
  eligibility (`InactiveSlot`, `NoteEditActive`, `OverdubNoLiveWindow`, `NotPlaying`), then the rolling resolver's own
  exits (`NoRuntime` … `AdoptRefused`). There is no generic "not usable" value. `NotEvaluated` / `Served` /
  `RollingDisabled` are bookkeeping values, not predicates.
- **Blocked frames.** A rejecting resolver exit blocks the frame (`rejectRollingFrame`), and every later tick returns
  at the top as blocked. The rejecting predicate is therefore latched in the frame (`RollingFrame::blockedBy`, which
  uses existing padding; the `sizeof(LoopPlaybackRuntime)` pin is unchanged). It is reported as
  `FrameBlocked(cause)`. A frame the producer blocked carries cause 0, and the producer's last exit token
  (`producerExit`, latched in `reportPlaybackRollingGateRow` on every capture image) names the producer side.
- **Cost (task 2.2).**
  - A served tick writes two bytes (the turn's reset) and computes nothing.
  - A failing predicate writes one byte; the naming re-reads the terms of an already-failed condition (flash, reject
    path only).
  - The reason is filed under the slot only in `selectMergedPlaybackFallbackForTick` (merged selected).
  - The row is emitted on change from the main loop (`TrackManager::reportPlaybackRouteRejects`, after
    `serviceOwedTickPlaybackFallbacks`), at most one per slot per 50 ms, with `chg` carrying the number of changes.
  - The interrupt tick never emits.
- **Row.** `DIAG,route_reject,t=,s=,why=,cause=,prev=,prev_n=,since=,chg=,len=,prod=`. Tier-A, direct-first with ring
  fallback, flash format.
- **Non-goals.** Any repair; the capability fixture (phase 3).
- **Must preserve.** RAM1 variables 96,192; ITCM padding ≥ 840 (the ITCM hot path is untouched); existing rows.

## Oracle (fixed before the edit)

| Case (suite) | Expected |
|---|---|
| serving tick (`test_runtime_working_set_ownership`) | no reason written |
| clock epoch re-stamped under a serving frame | `ClockEpochChanged`; frame `blockedBy` = it |
| next tick | `FrameBlocked`, cause `ClockEpochChanged` |
| served span ends | `ServingExpired`, then `FrameIdle` |
| producer opens the successor; tick before its deadline | `WaitingForHandoff` |
| non-active long slot | `InactiveSlot` |
| merged filing + main-loop row | one row per change, nothing when unchanged, spacing held, `prev`/`prev_n` carried, bridge turn not filed |
| real hot path, lane compiled out (`test_runtime_profiler_sites`) | `RollingDisabled`, both clocks, one change |
| row (`test_capture_ring`) | format at field maxima, Tier-A, < 192 B, ring fallback, `prod=-` |

Values the harness cannot reach without the device's interleavings are device-only:
- the remaining gate and clock terms;
- the serving-view staleness splits;
- the cold-ready paths;
- `HandoffMissed` / `CandidateNotReady` / `HandoffPhaseMismatch` / `CandidateRejected` / `AdoptRefused`.

Task 2.3 names whichever ones occur.

## Completion record (2026-11-01)

- **Native.**
  - Focused: `test_capture_ring` + `test_runtime_profiler_sites` + `test_runtime_working_set_ownership`, 315/315.
  - Full native: see the phase-2 entry in `CURRENT_WORK.md`.
- **Build / RAM1.** Variables 96,192 on all three envs.
  - Default and no-grain: code 391,944, padding 1,272. Unchanged.
  - Profile: code 392,344, padding 872. Unchanged.
- **Markers.** `check_marker_contract.py 'DIAG,route_reject,'` OK (18/31 bytes); `--audit` exits 0.
- **Decoder.** `scripts/parse_tick_routes.py` adds the reason timeline and the merged ticks per closed reason. Tests
  6/6, including one that pins the decoder's names to the firmware enum.
- **Image.** Default `1799e442719f1808879aa41679c4dd15314f91a4b1fffa67d35ef63394a72aae`.
- **Device (2.3).** Pending.
