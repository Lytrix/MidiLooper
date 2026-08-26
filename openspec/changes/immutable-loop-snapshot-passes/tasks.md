## 1. Architecture and specification gate

Phase 0 is documentation only. Later sections are blocked until 1.1–1.8 are complete.

- [x] 1.1 Complete formal architecture reassessment for immutable snapshots, including ownership, transitions, execution bounds, storage, and compatibility removal. Recommendation is recorded in `ARCHITECTURE-REASSESSMENT.md`.
- [x] 1.2 Record the approved decision in `docs/DECISION_LOG.md` and update the refinement plan, parent runtime architecture, derived-representation authority, storage authority, and naming with the accepted `LoopPass` / `LoopSnapshot` boundary. The logical indexes are approved; the physical directory remains a later measurement decision.
- [x] 1.3 Keep this change selected in `docs/Runtime/CURRENT_WORK.md` as Phase 0 only and post the phase architecture gate before any firmware edits. Gate posted in the implementation session; no firmware is authorized.
- [x] 1.4 Complete `PREFLIGHT.md` / `DECISION_REVIEW.md` for each later ownership or stop-path implementation phase. Change-scoped artifacts cover Phases 1–5; no later phase is authorized by them.
- [x] 1.5 Record the persistent reference-root invariant: successor snapshots reuse unchanged event-object references, allocate new objects only for added or modified content, and must not copy the complete reference set.
- [x] 1.6 Select the logical reference structure: notes as pitch → bar → membership lists of `NoteRef`s (span index) and events as bar → membership lists of `EventRef`s (indexed view of existing event-stream order). Path-copy is rejected as the overlap model. The physical directory representation remains Phase 1 / measurement.
- [x] 1.7 Specify snapshot reachability/reclamation, provisional session snapshots versus global undo snapshots, redo-branch clearing, and latest-saved-snapshot reboot behavior.
- [x] 1.8 Record the architectural construction contract (identify, replace, construct root, freeze, publish) and require an `ExecutionBounds.md` class before runtime publication. Treat any numeric N-candidate quantum as a measurement hypothesis only.

## 2. Immutable snapshot model

Phase 1 is selected after section 1 completed. The logical indexes are
selected; the physical directory is a hard measurement gate before its
production implementation. Candidate construction remains native/offline.

- [x] 2.1 Define immutable note and non-note event objects with loop-scoped stable references, a memory allocation contract, canonical span access for projection consumers, and a construction-unit measurement contract for later execution-bound classification.
- [x] 2.2 After the physical-directory measurement gate, implement the selected pitch/bar membership indexes and complete immutable `LoopSnapshot` records under `Loop` / `LoopPasses`. Allocate only the minimum directory structure required by the selected representation. Keep `LoopPass` as the compatibility operation record.
- [x] 2.3 Implement native/offline candidate construction from a const active snapshot and an existing semantic mutation with explicit resumable progress and measured units. Freeze, validate, and retain a complete candidate without making it active; preserve the active snapshot on failure.
- [x] 2.4 Verify the architectural invariants with existing native gates plus narrowly scoped checks only: snapshot completeness and self-containment, unchanged-object sharing, changed-object replacement, non-note preservation and ordering, multi-bar/wrap membership, `NoteId` deduplication, snapshot identity stability, alias safety, older-snapshot stability, and failed-candidate preservation. Do not add a new test architecture.

## 3. Capture and edit publication

Blocked until section 1 is complete and dirty-bar construction has a declared `ExecutionBounds.md` class. A long note can still touch every intersecting bar; this gate does not treat that as already bounded.

- [x] 3.1 Adapt `Loop::commitCapturePass` at its existing boundary to publish one global `LoopSnapshot` per record or overdub session-exit without changing wrap boundaries. Intermediate record prefixes and overdub wraps remain provisional.
- [x] 3.2 Adapt NOTE_EDIT close/commit to publish one global `LoopSnapshot` for a settled edit batch. In-session batch snapshots remain provisional `E:` revisions.
- [x] 3.3 Retain compatibility pass records. Use existing verification gates plus invariant-protecting parity checks for overlap, LIFO pairing, wrap pairs, equal-tick ordering, and non-note events.
- [x] 3.4 Before runtime publication, classify each named candidate-construction operation in `ExecutionBounds.md`; define the unit cap, maximum indivisible step, progress state, completion condition, and explicit overrun/failure policy. Do not introduce content-sized construction in `handleMidiInput()`, clock dispatch, or track-tick paths.

## 4. Direct consumers

- [x] 4.1 Make playback preparation read the active snapshot directly and use snapshot identity for freshness while retaining safe fallback; select the bounded playback span range before applying projection and keep `playbackEventPhase` scalar in the hot path.
- [x] 4.2 Preserve `Track` / `LoopPlaybackRuntime` ownership of cursor, order, ledger, and horizon state; verify output with existing playback gates.
- [ ] 4.3 Convert `LoopContentResolution` to stateless explicit-snapshot queries without parent traversal or maintained replay.
- [ ] 4.4 Migrate display, NOTE_EDIT, overdub analysis, and validation callers independently with rollback gates; select bounded canonical span ranges before `IntervalProjection` and define long-span `k` candidate limits or empirical overrun policies as consumer bounds, not snapshot-architecture units. Display paint fallback and visual-cache fill now read published snapshot events; NOTE_EDIT / analysis / validation remain on compatibility sources.
- [ ] 4.5 Verify record, overdub, wrap, short/long loop, slot, load, undo/redo, and active-capture separation with existing HITL and native gates.
- [ ] 4.6 Update each migrated derived representation to use snapshot identity for source freshness while retaining its existing owner, invalidation, bounded build, and stale/fallback policy; verify no full-loop rebuild is introduced on a timing-critical path.

## 4b. Post-stop MIDI stability

Stop-display hold is device PASS
[`004934`](../../../captures/session_20260826_004934.log)
(`ODUB,stop,display` 8.8 / 12.2 ms) unless wrap immediately precedes stop.
[`011829`](../../../captures/session_20260826_011829.log) last stop was
**155.6 ms** after wrap cleared the composed frame. Device PASS
[`092755`](../../../captures/session_20260826_092755.log) last stop
**11.1 ms**. **Now:** 4b.2 selected (host implement; device verify open).
4b.1 same-turn lane stays in tree and is not closed. 4b.3 host
implementation (load-frame polls + post-overdub MIDI batch cap) is in tree
(device verify open). 4b.4 is closed for the `DisplayResolveLiveCapture`
owner after repeated sub-10 ms captures.

- [x] 4b.1a Implement Ring B idle admission with one unconditional five-owner
      cursor turn; remove the snapshot cadence, visual cadence, and post-stop
      persist/snapshot pairwise gates. Keep the wrapped-content dependency skip
      non-priority and owned by the cursor position.
- [ ] 4b.1b Run the capture-serial device gate for Ring B: verify bounded
      `snapshot_idle`, visual dirty-bar progress during OVERDUBBING, and no
      same-turn snapshot/persistence overlap.
- [x] 4b.2a Implement RC2 representation coverage enforcement: overdub paint
      refuses uncovered visual-cache reads, wrap append is admitted through the
      Ring B owner, and dirty visual-cache source seeds fall back to committed
      pass reconstruction.
- [ ] 4b.2b Run the RC2 device gate: verify wrapped content reaches `DISP` and
      source-span diagnostics never use an uncovered dirty-cache seed.

- [x] 4b.0 After overdub wrap, keep the composed `DisplayNote` vector when
      resetting live compose bookkeeping (`invalidateLiveDisplayCache(true)`).
      `ODUB,stop,display` holds that frame while `visualCache` is dirty and
      does not require a `playbackRevision` match. Do not snapshot-gather on
      the stop stack. Evidence: [`011829`](../../../captures/session_20260826_011829.log)
      last DISP 121.203 s, wrap 121.318 s, no DISP until display **155.6 ms**;
      OD1 stop without wrap was 9.8 ms. Device PASS
      [`092755`](../../../captures/session_20260826_092755.log): wrap at
      151.099 s then DISP, last `ODUB,stop,display` **11.1 ms** (no `cer_win`
      on the stop stack); OD1 stop **21.4 ms**. BPM after last stop 117.7–122.4.
- [ ] 4b.1 Persist and first-record snapshot stay on PLAYING and OVERDUBBING
      idle through the Ring B five-owner cursor, but not on the same turn as
      one another. Do not move save onto the stop commit path. Do not convert
      `VerifyAndRename`. The 2 s playback-save
      grace is withdrawn — [`010505`](../../../captures/session_20260826_010505.log)
      stacked `LoopPersist` `done=46697` with `snapshot_idle` on first OD
      (`midi_gap` 32–51 ms).
      [`131852`](../../../captures/session_20260826_131852.log) still shows
      same-turn overlap (`snapshot_idle us=26823` at 147.341 s followed by
      `PERS,work payload_slice=20806`; repeated while pending). Guard landed
      in tree to block deferred save after a same-turn snapshot; recapture
      required before checkoff.
      [`132627`](../../../captures/session_20260826_132627.log) did not emit
      `PERS,work` while `snapshot_idle` was active (same-turn overlap not
      observed in that run). Keep open until repeated device recaptures hold.
      [`133517`](../../../captures/session_20260826_133517.log) again showed
      interleaving while pending (`snapshot_idle us=7360` then
      `PERS,work 86`). In tree: lane arbitration now includes RECORDING
      pending snapshot work and persist lane toggles now use
      RECORDING/PLAYING/OVERDUBBING pending snapshot detection; recapture
      required before checkoff.
      [`134438`](../../../captures/session_20260826_134438.log) emitted
      `snapshot_idle` lines but `PERS,work` count stayed `0`; same-turn
      overlap was not observed in that run. Keep open until repeated
      recaptures hold.
      [`135317`](../../../captures/session_20260826_135317.log) again kept
      `PERS,work=0` while `snapshot_idle` remained active. Keep open until
      repeated recaptures hold.
- [ ] 4b.2 Bound first-record `snapshot_idle` by starting construction after
      the playhead has left bar 1, using the overdub live-enqueue setup, and
      starting on PLAYING. First-take `loopLengthTicks` updates during
      RECORDING with `computeRecordStopLengthTicks`. Live construction
      starts after the playhead leaves bar 1 and continues each later
      completed bar when the parent still has tick room. Live RECORDING
      does not rebuild the settled prefix when the parent is too short.
      Growing record
      does not clamp capture ticks to that
      working length. OVERDUBBING only advances an in-flight candidate.
      Evidence: [`095720`](../../../captures/session_20260826_095720.log)
      had no `snapshot_idle` until PLAYING.
      [`114012`](../../../captures/session_20260826_114012.log) started at
      `record_length=768` while bar 1 was still being written.
      [`120020`](../../../captures/session_20260826_120020.log) started after
      bar 1 and did not restart during RECORDING; first-take display keeps
      continuous playhead length so `DISP` does not rescale on each
      `record_length` step.
      [`121754`](../../../captures/session_20260826_121754.log) rebuilt the
      prefix on each length step.
      [`125527`](../../../captures/session_20260826_125527.log) kept
      unpublished in-flight snapshot idle running on consecutive
      OVERDUBBING/PLAYING turns (2.6–27.4 ms slices; `midi_gap` spikes).
      In tree: Ring B admits the snapshot owner on its five-way cursor.
      [`130244`](../../../captures/session_20260826_130244.log) still stacked
      unpublished snapshot with long visual idle reconstruct bursts.
      In tree: unpublished snapshot contention keeps visual fallback
      constrained and cadence-gated while snapshot updates stay active.
      [`131338`](../../../captures/session_20260826_131338.log) still showed
      PLAYING stalls with first-PLAYING stacked idle/snapshot/persist and
      later `usbdisp`/`load_frame`/`late_on` windows.
      In tree: unpublished visual constraints now apply whenever
      unpublished work is pending, and live-record append-start range is
      capped to one completed bar per start.
      Do not check off until device verify. Do not
      step successor construction during OVERDUBBING after the first root.
- [ ] 4b.3 Attribute and bound the post-overdub-stop PLAYING window whose
      `midi_gap` is `usbdisp` + `load_frame` + `late_on`, not
      `ODUB,stop,display`. Evidence: `004934` 67.4 ms at 113.911 s
      (`usbdisp=60134`, `late_on=57719`, `load_frame=17575`; display was
      8.8 ms) and 152.3 ms at 168.952 s (`usbdisp=93249`, `late_on=103564`,
      `load_frame=27039`; display was 12.2 ms). Do not implement with 4b.1.
      [`131852`](../../../captures/session_20260826_131852.log) reinforces
      this owner split: OVERDUB `midi_gap=236090` (`usbdisp=143649`,
      `load_frame=25638`) and STOPPED `midi_gap=430552`
      (`usbdisp=538063`, `load_frame=401498`, `idle_maint=337246`) while
      `ODUB,stop,display` stays 21.9-50.7 ms.
      [`132627`](../../../captures/session_20260826_132627.log) still shows
      post-overdub PLAYING `midi_gap=175230`
      (`usbdisp=121603`, `load_frame=14728`, `late_on=184364`) and STOPPED
      `midi_gap=198666` (`usbdisp=319516`, `load_frame=191331`,
      `late_on=30491`) while `ODUB,stop,display` remains bounded
      (18.5-38.1 ms). In tree: post-overdub drain now polls MIDI during
      `runDeferredLoadAndDisplayFrame` between display/load slices; recapture
      required before checkoff.
      [`133517`](../../../captures/session_20260826_133517.log) kept
      post-overdub PLAYING windows in the 26-52 ms class after stop-display
      lines (`ODUB,stop,display` 19.6-51.4 ms), while larger windows remained
      in OVERDUBBING (`midi_gap=200549` with `usbdisp=129828`,
      `load_frame=23600`, `late_on=26036`).
      [`134438`](../../../captures/session_20260826_134438.log) still shows
      post-overdub PLAYING `midi_gap=242938`
      (`usbdisp=149450`, `load_frame=53120`, `late_on=69979`) after
      `ODUB,stop,display=44222`; STOPPED still reached `midi_gap=323973`
      with `loop_rem,idle_reconstruct=148417`, `loop_rem,idle_maint=180315`,
      and `load_frame=38737`.
      In tree: `MidiHandler::handleMidiInput` now caps per-call dispatch batch
      size to `32` (from `128`) while
      `anyPlayingMidiDrainAfterOverdubStop()` is active, so post-overdub
      drain turns cannot spend a full 128-message burst in one `usbdisp`
      slice. Recapture required before checkoff.
      [`135317`](../../../captures/session_20260826_135317.log) first
      post-stop cycle remained bounded (`ODUB,stop,display=15428`), but later
      `midi_gap=181392` appeared after PLAYING re-entered OVERDUBBING; later
      cycles were `midi_gap=42033` and `midi_gap=22132`. Keep open.
      [`140201`](../../../captures/session_20260826_140201.log) improved
      post-stop PLAYING `midi_gap` to `48361` (from `142534` in `135317`), but
      first-bars OVERDUBBING still reached `midi_gap=133468`. The same run also
      showed overdub reopen source drift (`DIAG,lcr,src,why=open` notes
      `0`/`165`/`228` versus nearby PLAYING `DISP` totals `240`/`253`/`308`).
      In tree: source-cache reopen hardening now invalidates span-cache reuse on
      restamp miss and seeds from clean `visualCache` when prepared spans miss
      and no prior cache seed exists. Keep open until recapture.
- [x] 4b.4 Bound `DisplayResolveLiveCapture` during OVERDUBBING so a live
      compose does not take a 50 ms class slice. Evidence: `004934`
      `timing_max,DisplayResolveLiveCapture,58188` at 150.535 s. Owner stays
      `rebuildOverdubCommittedDisplayLayer`. Do not CER-gather on overdub
      paint. Do not implement with 4b.1.
      [`131852`](../../../captures/session_20260826_131852.log) records
      `timing_max,DisplayResolveLiveCapture,215619` at 194.429 s.
      [`134438`](../../../captures/session_20260826_134438.log) records
      `timing_max,DisplayResolveLiveCapture,225601`.
      [`135317`](../../../captures/session_20260826_135317.log) records
      `timing_max,DisplayResolveLiveCapture,163008`.
      [`140201`](../../../captures/session_20260826_140201.log) records
      `timing_max,DisplayResolveLiveCapture,7107` (improved from `163008`).
      [`141034`](../../../captures/session_20260826_141034.log) records
      `timing_max,DisplayResolveLiveCapture,8608` (still below the 50 ms class).
      In tree: `DisplayManager::rebuildOverdubCommittedDisplayLayer` now skips
      overdub paint snapshot gather+reconstruct when `visualCache` already has
      notes and falls through to held-layer/window-filter branches. This keeps
      the owner-local fallback bounded. Closed for this owner; first-overdub
      stalls and wrap note-loss remain tracked under 4b.3 / source-cache reopen.

## 5. Undo, load, and persistence

- [ ] 5.1 Make `TrackUndo` and clear-slot restore select before/after snapshot identities while preserving strict LIFO, runtime-only undo/redo, and redo-branch clearing after new work.
- [ ] 5.2 Approve the physical storage version/layout for immutable objects, the latest saved snapshot, metadata, and local handle rebuild. Do not persist undo history or post-reboot undo lineage.
- [ ] 5.3 Implement the new content-only storage reader/writer through existing `StorageManager` deferred units and compatibility migration with rollback behavior; classify any interval-path unit in `ExecutionBounds.md` and keep runtime addresses and derived representations out of the format.
- [ ] 5.4 Verify save/load round trips, selected-snapshot equivalence, clear-slot restore, and old-format migration with existing persistence gates plus invariant-protecting checks. After load, undo depth is 0 and redo is empty.
- [ ] 5.5 Verify deferred persistence remains owned by `StorageManager` without blocking playback or `handleMidiInput()`.
- [ ] 5.6 Verify large-loop save and migration progress, overrun/failure preservation, active-snapshot availability, and absence of content-sized synchronous work at stop, input dispatch, clock dispatch, and track-tick paths.

## 6. Compatibility retirement and closeout

- [ ] 6.1 Prove no normal consumer depends on maintained LCR history, `passesMaterializedStore_`, or full replay before removing each path.
- [ ] 6.2 Remove superseded maintained-LCR state and replay APIs only after caller migration and rollback gates pass.
- [ ] 6.3 Remove compatibility effective-state derivation and old readers only after documented triggers close.
- [ ] 6.4 Run capture-serial firmware build first with RAM1 locals at least 4000, then `pio test -e native`, followed by required existing HITL verification.
- [ ] 6.5 Update runtime docs, deliverable tracking, the refinement plan, and archived specs; sync and archive only after all gates pass.
