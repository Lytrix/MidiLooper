//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "SlotLoadSession.h"

#include "LoopEventStore.h"

uint8_t SlotLoadSession::activeCount_ = 0;
uint8_t SlotLoadSession::activeTrack_ = 0;
uint8_t SlotLoadSession::activeSlot_ = 0;
SlotLoadSessionState SlotLoadSession::activeState_ = SlotLoadSessionState::Dequeued;

SlotLoadSession::SlotLoadSession(uint8_t trackIndex, uint8_t slotIndex)
    : trackIndex_(trackIndex),
      slotIndex_(slotIndex),
      state_(SlotLoadSessionState::Dequeued),
      finished_(false) {
  LoopEventStore::enterSdLoadStaging();
  ++activeCount_;
  activeTrack_ = trackIndex_;
  activeSlot_ = slotIndex_;
  activeState_ = SlotLoadSessionState::Dequeued;
}

SlotLoadSession::~SlotLoadSession() {
  if (!finished_) {
    if (state_ != SlotLoadSessionState::Completed && state_ != SlotLoadSessionState::Failed) {
      state_ = SlotLoadSessionState::Failed;
      activeState_ = SlotLoadSessionState::Failed;
    }
  }
  LoopEventStore::leaveSdLoadStaging();
  if (activeCount_ > 0) {
    --activeCount_;
  }
  if (activeCount_ == 0) {
    activeState_ = SlotLoadSessionState::Dequeued;
  }
}

void SlotLoadSession::setState(SlotLoadSessionState state) {
  state_ = state;
  activeState_ = state;
  activeTrack_ = trackIndex_;
  activeSlot_ = slotIndex_;
}

void SlotLoadSession::fail() {
  state_ = SlotLoadSessionState::Failed;
  activeState_ = SlotLoadSessionState::Failed;
  finished_ = true;
}

void SlotLoadSession::complete() {
  state_ = SlotLoadSessionState::Completed;
  activeState_ = SlotLoadSessionState::Completed;
  finished_ = true;
}

bool SlotLoadSession::isActive() { return activeCount_ > 0; }

bool SlotLoadSession::isActiveFor(uint8_t trackIndex, uint8_t slotIndex) {
  return activeCount_ > 0 && activeTrack_ == trackIndex && activeSlot_ == slotIndex;
}

SlotLoadSessionState SlotLoadSession::activeState() { return activeState_; }

uint8_t SlotLoadSession::activeCount() { return activeCount_; }
