//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

namespace RevisionCommitPolicy {

/// Revision commit WRITE streams StorageLoopIo pass shapes — not materialize.
constexpr bool kWritePathUsesLoopPassesMaterialize = false;

/// LoopSlot bodies stream from RAM via StorageLoopIo (not opaque current/ epoch copy).
constexpr bool kLoopSlotBodyUsesStorageLoopIoStream = true;

}  // namespace RevisionCommitPolicy
