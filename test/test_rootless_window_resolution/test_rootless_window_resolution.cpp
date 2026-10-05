//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
#include <unity.h>
#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Track/RuntimeNotePairLookup.cpp"
#include "../../src/Track/RuntimeCompanionOverlapWalk.cpp"
#include "../../src/Track/RuntimeCompanionSpanWalk.cpp"
#include "../../src/Track/RuntimeCompanionReconstruction.cpp"
#include "../../src/Track/RuntimeCompanionApply.cpp"
#include "../../src/Track/RuntimeCompanionBridgeMap.cpp"
#include "../../src/Track/RuntimeCompanionPasses.cpp"
#include "../../src/Track/RuntimeCanonicalPassInputs.cpp"
#include "../../src/Track/RuntimeCaptureCompanions.cpp"
#include "../../src/Track/RuntimePlaybackView.cpp"
#include "../../src/Track/RuntimePlaybackOrderBuilder.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../test_support/NoteEditFocusTestDeps.cpp"
#include "../../src/EditManager/NoteEditSessionUndoStack.cpp"
#include "EditApply.h"
#include "EditPass.h"
#include "MidiEvent.h"

// Differential characterization for Route D. This deliberately compares the two operation orders, not production audio:
// full-context edit then window (effective-content reference), versus window-first raw events then edit (naive rootless
// gather). It proves the rootless gather must preserve enough note identity/context or defer closure to the ledger.
void test_rootless_window_filter_then_delete_loses_predecessor_note_context() {
  constexpr uint32_t loopLength = 8 * Config::TICKS_PER_BAR;
  constexpr uint32_t windowStart = 400;
  constexpr uint32_t windowEnd = 600;
  constexpr NoteId id = 77;

  MidiEvent on = MidiEvent::NoteOn(100, 1, 60, 100);
  on.noteId = id;
  MidiEvent off = MidiEvent::NoteOff(500, 1, 60, 0);
  off.noteId = id;
  MidiEventVec raw{on, off};

  EditPass hide{};
  hide.state = EditPassState::Active;
  hide.passType = EditPassType::Note;
  hide.actionType = EditActionType::Delete;
  hide.targetNoteId = id;

  MidiEventVec effective = raw;
  applyNoteEditPassSequence(effective, EditPassVec{hide}, loopLength);
  MidiEventVec expectedWindow;
  for (const MidiEvent& event : effective) {
    if (event.tick >= windowStart && event.tick < windowEnd) expectedWindow.push_back(event);
  }

  MidiEventVec rootlessCandidate;
  for (const MidiEvent& event : raw) {
    if (event.tick >= windowStart && event.tick < windowEnd) rootlessCandidate.push_back(event);
  }
  applyNoteEditPassSequence(rootlessCandidate, EditPassVec{hide}, loopLength);

  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(expectedWindow.size()));
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(rootlessCandidate.size()));
  TEST_ASSERT_TRUE(rootlessCandidate[0].isNoteOff());
  TEST_ASSERT_EQUAL_UINT32(500, rootlessCandidate[0].tick);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rootless_window_filter_then_delete_loses_predecessor_note_context);
  return UNITY_END();
}
