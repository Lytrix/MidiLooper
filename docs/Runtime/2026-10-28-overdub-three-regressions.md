# Three open regressions — 2026-10-28 owner capture

Investigation only: `captures/session_20261028_220707.log`. Branch verified as
`feature/unified-rolling-playback-path`. Rows are buffered/out of file order;
use embedded timestamps. Capture starts mid-session; image provenance is unknown.
No firmware/test/OpenSpec/decision change, build, upload, serial command, reboot,
SD action, commit or push. Tests inspected, not rerun.

## Scope traced

Repeated overdub Stop failure, missing recorded notes beyond bar 16 after wrap,
and inaudible overdubs. Applicable existing work: `display-freeze-under-overdub-chain`
and `deferred-canonical-convergence`. No redesign authorized by this investigation.

## Current execution path

Gestures reach Track stop admission; `processDeferredIdleMaintenance` drives
`processDeferredOverdubStopGrain`. In `CompanionHistory`, readiness is queried,
then `preparedCompanionHistoryCanStep`; false attempts begin, true calls
`TrackPreparedStopCommit::stepPreparation`. See `src/Track/TrackOverdubLifecycle.cpp`
and `src/Loop/LoopPreparedCompanionHistory.cpp`.

## Ownership/lifetime map

Track owns stop jobs and pending-pass receipts. Loop owns shared prepared companion
history. A begun preparation binds frozen companions/staging/canonical inputs and
the job's pass; `preparedCompanionHistorySourceCurrent` pins revision, storage,
history count, next pass id, snapshot companion generation and input identities.
Begin requires Prewarmed: a stale begun preparation cannot simply begin again.

## Deferred-work map

Pending stops return before ordinary Snapshot maintenance, except the explicit
RecordPublished exception. Pending-stop VisualCache turns can still advance the
bounded display continuation. A stuck overdub head can block root publication
while display DMA and bounded display production continue.

## State-transition map

VERIFIED: phase 7 is CompanionHistory (`include/TrackPlaybackRuntime.h`). Its
frozen-generation-9 grain appears at 589.373 s (line 21720), still phase 7 at
621.374 s (line 23398), with no intervening head phase transition recorded.
The final summary reports 50,121 observations: ~32 s in this phase, not merely
the ~19 s since the summary's latest productive timestamp (602.566 s).
Stops at 617.954/619.951 s are retained behind interval 53 (lines 2370/2425).
Transport Stop retargets at 621.343 s; at 621.772 s the head escapes raw-only.
Queued jobs follow via the no-canonical-base raw route. Capture-pass census rises
18 → 25 after the drain; edit-pass census remains 30.

`stopPreparationIsBounded` applies the companion fallback only after retargeting,
not while destined for Playing. This explains escape eligibility, not the original
preparation refusal.

## Publication/persistence/undo and consumer map

- Display: `syncDetailedPaintWindow` separates viewport from retained content;
  `collectQualifiedCommittedDisplayNotes` selects bounded/prepared/cache content.
  At 607.052 s it serves `32,12288`; at 621.277 s `5488,12288`, both `why=bound`.
  This reaches late bars of the 18,432-tick/24-bar loop. Coverage and note counts
  do not prove original late-bar NoteIds are present. User's paint defect remains open.
- Playback: `playCommittedLoopMidi` can select frozen Record windows through
  `adoptOrServiceOnwardRecordWindow`, bypassing merged fallback. After the 590.98 s
  wrap, captured `playback_read` rows continue with Capture origin 2/one-bar coverage.
  Captured channel-8 output NoteOns during 591–621 s contain pitches 70/74/78/82,
  but none of the distinctive incoming overdub pitches 39/45/48/51/59. Supports a
  missing committed-overdub handoff; shedding means this is not a complete inventory.
- Raw retirement can publish the pass without its companion representation.
  Any repair must preserve undo identities and persistence publication; neither
  was changed or independently validated here.

## Resource/timing consequences

No MEM_DENIED rows; late heap census steady at 86,016 B. DMA reaches 9,498 frames
with zero timeouts at 630.643 s. Neither proves correct content or excludes all
resource faults. No new RAM1/PSRAM/LoopPool/timing claim. Bound authority remains
`docs/Authority/Architecture/ExecutionBounds.md`; no owner class changed.

## Existing validation

`exerciseDisplayOverdubChain` starts with a current Record root, drains pending
stops before each Start (up to 4,000 owner turns), and uses ticks
`96 + overdub*96` for the 24-bar case. Its expected Record set is first-16-bars
only. It does not exercise this unrooted continuous chain/full wrap/scroll or
exact Record+overdub MIDI inventory. Separate late-bar controls give 2,048 producer
turns before asserting content. See `test/test_live_capture_display_boundary/`.

## Hidden dependencies

One stuck companion preparation excludes normal root publication. A qualified
frozen Record reader may remain valid yet lack newer overdubs. Display has its
own coverage/content qualification. Throughput/DMA observations cannot replace
independent content oracles.

## REPRODUCED (host, 2026-10-28) — the refusing predicate is named

`test_companion_history_survives_overlap_rows_wrap_and_a_backlogged_stop`
(`COMPANION_REVISION_RED=1 pio test -e native -f test_continuous_capture`) reproduces the device shape on
the production path: a long record note every overdub overlaps, a begun companion preparation, a successor
start, a wrap commit and a Stop retained behind the head. Result:

```
phase=7 inFlight=1 reach=30 grains=20000 edit=0 cap=2 credit=2
revAtReach=1 revAfterWrap=2 refusal=playback revision moved atReach=preparation step made no progress
[FAILED] a companion preparation begun before the successor starts must still retire
```

**Refusing predicate: `preparedCompanionHistorySourceCurrent()` → `work.revision != playbackRevision`
(`CompanionPreparationRefusal::SourceRevision`).** The horizon pins `playbackRevision` at `begin`; a commit
that moves it while the preparation is in flight makes the preparation stale, `CanStep` goes false, `begin`
refuses because the phase is no longer `Prewarmed`, and no bound covers a non-retargeted phase 7.

Control `test_companion_history_retires_when_no_commit_moves_the_revision` (identical overlap, begun
preparation and successor start, no mid-phase commit) retires: the revision move is the trigger, not the
overlap rows or the successor. `credit=2` falsifies grain-credit exhaustion for this shape; `edit=0` shows
the companion rows were never published, which is the mechanism behind the two content symptoms
(display resolves overdub transforms from the companion representation; playback keeps the older source).

Device status: the **reason** is now emitted once per job (`Deferred stop stalled: <name>`), so one run on a
diagnostic image confirms which commit moved the revision there (wrap commit vs companion publication vs
note-edit commit). That is an assumption until then.

## VERIFIED

Stop failure is actively serviced and phase-local, not missing gestures or heap
admission. The refusing predicate is `SourceRevision` (pinned `playbackRevision` moved) in the reproduced
shape, and the device's 50,121 no-progress grains are consistent with it. Current combined native coverage
misses the owner's timeline because it never begins the preparation before starting the successor or
committing a wrap.

## ASSUMPTIONS

On device the revision move is a wrap commit, a companion publication or a note-edit commit; the host proof
covers the wrap-commit source. Frozen Record playback selection is a playback suspect, not full
emitted-content proof.

## UNKNOWNS

Which commit moved the revision on device (one diagnostic run names it); missing late-bar Record NoteIds;
complete emitted Record+overdub inventory; installed-image identity.

## CONFLICTS

Earlier status calling the user's start/stop symptom solved is superseded by
this reproduction. No normative architecture conflict established. Repair options (re-pin under a licence,
retire-by-cause onto DEC-172's convergence, or drop the companion-half pin) are recorded in DEC-186 and
need an owner choice; no new decision or exclusion adopted yet. Existing decisions remain in force.

## G0 verdict: PASS for the stop-side predicate; STOP before patching

Evidence, not implementation approval. The refusing predicate is identified and reproducible. The repair is
an architectural choice between the three DEC-186 options, so it is not started here. Still needed for the
other two symptoms: exact visible late-bar Record NoteIds and the emitted Record+overdub inventory across a
wrap; do not silently widen fallback, drop companion work or touch device SD.

## 2026-10-28 22:54 capture — the page stops following at bar 16 by construction

`captures/session_20261028_225450.log` (DEC-186 image `c1555373…`): 12 overdubs, 12 stops, **0 retained**,
**0** `companion preparation obsolete` rows, and every phase-7 job finished in **4-5 grains** with
`finishedPhase=1`. So this run did **not** exercise the new escape (no revision moved under a begun
preparation) and neither confirms nor refutes it; it also did not wedge.

The display report is a different, deterministic mechanism. The loop is 18,432 ticks (24 bars) and the paint
window is `b=16` (12,288 ticks), so:

- `resolveCenteredWindowStart` keeps the playhead centred: `start = playhead - 6,144`;
- `resolveDetailedPaintWindow` then clamps `start <= loopLength - length = 6,144` (bar 8).

Therefore the page **stops scrolling once the playhead passes bar 16** and holds at `w=6144` while the last
8 bars play; the cursor keeps traversing the page and the page jumps back at the wrap. Measured:
`disp_window ... w=6144` first at 143.809 s, again at 190.916 s and unchanged through the transport stop.
`docs/Guides/control-surface/Display.md` states the window "auto-follows the playhead" during playback, so
this clamp is the reported symptom rather than a capture artifact: with a 16-bar window on a 24-bar loop,
8 bars of every 24 (≈16 s at 120 BPM) play with a static page.

Second, weaker observation (not reliable): after the last Stop at 160.516 s the window does not move again
until 174.976 s, and `win_frozen_readiness ... adopt_wait,e2_boundary` plus one
`build_drop,completed_unpublished` (182.933 s) appear in that interval. That capture **shed 15,069 lines and
evicted 13 Tier-A rows starting at 168.19 s** (`RING,shed` / `RING,tier_a_evicted` at 197.975 s), so absence
of rows there is not evidence. The follow-stall question needs a capture that does not overflow.

Status: page-clamp identified (host-checkable, no capture needed); follow-stall UNKNOWN pending a clean run.

## 2026-10-28 23:20 — the frame freeze is real but unattributed; the last capture cannot decide it

Owner report on `session_20261028_225450.log`: the panel freezes completely (cursor, bar readout, notes)
at bar 15 after the last overdub, and the centred follow itself is correct as designed (no window change
wanted). Re-reading that capture with its non-`#CAP` rows included changes the picture:

- **The transport never stalled.** `#DMA_XFER` advances `dma_frames` 2396 -> 2906 at 11-15 fps with
  `timeouts=0, rejections=0, active=0` across 168-207 s, i.e. through the whole reported freeze.
- **The paint receipt lane kept composing.** `DIAG,disp_turn` reports `st=fresh, pub == req` with a window
  moving 32 -> 6144 between 174.98 s and 190.90 s.
- **Every absence is non-evidence.** The capture sheds: `RING,shed,15069` plus
  `RING,tier_a_evicted,13` (first Tier-A eviction 168.19 s) and **no `RING,stats` row**, which per
  `docs/Agents/hitl_authoring_traps.md` makes "missing row" unreadable as "not emitted". `DISP,3` also
  dedupes on (note count, state, loop length) only (`DisplayNoteResolve.cpp`), so its silence while the
  count sits at 415 is expected and proves nothing.

So the freeze is real in the owner's eyes, and the log cannot name its layer: painted tick vs playhead is
exactly the fact that was never recorded. **DEC-187** adds one un-sheddable row for it (`#PAINT_BEAT`, direct
`Serial.printf_P` like `#DMA_XFER`, 1 Hz, `paint` vs `play` tick, pending phase, transport busy flag, DMA
frame count). Recipe: [2026-10-28-paint-beat-recipe.md](2026-10-28-paint-beat-recipe.md). Built and gated
(`variables 96,192`, padding 2,232 -> 1,976); **not flashed**.
