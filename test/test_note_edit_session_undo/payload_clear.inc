using PayloadClearResult = EditSession::PayloadClearResult;

static void seedSessionPayload(EditSession& session) {
  session.active = true;
  session.sessionType = EditSessionType::Note;
  session.replaceEditPassOnClose = true;
  session.openedOnCurrentSnapshot = true;
  session.openedSnapshotRevision = 42;
  session.durableCheckpointCreated = true;
  auto& store = session.store.mutStore();
  for (uint32_t tick = 0; tick < 257; ++tick)
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(tick, 1, 60, 100)));
  session.store.mutEvents().push_back(MidiEvent::NoteOn(999, 1, 70, 90));
  for (NoteId id = 1; id <= 5; ++id) {
    const NoteBaseline span{61, 99, id * 10, id * 10 + 5};
    session.noteEditCurrentState.upsertRow(id, span, span, NoteEditPresenceType::Visible);
  }
  TEST_ASSERT_TRUE(session.noteEditCurrentState.rows().bucket_count() > 2);
  TEST_ASSERT_TRUE(session.undoStack.pushEntry(makeNestedClearEntry()));
  session.focus.active = true;
  session.focus.movingNoteId = 77;
  session.focus.baselineMap.rehash(257);
  session.focus.overlapNotes.rehash(257);
  for (NoteId id = 1; id <= 3; ++id)
    session.focus.baselineMap.emplace(id, NoteBaseline{62, 98, id * 10, id * 10 + 5});
  for (NoteId id = 4; id <= 5; ++id) {
    OverlapNote row; row.noteId = id;
    session.focus.overlapNotes.emplace(id, row);
  }
  session.editPassIds = {100, 101};
  session.durableCheckpointedEditPassIds = {100};
  for (unsigned count : {3u, 2u}) {
    EditPass row; row.id = 200 + count;
    for (unsigned event = 0; event < count; ++event)
      row.addedEvents.push_back(MidiEvent::ControlChange(event, 1, 7, 64));
    session.applyOwnedEditPassRows.push_back(std::move(row));
  }
}

static size_t drainSessionPayload(EditSession& session,
                                 const EditSession::PayloadClearReceipt& receipt) {
  for (size_t grant = 1; grant <= 4096; ++grant) {
    const auto result = session.stepPayloadClear(receipt);
    if (result == PayloadClearResult::Complete) return grant;
    TEST_ASSERT_TRUE_MESSAGE(result == PayloadClearResult::Progress,
                             "session payload retirement stopped before completion");
  }
  TEST_FAIL_MESSAGE("session payload retirement exceeded4096 grants");
  return 0;
}

static void test_session_payload_clear_withheld_and_exact_composed_grants() {
  LoopEventStore::resetPoolForTests();
  EditSession session;
  seedSessionPayload(session);
  TEST_ASSERT_EQUAL(94u, session.payloadClearInventoryForTest());
  const auto receipt = session.beginPayloadClear();
  TEST_ASSERT_TRUE(receipt.owner == &session);
  TEST_ASSERT_NOT_EQUAL(0, receipt.token);
  TEST_ASSERT_FALSE(session.active);
  TEST_ASSERT_FALSE(session.focus.active);
  TEST_ASSERT_FALSE(session.replaceEditPassOnClose);
  TEST_ASSERT_FALSE(session.openedOnCurrentSnapshot);
  TEST_ASSERT_EQUAL(0, session.openedSnapshotRevision);
  TEST_ASSERT_FALSE(session.durableCheckpointCreated);
  TEST_ASSERT_FALSE(session.undoStack.canUndo());
  TEST_ASSERT_EQUAL(257u, session.store.size());
  TEST_ASSERT_EQUAL(258u, session.store.eventValueCount());
  for (unsigned query = 0; query < 8; ++query) {
    TEST_ASSERT_TRUE(session.payloadClearResult(receipt) == PayloadClearResult::Progress);
    TEST_ASSERT_EQUAL(receipt.token, session.beginPayloadClear().token);
    TEST_ASSERT_EQUAL(94u, session.payloadClearInventoryForTest());
  }
  unsigned retired = 0, transitions = 0;
  for (unsigned grant = 1; grant <= 102; ++grant) {
    const size_t before = session.payloadClearInventoryForTest();
    const auto result = session.stepPayloadClear(receipt);
    const size_t after = session.payloadClearInventoryForTest();
    TEST_ASSERT_TRUE_MESSAGE(after == before || after + 1 == before,
                             "one grant retired more than one payload/capacity unit");
    if (after == before) ++transitions; else ++retired;
    TEST_ASSERT_TRUE(result == (grant == 102 ? PayloadClearResult::Complete
                                           : PayloadClearResult::Progress));
  }
  TEST_ASSERT_EQUAL(94u, retired);
  TEST_ASSERT_EQUAL(8u, transitions);
  TEST_ASSERT_EQUAL(0u, session.payloadClearInventoryForTest());
  TEST_ASSERT_FALSE(session.store.hasEventsCacheForTest());
  TEST_ASSERT_TRUE(session.stepPayloadClear(receipt) == PayloadClearResult::Complete);
  TEST_ASSERT_TRUE(session.payloadClearResult(receipt) == PayloadClearResult::Complete);
}

static void test_session_payload_clear_shared_source_unchanged() {
  LoopEventStore::resetPoolForTests();
  EditSession session;
  auto& store = session.store.mutStore();
  for (uint32_t tick = 0; tick < 257; ++tick)
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(tick, 1, 60, 100)));
  session.store.mutEvents().push_back(MidiEvent::NoteOn(999, 1, 70, 90));
  auto source = session.store.shareForSnapshot();
  const auto ids = source->chunkIds();
  const auto free = LoopEventStore::freeChunkCount();
  const auto receipt = session.beginPayloadClear();
  TEST_ASSERT_EQUAL(2, source.use_count());
  TEST_ASSERT_TRUE(session.stepPayloadClear(receipt) == PayloadClearResult::Progress);
  TEST_ASSERT_EQUAL(2, source.use_count());
  TEST_ASSERT_FALSE(session.store.hasEventsCacheForTest());
  TEST_ASSERT_FALSE(session.store.isEventsDirty());
  TEST_ASSERT_EQUAL(257u, session.store.size());
  TEST_ASSERT_EQUAL(257u, source->size());
  TEST_ASSERT_EQUAL(256u, source->at(256).tick);
  TEST_ASSERT_EQUAL(free, LoopEventStore::freeChunkCount());
  for (auto id : ids) TEST_ASSERT_EQUAL(1, LoopEventStore::chunkReferenceCount(id));
  TEST_ASSERT_TRUE(session.stepPayloadClear(receipt) == PayloadClearResult::Progress);
  TEST_ASSERT_EQUAL(1, source.use_count());
  TEST_ASSERT_TRUE(session.store.empty());
  for (unsigned grant = 0; grant < 8; ++grant) {
    const auto result = session.stepPayloadClear(receipt);
    TEST_ASSERT_EQUAL(257u, source->size());
    TEST_ASSERT_EQUAL(0u, source->at(0).tick);
    TEST_ASSERT_EQUAL(256u, source->at(256).tick);
    TEST_ASSERT_EQUAL(free, LoopEventStore::freeChunkCount());
    for (auto id : ids) TEST_ASSERT_EQUAL(1, LoopEventStore::chunkReferenceCount(id));
    if (grant == 7) TEST_ASSERT_TRUE(result == PayloadClearResult::Complete);
    else TEST_ASSERT_TRUE(result == PayloadClearResult::Progress);
  }
  source.reset();
  LoopEventStore::releaseChunkRefs(ids);
}

static void test_session_payload_clear_receipt_rejects_foreign_zero_and_reuse() {
  LoopEventStore::resetPoolForTests();
  EditSession first, second;
  seedSessionPayload(first);
  seedSessionPayload(second);
  const auto old = first.beginPayloadClear();
  const auto other = second.beginPayloadClear();
  TEST_ASSERT_TRUE(second.stepPayloadClear(old) == PayloadClearResult::Superseded);
  TEST_ASSERT_TRUE(first.stepPayloadClear({}) == PayloadClearResult::Superseded);
  TEST_ASSERT_EQUAL(94u, first.payloadClearInventoryForTest());
  TEST_ASSERT_EQUAL(94u, second.payloadClearInventoryForTest());
  TEST_ASSERT_TRUE(first.stepPayloadClear(old) == PayloadClearResult::Progress);
  TEST_ASSERT_EQUAL(93u, first.payloadClearInventoryForTest());
  first.cancelPayloadClear();
  TEST_ASSERT_TRUE(first.payloadClearResult(old) == PayloadClearResult::Superseded);
  const auto resumed = first.beginPayloadClear();
  TEST_ASSERT_NOT_EQUAL(old.token, resumed.token);
  TEST_ASSERT_TRUE(first.stepPayloadClear(old) == PayloadClearResult::Superseded);
  TEST_ASSERT_EQUAL(93u, first.payloadClearInventoryForTest());
  drainSessionPayload(first, resumed);
  seedSessionPayload(first);
  const auto successor = first.beginPayloadClear();
  TEST_ASSERT_NOT_EQUAL(resumed.token, successor.token);
  TEST_ASSERT_TRUE(first.stepPayloadClear(resumed) == PayloadClearResult::Superseded);
  TEST_ASSERT_EQUAL(94u, first.payloadClearInventoryForTest());
  drainSessionPayload(first, successor);
  drainSessionPayload(second, other);
}

static void test_session_payload_clear_empty_completion_is_separate() {
  EditSession session;
  const auto receipt = session.beginPayloadClear();
  TEST_ASSERT_EQUAL(0u, session.payloadClearInventoryForTest());
  TEST_ASSERT_EQUAL(8u, drainSessionPayload(session, receipt));
  TEST_ASSERT_TRUE(session.stepPayloadClear(receipt) == PayloadClearResult::Complete);
  TEST_ASSERT_EQUAL(0u, session.payloadClearInventoryForTest());
  TEST_ASSERT_FALSE(session.store.hasEventsCacheForTest());
}

static void test_session_payload_clear_interrupted_child_undo_stops_later_phases() {
  LoopEventStore::resetPoolForTests();
  EditSession session;
  session.focus.baselineMap.emplace(900, NoteBaseline{60, 100, 700, 720});
  session.editPassIds.push_back(100);
  const auto receipt = session.beginPayloadClear();
  session.undoStack.clear();
  TEST_ASSERT_TRUE(session.stepPayloadClear(receipt) == PayloadClearResult::Progress);
  TEST_ASSERT_TRUE(session.stepPayloadClear(receipt) == PayloadClearResult::Progress);
  const auto inventory = session.payloadClearInventoryForTest();
  TEST_ASSERT_TRUE(session.stepPayloadClear(receipt) == PayloadClearResult::Superseded);
  TEST_ASSERT_TRUE(session.stepPayloadClear(receipt) == PayloadClearResult::Superseded);
  TEST_ASSERT_EQUAL(inventory, session.payloadClearInventoryForTest());
  TEST_ASSERT_EQUAL(700u, session.focus.baselineMap.at(900).startTick);
  TEST_ASSERT_EQUAL(100u, session.editPassIds.front());
  session.cancelPayloadClear();
  drainSessionPayload(session, session.beginPayloadClear());

  // Child completion cannot authorize destruction of a newly admitted entry.
  EditSession replaced;
  const auto replacingReceipt = replaced.beginPayloadClear();
  for (unsigned turn = 0; turn < 3; ++turn)
    TEST_ASSERT_TRUE(replaced.stepPayloadClear(replacingReceipt) == PayloadClearResult::Progress);
  TEST_ASSERT_TRUE(replaced.undoStack.pushEntry(makeNestedClearEntry()));
  TEST_ASSERT_TRUE(replaced.stepPayloadClear(replacingReceipt) == PayloadClearResult::Superseded);
  TEST_ASSERT_EQUAL(63u, replaced.undoStack.clearInventoryForTest());
  TEST_ASSERT_EQUAL(1u, replaced.undoStack.clearEntryCountForTest());
  replaced.cancelPayloadClear();
  drainSessionPayload(replaced, replaced.beginPayloadClear());
}

static void test_session_payload_clear_stamped_pool_reset_preserves_recycled_source() {
  LoopEventStore::resetPoolForTests();
  EditSession session;
  auto& store = session.store.mutStore();
  TEST_ASSERT_TRUE(store.reserveCaptureTail());
  for (uint32_t tick = 0; tick < 257; ++tick)
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(tick, 1, 60, 100)));
  const auto receipt = session.beginPayloadClear();
  LoopEventStore::resetPoolForTests();
  LoopEventStore recycled;
  TEST_ASSERT_TRUE(recycled.append(MidiEvent::NoteOn(42, 1, 72, 99)));
  const auto id = recycled.chunkIds().front();
  for (unsigned grant = 0; grant < 16; ++grant) {
    const auto result = session.stepPayloadClear(receipt);
    TEST_ASSERT_EQUAL(1, LoopEventStore::chunkReferenceCount(id));
    TEST_ASSERT_EQUAL(42u, recycled.at(0).tick);
    if (result == PayloadClearResult::Complete) { recycled.clear(); return; }
    TEST_ASSERT_TRUE(result == PayloadClearResult::Progress);
  }
  TEST_FAIL_MESSAGE("stamped session clear did not finish within16 grants");
}
