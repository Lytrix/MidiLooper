// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "Utils/LoopStopFinalize.h"
#include "Utils/LoopMem.h"

namespace LoopStopFinalize {

LOOP_COLD_MEM bool TailRepairCursor::current() const {
  return workspace_ && generation_ && *generation_ == expectedGeneration_;
}

LOOP_COLD_MEM bool TailRepairCursor::begin(TailRepairWorkspace& workspace,
    const uint64_t& generation, uint32_t length, uint32_t closeTick, uint32_t window) {
  if (phase_ != Phase::Idle || !length) return false;
  workspace_ = &workspace; generation_ = &generation; expectedGeneration_ = generation;
  length_ = length; closeTick_ = closeTick;
  headEnd_ = std::min(window, length); tailStart_ = length - headEnd_;
  lane_ = lastStepUnits_ = 0; phase_ = Phase::Reset;
  return true;
}

LOOP_COLD_MEM TailRepairStep TailRepairCursor::stepReset(size_t quantum) {
  lastStepUnits_ = 0;
  if (!current() || !quantum) return TailRepairStep::Denied;
  if (phase_ == Phase::Input) return TailRepairStep::InputReady;
  if (phase_ != Phase::Reset) return TailRepairStep::Denied;
  while (lastStepUnits_ < quantum && lane_ < TailRepairWorkspace::kLanes) {
    workspace_->lanes[lane_++] = {UINT32_MAX, UINT32_MAX, 0};
    ++lastStepUnits_;
  }
  if (lane_ != TailRepairWorkspace::kLanes) return TailRepairStep::Pending;
  phase_ = Phase::Input; lane_ = 0;
  return TailRepairStep::InputReady;
}

LOOP_COLD_MEM bool TailRepairCursor::consume(const MidiEvent& event) {
  lastStepUnits_ = 0;
  if (!current() || phase_ != Phase::Input) return false;
  const bool on = event.isNoteOn(), off = event.isNoteOff();
  if (on || off) {
    if (!event.channel || event.channel > TailRepairWorkspace::kChannels ||
        event.data.noteData.note >= TailRepairWorkspace::kNotes) return false;
    auto& lane = workspace_->lanes[(event.channel - 1) * TailRepairWorkspace::kNotes +
                                   event.data.noteData.note];
    const bool head = event.tick < headEnd_;
    const bool tail = event.tick >= tailStart_ && event.tick < length_;
    if (on) {
      if (tail) lane.activeTailTick = event.tick;
    } else {
      lane.lastOffTick = std::max(lane.lastOffTick, event.tick);
      if (head) lane.firstHeadOffTick = std::min(lane.firstHeadOffTick, event.tick);
      if (head || tail) lane.activeTailTick = UINT32_MAX;
    }
  }
  lastStepUnits_ = 1;
  return true;
}

LOOP_COLD_MEM bool TailRepairCursor::finishInput() {
  if (!current() || phase_ != Phase::Input) return false;
  lane_ = lastStepUnits_ = 0; phase_ = Phase::Output;
  return true;
}

LOOP_COLD_MEM TailRepairStep TailRepairCursor::stepOutput(size_t quantum, MidiEvent& out) {
  lastStepUnits_ = 0;
  if (!current() || !quantum) return TailRepairStep::Denied;
  if (phase_ == Phase::Complete) return TailRepairStep::Complete;
  if (phase_ != Phase::Output && phase_ != Phase::Ready) return TailRepairStep::Denied;
  while (phase_ != Phase::Ready && lane_ < TailRepairWorkspace::kLanes &&
         lastStepUnits_ < quantum) {
    const auto& lane = workspace_->lanes[lane_];
    ++lastStepUnits_;
    const bool wrapped = lane.firstHeadOffTick < lane.activeTailTick &&
        lane.activeTailTick - lane.firstHeadOffTick > length_ / 2;
    if (lane.activeTailTick != UINT32_MAX && lane.lastOffTick <= lane.activeTailTick && !wrapped)
      phase_ = Phase::Ready;
    else ++lane_;
  }
  if (phase_ == Phase::Ready) {
    const uint32_t on = workspace_->lanes[lane_].activeTailTick;
    uint32_t tick = closeTick_ == UINT32_MAX ? length_ - 1 : std::min(closeTick_, length_ - 1);
    if (closeTick_ != UINT32_MAX && tick < on) tick = length_ - 1;
    out = MidiEvent::NoteOff(tick, 1 + lane_ / TailRepairWorkspace::kNotes,
                            lane_ % TailRepairWorkspace::kNotes, 0);
    return TailRepairStep::EventReady;
  }
  if (lane_ != TailRepairWorkspace::kLanes) return TailRepairStep::Pending;
  phase_ = Phase::Complete;
  return TailRepairStep::Complete;
}

LOOP_COLD_MEM bool TailRepairCursor::acknowledgeOutput() {
  if (!current() || phase_ != Phase::Ready) return false;
  ++lane_; phase_ = Phase::Output;
  return true;
}

LOOP_COLD_MEM void TailRepairCursor::discard() {
  workspace_ = nullptr; generation_ = nullptr;
  phase_ = Phase::Idle; lane_ = lastStepUnits_ = 0;
}

} // namespace LoopStopFinalize
