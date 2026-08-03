#pragma once

#include <cstddef>
#include <cstdint>

#include "MidiEvent.h"
#include "Utils/IntervalProjection.h"

enum class PlaybackAdvanceResult { Completed, PlaybackOrderInvalid };

enum class PlaybackEmitPolicy { LayeredSlot, ActiveCommitted, ActiveCaptureOverdub };

struct PlaybackTickFrame {
  const ProjectionContext* playbackContext = nullptr;
  uint32_t tickInLoop = 0;
  uint32_t prevTickInLoop = UINT32_MAX;
  bool atLoopStart = false;
};

struct PlaybackCursorAdvanceState {
  uint16_t* cursor = nullptr;
  bool* playbackOrderDirty = nullptr;
};

using PlaybackSendFn = void (*)(void* ctx, const MidiEvent& evt, uint8_t slotIndex);
using PlaybackJamFilterFn = bool (*)(void* ctx, uint32_t storageTick);

struct PlaybackEventStream {
  const void* ctx = nullptr;
  size_t (*size)(const void* ctx) = nullptr;
  bool (*isCursorValid)(const void* ctx, uint16_t cursor) = nullptr;
  const MidiEvent& (*eventAt)(const void* ctx, uint16_t cursor);
  uint32_t (*eventPhase)(const MidiEvent& evt, const ProjectionContext& playbackContext);
};

/// Advance the committed playback cursor while emitting every event crossed by the playback
/// interval. Mutates only cursorAdvance.cursor and optional cursorAdvance.playbackOrderDirty.
/// Wrap / projection / order rebuild stay outside this function.
PlaybackAdvanceResult advancePlaybackCursor(PlaybackCursorAdvanceState cursorAdvance,
                                            const PlaybackTickFrame& frame,
                                            PlaybackEmitPolicy policy,
                                            const PlaybackEventStream& stream, PlaybackSendFn send,
                                            void* sendCtx, uint8_t playbackSlotIndex,
                                            PlaybackJamFilterFn jamFilter, void* jamCtx,
                                            uint8_t trackMidiChannelForDedup);
