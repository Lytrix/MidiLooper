// Host-only adapter: use firmware parsers and event resolution; never publish into a Track.
#include <fstream>
#include <iterator>

struct DecodedSlotBody {
  uint32_t length = 0;
  SessionMidiEventVec events;
  bool lsv1 = false;
};

bool readTargetLegacy(std::vector<uint8_t>& bytes, PersistedLoopSnapshot& parsed, bool teensyWire) {
  MemoryStorageIo memory(&bytes);
  const auto raw = memory.io();
  StorageIo io = raw;
  if (teensyWire) {
    // In the legacy parser every scalar read is <= 4 bytes; larger reads are
    // MidiEvent batches. Adapt only that existing IO boundary, not the parser.
    io.read = [&raw](void* target, size_t size) {
      if (size <= 4) return raw.read(target, size);
      if (size % sizeof(MidiEvent)) return false;
      auto* events = static_cast<MidiEvent*>(target);
      for (size_t i = 0; i < size / sizeof(MidiEvent); ++i) {
        uint8_t wire[20];
        if (!raw.read(wire, sizeof(wire))) return false;
        // Pointer-bearing SysEx is not an admissible portable event.
        if (wire[4] == static_cast<uint8_t>(midi::SystemExclusive)) return false;
        events[i] = MidiEvent{};
        std::memcpy(&events[i].tick, wire, 4);
        events[i].type = static_cast<midi::MidiType>(wire[4]);
        events[i].channel = wire[5];
        std::memcpy(&events[i].noteId, wire + 8, 4);
        std::memcpy(&events[i].data, wire + 12, 2);
      }
      return true;
    };
  }
  bool ok = readPersistedLoopSnapshot(io, parsed, false);
  if (!ok) {
    releasePersistedLoopSnapshotChunks(parsed);
    parsed = PersistedLoopSnapshot{};
    memory.resetRead();
    ok = readPersistedLoopSnapshot(io, parsed, true);
  }
  return ok && memory.readPosition() == bytes.size();
}

// Convert only ABI-dependent event storage; semantic validation/resolution remains
// in the production parsers. Section lengths are rebuilt in host scratch memory.
bool adaptTargetLsv1(std::vector<uint8_t>& bytes, std::vector<uint8_t>& host) {
  if (bytes.size() < kLsv1BodyHeaderBytes) return false;
  host.assign(bytes.begin(), bytes.begin() + kLsv1BodyHeaderBytes);
  BufferReader reader{bytes.data(), bytes.size(), kLsv1BodyHeaderBytes};
  while (reader.remaining()) {
    uint8_t n = 0;
    if (!reader.read(&n, 1) || !n || n > kLsv1MaxSectionNameLength || n > reader.remaining()) return false;
    std::string name(reinterpret_cast<const char*>(bytes.data() + reader.pos), n);
    reader.skip(n);
    uint32_t length = 0;
    if (!reader.read(&length, 4) || length > reader.remaining()) return false;
    std::vector<uint8_t> payload(bytes.begin() + reader.pos, bytes.begin() + reader.pos + length);
    reader.skip(length);
    if (name == kLsv1SectionPersistedLoopSnapshot) {
      PersistedLoopSnapshot parsed{};
      const bool ok = readTargetLegacy(payload, parsed, true);
      if (!ok) { releasePersistedLoopSnapshotChunks(parsed); return false; }
      payload.clear();
      MemoryStorageIo output(&payload);
      const bool written = writePersistedLoopSnapshot(output.io(), parsed);
      releasePersistedLoopSnapshotChunks(parsed);
      if (!written) return false;
    } else if (name == kLsv1SectionLoopSnapshot) {
      BufferReader root{payload.data(), payload.size(), kLsv1LoopSnapshotRootHeaderBytes};
      if (payload.size() < kLsv1LoopSnapshotRootHeaderBytes) return false;
      uint32_t notes = 0, events = 0;
      // Serialized fields: id(4), pitch/velocity(2), padding(2), ticks(8).
      // The production size-estimate constant is not the serialized stride.
      constexpr size_t noteWireBytes = 16;
      if (!root.read(&notes, 4) || notes > root.remaining() / noteWireBytes) return false;
      root.skip(notes * noteWireBytes);
      if (!root.read(&events, 4) || events > root.remaining() / (kLsv1LoopSnapshotEventRecordPrefixBytes + 20)) return false;
      std::vector<uint8_t> converted(payload.begin(), payload.begin() + root.pos);
      for (uint32_t i = 0; i < events; ++i) {
        converted.insert(converted.end(), payload.begin()+root.pos, payload.begin()+root.pos+8);
        root.skip(8);
        const uint8_t* wire=payload.data()+root.pos;
        if (wire[4] == static_cast<uint8_t>(midi::SystemExclusive)) return false;
        MidiEvent event{};
        std::memcpy(&event.tick, wire, 4);
        event.type=static_cast<midi::MidiType>(wire[4]); event.channel=wire[5];
        std::memcpy(&event.noteId, wire+8, 4);
        std::memcpy(&event.data, wire+12, 2);
        appendRaw(converted,event);
        root.skip(20);
      }
      converted.insert(converted.end(),payload.begin()+root.pos,payload.end());
      payload=std::move(converted);
    }
    host.push_back(n);host.insert(host.end(),name.begin(),name.end());
    appendRaw(host,static_cast<uint32_t>(payload.size()));
    host.insert(host.end(),payload.begin(),payload.end());
  }
  return true;
}

bool decodeSlotBody(std::vector<uint8_t>& bytes, DecodedSlotBody& out, bool teensyWire = false) {
  out = DecodedSlotBody{};
  const auto format = probeLoopSnapshotBodyFormat(bytes.data(), bytes.size());
  if (format == LoopSnapshotBodyFormatProbeResult::SupportedV1) {
    std::vector<uint8_t> host;
    if (teensyWire && !adaptTargetLsv1(bytes,host)) return false;
    const auto& input=teensyWire ? host : bytes;
    LoopSnapshotBodyFormatV1ReadResult parsed{};
    const bool ok = readLoopSnapshotBodyFormatV1(input.data(),input.size(),parsed);
    if (ok && parsed.ok && parsed.loopSnapshot) {
      out.lsv1 = true;
      out.length = parsed.loopSnapshot->loopLengthTicks();
      LoopContentResolution::resolveWindow(*parsed.loopSnapshot, 0, out.length, out.events);
    }
    releasePersistedLoopSnapshotChunks(parsed.persistedOracle);
    return ok && parsed.ok && static_cast<bool>(parsed.loopSnapshot);
  }
  if (format != LoopSnapshotBodyFormatProbeResult::Legacy) return false;
  PersistedLoopSnapshot parsed{};
  const bool ok=readTargetLegacy(bytes,parsed,teensyWire);
  if (ok) {
    out.length=parsed.loopLengthTicks;
    parsed.passes.materializeToEventVector(out.events,out.length);
  }
  releasePersistedLoopSnapshotChunks(parsed);
  return ok;
}

int decodeSlotBodyFile(const char* path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file || file.tellg() <= 0 || file.tellg() > 2 * 1024 * 1024) return 2;
  const size_t size = static_cast<size_t>(file.tellg());
  file.seekg(0);
  std::vector<uint8_t> bytes(size);
  if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) return 2;
  LoopEventStore::initPool();
  DecodedSlotBody decoded;
  if (!decodeSlotBody(bytes, decoded, true)) return 2;
  printf("SLOT_DECODE,{\"format\":\"%s\",\"length\":%u,\"events\":[", decoded.lsv1 ? "LSV1" : "legacy", decoded.length);
  bool first = true;
  for (const auto& e : decoded.events) {
    printf("%s[%u,%u,%u,%u,%u,%u]", first ? "" : ",", e.tick,
           static_cast<unsigned>(e.type), e.channel, e.data.noteData.note,
           e.data.noteData.velocity, static_cast<unsigned>(e.noteId));
    first = false;
  }
  printf("]}\n");
  return 0;
}
