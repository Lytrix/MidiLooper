# 3.15e — execution packet (overdub-overlay slice: live overdub across the wrap and the stop handoff)

**Change:** `windowed-playback-generation` · **Task:** 3.15e (bounded dense/legacy and overdub content through the single
reader) · **Branch:** `feature/unified-rolling-playback-path` · **Base:** `901610ef`.
**Task state on entry:** dense/whole-loop wrap continuation landed (`50754fe8`, boot-fixed) and the task stays **open for
its overdub-overlay clause**, which is this slice. No 3.15e checkbox is in scope.

## Identity and scope

- **Approved contract:** the 3.15e task text's two sentences —
  *"Live overdub Capture remains a source overlay with exactly-once note/ledger ownership; no second writer to `current`
  or hidden whole-loop materialization"* and *"overdub MIDI and ledger events belong to the same reader exactly once
  through wrap, stop and pass handoff."* Supporting design-intent tightening (tasks.md, 2026-10-08): *"3.15e must remove
  independent scheduled live-Capture replay and edge sorting as that route migrates."* Relevant DECs: DEC-069 (wrap commit
  deferral while a predecessor stop owns the slot), DEC-116 (one rolling playback path per sounding slot), DEC-142/143
  (Record presentation lifetime, sealed-extent overlay).
- **Status:** specified (this packet) → fixture → native verified.
- **Owner and production entry:** `Track::playCommittedLoopMidi` (`src/Track/TrackPlaybackHotPath.cpp`), reached from
  `Track::playMidiEvents`; the overdub pass commit at the wrap is `Track::maybeCommitOverdubWrap` /
  `Track::commitOverdubWrap` (`src/Track/TrackOverdubWrap.cpp`); the stop/commit handoff is the deferred
  `TrackCaptureStopTestAccess`-drivable Record/overdub stop owner (`src/Track/TrackCaptureStopCommit.cpp`).
- **Primary files:** `src/Track/TrackPlaybackHotPath.cpp`, `src/Track/TrackOverdubWrap.cpp`,
  `src/Track/TrackPlaybackWindowBuild.cpp` (read-only unless the oracle is RED).
- **Existing test harness:** `test/test_shared_capture_finalization/test_shared_capture_finalization.cpp` — it defines
  `HARNESS_REAL_PLAYBACK_HOT_PATH` / `HARNESS_REAL_CAPTURE_STOP`, so `Track::playMidiEvents` and the real deferred stop
  owner are the production ones. Helpers reused: `stageTwoBarWorld`, `publishRootPairs`, `start`/`stop`,
  `driveStopToRetirement`, `TrackCaptureStopTestAccess` (`runtimeFor`, `selectSlot`, `resetProjectionCycle`, `unmute`),
  `MidiSendCapture`.
- **Preconditions present:** a committed two-bar Record pass with a published stale root and a prewarmed runtime
  (`stageTwoBarWorld`); the live-overdub note oracle `assertExactlyOnePlaybackNotePair` already used for a *non-wrapping*
  live overdub (`assertRecordRecoveryServesSecondBar(true)`).
- **Prerequisites inside this slice:** none discovered beyond the fixture itself (see G0 findings).
- **External blockers:** none. Device qualification is 3.15f's.
- **Explicit non-goals:** no route-selection, admission-constant, grain-size, owner, ledger-policy or pin change; no
  removal of the live-capture echo walk in this slice (see *Named architecture gap* below); no device claim; no 3.15e
  checkbox unless both the content oracle and the ownership clause are met.

## Oracle fixed before behavior edits

One case, driven entirely through the production clock entry (`Track::playMidiEvents`), on a two-bar loop (1 536 ticks),
with the production deferred overdub stop.

- **Stable case ID:** `test_3_15e_a_live_overdub_crosses_the_wrap_and_the_stop_handoff_exactly_once`.
- **Initial state:** committed two-bar Record pass (48@12/96, 62@800/880), published root carrying the same two soundings,
  prewarmed playback runtime, transport PLAYING, slot 0 selected, projection cycle reset.
- **Actions, in order:**
  1. `startOverdubbing` at an absolute tick inside bar two; capture note **A** (`70`) entirely **before** the wrap
     (`On@1470`, `Off@1500`).
  2. Drive the edge in bounded steps across the wrap (`1536`) so the wrap turn commits A's pass mid-overdub
     (`maybeCommitOverdubWrap` → `commitOverdubWrap` → `beginCapture(Overdub)`), then capture note **B** (`71`) entirely
     **after** the wrap (`On@1600`, `Off@1664`).
  3. Interleave the deferred playback-view owner between edge turns (the production `stepPlaybackViewMaintenance` grain),
     exactly as `main.cpp` does, so a stale pair/window is replaced off-clock rather than on the MIDI turn.
  4. Continue over a **second** wrap (`3072`) and into the next traversal.
  5. `stopOverdubbing`, then `driveStopToRetirement` (the real deferred stop), then drive a further traversal.
- **Expected observation after each step:**
  - *Before the wrap:* the live overlay's A pair is echoed exactly once (`70@1470` On, `70@1500` Off) while the committed
    base continues to be served from its reader.
  - *Wrap turn:* A's pass is committed (`overdubPasses` grows by one, `loop.playbackRevision` advances) and
    `capture.store` is emptied by `beginCapture` — no event is emitted **twice** for A in that turn (the wrap catch-up
    path `advancePlaybackCursorAcrossOverdubWrap` owns the committed interval, so the ordinary walk does not re-serve it).
  - *After the wrap:* B's live pair echoes exactly once (`71@1600/1664`); A is now committed content, served by the
    committed reader, **once per traversal** — never by both the echo and the committed walk in the same traversal.
  - *Stop handoff:* after the deferred stop retires, the second pass's content is served once per traversal from the
    committed reader; no duplicate articulation and no silent omission of an accepted event.
  - *Ledger:* the committed stream closes every articulation (open count 0 after each traversal). The live echo is
    emit-only by design (invariant pinned by sessions `221334`/`224719`) and must **not** write ledger entries.
  - *No hidden whole-loop materialization on the clock edge:* `Track::playbackOrderHotRebuildCount()` stays 0 across the
    driven turns (the counter the repo uses for content-sized builds on a timing-critical transport), and the merged
    store for the slot never carries live capture rows.
  - *No second writer to `current`:* the served reader role (`viewSet->lifecycle.current`, `preparedPair.generation`)
    changes only through an adoption of a **complete** prepared source, never by copying live rows in.
- **Counterexample that must fail:** any turn that emits A (or B) twice, any traversal that omits A after it is committed,
  any `playbackOrderHotRebuildCount()` growth during the live-overdub turns, or any live capture row appearing in
  `runtime.mergedMidiEvents.mergedEvents`.
- **Control:** the same drive with **no overdub** (world B of the existing 3.15a fixture) so the overlay's pairs cannot be
  confused with the committed base, plus the existing `assertRecordRecoveryServesSecondBar(true)` non-wrapping case.
- **Independence:** the expected stream is written explicitly per traversal; the count and order are compared
  element-wise from `MidiSendCapture`, never derived from the route's own state.

## Controls and coverage

- Seams reused: `MidiSendCapture` (the wire, both writers), `ActiveNoteLedger::forEachActive` (committed ownership),
  `Track::playbackOrderHotRebuildCount()`, `runtime.mergedMidiEvents.mergedEvents.size()`, and
  `TrackCaptureStopTestAccess` for state placement. No new test-only seam unless the oracle needs one, and then it is
  named in the evidence.
- Production paths exercised: `Track::playMidiEvents` → `playCommittedLoopMidi` → (`resolveQualificationPlaybackSourceForTurn`,
  prepared pair, merged fallback, live-capture echo) and `startOverdubbing`/`stopOverdubbing` → deferred stop owner.
- Mocks/stubs excluded from the asserted path: `HARNESS_REAL_PLAYBACK_HOT_PATH` and `HARNESS_REAL_CAPTURE_STOP` are
  defined in this suite, so neither the edge nor the stop is a fixture stand-in.
- Mutation (one, discriminating): force the live-capture echo walk's store read to be skipped (or force
  `maybeCommitOverdubWrap` to never commit) and require the content oracle to fail; restore byte-identically.
- Bounded drain: the fixture uses explicit bounded step counts; a failure prints ticks, emitted count, the store size and
  the route counters for the failing step.

## Named architecture gap (recorded, not fixed in this slice)

G0 verified that a **live** overdub's already-captured rows reach the wire through a **second cursor walk** at the end of
`playCommittedLoopMidi` (`advancePlaybackCursor(… PlaybackEmitPolicy::ActiveCaptureOverdub, makeCapturePlaybackStream(loop))`),
after the committed walk, with `loop.ensureCaptureEventsSorted()` + `reanchorCaptureIndex` on the MIDI turn. Every other
route (view, bridge, prepared pair, paged ring, merged store) is committed-only: the merged store refuses live capture by
contract. The echo is deliberately emit-only and non-ledger (`221334`/`224719`).

So the **content** claim of the 3.15e clause is what this slice tests; the **"same reader" / "remove the independent
live-Capture replay and edge sorting"** half is *not* satisfied by the current architecture and is recorded here as the
remaining 3.15e work item. It is a route/ownership change (a DEC-level question, DEC-116-adjacent), not a fixture detail.

## Verification recipe

- Focused: `pio test -e native -f test_shared_capture_finalization` (its full case list, including the new case).
- Broad: `pio test -e native` (expected current baseline 3 383 / 7 skipped / 3 376 succeeded).
- Builds: `pio run -e teensy41-capture-serial` and `-e teensy41-capture-serial-window-qualification` (RAM1 gates
  `≥ 840` padding; no re-base expected because no firmware line is planned).
- Spec: `openspec validate windowed-playback-generation --strict`; `git diff --check`.
- Device: NOT RUN (3.15f owns device qualification).

## Completion record

Filled after the run; planned results are never copied in as passes. See `EXECUTION.md` in this directory.
