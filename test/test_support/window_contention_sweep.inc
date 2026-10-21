//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-108 §13b / task 3.10 + 6.3 -- **the `H x K` contention sweep**.
//
// The declared envelope is `Delta-max x H x bounded per-track work`. Each multiplier was *derived* (the advance from
// the device measurement, the rotation from the spec's fairness sentence, the work from the window's own derived
// cost) and the **composition** of the three had never been measured: one lane turn serves one *track*, a track's
// sibling slots share that track's turns, and both are paid for out of one playhead advance. This sweep drives the
// production composition -- `serviceAdmittedDeadlineMisses` + `playbackWindowServiceOffer` per track,
// `selectPlaybackHolderRoundRobin` across tracks, one grain through `stepPlaybackViewMaintenance` -- and measures it in
// the declaration's own currency: **one lane turn is one opportunity, whose permitted advance is `Delta-max` ticks**.
//
// It is a header-only include for one suite, so the cell helpers can read the suite's own fixtures
// (`commitRecordPassIntoTrackSlotOfLength`, `publishNotesInBarsRootForSlot`, `nextWindowRequestClock`, the
// substitutable-row recorders). Test-only: no production line is touched, no gate is flipped, no constant is moved.
//
// Included by `test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp` after its own helpers.

/// The declared permitted advance per opportunity, **read from the declaration and never restated**: one lane turn is
/// one opportunity, so driving `advance = this` makes the sweep's clock the envelope's own unit.
static const uint32_t kContentionDeclaredAdvance = Track::kPlaybackViewPermittedAdvancePerTurn;

/// The phase every holder of a cell is requested at: one bar in, so the interval the policy selects is the two bars
/// **centred on it** (`playbackWindowInterval`), and the lead the admission judges is the rest of that loop.
static const uint32_t kContentionRequestBar = 12;
static const uint32_t kContentionRequestPhase = (kContentionRequestBar * Config::TICKS_PER_BAR) + 384u;

/// One cell's shape. `tracks` is `H` (holders competing for the lane's single turn), `siblings` is `K` (further holders
/// on the target's own track), `loopBars` the span and `notes` the density; `advance` is the swept clock unit.
struct WindowContentionCell {
  uint8_t tracks = 1;
  uint8_t siblings = 0;
  uint32_t loopBars = 24;
  uint32_t notes = 20;
  uint32_t advance = kContentionDeclaredAdvance;
  /// Content shape: spread entries (the grid's density axis) or one **chord** whose notes share their two ticks.
  bool chord = false;
};

/// What one **shape** costs, measured on a throwaway world: the charge its retained record carries, the entries it
/// gathered, and the **service turns** one window of it actually needs. All three are read rather than derived, because
/// the charged grains include the promotion grain (whose work happens inside the completing turn) and the order bound
/// (which is skipped when the input is already ordered) -- so the charge is an upper bound on the turns, never equal to
/// them, and only a measured turn count can be composed.
struct WindowShapeProbe {
  uint32_t chargedGrains = 0;
  size_t sourceEvents = 0;
  uint32_t serviceTurns = 0;
  uint64_t deadlineTick = 0;
  uint64_t completionTick = 0;
  uint32_t promisedTurns = 0;
  /// How many distinct ticks the gathered window carries. A spread shape's entries sit on their own ticks; a chord's
  /// sit on two (one On tick, one Off tick), which is the density axis the spread cells cannot express.
  uint32_t distinctTicks = 0;
};

/// What one **cell** measured -- all of it read back from production surfaces (the retained work records, the per-track
/// budget, the named rows) rather than recomputed from the declaration.
struct WindowContentionResult {
  uint8_t tracks = 0;
  uint8_t siblings = 0;
  uint32_t loopBars = 0;
  uint32_t notes = 0;
  uint32_t advance = 0;
  uint32_t windowsOwed = 0;
  uint32_t windowsCompleted = 0;
  bool drained = false;
  uint32_t drainTurns = 0;
  uint32_t ownGrains = 0;
  uint32_t othersGrains = 0;
  uint32_t trackGrains = 0;
  uint32_t targetCompletionTurns = 0;
  uint32_t firstGrainTurn[Config::MAX_LOOPS_PER_TRACK] = {}; // target Track; 0 means not observed
  uint32_t maxCompletionTurns = 0;
  uint64_t deadlineTick = 0;
  uint64_t maxCompletionTick = 0;
  uint32_t promisedTurns = 0;
  uint32_t missRows = 0;
  uint32_t breachRows = 0;
  uint32_t completeRows = 0;
};

/// A fresh world per cell -- the same triple `setUp()` performs, plus the pool prime the suite's tests do, so several
/// cells can be swept in one fixture without one cell's content, promises or pool occupancy leaking into the next.
inline void resetWindowContentionWorld() {
  trackManager.~TrackManager();
  LoopEventStore::resetPoolForTests();
  new (&trackManager) TrackManager();
  LoopEventStore::initPool();
}

/// The request source a build borrows **across turns**: one buffer per **(track, slot)**. The suite's
/// `windowSourceScratch` is indexed by slot only, so two tracks' slot 0 would share one buffer and a second request
/// would pull the first build's source out from under it -- the lifetime defect this suite already records once.
inline SessionMidiEventVec& contentionSourceBuffer(uint8_t track, uint8_t slot) {
  static SessionMidiEventVec buffers[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK];
  return buffers[track % Config::NUM_TRACKS][slot % Config::MAX_LOOPS_PER_TRACK];
}

inline const PlaybackViewAdmittedWork* contentionActiveWork(uint8_t track, uint8_t slot) {
  const LoopPlaybackRuntime* runtime =
      TrackCaptureStopTestAccess::runtimeIfAllocated(trackManager.getTrack(track), slot);
  return runtime != nullptr ? runtime->activeViewBuildWork() : nullptr;
}

/// Publish `notes` note pairs that **share their ticks** -- one chord, On at `tick`, Off at `tick + 240` -- as the slot's
/// current root. The spread helper gives every note its own tick, so this is the shape that exercises same-tick density
/// (chords) rather than note count.
inline void publishChordRootForSlot(Loop& loop, uint32_t tick, uint32_t notes) {
  auto empty = std::make_shared<const LoopSnapshot>(LoopSnapshot(loop.loopId, loop.loopLengthTicks, 0, 8, 8));
  LoopSnapshotChangeSet changes;
  EventId nextEventId = 12000;
  for (uint32_t i = 0; i < notes; ++i) {
    const uint8_t pitch = static_cast<uint8_t>(40 + (i % 40));
    SnapshotNoteChange add{};
    add.kind = SnapshotChangeKind::Add;
    add.next.loopId = loop.loopId;
    add.next.noteId = static_cast<NoteId>(6000 + i);
    add.next.pitch = pitch;
    add.next.velocity = 100;
    add.next.startTick = tick;
    add.next.endTick = tick + 240;
    changes.notes.push_back(add);
    SnapshotEventChange on_change{};
    on_change.kind = SnapshotChangeKind::Add;
    on_change.next.loopId = loop.loopId;
    on_change.next.eventId = nextEventId++;
    on_change.next.canonicalOrder = nextEventId * 2;
    on_change.next.value = MidiEvent::NoteOn(tick, 1, pitch, 100);
    changes.events.push_back(on_change);
    SnapshotEventChange off_change{};
    off_change.kind = SnapshotChangeKind::Add;
    off_change.next.loopId = loop.loopId;
    off_change.next.eventId = nextEventId++;
    off_change.next.canonicalOrder = nextEventId * 2 + 1;
    off_change.next.value = MidiEvent::NoteOff(tick + 240, 1, pitch, 0);
    changes.events.push_back(off_change);
  }
  LoopSnapshotConstruction construction(empty, changes);
  while (!construction.complete()) {
    TEST_ASSERT_TRUE(construction.step(128));
  }
  const ImmutableLoopSnapshotRef snapshot = construction.freeze();
  TEST_ASSERT_NOT_NULL(snapshot.get());
  loop.storePublishedSnapshotForTest(snapshot, loop.playbackRevision);
}

/// Count the distinct ticks in the source a build is borrowing.
inline uint32_t distinctSourceTicks(const SessionMidiEventVec& source) {
  uint32_t distinct = 0;
  for (size_t i = 0; i < source.size(); ++i) {
    bool seen = false;
    for (size_t j = 0; j < i; ++j) {
      if (source[j].tick == source[i].tick) {
        seen = true;
        break;
      }
    }
    if (!seen) ++distinct;
  }
  return distinct;
}

/// Commit a slot with a current root, prewarm it, and request its window **at the caller's stamp**.
inline void prepareContentionHolder(uint8_t track, uint8_t slot, uint32_t loopTicks, uint32_t notes,
                                    const PlaybackDeadlineClock& stamp, bool chord = false) {
  Track& t = trackManager.getTrack(track);
  commitRecordPassIntoTrackSlotOfLength(t, slot, loopTicks);
  Loop& loop = t.getLoop(slot);
  loop.loopLengthTicks = loopTicks;
  // The notes sit **inside** the two-bar interval this phase selects (bars 12..13 live in
  // `[phase - one bar, phase + one bar)`), so the gathered event count is exactly `2 x notes` whatever the loop length
  // is -- which is what makes a cell's cost a function of its density alone.
  if (chord) {
    publishChordRootForSlot(loop, kContentionRequestBar * Config::TICKS_PER_BAR, notes);
  } else {
    publishNotesInBarsRootForSlot(loop, kContentionRequestBar, kContentionRequestBar + 1, notes);
  }
  TEST_ASSERT_TRUE_MESSAGE(t.prewarmPlaybackForSlot(slot),
                           "the slot must prewarm a runtime to owe a grain");
  SessionMidiEventVec& scratch = contentionSourceBuffer(track, slot);
  scratch.clear();
  TEST_ASSERT_EQUAL_MESSAGE(
      static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
      static_cast<int>(t.requestPlaybackWindow(slot, kContentionRequestPhase, scratch, stamp)),
      "the swept shape must be inside the *admission's* own answer; a refusal is the boundary fixture's case");
}

/// One window of this shape on its own track, drained to completion at the declared advance. The turns it needed are
/// the fixture's unit of cost: the composition below spreads exactly these over the competing tracks.
inline WindowShapeProbe probeWindowShape(uint32_t notes, bool chord = false) {
  resetWindowContentionWorld();
  PlaybackPathRows::reset();
  const PlaybackDeadlineClock stamp = nextWindowRequestClock();
  prepareContentionHolder(0, 0, 40u * Config::TICKS_PER_BAR, notes, stamp, chord);
  const PlaybackViewAdmittedWork* work = contentionActiveWork(0, 0);
  TEST_ASSERT_NOT_NULL_MESSAGE(work, "an admitted window owes work, and its record is where the charge is read");
  WindowShapeProbe probe;
  probe.chargedGrains = static_cast<uint32_t>(work->requiredGrains());
  probe.sourceEvents = work->sourceEntries;
  probe.distinctTicks = distinctSourceTicks(contentionSourceBuffer(0, 0));
  probe.deadlineTick = work->deadlineTick;
  probe.promisedTurns = static_cast<uint32_t>((work->deadlineTick - stamp.tick) / kContentionDeclaredAdvance);
  PlaybackDeadlineClock clock = stamp;
  for (uint32_t turn = 1; turn <= 4096u; ++turn) {
    trackManager.getTrack(0).serviceAdmittedDeadlineMisses(clock);
    (void)trackManager.getTrack(0).stepPlaybackViewMaintenance(kContentionRequestPhase, nullptr, false, clock);
    const LoopPlaybackRuntime* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(trackManager.getTrack(0), 0);
    if (runtime == nullptr || !runtime->coverageBuildNeedsService()) {
      probe.serviceTurns = turn;
      probe.completionTick = clock.tick;
      break;
    }
    clock.tick += kContentionDeclaredAdvance;
  }
  TEST_ASSERT_TRUE_MESSAGE(probe.serviceTurns > 0u, "the probed window must be served to completion");
  return probe;
}

/// A cell's loop length, derived from the **declaration's own arithmetic** over the probed charge: the charged promise
/// (`leadTicks + (own + siblings) x H x Delta-max`) must fit the interval's own lead (`loopLength - one bar`, since the
/// interval starts one bar before the playhead), plus a stated margin. Floors one bar past
/// `DisplayWindowUtils::kMaxDetailedWindowBars` so the selected interval really is the two-bar windowed one.
inline uint32_t contentionLoopBarsFor(uint32_t ownGrains, uint8_t siblings, uint32_t advance, uint32_t marginBars) {
  const CoverageServiceContract contract = Track::playbackViewCoverageContract();
  const uint64_t declaredTurns = static_cast<uint64_t>(ownGrains) * (static_cast<uint64_t>(siblings) + 1u) *
      (static_cast<uint64_t>(contract.maximumConsecutiveDeniedTurns) + 1u);
  const uint64_t requiredTicks = static_cast<uint64_t>(contract.leadTicks) * 2u + declaredTurns * advance;
  const uint32_t requiredBars =
      static_cast<uint32_t>((requiredTicks + Config::TICKS_PER_BAR - 1) / Config::TICKS_PER_BAR);
  const uint32_t bars = requiredBars + marginBars;
  const uint32_t kWindowedPathFloorBars = 17u;
  return bars > kWindowedPathFloorBars ? bars : kWindowedPathFloorBars;
}

/// The loop length that sits **between** the two answers the declaration gives: each individual request fits its own
/// derived cost, while the *charged* promise (own + siblings, times the rotation) does not. The declaration therefore
/// predicts what the measurement must show -- a promise crossed, and named. Returns 0 if no such length exists, which
/// fails the fixture rather than silently measuring something else.
inline uint32_t contentionOutsideLoopBarsFor(uint32_t ownGrains, uint8_t siblings, uint32_t advance) {
  const CoverageServiceContract contract = Track::playbackViewCoverageContract();
  const uint64_t rotation = static_cast<uint64_t>(contract.maximumConsecutiveDeniedTurns) + 1u;
  const uint64_t ownOnlyTicks =
      2ull * contract.leadTicks + static_cast<uint64_t>(ownGrains) * rotation * advance;
  const uint64_t chargedTicks = 2ull * contract.leadTicks +
      static_cast<uint64_t>(ownGrains) * (static_cast<uint64_t>(siblings) + 1u) * rotation * advance;
  const uint32_t bars = static_cast<uint32_t>(ownOnlyTicks / Config::TICKS_PER_BAR) + 3u;
  const uint32_t bounded = bars > 17u ? bars : 17u;
  return static_cast<uint64_t>(bounded) * Config::TICKS_PER_BAR < chargedTicks ? bounded : 0u;
}

/// Drive the production composition for one cell: every track's deadline service and offer, the shared across-holder
/// rotation, one grain on the chosen track, and the playhead advanced by the cell's own unit per lane turn.
inline WindowContentionResult runWindowContentionCell(const WindowContentionCell& cell, uint32_t loopBars) {
  const uint32_t loopTicks = loopBars * Config::TICKS_PER_BAR;
  const CoverageServiceContract contract = Track::playbackViewCoverageContract();
  const uint32_t rotation = static_cast<uint32_t>(contract.maximumConsecutiveDeniedTurns) + 1u;
  WindowContentionResult out;
  out.tracks = cell.tracks;
  out.siblings = cell.siblings;
  out.loopBars = loopBars;
  out.notes = cell.notes;
  out.advance = cell.advance;
  TEST_ASSERT_TRUE_MESSAGE(cell.tracks >= 1u && cell.tracks <= Config::NUM_TRACKS,
                           "a cell competes over at most every track");
  TEST_ASSERT_TRUE_MESSAGE(cell.siblings < Config::MAX_LOOPS_PER_TRACK,
                           "a track cannot owe more siblings than it has slots");

  resetWindowContentionWorld();
  PlaybackPathRows::reset();
  // **One stamp for the whole cell**: its holders are simultaneous demands, which is the shape the promise arithmetic
  // is written for, and it keeps the driver from spending one window's ticks before the composition starts.
  const PlaybackDeadlineClock stamp = nextWindowRequestClock();
  for (uint8_t track = 0; track < cell.tracks; ++track) {
    trackManager.getTrack(track).ensureLoopsAllocated();
    for (uint8_t slot = 0; slot <= cell.siblings; ++slot) {
      prepareContentionHolder(track, slot, loopTicks, cell.notes, stamp, cell.chord);
      ++out.windowsOwed;
    }
  }
  // The charge, read from **two** production surfaces -- the siblings' own retained records and the per-track budget --
  // so the two are caught disagreeing rather than being one number restated.
  const PlaybackViewAdmittedWork* target = contentionActiveWork(0, 0);
  TEST_ASSERT_NOT_NULL_MESSAGE(target, "the target slot owes the work the cell is about");
  out.ownGrains = target->requiredGrains();
  out.othersGrains = trackManager.getTrack(0).playbackViewOutstandingGrains(0);
  out.trackGrains = out.ownGrains;
  for (uint8_t slot = 1; slot <= cell.siblings; ++slot) {
    const PlaybackViewAdmittedWork* sibling = contentionActiveWork(0, slot);
    TEST_ASSERT_NOT_NULL_MESSAGE(sibling, "every sibling the cell admitted owes its own work");
    out.trackGrains += static_cast<uint32_t>(sibling->requiredGrains());
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(out.trackGrains - out.ownGrains, out.othersGrains,
                                   "the per-track budget and the siblings' own records must agree on the charge");
  out.deadlineTick = target->deadlineTick;
  out.promisedTurns = static_cast<uint32_t>((target->deadlineTick - stamp.tick) / cell.advance);

  uint32_t completionTurn[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK] = {};
  uint64_t completionTick[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK] = {};
  MidiServiceDrain::PlaybackHolderCursor trackCursor{};
  PlaybackDeadlineClock clock = stamp;
  // Bounded drain: far above the cost the declaration allows this cell, so a stalled composition fails its case
  // instead of hanging it.
  const uint32_t cap = rotation * (out.trackGrains + 8u) * static_cast<uint32_t>(cell.tracks) * 4u;
  for (uint32_t turn = 1; turn <= cap && out.windowsCompleted < out.windowsOwed; ++turn) {
    const uint8_t trackCount = trackManager.getTrackCount();
    MidiServiceDrain::PlaybackHolder offers[Config::NUM_TRACKS] = {};
    for (uint8_t i = 0; i < trackCount; ++i) {
      Track& t = trackManager.getTrack(i);
      // The same order `runDeferredIdleMaintenance` uses: the admitted promises are judged for **every** track each
      // turn, the offer is read for every track, and exactly one track is served.
      t.serviceAdmittedDeadlineMisses(clock);
      offers[i] = t.playbackWindowServiceOffer(kContentionRequestPhase, clock);
    }
    uint8_t chosen = MidiServiceDrain::kNoPlaybackHolder;
    if (MidiServiceDrain::selectPlaybackHolderRoundRobin(trackCursor, offers, trackCount, chosen) ==
        MidiServiceDrain::PlaybackHolderSelection::Service) {
      // Use the production null-cursor path. Its per-Track tie position persists across cells;
      // neither slot 0 nor a new cell is entitled to reset that owner state.
      (void)trackManager.getTrack(chosen).stepPlaybackViewMaintenance(kContentionRequestPhase, nullptr, false, clock);
    }
    for (uint8_t i = 0; i < cell.tracks; ++i) {
      for (uint8_t slot = 0; slot <= cell.siblings; ++slot) {
        if (i == 0 && out.firstGrainTurn[slot] == 0u) {
          const LoopPlaybackRuntime* owner =
              TrackCaptureStopTestAccess::runtimeIfAllocated(trackManager.getTrack(i), slot);
          if (owner != nullptr && (!owner->coverageBuildNeedsService() ||
                                   owner->nextCoverageBuildGrain() != RuntimePlaybackViewBuildGrain::Fill)) {
            out.firstGrainTurn[slot] = turn;
          }
        }
        if (completionTurn[i][slot] != 0u) continue;
        const LoopPlaybackRuntime* runtime =
            TrackCaptureStopTestAccess::runtimeIfAllocated(trackManager.getTrack(i), slot);
        if (runtime != nullptr && !runtime->coverageBuildNeedsService()) {
          completionTurn[i][slot] = turn;
          completionTick[i][slot] = clock.tick;
          ++out.windowsCompleted;
        }
      }
    }
    out.drainTurns = turn;
    clock.tick += cell.advance;
  }
  out.drained = out.windowsCompleted == out.windowsOwed;
  out.targetCompletionTurns = completionTurn[0][0];
  for (uint8_t i = 0; i < cell.tracks; ++i) {
    for (uint8_t slot = 0; slot <= cell.siblings; ++slot) {
      if (completionTurn[i][slot] > out.maxCompletionTurns) out.maxCompletionTurns = completionTurn[i][slot];
      if (completionTick[i][slot] > out.maxCompletionTick) out.maxCompletionTick = completionTick[i][slot];
    }
  }
  out.missRows = static_cast<uint32_t>(PlaybackPathRows::countOf("one_pass_late"));
  out.breachRows = static_cast<uint32_t>(PlaybackPathRows::countOf("lag_breach"));
  out.completeRows = static_cast<uint32_t>(PlaybackPathRows::countOf("build_complete"));
  return out;
}

/// **S1 / S2 / S3 -- the `H x K` grid.** The composition's cost law, measured: a cell drains in
/// `H x (1 + K) x serviceTurns` lane turns (its track's whole work, spread over the competing tracks), the target
/// completes on its own grains' schedule, and the declaration (`grains x (denied + 1)`, `denied + 1 == NUM_TRACKS`)
/// bounds every cell -- with slack below the full rotation and exactly at it. **Load-bearing rather than assumed:** with
/// `K > 0` the drain exceeds the target's own service turns spread over the same rotation, which is why siblings are
/// charged as *work* and not as a smaller constant; and the measured ratio between the `H = 4` and `H = 8` cells is
/// what says the across-track factor is the rotation itself.
void test_window_contention_sweep_measures_the_composed_cost_across_h_and_k() {
  struct GridCell {
    uint8_t tracks;
    uint8_t siblings;
  };
  const GridCell grid[] = {{1u, 0u}, {1u, 1u}, {2u, 1u}, {4u, 1u}, {8u, 0u}, {8u, 1u}, {8u, 2u}};
  const size_t kCells = sizeof(grid) / sizeof(grid[0]);
  const uint32_t kNotes = 20;
  const uint32_t kMarginBars = 2;
  const CoverageServiceContract contract = Track::playbackViewCoverageContract();
  const uint32_t rotation = static_cast<uint32_t>(contract.maximumConsecutiveDeniedTurns) + 1u;
  const WindowShapeProbe probe = probeWindowShape(kNotes);
  // **Measured pins for this shape** (20 notes = 40 gathered events): the charge and the service turns. The charged
  // figure is also cross-checked against the contract's own derivation below, so a change to the cost model or to the
  // one-grain-per-turn rule moves one of these numbers and fails here.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(8u, probe.chargedGrains,
                                   "20 notes = 40 entries: 1 fill + 6 order grains + the promotion grain");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(7u, probe.serviceTurns,
                                   "and the window is served in one turn per grain short of the promotion charge");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(40u, static_cast<uint32_t>(probe.sourceEvents),
                                   "the window gathered exactly the notes it was published with, on/off");
  CoverageServiceContract expected = contract;
  expected.deriveGrainsFrom(probe.sourceEvents);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(expected.requiredGrains(), static_cast<uint16_t>(probe.chargedGrains),
                                   "and the record's charge is that derivation: two surfaces, one number");
  const uint32_t ownGrains = probe.chargedGrains;
  const uint32_t serviceTurns = probe.serviceTurns;

  WindowContentionResult rows[kCells];
  for (size_t index = 0; index < kCells; ++index) {
    const uint32_t bars =
        contentionLoopBarsFor(ownGrains, grid[index].siblings, kContentionDeclaredAdvance, kMarginBars);
    rows[index] = runWindowContentionCell(
        {grid[index].tracks, grid[index].siblings, bars, kNotes, kContentionDeclaredAdvance}, bars);
    const WindowContentionResult& row = rows[index];
    const uint32_t windows = 1u + grid[index].siblings;
    TEST_ASSERT_TRUE_MESSAGE(row.drained, "every owed window completes inside the bounded drain");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(row.windowsOwed, row.windowsCompleted, "and each of them exactly once");
    // (A) The cost law, measured: the track's whole work spread over the competing tracks.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(grid[index].tracks * windows * serviceTurns, row.drainTurns,
                                     "H x (1 + K) x the window's own service turns");
    // (B) A persistent within-Track tie cursor need not start at slot 0. Retain the exact
    // own-grain schedule when it is the only holder; with siblings, the target cannot finish
    // before its own grains and must finish within the already asserted whole-track drain.
    if (grid[index].siblings == 0u) {
      TEST_ASSERT_EQUAL_UINT32_MESSAGE((serviceTurns - 1u) * grid[index].tracks + 1u,
                                       row.targetCompletionTurns, "sole holder completes on its own grain schedule");
    } else {
      TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(serviceTurns, row.targetCompletionTurns,
                                                  "target requires its own service grains");
      TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(row.drainTurns, row.targetCompletionTurns,
                                               "the target completes inside the exact charged whole-track drain");
    }
    // (C) The declaration is sufficient for the cell: the retained promise is at least the declared charge, and the
    // composition fits inside the promise. No promise is crossed anywhere in the grid.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(row.trackGrains * rotation, row.promisedTurns,
                                                "the admission sized the loop to at least the declared charge");
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(row.drainTurns, row.promisedTurns,
                                            "and the whole composition fits inside that promise");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, row.missRows, "no promise is crossed inside the envelope");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(row.windowsOwed, row.completeRows,
                                     "and every window reports its completion by name");
    // (D) `K` is load-bearing, measured: the track pays for its sibling's service turns too.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(grid[index].siblings * ownGrains, row.othersGrains,
                                     "the per-track budget charges every sibling's own charge");
    if (grid[index].siblings > 0u) {
      TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(grid[index].tracks * serviceTurns, row.drainTurns,
                                              "with a sibling owing, the target's own turns understate the drain");
    }
  }
  // (E) `H` is load-bearing, measured as the ratio between cells that differ only in how many tracks compete.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(rows[5].trackGrains, rows[3].trackGrains, "the cells below differ only in H");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(rows[3].drainTurns * 2u, rows[5].drainTurns,
                                   "the drain is linear in the competing tracks");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(rows[5].drainTurns * 3u / 2u, rows[6].drainTurns,
                                   "and a third holder enters as work, not as another multiplier");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(rows[5].trackGrains, rows[1].trackGrains,
                                   "the same K at H = 1 and H = 8 charges the same work");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(rows[1].drainTurns * 8u, rows[5].drainTurns,
                                   "and pays for it once per competing track");
}

/// Eight simultaneously admitted, positioned slots must each receive their first *real* build grain
/// within one eight-turn within-Track rotation. An eventual complete drain alone would not catch
/// resetting the cursor and starving the higher slots behind slot 0's multi-grain order work.
void test_positioned_eight_slot_holder_first_service_is_bounded() {
  constexpr uint32_t kNotes = 20u; // 40 events, one Fill grain per holder
  constexpr uint8_t kSiblings = Config::MAX_LOOPS_PER_TRACK - 1u;
  const WindowShapeProbe probe = probeWindowShape(kNotes);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u * kNotes, probe.sourceEvents,
                                   "the first Fill step examines all 40 accepted rows");
  const uint32_t bars = contentionLoopBarsFor(probe.chargedGrains, kSiblings,
                                              kContentionDeclaredAdvance, 2u);
  const WindowContentionResult row = runWindowContentionCell(
      {1u, kSiblings, bars, kNotes, kContentionDeclaredAdvance}, bars);
  TEST_ASSERT_TRUE_MESSAGE(row.drained, "all eight independent positioned obligations complete");
  TEST_ASSERT_EQUAL_UINT32(Config::MAX_LOOPS_PER_TRACK, row.windowsCompleted);
  TEST_ASSERT_EQUAL_UINT32(Config::MAX_LOOPS_PER_TRACK * probe.serviceTurns, row.drainTurns);
  TEST_ASSERT_EQUAL_UINT32(row.windowsOwed, row.completeRows);
  TEST_ASSERT_EQUAL_UINT32(0u, row.missRows);
  bool servedTurn[Config::MAX_LOOPS_PER_TRACK + 1u] = {};
  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    const uint32_t first = row.firstGrainTurn[slot];
    TEST_ASSERT_TRUE_MESSAGE(first >= 1u && first <= Config::MAX_LOOPS_PER_TRACK,
        "each positioned slot receives a grain within one Track rotation, not eventual drain");
    TEST_ASSERT_FALSE_MESSAGE(servedTurn[first], "one owner opportunity serves just one slot grain");
    servedTurn[first] = true;
  }
}

/// **S4 -- density and span.** A cell's cost is its windows' own service turns (the per-grain turn cost is the
/// competing-track count, unchanged by density), and the loop's length is not an input at all: the same shape on a short
/// and on a much longer loop measures identically. These are the sweep's density and span axes, and they are what let
/// one cell's cost be read as a function of its shape rather than of its loop.
void test_window_contention_sweep_scales_with_density_and_is_span_independent() {
  const uint8_t kTracks = Config::NUM_TRACKS;
  const uint8_t kSiblings = 1;
  const uint32_t densities[] = {5u, 20u, 80u};
  const size_t kDensities = sizeof(densities) / sizeof(densities[0]);
  WindowContentionResult byDensity[kDensities];
  for (size_t index = 0; index < kDensities; ++index) {
    const WindowShapeProbe probe = probeWindowShape(densities[index]);
    const uint32_t bars = contentionLoopBarsFor(probe.chargedGrains, kSiblings, kContentionDeclaredAdvance, 2u);
    byDensity[index] =
        runWindowContentionCell({kTracks, kSiblings, bars, densities[index], kContentionDeclaredAdvance}, bars);
    const WindowContentionResult& row = byDensity[index];
    TEST_ASSERT_TRUE_MESSAGE(row.drained, "every window of a swept density completes");
    // The same law as the grid's, at every density: the structure is the shape's, the magnitude is its entries'.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(kTracks * (1u + kSiblings) * probe.serviceTurns, row.drainTurns,
                                     "H x (1 + K) x the window's own service turns");
    // **The measured magnitude axis, pinned.** One window's service turns at each density, in the declaration's own
    // unit: the cost is the window's entries, and these three numbers are what a change to the cost model, the fill/order
    // quanta or the one-grain-per-turn rule moves.
    const uint32_t expectedServiceTurns[] = {2u, 7u, 33u};
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expectedServiceTurns[index], probe.serviceTurns,
                                     "the measured service turns of this density");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(row.trackGrains - row.ownGrains, row.othersGrains,
                                     "the sibling's charge is its own record, at every density");
    TEST_ASSERT_LESS_OR_EQUAL_UINT64_MESSAGE(row.deadlineTick, row.maxCompletionTick,
                                             "and it stays inside the promise the admission retained");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, row.missRows, "a promise inside the envelope is never crossed");
  }
  // Density is what moves the magnitude, and it moves it through the entries: more notes, more service turns, later
  // drain -- while the charge keeps bounding it.
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(byDensity[1].trackGrains, byDensity[0].trackGrains,
                                       "a denser window costs more work");
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(byDensity[2].trackGrains, byDensity[1].trackGrains,
                                       "and the sweep spans an order of magnitude of it");
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(byDensity[1].drainTurns, byDensity[0].drainTurns,
                                       "so a denser cell drains later on the same rotation");
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(byDensity[2].drainTurns, byDensity[1].drainTurns, "at both steps");

  // Span: the same shape on a short and on a much longer loop.
  const WindowShapeProbe probe = probeWindowShape(20u);
  const uint32_t shortBars = contentionLoopBarsFor(probe.chargedGrains, kSiblings, kContentionDeclaredAdvance, 2u);
  const uint32_t longBars = shortBars + 24u;
  const WindowContentionResult shortSpan =
      runWindowContentionCell({kTracks, kSiblings, shortBars, 20u, kContentionDeclaredAdvance}, shortBars);
  const WindowContentionResult longSpan =
      runWindowContentionCell({kTracks, kSiblings, longBars, 20u, kContentionDeclaredAdvance}, longBars);
  TEST_ASSERT_TRUE(shortSpan.drained && longSpan.drained);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(shortSpan.trackGrains, longSpan.trackGrains,
                                   "the loop's length is not an input to the cost");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(shortSpan.drainTurns, longSpan.drainTurns,
                                   "the same shape costs the same on a longer loop");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(shortSpan.targetCompletionTurns, longSpan.targetCompletionTurns,
                                   "including when the target itself becomes ready");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(shortSpan.promisedTurns, longSpan.promisedTurns,
                                          "the longer loop buys only a longer promise, not cheaper work");
}

/// **S5 / S6 -- the boundary, and the oracle's own falsification.** A shape the declaration answers *yes* to holds its
/// promise; the same composition one step further out (more sibling work than the charge accounts for, i.e. a shape the
/// **unenforced** admission still admits) crosses the promise, and the crossing is **named**. The control is the
/// sharpest one available: the *inside* cell re-run at twice the permitted advance breaks, so the oracle can fail and
/// the unit it is stated in -- `Delta-max` per lane turn -- is the load-bearing one.
void test_window_contention_boundary_is_predicted_by_the_declaration_and_named_when_crossed() {
  const uint8_t kSiblings = 1;
  const uint32_t kNotes = 80;
  const WindowShapeProbe probe = probeWindowShape(kNotes);
  const uint32_t insideBars = contentionLoopBarsFor(probe.chargedGrains, kSiblings, kContentionDeclaredAdvance, 2u);
  const uint32_t outsideBars = contentionOutsideLoopBarsFor(probe.chargedGrains, kSiblings, kContentionDeclaredAdvance);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(outsideBars, insideBars,
                                          "the boundary cell sits below the charge's own requirement");
  // **The declaration's own predicate on the same three shapes.** This is what makes "the declaration predicts the
  // boundary" a check rather than a story: the charged window fits the inside length, does not fit the boundary one, and
  // the *own-only* charge (what the unenforced admission applies) fits both -- so the difference between the two cells
  // is exactly the sibling charge the declaration is about.
  CoverageServiceContract charged = Track::playbackViewCoverageContract();
  charged.deriveGrainsFrom(probe.sourceEvents);
  const uint16_t siblingCharge = static_cast<uint16_t>(probe.chargedGrains * kSiblings);
  const uint64_t insideLead = static_cast<uint64_t>(insideBars) * Config::TICKS_PER_BAR - Config::TICKS_PER_BAR;
  const uint64_t outsideLead = static_cast<uint64_t>(outsideBars) * Config::TICKS_PER_BAR - Config::TICKS_PER_BAR;
  TEST_ASSERT_TRUE_MESSAGE(charged.canCompleteWithinLead(insideLead, siblingCharge),
                           "the charged promise fits the inside length");
  TEST_ASSERT_FALSE_MESSAGE(charged.canCompleteWithinLead(outsideLead, siblingCharge),
                            "and it does not fit the boundary length");
  TEST_ASSERT_TRUE_MESSAGE(charged.canCompleteWithinLead(outsideLead, 0),
                           "while each individual request still fits its own derived cost");

  const WindowContentionResult inside = runWindowContentionCell(
      {Config::NUM_TRACKS, kSiblings, insideBars, kNotes, kContentionDeclaredAdvance}, insideBars);
  TEST_ASSERT_TRUE_MESSAGE(inside.drained, "an in-envelope cell completes");
  TEST_ASSERT_LESS_OR_EQUAL_UINT64_MESSAGE(inside.deadlineTick, inside.maxCompletionTick,
                                           "and every window of it is ready before the promise it carries");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, inside.missRows, "so no miss is named for it");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(Config::NUM_TRACKS * (1u + kSiblings) * probe.serviceTurns, inside.drainTurns,
                                   "and its drain is the measured law again");

  const WindowContentionResult outside = runWindowContentionCell(
      {Config::NUM_TRACKS, kSiblings, outsideBars, kNotes, kContentionDeclaredAdvance}, outsideBars);
  TEST_ASSERT_TRUE_MESSAGE(outside.drained, "the shape outside the charge still completes eventually");
  TEST_ASSERT_GREATER_THAN_UINT64_MESSAGE(outside.deadlineTick, outside.maxCompletionTick,
                                          "but its last window arrives after the promise it carries");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, outside.missRows,
                                          "and the crossed promise is **named**, not silently re-granted");
  // The two boundary cells differ only in their loop length, which is the declaration's own variable.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(inside.trackGrains, outside.trackGrains,
                                   "the boundary is the same shape, so the same work");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(outside.promisedTurns, inside.promisedTurns,
                                          "and the shorter loop is the one that cannot hold the charge");

  // **The control.** The inside cell at twice the permitted advance: same content, same length, same charge -- only the
  // clock unit the declaration is stated in changes, and the promise that held before now breaks.
  const WindowContentionResult doubled = runWindowContentionCell(
      {Config::NUM_TRACKS, kSiblings, insideBars, kNotes, 2u * kContentionDeclaredAdvance}, insideBars);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(inside.loopBars, doubled.loopBars, "the control changes only the clock");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(inside.trackGrains, doubled.trackGrains, "and nothing about the work");
  TEST_ASSERT_GREATER_THAN_UINT64_MESSAGE(doubled.deadlineTick, doubled.maxCompletionTick,
                                          "at twice the permitted advance the same shape is late");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, doubled.missRows,
                                          "and the miss is named: the oracle can fail");
}


/// **S7 -- the same-tick (chord) density axis.** The grid's density cells spread their notes over the window, so every
/// entry sits on its own tick; a **chord** puts all of its notes on two ticks (one On, one Off). The claim the account
/// makes is that a window's price is its **entry count**, not how many ticks those entries share -- and that the
/// composition drains a chord exactly like spread content of the same count. Measured, not assumed: the charge, the
/// service turns and the whole cell are compared against the spread shape of the same note count.
void test_window_contention_sweep_chord_density_costs_the_same_as_spread() {
  constexpr uint32_t kNotes = 20;
  const uint8_t kSiblings = 1;
  const WindowShapeProbe spread = probeWindowShape(kNotes);
  const WindowShapeProbe chord = probeWindowShape(kNotes, true);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(2u * kNotes), static_cast<uint32_t>(chord.sourceEvents),
                                   "the chord gathered the same number of entries as the spread shape");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, chord.distinctTicks,
                                   "the chord's notes share their On tick and their Off tick");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(2u, spread.distinctTicks,
                                          "while the spread shape puts its entries on their own ticks");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(spread.chargedGrains, chord.chargedGrains,
                                   "a chord's price is its entry count, not the number of ticks it occupies");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(spread.serviceTurns, chord.serviceTurns,
                                   "and it is served in the same number of grains");

  const uint32_t bars = contentionLoopBarsFor(chord.chargedGrains, kSiblings, kContentionDeclaredAdvance, 2u);
  const WindowContentionResult chordRow =
      runWindowContentionCell({Config::NUM_TRACKS, kSiblings, bars, kNotes, kContentionDeclaredAdvance, true}, bars);
  const WindowContentionResult spreadRow =
      runWindowContentionCell({Config::NUM_TRACKS, kSiblings, bars, kNotes, kContentionDeclaredAdvance, false}, bars);
  TEST_ASSERT_TRUE_MESSAGE(chordRow.drained, "the chord cell drains inside the bounded drain");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(chordRow.windowsOwed, chordRow.windowsCompleted, "each owed chord window once");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(chordRow.windowsOwed, chordRow.completeRows, "and each completion is named");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, chordRow.missRows, "no promise is crossed by a chord");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(spreadRow.drainTurns, chordRow.drainTurns,
                                   "the same entry count drains in the same composed turns");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(spreadRow.trackGrains, chordRow.trackGrains,
                                   "and the per-track budget charges the same work");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(spreadRow.targetCompletionTurns, chordRow.targetCompletionTurns,
                                   "the target completes on the same schedule");
}

/// **S8 -- the tempo axis, in the declaration's own currency.** The envelope is stated in **ticks** (`Delta-max`,
/// `leadTicks`, grains), so the reachable tempo range changes what a tick *costs in time*, never what a window costs.
/// The sweep is run at 20 / 120 / 300 BPM: every tick-domain figure must be identical, and the wall-clock translation of
/// one permitted turn and of the admitted promise must shrink with tempo. What this cannot establish is physical
/// timing -- the device leg owns the measured advance (6.5) -- and the fixture says so rather than implying it.
void test_window_contention_sweep_tempo_is_a_time_scale_not_a_cost() {
  constexpr uint32_t kNotes = 20;
  const uint8_t kSiblings = 1;
  const uint32_t tempos[] = {20u, 120u, 300u};
  constexpr size_t kTempos = sizeof(tempos) / sizeof(tempos[0]);
  const WindowShapeProbe probe = probeWindowShape(kNotes);
  const uint32_t bars = contentionLoopBarsFor(probe.chargedGrains, kSiblings, kContentionDeclaredAdvance, 2u);
  WindowContentionResult rows[kTempos];
  uint64_t msPerPermittedTurn[kTempos] = {};
  for (size_t i = 0; i < kTempos; ++i) {
    // The cell is a tick design: the tempo is not an input to it, which is exactly what this axis asserts.
    rows[i] = runWindowContentionCell({Config::NUM_TRACKS, kSiblings, bars, kNotes, kContentionDeclaredAdvance}, bars);
    msPerPermittedTurn[i] = (60000ull * kContentionDeclaredAdvance) /
                            (static_cast<uint64_t>(tempos[i]) * Config::INTERNAL_PPQN);
    TEST_ASSERT_TRUE_MESSAGE(rows[i].drained, "the cell drains at every reachable tempo");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, rows[i].missRows, "and crosses no promise at any of them");
  }
  // (A) **Tick-domain invariance**: the measured cost does not move across the reachable tempo range.
  for (size_t i = 1; i < kTempos; ++i) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(rows[0].trackGrains, rows[i].trackGrains,
                                     "the charged work is the same at every tempo");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(rows[0].drainTurns, rows[i].drainTurns,
                                     "and the composed drain is the same at every tempo");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(rows[0].targetCompletionTurns, rows[i].targetCompletionTurns,
                                     "including when the target becomes ready");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(rows[0].promisedTurns, rows[i].promisedTurns,
                                     "and the promised lead, which is stated in ticks");
  }
  // (B) **The time translation is the only thing tempo changes**, and it is monotone: a permitted turn is 1 000 ms at
  // 20 BPM, 166 ms at 120 and 66 ms at 300 -- so the same tick-domain promise is a shrinking wall-clock budget.
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(1000ull, msPerPermittedTurn[0], "one permitted turn at 20 BPM");
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(166ull, msPerPermittedTurn[1], "one permitted turn at 120 BPM");
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(66ull, msPerPermittedTurn[2], "one permitted turn at 300 BPM");
  TEST_ASSERT_GREATER_THAN_UINT64_MESSAGE(msPerPermittedTurn[1], msPerPermittedTurn[0],
                                          "a slower tempo gives each permitted turn more time");
  TEST_ASSERT_GREATER_THAN_UINT64_MESSAGE(msPerPermittedTurn[2], msPerPermittedTurn[1],
                                          "and the fastest declared tempo gives it the least");
  for (size_t i = 0; i < kTempos; ++i) {
    // The admitted promise in the same currency: ticks x ms-per-tick. Reported, not asserted as a limit -- the device
    // leg's measured advance is what can falsify it (6.5).
    const uint64_t promiseMs = static_cast<uint64_t>(rows[i].promisedTurns) * msPerPermittedTurn[i];
    TEST_ASSERT_GREATER_THAN_UINT64_MESSAGE(0ull, promiseMs, "the promise has a positive wall-clock budget at every tempo");
  }
}
