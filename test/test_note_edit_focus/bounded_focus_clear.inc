namespace {
void seedRetiringFocus(NoteEditFocus& focus) {
  focus.active = true;
  focus.movingNoteId = 77;
  focus.commitBaseline = {61, 90, 100, 200};
  focus.last = {62, 91, 120, 220};
  focus.movingNoteRange = {120, 220};
  focus.baselineMap.rehash(257);
  focus.overlapNotes.rehash(257);
  for (NoteId id = 1; id <= 3; ++id)
    focus.baselineMap.emplace(id, NoteBaseline{60, 99, id * 10, id * 10 + 5});
  for (NoteId id = 4; id <= 5; ++id) {
    OverlapNote row;
    row.noteId = id;
    row.baseline = {64, 98, id * 10, id * 10 + 5};
    row.state = OverlapNoteStoreState::Shortened;
    row.shortenedEndTick = id * 10 + 3;
    row.preCommitEmitted = true;
    focus.overlapNotes.emplace(id, row);
  }
}
void assertRetiredFocusScalars(const NoteEditFocus& focus) {
  TEST_ASSERT_FALSE(focus.active);
  TEST_ASSERT_EQUAL(kInvalidNoteId, focus.movingNoteId);
  for (const auto* span : {&focus.commitBaseline, &focus.last}) {
    TEST_ASSERT_EQUAL(0, span->pitch);
    TEST_ASSERT_EQUAL(64, span->velocity);
    TEST_ASSERT_EQUAL(0u, span->startTick);
    TEST_ASSERT_EQUAL(0u, span->endTick);
  }
  TEST_ASSERT_EQUAL(0u, focus.movingNoteRange.start);
  TEST_ASSERT_EQUAL(0u, focus.movingNoteRange.end);
}
void drainRetiringFocus(NoteEditFocus& focus) {
  for (unsigned grant = 0; grant < 32; ++grant)
    if (!focus.stepClear()) return;
  TEST_FAIL_MESSAGE("focus clear did not complete within32 grants");
}
}

void test_focus_clear_acceptance_is_scalar_only() {
  NoteEditFocus focus;
  seedRetiringFocus(focus);
  const auto baselineBuckets = focus.baselineMap.bucket_count();
  const auto overlapBuckets = focus.overlapNotes.bucket_count();
  focus.beginClear();
  assertRetiredFocusScalars(focus);
  TEST_ASSERT_EQUAL(3u, focus.baselineMap.size());
  TEST_ASSERT_EQUAL(2u, focus.overlapNotes.size());
  TEST_ASSERT_EQUAL(baselineBuckets, focus.baselineMap.bucket_count());
  TEST_ASSERT_EQUAL(overlapBuckets, focus.overlapNotes.bucket_count());
  TEST_ASSERT_EQUAL(10u, focus.baselineMap.at(1).startTick);
  TEST_ASSERT_EQUAL(43u, focus.overlapNotes.at(4).shortenedEndTick);
  TEST_ASSERT_TRUE(focus.overlapNotes.at(4).preCommitEmitted);
  focus.beginClear();
  TEST_ASSERT_EQUAL(3u, focus.baselineMap.size());
  TEST_ASSERT_EQUAL(2u, focus.overlapNotes.size());
  drainRetiringFocus(focus);
}

void test_focus_clear_one_node_or_table_per_grant() {
  NoteEditFocus focus;
  seedRetiringFocus(focus);
  focus.beginClear();
  const size_t baselineCounts[] = {2, 1, 0, 0, 0, 0, 0};
  const size_t overlapCounts[] = {2, 2, 2, 2, 1, 0, 0};
  for (size_t grant = 0; grant < 7; ++grant) {
    TEST_ASSERT_TRUE(focus.stepClear());
    TEST_ASSERT_EQUAL(baselineCounts[grant], focus.baselineMap.size());
    TEST_ASSERT_EQUAL(overlapCounts[grant], focus.overlapNotes.size());
    assertRetiredFocusScalars(focus);
    if (grant < 3) TEST_ASSERT_TRUE(focus.baselineMap.bucket_count() > 2);
    else TEST_ASSERT_TRUE(focus.baselineMap.bucket_count() <= 2);
    if (grant < 6) TEST_ASSERT_TRUE(focus.overlapNotes.bucket_count() > 2);
    else TEST_ASSERT_TRUE(focus.overlapNotes.bucket_count() <= 2);
    for (const auto& item : focus.baselineMap)
      TEST_ASSERT_EQUAL(item.first * 10, item.second.startTick);
    for (const auto& item : focus.overlapNotes)
      TEST_ASSERT_EQUAL(item.first * 10 + 3, item.second.shortenedEndTick);
  }
  const auto baselineBuckets = focus.baselineMap.bucket_count();
  const auto overlapBuckets = focus.overlapNotes.bucket_count();
  TEST_ASSERT_FALSE(focus.stepClear());
  TEST_ASSERT_FALSE(focus.stepClear());
  TEST_ASSERT_EQUAL(baselineBuckets, focus.baselineMap.bucket_count());
  TEST_ASSERT_EQUAL(overlapBuckets, focus.overlapNotes.bucket_count());
}

void test_focus_clear_empty_large_tables_require_separate_grants() {
  NoteEditFocus focus;
  focus.beginClear();
  TEST_ASSERT_FALSE(focus.stepClear());
  focus.baselineMap.rehash(257);
  focus.overlapNotes.rehash(257);
  focus.beginClear();
  TEST_ASSERT_TRUE(focus.stepClear());
  TEST_ASSERT_TRUE(focus.baselineMap.bucket_count() <= 2);
  TEST_ASSERT_TRUE(focus.overlapNotes.bucket_count() > 2);
  TEST_ASSERT_TRUE(focus.stepClear());
  TEST_ASSERT_TRUE(focus.overlapNotes.bucket_count() <= 2);
  TEST_ASSERT_FALSE(focus.stepClear());
}

void test_focus_clear_collisions_still_retire_one_head_node() {
  NoteEditFocus focus;
  focus.baselineMap.rehash(257);
  focus.overlapNotes.rehash(257);
  const auto stride = static_cast<NoteId>(focus.baselineMap.bucket_count());
  for (NoteId i = 1; i <= 8; ++i)
    focus.baselineMap.emplace(i * stride, NoteBaseline{63, 100, i * 10, i * 10 + 5});
  TEST_ASSERT_EQUAL(8u, focus.baselineMap.bucket_size(focus.baselineMap.bucket(stride)));
  OverlapNote row;
  row.noteId = 99;
  focus.overlapNotes.emplace(99, row);
  focus.beginClear();
  for (size_t grant = 0; grant < 8; ++grant) {
    TEST_ASSERT_TRUE(focus.stepClear());
    TEST_ASSERT_EQUAL(7 - grant, focus.baselineMap.size());
    TEST_ASSERT_EQUAL(1u, focus.overlapNotes.size());
    for (const auto& item : focus.baselineMap) {
      TEST_ASSERT_EQUAL(63, item.second.pitch);
      TEST_ASSERT_EQUAL(item.first / stride * 10, item.second.startTick);
    }
  }
  TEST_ASSERT_TRUE(focus.stepClear());
  TEST_ASSERT_EQUAL(1u, focus.overlapNotes.size());
  TEST_ASSERT_TRUE(focus.stepClear());
  TEST_ASSERT_TRUE(focus.stepClear());
  TEST_ASSERT_FALSE(focus.stepClear());
}

void test_focus_clear_isolated_from_live_current_state_and_reuse() {
  NoteEditFocus focus, neighbor;
  seedRetiringFocus(focus);
  seedRetiringFocus(neighbor);
  NoteEditCurrentState state;
  const NoteBaseline span{72, 100, 700, 720};
  state.upsertRow(900, span, span, NoteEditPresenceType::Visible);
  focus.beginClear();
  for (unsigned grant = 0; grant < 8; ++grant) {
    TEST_ASSERT_EQUAL(grant < 7, focus.stepClear());
    TEST_ASSERT_TRUE(neighbor.active);
    TEST_ASSERT_EQUAL(77u, neighbor.movingNoteId);
    TEST_ASSERT_EQUAL(3u, neighbor.baselineMap.size());
    TEST_ASSERT_EQUAL(2u, neighbor.overlapNotes.size());
    TEST_ASSERT_EQUAL(1u, state.size());
    TEST_ASSERT_EQUAL(700u, state.find(900)->currentSpan.startTick);
  }
  seedRetiringFocus(focus);
  focus.beginClear();
  drainRetiringFocus(focus);
  seedRetiringFocus(focus);
  focus.clear();
  assertRetiredFocusScalars(focus);
  TEST_ASSERT_TRUE(focus.baselineMap.empty());
  TEST_ASSERT_TRUE(focus.overlapNotes.empty());
  neighbor.clear();
}
