//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "EditPass.h"
#include "Loop.h"
#include "LoopPasses.h"
#include "Utils/LoopMem.h"

#if defined(__IMXRT1062__)
#define LOOP_INTERNAL_MEM LOOP_COLD_MEM
#else
#define LOOP_INTERNAL_MEM
#endif

LOOP_INTERNAL_MEM bool eventsEquivalent(const MidiEvent& a, const MidiEvent& b);

LOOP_INTERNAL_MEM bool isDuplicateCaptureEvent(const Loop& loop, const MidiEvent& candidate);

LOOP_INTERNAL_MEM const char* capturePhaseLabel(CapturePhase phase);

LOOP_INTERNAL_MEM void sortCaptureStoreByTick(LoopEventStore& store);

LOOP_INTERNAL_MEM void markPreviewSpan(CapturePreview& preview, uint32_t startTick, uint32_t endTick,
                                        uint32_t ticksPerBar);

LOOP_INTERNAL_MEM void applyCaptureEventToPreview(CapturePreview& preview, const MidiEvent& evt,
                                                  uint32_t ticksPerBar, uint32_t loopLength);

LOOP_INTERNAL_MEM void rebuildCapturePreviewFromStore(Loop& loop);

LOOP_INTERNAL_MEM RecordPass deepCloneRecordPass(const RecordPass& pass);

LOOP_INTERNAL_MEM OverdubPass deepCloneOverdubPass(const OverdubPass& pass);

LOOP_INTERNAL_MEM LoopPasses deepClonePasses(const LoopPasses& passes);

LOOP_INTERNAL_MEM size_t estimatedEditPassBytes(const EditPass& row);

LOOP_INTERNAL_MEM bool canHeapAdmitEditPass(const EditPass& row);

LOOP_INTERNAL_MEM const char* sealOutcomeLabel(SealOutcome outcome);

LOOP_INTERNAL_MEM size_t stopPathChunkRefCount(const Loop& loop);

LOOP_INTERNAL_MEM size_t stopPathEventCount(const Loop& loop);

LOOP_INTERNAL_MEM uint32_t traceMicros();
