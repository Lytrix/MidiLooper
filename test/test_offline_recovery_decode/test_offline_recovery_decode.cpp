#include <cstring>
#include <unity.h>
#include <vector>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../../src/StorageLoopIo.cpp"
#ifdef RECOVERY_TEENSY_ABI
#include "RecoveryStorageLoopIoBodyFormatV1.cpp"
#else
#include "../../src/StorageLoopIoBodyFormatV1.cpp"
#endif
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"

#include "GlobalUndoStack.h"
#include "Loop.h"
#include "../test_support/CommittedChunkIdTestHelpers.h"


#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>

struct Result {
  bool ok = false;
  uint32_t length = 0;
  size_t passes = 0, rawEvents = 0, notes = 0, otherEvents = 0;
  bool root = false;
};

size_t countEvents(const PersistedLoopSnapshot& snapshot) {
  size_t n = 0;
  auto count = [&](uint16_t id) {
    MidiEventVec events;
    LoopEventStore::appendChunkRefEvent(id, events);
    for (const auto& event : events)
      TEST_ASSERT_NOT_EQUAL_MESSAGE(midi::SystemExclusive, event.type, "SysEx pointer payload is unsupported by offline audit");
    n += events.size();
  };
  for (auto id : snapshot.passes.recordPass.committedChunkIds) count(id);
  for (const auto& pass : snapshot.passes.overdubPasses)
    for (auto id : pass.committedChunkIds) count(id);
  return n;
}

Result decode(const std::vector<uint8_t>& body) {
  Result r;
  if (body.size() >= 4 && std::memcmp(body.data(), "LSV1", 4) == 0) {
    LoopSnapshotBodyFormatV1ReadResult out;
    r.ok = readLoopSnapshotBodyFormatV1(body.data(), body.size(), out);
    if (!r.ok) return r;
    r.length = out.metadata.loopLengthTicks;
    r.passes = out.persistedOracle.passes.capturePassCount();
    r.rawEvents = countEvents(out.persistedOracle);
    r.root = bool(out.loopSnapshot);
    if (out.loopSnapshot) {
      std::set<NoteId> notes;
      std::set<uint32_t> events;
      const uint32_t bars = (r.length + Config::TICKS_PER_BAR - 1) / Config::TICKS_PER_BAR;
      for (uint32_t bar = 0; bar < bars; ++bar) {
        for (uint8_t pitch = 0; pitch < 128; ++pitch)
          forEachSnapshotMembershipReference(out.loopSnapshot->noteMembership(pitch, bar).get(),
              [&](const LoopSnapshotNoteRef& note) { notes.insert(note->noteId); });
        forEachSnapshotMembershipReference(out.loopSnapshot->eventMembership(bar).get(),
            [&](const LoopSnapshotEventRef& event) { TEST_ASSERT_NOT_EQUAL(midi::SystemExclusive, event->value.type); events.insert(event->eventId); });
      }
      r.notes = notes.size(); r.otherEvents = events.size();
    }
    return r;
  }
  for (bool legacy : {false, true}) {
    size_t pos = 0;
    PersistedLoopSnapshot out;
    StorageIo io{nullptr, [&](void* dest, size_t n) {
      if (n > body.size() - pos) return false;
      std::memcpy(dest, body.data()+pos, n); pos += n; return true;
    }};
    if (readPersistedLoopSnapshot(io, out, legacy) && pos == body.size()) {
      r.ok = true; r.length = out.loopLengthTicks;
      r.passes = out.passes.capturePassCount(); r.rawEvents = countEvents(out);
      return r;
    }
  }
  return r;
}

void test_known_record_round_trip() {
  PersistedLoopSnapshot source;
  source.loopId = 0; source.loopLengthTicks = 1536;
  source.passes.recordPass.id = 1;
  LoopEventStore capture;
  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(0, 1, 67, 100)));
  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOff(48, 1, 67, 0)));
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(capture, source.passes.recordPass.committedChunkIds));
  std::vector<uint8_t> body;
  StorageIo io{[&](const void* bytes, size_t n) {
    const auto* p = static_cast<const uint8_t*>(bytes); body.insert(body.end(), p, p+n); return true;
  }, nullptr};
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(io, source));
  auto result = decode(body);
  TEST_ASSERT_TRUE(result.ok);
  TEST_ASSERT_EQUAL_UINT32(2, result.rawEvents);
  TEST_ASSERT_EQUAL_UINT32(1536, result.length);
}

void test_truncated_header_refused() {
  TEST_ASSERT_FALSE(decode(std::vector<uint8_t>{'L','S','V','1'}).ok);
  TEST_ASSERT_FALSE(decode(std::vector<uint8_t>{1,2,3}).ok);
}

void test_copied_slots_decode() {
  const char* root = std::getenv("MIDILOOPER_RECOVERY_SLOTS");
  if (!root) TEST_IGNORE_MESSAGE("Set MIDILOOPER_RECOVERY_SLOTS to verified host-copy current/slots");
  if (sizeof(MidiEvent) != 20 || offsetof(MidiEvent, data) != 12)
    TEST_IGNORE_MESSAGE("Target ABI headers required: Mac MIDI event layout differs from SD layout");
  unsigned failures = 0;
  for (unsigned track = 0; track < 8; ++track) {
    for (unsigned slot = 0; slot < 8; ++slot) {
      char name[1024]; std::snprintf(name, sizeof(name), "%s/loop_%02u_%02u.bin", root, track, slot);
      std::ifstream file(name, std::ios::binary);
      std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
      TEST_ASSERT_GREATER_OR_EQUAL_UINT32(14, bytes.size());
      TEST_ASSERT_EQUAL_MEMORY("SAVE", bytes.data()+bytes.size()-4, 4);
      std::vector<uint8_t> body(bytes.begin()+10, bytes.end()-4);
      const auto r = decode(body);
      std::printf("RECOVERY_SLOT,{\"track\":%u,\"slot\":%u,\"decoded\":%s,\"length\":%u,\"passes\":%zu,\"raw_events\":%zu,\"snapshot\":%s,\"snapshot_notes\":%zu,\"snapshot_events\":%zu}\n",
          track, slot, r.ok?"true":"false", r.length, r.passes, r.rawEvents,
          r.root?"true":"false", r.notes, r.otherEvents);
      if (!r.ok) ++failures;
      // A valid body must not remain valid after a required header is truncated.
      TEST_ASSERT_FALSE(decode(std::vector<uint8_t>(body.begin(), body.begin()+3)).ok);
    }
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, failures, "See per-slot decoder results");
}

int main() {
  std::printf("RECOVERY_ABI,event=%zu,data=%zu\n", sizeof(MidiEvent), offsetof(MidiEvent, data));
  LoopEventStore::initPool();
  UNITY_BEGIN();
  RUN_TEST(test_known_record_round_trip);
  RUN_TEST(test_truncated_header_refused);
  RUN_TEST(test_copied_slots_decode);
  return UNITY_END();
}
