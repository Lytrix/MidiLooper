# Successive-overdub display/playback regression — 121645 capture

Read-only investigation, 2026-10-31. Source: `captures/session_20261031_121645.log`
(27,384 lines). Branch: `feature/unified-rolling-playback-path`. Extensive existing
staged/unstaged work preserved. No firmware/test/OpenSpec/device/SD changes or acceptance claim.

## Scope traced

Owner reports first-bar silence after Record stop, earlier overdub rows disappearing
on successor Start, temporary lengthening on window movement and one-tick/hidden
earlier overdub rows until commit completes. Applicable scope: CURRENT_WORK display
source / loop wrap, interacting with the still-open stop-edge device content gate.

## VERIFIED

- Record stop at 160.371973 s: `RECS,stopToStopped,3,15336,120,15216,15360,0`
  (line 14674). Length is 20 bars, not the previously fixed inside-final-bar trim.
- Frozen Record window `[0,768)` completes at 160.805294 s with lead 15352 ticks
  (line 14860): its next occurrence is almost a full loop away. `[768,1536)` completes
  at 160.887021 s, lead 728 (line 14867). Adoption is reported at 162.720425 s
  (line 14879). A surviving channel-8 NoteOn is phase 984 at 163.280801 s
  (line 17065). This supports a missed first-window occurrence, not full output parity.
- At 192.212682 s, `stop_tail,...,9,work_budget,16` selects raw-only retirement
  (lines 15752–15754). At 192.364662 s frame count falls **377 -> 342**, committed
  revision/pass **6 -> 7**, stop previews **2 -> 1** (lines 15750–15759). Window
  moves 1424 -> 1504 too; counts alone do not identify the lost rows.
- At 210.241041 s another budget cut occurs (lines 16368–16370). Direct
  `#PAINT_BEAT` falls **435 -> 382** from 209.890 to 210.936 s, with window fixed
  at 3072 (lines 16364, 16410): a stronger continuity witness, not proof of pass deletion.
- Direct frame observations fall **401 -> 344** across successor Start at
  217.645–217.711 s, window 0 in both (lines 16614, 16645–16650, 16668).
- The visual cache stays at **316 notes / dirty=1** throughout the later chain.
- Timing is genuinely degraded: the 175.768 s family reports late On/Off
  **133,364 / 133,322 us**, MIDI Input Gap **186,658 us** (first overdub Start);
  the 221.039 s family reports **60,942 / 61,108 us**, gap **112,747 us**.
- Named work includes `display_frame,87127` at 192.364629 s (line 25836) and
  `idle_maint,109020` at 192.050767 s (line 25673). `load_frame` encloses display
  work; these costs are nested, not additive.
- Close reports **4,136 shed rows / 2,370 Tier-A evictions** (line 27382).
  Direct and queued output interleave out of timestamp order: analysis sorted CAP
  timestamps, not file position. MO absence and aggregate output counters cannot
  establish target-track playback silence here.

## Current execution path / ownership and lifetime map

Track owns capture transitions, accepted interactions, frozen playback and stop-job
custody; TrackManager orchestrates. Loop owns passes, pending transforms, publication
and sealed/retired display overlays. DisplayManager reads them without mutating MIDI
storage. Playback reads Track event windows, never the visual cache/pixels.

Draw-source path: `syncDetailedPaintWindow` -> `gatherCommittedDrawSourceBars` ->
`applyDrawSourceOverlays` -> `applyPendingDrawSourceTransforms` ->
`applyCapturePlayheadTails`. The alternate live route uses
`buildCommittedDisplayBase` -> `rebuildDisplayNotesInWindow`, capped at 16 bars.
Both routes need coverage. The stale-root/pass-gather repair already present in
the dirty tree is not new work here; the capture does not prove that exact tree ran.

## Deferred-work / state-transition map

Record -> stopped-producer custody -> frozen playback preparation -> ready/adopt ->
committed publication. Observed first ready coverage misses its occurrence.
Overdub Start -> live Capture -> Stop closes input/returns Playing -> deferred
canonical/companion worker -> inline companion or budget raw fallback -> preview
retirement -> later convergence. A successor can already be live at retirement.
Playback readiness/adoption receipts come from `reportFrozenRecordReadinessReceipt`
and `reportRootReturnAdoptionForSlot` in `src/Track/TrackPlaybackWindowBuild.cpp`.

## Resource / timing consequences

`Architecture/ExecutionBounds.md` declares display resolve empirically bounded with
5 ms measurement; transfer is fixed-size but blocking; aggregate idle admission
does not bound an expensive single child. A 16-bar cap is not a microsecond bound.
No RAM1/PSRAM/LoopPool model changed or measured here. Persistence and undo remain
downstream consumers; saved content, refcounts and undo/redo were not verified.

## Existing validation / hidden dependencies

Relevant suites: `test_live_capture_display_boundary`, `test_pending_note_change`,
`test_immediate_capture_handoff`, `test_shared_capture_finalization`,
`test_overdub_source_view`. No suite/build run in this read-only investigation.
Earlier green fixtures do not establish every transition observed here.

Needed production-path discriminators before repair:
1. Twenty-bar Record stop with real clock/main ordering, preparing just after phase
   zero; compare original recorded events at every tick, without pre-draining work.
2. Live successor plus predecessor budget/raw retirement: compare exact painted IDs
   and spans at fixed viewport, then one-bar movement and wrap; current/stale-root controls.
3. Overdub-over-overdub: observe committed, after-pending and after-tail geometry of
   the same IDs after each service grain, including untouched future notes. Do not
   drain the finalizer between observations.
4. Attribute display resolve/gather/compose/draw cost using bounded existing capture
   gates, not another diagnostic stream. Current parent measurements do not name the child.

## ASSUMPTIONS

Preview retirement with an uncovered committed base strongly fits the count drops.
Moving-window gathering/composition plausibly contributes display cost. Neither is
yet an exact identity-level causal proof.

## UNKNOWNS

Exact flashed HEX/source (capture starts mid-session); complete first-bar emitted
inventory; transient one-tick/lengthened IDs and their transform stage; final effective
inventory after settle; the expensive display child.

Code hazard to discriminate, not patch by inspection:
`applyPendingHideAndShortenToNotes` guards tiny Shorten replacements but applies Hide
immediately; `upsertSourceTransform` can convert a degenerate Shorten into Hide before
that guard. This does not prove an observed Hide is wrong; legitimate overlap removal
must be preserved.

## CONFLICTS

No new authority conflict established. First-bar, frame-continuity and MIDI timing
acceptance remain open despite previous scoped fixes.

## G0 — implementation evidence verdict: BLOCKED

Evidence supports reopening the regression, not choosing a repair yet. Next: controlled
production-path fixtures above. Do not repeat a blanket tail freeze, expand the window,
claim durable deletion, or treat capture loss as proof of silence.

## Closeout

Runtime journals link this evidence. No architecture decision or changed exclusion;
nothing shipped, no task checkbox changed, no commit/flash/serial/SD operation.
