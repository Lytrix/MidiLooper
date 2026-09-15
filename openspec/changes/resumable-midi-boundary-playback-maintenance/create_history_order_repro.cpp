// Reproduces applyAddNote's append + std::sort(raw tick) ordering operation.
// Host algorithm characterization only; does not claim a device MIDI trace.
// From repository root: c++ -std=c++17 -DPIO_UNIT_TEST_NATIVE -Iinclude
//   openspec/changes/resumable-midi-boundary-playback-maintenance/create_history_order_repro.cpp
//   -o /tmp/create-history-order-repro
#include <algorithm>
#include <cstdio>
#include "MidiEvent.h"

int main() {
  for (unsigned count = 2; count <= 128; ++count) {
    MidiEventVec input;
    for (unsigned i = 0; i < count; ++i) {
      auto event = MidiEvent::NoteOn(i % 3, 1, 60, 90);
      event.noteId = i + 1;
      input.push_back(event);
    }
    auto reference = input;
    auto stable = input;
    const auto before = [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; };
    std::sort(reference.begin(), reference.end(), before);
    std::stable_sort(stable.begin(), stable.end(), before);
    for (unsigned i = 0; i < count; ++i) {
      if (reference[i].noteId == stable[i].noteId) continue;
      std::printf("count=%u first mismatch=%u\ninput ticks:", count, i);
      for (auto event : input) std::printf(" %u", event.tick);
      std::printf("\nstd::sort original indexes:");
      for (auto event : reference) std::printf(" %u", event.noteId - 1);
      std::printf("\nstable original indexes:");
      for (auto event : stable) std::printf(" %u", event.noteId - 1);
      std::printf("\n");
      return 0;
    }
  }
  std::printf("No mismatch found in this host sample.\n");
  return 1;
}
