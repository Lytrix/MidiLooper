# Undo/redo and the playback source — 2026-10-29

Owner report: *"the undo/redo makes the playback break; even returning to the earlier root still doesn't send MIDI
events to the playback"*, with `captures/session_20261029_121423.log`.

This note records what the capture proves, what the code proves, and the RED fixture that pins the defect. No
firmware behaviour changed in this slice.

## 1. Is there a planned undo/redo step to start with?

No open one covers this shape.

| Task | State | Why it is not this defect |
|---|---|---|
| `immutable-loop-snapshot-passes` 5.8 — requeue a committed-pass rebuild after undo/redo of a record/overdub entry | **checked / closed** (device `content-verify-03`, PASS) | It fixes *"no root is requested at all"*. The request works — the device row shows `pub=1` one millisecond after the undo. The open defect is downstream of the request. |
| `immutable-loop-snapshot-passes` 5.8a — an undo that disables a capture pass releases the sealed draw-source handoff | **checked / closed** | It fixes the **display** union of sealed rows (`content-verify-65`). Playback reads the published root through a different gather. |
| `immediate-record-overdub-handoff` 4.14 — live undo/redo capture coherence | open | Different shape: undo *inside a live overdub session* (canonical Capture, staging, overlap state). The reported gesture is a global undo after a stop, with no capture interval live. |
| `immutable-loop-snapshot-passes` 6.6.5 GAP-8b — stale published root during session-exit publish | deferred, **do not re-land without a design session** | Session-exit publish only; explicitly reverted once (`3c9dcd4a`). |
| `playback-regression-after-retained-start-fix` §9 (DEC-194) | open | The right home: it already fixes "a root is an optional accelerator, not a precondition". It does not yet state what the window must contain when the root is stale **because the inventory shrank**. |

So this is a **new step under DEC-194**, recorded as section 10 of this change's `tasks.md`.

## 2. What the capture proves (`session_20261029_121423.log`, image `bea5a99b…`)

31-bar slot (23,808 ticks), 7 committed passes, transport playing from 11.68 s.

1. **The undo edge requests a rewrite and it is served late.** `15.253 MIDI: Undo (entries=6)` → `15.343 Undo
   applied: kind=1 undo_count=5` → first row `#STOP_TRACE,15344,…,pub=1,q=6,a=0` and **73 such rows** through
   `#STOP_TRACE,87743,…`. `stop=0` on every one: no stop job is involved, the owed work is the undo's rewrite.
2. **The rewrite is a whole-root rebuild, not a window.** 84 `snapshot_unit,kind=start`, 84 `kind=finish`, 1432
   `kind=step`, and exactly one `kind=publish` at **88.379 s** — `n=6629` events, `b=587,816` B. Time from the undo
   to the replacement root: **72.4 s** — one full loop. `q` drains 6→0 and `a` climbs 0→7 in that window.
3. **Nothing serves the committed window in between.** `playback_read` = 0 rows for the whole run; the only
   `playback_build` row is `retained_identity_refreshed` at **88.418 s**, i.e. immediately after the publish.
4. **The display holds the same gap.** `disp_turn … st=ret,why=hold_gap` at 15.476 s and 23 such rows; every
   `#PAINT_BEAT` from 15.803 s on reports `dirty=1` with `notes=384` against `vcache=615`, i.e. the undo's
   invalidation is never drained while the rewrite is owed.
5. **The transport was not silent in this window.** `#PAINT_BEAT out=` stays at 17-54 note-ons per second across
   16-30 s, so a literal "zero MIDI events while the rewrite is owed" is **not** what this capture shows. What it
   shows is that the serving source never left the stale root: `#CAP,MO` rows are sampled out on this image
   (`shouldSampleMidiOutCapture`), so *what* was emitted cannot be read from here — only that something was.

Consequence for the reading of the report: the capture proves the **source did not follow the undo** for 72.4 s and
that no window path served at all. Whether the owner's silence is the undone part still sounding, a specific track,
or a later gesture is not decidable from this log; the code section below names why the *content* cannot follow.

## 3. What the code proves

`ensurePlaybackMergedMidiEventsBuilt` (`src/Track/TrackPlaybackWindowBuild.cpp`) tries the published root first:

```cpp
if (loop.tryGatherPublishedSnapshotEventsInWindow(runtime.mergedMidiEvents.mergedEvents, winStart, winLen, …)) {
  usedSnapshotIdentity = buildIdentity;
} else if (… !loop.publishedSnapshotIsCurrent() … ) {   // DEC-194 passes fallback
  loop.copyEffectiveCommittedEventsInRange(...);
}
```

`tryGatherPublishedSnapshotEventsInWindow` (`src/Loop/LoopSnapshotPlayback.cpp`) does this for a root that exists
but is **not current**:

1. `gatherPublishedSnapshotMembership(*snapshot, …)` — the root's immutable membership, **with no pass-state filter**
   (it walks `snapshot.eventMembership(bar)` and never consults `passes`).
2. then it appends only the passes the pending box enumerates (`queuedPassIds`, `appliedPassIds`, wrap journals,
   session-exit receipts).

So the stale-root path is **additive only**. When an undo *removes* a pass (`setCapturePassState(Disabled)` in
`Loop::setCapturePassState` — the owner `TrackUndo` calls), nothing can take that pass's rows back out:
the root still carries them, and the DEC-194 passes fallback is only reached when the gather **fails**, which it
does not.

The production pairs are exactly:

* `TrackUndo::applyPublishedSnapshotSelect` → `requestPlaybackSourceCurrency(loop, rebuildFromPassInventory)` →
  `Loop::requestCommittedPassesRebuild()` / `requestCommittedPassesRepublish()`, which by design *keeps the previous
  root selected and audible until the replacement lands* (`LoopSnapshotPublish.cpp`), and
* `Loop::setCapturePassState(id, Disabled)` → `++playbackRevision` + `notifyCommittedContentChanged()`.

Together that is "a new inventory, still served from the old root's membership".

## 4. The RED fixture (landed, failing)

`test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp`:

`test_an_undo_disabled_pass_leaves_the_playback_window_before_the_root_rewrites`

* 64-bar slot (`prepareWindowOwnerSlot`), record pass (pitch 48 at tick 0) + one committed overdub pass
  (pitch 60 at bar 1), root published **from the committed passes** through the production publisher, then
  `TRACK_PLAYING` with the playhead at tick 96 (window `[0, 1536)`).
* Baseline: the window holds the overseen pair once each (2 events pitch 48, 2 events pitch 60).
* Then the production undo edge only: `setCapturePassState(901, Disabled)` + `requestCommittedPassesRepublish()`.
* One rebuild turn, then assert the disabled pass is gone and the window is the surviving pass alone.

Measured RED:

```
Expected 0 Was 2. the undone pass must leave the window before the root rewrites
```

and, measured while narrowing (scratch run, assertion since removed): the window holds **4** events after the undo
(the record's two **and** the disabled overdub's two) while `countPitch(48)` is **2** — so this is not the additive
overlay duplicating a pass; it is the root's own membership surviving a pass the inventory no longer has.

Suite: `pio test -e native -f test_runtime_working_set_ownership` → **258 cases, 3 failed, 254 succeeded**. The three
are this RED, `test_dec193_a_restarted_rootless_slot_keeps_owing_a_build_that_is_never_served` (`gate_no_role`) and
`test_dec194_a_rootless_playing_slot_builds_its_first_window_without_a_root` (`role_promise_pending`) — the two
pre-existing REDs of this change, unchanged. No collateral failure, no firmware edit.

## 5. What the repair has to be (not implemented)

One existing owner, no new state:

* A published root may serve the window **only while it represents the current pass inventory**. "Current" already
  has one definition in this tree (`publishedSnapshotIsCurrent()`); the missing case is "not current *and* the
  inventory changed for a reason other than a session-exit append".
* The bounded shape that reuses DEC-194's answer: when the root is not current, take the **committed passes** as the
  window source (the same `copyEffectiveCommittedEventsInRange` the rootless path already uses) instead of the stale
  root's membership — the root then only serves the window while it is current. This keeps the "no playback gap on
  undo" property the republish request was written for, because the passes are the authoritative source, and it
  leaves the DEC-143 pre-publication Record overlay (the `!snapshot` branch) untouched.
* Named risks to prove, not assume: the paged-domain gate (`3_12` over-capacity refusal must still refuse), the
  whole-loop window cost for short loops, and the reuse guard's identity (revision + snapshot identity) so the
  passes-backed window cannot be served stale.

Acceptance is the pixel-free oracle of this fixture plus a device leg on the same 31-bar slot: an undo while playing
must change the emitted content **within the current loop**, without waiting for `snapshot_unit,kind=publish`.

## 6. Repair landed (DEC-199) — native + build green, device open

The fix is the user's own framing: an undo means the root must not be used for playback, so the window is computed from
the committed passes and sent.

`ensurePlaybackMergedMidiEventsBuilt` now selects the window's source by **currency, not by length**:

| Condition | Source |
|---|---|
| root exists and **is current** | the published root (immutable exact membership) |
| root exists but is **not current** (undo/redo/enable/disable/companion change) | the **committed passes**, pass-state filtered, bounded by the window |
| **no root at all** | unchanged: DEC-143 sealed Record extent, then gather miss → passes |
| paged lane owns the interval | unchanged: the pager answers (refused by name), never a pass copy |

Two supporting details the RED exposed:

* presence is read from `publishedSnapshotCoveredRevision() != kSnapshotCoverageUnset` — `publishedSnapshotIdentity()`
  answers **null while a root is stale by design**, so it can never detect the state this rule is about;
* the merged-window readiness receipt (`isPlaybackMergedMidiEventsReadyForSlot`) drops its extra `runtime->isStale()`
  term — `builtFromRevision == playbackRevision` already states currency for this runtime's cache, and a rootless slot's
  runtime has no synced root to compare against (that term is what failed the two `s4b_6` provisioning fixtures under the
  unified window).

One window rule also replaces the length split in this path (DEC-198's direction, completed here): window =
`min(2 bars, loopLength)` starting at the playhead for **every** loop; the loop only bounds it. The comments that
described a long-loop and a short-loop branch are gone.

Verification:

* fixture red → green with no other change: `Expected 0 Was 2` → pass;
* full native **3608 / 2 failed / 7 skipped / 3598 succeeded** — the two failures are the pre-existing DEC-193
  (`gate_no_role`) and DEC-194 (`role_promise_pending`) REDs;
* focused: `test_runtime_working_set_ownership` 258 (same 2 failures), `test_playback_prewarm` 252/252,
  `test_playback_midi_output` 18/18, `test_windowed_preview_integration` 48/48, `test_continuous_capture` 107/107,
  `test_live_capture_display_boundary` 43/43, `test_shared_capture_finalization` 142/142;
* `teensy41-capture-serial` **SUCCESS**, RAM1 `variables 96,192` unchanged, ITCM `code 392,056 + padding 1,160`
  (floor 840).

Not claimed: device behaviour. The acceptance is unchanged — undo on the 31-bar slot must change what is **sent**
inside the current loop, without waiting for `snapshot_unit,kind=publish`. No upload, no SD operation.
