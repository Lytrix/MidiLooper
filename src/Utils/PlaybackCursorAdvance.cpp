#include "Utils/PlaybackCursorAdvance.h"

PlaybackAdvanceResult advancePlaybackCursor(PlaybackCursorAdvanceState cursorAdvance,
                                            const PlaybackTickFrame& frame,
                                            PlaybackEmitPolicy policy,
                                            const PlaybackEventStream& stream, PlaybackSendFn send,
                                            void* sendCtx, uint8_t playbackSlotIndex,
                                            PlaybackJamFilterFn jamFilter, void* jamCtx,
                                            uint8_t trackMidiChannelForDedup,
                                            PlaybackSendFn applyLedgerOnly) {
  if (cursorAdvance.cursor == nullptr || stream.eventAt == nullptr ||
      stream.eventPhase == nullptr || frame.playbackContext == nullptr ||
      stream.size == nullptr) {
    return PlaybackAdvanceResult::Completed;
  }

  const bool applyJam = (policy != PlaybackEmitPolicy::LayeredSlot) && jamFilter != nullptr;
  const bool suppressDuplicateNoteMessages = (policy == PlaybackEmitPolicy::ActiveCommitted);

  uint32_t lastSentEvTick = UINT32_MAX;
  uint8_t lastSentChannel = 0;
  uint8_t lastSentNote = 0;
  uint8_t lastSentType = 0xFF;

  uint16_t& cursor = *cursorAdvance.cursor;
  const size_t streamSize = stream.size(stream.ctx);
  while (static_cast<size_t>(cursor) < streamSize) {
    if (stream.isCursorValid != nullptr && !stream.isCursorValid(stream.ctx, cursor)) {
      if (cursorAdvance.playbackOrderDirty != nullptr) {
        *cursorAdvance.playbackOrderDirty = true;
      }
      return PlaybackAdvanceResult::PlaybackOrderInvalid;
    }

    const MidiEvent& evt = stream.eventAt(stream.ctx, cursor);
    const uint32_t evTick = stream.eventPhase(evt, *frame.playbackContext);
    const uint32_t evStorageTick = evt.tick;

    if (!IntervalProjection::didPlaybackEventCross(frame.atLoopStart, frame.prevTickInLoop, evTick,
                                                   frame.tickInLoop)) {
      if (evTick > frame.tickInLoop) {
        break;
      }
      cursor++;
      continue;
    }

    if (applyJam && !jamFilter(jamCtx, evStorageTick)) {
      cursor++;
      continue;
    }

    bool shouldSend = true;
    if (suppressDuplicateNoteMessages && (evt.isNoteOn() || evt.isNoteOff())) {
      const uint8_t effectiveCh =
          (evt.channel >= 1 && evt.channel <= 16) ? trackMidiChannelForDedup : evt.channel;
      const uint8_t note = evt.data.noteData.note;
      const bool isDuplicate = (evTick == lastSentEvTick && effectiveCh == lastSentChannel &&
                                note == lastSentNote && evt.type == lastSentType);
      shouldSend = !isDuplicate;
      if (shouldSend) {
        send(sendCtx, evt, playbackSlotIndex);
        lastSentEvTick = evTick;
        lastSentChannel = effectiveCh;
        lastSentNote = note;
        lastSentType = evt.type;
      } else if (applyLedgerOnly != nullptr) {
        applyLedgerOnly(sendCtx, evt, playbackSlotIndex);
      }
    } else if (shouldSend) {
      send(sendCtx, evt, playbackSlotIndex);
    }

    cursor++;
  }

  return PlaybackAdvanceResult::Completed;
}

PlaybackAdvanceResult advancePlaybackCursorAcrossOverdubWrap(
    PlaybackCursorAdvanceState cursorAdvance, const PlaybackTickFrame& wrapFrame,
    PlaybackEmitPolicy policy, const PlaybackEventStream& stream, PlaybackSendFn send,
    void* sendCtx, uint8_t playbackSlotIndex, PlaybackJamFilterFn jamFilter, void* jamCtx,
    uint8_t trackMidiChannelForDedup, PlaybackSendFn applyLedgerOnly) {
  if (wrapFrame.playbackContext == nullptr) {
    return PlaybackAdvanceResult::Completed;
  }
  const uint32_t loopLength = wrapFrame.playbackContext->loopLength;
  if (loopLength > 1 && wrapFrame.prevTickInLoop != UINT32_MAX &&
      wrapFrame.prevTickInLoop < loopLength - 1) {
    PlaybackTickFrame tailFrame = wrapFrame;
    tailFrame.atLoopStart = false;
    tailFrame.tickInLoop = loopLength - 1;
    uint16_t tailCursor = 0;
    PlaybackCursorAdvanceState tailAdvance{&tailCursor, cursorAdvance.playbackOrderDirty};
    const PlaybackAdvanceResult tailResult = advancePlaybackCursor(
        tailAdvance, tailFrame, policy, stream, send, sendCtx, playbackSlotIndex, jamFilter, jamCtx,
        trackMidiChannelForDedup, applyLedgerOnly);
    if (tailResult != PlaybackAdvanceResult::Completed) {
      return tailResult;
    }
  }
  if (cursorAdvance.cursor != nullptr) {
    *cursorAdvance.cursor = 0;
  }
  return advancePlaybackCursor(cursorAdvance, wrapFrame, policy, stream, send, sendCtx,
                               playbackSlotIndex, jamFilter, jamCtx, trackMidiChannelForDedup,
                               applyLedgerOnly);
}
