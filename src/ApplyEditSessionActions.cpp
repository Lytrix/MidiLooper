//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ApplyEditSessionActions.h"

#include <algorithm>

#include "EditSessionLiveStoreSpan.h"
#include "Utils/NoteEditMem.h"

namespace {

NOTE_EDIT_MEM bool eraseNotePairByNoteId(MidiEventVec& liveStore, NoteId noteId, uint8_t channel,
                           uint32_t loopLength, NoteId movingNoteId) {
  // Resolve the live pair first (noteId-tagged off, else LIFO via findLinearOffForNoteId).
  // Erase by index — do not filter offs by noteId alone. Untagged offs are normal
  // (MidiEvent contract); leaving them behind orphans an end tick that the mover then
  // pairs to when bridging two close neighbors (session_20260804_222039).
  size_t onIndex = SIZE_MAX;
  for (size_t i = 0; i < liveStore.size(); ++i) {
    const MidiEvent& evt = liveStore[i];
    if (evt.channel == channel && evt.noteId == noteId && evt.isNoteOn() &&
        evt.data.noteData.velocity > 0) {
      onIndex = i;
      break;
    }
  }
  if (onIndex == SIZE_MAX) {
    return false;
  }

  MidiEvent* noteOn = &liveStore[onIndex];
  MidiEvent* noteOff = findLinearOffForNoteId(liveStore, *noteOn, noteId, loopLength);
  // Never erase an off already tagged to a different note — that orphans the other note-on,
  // which reconstructs to loopLength-1 (session_20260804_224309).
  if (noteOff != nullptr && noteOff->noteId != kInvalidNoteId && noteOff->noteId != noteId) {
    noteOff = nullptr;
  }
  // Never erase the mover's only linear off (often untagged). Hide would orphan the mover
  // and leave the overlap target's on open → loopLength-1 (session_20260804_230007).
  if (noteOff != nullptr && movingNoteId != kInvalidNoteId && movingNoteId != noteId) {
    MidiEvent* moverOn = nullptr;
    for (MidiEvent& evt : liveStore) {
      if (evt.channel == channel && evt.noteId == movingNoteId && evt.isNoteOn() &&
          evt.data.noteData.velocity > 0) {
        moverOn = &evt;
        break;
      }
    }
    if (moverOn != nullptr) {
      MidiEvent* moverOff =
          findLinearOffForNoteId(liveStore, *moverOn, movingNoteId, loopLength);
      if (moverOff == noteOff) {
        noteOff = nullptr;
      }
    }
  }
  if (noteOff == nullptr) {
    // Orphan note-on (no usable off): remove it so Restore cannot duplicate an open span.
    liveStore.erase(liveStore.begin() + static_cast<std::ptrdiff_t>(onIndex));
    return true;
  }

  size_t offIndex = SIZE_MAX;
  for (size_t i = 0; i < liveStore.size(); ++i) {
    if (&liveStore[i] == noteOff) {
      offIndex = i;
      break;
    }
  }
  if (offIndex == SIZE_MAX) {
    liveStore.erase(liveStore.begin() + static_cast<std::ptrdiff_t>(onIndex));
    return true;
  }

  if (onIndex > offIndex) {
    const size_t tmp = onIndex;
    onIndex = offIndex;
    offIndex = tmp;
  }
  liveStore.erase(liveStore.begin() + static_cast<std::ptrdiff_t>(offIndex));
  liveStore.erase(liveStore.begin() + static_cast<std::ptrdiff_t>(onIndex));
  return true;
}

NOTE_EDIT_MEM bool offIsUnsafeToReuseForOverlapTarget(MidiEventVec& liveStore, MidiEvent* noteOff,
                                                       NoteId targetNoteId, const NoteEditFocus& focus,
                                                       uint8_t channel, uint32_t loopLength) {
  if (noteOff == nullptr || focus.movingNoteId == kInvalidNoteId) {
    return noteOff == nullptr;
  }
  // Target's own tagged off is always safe to shorten — even when LIFO currently pairs the
  // mover to that same event (overlapping same-pitch store). Rejecting it caused Shorten to
  // append a new off every geometry tick → store bloat, lag, stuck moves
  // (session_20260804_230904: flatEvents=64, start stuck at 480).
  if (noteOff->noteId == targetNoteId) {
    return false;
  }
  if (noteOff->noteId == focus.movingNoteId) {
    return true;
  }
  if (noteOff->noteId != kInvalidNoteId) {
    // Tagged to a third note — do not rewrite.
    return true;
  }
  MidiEvent* moverOn = nullptr;
  for (MidiEvent& evt : liveStore) {
    if (evt.channel == channel && evt.noteId == focus.movingNoteId && evt.isNoteOn() &&
        evt.data.noteData.velocity > 0) {
      moverOn = &evt;
      break;
    }
  }
  if (moverOn == nullptr) {
    return false;
  }
  MidiEvent* moverOff =
      findLinearOffForNoteId(liveStore, *moverOn, focus.movingNoteId, loopLength);
  return moverOff == noteOff;
}

NOTE_EDIT_MEM MidiEvent* findNoteOnForNoteId(MidiEventVec& liveStore, NoteId noteId, uint8_t channel,
                               uint8_t pitch, uint32_t startTick) {
  for (MidiEvent& evt : liveStore) {
    if (evt.noteId != noteId || evt.channel != channel || !evt.isNoteOn() ||
        evt.data.noteData.velocity == 0 || evt.data.noteData.note != pitch ||
        evt.tick != startTick) {
      continue;
    }
    return &evt;
  }
  return nullptr;
}

NOTE_EDIT_MEM bool resolveNotePairForAction(MidiEventVec& liveStore, const NoteEditFocus& focus, NoteId noteId,
                              uint8_t channel, uint8_t pitch, uint32_t /*startTick*/,
                              uint32_t loopLength, MidiEvent*& noteOn, MidiEvent*& noteOff) {
  noteOn = nullptr;
  noteOff = nullptr;

  NoteBaseline span{};
  if (findLinearNoteSpanForNoteId(liveStore, noteId, channel, span, UINT32_MAX, loopLength)) {
    noteOn = findNoteOnForNoteId(liveStore, noteId, channel, span.pitch, span.startTick);
    if (noteOn != nullptr) {
      noteOff = findLinearOffForNoteId(liveStore, *noteOn, noteId, loopLength);
      if (noteOff != nullptr) {
        return true;
      }
    }
  }

  if (focus.active && focus.movingNoteId == noteId) {
    noteOn = findNoteOnForMovingNoteEdit(liveStore, focus, channel, pitch,
                                         focus.commitBaseline.startTick, loopLength);
  } else {
    for (MidiEvent& evt : liveStore) {
      if (evt.noteId == noteId && evt.channel == channel && evt.isNoteOn() &&
          evt.data.noteData.velocity > 0) {
        noteOn = &evt;
        break;
      }
    }
  }

  if (noteOn == nullptr) {
    return false;
  }

  noteOff = findLinearOffForNoteId(liveStore, *noteOn, noteId, loopLength);
  return noteOff != nullptr;
}

NOTE_EDIT_MEM void applyRestoreNote(const EditSessionAction& action, MidiEventVec& liveStore,
                                    const NoteEditFocus& focus, uint8_t channel,
                                    uint32_t loopLength) {
  MidiEvent* noteOn = nullptr;
  for (MidiEvent& evt : liveStore) {
    if (evt.channel == channel && evt.noteId == action.targetNoteId && evt.isNoteOn() &&
        evt.data.noteData.velocity > 0) {
      noteOn = &evt;
      break;
    }
  }

  MidiEvent* noteOff = nullptr;
  if (noteOn != nullptr) {
    noteOff = findLinearOffForNoteId(liveStore, *noteOn, action.targetNoteId, loopLength);
  }
  if (noteOn != nullptr && noteOff == nullptr) {
    // Prefer destination baseline end, then current (shortened) off via LIFO for this note-on.
    // Never pick the farthest same-pitch untagged off — that steals the mover's off when
    // restoring a left neighbor while moving L→R over a right neighbor
    // (session_20260804_215743).
    for (MidiEvent& evt : liveStore) {
      if (!evt.isNoteOff() || evt.channel != channel ||
          evt.data.noteData.note != action.pitch || evt.tick <= noteOn->tick) {
        continue;
      }
      if (evt.noteId != kInvalidNoteId && evt.noteId != action.targetNoteId) {
        continue;
      }
      if (focus.movingNoteId != kInvalidNoteId && evt.noteId == focus.movingNoteId) {
        continue;
      }
      if (evt.tick == action.endTick) {
        noteOff = &evt;
        break;
      }
    }
  }
  if (noteOn != nullptr && noteOff == nullptr) {
    MidiEvent* resolvedOn = nullptr;
    MidiEvent* resolvedOff = nullptr;
    if (resolveNotePairForAction(liveStore, focus, action.targetNoteId, channel, action.pitch,
                                 action.startTick, loopLength, resolvedOn, resolvedOff)) {
      noteOn = resolvedOn;
      if (resolvedOff != nullptr &&
          (resolvedOff->noteId == kInvalidNoteId ||
           resolvedOff->noteId == action.targetNoteId) &&
          !(focus.movingNoteId != kInvalidNoteId &&
            resolvedOff->noteId == focus.movingNoteId)) {
        noteOff = resolvedOff;
      }
    }
  }

  if (noteOn != nullptr && noteOff != nullptr) {
    // Spec: RestoreNote reinserts OR extends baseline span (shortened live pair).
    noteOn->tick = action.startTick;
    noteOn->data.noteData.note = action.pitch;
    noteOn->data.noteData.velocity = action.velocity;
    noteOn->noteId = action.targetNoteId;
    noteOff->tick = action.endTick;
    noteOff->data.noteData.note = action.pitch;
    noteOff->noteId = action.targetNoteId;
    return;
  }

  if (noteOn != nullptr) {
    // Orphan note-on (no off): repair by appending off only. Pushing a second on leaves the
    // orphan open, and IntervalProjection maps open notes to endTick = loopLength - 1
    // (session_20260804_224309).
    noteOn->tick = action.startTick;
    noteOn->data.noteData.note = action.pitch;
    noteOn->data.noteData.velocity = action.velocity;
    noteOn->noteId = action.targetNoteId;
    MidiEvent offEvt = MidiEvent::NoteOff(action.endTick, channel, action.pitch, 0);
    offEvt.noteId = action.targetNoteId;
    liveStore.push_back(offEvt);
    return;
  }

  MidiEvent onEvt = MidiEvent::NoteOn(action.startTick, channel, action.pitch, action.velocity);
  onEvt.noteId = action.targetNoteId;
  liveStore.push_back(onEvt);
  MidiEvent offEvt = MidiEvent::NoteOff(action.endTick, channel, action.pitch, 0);
  offEvt.noteId = action.targetNoteId;
  liveStore.push_back(offEvt);
}

NOTE_EDIT_MEM void applyShortenNote(const EditSessionAction& action, MidiEventVec& liveStore,
                      const NoteEditFocus& focus, uint8_t channel, uint32_t loopLength) {
  MidiEvent* noteOn = findNoteOnForNoteId(liveStore, action.targetNoteId, channel, action.pitch,
                                          action.startTick);
  if (noteOn == nullptr) {
    for (MidiEvent& evt : liveStore) {
      if (evt.noteId == action.targetNoteId && evt.channel == channel && evt.isNoteOn() &&
          evt.data.noteData.velocity > 0) {
        noteOn = &evt;
        break;
      }
    }
  }
  if (noteOn == nullptr) {
    return;
  }

  // Prefer noteId-tagged off, then transaction-baseline end tick, then LIFO. Baseline-end
  // lookup avoids stealing the mover's off when same-pitch spans briefly overlap in-store
  // (session_20260804_212156: overlapped notes kept their old ends).
  MidiEvent* noteOff = findLinearOffForNoteId(liveStore, *noteOn, action.targetNoteId, loopLength);
  if (noteOff != nullptr &&
      offIsUnsafeToReuseForOverlapTarget(liveStore, noteOff, action.targetNoteId, focus, channel,
                                         loopLength)) {
    noteOff = nullptr;
  }
  if (noteOff == nullptr) {
    uint32_t baselineEnd = UINT32_MAX;
    const auto baselineIt = focus.baselineMap.find(action.targetNoteId);
    if (baselineIt != focus.baselineMap.end()) {
      baselineEnd = baselineIt->second.endTick;
    }
    for (MidiEvent& evt : liveStore) {
      if (!evt.isNoteOff() || evt.channel != channel ||
          evt.data.noteData.note != noteOn->data.noteData.note || evt.tick <= noteOn->tick) {
        continue;
      }
      if (baselineEnd != UINT32_MAX && evt.tick == baselineEnd) {
        if (!offIsUnsafeToReuseForOverlapTarget(liveStore, &evt, action.targetNoteId, focus,
                                                channel, loopLength)) {
          noteOff = &evt;
          break;
        }
      }
    }
  }
  if (noteOff == nullptr) {
    MidiEvent* resolvedOn = nullptr;
    MidiEvent* resolvedOff = nullptr;
    if (resolveNotePairForAction(liveStore, focus, action.targetNoteId, channel, action.pitch,
                                 action.startTick, loopLength, resolvedOn, resolvedOff)) {
      if (!offIsUnsafeToReuseForOverlapTarget(liveStore, resolvedOff, action.targetNoteId, focus,
                                              channel, loopLength)) {
        noteOn = resolvedOn;
        noteOff = resolvedOff;
      }
    }
  }
  if (noteOn != nullptr && noteOff != nullptr) {
    noteOff->tick = action.endTick;
    if (noteOff->noteId == kInvalidNoteId) {
      noteOff->noteId = action.targetNoteId;
    }
    return;
  }
  // No safe existing off (mover owns the only later untagged off): append once. Prefer
  // updating any existing target-tagged off if findLinearOff missed it.
  if (noteOn == nullptr) {
    return;
  }
  for (MidiEvent& evt : liveStore) {
    if (evt.isNoteOff() && evt.channel == channel && evt.noteId == action.targetNoteId &&
        evt.data.noteData.note == action.pitch) {
      evt.tick = action.endTick;
      return;
    }
  }
  MidiEvent offEvt = MidiEvent::NoteOff(action.endTick, channel, action.pitch, 0);
  offEvt.noteId = action.targetNoteId;
  liveStore.push_back(offEvt);
}

NOTE_EDIT_MEM void applyHideNote(const EditSessionAction& action, MidiEventVec& liveStore,
                                 const NoteEditFocus& focus, uint8_t channel, uint32_t loopLength) {
  eraseNotePairByNoteId(liveStore, action.targetNoteId, channel, loopLength, focus.movingNoteId);
}

NOTE_EDIT_MEM void applyMoveNote(const EditSessionAction& action, MidiEventVec& liveStore, NoteEditFocus& focus,
                   uint8_t channel, uint32_t loopLength) {
  MidiEvent* noteOn = nullptr;
  MidiEvent* noteOff = nullptr;
  if (!resolveNotePairForAction(liveStore, focus, action.targetNoteId, channel, action.pitch,
                                action.startTick, loopLength, noteOn, noteOff)) {
    // Wrap / seam moves may have a note-on without a linear off yet. Relocate a wrap-head
    // remnant off (tick < note-on) when present; never claim a later untagged neighbor off.
    // Also never claim an earlier off that an earlier same-pitch note-on can own — that
    // orphans the neighbor open to loopLength-1 (session_20260804_224309).
    for (MidiEvent& evt : liveStore) {
      if (evt.noteId == action.targetNoteId && evt.channel == channel && evt.isNoteOn() &&
          evt.data.noteData.velocity > 0) {
        noteOn = &evt;
        break;
      }
    }
    if (noteOn == nullptr) {
      return;
    }
    MidiEvent* wrapHeadOff = nullptr;
    for (MidiEvent& evt : liveStore) {
      if (!evt.isNoteOff() || evt.channel != channel ||
          evt.data.noteData.note != action.pitch || evt.tick >= noteOn->tick) {
        continue;
      }
      if (evt.noteId != kInvalidNoteId && evt.noteId != action.targetNoteId) {
        continue;
      }
      bool earlierOnOwnsOff = false;
      for (const MidiEvent& onEvt : liveStore) {
        if (onEvt.channel != channel || !onEvt.isNoteOn() || onEvt.data.noteData.velocity == 0 ||
            onEvt.data.noteData.note != action.pitch || &onEvt == noteOn) {
          continue;
        }
        if (onEvt.tick < evt.tick) {
          earlierOnOwnsOff = true;
          break;
        }
      }
      if (earlierOnOwnsOff) {
        continue;
      }
      wrapHeadOff = &evt;
      break;
    }
    noteOn->tick = action.startTick;
    noteOn->data.noteData.note = action.pitch;
    if (wrapHeadOff != nullptr) {
      wrapHeadOff->tick = action.endTick;
      wrapHeadOff->data.noteData.note = action.pitch;
      wrapHeadOff->noteId = action.targetNoteId;
    } else {
      MidiEvent offEvt = MidiEvent::NoteOff(action.endTick, channel, action.pitch, 0);
      offEvt.noteId = action.targetNoteId;
      liveStore.push_back(offEvt);
    }
    if (focus.active && focus.movingNoteId == action.targetNoteId) {
      noteEditFocusApplyMoveEnd(focus, action.startTick, action.endTick);
    }
    return;
  }
  noteOn->tick = action.startTick;
  noteOff->tick = action.endTick;
  if (noteOff->noteId == kInvalidNoteId) {
    noteOff->noteId = action.targetNoteId;
  }
  if (focus.active && focus.movingNoteId == action.targetNoteId) {
    noteEditFocusApplyMoveEnd(focus, action.startTick, action.endTick);
  }
}

NOTE_EDIT_MEM void applyChangeLength(const EditSessionAction& action, MidiEventVec& liveStore,
                       NoteEditFocus& focus, uint8_t channel, uint32_t loopLength) {
  MidiEvent* noteOn = nullptr;
  MidiEvent* noteOff = nullptr;
  if (!resolveNotePairForAction(liveStore, focus, action.targetNoteId, channel, action.pitch,
                                action.startTick, loopLength, noteOn, noteOff)) {
    return;
  }
  noteOff->tick = action.endTick;
  if (noteOff->noteId == kInvalidNoteId) {
    noteOff->noteId = action.targetNoteId;
  }
  if (focus.active && focus.movingNoteId == action.targetNoteId) {
    noteEditFocusApplyLengthEnd(focus, action.endTick);
  }
}

NOTE_EDIT_MEM void applyChangePitch(const EditSessionAction& action, MidiEventVec& liveStore,
                      NoteEditFocus& focus, uint8_t channel, uint32_t loopLength) {
  MidiEvent* noteOn = nullptr;
  MidiEvent* noteOff = nullptr;
  if (!resolveNotePairForAction(liveStore, focus, action.targetNoteId, channel, action.pitch,
                                action.startTick, loopLength, noteOn, noteOff)) {
    return;
  }
  noteOn->data.noteData.note = action.pitch;
  noteOff->data.noteData.note = action.pitch;
  if (focus.active && focus.movingNoteId == action.targetNoteId) {
    noteEditFocusApplyPitch(focus, action.pitch, action.startTick, action.endTick, loopLength);
  }
}

NOTE_EDIT_MEM void syncFocusAfterApply(NoteEditFocus& focus, MidiEventVec& liveStore, uint8_t channel,
                         uint32_t loopLength) {
  if (!focus.active || focus.movingNoteId == kInvalidNoteId) {
    return;
  }
  syncNoteEditFocusLinearFromSessionStore(focus, liveStore, channel, loopLength);
}

}  // namespace

NOTE_EDIT_MEM void applyBoundarySplitForEditSession(MidiEventVec& liveStore, uint8_t channel) {
  for (MidiEvent& offEvt : liveStore) {
    if (!offEvt.isNoteOff() || offEvt.channel != channel) {
      continue;
    }
    const uint32_t offTick = offEvt.tick;
    if (offTick == 0) {
      continue;
    }

    bool anotherOnAtTick = false;
    for (const MidiEvent& onEvt : liveStore) {
      if (!onEvt.isNoteOn() || onEvt.channel != channel || onEvt.data.noteData.velocity == 0) {
        continue;
      }
      if (onEvt.tick != offTick) {
        continue;
      }
      anotherOnAtTick = true;
      break;
    }

    if (anotherOnAtTick) {
      offEvt.tick = offTick - 1;
    }
  }
}

NOTE_EDIT_MEM void ensureBaselineMapEntryForEditSessionAction(NoteEditFocus& focus,
                                                              const EditSessionAction& action) {
  if (!focus.active || action.targetNoteId == kInvalidNoteId ||
      action.targetNoteId == focus.movingNoteId) {
    return;
  }
  if (focus.baselineMap.find(action.targetNoteId) != focus.baselineMap.end()) {
    return;
  }
  focus.baselineMap[action.targetNoteId] = {action.pitch, action.velocity, action.startTick,
                                              action.endTick};
}

NOTE_EDIT_MEM void ensureBaselineMapBeforeShortenApply(NoteEditFocus& focus,
                                                       const EditSessionAction& action,
                                                       const MidiEventVec& liveStore,
                                                       uint8_t channel) {
  if (!focus.active || action.targetNoteId == kInvalidNoteId ||
      action.targetNoteId == focus.movingNoteId) {
    return;
  }
  const auto it = focus.baselineMap.find(action.targetNoteId);
  if (it != focus.baselineMap.end() && it->second.endTick > action.endTick) {
    return;
  }
  NoteBaseline liveFull{};
  NoteId resolvedId = action.targetNoteId;
  if (readLiveLinearSpan(liveStore, action.targetNoteId, channel, liveFull) ||
      readLiveLinearSpanForPitchStart(liveStore, channel, action.pitch, action.startTick,
                                      resolvedId, liveFull)) {
    const NoteId mapId =
        resolvedId != kInvalidNoteId ? resolvedId : action.targetNoteId;
    focus.baselineMap[mapId] = liveFull;
  }
}

NOTE_EDIT_MEM void applyEditSessionActions(const EditSessionActions& actions, MidiEventVec& liveStore,
                             NoteEditFocus& focus, uint8_t channel, uint32_t loopLength) {
  for (const EditSessionAction& action : actions) {
    switch (action.type) {
      case EditSessionActionType::RestoreNote:
        applyRestoreNote(action, liveStore, focus, channel, loopLength);
        break;
      case EditSessionActionType::ShortenNote:
        ensureBaselineMapBeforeShortenApply(focus, action, liveStore, channel);
        applyShortenNote(action, liveStore, focus, channel, loopLength);
        break;
      case EditSessionActionType::HideNote:
        ensureBaselineMapEntryForEditSessionAction(focus, action);
        applyHideNote(action, liveStore, focus, channel, loopLength);
        break;
      case EditSessionActionType::MoveNote:
        applyMoveNote(action, liveStore, focus, channel, loopLength);
        break;
      case EditSessionActionType::ChangeLength:
        applyChangeLength(action, liveStore, focus, channel, loopLength);
        break;
      case EditSessionActionType::ChangePitch:
        applyChangePitch(action, liveStore, focus, channel, loopLength);
        break;
    }
  }

  applyBoundarySplitForEditSession(liveStore, channel);
  syncFocusAfterApply(focus, liveStore, channel, loopLength);
}
