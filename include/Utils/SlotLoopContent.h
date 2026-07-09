#pragma once

/// Playable loop content: loaded in RAM or restorable from SD.
inline bool slotHasLoopContentInRamOrSd(bool hasDataInRam, bool hasPayloadOnSd) {
  return hasDataInRam || hasPayloadOnSd;
}
