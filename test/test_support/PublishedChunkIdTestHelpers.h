//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "LoopEventStore.h"

/// Detach capture store chunk ids into a published list (seal-path transfer).
inline bool transferCaptureStoreToPublished(LoopEventStore& store, PublishedChunkIdList& dest) {
  if (store.detachChunksToPublished(dest)) {
    return true;
  }
  CaptureChunkIdList captureIds;
  store.detachChunksTo(captureIds);
  return LoopEventStore::transferCaptureChunkIdsToPublished(dest, captureIds);
}
