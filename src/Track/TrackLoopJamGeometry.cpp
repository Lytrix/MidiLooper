//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include <Arduino.h>

#include "ClockManager.h"
#include "EditManager.h"
#include "Globals.h"
#include "Logger.h"
#include "Utils/IntervalProjection.h"
#include "Utils/RecordStopLength.h"

namespace {

TRACK_COLD_MEM void persistActiveLoopGeometryChange(Track& track) {
  requestActiveLoopSlotPersist(track);
  track.invalidateCaches();
}

TRACK_COLD_MEM void resetActiveLoopPlaybackCursorForJam(Track& track) {
  Loop& loop = track.getActiveLoop();
  loop.nextEventIndex = 0;
  loop.lastTickInLoop = UINT32_MAX;
}

}  // namespace

TRACK_COLD_MEM void Track::clear() {
    if (trackState == TRACK_EMPTY) {
        logger.debug("Track already empty; ignoring clear");
        return;
    }

    Loop& loop = getActiveLoop();
    loop.resetPassTimeline();
    loop.discardCapture();
    loop.startLoopTick = 0;
    loop.loopLengthTicks = 0;
    loop.loopStartTick = 0;

    reconcileTransportStateAfterSlotMutation();
    alignLoopOriginOnNextStop = false;
    playingMidiDrainAfterOverdubStop_ = false;
    playingMidiDrainAfterOverdubStopIdleNoted_ = false;
    loopPrefixMeasureAfterUndo_ = false;
    loopPrefixMeasureAfterUndoNoted_ = false;
    invalidateCaches();
    editManager.revertNoteEditSessionForLoopClear(*this);
    logger.logTrackEvent("Track cleared", clockManager.getCurrentTick());
}

uint32_t Track::getTicksPerBar() {
    return TICKS_PER_BAR;
}

void Track::setLoopLength(uint32_t ticks) {
  Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks == ticks) return;
  loop.loopLengthTicks = ticks;
  persistActiveLoopGeometryChange(*this);
}

void Track::setLoopLengthWithWrapping(uint32_t newLoopLength) {
  Loop& loop = getActiveLoop();
  if (newLoopLength == loop.loopLengthTicks) return;

  uint32_t oldLoopLength = loop.loopLengthTicks;
  logger.log(CAT_TRACK, LOG_INFO, "Loop length change: %lu -> %lu ticks", oldLoopLength, newLoopLength);
  loop.loopLengthTicks = newLoopLength;
  persistActiveLoopGeometryChange(*this);
  logger.log(CAT_TRACK, LOG_INFO, "Loop length updated to %lu ticks (wrapping handled dynamically)", loop.loopLengthTicks);
}

void Track::setLoopStartTick(uint32_t startTick) {
  Loop& loop = getActiveLoop();
  if (startTick == loop.loopStartTick) return;

  uint32_t oldStartTick = loop.loopStartTick;
  if (startTick >= loop.loopLengthTicks && loop.loopLengthTicks > 0) {
    startTick = startTick % loop.loopLengthTicks;
  }
  loop.loopStartTick = startTick;
  logger.log(CAT_TRACK, LOG_INFO, "Loop start point changed: %lu -> %lu ticks", oldStartTick, loop.loopStartTick);
  persistActiveLoopGeometryChange(*this);
}

void Track::setLoopStartAndEnd(uint32_t startTick, uint32_t endTick) {
  if (endTick <= startTick) {
    logger.log(CAT_TRACK, LOG_ERROR, "Invalid loop range: start=%lu >= end=%lu", startTick, endTick);
    return;
  }
  Loop& loop = getActiveLoop();
  uint32_t newLength = endTick - startTick;
  logger.log(CAT_TRACK, LOG_INFO, "Setting loop start=%lu, end=%lu, length=%lu", startTick, endTick, newLength);
  loop.loopStartTick = startTick;
  loop.loopLengthTicks = newLength;
  persistActiveLoopGeometryChange(*this);
}

void Track::setJam(uint32_t startTick, uint32_t length) {
  noInterrupts();
  jamStartTick = startTick;
  jamLength = length;
  jamTick = 0;
  resetActiveLoopPlaybackCursorForJam(*this);
  interrupts();
  logger.log(CAT_TRACK, LOG_INFO, "Jam set: start=%lu, length=%lu", jamStartTick, jamLength);
}

void Track::clearJam() {
  noInterrupts();
  jamStartTick = UINT32_MAX;
  jamLength = 0;
  jamPlaybackActive = false;
  jamTick = 0;
  resetActiveLoopPlaybackCursorForJam(*this);
  interrupts();
  logger.log(CAT_TRACK, LOG_INFO, "Jam cleared");
}

void Track::advanceJamTick(uint32_t delta) {
  if (!jamPlaybackActive || jamLength == 0) return;
  jamTick = IntervalProjection::tickPhaseInLoop(jamTick + delta, 0, jamLength);
}

uint32_t Track::getJamTick() const {
  noInterrupts();
  uint32_t t = jamTick;
  interrupts();
  return t;
}

void Track::setJamTick(uint32_t tick) {
  noInterrupts();
  uint32_t newTick = IntervalProjection::tickPhaseInLoop(tick, 0, jamLength);
  if (newTick != jamTick) {
    jamTick = newTick;
    resetActiveLoopPlaybackCursorForJam(*this);
  }
  interrupts();
}

void Track::setJamPlayback(bool enabled) {
  noInterrupts();
  jamPlaybackActive = enabled;
  if (enabled) {
    resetActiveLoopPlaybackCursorForJam(*this);
  }
  interrupts();
}

uint32_t Track::getEffectivePlaybackTick(uint32_t currentTick) const {
  if (!jamPlaybackActive || jamLength == 0) return currentTick;
  const Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks == 0) return currentTick;
  uint32_t storagePos = (jamStartTick + jamTick) % loop.loopLengthTicks;
  return loop.startLoopTick + storagePos;
}

TRACK_COLD_MEM bool Track::hasCommittedPassesInSlot(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return false;
  }
  return loopForSlot(slotIndex).hasCommittedPasses();
}

TRACK_COLD_MEM uint32_t Track::quantizeTransportRecordLength(uint32_t rawLength) const {
  return RecordStopLength::quantizeTransportRecordLength(rawLength);
}

TRACK_COLD_MEM uint32_t Track::computeRecordStopLengthTicks(uint32_t rawLength,
                                                            uint32_t lastEventTick) const {
  return RecordStopLength::computeRecordStopLengthTicks(rawLength, lastEventTick);
}

TRACK_COLD_MEM void Track::resetLoopSlotAfterEmptyCapture(uint8_t slotIndex) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  resetActiveLoopAfterEmptyCapture(loopForSlot(slotIndex));
}
